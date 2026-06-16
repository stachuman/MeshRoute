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
        store.ingest(.channelReceived(origin: 4, channelID: 3, channelMsgID: 0x0400_0001, seq: nil, layerID: nil, body: "gm"), now: now)
        XCTAssertEqual(store.messages(in: .channel(3)).count, 1)
        XCTAssertEqual(store.messages(in: .channel(3)).first?.origin, 4)
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
