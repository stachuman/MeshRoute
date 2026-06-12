// MeshRouteWireTests — InboxWireTests.swift
// Pin the PROPOSED inbox-sync wire contract (commands + inbox_dm/inbox_channel/inbox_end pushes).

import XCTest
@testable import MeshRouteWire

final class InboxWireTests: XCTestCase {

    func testInboxCommands() {
        XCTAssertEqual(Command.pullInbox(dmSince: 42, chanSince: 7).line, "pull_inbox 42 7")
        XCTAssertEqual(Command.pullInbox(dmSince: 0, chanSince: 0).line, "pull_inbox 0 0")
        XCTAssertEqual(Command.markRead(kind: .dm, seq: 42).line, "mark_read dm 42")
        XCTAssertEqual(Command.markRead(kind: .channel, seq: 7).line, "mark_read chan 7")
    }

    func testInboxEntryDM() {
        guard case .inboxEntry(let e)? = PushDecoder.decode(
            line: #"{"ev":"inbox_dm","seq":42,"origin":2,"ctr":7,"sender_hash":2319391746,"rx_ms":123456,"body":"hi"}"#) else {
            return XCTFail("not inbox_dm")
        }
        XCTAssertEqual(e.kind, .dm)
        XCTAssertEqual(e.seq, 42)
        XCTAssertEqual(e.origin, 2)
        XCTAssertEqual(e.channelID, 0)
        XCTAssertEqual(e.ctr, 7)
        XCTAssertEqual(e.senderHash, 2319391746)   // 0x8a3f1c02
        XCTAssertEqual(e.rxTimeMs, 123456)
        XCTAssertEqual(e.body, "hi")
    }

    func testInboxEntryChannel() {
        // channel identity is the full 32-bit channel_msg_id (no ctr field); 68298753 = 0x04020301.
        guard case .inboxEntry(let e)? = PushDecoder.decode(
            line: #"{"ev":"inbox_channel","seq":7,"origin":4,"channel_id":3,"channel_msg_id":68298753,"rx_ms":999,"body":"gm"}"#) else {
            return XCTFail("not inbox_channel")
        }
        XCTAssertEqual(e.kind, .channel)
        XCTAssertEqual(e.channelID, 3)
        XCTAssertEqual(e.channelMsgID, 68298753)
        XCTAssertEqual(e.origin, 4)
        XCTAssertEqual(e.seq, 7)
    }

    func testInboxEnd() {
        guard case .inboxEnd(let dm, let chan, let epoch, let count)? = PushDecoder.decode(
            line: #"{"ev":"inbox_end","dm_seq":42,"chan_seq":7,"epoch":3,"count":15}"#) else {
            return XCTFail("not inbox_end")
        }
        XCTAssertEqual(dm, 42); XCTAssertEqual(chan, 7); XCTAssertEqual(epoch, 3); XCTAssertEqual(count, 15)
    }

    func testSyncStateAdvanceTakesMaxPerStore() {
        var s = InboxSyncState(epoch: 1)
        s.advance(with: InboxEntry(seq: 5, kind: .dm, origin: 2, channelID: 0, ctr: 1, rxTimeMs: 0, body: ""))
        s.advance(with: InboxEntry(seq: 3, kind: .channel, origin: 2, channelID: 1, ctr: 1, rxTimeMs: 0, body: ""))
        s.advance(with: InboxEntry(seq: 2, kind: .dm, origin: 2, channelID: 0, ctr: 2, rxTimeMs: 0, body: "")) // older
        XCTAssertEqual(s.dmCursor, 5)     // never regresses
        XCTAssertEqual(s.chanCursor, 3)   // independent space
    }

    func testBeginSyncIncrementalWhenEpochUnchanged() {
        var s = InboxSyncState(epoch: 1, dmCursor: 500, chanCursor: 200)
        let r = s.beginSync(nodeEpoch: 1)
        XCTAssertEqual(r.dmSince, 500); XCTAssertEqual(r.chanSince, 200)   // keep cursors → incremental pull
        XCTAssertEqual(s.epoch, 1)
    }

    func testBeginSyncResetsOnEpochChange() {
        var s = InboxSyncState(epoch: 1, dmCursor: 500, chanCursor: 200)
        let r = s.beginSync(nodeEpoch: 2)                                  // node store wiped
        XCTAssertEqual(r.dmSince, 0); XCTAssertEqual(r.chanSince, 0)       // full re-pull (else seq 1,2,3 < 500 are MISSED)
        XCTAssertEqual(s.epoch, 2)
        XCTAssertEqual(s.dmCursor, 0); XCTAssertEqual(s.chanCursor, 0)
    }

    func testFirstSyncPullsFromZero() {
        var s = InboxSyncState()                                          // epoch 0, never synced
        let r = s.beginSync(nodeEpoch: 5)
        XCTAssertEqual(r.dmSince, 0); XCTAssertEqual(r.chanSince, 0)
        XCTAssertEqual(s.epoch, 5)
    }

    func testReadyCarriesInboxEpochOrNil() {
        guard case .ready(let r)? = PushDecoder.decode(
            line: #"{"ev":"ready","id":1,"key":"8a3f1c02","leaf_id":0,"mode":"node","gateway":false,"routing_sf":7,"inbox_epoch":3}"#) else {
            return XCTFail()
        }
        XCTAssertEqual(r.inboxEpoch, 3)
        // a node without a durable inbox omits the field → nil (no sync)
        guard case .ready(let r2)? = PushDecoder.decode(
            line: #"{"ev":"ready","id":1,"key":"8a3f1c02","leaf_id":0,"mode":"node","gateway":false,"routing_sf":7}"#) else {
            return XCTFail()
        }
        XCTAssertNil(r2.inboxEpoch)
    }

    // ---- model "B": live-push seq vs the per-store high-water (gap detection) ----

    func testClassifyLiveGapDetection() {
        let s = InboxSyncState(epoch: 1, dmCursor: 41, chanCursor: 6)
        XCTAssertEqual(s.classifyLive(kind: .dm, seq: 42), .contiguous)    // high+1
        XCTAssertEqual(s.classifyLive(kind: .channel, seq: 7), .contiguous)
        XCTAssertEqual(s.classifyLive(kind: .dm, seq: 45), .gap)           // > high+1 → a live push was dropped
        XCTAssertEqual(s.classifyLive(kind: .dm, seq: 41), .duplicate)     // == high → already held
        XCTAssertEqual(s.classifyLive(kind: .dm, seq: 10), .duplicate)     // < high → already held (epoch re-pull etc.)
    }

    func testAdvanceKindSeqTakesMax() {
        var s = InboxSyncState(epoch: 1, dmCursor: 10)
        s.advance(kind: .dm, seq: 12); XCTAssertEqual(s.dmCursor, 12)
        s.advance(kind: .dm, seq: 11); XCTAssertEqual(s.dmCursor, 12)      // older → never regresses
        s.advance(kind: .channel, seq: 3); XCTAssertEqual(s.chanCursor, 3)
    }
}
