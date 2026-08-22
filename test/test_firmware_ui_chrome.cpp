// MeshRoute — test_firmware_ui_chrome.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN — test_airtime.cpp provides main().
// NB: the native build is -fno-exceptions, so doctest's REQUIRE is a HARD COMPILE ERROR. Every check is a CHECK.
//
// §CHROME-1 (design `docs/superpowers/specs/2026-08-15-heltec-mobile-status-navigation-ui-design.md` §11.1) — the
// PURE half of the OLED chrome redesign: the icon assets' byte-order contract, the four compact formatters, the
// `UiChrome` projection with its field-by-field equality, and the ONE §5.2 navigation mapping.
//
// ★★ EVERY CASE BELOW IS WRITTEN TO BE ABLE TO COME OUT OTHERWISE. Four probes in the §B200 arc were green against
//    the very defect they were written to catch, so each block here names the wrong implementation it excludes, and
//    `tools/probe_ui_model_mutations.py --target=chrome` installs that wrong implementation in the real source and
//    requires this suite to go RED. A case with no such sibling entry is decoration.
#include "doctest.h"
#include "firmware_ui_chrome.h"
#include "firmware_ui_icons.h"
#include <cstdint>
#include <cstring>
#include <new>      // placement new — the padding case below builds one operand over poisoned storage

using namespace mrui;

// ---------------------------------------------------------------------------------------------------- helpers

// A snapshot with NOTHING established: no mobile plane, no team, no battery, no mail. Every case opts IN to the
// fact it is about, so a field it does not name cannot be quietly carrying the answer.
static UiSnapshot chrome_snap() {
    UiSnapshot s{};
    s.now_ms = 1000;
    s.batt_mv = -1;
    s.team_build = true;      // the team PLANE is compiled in; `team_id` still decides "configured"
    s.team_id = 0;
    return s;
}

static UiChrome chrome_of(const UiSnapshot& s, const UiState& st = UiState{},
                          Emergency emg = Emergency::idle, ChromeCfg cfg = ChromeCfg{}) {
    return ui_chrome(s, st, emg, cfg);
}

// Decode one pixel of an asset under the ONE convention the icon header declares (row-major, LSB-first, rows padded
// to whole bytes). ⛔ Deliberately written out here rather than calling a helper from the header: a decoder shared
// with the thing it verifies would agree with a mirrored asset just as happily.
static bool icon_pixel(const uint8_t* bits, uint8_t w, uint8_t y, uint8_t x) {
    const uint8_t stride = uint8_t((w + 7u) / 8u);
    return ((bits[y * stride + (x >> 3)] >> (x & 7u)) & 1u) != 0u;
}

static void icon_row_art(const uint8_t* bits, uint8_t w, uint8_t y, char* out) {
    for (uint8_t x = 0; x < w; ++x) out[x] = icon_pixel(bits, w, y, x) ? '#' : '.';
    out[w] = '\0';
}

// ===================================================================================== §8.1 — the icon assets

TEST_CASE("chrome-icons: the byte-order contract is ROW-MAJOR, LSB-FIRST, rows padded to whole bytes") {
    // ★★ THE CONFORMANCE GLYPH IS ASYMMETRIC ON BOTH AXES ON PURPOSE. §8.1's amendment: leaving the byte format to
    //    the board is how the same asset renders MIRRORED or BIT-REVERSED on the V4 port. A symmetric glyph passes a
    //    mirror, a flip AND a bit-reversal — the instrument-that-cannot-fail shape this project keeps finding.
    //    `kIconKey`'s bow is on the LEFT and its single tooth hangs on the RIGHT, below the shaft.
    static const char* kKeyArt[7] = {
        ".......",
        ".##....",
        "#..#...",
        "#..####",
        "#..#..#",
        ".##....",
        ".......",
    };
    char row[16];
    for (uint8_t y = 0; y < icons::kIconH; ++y) {
        icon_row_art(icons::kIconKey, icons::kIconW, y, row);
        CHECK(std::strcmp(row, kKeyArt[y]) == 0);
    }
    // The second conformance glyph: an arrow head pointing RIGHT. A horizontal mirror turns "send" into "receive",
    // which is the one icon error on this panel that would read as a working feature rather than as a glitch.
    static const char* kSendArt[7] = {
        "#......",
        "###....",
        "#####..",
        "#######",
        "#####..",
        "###....",
        "#......",
    };
    for (uint8_t y = 0; y < icons::kIconH; ++y) {
        icon_row_art(icons::kIconSend, icons::kIconW, y, row);
        CHECK(std::strcmp(row, kSendArt[y]) == 0);
    }
    // A NEGATIVE CONTROL for the decoder itself: if `icon_pixel` were reading MSB-first, the send arrow's row 0
    // would be `......#` and this would pass anyway. It must not.
    CHECK(icon_pixel(icons::kIconSend, icons::kIconW, 0, 0) == true);
    CHECK(icon_pixel(icons::kIconSend, icons::kIconW, 0, 6) == false);
}

TEST_CASE("chrome-icons: the stride rule is DERIVED and the battery is the one 2-byte-per-row asset") {
    CHECK(icons::stride_of(7) == 1);
    CHECK(icons::stride_of(8) == 1);
    CHECK(icons::stride_of(9) == 2);
    CHECK(icons::stride_of(icons::kBatteryW) == 2);
    CHECK(icons::byte_count_of(icons::kIconW, icons::kIconH) == sizeof icons::kIconKey);
    CHECK(icons::byte_count_of(icons::kBatteryW, icons::kBatteryH) == sizeof icons::kIconBattery);
    // ⛔ A call site that assumed "one byte per row" would read the 11-px battery as a 7x14 smear. Pin the two
    //    pixels that prove the stride is honoured: the terminal nub is at x=9..10, which only exists in byte 1.
    CHECK(icon_pixel(icons::kIconBattery, icons::kBatteryW, 3, 10) == true);   // the nub
    CHECK(icon_pixel(icons::kIconBattery, icons::kBatteryW, 0, 10) == false);  // …and the top edge stops before it
    CHECK(icon_pixel(icons::kIconBattery, icons::kBatteryW, 0, 8) == true);    // the body's top-right corner
}

TEST_CASE("chrome-icons: every glyph is 7 px high, and the four SETTINGS badge variants are DISTINCT") {
    CHECK(sizeof icons::kIconMail          == icons::kIconH);
    CHECK(sizeof icons::kIconHomeUnknown   == icons::kIconH);
    CHECK(sizeof icons::kIconHomeConfirmed == icons::kIconH);
    CHECK(sizeof icons::kIconHomeChecking  == icons::kIconH);
    CHECK(sizeof icons::kIconHomeLost      == icons::kIconH);
    CHECK(sizeof icons::kIconPeople        == icons::kIconH);
    CHECK(sizeof icons::kIconKeyCrossed    == icons::kIconH);
    CHECK(sizeof icons::kIconStatus        == icons::kIconH);
    // ★ FOUR BADGE STATES, FOUR DIFFERENT PICTURES. §6 gives them a strict visible priority, so two that render
    //   identically would make the priority unobservable on the panel — the badge would be right and useless.
    const uint8_t* badges[4] = { icons::kIconSettings, icons::kIconSettingsUnsaved,
                                 icons::kIconSettingsConflict, icons::kIconSettingsRestart };
    for (int a = 0; a < 4; ++a)
        for (int b = a + 1; b < 4; ++b)
            CHECK(std::memcmp(badges[a], badges[b], icons::kIconH) != 0);
    // The crossed key must not equal the plain key either — same argument, one slot over (§4.4).
    CHECK(std::memcmp(icons::kIconKey, icons::kIconKeyCrossed, icons::kIconH) != 0);
    // …and all four HOME glyphs are distinct: §4.2's four core states must be tellable apart at a glance.
    const uint8_t* homes[4] = { icons::kIconHomeUnknown, icons::kIconHomeConfirmed,
                                icons::kIconHomeChecking, icons::kIconHomeLost };
    for (int a = 0; a < 4; ++a)
        for (int b = a + 1; b < 4; ++b)
            CHECK(std::memcmp(homes[a], homes[b], icons::kIconH) != 0);
}

