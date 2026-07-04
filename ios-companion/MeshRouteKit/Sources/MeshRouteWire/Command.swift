// MeshRouteWire — Command.swift
//
// App→node commands, encoded as the firmware's LINE-ASCII console verbs (the format
// console_parse.cpp + fw_main.cpp service_debug actually accept today). This is the
// "line-ASCII commands + JSON pushes" contract chosen on review: zero firmware decoder
// work, the node already parses these. The transport appends the '\n'; `line` has none.
//
// Verb reference (console_parse.cpp / fw_main.cpp — 2026-06-21 unified send, D24):
//   send <id|hash> "<text>" [-a] [-e]    send_channel <ch> "<text>"    (send_layer — explicit path, app-unused)
//   resolve <hex> [hard] | cfg | cfg set <k> <v> | routes | status | whoami | lookup <hex> | hashof <id>
//   peerkey <hex64> | reqpubkey <hex8> | pull_inbox <d> <c> | mark_read <dm|chan> <seq>

import Foundation

/// A DM is addressed by short id (resolved now) or key_hash32 (the node resolves it).
public enum DMTarget: Hashable, Sendable {
    case id(UInt8)          // 0…254 (254 max; 255 reserved)
    case hash(KeyHash)
}

public struct SendDM: Hashable, Sendable {
    public var target: DMTarget
    public var body: String
    public var requestAck: Bool     // the `-a` flag — request the end-to-end delivery ack (wire E2E=0x08)
    public var encrypt: Bool        // the `-e` flag — per-message E2E crypt (D24). HASH-only (sealing needs the
                                    // recipient's pubkey; the node rejects -e on an id). Absent ⇒ the node's e2e_dm default.
    public init(target: DMTarget, body: String, requestAck: Bool = false, encrypt: Bool = false) {
        self.target = target; self.body = body; self.requestAck = requestAck; self.encrypt = encrypt
    }
}

public struct SendChannelPost: Hashable, Sendable {
    public var channelID: UInt8     // 0…255
    public var body: String
    public init(channelID: UInt8, body: String) { self.channelID = channelID; self.body = body }
}

public struct ResolveRequest: Hashable, Sendable {
    public var hash: KeyHash
    public var hard: Bool           // skip caches, reach the owner (verify-on-use)
    public init(hash: KeyHash, hard: Bool = false) { self.hash = hash; self.hard = hard }
}

public enum Command: Hashable, Sendable {
    case sendDM(SendDM)
    case sendChannel(SendChannelPost)
    case resolve(ResolveRequest)
    // diagnostics / config — line-ASCII passthrough (responses are human-text today; see Inbound).
    case whoami
    case routes
    case status
    case config                                 // "cfg" (dump)
    case duty                                    // "duty" — airtime-budget readout (D27)
    case configSet(key: String, value: String)  // "cfg set <key> <value>"
    case lookup(KeyHash)                         // local id_bind peek (no airtime)
    case hashOf(UInt8)                           // reverse: id → hash (authoritative only)
    // inbox sync (persistent-inbox spec §8) — catch up the durable history on connect / after being away.
    case pullInbox(dmSince: UInt32, chanSince: UInt32)
    case markRead(kind: InboxKind, seq: UInt32)
    // E2E peer-key provisioning (2026-06-16 contract). The app does NO crypto — these just hand the node bytes.
    case peerKey(pubkeyHex: String)              // install a scanned card's pubkey (PINNED) → "peerkey <hex64>"
    case reqPubkey(KeyHash)                       // user-triggered on-air key request → "reqpubkey <hex8>"
    // Leaf provisioning (R6 / D26) — key=value wire (2026-07-03, mirrors gateway; order-free). live, no reboot. freq = MHz (float); bw = kHz (FRACTIONAL — 62.5/41.67/31.25); dutyPercent = % (FRACTIONAL — 0.1 = the tight EU sub-band); layer=1..255 network id (wire leaf nibble = layer & 0x0F).
    case join(freqMHz: Double, bwKHz: Double, ctrlSF: Int, layer: Int)
    case createLeaf(freqMHz: Double, bwKHz: Double, ctrlSF: Int, layer: Int, sfList: String, dutyPercent: Double, name: String)
    case leave                                    // reset membership (wipe to default, KEEP freq)
    case raw(String)                             // escape hatch — sent verbatim

