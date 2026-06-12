// MeshRouteWire — Inbound.swift
//
// Node→app messages, decoded from the firmware's newline-delimited JSON (console_json.cpp).
// Each line is one JSON object, discriminated by which top-level key is present:
//   {"ack":"queued","ctr":5,"qd":0}                         → command result
//   {"ev":"msg_recv","origin":2,"ctr":7,"body":"hi"}        → a DM was delivered to us
//   {"ev":"channel_recv","origin":2,"channel_id":3,"body":…}→ a channel message
//   {"ev":"send_acked","dst":2,"ctr":7}  / "send_failed"    → our send's fate
//   {"ev":"hash_resolved","node":2,"auth":1,"hash":135…}    → a resolve completed (hash = DECIMAL u32)
//   {"ev":"ready"|"status", …}                              → node identity/state snapshot
//   {"log":"…"}  {"err":"code","msg":"…"}                   → diagnostics
// Anything we can't classify (e.g. today's human-text node) is kept as `.raw` — never dropped.

import Foundation

// ---- command result (console_json write_ack; cmdcode_name) ----
public enum AckCode: String, Codable, Sendable {
    case queued
    case errUnknownDst      = "err_unknown_dst"
    case errTooLarge        = "err_too_large"
    case errNoGateway       = "err_no_gateway"
    case errPriorityCapped  = "err_priority_capped"
    case errNoBinding       = "err_no_binding"
    case errUnsupported     = "err_unsupported"
    case errUnprovisioned   = "err_unprovisioned"   // node_id==0: join or `cfg set node_id` first
    case errNoDataSF        = "err_no_data_sf"       // allowed_sf_bitmap==0: configure sf_list first
    case unknown                                     // forward-compat: an ack string we don't model
    public init(wire: String) { self = AckCode(rawValue: wire) ?? .unknown }
    public var isError: Bool { self != .queued }
}

public struct CommandAck: Hashable, Sendable {
    public let code: AckCode
    public let ctr: Int          // the message id this result is for
    public let queueDepth: Int
    public init(code: AckCode, ctr: Int, queueDepth: Int) {
        self.code = code; self.ctr = ctr; self.queueDepth = queueDepth
    }
}

// ---- node snapshots (console_json write_ready / write_status) ----
public struct NodeReady: Hashable, Sendable, Codable {
    public let id: Int
    public let key: KeyHash
    public let leafID: Int
    public let mode: String
    public let gateway: Bool
    public let routingSF: Int
    /// The node's inbox epoch (bumped on any store reset). Optional + nil on firmware without a durable
    /// inbox — the proposed home for the epoch the sync layer needs before deciding pull cursors (TBD wire).
    public let inboxEpoch: UInt32?
    /// Node uptime (ms) at emit — the rx_ms→wall-clock anchor (the node has no RTC). nil on older firmware.
    public let nowMs: UInt64?
    enum CodingKeys: String, CodingKey {
        case id, key, leafID = "leaf_id", mode, gateway, routingSF = "routing_sf", inboxEpoch = "inbox_epoch",
             nowMs = "now_ms"
    }
}

public struct NodeStatusSnapshot: Hashable, Sendable, Codable {
    public let id: Int
    public let key: KeyHash
    public let state: String
    public let leafID: Int
    public let gateway: Bool
    public let routingSF: Int
    enum CodingKeys: String, CodingKey { case id, key, state, leafID = "leaf_id", gateway, routingSF = "routing_sf" }
}

// ---- the decoded inbound union ----
public enum Inbound: Hashable, Sendable {
    case ack(CommandAck)
    case messageReceived(origin: Int, ctr: Int, senderHash: UInt32?, seq: UInt32?, body: String)   // seq present iff inbox enabled
    case channelReceived(origin: Int, channelID: Int, channelMsgID: UInt32?, seq: UInt32?, body: String)
    case sendAcked(dst: Int, ctr: Int)
    case sendFailed(dst: Int, ctr: Int)
    case hashResolved(node: Int, authoritative: Bool, hash: KeyHash)   // node == 0 → unresolved/timeout
    case ready(NodeReady)
    case status(NodeStatusSnapshot)
    case inboxEntry(InboxEntry)                                       // one record from a pull_inbox stream
    case inboxEnd(dmSeq: UInt32, chanSeq: UInt32, epoch: UInt32?, count: Int, nowMs: UInt64?)  // pull done: newest seqs, served epoch, #streamed, uptime anchor
    case event(type: String, fields: [String: JSONValue])             // generic / future events
    case log(String)
    case error(code: String, message: String?)
    case raw(String)                                                  // unclassified line (kept for the debug log)
}

public enum PushDecoder {
    private static let decoder = JSONDecoder()

