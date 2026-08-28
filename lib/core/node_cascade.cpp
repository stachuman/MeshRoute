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

// Slice 6b: a terminal cascade giveup maps its giveup_event to the DM-failure reason the companion reads.
// The two roots are "rts_giveup"/"rts_silent_cascade" (CTS-timeout) and "data_ack_giveup"/"data_ack_silent_cascade"
// (DATA-ACK-timeout). Prefix-keyed so a new giveup label inherits the right reason. A non-DM/legacy giveup -> none.
SendFailReason Node::giveup_fail_reason(const char* ge) {
    if (!ge) return SendFailReason::none;
    if (ge[0]=='r' && ge[1]=='t' && ge[2]=='s') return SendFailReason::no_cts;                    // "rts_*"
    if (ge[0]=='d' && ge[1]=='a' && ge[2]=='t' && ge[3]=='a' && ge[4]=='_') return SendFailReason::no_ack;  // "data_ack_*"
    return SendFailReason::none;
}

// §3-B.2: the terminal giveup ritual, previously written out verbatim at 6 sites (4 here, 2 in node_mac_rx.cpp).
// ORDER IS LOAD-BEARING and matches every former site: tell the app FIRST (while the flight's dst/ctr are still the
// caller's), then drop the flight, then become_free() to re-service the queue. dst/ctr come in BY VALUE, so a caller
// holding `PendingTx& pt = *_active->_pending_tx` may pass pt.dst/pt.ctr safely — they are copied before reset().
void Node::giveup_flight(SendFailReason reason, uint8_t dst, uint16_t ctr) {
    push_send_failed(reason, dst, ctr);
    _active->_pending_tx.reset();
    become_free();
}

// ★★★ [[B159]] correction (2026-08-28) — THE GATEWAY GIVE-UP IS A REAL SENDER DEADLINE, NOT A TIMEOUT-ENTRY TEST.
// ⛔ THE DEFECT THIS CLOSES, and it disproves what `completed_flight_cache_ttl_ms`'s own note asserts ("nothing can
//    legitimately re-arrive after it"): `gateway_doorstep_hold` tested `age >= gateway_send_giveup_ms` and THEN
//    scheduled `now + backoff`, so a hold entered at age 149 999 ms armed a perfectly legitimate retry BEYOND the
//    bound. Nothing downstream re-checked: `become_free` (node_mac.cpp:899) picks purely on `next_attempt_ms <= now`,
//    and `issue_send`'s gateway-window arm (node_mac.cpp:1116) re-queues with ANOTHER full window defer. With
//    operator-configurable gateway periods that composition has NO fixed upper margin ⇒ the retry horizon was
//    UNBOUNDED, and any receiver-side retention "derived" from 150 s was pinning a constant, not a horizon.
// ⇒ WHAT THE DEADLINE BOUNDS, stated exactly because the receiver's retention is sized from it: **the START of a
//   gateway-bound attempt**. No RTS may BEGIN at or after `enqueue_time_ms + gateway_send_giveup_ms`. The DATA it
//   carries still arrives one MAC exchange later, so the receiver retention is that bound PLUS a margin covering one
//   exchange — see `seen_origin_ttl_ms` in protocol_constants.h, where the margin is named and measured.
// ⓘ THE EARLY **ADMISSION** GUARD — `rts_handoff_deadline_cancel` below; NOT the air start. ⚠ AN EARLIER CUT
//   ENFORCED IT AT `issue_send`'s dequeue/start boundary and that was NOT ENOUGH: it bounded ACCEPTANCE, and an RTS
//   can be LBT- or DUTY-deferred after acceptance and air far later (measured: 271 000 ms against a 151 000 ms
//   bound). That check has been deleted — with the handoff guard present, removing it left the entire native suite
//   green, i.e. it was redundancy no mutation could kill. Two earlier prospective checks at the ARMING sites went
//   the same way for the same measured reason. ⛔ A check nothing can kill is not a safeguard; keep the invariant
//   at the seam where the frame actually meets the HAL, and keep it in ONE function.
//   ⚠ `start_ms` is the ADMISSION instant, NOT the air start — `Hal::tx()` only ENQUEUES on a device, so the
//   TERMINAL bound is `DeviceHal::pump_tx`'s check against `TxParams::deadline_ms` (produced by
//   `rts_air_deadline_ms`). This guard is the admission layer of that pair; the parameter stays explicit so a
//   deadline test rather than a hidden clock read.
// C2: expiry is REFUSED LOUDLY through the EXISTING give-up shape (`send_giveup{gateway_unreachable_timeout}` +
// `giveup_flight(gateway_unreachable)`) — byte-for-byte what an age-expired hold already emitted — never a silent drop.
bool Node::gateway_deadline_expired(uint64_t enqueue_time_ms, uint64_t start_ms,
                                    uint8_t origin, uint8_t dst, uint16_t ctr) {
    const uint64_t enq = enqueue_time_ms ? enqueue_time_ms : start_ms;
    const uint64_t age = start_ms - enq;
    if (age < protocol::gateway_send_giveup_ms) return false;
    MR_EMIT("send_giveup", EF_I("origin", origin), EF_I("dst", dst), EF_I("ctr", ctr),
            EF_S("reason", "gateway_unreachable_timeout"), EF_I("age_ms", static_cast<int64_t>(age)));
    return true;
}

