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

uint8_t Node::select_data_sf(uint8_t rts_sf_index) const {
    // sf_index=3 (ANY) -> our preferred data SF. 0..2 (pinned) deferred — the R3
    // gate uses ANY=3 (decision Q4). PURE (no rand) — dv_dual_sf.lua:3027.
    (void)rts_sf_index;
    return _cfg.data_sf;
}

uint32_t Node::airtime_routing_ms(uint16_t len) const {
    return airtime_ms(_cfg.routing_sf, _cfg.radio_bw_hz, _cfg.radio_cr, protocol::preamble_sym, len);
}
// 3*airtime(routing, Lua RTS_LEN=8) — a TIMING constant from the Lua, NOT the 7-B C++ wire,
// so the retry rand RANGE matches the Lua and the lua-vs-meshroute streams stay aligned.
uint32_t Node::retry_jitter_ms() const { return 3 * airtime_routing_ms(8); }

uint16_t Node::do_send(uint8_t dst, const uint8_t* body, uint8_t body_len, uint8_t flags) {
    const uint16_t ctr = next_ctr(dst);
    TxItem item{};
    item.origin = _node_id; item.dst = dst; item.ctr = ctr; item.ctr_lo = static_cast<uint8_t>(ctr & 0x0F);
    item.flags = flags;     // E2E/PRIORITY ride the wire; their behaviour (ack-track/budget) is a later iteration
    item.inner[0] = 0x00; item.inner[1] = _node_id;      // src_addr_len=0 | origin | body
    if (body) for (uint8_t i = 0; i < body_len; ++i) item.inner[2 + i] = body[i];
    item.inner_len = static_cast<uint8_t>(2 + body_len);
    item.enqueue_time_ms = _hal.now();                   // first-enqueue time (cascade-requeue total-age cap)
    if (_tx_queue_n < kTxQueueCap) _tx_queue[_tx_queue_n++] = item;
    EventField f[] = {
        { .key = "origin", .type = EventField::T::i64, .i = item.origin },
        { .key = "dst",    .type = EventField::T::i64, .i = item.dst },
        { .key = "ctr",    .type = EventField::T::i64, .i = item.ctr },
        { .key = "depth",  .type = EventField::T::i64, .i = _tx_queue_n },
    };
    _hal.emit("tx_enqueue", f, 4);                       // dm_delivery record-creation key (fid==origin)
    become_free();
    return ctr;
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
    TxItem item = _tx_queue[pick];
    for (uint8_t i = pick + 1; i < _tx_queue_n; ++i) _tx_queue[i - 1] = _tx_queue[i];
    --_tx_queue_n;
    issue_send(item);
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
            EventField f[] = { { .key = "dst", .type = EventField::T::i64, .i = item.dst } };
            _hal.emit("send_no_route", f, 1);
        } else {
            defer_send(item);
        }
        return;
    }
    pt.next = first;
    _pending_tx = pt;
    tx_rts_retry();                                      // packs+emits the RTS + start_rts_timeout
}

void Node::tx_rts_retry() {
    if (!_pending_tx) return;
    PendingTx& pt = *_pending_tx;
    pt.awaiting_cts = true; pt.awaiting_ack = false; pt.chosen_data_sf = 0;
    rts_in rin{};
    rin.leaf_id = _cfg.leaf_id; rin.src = _node_id; rin.next = pt.next; rin.ctr_lo = pt.ctr_lo;
    rin.dst = pt.dst; rin.sf_index = 3 /*ANY*/; rin.rts_flags = 0;
    rin.payload_len = static_cast<uint8_t>(pt.inner_len + 4 /*MAC_LEN*/); rin.m_payload_id_lo16 = 0;
    uint8_t buf[9];
    const size_t l = pack_rts(rin, std::span<uint8_t>(buf, sizeof(buf)));
    if (l == 0) { _hal.log("RTS pack failed"); return; }
    EventField f[] = {
        { .key = "dst",  .type = EventField::T::i64, .i = pt.dst },
        { .key = "next", .type = EventField::T::i64, .i = pt.next },
        { .key = "ctr",  .type = EventField::T::i64, .i = pt.ctr },
    };
    _hal.emit("rts_tx", f, 3);                           // emit at the call site (before the LBT defer, dv-faithful)
    // R4.5: the actual TX + start_rts_timeout go through the LBT wrapper (defer if the channel is busy). RX stays
    // on routing_sf. lbt_enabled=false (every gate) -> straight TX + timeout, byte-identical.
    tx_initiating(buf, l, static_cast<int16_t>(_cfg.routing_sf), LbtKind::rts, pt.ctr_lo);
}