    /// Decode one received line. Returns nil for a blank line; `.raw` for a non-JSON / unrecognized line.
    public static func decode(line: String) -> Inbound? {
        let trimmed = line.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return nil }
        guard let data = trimmed.data(using: .utf8),
              let disc = try? decoder.decode(Discriminator.self, from: data) else {
            return .raw(trimmed)   // not JSON (e.g. a human-text line from today's firmware)
        }

        if let ack = disc.ack {
            return .ack(CommandAck(code: AckCode(wire: ack),
                                   ctr: disc.ctr ?? 0,
                                   queueDepth: disc.qd ?? 0))
        }
        if let ev = disc.ev {
            return decodeEvent(ev, data: data, trimmed: trimmed)
        }
        if let log = disc.log { return .log(log) }
        if let err = disc.err { return .error(code: err, message: disc.msg) }
        return .raw(trimmed)
    }

    private static func decodeEvent(_ ev: String, data: Data, trimmed: String) -> Inbound {
        switch ev {
        case "msg_recv":
            if let m = try? decoder.decode(MsgRecv.self, from: data) {
                return .messageReceived(origin: m.origin, ctr: m.ctr, senderHash: m.sender_hash, seq: m.seq, body: m.body)
            }
        case "channel_recv":
            if let m = try? decoder.decode(ChannelRecv.self, from: data) {
                return .channelReceived(origin: m.origin, channelID: m.channel_id,
                                        channelMsgID: m.channel_msg_id, seq: m.seq, body: m.body)
            }
        case "send_acked":
            if let m = try? decoder.decode(SendFate.self, from: data) {
                return .sendAcked(dst: m.dst, ctr: m.ctr)
            }
        case "send_failed":
            if let m = try? decoder.decode(SendFate.self, from: data) {
                return .sendFailed(dst: m.dst, ctr: m.ctr)
            }
        case "hash_resolved":
            if let m = try? decoder.decode(HashResolved.self, from: data) {
                return .hashResolved(node: m.node, authoritative: m.auth != 0, hash: KeyHash(m.hash))
            }
        case "ready":
            if let m = try? decoder.decode(NodeReady.self, from: data) { return .ready(m) }
        case "status":
            if let m = try? decoder.decode(NodeStatusSnapshot.self, from: data) { return .status(m) }
        case "inbox_dm":
            if let m = try? decoder.decode(InboxDM.self, from: data) {
                return .inboxEntry(InboxEntry(seq: m.seq, kind: .dm, origin: m.origin, channelID: 0,
                                              ctr: m.ctr, senderHash: m.sender_hash, rxTimeMs: m.rx_ms, body: m.body))
            }
        case "inbox_channel":
            if let m = try? decoder.decode(InboxCh.self, from: data) {
                return .inboxEntry(InboxEntry(seq: m.seq, kind: .channel, origin: m.origin, channelID: m.channel_id,
                                              ctr: Int(m.channel_msg_id & 0xFF), channelMsgID: m.channel_msg_id,
                                              rxTimeMs: m.rx_ms, body: m.body))
            }
        case "inbox_end":
            if let m = try? decoder.decode(InboxEnd.self, from: data) {
                return .inboxEnd(dmSeq: m.dm_seq, chanSeq: m.chan_seq, epoch: m.epoch, count: m.count, nowMs: m.now_ms)
            }
        default:
            break
        }
        // Known-but-malformed, or an event type we don't model yet → keep the fields generically.
        if let obj = try? decoder.decode([String: JSONValue].self, from: data) {
            var fields = obj
            fields.removeValue(forKey: "ev")
            return .event(type: ev, fields: fields)
        }
        return .raw(trimmed)
    }

    // ---- private decode shapes (mirror the exact console_json field names) ----
    private struct Discriminator: Decodable {
        let ack: String?; let ctr: Int?; let qd: Int?
        let ev: String?
        let log: String?
        let err: String?; let msg: String?
    }
    private struct MsgRecv: Decodable { let origin: Int; let ctr: Int; let sender_hash: UInt32?; let seq: UInt32?; let body: String }
    private struct ChannelRecv: Decodable { let origin: Int; let channel_id: Int; let channel_msg_id: UInt32?; let seq: UInt32?; let body: String }
    private struct SendFate: Decodable { let dst: Int; let ctr: Int }
    private struct HashResolved: Decodable { let node: Int; let auth: Int; let hash: UInt32 }
    private struct InboxDM: Decodable { let seq: UInt32; let origin: Int; let ctr: Int; let sender_hash: UInt32?; let rx_ms: UInt64; let body: String }
    private struct InboxCh: Decodable { let seq: UInt32; let origin: Int; let channel_id: Int; let channel_msg_id: UInt32; let rx_ms: UInt64; let body: String }
    private struct InboxEnd: Decodable { let dm_seq: UInt32; let chan_seq: UInt32; let epoch: UInt32?; let count: Int; let now_ms: UInt64? }
}