// ★★★ [[B159]] BLOCKER-1 CORRECTION (2026-08-28) — THE RTS **HAL-ADMISSION** GUARD (an EARLY cancellation).
// ⛔ THIS IS NOT THE PHYSICAL AIR START, and an earlier version of this banner said it was — WITHDRAWN. `Hal::tx()`
//    only ENQUEUES on a device; the TERMINAL PHYSICAL-START AUTHORITY is `DeviceHal::pump_tx()`, immediately before
//    `start_transmit()`, against the `TxParams::deadline_ms` this flight stamps (see `rts_air_deadline_ms`).
// ⛔ WHAT THE PREVIOUS CUT GOT WRONG, and it is the same class of error twice: `issue_send`'s boundary ran BEFORE
//    `tx_initiating`, so it bounded when an attempt was ACCEPTED, not when a frame was AIRED. Between the two the
//    RTS can be LBT-deferred, or DUTY-deferred — `lbt_complete` parks it in `_rts_duty_defer` and the code's own
//    comment prices that wait at "~1h" (node_mac.cpp) — and `rts_duty_defer_fire` then called `_hal.tx` with NO
//    deadline test. An attempt accepted at age 149 s could therefore still AIR long past the bound, and a stub HAL
//    that transmits synchronously can never see it.
// ⇒ THE TIGHTENED DEFINITION, and every derivation downstream depends on it: **"start" now means the ACTUAL AIR
//   START of an RTS.** This predicate is called immediately before each of the TWO places an RTS frame reaches the
//   HAL — `lbt_complete`'s RTS branch (the immediate and LBT-deferred path) and `rts_duty_defer_fire` (the
//   duty-deferred path) — so no NODE-SIDE LBT/duty deferral carries a frame past it (device-queue delay is NOT).
// ⓘ Same-hop RTS RETRIES re-enter `tx_initiating` and are therefore guarded too. That is what lets the receiver's
//   margin price ONE exchange (RTS+CTS+gap+DATA) instead of a whole retry chain — see `seen_origin_ttl_ms`.
// C2: a late attempt is CANCELLED LOUDLY through the same give-up shape, never a silent non-transmit.
// [[B159]]: the ABSOLUTE air deadline for the CURRENT flight's RTS, or 0 ("no deadline") when the flight is not
// gateway-bound. Same rule and same scope as `rts_handoff_deadline_cancel` below — one rule, expressed once (U1) —
// but delivered as a VALUE that travels with the frame, because the HAL cannot ask the Node anything (C3).
uint64_t Node::rts_air_deadline_ms() const {
    if (!_active->_pending_tx) return 0;
    const PendingTx& pt = *_active->_pending_tx;
    if (find_gw_schedule(pt.next) == nullptr) return 0;
    const uint64_t enq = pt.enqueue_time_ms ? pt.enqueue_time_ms : _hal.now();
    return enq + protocol::gateway_send_giveup_ms;
}

