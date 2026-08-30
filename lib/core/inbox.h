// MeshRoute — lib/core/inbox.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Persistent inbox (DM + channel durable history). A platform-neutral CORE — the Inbox logic + two
// append-log STORES behind an abstract InboxStore HAL — exactly the Hal / device_nv split, so all the
// logic is unit-testable in `native` against a RAM-backed fake (no flash). lib/core depends ONLY on
// the InboxStore interface; the LittleFS/QSPI-backed stores live in src/ (device, spec Phase 2). The
// node holds a bounded rolling history; the phone is the long-term archive (pulls incrementally).
// See docs/superpowers/specs/2026-06-10-persistent-inbox-spec.md.
#pragma once
#ifndef MESHROUTE_NS
#define MESHROUTE_NS meshroute   // Slice 5 faithful two-lib: gateway variant compiles with -DMESHROUTE_NS=meshroute_gw
#endif
#include "protocol_constants.h"
#include <cstdint>

namespace MESHROUTE_NS {

// One inbox entry, DECODED (what pull / a store dump yields). kind: DM or channel; channel_id is 0 for
// a DM. `msg_id` is the EXACT message identity: a DM's 16-bit ctr (pair with `origin`), or a channel's
// FULL 32-bit channel_msg_id (`origin<<24 | key_hash16<<8 | ctr` — self-contained; do not truncate it to
// the low byte, the app dedups channel messages by the whole id). rx_time_ms = node uptime at receive
// (absolute wall-clock is deferred — the companion stamps it on pull). `body` points into the store's
// record bytes — valid for the duration of the visiting callback only.
enum class InboxKind : uint8_t { dm = 0, channel = 1 };
struct InboxEntry {
    uint32_t       seq;
    InboxKind      kind;
    uint8_t        origin;       // DM sender / channel minter (== channel msg_id >> 24)
    uint8_t        channel_id;
    uint32_t       msg_id;       // DM: ctr (16-bit); channel: the full 32-bit channel_msg_id
    uint32_t       sender_hash;  // DM: the sender's key_hash32 (the STABLE identity, when SOURCE_HASH was set); 0 if absent / channel
    uint8_t        layer_id;     // §2/Q13: the FULL 8-bit receiving layer id — disambiguates `origin` across a gateway's two leaves
    uint8_t        enc;          // §8b: 1 = this DM was delivered SEALED (DATA_FLAG_CRYPTED + a successful e2e_open); 0 = plaintext / channel
    uint8_t        type;         // the frame DATA_TYPE: 0 = a normal app DM / channel; DATA_TYPE_E2E_ACK = an E2E-ack RECEIPT (no body, origin = the dest that confirmed, msg_id = the acked ctr). Room for H_ANSWER etc. later. ★ inbox_rec_type_tombstone (0xFE) is NOT a DataType — it marks a §3.5 DELETION marker (msg_id = the deleted record's seq, body_len 0); pull() never emits one.
    uint32_t       team_id;      // §S5: a channel message's team scoping (0 = a plain leaf channel / DM). Carries the ACTUAL id (not a flag) so post-team-switch history stays correctly labelled.
    uint8_t        origin_layer; // §GapA durable: the cross-layer SENDER's layer (layer_ids[0] of the preserved XL path; 0 = same-layer / non-XL). Durable twin of Push.origin_layer so a pulled record still yields the (layer_path, hash) reply address.
    uint64_t       rx_time_ms;
    const uint8_t* body;
    uint8_t        body_len;
};

// Serialized record = [seq u32][kind u8][origin u8][channel_id u8][msg_id u32][sender_hash u32][rx_time_ms u64]
// [layer_id u8][enc u8][type u8][team_id u32][origin_layer u8][body_len u8][body], all LITTLE-endian. Fixed 32-B header + body.
// The STORE adds the on-flash framing ([u16 total_len] …); Inbox owns this record (de)serialization. The app's DM identity is
// (sender_hash, ctr) when sender_hash != 0, else (origin, ctr); channel identity is the full msg_id. (§2/Q13: +layer_id 2026-06-13;
// §8b: +enc 2026-06-16; +type 2026-06-23 (E2E-ack receipts); §S5: +team_id 2026-07-16; §GapA-durable: +origin_layer 2026-07-19 —
// each bumps the device store version so old records are rejected.)
inline constexpr uint16_t inbox_record_header_bytes = 4 + 1 + 1 + 1 + 4 + 4 + 8 + 1 + 1 + 1 + 4 + 1 + 1; // = 32
inline constexpr uint16_t inbox_record_max_bytes    = inbox_record_header_bytes + protocol::inbox_max_body;  // 273 (32 + 241)

// ---- §3.5/§6.2 durable single-record delete: the TOMBSTONE record type -----------------------------
// A deletion is an APPENDED marker, never a rewrite or a segment erase (owner ruling 2026-08-06). The marker is an
// ORDINARY record — same 32-B header, same store, its OWN seq off the same monotonic counter — discriminated by the
// `type` byte. `msg_id` carries the TARGET record's seq; `body_len` is 0. Inbox::pull() filters both the tombstone
// and its target, so no reader (companion pull_inbox, the OLED list) ever sees either.
//   Why the `type` byte and not a new header field: the record layout is UNCHANGED, so every already-stored record
//   stays parseable and NO store-format version bump is needed. That is not contorting the encoding to dodge a bump
//   (M3 — a bump would be free); it is that a bump here would WIPE the on-node history to buy nothing.
//   Why 0xFE and not 0xFF: `type` holds a frame DataType, and since the §CUSTODY-A namespace transition the whole
//   DataType space is bounded ABOVE by the protocol-internal range's top, `data_type_internal_hi` = 0xBF
//   (frame_codec.h). ⇒ 0xFE cannot collide with any DataType STRUCTURALLY, not merely by staying ahead of a
//   sequential allocator — a static_assert in test/test_data_type_namespace.cpp pins that ordering. ⛔ CORRECTED
//   2026-08-29: this used to read "1..19 allocated, sequentially, from 1", which stopped being true with the
//   transition (values are a RANGE contract now, and 0x05/0x8A/0x94 are allocated-but-not-live). 0xFF is skipped
//   because it is the ERASED-FLASH byte
//   and a value that a blank region could decode into is a bad discriminator, even though the segment framing
//   ([u16 framed_len], rejected when < 6 or past the segment) already stops a blank region from parsing at all.
inline constexpr uint8_t inbox_rec_type_tombstone = 0xFE;

// Inbox::erase outcome — THREE distinguishable states, because §3.5 renders each differently and a bool cannot say
// which happened. ⛔ `not_found` must never be reported as success and never as failure: the UI shows MESSAGE GONE
// for one and DELETE FAILED for the other, and a visual disappearance without durable success is forbidden.
enum class InboxEraseResult : uint8_t {
    erased    = 0,   // the tombstone was APPENDED (durable); the record is gone from every future pull
    not_found = 1,   // no such live record in that kind's store — evicted by the bounded ring, or already deleted
    io_error  = 2,   // the store could not persist the tombstone (append failed / inbox disabled / tombstone cap full)
};

// ---- the storage HAL: a bounded, crash-safe append + iterate + drop-oldest record log -------------
// One instance per store (DM, channel). "Dumb bytes + bookkeeping": the store owns the bytes, the
// byte-cap drop-oldest eviction, and the two persisted counters (next-seq, read-cursor); Inbox owns the
// record format + policy. Implementations: a RAM fake (tests) + a segmented-LittleFS log (device). NO
// heap / exceptions in the contract (matches Hal).
class InboxStore {
public:
    // Visit each record with seq > `since`, OLDEST-first; `rec`/`len` are the raw record bytes. Return
    // false from cb to stop early. A function-pointer + ctx (NOT std::function) — no heap, device-safe.
    using ReadCb = bool (*)(void* ctx, uint32_t seq, const uint8_t* rec, uint16_t len);

