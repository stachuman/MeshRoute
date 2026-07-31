// MeshRouteWire — Command.swift
//
// App→node commands, encoded as the firmware's LINE-ASCII console verbs (the format
// console_parse.cpp + fw_main.cpp service_debug actually accept today). This is the
// "line-ASCII commands + JSON pushes" contract chosen on review: zero firmware decoder
// work, the node already parses these. The transport appends the '\n'; `line` has none.
//
// Verb reference (console_parse.cpp / fw_main.cpp — D24 unified send + D30 plane split; hashes 0x-PREFIXED):
//   send <id|0xhash> "<text>" [-a] [-e] [-t]   send_channel <ch> "<text>"   (send_layer — explicit path, app-unused)
//   resolve 0x<hex8> [hard] | cfg | cfg set <k> <v> | routes | status | whoami | lookup 0x<hex8> | hashof <id>
//   peerkey <hex64> | reqpubkey 0x<hex8> [-t] | pull_inbox <d> <c> | mark_read <dm|chan> <seq>
//   -t = the TEAM plane (D30): a bare id under -t is a team_local_id (a distinct id space from static node ids).

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
    public var teamPlane: Bool      // the `-t` flag (D30 plane split): route on the TEAM overlay. With .id the id
                                    // is a team_local_id (a DISTINCT id space); with .hash it team-H-flood-resolves.
                                    // Without -t a teammate is NOT reachable (global/home plane → no_route).
    public var attachLocation: Bool // the `-l` flag (2026-07-31, B0): attach THIS node's position to THIS message.
                                    // ⚠ Location requires encryption — a `-l` DM that can't be sealed REFUSES
                                    // (`unsealable`), and no fix REFUSES (`no_location`). It never sends without it.
    public init(target: DMTarget, body: String, requestAck: Bool = false, encrypt: Bool = false,
                teamPlane: Bool = false, attachLocation: Bool = false) {
        self.target = target; self.body = body; self.requestAck = requestAck; self.encrypt = encrypt
        self.teamPlane = teamPlane; self.attachLocation = attachLocation
    }
}

public struct SendChannelPost: Hashable, Sendable {
    public var channelID: UInt8     // 0…255
    public var body: String
    /// `-t` — post on the TEAM plane (team_id-scoped). Plain = GLOBAL (channel-crypt spec §2.2 matrix).
    public var teamPlane: Bool
    /// `-e` — seal the post to the team channel key. ⚠ VALID ONLY WITH `-t`: there is no key for a global
    /// channel, and `-e` without `-t` REFUSES. (`-t -g -e` also refuses — it would air an identical CLEAR
    /// global copy and defeat the encryption. The app never offers `-g`, so that trap is unreachable here.)
    public var encrypt: Bool
    /// Attach this node's position as the post's `inner_type = 1`. ⚠ REQUIRES the crypted flavour — a
    /// location in a plaintext channel post is the same leak, broadcast wider. ⚠ NOT YET ON THE WIRE: the
    /// console form is undefined (today `send_channel -l` = `bad_args`), so callers must leave this false
    /// until T-K5 lands; the encoder below is ready for it.
    public var attachLocation: Bool

    public init(channelID: UInt8, body: String, teamPlane: Bool = false, encrypt: Bool = false,
                attachLocation: Bool = false) {
        self.channelID = channelID; self.body = body
        self.teamPlane = teamPlane; self.encrypt = encrypt; self.attachLocation = attachLocation
    }
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
    case limits                                  // "limits" — anti-spam pacing snapshot (D29)
    case configSet(key: String, value: String)  // "cfg set <key> <value>"
    case lookup(KeyHash)                         // local id_bind peek (no airtime)
    case hashOf(UInt8)                           // reverse: id → hash (authoritative only)
    // inbox sync (persistent-inbox spec §8) — catch up the durable history on connect / after being away.
    case pullInbox(dmSince: UInt32, chanSince: UInt32)
    case markRead(kind: InboxKind, seq: UInt32)
    // E2E peer-key provisioning (2026-06-16 contract). The app does NO crypto — these just hand the node bytes.
    case peerKey(pubkeyHex: String)              // install a scanned card's pubkey (PINNED) → "peerkey <hex64>"
    case reqPubkey(KeyHash, team: Bool)           // on-air key request → "reqpubkey 0x<hex8> [-t]" (-t = team-scoped, D30)
    case reqPubkeyTeam(localID: UInt8)            // "reqpubkey <id>" — a BARE decimal is implicitly TEAM-scoped (D30):
                                                  // the teammate-bootstrap (mutual handshake → peer_key_cached{hash,name})
    // Mobile roam / status (D30 / S3) — the roam screen's verbs.
    case mobileStatus                             // "mobile status"  → {"ev":"mobile_status",…}
    case mobileGateways                           // "mobile gateways" → mobile_gw* / mobile_net* / mobile_gw_end
    case mobileRegister                           // "mobile register" — (re-)register on the current PHY
    case mobileRegisterScan                       // "mobile register scan" — cycle the learned networks
    case mobileRegisterTarget(freqKHz: Int, sf: Int, bwHz: Int)   // target a `mobile_net` row (integer wire units → MHz/kHz tokens)
    /// Join a team from a scanned team QR (T-K1/T-K4), adopting its channel keypair:
    ///   `team <0xid> [freq=<MHz> sf=<5-12> bw=<kHz>] tkpub=<64 hex> tkpriv=<64 hex>`
    /// ⚠ The id MUST carry `0x` (2026-07-30 grammar: a bare `88A672BA` would join *team 88*), and the verb
    /// REJECTS unknown keys — it takes freq/sf/bw only (no `sf_list=`/`cr=`).
    case teamJoin(teamIDHex: String, freqMHz: Double?, ctrlSF: Int?, bwKHz: Double?,
                  tkPubHex: String?, tkPrivHex: String?)
    case teamExportKey                            // "team exportkey" → team_key_export | team_key_err (⚠ discloses the PRIVATE key)
    case teamGrantKey(KeyHash)                    // "team grantkey 0x<hash>" — sealed TEAM_KEY_GRANT to a vetted joiner (T-K3)
    // Leaf provisioning (R6 / D26) — key=value wire (2026-07-03, mirrors gateway; order-free). live, no reboot. freq = MHz (float); bw = kHz (FRACTIONAL — 62.5/41.67/31.25); dutyPercent = % (FRACTIONAL — 0.1 = the tight EU sub-band); layer=1..255 network id (wire leaf nibble = layer & 0x0F).
    case join(freqMHz: Double, bwKHz: Double, ctrlSF: Int, layer: Int)
    case createLeaf(freqMHz: Double, bwKHz: Double, ctrlSF: Int, layer: Int, sfList: String, dutyPercent: Double, name: String)
    case leave                                    // reset membership (wipe to default, KEEP freq)
    case raw(String)                             // escape hatch — sent verbatim

