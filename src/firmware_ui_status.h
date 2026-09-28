// MeshRoute — src/firmware_ui_status.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// ★★★★ W4b (design §6.2–§6.7, 2026-09-27) — THE LANDING SCREEN IS NOW **HOME**, AND THIS FILE COMPOSES ITS BYTES:
//      Home's rows 0–1 (who, which team), its item labels and notes, My device's four rows, the key-help and
//      blocked-setup notes, and the body invalidation that repaints them when a visible fact changes with no press.
//      The §UI-17 STATUS body's four row helpers (`ui_status_team` / `_me` / `_known` / `_unread_home`) and its two
//      column budgets RETIRED with the 24x24 mark and the x = 40 rows (brief §2.9); `ui_status_have_fix` and
//      `ui_status_location` are KEPT — My device's position row is exactly the old row 4 without its restart arm.
//      ⓘ The paragraphs below are §UI-17 S3's and are kept as that slice's record; where they describe the STATUS
//      geometry (the mark, the 14-column rows) they are history, and the W4b blocks further down are current.
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

// ---- W4b: the Home body's one budget ------------------------------------------------------------------------------
// ★ Home draws at the ORDINARY body — x = 12, 19 columns (design §6.2) — so there is ONE width now. ⛔ DERIVED IN THE
//   RENDERER, NEVER A SECOND LITERAL THERE: `src/firmware_ui.cpp` static_asserts `kHomeCols == kBodyCols`.
// ⓘ RETIRED WITH THE MARK (brief §2.9): `kStatusNarrowCols` (14, rows 0-2 at x = 40) and `kStatusWideCols` (19).
inline constexpr std::size_t kHomeCols = 19;

// The scratch buffer a caller hands these formatters. ⚠ DELIBERATELY OVERSIZED vs the 19 visible columns, for the
// reason `kLineCap` states next door and NOT as slack for its own sake: every line here is `snprintf`, and
// `-Wformat-truncation=` is on under `-Wall` and GATE-BLOCKING in this project — it fires whenever GCC cannot PROVE
// the widest expansion fits, and it cannot prove a bound on a `%ld` degree field. ⛔ Shrinking this to 20 would buy
// nothing (the margin costs stack, not flash) and would cost the warning census its pin.
inline constexpr std::size_t kStatusLineCap = 48;
// `9+` / `99+` + NUL — the widest count token `ui_fmt_team` or `ui_fmt_mail` can hand back to a Home label.
inline constexpr std::size_t kHomeCountCap = 4;

// ================================================================== W4b — HOME ROW 0: WHO THIS DEVICE IS (§4.2)
// ★★ `ME ` + THE W4a IDENTITY LABEL IN 16 CELLS (3 + 16 = 19): the full counted name through `ui_fmt_identity` —
//    sanitized, abbreviated with the generated `»` past 16 cells, or `0x<HASH8>` WHOLE for an unnamed key (W4a's
//    one formatter, ⛔ never pre-clipped here). ⛔ `IdentityFmt::none` (no name AND no key hash) reads `ME` alone —
//    never a fabricated identity (§4.1's no-fabrication rule).
inline constexpr uint8_t kHomeNameCols = 16;
inline void ui_home_me_line(char* out, std::size_t cap, const UiSnapshot& s) {
    char id[kHomeNameCols + 1];
    const IdentityFmt f = ui_fmt_identity(id, sizeof id, s.own_name, s.own_name_len, s.my_key_hash32, kHomeNameCols);
    const int n = (f == IdentityFmt::name || f == IdentityFmt::hash) ? snprintf(out, cap, "ME %s", id)
                                                                      : snprintf(out, cap, "ME");
    ui_pad_token(out, cap, (n < 0) ? 0u : std::size_t(n) + 1u);
}

// ============================================================== W4b — HOME ROW 1: THE TEAM LINE (design §6.2)
// ★★ `TEAM 12A1B2C3 T220` (18) · `TEAM 12A1B2C3 NO ID` (19) · `NO TEAM` · blank on a build with no team plane.
// ★ THE EIGHT DIGITS ARE `ui_fmt_team_id_full`'s — the existing full team-ID formatter (U1) — with its `0x`
//   dropped: they end with the six-digit nearby/invite fingerprint, so the displays agree WITHOUT a new format.
//   ⛔ Not the retired `ui_status_team`'s declared third spelling (`TEAM %08lX`), which this row replaces.
// ⛔ A pending team-DAD is `NO ID`, never `T0` (the "no plausible substitute" rule above); the team ID is the
//   core's `team_id != 0` fact, read as its meaning.
inline void ui_home_team_line(char* out, std::size_t cap, const UiSnapshot& s) {
    if (!s.team_build) { ui_pad_token(out, cap, 0); return; }        // no team plane: the row is blank
    int n;
    if (s.team_id == 0) {
        n = snprintf(out, cap, "NO TEAM");
    } else {
        char full[kTeamIdTokenCap];
        ui_fmt_team_id_full(full, sizeof full, s.team_id);
        const char* hex8 = full + 2;                                  // the eight digits of `0x%08lX`
        n = (s.my_team_id == 0) ? snprintf(out, cap, "TEAM %s NO ID", hex8)
                                : snprintf(out, cap, "TEAM %s T%u", hex8, unsigned(s.my_team_id));
    }
    ui_pad_token(out, cap, (n < 0) ? 0u : std::size_t(n) + 1u);
}

