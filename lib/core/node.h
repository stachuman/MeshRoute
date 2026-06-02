// MeshRoute — lib/core/node.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// The protocol node. Depends ONLY on hal.h (no Arduino/RadioLib/sol/json), so
// it runs unchanged on both HAL backends: FirmwareNode in the simulator and the
// MeshCore-PHY device backend. Bounded, fixed-size state (no heap in hot paths).
//
// R1 (beacon emit): periodic §10 BCN (C5 pack_beacon) over each rt[dest] primary
// + DV route-table merge on RX (beacon_rx / rt_update / rt_full).
// R2 (route-plane hardening): route aging/TTL eviction, the 3-cycle prune,
// dirty-only differential beacons + paging, the triggered beacon kind, and the
// discovery fast-cadence state machine. The MAC/data plane (RTS/CTS/DATA/ACK) and
// the adaptive throttle are later R-iterations. See docs/specs/2026-05-29-r1-*
// and 2026-05-30-r2-route-hardening-design.md.
#pragma once
#include "hal.h"
#include "command.h"
#include "protocol_constants.h"
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>
#include <optional>

namespace meshroute {

// POD; no heap, no JSON. Only the T/F-class knobs the Lua on_init reads.
// PROTOCOL constants stay in protocol_constants.h (hardcoded on device).
// TODO: Node config should also store name - which can be then exchanged through higher level (app level) - along with the public key
// TODO: Each layer (leaf) has its crypt key - shared during join operation. This key is used to crypt portion of BCN - why?
// TODO: It is to prevent joining leaf without a proper join process AND possibly - to make private leafs
// TODO: To discuss hot wo implement / what technology
// TODO: Versions - in JOIN one byte - it shows only wire compatibility, not the full version. If same - it doesn't mean - it is the same version
// TODO: it means - it is wire compatible
struct NodeConfig {
    bool     is_gateway          = false;
    bool     is_mobile           = false;
    bool     join_required       = false;
    bool     req_sync_on_boot    = true;
    bool     seen_bitmap_enabled = true;
    uint8_t  routing_sf          = 7;
    uint16_t allowed_sf_bitmap   = 0;            // allowed DATA-SF set (bit=sf), from config allowed_data_sfs; 0 = use data_sf
    uint32_t beacon_period_ms    = 900000;
    uint32_t beacon_max_idle_ms  = 900000;
    uint8_t  req_sync_min_routes = 8;
    uint32_t quiet_threshold_ms  = 30000;        // beacon throttle gate; <=0 = unthrottled (R1 fast path)
    uint8_t  leaf_id             = 0;            // layer id (single-layer R1 = 0)
    uint16_t peer_count          = 0;            // host-set (N-1); 0 = no rt_full emit (sim telemetry)
    // R2 route-aging TTLs (config-overridable so a gate can shrink them; Lua
    // reads `config.X or <constant>`). hops<=1 uses neighbor, else remote.
    uint32_t rt_aging_ttl_neighbor_ms = protocol::rt_aging_ttl_neighbor_ms;  // 45 min
    uint32_t rt_aging_ttl_remote_ms   = protocol::rt_aging_ttl_remote_ms;    //  3 h
    uint32_t rt_aging_check_period_ms = protocol::rt_aging_check_period_ms;  // 60 s
    // R3 data plane: radio params for floor-exact airtime (timeout/retry sizing).
    uint32_t radio_bw_hz = 250000;
    uint8_t  radio_cr    = 5;
    uint8_t  data_sf     = 12;       // preferred data SF (CTS picks it for sf_index=ANY)
    uint8_t  dv_hop_cap  = protocol::dv_hop_cap;  // DV route hop cap + F RREQ TTL. Network-wide: set via the J join
                                                  // frame (Slice 3); static config is the bootstrap/fallback. Default 16.
    // R4.0 duty-cycle budget. Default OFF (0.0) so every prior gate stays HEALTHY/inert; a
    // budget scenario sets duty_cycle explicitly. budget_ms = floor(duty_cycle*window) at on_init.
    // (Lua default is 0.01; we default OFF — see spec §2. Lua dv:8495-8497.)
    double   duty_cycle           = 0.0;        // fraction of the window we may transmit; <=0 = disabled
                                                // (double, NOT float: floor(0.01*window) must match the Lua's
                                                //  double exactly — float 0.01f*3.6e6 floors to 35999, not 36000)
    uint32_t duty_cycle_window_ms = 3600000;    // rolling airtime window (1 h)
    uint16_t originator_max_per_window = 6;      // R4.4 anti-spam: apparent_origination drop threshold (T-class)
    uint32_t beacon_silence_jitter_ms  = 10000;  // R4.3 adaptive-throttle deferred-TX spread (dv:921)
    // R4.5 listen-before-talk. lbt_enabled default true (Lua dv:8625). The two delays default to 0 = "derive
    // in on_init" (lbt_backoff_ms = max(1, retry_jitter_ms/2); flood_lbt_max_defer_ms = airtime(beacon_max_bytes)).
    bool     lbt_enabled               = true;
    uint32_t lbt_backoff_ms            = 0;       // 0 => derive
    uint32_t flood_lbt_max_defer_ms    = 0;       // 0 => derive
};

// One route candidate (DV). Mirrors the Lua rt[dest].candidates[i] fields
// (dv_dual_sf.lua:9646-9654). score is Q4 dB. effective_score = score − budget_penalty
// (R4.2; == score for a HEALTHY-tier next_hop) − suspect_penalty (0, deferred plane).
struct RtCandidate {
    uint8_t  next_hop         = 0;
    int16_t  score            = 0;   // Q4 dB
    uint8_t  hops             = 0;
    uint64_t last_seen_ms     = 0;
    uint8_t  n2_hop           = 0;   // advertised next-hop (for the R2 3-cycle prune)
    bool     is_gateway       = false;
    uint8_t  learned_layer_id = 0;
};
struct RtEntry {
    uint8_t     dest = 0;
    RtCandidate candidates[protocol::max_rt_candidates];
    uint8_t     n     = 0;           // candidates in use (1..K)
    bool        dirty = false;       // set when candidates[0] changes (R2/R4 dirty-only beacons)
};

// ---- R3 data-plane state (MAC) ---------------------------------------------
// inner = src_addr_len(1)|origin(1)|body — the DATA C6 inner (parse_unicast_inner).
struct TxItem {                      // a queued message awaiting a flight
    uint8_t  origin = 0, dst = 0, ctr_lo = 0;
    uint16_t ctr = 0;
    uint8_t  flags = 0;
    uint8_t  inner[protocol::max_payload_bytes_hard_cap] = {};
    uint8_t  inner_len = 0;
    bool     is_forward = false;     // true => previous_hop valid (a relayed item)
    uint8_t  previous_hop = 0;
    // Cascade-requeue meta (the Lua queue_meta): requeue_count drives the
    // exponential backoff cap; enqueue_time_ms is the ORIGINAL first-enqueue
    // time, preserved across every requeue so the total-age cap is honest;
    // next_attempt_ms gates the dequeue so the backoff can't be skipped by a
    // concurrent become_free (the queue itself enforces the hold).
    uint8_t  requeue_count = 0;
    uint64_t enqueue_time_ms = 0;
    uint64_t next_attempt_ms = 0;
    // Hop-budget carried forward on a relayed item (a forwarder's already-decremented
    // values; originators recompute from rt). Ignored unless is_forward.
    uint8_t  fwd_remaining = 0;
    uint8_t  fwd_committed = 0;
};
struct PendingTx {                   // the in-flight sender state (one per node)
    uint8_t  origin = 0, dst = 0, next = 0, ctr_lo = 0;
    uint16_t ctr = 0;
    uint8_t  flags = 0;
    uint8_t  inner[protocol::max_payload_bytes_hard_cap] = {};
    uint8_t  inner_len = 0;
    uint8_t  chosen_data_sf = 0;     // 0 = unset until the CTS arrives
    uint8_t  retries_left = 0;
    bool     awaiting_cts = false;
    bool     awaiting_ack = false;
    // Cascade-to-alt state: which next-hops this flight has already tried (so the
    // walk never re-picks them), the upstream hop to avoid looping back to, and
    // the requeue meta threaded from the TxItem.
    uint8_t  previous_hop = 0;
    bool     has_previous_hop = false;
    uint8_t  alts_tried[protocol::max_rt_candidates] = {};
    uint8_t  alts_tried_n = 0;
    uint8_t  requeue_count = 0;
    uint64_t enqueue_time_ms = 0;
    // Hop-budget for a forwarded flight (inherited from the received DATA, already
    // decremented in handle_data). For an originator, do_data_tx recomputes from rt.
    uint8_t  fwd_remaining = 0;
    uint8_t  fwd_committed = 0;
    // Monotonic flight identity (bumped on each new pending_tx at issue_send). The C++ equivalent of the Lua's
    // object-identity guard `__pending_tx_ref` (dv:3712) — an exact staleness key for a deferred RTS, replacing the
    // 4-bit ctr_lo proxy that wraps every 16 sends (cleanup #A redo). cascade_to_alt mutates in place (same gen).
    uint32_t flight_gen = 0;
};
struct DeferredSend {                // a send with no route yet — held until one appears (or TTL)
    TxItem   item;
    uint64_t deferred_at_ms = 0;     // for the send_defer_ttl giveup (TTL checked FIRST on drain)
};
struct PendingRx {                   // the receiver state awaiting DATA (one per node)
    uint8_t  from = 0, dst = 0, ctr_lo = 0, chosen_data_sf = 0, payload_len = 0;
    uint64_t set_at_ms = 0;
    uint64_t expiry_ms = 0;          // absolute DATA-wait expiry (for the BUSY_RX NACK busy_for calc)
};
struct PostAck {                     // deferred deliver/forward after the ACK airtime
    bool     pending = false;
    bool     is_forward = false;     // false => deliver (dst==self); true => forward
    uint8_t  origin = 0, dst = 0, ctr_lo = 0, previous_hop = 0;
    uint16_t ctr = 0;
    uint8_t  flags = 0;
    uint8_t  inner[protocol::max_payload_bytes_hard_cap] = {};
    uint8_t  inner_len = 0;
    // Hop-budget for the forward (the decremented values from handle_data); copied
    // into the forward TxItem in do_post_ack.
    uint8_t  fwd_remaining = 0;
    uint8_t  fwd_committed = 0;
};
struct LastAcked { uint8_t chosen_data_sf = 0; uint64_t t_ms = 0; };

class Node {
public:
    Node(Hal& hal, uint8_t node_id, uint32_t key_hash32, const char* name = nullptr);

