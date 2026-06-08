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
// Do we hold a channel msg whose id low-16 == lo? The M_BROADCAST RTS carries only the low 16 of the
// channel_msg_id; an overhearer uses this to SKIP the retune when it (probably) already has the msg (dv:2081).
bool Node::channel_have_id_lo16(uint16_t lo) const {
    for (uint16_t i = 0; i < _channel_buffer_n; ++i)
        if (static_cast<uint16_t>(_channel_buffer[i].id & 0xffff) == lo) return true;
    return false;
}

// ---- per-origin anti-spam admission (dv:3456). Distinct-id count over a sliding window; a repeat
//      id REFRESHES (not re-counts) so a heavily re-gossiped legit msg can't false-throttle its
//      origin. origin==self bypasses (own posts use the origination self-cap). Returns admit. ----
bool Node::channel_origin_admit(uint8_t origin, uint32_t msg_id) {
    if (origin == _node_id) return true;                        // self bypasses
    const uint64_t now    = _hal.now();
    const uint64_t cutoff = (now >= _cfg.channel_origin_window_ms) ? now - _cfg.channel_origin_window_ms : 0;
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
    if (L.n >= _cfg.channel_origin_max_per_window) {            // over cap -> drop the frame entirely
        MR_TELEMETRY(
            EventField f[] = { { .key = "origin",    .type = EventField::T::i64, .i = origin },
                               { .key = "msg_id",    .type = EventField::T::i64, .i = static_cast<int64_t>(msg_id) },
                               { .key = "count",     .type = EventField::T::i64, .i = L.n },
                               { .key = "threshold", .type = EventField::T::i64, .i = _cfg.channel_origin_max_per_window },
                               { .key = "window_ms", .type = EventField::T::i64, .i = static_cast<int64_t>(_cfg.channel_origin_window_ms) } };
            _hal.emit("channel_drop_originator_throttle", f, 5); );
        return false;
    }
    if (L.n < _cfg.channel_origin_max_per_window) L.ev[L.n++] = { msg_id, now };   // record the new distinct id
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
            [[maybe_unused]] const uint32_t evicted_id = _channel_buffer[idx].id;
            // remove [idx], shift the tail down to keep insertion order (oldest at [0])
            const uint16_t tail = static_cast<uint16_t>(_channel_buffer_n - idx - 1);
            if (tail) std::memmove(&_channel_buffer[idx], &_channel_buffer[idx + 1], tail * sizeof(ChannelEntry));
            --_channel_buffer_n;
            MR_TELEMETRY(
                EventField f[] = { { .key = "id",   .type = EventField::T::i64, .i = static_cast<int64_t>(evicted_id) },
                                   { .key = "mode", .type = EventField::T::str, .s = safe ? "safe" : "fallback" } };
                _hal.emit("channel_msg_evicted", f, 2); );
        }
    }
    _channel_buffer[_channel_buffer_n++] = e;
}

// ---- promiscuous-overhear pull cancel (dv:11006). We got `id` (overheard M-broadcast) OR saw a peer pull
//      it -> drop our matching pending pull(s) so we don't double-pull. The pending ring is populated by
//      process_channel_digest (Phase 2). -------------------------
void Node::cancel_channel_pull(uint32_t id, [[maybe_unused]] uint8_t overheard_from, bool peer_q) {
    for (uint8_t i = 0; i < protocol::cap_channel_pull_pending; ++i) {
        ChannelPullPending& p = _channel_pull_pending[i];
        if (p.active && p.id == id) {
            p.active = false;
            if (peer_q) {   // a peer's Q already pulled this id -> stand down so we don't double-pull (dv:11831)
                MR_TELEMETRY(
                    EventField f[] = { { .key = "id",            .type = EventField::T::i64, .i = static_cast<int64_t>(id) },
                                       { .key = "overheard_from", .type = EventField::T::str, .s = "peer_q" },
                                       { .key = "peer",          .type = EventField::T::i64, .i = overheard_from } };
                    _hal.emit("channel_pull_suppressed", f, 3); );
            } else {        // we received the msg (overheard M-broadcast) -> drop the now-moot pull (dv:11006)
                MR_TELEMETRY(
                    EventField f[] = { { .key = "id",            .type = EventField::T::i64, .i = static_cast<int64_t>(id) },
                                       { .key = "overheard_from", .type = EventField::T::i64, .i = overheard_from } };
                    _hal.emit("channel_pull_suppressed", f, 2); );
            }
        }
    }
}