// ★★★★ §UI-17 S6 — THE STATUS BODY'S 24x24 MARK, DECODED ROW BY ROW. Same instrument as the two 7-px conformance
//      glyphs above and for the same reason, with one addition that only this asset has: it is the FIRST 3-BYTE-
//      STRIDE asset in the tree, so a decoder or an authoring pass that assumed the battery's stride of 2 — or the
//      other fourteen assets' stride of 1 — produces a picture, not a compile error.
// ★★ THE ART IS THE SPECIFICATION, and it is pinned in FULL rather than by sampled pixels: the asset is INTERIM
//    (owner ruling 2026-08-22) and the final mark arrives as a byte swap, so this case is exactly the thing that
//    tells whoever performs that swap what the bytes are supposed to draw. ⛔ Re-point it to the new picture; ⛔
//    never relax it to "some ink somewhere", which every mirror, flip and bit-reversal would satisfy.
TEST_CASE("chrome-icons: the 24x24 MeshRoute mark decodes to the INTERIM `MR` letterform, stride 3") {
    static const char* kMarkArt[24] = {
        "........................",
        "........................",
        "........................",
        "##.......##..#########..",
        "###.....###..##########.",
        "####...####..##......##.",
        "##.##.##.##..##......##.",
        "##..###..##..##......##.",
        "##...#...##..##......##.",
        "##.......##..##......##.",
        "##.......##..##########.",
        "##.......##..#########..",
        "##.......##..##..##.....",
        "##.......##..##...##....",
        "##.......##..##...##....",
        "##.......##..##....##...",
        "##.......##..##.....##..",
        "##.......##..##......##.",
        "##.......##..##.......##",
        "##.......##..##.......##",
        "##.......##..##.......##",
        "........................",
        "........................",
        "........................",
    };
    char row[32];
    for (uint8_t y = 0; y < icons::kMarkH; ++y) {
        icon_row_art(icons::kMarkMeshRoute, icons::kMarkW, y, row);
        CHECK(std::strcmp(row, kMarkArt[y]) == 0);
    }
    // The stride is DERIVED for this asset too, and its size follows from it. ⛔ A hand-written stride of 1 would
    //   read the mark as a 8x24 smear of its own left third — the `kIconBattery` failure one size up.
    CHECK(icons::stride_of(icons::kMarkW) == 3);
    CHECK(icons::byte_count_of(icons::kMarkW, icons::kMarkH) == sizeof icons::kMarkMeshRoute);
    CHECK(sizeof icons::kMarkMeshRoute == 72u);
    // NEGATIVE CONTROLS FOR THE DECODER ITSELF, on the two axes the letterform is asymmetric about — without these
    // the row comparison above could pass against a decoder that agreed with a mirrored asset.
    CHECK(icon_pixel(icons::kMarkMeshRoute, icons::kMarkW, 3,  0) == true);   // the M's stem, top-left
    CHECK(icon_pixel(icons::kMarkMeshRoute, icons::kMarkW, 3, 23) == false);  // …and the R's bowl stops short of it
    CHECK(icon_pixel(icons::kMarkMeshRoute, icons::kMarkW, 20, 23) == true);  // the R's leg foot, bottom-RIGHT
    CHECK(icon_pixel(icons::kMarkMeshRoute, icons::kMarkW, 20, 13) == true);  // …and the R's stem beside it
    CHECK(icon_pixel(icons::kMarkMeshRoute, icons::kMarkW, 20,  5) == false); // the M is hollow at the bottom
    // …and the mark is not one of the 7-px glyphs by accident: it must differ from every one of them in its first
    // seven bytes, which is all a mistaken copy-paste of a strip asset would leave behind.
    CHECK(std::memcmp(icons::kMarkMeshRoute, icons::kIconStatus, icons::kIconH) != 0);
    CHECK(std::memcmp(icons::kMarkMeshRoute, icons::kIconBattery, icons::kIconH) != 0);
}

// ===================================================================================== §4.1 — the mail slot

TEST_CASE("chrome-mail: 0 / 1 / 99 exact, 100 -> 99+, and a SUM that crosses the boundary") {
    struct { uint16_t dm, ch; uint8_t want; bool over; const char* tok; } k[] = {
        {  0,   0,  0, false, "0"   },
        {  1,   0,  1, false, "1"   },
        {  0,   1,  1, false, "1"   },
        { 50,  49, 99, false, "99"  },   // the exact boundary, reached as a SUM (§11.1's "a sum crossing the boundary")
        { 50,  50, 99, true,  "99+" },   // …and one over it, still a sum
        { 99,   0, 99, false, "99"  },
        {100,   0, 99, true,  "99+" },
        {999, 999, 99, true,  "99+" },   // both operands already clamped to kUnreadCap
    };
    char tok[8];
    for (const auto& c : k) {
        UiSnapshot s = chrome_snap(); s.unread_dm = c.dm; s.unread_ch = c.ch;
        const UiChrome ch = chrome_of(s);
        CHECK(ch.mail == c.want);
        CHECK(ch.mail_overflow == c.over);
        ui_fmt_mail(tok, sizeof tok, ch.mail, ch.mail_overflow);
        CHECK(std::strcmp(tok, c.tok) == 0);
    }
}

// ===================================================================================== §4.2 — the home slot

TEST_CASE("chrome-home: the compact age at EVERY boundary, in BOTH directions") {
    // ⚠ An off-by-one here is invisible on a panel: `59m` and `1h` are both plausible. So every boundary is pinned
    //   from BELOW and from ABOVE, in the same table.
    const uint64_t kSec = 1000ull, kMin = 60ull * kSec, kHour = 60ull * kMin, kDay = 24ull * kHour;
    struct { uint64_t ms; const char* tok; } k[] = {
        {              0ull, "0s"  },
        {            999ull, "0s"  },
        {           1000ull, "1s"  },
        {      59ull * kSec, "59s" },   // 59 s  ...
        {      60ull * kSec, "1m"  },   // ... and 60 s is the NEXT unit, not `60s`
        {  59ull * kSec + 999ull, "59s" },
        {      59ull * kMin, "59m" },   // 59 m  ...
        {      60ull * kMin, "1h"  },   // ... and 60 m
        {  59ull * kMin + 59ull * kSec, "59m" },
        {     23ull * kHour, "23h" },   // 23 h  ...
        {     24ull * kHour, "1d"  },   // ... and 24 h
        { 23ull * kHour + 59ull * kMin, "23h" },
        {      99ull * kDay, "99d" },   // 99 d  ...
        {     100ull * kDay, "old" },   // ... and 100 d
        { 99ull * kDay + 23ull * kHour, "99d" },
        {    5000ull * kDay, "old" },
    };
    char tok[kAgeTokenCap];
    for (const auto& c : k) {
        ui_fmt_home_age(tok, sizeof tok, /*ever=*/true, c.ms);
        CHECK(std::strcmp(tok, c.tok) == 0);
    }
    // §4.2's third state, and it is NOT `0s`: never confirmed renders `--`. A fresh boot that showed `0s` would be
    // claiming a confirmation it has never had.
    ui_fmt_home_age(tok, sizeof tok, /*ever=*/false, 0);
    CHECK(std::strcmp(tok, "--") == 0);
    ui_fmt_home_age(tok, sizeof tok, /*ever=*/false, 7ull * kDay);   // …and the age is IGNORED while `!ever`
    CHECK(std::strcmp(tok, "--") == 0);
}

TEST_CASE("chrome-home: an age ABOVE UINT32_MAX ms does not wrap to a recent value") {
    // ★★★★ THE SINGLE MOST LIKELY DEFECT IN THIS SLICE (§4.2, and the snapshot's own idiom invites it): `now_ms` is
    //      `uint32_t` and `last_dm_age_s` is a `uint32_t` seconds age, so "age = now_ms - confirmed_ms" would be
    //      written naturally — and would re-create the ~49.7-day wrap this project already fixed once.
    //   ⇒ every value below is chosen so that a 32-bit TRUNCATION lands on a DIFFERENT, PLAUSIBLE, RECENT token.
    char tok[kAgeTokenCap];
    const uint64_t kDay = 86400000ull;

    // UINT32_MAX ms is 49.71 days. One millisecond MORE truncates to 0 -> `0s`, i.e. "confirmed just now".
    ui_fmt_home_age(tok, sizeof tok, true, uint64_t(UINT32_MAX) + 1ull);
    CHECK(std::strcmp(tok, "49d") == 0);
    CHECK(std::strcmp(tok, "0s")  != 0);            // the truncated answer, named so the case cannot pass vacuously

    // 100 days + 1 ms is `old`. Truncated it is 50 065 409 ms = 13 h — a link that has been dead for over three
    // months, rendered as one confirmed this afternoon.
    ui_fmt_home_age(tok, sizeof tok, true, 100ull * kDay + 1ull);
    CHECK(std::strcmp(tok, "old") == 0);
    CHECK(std::strcmp(tok, "13h") != 0);

    // And one that is NOT near a 2^32 multiple, so the case is not an artefact of the two chosen constants.
    ui_fmt_home_age(tok, sizeof tok, true, 60ull * kDay);
    CHECK(std::strcmp(tok, "60d") == 0);

    // ★★★ THE SAME THREE VALUES ROUTED THROUGH THE **SNAPSHOT**, which is the half the formatter alone cannot pin.
    //     `UiSnapshot::home_confirm_age_ms` is where the truncation would actually be written (the snapshot's own
    //     idiom is `uint32_t` ages), so without this block a narrowing of that FIELD would go unmeasured while the
    //     formatter's own parameter stayed 64-bit — an instrument that measures the easy half.
    UiSnapshot s = chrome_snap();
    s.mobile_build = true; s.home_confirmed_ever = true;
    s.home_confirm_age_ms = uint64_t(UINT32_MAX) + 1ull;
    CHECK(std::strcmp(chrome_of(s).home_age, "49d") == 0);
    s.home_confirm_age_ms = 100ull * kDay + 1ull;
    CHECK(std::strcmp(chrome_of(s).home_age, "old") == 0);
    s.home_confirm_age_ms = 60ull * kDay;
    CHECK(std::strcmp(chrome_of(s).home_age, "60d") == 0);
}

TEST_CASE("chrome-home: all four link states map to their own icon, and a non-mobile build is BLANK") {
    using L = MESHROUTE_NS::Node::MobileHomeLink;
    struct { L link; HomeIcon want; } k[] = {
        { L::unknown,   HomeIcon::unknown   },
        { L::confirmed, HomeIcon::confirmed },
        { L::checking,  HomeIcon::checking  },
        { L::lost,      HomeIcon::lost      },
    };
    for (const auto& c : k) {
        UiSnapshot s = chrome_snap();
        s.mobile_build = true; s.home_link = c.link;
        s.home_confirmed_ever = true; s.home_confirm_age_ms = 5000;
        const UiChrome ch = chrome_of(s);
        CHECK(ch.home == c.want);
        CHECK(std::strcmp(ch.home_age, "5s") == 0);
    }
    // ★★ THE FIFTH DISPLAY STATE. §4.2: "on a non-mobile OLED build the home slot is BLANK, not crossed. 'Not
    //    applicable' must not be rendered as a fault." `gateway_heltec` is a real OLED build with MOBILE=0.
    // ⛔ AND THE AGE IS EMPTY, NOT `--`: "no home plane" and "never confirmed" are different silences, and a `--`
    //    beside a blank slot would suggest a home was expected and missing.
    UiSnapshot s = chrome_snap();
    s.mobile_build = false; s.home_link = MESHROUTE_NS::Node::MobileHomeLink::lost;
    s.home_confirmed_ever = true; s.home_confirm_age_ms = 5000;
    const UiChrome ch = chrome_of(s);
    CHECK(ch.home == HomeIcon::blank);
    CHECK(ch.home != HomeIcon::lost);
    CHECK(ch.home_age[0] == '\0');
    CHECK(std::strcmp(ch.home_age, "--") != 0);
}