    void on_init(const NodeConfig& cfg);                                 // cfg borrowed
    void on_recv(const uint8_t* bytes, size_t len, const RxMeta& meta);  // bytes valid during call only
    void on_timer(uint32_t timer_id);                                    // dispatch on Node-owned id
    void on_radio_busy(const BusyInfo& info);                            // deferred-TX retry/giveup
    void on_preamble_detected(uint64_t time_ms);                         // SX1262 IRQ / throttle witness
    CmdResult on_command(const Command& c);                              // the typed app<->firmware seam
    bool      next_push(Push& out);                                      // drain the async push ring (CMD_SYNC_NEXT)

    // Exposed for the R3.x determinism golden test. The retry-jitter RANGE is a
    // cross-engine alignment contract: 3*airtime_routing(RTS_LEN=8) must equal
    // the Lua's, or the lua-vs-meshroute forced-retry streams de-align (see the
    // node.cpp definition comment). Pure, const, no side effects.
    uint32_t  retry_jitter_ms() const;                                   // 3*airtime(routing, RTS_LEN=8)

    // R4.0 duty-cycle budget tier (route-free; from the rolling airtime window). Lua dv:3555-3571.
    // Public for the tier-table unit test (the emit only observes >=CRITICAL, so the HEALTHY/STRAINED
    // boundary needs a direct call). Pure, const.
    enum class BudgetTier : uint8_t { healthy = 0, strained = 1, critical = 2, exhausted = 3 };
    BudgetTier compute_budget_tier() const;                              // HEALTHY when duty_cycle<=0 (disabled)
    bool       is_blind(uint8_t next_hop) const;                         // _blind_until active? (read-only; bounded by neighbour count)
    uint8_t    get_neighbor_tier(uint8_t node_id) const;                 // R4.2 tier read (TTL-expiring lazy-prune); public for tests
    void       schedule_triggered_beacon();                             // R4.3 trigger jitter + min-interval defer; public for tests
    int        mark_neighbor_budget_tier(uint8_t node_id, uint8_t tier, const char* source, bool local_only); // :4320; public for tests
    // R4.4 originator anti-spam (dv:3205-3277). track = ledger append (prune+dedup-first); compute = the
    // sliding-window metric. kind: 0=rts, 1=cts. Draw-free. Public for tests.
    void       track_originator_observation(uint8_t sender, uint8_t kind, uint8_t ctr_lo, uint32_t air);
    void       compute_originator_metric(uint8_t sender, int& apparent, uint32_t& total_air,
                                         uint8_t& rts, uint8_t& cts) const;

private:
    // Node-owned timer-id namespace (Hal::after re-arm-by-id, cap 64). Reserve
    // 4+ for the R3 RTS/CTS/ACK timers.
    static constexpr uint32_t kBeaconTimerId           = 1;
    static constexpr uint32_t kAgingTimerId            = 2;
    static constexpr uint32_t kTriggeredBeaconTimerId  = 3;
    // R3 data-plane (MAC) timers — single-flight per node, so one live instance each.
    static constexpr uint32_t kRtsTimeoutTimerId       = 4;   // sender: CTS-wait
    static constexpr uint32_t kAckTimeoutTimerId       = 5;   // sender: ACK-wait
    static constexpr uint32_t kPendingRxExpiryTimerId  = 6;   // receiver: DATA-wait
    static constexpr uint32_t kCtsToDataGapTimerId     = 7;   // sender: CTS-rx -> DATA-tx gap
    static constexpr uint32_t kQueueWakeupTimerId      = 8;   // become_free: not-ready re-arm
    static constexpr uint32_t kPostAckTimerId          = 9;   // receiver: ACK-air -> deliver/forward
    static constexpr uint32_t kRetryBackoffTimerId     = 10;  // sender: jittered RTS retry
    // Cascade-to-alt / no-route defer plane.
    static constexpr uint32_t kDeferredDrainTimerId    = 11;  // periodic 1s drain of _deferred (TTL giveup)
    static constexpr uint32_t kCascadeRequeueTimerId   = 12;  // backoff before re-draining a requeued flight
    static constexpr uint32_t kNackWaitTimerId         = 13;  // NACK BUSY_RX wait-same-hop one-shot
    static constexpr uint32_t kBeaconJitterTimerId     = 27;  // R4.3 silence-jitter deferred periodic beacon; BASE of a 4-slot ring [27..30] (cleanup #D: two defers in one jitter window must BOTH fire, dv per-closure)
    static constexpr uint8_t  kBeaconJitterSlots       = 4;
    static constexpr uint32_t kRtsDutyDeferTimerId     = 31;  // cleanup #A redo: over-budget RTS duty-defer re-check/hand
    static constexpr uint32_t kLbtDeferTimerId         = 15;  // R4.5 LBT deferred-TX re-fire; BASE of a 4-slot range [15..18]
    static constexpr uint32_t kRadioBusyRetryTimerId   = 19;  // R4.5b on_radio_busy stash re-issue; BASE of a 4-slot range [19..22]
    static constexpr uint32_t kDutyDeferTimerId        = 23;  // tx_with_retry duty-cycle pre-check defer; BASE of a 4-slot range [23..26]

