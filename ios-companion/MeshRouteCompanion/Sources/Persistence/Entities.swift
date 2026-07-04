// MeshRouteCompanion — Entities.swift
//
// The durable archive (the phone, not the bounded-inbox node, is the long-term store). SwiftData
// @Model classes + mapping to/from the MeshRouteCore value types. The app uses @Query over these
// directly as the UI source of truth; AppModel writes them (with the dedup rule from ConversationStore).

import Foundation
import SwiftData
import MeshRouteWire
import MeshRouteCore

// LEGACY (D28 / D-b): superseded by `NodeEntity` — a contact is now a NAMED node. Kept in the schema for the
// one-time migration copy (AppModel.migrateContactsToNodesIfNeeded); no app logic reads it. Drop in a later store migration.
@Model
final class ContactEntity {
    @Attribute(.unique) var hashValue32: UInt32
    var name: String
    var lastKnownID: Int?          // most recent short id seen for this hash
    var createdAt: Date

    init(hashValue32: UInt32, name: String, lastKnownID: Int? = nil, createdAt: Date = .now) {
        self.hashValue32 = hashValue32; self.name = name
        self.lastKnownID = lastKnownID; self.createdAt = createdAt
    }

    var keyHash: KeyHash { KeyHash(hashValue32) }
}

/// The unified Known-Nodes Directory (D28 / Option A): EVERY node the app has heard of, keyed by the stable
/// `key_hash32`. A **contact** is just a node with a user-given `name` or `favorite`. Heard-only nodes (from a
/// DM sender, a resolve, a route) live here too — feeding the Mesh tab's map + list. Position/battery/role stay
/// nil until firmware provides them (the have-now sources fill the rest — see the redesign spec §3.3).
@Model
final class NodeEntity {
    @Attribute(.unique) var hash32: UInt32     // key_hash32 — the directory key
    var name: String?                          // user-given name ⇒ a "contact"; nil = heard-only
    var favorite: Bool = false                 // pinned as a contact even without a custom name
    var lastKnownID: Int?                      // most recent short id (reassignable; hash is the identity)
    var role: String = "unknown"               // "normal" | "gateway" | "mobile" | "unknown"
    var lineage: Int?                          // leaf membership (0 = unmanaged; nil = unknown)
    var leafName: String?
    var layer: Int?                            // the 1..255 layer id (leaf = layer & 0x0F); WIP node-directory field
    var latE7: Int?                            // last-known position (firmware-gated for remotes; degrees × 1e7)
    var lonE7: Int?
    var battMv: Int?                           // last-known battery (firmware-gated)
    var linkScoreQ4: Int?                      // route score (Q4 dB) when reachable
    var hops: Int?                             // route distance when reachable
    var verified: Bool = false                 // key PINNED via a QR scan (safety-numbers) vs on-air TOFU
    var firstSeen: Date
    var lastSeen: Date                         // any evidence: message, resolve, route, ready, beacon
    var positionAt: Date?                      // when lat/lon was last set (staleness / TTL)

    init(hash32: UInt32, name: String? = nil, favorite: Bool = false, lastKnownID: Int? = nil,
         role: String = "unknown", firstSeen: Date = .now, lastSeen: Date = .now) {
        self.hash32 = hash32; self.name = name; self.favorite = favorite; self.lastKnownID = lastKnownID
        self.role = role; self.firstSeen = firstSeen; self.lastSeen = lastSeen
    }

    var keyHash: KeyHash { KeyHash(hash32) }
    var isContact: Bool { name != nil || favorite }
    var hasPosition: Bool { latE7 != nil && lonE7 != nil }
    /// Display name: the given name, else "Node <id>", else the short hash.
    func displayName() -> String {
        if let n = name, !n.isEmpty { return n }
        if let id = lastKnownID { return "Node \(id)" }
        return "0x" + keyHash.hex8
    }
}