bool Node::rts_handoff_deadline_cancel(uint32_t flight_gen) {
    if (!_active->_pending_tx) return false;
    PendingTx& pt = *_active->_pending_tx;
    if (pt.flight_gen != flight_gen) return false;          // a stale completion: the caller's own guard owns it
    // ⛔ THE SCOPE IS DELIBERATE AND NOT REMOVABLE REDUNDANCY, although no mutation can kill it (battery `b159dl`
    // B09 reports it unusable, and that verdict is RECORDED rather than engineered away). The reason it has no
    // behavioural signature today is measured: a NON-gateway flight is capped by `try_cascade_requeue` at
    // `cascade_requeue_total_max_ms`(60 s) + one `requeue_backoff_ms`(<=20 s) = 80 s, so it can never reach this
    // 150 s bound to be judged by it. The guard encodes WHICH patience governs WHICH flight; without it a later
    // widening of the cascade cap past 150 s would let a *gateway* constant silently kill ordinary flights.
    if (find_gw_schedule(pt.next) == nullptr) return false;
    if (!gateway_deadline_expired(pt.enqueue_time_ms, _hal.now(), pt.origin, pt.dst, pt.ctr)) return false;
    giveup_flight(SendFailReason::gateway_unreachable, pt.dst, pt.ctr);
    return true;
}

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
    if (route_uses_mobile_as_transit(pt.dst, c.next_hop)) return false;      // ① never relay THROUGH a mobile (belt-and-suspenders if a route turned mobile post-install; dv:4099)
    // NOTE: freshness/liveness is NOT a hard selectability gate (reverted from 4895480 — it false-rejected good
    // next-hops you TX-to-but-rarely-RX-from, dropping sole-but-functional paths: s18 108→98). It lives in the
    // SORT (route_strictly_better viability, node_routing.cpp) — stale loses to fresh but stays pickable if sole.
    // A LESS-AGGRESSIVE pick-time preference is the planned re-add (prefer-fresh-but-fall-back, never drop a sole path).
    // §intra-layer-relay (2026-07-05): NEVER route THROUGH a gateway — it won't relay intra-leaf traffic (Edit 2 +
    // design §6). Recognize a gateway via is_gateway_dest() — it checks _gw_schedules + _bridged_layers (populated
    // from the gateway's self_gateway beacon + schedule/TLV), so it is the LEARNED gateway role, independent of the
    // RtCandidate.is_gateway flag that learn_route_via zeroes on RREQ/RREP routes. ⚠ NOT the reserved id-range 1..16 —
    // tests/sim use ids 1..16 for NORMAL nodes (bypassing DAD), and an id-range gate tanks s18 + 41 tests. is_gateway_dest
    // is false for a normal node -> those stay green. ALLOW next_hop==pt.dst: routing TO the gateway (cross-layer egress
    // or a DM to it) is legitimate; reject only TRANSIT. Edit 2's gateway-side drop backstops the unlearned-gateway case.
    if (is_gateway_dest(c.next_hop) && c.next_hop != pt.dst) return false;
    return true;
}

