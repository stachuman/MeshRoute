// MeshRoute — lib/core/node_cascade.cpp  (cascade-to-alt walk + no-route defer queue)
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// When a flight gives up on its current next-hop: the selectable filter + two-pass
// cascade walk to an alternate, the requeue-with-backoff on exhaustion, the
// no-route originator defer queue (TTL-first drain), and the RTS/ACK/RX timeout
// fires that drive them. Behaviour mirrors dv_dual_sf.lua. Part of the Node class
// (declared in node.h); split out of node.cpp for readability.
#include "node.h"

namespace MESHROUTE_NS {

bool Node::alt_tried(const PendingTx& pt, uint8_t hop) const {
    for (uint8_t i = 0; i < pt.alts_tried_n; ++i) if (pt.alts_tried[i] == hop) return true;
    return false;
}
void Node::mark_tried(PendingTx& pt, uint8_t hop) {
    if (alt_tried(pt, hop)) return;
    if (pt.alts_tried_n < protocol::max_rt_candidates) pt.alts_tried[pt.alts_tried_n++] = hop;
}

// The minimal selectable filter (dv_dual_sf.lua:3990-4042): skip the upstream hop
// (no loop-back) + skip already-tried hops this flight + skip blind peers. suspect/
// freshness/mobile-transit are stubbed (empty-table no-ops). Post-R4.2 the budget
// penalty CAN create an effective_score gradient (so candidates[] may be demoted),
// but the candidate WALK here is order-only; allow_uphill's two-pass SHAPE is kept.
// (review #01, FIXED cleanup #B): pick_next_cascade_hop now calls refresh_route_order FIRST (node_cascade.cpp:48),
// matching the Lua — re-sort + the conditional triggered-beacon draw, catching a tier change (TTL-expiry) since the
// mark-time sort. Gate-inert (no tier change in a gate -> the re-sort keeps the primary -> no draw).
bool Node::is_blind(uint8_t next_hop) const {
    // A peer is "blind" (deaf on routing_sf, busy in its data_sf RX window) until
    // _active->_blind_until[next_hop]. Pure const read; expired entries read as not-blind (the
    // map is bounded by the neighbour count, so stale entries don't grow it).
    auto it = _active->_blind_until.find(next_hop);
    return it != _active->_blind_until.end() && it->second > _hal.now();
}

bool Node::next_hop_selectable(const RtCandidate& c, const PendingTx& pt, bool allow_uphill) const {
    (void)allow_uphill;
    if (c.next_hop == 0) return false;
    if (pt.has_previous_hop && c.next_hop == pt.previous_hop) return false;   // dv:3992
    if (alt_tried(pt, c.next_hop)) return false;                             // dv:4006
    if (is_blind(c.next_hop)) return false;                                  // F1: skip blind peers (dv:4030)
    return true;
}

uint8_t Node::pick_next_cascade_hop(const PendingTx& pt) {
    // Cleanup #B (dv:5434): refresh the route order FIRST — catch a tier change since the last sort before walking.
    RtEntry* e = refresh_route_order(pt.dst, "cascade_order");
    if (e == nullptr) return 0;
    // Two-pass (dv:5430-5450): pass 1 gradient-respecting, pass 2 uphill fallback.
    // candidates[] is kept sorted by route_strictly_better (stable: ties keep insertion
    // order, NO id tie-break — see node_routing.cpp route_strictly_better).
    for (int pass = 0; pass < 2; ++pass) {
        const bool allow_uphill = (pass == 1);
        for (uint8_t i = 0; i < e->n; ++i)
            if (next_hop_selectable(e->candidates[i], pt, allow_uphill)) return e->candidates[i].next_hop;
    }
    return 0;
}

uint32_t Node::requeue_backoff_ms(uint8_t requeue_count) const {
    // PURE base*2^(n-1) capped — NO rand (dv:6209-6213). n>=1.
    uint32_t b = protocol::cascade_requeue_base_ms;
    for (uint8_t i = 1; i < requeue_count; ++i) {
        b <<= 1;
        if (b >= protocol::cascade_requeue_backoff_cap_ms) { b = protocol::cascade_requeue_backoff_cap_ms; break; }
    }
    return b;
}

uint8_t Node::effective_rts_max_retries(uint8_t requeue_count) const {
    // A requeued flight gets FEWER same-hop retries (dv:3119) — critical for
    // determinism: a flat budget would fire extra retry-jitter draws on a
    // requeued flight and de-align the lua/meshroute mt19937 streams.
    const int n = static_cast<int>(protocol::rts_max_retries) - static_cast<int>(requeue_count);
    return n < 0 ? 0 : static_cast<uint8_t>(n);
}

// On a flight giving up on its current next-hop (RTS- or ACK-timeout exhausted the
// same-hop retries): mark it tried, walk to the next candidate and re-RTS there with
// NO jitter draw (dv:6478 — adding a rand here de-aligns the lua/meshroute streams).
// When no untried candidate remains, hand off to try_cascade_requeue.
void Node::cascade_to_alt(const char* giveup_event) {
    if (!_active->_pending_tx) return;
    PendingTx& pt = *_active->_pending_tx;
    const uint8_t from_next = pt.next;   // the hop that just failed (capture before overwrite; used by both branches)
    mark_tried(pt, pt.next);
    const uint8_t alt = pick_next_cascade_hop(pt);
    if (alt != 0) {
        MR_TELEMETRY(
            EventField f[] = { { .key = "origin",   .type = EventField::T::i64, .i = pt.origin },
                               { .key = "dst",      .type = EventField::T::i64, .i = pt.dst },
                               { .key = "ctr",      .type = EventField::T::i64, .i = pt.ctr },
                               { .key = "from_next", .type = EventField::T::i64, .i = from_next },
                               { .key = "next",     .type = EventField::T::i64, .i = alt } };
            _hal.emit("path_cascade", f, 5); );
        pt.next = alt;
        pt.retries_left = effective_rts_max_retries(pt.requeue_count);   // requeue-aware budget on the alt
        tx_rts_retry();                                  // re-RTS on the alt — NO jitter (re-arms kRtsTimeoutTimerId)
    } else {
        // §P3 active rediscovery: all candidates exhausted AND the primary that just failed is SILENT/DEAD (confirmed
        // flaky, not merely congested) -> the route table holds only dead paths to dst. Flood an RREQ to find a FRESH
        // path NOW rather than stalling on the requeue / 3h aging — closes the no-alt dead-relay case (the user's bug:
        // a dest reachable only via a departed relay). Rate-limited (rreq_rate_ok); a normal congested giveup does NOT.
        if (liveness_penalty_q4(from_next) >= protocol::peer_silent_penalty_q4)
            emit_route_request(pt.dst, _cfg.dv_hop_cap);  // full-radius requery (network-wide configured TTL, like the deferred-drain requery)
        try_cascade_requeue(pt, giveup_event);           // all candidates tried
    }
}

// All candidates exhausted: requeue the flight onto _active->_tx_queue with a pure
// exponential backoff (held idle until kCascadeRequeueTimerId fires), or — once the
// requeue-count / total-age caps are hit — a true giveup (dv:6159-6213).
void Node::try_cascade_requeue(const PendingTx& pt, [[maybe_unused]] const char* giveup_event) {
    const uint64_t now = _hal.now();
    const bool count_done = pt.requeue_count >= protocol::cascade_requeue_max;
    const bool age_done   = (now - pt.enqueue_time_ms) >= protocol::cascade_requeue_total_max_ms;
    if (count_done || age_done || _active->_tx_queue_n >= kTxQueueCap) {
        MR_TELEMETRY(
            EventField f[] = { { .key = "dst", .type = EventField::T::i64, .i = pt.dst },
                               { .key = "ctr", .type = EventField::T::i64, .i = pt.ctr } };
            _hal.emit("path_cascade_exhausted", f, 2);
            _hal.emit(giveup_event, f, 2); );
        { Push pu{}; pu.kind = PushKind::send_failed; pu.dst = pt.dst; pu.ctr = pt.ctr; enqueue_push(pu); }
        _active->_pending_tx.reset();
        become_free();
        return;
    }
    TxItem it{};
    it.origin = pt.origin; it.dst = pt.dst; it.ctr = pt.ctr; it.ctr_lo = pt.ctr_lo; it.flags = pt.flags;
    it.inner_len = pt.inner_len;
    for (uint8_t i = 0; i < pt.inner_len; ++i) it.inner[i] = pt.inner[i];
    it.is_forward = pt.has_previous_hop; it.previous_hop = pt.previous_hop;
    it.is_gw_relay = pt.is_gw_relay;                                            // Slice 4c.2: a requeued cross-layer relay keeps RTS_FLAG_RELAY
    it.fwd_remaining = pt.fwd_remaining; it.fwd_committed = pt.fwd_committed;   // carry hop budget across the requeue
    it.requeue_count = static_cast<uint8_t>(pt.requeue_count + 1);
    it.enqueue_time_ms = pt.enqueue_time_ms;             // PRESERVE the original first-enqueue time
    // The queue ITSELF enforces the backoff: next_attempt_ms gates the dequeue
    // (become_free scans for the first ready item), so a concurrent become_free
    // can't skip the hold. The timer is just the wakeup at the ready time.
    it.next_attempt_ms = now + requeue_backoff_ms(it.requeue_count);
    MR_TELEMETRY(
        EventField rf[] = { { .key = "dst",           .type = EventField::T::i64, .i = it.dst },
                            { .key = "ctr",           .type = EventField::T::i64, .i = it.ctr },
                            { .key = "requeue_count", .type = EventField::T::i64, .i = it.requeue_count } };
        _hal.emit("cascade_requeue", rf, 3); );
    _active->_tx_queue[_active->_tx_queue_n++] = it;                       // tail; held by next_attempt_ms until the backoff
    _active->_pending_tx.reset();
    (void)_hal.after(requeue_backoff_ms(it.requeue_count), kCascadeRequeueTimerId);
}

// An ORIGINATOR send with no usable route yet: hold it until a beacon installs a
// route (drain-on-rt_changed) or the periodic 1s drain ages it out by send_defer_ttl.
void Node::defer_send(const TxItem& item) {
    if (_active->_deferred_n >= protocol::cap_deferred_sends) {   // full -> REFUSE the NEW send (Lua table_cap_hit
        MR_TELEMETRY(
            EventField cf[] = {                          // dv:5549-5553), NOT drop-oldest. Complete the
                { .key = "dst", .type = EventField::T::i64, .i = item.dst },   // app future so it never hangs.
                { .key = "ctr", .type = EventField::T::i64, .i = item.ctr } };
            _hal.emit("send_deferred_refused", cf, 2); );
        { Push pu{}; pu.kind = PushKind::send_failed; pu.dst = item.dst; pu.ctr = item.ctr; enqueue_push(pu); }
        return;
    }
    DeferredSend d{}; d.item = item; d.deferred_at_ms = _hal.now();
    _active->_deferred[_active->_deferred_n++] = d;
    MR_TELEMETRY(
        EventField f[] = { { .key = "dst", .type = EventField::T::i64, .i = item.dst },
                           { .key = "ctr", .type = EventField::T::i64, .i = item.ctr } };
        _hal.emit("send_deferred", f, 2); );
    emit_route_request(item.dst, 1);                     // ask for a route: cheap ttl=1 probe (Lua emit_route_request)
    if (!_active->_drain_armed) {                                 // arm the periodic TTL-giveup drain
        _active->_drain_armed = true;
        (void)_hal.after(protocol::send_defer_drain_period_ms, kDeferredDrainTimerId);
    }
}

void Node::try_drain_deferred() {
    const uint64_t now = _hal.now();
    // STATIC, not stack: drained[32] + nq[8] of TxItem (~272 B each) = ~11 KB, which overflows the
    // nRF52840's ~8 KB app stack (under the SoftDevice) and FREEZES the device the moment this drain
    // first runs after a send defers — invisible to the native tests/gates (MB stack). try_drain_deferred
    // is non-reentrant (no recursion, single-threaded on device AND in the sim), so one shared copy is
    // safe: each call fully writes [0,n) before it reads it, so no stale carry-over across calls/nodes.
    static TxItem drained[protocol::cap_deferred_sends]; // route appeared -> fly (oldest first)
    uint8_t  drained_n = 0;
    uint8_t  w = 0;                                       // compaction write cursor (insertion order kept)
    for (uint8_t r = 0; r < _active->_deferred_n; ++r) {
        DeferredSend d = _active->_deferred[r];
        // TTL FIRST (the defer_ttl_route_exists_trap fix, dv:6775-6782): age out a
        // held send BEFORE checking route-exists, else a flapping route never lets
        // it expire (the s12 477-defer infinite loop).
        if ((now - d.deferred_at_ms) >= protocol::send_defer_ttl_ms) {
            MR_TELEMETRY(
                EventField f[] = { { .key = "dst", .type = EventField::T::i64, .i = d.item.dst },
                                   { .key = "ctr", .type = EventField::T::i64, .i = d.item.ctr } };
                _hal.emit("send_deferred_giveup", f, 2); );
            { Push pu{}; pu.kind = PushKind::send_failed; pu.dst = d.item.dst; pu.ctr = d.item.ctr; enqueue_push(pu); }
            continue;                                    // drop (don't keep)
        }
        RtEntry* e = rt_find(d.item.dst);
        if (e != nullptr && e->n > 0) {
            MR_TELEMETRY(
                EventField sf[] = { { .key = "origin",    .type = EventField::T::i64, .i = d.item.origin },
                                    { .key = "dst",       .type = EventField::T::i64, .i = d.item.dst },
                                    { .key = "ctr",       .type = EventField::T::i64, .i = d.item.ctr },
                                    { .key = "waited_ms", .type = EventField::T::i64, .i = static_cast<int64_t>(now - d.deferred_at_ms) } };
                _hal.emit("send_drained", sf, 4); );      // route appeared (dv:6953) — the held send flies
            drained[drained_n++] = d.item;               // route appeared -> drain to the queue HEAD below
            continue;
        }
        emit_route_request(d.item.dst, _cfg.dv_hop_cap); // still no route -> requery at full radius (rate-limited)
        _active->_deferred[w++] = d;                              // still no route + not expired -> keep
    }
    _active->_deferred_n = w;
    if (drained_n > 0) {
        // Re-queue drained items to the HEAD of _active->_tx_queue (oldest first), ahead of
        // newer queued messages — the Lua re-queues to head (dv:6843-6886). Overflow
        // past kTxQueueCap is dropped (a rare edge; same as the original tail path).
        static TxItem nq[kTxQueueCap]; uint8_t n = 0;    // STATIC (see drained above) — keep off the stack
        for (uint8_t i = 0; i < drained_n && n < kTxQueueCap; ++i) nq[n++] = drained[i];
        for (uint8_t i = 0; i < _active->_tx_queue_n && n < kTxQueueCap; ++i) nq[n++] = _active->_tx_queue[i];
        for (uint8_t i = 0; i < n; ++i) _active->_tx_queue[i] = nq[i];
        _active->_tx_queue_n = n;
    }
    become_free();                                       // service anything just re-queued (no-op if a flight is live)
    if (_active->_deferred_n > 0) {                               // re-arm while items remain
        (void)_hal.after(protocol::send_defer_drain_period_ms, kDeferredDrainTimerId);
    } else {
        _active->_drain_armed = false;
    }
}

void Node::rts_timeout_fire() {
    if (!_active->_pending_tx || !_active->_pending_tx->awaiting_cts) return;          // stale (CTS already matched)
    if (_active->_pending_rx) { (void)_hal.after(protocol::rts_busy_retry_ms, kRtsTimeoutTimerId); return; }
    if (_active->_pending_tx->retries_left > 0) {
        // §P3 silent-next cascade: the primary is ALREADY known silent/dead (prior-flight liveness evidence) ->
        // don't burn same-hop retries on a confirmed-dead path; cascade to a viable alt NOW (or RREQ on no-alt).
        // Reads the persisted tier (no per-timeout counting — that churned the suite). DRIFT from the spec's literal
        // per-failure/suspect trigger: gated on SILENT (confirmed flaky), not suspect; see the phase report.
        if (liveness_penalty_q4(_active->_pending_tx->next) >= protocol::peer_silent_penalty_q4) {
            cascade_to_alt("rts_silent_cascade");
            return;
        }
        --_active->_pending_tx->retries_left;
        const int jit = _hal.rand_range(0, static_cast<int>(retry_jitter_ms()) + 1);   // RNG site #1
        (void)_hal.after(static_cast<uint32_t>(jit), kRetryBackoffTimerId);
    } else {
        record_peer_rts_timeout(_active->_pending_tx->next, _active->_pending_tx->ctr_lo);   // §P1: same-hop RTS giveup = liveness evidence
        cascade_to_alt("rts_giveup");                    // same-hop retries exhausted -> walk to an alternate (§P3: + RREQ if it's silent)
    }
}
void Node::ack_timeout_fire() {
    if (!_active->_pending_tx || !_active->_pending_tx->awaiting_ack) return;
    if (_active->_pending_rx) { (void)_hal.after(protocol::rts_busy_retry_ms, kAckTimeoutTimerId); return; }
    if (_active->_pending_tx->retries_left > 0) {
        // §P3 silent-next cascade (mirror of rts_timeout_fire): a missed DATA-ACK on an ALREADY-silent primary
        // cascades immediately rather than re-RTSing the dead path. Persisted-tier read, no per-timeout counting.
        if (liveness_penalty_q4(_active->_pending_tx->next) >= protocol::peer_silent_penalty_q4) {
            cascade_to_alt("data_ack_silent_cascade");
            return;
        }
        --_active->_pending_tx->retries_left;
        _active->_pending_tx->awaiting_ack = false; _active->_pending_tx->awaiting_cts = false; _active->_pending_tx->chosen_data_sf = 0;
        const int jit = _hal.rand_range(0, static_cast<int>(retry_jitter_ms()) + 1);   // RNG site #2
        (void)_hal.after(static_cast<uint32_t>(jit), kRetryBackoffTimerId);
    } else {
        record_peer_rts_timeout(_active->_pending_tx->next, _active->_pending_tx->ctr_lo);   // §P1: same-hop ACK giveup = liveness evidence
        cascade_to_alt("data_ack_giveup");               // same-hop retries exhausted -> walk to an alternate (§P3: + RREQ if it's silent)
    }
}
void Node::pending_rx_expiry_fire() {
    if (!_active->_pending_rx) return;
    _hal.set_rx_sf(_cfg.routing_sf);
    _active->_pending_rx.reset();
    MR_TELEMETRY( _hal.emit("data_rx_timeout", nullptr, 0); );
    become_free();
}

}  // namespace meshroute
