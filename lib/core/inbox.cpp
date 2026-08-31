// MeshRoute — lib/core/inbox.cpp  (persistent inbox logic; platform-neutral)
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// The Inbox owns the record (de)serialization, the monotonic-seq-across-reboot rule (§6), record-on-
// delivery, and the cursor-based pull. The bytes + drop-oldest eviction + persisted counters live
// behind InboxStore (a RAM fake in tests, a segmented-LittleFS log on device). No heap, no exceptions.
// See docs/superpowers/specs/2026-06-10-persistent-inbox-spec.md.
#include "inbox.h"
#include "frame_codec.h"   // DataType (DATA_TYPE_E2E_ACK) — the `type` byte's value space

namespace MESHROUTE_NS {
namespace {

// ---- little-endian field (de)serialization (self-contained; no wire.h dep) ----
inline void w_u16(uint8_t*& p, uint16_t v) { *p++ = uint8_t(v); *p++ = uint8_t(v >> 8); }
inline void w_u32(uint8_t*& p, uint32_t v) { for (int i = 0; i < 4; ++i) { *p++ = uint8_t(v); v >>= 8; } }
inline void w_u64(uint8_t*& p, uint64_t v) { for (int i = 0; i < 8; ++i) { *p++ = uint8_t(v); v >>= 8; } }
inline uint16_t r_u16(const uint8_t* p) { return uint16_t(p[0] | (uint16_t(p[1]) << 8)); }
inline uint32_t r_u32(const uint8_t* p) { uint32_t v = 0; for (int i = 3; i >= 0; --i) v = (v << 8) | p[i]; return v; }
inline uint64_t r_u64(const uint8_t* p) { uint64_t v = 0; for (int i = 7; i >= 0; --i) v = (v << 8) | p[i]; return v; }

// [seq u32][kind u8][origin u8][channel_id u8][msg_id u32][sender_hash u32][rx_time_ms u64][layer_id u8][enc u8][type u8][team_id u32][origin_layer u8][body_len u8][body] — all LE.
uint16_t serialize(uint8_t* out, uint32_t seq, InboxKind kind, uint8_t origin, uint8_t channel_id,
                   uint32_t msg_id, uint32_t sender_hash, uint64_t rx_time_ms, uint8_t layer_id, uint8_t enc, uint8_t type, uint32_t team_id, uint8_t origin_layer, const uint8_t* body, uint8_t len) {
    uint8_t* p = out;
    w_u32(p, seq);
    *p++ = static_cast<uint8_t>(kind);
    *p++ = origin;
    *p++ = channel_id;
    w_u32(p, msg_id);
    w_u32(p, sender_hash);
    w_u64(p, rx_time_ms);
    *p++ = layer_id;
    *p++ = enc;                                                   // §8b: sealed-delivery flag
    *p++ = type;                                                  // the frame DATA_TYPE (0 = normal DM; DATA_TYPE_E2E_ACK = receipt)
    w_u32(p, team_id);                                            // §S5: channel team scoping (0 = leaf channel / DM)
    *p++ = origin_layer;                                          // §GapA-durable: the XL sender's layer (0 = same-layer / non-XL)
    *p++ = len;
    for (uint8_t i = 0; i < len; ++i) *p++ = body ? body[i] : 0;
    return static_cast<uint16_t>(p - out);
}

// false = malformed (a torn append at a flash tail): too short for the header, or body truncated.
bool deserialize(const uint8_t* rec, uint16_t len, InboxEntry& e) {
    if (len < inbox_record_header_bytes) return false;
    const uint8_t* p = rec;
    e.seq = r_u32(p); p += 4;
    e.kind = static_cast<InboxKind>(*p++);
    e.origin = *p++;
    e.channel_id = *p++;
    e.msg_id = r_u32(p); p += 4;
    e.sender_hash = r_u32(p); p += 4;
    e.rx_time_ms = r_u64(p); p += 8;
    e.layer_id = *p++;
    e.enc = *p++;                                                 // §8b
    e.type = *p++;                                                // the frame DATA_TYPE (E2E-ack receipt vs normal DM)
    e.team_id = r_u32(p); p += 4;                                 // §S5: channel team scoping
    e.origin_layer = *p++;                                        // §GapA-durable: the XL sender's layer
    e.body_len = *p++;
    if (static_cast<uint16_t>(inbox_record_header_bytes + e.body_len) > len) return false;   // body truncated
    e.body = (e.body_len > 0) ? p : nullptr;
    return true;
}

// ---- on_init: recompute next-seq so it NEVER regresses/reuses a value (§6) ----
struct MaxSeqCtx { uint32_t max_seq; };
bool max_seq_cb(void* ctx, uint32_t seq, const uint8_t*, uint16_t) {
    auto* m = static_cast<MaxSeqCtx*>(ctx);
    if (seq > m->max_seq) m->max_seq = seq;
    return true;   // visit all
}
uint32_t restore_next(InboxStore* s) {
    MaxSeqCtx m{ 0 };
    s->read_since(0, max_seq_cb, &m);                              // stored records are the AUTHORITATIVE high-water
    const uint32_t from_records = m.max_seq + 1;                   // (drop-oldest always KEEPS the newest -> the true high-water)
    const uint32_t from_persist = s->persisted_next_seq();         // backstop for "records lost but the meta survived" (device segment loss)
    return (from_records > from_persist) ? from_records : from_persist;
    // RESIDUAL (Phase 2, device store): a FULL wipe (format-on-dirty erases BOTH the segments AND the meta,
    // spec §10) loses the high-water entirely -> seq restarts at 1 and a companion past that silently misses
    // the new messages (§6). No in-store mechanism can prevent it (the high-water is gone). The device store
    // must EITHER keep the high-water outside the wiped store (e.g. InternalFS, beside /mrid) OR expose a
    // boot-epoch that bumps on wipe so the companion detects the reset + re-syncs. Flagged for Phase 2.
}

// ---- §3.5 tombstones: the bounded pre-pass that defeats the ORDERING hazard ----
// A tombstone is appended AFTER the record it cancels, so a single streaming pass over the store learns of the
// deletion too late — it would already have emitted the deleted record. Every read therefore does a PRE-PASS that
// collects the tombstone targets first. The array is fixed (no heap, stack-local) and cannot overflow, because
// Inbox::erase refuses to append the (inbox_max_tombstones+1)-th tombstone into the same store.
struct TombSet {
    uint32_t seq[protocol::inbox_max_tombstones];
    uint8_t  n = 0;
    bool     overflowed = false;   // structurally unreachable (erase caps the writer) — asserted by a test, not assumed
    bool contains(uint32_t s) const { for (uint8_t i = 0; i < n; ++i) if (seq[i] == s) return true; return false; }
    void add(uint32_t s) { if (n < protocol::inbox_max_tombstones) seq[n++] = s; else overflowed = true; }
    void clear() { n = 0; overflowed = false; }
};
// Collect the targets of every tombstone in `store` whose target is > `since` (a target at or below the cursor is
// never streamed anyway, so it needs no slot). Uses the SAME `since` as the streaming pass: a tombstone's own seq is
// always GREATER than its target's, so a tombstone that matters is always inside read_since(since).
struct TombScan { TombSet* set; };
bool tomb_scan_cb(void* p, uint32_t seq, const uint8_t* rec, uint16_t len) {
    auto* t = static_cast<TombScan*>(p);
    InboxEntry e{};
    if (!deserialize(rec, len, e)) return true;                   // torn record -> skip, keep scanning
    (void)seq;
    if (e.type == inbox_rec_type_tombstone) t->set->add(e.msg_id);   // msg_id = the DELETED record's seq
    return true;
}
void collect_tombstones(const InboxStore* store, uint32_t since, TombSet& out) {
    out.clear();
    TombScan s{ &out };
    store->read_since(since, tomb_scan_cb, &s);
}

// ---- pull: deserialize each raw record + hand the decoded entry to the user's cb ----
struct PullTramp { Inbox::PullCb cb; void* ctx; uint16_t count; bool stop; const TombSet* tombs; };
bool pull_cb(void* p, uint32_t seq, const uint8_t* rec, uint16_t len) {
    auto* t = static_cast<PullTramp*>(p);
    InboxEntry e{};
    if (!deserialize(rec, len, e)) return true;                   // skip a torn record, keep visiting
    e.seq = seq;                                                  // the store's seq is authoritative (== record seq)
    if (e.type == inbox_rec_type_tombstone) return true;          // §3.5: the marker itself is never a message
    if (t->tombs && t->tombs->contains(e.seq)) return true;       // §3.5: this record was deleted (the pre-pass saw its marker)
    ++t->count;
    if (!t->cb(t->ctx, e)) { t->stop = true; return false; }      // user asked to stop
    return true;
}

constexpr uint8_t kSeqPersistBatch = 8;   // persist next-seq every K appends (§6: limit wear; a lost batch only skips seq forward)

}  // namespace

void Inbox::on_init(InboxStore* dm, InboxStore* chan) {
    _dm = dm; _chan = chan;
    if (!enabled()) return;                                       // optional feature: no stores -> inert
    // ⛔⛔ BOTH MOUNTS RUN, UNCONDITIONALLY, AND THE `||` SHORT-CIRCUIT IS THE BUG THIS REPLACES ([[B134]] QG
    //    round 3). `!_dm->begin() || !_chan->begin()` skipped the CHANNEL mount whenever the DM one failed, so
    //    the channel store never even attempted to mount: its `mount_fault()` stayed `none` and two corrupted
    //    keys reported `5/0` instead of `5/5`. The diagnostic then UNDER-STATES the damage — an operator reads
    //    "the DM store is corrupt" and reflashes expecting to keep their channel history, which is not there.
    // ⇒ evaluate both, THEN combine. The disable is unchanged and still all-or-nothing: a half-mounted inbox
    //   would record into one store and silently drop the other.
    const bool dm_ok = _dm->begin();
    const bool ch_ok = _chan->begin();
    if (!dm_ok || !ch_ok) {                                       // a mount/format failure -> stay DISABLED, not half-broken
        _dm = _chan = nullptr;                                    // enabled() stays false; the backend can log + retry
        return;
    }
    _dm_next   = restore_next(_dm);
    _chan_next = restore_next(_chan);
    _dm_unpersisted = _chan_unpersisted = 0;
}

uint32_t Inbox::record(InboxStore* store, uint32_t& next, uint8_t& unpersisted, InboxKind kind, uint8_t origin,
                       uint8_t channel_id, uint32_t msg_id, uint32_t sender_hash, uint8_t layer_id, const uint8_t* body, uint8_t len, uint64_t now_ms, uint8_t enc, uint8_t type, uint32_t team_id, uint8_t origin_layer, bool* appended) {
    if (len > protocol::inbox_max_body) len = protocol::inbox_max_body;   // callers already bound the body; defensive
    uint8_t buf[inbox_record_max_bytes];
    const uint32_t seq = next++;                                  // monotonic; assign-then-advance
    const uint16_t n = serialize(buf, seq, kind, origin, channel_id, msg_id, sender_hash, now_ms, layer_id, enc, type, team_id, origin_layer, body, len);
    const bool ok = store->append(seq, buf, n);                   // drop-oldest within; a flash failure drops THIS record (seq still advances — monotonic, not gapless)
    if (appended) *appended = ok;                                 // only erase() cares (a lost tombstone must NOT read as success)
    // Batched persist (§6): reset the batch ONLY on a SUCCESSFUL set_next_seq — a failed flash write keeps
    // `unpersisted` high so the next append RETRIES, instead of swallowing the failure + skipping a batch.
    if (++unpersisted >= kSeqPersistBatch && store->set_next_seq(next)) unpersisted = 0;
    return seq;                                                   // the live Push stamps this -> the app's gap detector (model B)
}

uint32_t Inbox::record_dm(uint8_t origin, uint32_t sender_hash, uint16_t ctr, uint8_t layer_id, const uint8_t* body, uint8_t len, uint64_t now_ms, uint8_t enc, uint8_t origin_layer) {
    if (!enabled()) return 0;
    return record(_dm, _dm_next, _dm_unpersisted, InboxKind::dm, origin, /*channel_id*/ 0, /*msg_id*/ ctr, sender_hash, layer_id, body, len, now_ms, enc, /*type*/ 0, /*team_id*/ 0, origin_layer);
}

uint32_t Inbox::record_channel(uint8_t channel_id, uint32_t channel_msg_id, uint8_t layer_id,
                               const uint8_t* body, uint8_t len, uint64_t now_ms, uint32_t team_id, uint8_t enc) {
    if (!enabled()) return 0;
    const uint8_t origin = static_cast<uint8_t>(channel_msg_id >> 24);   // the minter (channel_msg_id high byte)
    return record(_chan, _chan_next, _chan_unpersisted, InboxKind::channel, origin, channel_id, channel_msg_id,
                  /*sender_hash*/ 0, layer_id, body, len, now_ms, enc, /*type*/ 0, team_id, /*origin_layer*/ 0);   // §S5 team scoping; §chan-crypt CL2a: enc=1 = the post arrived SEALED and `body` is the opened plaintext
}

uint32_t Inbox::record_ack(uint8_t from_origin, uint16_t acked_ctr, uint8_t layer_id, uint64_t now_ms, uint32_t acker_hash) {
    if (!enabled()) return 0;
    // A receipt rides the DM seq-space (an E2E ack IS a DATA frame): kind=dm, type=E2E_ACK, no body, origin = the acker.
    return record(_dm, _dm_next, _dm_unpersisted, InboxKind::dm, from_origin, /*channel_id*/ 0, /*msg_id*/ acked_ctr,
                  /*sender_hash*/ acker_hash, layer_id, /*body*/ nullptr, /*len*/ 0, now_ms, /*enc*/ 0, /*type*/ DATA_TYPE_E2E_ACK, /*team_id*/ 0, /*origin_layer*/ 0);
}

uint16_t Inbox::pull(uint32_t dm_since, uint32_t chan_since, PullCb cb, void* ctx) const {
    if (!enabled() || !cb) return 0;
    TombSet tombs;                                                // ONE instance reused for both stores (128 B of stack, not 256)
    PullTramp t{ cb, ctx, 0, false, &tombs };
    collect_tombstones(_dm, dm_since, tombs);                     // §3.5 pre-pass FIRST — a tombstone follows its target
    _dm->read_since(dm_since, pull_cb, &t);                       // DM block, oldest-first
    if (!t.stop) {
        collect_tombstones(_chan, chan_since, tombs);             // the channel store's own tombstones (separate seq space)
        _chan->read_since(chan_since, pull_cb, &t);               // then channel block, oldest-first
    }
    return t.count;
}

// ---- §3.5 / §6.2 durable single-record delete (the ONE entry point; see inbox.h for the contract) ----
namespace {
// One ordered scan answers all three questions erase() must settle before it writes anything.
struct EraseScan {
    uint32_t target;
    bool     live       = false;   // a NON-tombstone record with seq == target is present
    bool     tombstoned = false;   // a tombstone for `target` already exists (a repeat delete)
    uint16_t tombs      = 0;       // tombstones in this store (the write-side cap that keeps pull's array safe)
};
bool erase_scan_cb(void* p, uint32_t seq, const uint8_t* rec, uint16_t len) {
    auto* s = static_cast<EraseScan*>(p);
    InboxEntry e{};
    if (!deserialize(rec, len, e)) return true;                   // torn record -> skip, keep scanning
    if (e.type == inbox_rec_type_tombstone) {
        ++s->tombs;
        if (e.msg_id == s->target) s->tombstoned = true;
    } else if (seq == s->target) {
        s->live = true;
    }
    return true;                                                  // ALWAYS visit all: the tombstone count needs the whole store
}
}  // namespace

InboxEraseResult Inbox::erase(InboxKind kind, uint32_t seq) {
    if (!enabled()) return InboxEraseResult::io_error;            // an unwired inbox reports io_error, NEVER success (§6.2)
    if (seq == 0)   return InboxEraseResult::not_found;           // seq 0 is the "before everything" cursor, never a record
    // ★ The identity is the PAIR — the kind SELECTS THE STORE. The two seq spaces are independent, so the same
    // number names a different message in each; resolving on seq alone would delete the wrong message.
    InboxStore*  store = (kind == InboxKind::dm) ? _dm : _chan;
    uint32_t&    next  = (kind == InboxKind::dm) ? _dm_next : _chan_next;
    uint8_t&     unpst = (kind == InboxKind::dm) ? _dm_unpersisted : _chan_unpersisted;

    EraseScan s{ seq };
    store->read_since(0, erase_scan_cb, &s);                      // whole store: liveness + double-delete + tombstone count
    // Evicted by the bounded ring, or already deleted -> MESSAGE GONE. Sequences are never reused, so this can
    // never resolve to a newer replacement record (§6.2).
    if (!s.live || s.tombstoned) return InboxEraseResult::not_found;
    // The cap that makes pull()'s pre-pass array overflow-proof. Refuse LOUD (C2) rather than write a tombstone the
    // reader might not be able to hold — that would resurrect a deleted message, the one outcome §3.5 forbids.
    if (s.tombs >= protocol::inbox_max_tombstones) return InboxEraseResult::io_error;

    bool appended = false;
    record(store, next, unpst, kind, /*origin*/ 0, /*channel_id*/ 0, /*msg_id*/ seq, /*sender_hash*/ 0,
           /*layer_id*/ 0, /*body*/ nullptr, /*len*/ 0, /*now_ms*/ 0, /*enc*/ 0, inbox_rec_type_tombstone,
           /*team_id*/ 0, /*origin_layer*/ 0, &appended);
    return appended ? InboxEraseResult::erased : InboxEraseResult::io_error;
}

bool Inbox::mark_read(InboxKind kind, uint32_t seq) {
    if (!enabled()) return false;                                 // an unwired inbox persisted nothing
    InboxStore* s = (kind == InboxKind::dm) ? _dm : _chan;
    return s->set_read_cursor(seq);                               // ⛔ the verdict is RELAYED, never discarded
}

// ---- §CUSTODY-D whole-inbox clear (the ONE orchestration authority; see inbox.h for the contract) ----
bool Inbox::clear() {
    if (!enabled()) return false;                                 // an unwired inbox reports failure, NEVER success (§7.5)

    // ---- (1) THE HIGH-WATER GOES TO THE MEDIUM BEFORE ANY RECORD IS DESTROYED (the batch-persist gap). ----
    // Both are ATTEMPTED — a `&&` here would leave the channel store's counter unwritten (and its retry latch
    // unarmed) whenever the DM one failed, which is the same short-circuit `on_init` had to remove.
    const bool dm_hw = _dm->set_next_seq(_dm_next);
    const bool ch_hw = _chan->set_next_seq(_chan_next);
    if (dm_hw)   _dm_unpersisted   = 0;                           // the batch is only cleared by a SUCCESSFUL persist
    if (ch_hw)   _chan_unpersisted = 0;
    if (!dm_hw || !ch_hw) return false;                           // ⛔ ERASE NEITHER STORE — the records are still the only witness

    // ---- (2) ONE shared target epoch for BOTH stores (§7.5.5 + the single-epoch contract at storage_epoch()). ----
    // Strictly greater than BOTH stores' current values, so it is a ratchet even if the two ever disagree (an
    // external records loss can bump one store alone at mount). Wrap keeps it non-zero: 0 means "no durable epoch".
    uint32_t target = _dm->storage_epoch();
    const uint32_t chan_epoch = _chan->storage_epoch();
    if (chan_epoch > target) target = chan_epoch;
    ++target;
    if (target == 0) target = 1;

    // ---- (3) BOTH wipes run — ⛔ no short-circuit (a partial clear must still erase everything it can). ----
    const bool dm_w = _dm->wipe(target);
    const bool ch_w = _chan->wipe(target);

    // ---- (4) Both read cursors reset (§7.5.4). Here, not inside wipe(), so `prep-restart`/`factory_reset` keep
    // their exact behaviour (C1) and both store kinds get the same treatment from the one authority. The verdict
    // counts: a cursor left on the medium pointing past records that no longer exist is unpersisted bookkeeping,
    // which is precisely what `wipe()`'s own contract calls a failure.
    const bool dm_c = _dm->set_read_cursor(0);
    const bool ch_c = _chan->set_read_cursor(0);

    return dm_w && ch_w && dm_c && ch_c;
}

void Inbox::flush() {
    if (!enabled()) return;
    // InternalFS self-heal Part 3 (2026-06-24): write ONLY a store with un-persisted appends. The periodic caller
    // now runs on a relaxed cadence, and a quiet store must NOT issue a set_next_seq every cycle — that unconditional
    // write was a top contributor to the InternalFS write churn (the corruption window). Nothing dirty -> no write.
    if (_dm_unpersisted   && _dm->set_next_seq(_dm_next))     _dm_unpersisted   = 0;   // reset the batch only on a successful persist
    if (_chan_unpersisted && _chan->set_next_seq(_chan_next)) _chan_unpersisted = 0;
}

}  // namespace meshroute
