// MeshRoute — test_firmware_ui_status.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN (test_airtime.cpp provides main()); -fno-exceptions => CHECK, never
//     REQUIRE — doctest implements REQUIRE's abort with a throw, so it does not compile in this build.
//
// §UI-17 slice S3 — the native suite for the STATUS body's pure unit (`src/firmware_ui_status.h`): spec §2.2's five
// rows, EVERY substitution in its priority table, both column budgets (14 at x=40, 19 at x=12) and row 4's
// deterministic `RESTART NEEDED` > coordinates > `NO LOCATION` order.
//
// ★★★ WHY IT EXISTS AS ITS OWN SUITE. `src/firmware_ui.cpp` — the only other place these bytes could have been
//     composed — is compiled by NEITHER the native suite NOR the simulator, which is §B115's rule and the reason
//     the strings moved out of it. And a mutation battery is per-SOURCE-FILE, so its own file is also what gives
//     `--target=uistatus` isolated controls for each substitution rather than sharing `model`'s entries.
//
// ★★ THE WIDTHS ARE ASSERTED AS **COLUMNS**, NOT AS "it looked fine". §7.1 rule 5 forbids letting the panel clip as
//    a truncation policy, so a format whose widest expansion exceeds its row's budget is a DEFECT even though the
//    48-byte scratch buffer holds it. Each case drives the WIDEST reachable expansion of its own row.
#include "doctest.h"
#include "firmware_ui_status.h"
#include <cstdint>
#include <cstring>

using mrui::UiSnapshot;

namespace {

// The scratch buffer the renderer hands these formatters (`kLineCap` there, `kStatusLineCap` here — the renderer
// static_asserts that its own is at least as large).
struct Line {
    char b[mrui::kStatusLineCap] = {};
    std::size_t cols() const { return std::strlen(b); }
};

// A snapshot in the shape S3's rows read: an in-team node, DAD'd, with the content key, on a team+mobile build.
UiSnapshot base_snap() {
    UiSnapshot s{};
    s.team_build       = true;
    s.mobile_build     = true;
    s.team_id          = 0x3D9348A5u;
    s.my_team_id       = 220;
    s.team_total       = 4;
    s.team_key_present = true;
    s.unread_dm        = 2;
    s.unread_ch        = 1;
    s.home_confirmed_ever = true;
    s.home_confirm_age_ms = 42u * 1000u;
    return s;
}

// ★★ ROW 4 READS THE **FROZEN SNAPSHOT** (the frame runs once per OLED page — a live read tears the row), so its
//    three inputs ride `UiSnapshot`. This helper drives them the way `build_snapshot` publishes them: the two
//    coordinates VERBATIM and `own_fix` as `ui_status_have_fix`'s own answer, which is the ONE predicate.
// ⛔ It is NOT a shortcut around the field: the `own_fix`-is-the-authority case below drives the field DIRECTLY,
//    including the two combinations this helper cannot produce, so the row's trust in it is measured rather than
//    assumed.
void loc(char* out, std::size_t cap, bool reboot_required, int32_t lat_e7, int32_t lon_e7) {
    UiSnapshot s{};
    s.own_lat_e7 = lat_e7;
    s.own_lon_e7 = lon_e7;
    s.own_fix    = mrui::ui_status_have_fix(lat_e7, lon_e7);
    mrui::ui_status_location(out, cap, reboot_required, s);
}

}  // namespace

// ================================================================================================= ROW 0 — THE TEAM
TEST_CASE("ui17-status: row 0 names the team in EIGHT uppercase hex, and says NO TEAM for id 0") {
    Line l;
    UiSnapshot s = base_snap();
    mrui::ui_status_team(l.b, sizeof l.b, s);
    CHECK(std::strcmp(l.b, "TEAM 3D9348A5") == 0);          // S-1, the spec's own example
    CHECK(l.cols() <= mrui::kStatusNarrowCols);

    // ⛔ UPPERCASE, and zero-padded: a list where one entry reads `a1b2c3` and another `A1B2C3` reads as two teams,
    //   and a space-padded id stops being a fixed-width field the panel can place.
    s.team_id = 0x0000000Au;
    mrui::ui_status_team(l.b, sizeof l.b, s);
    CHECK(std::strcmp(l.b, "TEAM 0000000A") == 0);
    s.team_id = 0xFFFFFFFFu;                                 // the type's own widest value
    mrui::ui_status_team(l.b, sizeof l.b, s);
    CHECK(std::strcmp(l.b, "TEAM FFFFFFFF") == 0);
    CHECK(l.cols() == 13);
    CHECK(l.cols() <= mrui::kStatusNarrowCols);

    // ★ `team_id == 0` is the CORE's own "not in a team" — ⛔ never `TEAM 00000000`, which is a plausible id.
    s.team_id = 0;
    mrui::ui_status_team(l.b, sizeof l.b, s);
    CHECK(std::strcmp(l.b, "NO TEAM") == 0);                 // S-2
    CHECK(l.cols() <= mrui::kStatusNarrowCols);
}

