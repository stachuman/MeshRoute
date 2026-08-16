<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §CHROME-2 — the canvas extension: generic bitmap + outline primitives · dispatch brief · 2026-08-16

**Status: DISPATCHED 2026-08-16 on an owner directive.** ★ Role split: the QA-gate wrote this brief and verifies your
claims at the code; **the OWNER runs QG and rules.**
⛔ **Never `git commit`, never `git add`, NEVER `git checkout --` anything.** The tree carries **§CHROME-1
uncommitted** (`src/firmware_ui_chrome.h`, `src/firmware_ui_icons.h`, `test/test_firmware_ui_chrome.cpp`,
`src/firmware_ui_model.h`, `tools/probe_ui_model_mutations.py`) — **build on it, do not revert or restyle it.**

**Normative design:** `docs/superpowers/specs/2026-08-15-heltec-mobile-status-navigation-ui-design.md` §8.1 and
§11.2. ⚠ **Read its NINE amendment blocks** (search `2026-08-16`); where an amendment and the original disagree,
**the amendment wins**.

**Baseline (with §CHROME-1 applied):** native **1673 / 83284 / 0** · `lus` `b77cfd3d` · `s18` `9868cad3` / 269905 ·
`sizeof(Node)` **221880** · census **174 / 178 / 178** · board RAM/flash: `heltec_v3` 216180 / 1281332 ·
`heltec_mobile` 215700 / 1276156 · `gateway_heltec` 241348 / 1246904 · the three non-OLED envs byte-identical to
`b8929e5`.

---
## 0 — Scope: §10 slice 2, and NOTHING else

**BUILD:** the two **generic** canvas primitives, their Heltec implementation, and board-probe coverage.
⛔ **OUT:** ⛔ **no UI semantics** · no renderer change · no status strip, no rail, no header slots · no repaint
invalidation · no body migration. Those are slices 3-4.

⛔⛔ **§8.1 IS A HARD BOUNDARY: `board_ui.cpp` MUST NOT gain semantic calls** such as `draw_mail_icon()`, **and must
not include `firmware_ui_model.h`, `firmware_ui_chrome.h` or `firmware_ui_icons.h`.** The board copies pixels; the
firmware owns icon identity, placement and state selection. **The V4 port must be able to reuse the same bitmaps and
renderer**, which is only true if the board knows nothing about them.

---
## 1 — Verified state

**The existing canvas** (`variants/heltec_v3/board_ui.h`): `board_init` `:37` · `begin_frame` `:38` · `next_page`
`:39` · `set_font` `:40` · `draw_text` `:41` · `draw_hline` `:42` · `set_power_save` `:43` · `button_pressed` `:44` ·
`arm_button_wake` `:74` · `disarm_button_wake` `:90` · `battery_sample_mv` `:95`. **Match this file's idiom** (U3).

★★ **THE DESIGN'S BYTE FORMAT MAPS 1:1 ONTO U8g2'S NATIVE CALL — no conversion, no per-board reinterpretation.**
- `drawFrame(x, y, w, h)` — `.pio/libdeps/heltec_mobile/U8g2/src/U8g2lib.h:227`;
- `drawXBM(x, y, w, h, bitmap)` — `:254`, implemented by `u8g2_DrawXBM` (`clib/u8g2_bitmap.c:167`) row-by-row through
  `u8g2_DrawHXBM`, i.e. **row-major, LSB-first, rows padded to whole bytes** — exactly §8.1's amended contract.
- ⚠ **`drawXBM` vs `drawXBMP`: use `drawXBM`.** The `P` variant is the **PROGMEM** form for AVR; on ESP32 and nRF52
  flash is memory-mapped, and §CHROME-1's assets are `inline constexpr` at namespace scope. **Swapping them is a
  silent corruption on one platform and a needless indirection on the other.**

⚠ **NAMING COLLISION, decide it explicitly and record the choice.** `draw_frame` **already exists** as
`src/firmware_ui.cpp:946`'s whole-screen composer, inside that file's anonymous namespace (`:112`-`:965`). The design
names the new primitive `draw_frame(x, y, width, height)`. ⇒ in slices 3-4 the renderer would call **both**, one
meaning *"compose the entire screen"* and one meaning *"draw a rectangle outline"*.
ⓘ The house style already qualifies canvas calls (`mrui::draw_text(...)`), so keeping the design's name is survivable
— but **a distinct name (e.g. `draw_rect`) is cheaper than a reader's mistake.** ★ **Choose, say which, and if you
deviate from the design's name, record the deviation in the design as an amendment.**

---
## 2 — What to build
1. **`draw_bitmap(int x, int y, int w, int h, const uint8_t* bits)`** — generic, no semantics, forwarding to
   `drawXBM`. **Document the byte-order contract at the declaration**, referring to §CHROME-1's
   `firmware_ui_icons.h` header block as the single source of that contract (⛔ do not restate it divergently).
2. **`draw_frame`/`draw_rect(int x, int y, int w, int h)`** — a one-pixel outline, forwarding to `drawFrame`.
3. Both in **`mrui::`**, declared in `board_ui.h`, implemented in `board_ui.cpp`, **compose-only** — they write the
   page buffer and ⛔ **must not touch the bus outside the existing `next_page()` boundary** (§11.2).

