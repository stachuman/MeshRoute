// MeshRouteCoreTests — InboxCoreTests.swift
// The inbox catch-up: ingest dedups against the live path, and the mock serves a real pull_inbox.

import XCTest
@testable import MeshRouteCore
import MeshRouteWire

private func drainInbox(_ link: NodeLink, until count: Int) async -> [Inbound] {
    var out: [Inbound] = []
    for await ev in link.events {
        if case .line(let l) = ev, let inb = PushDecoder.decode(line: l) {
            out.append(inb)
            if out.count >= count { break }
        }
    }
    return out
}

final class InboxCoreTests: XCTestCase {
    let now = Date(timeIntervalSince1970: 2_000_000)

    func testInboxIngestDedupsAgainstLivePush() {
        var store = ConversationStore()
        store.ingest(.messageReceived(origin: 2, ctr: 7, senderHash: nil, seq: nil, body: "hi"), now: now)  // live first (legacy DM)
        // the same message later arrives via pull_inbox → must NOT duplicate
        let dup = store.ingestInbox(InboxEntry(seq: 5, kind: .dm, origin: 2, channelID: 0,
                                               ctr: 7, rxTimeMs: 1, body: "hi"), now: now)
        XCTAssertNil(dup)
        XCTAssertEqual(store.messages(in: .dm(KeyHash(2))).count, 1)
        // a different ctr is a genuinely new record, and carries its seq
        let fresh = store.ingestInbox(InboxEntry(seq: 6, kind: .dm, origin: 2, channelID: 0,
                                                 ctr: 8, rxTimeMs: 2, body: "new"), now: now)
        XCTAssertEqual(fresh?.seq, 6)
        XCTAssertEqual(store.messages(in: .dm(KeyHash(2))).count, 2)
    }

    func testInboxChannelDedupByMsgID() {
        var store = ConversationStore()
        let a = store.ingestInbox(InboxEntry(seq: 1, kind: .channel, origin: 4, channelID: 3,
                                             ctr: 1, channelMsgID: 0x0400_0001, rxTimeMs: 1, body: "gm"), now: now)
        let b = store.ingestInbox(InboxEntry(seq: 1, kind: .channel, origin: 4, channelID: 3,
                                             ctr: 1, channelMsgID: 0x0400_0001, rxTimeMs: 1, body: "gm"), now: now)
        XCTAssertNotNil(a); XCTAssertNil(b)               // same channel_msg_id → deduped
        XCTAssertEqual(store.messages(in: .channel(3)).count, 1)
    }

    func testMockPullStreamsRecordsThenEnd() async throws {
        let mock = MockNodeLink()                          // seeded: 2 DM + 1 channel record
        await mock.connect()
        try await mock.send(line: "pull_inbox 0 0")
        let inbound = await drainInbox(mock, until: 5)     // ready, inbox_dm, inbox_dm, inbox_channel, inbox_end
        let entries = inbound.compactMap { inb -> InboxEntry? in
            if case .inboxEntry(let e) = inb { return e } else { return nil }
        }
        XCTAssertEqual(entries.count, 3)
        XCTAssertEqual(entries.filter { $0.kind == .dm }.count, 2)
        XCTAssertEqual(entries.filter { $0.kind == .channel }.count, 1)
        guard case .inboxEnd(let dmSeq, let chanSeq, _, let count, let nowMs)? = inbound.last else { return XCTFail("no inbox_end") }
        XCTAssertEqual(count, 3)
        XCTAssertEqual(dmSeq, 2)        // dm newest seq
        XCTAssertEqual(chanSeq, 1)      // chan newest seq (independent space)
        XCTAssertNotNil(nowMs)          // the mock serves the uptime anchor like current firmware
    }

    // ---- rx_ms → wall-clock anchoring (NodeTimeAnchor) ----

    func testAnchorConvertsUptimeToWallClock() {
        let connectedAt = Date(timeIntervalSince1970: 1_750_000_000)
        let anchor = NodeTimeAnchor(nodeNowMs: 100_000, capturedAt: connectedAt)   // node up 100 s
        // received at uptime 40 s → 60 s before the anchor moment
        XCTAssertEqual(anchor.wallClock(rxMs: 40_000), connectedAt.addingTimeInterval(-60))
        // received "now" → exactly the anchor moment
        XCTAssertEqual(anchor.wallClock(rxMs: 100_000), connectedAt)
    }

    func testAnchorClampsFutureRxMs() {
        let connectedAt = Date(timeIntervalSince1970: 1_750_000_000)
        let anchor = NodeTimeAnchor(nodeNowMs: 100_000, capturedAt: connectedAt)
        XCTAssertEqual(anchor.wallClock(rxMs: 100_001), connectedAt)   // never invents a future time
    }

    func testMockPullRespectsCursors() async throws {
        let mock = MockNodeLink()
        await mock.connect()
        try await mock.send(line: "pull_inbox 1 1")        // dm seq>1 → 1 record; chan seq>1 → none
        let inbound = await drainInbox(mock, until: 3)     // ready, inbox_dm(seq2), inbox_end
        let entries = inbound.compactMap { inb -> InboxEntry? in
            if case .inboxEntry(let e) = inb { return e } else { return nil }
        }
        XCTAssertEqual(entries.count, 1)
        XCTAssertEqual(entries.first?.seq, 2)
    }

    // ---- channel identity is the full 32-bit channel_msg_id (exact; the 8-bit ctr would alias) ----

