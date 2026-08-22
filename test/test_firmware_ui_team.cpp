// MeshRoute — test_firmware_ui_team.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN (test_airtime.cpp provides main()); -fno-exceptions => CHECK, never
//     REQUIRE — doctest implements REQUIRE's abort with a throw, so it does not compile in this build.
//
// §UI-17 slice S4 — the native suite for the TEAM row's pure unit (`src/firmware_ui_team.h`): spec §3.2's ruled
// `%c%-6.6s %3s %4s %2s` at every expansion, §3.3's honest route-age token, the two columns that are PRESENT and
// BLANK until S5, and §1.9 F-8's bounded clock-driven repaint.
//
// ★★★ WHY IT EXISTS AS ITS OWN SUITE — the same two reasons `test_firmware_ui_status.cpp` gives one screen over:
//     `src/firmware_ui.cpp` is compiled by NEITHER the native suite NOR the simulator (§B115), so a row composed
//     there is a row no gate can read; and a mutation battery is per-SOURCE-FILE, so its own file is what gives
//     `--target=uiteam` an isolated control for each column, the clamp, the bucket and the invalidation.
//
// ★★ EVERY CASE IS WRITTEN TO BE ABLE TO COME OUT OTHERWISE, and the exact-byte assertions are what make that true:
//    a row is asserted as its WHOLE 19 characters — trailing blanks included — because the two reserved columns are
//    the thing S5 must be able to fill WITHOUT MOVING A BYTE. A `strstr` of the label would pass against a row that
//    had silently dropped them.
#include "doctest.h"
#include "firmware_ui_team.h"
#include <cstdint>
#include <cstring>

using namespace mrui;

namespace {

// The scratch buffer the renderer hands the formatter (`kLineCap` there, `kTeamLineCap` here — the renderer
// static_asserts nothing smaller can reach it).
struct Row {
    char b[kTeamLineCap] = {};
    std::size_t cols() const { return std::strlen(b); }
};

// A published row, in the shape `build_snapshot` fills it: a resolved+clamped label and a route age in seconds.
// ⛔ `hops` / `score_q4` are deliberately left at their defaults and are NEVER passed in: this slice's whole point
//    is that no renderer reads them any more (spec §1.9 F-1/F-2).
TeamRow row_of(const char* label, uint32_t age_s) {
    TeamRow t{};
    std::snprintf(t.label, sizeof t.label, "%s", label);
    t.last_heard_s = age_s;
    return t;
}

// ★ §UI-17 S5 — THE DEFAULT FIXTURE HAS **NO OWN FIX AND NO CACHED PEER POSITION**, which is the honest majority
//   state of the device and is what keeps every S4 expectation below byte-for-byte true: no evidence ⇒ both location
//   columns blank. ⛔ It is not a convenience — the located rows are asserted in their own cases further down, and
//   `test/test_firmware_ui_geo.cpp` drives every term of the rule that decides between the two.
void fmt(Row& r, bool marked, const char* label, uint32_t age_s) {
    const TeamRow t = row_of(label, age_s);
    ui_team_row(r.b, sizeof r.b, marked, t, GeoFix{});
}

// ---- §UI-17 S5's fixture: our own fix, and a teammate at a stated offset from it ---------------------------------
// ⓘ ⛔ NOT `(0,0)`: that pair is the core's own "no fix at all", so a located fixture built on it would be driving a
//   position the device would have refused to publish. 52 N, 21 E — the same mid latitude the geo suite uses.
constexpr int32_t kOwnLat = 520000000;
constexpr int32_t kOwnLon = 210000000;
constexpr int32_t kKmLat  = 89832;                 // ~1 000 m of latitude, in 1e-7 degrees
const GeoFix kOwnFix{ /*have=*/true, kOwnLat, kOwnLon };

TeamRow located_row(const char* label, uint32_t age_s, int32_t dlat, int32_t dlon, uint32_t loc_age_s) {
    TeamRow t = row_of(label, age_s);
    t.peer_loc_valid = true;
    t.peer_lat_e7    = kOwnLat + dlat;
    t.peer_lon_e7    = kOwnLon + dlon;
    t.peer_loc_age_s = loc_age_s;
    return t;
}

// A snapshot carrying `n` teammate rows, all with the same age, labelled `id <10+i>`.
UiSnapshot team_snap(uint8_t n, uint32_t age_s) {
    UiSnapshot s{};
    s.now_ms     = 1000;
    s.batt_mv    = -1;
    s.team_build = true;
    s.team_id    = 0xABCD1234u;
    s.team_total = n;
    s.team_shown = n;
    for (uint8_t i = 0; i < n; ++i) {
        std::snprintf(s.team[i].label, sizeof s.team[i].label, "id %u", unsigned(10 + i));
        s.team[i].last_heard_s = age_s;
    }
    return s;
}

// Bring a fresh model to LIT + CLEAN **on the TEAM screen**, the way the device does: one real gesture walks
// STATUS -> TEAM (the model's own cycle), then one complete frame consumes every invalidation raised so far.
// ⛔ The screen is reached by a GESTURE, never by poking `UiState`: the invalidation is gated on the current screen,
//    and a poked screen would prove the gate against a state the model cannot actually be in.
void team_settle(UiModel& m, FrameGate& g, UiInboxCounters& c, const UiSnapshot& s) {
    m.on_tick(s);                                                // §B65: the first tick seeds the blank timer
    m.on_gesture(Gesture::short_press, s);                       // STATUS -> TEAM
    CHECK(m.state().screen == Screen::team);
    while (g.step(m, s, /*mac_idle=*/true) == FrameStep::open || g.frame_open()) g.on_page(false, m, c);
    CHECK(m.state().dirty   == false);
    CHECK(m.state().blanked == false);
}

}  // namespace

