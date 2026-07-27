// MeshRouteCoreTests — TeamCardTests.swift
// Pin the TEAM QR payload against the team-encrypted-channel spec T-K4 §2.4.

import XCTest
@testable import MeshRouteCore

final class TeamCardTests: XCTestCase {

    private func sample(name: String = "Ridge team") -> TeamCard {
        TeamCard(teamName: name, teamID: 0xcccc_0001, freqKHz: 869_525, bwHz: 125_000,
                 routingSF: 9, sfListBitmap: (1 << 7) | (1 << 9), cr: 5,
                 teamChPubHex: String(repeating: "ab", count: 32),
                 teamChPrivHex: String(repeating: "cd", count: 32))
    }

    func testRoundTrip() {
        let card = sample()
        guard let decoded = TeamCard(qrString: card.qrString) else { return XCTFail("did not decode") }
        XCTAssertEqual(decoded, card)                       // every field survives the pack/unpack
        XCTAssertEqual(decoded.teamIDHex, "cccc0001")       // the contract's hex-string form
        XCTAssertEqual(decoded.sfList, [7, 9])              // bitmap → data SFs
        XCTAssertEqual(decoded.freqMHz, 869.525, accuracy: 1e-6)
        XCTAssertEqual(decoded.bwKHz, 125, accuracy: 1e-6)
        XCTAssertEqual(decoded.teamChPrivHex, String(repeating: "cd", count: 32))
    }

    func testCustomSchemeNotHTTPS() {
        // ⚠ the card carries a PRIVATE key — it must never be a URL a stock camera would send to a server
        let s = sample().qrString
        XCTAssertTrue(s.hasPrefix("meshroute://team?d="))
        XCTAssertFalse(s.lowercased().contains("http"))
    }

    func testPayloadFitsAQRComfortably() {
        // spec: ~90 B + name → base64url stays a comfortable QR
        XCTAssertLessThan(sample().qrString.count, 200)
    }

    func testUnicodeAndEmptyName() {
        let uni = sample(name: "Grzbiet — zespół ⛰")
        XCTAssertEqual(TeamCard(qrString: uni.qrString)?.teamName, "Grzbiet — zespół ⛰")
        let none = sample(name: "")
        XCTAssertEqual(TeamCard(qrString: none.qrString)?.teamName, "")
    }

    func testRejectsCorruptedScan() {
        // flip a payload character → the CRC32 must reject it (QR scans corrupt)
        let s = sample().qrString
        let idx = s.index(s.startIndex, offsetBy: s.count - 8)
        var bad = s
        bad.replaceSubrange(idx...idx, with: s[idx] == "A" ? "B" : "A")
        XCTAssertNil(TeamCard(qrString: bad))
    }

    func testRejectsForeignPayloads() {
        XCTAssertNil(TeamCard(qrString: "https://meshroute.eu/c?v=1&h=8a3f1c02"))   // a CONTACT card
        XCTAssertNil(TeamCard(qrString: "meshroute://team?d=zzzz"))                  // not base64/too short
        XCTAssertNil(TeamCard(qrString: "totally unrelated"))
    }

    func testContactCardDoesNotSwallowTeamPayload() {
        XCTAssertNil(ContactCard(qrString: sample().qrString))   // the two QR types never cross-parse
    }
}
