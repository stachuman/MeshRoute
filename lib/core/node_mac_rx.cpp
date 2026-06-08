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

namespace meshroute {

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
    // No data SF configured (empty sf_list) -> this node is data-incapable: it can't pick a DATA SF, so it does
    // NOT CTS / retune / arm NAV (no silent fallback). The sender's DM just fails — fail loud. Control plane
    // (neighbour-learn above, beacons, routing) still runs; only data participation is refused.
    if (_cfg.allowed_sf_bitmap == 0) return;
    // ROADMAP §3: an M_BROADCAST RTS is a fire-and-forget channel re-broadcast (no CTS). ANY node that hears
    // it and LACKS the msg (by the id low-16) retunes RX to the advertised SF to catch the DATA-M — not just
    // the addressed puller. The retune-back timer restores routing_sf. Holders + gateways skip. (dv:2081/9940.)
    if (r.m_broadcast) {
        if (!_cfg.is_gateway && !channel_have_id_lo16(r.m_payload_id_lo16)) {
            const uint8_t data_sf = select_data_sf(r.sf_index, protocol::db_to_q4(meta.snr_db));
            _hal.set_rx_sf(data_sf);
            // Stay on the data SF until the DATA-M lands: gap (RTS->DATA) + the FULL DATA-frame airtime
            // (r.payload_len is the inner+MAC; +13 covers the DATA header) + margin. Sizing only the inner
            // retunes back ~one header's airtime too early -> the DATA-M is dropped (drop_sf_mismatch).
            const uint32_t back = protocol::cts_to_data_gap_ms
                + airtime_ms(data_sf, _cfg.radio_bw_hz, _cfg.radio_cr, protocol::preamble_sym,
                             static_cast<uint16_t>(r.payload_len + 13)) + 30;
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
    auto la = _last_acked_from.find(lakey);
    if (la != _last_acked_from.end() && (_hal.now() - la->second.t_ms) < protocol::last_acked_ttl_ms) {
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
    if (_pending_rx && _pending_rx->from == r.src && _pending_rx->dst == r.dst &&
        _pending_rx->ctr_lo == r.ctr_lo) {
        cts_in cin{}; cin.chosen_data_sf = _pending_rx->chosen_data_sf;
        cin.already_received = false; cin.tx_id = _node_id; cin.rx_id = r.src;
        cin.payload_len = _cfg.nav_enabled ? r.payload_len : 0;   // NAV: size the overhearer's DATA reservation
        uint8_t cbuf[4]; const size_t cl = pack_cts(cin, std::span<uint8_t>(cbuf, sizeof cbuf));
        tx_with_retry(cbuf, cl, static_cast<int16_t>(_cfg.routing_sf), FrameTag::cts);   // R4.5b
        MR_TELEMETRY(
            EventField f[] = { { .key = "to", .type = EventField::T::i64, .i = r.src },
                               { .key = "dup", .type = EventField::T::boolean, .b = true } };
            _hal.emit("cts_tx", f, 2); );
        start_pending_rx_expiry(_pending_rx->payload_len);
        return;
    }
    // Busy with a DIFFERENT flight. If we hold a pending_rx (receiving someone else's
    // DATA), NACK the sender with how-long-busy so it waits/requeues instead of
    // grinding rts_timeout (dv:9934). If we hold a pending_tx (sending our own), STAY
    // SILENT (dv:9962 — the busy_for estimate lied for ACK-loss-stuck nodes).
    if (_pending_rx) {
        const uint64_t now = _hal.now();
        uint64_t busy_for = (_pending_rx->expiry_ms > now) ? (_pending_rx->expiry_ms - now) : 0;
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
    if (_pending_tx) {                                   // sending our own -> silent (no NACK)
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
    if (!(r.rts_flags & RTS_FLAG_RELAY)) {
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
    _pending_rx = prx;
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
        return;
    }
    if (!_pending_tx || !_pending_tx->awaiting_cts) return;   // ctr_lo flight-match dropped: rx_id==me + tx_id==next (below) pin the flight
    // Cascade disambiguation: the CTS now carries its sender (tx_id), so accept only the CTS from the
    // next-hop we RTS'd. Wire-backed (no PHY-sender god-view) — this is what distinguishes the primary
    // next-hop's CTS from an alt's when both answer the same RTS (cascade-to-alt). dv:10195.
    if (c.tx_id != _pending_tx->next) return;
    // Learn the CTS sender (= our next-hop) as a 1-hop neighbour (Lua learn_rx_source / cts_frame).
    if (learn_direct_neighbor(c.tx_id, protocol::db_to_q4(meta.snr_db), false)) schedule_triggered_beacon();
    _hal.cancel(kRtsTimeoutTimerId);                     // else it fires same-tick and burns a retry
    _hal.cancel(kRetryBackoffTimerId);                   // drop a stale retry armed by a just-fired rts_timeout
    _pending_tx->awaiting_cts = false;
    _pending_tx->chosen_data_sf = c.chosen_data_sf;
    MR_TELEMETRY(
        EventField f[] = { { .key = "from", .type = EventField::T::i64, .i = _pending_tx->next },   // CTS is from our next-hop (src_hint=-1 on metal)
                           { .key = "sf",   .type = EventField::T::i64, .i = c.chosen_data_sf } };
        _hal.emit("cts_rx", f, 2); );
    if (c.already_received) { _pending_tx.reset(); become_free(); return; }   // already delivered upstream
    (void)_hal.after(protocol::cts_to_data_gap_ms, kCtsToDataGapTimerId);     // fixed 5ms gap (NOT rand)
}

void Node::handle_data(const uint8_t* bytes, size_t len, const RxMeta& meta) {
    auto pd = parse_data(std::span<const uint8_t>(bytes, len));
    if (!pd) return;
    const data_out& d = *pd;
    // ROADMAP §3: a channel M-payload DATA is a 1-hop fire-and-forget broadcast — EVERY node that retuned to
    // the data SF (the overhear ARM) buffers it promiscuously, regardless of `next`, BEFORE the addressed-check
    // (dv:10942). No CTS/ACK/forward; the overhear retune-back timer restores routing_sf.
    if (d.payload_type_m) {
        const auto m_inner = data_inner(std::span<const uint8_t>(bytes, len), d);
        const auto m = parse_m_inner(m_inner);
        if (m) {
            const uint8_t from = (meta.src_hint >= 0) ? static_cast<uint8_t>(meta.src_hint) : 0xFF;
            ingest_channel_m(*m, d.next, d.dst, from);
        }
        return;
    }
    if (d.next != _node_id) return;
    if (!_pending_rx || _pending_rx->ctr_lo != d.ctr_lo4) return;
    // Inc 2 anti-spam: record this inbound DATA's airtime in the sender's window — the dominant
    // airtime a sender imposes on us (RTS-only never approached the cap). Keyed on _pending_rx->from
    // (== this hop's RTS src, so RTS+DATA accumulate in one entry; frame-derived, metal-correct) and
    // costed at the chosen data SF over the whole frame.
    track_originator_observation(_pending_rx->from, /*kind=data*/2, d.ctr_lo4,
        airtime_ms(_pending_rx->chosen_data_sf, _cfg.radio_bw_hz, _cfg.radio_cr,
                   protocol::preamble_sym, static_cast<uint16_t>(len)));
    int oa_app_; uint32_t orig_air; uint8_t oa_rts_, oa_cts_;   // sender's windowed airtime AFTER this DATA (calibration)
    compute_originator_metric(_pending_rx->from, oa_app_, orig_air, oa_rts_, oa_cts_);
    (void)oa_app_; (void)oa_rts_; (void)oa_cts_;
    // The DATA's link sender = whoever we CTS'd (_pending_rx->from, set in handle_rts).
    // src_hint is the SIM oracle (real LoRa carries no PHY source; the device sets -1),
    // so use it only when present, else fall back to our pending-RX contract — else
    // from=0xFF on metal -> the ACK + HOP_BUDGET/LOOP_DUP NACKs target node 255 and the
    // dedup/loop keys are corrupt, so the DM never completes.
    const uint8_t from = (meta.src_hint >= 0) ? static_cast<uint8_t>(meta.src_hint)
                                              : _pending_rx->from;
    // Parse the inner up-front so data_rx carries the (origin, ctr) message key — telemetry
    // parity with the Lua data_rx (dv:10911), which the analysis tools key delivery on. origin
    // is also needed below (BEFORE the ACK) so HOP_BUDGET/LOOP_DUP can NACK instead of re-ACKing.
    auto inner = data_inner(std::span<const uint8_t>(bytes, len), d);
    auto ui = parse_unicast_inner(inner);
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
    const uint8_t rx_sf = _pending_rx->chosen_data_sf;
    const uint8_t pl    = _pending_rx->payload_len;
    _hal.cancel(kPendingRxExpiryTimerId);
    _hal.set_rx_sf(_cfg.routing_sf);                     // receiver retunes back
    _pending_rx.reset();
    // last_acked cache: a retried RTS gets CTS already_received=1 instead of re-delivery.
    const uint32_t lakey = (uint32_t(from) << 24) | (uint32_t(d.dst) << 16) |
                           (uint32_t(d.ctr_lo4) << 8) | pl;
    const uint64_t nowm = _hal.now();
    for (auto it = _last_acked_from.begin(); it != _last_acked_from.end(); )   // prune expired (10s TTL)
        { if ((nowm - it->second.t_ms) >= protocol::last_acked_ttl_ms) it = _last_acked_from.erase(it); else ++it; }
    if (_last_acked_from.size() < protocol::cap_seen_origins)                  // bounded (reuse the 256 cap)
        _last_acked_from[lakey] = LastAcked{ rx_sf, nowm };
    // origin/inner parsed up-front (above) so data_rx carries the key; origin is read here
    // BEFORE the ACK so HOP_BUDGET/LOOP_DUP can NACK instead of re-ACKing.
    const uint32_t sokey = (uint32_t(origin) << 24) | (uint32_t(d.dst) << 16) | d.ctr;
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
        // prune-expired + cap, mirroring the pass-path write below.
        for (auto it = _seen_origins.begin(); it != _seen_origins.end(); )
            { if (it->second <= nowm) { _seen_origin_from.erase(it->first); it = _seen_origins.erase(it); } else ++it; }
        if (_seen_origins.size() < protocol::cap_seen_origins) {
            _seen_origins[sokey] = nowm + protocol::seen_origin_ttl_ms;
            _seen_origin_from[sokey] = from;
        }
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
    auto so = _seen_origins.find(sokey);
    const bool live_dup = (so != _seen_origins.end() && so->second > nowm);
    if (live_dup) {
        auto sof = _seen_origin_from.find(sokey);
        if (sof != _seen_origin_from.end() && sof->second != from) {
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
    for (auto it = _seen_origins.begin(); it != _seen_origins.end(); )              // prune expired (30s TTL)
        { if (it->second <= nowm) { _seen_origin_from.erase(it->first); it = _seen_origins.erase(it); } else ++it; }
    if (_seen_origins.size() < protocol::cap_seen_origins) {                        // bounded (256)
        _seen_origins[sokey] = nowm + protocol::seen_origin_ttl_ms;
        _seen_origin_from[sokey] = from;                                            // the prev-hop (LOOP_DUP discriminator)
    }
    // defer deliver/forward by the ACK airtime so it doesn't share a sim step with the ACK.
    _post_ack = PostAck{};
    _post_ack.pending = true; _post_ack.is_forward = (d.dst != _node_id);
    _post_ack.origin = origin; _post_ack.dst = d.dst; _post_ack.ctr_lo = d.ctr_lo4;
    _post_ack.ctr = d.ctr; _post_ack.flags = d.flags; _post_ack.previous_hop = from;
    _post_ack.inner_len = static_cast<uint8_t>(inner.size() <= protocol::max_payload_bytes_hard_cap
                                               ? inner.size() : protocol::max_payload_bytes_hard_cap);
    for (uint8_t i = 0; i < _post_ack.inner_len; ++i) _post_ack.inner[i] = inner[i];
    // Clamp the underflow: the exhaustion NACK that guarantees hb_new_remaining>=0 only fires for a FORWARD
    // (d.dst != self, line above) — the DELIVERY case (d.dst==self) is exempt, so a DM that arrived AT us with
    // hops_remaining==0 leaves hb_new_remaining==-1. That value is dead for a plain deliver, but an L2c
    // misdelivery re-forwards from the delivery case, so -1 -> uint8_t 255 (saturating to the 31 max) must not
    // leak into a budget. (L2c re-budgets its leg from rt anyway; this is belt-and-suspenders.)
    _post_ack.fwd_remaining = static_cast<uint8_t>(hb_new_remaining < 0 ? 0 : hb_new_remaining);
    _post_ack.fwd_committed = hb_new_committed;                         // carried into the forward TxItem
    (void)_hal.after(airtime_routing_ms(3) + 1, kPostAckTimerId);
}

void Node::do_post_ack() {
    if (!_post_ack.pending) return;
    const PostAck pa = _post_ack;
    _post_ack.pending = false;
    if (!pa.is_forward) {
        if (pa.inner_len >= 1 && (pa.inner[0] & PAYLOAD_FLAG_H_ANSWER)) {   // a hash-bind answer for us -> consume (routing info, NOT a DM)
            on_hash_bind_response(pa.inner, pa.inner_len);
            become_free();
            return;
        }
        if (pa.flags & DATA_FLAG_E2E_IS_ACK) {           // an end-to-end ACK for a DM we originated -> confirm, not deliver
            MR_TELEMETRY(
                const uint16_t acked = (pa.inner_len >= 4)
                                       ? static_cast<uint16_t>(pa.inner[2] | (pa.inner[3] << 8)) : 0;
                EventField ef[] = { { .key = "from", .type = EventField::T::i64, .i = pa.origin },
                                    { .key = "ctr",  .type = EventField::T::i64, .i = acked } };
                _hal.emit("e2e_ack_rx", ef, 2); );
            become_free();
            return;
        }
        // Parse the inner (handles the optional DST_HASH prefix) -> origin + body.
        auto ui = parse_unicast_inner(std::span<const uint8_t>(pa.inner, pa.inner_len));
        // L2c verify-on-delivery: DST_HASH present and naming a key that ISN'T ours => an id collision
        // misdelivered this DM. Heal the collision + redirect to the real owner; do NOT deliver locally.
        if (ui && ui->has_dst_hash && ui->dst_key_hash32 != _key_hash32) {
            l2c_handle_misdelivery(pa, ui->dst_key_hash32);     // forward to the real owner (identity-preserving)
            return;                                             // l2c re-kicks the queue itself (become_free)
        }
        // deliver: body from the parsed inner (legacy raw inner[2..] fallback if the inner didn't parse).
        char body[protocol::max_payload_bytes_hard_cap + 1];
        uint8_t blen;
        if (ui) { blen = static_cast<uint8_t>(ui->body.size());
                  for (uint8_t i = 0; i < blen; ++i) body[i] = static_cast<char>(ui->body[i]); }
        else    { blen = (pa.inner_len > 2) ? static_cast<uint8_t>(pa.inner_len - 2) : 0;
                  for (uint8_t i = 0; i < blen; ++i) body[i] = static_cast<char>(pa.inner[2 + i]); }
        body[blen] = '\0';
        MR_TELEMETRY(
            EventField f[] = {
                { .key = "origin",  .type = EventField::T::i64, .i = pa.origin },
                { .key = "dst",     .type = EventField::T::i64, .i = pa.dst },
                { .key = "ctr",     .type = EventField::T::i64, .i = pa.ctr },
                { .key = "payload", .type = EventField::T::str, .s = body },     // dm_delivery keys (dst, payload)
            };
            _hal.emit("delivered", f, 4); );
        Push pu{}; pu.kind = PushKind::msg_recv; pu.origin = pa.origin; pu.dst = pa.dst; pu.ctr = pa.ctr;
        pu.body_len = blen; for (uint8_t i = 0; i < blen; ++i) pu.body[i] = static_cast<uint8_t>(body[i]);
        enqueue_push(pu);                                // app channel: the inbound message
        // E2E ACK requested -> reply to the DM's origin with the acked ctr (routes home on the F reverse path).
        if (pa.flags & DATA_FLAG_E2E_ACK_REQ) send_e2e_ack(pa.origin, pa.ctr);
        become_free();
    } else {
        // C.2 cache-on-pass: a relayed hash-bind answer is cleartext -> snoop the binding before forwarding.
        if (pa.inner_len >= 1 && (pa.inner[0] & PAYLOAD_FLAG_H_ANSWER)) on_hash_bind_snoop(pa.inner, pa.inner_len);
        TxItem it{};
        it.origin = pa.origin; it.dst = pa.dst; it.ctr = pa.ctr; it.ctr_lo = pa.ctr_lo;
        it.flags = pa.flags; it.is_forward = true; it.previous_hop = pa.previous_hop;
        it.inner_len = pa.inner_len;
        for (uint8_t i = 0; i < pa.inner_len; ++i) it.inner[i] = pa.inner[i];
        it.fwd_remaining = pa.fwd_remaining; it.fwd_committed = pa.fwd_committed;   // carry the decremented budget
        it.enqueue_time_ms = _hal.now();                 // fresh hop attempt (dv:11391): the cascade-requeue
                                                         // total-age window starts when THIS hop accepts the
                                                         // forward — else it defaults 0 and the cap mis-fires.
        if (_tx_queue_n < kTxQueueCap) _tx_queue[_tx_queue_n++] = it;
        become_free();
    }
}

void Node::handle_ack(const uint8_t* bytes, size_t len, const RxMeta& meta) {
    auto pk = parse_ack(std::span<const uint8_t>(bytes, len));
    if (!pk) return;
    const ack_out& k = *pk;
    if (k.to != _node_id) return;
    if (!_pending_tx || !_pending_tx->awaiting_ack || _pending_tx->ctr_lo != k.ctr_lo) return;
    // src-less by design (see handle_cts): to+ctr_lo already identifies the ACK as our next-hop's. The
    // src_hint cross-check is SIM-ONLY, so gate it on availability rather than REJECTING when absent — the
    // old `src_hint < 0 ||` dropped EVERY ack on metal (device src_hint=-1), so the DM never completed.
    if (meta.src_hint >= 0 && static_cast<uint8_t>(meta.src_hint) != _pending_tx->next) return;  // (cf. NACK gate, Lua dv:10300)
    _hal.cancel(kAckTimeoutTimerId);
    _hal.cancel(kRetryBackoffTimerId);                   // drop a stale retry armed by a just-fired ack_timeout
    // Learn the ACK sender (= our next-hop) as a 1-hop neighbour (Lua learn_rx_source / ack_frame).
    if (learn_direct_neighbor(_pending_tx->next, protocol::db_to_q4(meta.snr_db), false)) schedule_triggered_beacon();
    // R4.2: consume the ACK's piggybacked budget_hint -> learn the next-hop's tier in the FORWARD
    // direction (the NACK only covers the reverse). local_only=true: rerank routes but DON'T dirty /
    // schedule a beacon (so NO triggered-beacon draw on the forward path). Lua dv:10341-10344.
    [[maybe_unused]] int ack_budget_reranked = 0;
    if (k.budget_hint > static_cast<uint8_t>(BudgetTier::healthy)) {
        const uint8_t tier = (k.budget_hint > static_cast<uint8_t>(BudgetTier::critical))
                             ? static_cast<uint8_t>(BudgetTier::critical) : k.budget_hint;
        // the ACK is from our next-hop (matched above) — use that, not src_hint (sim-only / -1 on device).
        ack_budget_reranked = mark_neighbor_budget_tier(_pending_tx->next,
                                                        tier, "ack_budget", /*local_only=*/true);
    }
    // Inc 3: a warn'd ACK means our next-hop considers us near its airtime cap. Honest back-off — park new
    // DM originations until the warn window expires (the hard receiver-side drop is the backstop). The
    // window self-clears: as our airtime ages out of the neighbour's window it stops setting the bit.
    if (k.warn) {
        _ack_warn_until = _hal.now() + protocol::originator_ack_warn_backoff_ms;
        MR_TELEMETRY(
            EventField wf[] = { { .key = "from",   .type = EventField::T::i64, .i = _pending_tx->next },
                                { .key = "ctr_lo", .type = EventField::T::i64, .i = k.ctr_lo },
                                { .key = "backoff_until_ms", .type = EventField::T::i64, .i = static_cast<int64_t>(_ack_warn_until) } };
            _hal.emit("originator_warned_by_ack", wf, 3); );
    }
    MR_TELEMETRY(
        EventField f[] = { { .key = "from",     .type = EventField::T::i64, .i = _pending_tx->next },   // ACK is from our next-hop (src_hint=-1 on metal)
                           { .key = "origin",   .type = EventField::T::i64, .i = _pending_tx->origin },
                           { .key = "dst",      .type = EventField::T::i64, .i = _pending_tx->dst },
                           { .key = "ctr",      .type = EventField::T::i64, .i = _pending_tx->ctr },
                           { .key = "budget_hint",     .type = EventField::T::i64, .i = k.budget_hint },
                           { .key = "budget_reranked", .type = EventField::T::i64, .i = ack_budget_reranked },
                           { .key = "airtime_warn",    .type = EventField::T::i64, .i = k.warn ? 1 : 0 } };
        _hal.emit("ack_rx", f, 7); );
    { Push pu{}; pu.kind = PushKind::send_acked; pu.dst = _pending_tx->dst; pu.ctr = _pending_tx->ctr; enqueue_push(pu); }
    _pending_tx.reset();
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
    if (!_pending_tx) return;                                       // no flight to react on
    if (_pending_tx->ctr_lo != n.ctr_lo) return;                    // stale (different flight)
    if (meta.src_hint >= 0 && static_cast<uint8_t>(meta.src_hint) != _pending_tx->next) {
        MR_TELEMETRY(
            EventField f[] = { { .key = "from", .type = EventField::T::i64, .i = static_cast<uint8_t>(meta.src_hint) } };
            _hal.emit("nack_drop_unexpected_src", f, 1); );
        return;
    }
    // Learn the NACK sender (= our next-hop) as a 1-hop neighbour (Lua learn_rx_source / nack_frame).
    if (learn_direct_neighbor(_pending_tx->next, protocol::db_to_q4(meta.snr_db), false)) schedule_triggered_beacon();
    _hal.cancel(kRtsTimeoutTimerId);                                // faster than the timeout (dv:10390)
    _hal.cancel(kAckTimeoutTimerId);
    _pending_tx->awaiting_cts = false; _pending_tx->awaiting_ack = false;
    PendingTx& pt = *_pending_tx;

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
            tx_rts_retry();
        } else {                                                  // LOOP_DUP miss -> DIRECT giveup (NOT requeue, dv:10588)
            MR_TELEMETRY(
                EventField f[] = { { .key = "dst", .type = EventField::T::i64, .i = pt.dst },
                                   { .key = "ctr", .type = EventField::T::i64, .i = pt.ctr } };
                _hal.emit("path_cascade_exhausted", f, 2);
                _hal.emit("rts_giveup", f, 2); );
            { Push pu{}; pu.kind = PushKind::send_failed; pu.dst = pt.dst; pu.ctr = pt.ctr; enqueue_push(pu); }
            _pending_tx.reset();
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
            auto bit = _blind_until.find(pt.next);
            _blind_until[pt.next] = (bit != _blind_until.end() && bit->second > until) ? bit->second : until;
            MR_TELEMETRY(
                EventField bf[] = { { .key = "next", .type = EventField::T::i64, .i = pt.next } };
                _hal.emit("blind_observed", bf, 1); );
        }
        if (busy_for <= protocol::nack_wait_threshold_ms) {        // short busy -> wait SAME hop
            const int jit = _hal.rand_range(0, static_cast<int>(retry_jitter_ms()) + 1);   // N1 (the only new draw)
            const uint32_t wait = static_cast<uint32_t>(busy_for) + 1 + static_cast<uint32_t>(jit);
            _nack_wait_ctr_lo = pt.ctr_lo; _nack_wait_pending = true;
            (void)_hal.after(wait, kNackWaitTimerId);
        } else {                                                  // long busy -> requeue SAME hop (verbatim meta)
            TxItem it{};
            it.origin = pt.origin; it.dst = pt.dst; it.ctr = pt.ctr; it.ctr_lo = pt.ctr_lo; it.flags = pt.flags;
            it.inner_len = pt.inner_len;
            for (uint8_t i = 0; i < pt.inner_len; ++i) it.inner[i] = pt.inner[i];
            it.is_forward = pt.has_previous_hop; it.previous_hop = pt.previous_hop;
            it.fwd_remaining = pt.fwd_remaining; it.fwd_committed = pt.fwd_committed;   // carry hop budget (forwarder)
            it.requeue_count = pt.requeue_count; it.enqueue_time_ms = pt.enqueue_time_ms;   // VERBATIM (no ++/backoff)
            it.next_attempt_ms = 0;
            MR_TELEMETRY(
                EventField tf[] = { { .key = "dst", .type = EventField::T::i64, .i = pt.dst },
                                    { .key = "ctr", .type = EventField::T::i64, .i = pt.ctr } };
                _hal.emit("tx_requeued", tf, 2); );
            if (_tx_queue_n < kTxQueueCap) {
                _tx_queue[_tx_queue_n++] = it;
            } else {                                              // queue full -> can't requeue; give up loudly
                MR_TELEMETRY(
                    EventField gf[] = { { .key = "dst", .type = EventField::T::i64, .i = pt.dst },
                                        { .key = "ctr", .type = EventField::T::i64, .i = pt.ctr } };
                    _hal.emit("path_cascade_exhausted", gf, 2);
                    _hal.emit("rts_giveup", gf, 2); );
                { Push pu{}; pu.kind = PushKind::send_failed; pu.dst = pt.dst; pu.ctr = pt.ctr; enqueue_push(pu); }
            }
            _pending_tx.reset();
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
        _pending_tx.reset();
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
        auto bit = _blind_until.find(pt.next);
        if (bit == _blind_until.end() || until > bit->second) {
            _blind_until[pt.next] = until;
            MR_TELEMETRY(
                EventField bf[] = { { .key = "next", .type = EventField::T::i64, .i = pt.next } };
                _hal.emit("blind_observed", bf, 1); );
        }
        // R4.2: record the persistent neighbor tier (routing-grade demotion beyond the blind window)
        // + rerank affected routes. local_only=false -> dirty + a triggered beacon if a primary moved.
        // Reads pt.next BEFORE try_cascade_requeue resets _pending_tx.
        [[maybe_unused]] const int reranked = mark_neighbor_budget_tier(pt.next, tier, "nack_budget", /*local_only=*/false);
        MR_TELEMETRY(
            EventField rf[] = { { .key = "from",     .type = EventField::T::i64, .i = pt.next },
                                { .key = "reason",   .type = EventField::T::i64, .i = protocol::nack_reason_budget },
                                { .key = "tier",     .type = EventField::T::i64, .i = tier },
                                { .key = "reranked", .type = EventField::T::i64, .i = reranked } };
            _hal.emit("nack_rx", rf, 4); );
        // requeue-or-giveup: the helper does both legs (caps -> exhausted+giveup+drop, else
        // requeue@backoff) + _pending_tx.reset() + become_free()/timer (dv:10449-10467). The caps
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