// ============================================================================================== ROW 1 — THE LOCAL ID
TEST_CASE("ui17-status: row 1 is the team-local id, BLANK with no team, and ME NO ID before team-DAD") {
    Line l;
    UiSnapshot s = base_snap();
    mrui::ui_status_me(l.b, sizeof l.b, s);
    CHECK(std::strcmp(l.b, "ME T220") == 0);                 // S-3
    CHECK(l.cols() <= mrui::kStatusNarrowCols);

    s.my_team_id = 255;                                       // uint8_t's own widest
    mrui::ui_status_me(l.b, sizeof l.b, s);
    CHECK(std::strcmp(l.b, "ME T255") == 0);
    CHECK(l.cols() == 7);

    // ★★ spec §2.2 note a: row 0 owns the NO-TEAM token, row 1 says NOTHING — ⛔ not a second `NO TEAM`, which
    //    would spend two of five body rows on one fact.
    s.team_id = 0;
    mrui::ui_status_me(l.b, sizeof l.b, s);
    CHECK(l.b[0] == '\0');
    CHECK(l.cols() == 0);

    // ★★ note b: `team_local_id() == 0` is "not team-DAD'd". ⛔ `ME T0` would be a PLAUSIBLE id for a node with none.
    s = base_snap();
    s.my_team_id = 0;
    mrui::ui_status_me(l.b, sizeof l.b, s);
    CHECK(std::strcmp(l.b, "ME NO ID") == 0);                // S-4
    CHECK(l.cols() == 8);
    CHECK(l.cols() <= mrui::kStatusNarrowCols);
}

// ============================================================================================ ROW 2 — HOW MANY KNOWN
TEST_CASE("ui17-status: row 2 says KNOWN — never HEARD, never MEMBERS — and saturates at 9+") {
    Line l;
    UiSnapshot s = base_snap();
    mrui::ui_status_known(l.b, sizeof l.b, s);
    CHECK(std::strcmp(l.b, "4 KNOWN") == 0);                 // S-5, the ruled word
    CHECK(l.cols() <= mrui::kStatusNarrowCols);
    // ⛔ THE WITHDRAWN WORDINGS, ASSERTED ABSENT so a revert is loud rather than quiet.
    CHECK(std::strstr(l.b, "HEARD") == nullptr);
    CHECK(std::strstr(l.b, "MEMBERS") == nullptr);

    s.team_total = 0;                                         // a real team nobody has been routed to yet
    mrui::ui_status_known(l.b, sizeof l.b, s);
    CHECK(std::strcmp(l.b, "0 KNOWN") == 0);
    s.team_total = 9;                                         // `ui_fmt_team`'s last exact value
    mrui::ui_status_known(l.b, sizeof l.b, s);
    CHECK(std::strcmp(l.b, "9 KNOWN") == 0);
    s.team_total = 10;                                        // the saturation edge — the STRIP's own token
    mrui::ui_status_known(l.b, sizeof l.b, s);
    CHECK(std::strcmp(l.b, "9+ KNOWN") == 0);
    CHECK(l.cols() == 8);
    s.team_total = 255;
    mrui::ui_status_known(l.b, sizeof l.b, s);
    CHECK(std::strcmp(l.b, "9+ KNOWN") == 0);
    CHECK(l.cols() <= mrui::kStatusNarrowCols);
}

