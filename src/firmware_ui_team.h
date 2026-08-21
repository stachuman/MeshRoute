// MeshRoute — src/firmware_ui_team.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §UI-17 slice S4 — THE TEAM ROW, AS PURE BYTES, AND THE BOUNDED CLOCK-DRIVEN REPAINT THAT KEEPS IT TRUE. Every byte
// a teammate row draws is composed here (spec §3.2's ruled `%c%-6.6s %3s %4s %2s`, string-inventory S-11); the
// renderer (`src/firmware_ui.cpp`'s `draw_team_screen`) does nothing but place the result on a body row.
// Normative: docs/superpowers/specs/2026-08-20-ui17-navigation-status-team-redesign-spec.md §3 (identity, the row and
// its width proof, the honest age word, the two reserved columns) and §1.9 F-1/F-2/F-8.
//
// ★★★ WHY IT IS A FILE OF ITS OWN — the §B115 rule this cluster states nine times (`firmware_ui_model.h:102-104`):
//     **a string built in `firmware_ui.cpp` is a string no automated gate can read**, because that TU is compiled by
//     neither the native suite nor the simulator. ⇒ its own header AND its own mutation battery target
//     (`--target=uiteam`), because a battery is per-SOURCE-FILE: left inside the 2700-line model these rows would
//     have shared `model`'s entries and no column would have had an isolated control ([[B217]]'s shape).
//
// ★★ THE ROW, AND ITS WIDTH PROOF (spec §3.2, adopted verbatim from note §5.2):
//        `%c%-6.6s %3s %4s %2s`   ⇒   1 + 6 + 1 + 3 + 1 + 4 + 1 + 2 = **19 of 19**
//     marker · label · route age · distance · direction. The body is 19 columns at `x = 12` (`kBodyCols`), so the
//     row is exactly full at EVERY expansion — there is no slack for a sixth field, which is why hops left.
// ⚠ THE VISIBLE LABEL NARROWS FROM **9** COLUMNS TO **6** (`%-9.9s` -> `%-6.6s`), and that is a real legibility
//   change on a screen whose whole purpose is naming people (spec §3.1). It is the note's own ruled format and it
//   buys the two columns S5 fills. ⛔ It is PRESENTATION ONLY: `_team_sel_id` remains the sole send identity
//   (`firmware_ui_model.h`'s `activate`), and ⛔ nothing selects or sends by the displayed text.
//
// ⛔⛔ WHAT THIS ROW MAY NOT SAY, and both are rulings rather than preferences:
//   1. **ROUTE AGE / KNOWN AGE — ⛔ never "heard", "seen", "online", "connected" or "in contact"** (spec §3.3).
//      `TeamRow::last_heard_s` is computed from the PRIMARY ROUTE CANDIDATE's `last_seen_ms`
//      (`src/firmware_ui.cpp`'s `build_snapshot`), so on a multihop path it says SOMEBODY ELSE heard that teammate.
//      ⓘ The FIELD is still spelled `last_heard_s`; renaming it is a refactor of shipped code (C1) and belongs with
//        the two deletions named below. The WORD on the panel and in every comment here is the honest one.
//   2. **NOTHING PLAUSIBLE IN A COLUMN THAT HAS NO EVIDENCE.** `UINT32_MAX` renders `--` (the token's own "never"),
//      and the distance/direction columns stay BLANK until S5 — ⛔ never `0m`, never a retained old value.
//
// ⛔⛔ WHAT LEFT THE ROW WITH THIS SLICE, INVENTORIED SO NOTHING GOES SILENTLY (spec §1.9 F-1/F-2, the
//     [[meshroute-mark-done-vs-missing-in-code]] rule):
//   · **HOPS ARE GONE BY RULING.** The shipped row ended `%uh` and design §3.3 promised *"last-heard age, signal
//     quality and hops"*. The new columns are distance and bearing and there is no room for hops at 19 columns.
//     ⇒ `TeamRow::hops` is from this slice on **written and read by nothing**. The design doc needs a
//     correction-in-place; it is DRAFTED by this slice's report and ⛔ not edited by it (the design doc is QG-owned).
//   · **`TeamRow::score_q4` has never been on the panel at all** (spec §1.9 F-2): it is filled at the snapshot and
//     read by nothing in `src/`, `test/` or `tools/` — so design §3.3's "signal quality" was never rendered.
//   ⇒ ★ DELETING EITHER FIELD IS A **REFACTOR** AND MAY NOT RIDE A FEATURE SLICE (C1). Both are named at spec §10 so
//     a later slice can claim them, together with the two team-id spellings (F-6). ⛔ Until then they stay, and this
//     comment is what stops a reader from concluding the renderer merely forgot them.
//
// ⓘ WHAT IS NOT HERE YET, AND WHY (the same rule, forward-looking): the DISTANCE and DIRECTION columns are
//   **present and BLANK**. S5 adds `src/firmware_ui_geo.h`, the `TeamRow` peer-location fields and the projection
//   over `Node::peer_loc_find`; ⛔ nothing in S4 computes, requests or transmits a position, and rendering TEAM
//   creates no traffic of any kind (spec §3.4). The columns exist NOW so that S5 lands without moving a byte of
//   this row — which is exactly what the exact-byte cases and the probe's row assertions pin.
//
// ⓘ RESOURCE COST, **MEASURED** (spec §6): **zero RAM**. The strings are `.rodata`, the scratch line is the
//   renderer's existing stack buffer, and the invalidation below compares the frozen snapshot that already exists
//   (`s_frame_snap`) against the live one the tick already builds — no new state, no new timer, no new buffer.
//   `UiSnapshot` / `UiState` / `UiModel` / `TeamRow` are all byte-identical to S3's.
#pragma once
#include <cstddef>   // std::size_t
#include <cstdint>
#include <cstdio>    // snprintf — the row is formatted HERE so the native suite asserts VISIBLE BYTES
#include "firmware_ui_model.h"    // TeamRow / UiSnapshot / UiModel / Screen — the frozen facts and the dirty bit
#include "firmware_ui_chrome.h"   // ui_fmt_home_age (design §4.2's ruled age table) + ui_pad_token — REUSED, not forked

