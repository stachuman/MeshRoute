// MeshRoute — lib/core/node.h
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
    uint16_t allowed_sf_bitmap   = (1u << 12);   // dv_dual_sf sf_set_to_bitmap
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
};

// One route candidate (DV). Mirrors the Lua rt[dest].candidates[i] fields
// (dv_dual_sf.lua:9646-9654). score is Q4 dB. Budget/suspect penalties are 0
// until later R-iterations, so effective_score == score in R1.
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

    // ---- beacon emit / ingest ----------------------------------------------
    void emit_beacon(const char* kind);                            // "periodic" | "triggered"
    void ingest_beacon(const uint8_t* bytes, size_t len, const RxMeta& meta);
    int16_t route_score_from_snr(int16_t snr_q4) const;            // dv_dual_sf.lua:3053

    // ---- route table (DV merge) --------------------------------------------
    enum class MergeAction : uint8_t { none, new_dest, primary_refresh, promote, alt_install };
    RtEntry*    rt_find(uint8_t dest);
    RtEntry*    rt_insert(uint8_t dest);                           // sorted insert; nullptr if full
    void        rt_remove(uint8_t idx);                            // R2: drop _rt[idx], keep sort
    MergeAction rt_merge(uint8_t dest, const RtCandidate& cand);   // dv_dual_sf.lua:4484
    void        sort_candidates(RtEntry& e);
    bool        route_strictly_better(const RtCandidate& a, const RtCandidate& b) const;  // :4227
    void        maybe_emit_rt_full();

    // ---- R2 route-plane hardening ------------------------------------------
    void     age_out_stale_routes();                               // dv_dual_sf.lua:5249
    uint32_t ttl_for_hops(uint8_t hops) const;                     // hops<=1 neighbor else remote
    void     rt_prune_cycle(uint8_t dest, uint8_t sender);         // 3-cycle prune  :5193
    void     schedule_triggered_beacon();                          // single-draw (rate-limit -> R4)  :7877
    bool     in_discovery() const { return _discovery_mode; }
    void     maybe_exit_discovery(const char* reason);            // :7517

    // ---- R3 data plane (MAC: RTS-CTS-DATA-ACK) -----------------------------
    uint16_t do_send(uint8_t dst, const uint8_t* body, uint8_t body_len, uint8_t flags);  // returns the ctr
    void     enqueue_push(const Push& p);                                  // append to the bounded ring
    void     become_free();                                       // dv_dual_sf.lua:7433 (FIFO single-drain)
    void     issue_send(const TxItem& item);                      // :7018 pending_tx + RTS
    void     handle_rts (const uint8_t* b, size_t n, const RxMeta& m);   // on_recv 'R' -> CTS
    void     handle_cts (const uint8_t* b, size_t n, const RxMeta& m);   // on_recv 'C' -> DATA
    void     handle_data(const uint8_t* b, size_t n, const RxMeta& m);   // on_recv 'D' -> deliver/forward + ACK
    void     handle_ack (const uint8_t* b, size_t n, const RxMeta& m);   // on_recv 'K' -> done
    void     handle_nack(const uint8_t* b, size_t n, const RxMeta& m);   // on_recv 'N' -> blind+wait / cascade
    bool     is_blind(uint8_t next_hop) const;                           // _blind_until active? (lazy-prune on read)
    void     do_data_tx();                                        // kCtsToDataGapTimerId fire
    void     do_post_ack();                                       // kPostAckTimerId fire (deliver|forward)
    void     start_rts_timeout();
    void     start_ack_timeout();
    void     start_pending_rx_expiry(uint8_t payload_len);
    void     rts_timeout_fire();                                  // :6326
    void     ack_timeout_fire();                                  // :6546
    void     pending_rx_expiry_fire();                            // :6699
    void     tx_rts_retry();                                      // re-pack SAME-ctr_lo RTS
    // ---- cascade-to-alt walk + no-route defer+Q ----------------------------
    uint8_t  pick_next_cascade_hop(const PendingTx& pt) const;    // two-pass walk :5430; 0 = none
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
    uint8_t  select_data_sf(uint8_t rts_sf_index) const;          // PURE :3027
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
    std::map<uint8_t, uint16_t>  _peer_send_counter;   // next_ctr per dst
    std::map<uint32_t, LastAcked> _last_acked_from;    // key (src<<24|dst<<16|ctr_lo<<8|len)
    std::map<uint32_t, uint64_t>  _seen_origins;       // key (origin<<24|dst<<16|ctr) -> expiry_ms
    std::map<uint32_t, uint8_t>   _seen_origin_from;   // same key -> the prev-hop (LOOP_DUP discriminator)
    std::map<uint8_t, uint64_t>   _blind_until;        // next_hop -> absolute_ms it's deaf-on-routing (F1)
    // NACK BUSY_RX wait-same-hop: the captured ctr_lo the kNackWaitTimerId re-RTSes for.
    uint8_t                      _nack_wait_ctr_lo = 0;
    bool                         _nack_wait_pending = false;
    // async push ring (the app channel; drained via next_push, drop-oldest on overflow)
    Push     _push_ring[protocol::cap_push_ring];
    uint8_t  _push_head = 0, _push_count = 0;
};

}  // namespace meshroute