@Model
final class MessageEntity {
    @Attribute(.unique) var id: UUID
    var threadKind: String         // "dm" | "channel"
    var threadHash: UInt32         // dm: the peer's key_hash32 (or a pseudo-hash == id until resolved)
    var threadChannel: Int         // channel: the channel id
    var directionRaw: String       // MessageDirection
    var body: String
    var timestamp: Date
    var stateRaw: String           // DeliveryState
    var origin: Int?               // sender short id (incoming)
    var ctr: Int?                  // node message counter — DM identity component (16-bit msg_id)
    var channelMsgID: Int?         // channel identity — full 32-bit channel_msg_id (nil for DM)
    var senderHash: Int?           // DM identity — sender's stable key_hash32 (nil/0 = legacy DM)
    var isRead: Bool = true        // unread badge driver; incoming inserts set false (default true keeps
                                   // pre-existing + outgoing rows read after the lightweight migration)
    var crypted: Bool = false      // E2E: incoming = the DATA CRYPTED flag (from the push, firmware-pending);
                                   // outgoing = sent encrypted. false = plaintext.
    var ackRequested: Bool = false // outgoing: an E2E delivery ack was requested (the -a flag, D16)
    var failReason: String?        // outgoing fail reason (E2E): "no_pubkey" → offer Request-key/Scan; "key_ready"
                                   // → set when peer_key_cached arrives so the bubble offers a secure resend.

    init(id: UUID, thread: ThreadKey, direction: MessageDirection, body: String,
         timestamp: Date, state: DeliveryState, origin: Int?, ctr: Int?, channelMsgID: Int? = nil,
         senderHash: Int? = nil) {
        self.id = id
        self.channelMsgID = channelMsgID
        self.senderHash = senderHash
        switch thread {
        case .dm(let h):      self.threadKind = "dm";      self.threadHash = h.value; self.threadChannel = -1
        case .channel(let c): self.threadKind = "channel"; self.threadHash = 0;       self.threadChannel = Int(c)
        }
        self.directionRaw = direction.rawValue
        self.body = body
        self.timestamp = timestamp
        self.stateRaw = state.rawValue
        self.origin = origin
        self.ctr = ctr
    }

    var threadKey: ThreadKey {
        threadKind == "channel" ? .channel(UInt8(clamping: threadChannel)) : .dm(KeyHash(threadHash))
    }
    var direction: MessageDirection { MessageDirection(rawValue: directionRaw) ?? .incoming }
    var state: DeliveryState { DeliveryState(rawValue: stateRaw) ?? .received }
}

@Model
final class ChannelLabelEntity {     // a LOCAL name for a channel number ("3 = Sailing crew") — never on the wire
    @Attribute(.unique) var channelID: Int
    var name: String
    init(channelID: Int, name: String) { self.channelID = channelID; self.name = name }
}

@Model
final class NodeProfileEntity {
    @Attribute(.unique) var key32: UInt32
    var shortID: Int
    var mode: String
    var routingSF: Int
    var gateway: Bool
    var lastSeen: Date
    // Per-node inbox sync state { epoch, dm_cursor, chan_cursor }. The cursors are meaningful only within
    // an epoch; a node store wipe bumps the epoch and we re-pull from 0 (see AppModel.startInboxSync).
    var inboxEpoch: Int = 0
    var dmCursor: Int = 0
    var chanCursor: Int = 0

    init(key32: UInt32, shortID: Int, mode: String, routingSF: Int, gateway: Bool, lastSeen: Date = .now) {
        self.key32 = key32; self.shortID = shortID; self.mode = mode
        self.routingSF = routingSF; self.gateway = gateway; self.lastSeen = lastSeen
    }

    var syncState: InboxSyncState {
        get { InboxSyncState(epoch: UInt32(clamping: inboxEpoch),
                             dmCursor: UInt32(clamping: dmCursor),
                             chanCursor: UInt32(clamping: chanCursor)) }
        set { inboxEpoch = Int(newValue.epoch); dmCursor = Int(newValue.dmCursor); chanCursor = Int(newValue.chanCursor) }
    }
}