    // ---- beacon emit / ingest ----------------------------------------------
    void emit_beacon(const char* kind);                            // "periodic" | "triggered"
    void periodic_beacon_fire();                                   // R4.3 throttle body (dv:7695-7851)
    void deferred_beacon_jitter_fire(uint8_t slot);                // R4.3 post-silence-jitter re-check (dv:7801-7849); #D ring slot
    bool _beacon_jitter_pending[kBeaconJitterSlots] = {};          // #D: which ring slots have a deferred periodic beacon armed
    bool beacon_max_idle_force(uint64_t now, bool emit_events);    // R4.3 max-idle B+C override (dv:7734-7784)
    void ingest_beacon(const uint8_t* bytes, size_t len, const RxMeta& meta);
    int16_t route_score_from_snr(int16_t snr_q4) const;            // dv_dual_sf.lua:3053
    // Direct (hops=1) neighbour learning from a received frame's immediate sender — the C++
    // learn_rx_source / learn_direct_from_frame. Returns true on a real change (new/promote/
    // refresh) so the caller can fire the triggered beacon. sender must be a real id (0..254);
    // 0xFF (unknown/reserved) and self are no-ops. C++ has no id-bind/dest-seen/liveness plane,
    // so (unlike the Lua) those sub-actions are absent.
    bool    learn_direct_neighbor(uint8_t sender, int16_t snr_q4, bool is_gw);
    void    learn_route_via(uint8_t dest, uint8_t via, uint8_t hops, int16_t snr_q4);  // multi-hop install (F path)
    // F route discovery (AODV RREQ/RREP) — node_route_discovery.cpp.
    void    handle_f(const uint8_t* bytes, size_t len, const RxMeta& meta);
    void    emit_route_request(uint8_t dst, uint8_t ttl);
    void    send_route_reply(uint8_t origin, uint8_t dst, uint8_t hops_to_dst);
    bool    rreq_seen_recently(uint8_t origin, uint8_t dst);
    void    mark_rreq_seen(uint8_t origin, uint8_t dst);
    bool    rreq_rate_ok(uint8_t dst, uint8_t ttl);

