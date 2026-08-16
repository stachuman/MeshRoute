<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §CHROME-3 — the status strip, its snapshot sources and the repaint invalidation · dispatch brief · 2026-08-16

**Status: DISPATCHED 2026-08-16 on an owner directive**, after §CHROME-1 and §CHROME-2 passed QG.
★ Role split: the QA-gate wrote this brief and verifies your claims at the code; **the OWNER runs QG and rules.**
⛔ **Never `git commit`, never `git add`, NEVER `git checkout --` anything.** The tree carries **§CHROME-1 and
§CHROME-2 uncommitted** — build on them; ⛔ do not revert, restyle or re-derive either.

**Normative design:** `docs/superpowers/specs/2026-08-15-heltec-mobile-status-navigation-ui-design.md` §3.1, §4, §8.2,
**§8.3 AND §8.3.1**, §11.2. ⚠ **Read every amendment block** (search `2026-08-16`) — **§8.3.1 WITHDRAWS a test that
would have caused harm**, and it is the section this slice is judged on. ⛔ Do not implement a withdrawn instruction.

**Baseline:** native **1673 / 83284 / 0** · `lus` `b77cfd3d` · `s18` `9868cad3` / 269905 · `sizeof(Node)` **221880** ·
census **174 / 178 / 178** · `heltec_v3` 216180 / 1281328 · `heltec_mobile` 215700 / 1276152 · `gateway_heltec`
241348 / 1246900 · the three non-OLED envs byte-identical to `b8929e5`.

---
## 0 — Scope: §10 slice 3

**BUILD:** publish the snapshot sources · freeze `UiChrome` with the frame · draw the **top status strip** at fixed
slots · implement §8.3's invalidation **as amended by §8.3.1** · delete the `fmt_volts` duplication.

⛔⛔ **OUT, and this is the boundary most likely to be crossed by accident:**
- **the NAVIGATION RAIL and its selection frame — slice 4.** `UiChrome` already carries the three rail fields
  (§CHROME-1); **freeze them, do not draw them.**
- ⛔ **the §6 removal of `CFG* UNSAVED` / `CFG! RELOAD` from the STATUS title — SLICE 4**, because the badge that
  replaces them lives on the **SETTINGS rail icon**. **Removing the text before the badge exists would delete the
  only signal.** §6.1's instrument-retargeting obligation travels with it.
- the 19-column body migration and §7.3's audit — **slice 4**.

★ **RECORDED 2026-08-16 (QG) — WHAT DEFERRING THE RAIL ALSO DEFERS.** The rail is `draw_rect`'s **only legitimate
caller**, so postponing it postpones `draw_rect`'s final-image linkage too. **Measured after this slice:**
`firmware_ui.o` references `draw_bitmap`, `board_ui.o` defines both, and ⛔ **`draw_rect` is ABSENT from all three
OLED images** (`nm -C firmware.elf`: 1 hit vs 0). ⇒ **§11.2's "linked and exercised" bullet is CLOSED for
`draw_bitmap` by this slice and TRANSFERS for `draw_rect` to §CHROME-4.** ⛔ **No dummy anchor** — see the design's
§8.1 correction. ⓘ Same split applies to the assets: nine strip icons are live; the six rail-only ones remain
`--gc-sections`'d until the rail draws them.

---
## 1 — Verified seams (V1 — re-verify before relying on them)

| seam | `src/firmware_ui.cpp` | note |
|---|---|---|
| the publisher | `build_snapshot(now_ms)` **:421** | where §CHROME-1's five defined-but-unpublished fields get their values |
| the strip to replace | `draw_status_bar(s)` **:555**, called **:948** | today: `DM%u CH%u T%u/%u %s` + the rule |
| the duplicated formatter | `fmt_volts` **:273** | §CHROME-1's declared debt — **delete it here** |
| ★ **the freeze point** | **:1043** `s_frame_snap = s;` | four frozen copies today |
| the render call | **:1055** `draw_frame(s_frame_state, s_frame_snap, s_frame_out, s_frame_cfg)` | **`UiChrome` becomes the fifth frozen copy — match this idiom (U3)** |

