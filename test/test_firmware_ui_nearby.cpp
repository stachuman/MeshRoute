// MeshRoute — test_firmware_ui_nearby.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN (test_airtime.cpp provides main()); -fno-exceptions => CHECK, never
//     REQUIRE — doctest implements REQUIRE's abort with a throw, so it does not compile in this build.
//
// §UI-16 N2 — the native suite for the NEARBY scan screen's pure unit (`src/firmware_ui_nearby.h`): the OWN-TEAM
// FILTER, the FIRST-OBSERVED order, the row's three tokens (the SHARED fingerprint · the `n/3` tier · the reused
// age) and every lexeme the screen can print.
//
// ★★★ THE EXPECTATIONS ARE INDEPENDENT OF THE IMPLEMENTATION, which is what lets them redden it:
//   · the SIGNAL cases are driven at NAMED dB VALUES (`db_to_q4(-20.0f)` …), ⛔ never by asking
//     `presence_quality_tier` what it thinks — a case that called the function under test could not fail when the
//     mapping is re-derived from different thresholds, which is exactly the mutation ruling R-4 exists to forbid;
//   · the FINGERPRINT cases assert the EXACT SIX BYTES *and* their value relation to the shared helper, so a local
//     re-spelling of `%06lX` that agrees today and drifts tomorrow is caught by the first half;
//   · the ORDER cases give the rows DECREASING signal in FIRST-OBSERVED order, so any sort at all — by signal, by
//     age, by id — moves them and reddens (owner ruling R-5).
//
// ⛔ WHAT IS NOT MEASURED HERE, and said out loud rather than silently skipped: the RENDERER
//   (`src/firmware_ui.cpp`'s `Provision::nearby` arm) is compiled by neither the native suite nor the simulator
//   (§B115). Its cover is `tools/probe_firmware_ui`'s NEARBY phase, which drives DISTINCTIVE observations through
//   the REAL node and asserts every drawn row's exact bytes at its exact coordinate.
#include "doctest.h"
#include "firmware_ui_nearby_row.h"   // ★ pulls `firmware_ui_nearby.h` with it — the two halves of one screen
#include <cstdint>
#include <cstring>

using mrui::NearbyList;
using mrui::NearbyRow;
using mrui::NearbySelList;
using mrui::NearbySelRow;
using mrui::kMaxNearbyRows;
using mrui::nearby_capture;
using mrui::nearby_note;
using mrui::nearby_sel_rows;
using mrui::ui_fmt_nearby_row;
using mrui::ui_fmt_nearby_signal;
namespace proto = MESHROUTE_NS::protocol;

namespace {

// One projected observation, as `build_snapshot` publishes it.
NearbyRow row(uint32_t team_id, float snr_db, uint64_t age_ms, bool age_valid = true) {
    NearbyRow r{};
    r.team_id   = team_id;
    r.snr_q4    = proto::db_to_q4(snr_db);
    r.age_ms    = age_ms;
    r.age_valid = age_valid;
    return r;
}

// The signal token for one SNR, as the panel would draw it.
const char* sig_of(float snr_db, char (&buf)[mrui::kNearbySignalCap]) {
    ui_fmt_nearby_signal(buf, sizeof buf, proto::db_to_q4(snr_db));
    return buf;
}

}  // namespace

// ================================================================================= the OWN-TEAM FILTER (R-5 / pin 3)
TEST_CASE("ui16-nearby: OUR OWN team is filtered out AT DISPLAY, and every other observation survives") {
    // ★ THE SPLIT OF AUTHORITY IS THE POINT (spec §4-N1 pin 9 / §4-N2 pin 3): the core records our own team like any
    //   other — *"which teams are audible"* is one question — and the READER decides *"which of them are worth
    //   offering"*. A filter at the write site would give the second question two authorities.
    const NearbyRow src[] = { row(0xAAAA0001u, 0.0f, 1000), row(0xC0FFEE01u, 0.0f, 2000),
                              row(0xAAAA0002u, 0.0f, 3000) };
    const NearbyList l = nearby_capture(src, 3, /*own_team_id=*/0xC0FFEE01u);
    CHECK(l.n == 2);
    CHECK(l.row[0].team_id == 0xAAAA0001u);
    CHECK(l.row[1].team_id == 0xAAAA0002u);   // ★ the survivors keep their RELATIVE order across the removal
    // ⛔ AND THE FILTER IS AN EQUALITY ON THE **FULL 32-BIT ID**, never on the fingerprint: two ids that share their
    //    low 24 bits fingerprint IDENTICALLY (`ui_fmt_team_fingerprint`'s mask IS its definition), and filtering on
    //    the display token would silently hide a DIFFERENT team the operator could legitimately join.
    const NearbyRow twin[] = { row(0x01C0FFEEu, 0.0f, 1000), row(0x02C0FFEEu, 0.0f, 2000) };
    const NearbyList t = nearby_capture(twin, 2, /*own_team_id=*/0x01C0FFEEu);
    CHECK(t.n == 1);
    CHECK(t.row[0].team_id == 0x02C0FFEEu);
}