    func testChannelIdentityIsFullMsgID() {
        var store = ConversationStore()
        // two distinct messages whose low-8 ctr aliases (…01) but whose full ids differ → both kept
        let a = store.ingestInbox(InboxEntry(seq: 1, kind: .channel, origin: 4, channelID: 3,
                                             ctr: 1, channelMsgID: 0x0400_0001, rxTimeMs: 1, body: "first"), now: now)
        let b = store.ingestInbox(InboxEntry(seq: 2, kind: .channel, origin: 4, channelID: 3,
                                             ctr: 1, channelMsgID: 0x0401_0001, rxTimeMs: 2, body: "second"), now: now)
        // an exact re-pull of the first (same full id) → deduped
        let c = store.ingestInbox(InboxEntry(seq: 1, kind: .channel, origin: 4, channelID: 3,
                                             ctr: 1, channelMsgID: 0x0400_0001, rxTimeMs: 1, body: "first"), now: now)
        XCTAssertNotNil(a); XCTAssertNotNil(b); XCTAssertNil(c)
        XCTAssertEqual(store.messages(in: .channel(3)).count, 2)
    }

    func testLiveChannelThenInboxPullDedups() {
        var store = ConversationStore()
        store.ingest(.channelReceived(origin: 4, channelID: 3, channelMsgID: 0x0400_0009, seq: nil, body: "gm"), now: now)
        // the same channel message later arrives via pull_inbox (same full id) → must NOT duplicate
        let dup = store.ingestInbox(InboxEntry(seq: 5, kind: .channel, origin: 4, channelID: 3,
                                               ctr: 9, channelMsgID: 0x0400_0009, rxTimeMs: 1, body: "gm"), now: now)
        XCTAssertNil(dup)
        XCTAssertEqual(store.messages(in: .channel(3)).count, 1)
    }

    // ---- the headline requirement: an epoch reset must not silently miss the node's restarted seqs ----

    func testEpochResetRepullMissesNothingAndDoesNotDuplicate() {
        var store = ConversationStore()
        var sync = InboxSyncState()

        // epoch 1: first sync from 0, pull two DM records.
        XCTAssertEqual(sync.beginSync(nodeEpoch: 1).dmSince, 0)
        for e in [InboxEntry(seq: 1, kind: .dm, origin: 2, channelID: 0, ctr: 1, rxTimeMs: 1, body: "a"),
                  InboxEntry(seq: 2, kind: .dm, origin: 2, channelID: 0, ctr: 2, rxTimeMs: 2, body: "b")] {
            store.ingestInbox(e, now: now); sync.advance(with: e)
        }
        XCTAssertEqual(sync.dmCursor, 2)
        XCTAssertEqual(store.messages(in: .dm(KeyHash(2))).count, 2)

        // node flash wiped → epoch 2, seq restarts at 1 with a NEW message. A naive "seq > 2" would MISS it.
        XCTAssertEqual(sync.beginSync(nodeEpoch: 2).dmSince, 0)   // epoch change → re-pull from 0
        XCTAssertEqual(sync.epoch, 2)
        let post = InboxEntry(seq: 1, kind: .dm, origin: 2, channelID: 0, ctr: 9, rxTimeMs: 5, body: "after-reset")
        XCTAssertNotNil(store.ingestInbox(post, now: now))       // NOT missed
        sync.advance(with: post)
        XCTAssertEqual(store.messages(in: .dm(KeyHash(2))).count, 3)

        // a re-pull re-streaming a pre-wipe message (same content) merges, not duplicates.
        let replay = store.ingestInbox(InboxEntry(seq: 1, kind: .dm, origin: 2, channelID: 0,
                                                  ctr: 1, rxTimeMs: 1, body: "a"), now: now)
        XCTAssertNil(replay)
        XCTAssertEqual(store.messages(in: .dm(KeyHash(2))).count, 3)
    }

    func testLiveDMCarriesSeqAndDroppedPushCreatesGap() async throws {
        let mock = MockNodeLink(seedInbox: false)          // empty stores → predictable seqs from 1
        await mock.connect()
        await mock.simulateIncomingDM(fromID: 2, body: "one")          // seq 1 → pushed
        await mock.simulateDroppedIncomingDM(fromID: 2, body: "two")   // seq 2 → recorded, NOT pushed (dropped)
        await mock.simulateIncomingDM(fromID: 2, body: "three")        // seq 3 → pushed
        let inbound = await drainInbox(mock, until: 3)     // ready, msg_recv(seq1), msg_recv(seq3)
        let seqs = inbound.compactMap { inb -> UInt32? in
            if case .messageReceived(_, _, _, let s, _) = inb { return s } else { return nil }
        }
        XCTAssertEqual(seqs, [1, 3])   // seq 2 missing → seq 3 jumps past high+1 → the app's gap detector fires
    }

    func testMockExposesEpochAndResetBumpsIt() async throws {
        let mock = MockNodeLink()
        await mock.connect()                 // ready (epoch 1)
        await mock.simulateStoreReset()      // wipe → epoch 2
        await mock.disconnect()
        await mock.connect()                 // ready (epoch 2)
        let lines = await drainInbox(mock, until: 2)   // ready1, ready2 (states are not lines)
        guard case .ready(let r1) = lines[0], case .ready(let r2) = lines[1] else { return XCTFail() }
        XCTAssertEqual(r1.inboxEpoch, 1)
        XCTAssertEqual(r2.inboxEpoch, 2)
    }
}