// ============================================================================== THE ROW — spec §3.2, string S-11

TEST_CASE("ui17-team: the ruled row is EXACTLY 19 columns, marker + label + age + the two blanks") {
    Row r;
    // The shipped fixture's own shape: a bare-id label (the resolver's last fallback) and a live route age.
    fmt(r, /*marked=*/false, "id 60", 12);
    CHECK(std::strcmp(r.b, " id 60  12s        ") == 0);
    CHECK(r.cols() == kTeamRowCols);
    // ★ THE MARKER IS THE ONLY DIFFERENCE between a picked row and a passive one — ⛔ not a second format, not a
    //   shifted column. §B64's suppression is the CALLER's rule (`draw_team_screen` passes `false` while
    //   `team_pick_gone` stands); what this unit owns is that a marked row moves nothing else on the line.
    fmt(r, /*marked=*/true, "id 60", 12);
    CHECK(std::strcmp(r.b, ">id 60  12s        ") == 0);
    CHECK(r.cols() == kTeamRowCols);
}

TEST_CASE("ui17-team: a long label is CLAMPED to six columns and cannot push the age off the row") {
    Row r;
    // ⛔ THE DEFECT THIS EXCLUDES is `%-6s` (a padding without a precision): it pads a SHORT label and lets a long
    //    one run, so a 14-column stored name would push the age, the distance and the direction off the panel and
    //    u8g2 would clip them silently. §7.1 rule 5 forbids exactly that as a truncation policy.
    fmt(r, false, "Wolfgangetta", 12);
    CHECK(std::strcmp(r.b, " Wolfga 12s        ") == 0);
    CHECK(r.cols() == kTeamRowCols);
    // The resolver's `0x<hash>` fallback is TEN columns and clamps the same way — six characters of a hex hash are
    // still a distinguishable label, which is why the ruled format can afford six.
    fmt(r, true, "0xdeadbeef", 12);
    CHECK(std::strcmp(r.b, ">0xdead 12s        ") == 0);
    CHECK(r.cols() == kTeamRowCols);
    // And the WIDEST label the snapshot can publish — `kLabelCap` characters — against the WIDEST age token.
    char widest[kLabelCap + 1];
    for (std::size_t i = 0; i < kLabelCap; ++i) widest[i] = 'W';
    widest[kLabelCap] = '\0';
    fmt(r, true, widest, 99u * 24u * 60u * 60u);              // -> `99d`
    CHECK(std::strcmp(r.b, ">WWWWWW 99d        ") == 0);
    CHECK(r.cols() == kTeamRowCols);
}

TEST_CASE("ui17-team: the label's own widths — a short name pads, a six-column one fills, none clips") {
    Row r;
    fmt(r, false, "Al", 5);
    CHECK(std::strcmp(r.b, " Al      5s        ") == 0);       // padded to six, age right-aligned in three
    CHECK(r.cols() == kTeamRowCols);
    fmt(r, false, "id 255", 5);                                // exactly six — the bare-id fallback at its widest
    CHECK(std::strcmp(r.b, " id 255  5s        ") == 0);
    CHECK(r.cols() == kTeamRowCols);
    // ⛔ AND AN EMPTY LABEL IS STILL A 19-COLUMN ROW rather than a collapsed line. `build_snapshot` cannot publish
    //    one (the resolver always writes something), which is exactly why the FORMAT's behaviour there is pinned
    //    rather than left to be discovered.
    fmt(r, false, "", 5);
    CHECK(std::strcmp(r.b, "         5s        ") == 0);
    CHECK(r.cols() == kTeamRowCols);
}

TEST_CASE("ui17-team: an UNKNOWN route age renders `--`, ⛔ never a plausible number") {
    Row r;
    // `build_snapshot` publishes `UINT32_MAX` for a route with no candidate and for a backwards clock. Rendering it
    // as an age — `4085d`, or worse `0s` — would be a claim about evidence that does not exist.
    fmt(r, false, "id 60", UINT32_MAX);
    CHECK(std::strcmp(r.b, " id 60   --        ") == 0);
    CHECK(r.cols() == kTeamRowCols);
    // ⓘ ...and `0s` is a REAL, DIFFERENT state: a route candidate seen this very second.
    fmt(r, false, "id 60", 0);
    CHECK(std::strcmp(r.b, " id 60   0s        ") == 0);
}