uint8_t Node::pick_next_cascade_hop(const PendingTx& pt) {
    // Cleanup #B (dv:5434): refresh the route order FIRST — catch a tier change since the last sort before walking.
    RtEntry* e = refresh_route_order(pt.dst, "cascade_order", pt.plane);   // Wave 2: dispatch on the flight's plane (GLOBAL never uses _rt_team even for a colliding id)
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
        MR_EMIT("path_cascade", EF_I("origin", pt.origin), EF_I("dst", pt.dst), EF_I("ctr", pt.ctr), EF_I("from_next", from_next),
                EF_I("next", alt));
        pt.next = alt;
        pt.retries_left = effective_rts_max_retries(pt.requeue_count);   // requeue-aware budget on the alt
        pt.retry_attempt = 0;                            // the alt is a NEW contention context -> reset the backoff growth
        tx_rts_retry();                                  // re-RTS on the alt — NO jitter (re-arms kRtsTimeoutTimerId)
    } else {
#if MR_FEAT_TEAM
        if (pt.plane == Plane::TEAM) {
            // §team-multihop (spec 2026-07-15 Plane 2): a TEAM flight with all _rt_team paths exhausted -> reactively
            // re-discover (the dynamic-team reroute — the user's hiking-group case) via a TEAM RREQ, then requeue to retry
            // once the RREP installs a fresh route. SKIP the static §P3 liveness gate + slow-reprobe below: from_next is a
            // team_local_id and those index the STATIC _peer_liveness / _link_bidi arrays (§18 aliasing). Full team<->static
            // separation on the reroute path; team liveness is 2c (for 2b, cascade-exhaustion is the trigger, rate-limited
            // by the team _rreq ledger).
            // §team-parity T1 (spec §3/T1 "Team RREQ TTL escalation uses team_hop_cap"): ★ DONE — the cap argument
            // now matches the RREQ's own plane argument, so a team cascade-exhaustion rediscovery floods at
            // team_hop_cap (8, R4's team ceiling) instead of the static radius of 16. This is a behaviour change and
            // is why T1 is not a byte-identity slice.
            // ★★ GATE-BLIND, measured 0/32 at T0 and re-measured 0/32 at T1 with a same-site control proving the
            // branch is genuinely dark (cascade_to_alt is entered 1148 times, `pt.plane == AUTO` every time), NOT
            // weakly probed. Byte-identity CANNOT see a mistake here. Native coverage:
            // test_node_r3.cpp "§team-parity T1 — team cascade exhaustion re-floods at team_hop_cap, not dv_hop_cap".
            emit_route_request(pt.dst, hop_cap_for(/*team_plane=*/true), /*team_plane=*/true);
            try_cascade_requeue(pt, giveup_event);
            return;
        }
#endif
        // §P3 active rediscovery: all candidates exhausted AND the primary that just failed is SILENT/DEAD (confirmed
        // flaky, not merely congested) -> the route table holds only dead paths to dst. Flood an RREQ to find a FRESH
        // path NOW rather than stalling on the requeue / 3h aging — closes the no-alt dead-relay case (the user's bug:
        // a dest reachable only via a departed relay). Rate-limited (rreq_rate_ok); a normal congested giveup does NOT.
        if (liveness_penalty_q4(from_next) >= protocol::peer_silent_penalty_q4)
            // §team-parity T0: full-radius requery (network-wide configured TTL, like the deferred-drain requery),
            // now read through the plane accessor. ★ This site is STATIC-ONLY BY CONSTRUCTION and stays that way:
            // the `pt.plane == Plane::TEAM` branch above returns before reaching here, and the RREQ it emits is
            // static-scoped (emit_route_request's team_plane defaults to false). It is NOT the twin of the :139 team
            // site — that one is cascade-exhaustion-on-the-team-plane; this one is the §P3 dead-primary rediscovery,
            // whose trigger (liveness_penalty_q4 / _peer_liveness) is a static-plane-indexed array the team branch
            // above deliberately skips. ⇒ nothing here flips in T1; it is routed through hop_cap_for purely so a
            // grep for `_cfg.dv_hop_cap` finds no surviving direct reads. Measured corpus reach: 94 executions,
            // 100% static. Static reduction: hop_cap_for(false) == _cfg.dv_hop_cap.
            emit_route_request(pt.dst, hop_cap_for(/*team_plane=*/false));
        // Slow-reprobe interception (asymmetric-link slice 6, MF4): a one-way next-hop stays liveness-HEALTHY
        // (clear_peer_suspect fires on its every beacon) so §P3 above never triggers on it -> the giveup would
        // fall straight to the 9–80-RTS try_cascade_requeue burst. Instead: throttle to ONE RTS per
        // link_reprobe_ttl_ms (the probe catches metal lucky-marginal deliveries + a real CTS recovers via
        // note_link_confirmed). The single probe STILL flies (sole-route delivery must not regress).
        if (_active->_link_bidi[from_next] == static_cast<uint8_t>(LinkBidi::one_way)) {
            const uint64_t now  = _hal.now();
            const uint64_t last = _active->_link_reprobe_last_ms[from_next];
            const bool window_open = (last == 0) || (now - last >= protocol::link_reprobe_ttl_ms);
            if (window_open) {
                _active->_link_reprobe_last_ms[from_next] = now;
                MR_EMIT("link_reprobe", EF_I("origin", pt.origin), EF_I("dst", pt.dst),
                        EF_I("ctr", pt.ctr), EF_I("next", from_next));
                pt.alts_tried_n = 0;                          // re-allow the one-way hop for the single probe
                pt.next = from_next;
                pt.retries_left = effective_rts_max_retries(pt.requeue_count);
                pt.retry_attempt = 0;
                tx_rts_retry();                                // ONE probe (re-arms kRtsTimeoutTimerId), NO jitter
            } else {
                // Inside the throttle window: clean giveup, NO burst. The route stays in the table (reversible).
                MR_TELEMETRY(
                    EventField gf[] = { { .key = "dst", .type = EventField::T::i64, .i = pt.dst },
                                        { .key = "ctr", .type = EventField::T::i64, .i = pt.ctr } };
                    _hal.emit("path_cascade_exhausted", gf, 2);
                    _hal.emit(giveup_event, gf, 2); );
                giveup_flight(giveup_fail_reason(giveup_event), pt.dst, pt.ctr);
            }
            return;
        }
        try_cascade_requeue(pt, giveup_event);           // all candidates tried (NOT one-way -> legacy burst)
    }
}

