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

    public init(selfID: Int = 1,
                selfHash: KeyHash = KeyHash(0x10a0_b0c0),
                knownPeers: [KeyHash: Int] = [KeyHash(0x8a3f1c02): 2, KeyHash(0x1122aabb): 3]) {
        self.selfID = selfID
        self.selfHash = selfHash
        self.knownPeers = knownPeers
        (self.events, self.continuation) = AsyncStream<LinkEvent>.makeStream()
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
        case "send", "send_ack":
            let id = tokens.first.flatMap { Int($0) } ?? 0
            ackThenDeliver(dst: id)
        case "sendhash", "sendhash_ack":
            let id = tokens.first.flatMap { KeyHash(hex: $0) }.flatMap { knownPeers[$0] } ?? 0
            ackThenDeliver(dst: id)
        case "send_channel":
            sendCtr += 1
            emit(ackLine("queued", ctr: sendCtr))           // channels aren't link-acked
        case "resolve":
            if let h = tokens.first.flatMap({ KeyHash(hex: $0) }) {
                let node = knownPeers[h] ?? 0                // 0 = unresolved/timeout
                emit(hashResolvedLine(node: node, auth: node != 0, hash: h))
            }
        case "whoami":
            emit(readyLine(state: "whoami"))
        case "status":
            emit(statusLine())
        case "cfg", "routes":
            emit(#"{"log":"\#(verb): (mock node — not modeled)"}"#)
        default:
            emit(#"{"log":"mock: unhandled '\#(verb)'"}"#)
        }
    }

    // ---- test/app hooks: inject inbound traffic as if a peer messaged us ----

    /// Simulate an inbound DM from `id`. Returns the (origin, ctr) the node assigned.
    @discardableResult
    public func simulateIncomingDM(fromID id: UInt8, body: String) -> (UInt8, Int) {
        let ctr = (inboundCtr[id] ?? 0) + 1
        inboundCtr[id] = ctr
        emit(#"{"ev":"msg_recv","origin":\#(id),"ctr":\#(ctr),"body":\#(jsonString(body))}"#)
        return (id, ctr)
    }

    public func simulateChannel(channelID: UInt8, fromID id: UInt8, body: String) {
        emit(#"{"ev":"channel_recv","origin":\#(id),"channel_id":\#(channelID),"body":\#(jsonString(body))}"#)
    }

    // ---- internals ----

    private func ackThenDeliver(dst: Int) {
        sendCtr += 1
        let ctr = sendCtr
        emit(ackLine(dst == 0 ? "err_unknown_dst" : "queued", ctr: ctr))
        if dst != 0 { emit(#"{"ev":"send_acked","dst":\#(dst),"ctr":\#(ctr)}"#) }
    }

    private func emit(_ jsonLine: String) { continuation.yield(.line(jsonLine)) }

    private func ackLine(_ code: String, ctr: Int, qd: Int = 0) -> String {
        #"{"ack":"\#(code)","ctr":\#(ctr),"qd":\#(qd)}"#
    }
    private func hashResolvedLine(node: Int, auth: Bool, hash: KeyHash) -> String {
        #"{"ev":"hash_resolved","node":\#(node),"auth":\#(auth ? 1 : 0),"hash":\#(hash.value)}"#
    }
    private func readyLine(state: String) -> String {
        #"{"ev":"ready","id":\#(selfID),"key":"\#(selfHash.hex8)","leaf_id":0,"mode":"\#(state)","gateway":false,"routing_sf":7}"#
    }
    private func statusLine() -> String {
        #"{"ev":"status","id":\#(selfID),"key":"\#(selfHash.hex8)","state":"up","leaf_id":0,"gateway":false,"routing_sf":7}"#
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
