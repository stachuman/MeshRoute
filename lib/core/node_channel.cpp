// MeshRoute — lib/core/node_channel.cpp  (channel-message gossip plane, ROADMAP §3)
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// The channel-message gossip plane: the channel buffer + per-origin anti-spam + send_channel
// origination, the managed FLOOD (fast primary, 2026-06-08) + the digest/CHANNEL_PULL repair
// backstop, and ingestion of the lean M frame (cmd 0xA, 2026-06-09 — its OWN frame, NOT a DATA
// inner; see frame_codec.h pack_m). Channel messages are LEAF-SCOPED — the M frame's byte-0
// leaf_id gates ingest. Gateways are consumer-not-provider per `gateway_only` (receive + pull for
// the owner; never rebroadcast/relay — see node.h §7). Mirrors dv_dual_sf.lua: channel_msg_id
// :2239, channel_buffer_find :3426, channel_buffer_mark_seen_by :3434, channel_origin_admit :3456,
// channel_buffer_pick_eviction :3485, channel_buffer_add :3511, the DATA-M ingest :10942, send_channel :12126.
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
#include <cstdio>      // snprintf — the DEBUG flood_log_coverage trace (trace_on-gated)

namespace MESHROUTE_NS {

// ---- channel_msg_id mint (dv:2239): origin<<24 | (key_hash32 LOW 16)<<8 | ctr low 8, big-endian on wire.
uint32_t Node::channel_msg_id_mint(uint8_t origin, uint32_t key_hash32, uint8_t ctr) {
    return (static_cast<uint32_t>(origin) << 24)
         | ((key_hash32 & 0xffffu) << 8)
         | (static_cast<uint32_t>(ctr) & 0xffu);
}

// ---- seen_by bitmap helpers (neighbour id 0..255 -> bit) ---------------------------------------
static inline bool seen_test(const uint8_t* bm, uint8_t nbr) { return (bm[nbr >> 3] >> (nbr & 7)) & 1u; }

// DEBUG (trace_on only): one console line dumping my hops==1 neighbours + each one's covered bit in `bm`. This is
// THE diagnostic for the asymmetric-coverage suspicion (a flood seeds "nodes I hear" but coverage is "nodes that
// hear me"). Bounded stack buffer + the n<sizeof-8 guard make it truncation-safe (a small net is ~3 neighbours).
#if 0  // FLOOD-DBG disabled 2026-06-23 — re-enable (this + the call sites + the node.h decl) for bench flood diag
void Node::flood_log_coverage(const char* tag, uint32_t id, const uint8_t* bm) const {
    char buf[128];
    int n = snprintf(buf, sizeof buf, "flood %08lX %s nbrs:", (unsigned long)id, tag);
    for (uint8_t i = 0; i < _active->_rt_count && n > 0 && n < (int)sizeof buf - 8; ++i)
        if (_active->_rt[i].n > 0 && _active->_rt[i].candidates[0].hops == 1)
            n += snprintf(buf + n, sizeof buf - (size_t)n, " %u=%c",
                          _active->_rt[i].dest, seen_test(bm, _active->_rt[i].dest) ? 'Y' : 'N');
    _hal.log(buf);
}
#endif
static inline bool seen_set(uint8_t* bm, uint8_t nbr) {        // returns true if newly set
    const uint8_t mask = static_cast<uint8_t>(1u << (nbr & 7));
    if (bm[nbr >> 3] & mask) return false;
    bm[nbr >> 3] |= mask; return true;
}

// ---- buffer find / seen_by (dv:3426 / 3434) ----------------------------------------------------
int Node::channel_buffer_find(uint32_t id) const {
    for (uint16_t i = 0; i < _active->_channel_buffer_n; ++i) if (_active->_channel_buffer[i].id == id) return static_cast<int>(i);
    return -1;
}
bool Node::channel_mark_seen_by(uint32_t id, uint8_t neighbour) {
    const int i = channel_buffer_find(id);
    if (i < 0) return false;
    return seen_set(_active->_channel_buffer[i].seen_by, neighbour);
}
// Do we hold a channel msg whose id low-16 == lo? The M_BROADCAST RTS carries only the low 16 of the
// channel_msg_id; an overhearer uses this to SKIP the retune when it (probably) already has the msg (dv:2081).
bool Node::channel_have_id_lo16(uint16_t lo) const {
    for (uint16_t i = 0; i < _active->_channel_buffer_n; ++i)
        if (static_cast<uint16_t>(_active->_channel_buffer[i].id & 0xffff) == lo) return true;
    return false;
}

// ---- per-origin anti-spam admission (dv:3456). Distinct-id count over a sliding window; a repeat
//      id REFRESHES (not re-counts) so a heavily re-gossiped legit msg can't false-throttle its
//      origin. origin==self bypasses (own posts use the origination self-cap). Returns admit. ----
bool Node::channel_origin_admit(uint8_t origin, uint32_t msg_id) {
    if (_cfg.n_layers == 2) return false;                       // Principle 11: a dual-layer gateway is OUT of the channel plane (justifies cap_channel_buffer=8)
    if (origin == _node_id) return true;                        // self bypasses
    const uint64_t now    = _hal.now();
    const uint64_t cutoff = (now >= _cfg.channel_origin_window_ms) ? now - _cfg.channel_origin_window_ms : 0;
    ChannelOriginLedger& L = _active->_per_origin_channel[origin];       // map insert-on-miss (default-constructed n=0)
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
    if (_active->_channel_buffer_n == 0) return -1;
    uint8_t nbrs[protocol::cap_routes]; uint8_t nn = 0;         // live direct neighbours (rt hops==1)
    for (uint8_t i = 0; i < _active->_rt_count; ++i)
        if (_active->_rt[i].n > 0 && _active->_rt[i].candidates[0].hops == 1) nbrs[nn++] = _active->_rt[i].dest;
    if (nn == 0) return 0;                                      // no neighbours observed -> oldest (fallback)
    for (uint16_t i = 0; i < _active->_channel_buffer_n; ++i) {         // oldest-first
        bool all = true;
        for (uint8_t j = 0; j < nn; ++j) if (!seen_test(_active->_channel_buffer[i].seen_by, nbrs[j])) { all = false; break; }
        if (all) { *safe = true; return static_cast<int>(i); }
    }
    return 0;                                                   // none fully seen -> oldest (fallback)
}

// ---- buffer add (dv:3511): evict (safe-then-oldest) when full, then append at the tail (FIFO). ---
void Node::channel_buffer_add(const ChannelEntry& e) {
    if (_active->_channel_buffer_n >= protocol::cap_channel_buffer) {
        bool safe = false;
        const int idx = channel_buffer_pick_eviction(&safe);
        if (idx >= 0) {
            [[maybe_unused]] const uint32_t evicted_id = _active->_channel_buffer[idx].id;
            // remove [idx], shift the tail down to keep insertion order (oldest at [0])
            const uint16_t tail = static_cast<uint16_t>(_active->_channel_buffer_n - idx - 1);
            if (tail) std::memmove(&_active->_channel_buffer[idx], &_active->_channel_buffer[idx + 1], tail * sizeof(ChannelEntry));
            --_active->_channel_buffer_n;
            MR_TELEMETRY(
                EventField f[] = { { .key = "id",   .type = EventField::T::i64, .i = static_cast<int64_t>(evicted_id) },
                                   { .key = "mode", .type = EventField::T::str, .s = safe ? "safe" : "fallback" } };
                _hal.emit("channel_msg_evicted", f, 2); );
        }
    }
    _active->_channel_buffer[_active->_channel_buffer_n++] = e;
}

// ---- promiscuous-overhear pull cancel (dv:11006). We got `id` (overheard M-broadcast) OR saw a peer pull
//      it -> drop our matching pending pull(s) so we don't double-pull. The pending ring is populated by
//      process_channel_digest (Phase 2). -------------------------
void Node::cancel_channel_pull(uint32_t id, [[maybe_unused]] uint8_t overheard_from, bool peer_q) {
    for (uint8_t i = 0; i < protocol::cap_channel_pull_pending; ++i) {
        ChannelPullPending& p = _active->_channel_pull_pending[i];
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
void Node::ingest_channel_m(const m_out& m, uint8_t from) {
    if (_cfg.n_layers == 2) return;                            // Principle 11: a dual-layer gateway never ingests channel gossip
    if (m.leaf_id != _cfg.leaf_id) return;                     // defensive leaf gate (dispatch already gated; tests call directly)
    const uint32_t id     = m.channel_msg_id;
    const uint8_t  origin = static_cast<uint8_t>((id >> 24) & 0xff);    // the minter (dv:2912)
    if (!channel_origin_admit(origin, id)) {                   // over per-origin budget -> drop (not buffered/forwarded)
        const int fs = flood_state_find(id);                  // C1 (§4.3 step 1): free any flood-state so §4.4 does
        if (fs >= 0) flood_state_free(static_cast<uint8_t>(fs)); // NOT fast-self-pull a deliberately-throttled message
        return;
    }
    if (_cfg.is_gateway && _cfg.gateway_only) return;          // §7 CONSUMER: a gateway+owner stores+delivers; a pure bridge stays out
    // The lean M frame carries no addressing — a receipt is a "pull_target" iff we have a pull pending for this
    // id (we asked for it), else "overheard" (it reached us via the flood / a peer's broadcast). The flood/pull
    // mechanics below are unchanged; only the source label is now derived from our own state, not the wire.
    bool was_pulled = false;
    for (uint8_t i = 0; i < protocol::cap_channel_pull_pending; ++i)
        if (_active->_channel_pull_pending[i].active && _active->_channel_pull_pending[i].id == id) { was_pulled = true; break; }
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
        // Record-on-delivery + app push — but NOT for our OWN posts. A node can re-encounter a channel
        // message it minted (its buffer entry was evicted, then the message came back via a peer's re-flood
        // or a digest pull) -> origin == _node_id. The app already shows that post as "sent"; recording +
        // pushing it would echo it back as "received" (the app can't dedup — its sent copy has no
        // channel_msg_id). So skip the inbox/app side for self-originated messages; the gossip/flood
        // mechanics below still run (forwarding is unaffected).
        if (origin != _node_id) {
            // Record-on-delivery FIRST (once per msg): returns the inbox seq (0 if disabled). Store the FULL
            // 32-bit channel_msg_id (the exact identity the app dedups by). The live channel_recv push carries
            // the SAME channel_msg_id + seq -> the app unifies live+pulled + detects gaps (model B).
            const uint8_t rx_layer = active_layer_id();   // §2/Q13: the receiving layer (leaf-local; gateways skip channels)
            const uint32_t seq = _inbox.record_channel(m.channel_id, id, rx_layer, e.payload,
                                                       static_cast<uint8_t>(e.payload_len), _hal.now());
            Push pu{};
            pu.kind = PushKind::channel_recv; pu.origin = origin; pu.channel_id = m.channel_id;
            pu.layer_id = rx_layer;            // §2/Q13: the receiving layer
            pu.channel_msg_id = id;            // the FULL 32-bit channel id — the app's dedup identity (matches the inbox record)
            pu.seq = seq;                      // the inbox per-store seq (0 = inbox disabled -> write_push omits it)
            pu.body_len = static_cast<uint8_t>(e.payload_len > protocol::max_payload_bytes_hard_cap
                                               ? protocol::max_payload_bytes_hard_cap : e.payload_len);
            for (uint8_t i = 0; i < pu.body_len; ++i) pu.body[i] = e.payload[i];
            enqueue_push(pu);
        }
        MR_TELEMETRY(
            const char* src = was_pulled ? "pull_target" : "overheard";
            EventField f[] = { { .key = "id",         .type = EventField::T::i64, .i = static_cast<int64_t>(id) },
                               { .key = "channel_id", .type = EventField::T::i64, .i = m.channel_id },
                               { .key = "source",     .type = EventField::T::str, .s = src },
                               { .key = "from",       .type = EventField::T::i64, .i = from } };
            _hal.emit("channel_msg_received", f, 4); );
        if (!was_pulled) {                                    // we got it without asking -> the analyzer's flood/cascade-overlap signal (dv:11001)
            MR_TELEMETRY(
                EventField fo[] = { { .key = "id",          .type = EventField::T::i64, .i = static_cast<int64_t>(id) },
                                    { .key = "channel_id",  .type = EventField::T::i64, .i = m.channel_id },
                                    { .key = "from",        .type = EventField::T::i64, .i = from } };
                _hal.emit("channel_msg_overheard", fo, 3); );
        }
        cancel_channel_pull(id, from);                        // we got it -> drop any pending pull for it
        // FLOOD §4.3 step 3: if a flood-state is waiting on this DATA-M (i.e. it arrived via a FLOOD RTS-M),
        // cache the body into it (needed to re-flood) and run the forward decision (§4.5: silent | arm backoff).
        const int slot = flood_state_find(id);
        if (slot >= 0) {
            FloodState& fs = _active->_flood[slot];
            fs.awaiting_data = false; fs.channel_id = m.channel_id; fs.flavor = m.flavor;
            fs.body_len = static_cast<uint8_t>(e.payload_len);
            for (uint8_t k = 0; k < fs.body_len; ++k) fs.body[k] = e.payload[k];
            flood_forward_decision(static_cast<uint8_t>(slot));
        }
    } else {                                                   // ALREADY HAVE IT -> just track the holder
        channel_mark_seen_by(id, from);
        channel_reoffer_confirm(id);                           // Part 2: a relay of OUR message (DATA-M/M-frame from `from`) was overheard -> stop re-offering
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
        // `payload` carries the post text so the analyzer (dm_delivery_breakdown.py) can match a post to its
        // msg_id — emit-parity with the Lua self_originate event (the tool keys Pass 1 on the payload).
        char pbuf[protocol::channel_msg_max_payload_bytes + 1];
        const uint16_t pl = (e.payload_len > protocol::channel_msg_max_payload_bytes)
                            ? protocol::channel_msg_max_payload_bytes : e.payload_len;
        for (uint16_t i = 0; i < pl; ++i) pbuf[i] = static_cast<char>(e.payload[i]);
        pbuf[pl] = '\0';
        EventField f[] = { { .key = "id",         .type = EventField::T::i64, .i = static_cast<int64_t>(id) },
                           { .key = "channel_id", .type = EventField::T::i64, .i = channel_id },
                           { .key = "payload",    .type = EventField::T::str, .s = pbuf },
                           { .key = "source",     .type = EventField::T::str, .s = "self_originate" } };
        _hal.emit("channel_msg_received", f, 4); );
    // FLOOD origination (§4.1): seed the coverage bitmap {me + my hops==1 neighbours} and broadcast the FLOOD RTS-M +
    // DATA-M. A data-incapable node (no data SF) is non-operational (user rule) -> skip the flood, buffer-only; the
    // repair digest still covers it. No default-SF fallback.
    // 2026-06-26: the {neighbours} seed is a DELIBERATE divergence from the Lua's empty seed (frugality — a relay skips
    // the origin's OWN neighbours, which got the post directly from the origin). A 24-seed asymmetric sweep proved the
    // "honest"/empty seed REGRESSES coverage (it drops the neighbour skip -> more rebroadcast contention -> collisions
    // kill deliveries: 247 mean reach 4.04 -> 3.17) with NO orphan benefit, so it was dropped (spec Part 1). The origin
    // RE-OFFER (Part 2) + the repair-pull cover the asymmetric case the Lua handles via heavier flooding. Re-offer
    // confirmation is the dedicated "overheard a relay" flag (channel_reoffer_confirm) — INDEPENDENT of this seed.
    if (max_data_sf() != 0) {
        uint8_t bm[32] = {};
        flood_set_my_coverage(bm);                           // {self + hops==1 neighbours} — the frugal seed (KEPT; the honest seed regressed)
        enqueue_flood_m(e.channel_id, e.flavor, e.id, e.payload, static_cast<uint8_t>(e.payload_len), bm, protocol::flood_hop_max);
        channel_reoffer_register(e.id);                      // Part 2: own this message's propagation until a RELAY of it is overheard
    }
    schedule_triggered_beacon();                              // §4.1.7: make the repair digest prompt, not 15-min
    return c;
}

// =============================================================================
// Phase 2 — digest gossip + the jittered pull. The ONLY rand draw in the channel
// plane is process_channel_digest's pull jitter, at the Lua's exact gate-order
// (after the have/cap/recent gates, before storage) so the streams stay aligned.
// =============================================================================

// build_channel_digest_ext — SELECT (dv:1426): walk the buffer NEWEST-first, pick up to channel_dirty_max_per_bcn
// DIRTY ids, pack the ext-TLV, and return the picked ids in `picked`/`npicked`. SIDE-EFFECT-FREE (B, 2026-06-23):
// the per-advertisement ad_count++/retire is COMMITTED by emit_beacon only when the beacon actually aired — an
// LBT-suppressed / pack-dropped beacon no longer burns an advertisement. DRAW-FREE. Returns the TLV byte count.
size_t Node::build_channel_digest_ext(uint8_t* out, size_t cap, uint32_t* picked, uint8_t& npicked) {
    uint8_t count = 0;
    for (int i = static_cast<int>(_active->_channel_buffer_n) - 1; i >= 0 && count < protocol::channel_dirty_max_per_bcn; --i) {
        const ChannelEntry& e = _active->_channel_buffer[static_cast<uint16_t>(i)];
        if (e.dirty) picked[count++] = e.id;                       // SELECT only — no bcn_ad_count / dirty mutation here
    }
    npicked = count;
    if (count == 0) return 0;
    return pack_channel_digest_tlv(picked, count, std::span<uint8_t>(out, cap));
}

// Holder-aware retirement predicate (A, 2026-06-23): does every LIVE 1-hop neighbour already hold `e`? Same neighbour
// set as channel_buffer_pick_eviction (rt hops==1), but DELIBERATELY NOT shared — eviction's nn==0 path is
// fallback-evict-oldest (*safe=false), the OPPOSITE of retirement's nn==0=retire; merging would flip eviction's
// telemetry mode (fallback->safe), a silent regression. nn==0 -> no live neighbour to serve -> nothing to advertise
// -> retire. Else true iff every live 1-hop neighbour is in e.seen_by (they all hold it -> the repair-pull is moot).
bool Node::channel_entry_fully_seen(const ChannelEntry& e) const {
    uint8_t nbrs[protocol::cap_routes]; uint8_t nn = 0;
    for (uint8_t i = 0; i < _active->_rt_count; ++i)
        if (_active->_rt[i].n > 0 && _active->_rt[i].candidates[0].hops == 1) nbrs[nn++] = _active->_rt[i].dest;
    if (nn == 0) return true;                                      // no live 1-hop neighbour -> nothing to serve -> retire OK
    for (uint8_t j = 0; j < nn; ++j) if (!seen_test(e.seen_by, nbrs[j])) return false;
    return true;                                                   // every live 1-hop neighbour holds it
}

// commit_channel_digest_advertised — COMMIT (B): the per-advertisement side effects for the ids that ACTUALLY AIRED
// (emit_beacon calls this only when tx_flood `sent`). Re-find by id (indices may shift between select + commit; n<=3 so
// the cost is nil). ++bcn_ad_count, then RETIRE on HOLDER COVERAGE (channel_entry_fully_seen) — a blind count no longer
// orphans a held-by-nobody origin; channel_dirty_max_advertisements is now just the horizon SAFETY backstop (the
// asymmetric neighbour we hear but that never pulls). A retired entry still answers pulls; buffer eviction is the bound.
void Node::commit_channel_digest_advertised(const uint32_t* ids, uint8_t n) {
    for (uint8_t k = 0; k < n; ++k) {
        const int idx = channel_buffer_find(ids[k]);
        if (idx < 0) continue;                                     // evicted between select + commit -> nothing to commit
        ChannelEntry& e = _active->_channel_buffer[static_cast<uint16_t>(idx)];
        ++e.bcn_ad_count;
        const bool seen    = channel_entry_fully_seen(e);          // every live 1-hop neighbour holds it (or none to serve)
        const bool horizon = e.bcn_ad_count >= _cfg.channel_dirty_max_advertisements;
        const bool retired = seen || horizon;
        if (retired) {
            e.dirty = false;                                       // retire from advertising (still answers pulls)
            MR_TELEMETRY(
                EventField f[] = { { .key = "id",         .type = EventField::T::i64, .i = static_cast<int64_t>(e.id) },
                                   { .key = "channel_id", .type = EventField::T::i64, .i = e.channel_id },
                                   { .key = "ad_count",   .type = EventField::T::i64, .i = e.bcn_ad_count },
                                   { .key = "reason",     .type = EventField::T::str, .s = seen ? "seen" : "horizon" } };   // which path retired it
                _hal.emit("channel_dirty_cleared", f, 4); );
        }
        // ★ metal trace (debug on): shows whether an orphan (seen=0/N) keeps advertising or retired early. THE key line.
        if (_hal.trace_on()) {
            uint8_t live = 0, seen_cnt = 0;
            for (uint8_t i = 0; i < _active->_rt_count; ++i)
                if (_active->_rt[i].n > 0 && _active->_rt[i].candidates[0].hops == 1) { ++live; if (seen_test(e.seen_by, _active->_rt[i].dest)) ++seen_cnt; }
            char b[80];
            if (retired) snprintf(b, sizeof b, "chan %08lX ad=%u seen=%u/%u -> RETIRE(%s)", (unsigned long)e.id, e.bcn_ad_count, seen_cnt, live, seen ? "seen" : "horizon");
            else         snprintf(b, sizeof b, "chan %08lX ad=%u seen=%u/%u -> ADVERTISED", (unsigned long)e.id, e.bcn_ad_count, seen_cnt, live);
            _hal.log(b);
        }
    }
}

// re-pull dedup ring (Lua channel_pull_recent map). recently = a pull for `id` fired within the window.
bool Node::channel_pull_recently(uint32_t id) const {
    const uint64_t now = _hal.now();
    for (uint8_t i = 0; i < _active->_channel_pull_recent_n; ++i)
        if (_active->_channel_pull_recent[i].id == id)
            return (now - _active->_channel_pull_recent[i].t_ms) < protocol::channel_pull_window_ms;
    return false;
}
void Node::channel_pull_mark(uint32_t id) {
    const uint64_t now = _hal.now();
    for (uint8_t i = 0; i < _active->_channel_pull_recent_n; ++i)
        if (_active->_channel_pull_recent[i].id == id) { _active->_channel_pull_recent[i].t_ms = now; return; }
    if (_active->_channel_pull_recent_n < protocol::cap_channel_pull_recent) {
        _active->_channel_pull_recent[_active->_channel_pull_recent_n++] = { id, now };
    } else {                                                       // ring full -> evict the oldest
        uint8_t o = 0;
        for (uint8_t i = 1; i < _active->_channel_pull_recent_n; ++i) if (_active->_channel_pull_recent[i].t_ms < _active->_channel_pull_recent[o].t_ms) o = i;
        _active->_channel_pull_recent[o] = { id, now };
    }
}

// process_channel_digest (dv:3546): for each advertised id — if we HAVE it, mark the advertiser as a
// holder (eviction safety); else (capped at cap_channel_pulls_per_bcn_cycle/beacon, skipping ids pulled
// within the window) schedule a JITTERED pull. THE DRAW is rand(0, jitter+1) at the Lua's gate-order
// (dv:3568: after the recent gate, before storage). Gateways skip the entire plane (Principle 11).
void Node::process_channel_digest(uint8_t src, const uint32_t* ids, uint8_t count) {
    if (_cfg.n_layers == 2) return;                            // Principle 11: a dual-layer gateway never pulls channel gossip
    if (_cfg.is_gateway && _cfg.gateway_only) return;          // §7 CONSUMER: a gateway+owner pulls ITS OWN holes; a pure bridge stays out
    const uint64_t now = _hal.now();
    uint8_t scheduled = 0;
    for (uint8_t k = 0; k < count; ++k) {
        const uint32_t id = ids[k];
        if (channel_buffer_find(id) >= 0) {                       // already have it -> track the holder
            channel_mark_seen_by(id, src);
            if (_hal.trace_on()) { char b[64]; snprintf(b, sizeof b, "chan digest<-%u %08lX HAVE", src, (unsigned long)id); _hal.log(b); }
            continue;
        }
        if (scheduled >= protocol::cap_channel_pulls_per_bcn_cycle) {           // per-beacon pull cap
            if (_hal.trace_on()) { char b[64]; snprintf(b, sizeof b, "chan digest<-%u %08lX MISSING -> skip(cap)", src, (unsigned long)id); _hal.log(b); }
            continue;
        }
        if (channel_pull_recently(id)) {                          // recent-window gate (BEFORE the draw)
            if (_hal.trace_on()) { char b[64]; snprintf(b, sizeof b, "chan digest<-%u %08lX MISSING -> skip(recent)", src, (unsigned long)id); _hal.log(b); }
            continue;
        }
        // THE DRAW (dv:3568) — rand(0, channel_pull_jitter_ms+1). Made here regardless of slot availability
        // (the Lua always draws + stores into an unbounded map), so the stream stays aligned.
        const uint32_t jitter = static_cast<uint32_t>(_hal.rand_range(0, static_cast<int32_t>(_cfg.channel_pull_jitter_ms) + 1));
        ++scheduled;
        // one pending slot per id: reuse the id's live slot (overwrite/re-arm), else a free slot.
        int slot = -1;
        for (uint8_t s = 0; s < protocol::cap_channel_pull_pending; ++s)
            if (_active->_channel_pull_pending[s].active && _active->_channel_pull_pending[s].id == id) { slot = s; break; }
        if (slot < 0)
            for (uint8_t s = 0; s < protocol::cap_channel_pull_pending; ++s)
                if (!_active->_channel_pull_pending[s].active) { slot = static_cast<int>(s); break; }
        if (slot < 0) {                                           // ring full (Lua unbounded) — drop after the draw
            MR_TELEMETRY(
                EventField f[] = { { .key = "id", .type = EventField::T::i64, .i = static_cast<int64_t>(id) } };
                _hal.emit("channel_pull_drop_full", f, 1); );
            if (_hal.trace_on()) { char b[64]; snprintf(b, sizeof b, "chan digest<-%u %08lX MISSING -> skip(ringfull)", src, (unsigned long)id); _hal.log(b); }
            continue;
        }
        _active->_channel_pull_pending[slot] = { /*active*/true, id, src, /*requested_at*/now, /*fire_at*/now + jitter };
        if (_hal.trace_on()) { char b[72]; snprintf(b, sizeof b, "chan digest<-%u %08lX MISSING -> pull@%lums", src, (unsigned long)id, (unsigned long)jitter); _hal.log(b); }
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
    ChannelPullPending& p = _active->_channel_pull_pending[slot];
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
    if (_hal.trace_on()) { char b[64]; snprintf(b, sizeof b, "chan pull %08lX -> %u", (unsigned long)id, target); _hal.log(b); }
}

// =============================================================================
// Phase 2c — the CHANNEL_PULL responder + M-broadcast tx. DRAW-FREE.
// =============================================================================

// id of an M-payload TxItem/PendingTx inner (first 4 bytes, BE channel_msg_id).
uint32_t Node::m_inner_id(const uint8_t* inner) {                // Node static (node.h) — shared by the TX path so the BE decode isn't hand-rolled per call site
    return (static_cast<uint32_t>(inner[0]) << 24) | (static_cast<uint32_t>(inner[1]) << 16)
         | (static_cast<uint32_t>(inner[2]) << 8)  |  static_cast<uint32_t>(inner[3]);
}
// Is an M-payload for `id` already in flight or queued? (the s12 pull-storm dedup, dv:11850-11867)
bool Node::channel_m_in_flight(uint32_t id) const {
    if (_active->_pending_tx && _active->_pending_tx->m_broadcast
        && _active->_pending_tx->inner_len >= 4 && m_inner_id(_active->_pending_tx->inner) == id) return true;
    for (uint8_t i = 0; i < _active->_tx_queue_n; ++i)
        if (_active->_tx_queue[i].is_channel_m
            && _active->_tx_queue[i].inner_len >= 4 && m_inner_id(_active->_tx_queue[i].inner) == id) return true;
    return false;
}

// Stage an M-payload (id|channel_id|flavor|body) and enqueue it as an M-broadcast to the puller — the RTS
// carries RTS_FLAG_M_BROADCAST so overhearers catch the lean M frame on the data SF (dv:11875-11894). dst =
// the puller so the RTS-M is routed to it (the legacy M_BROADCAST RTS needs a next-hop); the M frame itself
// is address-less (the puller matches by channel_msg_id) so there's no per-target ctr — derive it from the id.
void Node::enqueue_channel_m(uint8_t target, const ChannelEntry& e) {
    if (_active->_tx_queue_n >= kTxQueueCap) return;                       // queue full -> drop (the puller can re-pull)
    TxItem item{};
    item.origin = _node_id; item.dst = target; item.is_channel_m = true;
    item.ctr    = static_cast<uint16_t>(e.id & 0xff); item.ctr_lo = static_cast<uint8_t>(e.id & 0x0F);  // id-derived (M frame has no ctr)
    item.inner[0] = static_cast<uint8_t>(e.id >> 24); item.inner[1] = static_cast<uint8_t>(e.id >> 16);
    item.inner[2] = static_cast<uint8_t>(e.id >> 8);  item.inner[3] = static_cast<uint8_t>(e.id);
    item.inner[4] = e.channel_id; item.inner[5] = e.flavor;
    for (uint16_t k = 0; k < e.payload_len; ++k) item.inner[6 + k] = e.payload[k];
    item.inner_len = static_cast<uint8_t>(6 + e.payload_len);
    item.enqueue_time_ms = _hal.now();
    _active->_tx_queue[_active->_tx_queue_n++] = item;
    if (_hal.trace_on()) { char b[64]; snprintf(b, sizeof b, "chan serve %08lX -> %u", (unsigned long)e.id, target); _hal.log(b); }
    MR_TELEMETRY(
        EventField f[] = { { .key = "id", .type = EventField::T::i64, .i = static_cast<int64_t>(e.id) },
                           { .key = "to", .type = EventField::T::i64, .i = target } };
        _hal.emit("channel_broadcast_tx", f, 2); );
}

// CHANNEL_PULL responder (dv:11821): cancel my own pending pulls for the requested ids (a peer is
// pulling them — overhear dedup), then — if WE are the addressed target — re-broadcast each held id as
// an M-payload (skipping ids already in flight/queue). Gateways (§7 PROVIDER half OFF): a gateway+owner
// now HOLDS channel messages, so the explicit is_gateway gate below serves a pull ONLY for a SELF-originated
// id — a gateway never relays another node's message (that airtime is reserved for the inter-leaf role).
void Node::handle_channel_pull(uint8_t src, uint8_t dest, const uint32_t* ids, uint8_t count) {
    if (_cfg.n_layers == 2) return;                              // Principle 11: a dual-layer gateway never serves a channel pull (holds no buffer)
    for (uint8_t i = 0; i < count; ++i) cancel_channel_pull(ids[i], src, /*peer_q=*/true);   // a peer pulled these -> cancel my pending pulls (dv:11831)
    if (dest != _node_id) return;                                // only the addressed target serves the pull
    bool any = false;
    for (uint8_t i = 0; i < count; ++i) {
        const int e = channel_buffer_find(ids[i]);
        if (e < 0) continue;                                     // we don't hold it
        if (_cfg.is_gateway && _active->_channel_buffer[e].origin != _node_id) continue;  // §7 PROVIDER off: a gateway serves a pull ONLY for its OWN message, never relays another node's
        if (!channel_m_in_flight(ids[i])) { enqueue_channel_m(src, _active->_channel_buffer[e]); any = true; }
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

// =============================================================================
// Channel FLOOD plane (2026-06-08 redesign): fast primary propagation. The digest+pull above is now the
// repair backstop. A node floods a channel message to its hops==1 neighbours suppressed by a coverage
// bitmap; the flood self-terminates when no node has an unmarked neighbour. Single-radio constraint: at
// most one flood-state is `awaiting_data` (one open DATA-M overhear window) — see §4.2.
// =============================================================================

int  Node::flood_state_find(uint32_t id) {
    for (uint8_t i = 0; i < protocol::cap_flood_pending; ++i)
        if (_active->_flood[i].active && _active->_flood[i].id == id) return i;
    return -1;
}
int  Node::flood_state_alloc(uint32_t id) {
    for (uint8_t i = 0; i < protocol::cap_flood_pending; ++i)
        if (!_active->_flood[i].active) { _active->_flood[i] = FloodState{}; _active->_flood[i].active = true; _active->_flood[i].id = id; return i; }
    return -1;   // §6/C3: ALL slots active -> drop the new flood to the repair layer; NEVER evict an active slot
}
void Node::flood_state_free(uint8_t slot) {
    if (slot >= protocol::cap_flood_pending) return;
    _hal.cancel(kFloodRebcastTimerId + slot);
    _active->_flood[slot] = FloodState{};   // active = false
}

// ---- Part 2: channel ORIGIN re-offer (spec 2026-06-25-channel-origin-reoffer.md) -------------------------------
// The origin owns its message's propagation until seen_by proves it got out. channel_reoffer_register arms a slot at
// flood origination; channel_reoffer_fire re-floods the cached body while seen_by stays empty, up to N retries.
void Node::channel_reoffer_register(uint32_t id) {
    for (uint8_t s = 0; s < protocol::cap_channel_reoffer_pending; ++s) {
        ChannelReofferPending& rp = _active->_channel_reoffer_pending[s];
        if (rp.active) continue;
        rp.active = true; rp.id = id; rp.retries_left = protocol::channel_reoffer_max_retries;
        const uint32_t jitter = static_cast<uint32_t>(_hal.rand_range(0, static_cast<int32_t>(protocol::channel_reoffer_jitter_ms) + 1));
        (void)_hal.after(protocol::channel_reoffer_delay_ms + jitter, kChannelReofferTimerId + s);
        return;
    }
    MR_EMIT("channel_reoffer_table_full", EF_I("id", static_cast<int64_t>(id)));   // >cap un-confirmed originations -> repair digest covers this one (rare)
}

void Node::channel_reoffer_fire(uint8_t slot) {
    if (slot >= protocol::cap_channel_reoffer_pending) return;
    ChannelReofferPending& rp = _active->_channel_reoffer_pending[slot];
    if (!rp.active) return;                                                         // confirmed (a relay was overheard) or freed -> done
    const int i = channel_buffer_find(rp.id);
    if (i < 0) { rp.active = false; return; }                                      // entry evicted -> nothing to re-offer
    const ChannelEntry& e = _active->_channel_buffer[i];
    if (rp.retries_left == 0 || max_data_sf() == 0) { rp.active = false; return; } // exhausted (or data-incapable) -> give up; repair digest is the last resort
    // RE-FLOOD the cached body with the SAME frugal seed as origination (flood_set_my_coverage — NOT empty, which the
    // fail-loud zero-bitmap guard in tx_m_broadcast_rts would refuse). Receivers dedup by originator_retry_dedup_ms
    // (no double-inbox) but DO re-broadcast for coverage; LBT is applied by the TX path (enqueue_flood_m -> become_free).
    uint8_t bm[32] = {};
    flood_set_my_coverage(bm);
    enqueue_flood_m(e.channel_id, e.flavor, e.id, e.payload, static_cast<uint8_t>(e.payload_len), bm, protocol::flood_hop_max);
    --rp.retries_left;
    MR_EMIT("channel_reoffer_tx", EF_I("id", static_cast<int64_t>(e.id)), EF_I("retries_left", rp.retries_left));
    const uint32_t jitter = static_cast<uint32_t>(_hal.rand_range(0, static_cast<int32_t>(protocol::channel_reoffer_jitter_ms) + 1));
    (void)_hal.after(protocol::channel_reoffer_delay_ms + jitter, kChannelReofferTimerId + slot);   // re-arm for the next retry
}

// Part 2 CONFIRMATION: the origin OVERHEARD A RELAY of its message (another node transmitting it — a flood RTS-M /
// DATA-M / M-frame) -> a holder formed, it propagated -> cancel the pending re-offer. A DEDICATED signal, NOT seen_by:
// immune to the {neighbours} seed and to digest/pull marks, so the re-offer stops ONLY on real relay activity (and
// keeps trying until then, up to the cap). No-ops on any node with no slot for `id` (every node except the origin).
void Node::channel_reoffer_confirm(uint32_t id) {
    for (uint8_t s = 0; s < protocol::cap_channel_reoffer_pending; ++s) {
        ChannelReofferPending& rp = _active->_channel_reoffer_pending[s];
        if (rp.active && rp.id == id) { rp.active = false; _hal.cancel(kChannelReofferTimerId + s); return; }
    }
}

// set my bit + my hops==1 neighbour bits (idempotent: originate-seed on a zeroed bm, OR-in on rebroadcast).
void Node::flood_set_my_coverage(uint8_t* bm) const {
    seen_set(bm, _node_id);
    for (uint8_t i = 0; i < _active->_rt_count; ++i)
        if (_active->_rt[i].n > 0 && _active->_rt[i].candidates[0].hops == 1) seen_set(bm, _active->_rt[i].dest);
}
bool Node::flood_any_unmarked(const uint8_t* bm) const {
    for (uint8_t i = 0; i < _active->_rt_count; ++i)
        if (_active->_rt[i].n > 0 && _active->_rt[i].candidates[0].hops == 1 && !seen_test(bm, _active->_rt[i].dest)) return true;
    return false;
}

// Build + enqueue a FLOOD m-broadcast: a fire-and-forget DATA-M whose RTS-M carries the 43-B FLOOD tail
// (id + 32-B bitmap). A true broadcast (no target); issue_send bypasses route selection (next=0xFF).
void Node::enqueue_flood_m(uint8_t channel_id, uint8_t flavor, uint32_t id, const uint8_t* body, uint8_t body_len,
                           const uint8_t* bitmap32, uint8_t hop_left) {
    if (_active->_tx_queue_n >= kTxQueueCap) return;                       // queue full -> drop (repair covers it)
    TxItem item{};
    item.origin = _node_id; item.dst = 0xFF;                      // broadcast; the RTS dst slot carries hop_left
    item.ctr = static_cast<uint16_t>(id & 0xff); item.ctr_lo = static_cast<uint8_t>(id & 0x0F);
    item.is_channel_m = true;
    item.flood = true; item.hop_left = hop_left;
    for (uint8_t i = 0; i < 32; ++i) item.flood_bitmap[i] = bitmap32[i];
    item.inner[0] = static_cast<uint8_t>(id >> 24); item.inner[1] = static_cast<uint8_t>(id >> 16);
    item.inner[2] = static_cast<uint8_t>(id >> 8);  item.inner[3] = static_cast<uint8_t>(id);
    item.inner[4] = channel_id; item.inner[5] = flavor;
    for (uint8_t k = 0; k < body_len; ++k) item.inner[6 + k] = body[k];
    item.inner_len = static_cast<uint8_t>(6 + body_len);
    item.enqueue_time_ms = _hal.now();
    _active->_tx_queue[_active->_tx_queue_n++] = item;
    MR_EMIT("flood_tx", EF_I("id", static_cast<int64_t>(id)), EF_I("hop_left", hop_left));
    become_free();                                                // kick the queue -> issue_m_broadcast (FLOOD RTS-M)
}

// §4.2 — RX of a FLOOD RTS-M (control SF). Returns true iff a FRESH flood-state was created (the caller then
// retunes to catch the DATA-M). The channel_id/flavor/body arrive later with the DATA-M (ingest).
bool Node::handle_flood_rts(const rts_out& r, const uint8_t* in_bm, int16_t snr_q4) {
    const uint32_t id = r.flood_channel_msg_id;
    const int existing = flood_state_find(id);
    if (existing >= 0) {                                          // active state -> overheard duplicate: OR coverage
        FloodState& fs = _active->_flood[existing];
        for (uint8_t i = 0; i < 32; ++i) fs.bitmap[i] |= in_bm[i];
        // §4.5 while-pending: a backoff-phase state now fully covered -> cancel the rebroadcast + free.
        if (!fs.awaiting_data && !flood_any_unmarked(fs.bitmap)) flood_state_free(static_cast<uint8_t>(existing));
        // FLOOD-DBG disabled 2026-06-23 (re-enable for bench diag): if (_hal.trace_on()) { char b[40]; snprintf(b, sizeof b, "flood %08lX dup-merge", (unsigned long)id); _hal.log(b); }   // D (DEBUG)
        return false;                                            // no new flood, no retune
    }
    if (channel_buffer_find(id) >= 0) {                          // already in the buffer, no state -> already forwarded, drop
        // FLOOD-DBG disabled 2026-06-23 (re-enable for bench diag): if (_hal.trace_on()) { char b[48]; snprintf(b, sizeof b, "flood %08lX already-buffered", (unsigned long)id); _hal.log(b); }   // D (DEBUG)
        channel_reoffer_confirm(id);                            // Part 2: a relay of OUR message (its FLOOD RTS-M) was overheard -> stop re-offering
        return false;
    }
    const int slot = flood_state_alloc(id);
    if (slot < 0) {                                              // C3 -> repair
        // FLOOD-DBG disabled 2026-06-23 (re-enable for bench diag): if (_hal.trace_on()) { char b[40]; snprintf(b, sizeof b, "flood %08lX state-full", (unsigned long)id); _hal.log(b); }   // D (DEBUG)
        MR_EMIT("flood_state_full", EF_I("id", static_cast<int64_t>(id))); return false;
    }
    FloodState& fs = _active->_flood[slot];
    fs.awaiting_data = true; fs.src = r.src; fs.rx_snr_q4 = snr_q4; fs.hop_left = r.dst;  // §3.1: dst slot = hop_left
    for (uint8_t i = 0; i < 32; ++i) fs.bitmap[i] = in_bm[i];
    // FLOOD-DBG disabled 2026-06-23 (re-enable for bench diag): if (_hal.trace_on()) { char b[56]; snprintf(b, sizeof b, "flood %08lX caught RTS-M from %u, awaiting DATA-M", (unsigned long)id, (unsigned)r.src); _hal.log(b); }   // F (DEBUG): this node will TRY to catch the flood body
    return true;                                                 // fresh -> catch the DATA-M (retune in the caller)
}

// §4.5 — after the DATA-M ingest: do I have an unmarked neighbour? No -> silent. Yes -> arm a SNR-x² backoff.
void Node::flood_forward_decision(uint8_t slot) {
    if (slot >= protocol::cap_flood_pending || !_active->_flood[slot].active) return;
    if (_cfg.is_gateway || _cfg.n_layers == 2) { flood_state_free(slot); return; }     // §7 provider half OFF / Principle 11: a (single- or dual-layer) gateway never rebroadcasts
    FloodState& fs = _active->_flood[slot];
    if (!flood_any_unmarked(fs.bitmap)) {                                     // every neighbour covered -> stay silent
        // FLOOD-DBG disabled 2026-06-23 (re-enable for bench diag): if (_hal.trace_on()) flood_log_coverage("SILENT", fs.id, fs.bitmap); // A (DEBUG): THE one — why this node went quiet + each hops==1 neighbour's covered state
        flood_state_free(slot); return;
    }
    // SNR-x² gives the backoff WINDOW = T_backoff * snr_norm^2 ; snr_norm = clamp((rx_snr-lo)/(hi-lo),0,1). Then
    // pick a RANDOM slot in [0, window] (rand_range is [lo,hi)). A deterministic delay makes every same-SNR node
    // fire at the SAME instant -> they collide and nobody hears anybody to cancel; worse, a uniformly high-SNR
    // mesh saturates every node to the max window so ALL fire together. The random slot de-collides them: the
    // earliest draw rebroadcasts, the rest hear its RTS-M, OR the coverage, and cancel. far-first holds in
    // expectation (E[delay] = window/2, monotonic in SNR). Standard LBT still gates the actual TX on top.
    const int32_t lo = protocol::flood_snr_lo_q4, hi = protocol::flood_snr_hi_q4;
    int32_t num = static_cast<int32_t>(fs.rx_snr_q4) - lo;
    if (num < 0) num = 0; if (num > (hi - lo)) num = (hi - lo);
    const int64_t span = static_cast<int64_t>(hi) - lo;          // statically > 0 (compile-time constants)
    const uint32_t window  = static_cast<uint32_t>(static_cast<int64_t>(protocol::flood_backoff_ms) * num * num / (span * span));
    const uint32_t backoff = static_cast<uint32_t>(_hal.rand_range(0, static_cast<int>(window) + 1));   // random slot in [0, window]
    (void)_hal.after(backoff, kFloodRebcastTimerId + slot);
    // FLOOD-DBG disabled 2026-06-23 (re-enable for bench diag): if (_hal.trace_on()) { char b[72]; snprintf(b, sizeof b, "flood %08lX relay in %lums slot=%u", (unsigned long)fs.id, (unsigned long)backoff, (unsigned)slot); _hal.log(b); }   // C (DEBUG)
    MR_EMIT("flood_rebroadcast_scheduled", EF_I("id", static_cast<int64_t>(fs.id)), EF_I("backoff_ms", backoff), EF_I("slot", slot));
}

// kFloodRebcastTimerId+slot — re-flood {my unmarked neighbours + me}, hop_left-1 (drop on TTL exhaustion).
void Node::flood_rebroadcast_fire(uint8_t slot) {
    if (slot >= protocol::cap_flood_pending || !_active->_flood[slot].active) return;
    const FloodState fs = _active->_flood[slot];                          // copy: we free the slot before re-enqueue
    uint8_t bm[32]; for (uint8_t i = 0; i < 32; ++i) bm[i] = fs.bitmap[i];
    flood_set_my_coverage(bm);                                   // §4.5 on-fire: {my unmarked neighbours + me}
    flood_state_free(slot);
    if (fs.hop_left <= 1) {                                      // TTL drop
        // FLOOD-DBG disabled 2026-06-23 (re-enable for bench diag): if (_hal.trace_on()) { char b[44]; snprintf(b, sizeof b, "flood %08lX hop-exhausted", (unsigned long)fs.id); _hal.log(b); }   // E (DEBUG)
        MR_EMIT("flood_hop_exhausted", EF_I("id", static_cast<int64_t>(fs.id))); return;
    }
    if (max_data_sf() == 0) return;                             // non-operational (no data SF) -> no fallback
    // FLOOD-DBG disabled 2026-06-23 (re-enable for bench diag): if (_hal.trace_on()) { char b[44]; snprintf(b, sizeof b, "flood %08lX RELAY hop=%u", (unsigned long)fs.id, (unsigned)(fs.hop_left - 1)); _hal.log(b); }   // E (DEBUG)
    enqueue_flood_m(fs.channel_id, fs.flavor, fs.id, fs.body, fs.body_len, bm, static_cast<uint8_t>(fs.hop_left - 1));
}

// §4.4 — caught the FLOOD RTS-M but missed the DATA-M (overhear window closed, still awaiting_data): pull
// the body immediately from `src` (a confirmed adjacent holder), instead of waiting for a digest.
void Node::flood_fast_self_pull(uint8_t slot) {
    if (slot >= protocol::cap_flood_pending || !_active->_flood[slot].active) return;
    const uint32_t id = _active->_flood[slot].id; const uint8_t src = _active->_flood[slot].src;
    flood_state_free(slot);
    if (channel_buffer_find(id) >= 0) return;                    // arrived meanwhile -> no pull
    if (channel_pull_recently(id)) return;                       // re-pull dedup window
    q_in in{};
    in.leaf_id = _cfg.leaf_id; in.src = _node_id; in.dest = src;
    in.opcode = q_opcode::channel_pull; in.mobile = _cfg.is_mobile;
    in.channel_ids = std::span<const uint32_t>(&id, 1);
    uint8_t buf[16];
    const size_t n = pack_q(in, std::span<uint8_t>(buf, sizeof(buf)));
    if (n == 0) return;
    // FLOOD-DBG disabled 2026-06-23 (re-enable for bench diag): if (_hal.trace_on()) { char b[60]; snprintf(b, sizeof b, "flood %08lX DATA-M MISSED -> self-pull from %u", (unsigned long)id, (unsigned)src); _hal.log(b); }   // G (DEBUG): live flood body LOST on this link -> pull (the weak-link path)
    MR_EMIT("channel_pull_sent", EF_I("id", static_cast<int64_t>(id)), EF_I("target", src), EF_S("trigger", "flood_fast"));
    tx_initiating(buf, n, static_cast<int16_t>(_cfg.routing_sf), LbtKind::flood, 0);
    channel_pull_mark(id);
}

}  // namespace meshroute
