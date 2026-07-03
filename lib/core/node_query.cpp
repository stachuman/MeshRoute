// MeshRoute — lib/core/node_query.cpp  (Q-frame REQ_SYNC route-bootstrap plane)
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Q REQ_SYNC (cmd-nibble 0x6, opcode 1): a route-starved node still in discovery broadcasts a
// REQ_SYNC during boot; any neighbour answers — after a jittered backoff — with a FULL-table
// "sync" beacon, so the joiner pulls the mesh's routing state instead of waiting out the slow
// periodic-beacon rotation. Mirrors the Lua send_req_sync_q / handle_q "Q" path /
// schedule_sync_response (dv_dual_sf.lua:8032 / 8064 / 11767), with two deliberate device
// improvements over the Lua BASELINE — both draw-equivalent below their caps, which realistic
// neighbourhoods never reach: (1) the responder dedup is a fixed evict-oldest ring (Lua: an
// unbounded, never-pruned table that refuses when full); (2) the pending responses live in a
// bounded slot ring fired by one timer-id each (Lua: an unbounded table of after()-closures).
//
// The ONLY rand draw in this plane is the schedule_sync_response backoff — made at the Lua's
// EXACT gate-order (after enabled / min-routes / one-per-requester, before storage) so the
// Lua/C++ mt19937 streams stay aligned; the differential gates depend on it. The neighbour-learn
// in handle_q fires the triggered beacon exactly like the MAC handlers (the Lua's learn_rx_source
// schedules it internally in learn_direct_from_frame), so that jitter draw aligns too.
// Part of Node (declared in node.h).
#include "node.h"
#include "frame_codec.h"
#include "wire.h"          // wire::cmd_byte / Cmd::CFG / flags_of — the C config frame header
#include "leaf_config.h"   // CConfig + pack/parse_c_config + leaf_config_hash + duty_to_bp/bp_to_duty

