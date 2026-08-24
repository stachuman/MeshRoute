<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §CHROME-5 — the strip's duty gauge · implementation plan · 2026-08-23

**Status: DISPATCHED 2026-08-23** (N4 + its two QG fixes are landed in-tree, uncommitted — everything in the
tree survives, C1). One slice, one dispatch. ⛔ **NOT part of §UI-16.**

**Authority:** the ★ CHROME-5 amendment in
`docs/superpowers/specs/2026-08-15-heltec-mobile-status-navigation-ui-design.md` §3.1 (the pixel-exact moved
table), mirrored in `docs/superpowers/specs/2026-07-31-onboard-oled-ui-design.md` §3.3. This plan adds only the
implementation shape; the amendment's rulings govern.

## The ruled facts (from the amendment — quoted, not remembered)
- Sixth slot **duty x=83..89**, 7×7, ⛔ **icon only, never a percentage**; home→27, people→54, key→75 (one-pixel
  gaps); ⛔ battery untouched (91/104/127, right-anchored reading unchanged — P13c's property must still hold).
- Three states: **crossed** = disabled · **empty-to-full** = ~0–99 % · **full + warning mark** = 100 %,
  duty-blocked.
- ★ Authority = **`Node::duty_status()`** (`node_mac.cpp:1716`, `DutyStatus{pct, avail_ms, enabled}`) — ⛔ never
  raw `duty_ms`, ⛔ never the five-minute anti-spam budget. Exact pct/recovery stay in `duty`/companion.
- ⛔ No wire, NV, routing or `Node` change (`duty_status()` is an existing `const` accessor).

## Steps (the owner's list, made concrete)
1. **Snapshot once.** `build_snapshot` reads `duty_status()` once per frame; ⛔ the renderer never calls it.
2. **Classify BEFORE freezing.** A pure `duty_bucket(enabled, pct)` → a small enum
   (`disabled | fill_0..fill_N | blocked`) stored in the frozen `UiChrome`; the renderer switches on the bucket,
   `default`-less. ★ **N is DERIVED, not typed**: the gauge glyph's drawable interior rows define the fill steps
   (a 7×7 outline ⇒ its measured interior), and the pct→step map is written from that one constant — a derivation
   pin, the OQ-2/M9b discipline. The double boundary facts are pinned: `pct==100` is `blocked` (never `fill_N`),
   `enabled==false` wins over any pct.
3. **Repaint only on a visible bucket change.** The chrome dirty-compare uses the bucket, ⛔ not the pct — a
   pct move inside one bucket owes no repaint (a case drives two pcts in one bucket and asserts zero repaint).
4. **The slot + bitmaps.** `kStrip[]` gains the duty slot; the moved x values land in the ONE layout table
   (`:87-88`'s rule — never repeated at draw sites); three/N glyph states drawn within y=0..6.
5. **Geometry, mutation, renderer gates.** Probe P13a/b/c bounds restate the NEW coordinates themselves (the
   amendment's own rule) + a duty arm driving all three states and the boundary pcts through the real renderer;
   `model` battery covers `duty_bucket` (boundaries, disabled-wins, blocked-vs-full — each RED at match count 1);
   the strip's existing controls re-anchor where the moved x values broke their substrings (expect this; the N3
   V04 lesson — check for vacuous twins after the move).
6. **Two-board gate** (QG's): `heltec_mobile` + `gateway` warning comparison, `-Wswitch` 0 (the bucket switch is
   enum-dispatched), RAM/flash diff. `src/`-only ⇒ s18-inert by construction.

## Report obligations
Standard: pins with case names + match counts · mutation ledger with full passes · probe proofs (incl. P13c's
every-earlier-glyph-same-x re-proof at the new table) · measured `UiChrome` growth (offsetof where a bool moves)
· the M2 residue draft (a real duty-blocked TX on metal, if any check is host-unreachable) · `git status --short`.
