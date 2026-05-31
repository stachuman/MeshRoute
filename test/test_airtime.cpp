// MeshRoute — test_airtime.cpp
//
// Differential test: every (sf, bw, cr, len) tuple here was captured
// from the Lua reference (spec/dv_dual_sf.lua::airtime_ms) and pins
// the C++ port to bit-identical output. If you change airtime.cpp,
// regenerate these from the Lua side with:
//
//   cd /home/staszek/lora-universal-simulator
//   lua5.4 -e 'function f(sf,b,c,p,L) ...end print(f(7,125000,5,16,64))'
//
// (The full helper is at the bottom of this file as a Lua block in
// comments — copy-paste into a lua5.4 prompt to regenerate.)

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "airtime.h"
#include "protocol_constants.h"

using meshroute::airtime_ms;

TEST_CASE("airtime_ms — SF7/BW125/CR4/5 baselines (Lua reference)") {
    // From Lua: airtime_ms(7, 125000, 5, 16, 0)  = 34
    //          airtime_ms(7, 125000, 5, 16, 64) = 126
    CHECK(airtime_ms(7, 125000, 5, 16,  0) ==  34);
    CHECK(airtime_ms(7, 125000, 5, 16, 64) == 126);
}

TEST_CASE("airtime_ms — SF12/BW125/CR4/5 slow leg (low-data-rate-optimize active)") {
    // SF12 + BW125: t_sym = 4096/125 = 32.768 ms >= 16 → de=1
    // From Lua: airtime_ms(12, 125000, 5, 16, 64) = 3055
    CHECK(airtime_ms(12, 125000, 5, 16, 64) == 3055);
}

TEST_CASE("airtime_ms — SF5/SF6 low-SF framing (SX126x §6.1.4: 6.25 sync, +36)") {
    // SF5/SF6 use the SX126x datasheet §6.1.4 special-case (added 2026-05-29 to
    // BOTH the Lua and this port). Values match RadioLib SX126x::calculateTimeOnAir:
    //   airtime_ms(5, 125000, 5, 16,  0) =  9   (RadioLib  9024 us)
    //   airtime_ms(5, 125000, 5, 16, 16) = 17   (RadioLib 17984 us)
    //   airtime_ms(6, 125000, 5, 16, 16) = 30   (RadioLib 30848 us)
    //   airtime_ms(6, 125000, 5, 16, 50) = 61   (RadioLib 61568 us; was 60 pre-fix)
    CHECK(airtime_ms(5, 125000, 5, 16,  0) ==  9);
    CHECK(airtime_ms(5, 125000, 5, 16, 16) == 17);
    CHECK(airtime_ms(6, 125000, 5, 16, 16) == 30);
    CHECK(airtime_ms(6, 125000, 5, 16, 50) == 61);
}

TEST_CASE("airtime_ms — SF8/BW125 default RF plan, full beacon") {
    namespace P = meshroute::protocol;
    // Maximum-size beacon at our default RF plan.
    // From Lua: airtime_ms(8, 125000, 5, 16, 151) = 457
    CHECK(airtime_ms(LORA_SF, (uint32_t)(LORA_BW * 1000), LORA_CR,
                      P::preamble_sym, P::beacon_max_bytes) == 457);
}

TEST_CASE("airtime_ms — wider BW = shorter airtime") {
    // From Lua:
    //   airtime_ms(8, 125000, 5, 16, 100) = 324
    //   airtime_ms(8, 250000, 5, 16, 100) = 162
    //   airtime_ms(8, 500000, 5, 16, 100) =  81
    CHECK(airtime_ms(8, 125000, 5, 16, 100) == 324);
    CHECK(airtime_ms(8, 250000, 5, 16, 100) == 162);
    CHECK(airtime_ms(8, 500000, 5, 16, 100) ==  81);
}

TEST_CASE("airtime_ms — RTS_LEN=8 timing constant pins the retry-jitter range (R3.x)") {
    // The retry-jitter RANGE = 3 * airtime_ms(routing_sf, bw, cr, 16, RTS_LEN=8)
    // is a cross-engine determinism contract: it MUST equal the Lua's so the
    // lua-vs-meshroute forced-retry mt19937 streams stay aligned (node.cpp
    // retry_jitter_ms / the dv_dual_sf.lua:8626 timing constant — RTS_LEN=8 is
    // the LUA wire length, deliberately NOT the 7-byte C++ RTS wire). Golden
    // values regenerated from the Lua reference (see the helper at the bottom).
    // If a future "8 -> 7" wire change touches this, these fail loudly here.
    CHECK(airtime_ms(7, 125000, 5, 16, 8) ==  44);   // jitter range [0,132]
    CHECK(airtime_ms(8, 125000, 5, 16, 8) ==  88);   // jitter range [0,264] (SF8 default)
    CHECK(airtime_ms(9, 125000, 5, 16, 8) == 156);   // jitter range [0,468]
}

TEST_CASE("airtime_ms — duty-cycle math from project_band_choice") {
    // PROTOCOL.duty 10% at 1-hour window = 360 000 ms TX budget.
    // A 457 ms BCN at our default plan can be sent at most ~787 times/hour
    // before hitting EXHAUSTED. Sanity check on the airtime arithmetic.
    constexpr uint32_t window_ms = 3600 * 1000;
    constexpr uint32_t budget_ms = window_ms / 10;   // 10% = 360 000 ms
    const uint32_t bcn_ms = airtime_ms(LORA_SF, (uint32_t)(LORA_BW * 1000), LORA_CR,
                                       meshroute::protocol::preamble_sym, 151);
    const uint32_t max_bcns_per_hour = budget_ms / bcn_ms;
    CHECK(max_bcns_per_hour >= 700);   // sanity floor
}

// ----------------------------------------------------------------------------
// Lua reference regeneration helper. Paste into a lua5.4 prompt:
//
//   function airtime_ms(sf, bw_hz, cr, preamble_sym, len_bytes)
//     local t_sym  = (2 ^ sf) / (bw_hz / 1000)
//     local low_sf = (sf == 5 or sf == 6)               -- SX126x §6.1.4
//     local t_pre  = (preamble_sym + (low_sf and 6.25 or 4.25)) * t_sym
//     local de     = (t_sym >= 16) and 1 or 0
//     local num    = 8 * len_bytes - 4 * sf + (low_sf and 36 or 44)
//     local den    = 4 * (sf - 2 * de)
//     local pay_sym = 8 + math.max(math.ceil(num / den) * cr, 0)
//     return math.floor(t_pre + pay_sym * t_sym)
//   end
//   print(airtime_ms(8, 125000, 5, 16, 151))   -- → 457
//   print(airtime_ms(6, 125000, 5, 16,  50))   -- → 61  (was 60 pre-fix)
// ----------------------------------------------------------------------------
