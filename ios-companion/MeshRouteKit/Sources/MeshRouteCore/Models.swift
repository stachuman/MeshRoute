// MeshRouteCore — Models.swift
//
// The app's domain types. THE WHOLE REASON THE APP EXISTS: the node is name-agnostic — it
// knows short ids + key_hash32, never names — so the app owns the name↔hash map and the
// durable archive. These are plain value types (no SwiftData here); the app maps them to
// @Model classes for persistence, keeping Core testable on macOS.

import Foundation
import MeshRouteWire

/// A named peer. Identity is the stable `key_hash32`; the short id is disposable (it can change
/// via DAD/heal), so a DM thread is keyed by hash, not id.
public struct Contact: Identifiable, Hashable, Sendable {
    public var hash: KeyHash
    public var name: String
    public var lastKnownID: UInt8?     // most recent short id seen for this hash (resolve surfaces it)
    public var id: KeyHash { hash }

    public init(hash: KeyHash, name: String, lastKnownID: UInt8? = nil) {
        self.hash = hash; self.name = name; self.lastKnownID = lastKnownID
    }
}

/// A conversation: a DM thread (keyed by the peer's hash) or a channel feed (by channel id).
public enum ThreadKey: Hashable, Sendable {
    case dm(KeyHash)
    case channel(UInt8)
}

public enum MessageDirection: String, Hashable, Sendable, Codable {
    case incoming, outgoing
}

/// Lifecycle of a message we sent (incoming messages are always `.received`).
public enum DeliveryState: String, Hashable, Sendable, Codable {
    case outbox       // composed while DISCONNECTED — waits in the outbox, drained FIFO on connect
    case sending      // composed, line written, no ack yet
    case queued       // node accepted it (ack: queued, ctr assigned)
    case acked        // link/forward ack returned (send_acked) — accepted/relayed, not yet end-to-end confirmed
    case deliveredE2E // an E2E delivery RECEIPT arrived (live `e2e_acked` / pulled `inbox_dm type:e2e_ack`) — the RECIPIENT confirmed (D25)
    case failed       // send_failed, or an error ack
    case received     // an inbound message
}

public struct ChatMessage: Identifiable, Hashable, Sendable {
    public var id: UUID
    public var thread: ThreadKey
    public var direction: MessageDirection
    public var body: String
    public var timestamp: Date
    public var state: DeliveryState
    /// Sender short id for an inbound message (origin); nil for outgoing.
    public var origin: UInt8?
    /// The node's message counter — the send id (outgoing) or the received message id (inbound DM).
    public var ctr: Int?
    /// Per-store inbox seq, set for messages sourced from a `pull_inbox` (nil for live pushes / outgoing).
    public var seq: UInt32?
    /// Full 32-bit channel_msg_id — the channel identity (channel messages only; nil for DM / outgoing).
    public var channelMsgID: UInt32?
    /// The sender's stable key_hash32 (DM SOURCE_HASH) — the DM identity + thread key; nil/0 = legacy DM.
    public var senderHash: UInt32?

    public init(id: UUID = UUID(), thread: ThreadKey, direction: MessageDirection, body: String,
                timestamp: Date, state: DeliveryState, origin: UInt8? = nil, ctr: Int? = nil,
                seq: UInt32? = nil, channelMsgID: UInt32? = nil, senderHash: UInt32? = nil) {
        self.id = id; self.thread = thread; self.direction = direction; self.body = body
        self.timestamp = timestamp; self.state = state; self.origin = origin; self.ctr = ctr
        self.seq = seq; self.channelMsgID = channelMsgID; self.senderHash = senderHash
    }

    /// Stable identity for an INBOUND message, shared by the live push and the inbox-pull paths so a
    /// record never duplicates across them OR across an epoch-reset re-pull from seq 0. nil when we can't
    /// form it. seq/epoch are deliberately NOT part of identity — those reset on a store wipe.
    public var inboundDedupKey: MessageIdentity? {
        guard direction == .incoming else { return nil }
        switch thread {
        case .dm:
            guard let ctr else { return nil }
            return .dm(senderHash: senderHash, origin: origin, ctr: ctr)
        case .channel:
            guard let channelMsgID else { return nil }
            return .channel(msgID: channelMsgID)
        }
    }
}

