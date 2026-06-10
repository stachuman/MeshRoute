// MeshRouteCompanion — AppModel.swift
//
// The controller: owns the NodeSession, consumes its events on the main actor, writes inbound
// messages into SwiftData (applying the dedup-by-(origin,ctr) rule that ConversationStore proves),
// and drives sending. Views read the data via @Query and read live state from here.

import Foundation
import SwiftData
import Observation
import MeshRouteWire
import MeshRouteCore

@MainActor
@Observable
final class AppModel {
    enum Backend: String, CaseIterable, Identifiable { case mock, ble; var id: String { rawValue } }

    let modelContainer: ModelContainer
    private var context: ModelContext { modelContainer.mainContext }

    // Live (non-persisted) state the UI observes.
    private(set) var linkState: LinkState = .disconnected
    private(set) var nodeIdentity: NodeReady?
    private(set) var consoleLog: [ConsoleLine] = []
    var backend: Backend = defaultBackend

    private var session: NodeSession?
    private var pump: Task<Void, Never>?
    private var activeMockLink: MockNodeLink?     // kept when running the mock, for the demo button
    private var pendingOutgoing: [UUID] = []     // FIFO: outgoing message ids awaiting their queued ack

    init(modelContainer: ModelContainer) {
        self.modelContainer = modelContainer
    }

    static var defaultBackend: Backend {
        #if targetEnvironment(simulator)
        return .mock          // CoreBluetooth has no hardware in the simulator
        #else
        return .ble
        #endif
    }

    var isConnected: Bool { if case .connected = linkState { return true } else { return false } }

    var statusText: String {
        switch linkState {
        case .disconnected:     return "Disconnected"
        case .scanning:         return "Scanning…"
        case .waitingForWindow: return "Waiting for the node's window…"
        case .connecting:       return "Connecting…"
        case .pairing:          return "Pairing…"
        case .connected:        return nodeIdentity.map { "Connected · node \($0.id)" } ?? "Connected"
        case .failed(let m):    return "Failed: \(m)"
        }
    }

    // ---- connection ----

    func connect() {
        disconnectInternal()
        let link: NodeLink
        if backend == .ble {
            link = BLENodeLink()
        } else {
            let mock = MockNodeLink(); activeMockLink = mock; link = mock
        }
        let session = NodeSession(link: link)
        self.session = session
        pump = Task { [weak self] in
            // This Task inherits @MainActor from the enclosing context, so handle() is a same-actor call.
            for await event in session.events { self?.handle(event) }
        }
        Task { await session.connect() }
    }

    func disconnect() {
        let s = session
        Task { await s?.disconnect() }
        disconnectInternal()
        linkState = .disconnected
    }

    private func disconnectInternal() {
        pump?.cancel(); pump = nil; session = nil; activeMockLink = nil; pendingOutgoing.removeAll()
    }

    private func handle(_ event: SessionEvent) {
        switch event {
        case .link(let s):
            linkState = s
        case .inbound(let inbound):
            appendConsole(inbound)
            ingest(inbound)
        }
    }

    // ---- inbound → SwiftData ----

    private func ingest(_ inbound: Inbound) {
        switch inbound {
        case .ready(let r):
            nodeIdentity = r
            upsertNodeProfile(r)
        case .messageReceived(let origin, let ctr, let body):
            insertInboundDM(origin: origin, ctr: ctr, body: body)
        case .channelReceived(let origin, let channelID, let body):
            insertChannel(origin: origin, channelID: channelID, body: body)
        case .sendAcked(_, let ctr):
            setOutgoingState(ctr: ctr, to: .acked)
        case .sendFailed(_, let ctr):
            setOutgoingState(ctr: ctr, to: .failed)
        case .hashResolved(let node, _, let hash) where node != 0:
            bindAndRekey(id: node, hash: hash)
        case .ack(let ack):
            attachAck(ack)
        default:
            break
        }
        try? context.save()
    }

    private func insertInboundDM(origin: Int, ctr: Int, body: String) {
        // dedup by (origin, ctr) — a reconnect / inbox pull must not duplicate.
        if incomingMessages().contains(where: { $0.origin == origin && $0.ctr == ctr }) { return }
        // Until a resolve binds the id to a hash, stage under a pseudo-hash == id; if a contact already
        // holds this short id, key straight to their real hash.
        let threadHash = contactHash(forID: origin) ?? UInt32(UInt8(clamping: origin))
        let msg = MessageEntity(id: UUID(), thread: .dm(KeyHash(threadHash)), direction: .incoming,
                                body: body, timestamp: .now, state: .received, origin: origin, ctr: ctr)
        context.insert(msg)
    }

