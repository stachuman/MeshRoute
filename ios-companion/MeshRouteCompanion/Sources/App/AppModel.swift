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
    private var activeSync: InboxSyncState?        // in-flight inbox cursors for this connection
    private var activeSyncProfile: NodeProfileEntity?
    private var greetedThisConnection = false      // sent `whoami` once on connect
    private var syncStartedThisConnection = false  // started the inbox pull once on connect

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
        activeSync = nil; activeSyncProfile = nil
        greetedThisConnection = false; syncStartedThisConnection = false
    }

    private func handle(_ event: SessionEvent) {
        switch event {
        case .link(let s):
            linkState = s
            // On a fresh, usable link, fetch identity deterministically (don't rely on an unsolicited
            // greeting). The node replies to `whoami` with a `ready` JSON → identity + (Phase 3) inbox sync.
            if case .connected = s, !greetedThisConnection {
                greetedThisConnection = true
                sendCommand(.whoami)
            }
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
            let profile = upsertNodeProfile(r)
            startInboxSync(r, profile: profile)
        case .inboxEntry(let e):
            importInboxEntry(e)
        case .inboxEnd(_, _, let epoch, _):
            handleInboxEnd(servedEpoch: epoch)
        case .messageReceived(let origin, let ctr, let senderHash, let body):
            insertInboundDM(origin: origin, ctr: ctr, senderHash: senderHash, body: body)
        case .channelReceived(let origin, let channelID, let channelMsgID, let body):
            insertChannel(origin: origin, channelID: channelID, channelMsgID: channelMsgID, body: body)
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

    private func insertInboundDM(origin: Int, ctr: Int, senderHash: UInt32?, body: String) {
        if dmExists(senderHash: senderHash, origin: origin, ctr: ctr) { return }
        let msg = MessageEntity(id: UUID(), thread: .dm(KeyHash(dmThreadHash(senderHash: senderHash, origin: origin))),
                                direction: .incoming, body: body, timestamp: .now, state: .received,
                                origin: origin, ctr: ctr, senderHash: senderHash.map(Int.init))
        context.insert(msg)
    }

    private func insertChannel(origin: Int, channelID: Int, channelMsgID: UInt32?, body: String) {
        if let mid = channelMsgID, channelExists(msgID: mid) { return }   // dedup by channel_msg_id
        let msg = MessageEntity(id: UUID(), thread: .channel(UInt8(clamping: channelID)), direction: .incoming,
                                body: body, timestamp: .now, state: .received, origin: origin, ctr: nil,
                                channelMsgID: channelMsgID.map(Int.init))
        context.insert(msg)
    }

    /// A DM thread key: the sender's STABLE hash when present (→ straight into the contact's thread, no
    /// resolve), else a known id→hash binding, else a pseudo-hash == id staged until a resolve rekeys it.
    private func dmThreadHash(senderHash: UInt32?, origin: Int) -> UInt32 {
        if let h = senderHash, h != 0 { return h }
        return contactHash(forID: origin) ?? UInt32(UInt8(clamping: origin))
    }

    // Dedup against the durable archive: DM by (sender_hash, ctr) | (origin, ctr); channel by channel_msg_id.
    private func dmExists(senderHash: UInt32?, origin: Int, ctr: Int) -> Bool {
        if let h = senderHash, h != 0 {
            let hi = Int(h)
            return incomingMessages().contains { $0.threadKind == "dm" && $0.senderHash == hi && $0.ctr == ctr }
        }
        return incomingMessages().contains { $0.threadKind == "dm" && $0.origin == origin && $0.ctr == ctr }
    }
    private func channelExists(msgID: UInt32) -> Bool {
        let m = Int(msgID)
        return incomingMessages().contains { $0.threadKind == "channel" && $0.channelMsgID == m }
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

    @discardableResult
    private func upsertNodeProfile(_ r: NodeReady) -> NodeProfileEntity {
        let key = r.key.value
        let d = FetchDescriptor<NodeProfileEntity>(predicate: #Predicate { $0.key32 == key })
        if let p = try? context.fetch(d).first {
            p.shortID = r.id; p.mode = r.mode; p.routingSF = r.routingSF; p.gateway = r.gateway; p.lastSeen = .now
            return p
        }
        let p = NodeProfileEntity(key32: key, shortID: r.id, mode: r.mode, routingSF: r.routingSF, gateway: r.gateway)
        context.insert(p)
        return p
    }

    // ---- inbox sync (handles the node's seq-epoch reset; see INBOX_SYNC_CONTRACT.md) ----

    /// On the `ready` snapshot: reconcile our saved cursors against the node's CURRENT inbox epoch and
    /// pull. If the epoch changed (store wiped / first sync), beginSync resets the cursors to 0 → a full
    /// re-pull → and dedup-on-import merges it into the archive. No-op on firmware without a durable inbox.
    private func startInboxSync(_ r: NodeReady, profile: NodeProfileEntity) {
        guard let nodeEpoch = r.inboxEpoch, !syncStartedThisConnection else { return }
        syncStartedThisConnection = true
        var state = profile.syncState
        let (dmSince, chanSince) = state.beginSync(nodeEpoch: nodeEpoch)
        profile.syncState = state                       // persist the (possibly reset) epoch + cursors
        try? context.save()
        activeSync = state; activeSyncProfile = profile
        sendCommand(.pullInbox(dmSince: dmSince, chanSince: chanSince))
    }

    private func importInboxEntry(_ e: InboxEntry) {
        if !inboxEntryExists(e) {                        // dedup-on-import by stable identity (no seq/epoch)
            let thread: ThreadKey = e.kind == .channel
                ? .channel(UInt8(clamping: e.channelID))
                : .dm(KeyHash(dmThreadHash(senderHash: e.senderHash, origin: e.origin)))
            context.insert(MessageEntity(id: UUID(), thread: thread, direction: .incoming, body: e.body,
                                         timestamp: .now, state: .received, origin: e.origin, ctr: e.ctr,
                                         channelMsgID: e.channelMsgID.map(Int.init),
                                         senderHash: e.senderHash.map(Int.init)))
        }
        activeSync?.advance(with: e)                     // advance the cursor for the store we just saw
    }

    /// Pull complete: `inbox_end.epoch` is authoritative for the cursors we just advanced (it also lets us
    /// detect a mid-pull wipe). Adopt it and persist { epoch, dm_cursor, chan_cursor }.
    private func handleInboxEnd(servedEpoch: UInt32?) {
        guard var state = activeSync, let profile = activeSyncProfile else { return }
        if let served = servedEpoch { state.epoch = served }
        activeSync = state
        profile.syncState = state
        try? context.save()
    }

    /// Dedup-on-import: DM by (origin, ctr); channel by the full channel_msg_id. seq/epoch are not identity.
    private func inboxEntryExists(_ e: InboxEntry) -> Bool {
        switch e.kind {
        case .dm:
            return dmExists(senderHash: e.senderHash, origin: e.origin, ctr: e.ctr)
        case .channel:
            guard let mid = e.channelMsgID else { return false }
            return channelExists(msgID: mid)
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
    case .messageReceived(let o, let c, _, let b):  return "msg_recv from \(o) ctr=\(c): \(b)"
    case .channelReceived(let o, let ch, _, let b): return "channel_recv ch\(ch) from \(o): \(b)"
    case .sendAcked(let d, let c):                 return "send_acked dst=\(d) ctr=\(c)"
    case .sendFailed(let d, let c):                return "send_failed dst=\(d) ctr=\(c)"
    case .hashResolved(let n, let a, let h):       return "hash_resolved \(h.hex8) → node \(n)\(a ? " (auth)" : "")"
    case .ready(let r):                            return "ready id=\(r.id) key=\(r.key.hex8) sf=\(r.routingSF)"
    case .status(let s):                           return "status id=\(s.id) \(s.state)"
    case .inboxEntry(let e):                       return "inbox_\(e.kind.rawValue) seq=\(e.seq) from \(e.origin) ctr=\(e.ctr): \(e.body)"
    case .inboxEnd(let dm, let chan, let epoch, let count): return "inbox_end dm=\(dm) chan=\(chan) epoch=\(epoch.map(String.init) ?? "—") count=\(count)"
    case .event(let t, _):                         return "event \(t)"
    case .log(let m):                              return "log: \(m)"
    case .error(let code, let msg):                return "err \(code)\(msg.map { ": \($0)" } ?? "")"
    case .raw(let s):                              return s
    }
}