    /// The exact console line (no trailing newline — the transport frames it).
    public var line: String {
        switch self {
        case .sendDM(let dm):
            // §2 unified send (firmware 2026-06-21, D24): `send <id|hash> "<body>" [-a] [-e]`. The node
            // AUTO-detects id (≤254 decimal) vs hash (8-hex). -a = E2E-ack; -e = encrypt (HASH-only — the
            // node errors on -e for an id target). Body is QUOTED + sanitized (see wireBody).
            let addr: String, isHash: Bool
            switch dm.target {
            case .id(let i):    addr = String(i); isHash = false
            case .hash(let h):  addr = h.hex8;    isHash = true
            }
            var s = "send \(addr) \"\(Self.wireBody(dm.body))\""
            if dm.requestAck      { s += " -a" }
            if dm.encrypt, isHash { s += " -e" }   // -e only on a hash target
            return s
        case .sendChannel(let p):
            return "send_channel \(p.channelID) \"\(Self.wireBody(p.body))\""
        case .resolve(let r):
            return r.hard ? "resolve \(r.hash.hex8) hard" : "resolve \(r.hash.hex8)"
        case .whoami:                       return "whoami"
        case .routes:                       return "routes"
        case .status:                       return "status"
        case .config:                       return "cfg"
        case .duty:                         return "duty"
        case .configSet(let k, let v):      return "cfg set \(k) \(v)"
        case .lookup(let h):                return "lookup \(h.hex8)"
        case .hashOf(let i):                return "hashof \(i)"
        case .pullInbox(let dm, let chan):  return "pull_inbox \(dm) \(chan)"
        case .markRead(let kind, let seq):  return "mark_read \(kind.commandToken) \(seq)"
        case .peerKey(let hex):             return "peerkey \(hex)"
        case .reqPubkey(let h):             return "reqpubkey \(h.hex8)"
        case .join(let f, let bw, let sf, let lyr):
            return "join layer=\(lyr) freq=\(Self.freqToken(f)) bw=\(Self.freqToken(bw)) sf=\(sf)"      // key=value; bw compact (62.5 / 125), wire leaf nibble = layer & 0x0F
        case .createLeaf(let f, let bw, let sf, let lyr, let sfList, let duty, let name):
            let sfs = sfList.replacingOccurrences(of: " ", with: "")   // sf_list must be one token: "7,9"
            return "create layer=\(lyr) freq=\(Self.freqToken(f)) bw=\(Self.freqToken(bw)) sf=\(sf) sf_list=\(sfs) duty=\(Self.freqToken(duty)) name=\"\(Self.wireBody(name))\""   // key=value; bw + duty compact (fractional ok); anti-spam knobs omitted → firmware defaults
        case .leave:                        return "leave"
        case .raw(let s):                   return s
        }
    }

    /// Sanitize a body for the QUOTED wire form. The firmware's `parse_send_tail` reads verbatim bytes up to
    /// the NEXT `"` with NO escape, and the BLE transport frames lines on `\n` — so neutralize the two chars
    /// that would corrupt the wire: a `"` (ends the body early → `bad_args`) → `'`; CR/LF (splits the command
    /// line) → a space. Both are 1 byte → 1 byte, so the body byte budget is unchanged. (A real wire escape is
    /// a firmware ask — roadmap §8.2.)
    static func wireBody(_ body: String) -> String {
        body.replacingOccurrences(of: "\"", with: "'")
            .replacingOccurrences(of: "\r\n", with: " ")
            .replacingOccurrences(of: "\n", with: " ")
            .replacingOccurrences(of: "\r", with: " ")
    }

    /// Format a MHz frequency (or any kHz/decimal radio value — freq + bw both use this) as a locale-independent
    /// wire token (the firmware `atof`-parses it); trims a trailing ".0" so a whole value reads "868"/"125" not
    /// "868.0"/"125.0", while a fractional value keeps its decimals ("62.5", "41.67").
    public static func freqToken(_ v: Double) -> String {
        let s = String(v)
        return s.hasSuffix(".0") ? String(s.dropLast(2)) : s
    }

    /// UTF-8 body byte budget for this command's payload (nil for non-payload commands).
    public var bodyByteLimit: Int? {
        switch self {
        case .sendDM:      return WireConstants.dmMaxBodyBytes
        case .sendChannel: return WireConstants.channelMaxBodyBytes
        default:           return nil
        }
    }

    /// True if the payload fits the node's inner buffer (UI should block over-long sends).
    public var bodyFits: Bool {
        guard let limit = bodyByteLimit else { return true }
        let body: String
        switch self {
        case .sendDM(let dm):      body = dm.body
        case .sendChannel(let p):  body = p.body
        default:                   return true
        }
        return body.utf8.count <= limit
    }
}