TEST_CASE("ui17-team: with NO location evidence the two columns are PRESENT and BLANK") {
    // ★★★ THIS IS THE PIN THAT MADE S4's "reserved columns" MEAN SOMETHING, and §UI-17 S5 kept every byte of it: the
    //     row's last eight characters are the separator + `%4s` + the separator + `%2s`, and with no own fix and no
    //     cached position they are still BLANK. A row that simply STOPPED after the age would satisfy every label
    //     and age assertion above — and S5 would have had to re-lay the line to fill them.
    //     ⛔ IT IS ALSO THE C2 RULE AT THE ROW LEVEL: no evidence ⇒ no column, ⛔ never `0m` (which is a real and
    //     different answer — see the coincident case below).
    Row r;
    const struct { const char* label; uint32_t age; } k[] = {
        { "id 60", 12 }, { "Wolfgangetta", 0 }, { "0xdeadbeef", UINT32_MAX }, { "id 255", 99u * 86400u },
    };
    bool tail_blank = true, width_ok = true;
    for (const auto& c : k) {
        fmt(r, false, c.label, c.age);
        if (r.cols() != kTeamRowCols) width_ok = false;
        for (std::size_t i = kTeamRowCols - (1 + kTeamDistCols + 1 + kTeamDirCols); i < kTeamRowCols; ++i)
            if (r.b[i] != ' ') tail_blank = false;
    }
    CHECK(tail_blank == true);
    CHECK(width_ok   == true);
    // ...and the columns are where the format says they are, derived from the widths rather than counted by hand.
    CHECK(1 + kTeamLabelCols + 1 + kTeamAgeCols + 1 + kTeamDistCols + 1 + kTeamDirCols == kTeamRowCols);
}

// ======================================================== §UI-17 S5 — THE TWO COLUMNS, FILLED, AT THE EXACT BYTES

TEST_CASE("ui17-team: a located teammate fills DIST and DIR, and the row is STILL exactly 19 columns") {
    // ★★★ THE HANDOFF THIS FILE OWNS: `firmware_ui_geo.h` decides what the two tokens SAY and this row decides where
    //     they GO. Asserting the whole 19 characters is what proves S5 filled the reserved columns without moving a
    //     byte of the label, the age or the separators.
    Row r;
    ui_team_row(r.b, sizeof r.b, /*marked=*/false, located_row("id 60", 12, /*dlat=*/184000, 0, /*loc_age=*/30),
                kOwnFix);
    CHECK(std::strcmp(r.b, " id 60  12s 2.0k  N") == 0);
    CHECK(r.cols() == kTeamRowCols);
    // ...a marked row moves the marker and ⛔ nothing else, exactly as it does without a location.
    ui_team_row(r.b, sizeof r.b, /*marked=*/true, located_row("id 60", 12, 184000, 0, 30), kOwnFix);
    CHECK(std::strcmp(r.b, ">id 60  12s 2.0k  N") == 0);
    // ...and the WIDEST expansion of every field at once — a `kLabelCap` label, `99d`, a four-column distance and a
    // two-column octant — is still 19. ⛔ This is the row's real width proof; the others are its corners.
    char widest[kLabelCap + 1];
    for (std::size_t i = 0; i < kLabelCap; ++i) widest[i] = 'W';
    widest[kLabelCap] = '\0';
    ui_team_row(r.b, sizeof r.b, true,
                located_row(widest, 99u * 86400u, /*dlat=*/-184000, /*dlon=*/-460000, 600), kOwnFix);
    CHECK(std::strcmp(r.b, ">WWWWWW 99d 3.7k SW") == 0);
    CHECK(r.cols() == kTeamRowCols);
}

TEST_CASE("ui17-team: a STALE cached position renders BLANK — ⛔ never a plausible number") {
    // ★★★★ THE C2 RULE, ON THE ROW ITSELF: the position is still there, the peer is still on the roster, and the
    //      panel says NOTHING about where they are. ⛔ A stale fix rendered as a current one is worse than no fix,
    //      because the operator acts on it. ⓘ The 600 s bound and its edges are the geo suite's; what this pins is
    //      that the ROW carries the decision through to the drawn bytes.
    Row r;
    ui_team_row(r.b, sizeof r.b, false, located_row("id 60", 12, 184000, 0, /*loc_age=*/600), kOwnFix);
    CHECK(std::strcmp(r.b, " id 60  12s 2.0k  N") == 0);         // 600 s — inside the bound, still shown
    ui_team_row(r.b, sizeof r.b, false, located_row("id 60", 12, 184000, 0, /*loc_age=*/601), kOwnFix);
    CHECK(std::strcmp(r.b, " id 60  12s        ") == 0);         // 601 s — BLANK, byte-identical to no evidence
    CHECK(r.cols() == kTeamRowCols);
    // ⛔ AND THE ROUTE AGE IS UNTOUCHED BY IT. The two ages are different facts with different bounds: the row stays,
    //    named and route-aged, and only the location columns go. (The bench step measures exactly this on glass.)
    ui_team_row(r.b, sizeof r.b, false, located_row("id 60", 12, 184000, 0, /*loc_age=*/0xFFFFFFFFu), kOwnFix);
    CHECK(std::strcmp(r.b, " id 60  12s        ") == 0);         // the cache's UNDATEABLE — blank, not `4085d`
}

