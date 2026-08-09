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

// ---- Link quality — the canonical scale ------------------------------------
// ONE shared signal-strength scale for every link-quality consumer (spec
// 2026-07-19-signal-strength-unification.md §2). SNR is carried in q4 dB exactly
// as a real SX126x REPORTS it: the chip never reports much above ~+10..+13 dB, so
// the meaningful reported window is −20…+12 dB (the sim now models this saturation —
// spec §3 / Slice A; the per-SF demod table sf_demod_threshold_q4 is the physical
// reference anchoring the low end). The STATIC wire is byte-frozen (beacon 4-bit
// bucket, ACK 2-bit bucket, route score) — this scale is what the one OUTLIER, the
// presence tiers, was recalibrated onto (Slice B), plus the shared EWMA helper so a
// fifth consumer can't drift.
inline constexpr int16_t snr_report_min_q4     = db_to_q4(-20.0f);  // demod-floor end of the reported window
inline constexpr int16_t snr_report_ceiling_q4 = db_to_q4( 12.0f);  // SX126x reporting ceiling (the sim clamps reports to this — Slice A)
// EWMA smoothing of a reported-SNR series. α = snr_ewma_alpha_q4/16 = 5/16 ≈ 0.3.
inline constexpr int16_t snr_ewma_alpha_q4     = 5;
// One α=5/16 EWMA step on a q4-dB accumulator (pure integer; bit-identical to the
// historical inline form  ew + (((sample-ew)*α)>>4)). A caller holding an ALREADY-SEEDED
// accumulator (e.g. the presence candidate table, which seeds at insertion) uses this.
inline constexpr int16_t snr_ewma_step(int16_t ew, int16_t sample_q4) {
    return static_cast<int16_t>(ew + (((sample_q4 - ew) * snr_ewma_alpha_q4) >> 4));
}
// Seed-if-zero variant: an unset (0) accumulator ADOPTS the first sample outright, then
// steps thereafter (the home per-mobile EWMA in node_join + the mobile home-RX EWMA in
// node_mobile). ⚠ NOT interchangeable with snr_ewma_step: they DIFFER when the stored
// value is exactly 0 dB (0 q4) — step() smooths toward the sample, update() re-seeds.
// The candidate table deliberately uses step() (it seeds at insert); keep each caller on
// its correct helper (verified bit-identical to the pre-refactor inline sites).
inline constexpr int16_t snr_ewma_update(int16_t ew, int16_t sample_q4) {
    return (ew == 0) ? sample_q4 : snr_ewma_step(ew, sample_q4);
}

// ---- Radio / PHY -----------------------------------------------------------
inline constexpr uint8_t  preamble_sym   = 16;
// R6.1 leaf-config membership: max leaf_name length (NV + the config_hash input; a change re-fingerprints the leaf).
// 2026-06-22 (C-frame §5): 16 -> 10 so the name fits the C config frame + the hash uses an identical ≤10 form on
// both sides (names are truncated to 10 at create / `leaf name`). NV Blob.leaf_name[16] STAYS [16] (no NV bump).
inline constexpr uint8_t  leaf_name_max  = 10;
// §1.3 / §AB2: max cached PEER name — the width of Node::PeerKey::name and of device_nv.h's PeerRec::name, and the cap
// the `peername` verb refuses past (C2: never silently truncate an operator label). It was a bare literal `32` written
// three times inside peer_key_set/push_peer_key_cached; a node.h static_assert now pins it to sizeof(PeerKey::name), so
// widening the field and widening this constant are one edit rather than four. ⚠ NOT the same quantity as
// leaf_name_max (a wire/config-hash input) — a peer name is local-only cache state and never rides a C frame.
inline constexpr uint8_t  peer_name_max  = 32;
// R6.2 config-sync: min gap between a node's CONFIG_PULL tx (rate-limit; a stale/joining node re-pulls until adopted).
inline constexpr uint32_t config_pull_retry_ms = 30000;   // 30 s
// R6.3 §7c: min gap between join_refused{wire_version} pushes (so a foreign-version neighbour's every beacon doesn't spam).
inline constexpr uint32_t join_refused_retry_ms = 60000;  // 60 s
// R6.2 §5.2: coarse wire-compat version stamped in the J frame's byte-1 rsv nibble (+0 B). A joiner/responder rejects a
// J whose wire_version differs -> no cross-version join. 4 bits (0..15); widen to a full byte if the version space runs out.
inline constexpr uint8_t  wire_version = 1;
inline constexpr int16_t  sf_margin_q4   = 80;   //  5.0 dB