**Publish these five** (`src/firmware_ui_model.h`, defined by §CHROME-1, all still at their "nothing established"
defaults): `mobile_build` · `home_link` · `home_confirmed_ever` · `team_key_present` · `home_confirm_age_ms`.
⛔⛔ **`home_confirm_age_ms` IS `uint64_t` AND MUST STAY SO ALL THE WAY FROM `Node::mobile_home_confirm_age_ms()`** —
`UiSnapshot::now_ms` is `uint32_t`, so computing the age by subtraction inside the snapshot is the ~49.7-day wrap
§4.2 forbids. **Take the accessor's value; do not recompute it.**

---
## 2 — ★★ THE SECTION THIS SLICE IS JUDGED ON: §8.3.1's invalidation

`FrameGate::step` (`src/firmware_ui_model.h:1926-1932`) tests **`blanked` FIRST**, sets `_open = false` and returns
`blank` — **`dirty` is never examined while dark** — and blanking itself **sets `dirty = true`** (`:831`), with
`:1931` recording that *"an invalidation raised while dark survives"*.

**Implement exactly these four, and no more:**
1. a **chrome-only** change while blanked must **not** unblank, **not** start a frame, **not** produce bus traffic,
   and **not** touch the attention clock (`_last_input_ms`);
2. ⛔ it must **NEVER CLEAR an existing dirty bit** — §B107's survival rule is not this slice's to relax;
3. after **wake**, the first frame **freezes the CURRENT live chrome**, never a projection captured while dark;
4. **lit + clean + a visible chrome change ⇒ dirty.** ★ the positive half, and the whole point of §8.3.

⛔ **The withdrawn instruction — do NOT implement it:** an earlier version required a blanked chrome change to mark
the model **clean**. That would clear a dirty bit while dark and **erase a legitimate pending redraw**.

**Comparison discipline (§8.3):** compare the live chrome against **the chrome frozen for the most recently opened
frame**, and **update the reference only when a NEW FRAME FREEZES** — not merely when a change is observed. If a
value changes while a page loop is open, **retain dirty** so one follow-up frame renders the newer projection.
⛔ **Do not bypass the MAC-idle paint gate or the 2 Hz ordinary-frame throttle**, and ⛔ allocate no timer.

---
## 3 — The strip itself
- **Fixed slots from ONE layout table in the renderer** (§3.1) — ⛔ coordinates must not be repeated at individual
  draw sites. Order: `[mail][count] [home][age] [people][count] [key] [battery][voltage]`, **battery right-aligned**
  so `--` and `4.1V` do not move the icons before it.
- Strip occupies `y=0..8`; the existing rule at `y=9` stays; text on the existing `y=7` baseline; icons ≤ 7 px tall.
- ★ **Consume the FROZEN chrome only.** ⛔ The renderer must not query `g_node`, `ConfigService`, the counters or the
  battery while U8g2 replays later pages (§8.2) — that is the tearing this whole architecture exists to prevent.
- **Delete `fmt_volts` and call `ui_fmt_batt`.** ⚠ They have **diverged**: `fmt_volts` has **no width guard**, which
  is the §CHROME-1 R2.2 defect. ★ A native case already pins both branches' bytes — **make this a verified move, not
  a rewrite.**

---
## 4 — ★ THE CROSS-COMPILE GAP CLOSES HERE, AND YOU MUST MEASURE IT
§CHROME-1 and §CHROME-2 both reported that **no board toolchain compiles `firmware_ui_chrome.h` or
`firmware_ui_icons.h`**, and §CHROME-2 measured the icons being **discarded by `--gc-sections`** because nothing
referenced them. **This slice is the first to reference them.** ⇒ expect **real flash growth** from the bitmaps and
the strip renderer.
**Report:** their first `-Os` cross-compile result (⚠ including any `-Wformat-truncation`), the flash delta
**attributed** (bitmaps versus renderer code versus snapshot growth), and **RAM separately** — §8.1's amendment
requires the bitmaps to land in **flash, not RAM**, and a RAM rise there is a design error, not a cost.

