// MeshRouteCore — MockNodeLink.swift
//
// An in-memory fake node that speaks the EXACT wire contract (line-ASCII commands in, JSON
// pushes out — the same strings console_parse.cpp accepts and console_json.cpp emits). It lets
// the entire app — contacts, threads, compose, delivery state — run and be tested with no
// firmware and no BLE. Swap in the real CoreBluetooth link and nothing above changes.

import Foundation
import MeshRouteWire

public actor MockNodeLink: NodeLink {
    public nonisolated let events: AsyncStream<LinkEvent>
    private let continuation: AsyncStream<LinkEvent>.Continuation

    // A believable little node identity + a couple of known peers for `resolve`/`whoami`.
    private let selfID: Int
    private let selfHash: KeyHash
    private var knownPeers: [KeyHash: Int]          // hash → current short id
    private var sendCtr: Int = 0
    private var inboundCtr: [UInt8: Int] = [:]      // per-origin received counter
    private var connected = false

    // A small durable inbox, mirroring the firmware's two independent seq stores (DM + channel).
    private var dmRecords: [InboxEntry] = []
    private var chanRecords: [InboxEntry] = []
    private var dmSeq: UInt32 = 0
    private var chanSeq: UInt32 = 0
    private var inboxEpoch: UInt32 = 1             // bumped on a simulated store wipe (seq restarts at 1)
    private var uptimeMs: UInt64 = 1_000           // fake node uptime that climbs as records arrive
    private var mockLatE7 = 0                       // node location (degrees×1e7); 0 = unset, set via `cfg set lat/lon`
    private var mockLonE7 = 0
    // Leaf membership (R6 / D26) — starts unmanaged; `join`/`create` set it, `leave` clears it.
    private var mockLineage: UInt32 = 0
    private var mockEpoch: UInt32 = 0
    private var mockLeaf = ""
    private var mockLayer: UInt32 = 0

    public init(selfID: Int = 1,
                selfHash: KeyHash = KeyHash(0x10a0_b0c0),
                knownPeers: [KeyHash: Int] = [KeyHash(0x8a3f1c02): 2, KeyHash(0x1122aabb): 3],
                seedInbox: Bool = true) {
        self.selfID = selfID
        self.selfHash = selfHash
        self.knownPeers = knownPeers
        (self.events, self.continuation) = AsyncStream<LinkEvent>.makeStream()
        if seedInbox {
            // "Messages received while you were away" — exercised by a pull_inbox on connect.
            recordDM(origin: 2, ctr: 11, body: "(missed) you around?")
            recordDM(origin: 3, ctr: 4,  body: "(missed) ping from 3")
            recordChannel(channelID: 1, origin: 2, ctr: 5, body: "(missed) gm channel 1")
        }
    }

    public func connect() async {
        continuation.yield(.state(.connecting))
        connected = true
        continuation.yield(.state(.connected))
        // The node greets a freshly-connected client with its identity (a `ready` snapshot).
        emit(readyLine(state: "ready"))
    }

    public func disconnect() async {
        connected = false
        continuation.yield(.state(.disconnected))
    }

    public func send(line: String) async throws {
        guard connected else { throw LinkError.notConnected }
        var tokens = line.split(separator: " ", omittingEmptySubsequences: false).map(String.init)
        guard !tokens.isEmpty else { return }
        let verb = tokens.removeFirst()

        switch verb {
        case "send":                                         // unified (D24/D30): bare decimal = id; 0x… = hash; -t = team plane
            let arg = tokens.first ?? ""
            var dst = 0, ackHash: UInt32 = 0
            if let i = Int(arg), i <= 254 { dst = i }                                         // decimal id (static, or team-local under -t)
            else if let h = KeyHash(hex: arg) {
                ackHash = h.value
                if (h.value & 0xFFFF_0000) == 0x7EA0_0000 { dst = Int(h.value & 0xFF) }       // a demo teammate (Add-teammate synthetic hash)
                else { dst = knownPeers[h] ?? 0 }
            }
            ackThenDeliver(dst: dst, e2eAck: tokens.contains("-a"), ackHash: ackHash)         // -a → also simulate the recipient's E2E receipt (D25)
        case "send_channel":
            sendCtr += 1
            let ctr = sendCtr
            emit(ackLine("queued", ctr: ctr))               // channels aren't link-acked
            emit(#"{"ev":"channel_sent","ctr":\#(ctr),"relayed":true}"#)   // D29: a relay was overheard = success (demo)
        case "pull_inbox":
            let dmSince  = tokens.count > 0 ? (UInt32(tokens[0]) ?? 0) : 0
            let chanSince = tokens.count > 1 ? (UInt32(tokens[1]) ?? 0) : 0
            var count = 0
            for e in dmRecords   where e.seq > dmSince   { emit(inboxDMLine(e));   count += 1 }
            for e in chanRecords where e.seq > chanSince { emit(inboxChLine(e)); count += 1 }
            emit(inboxEndLine(count: count))
        case "mark_read":
            emit(#"{"log":"mark_read \#(tokens.joined(separator: " "))"}"#)
        case "resolve":
            if let h = tokens.first.flatMap({ KeyHash(hex: $0) }) {
                let node = knownPeers[h] ?? 0                // 0 = unresolved/timeout
                emit(hashResolvedLine(node: node, auth: node != 0, hash: h))
            }
        case "peerkey":                                      // install a verified key → derive hash = ed_pub[:4]
            if let hex = tokens.first, hex.count == 64, let h = KeyHash(hex: String(hex.prefix(8))) {
                emit(#"{"ev":"peerkey_set","hash":\#(h.value),"pinned":true}"#)
            } else {
                emit(#"{"ev":"peerkey_err","reason":"bad_hex"}"#)
            }
        case "reqpubkey":
            if let arg = tokens.first, let id = Int(arg), id <= 254 {   // BARE decimal = a team-local id (D30) → the teammate handshake
                let hash: UInt32 = 0x7EA0_0000 | UInt32(id)             // a synthetic stable teammate hash for the demo
                emit(#"{"ev":"reqpubkey_sent","hash":\#(hash)}"#)
                emit(#"{"ev":"peer_key_cached","hash":\#(hash),"pinned":false,"name":"Teammate \#(id)"}"#)
            } else if let h = tokens.first.flatMap({ KeyHash(hex: $0) }) {   // 0x form; -t ignored by the mock
                emit(#"{"ev":"reqpubkey_sent","hash":\#(h.value)}"#)
                emit(#"{"ev":"peer_key_cached","hash":\#(h.value),"pinned":false,"name":"Peer \#(knownPeers[h] ?? 0)"}"#)   // mutual answer + the S6 name
            }
        case "mobile":                                       // D30/S3: roam-screen demo data (a registered team mobile)
            switch tokens.first {
            case "status":
                emit(#"{"ev":"mobile_status","mobile":true,"registered":true,"home":222,"local":17,"epoch":6,"home_layer":4,"autoregister":true,"layer":4,"freq_khz":869525,"sf":9,"bw_hz":125000,"nets":2}"#)
            case "gateways":
                emit(#"{"ev":"mobile_gw","gw":3,"leaf":4}"#)
                emit(#"{"ev":"mobile_net","layer":7,"name":"north field","freq_khz":869525,"sf":9,"bw_hz":125000}"#)
                emit(#"{"ev":"mobile_net","layer":12,"name":"harbour","freq_khz":868100,"sf":7,"bw_hz":250000}"#)
                emit(#"{"ev":"mobile_gw_end","gws":1,"nets":2}"#)
            case "register":
                emit(#"{"ev":"mobile_reg","home":222,"local":17,"home_layer":4,"epoch":7,"registered":true}"#)
            default:
                emit(#"{"ev":"mobile_err","reason":"not_mobile"}"#)
            }
        case "nameof":                                       // D30/S6: the cached peer name as JSON
            if let h = tokens.first.flatMap({ KeyHash(hex: $0) }) {
                if let id = knownPeers[h] { emit(#"{"ev":"peer_name","hash":\#(h.value),"name":"Peer \#(id)"}"#) }
                else { emit(#"{"ev":"peer_name","hash":\#(h.value)}"#) }   // unknown → name omitted
            }
        case "whoami":
            emit(readyLine(state: "whoami"))
        case "status":
            emit(statusLine())
        case "duty":          // D27: a believable airtime-budget readout
            emit(#"{"ev":"duty","pct":42,"avail_ms":0,"enabled":true}"#)
        case "limits":        // D29: a believable anti-spam pacing snapshot (headroom → next_ms 0)
            emit(#"{"ev":"limits","win_ms":300000,"win_left_ms":142000,"n":40,"ch_sf":7,"ch_cap":8,"ch_used":2,"ch_min_ms":10000,"ch_next_ms":0,"ch_ceiling":42,"dm_min_ms":3000,"dm_next_ms":0,"duty_ms":3000,"duty_used_ms":640}"#)
        case "cfg":
            if tokens.first == "set", tokens.count >= 3 {       // `cfg set <key> <val>` → apply, then echo cfg
                let key = tokens[1], val = tokens[2]
                if key == "lat" { mockLatE7 = Int((Double(val) ?? 0) * 1e7) }
                if key == "lon" { mockLonE7 = Int((Double(val) ?? 0) * 1e7) }
            }
            emit(cfgLine())
        case "routes":
            for (i, peer) in knownPeers.sorted(by: { $0.value < $1.value }).enumerated() {
                emit(routeLine(dest: peer.value, hops: 1 + i, score: -32 - i * 8))
            }
            emit(#"{"ev":"routes_end","count":\#(knownPeers.count)}"#)
        case "join":          // join layer=<1..255> freq= bw= sf=  (key=value, 2026-07-03) → pretend we joined an existing managed leaf
            let layer = tokens.first(where: { $0.hasPrefix("layer=") }).flatMap { UInt32($0.dropFirst(6)) } ?? 0
            mockLineage = 41153; mockEpoch = 3; mockLeaf = "north field"; mockLayer = layer
            emit(configAdoptedLine())
        case "create":        // create layer=<1..255> freq= bw= sf= sf_list= duty= name="…"  (key=value) → mint a leaf
            let layer = tokens.first(where: { $0.hasPrefix("layer=") }).flatMap { UInt32($0.dropFirst(6)) } ?? 0
            let quoted = line.split(separator: "\"", omittingEmptySubsequences: false)
            mockLeaf = quoted.count >= 2 ? String(quoted[1]) : "new leaf"
            mockLineage = 50001; mockEpoch = 1; mockLayer = layer
            emit(configAdoptedLine())
        case "leave":         // wipe membership back to unmanaged
            mockLineage = 0; mockEpoch = 0; mockLeaf = ""; mockLayer = 0
            emit(configAdoptedLine())
        default:
            emit(#"{"log":"mock: unhandled '\#(verb)'"}"#)
        }
    }

    // ---- test/app hooks: inject inbound traffic as if a peer messaged us ----

    /// Simulate an inbound DM from `id`: emit the live push AND record it durably (so a later
    /// pull_inbox returns it too). Returns the (origin, ctr) the node assigned.
    @discardableResult
    public func simulateIncomingDM(fromID id: UInt8, body: String) -> (UInt8, Int) {
        let ctr = (inboundCtr[id] ?? 0) + 1
        inboundCtr[id] = ctr
        let sh = hashForID(id) ?? 0      // the sender's stable key_hash32 (0 = unknown → legacy fallback)
        recordDM(origin: id, ctr: ctr, body: body)   // record BEFORE the push so the live push carries its seq
        emit(#"{"ev":"msg_recv","origin":\#(id),"layer_id":0,"ctr":\#(ctr),"sender_hash":\#(sh),"seq":\#(dmSeq),"body":\#(jsonString(body))}"#)
        return (id, ctr)
    }

    /// Record an inbound DM but DON'T emit the live push — simulates a push dropped from the bounded ring.
    /// The next simulateIncomingDM then carries a seq that jumps the gap, exercising model B's pull-backfill.
    public func simulateDroppedIncomingDM(fromID id: UInt8, body: String) {
        let ctr = (inboundCtr[id] ?? 0) + 1
        inboundCtr[id] = ctr
        recordDM(origin: id, ctr: ctr, body: body)
    }

    /// Reverse of knownPeers (hash→id): the sender's stable key_hash32 for a short id, if known.
    private func hashForID(_ id: UInt8) -> UInt32? {
        knownPeers.first { $0.value == Int(id) }?.key.value
    }

    /// Simulate the node's flash inbox being wiped (QSPI re-flash / format-on-dirty): bump the epoch and
    /// restart both seq spaces at 0 — so the next records get seq 1,2,… below any cursor we'd advanced to.
    public func simulateStoreReset() {
        inboxEpoch += 1
        dmRecords.removeAll(); chanRecords.removeAll()
        dmSeq = 0; chanSeq = 0
    }

    public func simulateChannel(channelID: UInt8, fromID id: UInt8, teamID: String? = nil, body: String) {
        let ctr = (inboundCtr[id] ?? 0) + 1
        inboundCtr[id] = ctr
        let msgID = Self.channelMsgID(origin: id, ctr: ctr)
        recordChannel(channelID: channelID, origin: id, ctr: ctr, teamID: teamID, body: body)   // record BEFORE push (assigns chanSeq)
        let team = teamID.map { #","team_id":"\#($0)""# } ?? ""    // D30/S4: omit-when-absent, exactly like the firmware
        emit(#"{"ev":"channel_recv","origin":\#(id),"layer_id":0,"channel_id":\#(channelID),"channel_msg_id":\#(msgID),"seq":\#(chanSeq)\#(team),"body":\#(jsonString(body))}"#)
    }

    // A stand-in for the firmware's 32-bit channel_msg_id (origin<<24 | key_hash16<<8 | ctr).
    private static func channelMsgID(origin: UInt8, ctr: Int) -> UInt32 {
        (UInt32(origin) << 24) | (UInt32(ctr) & 0x00FF_FFFF)
    }

    private func recordDM(origin: UInt8, ctr: Int, body: String) {
        uptimeMs += 1_000; dmSeq += 1
        dmRecords.append(InboxEntry(seq: dmSeq, kind: .dm, origin: Int(origin), channelID: 0,
                                    ctr: ctr, senderHash: hashForID(origin), rxTimeMs: uptimeMs, body: body))
    }
    private func recordChannel(channelID: UInt8, origin: UInt8, ctr: Int, teamID: String? = nil, body: String) {
        uptimeMs += 1_000; chanSeq += 1
        chanRecords.append(InboxEntry(seq: chanSeq, kind: .channel, origin: Int(origin), channelID: Int(channelID),
                                      ctr: ctr, channelMsgID: Self.channelMsgID(origin: origin, ctr: ctr),
                                      teamID: teamID, rxTimeMs: uptimeMs, body: body))
    }

    // ---- internals ----

    private func ackThenDeliver(dst: Int, e2eAck: Bool = false, ackHash: UInt32 = 0) {
        sendCtr += 1
        let ctr = sendCtr
        emit(ackLine(dst == 0 ? "err_unknown_dst" : "queued", ctr: ctr))
        guard dst != 0 else { return }
        emit(#"{"ev":"send_acked","dst":\#(dst),"ctr":\#(ctr)}"#)
        if e2eAck {   // -a: the recipient confirms end-to-end → the live receipt twin (D25). sender_hash set for a hash-addressed send.
            emit(#"{"ev":"e2e_acked","origin":\#(dst),"ctr":\#(ctr),"sender_hash":\#(ackHash)}"#)
        }
    }

    private func emit(_ jsonLine: String) { continuation.yield(.line(jsonLine)) }

    private func ackLine(_ code: String, ctr: Int, qd: Int = 0) -> String {
        #"{"ack":"\#(code)","ctr":\#(ctr),"qd":\#(qd)}"#
    }
    private func hashResolvedLine(node: Int, auth: Bool, hash: KeyHash) -> String {
        #"{"ev":"hash_resolved","node":\#(node),"auth":\#(auth ? 1 : 0),"hash":\#(hash.value)}"#
    }
    private func readyLine(state: String) -> String {
        let synced = (mockLineage == 0 || mockEpoch > 0) ? "true" : "false"
        return #"{"ev":"ready","id":\#(selfID),"key":"\#(selfHash.hex8)","name":"Mock \#(selfID)","pubkey":"\#(selfHash.hex8)\#(String(repeating: "0", count: 56))","leaf_id":0,"mode":"\#(state)","gateway":false,"routing_sf":7,"inbox_epoch":\#(inboxEpoch),"now_ms":\#(uptimeMs),"lineage":\#(mockLineage),"epoch":\#(mockEpoch)\#(leafField),"layer":\#(mockLayer),"synced":\#(synced),"duty_pct":42,"duty_avail_ms":0,"mobile":true,"mobile_registered":true,"mobile_home":222,"mobile_local":17,"mobile_home_layer":4,"team":"cccc0001","team_local":1}"#
        // ^ the mock plays a REGISTERED TEAM MOBILE (D30) so the roam screen, team chips, Add-teammate,
        //   and team-plane sends are all demoable in the simulator.
    }
    private var leafField: String { (mockLineage != 0 && !mockLeaf.isEmpty) ? #","leaf":\#(jsonString(mockLeaf))"# : "" }
    private func configAdoptedLine() -> String {
        #"{"ev":"config_adopted","lineage":\#(mockLineage),"epoch":\#(mockEpoch)\#(leafField),"layer":\#(mockLayer)}"#
    }
    private func statusLine() -> String {
        #"{"ev":"status","id":\#(selfID),"key":"\#(selfHash.hex8)","state":"operating","leaf_id":0,"gateway":false,"routing_sf":7,"uptime_ms":\#(uptimeMs),"duty_ms":1240,"txq":0,"txdrop":0,"rx":42,"tx":17,"routes":\#(knownPeers.count),"pending":false,"lbt":true,"batt_mv":4050}"#
    }
    private func cfgLine() -> String {
        #"{"ev":"cfg","node_id":\#(selfID),"freq_hz":869462500,"routing_sf":7,"sf_list":"7,12","bw_hz":125000,"cr":5,"tx_power":22,"duty_x1000":100,"lbt":true,"beacon_ms":900000,"hop_cap":16,"leaf_id":0,"gateway":false,"mobile":false,"ble_mode":"on","ble_period":15,"ble_pin":123456,"lat_e7":\#(mockLatE7),"lon_e7":\#(mockLonE7)}"#
    }
    private func routeLine(dest: Int, hops: Int, score: Int) -> String {
        #"{"ev":"route","dest":\#(dest),"next":\#(dest),"hops":\#(hops),"score":\#(score),"gw":false,"layer":0,"age_ms":4200,"cand":1}"#
    }
    private func inboxDMLine(_ e: InboxEntry) -> String {
        #"{"ev":"inbox_dm","seq":\#(e.seq),"origin":\#(e.origin),"layer_id":\#(e.layerID ?? 0),"ctr":\#(e.ctr),"sender_hash":\#(e.senderHash ?? 0),"rx_ms":\#(e.rxTimeMs),"body":\#(jsonString(e.body))}"#
    }
    private func inboxChLine(_ e: InboxEntry) -> String {
        let team = e.teamID.map { #","team_id":"\#($0)""# } ?? ""   // D30/S5: durable team tag, omit-when-absent
        return #"{"ev":"inbox_channel","seq":\#(e.seq),"origin":\#(e.origin),"layer_id":\#(e.layerID ?? 0),"channel_id":\#(e.channelID),"channel_msg_id":\#(e.channelMsgID ?? 0)\#(team),"rx_ms":\#(e.rxTimeMs),"body":\#(jsonString(e.body))}"#
    }
    private func inboxEndLine(count: Int) -> String {
        #"{"ev":"inbox_end","dm_seq":\#(dmSeq),"chan_seq":\#(chanSeq),"epoch":\#(inboxEpoch),"count":\#(count),"now_ms":\#(uptimeMs)}"#
    }
    /// Minimal JSON string escaping for injected bodies (matches console_json's escapes).
    private func jsonString(_ s: String) -> String {
        var out = "\""
        for c in s {
            switch c {
            case "\"": out += "\\\""
            case "\\": out += "\\\\"
            case "\n": out += "\\n"
            case "\r": out += "\\r"
            case "\t": out += "\\t"
            default: out.append(c)
            }
        }
        out += "\""
        return out
    }
}
