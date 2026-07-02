// MeshRoute — test_protocol_constants.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Pins the C++ PROTOCOL constants table to the same values as the
// Lua model's `PROTOCOL = {...}` block (spec/dv_dual_sf.lua).
// If you change one side, the other MUST change in the same commit.

#include "doctest.h"

#include "protocol_constants.h"

namespace P = meshroute::protocol;

TEST_CASE("Q4 conversion roundtrip") {
    // Integer dB values convert exactly.
    CHECK(P::db_to_q4(5.0f)  == 80);
    CHECK(P::db_to_q4(12.0f) == 192);
    CHECK(P::db_to_q4(40.0f) == 640);
    CHECK(P::db_to_q4(80.0f) == 1280);
    CHECK(P::db_to_q4(0.0f)  == 0);
    CHECK(P::db_to_q4(-20.0f) == -320);

    // Round-trip: q4_to_db(db_to_q4(x)) == x when x is exactly representable.
    CHECK(P::q4_to_db(P::db_to_q4(5.0f)) == doctest::Approx(5.0f));
    CHECK(P::q4_to_db(P::db_to_q4(-15.0f)) == doctest::Approx(-15.0f));

    // Q4 5/16 = 0.3125 — matches Lua alpha quantization.
    CHECK(P::q4_to_db(5) == doctest::Approx(0.3125f));
}

TEST_CASE("Q4 saturation") {
    CHECK(P::db_to_q4(10000.0f) == P::q4_max);
    CHECK(P::db_to_q4(-10000.0f) == P::q4_min);
}

TEST_CASE("SF demod thresholds match Lua's SF_DEMOD_THRESHOLD table") {
    // Lua reference: SF5 = -40, SF6 = -80, SF7 = -120, ..., SF12 = -320.
    CHECK(P::sf_demod_threshold_q4_table[5]  == -40);
    CHECK(P::sf_demod_threshold_q4_table[6]  == -80);
    CHECK(P::sf_demod_threshold_q4_table[7]  == -120);
    CHECK(P::sf_demod_threshold_q4_table[8]  == -160);
    CHECK(P::sf_demod_threshold_q4_table[9]  == -200);
    CHECK(P::sf_demod_threshold_q4_table[10] == -240);
    CHECK(P::sf_demod_threshold_q4_table[11] == -280);
    CHECK(P::sf_demod_threshold_q4_table[12] == -320);
}

TEST_CASE("select_data_sf_for_snr mirrors Lua select_data_sf (dv:3043)") {
    constexpr uint16_t SF79 = (1u << 7) | (1u << 9);   // allowed_data_sfs = {7, 9}
    constexpr int16_t  M    = P::sf_margin_q4;          // 80 Q4 = 5.0 dB
    // floor+margin (Q4): SF7 = -120+80 = -40 ; SF9 = -200+80 = -120

    SUBCASE("good link -> fastest SF") {
        CHECK(P::select_data_sf_for_snr(0,    SF79, M) == 7);   //   0 >= -40 -> SF7
        CHECK(P::select_data_sf_for_snr(-40,  SF79, M) == 7);   // -40 >= -40 -> SF7 (boundary)
    }
    SUBCASE("weak link -> robust SF (the s18 case)") {
        CHECK(P::select_data_sf_for_snr(-41,  SF79, M) == 9);   // below SF7 margin -> SF9
        CHECK(P::select_data_sf_for_snr(-50,  SF79, M) == 9);
        CHECK(P::select_data_sf_for_snr(-120, SF79, M) == 9);   // -120 >= -120 -> SF9 (boundary)
    }
    SUBCASE("below all margins -> most-robust available") {
        CHECK(P::select_data_sf_for_snr(-130, SF79, M) == 9);   // descending fallback -> highest present
        CHECK(P::select_data_sf_for_snr(-300, SF79, M) == 9);
    }
    SUBCASE("singleton bitmap -> that SF regardless of SNR") {
        CHECK(P::select_data_sf_for_snr(0,    (1u << 12), M) == 12);
        CHECK(P::select_data_sf_for_snr(-300, (1u << 12), M) == 12);
    }
    SUBCASE("full set {7..12} picks by SNR") {
        constexpr uint16_t FULL = (1u<<7)|(1u<<8)|(1u<<9)|(1u<<10)|(1u<<11)|(1u<<12);
        CHECK(P::select_data_sf_for_snr(-90,   FULL, M) == 9);  // SF7(-40),SF8(-80) fail; SF9(-120) ok
        CHECK(P::select_data_sf_for_snr(0,     FULL, M) == 7);  // fastest
        CHECK(P::select_data_sf_for_snr(-1000, FULL, M) == 12); // most robust
    }
    SUBCASE("empty bitmap -> 0 sentinel") {
        CHECK(P::select_data_sf_for_snr(0, 0, M) == 0);
    }
}

TEST_CASE("Peer-liveness penalties match the Lua PROTOCOL values") {
    CHECK(P::peer_suspect_penalty_q4 == 192);   // 12.0 dB
    CHECK(P::peer_silent_penalty_q4  == 640);   // 40.0 dB
    CHECK(P::peer_dead_penalty_q4    == 1280);  // 80.0 dB
}

TEST_CASE("Bounded-state caps match the Lua PROTOCOL values") {
    CHECK(P::cap_seen_origins              == 256);
    CHECK(P::cap_q_queried                 == 128);
    CHECK(P::cap_q_responded_to            == 128);
    CHECK(P::cap_deferred_sends            == 32);
    CHECK(P::cap_gateway_deferred_handoffs == 32);
    CHECK(P::cap_id_bind                   == 256);
}

