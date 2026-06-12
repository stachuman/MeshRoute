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
        if (a.window_ms && b.window_ms && a.window_ms + b.window_ms > a.window_period_ms) return false;  // explicit windows overlap
    }

    _cfg = cfg;
    // Slice 0: a single-layer node mirrors its legacy scalars into layers[0] (backward-compat until Slice 2a
    // migrates the readers). A gateway (n_layers==2) supplies layers[0..1] explicitly (validated above).
    if (_cfg.n_layers <= 1) {
        _cfg.n_layers = 1;
        _cfg.layers[0].layer_id          = _cfg.leaf_id;          // single-layer: layer_id == leaf_id (may be 0 for R1)
        _cfg.layers[0].routing_sf        = _cfg.routing_sf;
        _cfg.layers[0].allowed_sf_bitmap = _cfg.allowed_sf_bitmap;
        _cfg.layers[0].beacon_period_ms  = _cfg.beacon_period_ms;
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
                return CmdResult{ CmdCode::queued, ctr, _active->_tx_queue_n };
            }
            const uint16_t ctr = do_send(c.u.send.dst_id, c.body, c.body_len, c.u.send.flags);
            return CmdResult{ CmdCode::queued, ctr, _active->_tx_queue_n };
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
        case CmdKind::send_layer:    // cross-layer  -> R7
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