    virtual ~InboxStore() = default;
    virtual bool     begin() = 0;                                                  // mount/init; load the persisted counters
    virtual bool     append(uint32_t seq, const uint8_t* rec, uint16_t len) = 0;   // append; drop-oldest if over the byte cap
    virtual uint16_t read_since(uint32_t since_seq, ReadCb cb, void* ctx) const = 0;  // returns # visited
    virtual uint32_t persisted_next_seq() const = 0;                               // batched next-seq, for monotonicity across reboot (§6)
    virtual bool     set_next_seq(uint32_t next) = 0;                              // persist the counter (batched)
    virtual uint32_t read_cursor() const = 0;                                      // highest seq marked read (UX only)
    virtual bool     set_read_cursor(uint32_t seq) = 0;
    virtual uint16_t count() const = 0;                                            // live record count (diag)
    // §10.1 storage epoch: a u32 that BUMPS whenever this store's records are wiped (a flash format /
    // format-on-dirty). It must persist OUTSIDE the wiped record area (e.g. on-chip NV). The companion
    // reads it; a changed epoch ⇒ the node's history was wiped ⇒ the app resets its cursors to 0 and
    // re-pulls (dedup by stable message identity makes that non-duplicating). 0 = no durable epoch.
    virtual uint32_t storage_epoch() const = 0;
    // factory_reset (§5) / `prep-restart`: drop ALL persisted records, and ★ REPORT WHETHER THAT HAPPENED.
    // ⛔ THE RETURN TYPE WAS `void` UNTIL 2026-08-28 ([[B134]] QG blocker 3) AND THAT WAS A DATA-RETENTION LIE IN
    //    THE WORST DIRECTION: every erase result was discarded, so `prep-restart` printed *"inbox cleared"* and
    //    `factory_reset confirm` carried on while records stayed RECOVERABLE on flash. For a DESTRUCTIVE verb the
    //    honest failure direction is to say it failed — a user who is told the history is gone will act as if it is.
    // Contract: `true` iff, after this call, the store holds no recoverable records AND the bookkeeping that says so
    // is persisted. A store with nothing persistent (FixedInboxStore, a RAM ring cleared by the reboot that follows)
    // trivially satisfies that, which is why the default is `true` and not a silent no-op success.
    virtual bool     wipe() { return true; }
};

// ---- the inbox logic (lib/core; platform-neutral) -------------------------------------------------
class Inbox {
public:
    using PullCb = bool (*)(void* ctx, const InboxEntry& e);   // false = stop

