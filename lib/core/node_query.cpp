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
#include "leaf_config.h"   // R6.2: ConfigAnswer + pack/parse_config_answer
#include <cmath>           // lround — duty_ppm

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
            send_config_answer(q.src);
        return;
    }
    // Any other opcode is unknown -> silent.
}

// ---- R6.2 CONFIG_PULL / CONFIG_ANSWER ----------------------------------------------------------
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

// Answer a CONFIG_PULL: routed DATA TYPE 6 carrying OUR leaf config (internal DATA: app_dm=false, never participation-gated).
void Node::send_config_answer(uint8_t to) {
    ConfigAnswer ca{};
    ca.lineage_id = _cfg.lineage_id; ca.config_epoch = _cfg.config_epoch; ca.allowed_sf_bitmap = _cfg.allowed_sf_bitmap;
    ca.duty_ppm = (_cfg.duty_cycle > 0.0) ? static_cast<uint32_t>(std::lround(_cfg.duty_cycle * 1e6)) : 0u;
    ca.leaf_name_len = _cfg.leaf_name_len;
    for (uint8_t i = 0; i < _cfg.leaf_name_len; ++i) ca.leaf_name[i] = _cfg.leaf_name[i];
    uint8_t body[16 + protocol::leaf_name_max]; const size_t n = pack_config_answer(ca, body, sizeof body);
    if (n == 0) return;
    MR_EMIT("config_answer_tx", EF_I("to", to), EF_I("lineage", ca.lineage_id), EF_I("epoch", ca.config_epoch));
    enqueue_data(to, body, static_cast<uint8_t>(n), 0, "config_answer", /*app_dm=*/false, DATA_TYPE_CONFIG_ANSWER, CryptIntent::off);
}

// Adopt a pulled config (cfg + recompute hash on next beacon + a persist Push for the device). Guards: only our target
// lineage, and never older than ours.
void Node::adopt_config_answer(const uint8_t* body, size_t len) {
    ConfigAnswer ca{};
    if (!parse_config_answer(body, len, ca)) return;
    if (_cfg.lineage_id != 0 && ca.lineage_id != _cfg.lineage_id) return;   // not our target lineage
    if (ca.config_epoch < _cfg.config_epoch) return;                        // not newer -> ignore (same epoch IS adopted: the §4.1 LWW loser)
    // No-change guard is HASH-based (not just bitmap) so a name/duty-only LWW write at the SAME epoch still adopts.
    const uint16_t incoming_hash = leaf_config_hash(ca.allowed_sf_bitmap, ca.duty_ppm, ca.leaf_name, ca.leaf_name_len);
    if (_cfg.lineage_id == ca.lineage_id && _cfg.config_epoch == ca.config_epoch && incoming_hash == cfg_config_hash()) return;
    if (ca.config_epoch > _max_seen_epoch) _max_seen_epoch = ca.config_epoch;   // R6.3: adopting bumps our max-seen
    _cfg.lineage_id = ca.lineage_id; _cfg.config_epoch = ca.config_epoch;
    _cfg.allowed_sf_bitmap = ca.allowed_sf_bitmap;
    _cfg.duty_cycle = static_cast<double>(ca.duty_ppm) / 1e6;
    recompute_duty_budget();                                                // R6.3 §2(b): adopted duty applies live (no reboot)
    _cfg.leaf_name_len = ca.leaf_name_len;
    for (uint8_t i = 0; i < ca.leaf_name_len; ++i) _cfg.leaf_name[i] = ca.leaf_name[i];
    MR_EMIT("leaf_config_adopted", EF_I("lineage", ca.lineage_id), EF_I("epoch", ca.config_epoch),
            EF_I("sf_bitmap", ca.allowed_sf_bitmap));
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