// ===================================================================================== §4.3/§4.4 — team + key

TEST_CASE("chrome-team: NO TEAM is not a team with zero teammates, and the key slot follows the same split") {
    char tok[8];
    // (a) no team at all -> `--`, neutral people icon, BLANK key slot (§4.3, §4.4 row 1).
    UiSnapshot s = chrome_snap(); s.team_id = 0; s.team_total = 0; s.team_key_present = true;
    UiChrome ch = chrome_of(s);
    CHECK(ch.team_configured == false);
    CHECK(ch.team_count == 0);
    CHECK(ch.key == KeyIcon::blank);          // ⛔ NOT `absent`: with no team the content key is IRRELEVANT
    ui_fmt_team(tok, sizeof tok, ch.team_configured, ch.team_count, ch.team_overflow);
    CHECK(std::strcmp(tok, "--") == 0);

    // (b) a CONFIGURED team with zero known teammates -> `0`. This is the state (a) must never be confused with.
    s = chrome_snap(); s.team_id = 0x1234; s.team_total = 0; s.team_key_present = false;
    ch = chrome_of(s);
    CHECK(ch.team_configured == true);
    CHECK(ch.team_count == 0);
    CHECK(ch.key == KeyIcon::absent);         // team configured, no content key -> CROSSED key (§4.4 row 2)
    ui_fmt_team(tok, sizeof tok, ch.team_configured, ch.team_count, ch.team_overflow);
    CHECK(std::strcmp(tok, "0") == 0);

    // (c) configured + the content key held -> normal key (§4.4 row 3).
    s.team_key_present = true;
    CHECK(chrome_of(s).key == KeyIcon::present);

    // (d) a build with NO team plane at all reads as "no team", whatever `team_id` happens to hold.
    s = chrome_snap(); s.team_build = false; s.team_id = 0x1234; s.team_key_present = true; s.team_total = 4;
    ch = chrome_of(s);
    CHECK(ch.team_configured == false);
    CHECK(ch.key == KeyIcon::blank);
    CHECK(ch.team_count == 0);
}

TEST_CASE("chrome-team: 9 is exact, 10 renders 9+, and the value is team_TOTAL not team_shown") {
    char tok[8];
    struct { uint8_t total; uint8_t want; bool over; const char* text; } k[] = {
        { 1, 1, false, "1"  },
        { 8, 8, false, "8"  },
        { 9, 9, false, "9"  },
        {10, 9, true,  "9+" },
        {99, 9, true,  "9+" },
    };
    for (const auto& c : k) {
        UiSnapshot s = chrome_snap(); s.team_id = 7; s.team_total = c.total;
        // ★ `team_shown` is the UI's 8-row capacity and is deliberately set to a DIFFERENT number here: the retired
        //   `T8/12` fraction is exactly the display-shaped value §4.3 removes, so if the projection read `team_shown`
        //   every row of this table would be wrong.
        s.team_shown = (c.total > kMaxTeamRows) ? kMaxTeamRows : c.total;
        const UiChrome ch = chrome_of(s);
        CHECK(ch.team_count == c.want);
        CHECK(ch.team_overflow == c.over);
        ui_fmt_team(tok, sizeof tok, ch.team_configured, ch.team_count, ch.team_overflow);
        CHECK(std::strcmp(tok, c.text) == 0);
    }
    // The one that separates the two authorities outright: 12 rows, 8 shown. `team_shown` would render `8`.
    UiSnapshot s = chrome_snap(); s.team_id = 7; s.team_total = 12; s.team_shown = 8;
    CHECK(chrome_of(s).team_count == 9);
    CHECK(chrome_of(s).team_overflow == true);
}

// ===================================================================================== §4.5 — battery

TEST_CASE("chrome-batt: unavailable renders --, and the voltage TRUNCATES to one decimal") {
    char tok[kVoltsTokenCap];
    UiSnapshot s = chrome_snap(); s.batt_mv = -1;
    CHECK(chrome_of(s).batt_dv == -1);
    ui_fmt_batt(tok, sizeof tok, chrome_of(s).batt_dv);
    CHECK(std::strcmp(tok, "--") == 0);

    // ⚠ TRUNCATION, NOT ROUNDING (§4.5 keeps the existing formatter's semantics): 4 199 mV is `4.1V`, never `4.2V`.
    //   A rounding implementation passes every "nice" value and fails only here, which is why the table is full of
    //   near-boundary millivolts rather than round ones.
    struct { int32_t mv; int16_t dv; const char* text; } k[] = {
        {    0,   0, "0.0V" },
        {   99,   0, "0.0V" },
        { 3900,  39, "3.9V" },
        { 4099,  40, "4.0V" },
        { 4100,  41, "4.1V" },
        { 4123,  41, "4.1V" },
        { 4199,  41, "4.1V" },   // ⛔ rounding would say 4.2V
        { 4200,  42, "4.2V" },
    };
    for (const auto& c : k) {
        UiSnapshot t = chrome_snap(); t.batt_mv = c.mv;
        const UiChrome ch = chrome_of(t);
        CHECK(ch.batt_dv == c.dv);
        ui_fmt_batt(tok, sizeof tok, ch.batt_dv);
        CHECK(std::strcmp(tok, c.text) == 0);
    }
    // ★★★★ THE UPPER BOUND IS GEOMETRIC (QG round 2). §3.1 freezes the battery slot at 35 px = an 11-px outline
    //      plus FOUR small-font columns, so `d.dV` is the widest token it can draw. A reading that needs five
    //      characters (`10.0V` = 30 px of text + 11 px icon = 41 px) OVERRUNS the frozen slot and pushes every
    //      earlier icon out of budget ⇒ it is UNAVAILABLE, and unavailable renders `--`.
    // ⛔ ROUND 1 EMITTED `99.9V` and justified it as "nobody will mistake it for a cell" — a PLAUSIBILITY argument
    //    against a GEOMETRIC defect. ⛔ And the fix is NOT a clamp to `9.9V`: that would put a plausible-looking
    //    voltage this node never measured on the panel, the one substitution the battery path forbids.
    CHECK(kBattMaxDv == 99);
    struct { int32_t mv; const char* why; } wide[] = {
        { 10000,    "10.0V — the FIRST reading that needs five characters" },
        { 12600,    "a 3S pack: physically possible, still unrenderable here" },
        { 40000000, "a nonsense reading that would also overflow int16_t" },
    };
    for (const auto& c : wide) {
        (void)c.why;
        UiSnapshot big = chrome_snap(); big.batt_mv = c.mv;
        const UiChrome ch = chrome_of(big);
        CHECK(ch.batt_dv == -1);                       // classified UNAVAILABLE, not clamped
        CHECK(ch.batt_dv != kBattMaxDv);               // ⛔ the tempting plausible clamp, named so it cannot return
        ui_fmt_batt(tok, sizeof tok, ch.batt_dv);
        CHECK(std::strcmp(tok, "--") == 0);
    }
    // And the LAST renderable value is still rendered — the bound is exact, not a rounded-down safety margin.
    UiSnapshot edge = chrome_snap(); edge.batt_mv = 9999;
    CHECK(chrome_of(edge).batt_dv == 99);
    ui_fmt_batt(tok, sizeof tok, chrome_of(edge).batt_dv);
    CHECK(std::strcmp(tok, "9.9V") == 0);
    // ⓘ THE FORMATTER'S OWN GUARD, pinned SEPARATELY so neither layer masks the other's mutation: even handed a
    //   value the projection would never publish, it fails closed rather than emitting an over-wide token.
    ui_fmt_batt(tok, sizeof tok, int16_t(kBattMaxDv + 1));
    CHECK(std::strcmp(tok, "--") == 0);
    ui_fmt_batt(tok, sizeof tok, int16_t(400));
    CHECK(std::strcmp(tok, "--") == 0);
    // The token buffer is sized to the SLOT: `9.9V` + NUL, and nothing this formatter can emit exceeds it.
    CHECK(kVoltsTokenCap == 5);
}

// ======================================================== §UI-15 §7 — the team-id fingerprint (DISPLAY-ONLY)
//
// ★★ THE ONLY REASON THIS HELPER EXISTS IS THAT TWO INDEPENDENT IMPLEMENTATIONS COULD DISAGREE, so these cases pin
//    the DEFINITION rather than a sample of outputs: the mask, the width, the zero, and the case. §UI-15 plan §7
//    rejected "six digits derived from the id" for admitting high-bits / low-bits / hash variants — every one of
//    which passes a test that only checks "six characters come out".
// ⓘ The helper is CURRENTLY UNCALLED (slice 3 is the definition alone; the INVITE and NEARBY screens are slices 5/6),
//   so these cases drive it DIRECTLY. That is not a gap — a pinned definition landed before its first consumer is
//   exactly what stops the second consumer forking it.