TEST_CASE("ui17-status: row 2's NO TEAM KEY outranks the count, and no team at all is SILENCE") {
    Line l;
    UiSnapshot s = base_snap();
    // ★★ note c: the CONTENT key is the ACTIONABLE half — routes without it are real and every post unreadable.
    s.team_key_present = false;
    mrui::ui_status_known(l.b, sizeof l.b, s);
    CHECK(std::strcmp(l.b, "NO TEAM KEY") == 0);             // S-6
    CHECK(l.cols() == 11);
    CHECK(l.cols() <= mrui::kStatusNarrowCols);
    // ...and it outranks the count even when the count is large and would fit.
    s.team_total = 9;
    mrui::ui_status_known(l.b, sizeof l.b, s);
    CHECK(std::strcmp(l.b, "NO TEAM KEY") == 0);

    // ⛔ A BUILD WITH NO TEAM PLANE CLAIMS NOTHING (`gateway_heltec`: OLED=1, TEAM=0) — S3 pin 6.
    s = base_snap();
    s.team_build = false;
    mrui::ui_status_known(l.b, sizeof l.b, s);
    CHECK(l.b[0] == '\0');

    // ⚠ REPORTED, NOT INVENTED — the combination §2.2's table leaves open: the plane exists, no team is configured.
    //   The row is SILENT rather than `-- KNOWN`, because `--` IS row 0's `NO TEAM` fact and note a forbids
    //   spending a second row on it. See the header's block for the three reasons and how to reverse it.
    s = base_snap();
    s.team_id = 0;
    mrui::ui_status_known(l.b, sizeof l.b, s);
    CHECK(l.b[0] == '\0');
    CHECK(std::strstr(l.b, "--") == nullptr);
}

// ====================================================================================== ROW 3 — UNREAD, AND THE HOME
TEST_CASE("ui17-status: row 3 combines the two unread counts and saturates at the STRIP's 99+") {
    Line l;
    UiSnapshot s = base_snap();
    mrui::ui_status_unread_home(l.b, sizeof l.b, s);
    CHECK(std::strcmp(l.b, "3 NEW / HOME 42s") == 0);        // S-7 + S-8
    CHECK(l.cols() <= mrui::kStatusWideCols);

    s.unread_dm = 0; s.unread_ch = 0;
    mrui::ui_status_unread_home(l.b, sizeof l.b, s);
    CHECK(std::strcmp(l.b, "0 NEW / HOME 42s") == 0);        // `0` is a fact, not a blank

    // ★ note e: the saturation token is `ui_fmt_mail`'s `99+`, ⛔ NOT `kUnreadCap`'s 999 — one fact, one token.
    s.unread_dm = 99; s.unread_ch = 0;
    mrui::ui_status_unread_home(l.b, sizeof l.b, s);
    CHECK(std::strcmp(l.b, "99 NEW / HOME 42s") == 0);
    s.unread_dm = 99; s.unread_ch = 1;                        // the crossing, from a SUM
    mrui::ui_status_unread_home(l.b, sizeof l.b, s);
    CHECK(std::strcmp(l.b, "99+ NEW / HOME 42s") == 0);
    s.unread_dm = 999; s.unread_ch = 999;                     // both at `kUnreadCap`
    mrui::ui_status_unread_home(l.b, sizeof l.b, s);
    CHECK(std::strcmp(l.b, "99+ NEW / HOME 42s") == 0);

    // ★★ THE WIDEST REACHABLE EXPANSION OF THIS ROW = 18 of 19 (spec §2.2 note e), driven rather than argued.
    s.home_confirm_age_ms = 59u * 60u * 1000u;                // `59m` — three columns, the token's own bound
    mrui::ui_status_unread_home(l.b, sizeof l.b, s);
    CHECK(std::strcmp(l.b, "99+ NEW / HOME 59m") == 0);
    CHECK(l.cols() == 18);
    CHECK(l.cols() <= mrui::kStatusWideCols);
}

