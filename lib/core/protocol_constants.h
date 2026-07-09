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
#ifndef MESHROUTE_NS
#define MESHROUTE_NS meshroute   // Slice 5 faithful two-lib: gateway variant compiles with -DMESHROUTE_NS=meshroute_gw
#endif

#include <cstdint>

// ---- Build-time knobs (per-env -D overrides in platformio.ini) --------------
// Defaults below = the single-layer LEAF, i.e. a TRUE no-op vs the pre-dual-layer
// firmware. Only the [env:gateway] build overrides them (and [env:native], which
// sets MR_N_LAYERS=2 so it can exercise the dual-layer logic tests):
//   MR_N_LAYERS            # of LayerRuntime instances. 1 = a normal leaf (the
//                          _layers[] array shrinks to one element); 2 = a gateway
//                          (one radio time-multiplexed across two layers, Slice 3+).
//                          A config with n_layers==2 on an MR_N_LAYERS<2 build is
//                          REFUSED in on_init (fail-loud — no silent single-layer fallback).
//   MR_CAP_CHANNEL_BUFFER  gossip FIFO depth. A gateway SKIPS the gossip plane
//                          (Principle 11), so its build cuts this hard to reclaim
//                          the ~60 KB the second LayerRuntime would otherwise cost.
//   MR_CAP_DEFERRED_SENDS  no-route defer-queue depth (secondary gateway RAM trim).
#ifndef MR_N_LAYERS
#define MR_N_LAYERS 1
#endif
#ifndef MR_CAP_CHANNEL_BUFFER
#define MR_CAP_CHANNEL_BUFFER 32
#endif
#ifndef MR_CAP_DEFERRED_SENDS
#define MR_CAP_DEFERRED_SENDS 32
#endif

