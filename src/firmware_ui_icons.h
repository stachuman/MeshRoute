// MeshRoute — src/firmware_ui_icons.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §CHROME-1 — THE OLED CHROME'S ICON ASSETS, and nothing else. Design
// `docs/superpowers/specs/2026-08-15-heltec-mobile-status-navigation-ui-design.md` §8.1.
//
// ★★★ THIS UNIT IS PURE AND ARDUINO-FREE BY CONSTRUCTION — it includes `<cstdint>` and nothing else, names no
//     display library, no board and no U8g2 type. §8.1's whole point is that the V4 port reuses these bytes
//     unchanged; a header that reached for a board type could not.
//
// ★★★★ THE BYTE-ORDER CONTRACT, DEFINED HERE AND NOT PER BOARD (§8.1 AMENDMENT 2026-08-16). Leaving it to the board
//      is how one asset renders MIRRORED on one port and BIT-REVERSED on another, which is precisely what §8.1 exists
//      to prevent. The convention is U8g2/XBM:
//        · ROW-MAJOR      — row 0 first, top to bottom;
//        · LSB-FIRST      — within a byte, bit 0 is the LEFTMOST pixel of that byte's 8-pixel run;
//        · 1 bit/pixel    — 1 = ink, 0 = background;
//        · ROWS PADDED TO WHOLE BYTES — every row starts a new byte, so
//              stride  = (width + 7) / 8            and     byte_count = stride * height.
//      ⇒ the ONE decode expression, and the only one any renderer or probe may use:
//              set(x, y) == ((bits[y * stride + (x >> 3)] >> (x & 7)) & 1u)
//      ★ It is PINNED BY A NATIVE TEST that decodes two deliberately ASYMMETRIC glyphs (`kIconKey`, `kIconSend`)
//        back into ASCII art — asymmetric on BOTH axes, so a mirror, a flip and a bit-reversal each fail it.
//        A symmetric glyph would have passed all three, which is the instrument-that-cannot-fail shape.
//
// ★★ FLASH, NOT RAM (§8.1 AMENDMENT 2026-08-16). Every array is `inline constexpr` at NAMESPACE scope, so it is
//    read-only data in `.rodata` and costs ZERO SRAM. `heltec_v3` is already at ~66 % RAM; a RAM rise here would be a
//    design error, not a cost. ⛔ Never make one of these non-`const`, never build one at runtime, and ⛔ never copy
//    one into a buffer to "fix" a draw call — pass the pointer.
//
// ⓘ SCOPE, CORRECTED AGAIN 2026-08-16 after §CHROME-4 (QG): **ALL FIFTEEN ASSETS ARE NOW LIVE.** §CHROME-3's strip
//   renderer drew nine; §CHROME-4's navigation rail draws the remaining six (`kIconStatus`, `kIconSend` and the
//   four `kIconSettings*`, 42 B). ⇒ **every asset has a real ODR-user and `nm` finds all fifteen in all three OLED
//   images**; none is discarded by `--gc-sections`, and §11.2's *"linked and exercised"* bullet is CLOSED for the
//   assets and for both canvas primitives. Flash cost is MEASURED throughout; **RAM cost is ZERO** (§8.1).
// ⓘ AMENDED 2026-08-22 by §UI-17 S6: **SIXTEEN.** The sixteenth is `kMarkMeshRoute`, the STATUS body's 24x24 mark
//   (72 B), drawn by `draw_status_screen` — so it too has a real ODR-user from the slice that adds it. ⛔ The count
//   above is amended, not deleted (§3 rule 3): it read *"ALL FIFTEEN"* and was true until this asset landed.
// ⓘ AMENDED AGAIN 2026-08-23 by §CHROME-5: **EIGHTEEN symbols, twenty-four pictures.** The duty gauge adds
//   `kIconDutyDisabled`, `kIconDutyBlocked` and the INDEXED `kIconDutyFill[kDutyFillLevels]` (six pictures in one
//   2-D block, 42 B) — 56 B of `.rodata` in total, all three drawn by `draw_status_strip`'s sixth slot, so every one
//   of them has a real ODR-user in the slice that adds it. ⛔ The counts above are amended, not deleted (§3 rule 3).
// ⛔ WITHDRAWN (§3 rule 3): the intervening wording said the six rail-only assets "still have no ODR-user" and were
//   discarded until §CHROME-4 drew them. True when written; false now.
// ⛔ WITHDRAWN WORDING, KEPT VISIBLE (§3 rule 3): this block previously read *"Nothing draws them yet — the canvas
//   primitives are slice 2 and the strip / rail renderers are slices 3-4. ⇒ in this slice these symbols have NO
//   ODR-user, so a linker is free to discard every one of them and the measured per-board flash delta is NOT
//   evidence that they fit."* That was TRUE WHEN WRITTEN and is now false for the nine strip assets.
//
// ⛔ NO HEAP, NO UNICODE, NO UTF-8 DECODING, NO ADDITIONAL U8g2 FONT (§8.1). The panel has exactly two fonts and this
//    unit adds neither a third nor a glyph range.

