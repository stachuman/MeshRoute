// MeshRouteCore — ConversationStore.swift
//
// The in-memory working set of threads + messages, with the dedup the design calls for:
// merge inbound DMs by (origin, ctr) so a reconnect / inbox pull never duplicates, and match
// our outgoing sends to their send_acked/send_failed by ctr. Pure + testable; the app persists
// a mirror of this in SwiftData.

import Foundation
import MeshRouteWire

public struct ConversationStore: Sendable {
    /// All messages, grouped by thread, kept in timestamp order.
    public private(set) var threads: [ThreadKey: [ChatMessage]] = [:]
    private var seenInbound: Set<MessageIdentity> = []

    public init() {}

    public func messages(in thread: ThreadKey) -> [ChatMessage] { threads[thread] ?? [] }
    public var threadKeys: [ThreadKey] { Array(threads.keys) }

    // ---- inbound ----

    /// Ingest a decoded push. Returns the message it produced (nil if deduped / not a message event).
    @discardableResult
    public mutating func ingest(_ inbound: Inbound, now: Date) -> ChatMessage? {
        switch inbound {
        case .messageReceived(let origin, let ctr, let senderHash, let seq, _, _, let body):
            return insertInboundDM(origin: UInt8(clamping: origin), ctr: ctr, senderHash: senderHash,
                                   seq: seq, body: body, now: now)
        case .channelReceived(let origin, let channelID, let channelMsgID, let seq, _, let body):
            return insertChannel(origin: UInt8(clamping: origin), channelID: UInt8(clamping: channelID),
                                 channelMsgID: channelMsgID, seq: seq, body: body, now: now)
        case .sendAcked(_, let ctr):
            updateOutgoing(ctr: ctr, to: .acked); return nil
        case .sendFailed(_, let ctr, _):
            updateOutgoing(ctr: ctr, to: .failed); return nil
        default:
            return nil
        }
    }

    private mutating func insertInboundDM(origin: UInt8, ctr: Int, senderHash: UInt32?, seq: UInt32?,
                                          body: String, now: Date) -> ChatMessage? {
        guard let key = MessageIdentity.dm(senderHash: senderHash, origin: origin, ctr: ctr) else { return nil }
        guard !seenInbound.contains(key) else { return nil }   // dedup by (sender_hash, ctr) | (origin, ctr)
        seenInbound.insert(key)
        let msg = ChatMessage(thread: dmThread(senderHash: senderHash, origin: origin), direction: .incoming,
                              body: body, timestamp: now, state: .received, origin: origin, ctr: ctr,
                              seq: seq, senderHash: senderHash)
        append(msg)
        return msg
    }

    /// Ingest one record from a `pull_inbox` stream. Dedups against the live path by the kind's stable
    /// identity (DM = (sender_hash, ctr) | (origin, ctr); channel = channel_msg_id) so a message received
    /// live and later pulled — or re-pulled from 0 after an epoch reset — does not duplicate. The phone
    /// stamps wall-clock `now` (the node only knows uptime); `seq` is retained for cursor advancement.
    @discardableResult
    public mutating func ingestInbox(_ entry: InboxEntry, now: Date) -> ChatMessage? {
        let origin = UInt8(clamping: entry.origin)
        let key: MessageIdentity
        let thread: ThreadKey
        switch entry.kind {
        case .dm:
            guard let k = MessageIdentity.dm(senderHash: entry.senderHash, origin: origin, ctr: entry.ctr) else { return nil }
            key = k
            thread = dmThread(senderHash: entry.senderHash, origin: origin)
        case .channel:
            guard let mid = entry.channelMsgID else { return nil }   // a channel record must carry its id
            key = .channel(msgID: mid)
            thread = .channel(UInt8(clamping: entry.channelID))
        }
        guard !seenInbound.contains(key) else { return nil }   // dedup-on-import vs the live + prior-pull archive
        seenInbound.insert(key)
        let msg = ChatMessage(thread: thread, direction: .incoming, body: entry.body, timestamp: now,
                              state: .received, origin: origin, ctr: entry.ctr, seq: entry.seq,
                              channelMsgID: entry.channelMsgID, senderHash: entry.senderHash)
        append(msg)
        return msg
    }