TEST_CASE("ui16-nearby: a TEAMLESS joiner (own id 0) loses NOTHING — the normal case for this screen") {
    // The write gate in `lib/core/node_beacon.cpp` is `peer_team != 0`, so no recorded row can carry 0 and the
    // equality simply never matches. ⓘ This is the state §3.6.4 point 2's joiner is actually in.
    const NearbyRow src[] = { row(0xAAAA0001u, 0.0f, 1000), row(0xAAAA0002u, 0.0f, 2000) };
    const NearbyList l = nearby_capture(src, 2, /*own_team_id=*/0);
    CHECK(l.n == 2);
    CHECK(l.row[0].team_id == 0xAAAA0001u);
    CHECK(l.row[1].team_id == 0xAAAA0002u);
}

TEST_CASE("ui16-nearby: the capture FAILS CLOSED and is BOUNDED") {
    const NearbyList none = nearby_capture(nullptr, 4, 0);
    CHECK(none.n == 0);                                  // ⛔ no source is an EMPTY list, never a guess
    NearbyRow big[kMaxNearbyRows + 4];
    for (std::size_t i = 0; i < sizeof big / sizeof big[0]; ++i) big[i] = row(0x1000u + uint32_t(i), 0.0f, 1000);
    const NearbyList l = nearby_capture(big, uint8_t(sizeof big / sizeof big[0]), 0);
    CHECK(l.n == kMaxNearbyRows);                        // the publisher's bound, restated where the copy happens
    CHECK(l.row[0].team_id == 0x1000u);
    CHECK(l.row[kMaxNearbyRows - 1].team_id == 0x1000u + uint32_t(kMaxNearbyRows - 1));
    // ⓘ THE CAPACITY IS THE RING'S OWN, ⛔ never a second literal: a re-tuned cache re-sizes this list with it.
    CHECK(kMaxNearbyRows == proto::cap_team_seen);
}

// ============================================================================ FIRST-OBSERVED ORDER (R-5, pin 6/§4-N2)
TEST_CASE("ui16-nearby: the order is the SOURCE's — ⛔ never sorted, least of all by signal") {
    // ★ THE FIXTURE IS BUILT SO A SORT CANNOT HIDE: signal DECREASES down the list while age INCREASES, so a sort by
    //   either key produces a DIFFERENT order from the one the ring handed over.
    const NearbyRow src[] = { row(0x11111111u, -20.0f, 1000),    // weakest, freshest
                              row(0x22222222u,   0.0f, 2000),
                              row(0x33333333u,  10.0f, 3000) };  // strongest, stalest
    const NearbyList l = nearby_capture(src, 3, 0);
    CHECK(l.n == 3);
    CHECK(l.row[0].team_id == 0x11111111u);
    CHECK(l.row[1].team_id == 0x22222222u);
    CHECK(l.row[2].team_id == 0x33333333u);
    // ★★ AND THE SAME ORDER SURVIVES INTO THE ROWS THE SCREEN WALKS — a sort inserted at either step reddens.
    const NearbySelList sel = nearby_sel_rows(l);
    NearbySelRow r{};
    CHECK(sel.at(0, r)); CHECK(!r.back); CHECK(r.team.team_id == 0x11111111u);
    CHECK(sel.at(1, r)); CHECK(!r.back); CHECK(r.team.team_id == 0x22222222u);
    CHECK(sel.at(2, r)); CHECK(!r.back); CHECK(r.team.team_id == 0x33333333u);
}

// ======================================================================================= the rows, AS IDENTITIES
TEST_CASE("ui16-nearby: BACK is the UNCONDITIONAL last row, and the list FAILS CLOSED past its end") {
    NearbySelRow r{};
    {   // an EMPTY scan still opens a list the operator can leave (spec §4-N2 pin 4)
        const NearbySelList sel = nearby_sel_rows(NearbyList{});
        CHECK(sel.n == 1);
        CHECK(sel.at(0, r)); CHECK(r.back);
        CHECK(sel.at(1, r) == false);
        CHECK(sel.at(255, r) == false);
    }
    {   // a full ring: every retained team is a row, and BACK is still the last one (owner ruling R-5: ALL of them)
        NearbyRow src[kMaxNearbyRows];
        for (std::size_t i = 0; i < kMaxNearbyRows; ++i) src[i] = row(0x2000u + uint32_t(i), 0.0f, 1000);
        const NearbySelList sel = nearby_sel_rows(nearby_capture(src, kMaxNearbyRows, 0));
        CHECK(sel.n == kMaxNearbyRows + 1);
        CHECK(sel.at(kMaxNearbyRows, r)); CHECK(r.back);
        CHECK(sel.at(uint8_t(kMaxNearbyRows - 1), r)); CHECK(!r.back);
        CHECK(sel.at(uint8_t(kMaxNearbyRows + 1), r) == false);
    }
}

