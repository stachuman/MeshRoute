// MeshRoute — test_airtime.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
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
#include "frame_codec.h"   // §HYBRID-RTS-S5: unicast_rts_wire_len / terminal_cts_wire_len — the pricing helpers

using meshroute::airtime_ms;
using meshroute::unicast_rts_wire_len;
using meshroute::terminal_cts_wire_len;

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

TEST_CASE("airtime_ms — RTS_LEN=8 pins TODAY's retry-jitter range (pre-B158 baseline, expected to be REPLACED)") {
    // ★★★ SCOPE OF THIS CASE, CORRECTED 2026-08-10 — READ BEFORE TREATING IT AS A REQUIREMENT.
    //   These three numbers are a **TEMPORARY BASELINE**: they pin the retry-jitter range as it stands TODAY so an
    //   accidental change fails loudly. ⛔ They are **NOT** a statement that the value is correct or final.
    //   The owner ruled 2026-08-10 (reported form; owner-rulings-ledger §1.13) that **Lua parity is NOT a final
    //   MeshRoute jitter requirement** and that **[[B158]] stays OPEN until MeshRoute-native jitter has been
    //   independently measured and selected**. ⇒ ★ **[[B158]] IS EXPECTED TO REPLACE THESE ASSERTIONS**, and when it
    //   does, changing them is the intended outcome — NOT a regression. Keeping them until then is deliberate:
    //   an unpinned jitter range would let the B158 arc's own arms move without anyone noticing.
    // ⛔⛔ THE PREVIOUS FRAMING IS WITHDRAWN. It called this "a cross-engine determinism contract" that "MUST equal
    //   the Lua's", and said the Lua-parity ruling "is unchanged" and the under-price is "deliberately NOT
    //   corrected". Parity is now a CONSEQUENCE TO MEASURE, not a constraint to satisfy.
    // ⓘ What the numbers still are, factually: 3 * airtime_ms(routing_sf, bw, cr, 16, RTS_LEN=8), the Lua's
    //   dv_dual_sf.lua:8626 timing constant (RTS_LEN=8 is the LUA wire length — the C++ unicast RTS has been
    //   10 B plaintext / 11 B crypted since §hybrid-rts S1, and 7 B is RETIRED and REJECTED by `parse_rts`).
    //   Golden values regenerated from the Lua reference (see the helper at the bottom).
    // ⚠ `Node::retry_jitter_ms`'s own block carries the per-PHY under-price measurement AND the four coupled
    //   policies (DM origination · same-hop retry · BUSY_RX release · the LBT backoff at retry_jitter_ms()/2).
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

// ============================================================================
// §HYBRID-RTS-S5 / [[B158]] — THE RTS/CTS TIMING GRID.
//
// The audit these cases close is `docs/superpowers/plans/2026-08-10-hybrid-rts-s5-timing-audit.md`; the full
// 128-cell ledger and every semantic owner live in `simulation/BASELINE.md` §HYBRID-RTS-S5. What lives HERE is
// the part a gate can hold: the named golden cells, and the two grid-scope INVARIANTS.
//
// ⛔ THE GRID IS THE POINT, NOT A SPOT CHECK. §HYBRID-RTS-S1 proved a spot check can be vacuous: at the corpus's
//    dominant SF8/BW125k/CR5 the plaintext CTS-wait correction is +0 ms, so a case written there would have
//    PASSED against the retired `a(8)+a(4)` formula and proven nothing. The sign is NOT monotone in SF —
//    RTS 7->10 is +0 ms at SF8/BW62.5k and +164 ms at SF11/BW62.5k — so only a full sweep can assert a bound.
// ⓘ SUPPORTED GRID = SF 5..12 x BW {62.5k, 125k, 250k, 500k} x CR 5..8 = 128 cells.
// ⓘ U1: the Node-level arming of `start_rts_timeout` (the frame that really flies, at two discriminating PHYs)
//    is already asserted in `test_node_r3.cpp` §hybrid-rts S1 item 8. These cases do NOT duplicate it; they
//    bound the same expression over the whole grid instead of at two cells.
// ============================================================================

namespace {
constexpr uint32_t kS5Bws[4] = { 62500, 125000, 250000, 500000 };
}

TEST_CASE("§HYBRID-RTS-S5 — the CTS 3/4/6/7 + RTS 7/9/10/11/43 ledger at the named PHY cells") {
    namespace P = meshroute::protocol;
    auto a = [](uint8_t sf, uint32_t bw, uint8_t cr, uint16_t len) {
        return airtime_ms(sf, bw, cr, P::preamble_sym, len);
    };
    // ⓘ RTS 7 B is RETIRED from the wire (§hybrid-rts S1 rejects it outright). It is pinned here as the
    //   HISTORICAL column so the ledger shows what changed, NOT because any producer can emit it.
    //                                            CTS                RTS
    //                                       3    4    6    7     7    9   10   11    43
    // s06_seattle_lifecycle routing PHY — the brief's mandatory BW 62.5 kHz row
    CHECK(a( 8, 62500,5,3)==156); CHECK(a( 8, 62500,5,4)==156); CHECK(a( 8, 62500,5,6)==156); CHECK(a( 8, 62500,5,7)==177);
    CHECK(a( 8, 62500,5,9)==177); CHECK(a( 8, 62500,5,10)==177); CHECK(a( 8, 62500,5,11)==197); CHECK(a( 8, 62500,5,43)==361);
    // s18_meshroute routing PHY (the corpus's dominant cell — and the one where plaintext growth is FREE)
    CHECK(a( 8,125000,5,3)== 78); CHECK(a( 8,125000,5,4)== 78); CHECK(a( 8,125000,5,6)== 78); CHECK(a( 8,125000,5,7)== 88);
    CHECK(a( 8,125000,5,9)== 88); CHECK(a( 8,125000,5,10)== 88); CHECK(a( 8,125000,5,11)== 98); CHECK(a( 8,125000,5,43)==180);
    // s16_dense_gateway routing PHY — ★ and the one cell where a(4) != a(3) inside the corpus (272 vs 313)
    CHECK(a( 9, 62500,5,3)==272); CHECK(a( 9, 62500,5,4)==313); CHECK(a( 9, 62500,5,6)==313); CHECK(a( 9, 62500,5,7)==313);
    CHECK(a( 9, 62500,5,9)==354); CHECK(a( 9, 62500,5,10)==354); CHECK(a( 9, 62500,5,11)==354); CHECK(a( 9, 62500,5,43)==641);
    // SF11/BW62.5k/CR5 — the plaintext-discriminating cell test_node_r3.cpp's S1 case is pinned at
    CHECK(a(11, 62500,5,3)==1089); CHECK(a(11, 62500,5,4)==1089); CHECK(a(11, 62500,5,6)==1253); CHECK(a(11, 62500,5,7)==1253);
    CHECK(a(11, 62500,5,9)==1253); CHECK(a(11, 62500,5,10)==1417); CHECK(a(11, 62500,5,11)==1417); CHECK(a(11, 62500,5,43)==2564);
    // ⛔ THE WORST CASE THE BRIEF NAMES: SF12 / BW 62.5 kHz / CR 4/8
    CHECK(a(12, 62500,8,3)==2375); CHECK(a(12, 62500,8,4)==2375); CHECK(a(12, 62500,8,6)==2899); CHECK(a(12, 62500,8,7)==2899);
    CHECK(a(12, 62500,8,9)==2899); CHECK(a(12, 62500,8,10)==2899); CHECK(a(12, 62500,8,11)==3424); CHECK(a(12, 62500,8,43)==6569);
    // SF12 at the WIDEST bandwidth — same SF, and the growth lands in different buckets (never quote one figure)
    CHECK(a(12,500000,5,3)==272); CHECK(a(12,500000,5,4)==272); CHECK(a(12,500000,5,6)==272); CHECK(a(12,500000,5,7)==313);
    CHECK(a(12,500000,5,10)==313); CHECK(a(12,500000,5,11)==313); CHECK(a(12,500000,5,43)==559);
    // the SX126x §6.1.4 low-SF framing corner (deliberate special case — do not "simplify" it away)
    CHECK(a( 5,125000,5,3)== 10); CHECK(a( 5,125000,5,4)== 11); CHECK(a( 5,125000,5,6)== 12); CHECK(a( 5,125000,5,7)== 12);
    CHECK(a( 5,125000,5,10)== 14); CHECK(a( 5,125000,5,11)== 15); CHECK(a( 5,125000,5,43)== 30);
    // the retired 7-B RTS priced identically to the live 9-B M-broadcast at these two cells — the historical column
    CHECK(a( 8, 62500,5,7) == a( 8, 62500,5,9));
    CHECK(a( 8,125000,5,7) == a( 8,125000,5,9));
}

TEST_CASE("§HYBRID-RTS-S5 / [[B158]] — start_rts_timeout's margin is NON-NEGATIVE at ALL 128 supported PHY cells") {
    namespace P = meshroute::protocol;
    // ★★ WHAT MAKES THIS FALSIFIABLE RATHER THAN TAUTOLOGICAL, because the obvious form is not:
    //    the ARMED side is computed through the PRODUCTION helpers the site uses
    //    (`unicast_rts_wire_len` / `terminal_cts_wire_len`), and the ACTUAL side through INDEPENDENT literals
    //    for the lengths that really fly. Mutate either helper and the sweep goes negative.
    //    The literals themselves are tied to the codec by `test_frame_codec.cpp`'s pack/parse length matrix and
    //    by `test_node_r3.cpp`'s two S1 cases, which assert the emitted RTS is 10 B / 11 B on the wire (U1).
    constexpr uint16_t kRealUnicastRts[2] = { 10, 11 };   // [plaintext, crypted] — the frame `pack_rts` emits
    constexpr uint16_t kRealTerminalCts[2] = { 6,  7 };   // [plaintext, crypted] — the frame `pack_cts` emits
    CHECK(unicast_rts_wire_len(false)  == kRealUnicastRts[0]);
    CHECK(unicast_rts_wire_len(true)   == kRealUnicastRts[1]);
    CHECK(terminal_cts_wire_len(false) == kRealTerminalCts[0]);
    CHECK(terminal_cts_wire_len(true)  == kRealTerminalCts[1]);

    int cells = 0, min_margin = 1 << 30, discriminating_pt = 0, discriminating_ct = 0;
    bool all_non_negative = true;
    for (uint8_t sf = 5; sf <= 12; ++sf)
      for (uint32_t bw : kS5Bws)
        for (uint8_t cr = 5; cr <= 8; ++cr) {
            ++cells;
            for (int crypted = 0; crypted <= 1; ++crypted) {
                const bool c = crypted != 0;
                // the site: base = a(unicast_rts_wire_len(c)) + a(terminal_cts_wire_len(c)); delay = (base<<shift) + 2*slop + 1
                // shift 0 and slop 0 (the sim/native HAL) is the WORST case — a metal slop only adds margin.
                const uint32_t armed = airtime_ms(sf, bw, cr, P::preamble_sym, static_cast<uint16_t>(unicast_rts_wire_len(c)))
                                     + airtime_ms(sf, bw, cr, P::preamble_sym, static_cast<uint16_t>(terminal_cts_wire_len(c)))
                                     + 1u;
                // the wire: the request that flies plus the LONGEST legal response to it. An ordinary 3/4-B CTS
                // and every NACK shape are SHORTER than the terminal CTS, so the terminal shape IS the bound
                // (airtime_ms is monotonic non-decreasing in length — asserted in the next case).
                const uint32_t actual = airtime_ms(sf, bw, cr, P::preamble_sym, kRealUnicastRts[crypted])
                                      + airtime_ms(sf, bw, cr, P::preamble_sym, kRealTerminalCts[crypted]);
                const int margin = static_cast<int>(armed) - static_cast<int>(actual);
                if (margin < 0) all_non_negative = false;
                if (margin < min_margin) min_margin = margin;
                // the no-pending-flight re-arm prices the CRYPTED worst case against a possibly PLAINTEXT flight
                // -> it can only OVER-wait, which fails safe. Asserted, not assumed.
                const uint32_t fallback = airtime_ms(sf, bw, cr, P::preamble_sym, kRealUnicastRts[1])
                                        + airtime_ms(sf, bw, cr, P::preamble_sym, kRealTerminalCts[1]) + 1u;
                CHECK(fallback >= actual);
            }
            // ⛔ AND THE RETIRED FORMULA IS SEPARATED AT GRID SCOPE, not at one lucky cell.
            const uint32_t retired = airtime_ms(sf, bw, cr, P::preamble_sym, 8) + airtime_ms(sf, bw, cr, P::preamble_sym, 4);
            const uint32_t now_pt  = airtime_ms(sf, bw, cr, P::preamble_sym, 10) + airtime_ms(sf, bw, cr, P::preamble_sym, 6);
            const uint32_t now_ct  = airtime_ms(sf, bw, cr, P::preamble_sym, 11) + airtime_ms(sf, bw, cr, P::preamble_sym, 7);
            CHECK(now_pt >= retired);                    // the correction NEVER shortens the wait
            CHECK(now_ct >= now_pt);                     // crypted is never cheaper than plaintext
            if (now_pt != retired) ++discriminating_pt;
            if (now_ct != retired) ++discriminating_ct;
        }
    CHECK(cells == 128);
    CHECK(all_non_negative);
    CHECK(min_margin == 1);                  // ★ the minimum is the site's own `+ 1u` literal, at EVERY cell
    CHECK(discriminating_pt == 102);          // measured: 102 of 128 cells separate a(10)+a(6) from a(8)+a(4)
    CHECK(discriminating_ct == 124);          // measured: 124 of 128 separate the crypted price (max +1049 ms)
}

TEST_CASE("§HYBRID-RTS-S5 — airtime_ms is monotonic in length over the whole grid (the 'safe direction' premise)") {
    namespace P = meshroute::protocol;
    // ★ EVERY "the error is only ever the safe direction" claim in the MAC timing audit rests on this and on
    //   nothing else — above all `start_pending_rx_expiry`'s `airtime_routing_ms(4)`, which prices OUR OWN
    //   outgoing CTS whose real length is 3 B (no NAV hint) or 4 B (with one). If airtime were ever DECREASING
    //   in length, that site would under-wait. Proven over the grid instead of argued.
    int violations = 0, cts_4_vs_3_differ = 0, cts_6_vs_4_differ = 0;
    int worst_terminal_deficit = 0;
    for (uint8_t sf = 5; sf <= 12; ++sf)
      for (uint32_t bw : kS5Bws)
        for (uint8_t cr = 5; cr <= 8; ++cr) {
            for (int len = 1; len <= 255; ++len)
                if (airtime_ms(sf, bw, cr, P::preamble_sym, static_cast<uint16_t>(len))
                    < airtime_ms(sf, bw, cr, P::preamble_sym, static_cast<uint16_t>(len - 1))) ++violations;
            const uint32_t a3 = airtime_ms(sf, bw, cr, P::preamble_sym, 3);
            const uint32_t a4 = airtime_ms(sf, bw, cr, P::preamble_sym, 4);
            const uint32_t a6 = airtime_ms(sf, bw, cr, P::preamble_sym, 6);
            CHECK(a4 >= a3);                                        // the CTS_LEN=4 price covers a 3-B CTS everywhere
            if (a4 != a3) ++cts_4_vs_3_differ;
            if (a6 != a4) ++cts_6_vs_4_differ;
            const int deficit = static_cast<int>(a4) - static_cast<int>(a6);
            if (deficit < worst_terminal_deficit) worst_terminal_deficit = deficit;
        }
    CHECK(violations == 0);
    CHECK(cts_4_vs_3_differ == 34);        // measured: a 4th CTS byte is FREE at 94 of 128 cells and costs at 34
    CHECK(cts_6_vs_4_differ == 83);
    // ⛔ WHY `start_pending_rx_expiry`'s SAFETY IS STRUCTURAL AND NOT NUMERIC: were a 6/7-B TERMINAL CTS ever
    //    able to reach that site, `airtime_routing_ms(4)` would UNDER-price it by up to this much. It cannot —
    //    `handle_rts`'s terminal branch RETURNS before any PendingRx is allocated, and both callers of
    //    `start_pending_rx_expiry` build `cin.already_received = false` into a `uint8_t cbuf[4]`. The number is
    //    pinned here so the structural argument has a stake: if the call graph ever changes, this is the cost.
    CHECK(worst_terminal_deficit == -524);   // SF12 / BW 62.5 kHz / CR 4/8
}

TEST_CASE("§HYBRID-RTS-S5 — the ACK is 3 B and the routing-frame length set is {3,4,6,7,9,10,11,43}") {
    namespace P = meshroute::protocol;
    using namespace meshroute;
    // ⓘ Items 4 and 5 of the audit (`node_mac_rx.cpp`'s post-ACK timer, `start_ack_timeout` and
    //   `nav_duration_cts`) all price the ACK as `airtime_routing_ms(3)`. That is EXACT, and the premise is the
    //   codec's, so assert it AT the codec rather than trusting the comments.
    uint8_t buf[8] = {};
    ack_in ai{}; ai.ctr_lo = 5; ai.budget_hint = 1; ai.snr_bucket = 2; ai.to = 20;
    CHECK(pack_ack(ai, std::span<uint8_t>(buf, sizeof buf)) == 3);
    CHECK(pack_ack(ai, std::span<uint8_t>(buf, 3)) == 3);              // ★ EXACTLY 3 B suffices — a raised floor is caught
    CHECK(pack_ack(ai, std::span<uint8_t>(buf, 2)) == 0);              // and 3 B is a HARD floor, not a hint
    // the two semantic pricing helpers, and the fact that the four DM lengths are all distinct from the
    // M_BROADCAST (9) and FLOOD (43) lengths the timing sites must keep separate (design §6).
    CHECK(unicast_rts_wire_len(false) == 10); CHECK(unicast_rts_wire_len(true) == 11);
    CHECK(terminal_cts_wire_len(false) == 6); CHECK(terminal_cts_wire_len(true) == 7);
    CHECK(unicast_rts_wire_len(false) != 9);  CHECK(unicast_rts_wire_len(true) != 43);
    // the flood RTS is the LONGEST routing frame at every PHY — the fact [[B165]]'s 16-B slot contradicts
    for (uint8_t sf = 5; sf <= 12; ++sf)
      for (uint32_t bw : kS5Bws)
        for (uint8_t cr = 5; cr <= 8; ++cr)
            CHECK(airtime_ms(sf, bw, cr, P::preamble_sym, 43)
                  >= airtime_ms(sf, bw, cr, P::preamble_sym, 11));
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