// ================================================================= W4b — HOME's ITEM LABELS (design §6.2/§6.3)
// ★★ STATUS RIDES IN THE LABELS: `INBOX 3 NEW` (the strip's own `99+` token, `ui_fmt_mail`, omitted at zero) and
//    `TEAM 4 KNOWN` (the strip's `9+` token, `ui_fmt_team`, omitted at zero — `KNOWN`, never `HEARD`, S-5's honesty
//    rule above). ⛔ A count changing inside a label is NOT a change of item (the arrow keeps it).
// ★ THE ACTION WORDS ARE CALLED, NOT COPIED: JOIN/CREATE/INVITE are `provision_row_label`'s own (S-12: one spelling
//   for one operation), and `MENU` is `kListMenuText`.
inline void ui_home_item_label(char* out, std::size_t cap, HomeItem it, const UiSnapshot& s) {
    int n = 0;
    switch (it) {
        case HomeItem::inbox: {
            const uint32_t total = uint32_t(s.unread_dm) + uint32_t(s.unread_ch);
            if (total == 0) { n = snprintf(out, cap, "INBOX"); break; }
            const bool overflow = total > uint32_t(kMailMax);
            char tok[kHomeCountCap];
            ui_fmt_mail(tok, sizeof tok, overflow ? kMailMax : uint8_t(total), overflow);
            n = snprintf(out, cap, "INBOX %s NEW", tok);
            break;
        }
        case HomeItem::team: {
            if (s.team_total == 0) { n = snprintf(out, cap, "TEAM"); break; }
            const bool overflow = s.team_total > kTeamMax;
            char tok[kHomeCountCap];
            ui_fmt_team(tok, sizeof tok, /*configured=*/true, overflow ? kTeamMax : s.team_total, overflow);
            n = snprintf(out, cap, "TEAM %s KNOWN", tok);
            break;
        }
        case HomeItem::send:      n = snprintf(out, cap, "SEND TO TEAM");                                  break;
        case HomeItem::invite:    n = snprintf(out, cap, "%s", provision_row_label(ProvRow::invite));      break;
        case HomeItem::my_device: n = snprintf(out, cap, "MY DEVICE");                                     break;
        case HomeItem::menu:      n = snprintf(out, cap, "%s", kListMenuText);                             break;
        case HomeItem::join:      n = snprintf(out, cap, "%s", provision_row_label(ProvRow::join_team));   break;
        case HomeItem::create:    n = snprintf(out, cap, "%s", provision_row_label(ProvRow::create_team)); break;
        case HomeItem::key_help:  n = snprintf(out, cap, "NO TEAM KEY - HELP");                            break;
        case HomeItem::none:      ui_pad_token(out, cap, 0); return;
    }
    ui_pad_token(out, cap, (n < 0) ? 0u : std::size_t(n) + 1u);
}
// ★ `OPTIONS CHANGED` (15 columns) replaces ITEM 1's label while the note is up (design §6.4).
inline constexpr const char* kHomeOptionsChangedText = "OPTIONS CHANGED";
// A list row: ONE marker cell, then the label — `>NO TEAM KEY - HELP` is the widest, 19 of 19. ⛔ The arrow is drawn
// only in LIST FOCUS; a menu-mode preview is the same rows with a blank marker cell (design §6.1 rule 4).
inline void ui_home_row(char* out, std::size_t cap, bool arrow, const char* label) {
    const int n = snprintf(out, cap, "%c%s", arrow ? '>' : ' ', label);
    ui_pad_token(out, cap, (n < 0) ? 0u : std::size_t(n) + 1u);
}

