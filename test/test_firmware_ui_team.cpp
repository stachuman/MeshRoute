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

void fmt(Row& r, bool marked, const char* label, uint32_t age_s) {
    const TeamRow t = row_of(label, age_s);
    ui_team_row(r.b, sizeof r.b, marked, t);
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

TEST_CASE("ui17-team: the DISTANCE and DIRECTION columns are PRESENT and BLANK (S5 must not move a byte)") {
    // ★★★ THIS IS THE PIN THAT MAKES S4's "reserved columns" MEAN SOMETHING. The row's last eight characters are
    //     the separator + `%4s` + the separator + `%2s`, all blank in this slice. A row that simply STOPPED after
    //     the age would satisfy every label and age assertion above and would then force S5 to re-lay the line.
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
