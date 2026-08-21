// MeshRoute — src/firmware_ui_status.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §UI-17 slice S3 — THE STATUS BODY'S FIVE FACTS, AS PURE STRINGS. Every byte the STATUS screen draws is composed
// here: the five rows of spec §2.2, each substitution, and row 4's deterministic priority. The renderer
// (`src/firmware_ui.cpp`'s `draw_status_screen`) does nothing but place them beside the reserved 24x24 mark.
// Normative: docs/superpowers/specs/2026-08-20-ui17-navigation-status-team-redesign-spec.md §2 (geometry, the row
// table and its notes a-j, the string inventory S-1…S-10 and S-17), plus design doc §3.6.5 for row 4's priority.
//
// ★★★ WHY IT IS A FILE OF ITS OWN, and it is the §B115 rule this file cluster states nine times
//     (`firmware_ui_model.h:102-104`): **a string built in `firmware_ui.cpp` is a string no automated gate can
//     read** — that TU is compiled by neither the native suite nor the simulator. Five rows with nine
//     substitutions between them is exactly the shape that rots there. ⇒ its own header AND its own mutation
//     battery target (`--target=uistatus`), because a battery is per-SOURCE-FILE
//     (`tools/probe_ui_model_mutations.py`'s own header): left inside the 2700-line model these rows would have
//     shared `model`'s entries and none of the substitutions would have had an isolated control — the [[B217]]
//     shape this project keeps registering.
//
// ★★ THE GEOMETRY THESE STRINGS ARE BUDGETED AGAINST (spec §2.1, and it is the reason two widths exist):
//     the reserved mark slot is `x = 12..35, y = 12..35`, so **rows 0-2 draw at `x = 40`** — 88 px = **14
//     columns** — while **rows 3-4 draw at `x = 12`** and keep the body's full **19**. ⛔ The widths are not
//     enforced by a clamp here: §7.1 rule 5 forbids clipping as a truncation policy, and a blanket clamp would
//     make the probe's P14f an instrument that cannot fail. ⇒ every format's widest expansion is PROVEN by a
//     native case (`test/test_firmware_ui_status.cpp`) and MEASURED end to end by `tools/probe_firmware_ui`.
//
// ⛔⛔ THREE THINGS THIS BODY MAY NOT SAY, all three ruled:
//   1. **`KNOWN`, never `HEARD` and never `MEMBERS`** (spec §2.2 note d, string S-5, QA/owner-ruled 2026-08-20).
//      `rt_team_count()` is ROUTE EVIDENCE — on a multihop path it says somebody ELSE heard that teammate — and
//      the route table is not an authoritative membership roster either.
//   2. **NO CONFIGURATION TEXT** (§9 R-3, §CHROME-4, design §6). `CFG* UNSAVED` / `CFG! RELOAD` were removed from
//      STATUS on purpose and stay removed: the SETTINGS rail BADGE carries that state from every screen and
//      SETTINGS says the words. ⛔ Only `RESTART NEEDED` appears here, on row 4, as it does today.
//   3. **NO PLAUSIBLE SUBSTITUTE.** `(0,0)` is `NO LOCATION`, never `0.000,0.000`; a team-DAD that has not
//      happened is `ME NO ID`, never `ME T0`; a cache-less count is silence, never `0`.
//
// ⓘ RESOURCE COST, **MEASURED** (spec §6's estimate-until-measured rule). The strings are `.rodata` and the
//   scratch line is a stack local, so the formatters themselves cost ZERO RAM — but the frame-freeze fix below
//   does not: `UiSnapshot` gains `own_fix` + `own_lat_e7` + `own_lon_e7` and measures **608 -> 616** on the host
//   reveal (the bool is free in existing padding; the two `int32_t` cost their own 8). ⚠ `UiSnapshot` is
//   instantiated TWICE on the OLED envs (`s_frame_snap` plus the model's copy) and is additionally a per-tick
//   STACK local, so that is ~+16 B of static RAM and +8 B of loop-task stack. `UiState` / `UiModel` are unchanged.
//
// ⚠ WHAT LEFT THE PANEL WITH THIS SLICE, so nothing goes silently (spec §2.3, OWNER-ACCEPTED 2026-08-20):
//   the EXACT battery millivolts (`batt %ldmV` / `batt --`) — the strip's `4.1V` decivolt token and the console
//   keep it — and the PER-KIND newest-message age (`DM %u, newest %s` / `CH %u, newest %s`), whose per-kind split
//   survives on the INBOX screen and whose combined unread count is this row 3 and the strip's envelope.
//   ⛔ No slice may re-add a row to restore either, and neither is an oversight.
#pragma once
#include <cstddef>   // std::size_t
#include <cstdint>
#include <cstdio>    // snprintf — the rows are formatted HERE so the native suite asserts VISIBLE BYTES
#include "firmware_ui_model.h"    // UiSnapshot — the frozen facts; kCfgRestartText — the REUSED lexeme (S-17)
#include "firmware_ui_chrome.h"   // ui_fmt_mail / ui_fmt_home_age / ui_fmt_team / ui_pad_token — REUSED, not forked