    /// A DM thread key. With a sender_hash we key by the sender's STABLE hash → straight into the contact's
    /// thread, no resolve/rekey. Without it (legacy DM) we stage under a pseudo-hash == id until a resolve
    /// binds it (rekeyDM moves it).
    private func dmThread(senderHash: UInt32?, origin: UInt8) -> ThreadKey {
        if let h = senderHash, h != 0 { return .dm(KeyHash(h)) }
        return .dm(KeyHash(UInt32(origin)))
    }

    private mutating func insertChannel(origin: UInt8, channelID: UInt8, channelMsgID: UInt32?,
                                        seq: UInt32?, body: String, now: Date) -> ChatMessage? {
        if let mid = channelMsgID {                            // dedup live channels too, now we have the id
            let key = MessageIdentity.channel(msgID: mid)
            guard !seenInbound.contains(key) else { return nil }
            seenInbound.insert(key)
        }
        let msg = ChatMessage(thread: .channel(channelID), direction: .incoming, body: body,
                              timestamp: now, state: .received, origin: origin, ctr: nil, seq: seq,
                              channelMsgID: channelMsgID)
        append(msg)
        return msg
    }

    // ---- outbound ----

    /// Record a message we just sent (state .sending). Call `attach(ctr:)` when its ack arrives.
    @discardableResult
    public mutating func appendOutgoing(thread: ThreadKey, body: String, now: Date) -> ChatMessage {
        let msg = ChatMessage(thread: thread, direction: .outgoing, body: body,
                              timestamp: now, state: .sending)
        append(msg)
        return msg
    }

    /// Bind an outgoing message (by its local UUID) to the node's ctr from the queued ack.
    public mutating func attach(ctr: Int, toMessage id: UUID, state: DeliveryState = .queued) {
        mutate(id: id) { $0.ctr = ctr; $0.state = state }
    }

    public mutating func updateState(of id: UUID, to state: DeliveryState) {
        mutate(id: id) { $0.state = state }
    }

    /// Flip the outgoing message with this ctr to a new delivery state (from send_acked/send_failed).
    private mutating func updateOutgoing(ctr: Int, to state: DeliveryState) {
        for (thread, var list) in threads {
            var changed = false
            for i in list.indices where list[i].direction == .outgoing && list[i].ctr == ctr {
                list[i].state = state; changed = true
            }
            if changed { threads[thread] = list }
        }
    }

    // ---- helpers ----

    private mutating func append(_ msg: ChatMessage) {
        threads[msg.thread, default: []].append(msg)
        threads[msg.thread]?.sort { ($0.timestamp, $0.seq ?? 0) < ($1.timestamp, $1.seq ?? 0) }
    }

    private mutating func mutate(id: UUID, _ body: (inout ChatMessage) -> Void) {
        for (thread, var list) in threads {
            if let i = list.firstIndex(where: { $0.id == id }) {
                body(&list[i]); threads[thread] = list; return
            }
        }
    }

    /// Re-key an id-only DM thread to the peer's real hash once a binding resolves it.
    public mutating func rekeyDM(fromID id: UInt8, toHash hash: KeyHash) {
        let from = ThreadKey.dm(KeyHash(UInt32(id)))
        let to = ThreadKey.dm(hash)
        guard from != to, let moving = threads[from] else { return }
        let retargeted = moving.map { m -> ChatMessage in var m = m; m.thread = to; return m }
        threads[to, default: []].append(contentsOf: retargeted)
        threads[to]?.sort { ($0.timestamp, $0.seq ?? 0) < ($1.timestamp, $1.seq ?? 0) }
        threads[from] = nil
    }
}
