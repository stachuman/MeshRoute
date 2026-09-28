// MeshRoute — test_firmware_ui_status.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN (test_airtime.cpp provides main()); -fno-exceptions => CHECK, never
//     REQUIRE — doctest implements REQUIRE's abort with a throw, so it does not compile in this build.
//
// ★★★★ W4b (2026-09-27) — the native suite for `src/firmware_ui_status.h`, whose landing-screen body is now HOME
//      (design §6.2–§6.7): row 0's identity, row 1's team line, the item labels, My device's rows, the two notes and
//      the body invalidation. The §UI-17 S3 STATUS-row cases (rows 0-3, the 14-column budget, the old gateway and
//      "every row fits" shapes) RETIRED with those rows (brief §2.9's ledger names each replacement); the
//      coordinate/fix cases are KEPT verbatim — My device's position row is exactly `ui_status_location`.
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

// W4b — a counted own name, published the way `build_snapshot` does (raw bytes, never terminated).
void set_name(UiSnapshot& s, const char* bytes, uint8_t len) {
    for (uint8_t i = 0; i < sizeof s.own_name; ++i) s.own_name[i] = (i < len) ? bytes[i] : '\0';
    s.own_name_len = len;
}
// W4b — the item's label, through the ONE formatter the renderer calls.
const char* label_of(Line& l, mrui::HomeItem it, const UiSnapshot& s) {
    mrui::ui_home_item_label(l.b, sizeof l.b, it, s);
    return l.b;
}

}  // namespace

// ================================================================== W4b — HOME ROW 0: WHO THIS DEVICE IS (§4.2)
// ⓘ REPLACES the retired `ui17-status` rows 0-3 cases (the team/ME/KNOWN/unread rows at x = 40 and x = 12): their
//   facts moved into Home's team line and item labels below, and the 14-column budget left with the mark.
TEST_CASE("w4b-home: row 0 is ME plus the W4a identity in 16 cells — whole, abbreviated, the full hash, or ME alone") {
    Line l;
    UiSnapshot s = base_snap();
    s.my_key_hash32 = 0x12AB34CDu;
    set_name(s, "STAN", 4);
    mrui::ui_home_me_line(l.b, sizeof l.b, s);
    CHECK(std::strcmp(l.b, "ME STAN") == 0);
    set_name(s, "ABCDEFGHIJKLMNOP", 16);                              // exactly the budget: WHOLE
    mrui::ui_home_me_line(l.b, sizeof l.b, s);
    CHECK(std::strcmp(l.b, "ME ABCDEFGHIJKLMNOP") == 0);
    CHECK(l.cols() == mrui::kHomeCols);
    set_name(s, "ABCDEFGHIJKLMNOPQ", 17);                             // one past: 15 cells + the generated marker
    mrui::ui_home_me_line(l.b, sizeof l.b, s);
    CHECK(std::strcmp(l.b, "ME ABCDEFGHIJKLMNO" "\xBB") == 0);
    CHECK(l.cols() == mrui::kHomeCols);
    for (uint8_t len : { uint8_t(19), uint8_t(20) }) {                // My device's row split: Home still abbreviates
        set_name(s, "ABCDEFGHIJKLMNOPQRSTUVWXYZ123456", len);
        mrui::ui_home_me_line(l.b, sizeof l.b, s);
        CHECK(std::strcmp(l.b, "ME ABCDEFGHIJKLMNO" "\xBB") == 0);
    }
    set_name(s, "ABCDEFGHIJKLMNOPQRSTUVWXYZ123456", 32);              // the 32-byte cap: the SAME abbreviation
    mrui::ui_home_me_line(l.b, sizeof l.b, s);
    CHECK(std::strcmp(l.b, "ME ABCDEFGHIJKLMNO" "\xBB") == 0);
    set_name(s, "\xC5\x82" "AB", 4);                                  // high bytes are SANITIZED, never mojibake
    mrui::ui_home_me_line(l.b, sizeof l.b, s);
    CHECK(std::strcmp(l.b, "ME ..AB") == 0);
    set_name(s, "", 0);                                               // unnamed: the key's full hash, WHOLE (D10)
    mrui::ui_home_me_line(l.b, sizeof l.b, s);
    CHECK(std::strcmp(l.b, "ME 0x12AB34CD") == 0);
    s.my_key_hash32 = 0;                                              // ⛔ no name and no hash: never a fabrication
    mrui::ui_home_me_line(l.b, sizeof l.b, s);
    CHECK(std::strcmp(l.b, "ME") == 0);
}