    // Install the two stores + restore the seq counters (begin() both; recompute next-seq, §6). NULL
    // stores leave the inbox DISABLED — an optional companion feature: record_* / pull are inert until a
    // backend wires durable storage. NOT a hidden fallback masking a misconfig: an unconfigured inbox
    // does nothing, explicitly distinct (enabled()) from a wired one; a node without a companion needs none.
    void on_init(InboxStore* dm, InboxStore* chan);
    bool enabled() const { return _dm != nullptr && _chan != nullptr; }

    // Record-on-delivery (called from the node's DM / channel deliver paths). RETURNS the assigned per-store
    // `seq` (0 if the inbox is disabled — nothing recorded). The caller stamps it into the live Push so the app
    // can unify live + pulled by seq + detect a dropped live push (the contract's model "B"); hence record
    // BEFORE enqueue_push. `sender_hash` = the DM sender's key_hash32 (stable identity, 0 if SOURCE_HASH absent).
    // The channel identity is the FULL channel_msg_id (origin = its high byte) — store it whole, not the ctr.
    uint32_t record_dm(uint8_t origin, uint32_t sender_hash, uint16_t ctr, uint8_t layer_id, const uint8_t* body, uint8_t len, uint64_t now_ms, uint8_t enc = 0, uint8_t origin_layer = 0);   // §8b: enc=1 if the DM was delivered sealed; §GapA-durable: origin_layer = the XL sender's layer (0 = same-layer)
    uint32_t record_channel(uint8_t channel_id, uint32_t channel_msg_id, uint8_t layer_id, const uint8_t* body, uint8_t len, uint64_t now_ms, uint32_t team_id = 0, uint8_t enc = 0);   // §S5 team_id scopes the durable record; §chan-crypt CL2a: enc=1 iff the post arrived CRYPTED and we OPENED it (`body` is then the recovered plaintext) — the record FORMAT always carried the field (InboxEntry::enc), so this is a parameter, not a store-version bump. APPENDED after team_id so every existing call site is untouched and defaults to 0.
    // Record an E2E-ack RECEIPT for a -a DM WE originated (the dest `from_origin` confirmed delivery of our ctr `acked_ctr`).
    // A DM-store entry under the DM seq-cursor: kind=dm, type=DATA_TYPE_E2E_ACK, origin=from_origin, msg_id=acked_ctr, body_len=0,
    // enc=0. `acker_hash` = the acker's stable key_hash32 for a cross-layer ack (the 8-bit origin aliases across leaves); 0 same-layer.
    uint32_t record_ack(uint8_t from_origin, uint16_t acked_ctr, uint8_t layer_id, uint64_t now_ms, uint32_t acker_hash = 0);

    // Companion pull: stream DM records (seq > dm_since), THEN channel records (seq > chan_since), each
    // oldest-first, via cb. Returns the total entries visited. DM-block-then-channel-block (the two seq
    // spaces are independent; there is no shared clock to interleave on — the app advances each cursor).
    // ★ §3.5 DELETE FILTER — and the ORDERING hazard it exists to defeat: a tombstone is appended AFTER the record
    // it cancels, so a single streaming pass would emit the deleted record and only LEARN of the deletion later.
    // pull() therefore runs a bounded PRE-PASS per store (read_since with the same `since`, collecting tombstone
    // targets into a stack array of protocol::inbox_max_tombstones u32 = 128 B), then streams, skipping both the
    // tombstones themselves and every targeted record. Overflow is impossible by construction: erase() refuses at
    // the same cap. Cost = the store is scanned TWICE per pull; pull is a console/UI operation, never a MAC path.
    uint16_t pull(uint32_t dm_since, uint32_t chan_since, PullCb cb, void* ctx) const;