// All candidates exhausted: requeue the flight onto _active->_tx_queue with a pure
// exponential backoff (held idle until kCascadeRequeueTimerId fires), or — once the
// requeue-count / total-age caps are hit — a true giveup (dv:6159-6213).
// ④ load-adaptive back-pressure: effective requeue budget at this TX-queue depth (Lua cascade_load_skip dv:6275-6303).
// Signed intermediates — uint8_t subtraction would wrap; the Lua uses math.max(0, …).
int Node::cascade_effective_max(uint8_t queue_depth) {
    const int load_excess = static_cast<int>(queue_depth) - static_cast<int>(protocol::cascade_requeue_load_threshold);
    const int eff = static_cast<int>(protocol::cascade_requeue_max) - (load_excess > 0 ? load_excess : 0);
    return eff > 0 ? eff : 0;
}

void Node::try_cascade_requeue(const PendingTx& pt, const char* giveup_event) {
    const uint64_t now = _hal.now();
    const bool count_done = pt.requeue_count >= protocol::cascade_requeue_max;
    const bool age_done   = (now - pt.enqueue_time_ms) >= protocol::cascade_requeue_total_max_ms;
    if (count_done || age_done || _active->_tx_queue_n >= kTxQueueCap) {
        MR_TELEMETRY(
            EventField f[] = { { .key = "dst", .type = EventField::T::i64, .i = pt.dst },
                               { .key = "ctr", .type = EventField::T::i64, .i = pt.ctr } };
            _hal.emit("path_cascade_exhausted", f, 2);
            _hal.emit(giveup_event, f, 2); );
        giveup_flight(giveup_fail_reason(giveup_event), pt.dst, pt.ctr);
        return;
    }
    // ④ load-adaptive shed: under a backed-up queue the budget shrinks below cascade_requeue_max, so a congested node
    // sheds cascade-waste instead of requeuing at the fixed budget. Same TERMINAL drop as the hard cap above (so the
    // analyzers still see path_cascade_exhausted + the giveup) + a cascade_load_skip marker. The kTxQueueCap overflow
    // above stays the absolute backstop. dv:6275-6303.
    if (static_cast<int>(pt.requeue_count) + 1 > cascade_effective_max(_active->_tx_queue_n)) {
        MR_TELEMETRY(
            EventField f[] = { { .key = "dst",         .type = EventField::T::i64, .i = pt.dst },
                               { .key = "ctr",         .type = EventField::T::i64, .i = pt.ctr },
                               { .key = "queue_depth", .type = EventField::T::i64, .i = _active->_tx_queue_n },
                               { .key = "eff_max",     .type = EventField::T::i64, .i = cascade_effective_max(_active->_tx_queue_n) } };
            _hal.emit("cascade_load_skip", f, 4);
            _hal.emit("path_cascade_exhausted", f, 2);
            _hal.emit(giveup_event, f, 2); );
        giveup_flight(giveup_fail_reason(giveup_event), pt.dst, pt.ctr);
        return;
    }
    TxItem it = txitem_from_pending(pt);   // S1: full identity+crypto core (incl. type + nonce_seed — the H4 drop)
    it.requeue_count = static_cast<uint8_t>(pt.requeue_count + 1);
    it.enqueue_time_ms = pt.enqueue_time_ms;             // PRESERVE the original first-enqueue time
    // The queue ITSELF enforces the backoff: next_attempt_ms gates the dequeue
    // (become_free scans for the first ready item), so a concurrent become_free
    // can't skip the hold. The timer is just the wakeup at the ready time.
    it.next_attempt_ms = now + requeue_backoff_ms(it.requeue_count);
    MR_EMIT("cascade_requeue", EF_I("dst", it.dst), EF_I("ctr", it.ctr), EF_I("requeue_count", it.requeue_count));
    _active->_tx_queue[_active->_tx_queue_n++] = it;                       // tail; held by next_attempt_ms until the backoff
    _active->_pending_tx.reset();
    (void)_hal.after(requeue_backoff_ms(it.requeue_count), kCascadeRequeueTimerId);
}

