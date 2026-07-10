// MeshRouteWireTests — PushDecoderTests.swift
// Pin the JSON push decode against console_json.cpp's exact line shapes.

import XCTest
@testable import MeshRouteWire

final class PushDecoderTests: XCTestCase {

    func testAck() {
        // legacy ack (no send-handle fields) → dstHash/layerPath nil
        guard case .ack(let a)? = PushDecoder.decode(line: #"{"ack":"queued","ctr":5,"qd":0}"#) else {
            return XCTFail("not an ack")
        }
        XCTAssertEqual(a.code, .queued)
        XCTAssertEqual(a.ctr, 5)
        XCTAssertEqual(a.queueDepth, 0)
        XCTAssertNil(a.dstHash); XCTAssertNil(a.layerPath)
    }

    func testAckSendHandle() {       // D19 send-handle: dh = hash-addressed dst, lp = packed cross-layer path
        guard case .ack(let a)? = PushDecoder.decode(
            line: #"{"ack":"queued","ctr":7,"qd":0,"dh":2319391746,"lp":515}"#) else {
            return XCTFail("not an ack")
        }
        XCTAssertEqual(a.ctr, 7)
        XCTAssertEqual(a.dstHash, 2319391746)   // 0x8a3f1c02
        XCTAssertEqual(a.layerPath, 515)        // 0x0203 = (2<<8)|3 = hops [2,3], MSB-first
        // dh:0 / lp:0 (a plain `send <id>` / same-layer) → both nil
        guard case .ack(let b)? = PushDecoder.decode(line: #"{"ack":"queued","ctr":8,"qd":0,"dh":0,"lp":0}"#) else {
            return XCTFail("not an ack")
        }
        XCTAssertNil(b.dstHash); XCTAssertNil(b.layerPath)
    }

    func testAckError() {
        guard case .ack(let a)? = PushDecoder.decode(line: #"{"ack":"err_unknown_dst","ctr":0,"qd":1}"#) else {
            return XCTFail("not an ack")
        }
        XCTAssertEqual(a.code, .errUnknownDst)
        XCTAssertTrue(a.code.isError)
    }

    func testAckUnknownCodeIsForwardCompatible() {
        guard case .ack(let a)? = PushDecoder.decode(line: #"{"ack":"err_unknown","ctr":0,"qd":0}"#) else {
            return XCTFail("not an ack")
        }
        XCTAssertEqual(a.code, .unknown)   // cmdcode_name's fallback string maps to .unknown, not a crash
    }