    private func insertChannel(origin: Int, channelID: Int, body: String) {
        let msg = MessageEntity(id: UUID(), thread: .channel(UInt8(clamping: channelID)), direction: .incoming,
                                body: body, timestamp: .now, state: .received, origin: origin, ctr: nil)
        context.insert(msg)
    }

    private func setOutgoingState(ctr: Int, to state: DeliveryState) {
        for m in outgoingMessages() where m.ctr == ctr { m.stateRaw = state.rawValue }
    }

    private func attachAck(_ ack: CommandAck) {
        guard !pendingOutgoing.isEmpty else { return }
        let id = pendingOutgoing.removeFirst()
        guard let m = message(id: id) else { return }
        m.ctr = ack.ctr
        m.stateRaw = (ack.code.isError ? DeliveryState.failed : .queued).rawValue
    }

    private func bindAndRekey(id: Int, hash: KeyHash) {
        let shortID = UInt8(clamping: id)
        // The resolve gave us the hash → find the contact BY HASH and record its current short id.
        if let c = contact(for: hash) { c.lastKnownID = id }
        // Fold any messages staged under the pseudo-id thread onto the real hash thread.
        let pseudo = UInt32(shortID)
        guard pseudo != hash.value else { return }
        for m in dmMessages(threadHash: pseudo) { m.threadHash = hash.value }
    }

    // ---- outbound ----

    func sendDM(to thread: ThreadKey, body: String) {
        guard case .dm = thread, let target = target(for: thread) else { return }
        let msg = MessageEntity(id: UUID(), thread: thread, direction: .outgoing, body: body,
                                timestamp: .now, state: .sending, origin: nil, ctr: nil)
        context.insert(msg); try? context.save()
        pendingOutgoing.append(msg.id)
        sendCommand(.sendDM(.init(target: target, body: body)))
    }

    func sendChannel(_ channelID: UInt8, body: String) {
        let msg = MessageEntity(id: UUID(), thread: .channel(channelID), direction: .outgoing, body: body,
                                timestamp: .now, state: .sending, origin: nil, ctr: nil)
        context.insert(msg); try? context.save()
        pendingOutgoing.append(msg.id)
        sendCommand(.sendChannel(.init(channelID: channelID, body: body)))
    }

    func addContact(name: String, hash: KeyHash) {
        if let existing = contact(for: hash) { existing.name = name }
        else { context.insert(ContactEntity(hashValue32: hash.value, name: name)) }
        try? context.save()
    }

    func resolve(_ hash: KeyHash, hard: Bool = false) { sendCommand(.resolve(.init(hash: hash, hard: hard))) }

    func sendRaw(_ line: String) {
        guard !line.isEmpty else { return }
        consoleLog.append(ConsoleLine(text: "» \(line)", kind: .outgoing))
        Task { try? await session?.sendRaw(line) }
    }

    /// One-shot scripted demo (launch with env MR_DEMO=1) that drives the full pipeline against the
    /// mock: resolve a contact's id, receive a DM, and send one — so the UI shows a real conversation.
    private var demoStarted = false
    func startDemoIfRequested() {
        guard !demoStarted, ProcessInfo.processInfo.environment["MR_DEMO"] == "1" else { return }
        demoStarted = true
        let bench = KeyHash(0x8a3f1c02)              // the mock knows this peer as short id 2
        backend = .mock
        addContact(name: "Bench-2", hash: bench)
        connect()
        Task { @MainActor in
            try? await Task.sleep(for: .milliseconds(500))   // let connect + the ready greeting settle
            resolve(bench)                                    // binds id 2 ↔ hash, names the thread
            try? await Task.sleep(for: .milliseconds(300))
            simulateInbound(fromID: 2, body: "hey — it works over BLE!")
            try? await Task.sleep(for: .milliseconds(300))
            sendDM(to: .dm(bench), body: "hello from the iOS app 👋")
        }
    }

    /// Demo helper (mock backend): pretend a peer messaged us, to exercise the inbound path.
    var canSimulateInbound: Bool { activeMockLink != nil }
    func simulateInbound(fromID id: UInt8, body: String) {
        guard let mock = activeMockLink else { return }
        Task { await mock.simulateIncomingDM(fromID: id, body: body) }
    }

