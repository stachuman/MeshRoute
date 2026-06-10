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
    private var seenInbound: Set<InboundKey> = []

    public init() {}

    public func messages(in thread: ThreadKey) -> [ChatMessage] { threads[thread] ?? [] }
    public var threadKeys: [ThreadKey] { Array(threads.keys) }

    // ---- inbound ----

    /// Ingest a decoded push. Returns the message it produced (nil if deduped / not a message event).
    @discardableResult
    public mutating func ingest(_ inbound: Inbound, now: Date) -> ChatMessage? {
        switch inbound {
        case .messageReceived(let origin, let ctr, let body):
            return insertInboundDM(origin: UInt8(clamping: origin), ctr: ctr, body: body, now: now)
        case .channelReceived(let origin, let channelID, let body):
            return insertChannel(origin: UInt8(clamping: origin), channelID: UInt8(clamping: channelID),
                                 body: body, now: now)
        case .sendAcked(_, let ctr):
            updateOutgoing(ctr: ctr, to: .acked); return nil
        case .sendFailed(_, let ctr):
            updateOutgoing(ctr: ctr, to: .failed); return nil
        default:
            return nil
        }
    }

    private mutating func insertInboundDM(origin: UInt8, ctr: Int, body: String, now: Date) -> ChatMessage? {
        let key = InboundKey(origin: origin, ctr: ctr)
        guard !seenInbound.contains(key) else { return nil }   // dedup by (origin, ctr)
        seenInbound.insert(key)
        // A DM thread is keyed by the peer's HASH, but an inbound DM only carries the short id. Until a
        // binding resolves the hash we can't key by hash — so we stage id-only DMs under a pseudo-hash
        // derived from the id; the app re-keys to the real hash when a resolve binds it.
        let msg = ChatMessage(thread: .dm(KeyHash(UInt32(origin))), direction: .incoming, body: body,
                              timestamp: now, state: .received, origin: origin, ctr: ctr)
        append(msg)
        return msg
    }

    private mutating func insertChannel(origin: UInt8, channelID: UInt8, body: String, now: Date) -> ChatMessage {
        let msg = ChatMessage(thread: .channel(channelID), direction: .incoming, body: body,
                              timestamp: now, state: .received, origin: origin, ctr: nil)
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
        threads[msg.thread]?.sort { $0.timestamp < $1.timestamp }
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
        threads[to]?.sort { $0.timestamp < $1.timestamp }
        threads[from] = nil
    }
}