    func testMsgRecv() {
        // companion firmware: msg_recv carries sender_hash AND the live seq (model B gap detector)
        guard case .messageReceived(let origin, let ctr, let senderHash, let seq, let layer, let crypted, let body)? =
                PushDecoder.decode(line: #"{"ev":"msg_recv","origin":2,"layer_id":5,"ctr":7,"sender_hash":2319391746,"seq":42,"enc":true,"body":"hi there"}"#) else {
            return XCTFail("not msg_recv")
        }
        XCTAssertEqual(origin, 2); XCTAssertEqual(ctr, 7); XCTAssertEqual(body, "hi there")
        XCTAssertEqual(senderHash, 2319391746)   // 0x8a3f1c02 — the sender's stable key_hash32
        XCTAssertEqual(seq, 42)
        XCTAssertEqual(layer, 5)                 // D12: receiving layer
        XCTAssertEqual(crypted, true)            // E2E: the wire `enc` (CRYPTED) flag
        // legacy DM without the optional fields → all nil
        guard case .messageReceived(_, _, let sh2, let seq2, let layer2, let cr2, _)? =
                PushDecoder.decode(line: #"{"ev":"msg_recv","origin":2,"ctr":7,"body":"hi"}"#) else { return XCTFail() }
        XCTAssertNil(sh2); XCTAssertNil(seq2); XCTAssertNil(layer2); XCTAssertNil(cr2)
    }

    func testChannelRecv() {
        // without channel_msg_id/seq (pre-companion firmware) → both nil, still decodes
        guard case .channelReceived(let origin, let ch, let mid, let seq, _, let body)? =
                PushDecoder.decode(line: #"{"ev":"channel_recv","origin":4,"channel_id":3,"body":"gm"}"#) else {
            return XCTFail("not channel_recv")
        }
        XCTAssertEqual(origin, 4); XCTAssertEqual(ch, 3); XCTAssertNil(mid); XCTAssertNil(seq); XCTAssertEqual(body, "gm")
        // with channel_msg_id + seq + layer_id (companion firmware) → full identity + live high-water + layer
        guard case .channelReceived(_, _, let mid2, let seq2, let layer2, _)? =
                PushDecoder.decode(line: #"{"ev":"channel_recv","origin":4,"layer_id":9,"channel_id":3,"channel_msg_id":68298753,"seq":7,"body":"gm"}"#) else {
            return XCTFail("not channel_recv")
        }
        XCTAssertEqual(mid2, 68298753); XCTAssertEqual(seq2, 7); XCTAssertEqual(layer2, 9)
    }

    func testSendAckedAndFailed() {
        guard case .sendAcked(let dst, let ctr)? =
                PushDecoder.decode(line: #"{"ev":"send_acked","dst":2,"ctr":7}"#) else {
            return XCTFail("not send_acked")
        }
        XCTAssertEqual(dst, 2); XCTAssertEqual(ctr, 7)
        guard case .sendFailed(let d2, let c2, let r2)? =
                PushDecoder.decode(line: #"{"ev":"send_failed","dst":9,"ctr":3}"#) else {
            return XCTFail("not send_failed")
        }
        XCTAssertEqual(d2, 9); XCTAssertEqual(c2, 3); XCTAssertNil(r2)
        // E2E: a CRYPTED send dropped for no peer key carries a reason
        guard case .sendFailed(_, _, let r3)? =
                PushDecoder.decode(line: #"{"ev":"send_failed","dst":2,"ctr":7,"reason":"no_pubkey"}"#) else {
            return XCTFail("not send_failed")
        }
        XCTAssertEqual(r3, "no_pubkey")
    }

    func testE2eAckedLiveReceipt() {
        // D25: the live E2E delivery-receipt twin → marks the OUTBOX delivered (NOT an inbound DM). origin = the dst that confirmed.
        guard case .e2eAcked(let dst, let ctr, let senderHash)? =
                PushDecoder.decode(line: #"{"ev":"e2e_acked","origin":2,"ctr":7,"sender_hash":2319391746}"#) else {
            return XCTFail("not e2e_acked")
        }
        XCTAssertEqual(dst, 2); XCTAssertEqual(ctr, 7)
        XCTAssertEqual(senderHash, 2319391746)        // a cross-layer ack carries the stable key
        // a same-layer ack sends sender_hash 0 → nil (the app then matches by (dst, ctr))
        guard case .e2eAcked(_, _, let sh0)? =
                PushDecoder.decode(line: #"{"ev":"e2e_acked","origin":2,"ctr":8,"sender_hash":0}"#) else {
            return XCTFail("not e2e_acked")
        }
        XCTAssertNil(sh0)
    }

    func testAntiSpamPacingEvents() {     // D29
        guard case .sendBlocked(let kind, let reason, let nextMs)? = PushDecoder.decode(
            line: #"{"ev":"send_blocked","kind":"channel","reason":"min_interval","next_ms":7300}"#) else {
            return XCTFail("not send_blocked")
        }
        XCTAssertEqual(kind, "channel"); XCTAssertEqual(reason, "min_interval"); XCTAssertEqual(nextMs, 7300)
        guard case .channelSent(let ctr, let relayed, let r)? = PushDecoder.decode(
            line: #"{"ev":"channel_sent","ctr":6,"relayed":false,"reason":"no_relay"}"#) else {
            return XCTFail("not channel_sent")
        }
        XCTAssertEqual(ctr, 6); XCTAssertFalse(relayed); XCTAssertEqual(r, "no_relay")
        guard case .limits(let l)? = PushDecoder.decode(
            line: #"{"ev":"limits","win_ms":300000,"win_left_ms":142000,"n":40,"ch_sf":7,"ch_cap":8,"ch_used":2,"ch_min_ms":10000,"ch_next_ms":0,"ch_ceiling":42,"dm_min_ms":3000,"dm_next_ms":1200,"duty_ms":3000,"duty_used_ms":640}"#) else {
            return XCTFail("not limits")
        }
        XCTAssertEqual(l.chCap, 8); XCTAssertEqual(l.dmNextMs, 1200); XCTAssertEqual(l.dutyMs, 3000)
        // the new DM-giveup reasons ride the existing send_failed
        guard case .sendFailed(_, _, let fr)? = PushDecoder.decode(line: #"{"ev":"send_failed","dst":2,"ctr":7,"reason":"no_cts"}"#) else { return XCTFail() }
        XCTAssertEqual(fr, "no_cts")
    }

    func testPeerKeyProvisioningEvents() {     // E2E peer-key provisioning (2026-06-16)
        XCTAssertEqual(Command.peerKey(pubkeyHex: String(repeating: "ab", count: 32)).line,
                       "peerkey " + String(repeating: "ab", count: 32))
        XCTAssertEqual(Command.reqPubkey(KeyHash(0xff60_9d5c)).line, "reqpubkey ff609d5c")
        guard case .peerKeySet(let h, let pinned)? =
                PushDecoder.decode(line: #"{"ev":"peerkey_set","hash":3735928559,"pinned":true}"#) else {
            return XCTFail("not peerkey_set")
        }
        XCTAssertEqual(h, KeyHash(0xDEAD_BEEF)); XCTAssertTrue(pinned)
        guard case .peerKeyError(let reason)? =
                PushDecoder.decode(line: #"{"ev":"peerkey_err","reason":"hash_mismatch"}"#) else {
            return XCTFail("not peerkey_err")
        }
        XCTAssertEqual(reason, "hash_mismatch")
        guard case .peerKeyCached(let hc, _)? =
                PushDecoder.decode(line: #"{"ev":"peer_key_cached","hash":3735928559,"pinned":false}"#) else {
            return XCTFail("not peer_key_cached")
        }
        XCTAssertEqual(hc, KeyHash(0xDEAD_BEEF))
    }

    func testHashResolvedDecimalHash() {
        // console_json emits the hash as a DECIMAL u32 (j.u32). 0x8a3f1c02 == 2319391746.
        guard case .hashResolved(let node, let auth, let hash)? =
                PushDecoder.decode(line: #"{"ev":"hash_resolved","node":2,"auth":1,"hash":2319391746}"#) else {
            return XCTFail("not hash_resolved")
        }
        XCTAssertEqual(node, 2)
        XCTAssertTrue(auth)
        XCTAssertEqual(hash.hex8, "8a3f1c02")   // decimal-in → canonical hex-out
    }

    func testHashResolvedTimeout() {
        guard case .hashResolved(let node, _, _)? =
                PushDecoder.decode(line: #"{"ev":"hash_resolved","node":0,"auth":0,"hash":1}"#) else {
            return XCTFail("not hash_resolved")
        }
        XCTAssertEqual(node, 0)   // node == 0 → unresolved / timeout
    }

    func testReadyAndStatus() {
        guard case .ready(let r)? = PushDecoder.decode(
            line: #"{"ev":"ready","id":1,"key":"8a3f1c02","leaf_id":0,"mode":"node","gateway":false,"routing_sf":7}"#) else {
            return XCTFail("not ready")
        }
        XCTAssertEqual(r.id, 1); XCTAssertEqual(r.key.hex8, "8a3f1c02")
        XCTAssertEqual(r.mode, "node"); XCTAssertFalse(r.gateway); XCTAssertEqual(r.routingSF, 7)

        guard case .status(let s)? = PushDecoder.decode(
            line: #"{"ev":"status","id":1,"key":"8a3f1c02","state":"up","leaf_id":2,"gateway":true,"routing_sf":9}"#) else {
            return XCTFail("not status")
        }
        XCTAssertEqual(s.state, "up"); XCTAssertTrue(s.gateway); XCTAssertEqual(s.leafID, 2)
    }

    func testLogAndErr() {
        guard case .log(let m)? = PushDecoder.decode(line: #"{"log":"radio OK"}"#) else { return XCTFail() }
        XCTAssertEqual(m, "radio OK")
        guard case .error(let code, let msg)? = PushDecoder.decode(line: #"{"err":"bad_args","msg":"need hex"}"#) else { return XCTFail() }
        XCTAssertEqual(code, "bad_args"); XCTAssertEqual(msg, "need hex")
        guard case .error(let code2, let msg2)? = PushDecoder.decode(line: #"{"err":"oops"}"#) else { return XCTFail() }
        XCTAssertEqual(code2, "oops"); XCTAssertNil(msg2)
    }

    func testGenericEventKeepsFields() {
        guard case .event(let type, let fields)? =
                PushDecoder.decode(line: #"{"ev":"duty","pct":1.5,"window_ms":3600000}"#) else {
            return XCTFail("not a generic event")
        }
        XCTAssertEqual(type, "duty")
        XCTAssertEqual(fields["window_ms"]?.intValue, 3600000)
        XCTAssertNil(fields["ev"])   // discriminator stripped
    }

    func testNonJSONKeptAsRaw() {
        // Today's firmware emits human text — we must keep it, not drop it.
        guard case .raw(let s)? = PushDecoder.decode(line: "RECV from=2: hi") else { return XCTFail() }
        XCTAssertEqual(s, "RECV from=2: hi")
    }

    func testBlankLineIsNil() {
        XCTAssertNil(PushDecoder.decode(line: "   "))
        XCTAssertNil(PushDecoder.decode(line: ""))
    }
}

final class LineAccumulatorTests: XCTestCase {
    func testReassemblesAcrossChunks() {
        var acc = LineAccumulator()
        XCTAssertEqual(acc.append(Data(#"{"ev":"msg_"#.utf8)), [])
        let lines = acc.append(Data((#"recv"}"# + "\n").utf8))
        XCTAssertEqual(lines, [#"{"ev":"msg_recv"}"#])
    }
    func testSplitsMultipleLinesAndStripsCR() {
        var acc = LineAccumulator()
        let lines = acc.append(Data("a\r\nb\nc\n".utf8))
        XCTAssertEqual(lines, ["a", "b", "c"])
    }
}
