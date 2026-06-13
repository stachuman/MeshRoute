// MeshRouteCoreTests — the QR contact-card payload (roadmap B1).

import XCTest
import MeshRouteWire
@testable import MeshRouteCore

final class ContactCardTests: XCTestCase {

    func testRoundTrip() {
        let card = ContactCard(name: "Stani K", hash: KeyHash(0xff60_9d5c))
        let parsed = ContactCard(qrString: card.qrString)
        XCTAssertEqual(parsed, card)
        XCTAssertTrue(card.qrString.hasPrefix("https://meshroute.eu/c?"))   // the project domain is canonical
        XCTAssertTrue(card.qrString.contains("h=ff609d5c"))
        XCTAssertTrue(card.qrString.contains("n=Stani%20K"))     // names percent-encode
    }

    func testParseToleratesUnknownParamsAndMissingName() {
        // a future (v2) card with extra params still parses on this app
        let parsed = ContactCard(qrString: "https://meshroute.eu/c?v=2&h=6bac7eb9&x=future&n=Marek")
        XCTAssertEqual(parsed?.hash, KeyHash(0x6bac_7eb9))
        XCTAssertEqual(parsed?.name, "Marek")
        // nameless card → empty name (the add-UI asks for one)
        XCTAssertEqual(ContactCard(qrString: "https://meshroute.eu/c?v=1&h=6bac7eb9")?.name, "")
        // www + /contact variants are accepted
        XCTAssertEqual(ContactCard(qrString: "https://www.meshroute.eu/contact?v=1&h=6bac7eb9&n=M")?.name, "M")
    }

    func testLegacySchemeStillParses() {
        let parsed = ContactCard(qrString: "meshroute://contact?v=1&h=6bac7eb9&n=Marek")
        XCTAssertEqual(parsed?.hash, KeyHash(0x6bac_7eb9))
        XCTAssertEqual(parsed?.name, "Marek")
    }

    func testParseCarriesReservedPubkey() {
        let hex64 = String(repeating: "ab", count: 32)
        let parsed = ContactCard(qrString: "https://meshroute.eu/c?v=1&h=6bac7eb9&p=\(hex64)")
        XCTAssertEqual(parsed?.pubkeyHex, hex64)                 // opaque bytes (D6), stored not verified
        // malformed pubkey is dropped, card still valid
        XCTAssertNil(ContactCard(qrString: "https://meshroute.eu/c?v=1&h=6bac7eb9&p=zz")?.pubkeyHex)
    }

    func testRejectsForeignPayloads() {
        XCTAssertNil(ContactCard(qrString: "https://example.com/c?h=6bac7eb9"))  // wrong domain
        XCTAssertNil(ContactCard(qrString: "https://meshroute.eu/join?leaf=3"))  // different card type
        XCTAssertNil(ContactCard(qrString: "meshroute://join?leaf=3"))
        XCTAssertNil(ContactCard(qrString: "https://meshroute.eu/c?v=1&h=nothex"))
        XCTAssertNil(ContactCard(qrString: "WIFI:T:WPA;S:home;;"))
    }
}