TEST_CASE("ui17-team: without OUR OWN fix every row's columns are blank, however fresh the peer is") {
    // ⛔ A distance needs TWO positions. `own_fix` is the published answer of the one predicate the core's own
    //    located-send refusal is keyed on, so a row that drew a distance without it would contradict the thing that
    //    actually rejects us.
    Row r;
    ui_team_row(r.b, sizeof r.b, false, located_row("id 60", 12, 184000, 0, /*loc_age=*/0), GeoFix{});
    CHECK(std::strcmp(r.b, " id 60  12s        ") == 0);
    // ⓘ ...and the coordinates are still carried in the fix: `(0,0)` with `have == false` is "no position", never a
    //   place. A row must not start drawing distances from the Gulf of Guinea.
    ui_team_row(r.b, sizeof r.b, false, located_row("id 60", 12, 184000, 0, 0), GeoFix{ false, kOwnLat, kOwnLon });
    CHECK(std::strcmp(r.b, " id 60  12s        ") == 0);
}

TEST_CASE("ui17-team: a COINCIDENT teammate draws `0m` and a BLANK direction — ⛔ never `N`") {
    // ★★★★ THE RULING, AT THE BYTES (owner, 2026-08-20). Two nodes at one campsite: the distance is a fact and the
    //      bearing does not exist. ⛔ Octant 0 would be a fabricated cardinal — the exact class this screen refuses.
    Row r;
    ui_team_row(r.b, sizeof r.b, false, located_row("id 60", 12, /*dlat=*/0, /*dlon=*/0, /*loc_age=*/30), kOwnFix);
    CHECK(std::strcmp(r.b, " id 60  12s   0m   ") == 0);
    CHECK(r.cols() == kTeamRowCols);
    // ⛔ ...and it is NOT the same row as "no evidence": `0m` is an answer, blank is the absence of one.
    Row none;
    fmt(none, false, "id 60", 12);
    CHECK(std::strcmp(r.b, none.b) != 0);
}

TEST_CASE("ui17-team: the interactive list's `BACK` row fits beside these, in the ONE shipped spelling") {
    // The `BACK` row is drawn by `draw_team_screen`'s shared `body_back_row` (`%c%s`, spec §3.2's action-row idiom),
    // so what this suite owns is the WIDTH claim: 1 + 4 of 19, in the one spelling S-12 rules (⛔ no second word).
    CHECK(std::strcmp(kListBackText, "BACK") == 0);
    CHECK(1 + std::strlen(kListBackText) <= kTeamRowCols);
}

// ================================================================ THE AGE TOKEN AND ITS BUCKET — spec §3.3 / F-8

TEST_CASE("ui17-team: the route-age token is `ui_fmt_home_age`'s ruled table, `--` for unknown") {
    char tok[kAgeTokenCap];
    struct { uint32_t s; const char* want; } k[] = {
        { 0,           "0s"  }, { 59,          "59s" }, { 60,          "1m"  },
        { 3599,        "59m" }, { 3600,        "1h"  }, { 86399,       "23h" },
        { 86400,       "1d"  }, { 99u*86400u,  "99d" }, { 100u*86400u, "old" },
        { UINT32_MAX,  "--"  },
    };
    bool all_ok = true, bounded = true;
    for (const auto& c : k) {
        ui_team_age_token(tok, sizeof tok, c.s);
        if (std::strcmp(tok, c.want) != 0) all_ok = false;
        if (std::strlen(tok) > kTeamAgeCols) bounded = false;
    }
    CHECK(all_ok == true);
    CHECK(bounded == true);
    // ⚠ `UINT32_MAX` seconds is ~49 710 days, which the table would otherwise print as `old` — a plausible age for
    //   a teammate we have NO evidence about. The `ever = false` arm is what keeps the two apart.
    ui_team_age_token(tok, sizeof tok, UINT32_MAX);
    CHECK(std::strcmp(tok, "old") != 0);
}