namespace mrui {

// ---- the row's column budget, stated as the numbers the cases assert against (spec §3.2) --------------------------
// ⛔ THE FIELD WIDTHS ARE NOT A SECOND SPELLING OF THE FORMAT: the format string below is the one authority for what
//    is drawn, and these constants are what the width proof and the invalidation are written against. A native case
//    asserts the composed row is exactly `kTeamRowCols` at its widest expansion, so the two cannot drift silently.
inline constexpr std::size_t kTeamRowCols   = 19;  // = kBodyCols; the renderer static_asserts its own width against it
inline constexpr std::size_t kTeamLabelCols = 6;   // `%-6.6s` — pads AND bounds (the clamp/clip difference, §7.1 r5)
inline constexpr std::size_t kTeamAgeCols   = 3;   // `%3s`  — `ui_fmt_home_age` is bounded to 3 BY CONSTRUCTION
inline constexpr std::size_t kTeamDistCols  = 4;   // `%4s`  — S5's distance token; BLANK in S4
inline constexpr std::size_t kTeamDirCols   = 2;   // `%2s`  — S5's eight-way octant; BLANK in S4
static_assert(1 + kTeamLabelCols + 1 + kTeamAgeCols + 1 + kTeamDistCols + 1 + kTeamDirCols == kTeamRowCols,
              "spec §3.2: the ruled TEAM row format must fill exactly the body's 19 columns");
// ★ THE AGE FIELD HAS NO PRECISION (`%3s`, ⛔ not `%3.3s`) AND IT IS STILL PROVABLY BOUNDED: `ui_fmt_home_age` emits
//   at most three characters by construction (`kAgeTokenCap` = 3 + NUL), which IS the width the format reserves.
//   ⓘ The shipped row used `%4.4s` — a bound that could never be reached — because the age formatter it predates
//     was unbounded (§CHROME-4 retired that one). The static_assert is what keeps this line honest if either moves.
static_assert(kAgeTokenCap == kTeamAgeCols + 1,
              "spec §3.2: the age column is the age TOKEN's own width — a wider token would push the row off");

// The scratch buffer a caller hands the formatter. ⚠ DELIBERATELY OVERSIZED vs the 19 visible columns, for the
// reason `kStatusLineCap` and `kLineCap` both state: every line here is `snprintf`, and `-Wformat-truncation=` is on
// under `-Wall` and GATE-BLOCKING in this project — it fires whenever GCC cannot PROVE the widest expansion fits.
inline constexpr std::size_t kTeamLineCap = 48;

// ★★ THE TWO RESERVED COLUMNS, NAMED RATHER THAN WRITTEN AS TWO BARE `""` AT THE CALL BELOW. S5 replaces these two
//    expressions with its tokens and ⛔ moves nothing else; naming them is what makes that a one-line change and
//    what lets a reader see that the blank is a DECISION (spec §3.4: no evidence ⇒ no column, ⛔ never `0m`).
inline constexpr const char* kTeamDistBlank = "";
inline constexpr const char* kTeamDirBlank  = "";

// ★★ THE ROUTE-AGE TOKEN. ⚠ A **DECLARED DUPLICATE** (the [[B224]] idiom) of `src/firmware_ui.cpp`'s one-line
//    `fmt_age` adapter, and it is declared rather than shared for two reasons: that function is a RENDERER-TU helper
//    the INBOX rows still use, and hoisting it out is a refactor of shipped code that may not ride a feature slice
//    (C1). ⛔ The two must move together when it lands. ★ THE TOKEN ITSELF IS ALREADY SHARED — `ui_fmt_home_age` is
//    design §4.2's ruled table (`--` / `Ns` / `Nm` / `Nh` / `Nd` / `old`), pinned boundary by boundary by
//    `test/test_firmware_ui_chrome.cpp` — which is the half that decides what the operator reads.
// ⓘ `UINT32_MAX` IS "UNKNOWN", and it reaches the formatter as `ever = false` ⇒ `--`. That is the value
//   `build_snapshot` publishes for a route with no candidate or a backwards clock; ⛔ it must never render as an age.
inline void ui_team_age_token(char* out, std::size_t cap, uint32_t age_s) {
    ui_fmt_home_age(out, cap, /*ever=*/age_s != UINT32_MAX, uint64_t(age_s) * 1000u);
}

// ★★★★ THE ROW. ⛔ ONE `snprintf`, and every decision it expresses is here rather than at the call site: a condition
//      written in `firmware_ui.cpp` is a condition no battery can attack.
// ★ THE MARKER IS THE CALLER'S ONE INPUT and it carries §B64's suppression: the renderer passes `false` for a
//   PASSIVE preview (nothing is picked) and while `team_pick_gone` stands (a `>` beside a teammate the model has
//   already refused to send to is the mis-send in display form). ⛔ This function does not re-derive that rule — it
//   is `UiState`'s, and `draw_team_screen` states it once for both row kinds.
// ⓘ `%-6.6s` PADS **AND** BOUNDS, which is the whole difference between a clamp and a clip: a 14-column
//   `kLabelCap` label can neither push the age off the row nor leave the columns ragged. §7.1 rule 5 forbids letting
//   the panel clip as a truncation policy, so the bound is expressed HERE where its meaning can be judged.
inline void ui_team_row(char* out, std::size_t cap, bool marked, const TeamRow& t) {
    char age[kAgeTokenCap];
    ui_team_age_token(age, sizeof age, t.last_heard_s);
    const int n = snprintf(out, cap, "%c%-6.6s %3s %4s %2s",
                           marked ? '>' : ' ', t.label, age, kTeamDistBlank, kTeamDirBlank);
    ui_pad_token(out, cap, (n < 0) ? 0u : std::size_t(n) + 1u);   // the neighbours' rule: the WHOLE buffer is defined
}

// ==================================================== §UI-17 S4 / spec §1.9 F-8 — THE BOUNDED CLOCK-DRIVEN REPAINT
//
// ★★★★ THE GAP THIS CLOSES IS **PRE-EXISTING**, not one this slice introduced (spec §1.9 F-8). A lit TEAM screen's
//      age column goes stale today: `FrameGate::step` answers `idle` on a clean model, and the ONLY invalidation in
//      the tree compares the **chrome** projection (`ui_chrome_invalidate`), which carries the strip and the rail and
//      ⛔ no per-row body token. So the panel sits showing `12s` for as long as nothing else asks for a paint.
//
// ★★★ IT IS THE §CHROME-3 IDIOM, DELIBERATELY — same shape, same file-level home, same one rule:
//      **IT RAISES, OR IT DOES NOTHING. ⛔ IT NEVER CLEARS.** (§8.3.1's WITHDRAWN instruction, recorded at
//      `firmware_ui_chrome.h`: clearing a dirty bit while dark ERASES A LEGITIMATE PENDING REDRAW.) ⛔ And it cannot
//      wake a dark panel: `FrameGate::step` tests `blanked` FIRST and never examines `dirty`, so while the panel is
//      off this costs one comparison and changes nothing observable — pinned by a case, ⛔ not asserted in prose.
// ⛔ NO NEW TIMER, NO NEW STATE, NO NEW RAM: the reference is the frozen snapshot the frame already keeps
//    (`s_frame_snap`) and the operand is the live snapshot the tick already builds. The 2 Hz throttle and the
//    MAC-idle gate inside `FrameGate::step` are still free to REFUSE the paint this asks for.
//
// ★★★★ WHAT IS COMPARED, AND WHY IT IS **NOT** THE RAW AGES. §8.2's own argument, one screen over: a projection must
//      change only when the RENDERED OUTPUT changes. Raw ages move every second, so comparing them would repaint a
//      panel that did not change — once per second, per row, for ever. ⇒ the comparison is over the values that map
//      **1:1 to the drawn tokens**:
//        · the AGE BUCKET (below) — one value per row, equal exactly when the age token's bytes are equal;
//        · the label's **DRAWN PREFIX**, `kTeamLabelCols` bytes — ⛔ not the whole 15-byte array: two names that
//          differ only past column 6 draw the same six characters, and repainting for that is the same defect in the
//          other direction. ⓘ The id is deliberately NOT compared: it is not drawn, and two rows that resolve to
//          the same visible label ARE the same row as far as the panel is concerned;
//        · the number of rows shown, because that is how many the renderer draws.
//      ⛔ AND ⛔ NEVER A RE-FORMATTED STRING PER TICK: the bucket is an integer comparison, and its 1:1 agreement
//      with `ui_fmt_home_age`'s token is MEASURED by a native sweep rather than asserted here.
// ⓘ S5 EXTENDS THIS LIST with the distance token and the octant — the same rule, the same function; ⛔ it must not
//   add the raw coordinates or the raw location age.
//
// THE BUCKET. The top byte is the UNIT the token uses and the low bytes the count it prints, so two ages compare
// equal exactly when `ui_fmt_home_age` prints the same three characters. ⛔ It is a COMPARISON VALUE and is never
// rendered, never stored and never published — nothing may start reading it as a duration.
// ⚠ IT IS A SECOND EXPRESSION OF THE TOKEN'S BOUNDARIES, WHICH IS A DRIFT RISK, AND THE ANSWER IS MEASUREMENT, NOT
//   A COMMENT: `test/test_firmware_ui_team.cpp` sweeps consecutive seconds across every boundary and asserts
//   `bucket(a) == bucket(b)` **if and only if** the two tokens are byte-identical. A boundary moved on either side
//   fails that sweep.
inline constexpr uint32_t kTeamAgeUnknown = 0;            // `--` — its own bucket, ⛔ not "0 seconds"
inline uint32_t ui_team_age_bucket(uint32_t age_s) {
    if (age_s == UINT32_MAX) return kTeamAgeUnknown;      // the published "unknown", the token's `--`
    const uint32_t s = age_s;                             // `ui_fmt_home_age` divides age_ms by 1000 -> the same value
    if (s < 60u)  return 0x01000000u | s;
    const uint32_t m = s / 60u;
    if (m < 60u)  return 0x02000000u | m;
    const uint32_t h = m / 60u;
    if (h < 24u)  return 0x03000000u | h;
    const uint32_t d = h / 24u;
    if (d < 100u) return 0x04000000u | d;
    return 0x05000000u;                                   // `old` — every age past 100 days draws the same token
}

// ⓘ THE COMPARISON IS **POSITIONAL**, because the rows are drawn in the order the snapshot publishes them
//   (`rt_team_at` order, owner-ruled KEPT — spec §9 R-2). Two rosters holding the same teammates in a different
//   order draw a different panel and must repaint, which is exactly what indexing both sides by `i` says.
inline bool ui_team_rows_equal(const UiSnapshot& a, const UiSnapshot& b) {
    if (a.team_shown != b.team_shown) return false;
    for (uint8_t i = 0; i < a.team_shown; ++i) {
        if (ui_team_age_bucket(a.team[i].last_heard_s) != ui_team_age_bucket(b.team[i].last_heard_s)) return false;
        for (std::size_t c = 0; c < kTeamLabelCols; ++c)
            if (a.team[i].label[c] != b.team[i].label[c]) return false;
        // ⓘ A label SHORTER than the drawn width is NUL-padded (`build_snapshot` value-initialises the snapshot and
        //   `snprintf` terminates), so the loop above compares defined bytes for every reachable label.
    }
    return true;
}

// ★★★★ **ARE THESE ROWS ON THE PANEL AT ALL?** — and it is FOUR terms, not one, because three views REPLACE the
//      body from any screen. ⛔ THE ORDER AND THE TERMS MIRROR `draw_frame`'s OWN ARMS (`src/firmware_ui.cpp`:
//      emergency, then compose, then the inbox detail, then the screen switch), and that coupling is DECLARED: a
//      pure unit cannot call the renderer, so if that precedence ever changes THESE MOVE WITH IT.
// ⛔⛔ IT IS A DEFECT FOUND BY MEASUREMENT, NOT A TIDY-UP, AND THE MEASUREMENT IS WORTH KEEPING. A first cut gated on
//     `screen == Screen::team` ALONE — and a DM compose opened FROM the TEAM screen leaves `_st.screen` at `team`
//     (the rail boxes SEND by ruling §9 R-4, but the screen does not move). So while an operator composed a message,
//     rows nobody could see kept asking for paints: the probe's P9d caught a compose RESULT that never reached the
//     panel, because the extra frame consumed that 500 ms throttle window and the result's own frame was refused.
//     ⇒ an invalidation for a body that is not being drawn is not merely wasteful, it DISPLACES a paint that matters.
// ⓘ THE DETAIL TERM IS UNREACHABLE **THROUGH THE MODEL** TODAY ([[meshroute-mark-done-vs-missing-in-code]]): the
//   inbox detail modal can only be opened from INBOX, so no gesture sequence reaches `screen == team` with it open.
//   It is here because the RENDERER's precedence has it and the two must not be free to disagree — and because a
//   PURE predicate over its four inputs can be driven straight through that state by the native suite, which is
//   exactly the [[B223]] reason for keeping a decision where a case can reach it and a mutation can redden it.
inline bool ui_team_rows_visible(Screen screen, bool compose_open, InboxModal detail, Emergency emg) {
    return emg == Emergency::idle          // the alarm owns the body, from any screen
        && !compose_open                   // ...then a compose sub-view, which TEAM itself opens
        && detail == InboxModal::closed    // ...then the inbox detail modal
        && screen == Screen::team;         // ...and only then does the screen decide
}

// ⛔ THE VISIBILITY GATE IS PART OF THE RULE, NOT AN OPTIMISATION: an invalidation raised for rows nobody is looking
//    at asks for a repaint of something else's body (see the measured note above).
//    ⓘ It also bounds the cost to spec §6's figure — at most `kMaxTeamRows` (8) bucketed comparisons per tick, and
//    only while the TEAM rows are the body being drawn.
inline bool ui_team_invalidate(UiModel& m, const UiSnapshot& live, const UiSnapshot& frozen) {
    if (!ui_team_rows_visible(m.state().screen, m.compose_open(), m.state().detail, m.emergency()))
        return false;                                      // ⛔ NOTHING is cleared on this arm either
    if (ui_team_rows_equal(live, frozen)) return false;    // ⛔ nor on this one — an equal projection asks for nothing
    m.mark_dirty();
    return true;
}

}  // namespace mrui