// ================================================================ W4b — HOME ROW 1: THE TEAM LINE (design §6.2)
TEST_CASE("w4b-home: row 1's team line — T<id>, NO ID before team-DAD, NO TEAM, blank with no team plane") {
    Line l;
    UiSnapshot s = base_snap();                                       // team 3D9348A5, local id 220
    mrui::ui_home_team_line(l.b, sizeof l.b, s);
    CHECK(std::strcmp(l.b, "TEAM 3D9348A5 T220") == 0);
    CHECK(l.cols() == 18);
    s.my_team_id = 0;                                                 // ⛔ never `T0`: the DAD has not happened
    mrui::ui_home_team_line(l.b, sizeof l.b, s);
    CHECK(std::strcmp(l.b, "TEAM 3D9348A5 NO ID") == 0);
    CHECK(l.cols() == mrui::kHomeCols);
    // ★ THE EIGHT DIGITS ARE THE FULL TEAM ID's, ending with the six-digit fingerprint (the displays agree).
    char fp[mrui::kTeamFpTokenCap];
    mrui::ui_fmt_team_fingerprint(fp, sizeof fp, s.team_id);
    CHECK(std::strncmp(l.b + 7, fp, 6) == 0);
    s.team_id = 0xFFFFFFFFu; s.my_team_id = 254;                      // the widest ready spelling
    mrui::ui_home_team_line(l.b, sizeof l.b, s);
    CHECK(std::strcmp(l.b, "TEAM FFFFFFFF T254") == 0);
    s.team_id = 0;
    mrui::ui_home_team_line(l.b, sizeof l.b, s);
    CHECK(std::strcmp(l.b, "NO TEAM") == 0);
    s.team_build = false; s.team_id = 0x3D9348A5u;                    // no team plane: the row is BLANK
    mrui::ui_home_team_line(l.b, sizeof l.b, s);
    CHECK(l.cols() == 0);
}

// ============================================================== W4b — HOME's ITEM LABELS (design §6.2 / §6.3)
TEST_CASE("w4b-home: INBOX and TEAM carry the STRIP's tokens — n NEW to 99+, n KNOWN to 9+ — and omit them at zero") {
    Line l;
    UiSnapshot s = base_snap();
    s.unread_dm = 0; s.unread_ch = 0;
    CHECK(std::strcmp(label_of(l, mrui::HomeItem::inbox, s), "INBOX") == 0);
    s.unread_dm = 2; s.unread_ch = 1;
    CHECK(std::strcmp(label_of(l, mrui::HomeItem::inbox, s), "INBOX 3 NEW") == 0);
    s.unread_dm = 60; s.unread_ch = 39;
    CHECK(std::strcmp(label_of(l, mrui::HomeItem::inbox, s), "INBOX 99 NEW") == 0);
    s.unread_ch = 40;                                                 // 100: the strip's saturation
    CHECK(std::strcmp(label_of(l, mrui::HomeItem::inbox, s), "INBOX 99+ NEW") == 0);
    s.team_total = 0;
    CHECK(std::strcmp(label_of(l, mrui::HomeItem::team, s), "TEAM") == 0);
    s.team_total = 4;
    CHECK(std::strcmp(label_of(l, mrui::HomeItem::team, s), "TEAM 4 KNOWN") == 0);   // KNOWN, never HEARD
    s.team_total = 9;
    CHECK(std::strcmp(label_of(l, mrui::HomeItem::team, s), "TEAM 9 KNOWN") == 0);
    s.team_total = 10;
    CHECK(std::strcmp(label_of(l, mrui::HomeItem::team, s), "TEAM 9+ KNOWN") == 0);
}