// ---- DATA-M ingestion (dv:10942): admit (gateways included) -> if !gateway, merge into the buffer
//      (new -> add+dirty+seen_by+cancel-pull; existing -> mark seen_by). The caller (handle_data)
//      handles the ACK / forward of the underlying DATA frame; this is the gossip side-effect. -----
void Node::ingest_channel_m(const data_m_inner& m, uint8_t next, [[maybe_unused]] uint8_t dst, uint8_t from) {
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
        // App push: surface a NEW channel message to the app/console, like a DM's msg_recv (the device
        // console prints it; the sim observes via the emit below). Load-bearing -> OUTSIDE the wrap.
        {
            Push pu{};
            pu.kind = PushKind::channel_recv; pu.origin = origin; pu.channel_id = m.channel_id;
            pu.body_len = static_cast<uint8_t>(e.payload_len > protocol::max_payload_bytes_hard_cap
                                               ? protocol::max_payload_bytes_hard_cap : e.payload_len);
            for (uint8_t i = 0; i < pu.body_len; ++i) pu.body[i] = e.payload[i];
            enqueue_push(pu);
        }
        MR_TELEMETRY(
            const char* src = (next == _node_id && dst == _node_id) ? "pull_target"
                            : (next == _node_id)                    ? "forwarder" : "overheard";
            EventField f[] = { { .key = "id",         .type = EventField::T::i64, .i = static_cast<int64_t>(id) },
                               { .key = "channel_id", .type = EventField::T::i64, .i = m.channel_id },
                               { .key = "source",     .type = EventField::T::str, .s = src },
                               { .key = "from",       .type = EventField::T::i64, .i = from } };
            _hal.emit("channel_msg_received", f, 4); );
        if (next != _node_id) {                               // overheard (not addressed to us) -> the analyzer's cascade-overlap signal (dv:11001)
            MR_TELEMETRY(
                EventField fo[] = { { .key = "id",          .type = EventField::T::i64, .i = static_cast<int64_t>(id) },
                                    { .key = "channel_id",  .type = EventField::T::i64, .i = m.channel_id },
                                    { .key = "from",        .type = EventField::T::i64, .i = from },
                                    { .key = "intended_to", .type = EventField::T::i64, .i = next } };
                _hal.emit("channel_msg_overheard", fo, 4); );
        }
        cancel_channel_pull(id, from);                        // we got it -> drop any pending pull for it
    } else {                                                   // ALREADY HAVE IT -> just track the holder
        channel_mark_seen_by(id, from);
        MR_TELEMETRY(
            EventField f[] = { { .key = "id",   .type = EventField::T::i64, .i = static_cast<int64_t>(id) },
                               { .key = "from", .type = EventField::T::i64, .i = from } };
            _hal.emit("channel_msg_already_present", f, 2); );
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
    MR_TELEMETRY(
        EventField f[] = { { .key = "id",         .type = EventField::T::i64, .i = static_cast<int64_t>(id) },
                           { .key = "channel_id", .type = EventField::T::i64, .i = channel_id },
                           { .key = "source",     .type = EventField::T::str, .s = "self_originate" } };
        _hal.emit("channel_msg_received", f, 3); );
    return c;
}

// =============================================================================
// Phase 2 — digest gossip + the jittered pull. The ONLY rand draw in the channel
// plane is process_channel_digest's pull jitter, at the Lua's exact gate-order
// (after the have/cap/recent gates, before storage) so the streams stay aligned.
// =============================================================================

