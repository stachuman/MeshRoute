// MeshRoute — lib/core/inbox.cpp  (persistent inbox logic; platform-neutral)
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// The Inbox owns the record (de)serialization, the monotonic-seq-across-reboot rule (§6), record-on-
// delivery, and the cursor-based pull. The bytes + drop-oldest eviction + persisted counters live
// behind InboxStore (a RAM fake in tests, a segmented-LittleFS log on device). No heap, no exceptions.
// See docs/superpowers/specs/2026-06-10-persistent-inbox-spec.md.
#include "inbox.h"

namespace MESHROUTE_NS {
namespace {

// ---- little-endian field (de)serialization (self-contained; no wire.h dep) ----
inline void w_u16(uint8_t*& p, uint16_t v) { *p++ = uint8_t(v); *p++ = uint8_t(v >> 8); }
inline void w_u32(uint8_t*& p, uint32_t v) { for (int i = 0; i < 4; ++i) { *p++ = uint8_t(v); v >>= 8; } }
inline void w_u64(uint8_t*& p, uint64_t v) { for (int i = 0; i < 8; ++i) { *p++ = uint8_t(v); v >>= 8; } }
inline uint16_t r_u16(const uint8_t* p) { return uint16_t(p[0] | (uint16_t(p[1]) << 8)); }
inline uint32_t r_u32(const uint8_t* p) { uint32_t v = 0; for (int i = 3; i >= 0; --i) v = (v << 8) | p[i]; return v; }
inline uint64_t r_u64(const uint8_t* p) { uint64_t v = 0; for (int i = 7; i >= 0; --i) v = (v << 8) | p[i]; return v; }

// [seq u32][kind u8][origin u8][channel_id u8][msg_id u32][sender_hash u32][rx_time_ms u64][layer_id u8][enc u8][body_len u8][body] — all LE.
uint16_t serialize(uint8_t* out, uint32_t seq, InboxKind kind, uint8_t origin, uint8_t channel_id,
                   uint32_t msg_id, uint32_t sender_hash, uint64_t rx_time_ms, uint8_t layer_id, uint8_t enc, const uint8_t* body, uint8_t len) {
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

// ---- pull: deserialize each raw record + hand the decoded entry to the user's cb ----
struct PullTramp { Inbox::PullCb cb; void* ctx; uint16_t count; bool stop; };
bool pull_cb(void* p, uint32_t seq, const uint8_t* rec, uint16_t len) {
    auto* t = static_cast<PullTramp*>(p);
    InboxEntry e{};
    if (!deserialize(rec, len, e)) return true;                   // skip a torn record, keep visiting
    e.seq = seq;                                                  // the store's seq is authoritative (== record seq)
    ++t->count;
    if (!t->cb(t->ctx, e)) { t->stop = true; return false; }      // user asked to stop
    return true;
}

constexpr uint8_t kSeqPersistBatch = 8;   // persist next-seq every K appends (§6: limit wear; a lost batch only skips seq forward)

}  // namespace

void Inbox::on_init(InboxStore* dm, InboxStore* chan) {
    _dm = dm; _chan = chan;
    if (!enabled()) return;                                       // optional feature: no stores -> inert
    if (!_dm->begin() || !_chan->begin()) {                       // a mount/format failure -> stay DISABLED, not half-broken
        _dm = _chan = nullptr;                                    // enabled() stays false; the backend can log + retry
        return;
    }
    _dm_next   = restore_next(_dm);
    _chan_next = restore_next(_chan);
    _dm_unpersisted = _chan_unpersisted = 0;
}

uint32_t Inbox::record(InboxStore* store, uint32_t& next, uint8_t& unpersisted, InboxKind kind, uint8_t origin,
                       uint8_t channel_id, uint32_t msg_id, uint32_t sender_hash, uint8_t layer_id, const uint8_t* body, uint8_t len, uint64_t now_ms, uint8_t enc) {
    if (len > protocol::inbox_max_body) len = protocol::inbox_max_body;   // callers already bound the body; defensive
    uint8_t buf[inbox_record_max_bytes];
    const uint32_t seq = next++;                                  // monotonic; assign-then-advance
    const uint16_t n = serialize(buf, seq, kind, origin, channel_id, msg_id, sender_hash, now_ms, layer_id, enc, body, len);
    (void)store->append(seq, buf, n);                             // drop-oldest within; a flash failure drops THIS record (seq still advances — monotonic, not gapless)
    // Batched persist (§6): reset the batch ONLY on a SUCCESSFUL set_next_seq — a failed flash write keeps
    // `unpersisted` high so the next append RETRIES, instead of swallowing the failure + skipping a batch.
    if (++unpersisted >= kSeqPersistBatch && store->set_next_seq(next)) unpersisted = 0;
    return seq;                                                   // the live Push stamps this -> the app's gap detector (model B)
}

uint32_t Inbox::record_dm(uint8_t origin, uint32_t sender_hash, uint16_t ctr, uint8_t layer_id, const uint8_t* body, uint8_t len, uint64_t now_ms, uint8_t enc) {
    if (!enabled()) return 0;
    return record(_dm, _dm_next, _dm_unpersisted, InboxKind::dm, origin, /*channel_id*/ 0, /*msg_id*/ ctr, sender_hash, layer_id, body, len, now_ms, enc);
}

uint32_t Inbox::record_channel(uint8_t channel_id, uint32_t channel_msg_id, uint8_t layer_id,
                               const uint8_t* body, uint8_t len, uint64_t now_ms) {
    if (!enabled()) return 0;
    const uint8_t origin = static_cast<uint8_t>(channel_msg_id >> 24);   // the minter (channel_msg_id high byte)
    return record(_chan, _chan_next, _chan_unpersisted, InboxKind::channel, origin, channel_id, channel_msg_id,
                  /*sender_hash*/ 0, layer_id, body, len, now_ms, /*enc*/ 0);   // channels are cleartext today
}

uint16_t Inbox::pull(uint32_t dm_since, uint32_t chan_since, PullCb cb, void* ctx) const {
    if (!enabled() || !cb) return 0;
    PullTramp t{ cb, ctx, 0, false };
    _dm->read_since(dm_since, pull_cb, &t);                       // DM block, oldest-first
    if (!t.stop) _chan->read_since(chan_since, pull_cb, &t);      // then channel block, oldest-first
    return t.count;
}

void Inbox::mark_read(InboxKind kind, uint32_t seq) {
    if (!enabled()) return;
    InboxStore* s = (kind == InboxKind::dm) ? _dm : _chan;
    s->set_read_cursor(seq);
}

void Inbox::flush() {
    if (!enabled()) return;
    if (_dm->set_next_seq(_dm_next))     _dm_unpersisted   = 0;   // reset the batch only on a successful persist
    if (_chan->set_next_seq(_chan_next)) _chan_unpersisted = 0;
}

}  // namespace meshroute