TEST_CASE("ui16-nearby: a row's MEANING is its team id, ⛔ never its index (§B66)") {
    // ★ THE FILTER MOVES EVERY LATER TEAM ONE ROW UP, which is exactly why a position may not stand for an identity:
    //   the row at index 0 here is the SECOND observation.
    const NearbyRow src[] = { row(0xC0FFEE01u, 0.0f, 1000), row(0xDEAD0002u, 0.0f, 2000),
                              row(0xDEAD0003u, 0.0f, 3000) };
    const NearbySelList sel = nearby_sel_rows(nearby_capture(src, 3, /*own_team_id=*/0xC0FFEE01u));
    NearbySelRow r{};
    CHECK(sel.at(0, r));
    CHECK(r.team.team_id == 0xDEAD0002u);   // ⛔ NOT the observation at source index 0
    CHECK(sel.at(1, r));
    CHECK(r.team.team_id == 0xDEAD0003u);
    // ⓘ The row carries the WHOLE record (U2) — the id N3's confirmation will act on is the id this row drew.
    CHECK(r.team.age_ms == 3000u);
}

// ============================================================================================ the SIGNAL token (R-4)
TEST_CASE("ui16-nearby: the signal token is FOUR levels `0/3`..`3/3` at the SHARED tier boundaries") {
    char b[mrui::kNearbySignalCap];
    // ★ DRIVEN AT NAMED dB VALUES, ⛔ never by asking `presence_quality_tier` what it thinks. The boundaries are the
    //   ONE canonical family's — −12 / −4 / +4 dB — and each is asserted INCLUSIVE (the tier a boundary belongs to).
    CHECK(strcmp(sig_of(-30.0f, b), "0/3") == 0);
    CHECK(strcmp(sig_of(-12.1f, b), "0/3") == 0);
    CHECK(strcmp(sig_of(-12.0f, b), "1/3") == 0);   // the weak floor is INCLUSIVE
    CHECK(strcmp(sig_of(-8.0f,  b), "1/3") == 0);
    CHECK(strcmp(sig_of(-4.0f,  b), "2/3") == 0);   // ...and so is the ok floor
    CHECK(strcmp(sig_of(0.0f,   b), "2/3") == 0);
    CHECK(strcmp(sig_of(4.0f,   b), "3/3") == 0);   // ...and the strong floor
    CHECK(strcmp(sig_of(12.0f,  b), "3/3") == 0);
    // ⚠ WIDTH IS A CONSTRAINT: three fixed ASCII columns, so the row's layout cannot shift with the link quality.
    CHECK(strlen(sig_of(-30.0f, b)) == 3);
    CHECK(strlen(sig_of(12.0f,  b)) == 3);
    // ⓘ THE DENOMINATOR IS THE TOP TIER'S OWN VALUE, so a fifth tier would move the token rather than leave `3/3`
    //   naming a scale that no longer exists.
    CHECK(mrui::kNearbyTierMax == uint8_t(proto::presence_q_strong));
}

// =============================================================================================== the ROW's bytes
TEST_CASE("ui16-nearby: the row is fingerprint · n/3 · age — and ⛔ NOTHING name-shaped (P-5 / R-13 rule 1)") {
    char out[mrui::kNearbyRowCap];
    ui_fmt_nearby_row(out, sizeof out, row(0x12A1B2C3u, 0.0f, 42000));
    CHECK(strcmp(out, "A1B2C3 2/3 42s") == 0);
    // ★★ THE FINGERPRINT IS **THE SHARED HELPER'S** ANSWER, asserted as a VALUE RELATION as well as as bytes: one
    //    definition of the six-hex token, called from both ends of a join (U1, spec §8's opening rule).
    char fp[mrui::kTeamFpTokenCap];
    mrui::ui_fmt_team_fingerprint(fp, sizeof fp, 0x12A1B2C3u);
    CHECK(strncmp(out, fp, 6) == 0);
    // ⛔ THE HIGH BYTE IS NOT IN THE TOKEN — the mask IS the definition, and two ids sharing their low 24 bits draw
    //    the same six characters. That is SAFE precisely because the row's identity is the full id above.
    char twin[mrui::kNearbyRowCap];
    ui_fmt_nearby_row(twin, sizeof twin, row(0xFFA1B2C3u, 0.0f, 42000));
    CHECK(strcmp(twin, "A1B2C3 2/3 42s") == 0);
    // ⚠ WIDTH: the widest row plus the renderer's own cursor marker must fit the rail's 19-column body.
    // ⚠ `ull` ON EVERY FACTOR, deliberately: 99 days is 8 553 600 000 ms, which OVERFLOWS a 32-bit product and
    //   would silently have driven this case at 49 days — the very wrap the `uint64_t` age exists to survive.
    ui_fmt_nearby_row(out, sizeof out, row(0xFFFFFFFFu, 12.0f, 99ull * 24ull * 3600ull * 1000ull));
    CHECK(strcmp(out, "FFFFFF 3/3 99d") == 0);
    CHECK(1u + strlen(out) <= 19u);
}