TEST_CASE("w4b-home: the action words are provision_row_label's, MENU is kListMenuText, and every row fits 19") {
    Line l;
    const UiSnapshot s = base_snap();
    CHECK(std::strcmp(label_of(l, mrui::HomeItem::join, s),   mrui::provision_row_label(mrui::ProvRow::join_team)) == 0);
    CHECK(std::strcmp(label_of(l, mrui::HomeItem::create, s), mrui::provision_row_label(mrui::ProvRow::create_team)) == 0);
    CHECK(std::strcmp(label_of(l, mrui::HomeItem::invite, s), mrui::provision_row_label(mrui::ProvRow::invite)) == 0);
    CHECK(std::strcmp(label_of(l, mrui::HomeItem::join, s),     "JOIN TEAM") == 0);
    CHECK(std::strcmp(label_of(l, mrui::HomeItem::create, s),   "CREATE TEAM") == 0);
    CHECK(std::strcmp(label_of(l, mrui::HomeItem::invite, s),   "INVITE MEMBER") == 0);
    CHECK(std::strcmp(label_of(l, mrui::HomeItem::menu, s),     mrui::kListMenuText) == 0);
    CHECK(std::strcmp(label_of(l, mrui::HomeItem::menu, s),     "MENU") == 0);
    CHECK(std::strcmp(label_of(l, mrui::HomeItem::send, s),     "SEND TO TEAM") == 0);
    CHECK(std::strcmp(label_of(l, mrui::HomeItem::my_device, s), "MY DEVICE") == 0);
    CHECK(std::strcmp(label_of(l, mrui::HomeItem::key_help, s), "NO TEAM KEY - HELP") == 0);
    CHECK(std::strcmp(label_of(l, mrui::HomeItem::none, s),     "") == 0);
    CHECK(std::strlen(mrui::kHomeOptionsChangedText) == 15);          // design §6.4: 15 columns
    // ★ ONE marker cell + the label: the widest row is 19 of 19, driven through the renderer's own composer.
    Line r;
    mrui::ui_home_row(r.b, sizeof r.b, true, "NO TEAM KEY - HELP");
    CHECK(std::strcmp(r.b, ">NO TEAM KEY - HELP") == 0);
    CHECK(r.cols() == mrui::kHomeCols);
    mrui::ui_home_row(r.b, sizeof r.b, false, "INBOX 99+ NEW");      // a preview row: a BLANK marker cell
    CHECK(std::strcmp(r.b, " INBOX 99+ NEW") == 0);
    for (mrui::HomeItem it : { mrui::HomeItem::inbox, mrui::HomeItem::send, mrui::HomeItem::team,
                               mrui::HomeItem::invite, mrui::HomeItem::my_device, mrui::HomeItem::menu,
                               mrui::HomeItem::join, mrui::HomeItem::create, mrui::HomeItem::key_help }) {
        UiSnapshot w = base_snap(); w.unread_dm = 999; w.team_total = 250;   // the widest tokens
        mrui::ui_home_row(r.b, sizeof r.b, true, label_of(l, it, w));
        CHECK(r.cols() <= mrui::kHomeCols);
    }
}

