<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §CHROME-4 — the navigation rail, the config badge and the 19-column body migration · dispatch brief · 2026-08-16

**Status: DISPATCHED 2026-08-16 on an owner directive.** ★ Role split: the QA-gate wrote this brief and verifies your
claims at the code; **the OWNER runs QG and rules.**
⛔ **Never `git commit`, never `git add`, NEVER `git checkout --` anything.** The tree carries **§CHROME-1, -2 and -3
uncommitted** — build on them; ⛔ do not revert or re-derive them.

**Normative design:** `docs/superpowers/specs/2026-08-15-heltec-mobile-status-navigation-ui-design.md` §3.2, §5, §6
**and §6.1**, §7, §11, §12, §13. ⚠ **Read every amendment block** (search `2026-08-16`); where an amendment and the
original disagree, **the amendment wins**.

**Baseline:** native **1678 / 83346 / 0** · `lus` `b77cfd3d` · `s18` `9868cad3` / 269905 · `sizeof(Node)` **221880** ·
census **174 / 178 / 178** · `heltec_v3` 216196 / 1283092 · `heltec_mobile` 215716 / 1277924 · `gateway_heltec`
241364 / 1248560 · the three non-OLED envs byte-identical to `b8929e5`.

★★ **THIS IS THE LAST SLICE, and §13 is explicit: a partial state — icons over 21-column content, or icon-only
configuration errors — is NOT an acceptable intermediate release.** Finish it or report it unfinished; do not leave
it half-migrated.

---
## 0 — Scope
**BUILD:** the navigation rail + selection frame · the SETTINGS **config badge** (§6) · **§6.1's instrument
reclassification** · the **19-column body migration** (§7) with §7.3's audit · the metal checks (§12).
**Three obligations INHERITED from earlier slices and explicitly transferred to this one:**
1. **`draw_rect`'s final-image linkage** — the rail is its only legitimate caller (`nm`: 1 hit `draw_bitmap`, **0**
   `draw_rect`, on all three OLED envs). ⛔ **No dummy anchor.**
2. **The six rail-only assets** (`kIconStatus`, `kIconSend`, the four `kIconSettings*`, 42 B) — still
   `--gc-sections`'d until the rail draws them.
3. **§11.2's "linked and exercised" bullet**, which §CHROME-3 closed for `draw_bitmap` only.

---
## 1 — ⛔⛔ §6.1's CENSUS, RE-RUN NOW AS THE AMENDMENT REQUIRES — AND IT IS FAR BIGGER THAN EITHER EARLIER FIGURE

**84 references across 18 files** (measured 2026-08-16). ⚠ **My own earlier counts — "48 across 10", then a corrected
"45 across 7" — BOTH UNDERCOUNTED**, which is precisely why the amendment says re-census rather than trust a number.
⇒ **Re-run it yourself before you touch anything; treat the table below as a starting point, not an inventory.**

| bucket | files | action |
|---|---|---|
| ⛔ **RETAIN — actionable SETTINGS/service text** | `test/test_firmware_ui_model.cpp` **14** · `test/test_firmware_config_service.cpp` **3** · `src/firmware_config.cpp` **2** · `src/firmware_config_service.h` **1** | **do not touch** |
| **MIXED — classify per reference** | `tools/probe_firmware_ui/probe_main.cpp` **21** · `src/firmware_ui.cpp` **4** · `src/firmware_ui_model.h` **4** · the two `run.sh` **1** each | STATUS-title presentation → badge; SETTINGS body text → retain |
| ★★ **MUTATION TARGETS — the hazard** | `tools/probe_ui_model_mutations.py` **5** | **retarget onto the badge; deleting a string a mutation targets silently neuters it** |
| **historical** | the spec/plan/register documents (**~27**) | preserve as withdrawn history |

★★ **THE MOST LIKELY SERIOUS MISTAKE IN THIS SLICE IS DELETING TOO MUCH.** `CFG! RELOAD` **remains required
actionable SETTINGS/service text** — §6 says so itself. **Only the STATUS-title presentation is removed.** The
retain bucket is the *majority* of the references, and the native/service tests in it are coverage of §UI-13/§UI-14
behaviour **nobody decided to stop testing**.
★ **The transition must be provable:** the badge's tests must fail against the old STATUS presentation **and vice
versa**, so the two cannot both pass.

---
## 2 — Verified seams (V1 — re-verify)
| seam | where | note |
|---|---|---|
| body row helper | `src/firmware_ui.cpp:586` `body_y(row)` | rows stay; **x moves** |
| body draw sites | `draw_text(0, body_y(n), …)` throughout | **every one** becomes `kBodyX = 12` — ONE authority (§7.1 rule 1) |
| detail wrap | `src/firmware_ui_model.h:279` `kDetailCols = 21` | **21 → 19** |
| page capacity | `:281` `kDetailPageChars = kDetailCols * kDetailBodyRows` | **42 → 38, DERIVED — re-derive, do not re-clamp** |
| page count | `:283` `kDetailPages`, used `:975`, offset `:1785` | changes with the capacity; **pagination must not lie** |
| line buffers | `:715` `detail_line[kDetailBodyRows][kDetailCols + 1]` | shrink with the constant |
| emergency exception | `draw_frame`'s emergency arm | ⛔ **stays full-width at `x=0`** (§5.3) |

⚠ **§7.3's amendment:** the audit must include the strings **§T3** added — `QUEUED`, the earned `SENT, waiting`, and
`NO RELAY HEARD`. All are ≤ 14 chars so they fit 19 columns, **but "it fits" is a MEASUREMENT, not an assumption** —
that is the entire point of the audit. ⚠ `NO RELAY HEARD` is also §5.3's cited reason emergency stays full-width;
those are **different fonts** (`Font::large` 12 columns vs `Font::small`) and must not be conflated.