TEST_CASE("chrome-fingerprint: §7's definition verbatim — UPPERCASE, ZERO-PADDED hex of the LOW 24 BITS") {
    char tok[kTeamFpTokenCap];
    // ★ THE SPEC'S OWN EXAMPLE, BYTE FOR BYTE. §UI-15 plan §7 / brief pin 1: `0x12A1B2C3` -> `A1B2C3`.
    ui_fmt_team_fingerprint(tok, sizeof tok, 0x12A1B2C3u);
    CHECK(std::strcmp(tok, "A1B2C3") == 0);

    struct { uint32_t id; const char* tok; const char* why; } k[] = {
        { 0x12A1B2C3u, "A1B2C3", "§7's example" },
        { 0x00000000u, "000000", "⛔ NO SPECIAL CASE: 0 is formatted like any other id (C2)" },
        { 0x00000001u, "000001", "the zero-padding, at its most visible" },
        { 0x0000000Fu, "00000F", "one hex digit, and it is UPPER case" },
        { 0x00000100u, "000100", "an interior zero as well as leading ones" },
        { 0x00FFFFFFu, "FFFFFF", "the widest value the mask can pass" },
        { 0xFFFFFFFFu, "FFFFFF", "…and the top byte adds NOTHING to it" },
        { 0x00ABCDEFu, "ABCDEF", "every letter digit, upper case" },
        { 0x00123456u, "123456", "every numeric digit" },
    };
    for (const auto& c : k) {
        (void)c.why;
        ui_fmt_team_fingerprint(tok, sizeof tok, c.id);
        CHECK(std::strcmp(tok, c.tok) == 0);
        // ⛔ EXACTLY SIX CHARACTERS, ALWAYS — it is a fixed-width field the panel places, not a variable token. A
        //    `%lX` (no width) implementation passes the `A1B2C3` example above and fails right here.
        CHECK(std::strlen(tok) == 6u);
        // …and UPPERCASE: a list holding `a1b2c3` beside `A1B2C3` reads as two teams. Pinned as a PROPERTY over the
        // whole table, not only on the entries that happen to contain a letter.
        for (std::size_t i = 0; i < 6; ++i)
            CHECK(((tok[i] >= '0' && tok[i] <= '9') || (tok[i] >= 'A' && tok[i] <= 'F')));
    }
    // The capacity is the token plus its NUL, and nothing this formatter can emit exceeds it.
    CHECK(kTeamFpTokenCap == 7);
    CHECK(kTeamFpMask == 0x00FFFFFFu);
}

TEST_CASE("chrome-fingerprint: the MASK IS the definition — the top byte is invisible, all 24 low bits are not") {
    char a[kTeamFpTokenCap], b[kTeamFpTokenCap];
    // ★★ A POSITIVE PROPERTY, NOT A DEFECT (brief pin 3): two ids differing ONLY in bits 24-31 fingerprint
    //    IDENTICALLY. ⛔ It is safe precisely because of the DISPLAY-ONLY rule — design §3.6.4 point 3 has the
    //    confirmation select the exact full `team_id`, never the visible fingerprint — so a collision costs a human
    //    one extra glance, never a wrong join. ⛔ Do not "fix" this by widening the token; that forks the definition.
    const uint32_t low = 0x00A1B2C3u;
    for (uint32_t top = 0; top < 256u; ++top) {
        ui_fmt_team_fingerprint(a, sizeof a, low);
        ui_fmt_team_fingerprint(b, sizeof b, (top << 24) | low);
        CHECK(std::strcmp(a, b) == 0);
        CHECK(std::strcmp(b, "A1B2C3") == 0);
    }
    // ★ AND THE OTHER HALF, which is what makes the mask a MASK rather than a truncation to any old six digits: every
    //   one of bits 0-23 REACHES the token, and each produces a token of its own. A high-bits implementation
    //   (`id >> 8`), a hash, or a mask one nibble too narrow all die here rather than on the example above.
    char seen[24][kTeamFpTokenCap];
    char zero[kTeamFpTokenCap];
    ui_fmt_team_fingerprint(zero, sizeof zero, 0u);
    for (int i = 0; i < 24; ++i) {
        ui_fmt_team_fingerprint(seen[i], sizeof seen[i], uint32_t(1u) << i);
        CHECK(std::strcmp(seen[i], zero) != 0);                 // the bit is VISIBLE
    }
    for (int i = 0; i < 24; ++i)
        for (int j = i + 1; j < 24; ++j)
            CHECK(std::strcmp(seen[i], seen[j]) != 0);          // …and no two of them collide
    // The NEGATIVE control for the same loop: bits 24-31 are invisible, one at a time.
    for (int i = 24; i < 32; ++i) {
        ui_fmt_team_fingerprint(a, sizeof a, uint32_t(1u) << i);
        CHECK(std::strcmp(a, zero) == 0);
    }
}

TEST_CASE("chrome-fingerprint: the WHOLE destination is defined, per the chrome formatters' padding rule") {
    // The neighbours' contract (`ui_pad_token`): each formatter NUL-fills to `cap`, so the bytes of a token are fully
    // defined and a byte-for-byte comparison of two tokens is sound. ⚠ Poison the buffer first — over a zeroed one
    // this case would pass against a formatter that padded nothing.
    char big[16];
    std::memset(big, 'Z', sizeof big);
    ui_fmt_team_fingerprint(big, sizeof big, 0x12A1B2C3u);
    CHECK(std::strcmp(big, "A1B2C3") == 0);
    for (std::size_t i = 6; i < sizeof big; ++i) CHECK(big[i] == '\0');
    // ⓘ And a cap SHORTER than the token truncates and still NUL-terminates — `snprintf`'s own contract, asserted
    //   here so the house idiom's behaviour is stated rather than assumed by a future caller with a tight buffer.
    char small[4];
    std::memset(small, 'Z', sizeof small);
    ui_fmt_team_fingerprint(small, sizeof small, 0x12A1B2C3u);
    CHECK(std::strcmp(small, "A1B") == 0);
}

// =============================================== §UI-15 slice 5 — the TWO TEAM-ID LINES §3.6.3's SCREENS DRAW
// ★★ Both live beside the fingerprint for the §B115 reason (a string built in `src/firmware_ui.cpp` is a string no
//    automated gate can read), and both are driven DIRECTLY here — the renderer that calls them is not compiled by
//    this suite, so these cases are the only place their VISIBLE BYTES are established.

TEST_CASE("chrome-teamid: the FULL id is the console's own spelling — `0x` + EIGHT uppercase hex, zero-padded") {
    char tok[kTeamIdTokenCap];
    struct { uint32_t id; const char* tok; } k[] = {
        { 0x12A1B2C3u, "0x12A1B2C3" },
        { 0x00000000u, "0x00000000" },   // ⛔ no special case: design §3.6.3 asks for the FULL id, whatever it is
        { 0x00000001u, "0x00000001" },   // the zero-padding, at its most visible
        { 0xFFFFFFFFu, "0xFFFFFFFF" },
        { 0x000ABCDEu, "0x000ABCDE" },   // every letter digit, UPPER case
    };
    for (const auto& c : k) {
        ui_fmt_team_id_full(tok, sizeof tok, c.id);
        CHECK(std::strcmp(tok, c.tok) == 0);
        // ⛔ EXACTLY TEN CHARACTERS, ALWAYS — a `%lX` without the width passes the first row and fails here.
        CHECK(std::strlen(tok) == 10u);
    }
    CHECK(kTeamIdTokenCap == 11);                            // the token plus its NUL
    // ★★ AND IT IS A DIFFERENT TOKEN FROM THE FINGERPRINT, which is the whole reason design §3.6.3 draws BOTH: the
    //    full id is the AUTHORITY value, the fingerprint is the human aid. Two ids that fingerprint IDENTICALLY (the
    //    mask's own property) must still render as two different full ids.
    char fpa[kTeamFpTokenCap], fpb[kTeamFpTokenCap], ida[kTeamIdTokenCap], idb[kTeamIdTokenCap];
    ui_fmt_team_fingerprint(fpa, sizeof fpa, 0x11A1B2C3u);
    ui_fmt_team_fingerprint(fpb, sizeof fpb, 0x22A1B2C3u);
    ui_fmt_team_id_full(ida, sizeof ida, 0x11A1B2C3u);
    ui_fmt_team_id_full(idb, sizeof idb, 0x22A1B2C3u);
    CHECK(std::strcmp(fpa, fpb) == 0);
    CHECK(std::strcmp(ida, idb) != 0);
    // The neighbours' padding rule: the WHOLE destination is defined (poison it first, or this passes vacuously).
    char big[16];
    std::memset(big, 'Z', sizeof big);
    ui_fmt_team_id_full(big, sizeof big, 0x12A1B2C3u);
    CHECK(std::strcmp(big, "0x12A1B2C3") == 0);
    for (std::size_t i = 10; i < sizeof big; ++i) CHECK(big[i] == '\0');
}

TEST_CASE("chrome-replaces: the confirmation's warning line — and a TEAMLESS node is NOT warned about a replacement") {
    char l[kProvReplacesCap];
    std::memset(l, 'Z', sizeof l);
    // ★★★ THE CONDITION IS THE DECISION, and it lives HERE: design §3.6.3 says the screen warns *"if already in a
    //     team"*, and `team_id == 0` is the core's own "not in a team" (node.h:261). ⛔ A renderer that tested it
    //     would be a decision no automated gate compiles.
    CHECK(ui_fmt_prov_replaces(l, sizeof l, 0u) == false);
    CHECK(std::strcmp(l, "") == 0);                          // ...and it leaves NO stale text behind for the panel
    for (std::size_t i = 0; i < sizeof l; ++i) CHECK(l[i] == '\0');
    // ★ A MEMBER IS warned, and the team is named by the ONE fingerprint definition — ⛔ never a second truncation.
    CHECK(ui_fmt_prov_replaces(l, sizeof l, 0x12A1B2C3u) == true);
    CHECK(std::strcmp(l, "REPLACES A1B2C3") == 0);
    char fp[kTeamFpTokenCap];
    ui_fmt_team_fingerprint(fp, sizeof fp, 0x12A1B2C3u);
    CHECK(std::strstr(l, fp) != nullptr);                    // the SHARED token, not a private spelling of it
    CHECK(std::strlen(l) <= 19u);                            // §7.3: it fits the rail's 19-column body
    CHECK(kProvReplacesCap == 16);
    // ⓘ EVERY non-zero id warns — including one whose fingerprint is all zeros, which a `fingerprint != 0` shortcut
    //   would silently drop (the team `0x01000000` is a real membership).
    CHECK(ui_fmt_prov_replaces(l, sizeof l, 0x01000000u) == true);
    CHECK(std::strcmp(l, "REPLACES 000000") == 0);
}