TEST_CASE("ui17-status: row 3's HOME half is `--` when never confirmed and ABSENT with no mobile plane") {
    Line l;
    UiSnapshot s = base_snap();
    // ★ note f: `HOME --`, ⛔ never `HOME UNKNOWN` (S-15, WITHDRAWN — 22 columns, it would clip).
    s.home_confirmed_ever = false;
    s.home_confirm_age_ms = 0;                                // ⛔ 0 with !ever is "never", not "just now"
    mrui::ui_status_unread_home(l.b, sizeof l.b, s);
    CHECK(std::strcmp(l.b, "3 NEW / HOME --") == 0);
    CHECK(std::strstr(l.b, "UNKNOWN") == nullptr);
    CHECK(l.cols() <= mrui::kStatusWideCols);

    // ⛔⛔ NO MOBILE PLANE ⇒ THE HALF IS OMITTED ENTIRELY, never `--`: design §4.2's "not applicable" is a different
    //     silence from "never confirmed", and the strip's home icon already draws that distinction.
    s = base_snap();
    s.mobile_build = false;
    mrui::ui_status_unread_home(l.b, sizeof l.b, s);
    CHECK(std::strcmp(l.b, "3 NEW") == 0);
    CHECK(std::strstr(l.b, "HOME") == nullptr);
    CHECK(l.cols() <= mrui::kStatusWideCols);
    // ...and the omission survives the widest count, which is what a naive "blank the token" fix would not.
    s.unread_dm = 999; s.unread_ch = 999;
    mrui::ui_status_unread_home(l.b, sizeof l.b, s);
    CHECK(std::strcmp(l.b, "99+ NEW") == 0);
    // ⛔ AND `home_confirmed_ever` MUST NOT BRING IT BACK on a build with no plane.
    s.home_confirmed_ever = true;
    s.home_confirm_age_ms = 7u * 1000u;
    mrui::ui_status_unread_home(l.b, sizeof l.b, s);
    CHECK(std::strcmp(l.b, "99+ NEW") == 0);
}

// ================================================================================== ROW 4 — WHERE WE ARE, OR WHAT NEXT
TEST_CASE("ui17-status: row 4's priority is RESTART NEEDED > coordinates > NO LOCATION, and never both") {
    Line l;
    // ★★ THE ORDER IS THE DECISION (note g / design §3.6.5): a saved-but-reboot-required state OWNS this row.
    loc(l.b, sizeof l.b, /*reboot_required=*/true, 521234567, 214567890);
    CHECK(std::strcmp(l.b, "RESTART NEEDED") == 0);           // S-17, the REUSED lexeme
    CHECK(l.cols() == 14);
    CHECK(l.cols() <= mrui::kStatusWideCols);
    CHECK(std::strstr(l.b, "52.") == nullptr);                // ⛔ the coordinates are ABSENT while it stands
    CHECK(std::strstr(l.b, ",")   == nullptr);

    // ...and it outranks the ABSENCE of a fix too — one row, one statement.
    loc(l.b, sizeof l.b, /*reboot_required=*/true, 0, 0);
    CHECK(std::strcmp(l.b, "RESTART NEEDED") == 0);

    // With the reboot fact clear the coordinates come back, unchanged.
    loc(l.b, sizeof l.b, /*reboot_required=*/false, 521234567, 214567890);
    CHECK(std::strcmp(l.b, "52.123,21.456") == 0);            // S-10, the spec's own example
    CHECK(l.cols() <= mrui::kStatusWideCols);
}

TEST_CASE("ui17-status: (0,0) is NO LOCATION — the CORE's own predicate, never a plausible 0.000,0.000") {
    Line l;
    loc(l.b, sizeof l.b, false, 0, 0);
    CHECK(std::strcmp(l.b, "NO LOCATION") == 0);              // S-9
    CHECK(l.cols() == 11);
    CHECK(l.cols() <= mrui::kStatusWideCols);
    CHECK(std::strstr(l.b, "0.000") == nullptr);

    // ⛔ THE PREDICATE IS `lat != 0 || lon != 0` — an OR, because that is what `Node::on_command` refuses on. ONE
    //   non-zero coordinate is a fix (a node on the prime meridian, or on the equator), and narrowing this to a
    //   single field or to an AND would disagree with the thing that actually rejects a located send.
    CHECK(mrui::ui_status_have_fix(0, 0) == false);
    CHECK(mrui::ui_status_have_fix(1, 0) == true);
    CHECK(mrui::ui_status_have_fix(0, 1) == true);
    CHECK(mrui::ui_status_have_fix(-1, 0) == true);
    CHECK(mrui::ui_status_have_fix(0, -1) == true);
    loc(l.b, sizeof l.b, false, 0, 214567890);
    CHECK(std::strcmp(l.b, "0.000,21.456") == 0);
    loc(l.b, sizeof l.b, false, 521234567, 0);
    CHECK(std::strcmp(l.b, "52.123,0.000") == 0);
}

