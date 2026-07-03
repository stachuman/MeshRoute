// MeshRouteWireTests — CommandEncoderTests.swift
// Pin the line-ASCII command format against console_parse.cpp / fw_main.cpp service_debug.

import XCTest
@testable import MeshRouteWire

final class CommandEncoderTests: XCTestCase {

    // §2 unified send (D24): `send <id|hash> "<body>" [-a] [-e]` — id/hash auto-detected; flags after the body.
    func testSendDMByID() {
        XCTAssertEqual(Command.sendDM(.init(target: .id(2), body: "hello world")).line,
                       #"send 2 "hello world""#)
        XCTAssertEqual(Command.sendDM(.init(target: .id(2), body: "hi", requestAck: true)).line,
                       #"send 2 "hi" -a"#)
        // -e is HASH-only: an id target never emits it even when encrypt is set (the node would error)
        XCTAssertEqual(Command.sendDM(.init(target: .id(2), body: "yo", encrypt: true)).line,
                       #"send 2 "yo""#)
    }

    func testSendDMByHash() {
        let h = KeyHash(0x8a3f1c02)
        XCTAssertEqual(Command.sendDM(.init(target: .hash(h), body: "yo")).line,
                       #"send 8a3f1c02 "yo""#)
        XCTAssertEqual(Command.sendDM(.init(target: .hash(h), body: "yo", requestAck: true)).line,
                       #"send 8a3f1c02 "yo" -a"#)
        XCTAssertEqual(Command.sendDM(.init(target: .hash(h), body: "yo", encrypt: true)).line,
                       #"send 8a3f1c02 "yo" -e"#)
        XCTAssertEqual(Command.sendDM(.init(target: .hash(h), body: "yo", requestAck: true, encrypt: true)).line,
                       #"send 8a3f1c02 "yo" -a -e"#)
    }

    func testSendChannel() {
        XCTAssertEqual(Command.sendChannel(.init(channelID: 3, body: "gm all")).line,
                       #"send_channel 3 "gm all""#)
    }

    func testBodySanitizedForQuotedWire() {
        // No wire escape: an embedded " would end the body early (→ bad_args), CR/LF would split the line → neutralize.
        XCTAssertEqual(Command.sendDM(.init(target: .id(1), body: #"he said "hi""#)).line,
                       #"send 1 "he said 'hi'""#)
        XCTAssertEqual(Command.sendDM(.init(target: .id(1), body: "line1\nline2")).line,
                       #"send 1 "line1 line2""#)
        XCTAssertEqual(Command.sendDM(.init(target: .id(1), body: "a\r\nb")).line,
                       #"send 1 "a b""#)
    }

    func testProvisioningVerbs() {     // R6 / D26: join / create / leave
        XCTAssertEqual(Command.join(freqMHz: 868.0, bwKHz: 125, ctrlSF: 7, level: 2).line,
                       "join level=2 freq=868 bw=125 sf=7")                  // key=value; whole MHz → "868"
        XCTAssertEqual(Command.join(freqMHz: 869.525, bwKHz: 250, ctrlSF: 9, level: 17).line,
                       "join level=17 freq=869.525 bw=250 sf=9")            // key=value; fractional MHz preserved
        XCTAssertEqual(Command.createLeaf(freqMHz: 868.0, bwKHz: 125, ctrlSF: 7, level: 2,
                                          sfList: "7, 9", dutyPercent: 10, name: "north field").line,
                       #"create level=2 freq=868 bw=125 sf=7 sf_list=7,9 duty=10 name="north field""#)   // key=value; sf_list spaces stripped; name quoted
        XCTAssertEqual(Command.join(freqMHz: 869.0, bwKHz: 62.5, ctrlSF: 7, level: 44).line,
                       "join level=44 freq=869 bw=62.5 sf=7")                // FRACTIONAL bw: 62.5 kHz stays "62.5" (firmware seeds 62500 Hz, not 62000)
        XCTAssertEqual(Command.createLeaf(freqMHz: 869.0, bwKHz: 62.5, ctrlSF: 7, level: 44,
                                          sfList: "6,7", dutyPercent: 0.1, name: "Test").line,
                       #"create level=44 freq=869 bw=62.5 sf=7 sf_list=6,7 duty=0.1 name="Test""#)   // FRACTIONAL bw AND duty both survive the wire
        XCTAssertEqual(Command.leave.line, "leave")
    }

    func testResolve() {
        XCTAssertEqual(Command.resolve(.init(hash: KeyHash(0x01))).line, "resolve 00000001")
        XCTAssertEqual(Command.resolve(.init(hash: KeyHash(0x01), hard: true)).line, "resolve 00000001 hard")
    }

    func testDiagnostics() {
        XCTAssertEqual(Command.whoami.line, "whoami")
        XCTAssertEqual(Command.routes.line, "routes")
        XCTAssertEqual(Command.status.line, "status")
        XCTAssertEqual(Command.config.line, "cfg")
        XCTAssertEqual(Command.configSet(key: "node_id", value: "5").line, "cfg set node_id 5")
        XCTAssertEqual(Command.lookup(KeyHash(0xdeadbeef)).line, "lookup deadbeef")
        XCTAssertEqual(Command.hashOf(7).line, "hashof 7")
        XCTAssertEqual(Command.raw("anything goes").line, "anything goes")
    }

    func testBodyQuotedVerbatim() {
        // The firmware reads the body verbatim BETWEEN the quotes (leading/trailing spaces preserved).
        XCTAssertEqual(Command.sendDM(.init(target: .id(1), body: "  leading + trailing  ")).line,
                       #"send 1 "  leading + trailing  ""#)
    }

    func testBodyByteLimits() {
        let dmOK = Command.sendDM(.init(target: .id(1), body: String(repeating: "a", count: 239)))
        XCTAssertTrue(dmOK.bodyFits)
        let dmTooBig = Command.sendDM(.init(target: .id(1), body: String(repeating: "a", count: 240)))
        XCTAssertFalse(dmTooBig.bodyFits)
        let chOK = Command.sendChannel(.init(channelID: 0, body: String(repeating: "a", count: 200)))
        XCTAssertTrue(chOK.bodyFits)
        let chTooBig = Command.sendChannel(.init(channelID: 0, body: String(repeating: "a", count: 201)))
        XCTAssertFalse(chTooBig.bodyFits)
        XCTAssertNil(Command.whoami.bodyByteLimit)
    }
}

final class KeyHashTests: XCTestCase {
    func testHexRoundTrip() {
        XCTAssertEqual(KeyHash(hex: "8a3f1c02")?.value, 0x8a3f1c02)
        XCTAssertEqual(KeyHash(hex: "0x8A3F1C02")?.value, 0x8a3f1c02) // prefix + uppercase
        XCTAssertEqual(KeyHash(hex: "1")?.value, 1)
        XCTAssertEqual(KeyHash(0x01).hex8, "00000001")               // canonical 8-digit lowercase
        XCTAssertEqual(KeyHash(0xdeadbeef).hex8, "deadbeef")
    }
    func testHexRejects() {
        XCTAssertNil(KeyHash(hex: ""))
        XCTAssertNil(KeyHash(hex: "xyz"))
        XCTAssertNil(KeyHash(hex: "123456789"))  // > 8 digits
    }
}
