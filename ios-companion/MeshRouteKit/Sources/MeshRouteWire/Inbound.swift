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
    public let ctr: Int          // the message id this result is for (correlates the async send_acked/send_failed)
    public let queueDepth: Int
    // Send-handle (firmware "dh"/"lp", D19): the hash/layer-addressed destination of this send. nil when 0
    // (a plain `send <id>` / same-layer). The app records ctr → (dstHash, layerPath) to display/re-send a
    // cross-layer message; the async outcomes still correlate by ctr.
    public let dstHash: UInt32?
    public let layerPath: UInt32?    // cross-layer path packed MSB-first ((2<<8)|3 = 0x0203 for [2,3]); nil = same-layer
    public init(code: AckCode, ctr: Int, queueDepth: Int, dstHash: UInt32? = nil, layerPath: UInt32? = nil) {
        self.code = code; self.ctr = ctr; self.queueDepth = queueDepth
        self.dstHash = dstHash; self.layerPath = layerPath
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
    /// The node's own /mrid name (`cfg set name`) — app-level identity label (§1.3). Omitted when unset.
    public let name: String?
    /// The node's own ed25519 public key (64 hex) — so `MyCardView` can put `p` in the QR (E2E, 2026-06-16).
    /// `key_hash32` alone can't seal a DM; the full pubkey is the sealing key. nil on pre-E2E firmware.
    public let pubkey: String?
    // ---- leaf membership (R6 / D26) — all optional; absent on pre-R6 firmware ----
    public let lineage: Int?      // lineage_id; 0 = unmanaged/standalone; nil = pre-R6 firmware
    public let configEpoch: Int?  // config_epoch (wire key "epoch" — distinct from inbox_epoch)
    public let leaf: String?      // leaf_name (omitted when unset)
    public let layer: Int?        // the 1..255 layer id (⚠ interim: firmware still sends the wire leaf nibble)
    public let synced: Bool?      // (lineage==0 || config_epoch>0)
    public let dutyPct: Int?      // D27: airtime budget used 0..100 (100 = silent); nil on older firmware
    public let dutyAvailMs: Int?  // ms until airtime frees (0 = can TX now)
    // ---- mobile + team (D30 / S1) — ALL omit-when-inactive (a static, teamless node omits the whole block) ----
    public let mobile: Bool?             // true ⇒ this node is a mobile (roaming endpoint)
    public let mobileRegistered: Bool?
    public let mobileHome: Int?          // current home node id (0 = unregistered)
    public let mobileLocal: Int?         // the home-assigned local id (never used app-side for addressing)
    public let mobileHomeLayer: Int?     // present only when registered
    public let hosting: Int?             // static host: mobiles registered to US (omit when 0)
    public let team: String?             // team_id as a lowercase hex string (like `key`); omit = no team
    public let teamLocal: Int?           // our OWN id on the team overlay — teammates address us by it
    enum CodingKeys: String, CodingKey {
        case id, key, leafID = "leaf_id", mode, gateway, routingSF = "routing_sf", inboxEpoch = "inbox_epoch",
             nowMs = "now_ms", name, pubkey,
             lineage, configEpoch = "epoch", leaf, layer, synced,
             dutyPct = "duty_pct", dutyAvailMs = "duty_avail_ms",
             mobile, mobileRegistered = "mobile_registered", mobileHome = "mobile_home",
             mobileLocal = "mobile_local", mobileHomeLayer = "mobile_home_layer", hosting,
             team, teamLocal = "team_local"
    }
}

public struct NodeStatusSnapshot: Hashable, Sendable, Codable {
    public let id: Int
    public let key: KeyHash
    public let state: String
    public let leafID: Int
    public let gateway: Bool
    public let routingSF: Int
    // Runtime telemetry (Theme D) — all optional so an older node's terse status still decodes.
    public let uptimeMs: UInt64?
    public let dutyMs: UInt32?
    public let txq: Int?
    public let txdrop: Int?
    public let rx: Int?
    public let tx: Int?
    public let routes: Int?
    public let pending: Bool?
    public let lbt: Bool?
    public let battMv: Int?          // omitted by the node when no battery reader is wired
    enum CodingKeys: String, CodingKey {
        case id, key, state, leafID = "leaf_id", gateway, routingSF = "routing_sf",
             uptimeMs = "uptime_ms", dutyMs = "duty_ms", txq, txdrop, rx, tx, routes, pending, lbt,
             battMv = "batt_mv"
    }
}