// ================================================================================== ROW 4 — WHERE WE ARE, OR WHAT NEXT
TEST_CASE("ui17-status: row 4's priority is RESTART NEEDED > coordinates > NO LOCATION, and never both") {
    Line l;
    // ★★ THE ORDER IS THE DECISION (note g / design §3.6.5): a saved-but-reboot-required state OWNS this row.
    loc(l.b, sizeof l.b, /*reboot_required=*/true, 521234567, 214567890);
    CHECK(std::strcmp(l.b, "RESTART NEEDED") == 0);           // S-17, the REUSED lexeme
    CHECK(l.cols() == 14);
    CHECK(l.cols() <= mrui::kHomeCols);
    CHECK(std::strstr(l.b, "52.") == nullptr);                // ⛔ the coordinates are ABSENT while it stands
    CHECK(std::strstr(l.b, ",")   == nullptr);

    // ...and it outranks the ABSENCE of a fix too — one row, one statement.
    loc(l.b, sizeof l.b, /*reboot_required=*/true, 0, 0);
    CHECK(std::strcmp(l.b, "RESTART NEEDED") == 0);

    // With the reboot fact clear the coordinates come back, unchanged.
    loc(l.b, sizeof l.b, /*reboot_required=*/false, 521234567, 214567890);
    CHECK(std::strcmp(l.b, "52.123,21.456") == 0);            // S-10, the spec's own example
    CHECK(l.cols() <= mrui::kHomeCols);
}

TEST_CASE("ui17-status: (0,0) is NO LOCATION — the CORE's own predicate, never a plausible 0.000,0.000") {
    Line l;
    loc(l.b, sizeof l.b, false, 0, 0);
    CHECK(std::strcmp(l.b, "NO LOCATION") == 0);              // S-9
    CHECK(l.cols() == 11);
    CHECK(l.cols() <= mrui::kHomeCols);
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
    CHECK(l.cols() <= mrui::kHomeCols);
    // ...and the poles / the antimeridian, which are in domain and must not widen it further.
    loc(l.b, sizeof l.b, false, -900000000, -1800000000);
    CHECK(std::strcmp(l.b, "-90.000,-180.000") == 0);
    CHECK(l.cols() == 16);
    loc(l.b, sizeof l.b, false, 900000000, 1800000000);
    CHECK(std::strcmp(l.b, "90.000,180.000") == 0);
    CHECK(l.cols() <= mrui::kHomeCols);
}

// ================================================================== W4b — MY DEVICE (design §4.2 / §6.7)
// ⓘ REPLACES the retired "gateway_heltec claims NOTHING" and "EVERY row fits its budget" STATUS-body cases: Home's
//   gateway shape and budgets are asserted below and in the Home cases above.
TEST_CASE("w4b-mydevice: the name is split RAW 0-18 / 19-31, each byte sanitized; an unnamed device is NO NAME SET") {
    Line r0, r1;
    UiSnapshot s = base_snap();
    set_name(s, "", 0);
    mrui::ui_my_device_name_rows(r0.b, sizeof r0.b, r1.b, sizeof r1.b, s);
    CHECK(std::strcmp(r0.b, "NO NAME SET") == 0);
    CHECK(r1.cols() == 0);
    struct Case { uint8_t len; const char* row0; const char* row1; };
    const char* full = "ABCDEFGHIJKLMNOPQRSTUVWXYZ123456";
    for (const Case& c : { Case{16, "ABCDEFGHIJKLMNOP", ""}, Case{17, "ABCDEFGHIJKLMNOPQ", ""},
                           Case{19, "ABCDEFGHIJKLMNOPQRS", ""}, Case{20, "ABCDEFGHIJKLMNOPQRS", "T"},
                           Case{32, "ABCDEFGHIJKLMNOPQRS", "TUVWXYZ123456"} }) {
        set_name(s, full, c.len);
        mrui::ui_my_device_name_rows(r0.b, sizeof r0.b, r1.b, sizeof r1.b, s);
        CHECK(std::strcmp(r0.b, c.row0) == 0);
        CHECK(std::strcmp(r1.b, c.row1) == 0);
        CHECK(r0.cols() <= mrui::kHomeCols);
    }
    // ⛔ NEVER an abbreviation split afterwards: no `»` on either row, every byte of the stored name is drawn.
    set_name(s, full, 32);
    mrui::ui_my_device_name_rows(r0.b, sizeof r0.b, r1.b, sizeof r1.b, s);
    CHECK(std::strchr(r0.b, '\xBB') == nullptr);
    CHECK(r0.cols() + r1.cols() == 32);
    // High bytes and a raw 0xBB are SANITIZED per byte — the raw split, then `ui_display_byte` on each cell.
    set_name(s, "\xC5\x82" "AB" "\xBB", 5);
    mrui::ui_my_device_name_rows(r0.b, sizeof r0.b, r1.b, sizeof r1.b, s);
    CHECK(std::strcmp(r0.b, "..AB.") == 0);
    char big[32]; for (uint8_t i = 0; i < 32; ++i) big[i] = char(0xC0 + (i % 32));
    set_name(s, big, 32);
    mrui::ui_my_device_name_rows(r0.b, sizeof r0.b, r1.b, sizeof r1.b, s);
    CHECK(r0.cols() == 19);
    CHECK(r1.cols() == 13);
    CHECK(std::strcmp(r0.b, "...................") == 0);        // ★ BOTH rows are sanitized per byte, row 1 too
    CHECK(std::strcmp(r1.b, ".............") == 0);
}