TEST_CASE("ui17-team: the age BUCKET agrees with the drawn token, boundary for boundary, MEASURED") {
    // ★★★★ THE BUCKET IS A SECOND EXPRESSION OF THE TOKEN'S BOUNDARIES — a real drift risk — so the agreement is
    //      SWEPT rather than asserted. For every consecutive pair of ages: the buckets are equal IF AND ONLY IF the
    //      tokens are byte-identical. A boundary moved on either side (`< 60` -> `<= 60`, a unit dropped, the
    //      unknown arm folded into `0s`) breaks it in one direction or the other.
    // ⓘ The sweeps are consecutive across every boundary the token has, plus the two far ones sampled at their
    //   crossing: seconds->minutes at 60, minutes->hours at 3600, hours->days at 86 400, days->`old` at 100 days.
    struct Sweep { uint32_t from, to; };
    const Sweep sweeps[] = {
        {          0u,       3700u },   // `0s` .. `1h`, every second
        {      86300u,      86500u },   // the day crossing
        { 8639900u,      8640100u },    // 100 days -> `old`
    };
    bool iff_ok = true;
    int  turns  = 0;
    for (const auto& sw : sweeps) {
        char prev[kAgeTokenCap] = {}, cur[kAgeTokenCap] = {};
        uint32_t prev_b = 0;
        bool have_prev = false;
        for (uint32_t s = sw.from; s <= sw.to; ++s) {
            ui_team_age_token(cur, sizeof cur, s);
            const uint32_t b = ui_team_age_bucket(s);
            if (have_prev) {
                const bool same_tok = (std::strcmp(cur, prev) == 0);
                const bool same_b   = (b == prev_b);
                if (same_tok != same_b) iff_ok = false;
                if (!same_tok) ++turns;
            }
            std::memcpy(prev, cur, sizeof prev);
            prev_b = b; have_prev = true;
        }
    }
    CHECK(iff_ok == true);
    // ⛔ VACUITY GUARD: a sweep whose token never turned would satisfy the `iff` trivially. 0..3700 alone turns 61
    //    times (59 second steps + the minute steps), so a three-figure count is the honest floor.
    CHECK(turns > 100);
    // ...and the two states that must NEVER share a bucket, stated directly rather than left to the sweep.
    CHECK(ui_team_age_bucket(UINT32_MAX) != ui_team_age_bucket(0));      // `--` is not `0s`
    CHECK(ui_team_age_bucket(59)  != ui_team_age_bucket(60));            // `59s` is not `1m`
    CHECK(ui_team_age_bucket(3600) == ui_team_age_bucket(3659));         // both draw `1h` — ⛔ no repaint owed
}

// ================================================================ THE REPAINT INVALIDATION — spec §1.9 F-8 / S4

TEST_CASE("ui17-team: a lit TEAM screen repaints when an age TOKEN turns (the F-8 gap, closed)") {
    UiModel m; FrameGate g; UiInboxCounters c{};
    UiSnapshot s = team_snap(3, /*age_s=*/59);
    team_settle(m, g, c, s);
    const UiSnapshot frozen = s;                       // what the frame that just paged out froze
    // ⛔ NOTHING ELSE CAN ASK FOR THIS PAINT: no gesture, no push, and the chrome projection carries no per-row
    //    token — which is precisely why the panel sat on a stale `59s` before this slice.
    UiSnapshot live = s; live.team[0].last_heard_s = 60;          // `59s` -> `1m`
    CHECK(ui_team_rows_equal(live, frozen) == false);
    CHECK(ui_team_invalidate(m, live, frozen) == true);
    CHECK(m.state().dirty == true);
    // ...and the panel really does paint it: the invalidation is what turns `idle` back into `open`.
    live.now_ms += kPaintThrottleMs;
    CHECK(g.step(m, live, /*mac_idle=*/true) == FrameStep::open);
}

TEST_CASE("ui17-team: a RAW age that moves INSIDE its bucket raises nothing (⛔ never a repaint per second)") {
    UiModel m; FrameGate g; UiInboxCounters c{};
    UiSnapshot s = team_snap(3, /*age_s=*/3600);       // `1h`
    team_settle(m, g, c, s);
    const UiSnapshot frozen = s;
    // ★★ §8.2's argument, applied to the body: two ages that draw the SAME three characters are the same panel.
    //    Comparing raw seconds here would mark the model dirty on EVERY tick, for ever, on a screen that changed
    //    nothing — and it would look like a fix for the staleness above.
    UiSnapshot live = s;
    live.team[0].last_heard_s = 3659;                  // still `1h`
    live.team[1].last_heard_s = 3601;
    CHECK(ui_team_rows_equal(live, frozen) == true);
    CHECK(ui_team_invalidate(m, live, frozen) == false);
    CHECK(m.state().dirty == false);
    CHECK(g.step(m, live, /*mac_idle=*/true) == FrameStep::idle);
}