#pragma once

#include <cstdint>

namespace mrui {
namespace icons {

// ★ THE STRIDE RULE, DERIVED ONCE (U1). ⛔ Never hand-write a stride at a call site: `kIconBattery` is 11 px wide and
//   therefore 2 bytes per row, while every other asset here is 7 px and 1 byte — a call site that assumed "1 byte per
//   row" would read the battery outline as a 7x14 smear.
inline constexpr uint8_t stride_of(uint8_t width) { return uint8_t((width + 7u) / 8u); }
inline constexpr uint16_t byte_count_of(uint8_t width, uint8_t height) {
    return uint16_t(stride_of(width) * height);
}

// §3.1: "all icons fit within a 7-pixel height"; §3.2's rail slots are 10 px tall and carry a "6-7 pixel icon".
// ⇒ ONE square size for every glyph but the battery, whose outline needs the extra width for its terminal nub.
inline constexpr uint8_t kIconW = 7;
inline constexpr uint8_t kIconH = 7;

// ---------------------------------------------------------------------------------- the status strip's glyphs (§4)

// MAIL / INBOX — one envelope, used by BOTH the strip's mail slot (§4.1) and the rail's INBOX slot (§3.2). ⛔ ONE
// declaration, deliberately (U1): two envelopes that could drift apart is exactly the parallel-asset rot.
//   #######
//   ##...##
//   #.#.#.#
//   #..#..#
//   #.....#
//   #######
//   .......
inline constexpr uint8_t kIconMail[7] = { 0x7F, 0x63, 0x55, 0x49, 0x41, 0x7F, 0x00 };

// HOME — FOUR glyphs for §4.2's four core states, plus the FIFTH display state (`blank`) which is the ABSENCE of a
// glyph and therefore has no bitmap at all. ⛔ "Not applicable" is drawn as NOTHING, never as the crossed house:
// §4.2 says so in as many words, and a fault icon for a build with no home plane is a lie about a measurement.
//   unknown — an EMPTY house: we have never confirmed, so nothing is claimed.
//   ...#...
//   ..#.#..
//   .#...#.
//   #######
//   #.....#
//   #.....#
//   #######
inline constexpr uint8_t kIconHomeUnknown[7] = { 0x08, 0x14, 0x22, 0x7F, 0x41, 0x41, 0x7F };
//   confirmed — a SOLID house.
//   ...#...
//   ..###..
//   .#####.
//   #######
//   ##...##
//   ##.#.##
//   ##.#.##
inline constexpr uint8_t kIconHomeConfirmed[7] = { 0x08, 0x1C, 0x3E, 0x7F, 0x63, 0x6B, 0x6B };
//   checking — the solid house with a query mark in the body: a confirmation is DUE, which is not a failure.
//   ...#...
//   ..###..
//   .#####.
//   #######
//   #.##..#
//   #...#.#
//   #...#.#
inline constexpr uint8_t kIconHomeChecking[7] = { 0x08, 0x1C, 0x3E, 0x7F, 0x4D, 0x51, 0x51 };
//   lost — the empty house with an X through its body.
//   ...#...
//   ..#.#..
//   .#...#.
//   #######
//   #.#.#.#
//   #..#..#
//   #.#.#.#
inline constexpr uint8_t kIconHomeLost[7] = { 0x08, 0x14, 0x22, 0x7F, 0x55, 0x49, 0x55 };

// PEOPLE — teammates HEARD/KNOWN (§4.3's ruled wording), used by both the strip's team slot and the rail's TEAM slot.
//   ##...##
//   ##...##
//   .......
//   ###.###
//   #######
//   #######
//   #######
inline constexpr uint8_t kIconPeople[7] = { 0x63, 0x63, 0x00, 0x77, 0x7F, 0x7F, 0x7F };

// KEY — the TEAM CHANNEL CONTENT key (§4.4). ⛔ It never means the node's own crypto identity, and never that peer
// public keys are cached. As with home, the "no team" state is the ABSENCE of a glyph and has no bitmap.
// ★ THIS IS ONE OF THE TWO BYTE-ORDER CONFORMANCE GLYPHS: the bow is on the LEFT and the single tooth hangs on the
//   RIGHT, so a horizontal mirror, a vertical flip and a bit-reversal are each visibly wrong and each fail the test.
//   .......
//   .##....
//   #..#...
//   #..####
//   #..#..#
//   .##....
//   .......
inline constexpr uint8_t kIconKey[7] = { 0x00, 0x06, 0x09, 0x79, 0x49, 0x06, 0x00 };
//   crossed key — `kIconKey` OR a top-right-to-bottom-left diagonal (0x40,0x20,0x10,0x08,0x04,0x02,0x01).
//   ......#
//   .##..#.
//   #..##..
//   #..####
//   #.##..#
//   .##....
//   #......
inline constexpr uint8_t kIconKeyCrossed[7] = { 0x40, 0x26, 0x19, 0x79, 0x4D, 0x06, 0x01 };

// BATTERY — 11 px wide, the one asset that is NOT 7 px, because §3.1 budgets 35 px for the outline plus `4.1V`
// (4 small-font columns = 24 px). The outline is UNFILLED and stays that way: a fill level would imply an approved
// chemistry/discharge model, which §4.5 puts out of scope.
// ⚠ STRIDE 2. Every row is TWO bytes; see `stride_of`.
//   #########..
//   #.......#..
//   #.......###
//   #.......###
//   #.......###
//   #.......#..
//   #########..
inline constexpr uint8_t kBatteryW = 11;
inline constexpr uint8_t kBatteryH = 7;
inline constexpr uint8_t kIconBattery[14] = {
    0xFF, 0x01,
    0x01, 0x01,
    0x01, 0x07,
    0x01, 0x07,
    0x01, 0x07,
    0x01, 0x01,
    0xFF, 0x01,
};

// ------------------------------------------------------------- §CHROME-5 — the DUTY-UTILIZATION GAUGE (§3.1's sixth
// slot, design §3.3). ⛔ ICON ONLY, NEVER A PERCENTAGE: the exact figure and the recovery time stay in the `duty`
// console verb and the companion diagnostics, which is where a number that needs reading rather than glancing belongs.
//
// ★★★★ **N IS DERIVED FROM THE GLYPH, NOT TYPED.** The gauge is the same 7x7 square every other strip asset is, so its
//      OUTLINE owns rows 0 and 6 and columns 0 and 6, and what is left — `kIconH - 2` rows — is the only thing that
//      can carry ink. ⇒ the number of fill STEPS is a property of the picture (`kDutyGaugeRows`), the enumerators in
//      `firmware_ui_chrome.h` are asserted against it at build time, and the pct -> step map is written from
//      `kDutyFillLevels` alone. ⛔ Never hand-write a step count anywhere: a map that disagrees with the artwork
//      renders two different percentages as the same picture (or worse, skips a row nobody notices is unreachable).
// ★ The DERIVATION IS PINNED BY A NATIVE CASE that decodes each level back into ASCII art and asserts that level `k`
//   inks exactly `k` interior rows, bottom-up — so the constant and the bytes cannot drift apart in silence.
inline constexpr uint8_t kDutyGaugeRows  = uint8_t(kIconH - 2u);        // 5 — the drawable interior rows of the outline
inline constexpr uint8_t kDutyFillLevels = uint8_t(kDutyGaugeRows + 1u);// 6 — empty, plus one level per interior row

// DISABLED — the empty outline with a diagonal through it: this node has NO duty limit (`duty_cycle <= 0`), so there
// is no utilization to report. ⛔ It is NOT "0 %": an unlimited node and an idle limited one are different facts, the
// same split the crossed key draws one slot over (§4.4's "irrelevant" is not "missing").
//   #######
//   #....##
//   #...#.#
//   #..#..#
//   #.#...#
//   ##....#
//   #######
inline constexpr uint8_t kIconDutyDisabled[7] = { 0x7F, 0x61, 0x51, 0x49, 0x45, 0x43, 0x7F };

// THE FILL FAMILY — ONE 2-D BLOCK, INDEXED BY LEVEL, ⛔ never six separately named symbols. Level `k` inks the
// BOTTOM `k` interior rows, so the gauge fills upward like a tank and level `kDutyGaugeRows` is a solid square.
// ⚠ It is a 2-D array rather than a table of POINTERS on purpose: a pointer table costs a relocation per entry and
//   invites a call site to index past its end; `kIconDutyFill[level]` decays to the same `const uint8_t*` every other
//   asset here hands `draw_bitmap`, and `sizeof kIconDutyFill` is the whole 42-byte cost, in `.rodata`.
//   level 0            level 1            level 2            level 3            level 4            level 5
//   #######            #######            #######            #######            #######            #######
//   #.....#            #.....#            #.....#            #.....#            #.....#            #######
//   #.....#            #.....#            #.....#            #.....#            #######            #######
//   #.....#            #.....#            #.....#            #######            #######            #######
//   #.....#            #.....#            #######            #######            #######            #######
//   #.....#            #######            #######            #######            #######            #######
//   #######            #######            #######            #######            #######            #######
inline constexpr uint8_t kIconDutyFill[kDutyFillLevels][kIconH] = {
    { 0x7F, 0x41, 0x41, 0x41, 0x41, 0x41, 0x7F },
    { 0x7F, 0x41, 0x41, 0x41, 0x41, 0x7F, 0x7F },
    { 0x7F, 0x41, 0x41, 0x41, 0x7F, 0x7F, 0x7F },
    { 0x7F, 0x41, 0x41, 0x7F, 0x7F, 0x7F, 0x7F },
    { 0x7F, 0x41, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F },
    { 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F },
};

// BLOCKED — the FULL gauge plus a warning mark: 100 %, i.e. transmission is duty-blocked right now. The mark is an
// exclamation KNOCKED OUT of the solid square (bar, gap, dot), so it cannot be confused with the plain full gauge at
// 99 % — which is the one distinction on this slot that changes what an operator should do.
//   #######
//   ###.###
//   ###.###
//   ###.###
//   #######
//   ###.###
//   #######
inline constexpr uint8_t kIconDutyBlocked[7] = { 0x7F, 0x77, 0x77, 0x77, 0x7F, 0x77, 0x7F };

// ------------------------------------------------------------------------------ the navigation rail's glyphs (§3.2)
// ⓘ Two of the five slots reuse a strip glyph rather than declaring a second one (U1): TEAM is `kIconPeople` and
//   INBOX is `kIconMail`. Only STATUS, SEND and SETTINGS need their own.

// STATUS — an information disc.
//   .#####.
//   #.....#
//   #..#..#
//   #.....#
//   #..#..#
//   #..#..#
//   .#####.
inline constexpr uint8_t kIconStatus[7] = { 0x3E, 0x41, 0x49, 0x41, 0x49, 0x49, 0x3E };

// SEND — an outgoing arrow head, pointing RIGHT.
// ★ THE SECOND BYTE-ORDER CONFORMANCE GLYPH, and it is asymmetric on the horizontal axis alone by design: a mirror
//   turns "send" into "receive", which is the one icon error on this panel that would read as a working feature.
//   #......
//   ###....
//   #####..
//   #######
//   #####..
//   ###....
//   #......
inline constexpr uint8_t kIconSend[7] = { 0x01, 0x07, 0x1F, 0x7F, 0x1F, 0x07, 0x01 };

// SETTINGS — FOUR glyphs, one per §6 badge state, rather than a gear plus three overlay sprites. ⛔ The overlay shape
// was declined: it would need a second draw call, a second placement table and an OR-composition rule at the draw
// site, i.e. three more things to keep in step for a 3x3 marker. One bitmap per state is what `CfgBadge` selects
// directly.
// ⚠ §6's own rule stays load-bearing beside these: the icon may replace the STATUS decoration; it may NEVER replace
//   SETTINGS' actionable text (`UNSAVED` / `RELOAD` / `RESTART NEEDED`). One small glyph cannot carry a remedy.
// The shared gear occupies rows 0-4, cols 0-4; the marker occupies rows 3-6, cols 4-6.
//   clean — the gear alone.
//   .###...
//   #####..
//   ##.##..
//   #####..
//   .###...
//   .......
//   .......
inline constexpr uint8_t kIconSettings[7] = { 0x0E, 0x1F, 0x1B, 0x1F, 0x0E, 0x00, 0x00 };
//   unsaved — the gear plus a solid 2x2 dot.
//   .###...
//   #####..
//   ##.##..
//   #####..
//   .###...
//   .....##
//   .....##
inline constexpr uint8_t kIconSettingsUnsaved[7] = { 0x0E, 0x1F, 0x1B, 0x1F, 0x0E, 0x60, 0x60 };
//   conflict — the gear plus an exclamation (bar, gap, dot). Highest badge priority (§6).
//   .###...
//   #####..
//   ##.##.#
//   #####.#
//   .###...
//   ......#
//   .......
inline constexpr uint8_t kIconSettingsConflict[7] = { 0x0E, 0x1F, 0x5B, 0x5F, 0x0E, 0x40, 0x00 };
//   restart-required — the gear plus a re-entrant arrow stub.
//   .###...
//   #####..
//   ##.##..
//   #####..
//   .###.##
//   .....#.
//   .....##
inline constexpr uint8_t kIconSettingsRestart[7] = { 0x0E, 0x1F, 0x1B, 0x1F, 0x6E, 0x20, 0x60 };

// ------------------------------------------------------------------- the STATUS body's MeshRoute mark (§UI-17 S6)
// ★★★★ FINAL ARTWORK — owner-supplied `logo_3.png`, 24x24 RGBA with a binary alpha mask, landed 2026-08-29.
//      The transparent pixels are OFF and the opaque black pixels are ON. The source already matches the reserved
//      24x24 slot, so this is the planned 72-byte artwork swap: no runtime scaling, coordinate change, text-row
//      movement or second rendering path. The row-by-row test and I05/I06 mutations pin the converted result.
//
// ⚠ STRIDE 3 — 24 px wide ⇒ every row is THREE bytes (`stride_of(24) == 3`), so this is the second asset here that
//   is not one byte per row. `byte_count_of(24, 24) == 72`.
// ★ ASYMMETRIC ON BOTH AXES BY CONSTRUCTION, which is what makes the decode test an instrument that CAN fail: a
//   horizontal mirror puts the R first and reverses both letters, a vertical flip turns the M into a W and stands
//   the R on its head, and an MSB-first authoring scrambles every 8-px run. A symmetric mark would pass all three.
//   ........................
//   ........................
//   ...##...........######..
//   ...###.........##....#..
//   ...#.##.......##.....#..
//   ...#..##.....##......#..
//   ...#...##...##.......#..
//   ...#....##.##........#..
//   ...#.....###.........#..
//   ...#......#..........#..
//   ...#.................#..
//   ...#........##########..
//   ...#.........##.........
//   ...#..........##........
//   ...#...........##.......
//   ...#............##......
//   ...#.............##.....
//   ...#..............##....
//   ..###..............###..
//   .#...#............#...#.
//   .#...#............#...#.
//   .#...#............#...#.
//   ..###..............###..
//   ........................
inline constexpr uint8_t kMarkW = 24;
inline constexpr uint8_t kMarkH = 24;
inline constexpr uint8_t kMarkMeshRoute[72] = {
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x18, 0x00, 0x3F,
    0x38, 0x80, 0x21,
    0x68, 0xC0, 0x20,
    0xC8, 0x60, 0x20,
    0x88, 0x31, 0x20,
    0x08, 0x1B, 0x20,
    0x08, 0x0E, 0x20,
    0x08, 0x04, 0x20,
    0x08, 0x00, 0x20,
    0x08, 0xF0, 0x3F,
    0x08, 0x60, 0x00,
    0x08, 0xC0, 0x00,
    0x08, 0x80, 0x01,
    0x08, 0x00, 0x03,
    0x08, 0x00, 0x06,
    0x08, 0x00, 0x0C,
    0x1C, 0x00, 0x38,
    0x22, 0x00, 0x44,
    0x22, 0x00, 0x44,
    0x22, 0x00, 0x44,
    0x1C, 0x00, 0x38,
    0x00, 0x00, 0x00,
};

}  // namespace icons
}  // namespace mrui
