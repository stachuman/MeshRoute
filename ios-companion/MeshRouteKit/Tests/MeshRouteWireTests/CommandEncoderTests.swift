// MeshRouteWireTests — CommandEncoderTests.swift
// Pin the line-ASCII command format against console_parse.cpp / fw_main.cpp service_debug.

import XCTest
@testable import MeshRouteWire

final class CommandEncoderTests: XCTestCase {

    func testSendDMByID() {
        XCTAssertEqual(Command.sendDM(.init(target: .id(2), body: "hello world")).line,
                       "send 2 hello world")
        XCTAssertEqual(Command.sendDM(.init(target: .id(2), body: "hi", requestAck: true)).line,
                       "send_ack 2 hi")
    }

    func testSendDMByHash() {
        let h = KeyHash(0x8a3f1c02)
        XCTAssertEqual(Command.sendDM(.init(target: .hash(h), body: "yo")).line,
                       "sendhash 8a3f1c02 yo")
        XCTAssertEqual(Command.sendDM(.init(target: .hash(h), body: "yo", requestAck: true)).line,
                       "sendhash_ack 8a3f1c02 yo")
        // per-message E2E crypt (2026-06-16): sendhashx / sendhashx_ack (hash-only)
        XCTAssertEqual(Command.sendDM(.init(target: .hash(h), body: "yo", encrypt: true)).line,
                       "sendhashx 8a3f1c02 yo")
        XCTAssertEqual(Command.sendDM(.init(target: .hash(h), body: "yo", requestAck: true, encrypt: true)).line,
                       "sendhashx_ack 8a3f1c02 yo")
        // encrypt is ignored for an id target (no encrypted id-send)
        XCTAssertEqual(Command.sendDM(.init(target: .id(2), body: "yo", encrypt: true)).line, "send 2 yo")
    }

    func testSendChannel() {
        XCTAssertEqual(Command.sendChannel(.init(channelID: 3, body: "gm all")).line,
                       "send_channel 3 gm all")
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

    func testBodyPreservedVerbatim() {
        // The firmware takes "remainder after one space, verbatim incl. spaces" — don't mangle it.
        XCTAssertEqual(Command.sendDM(.init(target: .id(1), body: "  leading + trailing  ")).line,
                       "send 1   leading + trailing  ")
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