// ---- PHY unit conversions — the ONE rounding path --------------------------
// Operators type FRACTIONAL kHz/MHz (62.5 / 41.67 / 31.25 are real LoRa bandwidths) but NV, the wire and the
// companion JSON carry INTEGER Hz / kHz. Both conversions ROUND (`+ 0.5`) rather than truncate, so 62.5 kHz lands
// on 62500 Hz and not 62499 — the fractional-BW bug this project has already paid for once.
//
// ★ The parameter is `double` ON PURPOSE. Every call site's operand is already a `double` (`atof`, or
// `LayerConfig::freq_mhz`); a `float` parameter would re-round it at ~7 significant digits and shift values that
// are not exactly representable. Do NOT "improve" these to `llround`/`lround` either: `(uint32_t)(x * 1000.0 + 0.5)`
// IS the shipped behaviour, and the whole point of the helper is being bit-identical to the 13 sites it replaced.
//
// They are kept as TWO functions although the arithmetic coincides: the unit is the contract, and reading
// `mhz_to_khz(freq_mhz)` at a call site is what makes a kHz/MHz mix-up visible to a reviewer (the compiler cannot
// catch it — both take a bare double). Keep them independently editable; never fold one into the other.
inline constexpr uint32_t khz_to_hz(double khz)  { return static_cast<uint32_t>(khz * 1000.0 + 0.5); }
inline constexpr uint32_t mhz_to_khz(double mhz) { return static_cast<uint32_t>(mhz * 1000.0 + 0.5); }

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
// ★★ §MH-S3 §5.2 — EQUAL JITTER, the SECOND member of the retry-backoff family. Given a capped backoff
// WINDOW it yields the half-open draw bounds for `Hal::rand_range(lo, hi)`:
//
//     delay = rand( window/2 , window + 1 )        // inclusive upper bound through the half-open API
//
// ★ WHY BOTH HALVES MATTER, and neither is decoration: the NON-ZERO lower half stops a fleet that failed
// together from storming again immediately (a plain `rand(0, window)` lets a whole fleet draw near-zero on
// the same round), and the RANDOM upper half breaks the PERMANENT phase alignment a fixed deadline creates
// (§2.2: mobiles powered together collided in the same OFFER window round after round, forever).
//
// ⚠ WHY `retry_backoff_window()` ABOVE IS NOT REUSED TO PRODUCE THE WINDOW, verified not assumed (V1):
// §5.2's window is `min(base * 2^attempt, ceiling)` — a VALUE clamp — while that function is a SHIFT clamp,
// and no shift of 5000 equals 120000. Its `retry_backoff_max_shift` is also globally **0** (measured, six
// lines up), i.e. it currently produces no growth at all. The mobile ladder therefore keeps its own
// in-place `min(2*prev, max)` accumulator (`_mobile_backoff_ms`), which IS §5.2's formula exactly; only the
// jitter is shared. ⛔ Do not "unify" the two — they clamp different things.
//
// ⓘ `hi_excl` is `window + 1` so the top of the window is REACHABLE; `lo` is a truncating halve, so a
//   window of 1 gives rand(0, 2) — still a legal, non-degenerate draw.
inline constexpr uint32_t equal_jitter_lo(uint32_t window)      { return window / 2; }
inline constexpr uint32_t equal_jitter_hi_excl(uint32_t window) { return window + 1; }

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
// (snr_ewma_alpha_q4 lives in the canonical "Link quality" section above — it is a
//  shared link-quality primitive, not a routing-only knob.)
// Routing-table bounded caps (R1). The Lua rt is an unbounded table; the port
// is fixed-size, no-heap. MAX_RT_CANDIDATES=3 (K), dv_hop_cap=16 (carried-route
// combined-hops ceiling). cap_routes bounds the distinct-dest count held in rt[].
inline constexpr uint8_t  max_rt_candidates = 3;
inline constexpr uint8_t  dv_hop_cap        = 16;
// §team-parity T0 (spec 2026-07-27 §3/T0, requirement R4): the TEAM plane's own hop ceiling. A team is a
// 3-10-member group, 1-3 hops typical and stragglers to 8 — half the static radius, so a team flood costs
// half the worst-case airtime. ★ ADOPTED AT T1 on the team DISCOVERY plane (RREQ TTL + the RREQ/RREP hop-cap
// backstops); the team DV combined-hops cap still runs on dv_hop_cap until T3. See Node::hop_cap_for in node.h
// for the live DONE/MISSING consumer list. Arithmetic, verified not assumed: a flood at ttl=N reaches a node k hops
// out carrying `hops == k-1`, so the `hops >= cap` guard first bites at k == cap+1 — the deepest teammate a single
// team flood resolves is exactly 8 hops (R4's ceiling), and the reciprocal RREP bound `hops > 2*cap` admits the
// legal worst case (a cacher 8 hops out holding an 8-hop route = 16) while still cutting a ping-pong loop.
inline constexpr uint8_t  team_hop_cap      = 8;
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
inline constexpr uint8_t  cap_team_liveness           = 16;    // §team-multihop 2c: the TEAM-plane liveness table (team_local_id-keyed, self-slotted mirror of _peer_liveness); right-sized to team scale, NEVER shares the static plane

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
// §S0 giveup: bound the drain->re-defer oscillation. A held send whose route APPEARS (drains) but is then UNUSABLE at
// select (an aliased-mobile or gateway transit next-hop) re-defers with a FRESH deferred_at_ms every cycle, so the
// send_defer_ttl_ms giveup above never fires -> the metal "send_deferred/send_drained every 1s FOREVER" burn. Cap the
// re-drain cycles at the SAME ~30s horizon as the TTL (ttl/drain_period) so a genuinely-flapping route still recovers
// inside the TTL window, while a truly-unroutable one ages out to send_failed{no_route}. s18-inert: s18 never drains a
// deferred send (0 send_drained), so redrain_count stays 0 and this bound is never reached.
inline constexpr uint8_t  send_defer_max_redrains = static_cast<uint8_t>(send_defer_ttl_ms / send_defer_drain_period_ms);   // 30

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
// F-XL-1 (2026-07-18): h_forward jitter. Sibling relays that heard the SAME H flood copy re-tx it with ZERO
// jitter today -> their forwards collide at the identical ms on any common/downstream receiver (deterministic —
// no capture; s27 hello-m4: T2+T3 forward same-ms every handoff retry, T4 behind T3 gets neither -> giveup).
// A small per-relay random delay decorrelates the siblings; the existing LBT then defers the later one. Bounded
// far below the 15-s handoff retry and comparable to a routing-frame's airtime, so the added per-hop latency is
// negligible. (Third instance of the same-ms disease: mobile OFFERs -> S6 stash; probes/rosters -> by design.)
inline constexpr uint16_t h_forward_jitter_min_ms       = 20;
inline constexpr uint16_t h_forward_jitter_max_ms       = 150;
// F-XL-2 (2026-07-19): rreq_forward de-storm — the IDENTICAL zero-jitter disease as F-XL-1, on the AODV RREQ relay
// (node_route_discovery.cpp: static + team). Sibling relays that heard the SAME RREQ flood re-broadcast it at the
// identical ms -> deterministic collision at any common/downstream receiver (no capture). A small per-relay delay
// decorrelates them; the existing LBT then defers the later one. RREQ is on the CRITICAL PATH of a first send (route
// discovery gates the parked DM), so the window is TIGHTER than F-XL-1's H-flood. ★ Unlike F-XL-1, the delay is
// DETERMINISTIC (a frame-byte mix — sibling relays differ in their `relay` id byte, so always land apart), NOT a
// rand_range() draw: RREQ discovery gates timing-fragile cross-layer deliveries, and consuming a shared-mt19937 draw
// on this hot path reorders the whole downstream sequence -> a phantom delivery flip with no functional cause. The
// frame-derived spread breaks the same-ms tie without perturbing the RNG order. See rreq_forward_stash for the rationale.
inline constexpr uint16_t rreq_forward_jitter_min_ms    = 10;
inline constexpr uint16_t rreq_forward_jitter_max_ms    = 80;
inline constexpr uint8_t  cap_parked_sends              = 8;       // send-by-hash DMs parked awaiting a hash-bind
// F-SL-1 (2026-07-19): bounded re-flood retry for a parked unresolved send. The park path fires ONE soft H at park
// time; in a QUIET net that single flood can die (RX timing) and the parked send ages out to send_hash_giveup
// with NO retry — the failure hits any sender whose contact RE-HOMED (s27 post-m2). Re-emit the H every
// park_reflood_retry_ms while parked, bounded to park_reflood_max_retries, with a DETERMINISTIC per-(hash,node,try)
// jitter of ≤ park_reflood_jitter_ms (batch B: the re-fire must not land on a fixed beat that re-collides). The jitter
// is deterministic (NOT a shared-mt19937 draw) — see park_reflood_fire for why. The hash_locate_giveup_ms age-out
// still fires (bounded, no runaway). No park-time draw either: the FIRST deadline is a fixed offset (park times differ).
//
// ★ P-BUDGET (2026-07-21): RE-SCALED for the realistic SX1262 physics regime (reported-SNR ceiling, 8 ms turnarounds,
// RX-window slop). [2026-07-25: this line read "27 ms turnarounds" — the stale overestimate; bench measures ~5-8 ms and 8
// is the ratified value. The constants below are UNCHANGED and need no re-derivation: the derivation prices a hop in
// SECONDS (~1-1.5 s of airtime + contention), so a ~19 ms per-hop turnaround delta is far below its resolution.] ROOT CAUSE (s27 hello-m2): an H flood reaches a MULTI-HOP target only PROBABILISTICALLY — its fragile
// last hop is missed whenever the target's RX-window/contention phase is unlucky, and that phase is CORRELATED over ~a
// beacon period. The old idealized-sim values (10 s spacing / 2 retries) bunched all 3 flood attempts into a 21 s window
// (two of them wasted to origin-side self-contention), so they all missed the same correlated bad phase; the send then
// sat idle from 242 s to an ~80 s giveup while a plain retry ~3 min later resolved on its FIRST propagation. The answer
// path itself is FAST (~5-6 s for the 3-hop round-trip once the flood lands) — the shortfall is entirely flood REACH.
// Fix = space each reflood >= a beacon period (so attempts hit INDEPENDENT phases) across a budget admitting several
// genuinely-independent flood windows. Derivation: a flood-out + routed-answer leg is ~1-1.5 s/hop; over an ~8-hop
// design envelope one round-trip is ~15-25 s, so the reflood spacing == one such attempt, and the giveup == park + all
// refloods (no dead zone). Bench-tunable; bounded well under mobile_home_cache_ttl_ms (300 s) so a stale-cache sender
// still re-locates within the cache horizon.
inline constexpr uint32_t park_reflood_retry_ms         = 25000;   // reflood spacing >= a beacon period -> each attempt hits an INDEPENDENT RX/contention phase (was send_defer_ttl_ms/3 = 10 s: bunched -> correlated -> all missed together)
inline constexpr uint16_t park_reflood_jitter_ms        = 3000;    // deterministic spread ceiling (break the fixed re-fire beat)
inline constexpr uint8_t  park_reflood_max_retries      = 6;       // ~6 independent flood windows before giveup (was 2 -> only one real propagation attempt)
// Parked send-by-hash age-out. DECOUPLED from send_defer_ttl_ms (the ROUTE-BLOCKED deferred-queue TTL, node_cascade.cpp):
// a parked send waits on a slow multi-hop flood ROUND-TRIP (retries needed), a deferred send waits on a LOCAL route
// reappearing on the next beacon — different regimes, different patience. Keeping send_defer_ttl_ms at 30 s also keeps
// s18 (which exercises the deferred-queue giveup but NEVER the parked path) byte-identical. = park + all refloods.
inline constexpr uint32_t hash_locate_giveup_ms         = park_reflood_retry_ms * (park_reflood_max_retries + 1);   // 175 s
// ★★ §id-hash S4b (spec §5) — THE `resolve-id-for-pubkey` INTENT. A `reqpubkey <id>` against an id with no binding
// is a TWO-STAGE operation: stage 1 asks "who owns id N?" (H_FLAG_BY_ID, want_pubkey=false), and the answer's arrival
// is what makes stage 2 — the ordinary HARD WANT_PUBKEY query BY HASH — expressible at all. The intent is the state
// that survives between them, and it is bounded on BOTH axes:
//
// · CAPACITY. The bound here is AIRTIME, not RAM. Every armed intent is one H FLOOD already on the air, and a second
//   flood follows it when the answer lands — so a ring of 4 is a de-facto rate limit on a plane where 4 floods inside
//   one round-trip is already aggressive. A FULL ring REFUSES the new command loud (CmdCode::err_resolve_pending_full)
//   and NEVER evicts: evict-oldest would silently kill a request the operator was told had been accepted, which is
//   precisely the "a success that isn't" class this whole arc exists to remove (cf. cap_pending_e2e_acks' identical
//   ruling). Re-issuing the SAME (id, plane) refreshes the deadline instead of consuming a second slot
//   (park_resolve_request's precedent).
// · TIME. The budget is ONE flood round-trip — park_reflood_retry_ms, whose own derivation above prices exactly that
//   ("a flood-out + routed-answer leg is ~1-1.5 s/hop; over an ~8-hop design envelope one round-trip is ~15-25 s").
//   NOT hash_locate_giveup_ms: that value prices park + SIX refloods, and this intent re-floods nothing — claiming its
//   patience would make the operator wait 175 s for a retry that never happens.
//   ⚠ THE EFFECTIVE BOUND IS COARSER THAN THE BUDGET, and it is stated rather than rounded away: the sweep runs on the
//   periodic kAgingTimerId (rt_aging_check_period_ms, 60 s) beside age_out_parked_sends — the SAME mechanism, for the
//   same question — so an intent expires somewhere in
//        [id_pubkey_intent_ttl_ms, id_pubkey_intent_ttl_ms + rt_aging_check_period_ms]  = [25 s, 85 s].
//   That is a real bound and the operator report names the window; a dedicated one-shot timer would tighten it to the
//   ms at the cost of the last free timer-wheel id (kCap 91, all consumed) for a diagnostic deadline. Recorded so the
//   choice is visible, not inferred.
inline constexpr uint32_t id_pubkey_intent_ttl_ms       = park_reflood_retry_ms;   // 25 s = one by-id flood round trip
inline constexpr uint8_t  cap_pending_id_pubkey         = 4;                       // see CAPACITY above — an airtime bound, refuse-when-full
// NOTE: the E2E-ack DEADLINE constants (e2e_ack_deadline_ms / _xl_ms / cap_pending_e2e_acks) live in the Gateway-scheduling
// section below — they derive from gateway_send_giveup_ms, which is declared there (a constexpr must see its base first).

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
inline constexpr uint8_t  bcn_ext_type_team_id         = 5;      // §mobile 6.2 BCN ext-TLV type 5: a team mobile's team_id (4 B LE) — the receiver scopes its team plane by it. Only a team mobile emits it (static/lone beacon has no type-5 -> byte-identical). An old parser skips it (forward-compat).
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
// ★ P-BUDGET (2026-07-21, s28 class): a TEAM flood crosses a MIXED homed/off-grid MULTI-HOP chain where the origin's
// "a relay was overheard -> it propagated" confirmation is a FALSE POSITIVE — one near relay (e.g. XO4->XO3) does NOT
// mean the FAR chain members (XO5/XH1/XH2) received it, and under realistic physics the flood's fragile hops are missed
// probabilistically. So a team flood IGNORES the relay-overheard confirm and re-offers ALL its retries (each re-inject
// gives the far members another independent shot). NON-team (single-plane, delivery-suite) floods keep the 1-retry
// relay-confirmed behaviour EXACTLY -> the delivery suite (s09/s10/s15/s16/s17, all team_id==0) is byte-inert.
inline constexpr uint8_t  channel_reoffer_team_max_retries = 3;  // team floods: several independent re-injections for mixed-chain coverage
// §F-CH-RELAY: a HOLDER (relay) re-offer budget — how many coverage-driven re-injections a relay makes to cover its own
// still-unmarked downstream team neighbours. Kept SMALL: the holder chain is self-reinforcing (each hop's holder re-offers
// independently, so the far members get shots from progressively-closer holders) and a holder re-offer is pure repair
// airtime on an already-flooded message — a large budget bloats contention (it perturbs unrelated timing-fragile deliveries)
// with diminishing coverage return. Coverage-stop (seen_by) ends it early when the downstream is confirmed.
inline constexpr uint8_t  channel_holder_reoffer_max_retries = 2;
inline constexpr uint32_t channel_reoffer_delay_ms    = 10000;   // base cadence (>= originator_retry_dedup_ms=10000 so re-floods dedup receiver-side, not double-inbox)
inline constexpr uint32_t channel_reoffer_jitter_ms   = 2000;    // +rand(0,jitter) spread so multiple origins don't re-offer in lockstep
// channel_msg_id flavor. The low bits carry the (still-unused) plaintext VARIANT value; the two high bits are FLAGS.
inline constexpr uint8_t  channel_flavor_public  = 0;
inline constexpr uint8_t  channel_flavor_group   = 1;
inline constexpr uint8_t  channel_flavor_private = 2;
inline constexpr uint8_t  channel_flavor_team    = 0x80;   // §mobile 6.3: a FLAG bit OR-ed into the M-frame flavor byte -> the frame is TEAM-scoped + carries a 4-B team_id tail (parse reads it). Low bits keep the flavor value; a non-team M has this clear -> byte-identical.
// ★★ §chan-crypt CL2a (T-K2 §2.2): the post BODY is SEALED under the team CONTENT key. A FLAG bit, like the team bit
// above and for the same reason — the low bits stay the plaintext variant value and every existing flavor byte keeps
// its meaning, so NO wire_version bump is needed (the flavor byte is an existing wire field and 0x40 was never
// emitted; the body stays opaque bytes the flood machinery never reads).
// ★ ALWAYS set TOGETHER with channel_flavor_team — v1 has exactly one content key, the TEAM key, so a crypted GLOBAL
// post is a state that cannot be reached (Node::on_command refuses `-e` without `-t`, T-K2 §2.2 / spec §2.2).
inline constexpr uint8_t  channel_flavor_crypted = 0x40;
// Body overhead of a sealed post: [seal_ctr 2][seed8 8][ct‖tag] — the ct is the plaintext length, so the constant is
// 2 + DM_NONCE_SEED_LEN + DM_TAG_LEN. Spelled numerically here because protocol_constants.h is deliberately free of
// dm_crypto.h; the identity is static_assert'ed where the two headers meet (node_channel.cpp).
inline constexpr uint8_t  channel_seal_overhead_bytes = 2 + 8 + 16;   // = 26
// Max PLAINTEXT of a sealed post: the sealed blob must still fit the 200-B channel payload carriers (ChannelEntry,
// FloodState). Origination REFUSES above this (C2 — never silently truncate a message the operator typed).
// ★ §chan-crypt CL2b/CL2c: this bounds the whole INNER `[flags][source_hash 4?][loc6?][text?]`, not the text — see
// channel_inner_overhead. ⇒ the TEXT a sealed post can carry is 173 plain, and 163 with `-l` (1 + 4 + 6 of header).
inline constexpr uint16_t channel_seal_max_plaintext_bytes = channel_msg_max_payload_bytes - channel_seal_overhead_bytes;   // 174
// ★★★ §chan-crypt CL2b/CL2c (spec 2026-07-30 §2.2.1 + §3.0, T-K2 §2.2 as CORRECTED) — THE SEALED INNER IS
// `[flags u8][source_hash 4 if bit2][loc 6 if bit1][text if bit0]`, and the first byte is a FLAGS byte, NOT an
// enumerated inner_type.
// WHY FLAGS: an ENUMERATED space must spend a codepoint per COMBINATION (text, loc, text+loc, telemetry,
// telemetry+loc, waypoint, waypoint+loc…). Both codepoint spaces this project has already EXHAUSTED — the DATA flags
// byte (0xFF, full) and q_opcode (2 bits, full) — died exactly that way. With flags each FEATURE costs one bit and
// every COMBINATION is free: THREE bits used, five spare, and the explosion never happens. It is also the only
// encoding that can express the owner's actual request, `send_channel -t -l -e` = text TOGETHER WITH a position.
// ✔ CL2c IS THE PROOF THE ENCODING CHOICE WAS RIGHT: adding `source_hash` cost ONE BIT and no reshaping. Under the
// enumerated `inner_type` T-K2 first specified it would have cost FOUR new codepoints (src, src+loc, src+text,
// src+loc+text) on a space that was already the wrong shape.
// LAYOUT mirrors the DATA inner FIELD FOR FIELD AND IN ORDER — `[dst_hash?][origin][source_hash?][location?][body]`
// (frame_codec.cpp pack_unicast_inner) — so one mental model covers both planes: fixed-size fields first in the same
// sequence, variable-length body last. ★ §chan-crypt CL2c put `source_hash` BEFORE `location` for exactly that
// reason, and it also matches the DM's 4-B LITTLE-ENDIAN encoding of the same quantity (U1).
// ⚠ THE INNER EXISTS ONLY INSIDE THE SEAL. A PLAINTEXT channel post's body is still the bare text, byte-for-byte as
// before: adding a flags byte there would change every plaintext post on the wire for nothing (bit1 requires the
// crypted flavour anyway, spec §2.4), and the global plane has no key to ever set it.
// ⚠ UNKNOWN BITS ⇒ THE READER MUST REFUSE, and that is structural, not strictness for its own sake: a future field
// sits BETWEEN the flags byte and the variable-length text, so a reader that does not know its width cannot find the
// text. A v2 sender therefore reaches v1 readers only by re-keying the feature, never by silent partial parse (C2).
// ⚠ ONE CROSS-BIT RULE, AND ONLY ONE: bit1 (location) REQUIRES bit2 (source_hash). It is not taste — an unattributable
// position is precisely what CL2c exists to remove, so a located inner without a sender is malformed on BOTH sides
// (the composer below cannot build one; ingest_channel_m refuses one). The converse is NOT a rule: bit2 alone is
// well-formed and readable. ★ NO `wire_version` BUMP FOR ANY OF THIS — the inner lives entirely inside the AEAD
// ciphertext, so a non-keyholder observes only a body four bytes longer; the flags byte IS this format's version
// mechanism, enforced by the unknown-bit refusal above.
inline constexpr uint8_t  channel_inner_flag_text     = 0x01;   // a text body is present (variable length, LAST)
inline constexpr uint8_t  channel_inner_flag_location = 0x02;   // a 6-B pack_loc6 position is present (fixed, SECOND)
// ★★★ §chan-crypt CL2c (owner 2026-08-01: "source_hash is required — when send channel message contains location it
// is required to include which node location is it"). The SENDER'S FULL 32-bit key_hash32, 4 B LE, fixed, FIRST.
// WHY IT IS CARRIED RATHER THAN INFERRED — the inference CL2b shipped is structurally weaker, and CL2b measured it
// failing: `origin` on a team post is a `team_local_id`, resolved through `_team_keys` (learned from the sender's
// BEACON) to a full hash. Two ways that loses: we may never have heard the sender's beacon at all, and a
// team_local_id is DAD-assigned and RE-PICKABLE, so a stale row names the WRONG peer. Both produced
// `peer_location_unattributed`, after which the position was surfaced but never retained. Carrying the fact removes
// the whole class: the post names its own sender, inside the seal, with no dependence on the receiver's beacon
// history. ⚠ IT DOES NOT ADD AUTHENTICATION and must never be read as if it did — see PeerLocSrc (node.h): the team
// content key is SHARED, so any keyholder can already publish a post under another member's `origin`. This makes
// attribution RELIABLE, not UNFORGEABLE.
inline constexpr uint8_t  channel_inner_flag_source   = 0x04;
inline constexpr uint8_t  channel_inner_flags_known   = channel_inner_flag_text | channel_inner_flag_location
                                                      | channel_inner_flag_source;
