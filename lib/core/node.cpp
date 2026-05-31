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

#include "wire.h"

namespace meshroute {

// ---- construction & lifecycle ----------------------------------------------

Node::Node(Hal& hal, uint8_t node_id, uint32_t key_hash32, const char* name)
    : _hal(hal), _node_id(node_id), _key_hash32(key_hash32) {
    (void)name;  // sim-debug only; the node identifies by node_id / key_hash32
}

void Node::on_init(const NodeConfig& cfg) {
    _cfg = cfg;
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

    // Discovery window: boot in fast-cadence / full-page mode until we have heard
    // enough of the mesh or a bounded timeout expires (dv_dual_sf.lua:8399-8401).
    _discovery_started_ms   = _hal.now();
    _discovery_mode         = (protocol::discovery_ms > 0);
    _discovery_until_ms     = _discovery_started_ms + protocol::discovery_ms;
    _discovery_bcn_rx_count = 0;

    // Arm the first beacon spread across the (phase-dependent) period to avoid a
    // mass-boot burst (dv_dual_sf.lua:9027-9035).
    const int first_period = static_cast<int>(in_discovery() ? protocol::discovery_beacon_period_ms
                                                             : _cfg.beacon_period_ms);
    (void)_hal.after(static_cast<uint32_t>(_hal.rand_range(0, first_period)), kBeaconTimerId);
    // Periodic route-aging sweep (dv_dual_sf.lua:9080-9086).
    (void)_hal.after(_cfg.rt_aging_check_period_ms, kAgingTimerId);
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
    case kBeaconJitterTimerId: deferred_beacon_jitter_fire(); break;   // R4.3 post-jitter re-check + emit
    case kAgingTimerId:
        age_out_stale_routes();
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
    case kCascadeRequeueTimerId:  become_free();           break;   // backoff elapsed -> drain the requeued flight
    case kNackWaitTimerId:                                          // BUSY_RX wait elapsed -> re-RTS SAME hop
        if (_nack_wait_pending) {
            _nack_wait_pending = false;
            if (_pending_tx && _pending_tx->ctr_lo == _nack_wait_ctr_lo) tx_rts_retry();
        }
        break;
    default:
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
        default: break;                                              // rest ignored
    }
}

// ---- the typed command seam (the app<->firmware entrypoint) -----------------

CmdResult Node::on_command(const Command& c) {
    switch (c.kind) {
        case CmdKind::send: {
            const uint16_t ctr = do_send(c.u.send.dst_id, c.body, c.body_len, c.u.send.flags);
            return CmdResult{ CmdCode::queued, ctr, _tx_queue_n };
        }
        case CmdKind::send_layer:    // cross-layer  -> R7
        case CmdKind::send_channel:  // channel      -> R5
        case CmdKind::join:          // address-assign -> later
        default:
            return CmdResult{ CmdCode::err_unsupported, 0, _tx_queue_n };
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
void Node::on_radio_busy(const BusyInfo& info)     { (void)info; }       // R4 (LBT defer)
// SX1262 PreambleDetected IRQ: the channel is busy with someone at our SF NOW, even if the packet
// won't decode. Feeds the throttle's channel-busy witness so beacon_fire's quiet check sees real
// activity, not the decode-success-biased view (dv:12219-12232). Pure timestamp, no rand.
void Node::on_preamble_detected(uint64_t time_ms)  { _last_rx_routing_sf_ms = time_ms; }

}  // namespace meshroute