TEST_CASE("w4b-mydevice: ID 0x<HASH8> is the stable identity, and the position row is the location without restart") {
    Line l;
    UiSnapshot s = base_snap();
    s.my_key_hash32 = 0x12AB34CDu;
    mrui::ui_my_device_id(l.b, sizeof l.b, s);
    CHECK(std::strcmp(l.b, "ID 0x12AB34CD") == 0);
    s.my_key_hash32 = 0x0000BEEFu;
    mrui::ui_my_device_id(l.b, sizeof l.b, s);
    CHECK(std::strcmp(l.b, "ID 0x0000BEEF") == 0);
    // The renderer hands `ui_status_location` reboot = false: RESTART NEEDED is Home's row 2, never My device's.
    loc(l.b, sizeof l.b, /*reboot_required=*/false, -5000, 214567890);
    CHECK(std::strcmp(l.b, "-0.000,21.456") == 0);
    loc(l.b, sizeof l.b, /*reboot_required=*/false, 0, 0);
    CHECK(std::strcmp(l.b, "NO LOCATION") == 0);
}

// ================================================================== W4b — THE TWO HOME NOTES (design §6.5 / §6.6)
TEST_CASE("w4b-notes: key help is five fixed rows; the blocked setup says its reason, IN SETTINGS only when resolvable") {
    CHECK(std::strcmp(mrui::kKeyHelpRows[0], "NO TEAM KEY") == 0);
    CHECK(std::strcmp(mrui::kKeyHelpRows[1], "A MEMBER WHO HAS IT") == 0);
    CHECK(std::strcmp(mrui::kKeyHelpRows[2], "MUST GRANT IT TO") == 0);
    CHECK(std::strcmp(mrui::kKeyHelpRows[3], "THIS DEVICE") == 0);
    CHECK(std::strcmp(mrui::kKeyHelpRows[4], "press = back") == 0);
    for (const char* row : mrui::kKeyHelpRows) CHECK(std::strlen(row) <= mrui::kHomeCols);
    CHECK(std::strcmp(mrui::prov_block_note(mrui::ProvBlock::unsaved),     "SAVE OR DISCARD") == 0);
    CHECK(std::strcmp(mrui::prov_block_note(mrui::ProvBlock::conflict),    "RELOAD OR DISCARD") == 0);
    CHECK(std::strcmp(mrui::prov_block_note(mrui::ProvBlock::unavailable), "CFG UNAVAILABLE") == 0);
    CHECK(std::strcmp(mrui::prov_block_note(mrui::ProvBlock::none),        "") == 0);
    CHECK(std::strcmp(mrui::ui_setup_block_row2(mrui::ProvBlock::unsaved),     "IN SETTINGS") == 0);
    CHECK(std::strcmp(mrui::ui_setup_block_row2(mrui::ProvBlock::conflict),    "IN SETTINGS") == 0);
    CHECK(std::strcmp(mrui::ui_setup_block_row2(mrui::ProvBlock::unavailable), "") == 0);   // Settings cannot fix it
    CHECK(std::strcmp(mrui::ui_setup_block_row2(mrui::ProvBlock::none),        "") == 0);
}