    // §3.5 / §6.2 DURABLE SINGLE-RECORD DELETE — the ONE entry point (U1); there is deliberately no per-store
    // delete virtual, so no InboxStore implementer can be missed or silently default to a no-op: erase() is built
    // from the two operations every store already has (append + read_since), which is also why the crash-safety
    // argument is the append's own — the tombstone either lands or it does not.
    // ⛔ SUPERSEDED 2026-08-07 ([[B135]], QA): this note used to end *"and nothing else is mutated"*. THAT WAS NOT
    // TRUE OF THE STORE and it is not what the durable append guarantees. The honest contract is: on a failed
    // append **no previously readable record is corrupted or made unreachable** — a torn frame is sealed off and
    // the next append rolls to a fresh segment (segmented_inbox_store.h, §B135). What a failure CAN still mutate:
    // the append may have had to ROLL first, and a roll on a full ring evicts the oldest segment before the write
    // is attempted (drop-oldest that the next successful append would have done anyway). Nothing about the TARGET
    // record changes on `io_error`, which is the property §3.5 renders.
    // ★ IDENTITY IS THE PAIR (kind, seq): the DM and channel seq spaces are INDEPENDENT, so `seq` alone names two
    // different messages. Sequences are never reused, so a seq whose record was evicted resolves to not_found and
    // can never select a newer replacement.
    // Costs: one full read_since scan of that store (to prove the target is live, reject a double delete and count
    // the tombstones) + one append. No rewrite, no segment erase, no heap. Deleting consumes a seq of its own —
    // history keeps a HOLE, monotonicity is preserved, and the read cursor / next_seq / storage epoch keep their
    // meanings (a one-record delete is NOT a wipe and must not make the companion reset its cursors).
    InboxEraseResult erase(InboxKind kind, uint32_t seq);

    uint32_t dm_newest_seq()   const { return _dm_next   > 1 ? _dm_next   - 1 : 0; }
    uint32_t chan_newest_seq() const { return _chan_next > 1 ? _chan_next - 1 : 0; }
    // §10.1: the node's inbox storage epoch (the companion's wipe-detector). DM + channel share the device's
    // data store, so they bump together; the DM store's value is canonical. 0 when disabled.
    uint32_t storage_epoch()   const { return enabled() ? _dm->storage_epoch() : 0; }
    // ⛔ RETURNS THE PERSISTENCE VERDICT since 2026-08-29 ([[B134]] QG round 7). It was `void`, so a store that
    // could not persist the cursor was indistinguishable from one that did, and `mark_read` acked success over a
    // durable cursor that never moved. `false` = the cursor is NOT on the medium (and an unwired inbox is false,
    // never a silent success — the same rule `erase` follows).
    bool     mark_read(InboxKind kind, uint32_t seq);

    // Force-persist both next-seq counters NOW (the "/ on a timer" half of §6's batched persist). The backend
    // should call this on a periodic timer and/or before a planned reboot, to bound how far the persisted
    // high-water can lag the records (it shrinks the seq-reuse window if a device store later loses records).
    void     flush();

private:
    // `appended` (optional out): did store->append() actually succeed? The record_* paths deliberately ignore a
    // failed append (the seq still advances — monotonic, not gapless), but erase() MUST NOT: a tombstone that did
    // not reach flash is exactly the "visual disappearance without durable success" §3.5 forbids, so it needs the
    // append's real verdict. ONE serialization path serves both (U2) — the flag is the only difference.
    uint32_t record(InboxStore* store, uint32_t& next, uint8_t& unpersisted, InboxKind kind, uint8_t origin,
                    uint8_t channel_id, uint32_t msg_id, uint32_t sender_hash, uint8_t layer_id, const uint8_t* body, uint8_t len, uint64_t now_ms, uint8_t enc, uint8_t type, uint32_t team_id, uint8_t origin_layer, bool* appended = nullptr);

    InboxStore* _dm   = nullptr;
    InboxStore* _chan = nullptr;
    uint32_t    _dm_next   = 1;     // next seq to assign (1-based; seq 0 is the "before everything" pull cursor)
    uint32_t    _chan_next = 1;
    uint8_t     _dm_unpersisted   = 0;   // appends since the last set_next_seq (batched persist, §6)
    uint8_t     _chan_unpersisted = 0;
};

}  // namespace meshroute