// ===================================================================================== §6 — the badge priority

TEST_CASE("chrome-badge: conflict > unsaved > restart-required > clean, including the two overlapping pairs") {
    // The full truth table, so no combination is left to an assumption.
    struct { bool cf, un, rr; CfgBadge want; } k[] = {
        { false, false, false, CfgBadge::clean    },
        { false, false, true,  CfgBadge::restart  },
        { false, true,  false, CfgBadge::unsaved  },
        { false, true,  true,  CfgBadge::unsaved  },   // ★ §11.1's "unsaved plus restart"
        { true,  false, false, CfgBadge::conflict },
        { true,  false, true,  CfgBadge::conflict },
        { true,  true,  false, CfgBadge::conflict },   // ★ §11.1's "conflict plus unsaved"
        { true,  true,  true,  CfgBadge::conflict },
    };
    for (const auto& c : k) {
        CHECK(ui_cfg_badge(c.cf, c.un, c.rr) == c.want);
        const UiSnapshot s = chrome_snap();
        ChromeCfg cfg{}; cfg.conflict = c.cf; cfg.unsaved = c.un; cfg.restart_required = c.rr;
        CHECK(chrome_of(s, UiState{}, Emergency::idle, cfg).badge == c.want);
    }
    // A NULL service fails CLOSED to `clean` — the model's own documented state for an unattached service. Nothing
    // has been edited, so nothing is claimed.
    const ChromeCfg none = ChromeCfg::from(nullptr);
    CHECK(none.unsaved == false);
    CHECK(none.conflict == false);
    CHECK(none.restart_required == false);
    CHECK(ui_cfg_badge(none.conflict, none.unsaved, none.restart_required) == CfgBadge::clean);
}

// ===================================================================================== §5.2/§5.3 — navigation

TEST_CASE("chrome-nav: an ORDINARY screen selects its own slot") {
    struct { Screen sc; NavSlot want; } k[] = {
        { Screen::status,   NavSlot::status   },
        { Screen::team,     NavSlot::team     },
        { Screen::inbox,    NavSlot::inbox    },
        { Screen::send,     NavSlot::send     },
        { Screen::settings, NavSlot::settings },
    };
    for (const auto& c : k) {
        UiState st{}; st.screen = c.sc;
        CHECK(ui_nav_slot(st, Emergency::idle) == c.want);
    }
    // `count` is the enum's BOUND, not a screen: fail closed rather than selecting a plausible wrong slot.
    UiState st{}; st.screen = Screen::count;
    CHECK(ui_nav_slot(st, Emergency::idle) == NavSlot::none);
}

TEST_CASE("chrome-nav: the rail describes the BODY, not the screen underneath it") {
    // §5.2 row 1 — Inbox list, Inbox DETAIL and `MESSAGE GONE` all read INBOX. The detail modal is only reachable
    // from the inbox screen, so the interesting half is that neither modal state loses the slot.
    for (InboxModal m : { InboxModal::closed, InboxModal::body, InboxModal::gone }) {
        UiState st{}; st.screen = Screen::inbox; st.detail = m;
        CHECK(ui_nav_slot(st, Emergency::idle) == NavSlot::inbox);
    }
    // §5.2 row 2 — BOTH compose kinds, and the send-RESULT phase of each.
    // ★★ THE DM COMPOSE MODAL IS OPENED FROM THE **TEAM** SCREEN, which is what makes this row load-bearing rather
    //    than cosmetic: without the modal precedence the rail would say TEAM while the panel shows a send.
    for (bool result : { false, true }) {
        UiState st{}; st.screen = Screen::team; st.compose = Compose::dm; st.compose_result = result;
        CHECK(ui_nav_slot(st, Emergency::idle) == NavSlot::send);
        CHECK(ui_nav_slot(st, Emergency::idle) != NavSlot::team);
        UiState ch{}; ch.screen = Screen::send; ch.compose = Compose::channel; ch.compose_result = result;
        CHECK(ui_nav_slot(ch, Emergency::idle) == NavSlot::send);
    }
    // ★★★★ WHAT THIS FILE DOES **NOT** CLAIM, AND THE CLAIM IT USED TO MAKE FALSELY (corrected at QG round 2).
    //      Round 1 looped over all 8 `DmState`s and all 9 `ChanState`s, DISCARDED the loop variable and rebuilt the
    //      SAME `UiState` each iteration, then claimed §11.1's *"every send-result state"* coverage. It proved the
    //      compose-result mapping seventeen times and outcome exhaustiveness zero times — a loop that cannot fail
    //      differently from its first iteration is not coverage.
    // ⇒ **QG OPTION (b) IS TAKEN, EXPLICITLY:** the mapping-level test stays, the exhaustive-coverage CLAIM is
    //   deleted, and outcome exhaustiveness rests where it is actually measured — the outcome-machine cases in
    //   `test/test_firmware_ui_model.cpp`: `:956` "the DM machine walks accepted -> waiting -> delivered" · `:965`
    //   "NO CONFIRM is reachable and a LATE ack upgrades it to DELIVERED" · `:974` "a DM refusal and a DM failure are
    //   terminal and distinct from no_key" · `:1005` "channel_remote_mint is handled explicitly and never claims
    //   PICKED UP" · `:1048` "a channel SEAL FAILURE is terminal and names its reason" · `:1082` "draining a DM
    //   request enters SUBMITTING, and only a DM does".
    // ★ WHAT IS PINNED HERE INSTEAD IS A REAL CROSS PRODUCT OF THE THREE THINGS THE MAPPING **DOES** CONSULT — the
    //   compose kind, the result phase, and the screen underneath — so every iteration can come out differently and
    //   the swapped-clause mutation (X19/X24) reddens it.
    for (Compose k : { Compose::dm, Compose::channel })
        for (bool result : { false, true })
            for (Screen under : { Screen::status, Screen::team, Screen::inbox, Screen::send, Screen::settings }) {
                UiState st{}; st.screen = under; st.compose = k; st.compose_result = result;
                CHECK(ui_nav_slot(st, Emergency::idle) == NavSlot::send);
            }
    // §5.2 row 3 — the settings editor. Both non-`closed` states, so a browse and an open value row agree.
    for (Settings sg : { Settings::browsing, Settings::editing }) {
        UiState st{}; st.screen = Screen::settings; st.settings = sg;
        CHECK(ui_nav_slot(st, Emergency::idle) == NavSlot::settings);
    }
}

TEST_CASE("chrome-nav: PRECEDENCE — compose OUTRANKS the inbox detail, because the RENDERER draws it that way") {
    // ★★★★ ROUND 1 HAD THIS BACKWARDS **AND PINNED THE WRONG ANSWER HERE**, which is why a full gate did not catch
    //      it: the assertion asserted INBOX and therefore ENFORCED the defect. It is the fifth instrument in this arc
    //      to do that. The authority is not this file's reading order — it is `draw_frame`
    //      (`src/firmware_ui.cpp:949-953`), which draws **emergency -> compose -> inbox detail -> screen**, each arm
    //      returning. §5.2: *"the rail must describe the body ACTUALLY BEING SHOWN."*
    // ⇒ with BOTH open, the panel shows COMPOSE, so the rail must say SEND.
    for (InboxModal m : { InboxModal::body, InboxModal::gone })
        for (Compose k : { Compose::dm, Compose::channel }) {
            UiState both{}; both.screen = Screen::inbox; both.detail = m; both.compose = k;
            CHECK(ui_nav_slot(both, Emergency::idle) == NavSlot::send);
            CHECK(ui_nav_slot(both, Emergency::idle) != NavSlot::inbox);   // the round-1 answer, named so it cannot return
        }
    // The CONTROL that gives the case above its meaning: with compose CLOSED the same detail states read INBOX, so
    // the assertion above is about precedence and not about the detail clause having been broken.
    for (InboxModal m : { InboxModal::body, InboxModal::gone }) {
        UiState st{}; st.screen = Screen::inbox; st.detail = m; st.compose = Compose::none;
        CHECK(ui_nav_slot(st, Emergency::idle) == NavSlot::inbox);
    }
    // ⓘ THE DEFENSIVE-CLAUSE CONTRACT TEST, LABELLED AS ONE. For every state the MODEL can reach today the inbox
    //   detail clause is REDUNDANT — the modal only opens from `Screen::inbox`, so the screen mapping would answer
    //   `inbox` anyway. ⇒ this is the only state that can redden a mutation of that clause (X21), and it is a legal
    //   `UiState` that the model does not produce. ⛔ It is NOT a claim that a user can reach this.
    UiState off{}; off.screen = Screen::team; off.detail = InboxModal::body;
    CHECK(ui_nav_slot(off, Emergency::idle) == NavSlot::inbox);
}