namespace MESHROUTE_NS::protocol {

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
// R6.1 leaf-config membership: max leaf_name length (NV + the config_hash input; a change re-fingerprints the leaf).
// 2026-06-22 (C-frame §5): 16 -> 10 so the name fits the C config frame + the hash uses an identical ≤10 form on
// both sides (names are truncated to 10 at create / `leaf name`). NV Blob.leaf_name[16] STAYS [16] (no NV bump).
inline constexpr uint8_t  leaf_name_max  = 10;
// R6.2 config-sync: min gap between a node's CONFIG_PULL tx (rate-limit; a stale/joining node re-pulls until adopted).
inline constexpr uint32_t config_pull_retry_ms = 30000;   // 30 s
// R6.3 §7c: min gap between join_refused{wire_version} pushes (so a foreign-version neighbour's every beacon doesn't spam).
inline constexpr uint32_t join_refused_retry_ms = 60000;  // 60 s
// R6.2 §5.2: coarse wire-compat version stamped in the J frame's byte-1 rsv nibble (+0 B). A joiner/responder rejects a
// J whose wire_version differs -> no cross-version join. 4 bits (0..15); widen to a full byte if the version space runs out.
inline constexpr uint8_t  wire_version = 1;
inline constexpr int16_t  sf_margin_q4   = 80;   //  5.0 dB

// ---- MAC / channel access --------------------------------------------------
inline constexpr uint16_t cts_to_data_gap_ms = 5;
inline constexpr uint16_t rts_busy_retry_ms  = 30;
inline constexpr uint8_t  rts_max_retries    = 2;
// Same-hop RTS/ACK retry — capped exponential backoff (spec 2026-06-26-rts-retry-backoff.md). The retry window would
// DOUBLE per attempt (1x,2x,4x,...) up to this shift cap. 0 = FLAT (the Lua-faithful current behaviour).
// ★ SHIPPED AT 0 (no-op): the 24-seed twin_9node_dm A/B REFUTED the BEB hypothesis — delivery falls MONOTONICALLY with
// the shift (flat 47.1% > 1:45.6 > 2/3:43.5). With only 3 same-hop retries before giveup->cascade, growing the window
// just makes a node loiter on a doomed retry (delaying the cascade/giveup, holding pending_tx) -> lower throughput under
// saturation; fast-fail wins. The machinery stays const-gated (tested, ready) for a future METAL experiment, where the
// real-RF contention dynamics may differ from the idealized-RF sim. Flip to 3 to re-enable.
inline constexpr uint8_t  retry_backoff_max_shift = 0;
// PURE: the per-attempt backoff window = base << min(attempt, max_shift). Host-unit-tested. max_shift=0 -> always base.
inline constexpr uint32_t retry_backoff_window(uint32_t base, uint8_t attempt, uint8_t max_shift) {
    return base << (attempt < max_shift ? attempt : max_shift);
}

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
inline constexpr uint8_t  discovery_min_routes  = 4;
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
inline constexpr uint8_t  peer_suspect_rts_timeouts    = 1;   // 1 giveup (a FULL RTS-retry budget all unanswered) is enough to deprioritise a next-hop. Was 2 — too slow: a dead route wasted a whole retry budget on EVERY send until a 2nd giveup accrued (the alt only won transiently mid-cascade, never persisted).
inline constexpr uint8_t  peer_silent_rts_timeouts     = 3;
inline constexpr uint8_t  peer_dead_rts_timeouts       = 6;
inline constexpr uint32_t peer_suspect_ttl_ms          = 300000;
inline constexpr uint32_t peer_silent_ttl_ms           = 900000;
inline constexpr uint32_t peer_dead_ttl_ms             = 3600000;
inline constexpr uint32_t peer_dead_evidence_window_ms = 900000;
inline constexpr int16_t  peer_suspect_penalty_q4      = 192;   // 12.0 dB
inline constexpr int16_t  peer_silent_penalty_q4       = 640;   // 40.0 dB
inline constexpr int16_t  peer_dead_penalty_q4         = 1280;  // 80.0 dB
// ---- Asymmetric-link bidirectionality plane (2026-06-29 design) ------------
inline constexpr int16_t  bidi_penalty_one_way_q4   = peer_silent_penalty_q4;   // 640 Q4 seed — one_way sorts below
                                                                                // any viable confirmed/unknown route
                                                                                // (fallback peer_suspect_penalty_q4=192 if metal strands good-RF one-way routes).
inline constexpr uint64_t bidi_confirm_ttl_ms       = next_hop_live_ttl_ms;     // 1200000 — confirmed decays to UNKNOWN past this
inline constexpr uint64_t link_reprobe_ttl_ms       = 60000;                    // slow-reprobe: one RTS per TTL on a one-way sole route
inline constexpr uint8_t  heard_set_census_min_headroom = 4;                    // census engages only if the full hops==1 set fits leaving >= this many beacon slots
inline constexpr uint8_t  peer_suspect_bcn_max         = 8;     // §P4: max suspect ids advertised per BCN (dv:1376; also clamped by the 4-bit TLV len <=15)
inline constexpr uint8_t  peer_liveness_state_bcn_max  = 7;     // §P4: type-2 LIVENESS_STATE cap — 2B/entry must fit the 4-bit TLV len (2*7=14<=15). The Lua wraps at >=8 dead peers (shared bug, dv:1376); we clamp.
inline constexpr uint8_t  cap_peer_liveness            = 64;    // bounded per-LayerRuntime liveness table (direct-neighbour set); LRU-evict oldest dest_seen

// ---- Duty-cycle budget tiers -----------------------------------------------
inline constexpr uint8_t  budget_strained_pct          = 50;
inline constexpr uint8_t  budget_critical_pct          = 80;
inline constexpr uint8_t  budget_exhausted_pct         = 95;
inline constexpr uint32_t budget_blind_strained_ms     = 60000;
inline constexpr uint32_t budget_blind_critical_ms     = 180000;
inline constexpr uint32_t budget_blind_exhausted_ms    = 300000;
inline constexpr uint32_t neighbor_budget_tier_ttl_ms  = 300000;

// ---- Anti-spam (P-class only; originator_max_per_window is T) --------------
// The 5-min sliding window that ALL the anti-spam planes measure over: the DM per-sender airtime backstop, the
// e2e-ack spoof-penalty TTL, AND the channel-cap duty basis D = duty_cycle * originator_window_ms (channel_duty_budget_ms).
// ★ INVARIANT: channel_origin_window_ms (the per-origin channel-cap ledger's aging window, below) MUST equal this —
// channel_cap_origin() prices C against THIS window while channel_origin_admit ages the ledger against THAT one; if
// they ever diverge the computed cap and the enforced count desync. They are two names for the same 5-min window.
inline constexpr uint32_t originator_window_ms        = 300000;
inline constexpr float    originator_airtime_share    = 0.35f;  // 0.25->0.35: C++ delivers more -> higher per-sender airtime (s18 heaviest hit 77% of the old cap / 96% of the warn) -> bumped for headroom
inline constexpr float    originator_airtime_warn_fraction = 0.8f;  // WARN (no drop) at 0.8x drop cap; Inc 3 carries it in the ACK warn bit
inline constexpr uint32_t originator_ack_warn_backoff_ms = 10000;   // DM Inc 3: park new DM originations this long after a warn'd ACK
// e2e-ack backstop exemption anti-spoof: how long a caught spoofer's RTS_FLAG_E2E_ACK is IGNORED (the backstop re-applies).
// Reuse the 5-min originator window as the revoke TTL — one free pass, then the exemption is off for a whole originator window.
inline constexpr uint32_t e2e_ack_spoof_penalty_ms    = originator_window_ms;
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
// ④ load-adaptive cascade back-pressure: the TX-queue depth at/below which the FULL cascade_requeue_max budget holds;
// above it the budget shrinks 1:1 with depth (cascade_effective_max). TUNED UP from the Lua's maximally-aggressive 0
// (gate-calibrated 2026-06-22 via the {1,2,3} sweep — lowest non-regressing on s16/s18). With kTxQueueCap=8 +
// cascade_requeue_max=3, threshold 2 keeps full budget through depth 2, shrinks at 3, fully gates at depth 5.
inline constexpr uint8_t  cascade_requeue_load_threshold = 2;

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
inline constexpr uint16_t cap_deferred_sends            = MR_CAP_DEFERRED_SENDS;   // default 32; gateway build trims (RAM)
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
inline constexpr uint16_t cap_channel_buffer            = MR_CAP_CHANNEL_BUFFER;   // default 32 FIFO gossip entries (Lua dv:988) - reduction!
inline constexpr uint16_t channel_msg_max_payload_bytes = 200;    // dv:989
inline constexpr uint32_t channel_origin_window_ms      = 300000; // per-origin channel-cap ledger aging window, 5 min (dv:997). ★ MUST equal originator_window_ms (see the invariant note there) — channel_cap_origin() prices C over that window.
// ---- Anti-spam v2 (2026-06-30 duty-channel-cap) — FORCED-DELAY burst floors ----------------------
// Two per-origin minimum-spacing "burst floors". A new DISTINCT origination arriving sooner than its floor is
// DEFERRED in place (NOT dropped) and the node emits send_blocked{reason:"min_interval", next_ms} so a trusted
// companion holds + retries after next_ms instead of firing blind (spec §Companion feedback). They are the
// user-visible "forced delays" of the anti-spam plane, distinct from the SF/mesh/duty per-origin *count* cap.
//   • channel_min_interval_ms — the CHANNEL floor, enforced at BOTH sites: the receiver-hook channel_origin_admit
//     (others' floods) and the do_send_channel self-gate (our own posts). Purpose: anti-flood-burst — bound one
//     origin's gossip rate. ★ LOAD-BEARING COUPLING: over the 5-min originator window this floor structurally caps
//     ledger recording at ~window/interval ≈ 30 distinct floods/origin/window — so it (not only cap_channel_origin_events)
//     bounds L.n. Shrinking the floor or growing the window raises that ceiling; keep both in view before changing either.
//   • dm_min_interval_ms — the OWN-DM floor, enforced in become_free (self only). Purpose: anti-per-keystroke — a user
//     typing fast must not emit a DM per keystroke. ★ EXEMPT by DATA type: e2e-ack + rcmd never wait on this floor (a
//     delivery-confirm that is throttled just makes the sender re-send — self-defeating; see the e2e-ack exemption).
// These are the FACTORY DEFAULTS; the live values are the per-leaf NodeConfig fields of the same name (a mother
// provisions them in the C config frame — see leaf_config.h), so a deployment can tune the forced delays.
inline constexpr uint32_t channel_min_interval_ms = 10000;   // 10 s — default channel burst floor
inline constexpr uint32_t dm_min_interval_ms      = 3000;    // 3 s  — default own-DM burst floor
// MF7: array bound for ChannelOriginLedger.ev[] (Slice 3 removed the flat channel_origin_max_per_window cap; this const
// carries the ledger sizing forward, and channel_origin_admit now enforces the duty-anchored channel_cap_origin()).
inline constexpr uint8_t  cap_channel_origin_events = 20;    // distinct msgs/origin/window the ledger tracks (dv:998)
// MF2: the legacy flat per-origin channel cap. channel_cap_origin() returns THIS when the duty plane is disabled
// (duty_cycle<=0 -> channel_duty_budget_ms()==0), so a default node keeps the old behaviour.
inline constexpr uint16_t cap_channel_origin_legacy = 20;
// MF3: the fixed DATA-M frame length feeding T_ch's airtime term = M_FRAME_HDR_LEN(7) + a representative 32-B channel
// body. A single deterministic length keeps channel_cap_origin() pure/SF-only (not per-message-size).
inline constexpr uint16_t channel_flood_sample_len = 39;
inline constexpr uint8_t  channel_dirty_max_per_bcn     = 3;      // dirty ids advertised per BCN digest (dv:1001) [Phase 2]
inline constexpr uint32_t channel_pull_window_ms        = 60000;  // re-pull dedup window (dv:1009) [Phase 2]
inline constexpr uint16_t channel_pull_jitter_ms        = 5000;   // pull backoff: rand(0, jitter+1) (dv:1019) [Phase 2]
inline constexpr uint8_t  cap_channel_pulls_per_bcn_cycle = 3;    // new pulls/digest (dv:1022) [Phase 2]
inline constexpr uint8_t  channel_dirty_max_advertisements = 3;   // 2026-06-25 REVERTED 16→3 (the Lua value, dv:1034). The 3→16 inflation was a holder-aware-retire backstop ("advertise an orphan longer"), but metal run 3b9abc proved it useless: the permanent-orphan case is "NO HOLDER EXISTS AT ALL" (the flood reached 0 nodes), so K is irrelevant. The origin RE-OFFER (channel_reoffer_*) is the correct lever — it re-injects the message so a holder forms — and supersedes the inflated K. (Reverting also isolates the re-offer's effect in the seed sweep.) Entry still retires on HOLDER COVERAGE (channel_entry_fully_seen); this is the horizon backstop.
inline constexpr uint8_t  cap_channel_pull_pending      = 8;      // bounded pending-pull ring (Lua: unbounded table)
inline constexpr uint8_t  bcn_ext_type_suspect_nodes   = 1;      // §P4 BCN ext-TLV type 1: gossip locally-observed SILENT peers (1B/id), applied as SUSPECT (dv:1241)
inline constexpr uint8_t  bcn_ext_type_liveness_state  = 2;      // §P4 BCN ext-TLV type 2: gossip peers incl. DEAD ([id, state&0x03] 2B/entry) (dv:1242)
inline constexpr uint8_t  bcn_ext_type_channel_digest  = 3;      // BCN ext-TLV type for the channel digest (dv:1248)
inline constexpr uint8_t  bcn_ext_type_gateway_layer   = 4;      // BCN ext-TLV type 4: multi-hop gateway-layer propagation (dv:1249) — gw_id->dest_leaf, re-gossiped by ALL nodes
inline constexpr uint8_t  cap_bridged_layers           = 8;      // Node-global gw_id->dest_leaf table (mirror _gw_schedules); leaves carry it (they ORIGINATE cross-layer DMs)
inline constexpr uint8_t  bridged_layers_max_per_tlv   = 9;      // N entries per type-4 TLV: 9 gw_ids + ceil(9/2)=5 nibble bytes = 14 <= the 4-bit len cap (15)
inline constexpr uint32_t bridged_layers_ttl_ms        = 172800000;  // 48 h (Lua); a sim gate may shrink to exercise aging
inline constexpr uint8_t  cap_channel_pull_recent      = 32;     // bounded re-pull dedup ring (Lua: unbounded map)

// ---- channel flood (2026-06-08 redesign): managed flood = fast primary; digest+pull = repair backstop ----
inline constexpr uint8_t  cap_flood_pending = 3;        // concurrent floods mid-backoff (bounded to the free timer band [61-63]); overflow -> repair backstop
inline constexpr uint8_t  flood_hop_max     = 16;       // TTL safety cap (≈ dv_hop_cap)
inline constexpr uint32_t flood_backoff_ms  = 2000;     // T_backoff: max rebroadcast jitter; >= one RTS-M+DATA-M airtime so an overhearer can cancel first
inline constexpr int16_t  flood_snr_lo_q4   = -15 * 16; // SNR-norm range lo (dB, Q4)
inline constexpr int16_t  flood_snr_hi_q4   =  10 * 16; // SNR-norm range hi (dB, Q4)

// ---- channel ORIGIN RE-OFFER (2026-06-25, spec 2026-06-25-channel-origin-reoffer.md, Part 2) ----
// A DIVERGENCE from the Lua (which relies on the pull). The origin owns its message's propagation until it sees
// proof it got out: with the honest empty flood seed (Part 1), ChannelEntry.seen_by starts empty; the FIRST
// overheard relay sets a bit -> non-empty seen_by = "it propagated" = confirmed. While seen_by stays empty the
// origin re-floods the cached body up to N times — re-injecting a message whose only link was too contended to
// hear it (the 247→0/7 orphan). The well-connected case confirms within the first delay and re-offers ZERO times.
inline constexpr uint8_t  cap_channel_reoffer_pending = 4;       // bounded per-origin re-offer table (timer ring [70..73]); a node rarely has >cap_flood_pending un-confirmed originations in flight
inline constexpr uint8_t  channel_reoffer_max_retries = 1;       // cap — bounds the airtime cost of a fragile message
inline constexpr uint32_t channel_reoffer_delay_ms    = 10000;   // base cadence (>= originator_retry_dedup_ms=10000 so re-floods dedup receiver-side, not double-inbox)
inline constexpr uint32_t channel_reoffer_jitter_ms   = 2000;    // +rand(0,jitter) spread so multiple origins don't re-offer in lockstep
// channel_msg_id flavor (encryption variant; crypto deferred — all plaintext v1, dv:2229-2231)
inline constexpr uint8_t  channel_flavor_public  = 0;
inline constexpr uint8_t  channel_flavor_group   = 1;
inline constexpr uint8_t  channel_flavor_private = 2;

// ---- Identity binding ------------------------------------------------------
inline constexpr uint32_t id_bind_ttl_ms = 172800000;   // 48 h
// E2E peer-pubkey cache (Phase 1 §6): key_hash32 -> ed_pub. Sparse (only sealed-DM partners); per LayerRuntime.
inline constexpr uint16_t cap_peer_keys   = 16;
inline constexpr uint32_t peer_key_ttl_ms = id_bind_ttl_ms;   // pubkeys are immutable; aging is cache hygiene, not correctness

// ---- Command interface (the app<->firmware seam) ---------------------------
inline constexpr uint8_t gw_env_max_hops = 4;    // GW_ENV_MAX_HOPS (send_layer hop path)
inline constexpr uint8_t cap_push_ring   = 32;   // async push ring

// ---- Gateway scheduling ----------------------------------------------------
// SENDER-SIDE settle margin (Lua dv:5027-5030): a node timing its RTS to hit a gateway's window adds this guard so
// the frame lands AFTER the window opens + the gateway's retune settles. Consumed by the SENDER-DEFER (Slice 3e:
// gateway_schedule_defer_ms / the receiver-anchored countdown), NOT the window-switch path (3d) — correctly unused there.
inline constexpr uint16_t gateway_schedule_guard_ms = 100;
// §3e herd-spread (Lua gateway_spread_nibble / gateway_schedule_defer_ms herd-jitter). A gateway sizes a 0..15 spread
// nibble from its 1-hop herd and advertises it; senders deferring to a window draw a uniform jitter over
// (nibble/15 × window) so they don't all re-collide at window-open (the dominant dense-gateway first-leg failure).
// The per-exchange airtime (RTS+CTS+gap+DATA+ACK) is COMPUTED from airtime_ms (Node::exchange_airtime_ms), NOT a
// constant — a C++ improvement over the Lua's fixed 600ms estimate. The DATA leg uses a rolling mean of the payloads
// the node passes; this is the bootstrap payload until the first DATA sample lands.
inline constexpr uint8_t  gateway_herd_assumed_payload_bytes = 64;  // DM body assumption for the exchange calc (pre-EWMA)
inline constexpr uint8_t  gateway_herd_min                = 3;    // herds < this advertise nibble 0 (≤2 has nothing to de-conflict)
inline constexpr uint8_t  gateway_herd_jitter_max_pct     = 60;   // cap jitter at this % of the window (Lua 0.6 frac)
// Adaptive guard (Lua dv:5029): a SPARSE herd (nibble 0 = herd-jitter inactive) biases the send deeper into the window
// for settle-edge margin; a DENSE herd (nibble>0) keeps the base guard (the jitter already disperses it).
inline constexpr uint16_t gateway_schedule_guard_sparse_bonus_ms = 200;
// Window-switch busy-retry: when a scheduled leaf-swap is deferred because the active layer is mid-exchange
// (_pending_tx / _pending_rx / _post_ack.pending), re-arm the switch after this. EXPLICIT named constant — the
// Lua silently fell back to max(rts_busy_retry_ms, 1000); we declare it (no-fallback rule). (Lua L8425 == 1000.)
inline constexpr uint32_t gateway_layer_busy_retry_ms = 1000;
// Gateway-doorstep hold (dv:6351): when an RTS to a known gateway times out, patient window-aware requeue
// instead of the generic cascade. The giveup timer (~10 visit windows at 15s each) is a long patience since
// the gateway may be away on its other leaf. The jitter spreads neighbours so they don't re-collide in lockstep
// when the gateway's window re-opens. (Lua gateway_send_giveup_ms / gateway_doorstep_retry_jitter_ms.)
inline constexpr uint32_t gateway_send_giveup_ms           = 150000;
inline constexpr uint32_t gateway_doorstep_retry_jitter_ms = 2000;
// Slice 3e.2: a node remembers the window schedule of nearby gateways (learned from their beacons) so it can time
// an RTS to hit the gateway's window on the SENDER's leaf. Small ring (a node hears few gateways); evict-oldest.
inline constexpr uint8_t  cap_gateway_neighbor_schedules = 4;
// Slice 4f: an unknown far-leaf binding defers the handoff (instead of dropping) — the gateway floods an H query on
// the target leaf + re-resolves on a later visit, giving up after the TTL. The reflood throttle is ~one visit period
// (the DELIVERY_ANALYSIS "~15s not 5s" insight: re-flooding every q_query_ttl thrashes the gateway's far-leaf window).
inline constexpr uint32_t gateway_handoff_defer_ttl_ms = 60000;   // ~4 visit periods (15s) before a loud giveup
inline constexpr uint32_t gateway_handoff_reflood_ms   = 15000;   // one H query per gateway visit period (not per drain)
// Slice 4c.1: the gateway's cross-layer re-inject HANDOFF buffer — a node-global ring of pending bridges, each
// waiting for its TARGET leaf's window to open (drained in activate_layer). 16 = the user's SMALL cap (full-body
// entries; a single gateway bridging one layer-pair can't have many in flight). NOTE: cap_gateway_deferred_handoffs
// (32, above) is the TTL/POLICY reference for the 4f unknown-binding giveup, NOT this live buffer's size.
#if MR_N_LAYERS >= 2
inline constexpr uint8_t cap_gateway_handoffs = 16;
#else
inline constexpr uint8_t cap_gateway_handoffs = 1;   // a single-layer node NEVER bridges (bridge_cross_layer refuses at the top when n_layers<2, L13) -> 1 slot, ~4 KB reclaimed vs 16
#endif
// Slice 3e F-C: the wire's schedule_record duration_100ms / offset_100ms are 8-bit ×100 ms => a 25.5 s ceiling, with NO
// escape unit (unlike period_units' ×5000 ms mode). on_init REFUSES (fail loud, no clamp) a gateway window beyond this:
// a clamped duration/countdown breaks the receiver's defer phase math (the clamped offset no longer ≈ the cycle). The
// offset is bounded by the active window (active leaf encodes 0; foreign leaf <= one active window away), so this single
// window bound covers BOTH 8-bit fields. 255 * 100 ms = 25500.
inline constexpr uint32_t gateway_schedule_window_max_ms = 25500;

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

// ---- node_id auto-assignment (DAD + heal) — docs/specs/2026-06-05-node-id-auto-assignment-design.md.
// DELIBERATE divergence from the Lua baseline (signed off 2026-06-06): the C++ DAD widens the claim guard
// (3s -> 20s, headroom for an objection over a slow/lossy LoRa link) and uses a tiebreak of
// claim_epoch -> key_hash32 (NOT the Lua's lease_age-first, which is provably non-convergent under wire
// staleness — spec §6). lease_age stays on the wire as telemetry. Convergence > Lua-lockstep here.
inline constexpr uint32_t dad_claim_guard_ms   = 20000;       // §13: wait this long for an objection before adopting
inline constexpr uint32_t dad_denied_id_ttl_ms = 86400000;    // §13: a lost slot stays denied 1 day, then reusable
inline constexpr uint8_t  cap_join_denied      = 16;          // bounded denied-id list (denials are rare; evict-oldest)
inline constexpr uint8_t  unjoined_node_id     = 0;           // 0 = unprovisioned (do_send refused until adopt)
// R6.3 / DAD G1: node-id reservation. 1..16 = gateways only; 17..254 = normal nodes; 0 fresh, 0xFF reserved. A
// provisioning/DAD-time convention (the picker + cfg-set), NOT an on_init/wire invariant (a hard check regresses the
// sim suite — static scenario ids bypass the picker). docs/superpowers/specs/2026-06-19-normal-node-id-reservation-design.md
inline constexpr uint8_t  gateway_node_id_max  = 16;          // 1..16 reserved for gateways
inline constexpr uint8_t  normal_node_id_min   = 17;          // normal nodes pick from 17..254
inline constexpr uint8_t  cap_host_mobiles     = 16;          // §mobile 2a: per-leaf host registry capacity (mobiles accepted by this host)
// §mobile 2b (mobile-side registration FSM):
inline constexpr uint8_t  cap_mobile_offers              = 8;        // OFFERs collected in one DISCOVER window
inline constexpr uint32_t mobile_discover_backoff_min_ms = 5000;     // exp-backoff floor when no host answers (B3)
inline constexpr uint32_t mobile_discover_backoff_max_ms = 120000;   //   ceiling
inline constexpr uint32_t mobile_offer_window_ms         = 2000;     // collect-OFFERs window before deciding (≈ B4)
inline constexpr uint32_t mobile_home_lost_ms            = 90000;    // no BCN from home -> re-register
inline constexpr uint32_t mobile_reclaim_ms              = 600000;   // 10-min periodic re-CLAIM (self-heal + refresh)
inline constexpr uint32_t mobile_liveness_ms            = 1500000;  // §mobile hash-locate: the home proxies for a mobile ONLY if heard within 25 min (≈2.5× re-CLAIM) — kills the long-term black hole; a just-died mobile is proxied ≤~25 min then goes silent
inline constexpr uint8_t  cap_learned_layers             = 4;        // §mobile 5a: neighbouring layers a mobile LEARNS (pulls from a gateway) to cross to on home-lost
inline constexpr uint32_t learned_layer_ttl_ms           = 3600000;  // §mobile 5a: 1 h (layer records are static)
inline constexpr uint32_t mobile_layer_query_period_ms   = 600000;   // §mobile 5a: 10-min directory refresh while connected
inline constexpr uint8_t  cap_mobile_home_cache          = 16;       // §mobile 3c: sender-side mobile_hash->home_id cache (id_bind can't hold a 2nd hash per node)
inline constexpr uint32_t mobile_home_cache_ttl_ms       = 300000;   // §mobile 3c: 5-min TTL (§17-C2 "short, minutes")
// L2a mediation airtime guard: one mediated DENY per (id, loser-hash) per window — else a flapping binding
// re-DENYs on EVERY beacon (a dense-storm airtime sink). Re-mediates after the window if the loser hasn't
// yet renumbered (covers a lost DENY). Bounded ring (evict-oldest); 32 covers realistic churn.
inline constexpr uint8_t  cap_mediated_recent     = 32;
inline constexpr uint32_t mediated_deny_suppress_ms = 30000;

// L2c verify-on-delivery: a DM whose DST_HASH != our key was misdelivered by an id collision. We redirect
// it to the real owner (send-by-hash) once per hash per window — a still-poisoned binding (collision not
// yet healed) would otherwise re-trigger redirect→deliver-to-self→redirect until the heal converges.
inline constexpr uint8_t  cap_l2c_redirect        = 16;
inline constexpr uint32_t l2c_redirect_suppress_ms = 30000;

// ---- Wire-format frame overhead (C++ DATA header DIVERGES from the frozen Lua) ----
// The C++ DATA frame DROPS the Lua's visited[6] (loop/dedup uses _seen_origins + hops_remaining TTL,
// never a visited list) — a DELIBERATE wire divergence, decided by the architect (like the data_sf
// removal and the lean M frame). See frame_codec.h / docs/frames.md. So:
//   C++  DATA_HDR_LEN = 8 (no visited)  ->  hard cap = 255 - 8 - 6 = 241.
//   Lua  DATA_HDR_LEN = 8 + VISITED_LEN(6) = 14 (dv_dual_sf.lua:2904-2905) -> 235.
// (The C++ value matches the stale Lua COMMENT at :8632-8633 ("...8 ... 241"); we diverge from the
// Lua CODE on purpose here, NOT following that comment.) DATA_INNER_OVERHEAD = 2 + MAC_LEN(4) = 6 (:2908).
inline constexpr uint8_t  data_hdr_len        = 8;
inline constexpr uint8_t  data_inner_overhead = 6;
inline constexpr uint8_t  lora_max_frame_bytes = 255;  // SX126x/SX127x 8-bit length register
inline constexpr uint8_t  max_payload_bytes_hard_cap =
    lora_max_frame_bytes - data_hdr_len - data_inner_overhead;  // = 241 (the TxItem.inner[] buffer size)
// A normal DM inner is [origin][body...] (enqueue_data writes body at inner[off+i]; no payload-flags byte
// anymore — DST_HASH/etc. are byte-1 header flags). The app body must fit in the inner buffer MINUS the
// prefix; kept at a conservative 2 (covers the [origin] prefix and leaves headroom; the DST_HASH variant's
// [dst_key_hash32 4][origin]=5-B prefix has its own explicit fit-check in enqueue_data). Exceeding it overruns inner[].
inline constexpr uint8_t  dm_inner_prefix_bytes = 2;                                      // conservative cap (>= the [origin] prefix)
inline constexpr uint8_t  dm_max_body_bytes = max_payload_bytes_hard_cap - dm_inner_prefix_bytes;  // = 239

// ---- Overheard-reserve YIELD (spec 2026-06-28-overheard-reserve-yield.md) ----------------------------------
// When a node mid-handshake (awaiting_cts/awaiting_ack) overhears its NEXT-HOP get reserved (an overheard CTS the
// next-hop sent, or an overheard RTS targeting the next-hop), it PUSHES its own pending timeout past the reserve
// WITHOUT burning a retry — CSMA politeness: yield to the in-progress exchange, retry once it's free. Bounded by the
// flight's total-lifetime giveup so a saturated cell can't make it yield forever. Const-gated A/B (each flippable).
// ★ SHIPPED OFF: the 24-seed twin_9node_dm A/B REFUTED Part A — yield ON 45.5% < OFF 47.1% (same direction as BEB).
// Yielding extends a flight's lifetime (it keeps yielding/retrying instead of fast-failing), holding pending_tx +
// blocking the node's tx-queue up to the 60s giveup horizon -> lower throughput under saturation. Fast-fail wins
// (the BEB + yield double-refutation points the opposite way: FEWER/faster retries). Const-gated + tested, kept for a
// METAL re-test (real-RF contention may differ) and for moderate-contention scenarios the extreme twin doesn't cover.
inline constexpr uint8_t  reserve_yield_enable      = 0;   // Part A (unicast reserve): 0 = off = today's blind-timeout behaviour
inline constexpr uint8_t  flood_yield_grab_enable   = 0;   // Part B (flood RTS-M grab while awaiting_cts): UNTESTED (twin has no floods) -> shipped off; needs a channel-bearing twin to A/B
inline constexpr uint16_t reserve_est_payload_bytes = max_payload_bytes_hard_cap / 2;   // ½-max DATA-length estimate for the reserve duration D (actual len unknown; LBT backstops an under-estimate)

// ---- Persistent inbox (DM + channel durable history; 2026-06-10 spec) -------
// Two independent flash stores: DMs are large + durable, channels persisted but freely evicted.
// Both drop-oldest at the byte cap. The store is a segmented append-log (delete-oldest-segment, no
// rewrite); segment <= store cap. A record = a 24-B header + body, body <= inbox_max_body, so a
// single record always fits a segment (the "record > segment" path is a defensive guard, never hit).
inline constexpr uint32_t inbox_dm_store_bytes     = 512u * 1024;   // ~thousands of short DMs
inline constexpr uint32_t inbox_chan_store_bytes   = 128u * 1024;   // freer (channels evict sooner)
// The segment (delete-oldest granularity) = the read-scratch size: read_since loads a WHOLE segment into a
// fixed scratch buffer, so a segment must NOT exceed it (a larger segment would silently truncate the read).
// Hence one value for both, enforced by the store's begin() guard. (Earlier 32K/16K spec values were never
// wired — they'd have overrun the 4 KB scratch; reconciled to the real, scratch-bounded size.)
inline constexpr uint32_t inbox_segment_bytes      = 4u * 1024;     // 4 KiB; == the store read-scratch
inline constexpr uint8_t  inbox_max_body           = max_payload_bytes_hard_cap;  // 241 (record body cap)

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
