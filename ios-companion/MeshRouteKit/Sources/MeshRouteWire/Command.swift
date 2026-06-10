// MeshRouteWire — Command.swift
//
// App→node commands, encoded as the firmware's LINE-ASCII console verbs (the format
// console_parse.cpp + fw_main.cpp service_debug actually accept today). This is the
// "line-ASCII commands + JSON pushes" contract chosen on review: zero firmware decoder
// work, the node already parses these. The transport appends the '\n'; `line` has none.
//
// Verb reference (console_parse.cpp / fw_main.cpp):
//   send <id> <text>            send_ack <id> <text>
//   sendhash <hex> <text>       sendhash_ack <hex> <text>
//   send_channel <ch> <text>    resolve <hex> [hard]
//   cfg | cfg set <k> <v> | routes | status | whoami | lookup <hex> | hashof <id>

import Foundation

/// A DM is addressed by short id (resolved now) or key_hash32 (the node resolves it).
public enum DMTarget: Hashable, Sendable {
    case id(UInt8)          // 0…254 (254 max; 255 reserved)
    case hash(KeyHash)
}

public struct SendDM: Hashable, Sendable {
    public var target: DMTarget
    public var body: String
    public var requestAck: Bool     // the *_ack verbs request the E2E ack (command.h flags E2E=0x08)
    public init(target: DMTarget, body: String, requestAck: Bool = false) {
        self.target = target; self.body = body; self.requestAck = requestAck
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
    case configSet(key: String, value: String)  // "cfg set <key> <value>"
    case lookup(KeyHash)                         // local id_bind peek (no airtime)
    case hashOf(UInt8)                           // reverse: id → hash (authoritative only)
    case raw(String)                             // escape hatch — sent verbatim

    /// The exact console line (no trailing newline — the transport frames it).
    public var line: String {
        switch self {
        case .sendDM(let dm):
            let verb: String
            switch (dm.target, dm.requestAck) {
            case (.id, false):   verb = "send"
            case (.id, true):    verb = "send_ack"
            case (.hash, false): verb = "sendhash"
            case (.hash, true):  verb = "sendhash_ack"
            }
            let addr: String
            switch dm.target {
            case .id(let i):    addr = String(i)
            case .hash(let h):  addr = h.hex8
            }
            return "\(verb) \(addr) \(dm.body)"
        case .sendChannel(let p):
            return "send_channel \(p.channelID) \(p.body)"
        case .resolve(let r):
            return r.hard ? "resolve \(r.hash.hex8) hard" : "resolve \(r.hash.hex8)"
        case .whoami:                       return "whoami"
        case .routes:                       return "routes"
        case .status:                       return "status"
        case .config:                       return "cfg"
        case .configSet(let k, let v):      return "cfg set \(k) \(v)"
        case .lookup(let h):                return "lookup \(h.hex8)"
        case .hashOf(let i):                return "hashof \(i)"
        case .raw(let s):                   return s
        }
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