TEST_CASE("chrome-nav: a REAL outcome landing on a live compose modal leaves the rail on SEND") {
    // ⚠ THIS IS A REACHABILITY CONTROL, NOT A COVERAGE CLAIM (see the note above): ONE genuine transition driven
    //   through `UiModel` — gesture -> compose modal -> drained send request -> a real correlated outcome — so the
    //   mapping is exercised against states the machine actually produced, rather than against a hand-built struct.
    UiModel m; UiSnapshot s = chrome_snap();
    s.team_id = 7; s.team_total = 3; s.team_shown = 3;
    for (uint8_t i = 0; i < 3; ++i) { s.team[i].id = uint8_t(10 + i); s.team[i].last_heard_s = 60; }
    m.on_gesture(Gesture::short_press, s);                       // STATUS -> TEAM
    if (m.state().screen != Screen::team) { CHECK(false); return; }
    m.on_gesture(Gesture::double_press, s);                      // §UI-17 S1: ENTER the interactive TEAM list...
    m.on_gesture(Gesture::double_press, s);                      // ...and open the DM compose sub-view
    if (m.state().compose != Compose::dm) { CHECK(false); return; }
    CHECK(ui_nav_slot(m.state(), m.emergency()) == NavSlot::send);
    CHECK(m.state().screen == Screen::team);                     // …while the SCREEN underneath is still TEAM

    m.on_gesture(Gesture::double_press, s);                      // send the highlighted canned text
    SendReq req{};
    const bool took = m.take_send_request(req);                  // ⚠ DRAINS — one call, into a local ([[B70]])
    CHECK(took);
    CHECK(m.dm_state() == DmState::submitting);
    CHECK(ui_nav_slot(m.state(), m.emergency()) == NavSlot::send);

    m.on_send_accepted(SendKind::dm, s.now_ms);                  // the core accepted it and minted a ctr
    CHECK(ui_nav_slot(m.state(), m.emergency()) == NavSlot::send);
    m.on_outcome(SendOutcome::dm_acked(), s.now_ms + 100);       // a real terminal outcome
    CHECK(m.dm_state() == DmState::delivered);
    CHECK(ui_nav_slot(m.state(), m.emergency()) == NavSlot::send);   // the BODY is still the compose result
}

TEST_CASE("chrome-nav: EMERGENCY suppresses the rail and selects NO slot, from every screen and every modal") {
    for (Emergency e : { Emergency::arming, Emergency::firing, Emergency::blocked, Emergency::picked_up,
                         Emergency::not_heard, Emergency::reply, Emergency::cancelled, Emergency::failed }) {
        CHECK(ui_rail_visible(e) == false);
        for (Screen sc : { Screen::status, Screen::team, Screen::inbox, Screen::send, Screen::settings }) {
            UiState st{}; st.screen = sc;
            CHECK(ui_nav_slot(st, e) == NavSlot::none);
        }
        UiState modal{}; modal.screen = Screen::inbox; modal.detail = InboxModal::body;
        CHECK(ui_nav_slot(modal, e) == NavSlot::none);
        // …and the PROJECTION reports it, with both rail fields normalised to nothing.
        UiSnapshot s = chrome_snap(); s.team_build = true;
        const UiChrome ch = chrome_of(s, modal, e);
        CHECK(ch.rail_visible == false);
        CHECK(ch.nav == NavSlot::none);
        CHECK(ch.slots == 0);
    }
    // The positive control: `idle` is the ONLY state that draws the rail.
    CHECK(ui_rail_visible(Emergency::idle) == true);
    UiState st{}; st.screen = Screen::inbox;
    CHECK(ui_nav_slot(st, Emergency::idle) == NavSlot::inbox);
}

// ★★★★ §CHROME-4 — THE ENUMERATOR ORDER IS §3.2's TABLE ORDER, AND THE RENDERER DEPENDS ON IT ARITHMETICALLY.
//      `draw_rail` (`src/firmware_ui.cpp`) walks its five 10-px slots as `NavSlot(i + 1)` for `i = 0..4` and places
//      slot `i` at `y = 10 + 10i`. ⇒ reordering these enumerators SILENTLY MOVES ICONS on a panel no automated gate
//      compiles, and nothing else in the tree would notice: `slot_bit` derives from the same value, so the mask would
//      move with them and stay self-consistent. This case is what turns that coupling into a build-time fact.
// ⓘ Design §3.2's table: 1 STATUS · 2 TEAM · 3 INBOX · 4 SEND · 5 SETTINGS.
TEST_CASE("chrome-nav: the rail's enumerator order IS §3.2's slot order, which the renderer indexes by") {
    CHECK(uint8_t(NavSlot::none)     == 0);
    CHECK(uint8_t(NavSlot::status)   == 1);
    CHECK(uint8_t(NavSlot::team)     == 2);
    CHECK(uint8_t(NavSlot::inbox)    == 3);
    CHECK(uint8_t(NavSlot::send)     == 4);
    CHECK(uint8_t(NavSlot::settings) == 5);
    // ★ AND THE ROUND TRIP THE RENDERER ACTUALLY PERFORMS: `NavSlot(i + 1)` must reproduce the table in order, and
    //   each one's mask bit must be the bit for that same slot — so an unavailable slot can only ever blank ITSELF.
    const NavSlot want[5] = { NavSlot::status, NavSlot::team, NavSlot::inbox, NavSlot::send, NavSlot::settings };
    for (uint8_t i = 0; i < 5; ++i) {
        CHECK(NavSlot(i + 1) == want[i]);
        CHECK(slot_bit(NavSlot(i + 1)) == uint8_t(1u << i));
    }
}

TEST_CASE("chrome-nav: a NON-TEAM build exposes no TEAM and no SEND slot") {
    UiSnapshot team = chrome_snap(); team.team_build = true;
    const UiChrome ct = chrome_of(team);
    CHECK(ct.rail_visible == true);
    CHECK((ct.slots & slot_bit(NavSlot::status))   != 0);
    CHECK((ct.slots & slot_bit(NavSlot::inbox))    != 0);
    CHECK((ct.slots & slot_bit(NavSlot::settings)) != 0);
    CHECK((ct.slots & slot_bit(NavSlot::team))     != 0);
    CHECK((ct.slots & slot_bit(NavSlot::send))     != 0);

    // `gateway_heltec` is a REAL build with OLED=1 and TEAM=0 — §3.2: those slots stay EMPTY and the remaining
    // icons keep the same locations rather than acquiring a second layout.
    UiSnapshot bare = chrome_snap(); bare.team_build = false;
    const UiChrome cb = chrome_of(bare);
    CHECK((cb.slots & slot_bit(NavSlot::team)) == 0);
    CHECK((cb.slots & slot_bit(NavSlot::send)) == 0);
    CHECK((cb.slots & slot_bit(NavSlot::status))   != 0);
    CHECK((cb.slots & slot_bit(NavSlot::inbox))    != 0);
    CHECK((cb.slots & slot_bit(NavSlot::settings)) != 0);
    CHECK(cb.slots != ct.slots);        // …and the two masks are not accidentally the same value

    // The bits are DERIVED from the enumerator, so no two slots share one and `none` claims none.
    CHECK(slot_bit(NavSlot::none) == 0);
    uint8_t seen = 0;
    for (NavSlot s : { NavSlot::status, NavSlot::team, NavSlot::inbox, NavSlot::send, NavSlot::settings }) {
        CHECK((seen & slot_bit(s)) == 0);
        seen = uint8_t(seen | slot_bit(s));
    }
    CHECK(seen == 0x1F);
}

TEST_CASE("chrome-nav: the mapping tracks the LIVE model, not a renderer-local cursor") {
    // A reachability control for the table-driven cases above: the same mapping, driven through real gestures.
    UiModel m; UiSnapshot s = chrome_snap(); s.team_id = 7; s.team_total = 3; s.team_shown = 3;
    for (uint8_t i = 0; i < 3; ++i) { s.team[i].id = uint8_t(10 + i); s.team[i].last_heard_s = 60; }
    CHECK(ui_nav_slot(m.state(), m.emergency()) == NavSlot::status);
    m.on_gesture(Gesture::short_press, s);
    CHECK(m.state().screen == Screen::team);
    CHECK(ui_nav_slot(m.state(), m.emergency()) == NavSlot::team);
    // §5.3 through the real machine: arming an alarm suppresses the rail without any screen transition.
    m.on_gesture(Gesture::long_arm, s);
    CHECK(m.emergency() != Emergency::idle);
    CHECK(m.state().screen == Screen::team);          // the SCREEN did not move …
    CHECK(ui_rail_visible(m.emergency()) == false);   // … but the rail is gone
    CHECK(ui_nav_slot(m.state(), m.emergency()) == NavSlot::none);
    m.on_gesture(Gesture::long_cancel, s);
}

// ===================================================================================== §8.2/§11.1 — equality

