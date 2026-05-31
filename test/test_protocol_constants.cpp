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

TEST_CASE("Frame overhead math matches the Lua DATA constants") {
    // Lua code: DATA_HDR_LEN = 8 + VISITED_LEN(6) = 14 (dv_dual_sf.lua:2905);
    // DATA_INNER_OVERHEAD = 6 (:2908); hard cap = 255 - 14 - 6 = 235 (:8637).
    CHECK(P::data_hdr_len              == 14);
    CHECK(P::data_inner_overhead       == 6);
    CHECK(P::lora_max_frame_bytes      == 255);
    CHECK(P::max_payload_bytes_hard_cap == 235);  // 255 - 14 - 6
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