/// The advisory anti-spam pacing snapshot (`limits` query, D29). The app paces sends against it; the ACTUAL
/// outcome (`send_blocked` / `send_failed` / `channel_sent`) is authoritative. `*_next_ms` = ms until allowed.
public struct LimitsInfo: Hashable, Sendable, Codable {
    public let winMs: Int          // the anti-spam window (≈5 min)
    public let winLeftMs: Int
    public let n: Int              // mesh size the per-origin channel cap divides by
    public let chSF: Int
    public let chCap: Int          // this origin's per-window channel cap
    public let chUsed: Int
    public let chMinMs: Int        // channel burst floor (leaf-configured)
    public let chNextMs: Int       // ms until a channel post is allowed (0 = now)
    public let chCeiling: Int
    public let dmMinMs: Int        // own-DM burst floor (leaf-configured)
    public let dmNextMs: Int       // ms until an own DM is allowed
    public let dutyMs: Int         // 5-min channel-duty budget (0 = disabled)
    public let dutyUsedMs: Int
    enum CodingKeys: String, CodingKey {
        case winMs = "win_ms", winLeftMs = "win_left_ms", n, chSF = "ch_sf", chCap = "ch_cap", chUsed = "ch_used",
             chMinMs = "ch_min_ms", chNextMs = "ch_next_ms", chCeiling = "ch_ceiling",
             dmMinMs = "dm_min_ms", dmNextMs = "dm_next_ms", dutyMs = "duty_ms", dutyUsedMs = "duty_used_ms"
    }
}

/// The `mobile status` answer (D30/S3) — this mobile's registration + live PHY + learned-networks count.
/// Integer kHz/Hz on the wire (no floats); `homeLayer` present only when registered.
public struct MobileStatusInfo: Hashable, Sendable, Codable {
    public let mobile: Bool
    public let registered: Bool
    public let home: Int            // 0 when unregistered
    public let local: Int
    public let epoch: Int
    public let homeLayer: Int?
    public let autoregister: Bool
    public let layer: Int           // the live PHY layer
    public let freqKHz: Int
    public let sf: Int
    public let bwHz: Int
    public let nets: Int            // learned networks (rows come from `mobile gateways`)
    enum CodingKeys: String, CodingKey {
        case mobile, registered, home, local, epoch, homeLayer = "home_layer", autoregister,
             layer, freqKHz = "freq_khz", sf, bwHz = "bw_hz", nets
    }
}

/// One route-table row (a `{"ev":"route",…}` line from the `routes` stream).
public struct RouteInfo: Hashable, Sendable, Codable {
    public let dest: Int
    public let next: Int
    public let hops: Int
    public let score: Int            // Q4 dB (÷16 for dB)
    public let gw: Bool
    public let leaf: Int             // the route's learned leaf nibble (layer & 0x0F)
    public let ageMs: UInt32
    public let cand: Int
    enum CodingKeys: String, CodingKey { case dest, next, hops, score, gw, leaf, ageMs = "age_ms", cand }
}