// R4.5 LBT: hand an INITIATING frame to the radio, but if the channel is busy (and lbt_enabled) defer the real
// TX past busy_until + rand(0,lbt_backoff+1) — the ONE LBT draw, ONLY when busy (dv:3693-3706). lbt_enabled=false
// (every gate) -> straight to lbt_complete -> byte-identical, NO draw.
void Node::tx_initiating(const uint8_t* bytes, size_t len, int16_t sf, LbtKind kind, uint8_t rts_ctr_lo) {
    if (_cfg.lbt_enabled) {
        const uint64_t now = _hal.now();
        const uint64_t busy_until = _hal.channel_busy_until();
        if (busy_until > now) {
            const uint32_t wait  = static_cast<uint32_t>(busy_until - now);
            const uint32_t delay = wait + static_cast<uint32_t>(_hal.rand_range(0, static_cast<int>(_lbt_backoff_ms) + 1));
            EventField f[] = { { .key = "kind",          .type = EventField::T::str, .s = "initiating" },
                               { .key = "defer_ms",      .type = EventField::T::i64, .i = delay },
                               { .key = "busy_until_ms", .type = EventField::T::i64, .i = static_cast<int64_t>(busy_until) } };
            _hal.emit("tx_lbt_defer", f, 3);
            schedule_lbt_defer(bytes, len, sf, kind, rts_ctr_lo, delay);
            return;
        }
    }
    lbt_complete(bytes, len, sf, kind, rts_ctr_lo);
}

// Stash a busy-channel deferred TX in a free ring slot + arm its own timer (kLbtDeferTimerId + slot), so
// concurrent defers each fire independently (Lua per-closure semantics). false = ring full (rare; >4 defers).
bool Node::schedule_lbt_defer(const uint8_t* bytes, size_t len, int16_t sf, LbtKind kind,
                              uint8_t rts_ctr_lo, uint32_t delay) {
    for (uint8_t s = 0; s < kLbtSlots; ++s) {
        if (_deferred_lbt[s].pending) continue;
        DeferredLbt& d = _deferred_lbt[s];
        d.pending = true; d.kind = static_cast<uint8_t>(kind); d.sf = sf; d.rts_ctr_lo = rts_ctr_lo;
        d.len = static_cast<uint8_t>(len < sizeof(d.buf) ? len : sizeof(d.buf));
        for (uint8_t i = 0; i < d.len; ++i) d.buf[i] = bytes[i];
        (void)_hal.after(delay, kLbtDeferTimerId + s);
        return true;
    }
    EventField f[] = { { .key = "kind", .type = EventField::T::i64, .i = static_cast<uint8_t>(kind) } };
    _hal.emit("tx_lbt_defer_dropped", f, 1);                            // ring full -> drop loudly
    return false;
}

// The actual TX (immediate clear-channel path OR the kLbtDeferTimerId re-fire). RTS: the __pending_tx_ref
// staleness check (cancel a stale deferred RTS, dv:3708) + start_rts_timeout (the after_tx). NACK/flood: just TX.
void Node::lbt_complete(const uint8_t* bytes, size_t len, int16_t sf, LbtKind kind, uint8_t rts_ctr_lo) {
    if (kind == LbtKind::rts) {
        if (!_pending_tx || _pending_tx->ctr_lo != rts_ctr_lo) {        // flight changed while we waited
            EventField f[] = { { .key = "reason", .type = EventField::T::str, .s = "pending_tx_changed" } };
            _hal.emit("rts_tx_cancelled_stale", f, 1);
            return;
        }
    }
    const FrameTag tag = (kind == LbtKind::rts)  ? FrameTag::rts
                       : (kind == LbtKind::nack) ? FrameTag::nack : FrameTag::beacon;
    tx_with_retry(bytes, len, sf, tag);                               // R4.5b: stash (NACK) + tag the frame
    if (kind == LbtKind::rts) start_rts_timeout();                     // after_tx: CTS-wait starts when the RTS is on air
}

