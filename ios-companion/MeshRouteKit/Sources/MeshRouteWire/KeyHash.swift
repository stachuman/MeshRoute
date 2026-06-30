// MeshRouteWire — KeyHash.swift
//
// key_hash32 = ed_pub[:4] — the node's stable cryptographic id (the device has NO name map;
// the app owns name↔hash). On the wire a hash appears two ways:
//   • as an 8-digit lowercase hex STRING in `ready`/`status` `key` (console_json key_hex32: "%08x"),
//     and as the hex token the app sends in `send`/`resolve`/`lookup` commands.
//   • as a DECIMAL u32 NUMBER in the `hash_resolved` push (console_json: j.u32(hash)).
// KeyHash therefore decodes flexibly (string-hex OR number) and renders canonical hex8.

import Foundation

public struct KeyHash: Hashable, Sendable, CustomStringConvertible {
    public var value: UInt32
    public init(_ value: UInt32) { self.value = value }

    /// Parse 1–8 hex digits, optional `0x`/`0X` prefix. nil on empty / non-hex / >8 digits.
    public init?(hex: String) {
        var s = Substring(hex)
        if s.hasPrefix("0x") || s.hasPrefix("0X") { s = s.dropFirst(2) }
        guard !s.isEmpty, s.count <= 8 else { return nil }
        var v: UInt32 = 0
        for c in s {
            guard let d = c.hexDigitValue else { return nil }
            v = (v << 4) | UInt32(d)
        }
        self.value = v
    }

    /// Canonical 8-digit lowercase hex (matches console_json key_hex32 + what the firmware parses).
    public var hex8: String { String(format: "%08x", value) }
    public var description: String { hex8 }
}

// Decodes from a hex string OR a numeric u32; encodes as a hex8 string.
extension KeyHash: Codable {
    public init(from decoder: Decoder) throws {
        let c = try decoder.singleValueContainer()
        if let n = try? c.decode(UInt32.self) {
            self.value = n
        } else if let s = try? c.decode(String.self), let parsed = KeyHash(hex: s) {
            self = parsed
        } else {
            throw DecodingError.dataCorruptedError(in: c, debugDescription: "KeyHash: not a u32 or hex string")
        }
    }
    public func encode(to encoder: Encoder) throws {
        var c = encoder.singleValueContainer()
        try c.encode(hex8)
    }
}
