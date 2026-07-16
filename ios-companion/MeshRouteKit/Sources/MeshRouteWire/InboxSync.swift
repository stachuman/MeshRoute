// MeshRouteWire — InboxSync.swift
//
// The companion catch-up contract for the firmware persistent inbox
// (docs/superpowers/specs/2026-06-10-persistent-inbox-spec.md §8). Two independent per-store seq
// spaces (DM + channel); the app holds a cursor per store and advances each as it pulls. `rx_time_ms`
// is node UPTIME (no node RTC yet) — the phone stamps wall-clock receive time on pull. (origin, ctr)
// is the message identity for app-side dedup, shared with the live msg_recv/channel_recv path.
//
// PROPOSED wire shapes (to confirm with the firmware side as it wires Phase 3):
//   app→node : pull_inbox <dm_since> <chan_since>      mark_read <dm|chan> <seq>
//   node→app : {"ev":"inbox_dm","seq":N,"origin":N,"ctr":N,"rx_ms":N,"body":"…"}
//              {"ev":"inbox_channel","seq":N,"origin":N,"channel_id":N,"ctr":N,"rx_ms":N,"body":"…"}
//              {"ev":"inbox_end","dm_seq":N,"chan_seq":N,"count":N}   // newest seqs + #streamed

import Foundation

public enum InboxKind: String, Hashable, Sendable, Codable {
    case dm
    case channel
    /// The console token used in `mark_read <dm|chan> <seq>` (the spec abbreviates channel to `chan`).
    public var commandToken: String { self == .dm ? "dm" : "chan" }
}

/// One decoded inbox record (a durable DM or channel message pulled from the node). Identity differs by
/// kind (firmware review): a DM is `(origin, ctr)` where ctr is the firmware's 16-bit msg_id; a channel
/// is the full 32-bit `channelMsgID` (= origin<<24 | key_hash16<<8 | ctr) — exact, so no body tiebreaker.
public struct InboxEntry: Hashable, Sendable {
    public let seq: UInt32          // per-store monotonic cursor (1-based; 0 = "before everything")
    public let kind: InboxKind
    public let origin: Int          // sender (DM) / minter (channel) short id (== channelMsgID >> 24)
    public let channelID: Int       // 0 for a DM
    public let ctr: Int             // DM identity component (16-bit msg_id). Channel: low-8, informational only.
    public let senderHash: UInt32?  // DM only: the sender's stable key_hash32 (SOURCE_HASH); 0/nil = legacy DM
    public let channelMsgID: UInt32? // channel identity: the full 32-bit channel_msg_id (nil for a DM)
    public let layerID: Int?        // receiving layer (D12; 0 on single-layer, nil on legacy firmware)
    public let crypted: Bool?       // DM only: the DATA CRYPTED flag (E2E, firmware-pending; nil = unknown)
    public let isReceipt: Bool      // DM only: a `type:"e2e_ack"` delivery RECEIPT (not a message) — rides the DM cursor; matches the OUTBOX (D25)
    public let teamID: String?      // channel only (D30/S5): the team_id hex string; nil = a leaf channel
    public let rxTimeMs: UInt64     // node uptime ms at receive (phone stamps wall-clock on pull)
    public let body: String

    public init(seq: UInt32, kind: InboxKind, origin: Int, channelID: Int, ctr: Int,
                senderHash: UInt32? = nil, channelMsgID: UInt32? = nil, layerID: Int? = nil,
                crypted: Bool? = nil, isReceipt: Bool = false, teamID: String? = nil, rxTimeMs: UInt64, body: String) {
        self.seq = seq; self.kind = kind; self.origin = origin; self.channelID = channelID
        self.ctr = ctr; self.senderHash = senderHash; self.channelMsgID = channelMsgID
        self.layerID = layerID; self.crypted = crypted; self.isReceipt = isReceipt; self.teamID = teamID
        self.rxTimeMs = rxTimeMs; self.body = body
    }
}

/// The app's per-node sync state: an inbox **epoch** plus a cursor per independent store. seq is only
/// monotonic WITHIN an epoch — if the node's flash inbox is wiped (bootloader re-flash erasing QSPI, or
/// a format-on-dirty recovery; spec §10/§14) its seq restarts at 1, so a node we'd synced to cursor 500
/// would re-emit new messages at seq 1,2,3 — all < 500 — and a naive "seq > 500" would SILENTLY MISS
/// them. The epoch guards that: the node bumps it on any store reset; the app detects the change and
/// re-pulls from 0. (Where the epoch rides on the wire is TBD with the BLE companion — this type stays
/// abstracted over that: callers feed it `nodeEpoch` from wherever the wire exposes it.)
public struct InboxSyncState: Hashable, Sendable, Codable {
    public var epoch: UInt32       // the node inbox epoch these cursors belong to (0 = never synced)
    public var dmCursor: UInt32
    public var chanCursor: UInt32
    public init(epoch: UInt32 = 0, dmCursor: UInt32 = 0, chanCursor: UInt32 = 0) {
        self.epoch = epoch; self.dmCursor = dmCursor; self.chanCursor = chanCursor
    }

    /// Reconcile against the node's CURRENT inbox epoch and return the cursors to pull from. On an epoch
    /// change (store wiped / first sync) reset BOTH cursors to 0 → a full re-pull → and adopt the new
    /// epoch. Dedup-on-import (see ConversationStore) then merges the re-pull into the existing archive.
    @discardableResult
    public mutating func beginSync(nodeEpoch: UInt32) -> (dmSince: UInt32, chanSince: UInt32) {
        if nodeEpoch != epoch {
            epoch = nodeEpoch
            dmCursor = 0
            chanCursor = 0
        }
        return (dmCursor, chanCursor)
    }

    /// Advance the relevant store's cursor to the max seq seen (seqs arrive oldest-first within a store).
    public mutating func advance(with entry: InboxEntry) { advance(kind: entry.kind, seq: entry.seq) }

    /// Advance a store's cursor to the max seq seen — also the high-water for LIVE gap detection (model B).
    public mutating func advance(kind: InboxKind, seq: UInt32) {
        switch kind {
        case .dm:      dmCursor = max(dmCursor, seq)
        case .channel: chanCursor = max(chanCursor, seq)
        }
    }

    public func cursor(for kind: InboxKind) -> UInt32 { kind == .dm ? dmCursor : chanCursor }

    /// Model "B" (2026-06-12): the per-store cursor is ALSO the live high-water. Classify a live push's
    /// `seq` against it — a jump past `high+1` means a live push was dropped (bounded drop-oldest ring),
    /// so the app backfills with `pull_inbox <high>` immediately instead of waiting for reconnect.
    public func classifyLive(kind: InboxKind, seq: UInt32) -> LiveSeq {
        let high = cursor(for: kind)
        if seq <= high      { return .duplicate }   // already held (live/pull overlap or epoch re-pull)
        if seq == high + 1  { return .contiguous }  // the expected next record
        return .gap                                 // seq > high+1 → a dropped live push between high and seq
    }
}

/// The verdict for a live push carrying a `seq` (model B). `nil`-seq pushes skip this entirely (best-effort).
public enum LiveSeq: Hashable, Sendable {
    case contiguous   // apply + advance
    case gap          // pull_inbox from the current cursor to backfill the dropped record(s), then apply
    case duplicate    // already held → dedup by stable identity, don't regress the cursor
}
