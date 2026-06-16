// MeshRouteCompanion — Entities.swift
//
// The durable archive (the phone, not the bounded-inbox node, is the long-term store). SwiftData
// @Model classes + mapping to/from the MeshRouteCore value types. The app uses @Query over these
// directly as the UI source of truth; AppModel writes them (with the dedup rule from ConversationStore).

import Foundation
import SwiftData
import MeshRouteWire
import MeshRouteCore

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
    var contact: Contact {
        Contact(hash: keyHash, name: name, lastKnownID: lastKnownID.map { UInt8(clamping: $0) })
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
    var ackRequested: Bool = false // outgoing: an E2E delivery ack was requested (send_ack/sendhash_ack, D16)
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