// ★ 6 BYTES, NOT the 8 (`lat_e7 i32, lon_e7 i32`) T-K2 originally sketched: the DM plane already carries `pack_loc6`
// (~11 m). Two encodings on two planes would force the companion to carry two decoders and make one plane silently
// more precise than the other (U1).
inline constexpr uint8_t  channel_inner_location_bytes = 6;
inline constexpr uint8_t  channel_inner_source_bytes   = 4;     // key_hash32, LE — the DM inner's own width for the same field
// The bytes a sealed inner spends BEFORE the text, DERIVED FROM THE FLAGS BYTE ITSELF. ONE definition, read by the
// size pre-flight (Node::on_command), the assembly (do_send_channel) and the parse (ingest_channel_m) — so the three
// can never disagree.
// ★ §chan-crypt CL2c took the argument from a bool (CL2b) to the flags byte: with two optional fields a bool PAIR is
// swappable at a call site and would silently mis-size the header, whereas the parse already HOLDS the flags byte and
// the send side composes one — so this form is the only one where the layout cannot drift from the bits that announce
// it. UNKNOWN bits are not this function's business; the parse refuses them before asking for a size (see below).
inline constexpr uint16_t channel_inner_overhead(uint8_t flags) {
    return static_cast<uint16_t>(1u + ((flags & channel_inner_flag_source)   ? channel_inner_source_bytes   : 0u)
                                    + ((flags & channel_inner_flag_location) ? channel_inner_location_bytes : 0u));
}
// The SEND side's flags byte, composed in ONE place. ★ THE `bit1 ⇒ bit2` RULE LIVES HERE and nowhere else: a located
// post always names its sender, so "required whenever location is set" is a property of the composer rather than a
// check some caller might forget. (The READ side accepts bit2 alone — a post that names its sender without carrying a
// position is well-formed and costs 4 B; forbidding a COMBINATION is the enum thinking that exhausted q_opcode. This
// node simply has no verb that asks for one yet.)
inline constexpr uint8_t channel_inner_flags(bool with_text, bool with_location) {
    return static_cast<uint8_t>((with_text     ? channel_inner_flag_text : 0u)
                              | (with_location ? (channel_inner_flag_location | channel_inner_flag_source) : 0u));
}
// Rate limit on the `team_channel_no_key` push (T-K2 §2.2). One prompt per minute is enough for the app to raise
// "ask a teammate for the key"; a busy team channel would otherwise emit one per post to a member who cannot read any
// of them. Telemetry (`channel_crypt_no_key`) is NOT rate-limited — the sim/bench wants every occurrence.
inline constexpr uint32_t team_channel_no_key_push_min_ms = 60000;