---
## 5 — Traps
- ⛔ **Do not clear `dirty` while blanked** (§2.2) — the withdrawn test's harm.
- ⛔ **Do not freeze a stale chrome**: the reference updates **at the freeze**, not at the observation.
- ⛔ **No live authority during page replay.**
- ⛔ **No `Node` growth, no timer ids, no wire/NV change.** `sizeof(Node)` stays **221880**.
- ⚠ **`UiSnapshot` grows again** — report the `sizeof` and the per-board RAM consequence, as §CHROME-1 did (592 → 608
  then, for the one static `s_frame_snap`).
- ⚠ **Do not reinterpret a field** (§2): the mail value is session-unread, `team_total` is route rows, the home age
  is a **confirmation** age and **never** "connected".

---
## 6 — Tests
**§11.2's renderer bullets that apply to the strip**, plus §8.3.1's four behaviours:
- exact header slot coordinates and the **right-aligned battery field**;
- **no draw call exceeds `x=127` or `y=63`**;
- the strip is correct on **all eight U8g2 page replays** (a frozen-chrome failure shows up only here);
- bitmap/frame calls touch **no I²C outside `next_page()`**;
- **blanking remains edge-triggered with no repeated bus traffic**.

★★ **The blanked/no-bus test must BEGIN AFTER the blanking edge has completed** — the first `set_power_save(true)`
**legitimately issues one panel command**. **Pin this sequence:** (1) blank and let the edge complete · (2) record the
bus-call count · (3) change mail/home/team chrome while still blanked · (4) run subsequent UI ticks · (5) require
**zero ADDITIONAL bus calls**, **no frame opened**, and **`dirty` PRESERVED**.

⛔ **Ask of every check whether it could have come out otherwise.** This arc has recorded **five** instruments that
were green against the very defect they were written to catch, and §CHROME-1's re-gate found a sixth failure mode: **a
mutation whose target line has MOVED measures nothing** — so **re-run whole batteries, not just new entries**, and
report any `unusable`/match-count-0 result rather than treating it as a pass.

---
## 7 — Gate
1. `pio test -e native`, **then RUN the binary**. From **1673 / 83284 / 0**.
2. **Both UI probes** with control sets. ⓘ `probe_firmware_ui` is **the only venue that runs the whole chain** (real
   `DeviceHal` + real `lib/core` + real `src/firmware_ui.cpp`) — it is where the strip earns its coverage.
3. **`warning_census.sh`** at its pins (**174 / 178 / 178**, `-Wswitch` 0).
4. **Simulator inertness, the four-step structural proof**; `s18` smoke only, keystone **read from
   `simulation/BASELINE.md`**. ⛔ No 36-run, ⛔ no anchor-table edit. ⓘ **The anchor block's `s18_meshroute` row reads
   `9868cad3` / 269905 and DOES reproduce** — ⚠ `1cd21235` / 271629 appears only as the *pre* column of a
   before/after table (`:573`); §CHROME-2 misread it once. **No re-anchor is pending or owed.**
5. **Six board envs**; per-board RAM/flash **attributed**, measured against this brief's header figures.
6. ★★ **D2 explicitly:** `sizeof(Node)` **221880**; `kCap` 91, no timer.

**Report:** the five published fields and how the 64-bit age reaches the formatter unrecomputed · the freeze point and
the fifth frozen copy · the layout table · the strip's slots · **§8.3.1's four behaviours, each with its test and
mutation** · the five-step blanked/no-bus sequence · the `fmt_volts` deletion as a verified move · **§4's
cross-compile measurement, attributed** · native · both probes · census · the four-step corpus proof · per-board
RAM/flash · the D2 answer · exact final `git status --short` and that nothing was committed.
⛔ **Anything you cannot establish, say so plainly.**

**Stop and report rather than improvising if:** the invalidation cannot be built without clearing `dirty` while dark ·
the chrome cannot be frozen without a live query during replay · a strip slot cannot fit its §3.1 budget ·
`sizeof(Node)` moves · a corpus row moves · or the design and an amendment disagree.