// build_channel_digest_ext (dv:1426): walk the buffer NEWEST-first, advertise up to
// channel_dirty_max_per_bcn DIRTY ids, increment each picked entry's bcn_ad_count, and retire it
// (dirty=false) once it has been advertised channel_dirty_max_advertisements times (the s12 holder
// load-bound). Writes the ext-TLV bytes into `out`; returns the byte count (0 = nothing to advertise).
// DRAW-FREE — fires on every beacon build (the side effects track per-beacon, matching the Lua).
size_t Node::build_channel_digest_ext(uint8_t* out, size_t cap) {
    uint32_t ids[protocol::channel_dirty_max_per_bcn];
    uint8_t  count = 0;
    for (int i = static_cast<int>(_channel_buffer_n) - 1; i >= 0 && count < protocol::channel_dirty_max_per_bcn; --i) {
        ChannelEntry& e = _channel_buffer[static_cast<uint16_t>(i)];
        if (!e.dirty) continue;
        ids[count++] = e.id;
        if (++e.bcn_ad_count >= _cfg.channel_dirty_max_advertisements) {   // K: per-node override (Lua k_max = node.channel_dirty_max_advertisements or 3)
            e.dirty = false;                                       // retire from advertising (still answers pulls)
            MR_TELEMETRY(
                EventField f[] = { { .key = "id",         .type = EventField::T::i64, .i = static_cast<int64_t>(e.id) },
                                   { .key = "channel_id", .type = EventField::T::i64, .i = e.channel_id },
                                   { .key = "ad_count",   .type = EventField::T::i64, .i = e.bcn_ad_count },
                                   { .key = "threshold",  .type = EventField::T::i64, .i = _cfg.channel_dirty_max_advertisements } };
                _hal.emit("channel_dirty_cleared", f, 4); );
        }
    }
    if (count == 0) return 0;
    return pack_channel_digest_tlv(ids, count, std::span<uint8_t>(out, cap));
}

// re-pull dedup ring (Lua channel_pull_recent map). recently = a pull for `id` fired within the window.
bool Node::channel_pull_recently(uint32_t id) const {
    const uint64_t now = _hal.now();
    for (uint8_t i = 0; i < _channel_pull_recent_n; ++i)
        if (_channel_pull_recent[i].id == id)
            return (now - _channel_pull_recent[i].t_ms) < protocol::channel_pull_window_ms;
    return false;
}
void Node::channel_pull_mark(uint32_t id) {
    const uint64_t now = _hal.now();
    for (uint8_t i = 0; i < _channel_pull_recent_n; ++i)
        if (_channel_pull_recent[i].id == id) { _channel_pull_recent[i].t_ms = now; return; }
    if (_channel_pull_recent_n < protocol::cap_channel_pull_recent) {
        _channel_pull_recent[_channel_pull_recent_n++] = { id, now };
    } else {                                                       // ring full -> evict the oldest
        uint8_t o = 0;
        for (uint8_t i = 1; i < _channel_pull_recent_n; ++i) if (_channel_pull_recent[i].t_ms < _channel_pull_recent[o].t_ms) o = i;
        _channel_pull_recent[o] = { id, now };
    }
}

// process_channel_digest (dv:3546): for each advertised id — if we HAVE it, mark the advertiser as a
// holder (eviction safety); else (capped at cap_channel_pulls_per_bcn_cycle/beacon, skipping ids pulled
// within the window) schedule a JITTERED pull. THE DRAW is rand(0, jitter+1) at the Lua's gate-order
// (dv:3568: after the recent gate, before storage). Gateways skip the entire plane (Principle 11).
void Node::process_channel_digest(uint8_t src, const uint32_t* ids, uint8_t count) {
    if (_cfg.is_gateway) return;
    const uint64_t now = _hal.now();
    uint8_t scheduled = 0;
    for (uint8_t k = 0; k < count; ++k) {
        const uint32_t id = ids[k];
        if (channel_buffer_find(id) >= 0) {                       // already have it -> track the holder
            channel_mark_seen_by(id, src);
            continue;
        }
        if (scheduled >= protocol::cap_channel_pulls_per_bcn_cycle) continue;   // per-beacon pull cap
        if (channel_pull_recently(id)) continue;                  // recent-window gate (BEFORE the draw)
        // THE DRAW (dv:3568) — rand(0, channel_pull_jitter_ms+1). Made here regardless of slot availability
        // (the Lua always draws + stores into an unbounded map), so the stream stays aligned.
        const uint32_t jitter = static_cast<uint32_t>(_hal.rand_range(0, static_cast<int32_t>(_cfg.channel_pull_jitter_ms) + 1));
        ++scheduled;
        // one pending slot per id: reuse the id's live slot (overwrite/re-arm), else a free slot.
        int slot = -1;
        for (uint8_t s = 0; s < protocol::cap_channel_pull_pending; ++s)
            if (_channel_pull_pending[s].active && _channel_pull_pending[s].id == id) { slot = s; break; }
        if (slot < 0)
            for (uint8_t s = 0; s < protocol::cap_channel_pull_pending; ++s)
                if (!_channel_pull_pending[s].active) { slot = static_cast<int>(s); break; }
        if (slot < 0) {                                           // ring full (Lua unbounded) — drop after the draw
            MR_TELEMETRY(
                EventField f[] = { { .key = "id", .type = EventField::T::i64, .i = static_cast<int64_t>(id) } };
                _hal.emit("channel_pull_drop_full", f, 1); );
            continue;
        }
        _channel_pull_pending[slot] = { /*active*/true, id, src, /*requested_at*/now, /*fire_at*/now + jitter };
        MR_TELEMETRY(
            EventField f[] = { { .key = "id",       .type = EventField::T::i64, .i = static_cast<int64_t>(id) },
                               { .key = "target",   .type = EventField::T::i64, .i = src },
                               { .key = "delay_ms", .type = EventField::T::i64, .i = static_cast<int64_t>(jitter) } };
            _hal.emit("channel_pull_scheduled", f, 3); );
        (void)_hal.after(jitter, kChannelPullTimerId + static_cast<uint32_t>(slot));
    }
}

