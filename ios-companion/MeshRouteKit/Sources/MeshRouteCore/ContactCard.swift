// MeshRouteCore — ContactCard.swift
//
// The QR contact-exchange payload (roadmap B1): in-person pairing — my phone SHOWS this, yours SCANS
// it. Physical presence is the trust ceremony, so v1 carries no signature; keys stay opaque bytes to
// the app (D6). The canonical format is an https URL on the project domain (booked 2026-06-12):
//
//   https://meshroute.eu/c?v=1&h=<key_hash32 hex8>&n=<name, percent-encoded>[&p=<ed_pub hex64>]
//
// https (not a custom scheme) so a STOCK-camera scan can deep-link into the app once meshroute.eu
// hosts an apple-app-site-association file — and falls back to a "get the app" web page for everyone
// else. The in-app scanner works either way. `meshroute://contact?…` is accepted as a legacy alias.
// `p` (the full pubkey) is RESERVED for B2/E2E — emitted when the app learns it, ignored-if-unknown
// today. Unknown query items never fail the parse (a v2 card scans fine on a v1 app).

import Foundation
import MeshRouteWire

public struct ContactCard: Hashable, Sendable {
    public let name: String
    public let hash: KeyHash
    public let pubkeyHex: String?     // 64 hex chars when present; opaque to the app (D6)

    public init(name: String, hash: KeyHash, pubkeyHex: String? = nil) {
        self.name = name
        self.hash = hash
        self.pubkeyHex = pubkeyHex
    }

    /// The QR payload string (canonical https://meshroute.eu form).
    public var qrString: String {
        var c = URLComponents()
        c.scheme = "https"
        c.host = "meshroute.eu"
        c.path = "/c"
        var items = [URLQueryItem(name: "v", value: "1"),
                     URLQueryItem(name: "h", value: hash.hex8)]
        if !name.isEmpty { items.append(URLQueryItem(name: "n", value: name)) }
        if let p = pubkeyHex { items.append(URLQueryItem(name: "p", value: p)) }
        c.queryItems = items
        return c.string ?? "https://meshroute.eu/c?v=1&h=\(hash.hex8)"
    }

    /// Parse a scanned payload. nil = not a MeshRoute contact card (let the scanner keep looking).
    /// Accepts the canonical https://meshroute.eu/c form and the legacy meshroute://contact alias.
    public init?(qrString: String) {
        guard let c = URLComponents(string: qrString) else { return nil }
        let isCanonical = (c.scheme == "https" || c.scheme == "http")
            && (c.host?.lowercased() == "meshroute.eu" || c.host?.lowercased() == "www.meshroute.eu")
            && (c.path == "/c" || c.path == "/contact")
        let isLegacy = c.scheme?.lowercased() == "meshroute" && c.host?.lowercased() == "contact"
        guard isCanonical || isLegacy else { return nil }
        var fields: [String: String] = [:]
        for item in c.queryItems ?? [] { fields[item.name] = item.value }
        guard let h = fields["h"], let hash = KeyHash(hex: h) else { return nil }
        self.hash = hash
        self.name = fields["n"] ?? ""
        if let p = fields["p"], p.count == 64, p.allSatisfy(\.isHexDigit) { self.pubkeyHex = p.lowercased() }
        else { self.pubkeyHex = nil }
    }
}