// ---- Identity binding ------------------------------------------------------
inline constexpr uint32_t id_bind_ttl_ms = 172800000;   // 48 h
// E2E peer-pubkey cache (Phase 1 §6): key_hash32 -> ed_pub. Sparse (only sealed-DM partners); per LayerRuntime.
inline constexpr uint16_t cap_peer_keys   = 16;
inline constexpr uint32_t peer_key_ttl_ms = id_bind_ttl_ms;   // pubkeys are immutable; aging is cache hygiene, not correctness
// ★★ §AB4 (address-book spec 2026-07-29 §2.7.1): the RETAINED PEER LOCATION ring — key_hash32 -> last known position.
// Sized to match cap_peer_keys / cap_team_liveness / _team_keys (every other team-scale table in the tree) and
// comfortably above R4's 3-10 members. ⚠ NOT a wire constant: nothing on air depends on it, it is purely this node's
// RAM budget, so widening it costs 20 B/slot of board RAM and breaks no peer. NO ttl twin on purpose — the ring never
// ages entries out, it REPORTS the age (`loc_age_s`) and lets the app decide; see node.h's PeerLoc for why.
inline constexpr uint8_t  cap_peer_loc    = 16;

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

// ★★★ §hybrid-rts S2 (2026-08-08) — THE COMPLETED-FLIGHT CACHE'S RETENTION HORIZON, DERIVED NOT PICKED.
// It answers "how long can the SAME flight, with the SAME identity, come back to THIS hop?" — which is not a
// dedup question but a RETRY question, so it is bounded by the longest live requeue patience, and that is the
// gateway doorstep hold declared immediately above.
// ★ MEASURED (§HYBRID-RTS-S0, re-read not re-derived): `gateway_hold_requeue.age_ms` reaches **149 134 ms**
//   over 1 175 firings and ONE flight was traced re-arriving at ONE hop **147 658 ms** after its earlier
//   completion. The DIRECTLY measurable exact-retry set only reached 18 971 ms, and the RETIRED
//   `last_acked_ttl_ms` (10 s, still declared above but referenced by nothing) covered just 73 of 74 of those.
// ⛔ DO NOT re-spell this as a bare 150000: it must MOVE WITH its base. `gateway_send_giveup_ms` is the bound at
//    which the flight is GIVEN UP rather than requeued, so nothing can legitimately re-arrive after it. A
//    `send_giveup.age_ms` observed beyond 150 s is a LATE CALLBACK, not another requeue, and grants no extension.
inline constexpr uint32_t completed_flight_cache_ttl_ms = gateway_send_giveup_ms;   // 150 000 ms
// ★★★ THE ENTRY CAP, AND IT IS **MEASURED, NOT PICKED** — a FIXED PER-LAYER ARRAY (no heap, no per-peer dynamic
// structure); a full table prunes the expired entries first and then evicts the OLDEST (min-expiry) —
// `record_seen_origin`'s policy verbatim (U1).
// The census ran on the S1 wire arm with the cap raised to 64 (unbounded for this corpus) and every would-be hit
// tagged with its recency rank, so capacity k is evaluated without rebuilding once per k:
//     4 515 stores · 429 hits · max live entries 23 globally and 12 for ONE immediate sender
//     capacity  1 -> 324/429 hits (75.5 %)   ⛔ "one latest flight per immediate sender" is PROVEN INSUFFICIENT
//     capacity  2 -> 393 (91.6 %) · 4 -> 418 (97.4 %) · 8 -> 424 (98.8 %)
//     capacity 12 -> ★ 429/429 (100 %), and so is every larger capacity
// ⇒ 12 is the SMALLEST capacity that loses no measured hit, and it exactly meets the measured per-sender maximum.
// ⚠ AT A 150-SECOND RETENTION THE CAP IS THE BINDING CONSTRAINT, NOT THE TTL — but the TTL is load-bearing too, and
//   that is measured on the same arm: a 10-second window keeps only 246/429 hits, 30 s keeps 349, 60 s keeps 416.
//   Neither number may be "simplified" toward the other.
inline constexpr uint8_t  cap_completed_flights = 12;