/// The stable dedup identity (firmware review). A DM uses the sender's stable `key_hash32` when present
/// (`sender_hash != 0` — the 8-bit `origin` is reassignable), else falls back to `(origin, ctr)`. A
/// channel uses the full 32-bit `channel_msg_id` (exact, no body tiebreaker). seq/epoch are NOT identity
/// (they're per-epoch/transport; a re-pull-from-0 must merge).
public enum MessageIdentity: Hashable, Sendable {
    case dmByHash(hash: UInt32, ctr: Int)
    case dmByID(origin: UInt8, ctr: Int)
    case channel(msgID: UInt32)

    /// Build the DM identity: prefer the stable sender hash; fall back to the short id for legacy DMs.
    public static func dm(senderHash: UInt32?, origin: UInt8?, ctr: Int) -> MessageIdentity? {
        if let h = senderHash, h != 0 { return .dmByHash(hash: h, ctr: ctr) }
        guard let origin else { return nil }
        return .dmByID(origin: origin, ctr: ctr)
    }
}

/// The node's leaf-membership state (R6 / D26), derived from `ready` + the live `config_adopted` push.
/// Drives the Node-screen membership chip and gates the Leave action.
public struct LeafMembership: Hashable, Sendable {
    public var lineage: Int        // lineage_id; 0 = unmanaged / standalone
    public var epoch: Int          // config_epoch
    public var leaf: String?       // leaf name (nil when unset)
    public var layer: Int?         // the 1..255 layer id (⚠ interim = the wire leaf nibble until the firmware plumbs the full id)
    public var synced: Bool

    public init(lineage: Int, epoch: Int, leaf: String?, layer: Int?, synced: Bool) {
        self.lineage = lineage; self.epoch = epoch; self.leaf = leaf; self.layer = layer; self.synced = synced
    }

    public enum State: Hashable, Sendable { case unmanaged, joining, member }
    public var state: State {
        if lineage == 0 { return .unmanaged }
        return synced ? .member : .joining
    }
    public var isManaged: Bool { lineage != 0 }
    public var label: String {
        switch state {
        case .unmanaged: return "Unmanaged · standalone"
        case .joining:   return leaf.map { "Joining \($0)…" } ?? "Joining…"
        case .member:    return "Member of \(leaf ?? "layer \(layer ?? 0)")"
        }
    }

    /// From a `ready` snapshot — nil on pre-R6 firmware (which omits `lineage`).
    public static func from(_ r: NodeReady) -> LeafMembership? {
        guard let lineage = r.lineage else { return nil }
        return LeafMembership(lineage: lineage, epoch: r.configEpoch ?? 0, leaf: r.leaf, layer: r.layer,
                              synced: r.synced ?? (lineage == 0 || (r.configEpoch ?? 0) > 0))
    }
    /// From a `config_adopted` push — `synced` is derived (`lineage==0 || epoch>0`).
    public static func adopted(lineage: Int, epoch: Int, leaf: String?, layer: Int?) -> LeafMembership {
        LeafMembership(lineage: lineage, epoch: epoch, leaf: leaf, layer: layer, synced: lineage == 0 || epoch > 0)
    }
}

/// The name↔hash map the app owns. Resolves a heard short id to a contact when a binding is known.
public struct ContactBook: Sendable {
    public private(set) var contacts: [KeyHash: Contact] = [:]
    public init(_ contacts: [Contact] = []) { for c in contacts { self.contacts[c.hash] = c } }

    public mutating func upsert(_ contact: Contact) { contacts[contact.hash] = contact }
    public func contact(for hash: KeyHash) -> Contact? { contacts[hash] }

    /// Record that `hash` currently holds short id `id` (from a resolve / hash_resolved / heard origin).
    public mutating func bind(hash: KeyHash, toID id: UInt8) {
        if var c = contacts[hash] { c.lastKnownID = id; contacts[hash] = c }
    }
    /// Reverse: which contact currently holds this short id (best-effort, last binding wins).
    public func contact(forID id: UInt8) -> Contact? {
        contacts.values.first { $0.lastKnownID == id }
    }
    public var all: [Contact] { Array(contacts.values) }
}
