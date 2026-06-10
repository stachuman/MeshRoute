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
    case sending      // composed, line written, no ack yet
    case queued       // node accepted it (ack: queued, ctr assigned)
    case acked        // link/E2E ack returned (send_acked)
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

    public init(id: UUID = UUID(), thread: ThreadKey, direction: MessageDirection, body: String,
                timestamp: Date, state: DeliveryState, origin: UInt8? = nil, ctr: Int? = nil) {
        self.id = id; self.thread = thread; self.direction = direction; self.body = body
        self.timestamp = timestamp; self.state = state; self.origin = origin; self.ctr = ctr
    }

    /// Dedup identity for an INBOUND DM: (origin, ctr). A reconnect/inbox-pull must not duplicate it.
    /// nil when we can't dedup (channel pushes carry no ctr on the wire — see console_json channel_recv).
    public var inboundDedupKey: InboundKey? {
        guard direction == .incoming, let origin, let ctr else { return nil }
        return InboundKey(origin: origin, ctr: ctr)
    }
}

public struct InboundKey: Hashable, Sendable {
    public let origin: UInt8
    public let ctr: Int
    public init(origin: UInt8, ctr: Int) { self.origin = origin; self.ctr = ctr }
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
