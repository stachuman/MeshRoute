// MeshRouteCore — TeamCard.swift
//
// The TEAM QR payload (team-encrypted-channel spec T-K4 §2.4): one scan = full team onboarding — the
// overlay/PHY parameters AND the team channel keypair. A second QR *type* alongside `ContactCard`
// (same scanner, different payload).
//
// ⚠ SECURITY — deliberately NOT an https URL. A `ContactCard` is https so a stock camera can deep-link
// (it carries only public data). This card carries the team channel PRIVATE key: an https QR opened by a
// stock camera would send the whole path+query to meshroute.eu (and into browser history/logs). So the
// team card uses a custom scheme — it can never resolve to a web request:
//
//     meshroute://team?d=<base64url(packed payload)>
//
// Packed payload (little-endian, per T-K4 §2.4) — the app treats both keys as OPAQUE bytes (D6):
//     ver u8 · team_id u32 · freq_khz u32 · bw_hz u32 · routing_sf u8 · sf_list u16 · cr u8 ·
//     team_ch_pub[32] · team_ch_priv[32] · name_len u8 · name utf8[name_len] · crc32 u32
// = 86 B + name. CRC32 (IEEE, reflected) covers every preceding byte — QR scans corrupt.

import Foundation
import MeshRouteWire

public struct TeamCard: Hashable, Sendable {
    public static let version: UInt8 = 1

    public let teamName: String
    public let teamID: UInt32          // rendered as the contract's lowercase hex string elsewhere
    public let freqKHz: UInt32
    public let bwHz: UInt32
    public let routingSF: UInt8
    public let sfListBitmap: UInt16    // data SFs as a bitmap (bit n = SF n)
    public let cr: UInt8
    public let teamChPubHex: String    // 64 hex — opaque to the app (D6)
    public let teamChPrivHex: String   // 64 hex — PRIVATE; never leaves the phone except into the node

    public init(teamName: String, teamID: UInt32, freqKHz: UInt32, bwHz: UInt32, routingSF: UInt8,
                sfListBitmap: UInt16, cr: UInt8, teamChPubHex: String, teamChPrivHex: String) {
        self.teamName = teamName; self.teamID = teamID
        self.freqKHz = freqKHz; self.bwHz = bwHz; self.routingSF = routingSF
        self.sfListBitmap = sfListBitmap; self.cr = cr
        self.teamChPubHex = teamChPubHex.lowercased(); self.teamChPrivHex = teamChPrivHex.lowercased()
    }

    /// The team id in the contract's wire form (a lowercase hex string, like `key`).
    public var teamIDHex: String { String(format: "%08x", teamID) }
    /// The data SFs the bitmap encodes, ascending (e.g. [7, 9]).
    public var sfList: [Int] { (0...15).filter { sfListBitmap & (1 << $0) != 0 } }
    /// Frequency in MHz for display / the provisioning verb.
    public var freqMHz: Double { Double(freqKHz) / 1000 }
    public var bwKHz: Double { Double(bwHz) / 1000 }

    // ---- encode ----

    public var qrString: String {
        var b = [UInt8]()
        b.append(Self.version)
        b.append(le32: teamID)
        b.append(le32: freqKHz)
        b.append(le32: bwHz)
        b.append(routingSF)
        b.append(le16: sfListBitmap)
        b.append(cr)
        b.append(contentsOf: Self.bytes(fromHex: teamChPubHex, count: 32))
        b.append(contentsOf: Self.bytes(fromHex: teamChPrivHex, count: 32))
        let name = Array(teamName.utf8.prefix(32))          // ≤32 per the grant-body convention
        b.append(UInt8(name.count))
        b.append(contentsOf: name)
        b.append(le32: crc32(b))                             // integrity over everything above
        return "meshroute://team?d=" + Data(b).base64URLEncoded
    }

    // ---- decode ----

