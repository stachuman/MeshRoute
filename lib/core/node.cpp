// MeshRoute — lib/core/node.cpp  (Node spine: construction, lifecycle, dispatch)
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// The Node's glue: construction, on_init lifecycle, the on_timer / on_recv
// dispatchers that route to the subsystem handlers, the typed command seam, and
// the async push ring. The subsystem implementations live in sibling TUs of the
// same Node class (declared in node.h):
//   node_beacon.cpp   — §10 BCN emit/ingest + discovery
//   node_routing.cpp  — DV route table + R2 aging/prune
//   node_mac.cpp       — R3 RTS-CTS-DATA-ACK-NACK data plane
//   node_cascade.cpp   — cascade-to-alt walk + no-route defer + timeout fires
// Behaviour mirrors dv_dual_sf.lua; the wire is C5 cmd-nibble. See
// docs/specs/2026-05-29-r1-beacon-emit-design.md + 2026-05-30-r2-route-hardening-design.md.
#include "node.h"

#include "airtime.h"   // airtime_ms — Slice 3a SF-weighted window derivation
#include "wire.h"

namespace MESHROUTE_NS {

// ---- construction & lifecycle ----------------------------------------------

Node::Node(Hal& hal, uint8_t node_id, uint32_t key_hash32, const char* name)
    : _hal(hal), _node_id(node_id), _key_hash32(key_hash32) {
    (void)name;  // sim-debug only; the node identifies by node_id / key_hash32
    // 0xFF is RESERVED — never a valid node id. It is the "unknown PHY source"
    // sentinel: RxMeta.src_hint=-1 casts to 0xFF, and real LoRa carries no link
    // source. The console `cfg id` already caps at 254; this guards the ctor too.
    if (node_id == 0xFF) _hal.panic("node_id 0xFF is reserved (invalid)");
}

// Reassign identity post-construct: the device boots id=0 then loads it from NV; the join runtime sets
// it too. 0 stays unprovisioned (do_send refused). 0xFF is reserved -> ignored.
void Node::set_identity(uint8_t node_id, uint32_t key_hash32) {
    if (node_id == 0xFF) return;
    _node_id    = node_id;
    _key_hash32 = key_hash32;
    _hal.set_protocol_id(node_id);   // keep the Hal short-id in sync (addressing / join)
    id_bind_set(_node_id, _key_hash32, IdBindSource::self, IdBindConf::authoritative);   // re-seed our own binding (authoritative) under the new identity
}

bool Node::on_init(const NodeConfig& cfg) {
#if defined(MR_GATEWAY_BUILD)
    // F1 (RAM-safety guard): the DEDICATED gateway firmware cuts cap_channel_buffer to 8 at COMPILE time, which is
    // only safe because a dual-layer gateway SKIPS the channel plane at RUNTIME (n_layers==2). A single-layer config
    // on a gateway build (e.g. fresh NV → n_layers=1) would run the FULL plane into the 8-entry buffer = silent lossy
    // gossip. REFUSE it (fail-loud): a gateway build is dedicated to gateways. (The node stays up for re-provisioning.)
    if (cfg.n_layers < 2) return false;
#endif
    // Defend the fixed _layers[MR_N_LAYERS] bound (fail-loud, §3.2): refuse an out-of-range layer count from ANY
    // source (corrupt NV, a direct caller, a mis-cast host config). The normalization/validation below only
    // handles n_layers in {1, 2}, so n_layers > MR_N_LAYERS would otherwise set _n_layers past the array end.
    if (cfg.n_layers > MR_N_LAYERS) return false;
    // Dual-layer validation gate (§3.2) — a GATEWAY (n_layers==2) must have both layers' REQUIRED fields set and
    // non-overlapping explicit windows. Fail LOUD (refuse, the node stays down) — no silent inherit / auto-adjust.
    if (cfg.n_layers == 2) {
#if MR_N_LAYERS < 2
        return false;   // a gateway config on a single-layer build — REFUSE (no _layers[1] exists). Fail loud,
                        // never silently fall back to single-layer. Build [env:gateway] (-DMR_N_LAYERS=2) for a gateway.
#endif
        for (uint8_t i = 0; i < 2; ++i) {
            const LayerConfig& L = cfg.layers[i];
            if (L.layer_id == 0)                       return false;   // REQUIRED: full 8-bit id (1..255)
            if (L.routing_sf < 5 || L.routing_sf > 12) return false;   // REQUIRED: a valid routing SF
            if (L.allowed_sf_bitmap == 0)              return false;   // REQUIRED: a gateway must route data
        }
        const LayerConfig& a = cfg.layers[0]; const LayerConfig& b = cfg.layers[1];
        // §0.8: the two layers MUST differ in their leaf nibble (layer_id & 0x0F) — that nibble is the coarse
        // byte-0 wire filter, so same-nibble co-channel layers would ALIAS (frames cross). Refuse loud.
        if ((a.layer_id & 0x0F) == (b.layer_id & 0x0F)) return false;
        // window_period_ms is the ONE shared layer0->layer1 cycle — per-LayerConfig for wire/cfg symmetry, but
        // the two must agree (a differing period is meaningless + would make the overlap check read a stale half).
        if (a.window_period_ms != b.window_period_ms)   return false;
        if (a.window_period_ms == 0)                    return false;   // §3.2 validate-not-clamp: refuse a 0 cycle
        if (a.window_ms && b.window_ms && a.window_ms + b.window_ms > a.window_period_ms) return false;  // explicit windows overlap
    }

    _cfg = cfg;
    // Slice 0: a single-layer node mirrors its legacy scalars into layers[0] (backward-compat until Slice 2a
    // migrates the readers). A gateway (n_layers==2) supplies layers[0..1] explicitly (validated above).
    if (_cfg.n_layers <= 1) {
        _cfg.n_layers = 1;
        _cfg.layers[0].layer_id          = _cfg.leaf_id;          // single-layer: layer_id == leaf_id (may be 0 for R1)
        _cfg.layers[0].node_id           = _node_id;              // single-layer: the one node_id (Slice 3a)
        _cfg.layers[0].routing_sf        = _cfg.routing_sf;
        _cfg.layers[0].allowed_sf_bitmap = _cfg.allowed_sf_bitmap;
        _cfg.layers[0].beacon_period_ms  = _cfg.beacon_period_ms;
        _cfg.layers[0].window_ms         = _cfg.layers[0].window_period_ms;   // no split: one window == whole period (always-on)
    }
    _n_layers = _cfg.n_layers;   // Slice 3c: mirror the (normalized) layer count to the runtime member activate_layer guards on
    // is_gateway is DERIVED, NOT configurable (user 2026-06-13): a gateway IS a dual-layer node (n_layers==2). The
    // single authoritative derivation point — overrides any cfg/NV value. So `cfg gateway` is gone, the wire bit a node
    // advertises about ITSELF (self_gateway / J gateway_capable) now reliably means "dual-layer gateway", and the iOS
    // companion's ready/status `"gateway"` is trustworthy. (The LEARNED neighbour is_gateway flag is a separate field.)
    _cfg.is_gateway = (_cfg.n_layers == 2);
    // Slice 3a: a GATEWAY derives the SF-weighted, anti-phase window split (§0.9/§4) for any window_ms/offset left
    // 0 (= DERIVE). Throughput-symmetric = equal DATA bytes/cycle, NOT equal time: weight each window by the layer
    // SF's MARGINAL per-byte airtime (preamble cancels), so the slower (higher-SF) leaf gets the longer window:
    //   window_i = period * per_byte_air(sf_i) / (per_byte_air(sf_0) + per_byte_air(sf_1)); offset[1] = window[0].
    if (_cfg.n_layers == 2) {
        LayerConfig& L0 = _cfg.layers[0]; LayerConfig& L1 = _cfg.layers[1];
        const uint32_t period = L0.window_period_ms;
        auto per_byte_air = [&](uint8_t sf) -> uint32_t {     // marginal payload airtime for 120 B (preamble cancels)
            return airtime_ms(sf, _cfg.radio_bw_hz, _cfg.radio_cr, protocol::preamble_sym, 240)
                 - airtime_ms(sf, _cfg.radio_bw_hz, _cfg.radio_cr, protocol::preamble_sym, 120);
        };
        const uint32_t w0 = per_byte_air(L0.routing_sf), w1 = per_byte_air(L1.routing_sf);
        if (w0 + w1 == 0) return false;                                       // degenerate (guard; SFs are 5..12)
        if (L0.window_ms == 0 && L1.window_ms == 0) {                         // both DERIVE: SF-weighted, fill the period
            L0.window_ms = static_cast<uint32_t>(static_cast<uint64_t>(period) * w0 / (w0 + w1));
            L1.window_ms = period - L0.window_ms;
        } else if (L0.window_ms == 0) {                                       // one explicit -> the other fills the rest
            L0.window_ms = (period > L1.window_ms) ? period - L1.window_ms : 0;
        } else if (L1.window_ms == 0) {
            L1.window_ms = (period > L0.window_ms) ? period - L0.window_ms : 0;
        }
        if (L1.window_offset_ms == 0) L1.window_offset_ms = L0.window_ms;     // anti-phase (layer0 offset stays 0)
        // Validate the concrete schedule (fail loud, no clamp): non-zero windows, each fits the period, no overlap.
        if (L0.window_ms == 0 || L1.window_ms == 0) return false;
        if (L0.window_offset_ms + L0.window_ms > period) return false;
        if (L1.window_offset_ms + L1.window_ms > period) return false;
        if (L0.window_offset_ms < L1.window_offset_ms + L1.window_ms &&
            L1.window_offset_ms < L0.window_offset_ms + L0.window_ms) return false;   // intervals overlap
        // §3e F-C: a window must FIT the wire's 8-bit ×100ms duration/offset field (max 25.5 s, no escape unit). Beyond
        // that, the schedule_record silently clamps + the receiver's defer phase math breaks. Refuse, don't clamp. The
        // offset is bounded by the active window (active leaf -> 0, foreign leaf <= one active window), so this covers both.
        if (L0.window_ms > protocol::gateway_schedule_window_max_ms ||
            L1.window_ms > protocol::gateway_schedule_window_max_ms) return false;
    }
    // Lua: (SF_DEMOD_THRESHOLD[routing_sf] or -240) + sf_margin_q4 (dv_dual_sf.lua:8386).
    // The out-of-range fallback is the literal -240 (SF10), NOT table[12].
    const int16_t demod = (_cfg.routing_sf >= 5 && _cfg.routing_sf <= 12)
                          ? protocol::sf_demod_threshold_q4_table[_cfg.routing_sf]
                          : static_cast<int16_t>(-240);
    _routing_snr_floor_q4 = static_cast<int16_t>(demod + protocol::sf_margin_q4);
    _hal.set_rx_sf(_cfg.routing_sf);                       // listen on routing SF

    // R4.0 duty-cycle budget = floor(duty_cycle * window) (Lua dv:8497). 0 => disabled (HEALTHY).
    _duty_cycle_budget_ms = (_cfg.duty_cycle > 0.0)
        ? static_cast<uint64_t>(_cfg.duty_cycle * _cfg.duty_cycle_window_ms)
        : 0;

    // R4.5 LBT delays (Lua dv:8628-8632). 0-config => derive: backoff = max(1, retry_jitter/2); the flood
    // max-defer = one full-size beacon's airtime. (retry_jitter_ms() is the same RTS_LEN=8 timing constant.)
    _lbt_backoff_ms = (_cfg.lbt_backoff_ms > 0) ? _cfg.lbt_backoff_ms
                      : (retry_jitter_ms() / 2 > 1 ? retry_jitter_ms() / 2 : 1);
    _flood_lbt_max_defer_ms = (_cfg.flood_lbt_max_defer_ms > 0) ? _cfg.flood_lbt_max_defer_ms
                              : airtime_routing_ms(protocol::beacon_max_bytes);

    // Slice 3c: a GATEWAY boots on leaf 0 — activate_layer(0) overrides the active-layer scalars (routing_sf /
    // leaf_id / beacon_period_ms / node_id), the SNR floor + LBT timing, and retunes the radio to leaf 0's SF.
    // (Runs AFTER the legacy-scalar setup above, which it supersedes; the discovery/beacon arming below then
    // reads leaf 0's values.) A single-layer node never activates — its scalars stay as set above (no-op path).
    if (_cfg.n_layers == 2) {
        activate_layer(0);                                                       // boot on leaf 0
        set_window_anchors(0);                                                   // Slice 3e: seed the countdown anchors
        (void)_hal.after(_cfg.layers[0].window_ms, kLayerWindowTimerId);         // Slice 3d: arm the first window-switch (leaf0 -> leaf1)
    }

    // Discovery window: boot in fast-cadence / full-page mode until we have heard
    // enough of the mesh or a bounded timeout expires (dv_dual_sf.lua:8399-8401).
    _discovery_started_ms   = _hal.now();
    _discovery_mode         = (protocol::discovery_ms > 0);
    _discovery_until_ms     = _discovery_started_ms + protocol::discovery_ms;
    _discovery_bcn_rx_count = 0;

    // Arm the first beacon spread across the (phase-dependent) period to avoid a mass-boot burst (dv:9027-9035).
    // A GATEWAY does NOT use the shared kBeaconTimerId — its single deadline would HALVE the per-leaf cadence (one
    // beacon/period landing on whichever leaf is active then). Instead it beacons each leaf at WINDOW-ACTIVATION on
    // that leaf's own cadence (maybe_emit_gateway_beacon); seed leaf 0 now (after discovery state is set above).
    if (_cfg.n_layers == 2) {
        maybe_emit_gateway_beacon();
    } else {
        const int first_period = static_cast<int>(in_discovery() ? protocol::discovery_beacon_period_ms
                                                                 : _cfg.beacon_period_ms);
        (void)_hal.after(static_cast<uint32_t>(_hal.rand_range(0, first_period)), kBeaconTimerId);
    }
    // Periodic route-aging sweep (dv_dual_sf.lua:9080-9086).
    (void)_hal.after(_cfg.rt_aging_check_period_ms, kAgingTimerId);
    // REQ_SYNC bootstrap (dv_dual_sf.lua:9166-9175): after a listen window, broadcast a REQ_SYNC Q
    // while still in discovery + route-starved, so a sparse joiner pulls neighbours' tables instead
    // of waiting out the slow periodic-beacon rotation. The loop (kReqSyncTimerId) re-arms itself.
    if (_cfg.req_sync_on_boot && in_discovery())
        (void)_hal.after(protocol::req_sync_listen_ms, kReqSyncTimerId);
    // Hash-locate A0: seed our OWN binding (authenticated) so we resolve self-directed H queries (Lua
    // dv:9072). node_id 0 is unprovisioned (no identity yet) — set_identity re-seeds after a join/cfg.
    if (_node_id != 0) id_bind_set(_node_id, _key_hash32, IdBindSource::self, IdBindConf::authoritative);
    return true;
}

// ---- Slice 3c: dual-layer leaf activation -------------------------------------------------------------------
// SF_DEMOD_THRESHOLD[sf] + sf_margin (Lua dv:8386); the -240 out-of-range fallback is the literal (SF10), not table[12].
int16_t Node::routing_snr_floor_for(uint8_t routing_sf) const {
    const int16_t demod = (routing_sf >= 5 && routing_sf <= 12)
                          ? protocol::sf_demod_threshold_q4_table[routing_sf] : static_cast<int16_t>(-240);
    return static_cast<int16_t>(demod + protocol::sf_margin_q4);
}

// §4 busy-guard: NEVER switch leaves mid-exchange. An in-flight RTS/CTS/DATA/ACK (_pending_tx / _pending_rx), the
// post-ACK deliver/forward that straddles the ACK (_post_ack.pending — must finish on its own leaf, code-verified
// 2026-06-12), or a BUSY_RX same-hop re-RTS wait (_nack_wait_pending — a paused flight) must complete first. The
// scheduler (3d) re-arms the switch after gateway_layer_busy_retry_ms when this returns true.
bool Node::layer_swap_blocked() const {
    if (_active->_pending_tx.has_value() || _active->_pending_rx.has_value()
        || _active->_post_ack.pending || _nack_wait_pending) return true;
    // Node-GLOBAL transient stash holding a frame packed for the LEAVING leaf's SF/id (the id-classification's
    // guard_defer set): the LBT-deferred ring + the on-radio-busy / duty-defer re-issue stash. They clear in ms
    // (one LBT backoff), so deferring the swap until then is a bounded wait — and stops a wrong-leaf-SF re-TX.
    for (uint8_t s = 0; s < kLbtSlots;   ++s) if (_deferred_lbt[s].pending)         return true;
    // Gate on reissue_pending (a busy/duty re-issue timer is ARMED), NOT bare `valid`: a cleanly-sent CTS/ACK/NACK
    // leaves its stash `valid` (the buffer is only cleared by a newer same-tag TX or an on_radio_busy giveup), so a
    // bare-`valid` gate left a gateway's first ACK blocking the layer swap FOREVER (the bridged DM never transmits).
    for (uint8_t s = 0; s < kRetrySlots; ++s) if (_tx_stash[s].valid && _tx_stash[s].reissue_pending) return true;
    return false;
}

// Switch the active leaf. The window scheduler (3d) drives this on timers, GATED by layer_swap_blocked(). Steps:
//  (1) drain the LEAVING leaf's sync-response ring — its timer ids (kSyncResponseTimerId+slot) are SHARED across
//      leaves, so a stale fire would hit the wrong leaf / leak a slot. (The single-flight MAC timers are covered by
//      layer_swap_blocked() — none in flight here; the channel rings are gateway-skipped, Principle 11.)
//  (2) make the active-layer scalars + SF-derived timing (the Lua active_*) reflect leaf i;  (3) swap _active;
//  (4) retune the radio + the Hal short-id;  (5) re-seed the now-active leaf's own id_bind binding.
void Node::activate_layer(uint8_t i) {
    if (i >= _n_layers) return;                                  // defensive: n_layers==2 only; single-layer never swaps
    for (uint8_t s = 0; s < protocol::cap_sync_response_pending; ++s)
        if (_active->_sync_pending[s].active) { _hal.cancel(kSyncResponseTimerId + s); _active->_sync_pending[s].active = false; }
    // Re-home (LEAVE): cancel the leaving leaf's per-leaf queue/drain timers (shared wheel ids — the id-classification
    // rehome set). The STATE lives per-leaf in LayerRuntime (preserved); only the wheel slots re-home, on ENTER below.
    // (The periodic BEACON plane — kBeaconTimerId/kTriggeredBeaconTimerId/kBeaconJitterTimerId + kReqSyncTimerId — is a
    // PER-LEAF-TIMER-id job for Slice 3d: a cancel+rearm here would RESET its long cadence on every window.)
    _hal.cancel(kDeferredDrainTimerId);
    _hal.cancel(kQueueWakeupTimerId);
    _hal.cancel(kCascadeRequeueTimerId);

    const LayerConfig& L = _cfg.layers[i];
    _cfg.routing_sf        = L.routing_sf;                       // active-layer scalars the active-layer-shared MAC reads
    _cfg.allowed_sf_bitmap = L.allowed_sf_bitmap;
    _cfg.leaf_id           = static_cast<uint8_t>(L.layer_id & 0x0F);   // the byte-0 wire leaf filter
    _cfg.beacon_period_ms  = L.beacon_period_ms;
    _node_id               = L.node_id;                          // the leaf's own 8-bit address (static; live DAD deferred)
    _routing_snr_floor_q4  = routing_snr_floor_for(L.routing_sf);
    _lbt_backoff_ms        = (_cfg.lbt_backoff_ms > 0) ? _cfg.lbt_backoff_ms     // SF-derived timing for the new leaf
                             : (retry_jitter_ms() / 2 > 1 ? retry_jitter_ms() / 2 : 1);
    _flood_lbt_max_defer_ms = (_cfg.flood_lbt_max_defer_ms > 0) ? _cfg.flood_lbt_max_defer_ms
                              : airtime_routing_ms(protocol::beacon_max_bytes);
    _active = &_layers[i];                                       // THE SWAP — the MAC pump now operates on leaf i
    _hal.set_rx_sf(L.routing_sf);                               // retune RX (SF latches in standby)
    _hal.set_protocol_id(L.node_id);                           // Hal short-id = the active leaf's node_id
    if (L.node_id != 0)                                          // seed leaf i's OWN id_bind binding (per-leaf table)
        id_bind_set(L.node_id, _key_hash32, IdBindSource::self, IdBindConf::authoritative);
    // Re-home (ENTER): re-derive the entering leaf's queue/drain drivers from its preserved LayerRuntime state.
    // Slice 4c.1: drain any cross-layer handoffs targeting THIS leaf into its tx_queue FIRST (now that _active is it),
    // so become_free() carries the bridged relay legs in this window. become_free() re-services the tx_queue (covers
    // the self-safe queue-wakeup / cascade-requeue ids); the deferred TTL-drain re-arms iff this leaf still has no-route
    // sends parked (1s period << a window — no cadence skew).
    drain_xl_handoffs_for_leaf(i);
    become_free();
    if (_active->_deferred_n > 0) { _active->_drain_armed = true; (void)_hal.after(protocol::send_defer_drain_period_ms, kDeferredDrainTimerId); }
}

// Slice 3d: the gateway WINDOW SCHEDULER (kLayerWindowTimerId). The two leaves' windows are anti-phase + back-to-back
// (§4): leaf 0 is active for window_ms[0], then leaf 1 for window_ms[1], summing to the period — so the "close" of one
// leaf IS the "open" of the other, ONE recurring switch event. Boot arms the first switch (after leaf-0's window);
// each fire alternates the active leaf + arms the next switch after the NOW-active leaf's window. BUSY-GUARD (Lua
// gate order): if mid-exchange, HOLD the current leaf + re-evaluate after gateway_layer_busy_retry_ms (the window
// slips, never yanks a flight) — NO separate close-retry, matching the Lua (the next switch re-checks).
void Node::window_switch_fire() {
    if (_n_layers != 2) return;                                    // gateways only (defensive; never armed single-layer)
    const uint8_t next = (_active == &_layers[0]) ? 1 : 0;
    if (layer_swap_blocked()) {                                    // mid-exchange -> hold, re-evaluate after the busy-retry
        // Telemetry: a deferral is OBSERVABLE so leaf STARVATION (one leaf reliably busy at switch time -> the other
        // never gets serviced -> cross-layer DMs to it never bridge, the gateway's core job) shows up in the sim. The
        // slip is currently UNBOUNDED (matches the Lua §4 "the window slips, never yanks a flight"); a max-hold /
        // drain-then-switch bound is an OPEN design item — gate it on this event's frequency in the sim.
        MR_TELEMETRY(
            EventField f[] = { { .key = "held_leaf", .type = EventField::T::i64, .i = (next == 0) ? 1 : 0 },
                               { .key = "next_leaf", .type = EventField::T::i64, .i = next } };
            _hal.emit("gateway_layer_window_deferred", f, 2); );
        (void)_hal.after(protocol::gateway_layer_busy_retry_ms, kLayerWindowTimerId);
        return;
    }
    activate_layer(next);
    set_window_anchors(next);                                            // Slice 3e: refresh the countdown anchors BEFORE the beacon advertises them
    maybe_emit_gateway_beacon();                                          // beacon the entering leaf iff its cadence is due
    (void)_hal.after(_cfg.layers[next].window_ms, kLayerWindowTimerId);   // next switch when this leaf's window closes
}

// Slice 3e: refresh each leaf's _next_open_ms — the anchor for the receiver-anchored schedule countdown. The just-
// activated leaf re-opens in a FULL cycle (now+period; its countdown will %period back to 0 = "open now"); the other
// leaf opens when this leaf's window closes (now + this leaf's window_ms). Robust to busy-defer slip (re-derived each switch).
void Node::set_window_anchors(uint8_t active_leaf) {
    const uint64_t now    = _hal.now();
    const uint32_t period = _cfg.layers[0].window_period_ms;
    _layers[active_leaf]._next_open_ms     = now + period;
    _layers[1 - active_leaf]._next_open_ms = now + _cfg.layers[active_leaf].window_ms;
}

// ---- Slice 3e.2: learned gateway schedules (RX consume + the sender-defer) ----------------------------------
const GatewaySchedule* Node::find_gw_schedule(uint8_t gw_node_id) const {
    for (uint8_t i = 0; i < protocol::cap_gateway_neighbor_schedules; ++i)
        if (_gw_schedules[i].valid && _gw_schedules[i].gw_node_id == gw_node_id) return &_gw_schedules[i];
    return nullptr;
}

void Node::store_gateway_schedule(const GatewaySchedule& gs) {
    uint8_t slot = 0xFF, oldest = 0;                               // refresh-by-id, else a free slot, else evict-oldest
    for (uint8_t i = 0; i < protocol::cap_gateway_neighbor_schedules; ++i) {
        if (_gw_schedules[i].valid && _gw_schedules[i].gw_node_id == gs.gw_node_id) { slot = i; break; }
        if (!_gw_schedules[i].valid && slot == 0xFF) slot = i;
        if (_gw_schedules[i].heard_ms < _gw_schedules[oldest].heard_ms) oldest = i;
    }
    _gw_schedules[(slot == 0xFF) ? oldest : slot] = gs;
}

// Multi-hop gateway discovery: record "gw_id bridges TO dest_leaf" (last-write-wins, one row per gw_id — Lua dv:4936).
// §6 OUT-OF-SCOPE (documented, not silently mis-routed): a SINGLE gateway bridging 3+ layers via PROPAGATION loses all
// but the last dest_leaf here (one row/gw_id). Direct neighbours still know every served leaf via _gw_schedules; full
// multi-bridge propagation belongs with the 3-layer work. Today's 2-layer gateways advertise exactly one other leaf.
void Node::ingest_bridged_layer(uint8_t gw_id, uint8_t dest_leaf) {
    uint8_t slot = 0xFF, oldest = 0;                               // refresh-by-id, else a free slot, else evict-oldest
    for (uint8_t i = 0; i < protocol::cap_bridged_layers; ++i) {
        if (_bridged_layers[i].valid && _bridged_layers[i].gw_id == gw_id) { slot = i; break; }
        if (!_bridged_layers[i].valid && slot == 0xFF) slot = i;
        if (_bridged_layers[i].last_seen_ms < _bridged_layers[oldest].last_seen_ms) oldest = i;
    }
    BridgedLayer& b = _bridged_layers[(slot == 0xFF) ? oldest : slot];
    b.valid = true; b.gw_id = gw_id; b.dest_leaf = dest_leaf; b.last_seen_ms = _hal.now();
}

void Node::prune_aged_bridged_layers(uint64_t now) {
    for (uint8_t i = 0; i < protocol::cap_bridged_layers; ++i)
        if (_bridged_layers[i].valid && now > _bridged_layers[i].last_seen_ms + protocol::bridged_layers_ttl_ms)
            _bridged_layers[i].valid = false;
}

// ms to defer an RTS so it lands during the gateway's window on OUR leaf (Lua gateway_schedule_defer_ms dv:5013). For
// each record: visit_start = heard_ms + offset (the receiver-anchored open, NO shared clock); phase = (now-visit_start)
// mod period. A FOREIGN-leaf record currently OPEN means the gateway is deaf to us -> wait until it ends. OUR-leaf record
// when the gateway is AWAY -> wait until our window comes around. Take the max defer; 0 = reachable now, send.
uint32_t Node::gateway_schedule_defer_ms(uint8_t gw_node_id) const {
    const GatewaySchedule* s = find_gw_schedule(gw_node_id);
    if (!s || !s->valid || s->period_ms == 0) return 0;           // unknown / no schedule -> send now
    const uint64_t now     = _hal.now();
    const uint8_t  my_leaf = _cfg.leaf_id;
    uint32_t best = 0;
    for (uint8_t i = 0; i < s->n_rec; ++i) {
        const GatewaySchedule::Rec& r = s->rec[i];
        const uint64_t visit_start = s->heard_ms + r.offset_ms;
        const int64_t  raw   = static_cast<int64_t>(now) - static_cast<int64_t>(visit_start);
        const uint32_t phase = static_cast<uint32_t>(((raw % s->period_ms) + s->period_ms) % s->period_ms);
        uint32_t defer = 0;
        if (r.leaf_id != my_leaf) {                               // foreign visit currently open -> deaf to us, wait it out
            if (phase < r.window_ms) defer = r.window_ms - phase + protocol::gateway_schedule_guard_ms;
        } else {                                                  // our visit not open yet -> wait until it comes around
            if (phase >= r.window_ms) defer = s->period_ms - phase + protocol::gateway_schedule_guard_ms;
        }
        if (defer > best) best = defer;
    }
    return best;   // (+ herd-jitter when gateway_spread_nibble is wired; 3e.1 left it 0 = no jitter)
}

// Slice 3d: per-leaf beacon — at WINDOW-ACTIVATION, beacon the now-active leaf iff its OWN cadence is due. The window
// schedule provides the emit opportunity (correct SF + the leaf is idle post-swap, so emit_beacon won't busy-skip); the
// per-leaf _last_beacon_ms gates it to the period (visit-quantized: at the first visit past due, ±one window). Replaces
// the shared kBeaconTimerId for gateways. Also drives the gateway's discovery-exit (no shared timer calls it otherwise).
void Node::maybe_emit_gateway_beacon() {
    maybe_exit_discovery("gateway_window");
    const uint64_t now    = _hal.now();
    const uint32_t period = in_discovery() ? protocol::discovery_beacon_period_ms : _cfg.beacon_period_ms;
    // The Lua's differential "gateway_sweep" (dv:8430): beacon on entry if the leaf has DIRTY routes (new info to
    // propagate NOW, don't wait out the period) — OR the periodic refresh cadence is due. emit_beacon sends dirty-only
    // in steady state (clearing the dirty flags), so the next visit stays silent unless something new appeared.
    bool dirty = false;
    for (uint8_t i = 0; i < _active->_rt_count; ++i) if (_active->_rt[i].dirty) { dirty = true; break; }
    if (dirty || now - _active->_last_beacon_ms >= period) {
        emit_beacon("gateway_window");                                    // guarded (busy/budget skip)
        _active->_last_beacon_ms = now;
    }
}

// ---- dispatch (timer ids -> subsystem handlers; RX cmd-nibble -> handlers) --

void Node::on_timer(uint32_t timer_id) {
    switch (timer_id) {
    case kBeaconTimerId: {
        periodic_beacon_fire();       // R4.3 throttle body (may emit now, skip, or defer to kBeaconJitterTimerId)
        maybe_exit_discovery("timer");// UNCONDITIONAL before the re-arm (dv:7858) so the period reflects the state
        // Re-arm ±20% jitter [0.8P, 1.2P] inclusive (dv_dual_sf.lua:7858-7864).
        // Period reflects the (possibly just-exited) discovery state. Integer
        // floor division; +1 makes hi inclusive (rand_range is [lo,hi)).
        const uint32_t P  = in_discovery() ? protocol::discovery_beacon_period_ms
                                           : _cfg.beacon_period_ms;
        const int      lo = static_cast<int>(P * 4 / 5);
        const int      hi = static_cast<int>(P * 6 / 5);
        (void)_hal.after(static_cast<uint32_t>(_hal.rand_range(lo, hi + 1)), kBeaconTimerId);
        break;
    }
    case kAgingTimerId:
        age_out_stale_routes();
        id_bind_age_out();            // hash-locate A0: drop expired bindings on the same periodic sweep
        age_out_parked_sends();       // hash-locate D: give up on DMs whose hash never resolved
        age_out_denied_ids();         // node_id DAD: a denied slot becomes reusable after dad_denied_id_ttl_ms
        age_out_mediated();           // L2a: drop mediation-suppression records past the window
        (void)_hal.after(_cfg.rt_aging_check_period_ms, kAgingTimerId);
        break;
    case kTriggeredBeaconTimerId:
        _triggered_beacon_pending = false;   // clear BEFORE emit so a re-trigger can re-arm
        emit_beacon("triggered");
        break;
    // ---- R3 data-plane timers ----
    case kRtsTimeoutTimerId:      rts_timeout_fire();      break;
    case kAckTimeoutTimerId:      ack_timeout_fire();      break;
    case kPendingRxExpiryTimerId: pending_rx_expiry_fire();break;
    case kCtsToDataGapTimerId:    do_data_tx();            break;
    case kQueueWakeupTimerId:     become_free();           break;
    case kPostAckTimerId:         do_post_ack();           break;
    case kRetryBackoffTimerId:    tx_rts_retry();          break;
    case kDeferredDrainTimerId:   try_drain_deferred();    break;   // periodic no-route drain / TTL giveup
    case kReqSyncTimerId:         req_sync_loop_fire();    break;   // REQ_SYNC boot loop: send + re-arm while starved
    case kMBcastClearTimerId:                                       // M-broadcast fire-and-forget: clear the flight (no ACK)
        if (_active->_pending_tx && _active->_pending_tx->m_broadcast) { _active->_pending_tx.reset(); become_free(); }
        break;
    case kOverhearRetuneTimerId:                                            // overhear ARM: retune RX back to routing_sf
        _hal.set_rx_sf(_cfg.routing_sf);
        // §4.4: a FLOOD flood-state still awaiting its DATA-M (caught the RTS-M, missed the body) -> fast-self-pull
        // from its src now. Single-radio + SF-gating normally means at most ONE awaiting_data state (§4.2), but
        // resolve ALL of them — never strand an awaiting_data slot if that invariant is ever broken (2nd radio /
        // a retune-logic change), which would otherwise leak the slot until reboot. (Defense-in-depth.)
        for (uint8_t i = 0; i < protocol::cap_flood_pending; ++i)
            if (_active->_flood[i].active && _active->_flood[i].awaiting_data) flood_fast_self_pull(i);
        break;
    case kJoinClaimGuardTimerId:  join_claim_guard_fire();         break;   // node_id DAD: guard elapsed -> adopt-or-deny
    case kJoinRetryTimerId:       join_start_claim("retry");       break;   // node_id DAD: re-claim after a lost claim/heal
    case kJoinListenTimerId:      _join_listen_pending = false; join_start_claim("listen_done"); break;   // L1: listen window done -> claim
    case kCascadeRequeueTimerId:  become_free();           break;   // backoff elapsed -> drain the requeued flight
    case kRtsDutyDeferTimerId:    rts_duty_defer_fire();   break;   // #A redo: over-budget RTS duty-defer re-check/hand
    case kNackWaitTimerId:                                          // BUSY_RX wait elapsed -> re-RTS SAME hop
        if (_nack_wait_pending) {
            _nack_wait_pending = false;
            if (_active->_pending_tx && _active->_pending_tx->ctr_lo == _nack_wait_ctr_lo) tx_rts_retry();
        }
        break;
    case kLayerWindowTimerId:     window_switch_fire();    break;   // Slice 3d: gateway window scheduler — alternate the active leaf
    default:
        // R4.5 LBT deferred-TX slots occupy the id range [kLbtDeferTimerId, +kLbtSlots) — each fires its own slot.
        if (timer_id >= kLbtDeferTimerId && timer_id < kLbtDeferTimerId + kLbtSlots) {
            DeferredLbt& d = _deferred_lbt[timer_id - kLbtDeferTimerId];
            if (d.pending) { d.pending = false;
                lbt_complete(d.buf, d.len, d.sf, static_cast<LbtKind>(d.kind), d.rts_flight_gen); }
        } else if (timer_id >= kRadioBusyRetryTimerId && timer_id < kRadioBusyRetryTimerId + kRetrySlots) {
            retry_stashed(static_cast<uint8_t>(timer_id - kRadioBusyRetryTimerId));   // R4.5b stash re-issue
        } else if (timer_id >= kDutyDeferTimerId && timer_id < kDutyDeferTimerId + kRetrySlots) {
            duty_defer_fire(static_cast<uint8_t>(timer_id - kDutyDeferTimerId));      // #2 duty-defer re-run
        } else if (timer_id >= kBeaconJitterTimerId && timer_id < kBeaconJitterTimerId + kBeaconJitterSlots) {
            deferred_beacon_jitter_fire(static_cast<uint8_t>(timer_id - kBeaconJitterTimerId));   // #D ring slot
        } else if (timer_id >= kSyncResponseTimerId && timer_id < kSyncResponseTimerId + kSyncRespSlots) {
            sync_response_fire(static_cast<uint8_t>(timer_id - kSyncResponseTimerId));            // REQ_SYNC jittered reply ring slot
        } else if (timer_id >= kChannelPullTimerId && timer_id < kChannelPullTimerId + kChannelPullSlots) {
            channel_pull_fire(static_cast<uint8_t>(timer_id - kChannelPullTimerId));             // channel CHANNEL_PULL jittered fire
        } else if (timer_id >= kFloodRebcastTimerId && timer_id < kFloodRebcastTimerId + protocol::cap_flood_pending) {
            flood_rebroadcast_fire(static_cast<uint8_t>(timer_id - kFloodRebcastTimerId));       // channel FLOOD rebroadcast ring slot
        }
        break;
    }
}

void Node::on_recv(const uint8_t* bytes, size_t len, const RxMeta& meta) {
    if (len < 1) return;
    // R4.3 channel-busy witness: ANY successful decode means the channel was busy now (broadcast OR
    // unicast, beacon OR data) — the throttle reads this to suppress the next beacon (dv:9164). No rand.
    _last_rx_routing_sf_ms = _hal.now();
    switch (wire::cmd_of(bytes[0])) {
        case wire::Cmd::B: ingest_beacon(bytes, len, meta); break;   // R1/R2 beacon (+max-idle witness set INSIDE, post-guards)
        case wire::Cmd::R: handle_rts (bytes, len, meta); break;     // R3 RTS  -> CTS
        case wire::Cmd::C: handle_cts (bytes, len, meta); break;     // R3 CTS  -> DATA
        case wire::Cmd::D: handle_data(bytes, len, meta); break;     // R3 DATA -> deliver/forward + ACK
        case wire::Cmd::K: handle_ack (bytes, len, meta); break;     // R3 ACK  -> done
        case wire::Cmd::N: handle_nack(bytes, len, meta); break;     // NACK -> blind+wait / cascade
        case wire::Cmd::F: handle_f  (bytes, len, meta); break;     // F route-find RREQ/RREP flood
        case wire::Cmd::Q: handle_q  (bytes, len, meta); break;     // Q REQ_SYNC route-bootstrap (-> jittered sync beacon)
        case wire::Cmd::H: handle_h  (bytes, len, meta); break;     // H hash-locate flood (key_hash32 -> node_id)
        case wire::Cmd::J: handle_j  (bytes, len, meta); break;     // J node_id DAD (CLAIM/DENY -> claim/heal)
        case wire::Cmd::M: handle_channel_data(bytes, len, meta); break;  // M lean channel-message frame (cmd 0xA) -> leaf gate + ingest
        default: break;                                              // rest ignored
    }
}

// ---- the typed command seam (the app<->firmware entrypoint) -----------------

// Pack a send_layer destination path into the CmdResult.layer_path correlation token: hops MSB-first, hops[0] in
// the highest used byte ([2,3] -> (2<<8)|3 = 0x0203). Layer ids are >=1, so no leading-zero hop (unambiguous).
static uint32_t pack_layer_path(const uint8_t* hops, uint8_t hop_count) {
    uint32_t v = 0;
    for (uint8_t i = 0; i < hop_count; ++i) v = (v << 8) | hops[i];
    return v;
}

CmdResult Node::on_command(const Command& c) {
    switch (c.kind) {
        case CmdKind::send: {
            if (_node_id == 0)                                    // unprovisioned: must join / cfg set node_id
                return CmdResult{ CmdCode::err_unprovisioned, 0, _active->_tx_queue_n };
            if (_cfg.allowed_sf_bitmap == 0)                      // no data SF (empty sf_list): refuse — no silent fallback
                return CmdResult{ CmdCode::err_no_data_sf, 0, _active->_tx_queue_n };
            if (c.body_len > protocol::dm_max_body_bytes)         // body + the 2-B inner prefix must fit inner[] (no OOB)
                return CmdResult{ CmdCode::err_too_large, 0, _active->_tx_queue_n };
            if (c.u.send.dst_hash != 0) {                         // address-by-hash (hash-locate): resolve, then send
                const uint16_t ctr = send_by_hash(c.u.send.dst_hash, c.body, c.body_len, c.u.send.flags);
                return CmdResult{ CmdCode::queued, ctr, _active->_tx_queue_n, c.u.send.dst_hash, /*layer_path*/ 0 };
            }
            const uint16_t ctr = do_send(c.u.send.dst_id, c.body, c.body_len, c.u.send.flags);
            return CmdResult{ CmdCode::queued, ctr, _active->_tx_queue_n };   // id-addressed: dst_hash/layer_path = 0
        }
        case CmdKind::send_channel: {                         // ROADMAP §3 channel gossip (single-layer)
            if (_node_id == 0)                                // unprovisioned: must join / cfg set node_id
                return CmdResult{ CmdCode::err_unprovisioned, 0, _active->_tx_queue_n };
            if (_cfg.allowed_sf_bitmap == 0)                  // channel gossip rides a data SF: refuse if none configured
                return CmdResult{ CmdCode::err_no_data_sf, 0, _active->_tx_queue_n };
            if (c.body_len > protocol::channel_msg_max_payload_bytes)
                return CmdResult{ CmdCode::err_too_large, 0, _active->_tx_queue_n };
            const uint16_t ctr = do_send_channel(c.u.channel.channel_id, c.body, c.body_len);
            return CmdResult{ CmdCode::queued, ctr, _active->_tx_queue_n };   // buffered dirty -> advertised next BCN -> pulled
        }
        case CmdKind::join: {        // node_id DAD. Idempotent once joined. CLAIM-AFTER-LISTEN (L1): hear the
                                     // leaf's beacons first (populate _active->_rt/_active->_id_bind so the picker sees existing
                                     // ids), THEN claim — armed here, fired on kJoinListenTimerId.
            if (_joined) return CmdResult{ CmdCode::queued, 0, _active->_tx_queue_n };
            if (!_join_claim.active && !_join_listen_pending) {
                _join_listen_pending = true;
                (void)_hal.after(protocol::join_listen_ms, kJoinListenTimerId);
            }
            return CmdResult{ CmdCode::queued, 0, _active->_tx_queue_n };
        }
        case CmdKind::resolve: {     // diagnostic hash-locate (no DM) — the answer rides the hash_resolved push
            if (_node_id == 0)       // unprovisioned: the H flood needs a valid origin
                return CmdResult{ CmdCode::err_unprovisioned, 0, _active->_tx_queue_n };
            request_resolve(c.u.resolve.dst_hash, c.u.resolve.hard);
            return CmdResult{ CmdCode::queued, 0, _active->_tx_queue_n };
        }
        case CmdKind::send_layer: {                          // Slice 4d: cross-layer DM origination (§5)
            if (_node_id == 0)                               return CmdResult{ CmdCode::err_unprovisioned, 0, _active->_tx_queue_n };
            if (_cfg.allowed_sf_bitmap == 0)                 return CmdResult{ CmdCode::err_no_data_sf, 0, _active->_tx_queue_n };
            if (c.body_len > protocol::dm_max_body_bytes)    return CmdResult{ CmdCode::err_too_large, 0, _active->_tx_queue_n };
            // Every send_layer return echoes the dst_hash (and, once known, the layer_path) so the app holds the
            // full "send handle" (CmdResult.dst_hash + layer_path); async pushes then correlate by CmdResult.ctr.
            if (c.u.layer.dst_hash == 0)                     return CmdResult{ CmdCode::err_unsupported, 0, _active->_tx_queue_n };  // a layer send needs a stable dst key
            if (c.u.layer.hop_count > 0) {
                // EXPLICIT-PATH origination (the user supplied the destination layer path) — route by it, no H-query.
                // Validate the path fail-loud (§5, no silent fix): the full path (1 + hop_count, after prepending our
                // own layer) must fit gw_env_max_hops; every layer id >= 1; and hops[0] must not be our OWN layer (a
                // cross-layer send to your own layer is a misconfig).
                const uint8_t hc = c.u.layer.hop_count;
                const uint32_t lp = pack_layer_path(c.u.layer.hops, hc);
                if (hc > protocol::gw_env_max_hops - 1)      return CmdResult{ CmdCode::err_unsupported, 0, _active->_tx_queue_n, c.u.layer.dst_hash, lp };  // path too long
                for (uint8_t i = 0; i < hc; ++i)
                    if (c.u.layer.hops[i] == 0)              return CmdResult{ CmdCode::err_unsupported, 0, _active->_tx_queue_n, c.u.layer.dst_hash, 0 };  // layer id 0 is unset (path invalid -> layer_path omitted)
                if (c.u.layer.hops[0] == active_layer_id())  return CmdResult{ CmdCode::err_unsupported, 0, _active->_tx_queue_n, c.u.layer.dst_hash, lp };  // self-layer = misconfig
                // Synchronous: queued (+ ctr to correlate) / err_no_gateway / err_too_large. NO orphan push.
                uint16_t ctr = 0;
                const CmdCode code = originate_layer_path(c.u.layer.dst_hash, c.u.layer.hops, hc, c.body, c.body_len, c.u.layer.flags, ctr);
                return CmdResult{ code, ctr, _active->_tx_queue_n, c.u.layer.dst_hash, lp };
            }
            // hop_count == 0: park-first (§5 / user 2026-06-13): resolve the dst's (node_id, target_layer) via an H
            // query; the drain decides same-layer-vs-cross-layer from the answer's target_layer (layer-in-id_bind cache
            // deferred). The target layer is resolved later, so layer_path is unknown here (0) — the app still has dst_hash.
            park_send_layer(c.u.layer.dst_hash, c.body, c.body_len, c.u.layer.flags);
            emit_hash_query(c.u.layer.dst_hash, /*hard=*/false);
            return CmdResult{ CmdCode::queued, 0, _active->_tx_queue_n, c.u.layer.dst_hash, /*layer_path*/ 0 };
        }
        default:
            return CmdResult{ CmdCode::err_unsupported, 0, _active->_tx_queue_n };
    }
}

void Node::enqueue_push(const Push& p) {
    if (_push_count >= protocol::cap_push_ring) {        // full -> drop-oldest (MeshCore offline queue)
        _push_head = static_cast<uint8_t>((_push_head + 1) % protocol::cap_push_ring);
        --_push_count;
    }
    const uint8_t tail = static_cast<uint8_t>((_push_head + _push_count) % protocol::cap_push_ring);
    _push_ring[tail] = p;
    ++_push_count;
}

bool Node::next_push(Push& out) {
    if (_push_count == 0) return false;
    out = _push_ring[_push_head];
    _push_head = static_cast<uint8_t>((_push_head + 1) % protocol::cap_push_ring);
    --_push_count;
    return true;
}

// ---- callbacks deferred to later R-iterations -------------------------------
// R4.5b: the sim's LBT/half-duplex safety-net fires this when a handed TX hits a busy channel (the firmware
// LBT defers the INITIATING TXs first, so this catches the residual + the non-LBT responses). info.tag is the
// frame-type the firmware tagged the TX with. RTS -> the already-armed rts_timeout re-RTSes (we must NOT clear
// awaiting_cts here — see below); DATA -> clear awaiting_ack + cancel the ack-timeout; then re-issue the stashed
// retry-eligible frame (CTS/DATA/ACK/NACK) up to TX_DEFER_MAX_RETRIES. Lua dv:12081-12215. NEVER fires in the gates
// (lbt_enabled=false + healthy duty) -> inert.
void Node::on_radio_busy(const BusyInfo& info) {
    const FrameTag tag = static_cast<FrameTag>(info.tag);
    MR_EMIT("radio_busy",EF_I("reason",info.reason),EF_I("busy_until_ms",info.busy_until_ms));
   
    if (tag == FrameTag::rts && _active->_pending_tx) {                      // RTS blocked: rts_timeout retries (dv:12089)
        // PORT DIVERGENCE (deliberate): Lua dv:12091 clears awaiting_cts here, but Lua's rts_timeout_fire does NOT
        // gate on it (it captures ctr_lo in the timer closure). OUR rts_timeout_fire uses awaiting_cts AS the
        // staleness key (the fixed timer id can't carry ctr_lo), so clearing it makes the already-armed timeout bail
        // -> the blocked RTS would never retry (carol stranded on r7_lbt_busy_diff). The RTS never hit the air, so
        // the node legitimately still awaits a CTS that won't come; leaving awaiting_cts=true lets the armed
        // rts_timeout fire + re-RTS, matching Lua's NET behaviour. Every other awaiting_cts=false transition cancels
        // kRtsTimeoutTimerId first (handle_cts:173, handle_nack:389), so the guard stays sound for those paths.
        MR_EMIT("rts_tx_blocked",EF_I("next",_active->_pending_tx->next),EF_I("ctr",_active->_pending_tx->ctr));
    }
    if (tag == FrameTag::data && _active->_pending_tx) {                     // DATA blocked: stash retry re-issues (dv:12109)
        _active->_pending_tx->awaiting_ack = false;
        _hal.cancel(kAckTimeoutTimerId);
        MR_EMIT("data_tx_blocked",EF_I("next",_active->_pending_tx->next),EF_I("ctr",_active->_pending_tx->ctr));
    }
    const int slot = retry_slot_of(tag);
    if (slot < 0) return;                                          // RTS/beacon: not stash-retried
    TxStashSlot& s = _tx_stash[slot];
    if (!s.valid) return;                                          // stash cleared by a newer same-tag TX
    if (s.retries_left == 0) {                                     // exhausted -> give up (dv:12190)
        MR_TELEMETRY(
            EventField f[] = { { .key = "tag", .type = EventField::T::i64, .i = info.tag } };
            _hal.emit("tx_giveup", f, 1); );
        s.valid = false;
        // SHARED-BUG FIX (#1, both engines): a DATA giveup STRANDS the flight — the DATA branch above cleared
        // awaiting_ack + cancelled the ack-timeout (and rts_timeout is moot), so _active->_pending_tx would sit forever with
        // no recovery timer and become_free() is blocked behind it -> the whole TX queue stalls. Release the flight
        // (mirror the DATA-M giveup, dv:12151) so the queue drains. Only DATA: a CTS/ACK/NACK giveup is a
        // receiver-side response whose pending_rx is freed by pending_rx_expiry; _active->_pending_tx may be unrelated.
        if (tag == FrameTag::data && _active->_pending_tx && _active->_pending_tx->ctr_lo == s.ctr_lo) {
            _active->_pending_tx.reset();
            become_free();
        }
        return;
    }
    --s.retries_left;
    s.reissue_pending = true;                                         // a busy re-issue timer is now armed (gates the gateway layer swap)
    const uint64_t now  = _hal.now();
    const uint64_t wait = (info.busy_until_ms > now) ? (info.busy_until_ms - now) : 0;
    const uint32_t delay = static_cast<uint32_t>(wait) + 2 +                                  // +2 guard (dv:12204)
                           static_cast<uint32_t>(_hal.rand_range(0, static_cast<int>(_lbt_backoff_ms) + 1));   // DRAW
    (void)_hal.after(delay, kRadioBusyRetryTimerId + slot);
}
// SX1262 PreambleDetected IRQ: the channel is busy with someone at our SF NOW, even if the packet
// won't decode. Feeds the throttle's channel-busy witness so beacon_fire's quiet check sees real
// activity, not the decode-success-biased view (dv:12219-12232). Pure timestamp, no rand.
void Node::on_preamble_detected(uint64_t time_ms)  { _last_rx_routing_sf_ms = time_ms; }

}  // namespace meshroute
