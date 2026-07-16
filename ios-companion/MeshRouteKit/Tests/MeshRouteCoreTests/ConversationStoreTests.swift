// MeshRouteCoreTests — ConversationStoreTests.swift
// The dedup + delivery-state logic the design calls for (merge inbound by (origin,ctr)).

import XCTest
@testable import MeshRouteCore
import MeshRouteWire

final class ConversationStoreTests: XCTestCase {
    let now = Date(timeIntervalSince1970: 1_000_000)

    func testInboundDMDedupByOriginCtr() {
        var store = ConversationStore()
        XCTAssertNotNil(store.ingest(.messageReceived(origin: 2, ctr: 5, senderHash: nil, seq: nil, layerID: nil, crypted: nil, body: "hi"), now: now))
        XCTAssertNil(store.ingest(.messageReceived(origin: 2, ctr: 5, senderHash: nil, seq: nil, layerID: nil, crypted: nil, body: "hi"), now: now)) // duplicate
        XCTAssertEqual(store.messages(in: .dm(KeyHash(2))).count, 1)
        // A different ctr from the same origin is a new message.
        XCTAssertNotNil(store.ingest(.messageReceived(origin: 2, ctr: 6, senderHash: nil, seq: nil, layerID: nil, crypted: nil, body: "again"), now: now))
        XCTAssertEqual(store.messages(in: .dm(KeyHash(2))).count, 2)
    }

    func testOutgoingFlipsToAckedOnSendAcked() {
        var store = ConversationStore()
        let out = store.appendOutgoing(thread: .dm(KeyHash(2)), body: "yo", now: now)
        XCTAssertEqual(store.messages(in: .dm(KeyHash(2))).first?.state, .sending)
        store.attach(ctr: 7, toMessage: out.id, state: .queued)
        XCTAssertEqual(store.messages(in: .dm(KeyHash(2))).first?.state, .queued)
        store.ingest(.sendAcked(dst: 2, ctr: 7), now: now)
        XCTAssertEqual(store.messages(in: .dm(KeyHash(2))).first?.state, .acked)
    }

    func testOutgoingFlipsToFailedOnSendFailed() {
        var store = ConversationStore()
        let out = store.appendOutgoing(thread: .dm(KeyHash(2)), body: "yo", now: now)
        store.attach(ctr: 9, toMessage: out.id)
        store.ingest(.sendFailed(dst: 2, ctr: 9, reason: nil), now: now)
        XCTAssertEqual(store.messages(in: .dm(KeyHash(2))).first?.state, .failed)
    }

    func testChannelIngest() {
        var store = ConversationStore()
        store.ingest(.channelReceived(origin: 4, channelID: 3, channelMsgID: 0x0400_0001, seq: nil, layerID: nil, teamID: nil, body: "gm"), now: now)
        XCTAssertEqual(store.messages(in: .channel(3)).count, 1)
        XCTAssertEqual(store.messages(in: .channel(3)).first?.origin, 4)
    }

    func testTeamChannelThreadsApartFromLeafChannel() {   // D30: same channel number, SEPARATE conversations
        var store = ConversationStore()
        store.ingest(.channelReceived(origin: 4, channelID: 3, channelMsgID: 0x0400_0001, seq: nil, layerID: nil, teamID: nil, body: "gm leaf"), now: now)
        store.ingest(.channelReceived(origin: 5, channelID: 3, channelMsgID: 0x0500_0001, seq: nil, layerID: nil, teamID: "cccc0001", body: "gm team"), now: now)
        XCTAssertEqual(store.messages(in: .channel(3)).map(\.body), ["gm leaf"])
        XCTAssertEqual(store.messages(in: .teamChannel(team: "cccc0001", channel: 3)).map(\.body), ["gm team"])
        // the durable pull path threads identically (a team inbox record → the team thread)
        store.ingestInbox(InboxEntry(seq: 9, kind: .channel, origin: 5, channelID: 3, ctr: 2,
                                     channelMsgID: 0x0500_0002, teamID: "cccc0001", rxTimeMs: 1000, body: "pulled team"), now: now)
        XCTAssertEqual(store.messages(in: .teamChannel(team: "cccc0001", channel: 3)).map(\.body), ["gm team", "pulled team"])
        XCTAssertEqual(store.messages(in: .channel(3)).count, 1)   // the leaf thread is untouched
    }

    func testRekeyDMFromIDToHash() {
        var store = ConversationStore()
        store.ingest(.messageReceived(origin: 2, ctr: 1, senderHash: nil, seq: nil, layerID: nil, crypted: nil, body: "hi"), now: now)   // legacy DM staged under dm(id)
        let realHash = KeyHash(0x8a3f1c02)
        store.rekeyDM(fromID: 2, toHash: realHash)
        XCTAssertTrue(store.messages(in: .dm(KeyHash(2))).isEmpty)
        XCTAssertEqual(store.messages(in: .dm(realHash)).count, 1)
        XCTAssertEqual(store.messages(in: .dm(realHash)).first?.body, "hi")
    }

    func testDMKeyedAndDedupedBySenderHash() {
        var store = ConversationStore()
        let h: UInt32 = 0x8a3f1c02
        // a DM with sender_hash keys STRAIGHT to .dm(hash) — no pseudo-id staging, no resolve needed
        store.ingest(.messageReceived(origin: 2, ctr: 7, senderHash: h, seq: nil, layerID: nil, crypted: nil, body: "hi"), now: now)
        XCTAssertEqual(store.messages(in: .dm(KeyHash(h))).count, 1)
        XCTAssertTrue(store.messages(in: .dm(KeyHash(2))).isEmpty)
        // same (sender_hash, ctr) but a REASSIGNED origin → still the same message → deduped
        XCTAssertNil(store.ingest(.messageReceived(origin: 9, ctr: 7, senderHash: h, seq: nil, layerID: nil, crypted: nil, body: "hi"), now: now))
        XCTAssertEqual(store.messages(in: .dm(KeyHash(h))).count, 1)
    }

    func testContactBookBindAndReverseLookup() {
        var book = ContactBook([Contact(hash: KeyHash(0x8a3f1c02), name: "Bench-2")])
        book.bind(hash: KeyHash(0x8a3f1c02), toID: 2)
        XCTAssertEqual(book.contact(forID: 2)?.name, "Bench-2")
        XCTAssertEqual(book.contact(for: KeyHash(0x8a3f1c02))?.lastKnownID, 2)
    }
}