namespace mrui {

// ---- the two budgets (spec §2.1), stated as the numbers the cases assert against ---------------------------------
// ⛔ DERIVED IN THE RENDERER, NEVER A SECOND LITERAL THERE: `src/firmware_ui.cpp` static_asserts its own pixel
//    arithmetic against both of these, so a mark slot that moved and a column budget that did not cannot coexist.
inline constexpr std::size_t kStatusNarrowCols = 14;   // rows 0-2 at x = 40 -> 88 px / 6 px per column
inline constexpr std::size_t kStatusWideCols   = 19;   // rows 3-4 at x = 12 -> 116 px, the body's own width

// The scratch buffer a caller hands these formatters. ⚠ DELIBERATELY OVERSIZED vs the 19 visible columns, for the
// reason `kLineCap` states next door and NOT as slack for its own sake: every line here is `snprintf`, and
// `-Wformat-truncation=` is on under `-Wall` and GATE-BLOCKING in this project — it fires whenever GCC cannot PROVE
// the widest expansion fits, and it cannot prove a bound on a `%ld` degree field. ⛔ Shrinking this to 20 would buy
// nothing (the margin costs stack, not flash) and would cost the warning census its pin.
inline constexpr std::size_t kStatusLineCap = 48;
// `9+` / `99+` / `--` + NUL — the widest token `ui_fmt_team` or `ui_fmt_mail` can hand back.
inline constexpr std::size_t kStatusCountCap = 4;

// ================================================================================== ROW 0 — WHICH TEAM (S-1 / S-2)
// ★★ `TEAM %08lX` IS A **THIRD SPELLING** OF THE TEAM ID AND IT IS DECLARED, NOT ACCIDENTAL (spec §2.2 note j, the
//    [[B224]] declared-duplication idiom). The existing pure token is `ui_fmt_team_id_full` = `0x%08lX`
//    (`firmware_ui_chrome.h`), and with the `TEAM ` prefix that is **15 columns — one past the 14 this row has at
//    `x = 40`**, so it cannot be reused here as-is. The shipped STATUS row already omitted the `0x`
//    (`team %08lx`); this slice only uppercases it, per the fingerprint's own uppercase rule.
// ⇒ ★ THE HONEST CURE IS A REFACTOR AND C1 FORBIDS IT RIDING A FEATURE SLICE: hoist the eight digits into one
//   `ui_fmt_team_id_hex8` that `ui_fmt_team_id_full` then composes. That is spec §1.9 **F-6**'s first deferred
//   refactor and it owns a slice of its own; ⛔ when it lands, THESE TWO MOVE TOGETHER.
// ⓘ `team_id == 0` is the CORE's own "we are not in a team" (`node.h:261`), read as the field's meaning rather
//   than reinterpreted — the same test `ui_chrome` makes one file over.
inline void ui_status_team(char* out, std::size_t cap, const UiSnapshot& s) {
    const int n = (s.team_id == 0) ? snprintf(out, cap, "NO TEAM")
                                   : snprintf(out, cap, "TEAM %08lX", (unsigned long)s.team_id);
    ui_pad_token(out, cap, (n < 0) ? 0u : std::size_t(n) + 1u);   // the neighbours' rule: the WHOLE buffer is defined
}

// ================================================================================ ROW 1 — WHICH LOCAL ID (S-3 / S-4)
// ★ ROW 1 IS **BLANK** WITH NO TEAM, NOT A SECOND `NO TEAM` (spec §2.2 note a — ⚠ REPORTED, NOT INVENTED). The note
//   this spec is built from says the ME row shows `NO TEAM` when there is none; rendering it on both rows would
//   spend two of five body rows on ONE fact. ⇒ row 0 owns the token, row 1 says nothing.
// ★★ `ME NO ID` IS A CASE THE NOTE DOES NOT COVER AND THE CODE MAKES REACHABLE (note b): `Node::team_local_id()`
//    documents **0 as "not team-DAD'd"** — and `firmware_ui_model.h`'s own team-cursor code relies on exactly that
//    — so an in-team node before DAD would otherwise render `ME T0`, ⛔ a PLAUSIBLE id for a node that has none.
inline void ui_status_me(char* out, std::size_t cap, const UiSnapshot& s) {
    if (s.team_id == 0) { ui_pad_token(out, cap, 0); return; }    // note a: row 0 already said it
    const int n = (s.my_team_id == 0) ? snprintf(out, cap, "ME NO ID")
                                      : snprintf(out, cap, "ME T%u", unsigned(s.my_team_id));
    ui_pad_token(out, cap, (n < 0) ? 0u : std::size_t(n) + 1u);
}

// ============================================================================ ROW 2 — HOW MANY, AND CAN WE READ THEM
// ★★★★ THE WORD IS `KNOWN` (S-5, QA/owner-ruled 2026-08-20), AND IT IS AN HONESTY FIX RATHER THAN A PREFERENCE.
//      ⛔ **WITHDRAWN WORDING, KEPT VISIBLE:** the design note and the spec's first draft both said `4 HEARD`. The
//      count is `rt_team_count()` — ROUTE EVIDENCE — and a multihop route says somebody ELSE heard that teammate,
//      which is exactly the "seen"/"heard" language spec §3.3 forbids for this quantity. ⛔ And not `MEMBERS`
//      either: the route table is not an authoritative membership roster.
// ★ THE VALUE IS `ui_fmt_team`'s — the STRIP's own already-clamped `0..9` / `9+` token (U1) — so the two surfaces
//   that draw this one number cannot disagree. ⛔ Never `team_shown` (the UI's 8-row capacity, the retired `T8/12`
//   fraction), always `team_total`.
// ★★ `NO TEAM KEY` OUTRANKS THE COUNT (note c) because it is the ACTIONABLE half: without the team CONTENT key the
//    routes are real but every post is unreadable, and `team_key_present` is that fact and no other.
// ⚠⚠ REPORTED, NOT INVENTED — THE ONE COMBINATION THE SPEC'S TABLE LEAVES OPEN, AND THE READING TAKEN.
//    §2.2's row-2 column names two substitutions (`!team_build` ⇒ empty, and `team_id != 0 && !team_key_present` ⇒
//    `NO TEAM KEY`) and is silent on **`team_build` true with `team_id == 0`** — a Heltec V3 that has the team
//    plane but is in no team. Rendering the count there means calling `ui_fmt_team(configured = false, …)`, whose
//    answer is `--` ⇒ the row would read **`-- KNOWN`**. ⇒ THIS ROW IS **EMPTY WHENEVER THE TEAM IS NOT
//    CONFIGURED**, for three reasons stated so an owner can reverse it in one line: (i) `ui_fmt_team`'s own note
//    says `--` means *"NO TEAM — which is not 'a team with zero teammates'"*, i.e. that token IS row 0's fact, and
//    note a forbids spending a second body row on one fact; (ii) note d describes the reused value as the
//    *"already-clamped 0..9/9+"* one and never the `--` arm; (iii) spec S3 pin 6 requires the non-team shape to
//    *"claim nothing"*. ⓘ `configured` is `ui_chrome`'s definition verbatim (`team_build && team_id != 0`), so the
//    strip and this row answer the same question with the same expression.
inline void ui_status_known(char* out, std::size_t cap, const UiSnapshot& s) {
    const bool configured = s.team_build && s.team_id != 0;
    if (!configured) { ui_pad_token(out, cap, 0); return; }
    int n;
    if (!s.team_key_present) {
        n = snprintf(out, cap, "NO TEAM KEY");
    } else {
        char tok[kStatusCountCap];
        const bool overflow = s.team_total > kTeamMax;
        ui_fmt_team(tok, sizeof tok, /*configured=*/true, overflow ? kTeamMax : s.team_total, overflow);
        n = snprintf(out, cap, "%s KNOWN", tok);
    }
    ui_pad_token(out, cap, (n < 0) ? 0u : std::size_t(n) + 1u);
}

// =========================================================================== ROW 3 — UNREAD, AND THE HOME (S-7/S-8)
// ★ THE SATURATION TOKEN IS `ui_fmt_mail`'s `99+` (note e), ⛔ NOT `kUnreadCap`'s 999: this row states the same
//   COMBINED count the strip's envelope draws, and one fact must have one token (U1). Widest expansion
//   `99+ NEW / HOME 59m` = **18** of 19, proven by a case.
// ⚠ THE THREE-LINE CLAMP BELOW IS A **DECLARED DUPLICATE** of `ui_chrome`'s §4.1 block, and it is declared rather
//   than shared because spec S3 rules `firmware_ui_chrome.h` **read-only for this slice** (hoisting the clamp into
//   it is a refactor of a probe-pinned file — C1). ⛔ The two must move together; the TOKEN itself is already
//   shared, which is the half that decides what the operator reads.
// ★★ `HOME --`, NOT `HOME UNKNOWN` (note f): `ui_fmt_home_age` is design §4.2's ruled table, is bounded to three
//    columns BY CONSTRUCTION and already renders `--` for "never confirmed" — while `99+ NEW / HOME UNKNOWN` is 22
//    columns and would clip. (S-15 is WITHDRAWN in the string inventory for exactly that reason.)
// ⛔⛔ AND ON A BUILD WITH NO MOBILE PLANE THE HOME HALF IS **OMITTED ENTIRELY** rather than rendered `--`. Design
//     §4.2's distinction between *"not applicable"* and *"never confirmed"* is already law for the strip's home
//     icon (`ui_chrome` blanks the slot on `!mobile_build`), and this row must not contradict the icon six pixels
//     above it. ⓘ `gateway_heltec` is a REAL `OLED=1 / MOBILE=0` build, so this is not hypothetical.
inline void ui_status_unread_home(char* out, std::size_t cap, const UiSnapshot& s) {
    const uint32_t mail_total = uint32_t(s.unread_dm) + uint32_t(s.unread_ch);
    const bool     overflow   = mail_total > uint32_t(kMailMax);
    char mail[kStatusCountCap];
    ui_fmt_mail(mail, sizeof mail, overflow ? kMailMax : uint8_t(mail_total), overflow);
    int n;
    if (!s.mobile_build) {
        n = snprintf(out, cap, "%s NEW", mail);
    } else {
        char age[kAgeTokenCap];
        ui_fmt_home_age(age, sizeof age, s.home_confirmed_ever, s.home_confirm_age_ms);
        n = snprintf(out, cap, "%s NEW / HOME %s", mail, age);
    }
    ui_pad_token(out, cap, (n < 0) ? 0u : std::size_t(n) + 1u);
}

// ============================================================== ROW 4 — WHERE WE ARE, OR WHAT MUST HAPPEN (S-9/S-10)
// ★★★ `have a fix` IS THE **CORE's** PREDICATE, REUSED AND NOT RE-DERIVED (note h). `Node::on_command` refuses a
//     located send when both coordinates are zero, so any other definition of "we have a position" would disagree
//     with the thing that actually rejects us. ⇒ `(0,0)` is `NO LOCATION`, ⛔ never a plausible `0.000,0.000`.
// ⓘ THE ONE DEFINITION LIVES HERE and it has exactly TWO callers, both of which are the SAME question asked at the
//   one site that can see `NodeConfig`: `src/firmware_ui.cpp`'s `ui_have_fix()` — the §4.1 `-l` gate, which needs
//   the LIVE answer at press time — and `build_snapshot`, which publishes the frozen answer as
//   `UiSnapshot::own_fix` for the frame. ⛔ Not a second predicate, and ⛔ the row below re-derives nothing: it
//   reads the published answer, so the panel and the thing that actually rejects a located send cannot disagree.
inline bool ui_status_have_fix(int32_t lat_e7, int32_t lon_e7) { return lat_e7 != 0 || lon_e7 != 0; }

// ★★★★ ROW 4's PRIORITY IS `RESTART NEEDED` > COORDINATES > `NO LOCATION`, DETERMINISTIC AND ⛔ NEVER BOTH (note
//      g). This PRESERVES the shipped behaviour and design §3.6.5 — *a saved-but-reboot-required state stays
//      visible until the reboot*, so it OWNS this row while it stands — and pays the note's "define a
//      deterministic priority" requirement. The coordinates remain readable on the console (`cfg`) meanwhile.
//      ⛔ `RESTART NEEDED` is the ONLY configuration text that may appear on STATUS (§9 R-3).
// ★★★ THE COORDINATE TOKEN **TRUNCATES TOWARD ZERO** to three decimals, it does NOT round (note i): a panel must
//     never render a position more precise — or further along — than the one that is stored. `lat_e7 / 10000` is
//     that truncation, and C++'s integer division truncates toward zero for both signs once the magnitude is taken
//     separately. ⇒ a lat of `-5000` (1/2000 of a degree SOUTH) renders `-0.000`: the SIGN is the raw value's and
//     is never inferred from the truncated digits, which is why it is passed as its own `%s`.
// ⚠ THE MAGNITUDE IS TAKEN IN `int64_t`. `lat_e7`/`lon_e7` are `int32_t`, and `-INT32_MIN` is undefined in 32
//   bits; widening first costs nothing on this path and makes the negation total. (The real domain is ±9e8 /
//   ±1.8e9, so no value is near the edge — the rule is written for the type, not for today's data.)
// ⛔⛔ IT TAKES THE **FROZEN SNAPSHOT**, NOT A LIVE READ, AND THAT IS A CORRECTNESS RULE RATHER THAN A STYLE ONE.
//     `draw_frame` runs ONCE PER OLED PAGE; a `g_node.config()` read in the renderer would let a `cfg set lat`
//     landing between two of the eight page replays draw HALF A COORDINATE ROW from the old fix and half from the
//     new one — a TORN position on a safety device. ⇒ `own_lat_e7` / `own_lon_e7` / `own_fix` are published once
//     per tick by `build_snapshot` and this reads only those. (QG, 2026-08-21.)
// ★ AND IT TRUSTS `own_fix` RATHER THAN RE-TESTING THE COORDINATES: that field IS `ui_status_have_fix`'s answer,
//   taken at the one site that can see `NodeConfig`. Re-deriving here would be the second definition U1 forbids;
//   the direction of any publish-site defect is then fail-CLOSED (`NO LOCATION`), never a fabricated position.
// Widest expansion: `-89.123,-179.123` = **16** of 19, proven by a case.
inline void ui_status_location(char* out, std::size_t cap, bool reboot_required, const UiSnapshot& s) {
    int n;
    if (reboot_required) {
        n = snprintf(out, cap, "%s", kCfgRestartText);            // S-17, REUSED — the one lexeme, not a copy
    } else if (!s.own_fix) {
        n = snprintf(out, cap, "NO LOCATION");
    } else {
        const bool    lat_neg = s.own_lat_e7 < 0, lon_neg = s.own_lon_e7 < 0;
        const int64_t la = lat_neg ? -int64_t(s.own_lat_e7) : int64_t(s.own_lat_e7);
        const int64_t lo = lon_neg ? -int64_t(s.own_lon_e7) : int64_t(s.own_lon_e7);
        n = snprintf(out, cap, "%s%ld.%03lu,%s%ld.%03lu",
                     lat_neg ? "-" : "", (long)(la / 10000000), (unsigned long)((la / 10000) % 1000),
                     lon_neg ? "-" : "", (long)(lo / 10000000), (unsigned long)((lo / 10000) % 1000));
    }
    ui_pad_token(out, cap, (n < 0) ? 0u : std::size_t(n) + 1u);
}

}  // namespace mrui