TEST_CASE("ui17-status: row 4 reads the FROZEN snapshot, and `own_fix` is its authority in both directions") {
    Line l;
    UiSnapshot s{};
    // ⛔⛔ THE ROW MUST NOT RE-DERIVE THE PREDICATE. `own_fix` is `ui_status_have_fix`'s answer taken at the ONE
    //     site that can see `NodeConfig`; a renderer or formatter that re-tested the coordinates would be the
    //     second definition U1 forbids, and it would disagree with the thing that actually refuses a located send.
    //     ⇒ these two combinations are UNREACHABLE through `build_snapshot` and are driven here on purpose,
    //     because they are exactly what a publish-site defect produces — and the direction must be fail-CLOSED.
    s.own_fix = false; s.own_lat_e7 = 521234567; s.own_lon_e7 = 214567890;
    mrui::ui_status_location(l.b, sizeof l.b, /*reboot_required=*/false, s);
    CHECK(std::strcmp(l.b, "NO LOCATION") == 0);          // no claimed fix ⇒ no position, whatever the fields hold
    s.own_fix = true;  s.own_lat_e7 = 0; s.own_lon_e7 = 0;
    mrui::ui_status_location(l.b, sizeof l.b, /*reboot_required=*/false, s);
    CHECK(std::strcmp(l.b, "0.000,0.000") == 0);          // ...and the converse is VISIBLE, never silently repaired

    // The three fields ride the snapshot VERBATIM — no cast, no clamp (the `home_confirm_age_ms` rule).
    s.own_fix = true; s.own_lat_e7 = -521234567; s.own_lon_e7 = 214567890;
    mrui::ui_status_location(l.b, sizeof l.b, /*reboot_required=*/false, s);
    CHECK(std::strcmp(l.b, "-52.123,21.456") == 0);
    // ...and the reboot fact still outranks every one of them.
    mrui::ui_status_location(l.b, sizeof l.b, /*reboot_required=*/true, s);
    CHECK(std::strcmp(l.b, "RESTART NEEDED") == 0);
}

TEST_CASE("ui17-status: the coordinate TRUNCATES toward zero, in all four quadrants, and -0.000 keeps its sign") {
    Line l;
    // ★★★ note i: TRUNCATION, ⛔ NOT ROUNDING — the panel must never render a position more precise, or further
    //     along, than the stored one. `.1239` truncates to `.123`; `.9999` truncates to `.999`.
    loc(l.b, sizeof l.b, false, 521239999, 214569999);
    CHECK(std::strcmp(l.b, "52.123,21.456") == 0);

    // The four sign quadrants, driven directly.
    loc(l.b, sizeof l.b, false,  521234567,  214567890);
    CHECK(std::strcmp(l.b, "52.123,21.456") == 0);            // NE
    loc(l.b, sizeof l.b, false,  521234567, -214567890);
    CHECK(std::strcmp(l.b, "52.123,-21.456") == 0);           // NW
    loc(l.b, sizeof l.b, false, -521234567,  214567890);
    CHECK(std::strcmp(l.b, "-52.123,21.456") == 0);           // SE
    loc(l.b, sizeof l.b, false, -521234567, -214567890);
    CHECK(std::strcmp(l.b, "-52.123,-21.456") == 0);          // SW

    // ★★ THE `-0.000` BOUNDARY: a position 1/2000 of a degree SOUTH truncates to zero digits but is NOT on the
    //    equator. The SIGN is the raw value's and is drawn as its own field — ⛔ it is never inferred from the
    //    truncated digits, which is exactly what a `%.3f`-shaped implementation would lose.
    loc(l.b, sizeof l.b, false, -5000, -5000);
    CHECK(std::strcmp(l.b, "-0.000,-0.000") == 0);
    loc(l.b, sizeof l.b, false, -5000, 5000);
    CHECK(std::strcmp(l.b, "-0.000,0.000") == 0);

    // ★ THE WIDEST REACHABLE EXPANSION = 16 of 19 (note i), driven rather than argued: the coordinate domain's own
    //   extremes with both signs.
    loc(l.b, sizeof l.b, false, -891234567, -1791234567);
    CHECK(std::strcmp(l.b, "-89.123,-179.123") == 0);
    CHECK(l.cols() == 16);
    CHECK(l.cols() <= mrui::kStatusWideCols);
    // ...and the poles / the antimeridian, which are in domain and must not widen it further.
    loc(l.b, sizeof l.b, false, -900000000, -1800000000);
    CHECK(std::strcmp(l.b, "-90.000,-180.000") == 0);
    CHECK(l.cols() == 16);
    loc(l.b, sizeof l.b, false, 900000000, 1800000000);
    CHECK(std::strcmp(l.b, "90.000,180.000") == 0);
    CHECK(l.cols() <= mrui::kStatusWideCols);
}