/// The node config snapshot (the `{"ev":"cfg",…}` object — read-only display v1).
public struct NodeConfigInfo: Hashable, Sendable, Codable {
    public let nodeID: Int
    public let freqHz: UInt32
    public let routingSF: Int
    public let sfList: String        // "7,12"
    public let bwHz: UInt32
    public let cr: Int
    public let txPower: Int
    public let dutyX1000: Int        // duty_cycle×1000 (no float on the wire); dutyPercent below
    public let lbt: Bool
    public let beaconMs: UInt32
    public let hopCap: Int
    public let leafID: Int
    public let gateway: Bool
    public let mobile: Bool
    public let bleMode: String
    public let blePeriod: Int
    public let blePin: UInt32
    public let latE7: Int?       // node location, degrees × 1e7 (nil on older firmware; 0 = unset)
    public let lonE7: Int?
    public let e2eDm: Bool?      // the node's default DM-encrypt toggle (`cfg set e2e_dm`); nil until firmware emits it
    public let mobileAutoregister: Bool?   // D30/S1: 1 = node self-registers/roams; 0 = the APP drives it (nil = pre-mobile fw)
    public let teamID: String?             // D30/S1: team_id hex string — ALWAYS present in cfg ("00000000" = unset)
    enum CodingKeys: String, CodingKey {
        case nodeID = "node_id", freqHz = "freq_hz", routingSF = "routing_sf", sfList = "sf_list",
             bwHz = "bw_hz", cr, txPower = "tx_power", dutyX1000 = "duty_x1000", lbt, beaconMs = "beacon_ms",
             hopCap = "hop_cap", leafID = "leaf_id", gateway, mobile, bleMode = "ble_mode",
             blePeriod = "ble_period", blePin = "ble_pin", latE7 = "lat_e7", lonE7 = "lon_e7", e2eDm = "e2e_dm",
             mobileAutoregister = "mobile_autoregister", teamID = "team_id"
    }
    public var freqMHz: Double { Double(freqHz) / 1_000_000 }
    public var dutyPercent: Double { Double(dutyX1000) / 10 }   // 100 → 10.0 %
    public var hasPosition: Bool { (latE7 ?? 0) != 0 || (lonE7 ?? 0) != 0 }
    public var latitude: Double { Double(latE7 ?? 0) / 1e7 }
    public var longitude: Double { Double(lonE7 ?? 0) / 1e7 }
}