// ================================================================== W4b — THE BODY INVALIDATION (design §6.7)
TEST_CASE("w4b-home: the body invalidation repaints Home for a visible fact change with NO press, and only on Home") {
    mrui::UiModel m;
    UiSnapshot frozen = base_snap();
    set_name(frozen, "STAN", 4);
    frozen.my_key_hash32 = 0x12AB34CDu; frozen.own_fix = true; frozen.own_lat_e7 = 521234567; frozen.own_lon_e7 = 1;
    m.on_tick(frozen);
    CHECK(m.state().screen == mrui::Screen::status);                  // the landing screen is Home
    m.clear_dirty();
    UiSnapshot live = frozen;
    CHECK(mrui::ui_home_invalidate(m, live, frozen) == false);        // ⛔ equal: nothing asked, nothing cleared
    CHECK(m.state().dirty == false);
    // Each Home/My-device fact, one at a time, with every strip token held still.
    for (int f = 0; f < 11; ++f) {
        live = frozen;
        switch (f) {
            case 0:  set_name(live, "STAN2", 5);                 break;   // a console rename
            case 1:  set_name(live, "STAM", 4);                  break;   // same length, one byte moved
            case 2:  live.my_key_hash32 = 0x12AB34CEu;          break;
            case 3:  live.team_id = 0x3D9348A6u;                break;
            case 4:  live.my_team_id = 221;                      break;   // the team-DAD answer
            case 5:  live.team_key_present = false;              break;   // the profile
            case 6:  live.own_lat_e7 = 521234568;                break;   // a new position
            case 7:  live.own_fix = false;                       break;
            case 8:  live.prov_invite = !live.prov_invite;       break;   // a capability moving an item
            case 9:  live.own_lon_e7 = 2;                        break;
            case 10: live.team_build = false;                    break;
        }
        m.clear_dirty();
        CHECK(mrui::ui_home_facts_equal(live, frozen) == false);
        CHECK(mrui::ui_home_invalidate(m, live, frozen) == true);
        CHECK(m.state().dirty == true);
    }
    // ⛔ ONLY WHERE THE BODY IS: off Home, and under an overlay or a modal, nothing is asked for.
    mrui::UiState st = m.state();
    CHECK(mrui::ui_home_body_visible(st, false, mrui::Emergency::idle) == true);
    CHECK(mrui::ui_home_body_visible(st, true,  mrui::Emergency::idle) == false);    // compose owns the body
    CHECK(mrui::ui_home_body_visible(st, false, mrui::Emergency::arming) == false);  // the alarm owns it
    st.detail = mrui::InboxModal::body;
    CHECK(mrui::ui_home_body_visible(st, false, mrui::Emergency::idle) == false);
    st = m.state(); st.screen = mrui::Screen::team;
    CHECK(mrui::ui_home_body_visible(st, false, mrui::Emergency::idle) == false);
}

// ================================================================== W4b — THE GATEWAY SHAPE (design §6.3)
TEST_CASE("w4b-home: gateway_heltec's shape — no team plane — has a blank team line and INBOX / MY DEVICE / MENU") {
    UiSnapshot s{};
    s.team_build = false; s.mobile_build = false;
    s.team_id = 0x3D9348A5u;                                          // ⛔ a stray id on a plane-less build says nothing
    Line l;
    mrui::ui_home_team_line(l.b, sizeof l.b, s);
    CHECK(l.cols() == 0);
    mrui::HomeCapture c{};
    mrui::home_items_of(s, c);
    CHECK(c.count == 3);
    CHECK(c.items[0] == mrui::HomeItem::inbox);
    CHECK(c.items[1] == mrui::HomeItem::my_device);
    CHECK(c.items[2] == mrui::HomeItem::menu);
    CHECK(mrui::home_profile(s) == mrui::HomeProfile::no_plane);
}