// ================================================================== THE WHOLE BODY, IN THE TWO SHAPES S3 PIN 6 NAMES
TEST_CASE("ui17-status: gateway_heltec's shape — no team plane, no mobile plane — claims NOTHING") {
    Line l;
    UiSnapshot s{};                     // every build flag false, every id 0: the `MR_FEAT_TEAM=0 / MOBILE=0` build
    mrui::ui_status_team(l.b, sizeof l.b, s);
    CHECK(std::strcmp(l.b, "NO TEAM") == 0);
    mrui::ui_status_me(l.b, sizeof l.b, s);
    CHECK(l.b[0] == '\0');
    mrui::ui_status_known(l.b, sizeof l.b, s);
    CHECK(l.b[0] == '\0');
    mrui::ui_status_unread_home(l.b, sizeof l.b, s);
    CHECK(std::strcmp(l.b, "0 NEW") == 0);                    // ⛔ no HOME half at all
    loc(l.b, sizeof l.b, false, 0, 0);
    CHECK(std::strcmp(l.b, "NO LOCATION") == 0);
}

TEST_CASE("ui17-status: EVERY row of the fully-populated body fits its own column budget") {
    Line l;
    UiSnapshot s = base_snap();
    // The widest reachable expansion of each row at once, so a format that grew is caught at its own budget.
    s.team_id = 0xFFFFFFFFu; s.my_team_id = 0; s.team_total = 255; s.team_key_present = false;
    s.unread_dm = 999; s.unread_ch = 999; s.home_confirm_age_ms = 59u * 60u * 1000u;

    mrui::ui_status_team(l.b, sizeof l.b, s);          CHECK(l.cols() <= mrui::kStatusNarrowCols);
    mrui::ui_status_me(l.b, sizeof l.b, s);            CHECK(l.cols() <= mrui::kStatusNarrowCols);
    mrui::ui_status_known(l.b, sizeof l.b, s);         CHECK(l.cols() <= mrui::kStatusNarrowCols);
    mrui::ui_status_unread_home(l.b, sizeof l.b, s);   CHECK(l.cols() <= mrui::kStatusWideCols);
    loc(l.b, sizeof l.b, false, -891234567, -1791234567);
    CHECK(l.cols() <= mrui::kStatusWideCols);
    loc(l.b, sizeof l.b, true, -891234567, -1791234567);
    CHECK(l.cols() <= mrui::kStatusWideCols);

    // ⛔ NO CONFIGURATION TEXT RETURNS TO STATUS (§9 R-3): the badge carries it and SETTINGS says the words. Only
    //    `RESTART NEEDED` may appear, and it does so on row 4 alone.
    UiSnapshot t = base_snap();
    Line rows[4];
    mrui::ui_status_team(rows[0].b, sizeof rows[0].b, t);
    mrui::ui_status_me(rows[1].b, sizeof rows[1].b, t);
    mrui::ui_status_known(rows[2].b, sizeof rows[2].b, t);
    mrui::ui_status_unread_home(rows[3].b, sizeof rows[3].b, t);
    for (const Line& r : rows) {
        CHECK(std::strstr(r.b, "CFG")     == nullptr);
        CHECK(std::strstr(r.b, "UNSAVED") == nullptr);
        CHECK(std::strstr(r.b, "RELOAD")  == nullptr);
        CHECK(std::strstr(r.b, "RESTART") == nullptr);
    }
}