TEST_CASE("chrome-equality: visible chrome equality changes ONLY when the rendered output changes") {
    // ★★★★ THE CASE THAT CATCHES A `memcmp` OR A STRAY RAW FIELD. Each pair below renders IDENTICAL pixels while
    //      differing in the underlying authority, so a projection that carried the raw value — or an equality that
    //      compared the struct's bytes, padding included — would report a difference that is not on the panel. §8.3
    //      marks the model DIRTY on a chrome difference, so that is a repaint every tick, for ever.
    UiSnapshot a = chrome_snap(), b = chrome_snap();

    // (1) mail: 100 and 1998 both draw `99+`.
    a.unread_dm = 100; a.unread_ch = 0;
    b.unread_dm = 999; b.unread_ch = 999;
    CHECK(ui_chrome_equal(chrome_of(a), chrome_of(b)) == true);

    // (2) battery: 4 123 mV and 4 199 mV both draw `4.1V`.
    a = chrome_snap(); b = chrome_snap();
    a.batt_mv = 4123; b.batt_mv = 4199;
    CHECK(ui_chrome_equal(chrome_of(a), chrome_of(b)) == true);

    // (3) home age: 5 000 ms and 5 999 ms both draw `5s`.
    a = chrome_snap(); b = chrome_snap();
    a.mobile_build = b.mobile_build = true;
    a.home_confirmed_ever = b.home_confirmed_ever = true;
    a.home_confirm_age_ms = 5000; b.home_confirm_age_ms = 5999;
    CHECK(ui_chrome_equal(chrome_of(a), chrome_of(b)) == true);

    // (4) team: 10 rows and 200 rows both draw `9+`; and `team_shown` is not on the strip at all.
    a = chrome_snap(); b = chrome_snap();
    a.team_id = b.team_id = 5;
    a.team_total = 10; a.team_shown = 8;
    b.team_total = 200; b.team_shown = 3;
    CHECK(ui_chrome_equal(chrome_of(a), chrome_of(b)) == true);

    // (5) while the rail is SUPPRESSED, the slot mask is not on the panel either — so a team build and a non-team
    //     build with nothing else to show compare EQUAL under an emergency.
    a = chrome_snap(); b = chrome_snap();
    a.team_build = true; b.team_build = false;    // both `team_id == 0`, so the people slot reads `--` either way
    CHECK(ui_chrome_equal(chrome_of(a, UiState{}, Emergency::firing),
                          chrome_of(b, UiState{}, Emergency::firing)) == true);

    // ★ AND THE POSITIVE HALF, which is what stops "always equal" from passing: every visible field, moved one step,
    //   MUST be reported. A rule that never invalidates is as wrong as one that always does.
    const UiSnapshot base = [] { UiSnapshot s = chrome_snap();
        s.mobile_build = true; s.home_link = MESHROUTE_NS::Node::MobileHomeLink::confirmed;
        s.home_confirmed_ever = true; s.home_confirm_age_ms = 5000;
        s.team_id = 5; s.team_total = 3; s.batt_mv = 4100; s.unread_dm = 2; s.team_key_present = true;
        return s; }();
    const UiChrome ref = chrome_of(base);
    CHECK(ui_chrome_equal(ref, chrome_of(base)) == true);        // reflexive, against a separately built operand

    { UiSnapshot s = base; s.unread_ch = 1;              CHECK(ui_chrome_equal(ref, chrome_of(s)) == false); }
    { UiSnapshot s = base; s.unread_dm = 500;            CHECK(ui_chrome_equal(ref, chrome_of(s)) == false); }  // overflow flips
    { UiSnapshot s = base; s.home_link = MESHROUTE_NS::Node::MobileHomeLink::lost;
                                                         CHECK(ui_chrome_equal(ref, chrome_of(s)) == false); }
    { UiSnapshot s = base; s.home_confirm_age_ms = 65000;CHECK(ui_chrome_equal(ref, chrome_of(s)) == false); }  // 5s -> 1m
    { UiSnapshot s = base; s.home_confirmed_ever = false;CHECK(ui_chrome_equal(ref, chrome_of(s)) == false); }
    { UiSnapshot s = base; s.mobile_build = false;       CHECK(ui_chrome_equal(ref, chrome_of(s)) == false); }
    { UiSnapshot s = base; s.team_total = 4;             CHECK(ui_chrome_equal(ref, chrome_of(s)) == false); }
    { UiSnapshot s = base; s.team_id = 0;                CHECK(ui_chrome_equal(ref, chrome_of(s)) == false); }
    { UiSnapshot s = base; s.team_key_present = false;   CHECK(ui_chrome_equal(ref, chrome_of(s)) == false); }
    { UiSnapshot s = base; s.batt_mv = 4200;             CHECK(ui_chrome_equal(ref, chrome_of(s)) == false); }
    { UiSnapshot s = base; s.batt_mv = -1;               CHECK(ui_chrome_equal(ref, chrome_of(s)) == false); }
    { UiSnapshot s = base; s.team_build = false;         CHECK(ui_chrome_equal(ref, chrome_of(s)) == false); }  // slots move
    { ChromeCfg cfg{}; cfg.unsaved = true;
      CHECK(ui_chrome_equal(ref, chrome_of(base, UiState{}, Emergency::idle, cfg)) == false); }
    { UiState st{}; st.screen = Screen::inbox;
      CHECK(ui_chrome_equal(ref, chrome_of(base, st)) == false); }                                   // nav moves
    CHECK(ui_chrome_equal(ref, chrome_of(base, UiState{}, Emergency::firing)) == false);             // rail goes
}

TEST_CASE("chrome-equality: two chromes with the SAME fields and DIFFERENT padding compare EQUAL") {
    // ★★★★ THE `memcmp` CASE, AND IT HAD TO BE BUILT THIS WAY TO BE ABLE TO FAIL. §8.2 forbids `memcmp` over
    //      `UiChrome` because the struct has padding and padding bytes are INDETERMINATE — but two chromes built
    //      the ordinary way BOTH come from `UiChrome c{}`, which zero-initialises padding too, so a `memcmp`
    //      equality would agree with every other case in this file. That is exactly the instrument-that-cannot-fail
    //      shape, so the second operand is constructed over DELIBERATELY POISONED storage: placement-new
    //      default-initialisation runs the member initialisers and leaves the padding alone.
    // ⚠ Consequence if this were allowed to regress: §8.3 marks the model dirty on a chrome difference, so a
    //   `memcmp` would repaint the panel every tick for a projection that never changed.
    UiSnapshot s = chrome_snap();
    s.mobile_build = true; s.home_link = MESHROUTE_NS::Node::MobileHomeLink::confirmed;
    s.home_confirmed_ever = true; s.home_confirm_age_ms = 5000;
    s.team_id = 5; s.team_total = 3; s.batt_mv = 4100; s.unread_dm = 2; s.team_key_present = true;
    const UiChrome ref = chrome_of(s);

    alignas(UiChrome) unsigned char buf[sizeof(UiChrome)];
    std::memset(buf, 0x5A, sizeof buf);
    UiChrome* p = ::new (static_cast<void*>(buf)) UiChrome;   // default-init: NSDMIs run, padding keeps 0x5A
    p->mail = ref.mail; p->mail_overflow = ref.mail_overflow;
    p->home = ref.home;
    for (std::size_t i = 0; i < kAgeTokenCap; ++i) p->home_age[i] = ref.home_age[i];
    p->team_configured = ref.team_configured; p->team_count = ref.team_count;
    p->team_overflow = ref.team_overflow; p->key = ref.key;
    p->batt_dv = ref.batt_dv; p->badge = ref.badge;
    p->rail_visible = ref.rail_visible; p->nav = ref.nav; p->slots = ref.slots;

    // THE PROPERTY: every VISIBLE field agrees, so the projections are equal and no repaint is owed.
    CHECK(ui_chrome_equal(ref, *p) == true);
    CHECK(ui_chrome_equal(*p, ref) == true);   // symmetric — a one-sided comparison is half a comparison
    // THE CONTROL, and without it the case above proves nothing: the two objects really do differ BYTEWISE, so a
    // `memcmp` equality would return false here while every pixel on the panel is identical.
    CHECK(std::memcmp(&ref, p, sizeof(UiChrome)) != 0);
    // …and the poisoned operand still reports a REAL difference, so it is not equal-to-everything.
    p->batt_dv = int16_t(ref.batt_dv + 1);
    CHECK(ui_chrome_equal(ref, *p) == false);
    p->~UiChrome();
}

TEST_CASE("chrome-equality: the age token is compared over its WHOLE capacity, with defined bytes") {
    // ⚠ `snprintf` alone leaves the tail of a shorter token untouched. If the formatters did not NUL-pad, a buffer
    //   that had held `59s` and then took `5s` would keep a stray `s`, and equality would depend on history.
    char tok[kAgeTokenCap];
    ui_fmt_home_age(tok, sizeof tok, true, 59000);
    CHECK(std::strcmp(tok, "59s") == 0);
    ui_fmt_home_age(tok, sizeof tok, true, 5000);          // the SAME buffer, a shorter token
    CHECK(std::strcmp(tok, "5s") == 0);
    for (std::size_t i = 2; i < kAgeTokenCap; ++i) CHECK(tok[i] == '\0');
    // The same for the other three formatters, whose tokens also vary in length.
    char m[8]; ui_fmt_mail(m, sizeof m, 99, true); CHECK(std::strcmp(m, "99+") == 0);
    ui_fmt_mail(m, sizeof m, 7, false);            CHECK(std::strcmp(m, "7") == 0);
    for (std::size_t i = 1; i < sizeof m; ++i) CHECK(m[i] == '\0');
    char t[8]; ui_fmt_team(t, sizeof t, true, 9, true);  CHECK(std::strcmp(t, "9+") == 0);
    ui_fmt_team(t, sizeof t, true, 3, false);           CHECK(std::strcmp(t, "3") == 0);
    for (std::size_t i = 1; i < sizeof t; ++i) CHECK(t[i] == '\0');
    char v[kVoltsTokenCap]; ui_fmt_batt(v, sizeof v, 41); CHECK(std::strcmp(v, "4.1V") == 0);
    ui_fmt_batt(v, sizeof v, -1);                         CHECK(std::strcmp(v, "--") == 0);
    for (std::size_t i = 2; i < kVoltsTokenCap; ++i) CHECK(v[i] == '\0');
}

// ======================================================= §8.3 / §8.3.1 — THE REPAINT INVALIDATION
//
// ★★★★ THESE ARE THE CASES §CHROME-3 IS JUDGED ON, AND THEY EXIST HERE RATHER THAN IN A RENDERER PROBE FOR ONE
//      MEASURABLE REASON: `dirty` is private to `UiModel`, so its VALUE — the thing §8.3.1 rules on — can be read
//      only from a native case driving the real model. `tools/probe_firmware_ui`'s P13 measures every OBSERVABLE
//      consequence (no frame, no bus command, no unblank, no moved attention clock); the bit itself is measured here.
//
// ⛔⛔ AND THE INSTRUCTION THAT IS **NOT** IMPLEMENTED IS PINNED AS SUCH. An earlier §8.3.1 required a blanked chrome
//     change to mark the model CLEAN. It was WITHDRAWN because it would clear a dirty bit while dark and erase a
//     legitimate pending redraw (§B107's survival rule). The case below therefore asserts the OPPOSITE, and mutation
//     X27 installs the withdrawn instruction and must turn it RED — so the withdrawal cannot be quietly re-adopted.

