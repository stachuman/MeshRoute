// MeshRoute — lib/core/node_channel.cpp  (channel-message gossip plane, ROADMAP §3)
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Phase 1 of the CHANNEL_PULL port: the channel buffer + per-origin anti-spam + DATA-M
// ingestion + send_channel origination. The digest gossip, the jittered pull, the responder,
// and the M-broadcast tx land in Phase 2. SINGLE-LAYER only: gateways skip the whole gossip
// plane (Principle 11) — they still FORWARD M-frames via the normal DATA path, but never buffer,
// advertise, or pull. Mirrors dv_dual_sf.lua: channel_msg_id :2239, channel_buffer_find :3426,
// channel_buffer_mark_seen_by :3434, channel_origin_admit :3456, channel_buffer_pick_eviction
// :3485, channel_buffer_add :3511, the DATA-M ingest :10942, send_channel :12126.
//
// DELIBERATE device divergences from the Lua BASELINE (draw-free plane — no determinism impact):
//   - per-origin anti-spam ledger is a map<origin, FIXED array[20]> (Lua: unbounded per-origin
//     table). Per-origin events are bounded by the 20-distinct cap, so the fixed array holds them
//     EXACTLY — same throttle semantics, no heap growth.
//   - seen_by is a 256-bit bitmap per entry (Lua: a Lua set table) — O(1) set + O(neighbours) cover.
// Part of Node (declared in node.h).
#include "node.h"
#include "frame_codec.h"

#include <cstring>