    /// Parse a scanned payload. nil = not a MeshRoute team card (bad scheme, truncation, or CRC mismatch).
    public init?(qrString: String) {
        guard let c = URLComponents(string: qrString),
              c.scheme?.lowercased() == "meshroute", c.host?.lowercased() == "team",
              let d = (c.queryItems ?? []).first(where: { $0.name == "d" })?.value,
              let raw = Data(base64URLEncoded: d) else { return nil }
        let b = [UInt8](raw)
        guard b.count >= 86, b[0] == Self.version else { return nil }     // ver + the fixed block + crc
        let nameLen = Int(b[81])
        let total = 82 + nameLen + 4
        guard b.count >= total else { return nil }
        let body = Array(b[0..<(total - 4)])
        guard crc32(body) == UInt32(le: Array(b[(total - 4)..<total])) else { return nil }   // corrupt scan
        self.teamID    = UInt32(le: Array(b[1..<5]))
        self.freqKHz   = UInt32(le: Array(b[5..<9]))
        self.bwHz      = UInt32(le: Array(b[9..<13]))
        self.routingSF = b[13]
        self.sfListBitmap = UInt16(b[14]) | (UInt16(b[15]) << 8)
        self.cr        = b[16]
        self.teamChPubHex  = Array(b[17..<49]).hexString
        self.teamChPrivHex = Array(b[49..<81]).hexString
        self.teamName  = String(decoding: b[82..<(82 + nameLen)], as: UTF8.self)
    }

    private static func bytes(fromHex hex: String, count: Int) -> [UInt8] {
        var out = [UInt8](); out.reserveCapacity(count)
        var i = hex.startIndex
        while i < hex.endIndex, out.count < count {
            let j = hex.index(i, offsetBy: 2, limitedBy: hex.endIndex) ?? hex.endIndex
            out.append(UInt8(hex[i..<j], radix: 16) ?? 0)
            i = j
        }
        while out.count < count { out.append(0) }            // tolerate a short/absent key (fail loud upstream)
        return out
    }
}

// ---- CRC32 (IEEE 802.3, reflected) — QR scans corrupt; the card must reject a bad read ----

func crc32(_ bytes: [UInt8]) -> UInt32 {
    var crc: UInt32 = 0xFFFF_FFFF
    for b in bytes {
        crc ^= UInt32(b)
        for _ in 0..<8 { crc = (crc >> 1) ^ (0xEDB8_8320 & (0 &- (crc & 1))) }
    }
    return crc ^ 0xFFFF_FFFF
}

// ---- small byte helpers (little-endian packing + base64url) ----

extension Array where Element == UInt8 {
    mutating func append(le16 v: UInt16) { append(UInt8(v & 0xFF)); append(UInt8((v >> 8) & 0xFF)) }
    mutating func append(le32 v: UInt32) {
        append(UInt8(v & 0xFF)); append(UInt8((v >> 8) & 0xFF))
        append(UInt8((v >> 16) & 0xFF)); append(UInt8((v >> 24) & 0xFF))
    }
    var hexString: String { map { String(format: "%02x", $0) }.joined() }
}

extension UInt32 {
    init(le b: [UInt8]) {
        self = UInt32(b[0]) | (UInt32(b[1]) << 8) | (UInt32(b[2]) << 16) | (UInt32(b[3]) << 24)
    }
}

extension Data {
    /// base64url (RFC 4648 §5) — QR-safe: no `+`, `/`, or `=` padding.
    var base64URLEncoded: String {
        base64EncodedString()
            .replacingOccurrences(of: "+", with: "-")
            .replacingOccurrences(of: "/", with: "_")
            .replacingOccurrences(of: "=", with: "")
    }
    init?(base64URLEncoded s: String) {
        var t = s.replacingOccurrences(of: "-", with: "+").replacingOccurrences(of: "_", with: "/")
        while t.count % 4 != 0 { t += "=" }
        guard let d = Data(base64Encoded: t) else { return nil }
        self = d
    }
}