---
## 3 — Traps
- ⛔ **No semantics on the board** (§8.1) — the boundary is the point of the whole redesign.
- ⛔ **No bus traffic outside `next_page()`.** `begin_frame()` *"composes only, touches NO bus"* (`board_ui.cpp:263`);
  the new primitives must keep that true.
- ⚠ **Bounds:** no draw may exceed `x=127` / `y=63`. U8g2 clips silently, so a probe that only checks "it did not
  crash" proves nothing — **assert the coordinates you passed**.
- ⛔ **Do not alter `begin_frame` / `next_page` / `set_power_save` behaviour**, and ⛔ do not disturb §B200's
  `arm_button_wake` / `disarm_button_wake` / `clear_button_wake_state` — that arc closed two days ago after five
  rounds.
- ⛔ **No `Node` growth, no timer ids, no wire/NV change.**

---
## 4 — ★★ TWO CLAIMS YOU MUST **NOT** MAKE IN THIS SLICE

**(a) The cross-compile gap does NOT close here.** §CHROME-1 reported that **no board toolchain compiles
`firmware_ui_chrome.h` or `firmware_ui_icons.h`**, because nothing includes them. ⛔ **Slice 2 does not change that** —
the board may not include them (§8.1), and the renderer that will is **slice 3**. ⇒ **their flash cost, board ABI and
`-Wformat-truncation` behaviour under a real `-Os` cross-compile REMAIN UNVERIFIED.** Say so plainly.

**(b) §11.2's *"the new canvas calls are linked and exercised in every OLED environment, not merely present in
source"* CANNOT be satisfied by this slice as written** — nothing calls the primitives until slice 3, so the linker
may legitimately drop them. ⇒ **Either** state that the bullet is deferred to slice 3 and why, **or** add a
deliberate, clearly-labelled compile/link anchor — ⛔ **but an anchor must not be a renderer change.** **Decide, say
which, and do not report the bullet as met if it is not.**

---
## 5 — Tests
- **`tools/probe_board_ui/`**: cover both primitives against the **real** `board_ui.cpp` with the existing fakes —
  the coordinates and dimensions actually passed through, the byte pointer forwarded unchanged, the U8g2 call chosen
  (`drawXBM`, ⛔ not `drawXBMP`), and **no bus traffic outside `next_page()`**.
- **Controls that must be RED:** each primitive deleted · coordinates transposed (x↔y, w↔h) · the `P` variant
  substituted · a bus call added at compose time · the outline drawn as a filled box (`drawBox`).
- ★ **A control that only proves "a function was called" is not coverage** — this arc has recorded **five** instruments
  that were green against the very defect they were written to catch. **Ask of each: could it have come out
  otherwise?** Match counts printed, sources restored and md5-verified.

---
## 6 — Gate
1. `pio test -e native`, **then RUN the binary**. From **1673 / 83284 / 0** (this slice may add none).
2. **Both UI probes** with their control sets; the new board-probe checks reported explicitly.
3. **`warning_census.sh`** at its pins (**174 / 178 / 178**, `-Wswitch` 0).
4. **Simulator inertness, the four-step structural proof** (pre-md5 → rebuild → **zero relevant build actions** →
   identical md5), `s18` as smoke only with the keystone **read from `simulation/BASELINE.md`**. ⛔ No 36-run, ⛔ no
   anchor-table edit.
5. **Six board envs**; per-board RAM/flash **with the split attributed**, measured **against the §CHROME-1 tree** (the
   figures in this brief's header), not against `b8929e5`. ⚠ **The three non-OLED envs must stay byte-identical.**
6. ★★ **D2 explicitly:** `sizeof(Node)` **221880**; `kCap` 91, no timer allocated.
7. ⛔ **Prove renderer inertness:** `git diff -- src/firmware_ui.cpp` must be **empty**.

---
## 7 — Method
- ★★ **Instruments that cannot fail — five in this arc.** ⓘ And the newest lesson from §CHROME-1's re-gate: **a
  mutation whose target line has MOVED silently measures nothing** — its runner reported *unusable* rather than a
  pass, which is why the whole battery is re-run, not just the new entries.
- ★ **When a test encodes an order or a call, check it against the code that actually executes**, not against prose.
  ⓘ Four of this arc's five bad instruments would have been caught by that one question.
- ⛔ **PROVENANCE (ledger §3):** never claim an owner or QA approval; **never quote an owner ruling** — reported form
  only.

**Report:** both primitives with `file:line` · the naming decision and any design amendment it required · why
`drawXBM` and not `drawXBMP` · the bus-discipline evidence · every probe check with its controls and match counts ·
**§4's two non-claims, stated** · native · both probes · census · the four-step corpus proof · per-board RAM/flash
attributed against the §CHROME-1 baseline · the D2 answer · renderer-inertness evidence · exact final
`git status --short` and that nothing was committed. ⛔ **Anything you cannot establish, say so plainly.**

**Stop and report rather than improvising if:** the primitives cannot be added without the board learning a UI type ·
a bus call is unavoidable at compose time · `sizeof(Node)` moves · a corpus row moves · or the design and an
amendment disagree.
