// MeshRoute — lib/core/node_mac.cpp  (R3 MAC data plane — TX / send path)
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// The sender side of the RTS-CTS-DATA-ACK handshake: the send queue drain
// (become_free), flight issue + RTS (re)transmit, the DATA transmit with the
// §7.6 hop-budget derivation, and the timeout-start helpers. RTS/CTS/ACK ride
// routing_sf; only DATA rides the chosen data_sf. The RX-side frame handlers are
// in node_mac_rx.cpp; the R4 duty/anti-spam plane is in node_budget.cpp.
// Behaviour mirrors dv_dual_sf.lua. Part of the Node class (declared in node.h).
// See docs/specs/2026-05-30-r3-data-plane-design.md.
#include "node.h"

#include "frame_codec.h"
#include "airtime.h"

#include <span>

namespace MESHROUTE_NS {

uint16_t Node::next_ctr(uint8_t dst) {
    uint16_t& c = _active->_peer_send_counter[dst];
    c = (c >= 65535) ? 1 : static_cast<uint16_t>(c + 1);   // wraps 65535->1 (NOT a rand site)
    return c;
}

uint8_t Node::select_data_sf(uint8_t rts_sf_index, int16_t rx_snr_q4) const {
    // Adaptive DATA-SF: resolve the requester's sf_index to a candidate SF set, then pick the fastest
    // SF the link SNR supports (Lua sf_index_to_bitmap :3027 + select_data_sf :3043). ANY(3) -> our full
    // allowed_sf_bitmap; pinned 0..2 -> that singleton (M-broadcast / forced SF). allowed_sf_bitmap==0
    // means "unconfigured" -> NO data SF: returns 0, and callers refuse to send / ignore the RTS (no silent
    // fallback — a misconfigured node must fail loud, not run on a hidden default). PURE (no rand).
    uint16_t bitmap = _cfg.allowed_sf_bitmap;
    if (rts_sf_index != 3 /*ANY*/) {                          // pinned: the index-th allowed SF
        uint16_t pin = 0; uint8_t seen = 0;
        for (uint8_t sf = 5; sf <= 12; ++sf)
            if (bitmap & (1u << sf)) { if (seen++ == rts_sf_index) { pin = static_cast<uint16_t>(1u << sf); break; } }
        if (pin) bitmap = pin;                                // out-of-range index -> keep full set (Lua :3035)
    }
    return protocol::select_data_sf_for_snr(rx_snr_q4, bitmap, protocol::sf_margin_q4);  // 0 if sf_list empty
}

uint32_t Node::airtime_routing_ms(uint16_t len) const {
    return airtime_ms(_cfg.routing_sf, _cfg.radio_bw_hz, _cfg.radio_cr, protocol::preamble_sym, len);
}
// 3*airtime(routing, Lua RTS_LEN=8) — a TIMING constant from the Lua, NOT the 7-B C++ wire,
// so the retry rand RANGE matches the Lua and the lua-vs-meshroute streams stay aligned.
uint32_t Node::retry_jitter_ms() const { return 3 * airtime_routing_ms(8); }

// Build + enqueue an app DATA. `tx_event` separates an app send ("tx_enqueue", the dm_delivery
// record-creation key) from an internal protocol DATA like the E2E ack ("e2e_ack_tx") that must NOT
// be counted as an app DM.
uint16_t Node::enqueue_data(uint8_t dst, const uint8_t* body, uint8_t body_len, uint8_t flags, [[maybe_unused]] const char* tx_event, bool app_dm, uint8_t type) {
    const uint16_t ctr = next_ctr(dst);
    TxItem item{};
    item.origin = _node_id; item.dst = dst; item.ctr = ctr; item.ctr_lo = static_cast<uint8_t>(ctr & 0x0F);
    item.flags = flags; item.type = type;
    // Inner = [dst_key_hash32 (4 B LE, iff DST_HASH)][origin][body] — NO payload-flags byte. DST_HASH (L2c
    // verify-on-delivery) is default-on for app DMs when we know the recipient's stable key (id_bind) and the
    // +4 B still fits the inner buffer — when present, set the byte-1 HEADER flag (item.flags) and prefix the
    // hash. Else plain ([origin][body]). NOT for internal DATA (E2E acks): app_dm=false.
    // Decide the optional inner fields (the SAME fit-checks as before), then build the bytes via the shared
    // pack_unicast_inner (Slice 4b — the single source of inner byte ORDER; proves byte-identical for this
    // non-cross-layer path). Inner = [dst_key_hash32 (iff DST_HASH)][origin][source_hash (iff SOURCE_HASH)][body].
    // DST_HASH (L2c verify-on-delivery) is default-on for app DMs when we know the recipient's stable key + it fits;
    // SOURCE_HASH carries the sender's STABLE key_hash32 AFTER origin (the 8-bit origin is reassignable). NOT for
    // internal DATA (E2E acks): app_dm=false. NO CROSS_LAYER here — that path is Slice 4d (origination) / 4c (bridge).
    uint32_t dh = 0;
    if (app_dm && key_hash_of_id(dst, dh)
        && static_cast<size_t>(4 + 1 + body_len) <= protocol::max_payload_bytes_hard_cap) {
        item.flags |= DATA_FLAG_DST_HASH;
    }
    const uint8_t after_origin = static_cast<uint8_t>((item.flags & DATA_FLAG_DST_HASH ? 4 : 0) + 1);
    if (app_dm && static_cast<size_t>(after_origin + 4 + body_len) <= protocol::max_payload_bytes_hard_cap) {
        item.flags |= DATA_FLAG_SOURCE_HASH;
    }
    // LOCATION (opt-in, 2026-06-14 spec §3): set ONLY on an app-DM ORIGINATION (app_dm), when the node opted in
    // (loc_in_dm) AND it HAS a fix (not (0,0)) AND the +6 B still fits. Mirrors the DST_HASH/SOURCE_HASH fit-gate
    // (drop the best-effort piggyback rather than overflow the inner). NEVER set for E2E acks (app_dm=false) or
    // forwards/relays (those don't call enqueue_data — they re-tx the received inner).
    if (app_dm && _cfg.loc_in_dm && (_cfg.lat_e7 != 0 || _cfg.lon_e7 != 0)) {
        const size_t with_loc = static_cast<size_t>(after_origin)
                              + (item.flags & DATA_FLAG_SOURCE_HASH ? 4 : 0) + 6 + body_len;
        if (with_loc <= protocol::max_payload_bytes_hard_cap) item.flags |= DATA_FLAG_LOCATION;
    }
    item.inner_len = static_cast<uint8_t>(
        pack_unicast_inner(std::span<uint8_t>(item.inner, sizeof item.inner), item.flags, dh,
                           /*layer_ids*/ nullptr, /*n_layers*/ 0, /*cur*/ 0, _node_id, _key_hash32, body, body_len,
                           _cfg.lat_e7, _cfg.lon_e7));   // written iff DATA_FLAG_LOCATION was set above (origination-only)
    item.enqueue_time_ms = _hal.now();                   // first-enqueue time (cascade-requeue total-age cap)
    // Inc 3 back-off: a warn'd ACK (a downstream neighbour says we're near its airtime cap) parks new DM
    // originations until the warn window expires, relieving that neighbour. The hard receiver-side airtime
    // drop is the backstop; this is the polite sender-side half. next_attempt_ms gates the dequeue.
    if (_ack_warn_until > _hal.now()) {
        item.next_attempt_ms = _ack_warn_until;
        MR_EMIT("origination_backoff_ack_warn", EF_I("origin", item.origin), EF_I("dst", item.dst),
                EF_I("ctr", item.ctr), EF_I("until_ms", _ack_warn_until));
    }
    // NAV companion (nav_enabled): jitter a fresh own-DM origination by rand(0, retry_jitter) so two nodes
    // originating at the same moment don't fire their RTS in the same airtime window — the one collision NAV
    // can't prevent (no RTS is decoded, so neither sets the other's NAV). App DMs only (E2E acks are responses).
    if (app_dm && _cfg.nav_enabled) {
        const uint64_t base = item.next_attempt_ms > _hal.now() ? item.next_attempt_ms : _hal.now();
        item.next_attempt_ms = base + static_cast<uint32_t>(_hal.rand_range(0, static_cast<int>(retry_jitter_ms()) + 1));
    }
    if (_active->_tx_queue_n < kTxQueueCap) _active->_tx_queue[_active->_tx_queue_n++] = item;
    MR_EMIT(tx_event, EF_I("origin", item.origin), EF_I("dst", item.dst),
            EF_I("ctr", item.ctr), EF_I("depth", _active->_tx_queue_n));
    become_free();
    return ctr;
}

// E2E/PRIORITY ride the wire via `flags`; the E2E ACK behaviour lives in do_post_ack + send_e2e_ack.
uint16_t Node::do_send(uint8_t dst, const uint8_t* body, uint8_t body_len, uint8_t flags) {
    return enqueue_data(dst, body, body_len, flags, "tx_enqueue", /*app_dm=*/true);   // app DM (dm_delivery record key); DST_HASH default-on
}

// ---- Slice 4d: cross-layer DM origination -------------------------------------------------------------------
// Two-pass gateway selection (Lua select_gateway_for_layer dv:5168). A gateway "bridges target_leaf" if a 1-hop
// _gw_schedules record serves it OR a multi-hop _bridged_layers row (type-4 TLV) maps gw->target_leaf. Pass 1 prefers
// a gw with a LIVE ROUTE (best by fewest hops, then score); Pass 2 falls back to a known-but-unrouted gw (the caller
// enqueues toward it; issue_send's no-route path fires a ROUTE_QUERY). The schedule-defer is the NEXT-HOP's job, NOT
// selection's — `_gw_schedules` is consulted here only for the membership test, never for timing.
uint8_t Node::select_gateway_for_leaf(uint8_t target_leaf) {
    prune_aged_bridged_layers(_hal.now());
    auto bridges_target = [&](uint8_t gw) -> bool {
        if (const GatewaySchedule* s = find_gw_schedule(gw))                                  // 1-hop: direct neighbour's schedule
            for (uint8_t r = 0; r < s->n_rec; ++r) if (s->rec[r].leaf_id == target_leaf) return true;
        for (uint8_t i = 0; i < protocol::cap_bridged_layers; ++i)                            // multi-hop: propagated TLV
            if (_bridged_layers[i].valid && _bridged_layers[i].gw_id == gw && _bridged_layers[i].dest_leaf == target_leaf) return true;
        return false;
    };
    // Pass 1 — a gateway WITH a live route (preferred): best (fewest hops, then best score).
    uint8_t best_gw = 0, best_hops = 0; int16_t best_score = 0;
    for (uint8_t i = 0; i < _active->_rt_count; ++i) {
        const RtEntry& e = _active->_rt[i];
        if (e.dest == _node_id || e.n == 0) continue;
        if (!bridges_target(e.dest)) continue;
        const RtCandidate& pc = e.candidates[0];
        if (best_gw == 0 || pc.hops < best_hops || (pc.hops == best_hops && pc.score > best_score)) {
            best_gw = e.dest; best_hops = pc.hops; best_score = pc.score;
        }
    }
    if (best_gw != 0) return best_gw;
    // Pass 2 — known-to-bridge but UNROUTED (fallback). Direct-neighbour schedule first (no rt route yet)...
    for (uint8_t i = 0; i < protocol::cap_gateway_neighbor_schedules; ++i) {
        const GatewaySchedule& s = _gw_schedules[i];
        if (!s.valid) continue;
        for (uint8_t r = 0; r < s.n_rec; ++r) if (s.rec[r].leaf_id == target_leaf) return s.gw_node_id;
    }
    // ...then a propagated row, WITH the on-layer seen-guard (Lua dv:5234): only trust a TLV entry for a gw we've
    // actually heard on OUR leaf (an authoritative id_bind). Rejects a cross-layer TLV LEAK — a gw that lives on a
    // DIFFERENT leaf whose mapping propagated in via a dual-layer gateway; unreachable from here.
    for (uint8_t i = 0; i < protocol::cap_bridged_layers; ++i) {
        const BridgedLayer& bl = _bridged_layers[i];
        if (!bl.valid || bl.dest_leaf != target_leaf) continue;
        uint32_t kh = 0;
        if (key_hash_of_id(bl.gw_id, kh)) return bl.gw_id;   // heard on our leaf -> trustworthy
    }
    return 0;
}

// Build + enqueue a CROSS_LAYER DM along an explicit layer-path (layer_ids[0..n_layers-1], cursor at `cur` = the
// next layer to enter), routed (MAC dst) to gateway gw_node. The inner carries the FULL path (immutable in transit;
// only the cursor advances, so the destination can reverse it for the 4e E2E ack) + dst_hash + SOURCE_HASH (REQUIRED
// for that reversed ack). pack_unicast_inner's size-first overflow is the cross-layer fit-check (returns 0 -> fail loud).
bool Node::enqueue_cross_layer(uint8_t gw_node, uint32_t dst_hash, const uint8_t* layer_ids, uint8_t n_layers, uint8_t cur, const uint8_t* body, uint8_t body_len, uint8_t flags, uint16_t* out_ctr) {
    TxItem item{};
    const uint16_t ctr = next_ctr(gw_node);          // MAC ctr vs the next-hop gateway (= the e2e (source_hash, ctr) identity)
    item.origin = _node_id; item.dst = gw_node; item.ctr = ctr; item.ctr_lo = static_cast<uint8_t>(ctr & 0x0F);
    item.flags = static_cast<uint8_t>(DATA_FLAG_CROSS_LAYER | DATA_FLAG_DST_HASH | DATA_FLAG_SOURCE_HASH
                                      | (flags & DATA_FLAG_E2E_ACK_REQ));   // 4d/e2e: honor the app's E2E-ack request -> Y acks over the reversed path (4e)
    const size_t n = pack_unicast_inner(std::span<uint8_t>(item.inner, sizeof item.inner), item.flags, dst_hash,
                                        layer_ids, n_layers, cur, _node_id, _key_hash32, body, body_len,
                                        /*lat_e7*/ 0, /*lon_e7*/ 0);   // v1 scope: cross-layer DMs carry NO location
                                                                       // (same-layer DM + M only; cross-layer = documented follow-up)
    if (n == 0) return false;                         // overflow (228-B body cap with the layer-path) -> fail loud
    item.inner_len = static_cast<uint8_t>(n);
    item.enqueue_time_ms = _hal.now();
    if (_active->_tx_queue_n >= kTxQueueCap) return false;
    _active->_tx_queue[_active->_tx_queue_n++] = item;
    if (out_ctr) *out_ctr = ctr;                     // the app's correlation token (returned in CmdResult.ctr)
    MR_EMIT("tx_enqueue_xl", EF_I("origin", item.origin), EF_I("dst", gw_node), EF_I("ctr", ctr),
            EF_I("target_layer", layer_ids[cur]), EF_I("depth", _active->_tx_queue_n));   // the next layer to enter (cur < n_layers, caller-guaranteed)
    become_free();                                   // 4a defers the RTS to the gateway's window on our leaf
    return true;
}

// Originate a cross-layer DM: select a bridging gateway (schedule-verified) + enqueue. NO gateway serves the target
// leaf -> err_no_gateway (fail loud, send_failed). A gateway IS known but UNROUTED -> 4d.2 (user 2026-06-13: park +
// reactive ROUTE_QUERY): enqueue anyway; issue_send's no-route path defers the origination (park in _deferred + an
// expanding-ring RREQ for G via emit_route_request) and try_drain_deferred re-flies it when a route to G appears
// (the Lua Pass-2; an EXPLICIT recovery, not a silent fallback — it ages out to send_failed on the deferred TTL).
void Node::send_cross_layer(uint8_t dst_node, uint32_t dst_hash, uint8_t target_layer, const uint8_t* body, uint8_t body_len, uint8_t flags) {
    const uint8_t target_leaf = static_cast<uint8_t>(target_layer & 0x0F);
    const uint8_t gw = select_gateway_for_leaf(target_leaf);
    if (gw == 0) {                                   // no gateway serves the target leaf at all -> fail loud
        MR_EMIT("xl_send_no_gateway", EF_I("target_layer", target_layer), EF_I("dst_hash", static_cast<int64_t>(dst_hash)));
        Push pu{}; pu.kind = PushKind::send_failed; pu.dst = dst_node; pu.ctr = 0; enqueue_push(pu);
        return;
    }
    // gw != 0: enqueue regardless of route. A live route -> issue_send fires (4a defers to G's window). No route ->
    // issue_send -> defer_send parks it + RREQs G (4d.2 park+reactive), re-flown by try_drain_deferred on the route.
    const uint8_t ids[2] = { active_layer_id(), target_layer };   // the 2-element path [our_layer, target_layer], cur=1
    if (!enqueue_cross_layer(gw, dst_hash, ids, /*n_layers*/ 2, /*cur*/ 1, body, body_len, flags)) {
        MR_EMIT("xl_send_too_large", EF_I("target_layer", target_layer), EF_I("gw", gw));
        Push pu{}; pu.kind = PushKind::send_failed; pu.dst = dst_node; pu.ctr = 0; enqueue_push(pu);
    }
}

// Explicit-path origination (console/companion send_layer, §5): the user supplied the DESTINATION layer path
// (hops[0..hop_count-1]); we PREPEND our own active layer as path[0] (cur=1, so the first layer entered is hops[0]),
// then route (MAC dst) to a gateway serving hops[0]'s leaf. NO H-query — the path is explicit. Multi-gateway transit
// then "just works": the 4c.1 bridge advances `cur` at each gateway along the preserved path. Fail loud (send_failed
// Push) if no gateway serves hops[0] or the inner overflows. The handler validated the path (hop_count <= max-1,
// each layer != 0, hops[0] != our layer) before calling, so building path[1+hop_count] stays in bounds.
CmdCode Node::originate_layer_path(uint32_t dst_hash, const uint8_t* hops, uint8_t hop_count, const uint8_t* body, uint8_t body_len, uint8_t flags, uint16_t& out_ctr) {
    out_ctr = 0;
    uint8_t path[protocol::gw_env_max_hops] = {};
    path[0] = active_layer_id();
    for (uint8_t i = 0; i < hop_count; ++i) path[1 + i] = hops[i];
    const uint8_t n_layers = static_cast<uint8_t>(1 + hop_count);
    const uint8_t first_leaf = static_cast<uint8_t>(hops[0] & 0x0F);     // hops[0] = the first layer to enter (cur=1)
    const uint8_t gw = select_gateway_for_leaf(first_leaf);
    if (gw == 0) {                                                       // no gateway serves the first hop's leaf -> fail loud
        MR_EMIT("xl_send_no_gateway", EF_I("target_layer", hops[0]), EF_I("dst_hash", static_cast<int64_t>(dst_hash)));
        return CmdCode::err_no_gateway;                                  // SYNCHRONOUS (the app holds the CmdResult handle); NO orphan push
    }
    if (!enqueue_cross_layer(gw, dst_hash, path, n_layers, /*cur*/ 1, body, body_len, flags, &out_ctr)) {
        MR_EMIT("xl_send_too_large", EF_I("target_layer", hops[0]), EF_I("gw", gw));
        return CmdCode::err_too_large;
    }
    return CmdCode::queued;                                              // out_ctr now holds the correlation token
}

// End-to-end ACK: a tiny DATA back to the DM's origin carrying the acked ctr. Typed by the frame TYPE
// (DATA_TYPE_E2E_ACK -> APP byte), NOT a byte-1 flag. Emits e2e_ack_tx (NOT tx_enqueue) so dm_delivery
// doesn't miscount the ack as an app DM; it routes home on the reverse path F discovery laid toward the origin.
void Node::send_e2e_ack(uint8_t to_origin, uint16_t acked_ctr) {
    const uint8_t body[2] = { static_cast<uint8_t>(acked_ctr & 0xFF), static_cast<uint8_t>(acked_ctr >> 8) };
    (void)enqueue_data(to_origin, body, 2, /*flags=*/0, "e2e_ack_tx", /*app_dm=*/false, DATA_TYPE_E2E_ACK);
}

// Slice 4e: the reversed-path CROSS_LAYER E2E ack. The inbound DM `dm` preserved the full layer-path (§0.10), so Y
// REVERSES it and acks the ORIGINAL sender X over a gateway bridging back. dst = X's stable key (dm.source_hash) —
// FAIL LOUD if absent (NEVER ack pa.origin on the local leaf — that's a different node on the wrong layer). The ack
// is a CROSS_LAYER TYPE=E2E_ACK DATA, bridged by 4c.1 exactly like any forward cross-layer DATA. Best-effort: no
// reverse gateway / route -> DROP loud (X's DM retry recovers it; an ack never floods/parks).
void Node::send_e2e_ack_cross_layer(const data_unicast_inner& dm, uint16_t acked_ctr) {
    if (!dm.has_cross_layer || dm.n_layers == 0) return;                  // not a cross-layer DM (caller already gated, defensive)
    if (!dm.has_source_hash || dm.source_hash == 0) {                     // no stable sender key -> can't address the ack. FAIL LOUD.
        MR_EMIT("xl_ack_no_source", EF_I("acked_ctr", acked_ctr));
        return;
    }
    // Reverse the preserved path: [A,B] -> [B,A]; cur reset to 1 (Y on rev[0], enters rev[1] = X's origin layer).
    uint8_t rev[protocol::gw_env_max_hops] = {};
    for (uint8_t i = 0; i < dm.n_layers; ++i) rev[i] = dm.layer_ids[dm.n_layers - 1 - i];
    const uint8_t target_leaf = static_cast<uint8_t>(rev[1] & 0x0F);      // the next layer to enter (cur=1)
    const uint8_t gw = select_gateway_for_leaf(target_leaf);
    if (gw == 0 || rt_find(gw) == nullptr) {                              // no reverse gateway / route -> DROP loud (never park/flood an ack)
        MR_EMIT("xl_ack_no_gateway", EF_I("target_leaf", target_leaf), EF_I("acked_ctr", acked_ctr), EF_I("to_hash", static_cast<int64_t>(dm.source_hash)));
        return;
    }
    TxItem item{};
    const uint16_t ctr = next_ctr(gw);                                   // a fresh MAC ctr vs the gateway (the ack's own identity)
    item.origin = _node_id; item.dst = gw; item.ctr = ctr; item.ctr_lo = static_cast<uint8_t>(ctr & 0x0F);
    item.flags = static_cast<uint8_t>(DATA_FLAG_CROSS_LAYER | DATA_FLAG_DST_HASH | DATA_FLAG_SOURCE_HASH);
    item.type  = DATA_TYPE_E2E_ACK;
    const uint8_t abody[2] = { static_cast<uint8_t>(acked_ctr & 0xFF), static_cast<uint8_t>(acked_ctr >> 8) };
    const size_t n = pack_unicast_inner(std::span<uint8_t>(item.inner, sizeof item.inner), item.flags, dm.source_hash,
                                        rev, dm.n_layers, /*cur*/ 1, _node_id, _key_hash32, abody, 2,
                                        /*lat_e7*/ 0, /*lon_e7*/ 0);   // E2E ack: NEVER carries location
    if (n == 0) return;                                                  // overflow (never for a 2-B ack) -> fail loud
    item.inner_len = static_cast<uint8_t>(n);
    item.enqueue_time_ms = _hal.now();
    if (_active->_tx_queue_n >= kTxQueueCap) return;
    _active->_tx_queue[_active->_tx_queue_n++] = item;
    MR_EMIT("xl_e2e_ack_tx", EF_I("to_hash", static_cast<int64_t>(dm.source_hash)), EF_I("gw", gw), EF_I("acked_ctr", acked_ctr));
    become_free();                                                       // 4a defers the RTS to the gateway's window
}

void Node::become_free() {
    if (_active->_pending_tx || _active->_pending_rx) return;              // half-duplex serialize
    if (_active->_tx_queue_n == 0) return;
    // Drain the FIRST item whose backoff has elapsed (next_attempt_ms <= now). The
    // Lua scans (not head-only) so a fresh send isn't blocked behind a backing-off
    // cascade-requeue, and the next_attempt_ms gate means a concurrent become_free
    // can't skip a requeue's backoff. Items with next_attempt_ms==0 are always ready,
    // so a queue without requeues behaves as plain FIFO.
    const uint64_t now = _hal.now();
    uint8_t  pick = _active->_tx_queue_n;                          // sentinel = none ready
    uint64_t soonest = UINT64_MAX;
    for (uint8_t i = 0; i < _active->_tx_queue_n; ++i) {
        if (_active->_tx_queue[i].next_attempt_ms <= now) { pick = i; break; }
        if (_active->_tx_queue[i].next_attempt_ms < soonest) soonest = _active->_tx_queue[i].next_attempt_ms;
    }
    if (pick == _active->_tx_queue_n) {                            // none ready -> wake at the soonest backoff
        (void)_hal.after(static_cast<uint32_t>(soonest - now), kQueueWakeupTimerId);
        return;
    }
    // Inc 4 self-cap (enforcing): cap our OWN DM originations per window. Checked here at transmit time —
    // become_free serializes sends, so the count never exceeds the cap (no enqueue-race overshoot). Exempt:
    // forwards (is_forward — we stay a good relay) and channel broadcasts (M_BROADCAST, when R5 lands —
    // governed by the per-origin channel COUNT plane). Defer-in-place (bump next_attempt_ms) until the
    // oldest origination ages out, then re-pick; the receiver-side airtime backstop is the hard floor.
    if (_active->_tx_queue[pick].origin == _node_id && !_active->_tx_queue[pick].is_forward
        && !_active->_tx_queue[pick].is_channel_m) {
        uint64_t oldest = now;
        const uint8_t own = self_originate_count(&oldest);
        if (own >= _cfg.originator_self_cap_per_window) {
            uint64_t until = oldest + protocol::originator_window_ms;
            if (until <= now) until = now + 1;
            _active->_tx_queue[pick].next_attempt_ms = until;          // defer in place
            MR_EMIT("originator_self_defer", EF_I("origin", _active->_tx_queue[pick].origin), EF_I("dst", _active->_tx_queue[pick].dst),
                    EF_I("ctr", _active->_tx_queue[pick].ctr), EF_I("own_count", own),
                    EF_I("cap", _cfg.originator_self_cap_per_window), EF_I("until_ms", until));
            become_free();                                    // re-pick (skips the now-deferred item)
            return;
        }
        self_originate_observe();                             // admitted -> record
    }
    TxItem item = _active->_tx_queue[pick];
    for (uint8_t i = pick + 1; i < _active->_tx_queue_n; ++i) _active->_tx_queue[i - 1] = _active->_tx_queue[i];
    --_active->_tx_queue_n;
    issue_send(item);
}

// ---- M-broadcast (channel gossip) fire-and-forget tx (dv:6997/7044/7389) ----------------------
// chosen_data_sf = max(allowed_sf_bitmap) — largest SF = most robust = most receivers decode it.
uint8_t Node::max_data_sf() const {
    const uint16_t bitmap = _cfg.allowed_sf_bitmap;             // 0 -> no data SF (returns 0; origination refused upstream)
    for (uint8_t sf = 12; sf >= 5; --sf) if (bitmap & (1u << sf)) return sf;
    return 0;
}
// The RTS sf_index that pins that max SF (its rank in the ascending allowed set) so a receiver resolves it
// via select_data_sf — no CTS needed to communicate the choice. (Assumes <=3 allowed SFs, like the Lua.)
uint8_t Node::max_data_sf_index() const {
    const uint16_t bitmap = _cfg.allowed_sf_bitmap;
    if (bitmap == 0) return 3;                                   // ANY (receiver picks) — no pinned singleton
    uint8_t count = 0;
    for (uint8_t sf = 5; sf <= 12; ++sf) if (bitmap & (1u << sf)) ++count;
    return static_cast<uint8_t>(count - 1);                     // ascending -> the highest SF is the last index
}
// Set up the fire-and-forget flight on the just-installed _active->_pending_tx, then fire the RTS. No CTS wait
// (the SF is in the RTS), no ACK wait (failures recover via the next BCN-digest cascade).
void Node::issue_m_broadcast() {
    if (!_active->_pending_tx) return;
    PendingTx& pt = *_active->_pending_tx;
    pt.m_broadcast = true; pt.awaiting_cts = false; pt.awaiting_ack = false;
    pt.chosen_data_sf = max_data_sf();                          // sender picks the SF; advertised in the RTS
    tx_m_broadcast_rts();
}
// Pack + TX the M_BROADCAST RTS (sf_index pinned to the max SF + the channel_msg_id low-16), then arm
// the RTS->DATA gap directly (kCtsToDataGapTimerId -> do_data_tx) — skipping the CTS round-trip.
void Node::tx_m_broadcast_rts() {
    if (!_active->_pending_tx) return;
    PendingTx& pt = *_active->_pending_tx;
    if (pt.inner_len < 6) { _hal.log("M-broadcast RTS inner_len < 6 — refusing tx"); _active->_pending_tx.reset(); become_free(); return; }   // fail loud: M inner is [id4|ch|fl|body]; <6 underflows inner_len-6
    rts_in rin{};
    rin.leaf_id = _cfg.leaf_id; rin.src = _node_id; rin.ctr_lo = pt.ctr_lo;
    rin.sf_index = max_data_sf_index();
    // payload_len announces the BODY length of the lean M frame to follow (pt.inner = [id 4][ch 1][fl 1][body]).
    // The overhearer sizes its retune window as airtime(payload_len + M_FRAME_HDR_LEN) = the full 7+body M frame.
    rin.payload_len = static_cast<uint8_t>(pt.inner_len - 6);
    if (pt.flood) {                                            // FLOOD RTS-M (43 B): id + 32-B coverage bitmap
        // FAIL LOUD (gate refinement #1): an all-zero bitmap is a VALID 43-B frame that makes every receiver
        // see all-neighbours-unmarked -> a rebroadcast storm. A legit flood always has >=1 bit set (the
        // originator's own + neighbours, or OR'd coverage), so a zero bitmap is a bug — refuse to transmit.
        bool any = false; for (uint8_t i = 0; i < 32; ++i) if (pt.flood_bitmap[i]) { any = true; break; }
        if (!any) {
            _hal.log("FLOOD RTS zero bitmap — refusing tx");
            MR_EMIT("flood_zero_bitmap_refused", EF_I("ctr", pt.ctr));
            _active->_pending_tx.reset(); become_free(); return;
        }
        rin.next = 0xFF; rin.dst = pt.hop_left;                // §3.1: next=0xFF (broadcast), dst slot = hop_left
        rin.rts_flags = static_cast<uint8_t>(RTS_FLAG_M_BROADCAST | RTS_FLAG_FLOOD);
        rin.flood_channel_msg_id = m_inner_id(pt.inner);
        rin.flood_bitmap = std::span<const uint8_t>(pt.flood_bitmap, 32);
        uint8_t fbuf[43];
        const size_t fl = pack_rts(rin, std::span<uint8_t>(fbuf, sizeof(fbuf)));
        if (fl == 0) { _hal.log("FLOOD RTS pack failed"); _active->_pending_tx.reset(); become_free(); return; }
        MR_EMIT("rts_tx", EF_I("next", 0xFF), EF_I("ctr", pt.ctr), EF_B("flood", true));
        tx_initiating(fbuf, fl, static_cast<int16_t>(_cfg.routing_sf), LbtKind::rts, pt.flight_gen);
        return;
    }
    rin.next = pt.next; rin.dst = pt.dst; rin.rts_flags = RTS_FLAG_M_BROADCAST;
    rin.m_payload_id_lo16 = static_cast<uint16_t>((pt.inner[2] << 8) | pt.inner[3]);   // low-16 of the BE id
    uint8_t buf[11];                                            // RTS(8) + id_lo16(2)
    const size_t l = pack_rts(rin, std::span<uint8_t>(buf, sizeof(buf)));
    if (l == 0) { _hal.log("M-broadcast RTS pack failed"); return; }
    MR_EMIT("rts_tx", EF_I("dst", pt.dst), EF_I("next", pt.next), EF_I("ctr", pt.ctr));
    tx_initiating(buf, l, static_cast<int16_t>(_cfg.routing_sf), LbtKind::rts, pt.flight_gen);
    // The RTS->DATA gap is armed in start_rts_timeout (the actual-TX hand-off, after any LBT/duty defer = the
    // Lua's on_handed, dv:7032) — NOT here at issue. Arming at issue desyncs the DATA when the RTS is deferred:
    // it fires while the RTS is still on air (self_tx_in_flight -> data_tx_blocked) and BEFORE overhearers
    // receive the full RTS + retune to the data SF, so they're never on the SF when the DATA-M lands.
}

void Node::issue_send(const TxItem& item) {
    // A new flight is going live -> drop any stale BUSY_RX nack-wait left armed for a
    // torn-down prior flight, so its timer can't spuriously re-RTS this one on a 4-bit
    // ctr_lo collision (issue_send is the only choke point that installs a new _active->_pending_tx).
    clear_nack_wait();
    // Build the flight first so pick_next_cascade_hop sees previous_hop (forwarders
    // must not loop back upstream) + the empty alts_tried set.
    PendingTx pt{};
    pt.origin = item.origin; pt.dst = item.dst;
    pt.ctr_lo = item.ctr_lo; pt.ctr = item.ctr; pt.flags = item.flags; pt.type = item.type;
    pt.inner_len = item.inner_len;
    for (uint8_t i = 0; i < item.inner_len; ++i) pt.inner[i] = item.inner[i];
    pt.chosen_data_sf = 0; pt.retries_left = effective_rts_max_retries(item.requeue_count);
    pt.awaiting_cts = true; pt.awaiting_ack = false;
    pt.alts_tried_n = 0;
    pt.previous_hop = item.previous_hop; pt.has_previous_hop = item.is_forward;
    pt.requeue_count = item.requeue_count; pt.enqueue_time_ms = item.enqueue_time_ms;
    pt.fwd_remaining = item.fwd_remaining; pt.fwd_committed = item.fwd_committed;   // hop budget (forwarder)
    pt.flood = item.flood; pt.hop_left = item.hop_left;                             // channel FLOOD: the 43-B RTS-M tail
    for (uint8_t i = 0; i < 32; ++i) pt.flood_bitmap[i] = item.flood_bitmap[i];
    pt.is_gw_relay = item.is_gw_relay;                                              // Slice 4c.2: thread the cross-layer relay marker -> the RTS sets RTS_FLAG_RELAY

    // A channel FLOOD is a TRUE broadcast (next=0xFF, no unicast route) -> skip route selection (which would
    // find no route and drop/defer) and fire the fire-and-forget flight directly.
    if (item.flood) {
        pt.next = 0xFF;
        pt.flight_gen = ++_flight_gen;
        _active->_pending_tx = pt;
        issue_m_broadcast();
        return;
    }

    const uint8_t first = pick_next_cascade_hop(pt);     // first SELECTABLE candidate (skips previous_hop)
    if (first == 0) {
        // No usable route yet. A FORWARDER drops (dv_dual_sf.lua:7041-7048 — it
        // can't hold someone else's transit); an ORIGINATOR defers the message
        // until a beacon installs a route or the defer-TTL expires (dv:7049-7052).
        if (item.is_forward) {
            MR_EMIT("send_no_route", EF_I("dst", item.dst));
        } else {
            defer_send(item);
        }
        return;
    }
    // Slice 4a (the deferred 3e.2b): if the resolved next-hop is a learned, time-multiplexing GATEWAY, HOLD the RTS
    // until its window on OUR leaf opens (dv:7331). gateway_schedule_defer_ms returns 0 for a non-gateway / unknown
    // next-hop (send now), or up to a full window_period_ms when the gateway is away on its other leaf — it isn't
    // listening on our leaf then, so firing now just burns airtime. Re-queue with a next_attempt_ms COMPOSED as
    // max() against any backoff already on the item (ack-warn / self-cap) — never shorten an existing hold. NOT for
    // channel gossip (is_channel_m is fire-and-forget; a gateway skips channels anyway). issue_send's ONLY caller is
    // become_free, which just removed this item, so the queue always has room to re-add it.
    if (!item.is_channel_m) {
        if (const uint32_t defer = gateway_schedule_defer_ms(first); defer > 0) {
            TxItem held = item;
            const uint64_t until = _hal.now() + defer;
            if (held.next_attempt_ms < until) held.next_attempt_ms = until;   // compose: never clobber an earlier-armed backoff
            if (_active->_tx_queue_n < kTxQueueCap) _active->_tx_queue[_active->_tx_queue_n++] = held;
            MR_EMIT("tx_gateway_schedule_defer", EF_I("dst", item.dst), EF_I("next", first),
                    EF_I("ctr", item.ctr), EF_I("defer_ms", defer));
            become_free();                               // re-service: pick another ready item, else arm kQueueWakeupTimerId for the soonest
            return;
        }
    }
    pt.next = first;
    pt.flight_gen = ++_flight_gen;                       // #A redo: a NEW flight identity (cascade_to_alt keeps it; a requeue re-installs here -> new gen)
    _active->_pending_tx = pt;
    if (item.is_channel_m) { issue_m_broadcast(); return; }   // channel gossip (pull-response): fire-and-forget M-broadcast
    tx_rts_retry();                                      // packs+emits the RTS + start_rts_timeout
}

void Node::tx_rts_retry() {
    if (!_active->_pending_tx) return;
    PendingTx& pt = *_active->_pending_tx;
    pt.awaiting_cts = true; pt.awaiting_ack = false; pt.chosen_data_sf = 0;
    rts_in rin{};
    rin.leaf_id = _cfg.leaf_id; rin.src = _node_id; rin.next = pt.next; rin.ctr_lo = pt.ctr_lo;
    // DM RTS; the M-broadcast RTS is tx_m_broadcast_rts. Slice 4c.2: a gateway's cross-layer re-inject sets RTS_FLAG_RELAY
    // so the receiver exempts it from the originator anti-spam (node_mac_rx :40/:199) — G is relaying, not a 1st-hop origination.
    rin.dst = pt.dst; rin.sf_index = 3 /*ANY*/; rin.rts_flags = pt.is_gw_relay ? RTS_FLAG_RELAY : 0;
    rin.payload_len = static_cast<uint8_t>(pt.inner_len + 4 /*MAC_LEN*/); rin.m_payload_id_lo16 = 0;
    uint8_t buf[9];
    const size_t l = pack_rts(rin, std::span<uint8_t>(buf, sizeof(buf)));
    if (l == 0) { _hal.log("RTS pack failed"); return; }
    MR_EMIT("rts_tx", EF_I("dst", pt.dst), EF_I("next", pt.next), EF_I("ctr", pt.ctr));   // emit at the call site (before the LBT defer, dv-faithful)
    // R4.5: the actual TX + start_rts_timeout go through the LBT wrapper (defer if the channel is busy). RX stays
    // on routing_sf. lbt_enabled=false (every gate) -> straight TX + timeout, byte-identical.
    tx_initiating(buf, l, static_cast<int16_t>(_cfg.routing_sf), LbtKind::rts, pt.flight_gen);
}

// R4.5 LBT: hand an INITIATING frame to the radio, but if the channel is busy (and lbt_enabled) defer the real
// TX past busy_until + rand(0,lbt_backoff+1) — the ONE LBT draw, ONLY when busy (dv:3693-3706). lbt_enabled=false
// (every gate) -> straight to lbt_complete -> byte-identical, NO draw.
void Node::tx_initiating(const uint8_t* bytes, size_t len, int16_t sf, LbtKind kind, uint32_t rts_flight_gen) {
    const uint64_t now = _hal.now();
    uint64_t busy_until = 0;
    if (_cfg.lbt_enabled) { const uint64_t b = _hal.channel_busy_until(); if (b > busy_until) busy_until = b; }  // physical carrier sense (LBT)
    if (_cfg.nav_enabled && _nav_until_ms > busy_until) busy_until = _nav_until_ms;                              // virtual carrier sense (NAV)
    if (busy_until > now) {
        const uint32_t wait  = static_cast<uint32_t>(busy_until - now);
        const uint32_t delay = wait + static_cast<uint32_t>(_hal.rand_range(0, static_cast<int>(_lbt_backoff_ms) + 1));   // shared backoff jitter -> NAV-released TX de-syncs
        MR_EMIT("tx_lbt_defer", EF_S("kind", "initiating"), EF_I("defer_ms", delay),
                EF_I("busy_until_ms", busy_until));
        schedule_lbt_defer(bytes, len, sf, kind, rts_flight_gen, delay);
        return;
    }
    lbt_complete(bytes, len, sf, kind, rts_flight_gen);
}

// NAV (virtual carrier sense) duration helpers — pure, native-testable. Conservative: an overheard RTS
// reserves CTS+DATA+ACK (DATA size known from payload_len, SF taken as our max = longest), a CTS reserves
// DATA+ACK (SF exact from chosen_data_sf, size assumed max). Per-leg turnaround gaps included.
uint32_t Node::nav_duration_rts(uint8_t data_sf, uint8_t payload_len) const {
    const uint32_t cts_air  = static_cast<uint32_t>(airtime_routing_ms(3));   // CTS = 3 B on the routing SF
    const uint32_t data_air = static_cast<uint32_t>(airtime_ms(data_sf, _cfg.radio_bw_hz, _cfg.radio_cr,
                                  protocol::preamble_sym, static_cast<uint16_t>(payload_len + 13)));   // +13 = DATA header (handle_rts:57)
    const uint32_t ack_air  = static_cast<uint32_t>(airtime_routing_ms(3));   // ACK = 3 B
    return cts_air + data_air + ack_air + 3u * static_cast<uint32_t>(protocol::cts_to_data_gap_ms);   // 3 turnarounds
}
uint32_t Node::nav_duration_cts(uint8_t data_sf, uint8_t payload_len) const {
    // Exact when the CTS carried payload_len (DATA frame = inner+MAC + 13 header); else max-frame fallback.
    const uint16_t data_bytes = payload_len ? static_cast<uint16_t>(payload_len + 13) : 255;
    const uint32_t data_air = static_cast<uint32_t>(airtime_ms(data_sf, _cfg.radio_bw_hz, _cfg.radio_cr,
                                  protocol::preamble_sym, data_bytes));
    const uint32_t ack_air  = static_cast<uint32_t>(airtime_routing_ms(3));
    return data_air + ack_air + 2u * static_cast<uint32_t>(protocol::cts_to_data_gap_ms);
}
void Node::nav_arm(uint32_t duration_ms) {
    const uint64_t until = _hal.now() + duration_ms;
    if (until > _nav_until_ms) _nav_until_ms = until;   // extend; never shorten
}

// Stash a busy-channel deferred TX in a free ring slot + arm its own timer (kLbtDeferTimerId + slot), so
// concurrent defers each fire independently (Lua per-closure semantics). false = ring full (rare; >4 defers).
bool Node::schedule_lbt_defer(const uint8_t* bytes, size_t len, int16_t sf, LbtKind kind,
                              uint32_t rts_flight_gen, uint32_t delay) {
    for (uint8_t s = 0; s < kLbtSlots; ++s) {
        if (_deferred_lbt[s].pending) continue;
        DeferredLbt& d = _deferred_lbt[s];
        d.pending = true; d.kind = static_cast<uint8_t>(kind); d.sf = sf; d.rts_flight_gen = rts_flight_gen;
        d.len = static_cast<uint8_t>(len < sizeof(d.buf) ? len : sizeof(d.buf));
        for (uint8_t i = 0; i < d.len; ++i) d.buf[i] = bytes[i];
        (void)_hal.after(delay, kLbtDeferTimerId + s);
        return true;
    }
    MR_EMIT("tx_lbt_defer_dropped", EF_I("kind", static_cast<uint8_t>(kind)));                         // ring full -> drop loudly
    return false;
}

// The actual TX (immediate clear-channel path OR the kLbtDeferTimerId re-fire). RTS: the flight-gen staleness check
// (cancel a stale deferred RTS, dv:3708/3712) + the #A duty pre-check (defer over-budget) + start_rts_timeout (the
// after_tx). NACK/flood: just TX.
void Node::lbt_complete(const uint8_t* bytes, size_t len, int16_t sf, LbtKind kind, uint32_t rts_flight_gen) {
    if (kind == LbtKind::rts) {
        if (!_active->_pending_tx || _active->_pending_tx->flight_gen != rts_flight_gen) {  // flight changed while we waited (flight_gen = object-identity, not the 4-bit ctr_lo)
            MR_EMIT("rts_tx_cancelled_stale", EF_S("reason", "pending_tx_changed"));
            return;
        }
        // Cleanup #A (redo): duty pre-check the RTS (the #2 slot<0 residual). Over budget -> defer in the DEDICATED
        // _rts_duty_defer slot (NOT the shared LBT ring — that reuse was net-worse, review wgvbtirmu) + arm the
        // re-check timer + return (NOT handed; start_rts_timeout armed on the eventual send by rts_duty_defer_fire).
        // The ~1h wait is SAFE now: flight_gen staleness is exact. Draw-free; gate-inert (healthy duty never defers).
        uint32_t wait = 0;
        if (duty_over_budget(len, sf, &wait)) {
            MR_EMIT("duty_cycle_blocked", EF_S("label", "RTS"), EF_I("wait_ms", wait), EF_S("source", "lbt_complete"));
            RtsDutyDefer& d = _rts_duty_defer;
            d.pending = true; d.sf = sf; d.flight_gen = rts_flight_gen;
            d.len = static_cast<uint16_t>(len < sizeof(d.buf) ? len : sizeof(d.buf));
            for (uint16_t i = 0; i < d.len; ++i) d.buf[i] = bytes[i];
            (void)_hal.after(wait, kRtsDutyDeferTimerId);
            return;
        }
    }
    const FrameTag tag = (kind == LbtKind::rts)  ? FrameTag::rts
                       : (kind == LbtKind::nack) ? FrameTag::nack : FrameTag::beacon;
    tx_with_retry(bytes, len, sf, tag);                               // R4.5b: stash (NACK) + tag the frame
    if (kind == LbtKind::rts) start_rts_timeout();                     // after_tx: CTS-wait starts when the RTS is on air
}

// Cleanup #A redo: the RTS duty-defer timer fired. Drop if the flight is gone/replaced (flight_gen = the Lua
// __pending_tx_ref object-identity, dv:3712); re-defer if still over budget; else hand the RTS + DRIFT: arm
// start_rts_timeout (a CTS-wait) — the Lua's duty-defer DROPS after_tx and stalls (asymmetric vs its own LBT-defer
// which keeps it); the C++ is more robust. Deliberate documented divergence ([[feedback_port_wire_divergence]] #0).
void Node::rts_duty_defer_fire() {
    RtsDutyDefer& d = _rts_duty_defer;
    if (!d.pending) return;
    if (!_active->_pending_tx || _active->_pending_tx->flight_gen != d.flight_gen) {       // the flight this RTS belonged to is gone
        d.pending = false;
        MR_EMIT("rts_tx_cancelled_stale", EF_S("reason", "pending_tx_changed"));
        return;
    }
    uint32_t wait = 0;
    if (duty_over_budget(d.len, d.sf, &wait)) {                          // still over budget -> re-defer (re-check later)
        (void)_hal.after(wait, kRtsDutyDeferTimerId);
        return;
    }
    d.pending = false;
    TxParams p; p.sf = d.sf; p.label = "RTS"; p.tag = static_cast<uint16_t>(FrameTag::rts);
    _hal.tx(d.buf, d.len, p);
    start_rts_timeout();                                                 // the DRIFT — arm the CTS-wait the Lua drops
}

// R4.5 FLOOD TX (beacon, dv:3765-3814). Duty pre-check (skip if it would breach budget), then LBT: drop the page
// if the channel is busy longer than flood_lbt_max_defer_ms, else defer past busy_until + a backoff, else TX now.
bool Node::tx_flood(const uint8_t* bytes, size_t len, int16_t sf) {
    if (_duty_cycle_budget_ms > 0) {                                   // duty pre-check (dv:7781) — only when enabled
        const uint64_t airtime = airtime_routing_ms(static_cast<int>(len));
        const uint64_t used    = _hal.airtime_used_ms(_cfg.duty_cycle_window_ms);
        if (used + airtime > _duty_cycle_budget_ms) {
            MR_EMIT("duty_cycle_blocked", EF_I("airtime_ms", airtime), EF_S("source", "tx_flood"));
            return false;
        }
    }
    {
        const uint64_t now = _hal.now();
        uint64_t busy_until = 0;
        if (_cfg.lbt_enabled) { const uint64_t b = _hal.channel_busy_until(); if (b > busy_until) busy_until = b; }  // physical carrier sense (LBT)
        if (_cfg.nav_enabled && _nav_until_ms > busy_until) busy_until = _nav_until_ms;                              // virtual carrier sense (NAV)
        const int64_t  wait = static_cast<int64_t>(busy_until) - static_cast<int64_t>(now);
        if (wait > static_cast<int64_t>(_flood_lbt_max_defer_ms)) {    // busy too long -> drop the page (dv:3796)
            MR_EMIT("tx_flood_skipped", EF_I("busy_for_ms", wait));
            return false;
        }
        if (wait > 0) {
            const uint32_t delay = static_cast<uint32_t>(wait) +
                                   static_cast<uint32_t>(_hal.rand_range(0, static_cast<int>(_lbt_backoff_ms) + 1));
            MR_EMIT("tx_lbt_defer", EF_S("kind", "flood"), EF_I("defer_ms", delay),
                    EF_I("busy_until_ms", busy_until));
            schedule_lbt_defer(bytes, len, sf, LbtKind::flood, 0, delay);
            return true;
        }
    }
    TxParams p; p.sf = sf; p.label = "BCN"; p.tag = static_cast<uint16_t>(FrameTag::beacon);  // tag the immediate beacon too (the deferred path tags via lbt_complete) — else a blocked clear-channel beacon reaches on_radio_busy mislabelled tag=0(rts)
    _hal.tx(bytes, len, p);
    return true;
}

// R4.5b: FrameTag -> the human label for telemetry/TxParams.
const char* Node::label_of_frame(FrameTag t) {
    switch (t) { case FrameTag::rts: return "RTS"; case FrameTag::cts: return "CTS";
                 case FrameTag::data: return "DATA"; case FrameTag::ack: return "ACK";
                 case FrameTag::nack: return "NACK"; default: return "BCN"; }
}
int Node::retry_slot_of(FrameTag tag) {
    // PORT DIVERGENCE (deliberate, non-goal): Lua keys its stash by string label and has SEPARATE retry-eligible
    // labels "CTS" vs "CTS-dup" (and "K-dup", "Q") — dv:3073-3081. We collapse the dup variants into the base slot
    // (CTS-dup -> cts slot 0), so a fresh CTS and a dup-CTS share one slot (a 2nd overwrites the 1st's retry budget).
    // Benign: these CTS variants target the same flight + a lost CTS retry is recovered by the peer's rts_timeout
    // re-RTS; and it is gate-inert (on_radio_busy never fires at lbt_enabled=false). A byte-faithful CTS-dup/K-dup/Q
    // split is a documented R-future non-goal ([[r4.5b spec §7]]).
    switch (tag) { case FrameTag::cts: return 0; case FrameTag::data: return 1;
                   case FrameTag::ack: return 2; case FrameTag::nack: return 3;
                   default: return -1; }                            // rts/beacon NOT retry-eligible
}

// check_duty_cycle (Lua dv:3573-3593): true if a `len`-byte TX at `sf` would breach the duty budget; *wait_ms = the
// earliest moment a fresh TX could fit (oldest in-window entry ages out), floored to 1. Disabled (budget 0) -> false.
// Pure airtime/timestamp arithmetic — NO rand. Used by tx_with_retry (#2 duty pre-check, retry-eligible frames only).
bool Node::duty_over_budget(size_t len, int16_t sf, uint32_t* wait_ms) {
    if (_duty_cycle_budget_ms == 0) return false;
    const uint64_t airtime = airtime_ms(sf, _cfg.radio_bw_hz, _cfg.radio_cr, protocol::preamble_sym,
                                        static_cast<uint16_t>(len));
    const uint64_t used = _hal.airtime_used_ms(_cfg.duty_cycle_window_ms);
    if (used + airtime <= _duty_cycle_budget_ms) return false;
    const uint64_t oldest = _hal.oldest_tx_end_ms();
    const uint64_t now    = _hal.now();
    uint32_t w = (oldest > 0 && oldest + _cfg.duty_cycle_window_ms > now)
                 ? static_cast<uint32_t>(oldest + _cfg.duty_cycle_window_ms - now)
                 : static_cast<uint32_t>(_cfg.duty_cycle_window_ms);
    if (w < 1) w = 1;                                                  // guarantee forward progress (dv:3590)
    if (wait_ms) *wait_ms = w;
    return true;
}

// R4.5b central TX (Lua tx_with_retry dv:3599-3639): STASH the retry-eligible frame so on_radio_busy can re-issue
// it on a busy channel, then hand to the radio tagged with the frame type (the sim echoes the tag back). RTS and
// beacon are not retry-eligible (slot -1).
bool Node::tx_with_retry(const uint8_t* bytes, size_t len, int16_t sf, FrameTag tag) {
    const int slot = retry_slot_of(tag);
    if (slot >= 0) {
        TxStashSlot& s = _tx_stash[slot];
        s.valid = true; s.sf = sf; s.retries_left = protocol::tx_defer_max_retries;
        s.reissue_pending = false;   // a fresh attempt: only the duty-defer / on_radio_busy paths below arm a re-issue
        s.ctr_lo = _active->_pending_tx ? _active->_pending_tx->ctr_lo : 0;   // READ only for the DATA slot (retry_stashed re-arm guard); for CTS/ACK/NACK this records the forwarder's OWN outbound flight + is never consulted
        s.len = static_cast<uint16_t>(len < sizeof(s.buf) ? len : sizeof(s.buf));
        for (uint16_t i = 0; i < s.len; ++i) s.buf[i] = bytes[i];
    }
    // SHARED-BUG FIX (#2): duty pre-check (Lua dv:3615-3635). Over budget -> emit duty_cycle_blocked + DEFER via a
    // timer (re-run tx_with_retry from the stash) instead of handing to the radio — else the sim's duty hard-block
    // bounces it via on_radio_busy, consuming a stash retry + an LBT draw per bounce (the Lua re-defers with fresh
    // retries). DRAW-FREE; gate-inert at healthy duty (the check passes -> _hal.tx, byte-identical). Scoped to the
    // retry-eligible frames (slot>=0): only they have a stash to re-run from, and only they hit the stash-retry
    // accounting the bug is about. slot<0 (RTS/beacon) keep their own recovery (rts_timeout / tx_flood's own duty
    // pre-check dv:7781), so they are NOT deferred here.
    uint32_t wait = 0;
    if (slot >= 0 && duty_over_budget(len, sf, &wait)) {
        MR_EMIT("duty_cycle_blocked", EF_S("label", label_of_frame(tag)), EF_I("wait_ms", wait), EF_S("source", "tx_with_retry"));
        _tx_stash[slot].reissue_pending = true;                        // a duty re-issue timer is now armed (gates the gateway layer swap)
        (void)_hal.after(wait, kDutyDeferTimerId + static_cast<uint32_t>(slot));
        return false;                                                  // NOT handed to the radio (caller must not arm post-tx state)
    }
    TxParams p; p.sf = sf; p.label = label_of_frame(tag); p.tag = static_cast<uint16_t>(tag);
    _hal.tx(bytes, len, p);
    return true;                                                      // handed
}

// SHARED-BUG FIX (#2): the duty-defer timer (kDutyDeferTimerId+slot) fired — re-run tx_with_retry from the stashed
// frame (re-checks duty + re-stashes fresh retries, faithful to the Lua `self:after(wait, tx_with_retry)` re-run).
void Node::duty_defer_fire(uint8_t slot) {
    if (slot >= kRetrySlots) return;
    TxStashSlot& s = _tx_stash[slot];
    if (!s.valid) return;
    static const FrameTag kSlotTag[kRetrySlots] = { FrameTag::cts, FrameTag::data, FrameTag::ack, FrameTag::nack };
    const FrameTag tag = kSlotTag[slot];
    // DATA staleness guard (review #6): if the flight moved on during the duty wait (ACK/NACK/implicit-ack replaced
    // _active->_pending_tx), do NOT re-transmit the stale DATA / re-stash with a mismatched ctr_lo. Mirrors retry_stashed +
    // the Lua m_broadcast retry guard (dv:12172). CTS/ACK/NACK are idempotent responses -> no flight guard.
    if (tag == FrameTag::data && (!_active->_pending_tx || _active->_pending_tx->ctr_lo != s.ctr_lo)) return;
    const bool handed = tx_with_retry(s.buf, s.len, s.sf, tag);       // re-runs the duty pre-check (re-defers if still over budget)
    // DATA re-hand: re-arm the ACK wait do_data_tx skipped at defer-time (the DATA now hit the air). Anchored to the
    // actual send time, matching the Lua deferred re-run replaying on_handed (dv:3633 -> 3637 -> 10274-10278).
    if (handed && tag == FrameTag::data && _active->_pending_tx && _active->_pending_tx->ctr_lo == s.ctr_lo) {
        _active->_pending_tx->awaiting_ack = true;
        start_ack_timeout();
    }
}

// R4.5b: re-issue a stashed frame (kRadioBusyRetryTimerId+slot fire). Re-uses the SAME tag so a repeat block
// lands back on the same stash slot (retries_left already decremented in on_radio_busy).
void Node::retry_stashed(uint8_t slot) {
    if (slot >= kRetrySlots) return;
    TxStashSlot& s = _tx_stash[slot];
    if (!s.valid) return;
    static const FrameTag kSlotTag[kRetrySlots] = { FrameTag::cts, FrameTag::data, FrameTag::ack, FrameTag::nack };
    const FrameTag tag = kSlotTag[slot];
    TxParams p; p.sf = s.sf; p.label = label_of_frame(tag); p.tag = static_cast<uint16_t>(tag);
    _hal.tx(s.buf, s.len, p);
    s.reissue_pending = false;   // the armed busy re-issue has now been handed to the radio. UNLIKE the duty path
                                 // (duty_defer_fire -> tx_with_retry, which clears it), THIS path calls _hal.tx
                                 // directly, so clear it HERE — else a gateway's busy-retried ACK leaves the stash
                                 // reissue_pending forever and layer_swap_blocked() deadlocks the leaf swap. If the
                                 // re-TX busies AGAIN, on_radio_busy re-arms + re-sets it.
    // DATA re-issue: re-arm the ACK wait the on_radio_busy block cleared, exactly as the Lua DATA on_handed does
    // (dv:10270-10278) — fires on the initial tx AND on the stash retry. Without this the re-sent DATA flies but the
    // sender stays !awaiting_ack with no ack-timeout, so the returning ACK is dropped + the flight never recovers.
    // Guarded on the pending flight (ctr_lo) so a retry against a since-replaced flight does NOT re-arm.
    if (tag == FrameTag::data && _active->_pending_tx && _active->_pending_tx->ctr_lo == s.ctr_lo) {
        _active->_pending_tx->awaiting_ack = true;
        start_ack_timeout();
    }
}

void Node::do_data_tx() {
    if (!_active->_pending_tx || _active->_pending_tx->awaiting_ack || _active->_pending_tx->chosen_data_sf == 0) return;
    PendingTx& pt = *_active->_pending_tx;
    // Channel M-broadcast: send the lean M frame (cmd 0xA) on the data SF, NOT a DATA frame. No DM header /
    // hop-budget / visited / MAC — pt.inner = [channel_msg_id 4 BE][channel_id][flavor][body]. Fire-and-forget
    // (no ACK): clear the flight after the M-frame airtime (kMBcastClearTimerId).
    if (pt.m_broadcast) {
        if (pt.inner_len < 6) { _hal.log("M-frame inner_len < 6 — refusing tx"); _active->_pending_tx.reset(); become_free(); return; }   // fail loud: would underflow inner_len-6 -> OOB body span
        const uint32_t id = m_inner_id(pt.inner);
        m_in min{};
        min.leaf_id = _cfg.leaf_id; min.channel_id = pt.inner[4]; min.flavor = pt.inner[5];
        min.channel_msg_id = id;
        min.body = std::span<const uint8_t>(pt.inner + 6, static_cast<size_t>(pt.inner_len - 6));
        uint8_t mbuf[protocol::lora_max_frame_bytes];
        const size_t mlen = pack_m(min, std::span<uint8_t>(mbuf, sizeof(mbuf)));
        if (mlen == 0) { _hal.log("M-frame pack failed"); return; }
        const bool handed = tx_with_retry(mbuf, mlen, static_cast<int16_t>(pt.chosen_data_sf), FrameTag::data);
        MR_EMIT("data_tx", EF_I("dst", pt.dst), EF_I("next", pt.next), EF_I("ctr", pt.ctr),
                EF_I("sf", pt.chosen_data_sf), EF_B("m_broadcast", true));
        if (handed) {
            const uint32_t data_air = airtime_ms(pt.chosen_data_sf, _cfg.radio_bw_hz, _cfg.radio_cr,
                                                 protocol::preamble_sym, static_cast<uint16_t>(mlen));
            (void)_hal.after(data_air + 5, kMBcastClearTimerId);
        }
        return;
    }
    // Hop budget (§7.6). An ORIGINATOR derives the initial budget from its route:
    // remaining = min(31, rt_hops + slack); a FORWARDER carries the already-decremented
    // values (threaded from handle_data). prev_fwd_rt_hops is ALWAYS re-stamped to
    // self's own rt[dst].hops (never inherited). Pure arithmetic — no rand.
    RtEntry* rte = rt_find(pt.dst);
    const bool have_rt = (rte != nullptr && rte->n > 0);
    uint8_t hb_remaining, hb_committed, hb_prev_fwd;
    if (pt.has_previous_hop) {                            // forwarder: inherit, re-stamp prev (fallback 0)
        hb_remaining = pt.fwd_remaining;
        hb_committed = pt.fwd_committed;
        hb_prev_fwd  = have_rt ? rte->candidates[0].hops : 0;
    } else {                                              // originator: budget from rt (fallback rt_hops=1)
        const uint8_t rt_hops = have_rt ? rte->candidates[0].hops : 1;
        const int rem = static_cast<int>(rt_hops) + protocol::hop_budget_slack;
        hb_remaining = static_cast<uint8_t>(rem > protocol::hop_budget_max_initial
                                            ? protocol::hop_budget_max_initial : rem);
        hb_committed = 0;
        hb_prev_fwd  = rt_hops;
    }
    const uint8_t mac[4] = { 0, 0, 0, 0 };
    data_in din{};
    din.addr_len = 0; din.flags = pt.flags; din.type = pt.type; din.next = pt.next; din.dst = pt.dst;
    din.hops_remaining = hb_remaining; din.committed_hops = hb_committed;
    din.prev_fwd_rt_hops = hb_prev_fwd; din.ctr = pt.ctr;
    din.inner = std::span<const uint8_t>(pt.inner, pt.inner_len);
    din.mac   = std::span<const uint8_t>(mac, 4);
    uint8_t buf[protocol::lora_max_frame_bytes];
    const size_t dlen = pack_data(din, std::span<uint8_t>(buf, sizeof(buf)));
    if (dlen == 0) { _hal.log("DATA pack failed"); return; }
    const bool handed = tx_with_retry(buf, dlen, static_cast<int16_t>(pt.chosen_data_sf), FrameTag::data);   // R4.5b stash; #2 may duty-defer
    MR_EMIT("data_tx", EF_I("dst", pt.dst), EF_I("next", pt.next), EF_I("ctr", pt.ctr),
            EF_I("sf", pt.chosen_data_sf), EF_B("m_broadcast", false));   // emitted before tx_with_retry (dv:10251); m_broadcast handled above (early return)
    // Arm the ACK wait ONLY if the DATA actually hit the air — mirrors the Lua DATA on_handed (dv:10270-10279, fires only
    // on real self:tx) + the not-handed clear (dv:10281-10283). #2's duty defer returns handed=false: arming a short
    // ack-timeout on an un-sent DATA would fire before the (long) duty wait, draw a rand + tear the flight down.
    if (handed) { pt.awaiting_ack = true; start_ack_timeout(); }
    else        { pt.awaiting_ack = false; }
}

void Node::start_rts_timeout() {
    // M-broadcast (fire-and-forget): there is no CTS to wait for. start_rts_timeout is called at the RTS
    // hand-off in EVERY path (lbt_complete:327 + rts_duty_defer_fire:351) = the Lua's on_handed (dv:7032),
    // so anchor the RTS->DATA gap to the ACTUAL TX here. The DATA then fires cts_to_data_gap_ms AFTER the
    // RTS clears the air, which is exactly when overhearers have received the RTS + retuned to the data SF.
    if (_active->_pending_tx && _active->_pending_tx->m_broadcast) {
        // The gap must outlast the RTS-M's OWN airtime so the M frame fires after the RTS clears the air (and
        // overhearers have decoded it + retuned). The RTS-M is 43 B for a FLOOD (id + 32-B bitmap) vs 9 B for a
        // legacy M_BROADCAST (7 base + id_lo16) — size the gap to the actual RTS, else a flood's M frame fires
        // mid-RTS and overhearers (whose retune window is anchored to the full 43-B RTS) miss it.
        const uint8_t rts_len = _active->_pending_tx->flood ? 43 : 9;
        const uint32_t gap = airtime_routing_ms(rts_len) + protocol::cts_to_data_gap_ms;
        (void)_hal.after(gap, kCtsToDataGapTimerId);                       // RTS->DATA gap fires do_data_tx (no CTS)
        return;
    }
    const uint32_t base = airtime_routing_ms(8) + airtime_routing_ms(4);   // Lua RTS_LEN=8 + CTS_LEN=4 (timing matches Lua)
    const uint8_t  attempt = static_cast<uint8_t>(protocol::rts_max_retries -
                              (_active->_pending_tx ? _active->_pending_tx->retries_left : 0));
    const uint32_t shift = attempt < 2 ? attempt : 2;                       // x2 backoff, cap x4
    (void)_hal.after((base << shift) + 1, kRtsTimeoutTimerId);
}
void Node::start_ack_timeout() {
    const uint8_t  sf  = _active->_pending_tx ? _active->_pending_tx->chosen_data_sf : max_data_sf();  // pending always set here
    const uint16_t len = static_cast<uint16_t>(18 + (_active->_pending_tx ? _active->_pending_tx->inner_len : 0));
    // base = DATA airtime + the ACK's routing airtime. PLUS the REAL hardware turnaround airtime_ms can't see
    // (rx_window_slop_ms, ZERO on the sim): the ACK round-trip crosses TWO SPI SF-reconfigs — the receiver
    // retunes data->routing to SEND the ACK, and WE restore RX data->routing to HEAR it. Without them the
    // ack-timeout landed ~70 ms BEFORE the ACK on metal -> it cleared awaiting_ack, so the real ACK was then
    // ignored (handle_ack needs awaiting_ack) and a redundant re-RTS fired on a DELIVERED flight. Mirrors
    // start_pending_rx_expiry, which already carries the slop for the symmetric DATA wait. (Bench: SF9 DATA.)
    const uint32_t base = airtime_ms(sf, _cfg.radio_bw_hz, _cfg.radio_cr, protocol::preamble_sym, len)
                        + airtime_routing_ms(3)
                        + _hal.rx_window_slop_ms(sf) + _hal.rx_window_slop_ms(_cfg.routing_sf);
    (void)_hal.after(base + 2, kAckTimeoutTimerId);
}
void Node::start_pending_rx_expiry(uint8_t payload_len) {
    const uint8_t  sf  = _active->_pending_rx ? _active->_pending_rx->chosen_data_sf : max_data_sf();  // pending always set here
    const uint16_t len = static_cast<uint16_t>(14 + payload_len);
    // +2: the original ideal-timing margin — this is ALL the sim uses, so s18 contention is unchanged.
    // On top, _hal.rx_window_slop_ms(sf) is the REAL hardware slop airtime_ms can't see, bench-measured
    // across SF5..SF12 as ~30 ms SPI reconfig/mode-switch (SF-flat) + ~1 symbol RX_DONE demod lag
    // (scales with SF; airtime verified vs the chip via [txair]: SF12 model 2564 == measured 2571). It's
    // a HAL hook so it's ZERO on the idealized sim (a fat shared margin inflated BUSY_RX busy_for +
    // over-held pending_rx on lost DATA -> 96%->69% s18 collapse) and metal-real on the device (else the
    // receiver hops back to routing before the slow DATA's RX_DONE and aborts it).
    const uint32_t t = airtime_routing_ms(4) /*CTS_LEN=4*/ + protocol::cts_to_data_gap_ms +
                       airtime_ms(sf, _cfg.radio_bw_hz, _cfg.radio_cr, protocol::preamble_sym, len)
                       + 2 + _hal.rx_window_slop_ms(sf);
    if (_active->_pending_rx) _active->_pending_rx->expiry_ms = _hal.now() + t;   // for the BUSY_RX NACK busy_for calc
    (void)_hal.after(t, kPendingRxExpiryTimerId);
}

}  // namespace meshroute
