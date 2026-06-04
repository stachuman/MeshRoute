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

namespace meshroute {

uint16_t Node::next_ctr(uint8_t dst) {
    uint16_t& c = _peer_send_counter[dst];
    c = (c >= 65535) ? 1 : static_cast<uint16_t>(c + 1);   // wraps 65535->1 (NOT a rand site)
    return c;
}

uint8_t Node::select_data_sf(uint8_t rts_sf_index, int16_t rx_snr_q4) const {
    // Adaptive DATA-SF: resolve the requester's sf_index to a candidate SF set, then pick the fastest
    // SF the link SNR supports (Lua sf_index_to_bitmap :3027 + select_data_sf :3043). ANY(3) -> our full
    // allowed_sf_bitmap; pinned 0..2 -> that singleton (M-broadcast / forced SF). allowed_sf_bitmap==0
    // means "unconfigured" -> the single preferred data_sf (legacy single-SF nodes). PURE (no rand).
    uint16_t bitmap = _cfg.allowed_sf_bitmap;
    if (bitmap == 0) bitmap = static_cast<uint16_t>(1u << _cfg.data_sf);
    if (rts_sf_index != 3 /*ANY*/) {                          // pinned: the index-th allowed SF
        uint16_t pin = 0; uint8_t seen = 0;
        for (uint8_t sf = 5; sf <= 12; ++sf)
            if (bitmap & (1u << sf)) { if (seen++ == rts_sf_index) { pin = static_cast<uint16_t>(1u << sf); break; } }
        if (pin) bitmap = pin;                                // out-of-range index -> keep full set (Lua :3035)
    }
    const uint8_t sf = protocol::select_data_sf_for_snr(rx_snr_q4, bitmap, protocol::sf_margin_q4);
    return (sf != 0) ? sf : _cfg.data_sf;
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
uint16_t Node::enqueue_data(uint8_t dst, const uint8_t* body, uint8_t body_len, uint8_t flags, [[maybe_unused]] const char* tx_event) {
    const uint16_t ctr = next_ctr(dst);
    TxItem item{};
    item.origin = _node_id; item.dst = dst; item.ctr = ctr; item.ctr_lo = static_cast<uint8_t>(ctr & 0x0F);
    item.flags = flags;
    item.inner[0] = 0x00; item.inner[1] = _node_id;      // payload-flags=0 (plaintext DM, no H_ANSWER) | origin | body
    if (body) for (uint8_t i = 0; i < body_len; ++i) item.inner[2 + i] = body[i];
    item.inner_len = static_cast<uint8_t>(2 + body_len);
    item.enqueue_time_ms = _hal.now();                   // first-enqueue time (cascade-requeue total-age cap)
    // Inc 3 back-off: a warn'd ACK (a downstream neighbour says we're near its airtime cap) parks new DM
    // originations until the warn window expires, relieving that neighbour. The hard receiver-side airtime
    // drop is the backstop; this is the polite sender-side half. next_attempt_ms gates the dequeue.
    if (_ack_warn_until > _hal.now()) {
        item.next_attempt_ms = _ack_warn_until;
        MR_TELEMETRY(
        EventField bf[] = { { .key = "origin", .type = EventField::T::i64, .i = item.origin },
                            { .key = "dst",    .type = EventField::T::i64, .i = item.dst },
                            { .key = "ctr",    .type = EventField::T::i64, .i = item.ctr },
                            { .key = "until_ms", .type = EventField::T::i64, .i = static_cast<int64_t>(_ack_warn_until) } };
        _hal.emit("origination_backoff_ack_warn", bf, 4); );
    }
    if (_tx_queue_n < kTxQueueCap) _tx_queue[_tx_queue_n++] = item;
    MR_TELEMETRY(
    EventField f[] = {
        { .key = "origin", .type = EventField::T::i64, .i = item.origin },
        { .key = "dst",    .type = EventField::T::i64, .i = item.dst },
        { .key = "ctr",    .type = EventField::T::i64, .i = item.ctr },
        { .key = "depth",  .type = EventField::T::i64, .i = _tx_queue_n },
    };
    _hal.emit(tx_event, f, 4); );
    become_free();
    return ctr;
}

// E2E/PRIORITY ride the wire via `flags`; the E2E ACK behaviour lives in do_post_ack + send_e2e_ack.
uint16_t Node::do_send(uint8_t dst, const uint8_t* body, uint8_t body_len, uint8_t flags) {
    return enqueue_data(dst, body, body_len, flags, "tx_enqueue");   // app DM (dm_delivery record key)
}

// End-to-end ACK: a tiny DATA back to the DM's origin carrying the acked ctr (E2E_IS_ACK). Emits
// e2e_ack_tx (NOT tx_enqueue) so dm_delivery doesn't miscount the ack as an app DM; it routes home
// on the reverse path the F discovery already laid toward the origin.
void Node::send_e2e_ack(uint8_t to_origin, uint16_t acked_ctr) {
    const uint8_t body[2] = { static_cast<uint8_t>(acked_ctr & 0xFF), static_cast<uint8_t>(acked_ctr >> 8) };
    (void)enqueue_data(to_origin, body, 2, DATA_FLAG_E2E_IS_ACK, "e2e_ack_tx");
}

void Node::become_free() {
    if (_pending_tx || _pending_rx) return;              // half-duplex serialize
    if (_tx_queue_n == 0) return;
    // Drain the FIRST item whose backoff has elapsed (next_attempt_ms <= now). The
    // Lua scans (not head-only) so a fresh send isn't blocked behind a backing-off
    // cascade-requeue, and the next_attempt_ms gate means a concurrent become_free
    // can't skip a requeue's backoff. Items with next_attempt_ms==0 are always ready,
    // so a queue without requeues behaves as plain FIFO.
    const uint64_t now = _hal.now();
    uint8_t  pick = _tx_queue_n;                          // sentinel = none ready
    uint64_t soonest = UINT64_MAX;
    for (uint8_t i = 0; i < _tx_queue_n; ++i) {
        if (_tx_queue[i].next_attempt_ms <= now) { pick = i; break; }
        if (_tx_queue[i].next_attempt_ms < soonest) soonest = _tx_queue[i].next_attempt_ms;
    }
    if (pick == _tx_queue_n) {                            // none ready -> wake at the soonest backoff
        (void)_hal.after(static_cast<uint32_t>(soonest - now), kQueueWakeupTimerId);
        return;
    }
    // Inc 4 self-cap (enforcing): cap our OWN DM originations per window. Checked here at transmit time —
    // become_free serializes sends, so the count never exceeds the cap (no enqueue-race overshoot). Exempt:
    // forwards (is_forward — we stay a good relay) and channel broadcasts (M_BROADCAST, when R5 lands —
    // governed by the per-origin channel COUNT plane). Defer-in-place (bump next_attempt_ms) until the
    // oldest origination ages out, then re-pick; the receiver-side airtime backstop is the hard floor.
    if (_tx_queue[pick].origin == _node_id && !_tx_queue[pick].is_forward
        && !(_tx_queue[pick].flags & DATA_FLAG_PAYLOAD_TYPE_M)) {
        uint64_t oldest = now;
        const uint8_t own = self_originate_count(&oldest);
        if (own >= _cfg.originator_self_cap_per_window) {
            uint64_t until = oldest + protocol::originator_window_ms;
            if (until <= now) until = now + 1;
            _tx_queue[pick].next_attempt_ms = until;          // defer in place
            MR_TELEMETRY(
            EventField f[] = { { .key = "origin",    .type = EventField::T::i64, .i = _tx_queue[pick].origin },
                               { .key = "dst",       .type = EventField::T::i64, .i = _tx_queue[pick].dst },
                               { .key = "ctr",       .type = EventField::T::i64, .i = _tx_queue[pick].ctr },
                               { .key = "own_count", .type = EventField::T::i64, .i = own },
                               { .key = "cap",       .type = EventField::T::i64, .i = _cfg.originator_self_cap_per_window },
                               { .key = "until_ms",  .type = EventField::T::i64, .i = static_cast<int64_t>(until) } };
            _hal.emit("originator_self_defer", f, 6); );
            become_free();                                    // re-pick (skips the now-deferred item)
            return;
        }
        self_originate_observe();                             // admitted -> record
    }
    TxItem item = _tx_queue[pick];
    for (uint8_t i = pick + 1; i < _tx_queue_n; ++i) _tx_queue[i - 1] = _tx_queue[i];
    --_tx_queue_n;
    issue_send(item);
}

// ---- M-broadcast (channel gossip) fire-and-forget tx (dv:6997/7044/7389) ----------------------
// chosen_data_sf = max(allowed_sf_bitmap) — largest SF = most robust = most receivers decode it.
uint8_t Node::max_data_sf() const {
    const uint16_t bitmap = _cfg.allowed_sf_bitmap;
    if (bitmap == 0) return _cfg.data_sf;                        // no allowed set -> the preferred data SF
    for (uint8_t sf = 12; sf >= 5; --sf) if (bitmap & (1u << sf)) return sf;
    return _cfg.data_sf;
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
// Set up the fire-and-forget flight on the just-installed _pending_tx, then fire the RTS. No CTS wait
// (the SF is in the RTS), no ACK wait (failures recover via the next BCN-digest cascade).
void Node::issue_m_broadcast() {
    if (!_pending_tx) return;
    PendingTx& pt = *_pending_tx;
    pt.m_broadcast = true; pt.awaiting_cts = false; pt.awaiting_ack = false;
    pt.chosen_data_sf = max_data_sf();                          // sender picks the SF; advertised in the RTS
    tx_m_broadcast_rts();
}
// Pack + TX the M_BROADCAST RTS (sf_index pinned to the max SF + the channel_msg_id low-16), then arm
// the RTS->DATA gap directly (kCtsToDataGapTimerId -> do_data_tx) — skipping the CTS round-trip.
void Node::tx_m_broadcast_rts() {
    if (!_pending_tx) return;
    PendingTx& pt = *_pending_tx;
    rts_in rin{};
    rin.leaf_id = _cfg.leaf_id; rin.src = _node_id; rin.next = pt.next; rin.ctr_lo = pt.ctr_lo;
    rin.dst = pt.dst; rin.sf_index = max_data_sf_index(); rin.rts_flags = RTS_FLAG_M_BROADCAST;
    rin.payload_len = static_cast<uint8_t>(pt.inner_len + 4 /*MAC_LEN*/);
    rin.m_payload_id_lo16 = static_cast<uint16_t>((pt.inner[2] << 8) | pt.inner[3]);   // low-16 of the BE id
    uint8_t buf[11];                                            // RTS(8) + id_lo16(2)
    const size_t l = pack_rts(rin, std::span<uint8_t>(buf, sizeof(buf)));
    if (l == 0) { _hal.log("M-broadcast RTS pack failed"); return; }
    MR_TELEMETRY(
    EventField f[] = { { .key = "dst",  .type = EventField::T::i64, .i = pt.dst },
                       { .key = "next", .type = EventField::T::i64, .i = pt.next },
                       { .key = "ctr",  .type = EventField::T::i64, .i = pt.ctr } };
    _hal.emit("rts_tx", f, 3); );
    tx_initiating(buf, l, static_cast<int16_t>(_cfg.routing_sf), LbtKind::rts, pt.flight_gen);
    // The RTS->DATA gap is armed in start_rts_timeout (the actual-TX hand-off, after any LBT/duty defer = the
    // Lua's on_handed, dv:7032) — NOT here at issue. Arming at issue desyncs the DATA when the RTS is deferred:
    // it fires while the RTS is still on air (self_tx_in_flight -> data_tx_blocked) and BEFORE overhearers
    // receive the full RTS + retune to the data SF, so they're never on the SF when the DATA-M lands.
}

void Node::issue_send(const TxItem& item) {
    // A new flight is going live -> drop any stale BUSY_RX nack-wait left armed for a
    // torn-down prior flight, so its timer can't spuriously re-RTS this one on a 4-bit
    // ctr_lo collision (issue_send is the only choke point that installs a new _pending_tx).
    clear_nack_wait();
    // Build the flight first so pick_next_cascade_hop sees previous_hop (forwarders
    // must not loop back upstream) + the empty alts_tried set.
    PendingTx pt{};
    pt.origin = item.origin; pt.dst = item.dst;
    pt.ctr_lo = item.ctr_lo; pt.ctr = item.ctr; pt.flags = item.flags;
    pt.inner_len = item.inner_len;
    for (uint8_t i = 0; i < item.inner_len; ++i) pt.inner[i] = item.inner[i];
    pt.chosen_data_sf = 0; pt.retries_left = effective_rts_max_retries(item.requeue_count);
    pt.awaiting_cts = true; pt.awaiting_ack = false;
    pt.alts_tried_n = 0;
    pt.previous_hop = item.previous_hop; pt.has_previous_hop = item.is_forward;
    pt.requeue_count = item.requeue_count; pt.enqueue_time_ms = item.enqueue_time_ms;
    pt.fwd_remaining = item.fwd_remaining; pt.fwd_committed = item.fwd_committed;   // hop budget (forwarder)

    const uint8_t first = pick_next_cascade_hop(pt);     // first SELECTABLE candidate (skips previous_hop)
    if (first == 0) {
        // No usable route yet. A FORWARDER drops (dv_dual_sf.lua:7041-7048 — it
        // can't hold someone else's transit); an ORIGINATOR defers the message
        // until a beacon installs a route or the defer-TTL expires (dv:7049-7052).
        if (item.is_forward) {
            MR_TELEMETRY(
            EventField f[] = { { .key = "dst", .type = EventField::T::i64, .i = item.dst } };
            _hal.emit("send_no_route", f, 1); );
        } else {
            defer_send(item);
        }
        return;
    }
    pt.next = first;
    pt.flight_gen = ++_flight_gen;                       // #A redo: a NEW flight identity (cascade_to_alt keeps it; a requeue re-installs here -> new gen)
    _pending_tx = pt;
    if (item.flags & DATA_FLAG_PAYLOAD_TYPE_M) { issue_m_broadcast(); return; }   // channel gossip: fire-and-forget broadcast
    tx_rts_retry();                                      // packs+emits the RTS + start_rts_timeout
}

void Node::tx_rts_retry() {
    if (!_pending_tx) return;
    PendingTx& pt = *_pending_tx;
    pt.awaiting_cts = true; pt.awaiting_ack = false; pt.chosen_data_sf = 0;
    rts_in rin{};
    rin.leaf_id = _cfg.leaf_id; rin.src = _node_id; rin.next = pt.next; rin.ctr_lo = pt.ctr_lo;
    rin.dst = pt.dst; rin.sf_index = 3 /*ANY*/; rin.rts_flags = 0;   // DM RTS; the M-broadcast RTS is tx_m_broadcast_rts
    rin.payload_len = static_cast<uint8_t>(pt.inner_len + 4 /*MAC_LEN*/); rin.m_payload_id_lo16 = 0;
    uint8_t buf[9];
    const size_t l = pack_rts(rin, std::span<uint8_t>(buf, sizeof(buf)));
    if (l == 0) { _hal.log("RTS pack failed"); return; }
    MR_TELEMETRY(
    EventField f[] = {
        { .key = "dst",  .type = EventField::T::i64, .i = pt.dst },
        { .key = "next", .type = EventField::T::i64, .i = pt.next },
        { .key = "ctr",  .type = EventField::T::i64, .i = pt.ctr },
    };
    _hal.emit("rts_tx", f, 3); );                        // emit at the call site (before the LBT defer, dv-faithful)
    // R4.5: the actual TX + start_rts_timeout go through the LBT wrapper (defer if the channel is busy). RX stays
    // on routing_sf. lbt_enabled=false (every gate) -> straight TX + timeout, byte-identical.
    tx_initiating(buf, l, static_cast<int16_t>(_cfg.routing_sf), LbtKind::rts, pt.flight_gen);
}

// R4.5 LBT: hand an INITIATING frame to the radio, but if the channel is busy (and lbt_enabled) defer the real
// TX past busy_until + rand(0,lbt_backoff+1) — the ONE LBT draw, ONLY when busy (dv:3693-3706). lbt_enabled=false
// (every gate) -> straight to lbt_complete -> byte-identical, NO draw.
void Node::tx_initiating(const uint8_t* bytes, size_t len, int16_t sf, LbtKind kind, uint32_t rts_flight_gen) {
    if (_cfg.lbt_enabled) {
        const uint64_t now = _hal.now();
        const uint64_t busy_until = _hal.channel_busy_until();
        if (busy_until > now) {
            const uint32_t wait  = static_cast<uint32_t>(busy_until - now);
            const uint32_t delay = wait + static_cast<uint32_t>(_hal.rand_range(0, static_cast<int>(_lbt_backoff_ms) + 1));
            MR_TELEMETRY(
            EventField f[] = { { .key = "kind",          .type = EventField::T::str, .s = "initiating" },
                               { .key = "defer_ms",      .type = EventField::T::i64, .i = delay },
                               { .key = "busy_until_ms", .type = EventField::T::i64, .i = static_cast<int64_t>(busy_until) } };
            _hal.emit("tx_lbt_defer", f, 3); );
            schedule_lbt_defer(bytes, len, sf, kind, rts_flight_gen, delay);
            return;
        }
    }
    lbt_complete(bytes, len, sf, kind, rts_flight_gen);
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
    MR_TELEMETRY(
    EventField f[] = { { .key = "kind", .type = EventField::T::i64, .i = static_cast<uint8_t>(kind) } };
    _hal.emit("tx_lbt_defer_dropped", f, 1); );                         // ring full -> drop loudly
    return false;
}

// The actual TX (immediate clear-channel path OR the kLbtDeferTimerId re-fire). RTS: the flight-gen staleness check
// (cancel a stale deferred RTS, dv:3708/3712) + the #A duty pre-check (defer over-budget) + start_rts_timeout (the
// after_tx). NACK/flood: just TX.
void Node::lbt_complete(const uint8_t* bytes, size_t len, int16_t sf, LbtKind kind, uint32_t rts_flight_gen) {
    if (kind == LbtKind::rts) {
        if (!_pending_tx || _pending_tx->flight_gen != rts_flight_gen) {  // flight changed while we waited (flight_gen = object-identity, not the 4-bit ctr_lo)
            MR_TELEMETRY(
            EventField f[] = { { .key = "reason", .type = EventField::T::str, .s = "pending_tx_changed" } };
            _hal.emit("rts_tx_cancelled_stale", f, 1); );
            return;
        }
        // Cleanup #A (redo): duty pre-check the RTS (the #2 slot<0 residual). Over budget -> defer in the DEDICATED
        // _rts_duty_defer slot (NOT the shared LBT ring — that reuse was net-worse, review wgvbtirmu) + arm the
        // re-check timer + return (NOT handed; start_rts_timeout armed on the eventual send by rts_duty_defer_fire).
        // The ~1h wait is SAFE now: flight_gen staleness is exact. Draw-free; gate-inert (healthy duty never defers).
        uint32_t wait = 0;
        if (duty_over_budget(len, sf, &wait)) {
            MR_TELEMETRY(
            EventField f[] = { { .key = "label",   .type = EventField::T::str, .s = "RTS" },
                               { .key = "wait_ms", .type = EventField::T::i64, .i = static_cast<int64_t>(wait) },
                               { .key = "source",  .type = EventField::T::str, .s = "lbt_complete" } };
            _hal.emit("duty_cycle_blocked", f, 3); );
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
    if (!_pending_tx || _pending_tx->flight_gen != d.flight_gen) {       // the flight this RTS belonged to is gone
        d.pending = false;
        MR_TELEMETRY(
        EventField f[] = { { .key = "reason", .type = EventField::T::str, .s = "pending_tx_changed" } };
        _hal.emit("rts_tx_cancelled_stale", f, 1); );
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
            MR_TELEMETRY(
            EventField f[] = { { .key = "airtime_ms", .type = EventField::T::i64, .i = static_cast<int64_t>(airtime) },
                               { .key = "source",     .type = EventField::T::str, .s = "tx_flood" } };
            _hal.emit("duty_cycle_blocked", f, 2); );
            return false;
        }
    }
    if (_cfg.lbt_enabled) {
        const uint64_t now = _hal.now();
        const uint64_t busy_until = _hal.channel_busy_until();
        const int64_t  wait = static_cast<int64_t>(busy_until) - static_cast<int64_t>(now);
        if (wait > static_cast<int64_t>(_flood_lbt_max_defer_ms)) {    // busy too long -> drop the page (dv:3796)
            MR_TELEMETRY(
            EventField f[] = { { .key = "busy_for_ms", .type = EventField::T::i64, .i = wait } };
            _hal.emit("tx_flood_skipped", f, 1); );
            return false;
        }
        if (wait > 0) {
            const uint32_t delay = static_cast<uint32_t>(wait) +
                                   static_cast<uint32_t>(_hal.rand_range(0, static_cast<int>(_lbt_backoff_ms) + 1));
            MR_TELEMETRY(
            EventField f[] = { { .key = "kind",          .type = EventField::T::str, .s = "flood" },
                               { .key = "defer_ms",      .type = EventField::T::i64, .i = delay },
                               { .key = "busy_until_ms", .type = EventField::T::i64, .i = static_cast<int64_t>(busy_until) } };
            _hal.emit("tx_lbt_defer", f, 3); );
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
        s.ctr_lo = _pending_tx ? _pending_tx->ctr_lo : 0;   // READ only for the DATA slot (retry_stashed re-arm guard); for CTS/ACK/NACK this records the forwarder's OWN outbound flight + is never consulted
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
        MR_TELEMETRY(
        EventField f[] = { { .key = "label",   .type = EventField::T::str, .s = label_of_frame(tag) },
                           { .key = "wait_ms", .type = EventField::T::i64, .i = static_cast<int64_t>(wait) },
                           { .key = "source",  .type = EventField::T::str, .s = "tx_with_retry" } };
        _hal.emit("duty_cycle_blocked", f, 3); );
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
    // _pending_tx), do NOT re-transmit the stale DATA / re-stash with a mismatched ctr_lo. Mirrors retry_stashed +
    // the Lua m_broadcast retry guard (dv:12172). CTS/ACK/NACK are idempotent responses -> no flight guard.
    if (tag == FrameTag::data && (!_pending_tx || _pending_tx->ctr_lo != s.ctr_lo)) return;
    const bool handed = tx_with_retry(s.buf, s.len, s.sf, tag);       // re-runs the duty pre-check (re-defers if still over budget)
    // DATA re-hand: re-arm the ACK wait do_data_tx skipped at defer-time (the DATA now hit the air). Anchored to the
    // actual send time, matching the Lua deferred re-run replaying on_handed (dv:3633 -> 3637 -> 10274-10278).
    if (handed && tag == FrameTag::data && _pending_tx && _pending_tx->ctr_lo == s.ctr_lo) {
        _pending_tx->awaiting_ack = true;
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
    // DATA re-issue: re-arm the ACK wait the on_radio_busy block cleared, exactly as the Lua DATA on_handed does
    // (dv:10270-10278) — fires on the initial tx AND on the stash retry. Without this the re-sent DATA flies but the
    // sender stays !awaiting_ack with no ack-timeout, so the returning ACK is dropped + the flight never recovers.
    // Guarded on the pending flight (ctr_lo) so a retry against a since-replaced flight does NOT re-arm.
    if (tag == FrameTag::data && _pending_tx && _pending_tx->ctr_lo == s.ctr_lo) {
        _pending_tx->awaiting_ack = true;
        start_ack_timeout();
    }
}

void Node::do_data_tx() {
    if (!_pending_tx || _pending_tx->awaiting_ack || _pending_tx->chosen_data_sf == 0) return;
    PendingTx& pt = *_pending_tx;
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
    din.addr_len = 0; din.flags = pt.flags; din.next = pt.next; din.dst = pt.dst;
    din.hops_remaining = hb_remaining; din.committed_hops = hb_committed;
    din.prev_fwd_rt_hops = hb_prev_fwd; din.ctr = pt.ctr;
    din.visited = {};                                    // empty -> 6 zero bytes
    din.inner = std::span<const uint8_t>(pt.inner, pt.inner_len);
    din.mac   = std::span<const uint8_t>(mac, 4);
    uint8_t buf[protocol::lora_max_frame_bytes];
    const size_t dlen = pack_data(din, std::span<uint8_t>(buf, sizeof(buf)));
    if (dlen == 0) { _hal.log("DATA pack failed"); return; }
    const bool handed = tx_with_retry(buf, dlen, static_cast<int16_t>(pt.chosen_data_sf), FrameTag::data);   // R4.5b stash; #2 may duty-defer
    MR_TELEMETRY(
    EventField f[] = {
        { .key = "dst",         .type = EventField::T::i64,  .i = pt.dst },
        { .key = "next",        .type = EventField::T::i64,  .i = pt.next },
        { .key = "ctr",         .type = EventField::T::i64,  .i = pt.ctr },
        { .key = "sf",          .type = EventField::T::i64,  .i = pt.chosen_data_sf },     // the SF this DATA went out on (M-broadcast pins max_data_sf; t69)
        { .key = "m_broadcast", .type = EventField::T::boolean, .b = pt.m_broadcast },     // channel-gossip fire-and-forget broadcast (no ACK)
    };
    _hal.emit("data_tx", f, 5); );                                    // emitted regardless (Lua emits it before tx_with_retry, dv:10251)
    // Arm the ACK wait ONLY if the DATA actually hit the air — mirrors the Lua DATA on_handed (dv:10270-10279, fires only
    // on real self:tx) + the not-handed clear (dv:10281-10283). #2's duty defer returns handed=false: arming a short
    // ack-timeout on an un-sent DATA would fire before the (long) duty wait, draw a rand + tear the flight down.
    if (handed) {
        if (pt.m_broadcast) {                                            // fire-and-forget: no ACK; clear after the DATA airtime
            const uint32_t data_air = airtime_ms(pt.chosen_data_sf, _cfg.radio_bw_hz, _cfg.radio_cr,
                                                 protocol::preamble_sym, static_cast<uint16_t>(dlen));
            (void)_hal.after(data_air + 5, kMBcastClearTimerId);
        } else { pt.awaiting_ack = true; start_ack_timeout(); }
    } else { pt.awaiting_ack = false; }
}

void Node::start_rts_timeout() {
    // M-broadcast (fire-and-forget): there is no CTS to wait for. start_rts_timeout is called at the RTS
    // hand-off in EVERY path (lbt_complete:327 + rts_duty_defer_fire:351) = the Lua's on_handed (dv:7032),
    // so anchor the RTS->DATA gap to the ACTUAL TX here. The DATA then fires cts_to_data_gap_ms AFTER the
    // RTS clears the air, which is exactly when overhearers have received the RTS + retuned to the data SF.
    if (_pending_tx && _pending_tx->m_broadcast) {
        const uint32_t gap = airtime_routing_ms(9 /*M_BROADCAST RTS = 7 base + id_lo16(2)*/) + protocol::cts_to_data_gap_ms;
        (void)_hal.after(gap, kCtsToDataGapTimerId);                       // RTS->DATA gap fires do_data_tx (no CTS)
        return;
    }
    const uint32_t base = airtime_routing_ms(8) + airtime_routing_ms(4);   // Lua RTS_LEN=8 + CTS_LEN=4 (timing matches Lua)
    const uint8_t  attempt = static_cast<uint8_t>(protocol::rts_max_retries -
                              (_pending_tx ? _pending_tx->retries_left : 0));
    const uint32_t shift = attempt < 2 ? attempt : 2;                       // x2 backoff, cap x4
    (void)_hal.after((base << shift) + 1, kRtsTimeoutTimerId);
}
void Node::start_ack_timeout() {
    const uint8_t  sf  = _pending_tx ? _pending_tx->chosen_data_sf : _cfg.data_sf;
    const uint16_t len = static_cast<uint16_t>(18 + (_pending_tx ? _pending_tx->inner_len : 0));
    const uint32_t base = airtime_ms(sf, _cfg.radio_bw_hz, _cfg.radio_cr, protocol::preamble_sym, len)
                        + airtime_routing_ms(3);
    (void)_hal.after(base + 2, kAckTimeoutTimerId);
}
void Node::start_pending_rx_expiry(uint8_t payload_len) {
    const uint8_t  sf  = _pending_rx ? _pending_rx->chosen_data_sf : _cfg.data_sf;
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
    if (_pending_rx) _pending_rx->expiry_ms = _hal.now() + t;   // for the BUSY_RX NACK busy_for calc
    (void)_hal.after(t, kPendingRxExpiryTimerId);
}

}  // namespace meshroute