// Page the rest of a frame out. ⓘ Local rather than shared with `test_firmware_ui_model.cpp`'s `page_out`: that one
// takes a snapshot it does not need here, and the two suites are separate translation units.
static void page_out_pages(FrameGate& g, UiModel& m, UiInboxCounters& c, int pages) {
    for (int i = 0; i < pages; ++i) g.on_page(i + 1 < pages, m, c);
}

// Bring a fresh model to LIT + CLEAN, the way the device does: one complete frame consumes the boot invalidation.
static void chrome_settle(UiModel& m, FrameGate& g, UiInboxCounters& c, const UiSnapshot& s) {
    CHECK(g.step(m, s, /*mac_idle=*/true) == FrameStep::open);   // the boot frame — the model starts dirty
    g.on_page(false, m, c);                                      // ...and it pages all the way out
    CHECK(m.state().dirty == false);
}

TEST_CASE("chrome-invalidate: LIT + CLEAN + a visible change ⇒ DIRTY (§8.3.1's positive half)") {
    UiModel m; FrameGate g; UiInboxCounters c{};
    UiSnapshot s = chrome_snap();
    chrome_settle(m, g, c, s);
    const UiChrome frozen = chrome_of(s, m.state(), m.emergency());
    // A snapshot-only fact moves with NO gesture and NO push — a team route arriving on a beacon is the design's own
    // example. Nothing else can mark the model dirty, and `FrameGate::step` answers `idle` for ever on a clean model.
    UiSnapshot s2 = s; s2.team_id = 0x1234u; s2.team_total = 4;
    const UiChrome live = chrome_of(s2, m.state(), m.emergency());
    CHECK(ui_chrome_equal(live, frozen) == false);
    CHECK(ui_chrome_invalidate(m, live, frozen) == true);
    CHECK(m.state().dirty == true);
    // ...and the panel really does paint it: the invalidation is what turns `idle` back into `open`.
    s.now_ms += kPaintThrottleMs;
    CHECK(g.step(m, s, true) == FrameStep::open);
}

TEST_CASE("chrome-invalidate: an EQUAL chrome raises nothing — and ⛔ CLEARS nothing") {
    UiModel m; FrameGate g; UiInboxCounters c{};
    UiSnapshot s = chrome_snap();
    chrome_settle(m, g, c, s);
    const UiChrome frozen = chrome_of(s, m.state(), m.emergency());
    CHECK(ui_chrome_invalidate(m, frozen, frozen) == false);
    CHECK(m.state().dirty == false);                 // nothing invented from an unchanged projection
    // ★★ THE WITHDRAWN INSTRUCTION'S EXACT HARM, PINNED. Something else has asked for a repaint (a push, a gesture,
    //    a blank); an equal chrome must leave that request ALONE. A rule that "tidies up" here erases a redraw the
    //    panel still owes — and it would look perfectly reasonable in review.
    m.mark_dirty();
    CHECK(ui_chrome_invalidate(m, frozen, frozen) == false);
    CHECK(m.state().dirty == true);                  // ⛔ NOT cleared
}

TEST_CASE("chrome-invalidate: an INVISIBLE change raises nothing (§11.1's last rule)") {
    UiModel m; FrameGate g; UiInboxCounters c{};
    UiSnapshot s = chrome_snap();
    s.unread_dm = 500; s.unread_ch = 500;            // combined 1000 -> the panel draws `99+`
    s.batt_mv = 4123;                                // -> 41 dV -> `4.1V`
    chrome_settle(m, g, c, s);
    const UiChrome frozen = chrome_of(s, m.state(), m.emergency());
    // Two snapshots that RENDER IDENTICALLY: a bigger combined mail count still clamped to `99+`, a battery reading
    // 76 mV higher still truncating to `4.1V`, and two fields the strip does not draw at all. Carrying raw values
    // instead of classified ones would make every one of these a repaint — per tick, for ever.
    UiSnapshot s2 = s;
    s2.unread_dm = 900; s2.batt_mv = 4199; s2.team_shown = 7; s2.last_dm_age_s = 42;
    const UiChrome live = chrome_of(s2, m.state(), m.emergency());
    CHECK(ui_chrome_equal(live, frozen) == true);
    CHECK(ui_chrome_invalidate(m, live, frozen) == false);
    CHECK(m.state().dirty == false);
}

TEST_CASE("chrome-invalidate: while BLANKED it never unblanks and never clears (§8.3.1 rules 1-2)") {
    UiModel m; FrameGate g; UiInboxCounters c{};
    UiSnapshot s = chrome_snap();
    chrome_settle(m, g, c, s);
    // Blank the panel the way the device does — the attention window expiring, not a poke at the state.
    m.on_tick(s);                                    // §B65: the first tick only SEEDS the blank timer
    s.now_ms += kBlankMs; m.on_tick(s);
    CHECK(m.state().blanked == true);
    CHECK(m.state().dirty   == true);                // blanking itself raises one (`FrameGate`'s §B107 note)
    CHECK(g.step(m, s, true) == FrameStep::blank);
    CHECK(g.frame_open()     == false);
    const UiChrome frozen = chrome_of(s, m.state(), m.emergency());
    UiSnapshot s2 = s; s2.team_id = 0x99u; s2.team_total = 2; s2.batt_mv = 3900;
    const UiChrome live = chrome_of(s2, m.state(), m.emergency());
    // Many ticks' worth of chrome changes while dark. Each RAISES (the bit is already set, so nothing observable
    // moves) and ⛔ not one of them may CLEAR it, unblank, or open a frame.
    for (int i = 0; i < 5; ++i) {
        CHECK(ui_chrome_invalidate(m, live, frozen) == true);
        CHECK(m.state().dirty   == true);            // ⛔ the withdrawn instruction would have cleared it here
        CHECK(m.state().blanked == true);            // ⛔ ...and a "show them the change" fix would have woken it
        CHECK(g.step(m, s, true) == FrameStep::blank);
        CHECK(g.frame_open()     == false);          // no frame, so light sleep is not inhibited either
    }
    // §8.3.1 RULE 3 — after the WAKE, the first frame freezes the CURRENT projection. The tick rebuilds `live` from
    // the current snapshot every pass, so the frame that opens here takes the NEW chrome, never the dark-captured one.
    s.now_ms += 1000;
    m.on_gesture(Gesture::short_press, s);           // the waking press is consumed by the model
    CHECK(m.state().blanked == false);
    CHECK(g.step(m, s, true) == FrameStep::open);
    const UiChrome at_freeze = chrome_of(s2, m.state(), m.emergency());
    CHECK(ui_chrome_equal(at_freeze, frozen) == false);
}

TEST_CASE("chrome-invalidate: a change under an OPEN frame is RETAINED for one follow-up frame") {
    UiModel m; FrameGate g; UiInboxCounters c{};
    UiSnapshot s = chrome_snap();
    chrome_settle(m, g, c, s);
    const UiChrome frozen = chrome_of(s, m.state(), m.emergency());
    // A frame opens and starts paging out. ⓘ `mark_dirty` stands in for whatever asked for it — a push, a gesture.
    m.mark_dirty();
    s.now_ms += kPaintThrottleMs;
    CHECK(g.step(m, s, true) == FrameStep::open);
    CHECK(m.state().dirty == false);                 // §B107: consumed AT THE FREEZE
    g.on_page(true, m, c);                           // page 0 of 8 is out
    UiSnapshot s2 = s; s2.batt_mv = 3900;
    const UiChrome live = chrome_of(s2, m.state(), m.emergency());
    // ★★ THE REFERENCE HAS NOT MOVED, because no NEW frame has frozen (§8.3 rule 4). ⇒ the change is raised on this
    //    tick AND on every tick until one does — which is what makes rule 5 ("retain dirty so ONE follow-up frame
    //    renders the newer projection") true rather than dependent on which tick the change landed on.
    for (int i = 0; i < 3; ++i) {
        CHECK(ui_chrome_invalidate(m, live, frozen) == true);
        CHECK(m.state().dirty == true);
    }
    page_out_pages(g, m, c, 7);
    s.now_ms += kPaintThrottleMs;
    CHECK(g.step(m, s, true) == FrameStep::open);     // the follow-up frame the retained bit bought
}

TEST_CASE("chrome-projection: an untouched snapshot reports NOTHING ESTABLISHED, not plausible values") {
    // ⛔ The five §CHROME-1 snapshot fields are DEFINED BUT NOT YET PUBLISHED (slice 3 wires `build_snapshot`), so
    //    this is the state the panel would show today. It must be the honest one: no home plane, no team, no
    //    battery, no mail — never a guess.
    const UiChrome c = chrome_of(UiSnapshot{});
    CHECK(c.mail == 0);
    CHECK(c.mail_overflow == false);
    CHECK(c.home == HomeIcon::blank);
    CHECK(c.home_age[0] == '\0');
    CHECK(c.team_configured == false);
    CHECK(c.key == KeyIcon::blank);
    CHECK(c.batt_dv == -1);
    CHECK(c.badge == CfgBadge::clean);
    // `UiSnapshot::team_build` defaults TRUE (it is the majority build), so an all-defaults snapshot still draws the
    // five rail slots — stated so the number is not read as an accident.
    CHECK(c.rail_visible == true);
    CHECK(c.nav == NavSlot::status);
    CHECK(c.slots == 0x1F);
}