namespace MESHROUTE_NS {

// ---- responder dedup ring (Lua q_responded_to; key opcode|src|dest, ttl q_respond_ttl_ms) ------
// REQ_SYNC carries no key_hash32 (that was the removed HASH_QUERY field), so the Lua key's
// key_hash32 term is always 0 here — we key on (opcode, src, dest) only.
bool Node::q_responded_recently(uint8_t opcode, uint8_t src, uint8_t dest) {
    const uint64_t now = _hal.now();
    for (uint8_t i = 0; i < _active->_q_responded_n; ++i) {
        const QResponded& e = _active->_q_responded[i];
        if (e.opcode == opcode && e.src == src && e.dest == dest)
            return (now - e.t_ms) < protocol::q_respond_ttl_ms;
    }
    return false;
}
void Node::mark_q_responded(uint8_t opcode, uint8_t src, uint8_t dest) {
    const uint64_t now = _hal.now();
    for (uint8_t i = 0; i < _active->_q_responded_n; ++i) {
        QResponded& e = _active->_q_responded[i];
        if (e.opcode == opcode && e.src == src && e.dest == dest) { e.t_ms = now; return; }   // refresh
    }
    if (_active->_q_responded_n < protocol::cap_q_responded_to) {
        _active->_q_responded[_active->_q_responded_n++] = { opcode, src, dest, now };
    } else {                                              // ring full -> evict the oldest (Lua refuses; equal below cap)
        uint8_t o = 0;
        for (uint8_t i = 1; i < _active->_q_responded_n; ++i) if (_active->_q_responded[i].t_ms < _active->_q_responded[o].t_ms) o = i;
        _active->_q_responded[o] = { opcode, src, dest, now };
    }
}

// ---- originator (Lua send_req_sync_q dv:8032; NO rand draw) -------------------------------------
void Node::send_req_sync_q(const char* reason, bool force) {
    (void)reason;                                              // sim-debug log string only (the Lua logs it)
    // §P0 (mirrors emit_beacon's id-0 guard): an UNPROVISIONED node (id 0) must NEVER REQ_SYNC — its src would be
    // the reserved sentinel 0, so receivers learn a route to "0" (which then propagates) + schedule a sync-response
    // addressed to 0. A node REQ_SYNCs only once it has claimed a short id (boot discovery LISTENs + DADs first).
    // The force path (gw-relay no-route reactive pull) is gateway-only -> always id != 0, so it is unaffected.
    if (_node_id == 0) return;
    if (!force && !_cfg.req_sync_on_boot) return;
    const uint64_t now = _hal.now();
    if (_last_req_sync_tx_ms != 0 && (now - _last_req_sync_tx_ms) < protocol::req_sync_retry_ms) return;
    if (!force && _active->_rt_count >= _cfg.req_sync_min_routes) return;  // route-rich -> no need (force: missing THIS route, ask anyway)
    _last_req_sync_tx_ms = now;
    q_in in{};
    in.leaf_id = _cfg.leaf_id; in.src = _node_id; in.dest = 0xFF;   // broadcast
    in.opcode = q_opcode::req_sync; in.mobile = _cfg.is_mobile;
    uint8_t buf[8];
    const size_t n = pack_q(in, std::span<uint8_t>(buf, sizeof(buf)));
    if (n == 0) return;
    MR_TELEMETRY(
        EventField f[] = { { .key = "opcode",           .type = EventField::T::i64, .i = static_cast<uint8_t>(q_opcode::req_sync) },
                           { .key = "rt_total",         .type = EventField::T::i64, .i = _active->_rt_count },
                           { .key = "requester_mobile", .type = EventField::T::i64, .i = _cfg.is_mobile ? 1 : 0 } };
        _hal.emit("q_tx", f, 3); );
    tx_initiating(buf, n, static_cast<int16_t>(_cfg.routing_sf), LbtKind::flood, 0);
}

// ---- boot loop (Lua req_sync_loop dv:9167; first fire armed at +req_sync_listen_ms in on_init) --
void Node::req_sync_loop_fire() {
    if (!in_discovery()) return;
    send_req_sync_q("discovery");
    if (in_discovery() && _active->_rt_count < _cfg.req_sync_min_routes)
        (void)_hal.after(protocol::req_sync_retry_ms, kReqSyncTimerId);
}

// ---- Q RX dispatch (Lua handle_q "Q" path dv:11767) --------------------------------------------
void Node::handle_q(const uint8_t* bytes, size_t len, const RxMeta& meta) {
    auto pq = parse_q(std::span<const uint8_t>(bytes, len));
    if (!pq) return;
    const q_out& q = *pq;
    if (q.leaf_id != _cfg.leaf_id) return;                       // cross-network filter — drop foreign Q first
    // Learn the Q sender as a 1-hop neighbour (Lua learn_rx_source -> learn_direct_from_frame, which
    // fires the triggered beacon internally on a real learn; self / invalid id are no-ops inside).
    if (learn_direct_neighbor(q.src, protocol::db_to_q4(meta.snr_db), false)) schedule_triggered_beacon();
    if (q.src == _node_id) return;                               // loop guard — never answer ourselves
    if (q_responded_recently(q.opcode, q.src, q.dest)) return;   // recently answered this query -> skip
    mark_q_responded(q.opcode, q.src, q.dest);
    MR_TELEMETRY(
        EventField f[] = { { .key = "from",             .type = EventField::T::i64, .i = q.src },
                           { .key = "dest",             .type = EventField::T::i64, .i = q.dest },
                           { .key = "opcode",           .type = EventField::T::i64, .i = q.opcode },
                           { .key = "requester_mobile", .type = EventField::T::i64, .i = q.mobile ? 1 : 0 } };
        _hal.emit("q_rx", f, 4); );
    if (q.opcode == static_cast<uint8_t>(q_opcode::req_sync)) {
        schedule_sync_response(q.src, q.mobile);
        return;
    }
    if (q.opcode == static_cast<uint8_t>(q_opcode::channel_pull)) {
        uint32_t ids[16]; uint8_t nids = 0;                       // pulls carry few ids (usually 1); cap the parse
        for (uint8_t i = 0; i < q.channel_id_count && nids < 16; ++i) {
            const auto cid = parse_q_channel_id(std::span<const uint8_t>(bytes, len), q, i);
            if (cid) ids[nids++] = *cid;
        }
        handle_channel_pull(q.src, q.dest, ids, nids);
        return;
    }
    if (q.opcode == static_cast<uint8_t>(q_opcode::config_pull)) {
        // R6.2 §4.2 durability: ANY member of the requested lineage at >= the requested epoch answers (config lives in
        // every puller, survives the originator leaving). We answer only if WE are a synced member of that lineage.
        if (_cfg.lineage_id != 0 && _cfg.lineage_id == q.pull_lineage &&
            _cfg.config_epoch > 0 && _cfg.config_epoch >= q.pull_epoch)
            send_c_config(q.src);
        return;
    }
    // Any other opcode is unknown -> silent.
}

// ---- R6.2 CONFIG_PULL / C config-answer frame --------------------------------------------------
// 1-hop pull of a leaf's config from a heard member (the joiner/stale node asks directly; needs no F).
void Node::send_config_pull(uint8_t to, uint16_t lineage, uint16_t epoch) {
    if (to == 0 || to == 0xFF) return;
    const uint64_t now = _hal.now();
    if (_last_config_pull_tx_ms != 0 && (now - _last_config_pull_tx_ms) < protocol::config_pull_retry_ms) return;
    _last_config_pull_tx_ms = now;
    q_in in{};
    in.leaf_id = _cfg.leaf_id; in.src = _node_id; in.dest = to;
    in.opcode = q_opcode::config_pull; in.mobile = _cfg.is_mobile;
    in.pull_lineage = lineage; in.pull_epoch = epoch;
    uint8_t buf[8];
    const size_t n = pack_q(in, std::span<uint8_t>(buf, sizeof(buf)));
    if (n == 0) return;
    MR_EMIT("config_pull_tx", EF_I("to", to), EF_I("lineage", lineage), EF_I("epoch", epoch));
    tx_initiating(buf, n, static_cast<int16_t>(_cfg.routing_sf), LbtKind::flood, 0);
}

// Answer a CONFIG_PULL with a C control frame on routing_sf (cmd 0xB), 1-hop direct to the puller. UNLIKE the old
// routed CONFIG_ANSWER (a DATA needing the RTS/CTS handshake + a data SF), a control-plane frame reaches a joiner that
// has NO data sf_list yet — the whole bootstrap fix. Best-effort: a lost C is re-answered when the puller re-pulls.
void Node::send_c_config(uint8_t to) {
    if (to == 0 || to == 0xFF) return;
    CConfig cc{};
    cc.allowed_sf_bitmap  = _cfg.allowed_sf_bitmap;
    cc.duty_bp            = duty_to_bp(_cfg.duty_cycle);
    cc.active_fraction_bp = frac_to_bp(_cfg.channel_active_fraction);       // anti-spam v2: promote the 3 knobs onto the wire
    cc.ch_interval_ms     = ms_to_u16(_cfg.channel_min_interval_ms);
    cc.dm_interval_ms     = ms_to_u16(_cfg.dm_min_interval_ms);
    cc.config_epoch       = _cfg.config_epoch;
    cc.leaf_name_len      = _cfg.leaf_name_len;
    for (uint8_t i = 0; i < _cfg.leaf_name_len && i < protocol::leaf_name_max; ++i) cc.leaf_name[i] = _cfg.leaf_name[i];
    uint8_t frame[3 + 12 + protocol::leaf_name_max];                        // [cmd|leaf][src][dst] + body (sf·duty·frac·chI·dmI·epoch·name)
    frame[0] = wire::cmd_byte(wire::Cmd::CFG, static_cast<uint8_t>(_cfg.leaf_id & 0x0F));
    frame[1] = _node_id;
    frame[2] = to;
    const size_t bn = pack_c_config(cc, frame + 3, sizeof(frame) - 3);
    if (bn == 0) return;
    MR_EMIT("c_config_tx", EF_I("to", to), EF_I("epoch", cc.config_epoch));
    tx_initiating(frame, 3 + bn, static_cast<int16_t>(_cfg.routing_sf), LbtKind::flood, 0);
}

// C config frame RX (cmd 0xB): header [cmd|leaf][src][dst]. Adopt only if it's addressed to us on our leaf nibble.
// An empty-sf_list joiner reaches HERE (control plane) where the old routed CONFIG_ANSWER could never arrive.
void Node::handle_c(const uint8_t* bytes, size_t len, const RxMeta& meta) {
    (void)meta;
    if (len < 3) return;
    const uint8_t leaf = wire::flags_of(bytes[0]);
    const uint8_t dst  = bytes[2];
    if (leaf != _cfg.leaf_id) return;                                      // not our leaf nibble
    if (_node_id == 0 || dst != _node_id) return;                          // must be addressed to us (we hold an id)
    adopt_c_config(bytes + 3, len - 3);
}

// Adopt a pulled config from a C-frame body (cfg + recompute hash on next beacon + a persist Push for the device).
// lineage_id is NOT on the wire — it's our target, set when we heard the managed beacon that triggered the pull (so we
// must already have one). Guard: never older than ours; the SAME epoch IS adopted (the §4.1 LWW loser pulls the winner).
void Node::adopt_c_config(const uint8_t* body, size_t len) {
    if (_cfg.lineage_id == 0) return;                                      // no target lineage -> nothing to sync to
    CConfig cc{};
    if (!parse_c_config(body, len, cc)) return;
    if (cc.config_epoch < _cfg.config_epoch) return;                       // not newer -> ignore
    // No-change guard is HASH-based (not just bitmap) so a name/duty-only LWW write at the SAME epoch still adopts.
    const uint16_t incoming_hash = leaf_config_hash(cc.allowed_sf_bitmap, cc.duty_bp, cc.active_fraction_bp,
                                                    cc.ch_interval_ms, cc.dm_interval_ms, cc.leaf_name, cc.leaf_name_len);
    if (_cfg.config_epoch == cc.config_epoch && incoming_hash == cfg_config_hash()) return;
    if (cc.config_epoch > _max_seen_epoch) _max_seen_epoch = cc.config_epoch;   // R6.3: adopting bumps our max-seen
    _cfg.config_epoch = cc.config_epoch;
    _cfg.allowed_sf_bitmap = cc.allowed_sf_bitmap;
    _cfg.duty_cycle = bp_to_duty(cc.duty_bp);
    _cfg.channel_active_fraction = bp_to_frac(cc.active_fraction_bp);       // anti-spam v2: adopt the 3 promoted knobs live
    _cfg.channel_min_interval_ms = cc.ch_interval_ms;
    _cfg.dm_min_interval_ms      = cc.dm_interval_ms;
    recompute_duty_budget();                                                // R6.3 §2(b): adopted duty applies live (no reboot)
    _cfg.leaf_name_len = cc.leaf_name_len;
    for (uint8_t i = 0; i < cc.leaf_name_len && i < protocol::leaf_name_max; ++i) _cfg.leaf_name[i] = cc.leaf_name[i];
    MR_EMIT("leaf_config_adopted", EF_I("lineage", _cfg.lineage_id), EF_I("epoch", cc.config_epoch),
            EF_I("sf_bitmap", cc.allowed_sf_bitmap));
    schedule_triggered_beacon();                                            // re-advertise the adopted config -> propagate
    Push pu{}; pu.kind = PushKind::config_adopted; enqueue_push(pu);        // device: persist to NV
}

// R6.3 §4.1: an OPERATOR config write. The caller has already mutated the leaf fields (allowed_sf_bitmap / duty_cycle /
// leaf_name) via mutable_config(); this commits the change as a deliberate, propagating bump: epoch = max_seen + 1,
// recompute (config_hash is derived on the next beacon), re-advertise, persist. The operator-command gate IS the
// "deliberate intent" marker — a merely-misconfigured node never calls this, so never propagates. Managed leaves only
// (lineage 0 = unmanaged has no epoch plane). LWW (ties -> higher key_hash32) resolves a concurrent same-epoch write
// in the beacon filter. Returns false (no-op) on an unmanaged leaf.
bool Node::leaf_config_write() {
    if (_cfg.lineage_id == 0) return false;                                 // unmanaged -> no epoch plane to propagate within
    uint16_t base = _max_seen_epoch > _cfg.config_epoch ? _max_seen_epoch : _cfg.config_epoch;
    _cfg.config_epoch = static_cast<uint16_t>(base + 1);
    _max_seen_epoch   = _cfg.config_epoch;
    MR_EMIT("leaf_config_write", EF_I("epoch", _cfg.config_epoch), EF_I("hash", static_cast<int64_t>(cfg_config_hash())));
    schedule_triggered_beacon();                                            // re-advertise immediately -> neighbours go stale -> pull
    Push pu{}; pu.kind = PushKind::config_adopted; enqueue_push(pu);        // device: persist the new {epoch, config}
    return true;
}

// ---- jittered full-table response (Lua schedule_sync_response dv:8064; the ONLY draw) ----------
void Node::schedule_sync_response(uint8_t requester, bool requester_mobile) {
    if (!_cfg.sync_response_enabled) return;
    const uint8_t route_n = _active->_rt_count;
    if (route_n < _cfg.sync_response_min_routes) {              // route-starved responder skip (inert at default min=0)
        MR_TELEMETRY(
            EventField f[] = { { .key = "joiner",   .type = EventField::T::i64, .i = requester },
                               { .key = "reason",   .type = EventField::T::str, .s = "rt_small" },
                               { .key = "rt_total", .type = EventField::T::i64, .i = route_n } };
            _hal.emit("sync_response_skip", f, 3); );
        return;
    }
    // One pending response per requester (Lua sync_response_pending[key]) — BEFORE the draw.
    for (uint8_t i = 0; i < protocol::cap_sync_response_pending; ++i)
        if (_active->_sync_pending[i].active && _active->_sync_pending[i].requester == requester) return;
    // THE DRAW — rand_range(min, max+1) == Lua self:rand(lo, hi+1) (dv:8083). Placed here, at the
    // Lua's exact gate-order, so the streams stay aligned even when the ring is full below (the Lua
    // has no ring — it always draws + stores).
    uint32_t delay = static_cast<uint32_t>(_hal.rand_range(protocol::sync_response_backoff_min_ms,
                                                           protocol::sync_response_backoff_max_ms + 1));
    if (_cfg.is_mobile)   delay += protocol::sync_response_mobile_penalty_ms;             // dv:8085
    if (requester_mobile) delay += protocol::sync_response_requester_mobile_penalty_ms;   // dv:8088
    int slot = -1;
    for (uint8_t i = 0; i < protocol::cap_sync_response_pending; ++i)
        if (!_active->_sync_pending[i].active) { slot = static_cast<int>(i); break; }
    if (slot < 0) {                                            // ring full (device cap; Lua unbounded) — drop AFTER the draw
        MR_TELEMETRY(
            EventField f[] = { { .key = "joiner", .type = EventField::T::i64, .i = requester } };
            _hal.emit("sync_response_drop_full", f, 1); );
        return;
    }
    const uint64_t now = _hal.now();
    _active->_sync_pending[slot] = { .active = true, .suppressed = false, .requester = requester,
                            .requester_mobile = requester_mobile, .requested_at = now, .fire_at = now + delay };
    MR_TELEMETRY(
        EventField f[] = { { .key = "joiner",            .type = EventField::T::i64, .i = requester },
                           { .key = "delay_ms",         .type = EventField::T::i64, .i = static_cast<int64_t>(delay) },
                           { .key = "rt_total",         .type = EventField::T::i64, .i = route_n },
                           { .key = "requester_mobile", .type = EventField::T::i64, .i = requester_mobile ? 1 : 0 } };
        _hal.emit("sync_response_scheduled", f, 4); );
    (void)_hal.after(delay, kSyncResponseTimerId + static_cast<uint32_t>(slot));
}

// ---- response fire (Lua the schedule_sync_response after()-closure body dv:8108) ----------------
void Node::sync_response_fire(uint8_t slot) {
    if (slot >= protocol::cap_sync_response_pending) return;
    SyncPending& p = _active->_sync_pending[slot];
    if (!p.active) return;                                     // already fired / never armed
    p.active = false;
    if (p.suppressed) {                                        // a useful beacon was overheard in-window -> stand down
        MR_TELEMETRY(
            EventField f[] = { { .key = "joiner", .type = EventField::T::i64, .i = p.requester },
                               { .key = "reason", .type = EventField::T::str, .s = "heard_useful_bcn" } };
            _hal.emit("sync_response_suppressed", f, 2); );
        return;
    }
    MR_TELEMETRY(
        EventField f[] = { { .key = "joiner",   .type = EventField::T::i64, .i = p.requester },
                           { .key = "rt_total", .type = EventField::T::i64, .i = _active->_rt_count } };
        _hal.emit("sync_response_tx", f, 2); );
    emit_beacon("sync");                                       // full-table page (dirty_only=false for kind=="sync")
}

}  // namespace meshroute