    // ---- route table (DV merge) --------------------------------------------
    enum class MergeAction : uint8_t { none, new_dest, primary_refresh, promote, alt_install };
    RtEntry*    rt_find(uint8_t dest);
    RtEntry*    rt_insert(uint8_t dest);                           // sorted insert; nullptr if full
    void        rt_remove(uint8_t idx);                            // R2: drop _rt[idx], keep sort
    MergeAction rt_merge(uint8_t dest, const RtCandidate& cand);   // dv_dual_sf.lua:4484
    void        sort_candidates(RtEntry& e);
    // route_strictly_better/effective_score take the candidate LIST (cands,n) as context so the
    // R4.2 budget penalty can count viable alternatives (Lua signature (a,b,viab,candidates)). The
    // penalty is 0 for every HEALTHY-tier next_hop, so effective_score == score until a tier is marked.
    bool        route_strictly_better(const RtCandidate& a, const RtCandidate& b,
                                      const RtCandidate* cands, uint8_t n) const;  // :4227
    int16_t     effective_score(const RtCandidate& c, const RtCandidate* cands, uint8_t n) const; // :4050
    int16_t     budget_penalty_q4(const RtCandidate& c, const RtCandidate* cands, uint8_t n) const; // :3887
    int         resort_routes_for_neighbor_penalty(uint8_t node_id, const char* source, bool local_only);      // :4255
    RtEntry*    refresh_route_order(uint8_t dst, const char* reason);   // re-sort ONE dest's candidates (catch a tier change since the last sort), dv:4455
    void        maybe_emit_rt_full();