    /// The exact console line (no trailing newline — the transport frames it).
    public var line: String {
        switch self {
        case .sendDM(let dm):
            // Unified send (D24) + plane split (D30): `send <id|0xhash> "<body>" [-a] [-e] [-t]`.
            // ⚠ 2026-07-13: a hash MUST be 0x-prefixed (bare decimal = always an id; the 8-hex autodetect is
            // GONE). -a = E2E-ack; -e = encrypt (HASH-only); -t = the TEAM plane (id ⇒ team_local_id).
            let addr: String, isHash: Bool
            switch dm.target {
            case .id(let i):    addr = String(i);        isHash = false
            case .hash(let h):  addr = "0x" + h.hex8;    isHash = true
            }
            var s = "send \(addr) \"\(Self.wireBody(dm.body))\""
            if dm.requestAck      { s += " -a" }
            if dm.encrypt, isHash { s += " -e" }   // -e only on a hash target
            if dm.teamPlane       { s += " -t" }   // the ONLY way onto the team overlay
            if dm.attachLocation  { s += " -l" }   // attach this node's position to THIS message
            return s
        case .sendChannel(let p):
            var s = "send_channel \(p.channelID) \"\(Self.wireBody(p.body))\""
            if p.teamPlane { s += " -t" }
            if p.encrypt, p.teamPlane { s += " -e" }          // -e is meaningless (and REFUSED) without -t
            if p.attachLocation, p.teamPlane, p.encrypt { s += " -l" }   // location requires the crypted flavour
            return s
        case .resolve(let r):
            return r.hard ? "resolve 0x\(r.hash.hex8) hard" : "resolve 0x\(r.hash.hex8)"   // 0x-prefixed (D30)
        case .whoami:                       return "whoami"
        case .routes:                       return "routes"
        case .status:                       return "status"
        case .config:                       return "cfg"
        case .duty:                         return "duty"
        case .limits:                       return "limits"
        case .configSet(let k, let v):      return "cfg set \(k) \(v)"
        case .lookup(let h):                return "lookup 0x\(h.hex8)"
        case .hashOf(let i):                return "hashof \(i)"
        case .pullInbox(let dm, let chan):  return "pull_inbox \(dm) \(chan)"
        case .markRead(let kind, let seq):  return "mark_read \(kind.commandToken) \(seq)"
        case .peerKey(let hex):             return "peerkey \(hex)"
        case .reqPubkey(let h, let team):   return "reqpubkey 0x\(h.hex8)\(team ? " -t" : "")"
        case .reqPubkeyTeam(let id):        return "reqpubkey \(id)"
        case .mobileStatus:                 return "mobile status"
        case .mobileGateways:               return "mobile gateways"
        case .mobileRegister:               return "mobile register"
        case .mobileRegisterScan:           return "mobile register scan"
        case .mobileRegisterTarget(let khz, let sf, let bwHz):
            // wire args: freq in MHz (float token), bw in kHz (may be fractional — 62500 Hz → "62.5")
            return "mobile register freq=\(Self.freqToken(Double(khz) / 1000)) sf=\(sf) bw=\(Self.freqToken(Double(bwHz) / 1000))"
        case .teamJoin(let id, let f, let sf, let bw, let pub, let priv):
            var s = "team 0x\(id)"                                   // 0x REQUIRED (2026-07-30 grammar)
            if let f  { s += " freq=\(Self.freqToken(f))" }          // MHz
            if let sf { s += " sf=\(sf)" }                            // 5..12 (required alongside freq)
            if let bw { s += " bw=\(Self.freqToken(bw))" }            // kHz, fractional ok
            if let pub, let priv { s += " tkpub=\(pub) tkpriv=\(priv)" }   // both or neither
            return s
        case .teamExportKey:                return "team exportkey"
        case .teamGrantKey(let h):          return "team grantkey 0x\(h.hex8)"
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