// channel_pull_fire (the dv:3573 after()-closure): if the msg arrived via overhear before the jitter
// fired, suppress; else broadcast a CHANNEL_PULL Q for {id} to the advertiser + record the recent pull.
void Node::channel_pull_fire(uint8_t slot) {
    if (slot >= protocol::cap_channel_pull_pending) return;
    ChannelPullPending& p = _channel_pull_pending[slot];
    if (!p.active) return;
    p.active = false;
    const uint32_t id = p.id; const uint8_t target = p.target;
    if (channel_buffer_find(id) >= 0) {                           // got it via promiscuous overhear -> stand down
        MR_TELEMETRY(
            EventField f[] = { { .key = "id",             .type = EventField::T::i64, .i = static_cast<int64_t>(id) },
                               { .key = "overheard_from", .type = EventField::T::str, .s = "promiscuous_receive" } };
            _hal.emit("channel_pull_suppressed", f, 2); );
        return;
    }
    q_in in{};
    in.leaf_id = _cfg.leaf_id; in.src = _node_id; in.dest = target;
    in.opcode = q_opcode::channel_pull; in.mobile = _cfg.is_mobile;
    in.channel_ids = std::span<const uint32_t>(&id, 1);
    uint8_t buf[16];
    const size_t n = pack_q(in, std::span<uint8_t>(buf, sizeof(buf)));
    if (n == 0) return;
    MR_TELEMETRY(
        EventField f[] = { { .key = "id",      .type = EventField::T::i64, .i = static_cast<int64_t>(id) },
                           { .key = "target",  .type = EventField::T::i64, .i = target },
                           { .key = "trigger", .type = EventField::T::str, .s = "bcn_digest" } };  // only trigger today: a BCN digest advertised an unknown id (dv:3600)
        _hal.emit("channel_pull_sent", f, 3); );
    tx_initiating(buf, n, static_cast<int16_t>(_cfg.routing_sf), LbtKind::flood, 0);
    channel_pull_mark(id);                                        // dedup re-pulls for the window
}

// =============================================================================
// Phase 2c — the CHANNEL_PULL responder + M-broadcast tx. DRAW-FREE.
// =============================================================================

// id of an M-payload TxItem/PendingTx inner (first 4 bytes, BE channel_msg_id).
static inline uint32_t m_inner_id(const uint8_t* inner) {
    return (static_cast<uint32_t>(inner[0]) << 24) | (static_cast<uint32_t>(inner[1]) << 16)
         | (static_cast<uint32_t>(inner[2]) << 8)  |  static_cast<uint32_t>(inner[3]);
}
// Is an M-payload for `id` already in flight or queued? (the s12 pull-storm dedup, dv:11850-11867)
bool Node::channel_m_in_flight(uint32_t id) const {
    if (_pending_tx && (_pending_tx->flags & DATA_FLAG_PAYLOAD_TYPE_M)
        && _pending_tx->inner_len >= 4 && m_inner_id(_pending_tx->inner) == id) return true;
    for (uint8_t i = 0; i < _tx_queue_n; ++i)
        if ((_tx_queue[i].flags & DATA_FLAG_PAYLOAD_TYPE_M)
            && _tx_queue[i].inner_len >= 4 && m_inner_id(_tx_queue[i].inner) == id) return true;
    return false;
}