---
## 3 — What to build
- **Rail** (§3.2): `x=0..9`, `y=10..59`, five 10-px slots aligned to the body rows; a **one-pixel frame** on the
  active slot via `draw_rect`; **unavailable slots stay EMPTY and the others do not move** (⛔ no second layout).
- **Selection** follows the **frozen** `UiChrome` rail fields from §CHROME-1 — ⛔ never a renderer-local cursor, and
  ⛔ never re-derived in the renderer (§5.2's mapping already feeds the projection).
- ⛔ **Emergency: no rail at all**, body at `x=0`, full 128 px (§5.3).
- **Badge** (§6) on the SETTINGS icon, priority **`conflict > unsaved > restart-required > clean`**.
  ⛔⛔ **The badge may replace the STATUS decoration; it may NEVER replace the instruction** — SETTINGS must keep
  rendering actionable text distinguishing `UNSAVED`, `RELOAD`/conflict and `RESTART NEEDED`.
- **Body migration** (§7.1): one `kBodyX` authority · emergency the only full-width body · **every** normal line
  proven ≤ 116 px · buffer capacity ≠ pixel capacity · dynamic labels clamped or wrapped, **never panel-clipped** ·
  ★ **rule 6: two selectable presets must not become visually identical after clamping** — if their visible prefixes
  collide the UI needs distinct short labels, and relying on the hidden suffix is forbidden.

---
## 4 — Traps
- ⛔ **Deleting retain-bucket references** (§1) — the biggest risk in the slice.
- ⛔ **Changing only the draw origin.** The detail wrap must move **at the model's freeze point**, or already-frozen
  21-column lines clip and pagination lies (§7.3).
- ⛔ **Re-clamping instead of re-deriving** the page capacity.
- ⛔ **A renderer-local selection cursor**, or querying a live authority during page replay.
- ⛔ **Emergency losing its full width** — `NO RELAY HRD` would clip, and that is safety text.
- ⛔ **No `Node` growth, no timer, no wire/NV change.** `sizeof(Node)` stays **221880**.

---
## 5 — Tests
**§11.1's rail bullets and §11.2's renderer bullets are normative.** The ones that carry the weight:
- **exactly one navigation frame** on ordinary and modal views; **no rail call at all** on an emergency frame;
- the correct icon stays selected across **all eight U8g2 page replays**;
- **every normal text line fits 116 px** — the §7.3 audit, measured not eyeballed;
- **non-team builds expose no TEAM/SEND slot**, and the remaining icons keep their coordinates;
- the badge priority table, including *unsaved + restart* and *conflict + unsaved*;
- ★ **inbox detail pagination at 19 columns** — a message whose page count changes must page correctly, and the last
  page must not lie.
⛔ **Ask of every check whether it could have come out otherwise.** This arc has recorded **six** instruments that were
green against the defect they were written to catch — the newest found by §CHROME-3 **in its own work** (B201).
⚠ **Re-run whole batteries, not just new entries:** a mutation whose target line has MOVED measures nothing and is
reported `unusable`, not as a pass — and this slice moves a great many lines.

---
## 6 — Gate
1. `pio test -e native`, **then RUN the binary**. From **1678 / 83346 / 0**.
2. **Both UI probes** with control sets; `probe_firmware_ui` is the only venue running the whole chain.
3. **`warning_census.sh`** at its pins.
4. **Simulator inertness, four-step**; `s18` smoke only, keystone **read from `simulation/BASELINE.md`**. ⛔ No 36-run,
   ⛔ no anchor-table edit. ⓘ The anchor row reads **`9868cad3` / 269905 and DOES reproduce**; `1cd21235` is only the
   *pre* column of the before/after table. **No re-anchor is pending or owed.**
5. **Six board envs**; per-board RAM/flash **attributed**, against this brief's header. ⚠ **Expect flash growth** —
   the rail code plus the six rail-only assets that stop being discarded. **RAM must not rise from the bitmaps.**
6. ★★ **D2 explicitly:** `sizeof(Node)` **221880**; `kCap` 91, no timer.
7. **`nm` evidence that `draw_rect` is now in all three OLED images** (§0 obligation 1), and that the six rail-only
   assets are linked (obligation 2).
8. **M2:** §12's metal checks into `docs/2026-07-31-bench-test-script.md` — geometry, modal mapping, emergency width,
   badge priority, and §12.11's `slept=` regression guard with the **no-console-byte** rule.

**Report:** the rail with `file:line` · the badge and its priority · **§6.1's re-census with every reference
classified, and what you retained** · the `kBodyX` authority and the §7.3 audit result per renderer · the detail
re-derivation (21→19, 42→38) and its pagination evidence · every test with its mutation and match count · native ·
both probes · census · the four-step corpus proof · per-board RAM/flash attributed · the `nm` linkage evidence · the
D2 answer · the bench additions · exact final `git status --short` and that nothing was committed.
⛔ **Anything you cannot establish, say so plainly — especially anything only metal can settle.**

**Stop and report rather than improvising if:** a body string cannot fit 116 px without losing meaning (⇒ **report it;
§7.1 rule 5 forbids panel clipping as a truncation policy**) · two presets collide after clamping · pagination cannot
be re-derived honestly · `sizeof(Node)` moves · a corpus row moves · or a retain-bucket reference appears to need
deleting (⇒ **stop — that is the §6.1 mistake**).