    // ---- private ----

    private func sendCommand(_ command: Command) {
        Task { try? await session?.send(command) }
    }

    private func target(for thread: ThreadKey) -> DMTarget? {
        guard case .dm(let hash) = thread else { return nil }
        if hash.value <= 254 { return .id(UInt8(hash.value)) }   // unresolved pseudo-id thread → send by id
        return .hash(hash)
    }

    private func appendConsole(_ inbound: Inbound) {
        consoleLog.append(ConsoleLine(text: "« \(describe(inbound))", kind: .incoming))
        if consoleLog.count > 500 { consoleLog.removeFirst(consoleLog.count - 500) }
    }

    // ---- fetch helpers (small archive → simple fetch-and-filter) ----

    private func incomingMessages() -> [MessageEntity] { fetchMessages(direction: "incoming") }
    private func outgoingMessages() -> [MessageEntity] { fetchMessages(direction: "outgoing") }
    private func fetchMessages(direction: String) -> [MessageEntity] {
        let d = FetchDescriptor<MessageEntity>(predicate: #Predicate { $0.directionRaw == direction })
        return (try? context.fetch(d)) ?? []
    }
    private func dmMessages(threadHash: UInt32) -> [MessageEntity] {
        let d = FetchDescriptor<MessageEntity>(predicate: #Predicate {
            $0.threadKind == "dm" && $0.threadHash == threadHash
        })
        return (try? context.fetch(d)) ?? []
    }
    private func message(id: UUID) -> MessageEntity? {
        let d = FetchDescriptor<MessageEntity>(predicate: #Predicate { $0.id == id })
        return try? context.fetch(d).first
    }
    private func contact(for hash: KeyHash) -> ContactEntity? {
        let v = hash.value
        let d = FetchDescriptor<ContactEntity>(predicate: #Predicate { $0.hashValue32 == v })
        return try? context.fetch(d).first
    }
    private func contact(forID id: Int) -> ContactEntity? {
        let d = FetchDescriptor<ContactEntity>(predicate: #Predicate { $0.lastKnownID == id })
        return try? context.fetch(d).first
    }
    private func contactHash(forID id: Int) -> UInt32? { contact(forID: id)?.hashValue32 }

    private func upsertNodeProfile(_ r: NodeReady) {
        let key = r.key.value
        let d = FetchDescriptor<NodeProfileEntity>(predicate: #Predicate { $0.key32 == key })
        if let p = try? context.fetch(d).first {
            p.shortID = r.id; p.mode = r.mode; p.routingSF = r.routingSF; p.gateway = r.gateway; p.lastSeen = .now
        } else {
            context.insert(NodeProfileEntity(key32: key, shortID: r.id, mode: r.mode,
                                             routingSF: r.routingSF, gateway: r.gateway))
        }
    }
}

struct ConsoleLine: Identifiable {
    enum Kind { case incoming, outgoing }
    let id = UUID()
    let text: String
    let kind: Kind
}

/// A short human description of an inbound event for the debug console.
func describe(_ inbound: Inbound) -> String {
    switch inbound {
    case .ack(let a):                              return "ack \(a.code) ctr=\(a.ctr) qd=\(a.queueDepth)"
    case .messageReceived(let o, let c, let b):    return "msg_recv from \(o) ctr=\(c): \(b)"
    case .channelReceived(let o, let ch, let b):   return "channel_recv ch\(ch) from \(o): \(b)"
    case .sendAcked(let d, let c):                 return "send_acked dst=\(d) ctr=\(c)"
    case .sendFailed(let d, let c):                return "send_failed dst=\(d) ctr=\(c)"
    case .hashResolved(let n, let a, let h):       return "hash_resolved \(h.hex8) → node \(n)\(a ? " (auth)" : "")"
    case .ready(let r):                            return "ready id=\(r.id) key=\(r.key.hex8) sf=\(r.routingSF)"
    case .status(let s):                           return "status id=\(s.id) \(s.state)"
    case .event(let t, _):                         return "event \(t)"
    case .log(let m):                              return "log: \(m)"
    case .error(let code, let msg):                return "err \(code)\(msg.map { ": \($0)" } ?? "")"
    case .raw(let s):                              return s
    }
}