// R4.5 FLOOD TX (beacon, dv:3765-3814). Duty pre-check (skip if it would breach budget), then LBT: drop the page
// if the channel is busy longer than flood_lbt_max_defer_ms, else defer past busy_until + a backoff, else TX now.
bool Node::tx_flood(const uint8_t* bytes, size_t len, int16_t sf) {
    if (_duty_cycle_budget_ms > 0) {                                   // duty pre-check (dv:7781) — only when enabled
        const uint64_t airtime = airtime_routing_ms(static_cast<int>(len));
        const uint64_t used    = _hal.airtime_used_ms(_cfg.duty_cycle_window_ms);
        if (used + airtime > _duty_cycle_budget_ms) {
            EventField f[] = { { .key = "airtime_ms", .type = EventField::T::i64, .i = static_cast<int64_t>(airtime) },
                               { .key = "source",     .type = EventField::T::str, .s = "tx_flood" } };
            _hal.emit("duty_cycle_blocked", f, 2);
            return false;
        }
    }
    if (_cfg.lbt_enabled) {
        const uint64_t now = _hal.now();
        const uint64_t busy_until = _hal.channel_busy_until();
        const int64_t  wait = static_cast<int64_t>(busy_until) - static_cast<int64_t>(now);
        if (wait > static_cast<int64_t>(_flood_lbt_max_defer_ms)) {    // busy too long -> drop the page (dv:3796)
            EventField f[] = { { .key = "busy_for_ms", .type = EventField::T::i64, .i = wait } };
            _hal.emit("tx_flood_skipped", f, 1);
            return false;
        }
        if (wait > 0) {
            const uint32_t delay = static_cast<uint32_t>(wait) +
                                   static_cast<uint32_t>(_hal.rand_range(0, static_cast<int>(_lbt_backoff_ms) + 1));
            EventField f[] = { { .key = "kind",          .type = EventField::T::str, .s = "flood" },
                               { .key = "defer_ms",      .type = EventField::T::i64, .i = delay },
                               { .key = "busy_until_ms", .type = EventField::T::i64, .i = static_cast<int64_t>(busy_until) } };
            _hal.emit("tx_lbt_defer", f, 3);
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

// R4.5b central TX (Lua tx_with_retry dv:3599-3639): STASH the retry-eligible frame so on_radio_busy can re-issue
// it on a busy channel, then hand to the radio tagged with the frame type (the sim echoes the tag back). RTS and
// beacon are not retry-eligible (slot -1). (The Lua duty pre-check at dv:3622 is the response-duty defer — DEFERRED;
// it is draw-free + gate-inert at healthy duty, tracked in the R4.5b spec non-goals.)
void Node::tx_with_retry(const uint8_t* bytes, size_t len, int16_t sf, FrameTag tag) {
    const int slot = retry_slot_of(tag);
    if (slot >= 0) {
        TxStashSlot& s = _tx_stash[slot];
        s.valid = true; s.sf = sf; s.retries_left = protocol::tx_defer_max_retries;
        s.ctr_lo = _pending_tx ? _pending_tx->ctr_lo : 0;   // READ only for the DATA slot (retry_stashed re-arm guard); for CTS/ACK/NACK this records the forwarder's OWN outbound flight + is never consulted
        s.len = static_cast<uint16_t>(len < sizeof(s.buf) ? len : sizeof(s.buf));
        for (uint16_t i = 0; i < s.len; ++i) s.buf[i] = bytes[i];
    }
    TxParams p; p.sf = sf; p.label = label_of_frame(tag); p.tag = static_cast<uint16_t>(tag);
    _hal.tx(bytes, len, p);
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
    tx_with_retry(buf, dlen, static_cast<int16_t>(pt.chosen_data_sf), FrameTag::data);   // R4.5b: stash for on_radio_busy retry
    EventField f[] = {
        { .key = "dst",  .type = EventField::T::i64, .i = pt.dst },
        { .key = "next", .type = EventField::T::i64, .i = pt.next },
        { .key = "ctr",  .type = EventField::T::i64, .i = pt.ctr },
    };
    _hal.emit("data_tx", f, 3);
    pt.awaiting_ack = true;
    start_ack_timeout();
}

void Node::start_rts_timeout() {
    const uint32_t base = airtime_routing_ms(8) + airtime_routing_ms(3);   // Lua RTS_LEN=8 + CTS (timing matches Lua)
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
    const uint32_t t = airtime_routing_ms(3) + protocol::cts_to_data_gap_ms +
                       airtime_ms(sf, _cfg.radio_bw_hz, _cfg.radio_cr, protocol::preamble_sym, len) + 2;
    if (_pending_rx) _pending_rx->expiry_ms = _hal.now() + t;   // for the BUSY_RX NACK busy_for calc
    (void)_hal.after(t, kPendingRxExpiryTimerId);
}

}  // namespace meshroute
