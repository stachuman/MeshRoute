// MeshRouteCompanion — AppModel.swift
//
// The controller: owns the NodeSession, consumes its events on the main actor, writes inbound
// messages into SwiftData (applying the dedup-by-(origin,ctr) rule that ConversationStore proves),
// and drives sending. Views read the data via @Query and read live state from here.

import Foundation
import SwiftData
import Observation
import UserNotifications
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
    // Node / Network screens (Theme D) — refreshed on demand from status/routes/cfg.
    private(set) var latestStatus: NodeStatusSnapshot?
    private(set) var routes: [RouteInfo] = []
    private(set) var latestConfig: NodeConfigInfo?
    // Leaf membership (R6 / D26) — from `ready` + the live `config_adopted` push.
    private(set) var membership: LeafMembership?
    private(set) var joinInFlight = false            // optimistic "Joining…" between a join/create and config_adopted
    private(set) var joinRefusal: JoinRefusal?        // a banner when the node can't join (wire_version / leaf_full)
    private var routesAccumulator: [RouteInfo] = []   // fills during a `routes` stream, swapped in at routes_end
    var backend: Backend = defaultBackend
    // Navigation the notification-tap router drives (Messages = tab 0).
    var selectedTab = 0
    var messagesPath: [ThreadKey] = []
    private var notifRouter: NotificationRouter?

    private var session: NodeSession?
    private var pump: Task<Void, Never>?
    private var activeMockLink: MockNodeLink?     // kept when running the mock, for the demo button
    private var pendingOutgoing: [UUID] = []     // FIFO: outgoing message ids awaiting their queued ack
    private var activeSync: InboxSyncState?        // in-flight inbox cursors for this connection
    private var activeSyncProfile: NodeProfileEntity?
    private var timeAnchor: NodeTimeAnchor?        // node-uptime ↔ wall-clock pairing (from ready/inbox_end now_ms)
    private var greetedThisConnection = false      // sent `whoami` once on connect
    private var syncStartedThisConnection = false  // started the inbox pull once on connect
    private var isForeground = true                 // drives whether an inbound DM posts a local notification

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
        case .connected:
            guard let id = nodeIdentity else { return "Connected" }
            return "Connected · " + (id.name.map { "\($0) (\(id.id))" } ?? "node \(id.id)")
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
        activeSync = nil; activeSyncProfile = nil; timeAnchor = nil
        greetedThisConnection = false; syncStartedThisConnection = false
        latestStatus = nil; latestConfig = nil; routes = []; routesAccumulator = []
    }

    private func handle(_ event: SessionEvent) {
        switch event {
        case .link(let s):
            linkState = s
            switch s {
            case .connected where !greetedThisConnection:
                // On a fresh, usable link, fetch identity deterministically (don't rely on an unsolicited
                // greeting). `whoami` → `ready` JSON → identity + inbox sync. Also fires on an AUTO-reconnect
                // (a dropped link / periodic window / background resume), because the guards were cleared below.
                greetedThisConnection = true
                sendCommand(.whoami)
                drainOutbox()                       // messages composed while away go out now, oldest first
            case .disconnected, .failed:
                // The link dropped — BUT BLENodeLink may auto-reconnect through the SAME session (it never
                // calls disconnectInternal). Clear the per-connection guards so the next .connected re-greets
                // + re-pulls the inbox (catch-up). Without this, an auto-reconnect shows "connected" but never
                // syncs → messages that arrived during the drop don't appear until a manual reconnect.
                greetedThisConnection = false
                syncStartedThisConnection = false
                timeAnchor = nil
            default:
                break                                // .scanning/.connecting/.pairing — transient, keep guards
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
            if let m = LeafMembership.from(r) { membership = m; if m.state == .member { joinInFlight = false } }
            if let n = r.nowMs { timeAnchor = NodeTimeAnchor(nodeNowMs: n) }   // anchor BEFORE the pull streams in
            let profile = upsertNodeProfile(r)
            startInboxSync(r, profile: profile)
        case .inboxEntry(let e):
            importInboxEntry(e)
        case .inboxEnd(_, _, let epoch, _, let nowMs):
            if let n = nowMs { timeAnchor = NodeTimeAnchor(nodeNowMs: n) }     // refresh for later gap-pulls
            handleInboxEnd(servedEpoch: epoch)
        case .messageReceived(let origin, let ctr, let senderHash, let seq, _, let crypted, let body):
            insertInboundDM(origin: origin, ctr: ctr, senderHash: senderHash, crypted: crypted ?? false, body: body)
            applyLiveSeq(kind: .dm, seq: seq)
        case .channelReceived(let origin, let channelID, let channelMsgID, let seq, _, let body):
            insertChannel(origin: origin, channelID: channelID, channelMsgID: channelMsgID, body: body)
            applyLiveSeq(kind: .channel, seq: seq)
        case .sendAcked(_, let ctr):
            setOutgoingState(ctr: ctr, to: .acked)
        case .sendFailed(_, let ctr, let reason):
            setOutgoingState(ctr: ctr, to: .failed, reason: reason)   // "no_pubkey" → the bubble offers Request-key/Scan
        case .e2eAcked(let dst, let ctr, let senderHash):
            markDelivered(dst: dst, ctr: ctr, senderHash: senderHash)   // recipient confirmed end-to-end (live twin, D25)
        case .peerKeyCached(let hash, _):
            markKeyReady(for: hash)                                   // failed-no_pubkey DMs to this hash → "secure resend"
        case .peerKeySet, .peerKeyError, .reqPubkeySent:
            break   // provisioning results — visible in the console for now
        case .hashResolved(let node, _, let hash) where node != 0:
            bindAndRekey(id: node, hash: hash)
        case .ack(let ack):
            attachAck(ack)
        case .status(let s):
            latestStatus = s
        case .route(let r):
            routesAccumulator.append(r)
        case .routesEnd:
            routes = routesAccumulator.sorted { $0.dest < $1.dest }; routesAccumulator = []
        case .cfg(let c):
            latestConfig = c
        case .configAdopted(let lineage, let epoch, let leaf, let level):
            membership = .adopted(lineage: lineage, epoch: epoch, leaf: leaf, level: level)
            joinInFlight = false; joinRefusal = nil           // a successful adopt clears the pending/refusal UI
        case .joinRefused(let reason, let theirVer, let myVer):
            joinRefusal = JoinRefusal(reason: reason, theirVer: theirVer, myVer: myVer)
            joinInFlight = false
        default:
            break
        }
        try? context.save()
    }

    /// An inbound DM from a hash we don't know auto-creates a placeholder contact ("Node <id>", renameable
    /// in Contacts) so the thread lists under a name instead of a raw hash; a known hash refreshes the
    /// contact's lastKnownID (the id↔hash binding is reassignable, the hash is the identity).
    private func ensureContact(senderHash: UInt32?, origin: Int) {
        guard let h = senderHash, h != 0 else { return }     // no stable identity → nothing to pin a contact to
        if let c = contact(for: KeyHash(h)) { c.lastKnownID = origin; return }
        context.insert(ContactEntity(hashValue32: h, name: "Node \(origin)", lastKnownID: origin))
    }

    private func insertInboundDM(origin: Int, ctr: Int, senderHash: UInt32?, crypted: Bool = false, body: String) {
        ensureContact(senderHash: senderHash, origin: origin)
        if dmExists(senderHash: senderHash, origin: origin, ctr: ctr) { return }
        let threadHash = dmThreadHash(senderHash: senderHash, origin: origin)
        let msg = MessageEntity(id: UUID(), thread: .dm(KeyHash(threadHash)),
                                direction: .incoming, body: body, timestamp: .now, state: .received,
                                origin: origin, ctr: ctr, senderHash: senderHash.map(Int.init))
        msg.isRead = false
        msg.crypted = crypted
        context.insert(msg)
        // A genuinely new DM arriving while we're NOT on screen (background BLE, or another tab inactive)
        // → a local banner. Only on the LIVE path (this fn) — never the bulk pull-catch-up (importInboxEntry).
        notifyInboundDM(threadHash: threadHash, origin: origin, body: body)
    }

    private func insertChannel(origin: Int, channelID: Int, channelMsgID: UInt32?, body: String) {
        if let mid = channelMsgID, channelExists(msgID: mid) { return }   // dedup by channel_msg_id
        let msg = MessageEntity(id: UUID(), thread: .channel(UInt8(clamping: channelID)), direction: .incoming,
                                body: body, timestamp: .now, state: .received, origin: origin, ctr: nil,
                                channelMsgID: channelMsgID.map(Int.init))
        msg.isRead = false
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

    private func setOutgoingState(ctr: Int, to state: DeliveryState, reason: String? = nil) {
        for m in outgoingMessages() where m.ctr == ctr && m.state != .deliveredE2E {   // never regress a confirmed E2E delivery (D25)
            m.stateRaw = state.rawValue
            if state == .failed { m.failReason = reason } else { m.failReason = nil }
        }
    }

    /// An E2E delivery receipt (live `e2e_acked` or pulled `inbox_dm type:e2e_ack`) confirmed an `-a` DM we sent.
    /// Match it to the OUTBOX by (sender_hash, ctr) when a hash is present (cross-layer: the 8-bit dst aliases
    /// across leaves), else by the recipient short id; upgrade that message to `.deliveredE2E`. NEVER inserted
    /// as an inbound message.
    private func markDelivered(dst: Int, ctr: Int, senderHash: UInt32?) {
        for m in outgoingMessages()
            where m.ctr == ctr && m.ackRequested && receiptMatchesRecipient(m, dst: dst, senderHash: senderHash) {
            m.stateRaw = DeliveryState.deliveredE2E.rawValue
        }
        try? context.save()
    }
    /// Does this outgoing DM's recipient match the receipt? hash present → the thread's hash; else the receipt
    /// names the recipient's short id (an id-thread, or a hash-thread the contact binds to that id).
    private func receiptMatchesRecipient(_ m: MessageEntity, dst: Int, senderHash: UInt32?) -> Bool {
        guard m.threadKind == "dm" else { return false }
        if let h = senderHash, h != 0 { return m.threadHash == h }            // cross-layer: the stable key
        if m.threadHash == UInt32(clamping: dst) { return true }              // id-thread (threadHash == the short id)
        return contact(for: KeyHash(m.threadHash))?.lastKnownID == dst        // hash-thread bound to that id
    }

    /// A verified key for `hash` just arrived (peer_key_cached) → any failed "no_pubkey" DM to that contact can
    /// now seal: flag it so the bubble offers a secure resend.
    private func markKeyReady(for hash: KeyHash) {
        for m in dmMessages(threadHash: hash.value) where m.state == .failed && m.failReason == "no_pubkey" {
            m.failReason = "key_ready"
        }
        try? context.save()
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

    func sendDM(to thread: ThreadKey, body: String, requestAck: Bool = false, encrypt: Bool = false) {
        guard case .dm = thread, let t = target(for: thread) else { return }
        // encrypt is HASH-only (sealing needs the recipient's pubkey); a plain id-thread can't be sealed.
        let canEncrypt: Bool = { if case .hash = t { return true }; return false }()
        compose(thread: thread, body: body, requestAck: requestAck, encrypt: encrypt && canEncrypt)
    }

    func sendChannel(_ channelID: UInt8, body: String) {
        compose(thread: .channel(channelID), body: body)
    }

    /// Insert the outgoing message and dispatch it — or park it in the OUTBOX when there's no link
    /// (drained FIFO by `drainOutbox` on the next connect). `requestAck`/`encrypt` ride on the message.
    private func compose(thread: ThreadKey, body: String, requestAck: Bool = false, encrypt: Bool = false) {
        let msg = MessageEntity(id: UUID(), thread: thread, direction: .outgoing, body: body,
                                timestamp: .now, state: isConnected ? .sending : .outbox,
                                origin: nil, ctr: nil)
        msg.ackRequested = requestAck
        msg.crypted = encrypt                       // we requested E2E sealing → the lock marker shows
        context.insert(msg); try? context.save()
        if isConnected { dispatch(msg) }
    }

    /// Hand one stored outgoing message to the node (FIFO ack pairing via pendingOutgoing).
    private func dispatch(_ msg: MessageEntity) {
        msg.failReason = nil                        // a (re)send clears any prior failure
        switch msg.threadKey {
        case .dm(let h):
            guard let target = target(for: .dm(h)) else { return }
            pendingOutgoing.append(msg.id)
            sendCommand(.sendDM(.init(target: target, body: msg.body,
                                      requestAck: msg.ackRequested, encrypt: msg.crypted)))   // crypted → the -e flag (D24)
        case .channel(let c):
            pendingOutgoing.append(msg.id)
            sendCommand(.sendChannel(.init(channelID: c, body: msg.body)))
        }
    }

    /// Send everything parked in the outbox, oldest first (called once per connection).
    private func drainOutbox() {
        let outboxRaw = DeliveryState.outbox.rawValue
        let d = FetchDescriptor<MessageEntity>(predicate: #Predicate { $0.stateRaw == outboxRaw },
                                               sortBy: [SortDescriptor(\.timestamp, order: .forward)])
        for msg in (try? context.fetch(d)) ?? [] {
            msg.stateRaw = DeliveryState.sending.rawValue
            dispatch(msg)
        }
        try? context.save()
    }

    /// Re-send a failed message: straight out when connected, else back to the outbox for the next link.
    func retry(_ msg: MessageEntity) {
        guard msg.direction == .outgoing, msg.state == .failed else { return }
        msg.stateRaw = (isConnected ? DeliveryState.sending : .outbox).rawValue
        if isConnected { dispatch(msg) }
        try? context.save()
    }

    /// Unread → read for every incoming message of a thread (read state is per-phone — D14).
    func markThreadRead(_ thread: ThreadKey) {
        for m in threadMessages(thread) where m.direction == .incoming && !m.isRead { m.isRead = true }
        try? context.save()
    }

    /// Local channel name ("3 = Sailing crew"); empty name removes the label. Never on the wire.
    func setChannelLabel(_ channelID: Int, name: String) {
        let trimmed = name.trimmingCharacters(in: .whitespaces)
        let d = FetchDescriptor<ChannelLabelEntity>(predicate: #Predicate { $0.channelID == channelID })
        let existing = try? context.fetch(d).first
        switch (existing, trimmed.isEmpty) {
        case (let e?, true):  context.delete(e)
        case (let e?, false): e.name = trimmed
        case (nil, false):    context.insert(ChannelLabelEntity(channelID: channelID, name: trimmed))
        case (nil, true):     break
        }
        try? context.save()
    }

    private func threadMessages(_ thread: ThreadKey) -> [MessageEntity] {
        let d: FetchDescriptor<MessageEntity>
        switch thread {
        case .dm(let h):
            let hv = h.value
            d = FetchDescriptor(predicate: #Predicate { $0.threadKind == "dm" && $0.threadHash == hv })
        case .channel(let c):
            let ci = Int(c)
            d = FetchDescriptor(predicate: #Predicate { $0.threadKind == "channel" && $0.threadChannel == ci })
        }
        return (try? context.fetch(d)) ?? []
    }

    func addContact(name: String, hash: KeyHash) {
        if let existing = contact(for: hash) { existing.name = name }
        else { context.insert(ContactEntity(hashValue32: hash.value, name: name)) }
        try? context.save()
    }

    func resolve(_ hash: KeyHash, hard: Bool = false) { sendCommand(.resolve(.init(hash: hash, hard: hard))) }

    // ---- E2E peer-key provisioning (the app carries opaque bytes; the node does the crypto) ----
    /// Install a scanned card's pubkey on the node (PINNED) so a first encrypted DM seals with no round-trip.
    func provisionPeerKey(_ pubkeyHex: String) { sendCommand(.peerKey(pubkeyHex: pubkeyHex)) }
    /// User-triggered on-air key request (the "Request key" action after a no-pubkey drop).
    func requestPubkey(_ hash: KeyHash) { sendCommand(.reqPubkey(hash)) }

    // ---- leaf provisioning (R6 / D26): live, no reboot ----
    func joinNetwork(freqMHz: Double, bwKHz: Int, ctrlSF: Int, level: Int) {
        joinRefusal = nil; joinInFlight = true
        sendCommand(.join(freqMHz: freqMHz, bwKHz: bwKHz, ctrlSF: ctrlSF, level: level))
    }
    func createLeaf(freqMHz: Double, bwKHz: Int, ctrlSF: Int, level: Int, sfList: String, dutyPercent: Int, name: String) {
        joinRefusal = nil; joinInFlight = true
        sendCommand(.createLeaf(freqMHz: freqMHz, bwKHz: bwKHz, ctrlSF: ctrlSF, level: level,
                                sfList: sfList, dutyPercent: dutyPercent, name: name))
    }
    func leaveNetwork() {
        sendCommand(.leave)
        membership = .adopted(lineage: 0, epoch: 0, leaf: nil, level: nil)   // optimistic unmanaged; the node confirms via config_adopted/ready
        joinInFlight = false; joinRefusal = nil
    }
    func dismissJoinRefusal() { joinRefusal = nil }
    /// Set the node's default DM encryption (`cfg set e2e_dm`); the per-message lock toggle overrides it.
    func setNodeEncryptDefault(_ on: Bool) {
        sendCommand(.configSet(key: "e2e_dm", value: on ? "on" : "off"))
        refreshConfig()
    }

    /// App returned to the foreground. If the link stayed up through a screen-off/suspend, a live push may
    /// have been dropped while we were suspended (no disconnect event → fix #1's auto-resync never fired), so
    /// pull from the current cursors to catch up. If the link actually died, CoreBluetooth will report the
    /// disconnect and the auto-reconnect path re-syncs instead.
    func handleForeground() {
        isForeground = true
        guard isConnected else { return }
        if let s = activeSync {
            sendCommand(.pullInbox(dmSince: s.dmCursor, chanSince: s.chanCursor))
        }
        refreshStatus()
    }
    func handleBackground() { isForeground = false }

    // ---- local notifications (Theme E1): alert on a DM that arrives while we're not on screen ----

    /// Ask once (idempotent — iOS only prompts the first time) + install the tap router. Call on launch.
    func requestNotificationAuthorization() {
        let router = NotificationRouter(model: self)
        notifRouter = router
        UNUserNotificationCenter.current().delegate = router
        UNUserNotificationCenter.current().requestAuthorization(options: [.alert, .sound, .badge]) { _, _ in }
    }

    /// Mirror the unread total onto the app-icon badge.
    func setAppBadge(_ count: Int) {
        UNUserNotificationCenter.current().setBadgeCount(count)
    }

    /// A tapped DM banner → jump to that conversation (Messages tab) and mark it read.
    func openConversation(threadHash: UInt32) {
        let key = ThreadKey.dm(KeyHash(threadHash))
        selectedTab = 0
        messagesPath = [key]
        markThreadRead(key)
    }

    private func notifyInboundDM(threadHash: UInt32, origin: Int, body: String) {
        guard !isForeground else { return }              // on screen → the thread updates live, no banner
        let title = contact(for: KeyHash(threadHash))?.name ?? "Node \(origin)"
        let content = UNMutableNotificationContent()
        content.title = title
        content.body = body
        content.sound = .default
        content.threadIdentifier = "dm-\(threadHash)"    // iOS groups a conversation's banners together
        let req = UNNotificationRequest(identifier: UUID().uuidString, content: content, trigger: nil)
        UNUserNotificationCenter.current().add(req)
    }

    /// Set the node's location (a FIXED node, set once). Sends `cfg set lat/lon` in decimal degrees; the
    /// node persists to /mrloc and replies with the fresh cfg → the Config view updates.
    func setNodeLocation(latitude: Double, longitude: Double) {
        sendCommand(.configSet(key: "lat", value: String(format: "%.7f", latitude)))
        sendCommand(.configSet(key: "lon", value: String(format: "%.7f", longitude)))
        sendCommand(.config)                              // pull the updated cfg back (mock + real both re-emit)
    }

    // ---- Node / Network refresh (Theme D) ----
    func refreshStatus() { sendCommand(.status) }
    func refreshConfig() { sendCommand(.config) }
    func refreshRoutes() { routesAccumulator = []; sendCommand(.routes) }
    /// Pull everything the Node tab shows in one go (on appear / pull-to-refresh).
    func refreshNodeInfo() {
        guard isConnected else { return }
        refreshStatus(); refreshConfig(); refreshRoutes()
    }

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
            try? await Task.sleep(for: .milliseconds(500))
            // Model B: a live push is dropped from the bounded ring; the NEXT push's seq jumps the gap →
            // the app detects it and pull_inbox-backfills the lost message immediately (no reconnect needed).
            if let mock = activeMockLink {
                await mock.simulateDroppedIncomingDM(fromID: 2, body: "dropped push — recovered by gap-pull")
                await mock.simulateIncomingDM(fromID: 2, body: "after the gap")
            }
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

    /// Model B (live-while-connected): the per-store cursor is the live high-water. If a live push's seq
    /// jumps past `high+1`, a push was dropped from the bounded ring → pull_inbox-backfill immediately
    /// (the just-ingested live message dedups against the pulled copy). Then advance + persist the cursor.
    /// No seq (inbox disabled) or no active sync ⇒ best-effort live only.
    private func applyLiveSeq(kind: InboxKind, seq: UInt32?) {
        guard let seq, var state = activeSync else { return }
        if state.classifyLive(kind: kind, seq: seq) == .gap {
            sendCommand(.pullInbox(dmSince: state.dmCursor, chanSince: state.chanCursor))
        }
        state.advance(kind: kind, seq: seq)
        activeSync = state
        activeSyncProfile?.syncState = state
        try? context.save()
    }

    private func importInboxEntry(_ e: InboxEntry) {
        if e.isReceipt {                                 // a pulled E2E delivery receipt → match the OUTBOX, never insert (D25)
            markDelivered(dst: e.origin, ctr: e.ctr, senderHash: e.senderHash)
            activeSync?.advance(with: e)                 // it rides the DM seq-cursor — advance past it
            return
        }
        if e.kind == .dm { ensureContact(senderHash: e.senderHash, origin: e.origin) }
        if !inboxEntryExists(e) {                        // dedup-on-import by stable identity (no seq/epoch)
            let thread: ThreadKey = e.kind == .channel
                ? .channel(UInt8(clamping: e.channelID))
                : .dm(KeyHash(dmThreadHash(senderHash: e.senderHash, origin: e.origin)))
            // True receive time via the uptime anchor (ready/inbox_end now_ms); pull-time as the fallback
            // on firmware without the field.
            let received = timeAnchor?.wallClock(rxMs: e.rxTimeMs) ?? .now
            let msg = MessageEntity(id: UUID(), thread: thread, direction: .incoming, body: e.body,
                                    timestamp: received, state: .received, origin: e.origin, ctr: e.ctr,
                                    channelMsgID: e.channelMsgID.map(Int.init),
                                    senderHash: e.senderHash.map(Int.init))
            msg.isRead = false
            msg.crypted = e.crypted ?? false
            context.insert(msg)
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

/// A "can't join" banner (R6 / D26): `wire_version` ⇒ update the node's firmware; `leaf_full` ⇒ no address.
struct JoinRefusal: Identifiable, Hashable {
    let id = UUID()
    let reason: String
    let theirVer: Int?
    let myVer: Int?
    var message: String {
        switch reason {
        case "wire_version": return "Can't join — the network runs wire v\(theirVer ?? 0), this node v\(myVer ?? 0). Update the node's firmware."
        case "leaf_full":    return "Can't join — this leaf is full (no node address available)."
        default:             return "Can't join — \(reason)."
        }
    }
    var isBlocking: Bool { reason == "wire_version" }   // a firmware update, not just a retry
}

/// Routes a tapped DM banner into the right conversation. The banner's `threadIdentifier` is "dm-<hash>".
final class NotificationRouter: NSObject, UNUserNotificationCenterDelegate {
    weak var model: AppModel?
    init(model: AppModel) { self.model = model }

    func userNotificationCenter(_ center: UNUserNotificationCenter,
                                didReceive response: UNNotificationResponse,
                                withCompletionHandler completionHandler: @escaping () -> Void) {
        let tid = response.notification.request.content.threadIdentifier
        if tid.hasPrefix("dm-"), let hash = UInt32(tid.dropFirst(3)) {
            Task { @MainActor in self.model?.openConversation(threadHash: hash) }
        }
        completionHandler()
    }
}

/// " 0x<hex8>" for a present sender hash, "" otherwise. Convention: any hash shown to a human is
/// hex8 (the wire carries decimal u32 — that stays raw-line-only).
private func hex(_ h: UInt32?) -> String {
    guard let h, h != 0 else { return "" }
    return " 0x" + KeyHash(h).hex8
}

/// A short human description of an inbound event for the debug console.
func describe(_ inbound: Inbound) -> String {
    switch inbound {
    case .ack(let a):                              return "ack \(a.code) ctr=\(a.ctr) qd=\(a.queueDepth)"
    case .messageReceived(let o, let c, let h, _, let layer, let cr, let b):  return "msg_recv from \(o)\(hex(h))\(layer.map { " L\($0)" } ?? "")\(cr == true ? " 🔒" : "") ctr=\(c): \(b)"
    case .channelReceived(let o, let ch, _, _, let layer, let b): return "channel_recv ch\(ch) from \(o)\(layer.map { " L\($0)" } ?? ""): \(b)"
    case .sendAcked(let d, let c):                 return "send_acked dst=\(d) ctr=\(c)"
    case .sendFailed(let d, let c, let r):         return "send_failed dst=\(d) ctr=\(c)\(r.map { " \($0)" } ?? "")"
    case .e2eAcked(let d, let c, let h):           return "e2e_acked dst=\(d) ctr=\(c)\(hex(h)) (delivered)"
    case .peerKeySet(let h, let p):                return "peerkey_set \(h.hex8)\(p ? " pinned" : "")"
    case .peerKeyError(let r):                     return "peerkey_err \(r)"
    case .reqPubkeySent(let h):                    return "reqpubkey_sent \(h.hex8)"
    case .peerKeyCached(let h, let p):             return "peer_key_cached \(h.hex8)\(p ? " pinned" : "")"
    case .hashResolved(let n, let a, let h):       return "hash_resolved \(h.hex8) → node \(n)\(a ? " (auth)" : "")"
    case .ready(let r):                            return "ready id=\(r.id) key=\(r.key.hex8) sf=\(r.routingSF)"
    case .status(let s):                           return "status id=\(s.id) \(s.state) up=\(s.uptimeMs.map(String.init) ?? "—") routes=\(s.routes.map(String.init) ?? "—")"
    case .route(let r):                            return "route dest=\(r.dest) next=\(r.next) hops=\(r.hops) score=\(r.score)"
    case .routesEnd(let n):                        return "routes_end count=\(n)"
    case .cfg(let c):                              return "cfg node=\(c.nodeID) sf=\(c.routingSF) freq=\(c.freqMHz)"
    case .configAdopted(let lin, let ep, let leaf, let lvl): return "config_adopted lineage=\(lin) epoch=\(ep) leaf=\(leaf ?? "—") level=\(lvl.map(String.init) ?? "—")"
    case .joinRefused(let r, let tv, let mv):      return "join_refused \(r)\(tv.map { " their=\($0)" } ?? "")\(mv.map { " my=\($0)" } ?? "")"
    case .inboxEntry(let e):                       return "inbox_\(e.kind.rawValue) seq=\(e.seq) from \(e.origin)\(hex(e.senderHash)) ctr=\(e.ctr): \(e.body)"
    case .inboxEnd(let dm, let chan, let epoch, let count, _): return "inbox_end dm=\(dm) chan=\(chan) epoch=\(epoch.map(String.init) ?? "—") count=\(count)"
    case .event(let t, _):                         return "event \(t)"
    case .log(let m):                              return "log: \(m)"
    case .error(let code, let msg):                return "err \(code)\(msg.map { ": \($0)" } ?? "")"
    case .raw(let s):                              return s
    }
}