// ======================================================================= W4b — MY DEVICE (design §4.2 / §6.7)
// ★★★ THE FULL NAME OVER TWO ROWS: raw counted bytes 0–18 on row 0 and 19–31 on row 1, EACH BYTE through
//     `ui_display_byte` — ⛔ never a 19-cell abbreviation split afterwards (that would lose bytes and split a `»`).
//     32 bytes <= 38 cells, so the whole stored name is always on the panel. An unnamed device reads `NO NAME SET`
//     on row 0 and a blank row 1 (W1c D10: unnamed is a real state, never a default name).
inline constexpr uint8_t kMyDeviceRowCols = 19;
inline constexpr const char* kNoNameSetText = "NO NAME SET";
inline void ui_my_device_name_rows(char* r0, std::size_t cap0, char* r1, std::size_t cap1, const UiSnapshot& s) {
    if (!r0 || !r1 || cap0 == 0 || cap1 == 0) return;
    const uint8_t len = (s.own_name_len > sizeof s.own_name) ? uint8_t(sizeof s.own_name) : s.own_name_len;
    if (len == 0) {
        snprintf(r0, cap0, "%s", kNoNameSetText);
        r1[0] = '\0';
        return;
    }
    const uint8_t n0 = (len < kMyDeviceRowCols) ? len : kMyDeviceRowCols;
    uint8_t w = 0;
    for (uint8_t i = 0; i < n0 && w + 1u < cap0; ++i) r0[w++] = ui_display_byte(uint8_t(s.own_name[i]));
    r0[w] = '\0';
    w = 0;
    for (uint8_t i = n0; i < len && w + 1u < cap1; ++i) r1[w++] = ui_display_byte(uint8_t(s.own_name[i]));
    r1[w] = '\0';
}
// `ID 0x<HASH8>` — the stable device identity, W4a's full member token (U1).
inline void ui_my_device_id(char* out, std::size_t cap, const UiSnapshot& s) {
    char h[kMemberHashCap];
    ui_fmt_member_hash_full(h, sizeof h, s.my_key_hash32);
    const int n = snprintf(out, cap, "ID %s", h);
    ui_pad_token(out, cap, (n < 0) ? 0u : std::size_t(n) + 1u);
}

// ============================================================ W4b — THE TWO HOME NOTES (design §6.5 / §6.6)
// ★ KEY HELP — the existing procedure, as a note (a key holder uses INVITE MEMBER or TEAM → GRANT KEY). ⛔ No
//   automatic key request. Either press returns Home on its opener.
inline constexpr const char* kKeyHelpRows[5] = { "NO TEAM KEY", "A MEMBER WHO HAS IT", "MUST GRANT IT TO",
                                                 "THIS DEVICE", "press = back" };
// ★★ THE BLOCKED SETUP — the reason on body row 1 (`prov_block_note`, the SAME words the Settings menu says), and
//    `IN SETTINGS` on row 2 for the two draft states the operator can resolve there; ⛔ blank for `CFG UNAVAILABLE`,
//    which Settings cannot resolve either. No arrow; the rail box is on SETTINGS (`ui_nav_slot`).
inline constexpr const char* kInSettingsText = "IN SETTINGS";
inline const char* ui_setup_block_row2(ProvBlock b) {
    return (b == ProvBlock::conflict || b == ProvBlock::unsaved) ? kInSettingsText : "";
}

// ============================================================= W4b — THE BODY INVALIDATION (design §6.7, §2.10)
// ★★★★ A FACT THE HOME OR MY-DEVICE BODY DRAWS CAN CHANGE WITH NO PRESS AND NO STRIP TOKEN MOVING — a console
//      rename, a team-DAD answer, the key arriving, a `cfg set lat` — and `FrameGate::step` answers `idle` while the
//      model is clean, so without this the lit body would go STALE. ⇒ the `ui_team_invalidate` shape (U3): a
//      visibility predicate, a field-wise comparison of the frozen frame against the live snapshot, and ONLY a
//      paint request — ⛔ it clears nothing and moves nothing; the frame being drawn stays ONE snapshot (§5).
inline bool ui_home_body_visible(const UiState& st, bool compose_open, Emergency emg) {
    return emg == Emergency::idle && !compose_open && st.detail == InboxModal::closed && st.screen == Screen::status;
}
inline bool ui_home_facts_equal(const UiSnapshot& a, const UiSnapshot& b) {
    if (a.own_name_len != b.own_name_len) return false;
    for (uint8_t i = 0; i < a.own_name_len && i < sizeof a.own_name; ++i)
        if (a.own_name[i] != b.own_name[i]) return false;
    return a.my_key_hash32    == b.my_key_hash32
        && a.team_build       == b.team_build
        && a.team_id          == b.team_id
        && a.my_team_id       == b.my_team_id
        && a.team_key_present == b.team_key_present
        && a.prov_join_team   == b.prov_join_team
        && a.prov_create_team == b.prov_create_team
        && a.prov_invite      == b.prov_invite
        && a.unread_dm        == b.unread_dm
        && a.unread_ch        == b.unread_ch
        && a.team_total       == b.team_total
        && a.own_fix          == b.own_fix
        && a.own_lat_e7       == b.own_lat_e7
        && a.own_lon_e7       == b.own_lon_e7;
}
inline bool ui_home_invalidate(UiModel& m, const UiSnapshot& live, const UiSnapshot& frozen) {
    if (!ui_home_body_visible(m.state(), m.compose_open(), m.emergency())) return false;   // ⛔ nothing cleared
    if (ui_home_facts_equal(live, frozen)) return false;
    m.mark_dirty();
    return true;
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