// ---- the decoded inbound union ----
public enum Inbound: Hashable, Sendable {
    case ack(CommandAck)
    case messageReceived(origin: Int, ctr: Int, senderHash: UInt32?, seq: UInt32?, layerID: Int?, crypted: Bool?, body: String)   // seq iff inbox; layerID = receiving layer (D12); crypted = the DATA CRYPTED flag (E2E, firmware-pending)
    case channelReceived(origin: Int, channelID: Int, channelMsgID: UInt32?, seq: UInt32?, layerID: Int?, teamID: String?, body: String)   // teamID (hex string, D30/S4): team-scoped group chat; nil = a leaf channel
    case sendAcked(dst: Int, ctr: Int)
    case sendFailed(dst: Int, ctr: Int, reason: String?)              // reason: no_pubkey · no_identity · too_large · bad_rng · no_route · joining (E2E 2026-06-16)
    case e2eAcked(dst: Int, ctr: Int, senderHash: UInt32?)            // live E2E delivery RECEIPT (D25): mark the OUTBOX msg delivered; dst=the node that confirmed; NOT an inbound DM
    case sendBlocked(kind: String, reason: String, nextMs: Int)       // D29 anti-spam: this node's cap/floor blocked a send PRE-TX → back off + retry after nextMs (0 = cap/duty, unknown)
    case channelSent(ctr: Int, relayed: Bool, reason: String?)        // D29: own channel-post outcome — relayed:true = a relay was overheard (success); false = no relay (fail)
    case limits(LimitsInfo)                                           // D29: the advisory pacing snapshot (the app paces against it; the outcome pushes are authoritative)
    case hashResolved(node: Int, authoritative: Bool, hash: KeyHash)   // node == 0 → unresolved/timeout
    // E2E peer-key provisioning (2026-06-16): results of `peerkey`/`reqpubkey` + a key becoming available.
    case peerKeySet(hash: KeyHash, pinned: Bool)                       // a scanned card's pubkey installed
    case peerKeyError(reason: String)                                 // bad_hex | hash_mismatch — not installed
    case reqPubkeySent(hash: KeyHash)                                 // an on-air key request went out
    case peerKeyCached(hash: KeyHash, pinned: Bool, name: String?)    // a key arrived → "secure send ready, resend"; name (D30/S6) = the peer's self-reported name, captured at cache time
    case peerName(hash: KeyHash, name: String?)                       // `nameof 0x<hash>` answer (D30/S6); name omitted when unknown
    case ready(NodeReady)
    case status(NodeStatusSnapshot)
    case route(RouteInfo)                                            // one row from the `routes` stream
    case routesEnd(count: Int)                                       // routes stream terminator
    case cfg(NodeConfigInfo)                                         // node config snapshot
    case configAdopted(lineage: Int, epoch: Int, leaf: String?, layer: Int?)   // R6/D26: leaf-config adopted/updated → live membership chip
    case joinRefused(reason: String, theirVer: Int?, myVer: Int?)              // R6/D26: can't join (wire_version → update fw; leaf_full)
    case duty(pct: Int, availMs: Int, enabled: Bool)                          // D27: airtime-budget readout (0..100; 100 = silent; enabled=false ⇒ unlimited)
    // ---- mobile + team pushes/queries (D30 / S2+S3) ----
    case mobileReg(home: Int, local: Int, homeLayer: Int?, epoch: Int?, registered: Bool)   // register / roam / home-loss — the connectivity chip
    case teamReg(team: String, local: Int)                                    // team-DAD id adopted / conflict re-pick
    case mobileStatus(MobileStatusInfo)                                       // `mobile status` answer
    case mobileGateway(gw: Int, leaf: Int)                                    // one row of the `mobile gateways` stream
    case mobileNet(layer: Int, name: String?, freqKHz: Int, sf: Int, bwHz: Int)   // a learned network row (roam-UI target)
    case mobileGatewaysEnd(gws: Int, nets: Int)                               // gateways-stream terminator
    case mobileError(reason: String)                                          // e.g. "not_mobile" — a `mobile` verb on a static node
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
                                   queueDepth: disc.qd ?? 0,
                                   dstHash: (disc.dh ?? 0) != 0 ? disc.dh : nil,    // 0 ⇒ send <id> / same-layer
                                   layerPath: (disc.lp ?? 0) != 0 ? disc.lp : nil))
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
                return .messageReceived(origin: m.origin, ctr: m.ctr, senderHash: m.sender_hash, seq: m.seq,
                                        layerID: m.layer_id, crypted: m.enc, body: m.body)
            }
        case "channel_recv":
            if let m = try? decoder.decode(ChannelRecv.self, from: data) {
                return .channelReceived(origin: m.origin, channelID: m.channel_id,
                                        channelMsgID: m.channel_msg_id, seq: m.seq, layerID: m.layer_id,
                                        teamID: m.team_id, body: m.body)
            }
        case "send_acked":
            if let m = try? decoder.decode(SendFate.self, from: data) {
                return .sendAcked(dst: m.dst, ctr: m.ctr)
            }
        case "send_failed":
            if let m = try? decoder.decode(SendFate.self, from: data) {
                return .sendFailed(dst: m.dst, ctr: m.ctr, reason: m.reason)
            }
        case "e2e_acked":                                              // D25: the live delivery-receipt twin
            if let m = try? decoder.decode(E2eAck.self, from: data) {
                return .e2eAcked(dst: m.origin, ctr: m.ctr, senderHash: (m.sender_hash ?? 0) != 0 ? m.sender_hash : nil)
            }
        case "send_blocked":                                           // D29 anti-spam back-off
            if let m = try? decoder.decode(SendBlocked.self, from: data) {
                return .sendBlocked(kind: m.kind, reason: m.reason, nextMs: m.next_ms)
            }
        case "channel_sent":                                           // D29 own channel-post outcome
            if let m = try? decoder.decode(ChannelSent.self, from: data) {
                return .channelSent(ctr: m.ctr, relayed: m.relayed, reason: m.reason)
            }
        case "limits":
            if let m = try? decoder.decode(LimitsInfo.self, from: data) { return .limits(m) }
        case "hash_resolved":
            if let m = try? decoder.decode(HashResolved.self, from: data) {
                return .hashResolved(node: m.node, authoritative: m.auth != 0, hash: KeyHash(m.hash))
            }
        case "peerkey_set":
            if let m = try? decoder.decode(PeerKeyEvent.self, from: data) {
                return .peerKeySet(hash: KeyHash(m.hash), pinned: m.pinned ?? true)
            }
        case "peerkey_err":
            if let m = try? decoder.decode(ReasonEvent.self, from: data) { return .peerKeyError(reason: m.reason) }
        case "reqpubkey_sent":
            if let m = try? decoder.decode(PeerKeyEvent.self, from: data) { return .reqPubkeySent(hash: KeyHash(m.hash)) }
        case "peer_key_cached":
            if let m = try? decoder.decode(PeerKeyEvent.self, from: data) {
                return .peerKeyCached(hash: KeyHash(m.hash), pinned: m.pinned ?? false, name: m.name)
            }
        case "peer_name":                                              // D30/S6: the `nameof` answer
            if let m = try? decoder.decode(PeerKeyEvent.self, from: data) {
                return .peerName(hash: KeyHash(m.hash), name: m.name)
            }
        case "ready":
            if let m = try? decoder.decode(NodeReady.self, from: data) { return .ready(m) }
        case "status":
            if let m = try? decoder.decode(NodeStatusSnapshot.self, from: data) { return .status(m) }
        case "route":
            if let m = try? decoder.decode(RouteInfo.self, from: data) { return .route(m) }
        case "routes_end":
            if let m = try? decoder.decode(RoutesEnd.self, from: data) { return .routesEnd(count: m.count) }
        case "cfg":
            if let m = try? decoder.decode(NodeConfigInfo.self, from: data) { return .cfg(m) }
        case "config_adopted":
            if let m = try? decoder.decode(ConfigAdopted.self, from: data) {
                return .configAdopted(lineage: m.lineage, epoch: m.epoch, leaf: m.leaf, layer: m.layer)
            }
        case "join_refused":
            if let m = try? decoder.decode(JoinRefused.self, from: data) {
                return .joinRefused(reason: m.reason, theirVer: m.their_ver, myVer: m.my_ver)
            }
        case "duty":
            if let m = try? decoder.decode(Duty.self, from: data) {
                return .duty(pct: m.pct, availMs: m.avail_ms, enabled: m.enabled)
            }
        case "mobile_reg":                                             // D30/S2: register / roam / home-loss
            if let m = try? decoder.decode(MobileReg.self, from: data) {
                return .mobileReg(home: m.home, local: m.local, homeLayer: m.home_layer, epoch: m.epoch,
                                  registered: m.registered)
            }
        case "team_reg":                                               // D30/S2: team-DAD
            if let m = try? decoder.decode(TeamReg.self, from: data) {
                return .teamReg(team: m.team, local: m.local)
            }
        case "mobile_status":
            if let m = try? decoder.decode(MobileStatusInfo.self, from: data) { return .mobileStatus(m) }
        case "mobile_gw":
            if let m = try? decoder.decode(MobileGw.self, from: data) { return .mobileGateway(gw: m.gw, leaf: m.leaf) }
        case "mobile_net":
            if let m = try? decoder.decode(MobileNet.self, from: data) {
                return .mobileNet(layer: m.layer, name: m.name, freqKHz: m.freq_khz, sf: m.sf, bwHz: m.bw_hz)
            }
        case "mobile_gw_end":
            if let m = try? decoder.decode(MobileGwEnd.self, from: data) { return .mobileGatewaysEnd(gws: m.gws, nets: m.nets) }
        case "mobile_err":
            if let m = try? decoder.decode(ReasonEvent.self, from: data) { return .mobileError(reason: m.reason) }
        case "inbox_dm":
            if let m = try? decoder.decode(InboxDM.self, from: data) {
                let receipt = (m.type == "e2e_ack")     // a delivery RECEIPT rides the DM seq-cursor — NOT a message (D25)
                return .inboxEntry(InboxEntry(seq: m.seq, kind: .dm, origin: m.origin, channelID: 0,
                                              ctr: m.ctr, senderHash: m.sender_hash, layerID: m.layer_id,
                                              crypted: m.enc, isReceipt: receipt, rxTimeMs: m.rx_ms,
                                              body: receipt ? "" : m.body))
            }
        case "inbox_channel":
            if let m = try? decoder.decode(InboxCh.self, from: data) {
                return .inboxEntry(InboxEntry(seq: m.seq, kind: .channel, origin: m.origin, channelID: m.channel_id,
                                              ctr: Int(m.channel_msg_id & 0xFF), channelMsgID: m.channel_msg_id,
                                              layerID: m.layer_id, teamID: m.team_id, rxTimeMs: m.rx_ms, body: m.body))
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
        let ack: String?; let ctr: Int?; let qd: Int?; let dh: UInt32?; let lp: UInt32?   // dh/lp = send-handle (D19)
        let ev: String?
        let log: String?
        let err: String?; let msg: String?
    }
    private struct MsgRecv: Decodable { let origin: Int; let ctr: Int; let sender_hash: UInt32?; let seq: UInt32?; let layer_id: Int?; let enc: Bool?; let body: String }   // enc = the wire CRYPTED indicator
    private struct ChannelRecv: Decodable { let origin: Int; let channel_id: Int; let channel_msg_id: UInt32?; let seq: UInt32?; let layer_id: Int?; let team_id: String?; let body: String }   // team_id (hex string) ⇒ team-scoped (D30/S4)
    private struct SendFate: Decodable { let dst: Int; let ctr: Int; let reason: String? }   // reason on send_failed (E2E)
    private struct HashResolved: Decodable { let node: Int; let auth: Int; let hash: UInt32 }
    private struct PeerKeyEvent: Decodable { let hash: UInt32; let pinned: Bool?; let name: String? }   // peerkey_set / reqpubkey_sent / peer_key_cached / peer_name (name = D30/S6)
    private struct MobileReg: Decodable { let home: Int; let local: Int; let home_layer: Int?; let epoch: Int?; let registered: Bool }
    private struct TeamReg: Decodable { let team: String; let local: Int }
    private struct MobileGw: Decodable { let gw: Int; let leaf: Int }
    private struct MobileNet: Decodable { let layer: Int; let name: String?; let freq_khz: Int; let sf: Int; let bw_hz: Int }
    private struct MobileGwEnd: Decodable { let gws: Int; let nets: Int }
    private struct ReasonEvent: Decodable { let reason: String }                              // peerkey_err
    private struct InboxDM: Decodable { let seq: UInt32; let origin: Int; let ctr: Int; let sender_hash: UInt32?; let layer_id: Int?; let enc: Bool?; let type: String?; let rx_ms: UInt64; let body: String }   // enc = CRYPTED indicator; type "e2e_ack" = a delivery receipt (D25)
    private struct E2eAck: Decodable { let origin: Int; let ctr: Int; let sender_hash: UInt32? }   // live e2e_acked: origin = the dst that CONFIRMED delivery
    private struct SendBlocked: Decodable { let kind: String; let reason: String; let next_ms: Int }   // D29: kind ∈ channel|dm ; reason ∈ cap|min_interval
    private struct ChannelSent: Decodable { let ctr: Int; let relayed: Bool; let reason: String? }      // D29: own channel-post outcome
    private struct InboxCh: Decodable { let seq: UInt32; let origin: Int; let channel_id: Int; let channel_msg_id: UInt32; let layer_id: Int?; let team_id: String?; let rx_ms: UInt64; let body: String }   // team_id ⇒ team-scoped (D30/S5)
    private struct InboxEnd: Decodable { let dm_seq: UInt32; let chan_seq: UInt32; let epoch: UInt32?; let count: Int; let now_ms: UInt64? }
    private struct RoutesEnd: Decodable { let count: Int }
    private struct ConfigAdopted: Decodable { let lineage: Int; let epoch: Int; let leaf: String?; let layer: Int? }   // R6 membership update
    private struct JoinRefused: Decodable { let reason: String; let their_ver: Int?; let my_ver: Int? }                // wire_version carries the versions
    private struct Duty: Decodable { let pct: Int; let avail_ms: Int; let enabled: Bool }                              // D27 airtime budget
}