// ★ P-BUDGET (2026-07-24, shelf item (i) — the E2E-ack DEADLINE). An app DM with DATA_FLAG_E2E_ACK_REQ is a POSITIVE-ONLY
// receipt today: the send_e2e_acked push fires when the DATA_TYPE_E2E_ACK returns, and NOTHING fires when it never does
// (no awaiting-ack state existed). A fixed no-heap pending-ack ring (cap_pending_e2e_acks) ARMS silently when such a send
// mints its ctr and CLEARS silently on the matching send_e2e_acked — so any stream where every -a send is acked stays
// byte-identical. If the budget elapses first the firmware pushes send_failed{e2e_ack_timeout}. DERIVATION — the round trip
// is 2x the send's worst-case ONE-WAY delivery budget (no magic numbers; both from a named bench-tunable base):
//   • same-layer: one-way worst case = send_defer_ttl_ms (a routed DM ages out of the deferred queue in this window), so the
//     round trip DM-out + ack-back is 2x. The ctr mints only AFTER a parked/hash send RESOLVES, so the flood-reach patience
//     (hash_locate_giveup_ms) is already spent by arm time — this budget only covers the post-resolution round trip.
//   • cross-layer / delegated: the DM and the reversed ack each cross a gateway WINDOW — the doorstep hold is
//     gateway_send_giveup_ms, so the round-trip patience is 2x that latency class. Mobile delegate paths inherit it (the
//     mobile awaits an ack traversing its home + the far gateway both ways). A same-layer delegated wrapper also uses this
//     larger tier (the home may route the re-origination cross-layer).
// CONTRACT SEMANTIC (also in command.h): a timeout means delivery was never CONFIRMED, NOT that it failed — the DM may have
// arrived and the ack died returning; a LATE ack (after expiry) still fires send_e2e_acked and the app resolves (the ring
// entry is already gone, so the clear is a harmless no-op — no double-free/stale-slot hazard). Bench-tunable; both bounded
// at/under mobile_home_cache_ttl_ms (300 s) so a re-home invalidates a stale wait first.
inline constexpr uint32_t e2e_ack_deadline_ms    = 2 * send_defer_ttl_ms;       // 60 s  — same-layer round trip
inline constexpr uint32_t e2e_ack_deadline_xl_ms = 2 * gateway_send_giveup_ms;  // 300 s — cross-layer / delegated (gateway-window latency class)
inline constexpr uint8_t  cap_pending_e2e_acks   = 8;                           // fixed no-heap ring; a full ring REFUSES a new -a send LOUD (CmdCode::err_ack_ring_full) — NEVER evict-oldest (that would re-create the silent class)
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
// ★★ §MH-S2 §5.3.1 — THE **HOST** SIDE OF THE OFFER HANDSHAKE. Read the next two lines together with
// `cap_mobile_offers` five lines below, and then never conflate them again:
//   • `cap_pending_mobile_offers` (HERE, host)  = how many mobiles ONE HOME may owe an armed, targeted OFFER at once.
//   • `cap_mobile_offers`      (below, mobile)  = how many OFFERs ONE MOBILE may weigh inside one DISCOVER window.
// ⛔ NEVER derive, alias, `= cap_mobile_offers`, or default one from the other, and never describe them as "the same
// 8". They answer different questions, they are sized by different pressures (host RAM + cap_host_mobiles here; RF
// diversity there), and EITHER MAY BE RETUNED ALONE. The equal starting value is a coincidence, not a relationship.
// RAM cost, MEASURED not inferred (§MH-S2 go/no-go): sizeof(Node::PendingMobileOffer) = 40 B on all five real ABIs
// x 8 = 320 B node-global (NOT x MR_N_LAYERS — see the ring's own note in node.h).
inline constexpr uint8_t  cap_pending_mobile_offers      = 8;        // HOST: concurrently ARMED targeted OFFERs awaiting their jitter fire
// ★★ [[B137]] — the PENDING-ID RESERVATION bound (§5.3.2 owner ruling). A local id proposed in an OFFER is reserved
// from ADMISSION until the matching CLAIM arrives or this elapses, so four concurrent mobiles are offered four
// DISTINCT ids instead of four copies of the same one. Sized from the handshake it must span, not picked round:
// the host's own OFFER jitter (join_offer_backoff_max_ms = 1000) + the mobile's collect-OFFERs window
// (mobile_offer_window_ms = 2000) + its FIRST no-host retry (mobile_discover_backoff_min_ms = 5000, which
// re-DISCOVERs and re-uses the SAME reservation idempotently) = 8000, plus 2 s of air/LBT slop.
// ⚠ RE-CHECKED AT §MH-S3 (V1, against the code and not against this note), because that slice widened one
// of the terms: the collect window is now `mobile_offer_window_ms + rand(0, mobile_claim_jitter_ms + 1)` =
// 2000..3000 (§5.4). ★ AND THE RE-CHECK CORRECTED THE NOTE'S OWN ARITHMETIC: the `+ 5000 first retry` term
// above is CONSERVATIVE TO THE POINT OF BEING WRONG — `mobile_offer_admit`'s re-arm arm re-stamps
// `reserve_until_ms = now + this` on every re-DISCOVER, so a retry RESTARTS the budget rather than
// consuming it. The span this constant actually has to cover is ONE round, measured from admission:
// the mobile's own window+jitter, i.e. 3000 worst case (the host's OFFER jitter runs INSIDE it, not before
// it — both clocks start at the same DISCOVER). ⇒ ~7 s of margin, and `mobile_claim_jitter_ms` could rise
// to ~7000 before this would have to move. See its own note.
// ⚠ It is an UPPER BOUND on a leak, not a schedule: the normal path releases the reservation at the CLAIM, long
// before this. Raising it costs ring occupancy (8 slots), never correctness; lowering it below ~3 s would expire a
// reservation while the mobile is still inside its own collect window.
inline constexpr uint32_t mobile_offer_reservation_ms    = 10000;    // HOST: [[B137]] pending-id reservation lifetime
// ★★★ [[B145]] §MH-S2b — THE RE-SPACE FLOOR, and it exists because "at most ONE due OFFER per callback" was NOT the
// same statement as "at most one OFFER per millisecond". `mobile_offer_fire` transmits one entry and re-arms for the
// next; when that next entry is ALREADY OVERDUE the re-arm computed a delay of **zero**, and the production pump
// (`src/fw_main.cpp`, `for (int id; (id = g_hal.pop_due_timer()) >= 0; )`) drains every due timer against a clock it
// re-reads but which need not have advanced. `TimerWheel::pop_due` fires on `_due <= now`, so a zero-delay re-arm is
// due IMMEDIATELY and the same drain pass fires the callback again — four overdue entries become four OFFERs in one
// millisecond, which is exactly the burst the 100..1000 ms jitter exists to prevent.
// ⛔ NOT A JITTER, AND DELIBERATELY NOT AN RNG DRAW: S3 is this arc's only planned RNG re-anchor, so this is a
// CONSTANT. It is only ever substituted for a delay that would otherwise be 0 — a positive computed delay is passed
// through untouched, so the ordinary path is byte-identical.
// ⚠ ⛔ NOT AN ALIAS OF `join_offer_backoff_min_ms`, which happens to hold the same 100 today: that one is the LOW
// BOUND OF A RANDOM WINDOW (retune it and the OFFER jitter changes), this one is the MINIMUM SPACING BETWEEN TWO
// CONSECUTIVE OFFER TRANSMISSIONS from one home (retune it and only the overdue-drain changes). Same value, different
// question — the `cap_pending_mobile_offers` / `cap_mobile_offers` rule twelve lines up, applied again.
// SIZING: any value >= 1 breaks the same-pump re-entry (`pop_due` needs `_due <= now`), so 1 would be *correct* and
// useless — it would hand the radio a second frame in the next millisecond. 100 is the smallest value that is also a
// real inter-frame gap: it matches the de-storm window's own floor, and **in the normal fully-overdue drain** 8 slots
// drain in 700 ms, well inside both `mobile_offer_window_ms` (2000) and `mobile_offer_reservation_ms` (10000).
// ⚠ THAT IS THE DRAIN CALCULATION AND NOTHING MORE. An earlier revision of this line claimed "so no entry can be
// re-spaced past its own reservation" — **WITHDRAWN, it overclaims**: the 700 ms is measured from the moment the
// drain starts, so a timer first serviced close to an entry's expiry CAN still be re-spaced past that entry's
// reservation. The reservation sweep is what handles it (the entry expires and its id is released); re-spacing is
// not a guarantee about reservation lifetime, and no caller may assume one.
inline constexpr uint32_t mobile_offer_respace_ms        = 100;      // HOST: [[B145]] floor for an OVERDUE re-arm (never a draw)
// §mobile 2b (mobile-side registration FSM):
inline constexpr uint8_t  cap_mobile_offers              = 8;        // OFFERs collected in one DISCOVER window (MOBILE side — ⛔ NOT the host ring above)
inline constexpr uint32_t mobile_discover_backoff_min_ms = 5000;     // exp-backoff floor when no host answers (B3)
inline constexpr uint32_t mobile_discover_backoff_max_ms = 120000;   //   ceiling
inline constexpr uint32_t mobile_offer_window_ms         = 2000;     // collect-OFFERs window before deciding (≈ B4)
// ★★ §MH-S3 §5.2 — THE AUTOMATIC-BOOT STARTUP JITTER. `on_init` kicks the registration FSM with
// `after(0, kMobileDiscoverTimerId)`; a fleet powered from one switch therefore DISCOVERed at t=0 to the
// millisecond, so every mobile's first frame collided with every other mobile's first frame before LBT
// could help. 0..this is drawn ONCE, at boot, and ONLY when the kick is AUTOMATIC (`mobile_autoregister`):
// ⛔ a MANUAL `mobile register` stays IMMEDIATE (§5.2: "the manual first attempt is immediate"), and a
// team-only mobile (autoregister off, team_id set) whose kick exists solely to run team-DAD draws NOTHING.
// SIZING: one collect-OFFERs window. Large enough that N mobiles' DISCOVERs land in distinct milliseconds
// on any realistic fleet; small enough that boot-to-attach is not visibly slowed (the host answers inside
// its own 100..1000 ms OFFER jitter either way). ⛔ NOT an alias of `mobile_offer_window_ms` despite the
// equal value — that one is how long a mobile LISTENS, this one is how long it WAITS before speaking;
// retune either alone (the `cap_pending_mobile_offers` / `cap_mobile_offers` rule, applied again).
inline constexpr uint32_t mobile_boot_jitter_ms          = 2000;     // MOBILE: 0..this before the AUTOMATIC boot DISCOVER
// ★★ §MH-S3 §5.4 — CLAIM DE-SYNCHRONIZATION. The collect-OFFERs window is a MINIMUM, not a deadline: the
// claim guard is armed at `mobile_offer_window_ms + rand(0, this + 1)`, so mobiles that opened their
// windows in the same millisecond do not all close them — and CLAIM — in the same millisecond.
// ★ THE SELECTION RULE IS UNCHANGED: the chosen OFFER is still the STRONGEST received before this mobile's
// own deadline. The jitter moves the deadline; it does not touch the compare (`mobile_claim_guard_fire`).
// SIZING, against the reservation budget it must fit inside (`mobile_offer_reservation_ms` = 10000): host
// OFFER jitter (join_offer_backoff_max_ms 1000) + collect window (2000) + THIS (1000) = 4000 worst case,
// i.e. a CLAIM always arrives with ~6 s of reservation left. Anything up to ~7000 would still fit.
// ⛔⛔ THE OLD JUSTIFICATION FOR 1000 WAS FALSE TWICE OVER AND IS RETRACTED (§MH-S3-QA item 5, 2026-08-07).
// It read: *"1000 is chosen because it already spreads a full ring of 8 mobiles by ~125 ms each, which is
// more than a CLAIM's airtime"*. Both halves are wrong, and both are now MEASURED rather than asserted:
//   ① ⛔ A RANDOM DRAW GUARANTEES NO SPACING AT ALL. 1000/8 = 125 is the MEAN gap of an even comb, not
//      anything 8 independent uniform draws produce. Measured (2e6 Monte-Carlo trials, 8 draws on
//      [0,1000]): P(all seven adjacent gaps >= 125 ms) = **5.0e-7** — closed form (1 - 7*125/1000)^7 =
//      4.77e-7 — and the EXPECTED MINIMUM gap is **15.9 ms**, not 125.
//   ② ⛔ "MORE THAN A CLAIM'S AIRTIME" IS FALSE ON MOST SUPPORTED PHYs. An 11-byte J CLAIM
//      (`pack_j_claim`, whole on-air frame) at CR 4/5 and `preamble_sym` = 16, computed with THIS repo's
//      own `airtime_ms()` model: **BW 125 kHz — SF7 49 ms · SF8 98 ms · SF9 177 ms · SF10 354 ms ·
//      SF11 708 ms · SF12 1417 ms**. So 125 ms exceeds a CLAIM only at SF7/SF8 on 125 kHz; at SF12 one
//      CLAIM is 1417 ms — **longer than the entire 1000 ms jitter window**. (BW 250 kHz: 24/49/88/177/
//      313/708 ms. BW 500 kHz: 12/24/44/88/156/313 ms.)
//   ⇒ Even at the FASTEST 125 kHz PHY the measured P(some pair of the 8 lands within one 49 ms CLAIM
//      airtime) is **0.965**; at SF9's 177 ms it is **1.000**.
// ★ SO WHY 1000, HONESTLY: it is a TUNING CHOICE, not a separation guarantee, and the mechanism it buys is
//   INDEPENDENCE, not spacing. Un-jittered, a fleet that opened its windows in the same millisecond
//   collides on EVERY round, for ever — probability 1, permanently correlated. Jittered, each retry is a
//   fresh independent draw, so a colliding pair separates on a subsequent round instead of never; that is
//   the §5.4 property, and it does not require the draws to be spread. 1000 is then bounded ABOVE by the
//   reservation budget arithmetic on the two lines above (4000 of 10000 worst case) and by attach latency,
//   and bounded BELOW by wanting the window to be many CLAIM airtimes wide on the PHYs we actually fly.
//   ⚠ It is deliberately NOT sized to exceed a CLAIM's airtime on the slow PHYs — doing so would need
//   >1400 ms for SF12 alone, and no measurement says that buys anything. Retune it with a measurement.
inline constexpr uint32_t mobile_claim_jitter_ms         = 1000;     // MOBILE: 0..this ADDED to the collect window
inline constexpr uint32_t mobile_home_lost_ms            = 90000;    // no BCN from home -> re-register
inline constexpr uint32_t mobile_reclaim_ms              = 600000;   // 10-min periodic re-CLAIM (self-heal + refresh)
inline constexpr uint32_t mobile_liveness_ms            = 1500000;  // §mobile hash-locate: the home proxies for a mobile ONLY if heard within 25 min (≈2.5× re-CLAIM) — kills the long-term black hole; a just-died mobile is proxied ≤~25 min then goes silent
inline constexpr uint8_t  cap_learned_layers             = 4;        // §mobile 5a: neighbouring layers a mobile LEARNS (pulls from a gateway) to cross to on home-lost
inline constexpr uint32_t learned_layer_ttl_ms           = 3600000;  // §mobile 5a: 1 h (layer records are static)
inline constexpr uint32_t mobile_layer_query_period_ms   = 600000;   // §mobile 5a: 10-min directory refresh while connected
inline constexpr uint8_t  cap_mobile_home_cache          = 16;       // §mobile 3c: sender-side mobile_hash->home_id cache (id_bind can't hold a 2nd hash per node)
inline constexpr uint32_t mobile_home_cache_ttl_ms       = 300000;   // §mobile 3c: 5-min TTL (§17-C2 "short, minutes")
// §S6 presence plane (P-probe / P-roster) — the periodic-sync REPLACEMENT (spec 2026-07-17 §S6.3).
// All bench-tunable; named per the S6.3 table. Timing is DECOUPLED from beacon_ms — home-loss is now
// detected in ~T+15 s (was max(90 s, 2×beacon_ms) = 30 min at the fleet's 15-min beacons).
inline constexpr uint32_t presence_check_base_ms       = 120000;   // the mobile's probe/check period T at quality=ok
inline constexpr uint32_t presence_check_min_ms        = 60000;    // T clamp: weak/critical tier (aggressive)
inline constexpr uint32_t presence_check_max_ms        = 480000;   // T clamp: strong tier (min(4·base,max))
inline constexpr uint32_t presence_probe_jitter_ms     = 8000;     // 0..this drawn per probe — desynchronizes the fleet
inline constexpr uint32_t presence_probe_retry_ms      = 5000;     // unanswered-probe retry spacing
inline constexpr uint8_t  presence_probe_k_miss        = 2;        // retries before HOME LOST (detection ≈ T + k·retry)
// ★★ §MH-S4 §7.1 steps 5-6 — LIVE SINCE 2026-08-08, AND IT HAD **ZERO CONSUMERS** BEFORE THAT (measured by grep,
// and proven observably by the §S0-4 characterization case, which watched a full 135 000 ms presence cycle without
// one re-CLAIM reaching the air). Its TWO consumers are now `Node::presence_claim_unconfirmed` (the budget test and
// the exhaustion path) and `Node::mobile_reclaim_send` (the same-epoch retransmit). ⇒ the count bounds BOTH §7.1
// triggers, deliberately as ONE budget: a chosen-home roster that omits our hash (step 5) and silence at the
// confirmation deadline (step 6) are the same failure — "the CLAIM was not recorded" — so they must not each get
// three tries. Exhaustion returns the mobile to `seeking`, never to a false `registered`.
inline constexpr uint8_t  presence_claim_max_retries   = 3;        // §MH-S4 §7.1: same-epoch, same-local-id re-CLAIMs at the SAME home before a full re-DISCOVER (heals a CLAIM lost to an RX collision)
// ★★★★ §MH-S4b §7.1 step 3 — THE **SOLICITATION** PAIR. §MH-S4 armed the confirmation deadline at the STEADY
// check period (`presence_check_base_ms`, 120 000 ms) and then, in the same callback that sent the probe, spent a
// re-CLAIM. Two things were wrong with that and BOTH are timing constants, so they are named here:
//   1. §7.1 step 3 asks for a **SHORT** probe, not one T away — the whole point is to confirm within seconds of the
//      CLAIM rather than to sit provisionally attached for two minutes;
//   2. the re-CLAIM was spent BEFORE the probe could possibly be answered, so the probe was decorative. §7.1's
//      steps 3-4 are two events with a WAIT between them, and a budget may only be spent once that wait expires.
// ⓘ BOTH are handed to `presence_arm_check`, so each still costs the ONE pre-existing `presence_probe_jitter_ms`
//   draw that function has always made — no new draw SITE (the number of deadlines does change, and that is the
//   behaviour under change).
// ⓘ The two values themselves are declared BELOW `presence_roster_min_interval_ms`, because the confirmation
//   deadline is SIZED FROM it and the `static_assert` that pins that relationship must see both.
inline constexpr uint32_t presence_roster_coalesce_min_ms = 500;   // home: collect probes this long, then answer ONCE
inline constexpr uint32_t presence_roster_coalesce_max_ms = 1500;
inline constexpr uint32_t presence_roster_min_interval_ms = 10000; // home: roster rate-limit floor (spoof/burst)
inline constexpr uint32_t presence_reregister_stagger_ms  = 5000;  // 0..this after a roster-absent (home reboot) so N mobiles don't DISCOVER in lockstep
inline constexpr uint32_t presence_claim_solicit_ms    = 3000;      // §MH-S4b §7.1 step 3: CLAIM (or re-CLAIM) -> the SHORT searching solicitation probe. Sized ABOVE presence_roster_coalesce_max_ms (1500) so the home's OWN registry-change roster — scheduled when it records the CLAIM — normally confirms us before we even ask.
// ★ SIZED AGAINST THE HOME'S OWN RATE LIMIT, not guessed: a home that has just rostered (e.g. on recording our
// CLAIM) cannot roster again for `presence_roster_min_interval_ms` = 10 000 ms, and then adds up to
// `presence_roster_coalesce_max_ms` = 1500 ms of coalescing. A deadline shorter than the sum would declare
// "silence" while the answer was still legally queued at the home — a self-inflicted false negative.
inline constexpr uint32_t presence_claim_confirm_ms    = 12000;     // §MH-S4b §7.1 steps 4-6: the solicitation's ROSTER DEADLINE. Only when THIS expires is a re-CLAIM spent.
static_assert(presence_claim_confirm_ms >= presence_roster_min_interval_ms + presence_roster_coalesce_max_ms,
              "presence_claim_confirm_ms must outlast the home's roster rate-limit floor + max coalesce, or 'silence' can be declared while the answer is still queued at the home");