// An ORIGINATOR send with no usable route yet: hold it until a beacon installs a
// route (drain-on-rt_changed) or the periodic 1s drain ages it out by send_defer_ttl.
void Node::defer_send(const TxItem& item) {
    // §S0 giveup: this send has drained (a route appeared) then bounced back here too many times — a route EXISTS but is
    // never selectable (an aliased-mobile / gateway transit next-hop). Each re-defer RE-STAMPS deferred_at_ms, so the
    // send_defer_ttl giveup below can never age it out (the metal "re-drain every 1s forever" burn). Fail loud instead of
    // re-parking. s18-inert: s18 never drains a deferred send (redrain_count stays 0). See send_defer_max_redrains.
    if (item.redrain_count >= protocol::send_defer_max_redrains) {
        MR_EMIT("send_deferred_giveup", EF_I("dst", item.dst), EF_I("ctr", item.ctr));
        push_send_failed(SendFailReason::no_route, item.dst, item.ctr);
        return;
    }
    if (_active->_deferred_n >= protocol::cap_deferred_sends) {   // full -> REFUSE the NEW send (Lua table_cap_hit
        // dv:5549-5553), NOT drop-oldest. Complete the app future so it never hangs.
        MR_EMIT("send_deferred_refused", EF_I("dst", item.dst), EF_I("ctr", item.ctr));
        push_send_failed(SendFailReason::queue_full, item.dst, item.ctr);   // was reason=none -> a reason-LESS send_failed (the emit above is device-stripped, so this Push is the app's only signal)
        return;
    }
    DeferredSend d{}; d.item = item; d.deferred_at_ms = _hal.now();
    _active->_deferred[_active->_deferred_n++] = d;
    MR_EMIT("send_deferred", EF_I("dst", item.dst), EF_I("ctr", item.ctr));
    // §F-TR-2: discover the route on the SEND's OWN plane. A TEAM (or AUTO-resolved team-peer) dst must RREQ team-scoped —
    // a static RREQ for a team id is never self-answered by a DUAL owner (whose static node_id != its team_local_id), so the
    // route never installs and the send ages out. AUTO/static keeps team=false (is_team_peer is false for a static dst) -> byte-identical.
    const bool team_rreq = (item.plane == Plane::TEAM) || (item.plane == Plane::AUTO && is_team_peer(item.dst));
    emit_route_request(item.dst, 1, team_rreq);          // ask for a route: cheap ttl=1 probe (Lua emit_route_request)
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
            MR_EMIT("send_deferred_giveup", EF_I("dst", d.item.dst), EF_I("ctr", d.item.ctr));
            push_send_failed(SendFailReason::no_route, d.item.dst, d.item.ctr);   // §3-A.5: match the sibling defer_send giveup in defer_send() — was reason=none
            continue;                                    // drop (don't keep)
        }
        RtEntry* e = rt_find(d.item.dst, d.item.plane);   // Wave 2: drain on the item's OWN plane — a GLOBAL item must NOT be drained by a team route (AUTO would match _rt_team for a colliding team id -> drain -> re-issue GLOBAL -> no route -> re-defer -> re-stamp -> never ages out = the RREQ storm)
        if (e != nullptr && e->n > 0) {
            MR_EMIT("send_drained", EF_I("origin", d.item.origin), EF_I("dst", d.item.dst), EF_I("ctr", d.item.ctr),
                    EF_I("waited_ms", static_cast<int64_t>(now - d.deferred_at_ms)));  // route appeared (dv:6953) — the held send flies
            d.item.redrain_count++;                      // §S0: count this drain — if the route proves unusable at select and the item bounces back to defer_send, the giveup bound breaks the 1s restamp loop
            drained[drained_n++] = d.item;               // route appeared -> drain to the queue HEAD below
            continue;
        }
        const bool team_rreq = (d.item.plane == Plane::TEAM) || (d.item.plane == Plane::AUTO && is_team_peer(d.item.dst));   // §F-TR-2: requery on the item's OWN plane (team dst -> team-scoped RREQ)
        // §team-parity T1: ★ DONE — the requery TTL is now the item's OWN plane's radius (team_hop_cap 8 / dv_hop_cap
        // 16), matching the plane the requery already flooded on. This is the TTL escalation the spec's §3/T1 table
        // calls out at +1 s: the ttl=1 probe fired by defer_send, then this at full plane radius. The escalation
        // bypasses the rreq_rate_ok window precisely because the ttl grows (node_route_discovery.cpp:66-70), so the
        // team radius must be the one the team ledger records — with the static 16 here a subsequent legitimate
        // team requery at 8 would read as a DE-escalation and be suppressed.
        // Static reduction: team_rreq==false ⇒ hop_cap_for(false) == _cfg.dv_hop_cap, the pre-T0 expression verbatim.
        // Coverage: 783 corpus executions, ALL static (team half dark) → native
        // test_node_r3.cpp "§team-parity T1 — the deferred-drain requery escalates to team_hop_cap on a team item".
        emit_route_request(d.item.dst, hop_cap_for(/*team_plane=*/team_rreq), team_rreq); // still no route -> requery at full plane radius (rate-limited)
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
    // Gateway-doorstep hold (Lua dv:6452): an RTS to a KNOWN gateway on its doorstep hop timed out.
    // Patient window-aware requeue instead of burning retries or fanning out to cascade — the
    // gateway may simply be away on its other leaf. Handles its own giveup clock (150s).
    if (gateway_doorstep_hold()) return;
    if (_active->_pending_tx->retries_left > 0) {
        // §P3 silent-next cascade: the primary is ALREADY known silent/dead (prior-flight liveness evidence) ->
        // don't burn same-hop retries on a confirmed-dead path; cascade to a viable alt NOW (or RREQ on no-alt).
        // Reads the persisted tier (no per-timeout counting — that churned the suite). DRIFT from the spec's literal
        // per-failure/suspect trigger: gated on SILENT (confirmed flaky), not suspect; see the phase report.
        if (liveness_penalty_q4(_active->_pending_tx->next, _active->_pending_tx->plane == Plane::TEAM) >= protocol::peer_silent_penalty_q4) {   // §2c: a TEAM flight reads TEAM liveness (mirror of ack_timeout_fire; audit-caught missed twin)
            cascade_to_alt("rts_silent_cascade");
            return;
        }
        --_active->_pending_tx->retries_left;
        // Capped exponential backoff (spec 2026-06-26): the same-hop retry window doubles per attempt up to
        // retry_backoff_max_shift, so saturated contenders spread out instead of re-colliding in a flat window.
        // max_shift=0 -> window == retry_jitter_ms() == today's flat retry (the rand_range call/order is unchanged -> sim parity).
        const uint32_t window = protocol::retry_backoff_window(retry_jitter_ms(), _active->_pending_tx->retry_attempt, protocol::retry_backoff_max_shift);
        const int jit = _hal.rand_range(0, static_cast<int>(window) + 1);   // RNG site #1 (SAME call/order)
        (void)_hal.after(static_cast<uint32_t>(jit), kRetryBackoffTimerId);
        ++_active->_pending_tx->retry_attempt;
    } else {
        // §mobile (plane-separation re-audit): a mobile/team flight's next-hop is a LOCAL id -> keep it OUT of the static
        // _peer_liveness plane (else a §18-colliding static node gets suspected/DEAD-marked from a mobile flight's timeout,
        // penalising its real route via liveness_penalty_q4). Mirror the RX-side learn/blind guards.
        if (!(next_is_local_id(_active->_pending_tx->addr_len, _active->_pending_tx->next)))
            record_peer_rts_timeout(_active->_pending_tx->next, _active->_pending_tx->ctr_lo);   // §P1: same-hop RTS giveup = liveness evidence
#if MR_FEAT_TEAM
        else if (is_team_peer(_active->_pending_tx->next))
            record_peer_rts_timeout(_active->_pending_tx->next, _active->_pending_tx->ctr_lo, /*team_plane=*/true);   // §2c: a team next-hop's giveup accrues TEAM liveness (proactive-demotion evidence)
#endif
        cascade_to_alt("rts_giveup");                    // same-hop retries exhausted -> walk to an alternate (§P3: + RREQ if it's silent)
    }
}
void Node::ack_timeout_fire() {
    if (!_active->_pending_tx || !_active->_pending_tx->awaiting_ack) return;
    if (_active->_pending_rx) { (void)_hal.after(protocol::rts_busy_retry_ms, kAckTimeoutTimerId); return; }
    // Gateway-doorstep hold (Lua dv:6661): same as rts_timeout_fire — a DATA-ACK timeout to a
    // known gateway on its doorstep hop also gets patient window-aware requeue.
    if (gateway_doorstep_hold()) return;
    if (_active->_pending_tx->retries_left > 0) {
        // §P3 silent-next cascade (mirror of rts_timeout_fire): a missed DATA-ACK on an ALREADY-silent primary
        // cascades immediately rather than re-RTSing the dead path. Persisted-tier read, no per-timeout counting.
        if (liveness_penalty_q4(_active->_pending_tx->next, _active->_pending_tx->plane == Plane::TEAM) >= protocol::peer_silent_penalty_q4) {   // §2c: a TEAM flight reads TEAM liveness (not static _peer_liveness[team_id])
            cascade_to_alt("data_ack_silent_cascade");
            return;
        }
        --_active->_pending_tx->retries_left;
        _active->_pending_tx->awaiting_ack = false; _active->_pending_tx->awaiting_cts = false; _active->_pending_tx->chosen_data_sf = 0;
        // Capped exponential backoff (spec 2026-06-26) — identical to RNG site #1; max_shift=0 -> today's flat retry.
        const uint32_t window = protocol::retry_backoff_window(retry_jitter_ms(), _active->_pending_tx->retry_attempt, protocol::retry_backoff_max_shift);
        const int jit = _hal.rand_range(0, static_cast<int>(window) + 1);   // RNG site #2 (SAME call/order)
        (void)_hal.after(static_cast<uint32_t>(jit), kRetryBackoffTimerId);
        ++_active->_pending_tx->retry_attempt;
    } else {
        // §mobile (plane-separation re-audit): a mobile/team LOCAL-id next-hop must not enter the static _peer_liveness plane.
        if (!(next_is_local_id(_active->_pending_tx->addr_len, _active->_pending_tx->next)))
            record_peer_rts_timeout(_active->_pending_tx->next, _active->_pending_tx->ctr_lo);   // §P1: same-hop ACK giveup = liveness evidence
#if MR_FEAT_TEAM
        else if (is_team_peer(_active->_pending_tx->next))
            record_peer_rts_timeout(_active->_pending_tx->next, _active->_pending_tx->ctr_lo, /*team_plane=*/true);   // §2c: a team next-hop's giveup accrues TEAM liveness
#endif
        cascade_to_alt("data_ack_giveup");               // same-hop retries exhausted -> walk to an alternate (§P3: + RREQ if it's silent)
    }
}
// Gateway-doorstep hold (Lua gateway_doorstep_hold@6351): an RTS/ACK to a known gateway on its
// doorstep hop (next==dst) timed out. Instead of burning same-hop retries or cascading to alts
// (both would hit the same absent gateway), patient window-aware requeue: wait until the gateway's
// next window on our leaf + jitter. Separate giveup clock (150s ≈ 10 visit windows) so the
// message isn't lost on a transient window miss but DOES give up eventually.
bool Node::gateway_doorstep_hold() {
    const PendingTx& pt = *_active->_pending_tx;
    if (pt.next != pt.dst) return false;                             // only the doorstep hop (last-hop to the gateway)
    const GatewaySchedule* gs = find_gw_schedule(pt.dst);
    if (!gs || !gs->valid) return false;                             // no known schedule for this gateway
    const uint64_t now = _hal.now();
    const uint64_t enq = pt.enqueue_time_ms ? pt.enqueue_time_ms : now;
    // The ENTRY test (pre-existing behaviour, unchanged): a hold entered past the bound gives up here. It is NOT
    // what bounds the horizon. ⚠ WITHDRAWN: this line used to name `issue_send`'s start boundary as the bound —
    // that check was removed, and the TERMINAL authority is `DeviceHal::pump_tx()` before `start_transmit()`.
    if (gateway_deadline_expired(enq, now, pt.origin, pt.dst, pt.ctr)) {
        giveup_flight(SendFailReason::gateway_unreachable, pt.dst, pt.ctr);   // §3-A.5: was reason=none (telemetry-only)
        return true;
    }
    const uint32_t wait    = gateway_schedule_defer_ms(pt.dst);
    const uint32_t jitter  = static_cast<uint32_t>(_hal.rand_range(0, static_cast<int>(protocol::gateway_doorstep_retry_jitter_ms) + 1));
    uint32_t       backoff = wait + jitter;
    if (backoff < 200) backoff = 200;
    TxItem it = txitem_from_pending(pt);   // S1: full identity+crypto core (incl. nonce_seed — the M7b drop)
    it.requeue_count  = pt.requeue_count;                         // preserved — NOT incremented
    it.enqueue_time_ms = enq;                                      // preserved — giveup clock spans lifetime
    it.next_attempt_ms = now + backoff;
    MR_EMIT("gateway_hold_requeue", EF_I("origin", pt.origin), EF_I("dst", pt.dst), EF_I("ctr", pt.ctr),
            EF_I("wait_ms", wait), EF_I("jitter_ms", jitter), EF_I("backoff_ms", backoff),
            EF_I("age_ms", static_cast<int64_t>(now - enq)));   // ⛔ inlined, NOT a local: a variable used only
            // inside MR_EMIT is stripped on every device build and warns -Wunused-variable ([[B169]] class) —
            // invisible to native AND the corpus, caught only by the warning census.
    if (_active->_tx_queue_n < kTxQueueCap) _active->_tx_queue[_active->_tx_queue_n++] = it;
    _active->_pending_tx.reset();
    become_free();
    return true;
}
void Node::pending_rx_expiry_fire() {
    if (!_active->_pending_rx) return;
    _hal.set_rx_sf(_cfg.routing_sf);
    _active->_pending_rx.reset();
    MR_TELEMETRY( _hal.emit("data_rx_timeout", nullptr, 0); );
    become_free();
}

}  // namespace meshroute