TEST_CASE("ui17-team: an EQUAL projection raises nothing — and ⛔ CLEARS nothing") {
    UiModel m; FrameGate g; UiInboxCounters c{};
    UiSnapshot s = team_snap(2, 30);
    team_settle(m, g, c, s);
    CHECK(ui_team_invalidate(m, s, s) == false);
    CHECK(m.state().dirty == false);
    // ★★ §8.3.1's WITHDRAWN instruction, pinned by its exact harm (the sibling chrome case says the same): something
    //    else has already asked for a repaint — a push, a gesture, a blank — and an equal projection must leave that
    //    request ALONE. A rule that "tidies up" here erases a redraw the panel still owes.
    m.mark_dirty();
    CHECK(ui_team_invalidate(m, s, s) == false);
    CHECK(m.state().dirty == true);                    // ⛔ NOT cleared
    // ...and it does not clear on the CHANGED arm either: that arm only ever raises.
    UiSnapshot live = s; live.team[0].last_heard_s = 3600;
    CHECK(ui_team_invalidate(m, live, s) == true);
    CHECK(m.state().dirty == true);
}

TEST_CASE("ui17-team: the rows belong to ONE screen — ⛔ no repaint is asked for from any other") {
    UiModel m; FrameGate g; UiInboxCounters c{};
    UiSnapshot s = team_snap(3, 59);
    team_settle(m, g, c, s);
    // Walk on to INBOX with a real press, then settle again: the rows are still published (the snapshot always
    // carries them), but nothing is drawing them.
    m.on_gesture(Gesture::short_press, s);
    CHECK(m.state().screen == Screen::inbox);
    // ⚠ THE CLOCK MUST MOVE PAST THE 2 Hz THROTTLE OR NOTHING PAINTS: `FrameGate::step` answers `idle` on a DIRTY
    //   model inside the throttle window, so a settle that did not advance time would leave the press's own
    //   invalidation standing and the check below would be measuring that instead of this rule.
    UiSnapshot frozen = s; frozen.now_ms += kPaintThrottleMs;
    while (g.step(m, frozen, true) == FrameStep::open || g.frame_open()) g.on_page(false, m, c);
    CHECK(m.state().dirty == false);
    UiSnapshot live = frozen; live.team[0].last_heard_s = 60;     // a token that WOULD have turned
    CHECK(ui_team_rows_equal(live, frozen) == false);             // the projection really did change ...
    CHECK(ui_team_invalidate(m, live, frozen) == false);          // ... and nobody is looking at it
    CHECK(m.state().dirty == false);
}

TEST_CASE("ui17-team: ...and to ONE BODY — a view that replaces it asks for no team repaint") {
    // ★★★★ FOUR TERMS, DRIVEN DIRECTLY, because three views REPLACE the body from any screen and `draw_frame`'s own
    //      order is emergency > compose > inbox detail > screen. ⛔ A `screen == team` test ALONE is not the rule:
    //      a DM compose opened FROM this screen leaves `_st.screen` at `team`, so rows nobody can see would keep
    //      asking for paints — and the probe measured the consequence, a compose RESULT whose frame was refused
    //      because the invalidation had spent that throttle window.
    // ⓘ Driven as a PURE predicate over its four inputs rather than through gestures, so the combinations the model
    //   cannot reach today (the inbox detail modal over TEAM) are still measured — [[B223]], for the seventh time.
    struct { Screen screen; bool compose; InboxModal detail; Emergency emg; bool want; } k[] = {
        { Screen::team,     false, InboxModal::closed, Emergency::idle,    true  },   // the ONE visible case
        { Screen::team,     true,  InboxModal::closed, Emergency::idle,    false },   // a compose from TEAM itself
        { Screen::team,     false, InboxModal::body,   Emergency::idle,    false },   // ⓘ unreachable today
        { Screen::team,     false, InboxModal::gone,   Emergency::idle,    false },
        { Screen::team,     false, InboxModal::closed, Emergency::arming,  false },   // the alarm owns the body
        { Screen::team,     false, InboxModal::closed, Emergency::reply,   false },
        { Screen::inbox,    false, InboxModal::closed, Emergency::idle,    false },
        { Screen::status,   false, InboxModal::closed, Emergency::idle,    false },
        { Screen::send,     false, InboxModal::closed, Emergency::idle,    false },
        { Screen::settings, false, InboxModal::closed, Emergency::idle,    false },
    };
    bool all_ok = true;
    for (const auto& t : k)
        if (ui_team_rows_visible(t.screen, t.compose, t.detail, t.emg) != t.want) all_ok = false;
    CHECK(all_ok == true);

    // ...and the invalidation really is wired to it, through the model's own state: open a DM compose from the
    // interactive TEAM list with real gestures and require silence.
    UiModel m; FrameGate g; UiInboxCounters c{};
    UiSnapshot s = team_snap(3, 59);
    team_settle(m, g, c, s);
    m.on_gesture(Gesture::double_press, s);      // enter the interactive list (the pick is row 0, by identity)
    m.on_tick(s);
    m.on_gesture(Gesture::double_press, s);      // ...and activate that teammate -> the DM compose sub-view
    CHECK(m.state().screen  == Screen::team);    // ⚠ the SCREEN has not moved — that is the whole trap
    CHECK(m.compose_open()  == true);
    m.clear_dirty();
    UiSnapshot live = s; live.team[0].last_heard_s = 60;          // a token that WOULD have turned
    CHECK(ui_team_invalidate(m, live, s) == false);
    CHECK(m.state().dirty == false);
}

