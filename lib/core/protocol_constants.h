// MeshRoute — protocol_constants.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// One-to-one port of the PROTOCOL = {...} block in spec/dv_dual_sf.lua.
// These are production-fixed (audit class "P", see
// spec/docs/CONFIG_AUDIT.md). Changing a value is a protocol-design
// change — rebuild the full suite afterwards.
//
// Q4 fixed-point dB: 1 unit = 1/16 dB. Mirror of Lua's PROTOCOL.sf_margin_q4
// etc. q4_to_db(80) = 5.0; db_to_q4(5.0f) = 80.
//
// NOTE: this file MUST stay in lockstep with the Lua PROTOCOL block.
// When updating: edit BOTH sides + run the cross-implementation
// differential test (see test/test_protocol_constants.cpp).

#pragma once

#include <cstdint>

namespace meshroute::protocol {

// ---- Q4 fixed-point dB -----------------------------------------------------
inline constexpr int     q4_scale = 16;
inline constexpr int16_t q4_max   =  32767;
inline constexpr int16_t q4_min   = -32768;

constexpr int16_t db_to_q4(float db) {
    float scaled = (db >= 0.0f) ? (db * q4_scale + 0.5f)
                                : (db * q4_scale - 0.5f);
    if (scaled > q4_max) return q4_max;
    if (scaled < q4_min) return q4_min;
    return static_cast<int16_t>(scaled);
}

constexpr float q4_to_db(int16_t q4) {
    return static_cast<float>(q4) / q4_scale;
}

// ---- Radio / PHY -----------------------------------------------------------
inline constexpr uint8_t  preamble_sym   = 16;
inline constexpr int16_t  sf_margin_q4   = 80;   //  5.0 dB

// ---- MAC / channel access --------------------------------------------------
inline constexpr uint16_t cts_to_data_gap_ms = 5;
inline constexpr uint16_t rts_busy_retry_ms  = 30;
inline constexpr uint8_t  rts_max_retries    = 3;

// ---- Beacon plane ----------------------------------------------------------
inline constexpr uint32_t discovery_beacon_period_ms     = 5000;
inline constexpr uint16_t beacon_max_bytes               = 151;
inline constexpr uint16_t beacon_trigger_jitter_min_ms   = 2000;
inline constexpr uint16_t beacon_trigger_jitter_max_ms   = 10000;
inline constexpr uint32_t beacon_trigger_min_interval_ms = 120000;
inline constexpr uint8_t  tx_defer_max_retries           = 3;       // R4.5b on_radio_busy stash retries (dv:3082)
inline constexpr uint32_t quiet_threshold_ms             = 30000;
inline constexpr uint16_t beacon_silence_jitter_ms       = 10000;
inline constexpr uint32_t seen_bitmap_ttl_ms             = 1800000;

// ---- Boot / discovery ------------------------------------------------------
inline constexpr uint32_t discovery_ms          = 60000;
inline constexpr uint8_t  discovery_min_bcn_rx  = 3;
inline constexpr uint8_t  discovery_min_routes  = 8;
inline constexpr uint32_t beacon_boot_grace_ms  = 120000;
inline constexpr uint16_t req_sync_listen_ms    = 8000;
inline constexpr uint32_t req_sync_retry_ms     = 30000;

// ---- Routing (DV) ----------------------------------------------------------
inline constexpr uint32_t rt_aging_check_period_ms  = 60000;
inline constexpr uint32_t rt_aging_ttl_neighbor_ms  = 2700000;   // 45 min (hops<=1)   dv_dual_sf.lua:8783
inline constexpr uint32_t rt_aging_ttl_remote_ms    = 10800000;  //  3 h  (hops>=2)   dv_dual_sf.lua:8784
inline constexpr uint32_t next_hop_live_ttl_ms      = 1200000;
inline constexpr int16_t  route_snr_conservatism_q4 = 0;
inline constexpr int16_t  snr_ewma_alpha_q4         = 5;   // 0.3 ≈ 5/16
// Routing-table bounded caps (R1). The Lua rt is an unbounded table; the port
// is fixed-size, no-heap. MAX_RT_CANDIDATES=3 (K), dv_hop_cap=16 (carried-route
// combined-hops ceiling). cap_routes bounds the distinct-dest count held in rt[].
inline constexpr uint8_t  max_rt_candidates = 3;
inline constexpr uint8_t  dv_hop_cap        = 16;
inline constexpr uint16_t cap_routes        = 254;   // max leaf size: 255 valid 8-bit ids (0xFF rsv) - self

// ---- F route-discovery (AODV-style RREQ/RREP, §3.7b) -----------------------
inline constexpr uint8_t  cap_route_request_seen    = 64;     // relay flood-dedup ring (origin|dst)
inline constexpr uint8_t  cap_route_request_last    = 128;    // per-dst origination rate-limit ring
inline constexpr uint32_t route_request_seen_ttl_ms = 10000;  // flood-dedup + requery window
inline constexpr uint16_t route_reply_jitter_ms     = 400;    // RREP de-storm backoff (Phase B)

// ---- Peer liveness (suspect/silent/dead tiers) -----------------------------
inline constexpr uint8_t  peer_suspect_rts_timeouts    = 2;
inline constexpr uint8_t  peer_silent_rts_timeouts     = 3;
inline constexpr uint8_t  peer_dead_rts_timeouts       = 6;
inline constexpr uint32_t peer_suspect_ttl_ms          = 300000;
inline constexpr uint32_t peer_silent_ttl_ms           = 900000;
inline constexpr uint32_t peer_dead_ttl_ms             = 3600000;
inline constexpr uint32_t peer_dead_evidence_window_ms = 900000;
inline constexpr int16_t  peer_suspect_penalty_q4      = 192;   // 12.0 dB
inline constexpr int16_t  peer_silent_penalty_q4       = 640;   // 40.0 dB
inline constexpr int16_t  peer_dead_penalty_q4         = 1280;  // 80.0 dB
inline constexpr uint8_t  peer_suspect_bcn_max         = 8;

// ---- Duty-cycle budget tiers -----------------------------------------------
inline constexpr uint8_t  budget_strained_pct          = 50;
inline constexpr uint8_t  budget_critical_pct          = 80;
inline constexpr uint8_t  budget_exhausted_pct         = 95;
inline constexpr uint32_t budget_blind_strained_ms     = 60000;
inline constexpr uint32_t budget_blind_critical_ms     = 180000;
inline constexpr uint32_t budget_blind_exhausted_ms    = 300000;
inline constexpr uint32_t neighbor_budget_tier_ttl_ms  = 300000;

// ---- Anti-spam (P-class only; originator_max_per_window is T) --------------
inline constexpr uint32_t originator_window_ms        = 300000;
inline constexpr float    originator_airtime_share    = 0.35f;  // 0.25->0.35: C++ delivers more -> higher per-sender airtime (s18 heaviest hit 77% of the old cap / 96% of the warn) -> bumped for headroom
inline constexpr float    originator_airtime_warn_fraction = 0.8f;  // WARN (no drop) at 0.8x drop cap; Inc 3 carries it in the ACK warn bit
inline constexpr uint32_t originator_ack_warn_backoff_ms = 10000;   // DM Inc 3: park new DM originations this long after a warn'd ACK
inline constexpr uint16_t originator_retry_dedup_ms   = 10000;
// Per-sender fixed-ring depth for the originator ledger (heap-free; evict-oldest on overflow). The metric
// counts DISTINCT ctr_lo (16 per kind, 2 kinds = 32 ceiling), so 64 is 2x headroom: eviction only triggers
// under genuine spam (>64 non-deduped events in the 5-min window), never in normal traffic. C++-only — the
// Lua baseline keeps an unbounded table (no embedded heap concern there).
inline constexpr uint16_t cap_originator_events       = 64;

// ---- Cascade-requeue -------------------------------------------------------
inline constexpr uint8_t  cascade_requeue_max            = 3;
inline constexpr uint16_t cascade_requeue_base_ms        = 5000;
inline constexpr uint16_t cascade_requeue_backoff_cap_ms = 30000;
inline constexpr uint32_t cascade_requeue_total_max_ms   = 60000;
inline constexpr uint8_t  cascade_requeue_load_threshold = 0;

// ---- Q frames --------------------------------------------------------------
inline constexpr uint16_t q_query_ttl_ms   = 5000;
inline constexpr uint16_t q_respond_ttl_ms = 10000;

// ---- Sync response (REQ_SYNC) ----------------------------------------------
inline constexpr uint16_t sync_response_backoff_min_ms             = 500;
inline constexpr uint16_t sync_response_backoff_max_ms             = 6000;
inline constexpr uint16_t sync_response_mobile_penalty_ms          = 8000;
inline constexpr uint16_t sync_response_requester_mobile_penalty_ms = 2000;
inline constexpr uint16_t sync_response_suppress_window_ms         = 12000;

// ---- Defer / dedup ---------------------------------------------------------
inline constexpr uint32_t send_defer_ttl_ms = 30000;
inline constexpr uint32_t send_defer_drain_period_ms = 1000;   // periodic _deferred drain / TTL giveup

// ---- NACK plane ------------------------------------------------------------
inline constexpr uint8_t  nack_reason_busy_rx    = 0;          // receiver busy with a DIFFERENT flight
inline constexpr uint8_t  nack_reason_budget     = 1;          // (deferred -> R4 duty tiers)
inline constexpr uint8_t  nack_reason_hop_budget = 2;          // (deferred -> hop-budget milestone)
inline constexpr uint8_t  nack_reason_loop_dup   = 3;          // same flight via a different prev-hop (loop)
inline constexpr uint16_t nack_busy_quantum_ms   = 16;         // busy_for_ms = payload*16 (dv:2280)
inline constexpr uint16_t nack_wait_threshold_ms = 2000;       // <= -> wait-same-hop, > -> requeue (dv:10656)
inline constexpr uint16_t last_acked_ttl_ms = 10000;
inline constexpr uint32_t seen_origin_ttl_ms = 30000;

// ---- Hop budget (§7.6) -----------------------------------------------------
inline constexpr uint8_t hop_budget_slack       = 3;
inline constexpr uint8_t hop_budget_max_initial = 31;   // 5-bit field (hops_remaining); Lua dv_dual_sf.lua:1073

// ---- Bounded-state caps (§11.1) --------------------------------------------
inline constexpr uint16_t cap_seen_origins              = 256;
inline constexpr uint16_t cap_q_queried                 = 128;
inline constexpr uint16_t cap_q_responded_to            = 128;
inline constexpr uint8_t  cap_sync_response_pending     = 16;   // device ring of concurrent pending REQ_SYNC responses (Lua: unbounded table)
inline constexpr uint16_t cap_deferred_sends            = 32;
inline constexpr uint16_t cap_gateway_deferred_handoffs = 32;
inline constexpr uint16_t cap_id_bind                   = 256;
// H hash-locate flood (dv:1160-1162): per-(origin,hash) relay dedup + the originator's initial TTL.
inline constexpr uint8_t  hash_query_max_ttl            = 16;
inline constexpr uint32_t hash_query_seen_ttl_ms        = 10000;   // ~2x q_query_ttl_ms
inline constexpr uint8_t  cap_hash_query_seen           = 64;
inline constexpr uint8_t  cap_parked_sends              = 8;       // send-by-hash DMs parked awaiting a hash-bind

// ---- Channel-message gossip plane (ROADMAP §3) -----------------------------
// Single-layer only — gateways skip the whole plane (Principle 11). Phase 1 = the
// buffer + per-origin anti-spam + DATA-M ingest + send_channel origination.
inline constexpr uint16_t cap_channel_buffer            = 128;    // FIFO gossip-buffer entries (Lua dv:988)
inline constexpr uint16_t channel_msg_max_payload_bytes = 200;    // dv:989
inline constexpr uint32_t channel_origin_window_ms      = 300000; // per-origin anti-spam window, 5 min (dv:997)
inline constexpr uint8_t  channel_origin_max_per_window = 20;     // distinct msgs/origin/window before drop (dv:998)
inline constexpr uint8_t  channel_dirty_max_per_bcn     = 3;      // dirty ids advertised per BCN digest (dv:1001) [Phase 2]
inline constexpr uint32_t channel_pull_window_ms        = 60000;  // re-pull dedup window (dv:1009) [Phase 2]
inline constexpr uint16_t channel_pull_jitter_ms        = 5000;   // pull backoff: rand(0, jitter+1) (dv:1019) [Phase 2]
inline constexpr uint8_t  cap_channel_pulls_per_bcn_cycle = 3;    // new pulls/digest (dv:1022) [Phase 2]
inline constexpr uint8_t  channel_dirty_max_advertisements = 3;   // K: clear dirty after K ads (dv:1034) [Phase 2]
inline constexpr uint8_t  cap_channel_pull_pending      = 8;      // bounded pending-pull ring (Lua: unbounded table)
inline constexpr uint8_t  bcn_ext_type_channel_digest  = 3;      // BCN ext-TLV type for the channel digest (dv:1248)
inline constexpr uint8_t  cap_channel_pull_recent      = 32;     // bounded re-pull dedup ring (Lua: unbounded map)
// channel_msg_id flavor (encryption variant; crypto deferred — all plaintext v1, dv:2229-2231)
inline constexpr uint8_t  channel_flavor_public  = 0;
inline constexpr uint8_t  channel_flavor_group   = 1;
inline constexpr uint8_t  channel_flavor_private = 2;

// ---- Identity binding ------------------------------------------------------
inline constexpr uint32_t id_bind_ttl_ms = 172800000;   // 48 h

// ---- Command interface (the app<->firmware seam) ---------------------------
inline constexpr uint8_t gw_env_max_hops = 4;    // GW_ENV_MAX_HOPS (send_layer hop path)
inline constexpr uint8_t cap_push_ring   = 16;   // async push ring (MeshCore OFFLINE_QUEUE_SIZE)

// ---- Gateway scheduling ----------------------------------------------------
inline constexpr uint16_t gateway_schedule_guard_ms = 100;

// ---- Join state machine (§2a) ----------------------------------------------
inline constexpr uint16_t join_listen_ms                = 3000;
inline constexpr uint16_t join_discover_jitter_ms       = 3000;
inline constexpr uint16_t join_discover_wait_ms         = 10000;
inline constexpr uint8_t  join_discover_max_attempts    = 0;     // 0 = unlimited
inline constexpr uint16_t join_offer_backoff_min_ms     = 100;
inline constexpr uint16_t join_offer_backoff_max_ms     = 1000;
inline constexpr uint16_t join_claim_guard_ms           = 3000;
inline constexpr uint16_t join_retry_backoff_ms         = 10000;
inline constexpr uint32_t join_j_rate_limit_window_ms   = 300000;
inline constexpr uint8_t  join_j_max_per_window         = 6;

// ---- Wire-format frame overhead (matches Lua DATA_HDR_LEN + DATA_INNER_OVERHEAD) ----
// Lua CODE is authoritative: DATA_HDR_LEN = 8 + VISITED_LEN(6) = 14 (dv_dual_sf.lua:2904-2905);
// DATA_INNER_OVERHEAD = 2 + MAC_LEN(4) = 6 (:2908); hard cap = 255-14-6 = 235 (:8637).
// (A stale Lua COMMENT at :8632-8633 reads "DATA_HDR_LEN=8 ... 241" — ignore it; trust the code.)
inline constexpr uint8_t  data_hdr_len        = 14;
inline constexpr uint8_t  data_inner_overhead = 6;
inline constexpr uint8_t  lora_max_frame_bytes = 255;  // SX126x/SX127x 8-bit length register
inline constexpr uint8_t  max_payload_bytes_hard_cap =
    lora_max_frame_bytes - data_hdr_len - data_inner_overhead;  // = 235 (the TxItem.inner[] buffer size)
// A normal DM inner is [payload-flags][origin][body...] (enqueue_data writes body at inner[2+i]), so the
// app body must fit in the inner buffer MINUS that 2-byte prefix — 233. Exceeding it overruns inner[].
inline constexpr uint8_t  dm_inner_prefix_bytes = 2;                                      // [payload-flags][origin]
inline constexpr uint8_t  dm_max_body_bytes = max_payload_bytes_hard_cap - dm_inner_prefix_bytes;  // = 233

// ---- SF demod thresholds (Q4 dB, mirrors SF_DEMOD_THRESHOLD in Lua) -------
// SF5 = -2.5 dB → -40 Q4; SF12 = -20.0 dB → -320 Q4.
constexpr int16_t sf_demod_threshold_q4(uint8_t sf) {
    return (sf >= 5 && sf <= 12) ? static_cast<int16_t>((-2.5f * 16) * (sf - 4)) : 0;
}
// Static table for explicit lookup; kept here so test_protocol_constants
// can verify the formula matches the Lua table exactly.
inline constexpr int16_t sf_demod_threshold_q4_table[13] = {
    /* idx 0..4 unused */ 0, 0, 0, 0, 0,
    /* SF5  */  -40,
    /* SF6  */  -80,
    /* SF7  */ -120,
    /* SF8  */ -160,
    /* SF9  */ -200,
    /* SF10 */ -240,
    /* SF11 */ -280,
    /* SF12 */ -320,
};

// Pick the fastest (lowest) SF in `sf_bitmap` (bit = sf) whose demod floor + margin clears
// `rx_snr_q4`; if none clear it, the most-robust (highest) SF present; 0 if the bitmap is empty.
// Mirrors Lua select_data_sf (dv_dual_sf.lua:3043). Pure / draw-free.
inline uint8_t select_data_sf_for_snr(int16_t rx_snr_q4, uint16_t sf_bitmap, int16_t margin_q4) {
    for (uint8_t sf = 5; sf <= 12; ++sf) {            // ascending: fastest SF with SNR headroom
        if ((sf_bitmap & (1u << sf)) &&
            rx_snr_q4 >= sf_demod_threshold_q4_table[sf] + margin_q4) {
            return sf;
        }
    }
    for (uint8_t sf = 12; sf >= 5; --sf) {            // none meet margin: most-robust available
        if (sf_bitmap & (1u << sf)) return sf;
    }
    return 0;                                         // empty bitmap
}

}  // namespace meshroute::protocol