TEST_CASE("ui16-nearby: the AGE is `ui_fmt_home_age`'s token, REUSED — including its `--`") {
    char out[mrui::kNearbyRowCap];
    ui_fmt_nearby_row(out, sizeof out, row(0x00ABCDEFu, -20.0f, 5u * 60u * 1000u));
    CHECK(strcmp(out, "ABCDEF 0/3 5m") == 0);
    ui_fmt_nearby_row(out, sizeof out, row(0x00ABCDEFu, -20.0f, 3u * 3600u * 1000u));
    CHECK(strcmp(out, "ABCDEF 0/3 3h") == 0);
    // ⛔ AN UNDATEABLE OBSERVATION SAYS SO — `--`, ⛔ never a fabricated `0s` that reads as "just now". This is the
    //    publish site's own `age_valid`, i.e. `ui_fmt_home_age`'s `ever` parameter, and it is the honest answer for a
    //    stamp ahead of the clock that read it.
    ui_fmt_nearby_row(out, sizeof out, row(0x00ABCDEFu, -20.0f, /*age_ms=*/0, /*age_valid=*/false));
    CHECK(strcmp(out, "ABCDEF 0/3 --") == 0);
    // ★ AND THE 64-BIT AGE IS CARRIED WHOLE: a value past `UINT32_MAX` ms must not wrap into a fresh-looking token.
    ui_fmt_nearby_row(out, sizeof out, row(0x00ABCDEFu, -20.0f, uint64_t(UINT32_MAX) + 1u));
    CHECK(strcmp(out, "ABCDEF 0/3 49d") == 0);
    ui_fmt_nearby_row(out, sizeof out, row(0x00ABCDEFu, -20.0f, 200ull * 24ull * 3600ull * 1000ull));
    CHECK(strcmp(out, "ABCDEF 0/3 old") == 0);
}

// ==================================================================================================== the lexemes
TEST_CASE("ui16-nearby: every lexeme is exact, fits 19 columns, and the empty state has its own words") {
    CHECK(strcmp(mrui::kNearbyTitle, "NEARBY") == 0);                 // S-2
    CHECK(strcmp(mrui::kNearbyPhyLine, "CURRENT PHY ONLY") == 0);     // S-3, the design's own line
    CHECK(strcmp(mrui::kNearbyLeafLine, "SAME RADIO + LEAF") == 0);   // S-4, F-1's honest completion
    CHECK(strcmp(mrui::kNearbyEmpty, "NO TEAMS NEARBY") == 0);        // S-5
    CHECK(strlen(mrui::kNearbyTitle)    <= 19u);
    CHECK(strlen(mrui::kNearbyPhyLine)  <= 19u);
    CHECK(strlen(mrui::kNearbyLeafLine) <= 19u);
    CHECK(strlen(mrui::kNearbyEmpty)    <= 19u);
    // The NOTE row: the empty state, or nothing at all.
    CHECK(strcmp(nearby_note(NearbyList{}), mrui::kNearbyEmpty) == 0);
    const NearbyRow src[] = { row(0xAAAA0001u, 0.0f, 1000) };
    CHECK(strcmp(nearby_note(nearby_capture(src, 1, 0)), "") == 0);
    // ⛔ AND A LIST WHOSE ONLY OBSERVATION IS OUR OWN TEAM IS **EMPTY**, note and all: the filter runs before the
    //    question is asked, so the screen never says "one team" and then draws none.
    const NearbyRow mine[] = { row(0xC0FFEE01u, 0.0f, 1000) };
    const NearbyList only_mine = nearby_capture(mine, 1, 0xC0FFEE01u);
    CHECK(only_mine.n == 0);
    CHECK(strcmp(nearby_note(only_mine), mrui::kNearbyEmpty) == 0);
}