TEST_CASE("Frame overhead math — C++ DATA header drops the Lua visited[6] (deliberate divergence)") {
    // C++ DATA_HDR_LEN = 8 (no visited; loop/dedup via _seen_origins + TTL) -> hard cap 255 - 8 - 6 = 241.
    // The frozen Lua keeps visited -> DATA_HDR_LEN 14 -> 235 (dv_dual_sf.lua:2904-2905). Divergence on purpose.
    CHECK(P::data_hdr_len              == 8);
    CHECK(P::data_inner_overhead       == 6);
    CHECK(P::lora_max_frame_bytes      == 255);
    CHECK(P::max_payload_bytes_hard_cap == 241);  // 255 - 8 - 6
}

TEST_CASE("Hop-budget constants match the Lua PROTOCOL values") {
    // Lua code dv_dual_sf.lua:1072-1073 — hops_remaining is a 5-bit DATA field, max 31.
    // (A stale inline comment at :7338 says "(15)"; the table value at :1073 is authoritative.)
    CHECK(P::hop_budget_slack       == 3);
    CHECK(P::hop_budget_max_initial == 31);
}

TEST_CASE("Compile-time RF plan flags match the project_band_choice memory") {
    // These come from platformio.ini common.build_flags. If they ever
    // drift from project-band-choice's locked plan, this test fails.
    // Units match CustomSX1262::std_init: LORA_FREQ in MHz, LORA_BW in kHz.
    CHECK(LORA_FREQ           == 869.4625);    // MHz (std_init bring-up + banners)
    CHECK(LORA_BW             == 125.0);       // kHz
    CHECK(LORA_TX_POWER       == 22);          // dBm (cap to g3 ERP limit)
    CHECK(LORA_SF             == 8);
    CHECK(LORA_CR             == 5);
    CHECK(LORA_DUTY_CYCLE_PCT == 10);
    CHECK(LORA_PREAMBLE_SYM   == 16);
}

TEST_CASE("retry_backoff_window: capped per-attempt doubling (spec 2026-06-26-rts-retry-backoff)") {
    const uint32_t J = 600;   // an arbitrary base window (retry_jitter_ms = 3xRTS-airtime in the field)
    // max_shift = 3 (the shipped default): 1x, 2x, 4x, 8x, then HOLD at 8x (capped).
    CHECK(P::retry_backoff_window(J, 0, 3) == J);        // attempt 0 -> 1x
    CHECK(P::retry_backoff_window(J, 1, 3) == 2 * J);    // 1 -> 2x
    CHECK(P::retry_backoff_window(J, 2, 3) == 4 * J);    // 2 -> 4x
    CHECK(P::retry_backoff_window(J, 3, 3) == 8 * J);    // 3 -> 8x (== 1<<max_shift)
    CHECK(P::retry_backoff_window(J, 4, 3) == 8 * J);    // >= max_shift -> capped at 8x
    CHECK(P::retry_backoff_window(J, 99, 3) == 8 * J);   // far past the cap -> still 8x (no overflow/wrap)
    // max_shift = 0 -> FLAT: always the base (the Lua-faithful "before" leg; the A/B flip 0<->3).
    CHECK(P::retry_backoff_window(J, 0, 0) == J);
    CHECK(P::retry_backoff_window(J, 1, 0) == J);
    CHECK(P::retry_backoff_window(J, 50, 0) == J);
    // SHIPPED AT 0 (flat) — the 24-seed twin A/B refuted BEB (delivery falls monotonically with the shift), so the
    // divergence stays const-gated OFF (== the Lua-faithful flat retry). Flip to 3 to re-enable for a metal experiment.
    CHECK(P::retry_backoff_max_shift == 0);
}

TEST_CASE("overheard-reserve-yield: shipped OFF after the twin A/B refuted it (spec 2026-06-28)") {
    // Part A (yield ON) measured 45.5% DM delivery vs 47.1% flat on the saturating twin — yielding extends a flight's
    // lifetime and lowers throughput (same fast-fail-wins lesson as BEB). Both halves ship gated OFF (== today's
    // behaviour); the machinery stays for a metal re-test. Part B untested (twin carries no floods).
    CHECK(P::reserve_yield_enable == 0);
    CHECK(P::flood_yield_grab_enable == 0);
    CHECK(P::reserve_est_payload_bytes == P::max_payload_bytes_hard_cap / 2);   // ½-max D estimate (LBT backstops under-estimate)
}

TEST_CASE("Anti-spam v2 duty-channel-cap constants (Slice 0 — inert)") {
    CHECK(P::channel_min_interval_ms == 10000);   // 10 s per-origin channel spacing
    CHECK(P::dm_min_interval_ms      == 3000);     // 3 s self DM spacing
    // MF7: the ledger array bound that REPLACES channel_origin_max_per_window. Same value (20) for now -> inert.
    CHECK(P::cap_channel_origin_events == 20);
    CHECK(P::cap_channel_origin_events == P::channel_origin_max_per_window);
}

TEST_CASE("antispam v2 — channel_cap_origin support constants (Slice 1)") {
    CHECK(P::cap_channel_origin_legacy == 20);   // MF2: duty-disabled fallback (was channel_origin_max_per_window's 20)
    CHECK(P::channel_flood_sample_len == 39);     // MF3: DATA-M sample = M_FRAME_HDR_LEN(7) + 32-B body
}