inline constexpr uint32_t presence_rehome_dwell_ms     = 300000;   // anti-flap: min time (since last adopt) before a VOLUNTARY re-home
inline constexpr uint32_t presence_candidate_hold_ms   = 60000;    // §S6.4-C: a better candidate must be sustained this long before re-homing
inline constexpr uint32_t presence_safety_pull_ms      = 21600000; // D6: 6-h layer-directory safety pull (else purely dir_epoch-driven)
// §S6 / D11 link-quality tiers (2 bits on the wire): the home maps its per-mobile SNR EWMA to a tier; the mobile
// maps its heard-SNR EWMA of candidate homes to the same tiers for the re-home compare. Bench-tunable (dB, Q4).
// Boundaries {−12, −4, +4} dB ride the ONE canonical family (spec 2026-07-19 §2): {−12, −4} is EXACTLY the ACK
// reverse-link 2-bit bucket's boundary pair (bucket_of_snr_2b, node_mac_rx.cpp:21), extended by +4 for the 4th
// tier — one coarse family, the ACK wire bytes themselves UNTOUCHED. (The old 0/20/40 dB scale was unreachable on
// real LoRa, where an SX126x never reports SNR above ~+12 → every link read weak/critical and the §S6.4-C
// voluntary re-home could effectively never fire; see the canonical "Link quality" section above.)
enum PresenceQuality : uint8_t { presence_q_critical = 0, presence_q_weak = 1, presence_q_ok = 2, presence_q_strong = 3 };
inline constexpr int16_t  presence_q_weak_min_q4      = db_to_q4(-12.0f);  // −12 dB — below = critical (== ACK bucket low boundary)
inline constexpr int16_t  presence_q_ok_min_q4        = db_to_q4(-4.0f);   //  −4 dB — weak..ok boundary (== ACK bucket high boundary)
inline constexpr int16_t  presence_q_strong_min_q4    = db_to_q4(4.0f);    //  +4 dB — ok..strong boundary (family extended by +4)
inline constexpr uint8_t  presence_rehome_tier_delta  = 2;                 // §S6.4-C: candidate must be >= this many tiers better
inline constexpr uint8_t  cap_presence_candidates     = 8;                 // §S6.4-C: overheard candidate-home table (RAM-bound)
// Pure: SNR (Q4 dB) -> 2-bit presence tier. Shared by home (per-mobile EWMA) + mobile (heard candidate EWMA).
inline constexpr uint8_t presence_quality_tier(int16_t snr_q4) {
    return snr_q4 >= presence_q_strong_min_q4 ? presence_q_strong
         : snr_q4 >= presence_q_ok_min_q4     ? presence_q_ok
         : snr_q4 >= presence_q_weak_min_q4   ? presence_q_weak
                                              : presence_q_critical;
}

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
// rewrite); segment <= store cap. A record = a 32-B header (inbox_record_header_bytes) + body, body <= inbox_max_body, so a
// single record always fits a segment (the "record > segment" path is a defensive guard, never hit).
// (V1 2026-08-06: the header has read 32 B since §GapA-durable added origin_layer; the "31-B" text here was drift.)
inline constexpr uint32_t inbox_dm_store_bytes     = 512u * 1024;   // ~thousands of short DMs
inline constexpr uint32_t inbox_chan_store_bytes   = 128u * 1024;   // freer (channels evict sooner)
// The segment (delete-oldest granularity) = the read-scratch size: read_since loads a WHOLE segment into a
// fixed scratch buffer, so a segment must NOT exceed it (a larger segment would silently truncate the read).
// Hence one value for both, enforced by the store's begin() guard. (Earlier 32K/16K spec values were never
// wired — they'd have overrun the 4 KB scratch; reconciled to the real, scratch-bounded size.)
inline constexpr uint32_t inbox_segment_bytes      = 4u * 1024;     // 4 KiB; == the store read-scratch
inline constexpr uint8_t  inbox_max_body           = max_payload_bytes_hard_cap;  // 241 (record body cap)
// §3.5/§6.2 single-record delete = an appended TOMBSTONE (owner ruling 2026-08-06: no rewrite, no segment erase).
// Inbox::pull() must know the tombstones BEFORE it streams the records they cancel (a tombstone is always appended
// AFTER its target), so it runs a bounded pre-pass that collects tombstone targets into a fixed array. This is that
// array's size, and therefore ALSO the hard cap Inbox::erase() enforces on the number of tombstones a single store
// may hold at once: with the write side capped at the same value, the read side's array can NEVER overflow, so a
// deleted record can never be emitted for want of space. Cost = 4*32 = 128 B of STACK inside pull() (nothing in .bss,
// nothing in Node). 32 is not arbitrary: MR_RAM_INBOX_SLOTS is 32, so on the FixedInboxStore (the Heltec/ESP32 UI
// target) the cap can never bind before the ring itself evicts. On the big QSPI store it is a real product limit —
// the 33rd delete with 32 tombstones still un-evicted returns io_error ("DELETE FAILED"), never a silent no-op.
inline constexpr uint8_t  inbox_max_tombstones     = 32;

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