    // ---- R2 route-plane hardening ------------------------------------------
    void     age_out_stale_routes();                               // dv_dual_sf.lua:5249
    uint32_t ttl_for_hops(uint8_t hops) const;                     // hops<=1 neighbor else remote
    void     rt_prune_cycle(uint8_t dest, uint8_t sender);         // 3-cycle prune  :5193
    bool     in_discovery() const { return _discovery_mode; }
    void     maybe_exit_discovery(const char* reason);            // :7517

    // ---- R3 data plane (MAC: RTS-CTS-DATA-ACK) -----------------------------
    uint16_t do_send(uint8_t dst, const uint8_t* body, uint8_t body_len, uint8_t flags);  // returns the ctr
    uint16_t enqueue_data(uint8_t dst, const uint8_t* body, uint8_t body_len, uint8_t flags, const char* tx_event);
    void     send_e2e_ack(uint8_t to_origin, uint16_t acked_ctr);          // E2E ACK reply (E2E_IS_ACK; e2e_ack_tx)
    void     enqueue_push(const Push& p);                                  // append to the bounded ring
    void     become_free();                                       // dv_dual_sf.lua:7433 (FIFO single-drain)
    void     issue_send(const TxItem& item);                      // :7018 pending_tx + RTS
    void     clear_nack_wait() { _hal.cancel(kNackWaitTimerId); _nack_wait_pending = false; }   // drop a stale BUSY_RX wait
    void     handle_rts (const uint8_t* b, size_t n, const RxMeta& m);   // on_recv 'R' -> CTS
    void     handle_cts (const uint8_t* b, size_t n, const RxMeta& m);   // on_recv 'C' -> DATA
    void     handle_data(const uint8_t* b, size_t n, const RxMeta& m);   // on_recv 'D' -> deliver/forward + ACK
    void     handle_ack (const uint8_t* b, size_t n, const RxMeta& m);   // on_recv 'K' -> done
    void     handle_nack(const uint8_t* b, size_t n, const RxMeta& m);   // on_recv 'N' -> blind+wait / cascade
    void     do_data_tx();                                        // kCtsToDataGapTimerId fire
    void     do_post_ack();                                       // kPostAckTimerId fire (deliver|forward)
    void     start_rts_timeout();
    void     start_ack_timeout();
    void     start_pending_rx_expiry(uint8_t payload_len);
    void     rts_timeout_fire();                                  // :6326
    void     ack_timeout_fire();                                  // :6546
    void     pending_rx_expiry_fire();                            // :6699
    void     tx_rts_retry();                                      // re-pack SAME-ctr_lo RTS
    // R4.5 listen-before-talk. tx_initiating wraps an INITIATING TX (RTS/handle_rts NACK) — LBT pre-check,
    // defer at busy_until + rand(0,lbt_backoff+1) (dv:3680); tx_flood wraps a beacon (LBT + max-defer DROP +
    // duty pre-check, dv:3765); lbt_complete runs the deferred TX (+ the RTS staleness check + start_rts_timeout).
    enum class LbtKind : uint8_t { rts = 0, nack = 1, flood = 2 };
    // R4.5b frame-type tag (echoed by the sim in on_radio_busy; identifies a blocked TX heap-free).
    enum class FrameTag : uint16_t { rts = 0, cts = 1, data = 2, ack = 3, nack = 4, beacon = 5 };
    void     tx_initiating(const uint8_t* bytes, size_t len, int16_t sf, LbtKind kind, uint32_t rts_flight_gen);
    void     rts_duty_defer_fire();                                // cleanup #A redo: re-check duty + hand the deferred RTS (or re-defer / drop-if-stale)
    bool     tx_flood(const uint8_t* bytes, size_t len, int16_t sf);   // false = dropped/skipped (no TX)
    void     lbt_complete(const uint8_t* bytes, size_t len, int16_t sf, LbtKind kind, uint32_t rts_flight_gen);
    bool     schedule_lbt_defer(const uint8_t* bytes, size_t len, int16_t sf, LbtKind kind,   // free-slot stash
                                uint32_t rts_flight_gen, uint32_t delay);   // false = ring full (dropped)
    // R4.5b: the central TX helper (Lua tx_with_retry dv:3599) — stash the retry-eligible frame + set the
    // frame-type tag + duty pre-check + _hal.tx. Every TX except the beacon routes through it.
    bool     tx_with_retry(const uint8_t* bytes, size_t len, int16_t sf, FrameTag tag);   // returns handed (false on a duty defer)
    void     retry_stashed(uint8_t slot);                          // re-issue a stashed frame (kRadioBusyRetryTimerId+slot)
    void     duty_defer_fire(uint8_t slot);                        // re-run tx_with_retry from the stash after a duty defer (kDutyDeferTimerId+slot)
    bool     duty_over_budget(size_t len, int16_t sf, uint32_t* wait_ms);   // check_duty_cycle dv:3573; *wait_ms = defer time when over budget
    static int retry_slot_of(FrameTag tag);                        // FrameTag -> stash slot (0..3) or -1 (not eligible)
    static const char* label_of_frame(FrameTag tag);              // FrameTag -> "RTS"/"CTS"/...
    // ---- cascade-to-alt walk + no-route defer+Q ----------------------------
    uint8_t  pick_next_cascade_hop(const PendingTx& pt);          // two-pass walk :5430; 0 = none (NON-const: refreshes the route order first)
    bool     next_hop_selectable(const RtCandidate& c, const PendingTx& pt,
                                 bool allow_uphill) const;        // minimal filter :3990
    void     cascade_to_alt(const char* trigger);                 // on giveup: switch hop or requeue :6456
    void     try_cascade_requeue(const PendingTx& pt, const char* giveup_event);  // exhaustion -> requeue/giveup :6190
    uint32_t requeue_backoff_ms(uint8_t requeue_count) const;     // pure base*2^(n-1) capped :6209
    uint8_t  effective_rts_max_retries(uint8_t requeue_count) const;  // max(0, max-requeue_count) :3119
    void     defer_send(const TxItem& item);                      // no route yet -> hold (originator) :5545
    void     try_drain_deferred();                                // TTL-first, route-exists drain :6765
    bool     alt_tried(const PendingTx& pt, uint8_t hop) const;
    void     mark_tried(PendingTx& pt, uint8_t hop);
    uint16_t next_ctr(uint8_t dst);                               // per-(self,dst) counter (NOT rand)
    uint8_t  select_data_sf(uint8_t rts_sf_index, int16_t rx_snr_q4) const;  // adaptive DATA SF, Lua :3043+:3027
    uint32_t airtime_routing_ms(uint16_t len) const;             // floor-exact, for timeout sizing
    // retry_jitter_ms() is declared in the public section (R3.x golden test).