// Build an M-payload DATA (id|channel_id|flavor|body) and enqueue it as a unicast to the puller — it
// rides the normal DATA path, but the RTS carries RTS_FLAG_M_BROADCAST so overhearers can catch it
// (dv:11875-11894). The originator is US (the re-broadcaster), per the Lua.
void Node::enqueue_channel_m(uint8_t target, const ChannelEntry& e) {
    if (_tx_queue_n >= kTxQueueCap) return;                       // queue full -> drop (the puller can re-pull)
    TxItem item{};
    item.origin = _node_id; item.dst = target;
    item.ctr    = next_ctr(target); item.ctr_lo = static_cast<uint8_t>(item.ctr & 0x0F);
    item.flags  = DATA_FLAG_PAYLOAD_TYPE_M;
    item.inner[0] = static_cast<uint8_t>(e.id >> 24); item.inner[1] = static_cast<uint8_t>(e.id >> 16);
    item.inner[2] = static_cast<uint8_t>(e.id >> 8);  item.inner[3] = static_cast<uint8_t>(e.id);
    item.inner[4] = e.channel_id; item.inner[5] = e.flavor;
    for (uint16_t k = 0; k < e.payload_len; ++k) item.inner[6 + k] = e.payload[k];
    item.inner_len = static_cast<uint8_t>(6 + e.payload_len);
    item.enqueue_time_ms = _hal.now();
    _tx_queue[_tx_queue_n++] = item;
    MR_TELEMETRY(
        EventField f[] = { { .key = "id", .type = EventField::T::i64, .i = static_cast<int64_t>(e.id) },
                           { .key = "to", .type = EventField::T::i64, .i = target } };
        _hal.emit("channel_broadcast_tx", f, 2); );
}

// CHANNEL_PULL responder (dv:11821): cancel my own pending pulls for the requested ids (a peer is
// pulling them — overhear dedup), then — if WE are the addressed target — re-broadcast each held id as
// an M-payload (skipping ids already in flight/queue). Gateways: a pull is a unicast Q to a specific
// dest; a gateway only answers a pull addressed to IT (it forwarded the original DATA but holds no
// buffer, so channel_buffer_find misses -> it answers nothing). No explicit is_gateway gate needed.
void Node::handle_channel_pull(uint8_t src, uint8_t dest, const uint32_t* ids, uint8_t count) {
    for (uint8_t i = 0; i < count; ++i) cancel_channel_pull(ids[i], src, /*peer_q=*/true);   // a peer pulled these -> cancel my pending pulls (dv:11831)
    if (dest != _node_id) return;                                // only the addressed target serves the pull
    bool any = false;
    for (uint8_t i = 0; i < count; ++i) {
        const int e = channel_buffer_find(ids[i]);
        if (e < 0) continue;                                     // we don't hold it
        if (!channel_m_in_flight(ids[i])) { enqueue_channel_m(src, _channel_buffer[e]); any = true; }
        else {
            MR_TELEMETRY(
                EventField f[] = { { .key = "id", .type = EventField::T::i64, .i = static_cast<int64_t>(ids[i]) },
                                   { .key = "requester", .type = EventField::T::i64, .i = src } };
                _hal.emit("channel_broadcast_deduped", f, 2); );      // an existing M-tx already satisfies this id
        }
        channel_mark_seen_by(ids[i], src);                       // the requester expects to receive it
    }
    MR_TELEMETRY(
        EventField f[] = { { .key = "from", .type = EventField::T::i64, .i = src } };
        _hal.emit("channel_pull_received", f, 1); );
    if (any) {
        MR_TELEMETRY(
            EventField f[] = { { .key = "to", .type = EventField::T::i64, .i = src } };  // we held >=1 requested id and re-broadcast it to the puller (dv:11910)
            _hal.emit("channel_msg_pulled", f, 1); );
        become_free();                                           // kick the queue to start the M-broadcast
    }
}

}  // namespace meshroute
