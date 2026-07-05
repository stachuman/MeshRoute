// MeshRoute — lib/core/node_mac_rx.cpp  (R3/R4 MAC data plane — RX frame handlers)
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// The receive side of the RTS-CTS-DATA-ACK-NACK handshake: what we do when each
// frame type arrives. Includes the R4 anti-spam originator drop + budget-aware
// NACK (handle_rts), the §7.6 hop-budget enforcement + origin dedup + budget-hint
// ACK (handle_data), the post-ACK deliver/forward, and the sender's NACK reactions
// (LOOP_DUP cascade / BUSY_RX wait-or-requeue / HOP_BUDGET rt-bump / BUDGET blind).
// The TX/send path is in node_mac.cpp; the duty/anti-spam metric helpers are in
// node_budget.cpp. Behaviour mirrors dv_dual_sf.lua. Part of the Node class (node.h).
#include "node.h"

#include "frame_codec.h"
#include "airtime.h"

#include <span>

namespace MESHROUTE_NS {

// 2-bit ACK SNR bucket (dv_dual_sf.lua:842; centers -16/-8/+4) — NOT the 4-bit one.
static uint8_t bucket_of_snr_2b(int snr_q4) {
    if (snr_q4 < -192) return 0;        // < -12 dB
    if (snr_q4 <  -64) return 1;        // < -4 dB
    return 2;
}

void Node::handle_rts(const uint8_t* bytes, size_t len, const RxMeta& meta) {
    auto pr = parse_rts(std::span<const uint8_t>(bytes, len));
    if (!pr) return;
    const rts_out& r = *pr;
    if (r.leaf_id != _cfg.leaf_id) return;
    // R4.4 anti-spam: track this RTS in the sender's window even when it's NOT addressed to us (we
    // overhear routing-SF broadcasts) so all 1st-hop neighbours accumulate evidence. Gateway cross-layer
    // relays (RTS_FLAG_RELAY) are exempt — not a 1st-hop origination (dv:9709-9712). Keyed on the decoded
    // RTS src (frame-derived, metal-correct), NOT meta.src_hint (the sim PHY oracle, -1 on hardware).
    // M_BROADCAST RTS (a channel-gossip re-broadcast) is exempt too — a holder relaying a channel msg
    // is not a DM originator; counting it would DM-throttle honest gossipers (Lua dv:9709 `elseif
    // r.m_broadcast`). The become_free self-cap already exempts M_BROADCAST (Inc 4); this is the
    // RTS-observation half. Draw-free + inert until M_BROADCAST RTS flows (Phase 2 channel responder).
    if (!(r.rts_flags & RTS_FLAG_RELAY) && !r.m_broadcast)
        track_originator_observation(r.src, /*kind=rts*/0, r.ctr_lo,
                                     static_cast<uint32_t>(airtime_routing_ms(static_cast<int>(len))));
    // Learn the RTS sender as a 1-hop neighbour — any RTS, overheard or addressed (Lua learn_rx_source).
    if (learn_direct_neighbor(r.src, protocol::db_to_q4(meta.snr_db), false)) schedule_triggered_beacon();
    // ② implicit-ACK from an overheard forward-RTS (Lua dv:9863-9893): if we have a flight in progress and overhear
    // OUR next-hop forwarding the SAME DATA onward (its relay RTS), the hop decoded -> cancel our pending timeout
    // instead of waiting out the ACK timer + firing a redundant retry that collides with its downstream DATA. Match
    // next/dst/ctr_lo (strong) + payload_len (disambiguates a 4-bit ctr_lo wrap). payload_len is the end-to-end
    // inner+MAC length (forwarded unchanged across hops) so it equals what our pending DATA implies (the same
    // rin.payload_len = inner_len + data_mac_len(flags) we packed at node_mac.cpp:558).
    if (_active->_pending_tx
        && r.src     == _active->_pending_tx->next
        && r.dst     == _active->_pending_tx->dst
        && r.ctr_lo  == _active->_pending_tx->ctr_lo
        && r.payload_len == static_cast<uint8_t>(_active->_pending_tx->inner_len + data_mac_len(_active->_pending_tx->flags))) {
        _hal.cancel(kRtsTimeoutTimerId);
        _hal.cancel(kAckTimeoutTimerId);
        _hal.cancel(kRetryBackoffTimerId);                 // parity with handle_ack: drop a stale retry armed by a just-fired timeout
        MR_TELEMETRY(
            EventField f[] = { { .key = "from",         .type = EventField::T::i64, .i = r.src },
                               { .key = "dst",          .type = EventField::T::i64, .i = _active->_pending_tx->dst },
                               { .key = "ctr_lo",       .type = EventField::T::i64, .i = r.ctr_lo },
                               { .key = "forward_next", .type = EventField::T::i64, .i = r.next } };
            _hal.emit("implicit_ack_from_forward", f, 4); );
        _active->_pending_tx.reset();
        become_free();
        return;
    }
    // No data SF configured (empty sf_list) -> this node is data-incapable: it can't pick a DATA SF, so it does
    // NOT CTS / retune / arm NAV (no silent fallback). The sender's DM just fails — fail loud. Control plane
    // (neighbour-learn above, beacons, routing) still runs; only data participation is refused.
    if (_cfg.allowed_sf_bitmap == 0) return;
    // ROADMAP §3: an M_BROADCAST RTS is a fire-and-forget channel re-broadcast (no CTS). ANY node that hears
    // it and LACKS the msg (by the id low-16) retunes RX to the advertised SF to catch the DATA-M — not just
    // the addressed puller. The retune-back timer restores routing_sf. Holders + gateways skip. (dv:2081/9940.)
    if (r.m_broadcast) {
        // Mid-DATA-reception: don't abandon the in-flight RX to chase a channel overhear. SF-gating keeps us off
        // the control SF while _active->_pending_rx (so normally unreachable), but guard defensively — retuning here would
        // clobber the awaited DATA, and a fresh flood-state created without a resolving retune would leak the slot.
        // The flood/M-broadcast is best-effort + repair-backstopped, so skipping it while busy is safe.
        if (_active->_pending_rx) return;
        if (r.flood) {                                       // FLOOD RTS-M (§4.2): dedup/merge/create state, then catch the DATA-M
            // COUPLE create->resolve (§8 note): only a participant ALLOCS a flood-state — else the state is
            // created but the retune that resolves it is skipped and nothing ever frees it (the gateway-leak
            // bug). Gate the whole flood handling on the SAME condition as the retune. §7 CONSUMER half: a
            // gateway+owner participates (catches the DATA-M for its owner); a pure bridge (gateway_only) +
            // a data-incapable node (no data SF) stay out.
            if (!(_cfg.is_gateway && _cfg.gateway_only) && _cfg.n_layers != 2 && _cfg.allowed_sf_bitmap != 0) {   // Principle 11: a dual-layer gateway never overhears channel floods
                auto fbm = rts_flood_bitmap(std::span<const uint8_t>(bytes, len), r);
                if (fbm.size() == 32) {
                    const int16_t snr_q4 = protocol::db_to_q4(meta.snr_db);
                    const bool fresh = handle_flood_rts(r, fbm.data(), snr_q4);
                    if (fresh) {                              // §4.2 step 3: retune to catch the DATA-M for a FRESH state
                        const uint8_t data_sf = select_data_sf(r.sf_index, snr_q4);
                        _hal.set_rx_sf(data_sf);
                        // The data-SF frame is the lean M frame: payload_len carries its BODY length, +M_FRAME_HDR_LEN
                        // (7) = the full on-air M frame (was +13 = the old DATA-M header). Sizing it short retunes
                        // back before the M frame's RX_DONE -> drop_sf_mismatch. +30 ideal margin + the metal slop.
                        const uint32_t back = protocol::cts_to_data_gap_ms
                            + airtime_ms(data_sf, active_bw_hz(), active_cr(), protocol::preamble_sym,
                                         static_cast<uint16_t>(r.payload_len + M_FRAME_HDR_LEN)) + 30 + _hal.rx_window_slop_ms(data_sf);
                        (void)_hal.after(back, kOverhearRetuneTimerId);
                        MR_EMIT("channel_overhear_armed", EF_I("sender", r.src), EF_I("chosen_data_sf", data_sf), EF_B("flood", true));
                        // Part B YIELD (spec 2026-06-28): we retuned to grab a NEW flood while awaiting our CTS -> our
                        // next-hop likely retuned for it too, so the CTS won't arrive until the flood clears. Push our
                        // CTS-timeout past it (no retry burned) -> catch the channel msg AND keep the DM retry, instead
                        // of today's "miss the CTS on the wrong SF -> timeout -> burn a retry" with the flood caught anyway.
                        if (protocol::flood_yield_grab_enable && _active->_pending_tx && _active->_pending_tx->awaiting_cts)
                            reserve_yield(nav_duration_cts(data_sf, static_cast<uint8_t>(protocol::reserve_est_payload_bytes)));
                    }
                }
            }
            return;                                          // FLOOD RTS never CTSes
        }
        if (!(_cfg.is_gateway && _cfg.gateway_only) && _cfg.n_layers != 2 && !channel_have_id_lo16(r.m_payload_id_lo16)) {   // §7 consumer / Principle 11: a dual-layer gateway never overhears a channel pull-response
            const uint8_t data_sf = select_data_sf(r.sf_index, protocol::db_to_q4(meta.snr_db));
            _hal.set_rx_sf(data_sf);
            // Stay on the data SF until the M frame lands: gap (RTS->DATA) + the FULL M-frame airtime
            // (r.payload_len carries the BODY length; +M_FRAME_HDR_LEN (7) covers the M header) + margin.
            // Sizing it short retunes back ~one header's airtime too early -> the M frame is dropped
            // (drop_sf_mismatch). The +30 is the sim's ideal margin; rx_window_slop_ms adds the REAL metal
            // RX_DONE/SPI turnaround (ZERO on the sim; the same slop start_pending_rx_expiry carries).
            const uint32_t back = protocol::cts_to_data_gap_ms
                + airtime_ms(data_sf, active_bw_hz(), active_cr(), protocol::preamble_sym,
                             static_cast<uint16_t>(r.payload_len + M_FRAME_HDR_LEN)) + 30 + _hal.rx_window_slop_ms(data_sf);
            (void)_hal.after(back, kOverhearRetuneTimerId);
            MR_TELEMETRY(
                EventField f[] = { { .key = "id_lo16",        .type = EventField::T::i64,     .i = r.m_payload_id_lo16 },
                                   { .key = "sender",         .type = EventField::T::i64,     .i = r.src },
                                   { .key = "target",         .type = EventField::T::i64,     .i = r.next },
                                   { .key = "chosen_data_sf", .type = EventField::T::i64,     .i = data_sf },          // advertised SF we retuned to (t69)
                                   { .key = "guard_ms",       .type = EventField::T::i64,     .i = static_cast<int64_t>(back) },
                                   { .key = "addressed",      .type = EventField::T::boolean, .b = (r.next == _node_id) } };
                _hal.emit("channel_overhear_armed", f, 6); );
        }
        return;                                          // M_BROADCAST RTS never CTSes
    }
    if (r.next != _node_id) {                             // not addressed to us as next-hop = overheard
        // NAV (virtual carrier sense): an overheard UNICAST RTS reserves the medium for the rest of the
        // exchange (CTS+DATA+ACK) — M_BROADCAST already returned above, so this is unicast. Defer own
        // unsolicited TX until then (tx_initiating/tx_flood) so we don't step on the CTS in the silent gap.
        if (_cfg.nav_enabled) {
            const uint8_t nav_sf = (r.sf_index <= 2)
                ? select_data_sf(r.sf_index, protocol::db_to_q4(meta.snr_db))   // pinned singleton -> the exact data SF
                : max_data_sf();                                                // ANY(3) -> conservative (the receiver picks)
            nav_arm(nav_duration_rts(nav_sf, r.payload_len));
        }
        // Part A YIELD (spec 2026-06-28): the overheard RTS TARGETS our next-hop -> it's about to be occupied -> our
        // CTS/ACK can't come. Push our pending timeout past the reserve (max-SF est, LBT-backstopped), no retry burned.
        if (protocol::reserve_yield_enable && _active->_pending_tx
            && (_active->_pending_tx->awaiting_cts || _active->_pending_tx->awaiting_ack)
            && r.next == _active->_pending_tx->next)
            reserve_yield(nav_duration_rts(max_data_sf(), static_cast<uint8_t>(protocol::reserve_est_payload_bytes)));
        return;
    }
    // NAV: virtually busy under someone else's reservation -> (optionally) ignore this (new) addressed RTS;
    // the requester is a hidden node that didn't hear the reservation and will time out + retry. Tunable
    // (nav_ignore_rts): dropping it protects the reservation but causes the requester to cascade/give up.
    if (_cfg.nav_enabled && _cfg.nav_ignore_rts && _hal.now() < _nav_until_ms) return;
    MR_TELEMETRY(
        EventField f[] = { { .key = "from", .type = EventField::T::i64, .i = r.src },
                           { .key = "dst",  .type = EventField::T::i64, .i = r.dst } };
        _hal.emit("rts_rx", f, 2); );

    // last_acked dedup: a retried RTS after we already delivered -> CTS already_received, no re-deliver.
    const uint32_t lakey = (uint32_t(r.src) << 24) | (uint32_t(r.dst) << 16) |
                           (uint32_t(r.ctr_lo) << 8) | r.payload_len;
    auto la = _active->_last_acked_from.find(lakey);
    if (la != _active->_last_acked_from.end() && (_hal.now() - la->second.t_ms) < protocol::last_acked_ttl_ms) {
        // Fresh within the 10s TTL (dv_dual_sf.lua:9861) — the TTL gate is what stops a
        // stale 4-bit ctr_lo alias from false-positiving on slow sustained traffic.
        cts_in cin{}; cin.chosen_data_sf = la->second.chosen_data_sf;
        cin.already_received = true; cin.tx_id = _node_id; cin.rx_id = r.src;
        cin.payload_len = _cfg.nav_enabled ? r.payload_len : 0;   // NAV: size the overhearer's DATA reservation
        uint8_t cbuf[4]; const size_t cl = pack_cts(cin, std::span<uint8_t>(cbuf, sizeof cbuf));
        tx_with_retry(cbuf, cl, static_cast<int16_t>(_cfg.routing_sf), FrameTag::cts);   // R4.5b
        MR_TELEMETRY(
            EventField f[] = { { .key = "to", .type = EventField::T::i64, .i = r.src },
                               { .key = "dup", .type = EventField::T::boolean, .b = true } };
            _hal.emit("cts_tx", f, 2); );
        return;
    }
    // A retried RTS for the SAME flight while we still await its DATA -> re-CTS + restart
    // the expiry (dv_dual_sf.lua:218 CTS-dup) so the sender's retry gets a fresh CTS.
    if (_active->_pending_rx && _active->_pending_rx->from == r.src && _active->_pending_rx->dst == r.dst &&
        _active->_pending_rx->ctr_lo == r.ctr_lo) {
        cts_in cin{}; cin.chosen_data_sf = _active->_pending_rx->chosen_data_sf;
        cin.already_received = false; cin.tx_id = _node_id; cin.rx_id = r.src;
        cin.payload_len = _cfg.nav_enabled ? r.payload_len : 0;   // NAV: size the overhearer's DATA reservation
        uint8_t cbuf[4]; const size_t cl = pack_cts(cin, std::span<uint8_t>(cbuf, sizeof cbuf));
        tx_with_retry(cbuf, cl, static_cast<int16_t>(_cfg.routing_sf), FrameTag::cts);   // R4.5b
        MR_TELEMETRY(
            EventField f[] = { { .key = "to", .type = EventField::T::i64, .i = r.src },
                               { .key = "dup", .type = EventField::T::boolean, .b = true } };
            _hal.emit("cts_tx", f, 2); );
        start_pending_rx_expiry(_active->_pending_rx->payload_len);
        return;
    }
    // Busy with a DIFFERENT flight. If we hold a pending_rx (receiving someone else's
    // DATA), NACK the sender with how-long-busy so it waits/requeues instead of
    // grinding rts_timeout (dv:9934). If we hold a pending_tx (sending our own), STAY
    // SILENT (dv:9962 — the busy_for estimate lied for ACK-loss-stuck nodes).
    if (_active->_pending_rx) {
        const uint64_t now = _hal.now();
        uint64_t busy_for = (_active->_pending_rx->expiry_ms > now) ? (_active->_pending_rx->expiry_ms - now) : 0;
        if (busy_for > 65535) busy_for = 65535;
        const uint32_t q = (static_cast<uint32_t>(busy_for) + protocol::nack_busy_quantum_ms - 1)
                           / protocol::nack_busy_quantum_ms;                    // ceil
        nack_in nin{}; nin.reason = protocol::nack_reason_busy_rx; nin.ctr_lo = r.ctr_lo;
        nin.payload = static_cast<uint8_t>(q > 255 ? 255 : q); nin.to = r.src;
        uint8_t nbuf[4]; const size_t nl = pack_nack(nin, std::span<uint8_t>(nbuf, 4));
        MR_TELEMETRY(
            EventField f[] = { { .key = "to",      .type = EventField::T::i64, .i = r.src },
                               { .key = "reason",  .type = EventField::T::i64, .i = protocol::nack_reason_busy_rx },
                               { .key = "busy_ms", .type = EventField::T::i64, .i = static_cast<int64_t>(busy_for) } };
            _hal.emit("nack_tx", f, 3); );
        tx_initiating(nbuf, nl, static_cast<int16_t>(_cfg.routing_sf), LbtKind::nack, 0);   // R4.5 LBT (handle_rts NACK, dv:9953)
        return;
    }
    if (_active->_pending_tx) {                                   // sending our own -> silent (no NACK)
        MR_TELEMETRY(
            EventField f[] = { { .key = "from", .type = EventField::T::i64, .i = r.src } };
            _hal.emit("rts_drop_pending_tx", f, 1); );
        return;
    }

    // R4.4 anti-spam DROP (Inc 1+2): if this sender's overheard airtime over the window exceeds
    // originator_airtime_share of our duty budget, silently drop the RTS — no CTS, no NACK. Relay
    // forwards are exempt. Keyed on the decoded RTS src (frame-derived, metal-correct — NOT src_hint,
    // which is -1 on hardware). The airtime BACKSTOP is gated on a real budget: with duty disabled
    // (budget 0) there is no SHARE to enforce, so skip it (matches compute_budget_tier / check_duty_cycle).
    // R-C apparent-origination COUNT clause REMOVED (Inc 1): a missed CTS makes a forwarder look like an
    // originator -> 168 false-drops on s18; the airtime backstop is the robust half (honesty- and
    // CTS-loss-independent). app_orig/rts/cts kept as info-only emit fields.
    // e2e-ack backstop exemption (2026-07-02): an RTS marked RTS_FLAG_E2E_ACK (its pending DATA is a DATA_TYPE_E2E_ACK)
    // skips the DROP — an ack must never be throttled (a throttled ack -> the sender never learns delivery -> re-send ->
    // MORE traffic). It is still OBSERVED at :40 (honest airtime metric, no bypass). Anti-spoof: a sender caught faking
    // the bit (DATA-time verify below) is flagged (e2e_ack_spoofer_flagged) and its exemption revoked for a whole window.
    // The hard duty-cycle limit still binds the sender's own ack originations (the un-spoofable ceiling).
    const bool e2e_ack_exempt = (r.rts_flags & RTS_FLAG_E2E_ACK) && !e2e_ack_spoofer_flagged(r.src);
    if (!(r.rts_flags & RTS_FLAG_RELAY) && !e2e_ack_exempt) {
        int app_orig; uint32_t total_air; uint8_t rts_n, cts_n;
        compute_originator_metric(r.src, app_orig, total_air, rts_n, cts_n);
        const uint32_t airtime_cap = static_cast<uint32_t>(
            static_cast<double>(protocol::originator_airtime_share) * _duty_cycle_budget_ms);   // floor
        const bool over_airtime = (_duty_cycle_budget_ms > 0) && (total_air > airtime_cap);
        // WARN band (Inc 2): airtime in [warn_fraction x cap, cap) -> flag, don't drop. Inc 3 carries this
        // to the sender in the ACK warn bit so an honest node backs off before the hard cap. Emitted here
        // for calibration (how close legit traffic comes to the cap).
        const uint32_t airtime_warn = static_cast<uint32_t>(
            static_cast<double>(protocol::originator_airtime_warn_fraction) * airtime_cap);
        if (_duty_cycle_budget_ms > 0 && !over_airtime && total_air > airtime_warn) {
            MR_TELEMETRY(
                EventField wf[] = { { .key = "from",      .type = EventField::T::i64, .i = r.src },
                                    { .key = "ctr_lo",    .type = EventField::T::i64, .i = r.ctr_lo },
                                    { .key = "airtime_ms", .type = EventField::T::i64, .i = static_cast<int64_t>(total_air) },
                                    { .key = "warn_airtime_ms", .type = EventField::T::i64, .i = static_cast<int64_t>(airtime_warn) },
                                    { .key = "threshold_airtime_ms", .type = EventField::T::i64, .i = static_cast<int64_t>(airtime_cap) },
                                    { .key = "window_ms", .type = EventField::T::i64, .i = protocol::originator_window_ms } };
                _hal.emit("rts_originator_airtime_warn", wf, 6); );
        }
        if (over_airtime) {
            MR_TELEMETRY(
                EventField f[] = { { .key = "from",      .type = EventField::T::i64, .i = r.src },
                                   { .key = "ctr_lo",    .type = EventField::T::i64, .i = r.ctr_lo },
                                   { .key = "apparent_origination", .type = EventField::T::i64, .i = app_orig },
                                   { .key = "airtime_ms", .type = EventField::T::i64, .i = static_cast<int64_t>(total_air) },
                                   { .key = "rts_count", .type = EventField::T::i64, .i = rts_n },
                                   { .key = "cts_count", .type = EventField::T::i64, .i = cts_n },
                                   { .key = "threshold_count",      .type = EventField::T::i64, .i = _cfg.originator_max_per_window },
                                   { .key = "threshold_airtime_ms", .type = EventField::T::i64, .i = static_cast<int64_t>(airtime_cap) },
                                   { .key = "window_ms",            .type = EventField::T::i64, .i = protocol::originator_window_ms } };
                _hal.emit("rts_drop_originator_throttle", f, 9); );
            return;                                       // silent drop (no CTS, no NACK)
        }
    }
    // R4.1 budget-aware NACK (Lua dv:10016-10044): if OUR duty budget is >=CRITICAL we likely
    // can't carry this flight to completion (CTS+DATA-RX are free but the ACK + any forward cost
    // budget), so refuse early with a BUDGET NACK -> the sender reroutes via the blind machinery
    // instead of a full RTS-CTS-DATA-ACK that stalls mid-cycle. We still pay the small NACK
    // airtime but save the CTS+ACK round-trip. STRAINED still CTSes.
    const BudgetTier my_tier = compute_budget_tier();
    if (my_tier >= BudgetTier::critical) {
        nack_in nin{}; nin.reason = protocol::nack_reason_budget; nin.ctr_lo = r.ctr_lo;
        nin.payload = static_cast<uint8_t>((static_cast<uint8_t>(my_tier) & 0x0f) << 4);   // tier HIGH nibble
        nin.to = r.src;
        uint8_t nbuf[4]; const size_t nl = pack_nack(nin, std::span<uint8_t>(nbuf, 4));
        MR_TELEMETRY(
            EventField f[] = { { .key = "to",     .type = EventField::T::i64, .i = r.src },
                               { .key = "reason", .type = EventField::T::i64, .i = protocol::nack_reason_budget },
                               { .key = "tier",   .type = EventField::T::i64, .i = static_cast<uint8_t>(my_tier) } };
            _hal.emit("nack_tx", f, 3); );
        tx_initiating(nbuf, nl, static_cast<int16_t>(_cfg.routing_sf), LbtKind::nack, 0);   // R4.5 LBT (handle_rts NACK, dv:10043)
        return;                                          // NO CTS, NO pending_rx
    }

    const uint8_t sf = select_data_sf(r.sf_index, protocol::db_to_q4(meta.snr_db));
    PendingRx prx{}; prx.from = r.src; prx.dst = r.dst; prx.ctr_lo = r.ctr_lo;
    prx.chosen_data_sf = sf; prx.payload_len = r.payload_len; prx.set_at_ms = _hal.now();
    prx.claimed_e2e_ack = (r.rts_flags & RTS_FLAG_E2E_ACK) != 0;   // carried to DATA-time for the anti-spoof verify
    _active->_pending_rx = prx;
    start_pending_rx_expiry(r.payload_len);
    cts_in cin{}; cin.chosen_data_sf = sf; cin.already_received = false; cin.tx_id = _node_id; cin.rx_id = r.src;
    cin.payload_len = _cfg.nav_enabled ? r.payload_len : 0;   // NAV: size the overhearer's DATA reservation
    uint8_t cbuf[4]; const size_t cl = pack_cts(cin, std::span<uint8_t>(cbuf, sizeof cbuf));
    tx_with_retry(cbuf, cl, static_cast<int16_t>(_cfg.routing_sf), FrameTag::cts);   // R4.5b: stash + tag the CTS
    MR_TELEMETRY(
        EventField f[] = { { .key = "to", .type = EventField::T::i64, .i = r.src },
                           { .key = "sf", .type = EventField::T::i64, .i = sf } };
        _hal.emit("cts_tx", f, 2); );
    _hal.set_rx_sf(sf);                                  // NOW retune RX to hear the DATA on the data SF
}

void Node::handle_cts(const uint8_t* bytes, size_t len, const RxMeta& meta) {
    auto pc = parse_cts(std::span<const uint8_t>(bytes, len));
    if (!pc) return;
    const cts_out& c = *pc;
    // R4.4 anti-spam: track this CTS in the CTS sender's (c.tx_id) window (overheard, addressed to us or
    // not). CTS is the forwarder fingerprint — a legit forwarder emits ~1 CTS per inbound flight (dv:10149).
    // Unconditional now: tx_id is on the wire (no PHY-sender god-view). Dedup key is rx_id (the cleared
    // requester), not the dropped ctr_lo. Timing uses Lua CTS_LEN=4, not the 3-B C++ wire.
    track_originator_observation(c.tx_id, /*kind=cts*/1, /*dedup_key=*/c.rx_id,
                                 static_cast<uint32_t>(airtime_routing_ms(4)));
    if (c.rx_id != _node_id) {                            // overheard CTS (not clearing us)
        // NAV: reserve the medium for the DATA+ACK this CTS just authorized (covers the hidden node near the
        // receiver that didn't hear the RTS). chosen_data_sf is exact; size assumed max (conservative).
        if (_cfg.nav_enabled) nav_arm(nav_duration_cts(c.chosen_data_sf, c.payload_len));
        // Part A YIELD (spec 2026-06-28): the CTS sender is OUR next-hop -> it just cleared someone else and is about
        // to receive their DATA -> busy, our CTS/ACK can't come. Push our pending timeout past the reserve (½-max est,
        // LBT-backstopped) instead of timing out blind + burning a retry during it.
        if (protocol::reserve_yield_enable && _active->_pending_tx
            && (_active->_pending_tx->awaiting_cts || _active->_pending_tx->awaiting_ack)
            && c.tx_id == _active->_pending_tx->next)
            reserve_yield(nav_duration_cts(c.chosen_data_sf, static_cast<uint8_t>(protocol::reserve_est_payload_bytes)));
        return;
    }
    if (!_active->_pending_tx || !_active->_pending_tx->awaiting_cts) return;   // ctr_lo flight-match dropped: rx_id==me + tx_id==next (below) pin the flight
    // Cascade disambiguation: the CTS now carries its sender (tx_id), so accept only the CTS from the
    // next-hop we RTS'd. Wire-backed (no PHY-sender god-view) — this is what distinguishes the primary
    // next-hop's CTS from an alt's when both answer the same RTS (cascade-to-alt). dv:10195.
    if (c.tx_id != _active->_pending_tx->next) return;
    // Learn the CTS sender (= our next-hop) as a 1-hop neighbour (Lua learn_rx_source / cts_frame).
    if (learn_direct_neighbor(c.tx_id, protocol::db_to_q4(meta.snr_db), false)) schedule_triggered_beacon();
    note_link_confirmed(c.tx_id);                        // bidi plane: a real CTS proves our next-hop hears us -> confirmed (clears any one_way + emits link_recover)
    _hal.cancel(kRtsTimeoutTimerId);                     // else it fires same-tick and burns a retry
    _hal.cancel(kRetryBackoffTimerId);                   // drop a stale retry armed by a just-fired rts_timeout
    _active->_pending_tx->awaiting_cts = false;
    _active->_pending_tx->chosen_data_sf = c.chosen_data_sf;
    MR_TELEMETRY(
        EventField f[] = { { .key = "from", .type = EventField::T::i64, .i = _active->_pending_tx->next },   // CTS is from our next-hop (src_hint=-1 on metal)
                           { .key = "sf",   .type = EventField::T::i64, .i = c.chosen_data_sf } };
        _hal.emit("cts_rx", f, 2); );
    if (c.already_received) { _active->_pending_tx.reset(); become_free(); return; }   // already delivered upstream
    (void)_hal.after(protocol::cts_to_data_gap_ms, kCtsToDataGapTimerId);     // fixed 5ms gap (NOT rand)
}

// 2026-06-09: a channel message is now the lean M frame (cmd 0xA), NOT a DATA+PAYLOAD_TYPE_M. The data SF
// frame that follows a FLOOD/M_BROADCAST RTS-M is this M frame; every node that retuned (the overhear ARM)
// ingests it promiscuously — but the STANDARD byte-0 leaf gate runs first, so a stray that punched into an
// adjacent leaf dies before buffering (the cross-leaf leak fix). No CTS/ACK/forward; the retune-back timer
// restores routing_sf. `from` keeps the DATA-M's src_hint-or-0xFF derivation (metal carries no PHY sender).
void Node::handle_channel_data(const uint8_t* bytes, size_t len, const RxMeta& meta) {
    auto pm = parse_m(std::span<const uint8_t>(bytes, len));
    if (!pm) return;
    if (pm->leaf_id != _cfg.leaf_id) return;                 // the leak gate — a foreign-leaf M frame dies here
    const uint8_t from = (meta.src_hint >= 0) ? static_cast<uint8_t>(meta.src_hint) : 0xFF;
    ingest_channel_m(*pm, from);
}

// Record this flight's (origin,dst,ctr) key -> expiry + prev-hop for the loop/retransmit dedup. Prune the
// expired entries first; then if still at the cap (all live) and the key is NEW, ROLL — evict the OLDEST
// (min-expiry = earliest recorded, least remaining loop-window) to make room, rather than refusing the new
// key. Re-recording an existing key just refreshes it (no eviction). Bounded by cap_seen_origins; no growth.
void Node::record_seen_origin(uint64_t sokey, uint8_t from, uint64_t now_ms) {
    for (auto it = _active->_seen_origins.begin(); it != _active->_seen_origins.end(); )
        { if (it->second <= now_ms) { _active->_seen_origin_from.erase(it->first); it = _active->_seen_origins.erase(it); } else ++it; }
    if (_active->_seen_origins.size() >= protocol::cap_seen_origins
        && _active->_seen_origins.find(sokey) == _active->_seen_origins.end()) {              // full of LIVE entries + a NEW key -> roll
        auto oldest = _active->_seen_origins.begin();
        for (auto it = _active->_seen_origins.begin(); it != _active->_seen_origins.end(); ++it)
            if (it->second < oldest->second) oldest = it;                   // min expiry = the earliest recorded
        _active->_seen_origin_from.erase(oldest->first);
        _active->_seen_origins.erase(oldest);
    }
    _active->_seen_origins[sokey]     = now_ms + protocol::seen_origin_ttl_ms;
    _active->_seen_origin_from[sokey] = from;
}

void Node::handle_data(const uint8_t* bytes, size_t len, const RxMeta& meta) {
    auto pd = parse_data(std::span<const uint8_t>(bytes, len));
    if (!pd) return;
    
    const data_out& d = *pd;
    if (d.next != _node_id) return;
    if (!_active->_pending_rx || _active->_pending_rx->ctr_lo != d.ctr_lo4) return;
    // e2e-ack backstop exemption ANTI-SPOOF verify (2026-07-02): the RTS claimed RTS_FLAG_E2E_ACK (so its DROP was
    // exempted at handle_rts), but the DATA that arrived is NOT a DATA_TYPE_E2E_ACK -> the sender lied to bypass the
    // backstop. Flag it: while flagged, its RTS_FLAG_E2E_ACK is ignored (the backstop re-applies). Keyed on the PHYSICAL
    // SENDER = _pending_rx->from (the cleartext RTS src, metal-correct), NOT the sealed inner origin. One free pass, revoked.
    if (_active->_pending_rx->claimed_e2e_ack && d.type != DATA_TYPE_E2E_ACK) {
        const uint8_t spoofer = _active->_pending_rx->from;
        MR_EMIT("e2e_ack_spoof", EF_I("from", spoofer), EF_I("type", d.type));
        if (PeerLiveness* s = peer_liveness_slot(spoofer, /*create=*/true))
            s->e2e_ack_spoof_until_ms = _hal.now() + protocol::e2e_ack_spoof_penalty_ms;
    }
    // Inc 2 anti-spam: record this inbound DATA's airtime in the sender's window — the dominant
    // airtime a sender imposes on us (RTS-only never approached the cap). Keyed on _active->_pending_rx->from
    // (== this hop's RTS src, so RTS+DATA accumulate in one entry; frame-derived, metal-correct) and
    // costed at the chosen data SF over the whole frame.
    track_originator_observation(_active->_pending_rx->from, /*kind=data*/2, d.ctr_lo4,
        airtime_ms(_active->_pending_rx->chosen_data_sf, active_bw_hz(), active_cr(),
                   protocol::preamble_sym, static_cast<uint16_t>(len)));
    int oa_app_; uint32_t orig_air; uint8_t oa_rts_, oa_cts_;   // sender's windowed airtime AFTER this DATA (calibration)
    compute_originator_metric(_active->_pending_rx->from, oa_app_, orig_air, oa_rts_, oa_cts_);
    (void)oa_app_; (void)oa_rts_; (void)oa_cts_;
    // The DATA's link sender = whoever we CTS'd (_active->_pending_rx->from, set in handle_rts).
    // src_hint is the SIM oracle (real LoRa carries no PHY source; the device sets -1),
    // so use it only when present, else fall back to our pending-RX contract — else
    // from=0xFF on metal -> the ACK + HOP_BUDGET/LOOP_DUP NACKs target node 255 and the
    // dedup/loop keys are corrupt, so the DM never completes.
    const uint8_t from = (meta.src_hint >= 0) ? static_cast<uint8_t>(meta.src_hint)
                                              : _active->_pending_rx->from;
    // Parse the inner up-front so data_rx carries the (origin, ctr) message key — telemetry
    // parity with the Lua data_rx (dv:10911), which the analysis tools key delivery on. origin
    // is also needed below (BEFORE the ACK) so HOP_BUDGET/LOOP_DUP can NACK instead of re-ACKing.
    auto inner = data_inner(std::span<const uint8_t>(bytes, len), d);
    auto ui = parse_unicast_inner(inner, d.flags);
    const uint8_t origin = ui ? ui->origin : from;
    MR_TELEMETRY(
        EventField f[] = { { .key = "origin", .type = EventField::T::i64, .i = origin },
                           { .key = "ctr",    .type = EventField::T::i64, .i = d.ctr },
                           { .key = "ctr_lo", .type = EventField::T::i64, .i = d.ctr_lo4 },
                           { .key = "from",   .type = EventField::T::i64, .i = from },
                           { .key = "dst",    .type = EventField::T::i64, .i = d.dst },
                           { .key = "orig_airtime_ms", .type = EventField::T::i64, .i = static_cast<int64_t>(orig_air) } };
        _hal.emit("data_rx", f, 6); );
    // Learn the DATA prev-hop as a 1-hop neighbour (Lua learn_rx_source / data_frame).
    if (learn_direct_neighbor(from, protocol::db_to_q4(meta.snr_db), false)) schedule_triggered_beacon();
    const uint8_t rx_sf = _active->_pending_rx->chosen_data_sf;
    const uint8_t pl    = _active->_pending_rx->payload_len;
    _hal.cancel(kPendingRxExpiryTimerId);
    _hal.set_rx_sf(_cfg.routing_sf);                     // receiver retunes back
    _active->_pending_rx.reset();
    // last_acked cache: a retried RTS gets CTS already_received=1 instead of re-delivery.
    const uint32_t lakey = (uint32_t(from) << 24) | (uint32_t(d.dst) << 16) |
                           (uint32_t(d.ctr_lo4) << 8) | pl;
    const uint64_t nowm = _hal.now();
    for (auto it = _active->_last_acked_from.begin(); it != _active->_last_acked_from.end(); )   // prune expired (10s TTL)
        { if ((nowm - it->second.t_ms) >= protocol::last_acked_ttl_ms) it = _active->_last_acked_from.erase(it); else ++it; }
    if (_active->_last_acked_from.size() < protocol::cap_seen_origins)                  // bounded (reuse the 256 cap)
        _active->_last_acked_from[lakey] = LastAcked{ rx_sf, nowm };
    // §1b sealed-sender dedup key — TYPE-NAMESPACED into one 64-bit space so PLAINTEXT and CRYPTED can NEVER alias.
    // PLAINTEXT = (origin<<24|dst<<16|ctr), naturally in [0,2^32) — same VALUE as before, just widened (s18 invariant).
    // CRYPTED = the FULL 8-B cleartext nonce-seed loaded LE, top bit forced => [2^63,2^64). The seed is globally unique
    // per message (a crypto invariant), preserved VERBATIM across forwards (so a loop via a different prev-hop still
    // matches), and — once §1c seals `origin` — the ONLY flight-id a relay can read. Forcing bit 63 costs one seed bit
    // (63 left => ~2^-47 birthday at the 256 cap) and makes the plaintext/CRYPTED disjointness a HARD invariant, not a
    // probability. Extract the seed HERE, before the sokey: PLAINTEXT data_nonce_seed() returns an EMPTY span
    // (frame_codec:717), so nseed stays zero and is never read on that path. origin is still read BEFORE the ACK so
    // HOP_BUDGET/LOOP_DUP can NACK instead of re-ACKing.
    uint8_t nseed[8] = {0};
    if (d.crypted) { auto sd = data_nonce_seed(std::span<const uint8_t>(bytes, len), d);
                     for (uint8_t i = 0; i < 8 && i < sd.size(); ++i) nseed[i] = sd[i]; }
    uint64_t seed_u64 = 0; for (int i = 0; i < 8; ++i) seed_u64 |= uint64_t(nseed[i]) << (8 * i);   // LE load (zero for plaintext)
    const uint64_t sokey = d.crypted
        ? (seed_u64 | (uint64_t(1) << 63))                                                          // CRYPTED namespace: >= 2^63
        : ((uint64_t(origin) << 24) | (uint64_t(d.dst) << 16) | d.ctr);                             // PLAINTEXT namespace: < 2^32
    // HOP_BUDGET enforcement FIRST (dv:10918-10964), BEFORE the dedup AND the ACK so the
    // NACK fires IN LIEU OF the ACK. A FORWARDER (d.dst != self) decrements the TTL; if the
    // decremented value went negative (the frame arrived with hops_remaining==0 at a
    // non-destination), the budget is exhausted -> NACK the sender (terminal) instead of
    // forwarding. The destination is exempt. Lua runs this check ABOVE the loop-dup dedup,
    // so a budget-exhausted frame ALWAYS HOP_BUDGET-NACKs (terminal + rt-bump self-heal)
    // regardless of dup status.
    const int     hb_new_remaining = static_cast<int>(d.hops_remaining) - 1;
    const uint8_t hb_new_committed = (d.committed_hops >= 7) ? 7
                                     : static_cast<uint8_t>(d.committed_hops + 1);
    if (d.dst != _node_id && hb_new_remaining < 0) {
        MR_TELEMETRY(
            EventField ef[] = { { .key = "origin", .type = EventField::T::i64, .i = origin },
                                { .key = "dst",    .type = EventField::T::i64, .i = d.dst },
                                { .key = "ctr",    .type = EventField::T::i64, .i = d.ctr } };
            _hal.emit("hop_budget_exceeded", ef, 3); );
        // Record (origin,dst,ctr) so a LATER non-exhausted arrival of the SAME flight via
        // a DIFFERENT prev-hop is caught as LOOP_DUP (not accepted+forwarded) — dv:10933-10940.
        record_seen_origin(sokey, from, nowm);   // prune + roll-evict-oldest-if-full + insert (see the def)
        nack_in nin{}; nin.reason = protocol::nack_reason_hop_budget; nin.ctr_lo = d.ctr_lo4;
        nin.payload = static_cast<uint8_t>((hb_new_committed & 0x0f) << 4);   // committed in the HIGH nibble
        nin.to = from;
        uint8_t nbuf[4]; const size_t nl = pack_nack(nin, std::span<uint8_t>(nbuf, 4));
        tx_with_retry(nbuf, nl, static_cast<int16_t>(_cfg.routing_sf), FrameTag::nack);   // R4.5b (HOP_BUDGET NACK)
        MR_TELEMETRY(
            EventField nf[] = { { .key = "to",     .type = EventField::T::i64, .i = from },
                                { .key = "reason", .type = EventField::T::i64, .i = protocol::nack_reason_hop_budget },
                                { .key = "ctr",    .type = EventField::T::i64, .i = d.ctr } };
            _hal.emit("nack_tx", nf, 3); );
        become_free();
        return;
    }
    // Origin-level dedup (dv:10966+), AFTER HOP_BUDGET. A same-prev-hop dup is normal
    // lost-ACK recovery (ACK-only below); a DIFFERENT prev-hop means a mesh loop -> NACK.
    auto so = _active->_seen_origins.find(sokey);
    const bool live_dup = (so != _active->_seen_origins.end() && so->second > nowm);
    if (live_dup) {
        auto sof = _active->_seen_origin_from.find(sokey);
        if (sof != _active->_seen_origin_from.end() && sof->second != from) {
            // LOOP_DUP: the SAME flight arrived via a DIFFERENT prev-hop (a mesh loop,
            // dv:10971). NACK the sender so it cascades to an alt, and do NOT ACK (the
            // ACK would clear its pending_tx early). prior_from = the first prev-hop.
            nack_in nin{}; nin.reason = protocol::nack_reason_loop_dup; nin.ctr_lo = d.ctr_lo4;
            nin.payload = sof->second; nin.to = from;
            uint8_t nbuf[4]; const size_t nl = pack_nack(nin, std::span<uint8_t>(nbuf, 4));
            tx_with_retry(nbuf, nl, static_cast<int16_t>(_cfg.routing_sf), FrameTag::nack);   // R4.5b (LOOP_DUP NACK)
            MR_TELEMETRY(
                EventField nf[] = { { .key = "to",     .type = EventField::T::i64, .i = from },
                                    { .key = "reason", .type = EventField::T::i64, .i = protocol::nack_reason_loop_dup },
                                    { .key = "ctr",    .type = EventField::T::i64, .i = d.ctr } };
                _hal.emit("nack_tx", nf, 3); );
            MR_TELEMETRY(
                EventField df[] = { { .key = "origin", .type = EventField::T::i64, .i = origin },
                                    { .key = "dst",    .type = EventField::T::i64, .i = d.dst },
                                    { .key = "ctr",    .type = EventField::T::i64, .i = d.ctr } };
                _hal.emit("dup_drop", df, 3); );
            become_free();
            return;
        }
    }
    // ACK on routing_sf (2-bit SNR bucket). R4.2: piggyback OUR budget tier, capped at CRITICAL (the
    // protocol caps the forward hint at CRITICAL per Lua dv:11054 — a node already >=CRITICAL refuses
    // the RTS with a BUDGET NACK and rarely ACKs; EXHAUSTED is the reverse-NACK's concern). So the
    // sender learns our congestion in the FORWARD direction. Fires for a fresh DATA and a same-prev-hop dup.
    const BudgetTier my_tier = compute_budget_tier();
    const uint8_t hint = (static_cast<uint8_t>(my_tier) > static_cast<uint8_t>(BudgetTier::critical))
                         ? static_cast<uint8_t>(BudgetTier::critical) : static_cast<uint8_t>(my_tier);
    ack_in ain{}; ain.ctr_lo = d.ctr_lo4; ain.budget_hint = hint;
    ain.snr_bucket = bucket_of_snr_2b(protocol::db_to_q4(meta.snr_db)); ain.to = from;
    // Inc 3: warn the sender (via the ACK warn bit) when its observed airtime is in the warn band — the
    // soft sender-side precursor to the hard drop. orig_air = this sender's windowed airtime (post-DATA,
    // computed above for the data_rx diagnostic). cap = share x budget; warn at warn_fraction x cap.
    const uint32_t airtime_cap_a = static_cast<uint32_t>(
        static_cast<double>(protocol::originator_airtime_share) * _duty_cycle_budget_ms);
    ain.warn = (_duty_cycle_budget_ms > 0) &&
               (orig_air > static_cast<uint32_t>(
                   static_cast<double>(protocol::originator_airtime_warn_fraction) * airtime_cap_a));
    uint8_t abuf[3]; const size_t al = pack_ack(ain, std::span<uint8_t>(abuf, 3));
    tx_with_retry(abuf, al, static_cast<int16_t>(_cfg.routing_sf), FrameTag::ack);   // R4.5b: stash + tag the ACK
    MR_TELEMETRY(
        EventField f[] = { { .key = "to",  .type = EventField::T::i64, .i = from },
                           { .key = "ctr", .type = EventField::T::i64, .i = d.ctr },
                           { .key = "airtime_warn", .type = EventField::T::i64, .i = ain.warn ? 1 : 0 } };
        _hal.emit("ack_tx", f, 3); );
    if (live_dup) { become_free(); return; }                                        // same prev-hop dup -> ACK only
    record_seen_origin(sokey, from, nowm);                                          // record + roll-evict-oldest if full
    // defer deliver/forward by the ACK airtime so it doesn't share a sim step with the ACK.
    _active->_post_ack = PostAck{};
    _active->_post_ack.pending = true; _active->_post_ack.is_forward = (d.dst != _node_id);
    _active->_post_ack.origin = origin; _active->_post_ack.dst = d.dst; _active->_post_ack.ctr_lo = d.ctr_lo4;
    _active->_post_ack.ctr = d.ctr; _active->_post_ack.flags = d.flags; _active->_post_ack.type = d.type; _active->_post_ack.previous_hop = from;
    _active->_post_ack.inner_len = static_cast<uint8_t>(inner.size() <= protocol::max_payload_bytes_hard_cap
                                               ? inner.size() : protocol::max_payload_bytes_hard_cap);
    for (uint8_t i = 0; i < _active->_post_ack.inner_len; ++i) _active->_post_ack.inner[i] = inner[i];
    if (d.crypted) for (uint8_t i = 0; i < 8; ++i) _active->_post_ack.nonce_seed[i] = nseed[i];   // §1b: seed already extracted above (the dedup key) — stash verbatim for the open / forward
    // Clamp the underflow: the exhaustion NACK that guarantees hb_new_remaining>=0 only fires for a FORWARD
    // (d.dst != self, line above) — the DELIVERY case (d.dst==self) is exempt, so a DM that arrived AT us with
    // hops_remaining==0 leaves hb_new_remaining==-1. That value is dead for a plain deliver, but an L2c
    // misdelivery re-forwards from the delivery case, so -1 -> uint8_t 255 (saturating to the 31 max) must not
    // leak into a budget. (L2c re-budgets its leg from rt anyway; this is belt-and-suspenders.)
    _active->_post_ack.fwd_remaining = static_cast<uint8_t>(hb_new_remaining < 0 ? 0 : hb_new_remaining);
    _active->_post_ack.fwd_committed = hb_new_committed;                         // carried into the forward TxItem
    (void)_hal.after(airtime_routing_ms(3) + 1, kPostAckTimerId);
}

void Node::do_post_ack() {
    if (!_active->_post_ack.pending) return;
    const PostAck pa = _active->_post_ack;
    _active->_post_ack.pending = false;
    if (!pa.is_forward) {
        // Parse the inner up-front (the optional DST_HASH prefix + the cross-layer layer-path, read from pa.flags).
        auto ui = parse_unicast_inner(std::span<const uint8_t>(pa.inner, pa.inner_len), pa.flags);
        // Slice 4c.1 (the bridge KEYSTONE): a CROSS_LAYER DM in TRANSIT through this gateway -> BRIDGE it to the next
        // layer BEFORE any type-based consume (so a 4e cross-layer E2E-ack passing through bridges, not gets consumed).
        // dst_hash == our key => we ARE the recipient: fall through to the normal handling (E2E-ack confirm / deliver to
        // inbox). A malformed CROSS_LAYER inner is REFUSED (drop) — its layer-path bytes must never reach the app as body.
        if (pa.flags & DATA_FLAG_CROSS_LAYER) {
            if (!ui || !ui->has_cross_layer) { become_free(); return; }
            if (!(ui->has_dst_hash && ui->dst_key_hash32 == _key_hash32)) { bridge_cross_layer(pa, *ui); return; }
        }
        if (pa.type == DATA_TYPE_H_ANSWER || pa.type == DATA_TYPE_AUTHORITATIVE_H_ANSWER) {   // a hash-bind answer for us -> consume (routing info, NOT a DM)
            on_hash_bind_response(pa.inner, pa.inner_len, pa.type == DATA_TYPE_AUTHORITATIVE_H_ANSWER);
            become_free();
            return;
        }
        if (pa.type == DATA_TYPE_AUTHORITATIVE_H_ANSWER_PUBKEY) {   // E2E §6: the owner's pubkey answer -> cache (routing/key info, NOT a DM)
            on_hash_bind_pubkey(pa.inner, pa.inner_len);
            become_free();
            return;
        }
        if (pa.type == DATA_TYPE_REMOTE_CMD || pa.type == DATA_TYPE_REMOTE_RESP) {   // OTA remote diagnostics: STAGE for the main loop
            // NOT inbox'd / delivered-as-message, NOT consumed-silently like an E2E ack — fw_main executes (cmd) or prints
            // (resp) on the main loop, never the RX path. One in flight; a 2nd while pending drops (rcmd is human-paced).
            if (_remote_inbound.active) {
                MR_EMIT("remote_inbound_drop_full", EF_I("from", pa.origin));
            } else {
                const uint8_t* src = ui ? ui->body.data() : ((pa.inner_len > 1) ? pa.inner + 1 : nullptr);   // inner = [origin][body…]; body is ui->body (cleartext)
                uint8_t n = ui ? static_cast<uint8_t>(ui->body.size()) : ((pa.inner_len > 1) ? static_cast<uint8_t>(pa.inner_len - 1) : 0);
                if (n > protocol::inbox_max_body) n = protocol::inbox_max_body;
                _remote_inbound.active      = true;
                _remote_inbound.is_response = (pa.type == DATA_TYPE_REMOTE_RESP);
                _remote_inbound.from        = pa.origin;
                _remote_inbound.len         = n;
                for (uint8_t i = 0; i < n; ++i) _remote_inbound.body[i] = src ? src[i] : 0;
            }
            become_free();
            return;
        }
        if (pa.type == DATA_TYPE_E2E_ACK) {              // an end-to-end ACK for a DM we originated -> confirm + RECORD a receipt, not deliver
            // The acked ctr: a same-layer E2E_ACK inner is [origin][ctr_lo][ctr_hi] (ctr at inner[1..2]); a 4e
            // CROSS_LAYER ack is ...[origin][source_hash][body=ctr_lo,ctr_hi] -> the ctr is the parsed BODY (ui).
            // Computed ALWAYS (was telemetry-only): the durable receipt + the live push need it on metal (NO_TELEMETRY).
            const uint16_t acked = ((pa.flags & DATA_FLAG_CROSS_LAYER) && ui && ui->body.size() >= 2)
                                   ? static_cast<uint16_t>(ui->body[0] | (ui->body[1] << 8))
                                   : ((pa.inner_len >= 3) ? static_cast<uint16_t>(pa.inner[1] | (pa.inner[2] << 8)) : 0);
            // Cross-layer: the acker's STABLE key (the 8-bit origin aliases across leaves) -> the companion's match key.
            // Same-layer: (origin, ctr) suffices, acker_hash=0.
            const uint32_t acker_hash = ((pa.flags & DATA_FLAG_CROSS_LAYER) && ui && ui->has_source_hash) ? ui->source_hash : 0;
            _inbox.record_ack(pa.origin, acked, active_layer_id(), _hal.now(), acker_hash);   // durable receipt (DM store); inert if no backend (sim)
            Push pu{}; pu.kind = PushKind::send_e2e_acked; pu.dst = pa.origin; pu.ctr = acked; enqueue_push(pu);   // live fast-path (E2E-ACKED ctr=X from=D)
            MR_TELEMETRY(                                                                       // KEEP for the sim analyzer (free on metal)
                EventField ef[] = { { .key = "from", .type = EventField::T::i64, .i = pa.origin },
                                    { .key = "ctr",  .type = EventField::T::i64, .i = acked } };
                _hal.emit("e2e_ack_rx", ef, 2); );
            become_free();
            return;
        }
        // L2c verify-on-delivery (NON-cross-layer; cross-layer was forked above): DST_HASH present and naming a key
        // that ISN'T ours => an id collision misdelivered this DM. Heal the collision + redirect to the real owner.
        if (ui && ui->has_dst_hash && ui->dst_key_hash32 != _key_hash32) {
            l2c_handle_misdelivery(pa, ui->dst_key_hash32);     // forward to the real owner (identity-preserving)
            return;                                             // l2c re-kicks the queue itself (become_free)
        }
        // E2E OPEN (§1a sealed-sender): a CRYPTED DM carries NO cleartext sender hint -> TRIAL DECRYPT over the cached
        // peer keys; the Poly1305 tag identifies the sender + opens the sealed {source_hash + location + body}. The seed
        // rides the trailer. FAIL LOUD: no cached key opens it -> SILENT DROP (never deliver ciphertext to the app).
        uint32_t dec_source_hash = 0; bool dec_has_loc = false; int32_t dec_lat = 0, dec_lon = 0;
        // ADDENDUM 4 (2026-06-25): static, NOT stack. do_post_ack is non-reentrant (one timer fires at a time in the
        // single loop task) and is the deepest path on the cramped 4 KB FreeRTOS loop stack; the DWT watchpoint caught
        // route_strictly_better (reached via send_e2e_ack, below) overflowing right here. Moving these ~480 B of
        // payload buffers off the frame restores the headroom at the exact overflow point. e2e_open_trial fills
        // dec_body before any read; dec_body_len stays a fresh local (re-set each call).
        static uint8_t dec_body[protocol::max_payload_bytes_hard_cap]; uint8_t dec_body_len = 0;
        uint32_t dec_origin = pa.origin;   // §1a: for CRYPTED the trial recovers origin (== cleartext now; from the seal at 1c)
        bool crypted_ok = false;
        if (pa.flags & DATA_FLAG_CRYPTED) {
            // §1a sealed-sender: no cleartext sender hint -> TRIAL DECRYPTION over the cached keys; the tag identifies it.
            uint32_t trial_sender = 0;
            if (!e2e_open_trial(pa.inner, pa.inner_len, pa.nonce_seed, pa.flags, pa.ctr, trial_sender, dec_origin,
                                dec_source_hash, dec_has_loc, dec_lat, dec_lon, dec_body, dec_body_len)) {
                MR_EMIT("e2e_open_no_key", EF_I("ctr", pa.ctr));            // no cached key opens it -> SILENT DROP (no push/ack/inbox)
                become_free(); return;
            }
            crypted_ok = true; (void)trial_sender;   // dec_source_hash (sealed, anti-spoof-verified) == trial_sender = the sender
        }
        // deliver: body from the parsed inner (raw inner[1..] fallback — origin at inner[0] — if it didn't parse).
        static char body[protocol::max_payload_bytes_hard_cap + 1];   // ADDENDUM 4: static (non-reentrant) — paired with dec_body, off the do_post_ack stack frame
        uint8_t blen;
        if (crypted_ok) { blen = dec_body_len;                              // the DECRYPTED body (sealed region opened above)
                          for (uint8_t i = 0; i < blen; ++i) body[i] = static_cast<char>(dec_body[i]); }
        else if (ui)    { blen = static_cast<uint8_t>(ui->body.size());
                          for (uint8_t i = 0; i < blen; ++i) body[i] = static_cast<char>(ui->body[i]); }
        else            { blen = (pa.inner_len > 1) ? static_cast<uint8_t>(pa.inner_len - 1) : 0;
                          for (uint8_t i = 0; i < blen; ++i) body[i] = static_cast<char>(pa.inner[1 + i]); }
        body[blen] = '\0';
        MR_TELEMETRY(
            EventField f[] = {
                { .key = "origin",  .type = EventField::T::i64, .i = dec_origin },
                { .key = "dst",     .type = EventField::T::i64, .i = pa.dst },
                { .key = "ctr",     .type = EventField::T::i64, .i = pa.ctr },
                { .key = "payload", .type = EventField::T::str, .s = body },     // dm_delivery keys (dst, payload)
            };
            _hal.emit("delivered", f, 4); );
        // sender_hash = the origin's stable key_hash32 (when SOURCE_HASH was set) — the app's DM dedup identity.
        const uint32_t sender_hash = crypted_ok ? dec_source_hash : ((ui && ui->has_source_hash) ? ui->source_hash : 0);
        // Record-on-delivery FIRST (the FINAL-destination deliver path, once per delivered DM): it returns the
        // inbox seq (0 if disabled). The live msg_recv push then carries the SAME sender_hash + seq as the pulled
        // record -> the app dedups by (sender_hash, ctr) and detects a dropped live push by the seq (model B).
        const uint8_t rx_layer = active_layer_id();   // §2/Q13: which layer this DM arrived on (disambiguates origin on a gateway)
        const uint32_t seq = _inbox.record_dm(dec_origin, sender_hash, pa.ctr, rx_layer,
                                              reinterpret_cast<const uint8_t*>(body), blen, _hal.now(), /*enc=*/crypted_ok ? 1 : 0);  // §8b
        Push pu{}; pu.kind = PushKind::msg_recv; pu.origin = dec_origin; pu.dst = pa.dst; pu.ctr = pa.ctr;   // §1a: recovered origin for CRYPTED
        pu.layer_id = rx_layer; pu.sender_hash = sender_hash; pu.seq = seq; pu.enc = crypted_ok;   // §8b: was this DM sealed?
        pu.body_len = blen; for (uint8_t i = 0; i < blen; ++i) pu.body[i] = static_cast<uint8_t>(body[i]);
        // LOCATION (spec §5): the sender piggybacked its 6-B location -> surface it to the app on the Push (always
        // compiled — the companion renders it) + a peer_location telemetry for the sim/gate (device-stripped).
        // A firmware-side peer-location cache is the optional §5 follow-up (the companion holds the map for v1).
        const bool    loc_present = crypted_ok ? dec_has_loc : (ui && ui->has_location);
        const int32_t loc_lat     = crypted_ok ? dec_lat : (ui ? ui->lat_e7 : 0);
        const int32_t loc_lon     = crypted_ok ? dec_lon : (ui ? ui->lon_e7 : 0);
        if (loc_present) {
            pu.has_location = true; pu.lat_e7 = loc_lat; pu.lon_e7 = loc_lon;
            MR_TELEMETRY(
                EventField pf[] = {
                    { .key = "origin", .type = EventField::T::i64, .i = dec_origin },
                    { .key = "hash",   .type = EventField::T::i64, .i = static_cast<int64_t>(sender_hash) },
                    { .key = "lat_e7", .type = EventField::T::i64, .i = loc_lat },
                    { .key = "lon_e7", .type = EventField::T::i64, .i = loc_lon },
                };
                _hal.emit("peer_location", pf, 4); );
        }
        enqueue_push(pu);                                // app channel: the inbound message (live notify, seq-stamped)
        // E2E ACK requested -> reply with the acked ctr. CROSS_LAYER -> a reversed-path cross-layer ack (4e); else the
        // same-layer ack home on the F reverse path.
        if (pa.flags & DATA_FLAG_E2E_ACK_REQ) {
            if ((pa.flags & DATA_FLAG_CROSS_LAYER) && ui) send_e2e_ack_cross_layer(*ui, pa.ctr);
            else                                          send_e2e_ack(dec_origin, pa.ctr);   // §1a: ack the recovered origin
        }
        become_free();
    } else {
        // §intra-layer-relay (2026-07-05): a GATEWAY does NOT relay other nodes' same-leaf traffic by default (design §6).
        // A cross-layer transit DM is addressed TO the gateway (dst==_node_id -> the deliver/BRIDGE branch above), so ANY
        // forward (dst!=_node_id) reaching HERE on a gateway is an intra-leaf relay -> DROP (unless the operator opted in).
        // The cross-layer bridge (drain_xl_handoffs re-inject) is a SEPARATE originated-TX path, NOT this received-DATA
        // forward, so it is unaffected. Belt-and-suspenders to the sender-side next_hop_selectable gate (Edit 3).
        if (_cfg.is_gateway && !_cfg.intra_layer_relay) {
            MR_EMIT("gateway_intra_relay_drop", EF_I("dst", pa.dst), EF_I("origin", pa.origin));
            become_free();
            return;
        }
        // C.2 cache-on-pass: a relayed hash-bind answer is cleartext -> snoop the binding before forwarding.
        if (pa.type == DATA_TYPE_H_ANSWER || pa.type == DATA_TYPE_AUTHORITATIVE_H_ANSWER)
            on_hash_bind_snoop(pa.inner, pa.inner_len, pa.type == DATA_TYPE_AUTHORITATIVE_H_ANSWER);
        else if (pa.type == DATA_TYPE_AUTHORITATIVE_H_ANSWER_PUBKEY)
            on_hash_bind_pubkey(pa.inner, pa.inner_len);            // E2E §6: cache-on-pass the owner's pubkey
        TxItem it{};
        it.origin = pa.origin; it.dst = pa.dst; it.ctr = pa.ctr; it.ctr_lo = pa.ctr_lo;
        it.flags = pa.flags; it.type = pa.type; it.is_forward = true; it.previous_hop = pa.previous_hop;
        it.inner_len = pa.inner_len;
        for (uint8_t i = 0; i < pa.inner_len; ++i) it.inner[i] = pa.inner[i];
        for (int i = 0; i < 8; ++i) it.nonce_seed[i] = pa.nonce_seed[i];   // CRYPTED: a relay re-tx's the original nonce-seed verbatim
        it.fwd_remaining = pa.fwd_remaining; it.fwd_committed = pa.fwd_committed;   // carry the decremented budget
        it.enqueue_time_ms = _hal.now();                 // fresh hop attempt (dv:11391): the cascade-requeue
                                                         // total-age window starts when THIS hop accepts the
                                                         // forward — else it defaults 0 and the cap mis-fires.
        if (_active->_tx_queue_n < kTxQueueCap) _active->_tx_queue[_active->_tx_queue_n++] = it;
        become_free();
    }
}

// ---- Slice 4c.1: cross-layer DM bridge (the keystone) ------------------------------------------------------------
// Resolve key_hash32 -> node_id on a SPECIFIC leaf's id_bind. NEVER via _active-> : the bridge writes a NON-active
// leaf, and any _active-> deref mid-resolve would read/corrupt the wrong leaf's state (the subtlest aliasing trap).
// Mirrors id_bind_find_by_hash's match (key + not-expired, self exempt) but on _layers[leaf]. -1 = unknown.
int Node::id_on_leaf_by_hash(uint8_t leaf, uint32_t key_hash32) const {
    if (leaf >= _n_layers) return -1;
    const LayerRuntime& L = _layers[leaf];
    const uint64_t now = _hal.now();
    for (uint16_t i = 0; i < L._id_bind_n; ++i) {
        if (L._id_bind[i].key_hash32 != key_hash32) continue;
        const bool self_keep = (L._id_bind[i].node_id == _cfg.layers[leaf].node_id && L._id_bind[i].key_hash32 == _key_hash32);
        if (!self_keep && _cfg.id_bind_ttl_ms > 0
            && (now - L._id_bind[i].last_seen_ms) >= _cfg.id_bind_ttl_ms) continue;   // expired -> skip
        return L._id_bind[i].node_id;
    }
    return -1;
}

// Loop suppression: seed the TARGET leaf's _seen_origins for the re-inject's (origin, dst, ctr) so when THIS gateway
// later hears its own relay on that leaf it is caught as a live_dup (ACK-only, no re-bridge / re-forward). Mirrors
// record_seen_origin but on _layers[leaf] (the bridge writes a non-active leaf).
void Node::seed_seen_origin_on_leaf(uint8_t leaf, uint8_t origin, uint8_t dst, uint16_t ctr) {
    if (leaf >= _n_layers) return;
    // PLAINTEXT-namespace key (< 2^32), matching handle_data's non-CRYPTED sokey. (A CRYPTED DM never reaches a gateway
    // bridge — e2e_dm + cross-layer is refused — so this path is always plaintext.)
    const uint64_t sokey = (static_cast<uint64_t>(origin) << 24) | (static_cast<uint64_t>(dst) << 16) | ctr;
    const uint64_t now = _hal.now();
    _layers[leaf]._seen_origins[sokey]     = now + protocol::seen_origin_ttl_ms;
    _layers[leaf]._seen_origin_from[sokey] = _node_id;   // we re-injected it
}

// Buffer a handoff into the node-global ring; false = full (the caller REFUSES loud — never drop-oldest a transit DM).
bool Node::push_xl_handoff(const XlHandoff& h) {
    for (uint8_t i = 0; i < protocol::cap_gateway_handoffs; ++i)
        if (!_xl_handoffs[i].valid) { _xl_handoffs[i] = h; return true; }
    return false;
}

void Node::bridge_cross_layer(const PostAck& pa, const data_unicast_inner& ui) {
    // L13 (2026-07-04): a SINGLE-layer node NEVER bridges — only a dual-layer gateway (n_layers==2) does. Without
    // this guard a crafted CROSS_LAYER DM whose target_layer_id == our own single leaf's layer_id would match the
    // loop below (target_leaf=0), fill the cap-1 handoff slot, and induce an H-flood for up to ~60 s (a cheap DoS).
    // Refuse at the top (the caller relies on us to become_free()+return, matching the no-leaf-match early-refuse).
    if (_n_layers < 2) { become_free(); return; }
    // ui.has_cross_layer is guaranteed by the caller. The next layer to ENTER = layer_ids[cur].
    const uint8_t target_layer_id = ui.layer_ids[ui.cur];
    // Which of OUR leaves carries that layer_id? (A gateway owns 2.) Not one of ours -> REFUSE loud (no default leaf).
    int target_leaf = -1;
    for (uint8_t i = 0; i < _n_layers; ++i) if (_cfg.layers[i].layer_id == target_layer_id) { target_leaf = i; break; }
    if (target_leaf < 0) {
        MR_EMIT("xl_bridge_refused", EF_I("reason", 1), EF_I("target_layer", target_layer_id), EF_I("origin", pa.origin), EF_I("ctr", pa.ctr));
        become_free(); return;
    }
    // The stable recipient identity (dst_hash) must be present + resolvable on the TARGET leaf. Unknown binding ->
    // Slice 4f defers (H-flood + handoff TTL); v1 (4c.1) REFUSES loud (drop, NEVER a silent reroute).
    if (!ui.has_dst_hash) {
        MR_EMIT("xl_bridge_refused", EF_I("reason", 2), EF_I("origin", pa.origin), EF_I("ctr", pa.ctr));
        become_free(); return;
    }
    const int dst_node = id_on_leaf_by_hash(static_cast<uint8_t>(target_leaf), ui.dst_key_hash32);   // -1 = unknown -> 4f DEFERS (resolve at drain + H-flood), never drops
    // Advance cur ONLY if a further gateway hop remains (multi-gateway, reserved). v1: cur == n_layers-1 -> unchanged.
    uint8_t new_cur = ui.cur;
    if (static_cast<uint8_t>(ui.cur + 1) < ui.n_layers) new_cur = static_cast<uint8_t>(ui.cur + 1);
    // The re-inject inner is the ORIGINAL preserved verbatim (dst_hash + the full layer-path + origin + source_hash +
    // body); only the cursor byte is patched for a multi-gw advance. dst_node 0 = UNRESOLVED (drain re-resolves + H-floods).
    XlHandoff h{};
    h.valid = true; h.target_leaf = static_cast<uint8_t>(target_leaf);
    h.dst_node_id = (dst_node > 0) ? static_cast<uint8_t>(dst_node) : 0;
    h.dst_key_hash32 = ui.dst_key_hash32;                     // 4f: re-resolve + H-flood the binding on the target leaf
    h.origin = pa.origin; h.ctr = pa.ctr; h.ctr_lo = pa.ctr_lo; h.flags = pa.flags; h.type = pa.type;
    for (int i = 0; i < 8; ++i) h.nonce_seed[i] = pa.nonce_seed[i];   // S1: CRYPTED transit DM keeps the originator's seed across the bridge
    h.inner_len = pa.inner_len;
    for (uint8_t i = 0; i < pa.inner_len; ++i) h.inner[i] = pa.inner[i];
    if (new_cur != ui.cur) {                                   // patch cur (layer-path offset = dst_hash?4:0; cur byte at off+1)
        const uint8_t off = static_cast<uint8_t>(ui.has_dst_hash ? 4 : 0);
        if (static_cast<size_t>(off) + 1 < h.inner_len) h.inner[off + 1] = new_cur;
    }
    h.queued_at_ms = _hal.now();
    if (!push_xl_handoff(h)) {                                 // full -> REFUSE loud (the sender's E2E ack just won't come)
        MR_EMIT("xl_handoff_full", EF_I("origin", pa.origin), EF_I("dst", dst_node), EF_I("ctr", pa.ctr));
        become_free(); return;
    }
    // (The loop-suppression seed moves to the DRAIN: for an UNRESOLVED handoff dst_node isn't known yet; the drain
    //  seeds _seen_origins right before building the re-inject, once the recipient id is resolved on the target leaf.)
    MR_EMIT("xl_bridge", EF_I("origin", pa.origin), EF_I("dst", dst_node), EF_I("ctr", pa.ctr),
            EF_I("target_leaf", target_leaf), EF_I("cur", new_cur), EF_I("resolved", dst_node > 0 ? 1 : 0));
    become_free();                                            // drains (resolved) or defers + H-floods (unresolved) on the target leaf's window
}

// Called from activate_layer(leaf) with _active == &_layers[leaf]: move every handoff targeting this leaf into its
// tx_queue as a fresh-budget RELAY leg (identity preserved — mirrors l2c_enqueue_forward; + is_gw_relay + the DM type).
void Node::drain_xl_handoffs_for_leaf(uint8_t leaf) {
    if (leaf >= _n_layers || &_layers[leaf] != _active) return;   // the drain is _active-COUPLED (re-resolve/flood/rt/enqueue use _active) — refuse a wrong-leaf call
    const uint64_t now = _hal.now();
    for (uint8_t i = 0; i < protocol::cap_gateway_handoffs; ++i) {
        XlHandoff& h = _xl_handoffs[i];
        if (!h.valid || h.target_leaf != leaf) continue;
        // Slice 4f: an UNRESOLVED handoff (binding unknown at bridge) re-resolves on THIS (now-active target) leaf's
        // id_bind. Found -> drain below. Still unknown -> H-flood the binding on this leaf (throttled, one per visit)
        // + keep deferred; on the TTL -> give up LOUD (X's DM retry recovers it — an ack/transit DM never floods home).
        if (h.dst_node_id == 0) {
            const int rid = id_bind_find_by_hash(h.dst_key_hash32);   // _active == &_layers[leaf] here
            if (rid > 0) {
                h.dst_node_id = static_cast<uint8_t>(rid);
            } else if (now - h.queued_at_ms >= protocol::gateway_handoff_defer_ttl_ms) {
                MR_EMIT("xl_handoff_giveup", EF_I("origin", h.origin), EF_I("ctr", h.ctr), EF_I("dst_hash", static_cast<int64_t>(h.dst_key_hash32)), EF_I("leaf", leaf));
                h.valid = false;                                     // TTL exceeded -> DROP loud
                continue;
            } else {
                if (h.last_h_flood_ms == 0 || now - h.last_h_flood_ms >= protocol::gateway_handoff_reflood_ms) {  // 0 = never flooded -> fire now
                    emit_hash_query(h.dst_key_hash32, /*hard=*/false);   // flood an H query on THIS leaf (we're on it now)
                    // ONE H query resolves the binding for EVERY pending handoff to this hash -> stamp them all so
                    // siblings don't re-flood the SAME query this pass / window (review #4: no duplicate floods).
                    for (uint8_t j = 0; j < protocol::cap_gateway_handoffs; ++j)
                        if (_xl_handoffs[j].valid && _xl_handoffs[j].dst_node_id == 0 && _xl_handoffs[j].dst_key_hash32 == h.dst_key_hash32)
                            _xl_handoffs[j].last_h_flood_ms = now;
                    MR_EMIT("xl_handoff_h_flood", EF_I("ctr", h.ctr), EF_I("dst_hash", static_cast<int64_t>(h.dst_key_hash32)), EF_I("leaf", leaf));
                }
                continue;                                            // keep deferred -> re-resolve on a later visit / the H-answer
            }
        }
        // resolved -> build the relay leg. KEEP the slot until the enqueue SUCCEEDS: a transient queue-full RETRIES
        // next visit (like the deferred path), NEVER drops a resolved transit DM (review HIGH #1).
        if (_active->_tx_queue_n >= kTxQueueCap) {
            MR_EMIT("xl_handoff_queue_full_retry", EF_I("origin", h.origin), EF_I("dst", h.dst_node_id), EF_I("ctr", h.ctr));
            continue;                                          // h.valid stays true -> retried on the next window
        }
        // Loop suppression (moved here from the bridge, 4f): seed THIS leaf so we live_dup our own re-inject.
        seed_seen_origin_on_leaf(leaf, h.origin, h.dst_node_id, h.ctr);
        TxItem it{};
        it.origin = h.origin; it.dst = h.dst_node_id; it.ctr = h.ctr; it.ctr_lo = h.ctr_lo;
        it.flags = h.flags; it.type = h.type; it.is_forward = true; it.is_gw_relay = true; it.previous_hop = 0;
        for (int i = 0; i < 8; ++i) it.nonce_seed[i] = h.nonce_seed[i];   // S1: CRYPTED transit DM keeps its nonce seed on the re-inject leg
        RtEntry* rte = rt_find(h.dst_node_id);                // FRESH budget from THIS (target) leaf's route to the recipient
        const uint8_t rt_hops = (rte && rte->n > 0) ? rte->candidates[0].hops : 1;
        const int rem = static_cast<int>(rt_hops) + protocol::hop_budget_slack;
        it.fwd_remaining = static_cast<uint8_t>(rem > protocol::hop_budget_max_initial ? protocol::hop_budget_max_initial : rem);
        it.fwd_committed = 0;
        it.inner_len = (h.inner_len > protocol::max_payload_bytes_hard_cap) ? protocol::max_payload_bytes_hard_cap : h.inner_len;
        for (uint8_t k = 0; k < it.inner_len; ++k) it.inner[k] = h.inner[k];
        it.enqueue_time_ms = _hal.now();
        _active->_tx_queue[_active->_tx_queue_n++] = it;
        h.valid = false;                                       // consume the slot ONLY after a successful enqueue (review HIGH #1)
        MR_EMIT("xl_handoff_drained", EF_I("origin", h.origin), EF_I("dst", h.dst_node_id), EF_I("ctr", h.ctr), EF_I("leaf", leaf));
    }
    // activate_layer calls become_free() right after this -> the drained relay leg gets serviced in this window.
}

void Node::handle_ack(const uint8_t* bytes, size_t len, const RxMeta& meta) {
    auto pk = parse_ack(std::span<const uint8_t>(bytes, len));
    if (!pk) return;
    const ack_out& k = *pk;
    if (k.to != _node_id) return;
    if (!_active->_pending_tx || !_active->_pending_tx->awaiting_ack || _active->_pending_tx->ctr_lo != k.ctr_lo) return;
    // src-less by design (see handle_cts): to+ctr_lo already identifies the ACK as our next-hop's. The
    // src_hint cross-check is SIM-ONLY, so gate it on availability rather than REJECTING when absent — the
    // old `src_hint < 0 ||` dropped EVERY ack on metal (device src_hint=-1), so the DM never completed.
    if (meta.src_hint >= 0 && static_cast<uint8_t>(meta.src_hint) != _active->_pending_tx->next) return;  // (cf. NACK gate, Lua dv:10300)
    _hal.cancel(kAckTimeoutTimerId);
    _hal.cancel(kRetryBackoffTimerId);                   // drop a stale retry armed by a just-fired ack_timeout
    // Learn the ACK sender (= our next-hop) as a 1-hop neighbour (Lua learn_rx_source / ack_frame).
    if (learn_direct_neighbor(_active->_pending_tx->next, protocol::db_to_q4(meta.snr_db), false)) schedule_triggered_beacon();
    // R4.2: consume the ACK's piggybacked budget_hint -> learn the next-hop's tier in the FORWARD
    // direction (the NACK only covers the reverse). local_only=true: rerank routes but DON'T dirty /
    // schedule a beacon (so NO triggered-beacon draw on the forward path). Lua dv:10341-10344.
    [[maybe_unused]] int ack_budget_reranked = 0;
    if (k.budget_hint > static_cast<uint8_t>(BudgetTier::healthy)) {
        const uint8_t tier = (k.budget_hint > static_cast<uint8_t>(BudgetTier::critical))
                             ? static_cast<uint8_t>(BudgetTier::critical) : k.budget_hint;
        // the ACK is from our next-hop (matched above) — use that, not src_hint (sim-only / -1 on device).
        ack_budget_reranked = mark_neighbor_budget_tier(_active->_pending_tx->next,
                                                        tier, "ack_budget", /*local_only=*/true);
    }
    // Inc 3: a warn'd ACK means our next-hop considers us near its airtime cap. Honest back-off — park new
    // DM originations until the warn window expires (the hard receiver-side drop is the backstop). The
    // window self-clears: as our airtime ages out of the neighbour's window it stops setting the bit.
    if (k.warn) {
        _ack_warn_until = _hal.now() + protocol::originator_ack_warn_backoff_ms;
        MR_TELEMETRY(
            EventField wf[] = { { .key = "from",   .type = EventField::T::i64, .i = _active->_pending_tx->next },
                                { .key = "ctr_lo", .type = EventField::T::i64, .i = k.ctr_lo },
                                { .key = "backoff_until_ms", .type = EventField::T::i64, .i = static_cast<int64_t>(_ack_warn_until) } };
            _hal.emit("originator_warned_by_ack", wf, 3); );
    }
    MR_TELEMETRY(
        EventField f[] = { { .key = "from",     .type = EventField::T::i64, .i = _active->_pending_tx->next },   // ACK is from our next-hop (src_hint=-1 on metal)
                           { .key = "origin",   .type = EventField::T::i64, .i = _active->_pending_tx->origin },
                           { .key = "dst",      .type = EventField::T::i64, .i = _active->_pending_tx->dst },
                           { .key = "ctr",      .type = EventField::T::i64, .i = _active->_pending_tx->ctr },
                           { .key = "budget_hint",     .type = EventField::T::i64, .i = k.budget_hint },
                           { .key = "budget_reranked", .type = EventField::T::i64, .i = ack_budget_reranked },
                           { .key = "airtime_warn",    .type = EventField::T::i64, .i = k.warn ? 1 : 0 } };
        _hal.emit("ack_rx", f, 7); );
    { Push pu{}; pu.kind = PushKind::send_acked; pu.dst = _active->_pending_tx->dst; pu.ctr = _active->_pending_tx->ctr; enqueue_push(pu); }
    _active->_pending_tx.reset();
    become_free();
}

// The sender's NACK handler (dv:10365). A NACK is faster feedback than the timeout:
// LOOP_DUP -> cascade to an alt (or direct giveup); BUSY_RX -> mark the peer blind +
// wait-same-hop (short busy) or requeue (long busy). BUDGET/HOP_BUDGET deferred.
void Node::handle_nack(const uint8_t* bytes, size_t len, const RxMeta& meta) {
    auto pn = parse_nack(std::span<const uint8_t>(bytes, len));
    if (!pn) return;
    const nack_out& n = *pn;
    if (n.to != _node_id) return;                                   // not for us
    if (!_active->_pending_tx) return;                                       // no flight to react on
    if (_active->_pending_tx->ctr_lo != n.ctr_lo) return;                    // stale (different flight). L9 NOTE: WIRE-bounded — a NACK carries only the 4-bit ctr_lo, not flight_gen, so a NACK for a since-replaced flight with an ALIASED ctr_lo (1/16) can still match here. Fully fixing needs more wire ctr bits (a frame change, out of scope); the LOCAL re-arm paths (retry-stash, nack-wait) are now flight_gen-exact.
    if (meta.src_hint >= 0 && static_cast<uint8_t>(meta.src_hint) != _active->_pending_tx->next) {
        MR_TELEMETRY(
            EventField f[] = { { .key = "from", .type = EventField::T::i64, .i = static_cast<uint8_t>(meta.src_hint) } };
            _hal.emit("nack_drop_unexpected_src", f, 1); );
        return;
    }
    // Learn the NACK sender (= our next-hop) as a 1-hop neighbour (Lua learn_rx_source / nack_frame).
    if (learn_direct_neighbor(_active->_pending_tx->next, protocol::db_to_q4(meta.snr_db), false)) schedule_triggered_beacon();
    _hal.cancel(kRtsTimeoutTimerId);                                // faster than the timeout (dv:10390)
    _hal.cancel(kAckTimeoutTimerId);
    _active->_pending_tx->awaiting_cts = false; _active->_pending_tx->awaiting_ack = false;
    PendingTx& pt = *_active->_pending_tx;

    if (n.reason == protocol::nack_reason_loop_dup) {
        [[maybe_unused]] const uint8_t from_next = pt.next;
        mark_tried(pt, pt.next);
        const uint8_t alt = pick_next_cascade_hop(pt);
        if (alt != 0) {                                            // cascade to an alt (NO jitter)
            MR_TELEMETRY(
                EventField f[] = { { .key = "origin",   .type = EventField::T::i64, .i = pt.origin },
                                   { .key = "dst",      .type = EventField::T::i64, .i = pt.dst },
                                   { .key = "ctr",      .type = EventField::T::i64, .i = pt.ctr },
                                   { .key = "from_next", .type = EventField::T::i64, .i = from_next },
                                   { .key = "next",     .type = EventField::T::i64, .i = alt } };
                _hal.emit("path_cascade", f, 5);
                _hal.emit("tx_loop_alt", f, 5); );
            pt.next = alt;
            pt.retries_left = effective_rts_max_retries(pt.requeue_count);
            pt.retry_attempt = 0;                                 // cascade -> a NEW contention context: reset the backoff growth (spec 2026-06-26)
            tx_rts_retry();
        } else {                                                  // LOOP_DUP miss -> DIRECT giveup (NOT requeue, dv:10588)
            MR_TELEMETRY(
                EventField f[] = { { .key = "dst", .type = EventField::T::i64, .i = pt.dst },
                                   { .key = "ctr", .type = EventField::T::i64, .i = pt.ctr } };
                _hal.emit("path_cascade_exhausted", f, 2);
                _hal.emit("rts_giveup", f, 2); );
            { Push pu{}; pu.kind = PushKind::send_failed; pu.dst = pt.dst; pu.ctr = pt.ctr; enqueue_push(pu); }
            _active->_pending_tx.reset();
            become_free();
        }
        return;
    }

    if (n.reason == protocol::nack_reason_busy_rx) {
        const uint64_t now = _hal.now();
        const uint64_t busy_for = static_cast<uint64_t>(n.payload) * protocol::nack_busy_quantum_ms;
        MR_TELEMETRY(
            EventField rf[] = { { .key = "from",   .type = EventField::T::i64, .i = pt.next },
                                { .key = "reason", .type = EventField::T::i64, .i = protocol::nack_reason_busy_rx },
                                { .key = "busy_ms", .type = EventField::T::i64, .i = static_cast<int64_t>(busy_for) } };
            _hal.emit("nack_rx", rf, 3); );
        if (busy_for > 0) {                                        // mark the peer blind, max-merge (dv:10627)
            const uint64_t until = now + busy_for;
            auto bit = _active->_blind_until.find(pt.next);
            _active->_blind_until[pt.next] = (bit != _active->_blind_until.end() && bit->second > until) ? bit->second : until;
            MR_TELEMETRY(
                EventField bf[] = { { .key = "next", .type = EventField::T::i64, .i = pt.next } };
                _hal.emit("blind_observed", bf, 1); );
        }
        if (busy_for <= protocol::nack_wait_threshold_ms) {        // short busy -> wait SAME hop
            const int jit = _hal.rand_range(0, static_cast<int>(retry_jitter_ms()) + 1);   // N1 (the only new draw)
            const uint32_t wait = static_cast<uint32_t>(busy_for) + 1 + static_cast<uint32_t>(jit);
            _nack_wait_flight_gen = pt.flight_gen; _nack_wait_pending = true;   // L9: key the BUSY_RX re-RTS wait on the exact flight (was pt.ctr_lo)
            (void)_hal.after(wait, kNackWaitTimerId);
        } else {                                                  // long busy -> requeue SAME hop (verbatim meta)
            TxItem it = txitem_from_pending(pt);   // S1: full identity+crypto core (incl. nonce_seed — the uncited long-busy drop)
            it.requeue_count = pt.requeue_count; it.enqueue_time_ms = pt.enqueue_time_ms;   // VERBATIM (no ++/backoff)
            it.next_attempt_ms = 0;
            MR_TELEMETRY(
                EventField tf[] = { { .key = "dst", .type = EventField::T::i64, .i = pt.dst },
                                    { .key = "ctr", .type = EventField::T::i64, .i = pt.ctr } };
                _hal.emit("tx_requeued", tf, 2); );
            if (_active->_tx_queue_n < kTxQueueCap) {
                _active->_tx_queue[_active->_tx_queue_n++] = it;
            } else {                                              // queue full -> can't requeue; give up loudly
                MR_TELEMETRY(
                    EventField gf[] = { { .key = "dst", .type = EventField::T::i64, .i = pt.dst },
                                        { .key = "ctr", .type = EventField::T::i64, .i = pt.ctr } };
                    _hal.emit("path_cascade_exhausted", gf, 2);
                    _hal.emit("rts_giveup", gf, 2); );
                { Push pu{}; pu.kind = PushKind::send_failed; pu.dst = pt.dst; pu.ctr = pt.ctr; enqueue_push(pu); }
            }
            _active->_pending_tx.reset();
            become_free();
        }
        return;
    }

    if (n.reason == protocol::nack_reason_hop_budget) {
        // TERMINAL (dv:10487): the route was longer than the budget assumed. Bump
        // rt[dst].hops UPWARD so a FUTURE send (new ctr) budgets correctly, then DROP
        // (no retry/cascade — a same-ctr retry would recompute the same too-small
        // budget). committed (the NACK payload high nibble) is a lower bound on the
        // true distance. This bump feeds do_data_tx's initial-budget computation.
        const uint8_t committed = static_cast<uint8_t>((n.payload >> 4) & 0x0f);
        RtEntry* e = rt_find(pt.dst);
        if (e != nullptr && e->n > 0) {
            const int want = (committed + 1 > e->candidates[0].hops) ? (committed + 1)
                                                                     : e->candidates[0].hops;
            const uint8_t new_hops = static_cast<uint8_t>(want > 15 ? 15 : want);   // 4-bit DV field clamp
            if (new_hops != e->candidates[0].hops) {
                e->candidates[0].hops = new_hops;
                MR_TELEMETRY(
                    EventField uf[] = { { .key = "dest", .type = EventField::T::i64, .i = pt.dst },
                                        { .key = "next", .type = EventField::T::i64, .i = e->candidates[0].next_hop },
                                        { .key = "hops", .type = EventField::T::i64, .i = new_hops },
                                        { .key = "slot", .type = EventField::T::str, .s = "hop_budget_nack" } };
                    _hal.emit("rt_update", uf, 4); );
            }
        }
        MR_TELEMETRY(
            EventField rf[] = { { .key = "from",      .type = EventField::T::i64, .i = pt.next },
                                { .key = "reason",    .type = EventField::T::i64, .i = protocol::nack_reason_hop_budget },
                                { .key = "committed", .type = EventField::T::i64, .i = committed } };
            _hal.emit("nack_rx", rf, 3); );
        MR_TELEMETRY(
            EventField gf[] = { { .key = "dst", .type = EventField::T::i64, .i = pt.dst },
                                { .key = "ctr", .type = EventField::T::i64, .i = pt.ctr } };
            _hal.emit("path_cascade_exhausted", gf, 2);
            _hal.emit("rts_giveup", gf, 2); );
        { Push pu{}; pu.kind = PushKind::send_failed; pu.dst = pt.dst; pu.ctr = pt.ctr; enqueue_push(pu); }
        _active->_pending_tx.reset();
        become_free();
        return;
    }

    if (n.reason == protocol::nack_reason_budget) {
        // R4.1 (Lua dv:10406-10453): the next hop refused on its own duty budget. Blind it for a
        // tier-scaled window + requeue the flight (the re-issue skips the now-blind hop via
        // pick_next_cascade_hop -> alt, or originator-defer / forwarder-drop). The blind window is
        // short-term "don't try right now"; the routing-grade persistent demotion is R4.2.
        const uint8_t tier = static_cast<uint8_t>((n.payload >> 4) & 0x0f);                // inline decode
        uint32_t blind_ms = protocol::budget_blind_critical_ms;                            // tier==CRITICAL default
        if      (tier >= static_cast<uint8_t>(BudgetTier::exhausted)) blind_ms = protocol::budget_blind_exhausted_ms;
        else if (tier <= static_cast<uint8_t>(BudgetTier::strained))  blind_ms = protocol::budget_blind_strained_ms;
        const uint64_t until = _hal.now() + blind_ms;                                      // max-merge (dv:10416-10422)
        auto bit = _active->_blind_until.find(pt.next);
        if (bit == _active->_blind_until.end() || until > bit->second) {
            _active->_blind_until[pt.next] = until;
            MR_TELEMETRY(
                EventField bf[] = { { .key = "next", .type = EventField::T::i64, .i = pt.next } };
                _hal.emit("blind_observed", bf, 1); );
        }
        // R4.2: record the persistent neighbor tier (routing-grade demotion beyond the blind window)
        // + rerank affected routes. local_only=false -> dirty + a triggered beacon if a primary moved.
        // Reads pt.next BEFORE try_cascade_requeue resets _active->_pending_tx.
        [[maybe_unused]] const int reranked = mark_neighbor_budget_tier(pt.next, tier, "nack_budget", /*local_only=*/false);
        MR_TELEMETRY(
            EventField rf[] = { { .key = "from",     .type = EventField::T::i64, .i = pt.next },
                                { .key = "reason",   .type = EventField::T::i64, .i = protocol::nack_reason_budget },
                                { .key = "tier",     .type = EventField::T::i64, .i = tier },
                                { .key = "reranked", .type = EventField::T::i64, .i = reranked } };
            _hal.emit("nack_rx", rf, 4); );
        // requeue-or-giveup: the helper does both legs (caps -> exhausted+giveup+drop, else
        // requeue@backoff) + _active->_pending_tx.reset() + become_free()/timer (dv:10449-10467). The caps
        // giveup event is "rts_giveup" (Lua dv:10462; "budget_low" is the trigger, not the name).
        try_cascade_requeue(pt, "rts_giveup");
        return;
    }

    // BUDGET tier > CRITICAL etc. all handled above. Any other (future) reason: defensive restore
    // of awaiting_cts + re-arm so an unexpected NACK doesn't strand the flight (timeouts cancelled above).
    pt.awaiting_cts = true;
    start_rts_timeout();
}

}  // namespace meshroute