    Hal&     _hal;
    uint8_t  _node_id;            // reassignable via _hal.set_protocol_id (join/lease)
    uint32_t _key_hash32;         // stable long identity
    NodeConfig _cfg;             // borrowed copy from on_init
    int16_t  _routing_snr_floor_q4 = 0;   // SF_DEMOD_THRESHOLD[routing_sf] + sf_margin_q4
    RtEntry  _rt[protocol::cap_routes];
    uint8_t  _rt_count = 0;       // distinct dests, kept sorted ascending by dest
    bool     _rt_full_emitted = false;
    // R2 state
    uint8_t  _beacon_offset = 0;             // sliding stable-page rotation cursor
    bool     _discovery_mode = false;        // fast cadence + full pages until exit
    uint64_t _discovery_started_ms = 0;
    uint64_t _discovery_until_ms = 0;
    uint16_t _discovery_bcn_rx_count = 0;
    bool     _triggered_beacon_pending = false;  // coalesce: gates BEFORE the rand draw
    uint64_t _last_beacon_tx_ms = 0;
    uint64_t _duty_cycle_budget_ms = 0;          // R4.0: floor(duty_cycle*window), derived in on_init; 0 = disabled
    // R4.3 adaptive-throttle witnesses (channel-busy detector). Pure timestamps, no rand.
    uint64_t _last_rx_routing_sf_ms = 0;         // any successful decode OR preamble-detect (dv:9164/12231); 0 = never
    uint64_t _last_rx_bcn_ms        = 0;         // last beacon ingest (the max-idle B+C filter; dv:9559)
    // R4.5 LBT: derived delays (on_init) + a small RING of deferred-TX slots. The Lua uses independent
    // per-defer closures (dv:3704/3808), so two concurrent busy-channel defers BOTH fire — a single stash
    // would drop the first + desync the rand stream. Each slot has its own timer id (kLbtDeferTimerId+slot).
    // buf holds a full beacon (beacon_max_bytes=151) — a smaller buf would TRUNCATE a deferred page (review #04).
    uint32_t _lbt_backoff_ms        = 0;
    uint32_t _flood_lbt_max_defer_ms = 0;
    static constexpr uint8_t kLbtSlots = 4;
    struct DeferredLbt { bool pending = false; uint8_t kind = 0; uint8_t len = 0; int16_t sf = 0;
                         uint32_t rts_flight_gen = 0;   // RTS staleness key (flight_gen, not the old 4-bit ctr_lo proxy)
                         uint8_t buf[protocol::beacon_max_bytes] = {}; };
    DeferredLbt _deferred_lbt[kLbtSlots];
    // Cleanup #A redo: an over-budget RTS is duty-deferred in a DEDICATED slot (NOT the shared LBT ring — that reuse
    // was net-worse, review wgvbtirmu). One slot: there is only ever one pending_tx/flight. flight_gen staleness makes
    // the long (~1h) duty wait safe. kRtsDutyDeferTimerId fires rts_duty_defer_fire (re-check duty / hand / re-defer).
    uint32_t _flight_gen = 0;     // monotonic; bumped per new pending_tx (issue_send)
    struct RtsDutyDefer { bool pending = false; uint16_t len = 0; int16_t sf = 0; uint32_t flight_gen = 0;
                          uint8_t buf[16] = {}; };   // RTS pack is <=9 B (RTS_LEN 8 + M-broadcast 2)
    RtsDutyDefer _rts_duty_defer;
    // R4.5b on_radio_busy retry: a per-frame-type tag (echoed by the sim) lets on_radio_busy identify a blocked
    // TX (heap-free, no string label). The retry-eligible frames (CTS/DATA/ACK/NACK; RTS/beacon are NOT) are
    // STASHED so a busy-channel block re-issues them up to TX_DEFER_MAX_RETRIES. tx_stash keyed by the retry slot.
    static constexpr uint8_t kRetrySlots = 4;   // cts, data, ack, nack
    struct TxStashSlot { bool valid = false; uint16_t len = 0; int16_t sf = 0; uint8_t retries_left = 0;
                         uint8_t ctr_lo = 0;   // DATA slot: the pending_tx flight this DATA belongs to (re-arm guard, dv:10271)
                         uint8_t buf[protocol::lora_max_frame_bytes] = {}; };
    TxStashSlot _tx_stash[kRetrySlots];
    // R3 data-plane state (single flight per node)
    static constexpr uint8_t kTxQueueCap = 8;
    TxItem                   _tx_queue[kTxQueueCap];
    uint8_t                  _tx_queue_n = 0;          // FIFO depth
    std::optional<PendingTx> _pending_tx;
    std::optional<PendingRx> _pending_rx;
    PostAck                  _post_ack;
    // No-route defer queue (insertion-order array; drained TTL-first on a beacon
    // route-change or the 1s periodic timer). _drain_armed gates the periodic timer.
    DeferredSend             _deferred[protocol::cap_deferred_sends];
    uint8_t                  _deferred_n = 0;
    bool                     _drain_armed = false;
    // F route-discovery dedup state (Lua route_request_seen / route_request_last).
    struct RReqSeen { uint8_t origin; uint8_t dst; uint64_t t_ms; };   // relay flood-dedup
    struct RReqLast { uint8_t dst; uint8_t ttl; uint64_t t_ms; };      // per-dst origination rate-limit
    RReqSeen _rreq_seen[protocol::cap_route_request_seen] = {};
    uint8_t  _rreq_seen_n = 0;
    RReqLast _rreq_last[protocol::cap_route_request_last] = {};
    uint8_t  _rreq_last_n = 0;
    std::map<uint8_t, uint16_t>  _peer_send_counter;   // next_ctr per dst
    std::map<uint32_t, LastAcked> _last_acked_from;    // key (src<<24|dst<<16|ctr_lo<<8|len)
    std::map<uint32_t, uint64_t>  _seen_origins;       // key (origin<<24|dst<<16|ctr) -> expiry_ms
    std::map<uint32_t, uint8_t>   _seen_origin_from;   // same key -> the prev-hop (LOOP_DUP discriminator)
    std::map<uint8_t, uint64_t>   _blind_until;        // next_hop -> absolute_ms it's deaf-on-routing (F1)
    // R4.2 persistent neighbor budget tier (routing-grade demotion beyond the short blind window).
    // mutable: get_neighbor_tier lazy-prunes the TTL-expired entry on read, like the Lua (dv:3863-3868).
    mutable std::map<uint8_t, uint8_t>  _neighbor_budget_tier;       // next_hop -> tier (1..3); absent/0 = HEALTHY
    mutable std::map<uint8_t, uint64_t> _neighbor_budget_tier_set_at; // next_hop -> absolute_ms the mark was set
    // R4.4 originator anti-spam: per-sender sliding-window ledger of overheard RTS/CTS. kind: 0=rts, 1=cts.
    // FIXED RING (not a std::vector) — the old map-of-vectors rebuilt a vector on every overheard frame
    // (alloc/free per frame), which fragments the nRF52 heap; this keeps the events in a fixed in-struct
    // array (no per-frame heap), evicting the oldest on overflow. Insertion-ordered so the dedup-FIRST
    // refresh still matches the Lua ipairs scan. The std::map (sender -> ring) stays — its node alloc is
    // once per NEW sender (bounded by neighbours), not per frame (the accepted determinism relaxation).
    struct OrigEvent { uint64_t t; uint8_t kind; uint8_t ctr_lo; uint32_t air; };
    struct OrigRing  { OrigEvent ev[protocol::cap_originator_events]; uint8_t count = 0; };
    std::map<uint8_t, OrigRing> _per_sender_originator;  // sender_id -> recent events (fixed ring)
    // NACK BUSY_RX wait-same-hop: the captured ctr_lo the kNackWaitTimerId re-RTSes for.
    uint8_t                      _nack_wait_ctr_lo = 0;
    bool                         _nack_wait_pending = false;
    // async push ring (the app channel; drained via next_push, drop-oldest on overflow)
    Push     _push_ring[protocol::cap_push_ring];
    uint8_t  _push_head = 0, _push_count = 0;
};

}  // namespace meshroute