TEST_CASE("ui17-team: while BLANKED it raises, never unblanks, never clears, never opens a frame") {
    UiModel m; FrameGate g; UiInboxCounters c{};
    UiSnapshot s = team_snap(3, 59);
    team_settle(m, g, c, s);
    // Blank the panel the way the device does — the attention window expiring, not a poke at the state.
    s.now_ms += kBlankMs; m.on_tick(s);
    CHECK(m.state().blanked == true);
    CHECK(g.step(m, s, true) == FrameStep::blank);
    const UiSnapshot frozen = s;
    UiSnapshot live = s;
    bool stayed_dark = true, never_opened = true;
    for (int i = 0; i < 5; ++i) {
        live.team[0].last_heard_s = uint32_t(60 + i * 60);        // `1m`, `2m`, `3m` ... every one a token turn
        CHECK(ui_team_invalidate(m, live, frozen) == true);
        if (!m.state().dirty || !m.state().blanked) stayed_dark = false;
        if (g.step(m, live, true) != FrameStep::blank || g.frame_open()) never_opened = false;
    }
    CHECK(stayed_dark  == true);      // ⛔ the withdrawn instruction would have cleared the bit here
    CHECK(never_opened == true);      // ⛔ ...and a "show them the change" fix would have woken the panel
    // ★★★★ AND THE COST THAT ACTUALLY MATTERS WHILE DARK IS **SLEEP**, so it is asserted rather than argued (F-10):
    //      `ui_allows_sleep` is `blanked && !input && !frame_open`, and a repaint request that opened a frame — or a
    //      wake — would take all three away. ⓘ It is measured HERE because the feature probe structurally cannot:
    //      `mr_ui_allows_sleep()` there is latched off for the whole run by P10h's fail-closed hardware case.
    InputFsm in;
    in.update(/*pressed=*/false, s.now_ms);
    CHECK(ui_allows_sleep(m, in, g) == true);
    // ⓘ AND THE RAISED INVALIDATION SURVIVES THE DARK (§B107): the waking press paints, it is not swallowed.
    s.now_ms += 1000;
    m.on_gesture(Gesture::short_press, s);
    CHECK(m.state().blanked == false);
    CHECK(g.step(m, s, true) == FrameStep::open);
}

// =========================================== §UI-17 S5 — THE INVALIDATION CARRIES THE TWO NEW COLUMNS (F-8 again)

TEST_CASE("ui17-team: a DISTANCE token that turns repaints a lit TEAM screen; a drift inside it does not") {
    UiModel m; FrameGate g; UiInboxCounters c{};
    UiSnapshot s = team_snap(3, /*age_s=*/3600);       // `1h` — the AGE column is parked far from its boundary
    s.own_fix = true; s.own_lat_e7 = kOwnLat; s.own_lon_e7 = kOwnLon;
    for (uint8_t i = 0; i < s.team_shown; ++i) {
        s.team[i].peer_loc_valid = true;
        s.team[i].peer_lat_e7    = kOwnLat + 184000;   // ~2.0 km north
        s.team[i].peer_lon_e7    = kOwnLon;
        s.team[i].peer_loc_age_s = 30;
    }
    team_settle(m, g, c, s);
    const UiSnapshot frozen = s;
    // ⛔ A PEER DRIFTING A FEW METRES — AND ITS CACHE SECOND TICKING BY — IS NOT A REPAINT. Both raw inputs moved and
    //    the drawn `2.0k N` did not; comparing either raw value would repaint this panel on every tick, for ever.
    UiSnapshot live = s;
    live.team[0].peer_lat_e7    += 270;                // ~3 m
    live.team[0].peer_loc_age_s  = 45;
    CHECK(ui_team_rows_equal(live, frozen) == true);
    CHECK(ui_team_invalidate(m, live, frozen) == false);
    CHECK(m.state().dirty == false);
    // ...and a move that CROSSES the token's boundary is a different panel, so it repaints.
    live.team[0].peer_lat_e7 = kOwnLat + 184000 + kKmLat / 2;     // `2.0k` -> `2.5k`
    CHECK(ui_team_rows_equal(live, frozen) == false);
    CHECK(ui_team_invalidate(m, live, frozen) == true);
    CHECK(m.state().dirty == true);
}