namespace meshroute {

// ---- channel_msg_id mint (dv:2239): origin<<24 | (key_hash32 LOW 16)<<8 | ctr low 8, big-endian on wire.
uint32_t Node::channel_msg_id_mint(uint8_t origin, uint32_t key_hash32, uint8_t ctr) {
    return (static_cast<uint32_t>(origin) << 24)
         | ((key_hash32 & 0xffffu) << 8)
         | (static_cast<uint32_t>(ctr) & 0xffu);
}

// ---- seen_by bitmap helpers (neighbour id 0..255 -> bit) ---------------------------------------
static inline bool seen_test(const uint8_t* bm, uint8_t nbr) { return (bm[nbr >> 3] >> (nbr & 7)) & 1u; }
static inline bool seen_set(uint8_t* bm, uint8_t nbr) {        // returns true if newly set
    const uint8_t mask = static_cast<uint8_t>(1u << (nbr & 7));
    if (bm[nbr >> 3] & mask) return false;
    bm[nbr >> 3] |= mask; return true;
}

// ---- buffer find / seen_by (dv:3426 / 3434) ----------------------------------------------------
int Node::channel_buffer_find(uint32_t id) const {
    for (uint16_t i = 0; i < _channel_buffer_n; ++i) if (_channel_buffer[i].id == id) return static_cast<int>(i);
    return -1;
}
bool Node::channel_mark_seen_by(uint32_t id, uint8_t neighbour) {
    const int i = channel_buffer_find(id);
    if (i < 0) return false;
    return seen_set(_channel_buffer[i].seen_by, neighbour);
}

// ---- per-origin anti-spam admission (dv:3456). Distinct-id count over a sliding window; a repeat
//      id REFRESHES (not re-counts) so a heavily re-gossiped legit msg can't false-throttle its
//      origin. origin==self bypasses (own posts use the origination self-cap). Returns admit. ----
bool Node::channel_origin_admit(uint8_t origin, uint32_t msg_id) {
    if (origin == _node_id) return true;                        // self bypasses
    const uint64_t now    = _hal.now();
    const uint64_t cutoff = (now >= protocol::channel_origin_window_ms) ? now - protocol::channel_origin_window_ms : 0;
    ChannelOriginLedger& L = _per_origin_channel[origin];       // map insert-on-miss (default-constructed n=0)
    // Prune in place (keep in-window), refreshing the matching id; events are unique-id (dups refresh),
    // so the kept count IS the distinct count (matching the Lua's `seen` set).
    uint8_t k = 0; bool dup = false;
    for (uint8_t i = 0; i < L.n; ++i) {
        if (L.ev[i].t_ms < cutoff) continue;                    // aged out
        if (L.ev[i].id == msg_id) { L.ev[i].t_ms = now; dup = true; }
        L.ev[k++] = L.ev[i];
    }
    L.n = k;
    if (dup) return true;                                       // repeat id -> refreshed + admitted, not re-counted
    if (L.n >= protocol::channel_origin_max_per_window) {       // over cap -> drop the frame entirely
        EventField f[] = { { .key = "origin", .type = EventField::T::i64, .i = origin },
                           { .key = "msg_id", .type = EventField::T::i64, .i = static_cast<int64_t>(msg_id) },
                           { .key = "count",  .type = EventField::T::i64, .i = L.n } };
        _hal.emit("channel_drop_originator_throttle", f, 3);
        return false;
    }
    if (L.n < protocol::channel_origin_max_per_window) L.ev[L.n++] = { msg_id, now };   // record the new distinct id
    return true;
}

// ---- eviction pick (dv:3485): the OLDEST entry whose seen_by covers all live 1-hop neighbours
//      ("safe"); else the absolute oldest (index 0, "fallback"). No neighbours -> fallback. -------
int Node::channel_buffer_pick_eviction(bool* safe) const {
    *safe = false;
    if (_channel_buffer_n == 0) return -1;
    uint8_t nbrs[protocol::cap_routes]; uint8_t nn = 0;         // live direct neighbours (rt hops==1)
    for (uint8_t i = 0; i < _rt_count; ++i)
        if (_rt[i].n > 0 && _rt[i].candidates[0].hops == 1) nbrs[nn++] = _rt[i].dest;
    if (nn == 0) return 0;                                      // no neighbours observed -> oldest (fallback)
    for (uint16_t i = 0; i < _channel_buffer_n; ++i) {         // oldest-first
        bool all = true;
        for (uint8_t j = 0; j < nn; ++j) if (!seen_test(_channel_buffer[i].seen_by, nbrs[j])) { all = false; break; }
        if (all) { *safe = true; return static_cast<int>(i); }
    }
    return 0;                                                   // none fully seen -> oldest (fallback)
}

// ---- buffer add (dv:3511): evict (safe-then-oldest) when full, then append at the tail (FIFO). ---
void Node::channel_buffer_add(const ChannelEntry& e) {
    if (_channel_buffer_n >= protocol::cap_channel_buffer) {
        bool safe = false;
        const int idx = channel_buffer_pick_eviction(&safe);
        if (idx >= 0) {
            const uint32_t evicted_id = _channel_buffer[idx].id;
            // remove [idx], shift the tail down to keep insertion order (oldest at [0])
            const uint16_t tail = static_cast<uint16_t>(_channel_buffer_n - idx - 1);
            if (tail) std::memmove(&_channel_buffer[idx], &_channel_buffer[idx + 1], tail * sizeof(ChannelEntry));
            --_channel_buffer_n;
            EventField f[] = { { .key = "id",   .type = EventField::T::i64, .i = static_cast<int64_t>(evicted_id) },
                               { .key = "mode", .type = EventField::T::str, .s = safe ? "safe" : "fallback" } };
            _hal.emit("channel_msg_evicted", f, 2);
        }
    }
    _channel_buffer[_channel_buffer_n++] = e;
}

// ---- promiscuous-overhear pull cancel (dv:11006). Phase 1: the ring is empty (scheduling is Phase 2),
//      so this is a no-op today; wired now so the ingest path is complete. -------------------------
void Node::cancel_channel_pull(uint32_t id, uint8_t overheard_from) {
    for (uint8_t i = 0; i < protocol::cap_channel_pull_pending; ++i) {
        ChannelPullPending& p = _channel_pull_pending[i];
        if (p.active && p.id == id) {
            p.active = false;
            EventField f[] = { { .key = "id",            .type = EventField::T::i64, .i = static_cast<int64_t>(id) },
                               { .key = "overheard_from", .type = EventField::T::i64, .i = overheard_from } };
            _hal.emit("channel_pull_suppressed", f, 2);
        }
    }
}

// ---- DATA-M ingestion (dv:10942): admit (gateways included) -> if !gateway, merge into the buffer
//      (new -> add+dirty+seen_by+cancel-pull; existing -> mark seen_by). The caller (handle_data)
//      handles the ACK / forward of the underlying DATA frame; this is the gossip side-effect. -----
void Node::ingest_channel_m(const data_m_inner& m, uint8_t next, uint8_t dst, uint8_t from) {
    const uint32_t id     = m.channel_msg_id;
    const uint8_t  origin = static_cast<uint8_t>((id >> 24) & 0xff);    // the minter (dv:2912)
    if (!channel_origin_admit(origin, id)) return;             // over per-origin budget -> drop (not buffered/forwarded by caller)
    if (_cfg.is_gateway) return;                               // Principle 11: gateways don't gossip (still forward via DATA path)
    const int existing = channel_buffer_find(id);
    if (existing < 0) {                                        // NEW -> buffer it
        ChannelEntry e{};
        e.id = id; e.channel_id = m.channel_id; e.flavor = m.flavor; e.origin = origin;
        e.dirty = true; e.bcn_ad_count = 0; e.received_at = _hal.now();
        seen_set(e.seen_by, from);                            // the immediate sender holds it
        e.payload_len = static_cast<uint16_t>(m.body.size() > protocol::channel_msg_max_payload_bytes
                                              ? protocol::channel_msg_max_payload_bytes : m.body.size());
        if (e.payload_len) std::memcpy(e.payload, m.body.data(), e.payload_len);
        channel_buffer_add(e);
        const char* src = (next == _node_id && dst == _node_id) ? "pull_target"
                        : (next == _node_id)                    ? "forwarder" : "overheard";
        EventField f[] = { { .key = "id",         .type = EventField::T::i64, .i = static_cast<int64_t>(id) },
                           { .key = "channel_id", .type = EventField::T::i64, .i = m.channel_id },
                           { .key = "source",     .type = EventField::T::str, .s = src },
                           { .key = "from",       .type = EventField::T::i64, .i = from } };
        _hal.emit("channel_msg_received", f, 4);
        cancel_channel_pull(id, from);                        // we got it -> drop any pending pull for it
    } else {                                                   // ALREADY HAVE IT -> just track the holder
        channel_mark_seen_by(id, from);
        EventField f[] = { { .key = "id",   .type = EventField::T::i64, .i = static_cast<int64_t>(id) },
                           { .key = "from", .type = EventField::T::i64, .i = from } };
        _hal.emit("channel_msg_already_present", f, 2);
    }
}

// ---- send_channel origination (dv:12126): mint an id, buffer it dirty (the next BCN digest will
//      advertise it; neighbours pull on demand — no proactive broadcast). Counts toward the unified
//      self-origination budget. Returns the per-origin ctr used. -----------------------------------
uint16_t Node::do_send_channel(uint8_t channel_id, const uint8_t* body, uint8_t body_len) {
    const uint16_t c = next_ctr(_node_id);
    const uint32_t id = channel_msg_id_mint(_node_id, _key_hash32, static_cast<uint8_t>(c & 0xff));
    ChannelEntry e{};
    e.id = id; e.channel_id = channel_id; e.flavor = protocol::channel_flavor_public; e.origin = _node_id;
    e.dirty = true; e.bcn_ad_count = 0; e.received_at = _hal.now();
    e.payload_len = (body_len > protocol::channel_msg_max_payload_bytes)
                    ? protocol::channel_msg_max_payload_bytes : body_len;
    if (e.payload_len) std::memcpy(e.payload, body, e.payload_len);
    channel_buffer_add(e);
    self_originate_observe();                                  // Inc 4: channels share the DM self-cap ledger
    EventField f[] = { { .key = "id",         .type = EventField::T::i64, .i = static_cast<int64_t>(id) },
                       { .key = "channel_id", .type = EventField::T::i64, .i = channel_id },
                       { .key = "source",     .type = EventField::T::str, .s = "self_originate" } };
    _hal.emit("channel_msg_received", f, 3);
    return c;
}

}  // namespace meshroute