TEST_CASE("ui17-team: the OCTANT, the freshness bound and OUR OWN fix each repaint on their own") {
    UiModel m; FrameGate g; UiInboxCounters c{};
    UiSnapshot s = team_snap(2, 3600);
    s.own_fix = true; s.own_lat_e7 = kOwnLat; s.own_lon_e7 = kOwnLon;
    s.team[0].peer_loc_valid = true;
    s.team[0].peer_lat_e7    = kOwnLat + 184000;
    s.team[0].peer_lon_e7    = kOwnLon;
    s.team[0].peer_loc_age_s = 30;
    team_settle(m, g, c, s);
    // (a) the same distance in the opposite direction — `2.0k N` -> `2.0k S`.
    UiSnapshot dir = s; dir.team[0].peer_lat_e7 = kOwnLat - 184000;
    CHECK(ui_team_rows_equal(dir, s) == false);
    CHECK(ui_team_invalidate(m, dir, s) == true);
    m.clear_dirty();
    // (b) ★★★ THE POSITION GOING STALE IS A REPAINT, and it is the one a "nothing moved" reading would miss: no
    //     coordinate changed at all, the columns simply stop being true. ⛔ Without this the panel keeps showing a
    //     distance past the bound until something unrelated repaints it — precisely the defect the bound exists for.
    UiSnapshot stale = s; stale.team[0].peer_loc_age_s = 601;
    CHECK(ui_team_rows_equal(stale, s) == false);
    CHECK(ui_team_invalidate(m, stale, s) == true);
    m.clear_dirty();
    // ⓘ ...while an age moving INSIDE the bound is not (30 s -> 599 s, same columns, same panel).
    UiSnapshot fresher = s; fresher.team[0].peer_loc_age_s = 599;
    CHECK(ui_team_rows_equal(fresher, s) == true);
    CHECK(ui_team_invalidate(m, fresher, s) == false);
    CHECK(m.state().dirty == false);
    // (c) OUR OWN fix moving changes every row on the screen, so it repaints — and losing it blanks them all.
    UiSnapshot moved = s; moved.own_lat_e7 = kOwnLat + 184000;
    CHECK(ui_team_rows_equal(moved, s) == false);
    CHECK(ui_team_invalidate(m, moved, s) == true);
    m.clear_dirty();
    UiSnapshot lost = s; lost.own_fix = false;
    CHECK(ui_team_rows_equal(lost, s) == false);
    CHECK(ui_team_invalidate(m, lost, s) == true);
    CHECK(m.state().dirty == true);
}

TEST_CASE("ui17-team: the LABEL's drawn prefix is compared — a rename past column 6 is invisible") {
    UiModel m; FrameGate g; UiInboxCounters c{};
    UiSnapshot s = team_snap(2, 3600);
    std::snprintf(s.team[0].label, sizeof s.team[0].label, "%s", "Wolfgangetta");
    team_settle(m, g, c, s);
    const UiSnapshot frozen = s;
    // ⛔ §11.1's rule, in the direction that is easy to get wrong: both names draw `Wolfga`, so the panel did not
    //    change and no repaint is owed. Comparing the whole 15-byte array would repaint for a difference nobody
    //    can see — the same defect as comparing raw ages, wearing the other hat.
    UiSnapshot live = s;
    std::snprintf(live.team[0].label, sizeof live.team[0].label, "%s", "Wolfgastein");
    CHECK(ui_team_rows_equal(live, frozen) == true);
    CHECK(ui_team_invalidate(m, live, frozen) == false);
    CHECK(m.state().dirty == false);
    // ...and a rename that DOES reach the drawn six columns repaints.
    std::snprintf(live.team[0].label, sizeof live.team[0].label, "%s", "Wolfram");
    CHECK(ui_team_rows_equal(live, frozen) == false);
    CHECK(ui_team_invalidate(m, live, frozen) == true);
    CHECK(m.state().dirty == true);
}

TEST_CASE("ui17-team: the comparison is POSITIONAL and bounded by the rows the panel actually draws") {
    UiModel m; FrameGate g; UiInboxCounters c{};
    UiSnapshot s = team_snap(3, 3600);
    team_settle(m, g, c, s);
    // ★ ORDERING IS OWNER-RULED KEPT (spec §9 R-2) and the rows are drawn in `rt_team_at` order, so the SAME
    //   teammates in a DIFFERENT order are a DIFFERENT panel and must repaint.
    UiSnapshot swapped = s;
    swapped.team[0] = s.team[2];
    swapped.team[2] = s.team[0];
    CHECK(ui_team_rows_equal(swapped, s) == false);
    CHECK(ui_team_invalidate(m, swapped, s) == true);
    m.clear_dirty();
    // ⛔ AND A ROW BEYOND `team_shown` IS NOT DRAWN, so it may not ask for a paint: the snapshot's array is eight
    //    long whatever the roster holds, and comparing the tail would repaint for rows nobody can see.
    UiSnapshot tail = s;
    tail.team[5].last_heard_s = 12345;
    std::snprintf(tail.team[7].label, sizeof tail.team[7].label, "%s", "ghost");
    CHECK(ui_team_rows_equal(tail, s) == true);
    CHECK(ui_team_invalidate(m, tail, s) == false);
    CHECK(m.state().dirty == false);
    // ...while the NUMBER of drawn rows changing is a repaint by itself (a teammate joined or left the roster).
    UiSnapshot fewer = s; fewer.team_shown = 2;
    CHECK(ui_team_rows_equal(fewer, s) == false);
    CHECK(ui_team_invalidate(m, fewer, s) == true);
    CHECK(m.state().dirty == true);
}
