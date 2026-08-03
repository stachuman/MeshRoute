<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
> # ⛔⛔ WITHDRAWN — SUPERSEDED 2026-08-03. DO NOT REVIEW AGAINST THIS FILE.
>
> **The slice this brief was written to QA was measured and DISPROVEN before any file moved.** Its central premise —
> that a board-specific source tier exists to relocate — is false: genuinely board-discriminating code is **3 lines**
> (`board_name()`, `src/firmware_commands.cpp:339-349`), not the ~25 the spec claimed, because **23 of 26 board-macro
> sites are chip-family OR-chains** and were **double-counted on both sides** of that table.
>
> **Also falsified by measurement:** `boards/` is **PlatformIO's** (manifest + ldscript, `platformio.ini:97-98`), so the
> recommended directory drew the very objection used to reject `variants/` — and that objection was itself wrong, since
> the Adafruit core reads `variants_dir` from the board manifest, which ours does not set. **And §3's headline
> acceptance criterion is unachievable:** the controls have a genuine zero noise floor, yet a semantics-preserving
> `git mv` out of `src/` moves flash **+192 B**, RAM **−8 B**, and resizes **14 Arduino/ESP-IDF functions** via
> link-order xtensa relaxation.
>
> ★ **Owner ruling 2026-08-03: the broad split is PARKED.** What survives is a narrow **A0 placement slice** — move the
> *empty* `src/board_ui.cpp` seam to `variants/heltec_v3/board_ui.cpp`, rewire the Heltec envs, gate all eleven —
> plus **B61** as an independent one-line safety fix. **`variants/` is the ruled home** for per-board sources.
>
> ⇒ **Current authority:** the spec's **§0** (`docs/superpowers/specs/2026-07-31-onboard-oled-ui-design.md`) and the
> plan's **⛳ A0** (`docs/superpowers/plans/2026-07-31-onboard-oled-ui-phase-a.md`).
>
> **KEPT, NOT DELETED, for exactly two reasons:** §3's three false-pass traps (the ELF-md5/DWARF trap, PlatformIO's
> content-signature cache producing a 0-object "green" build, and concurrent-`pio` archive corruption) are **real and
> reusable on any build-system slice**; and this file is the audit trail for how a plausible refactor was stopped by
> measurement rather than by opinion.

---

# Phase 0 — board source split · QA OBJECTIVE

*Written for an independent QA agent reviewing the Phase 0 implementation. This file states **what the slice is for and
what must be true when it is done**. It is a QA brief, not a design: where it and the spec disagree, **the spec wins.***

**Authoritative documents — read both:**
- `docs/superpowers/specs/2026-07-31-onboard-oled-ui-design.md` → **§0 PREREQUISITE** (the four measured state findings
  and the corrected structure)
- `docs/superpowers/plans/2026-07-31-onboard-oled-ui-phase-a.md` → **⛳ PHASE 0**, steps 0.1–0.4

---

## 1. The objective

Move board-specific and chip-specific firmware code out of generic `src/` into per-board and per-chip directories, so
that **adding a board becomes adding a directory** instead of editing generic firmware.

```
src/            firmware_*.{h,cpp}      board-INDEPENDENT product logic
lib/hal/        iboard_ui.h iboard_rf.h the small composition interfaces
platform/       nrf52/  esp32/          chip-family code, SHARED by every board on that MCU
boards/         common/                 genuinely shared panel/render transport
                heltec_v3/ heltec_v4/ xiao_sx1262/ xiao_esp32s3/
                  └── board_port.* board_ui.* pins.h capabilities.h   (+ board_rf.* lora_fem.* on V4)
```

**Adopted from the 2026-08-03 review:** MeshCore-style *physical* source ownership · MeshRoute's **small** interfaces
rather than MeshCore's broad `Board` inheritance · **one composition point per board**.

★★ **This slice buys NO measurable behaviour, and that is not a finding.** Its entire value is that Heltec V4 (a second
silicon revision behind one product name, needing its own FEM/PA switching) and the OLED UI port then add a *directory*
rather than editing generic firmware. It is deliberately pre-emptive: it is cheaper before V4's conditionals exist than
after.

## 2. Acceptance criteria

| # | criterion | why it is a criterion |
|---|---|---|
| **1** | ★ **ZERO semantic change.** `git mv` + `platformio.ini` rewiring only | C1: refactor XOR feature, never both. A conditional that cannot move without changing meaning must be **left in place and recorded** — adapting it silently is the violation |
| **2** | ★★ **THREE tiers, not two.** `platform/{nrf52,esp32}/` exists and **no arch code is duplicated across boards** | Board macros ≈**25**, chip/arch macros ≈**65**. A `boards/`-only split absorbs the smaller half; applied naively it duplicates arch code per board — the U1 "fork a parallel one" rot |
| **3** | **One directory per BOARD, several envs per directory** — `heltec_v3` and `gateway_heltec` share one | 11 envs over ~4 boards: envs encode **role** (gateway/mobile/production) as well as board. Role stays in `MR_FEAT_*`. A directory bound to an env is a forked board |
| **4** | ⛔ **NOT under `variants/`** | That tree belongs to the **Arduino nRF52 core** — `variants/Seeed_XIAO_nRF52840/variant.{cpp,h}`, wired at `platformio.ini:117` and `:137`, and the core searches it for `variant.h` |
| **5** | **The conditional set was DERIVED, not copied** — the report contains a grep-derived table | The spec's "25" is a measurement to **reproduce**, not a list to trust. This arc got sweep scope wrong three times; the cure was always deriving mechanically |
| **6** | `boards/<board>/capabilities.h` is the **one place** a board's capabilities are declared (OLED, FEM, QSPI, battery ADC) — not inferred from scattered `#if`s | that is the deliverable that makes step 4 worth doing |

## 3. The gate

★ **All ELEVEN envs must link**, not the usual three:
`native · xiao_sx1262 · heltec_v3 · xiao_esp32s3 · gateway · gateway_heltec · gateway_esp32s3 · production ·
xiao_mobile · heltec_mobile · xiao_esp32s3_mobile`.
**D2's 3-env relaxation does NOT apply here** — it holds only while linker inputs are unchanged, and `build_src_filter`
changes for every environment.

**Also required, because `lib/hal/device_radio.h` is in scope and the simulator compiles `lib/hal/`:**
- native **via the binary**: `./.pio/build/native/program` — ⚠ `pio test -e native` prints a false *"0 test cases"*;
  the binary prints the real count
- the full **36-scenario** corpus, s18 keystone read from the **newest** `BASELINE.md` note (⚠ a scenario name greps to
  the *oldest* match first)

### ⚠⚠ Three traps that manufacture a false PASS — check for each explicitly

**① An ELF md5 CANNOT reproduce across a file move.** Paths are embedded in DWARF and in `__FILE__`, so the binary
changes even when the code is identical. **Reject "it builds" as a gate, and reject a chased md5 as evidence.** The
controls that are valid:
- **flash-bearing section sizes** (`.text` / `.rodata` / `.data`) — the standing rule is *diff flash-bearing SECTIONS,
  not object size*: an enlarged header inflates DWARF with 0 flash bytes
- the **RAM figure** — the trustworthy number; flash carries a ±32 B `__DATE__`/`__TIME__` floor
- the **`nm` symbol multiset** — same symbols, same sizes
- **warning multisets**, line-number-stripped, and `-Wswitch` **0**

⇒ per env, require: **RAM identical · section sizes identical (or the delta attributed) · symbol multiset identical.**

**② `touch` is a NO-OP for PlatformIO** — it keys on a content signature, not mtime. Without
`rm -rf .pio/build/<env>/{src,lib*}` the comparison runs against **stale objects**. A slice earlier in this arc reported
**0 objects compiled and 0 warnings on three envs** this way — a green light wired to nothing.
⇒ **require the object count beside every total**; a zero-object build must be visible, not inferred.

**③ Concurrent `pio` builds on one env corrupt the archive** and produce a false `FAILED` (observed 2026-08-02 as
`libBluefruit52Lib.a Error 1`, self-inflicted by two parallel builds). ⇒ **if a build failure is reported, confirm it was
serial before accepting it.**

⚠ **Rebuild `lus` before trusting any corpus run.** A stale simulator binary silently verified nothing on 2026-08-02 —
the md5 moving on a supposedly comment-only rebuild is what exposed it. Simulator is a **separate repo** at
`/home/staszek/lora-universal-simulator` (`build/orchestrator/lus <scenario.json> <out.ndjson>`).
⚠ **Do not redirect stdout away** when gating: `lus` prints `N events emitted, M assertion failure(s)` to **stdout**,
never into the ndjson, so any `"ok":false` grep over the event file is **vacuous**.

## 4. Out of scope — do NOT flag as missing

- OLED UI work (that is **Phase A**, gated behind this slice)
- the `IBoardRf` seam / the Heltec V4 RF port (its own spec: `2026-08-01-heltec-v4-radio-port-and-board-rf-seam-design.md`)
- **any** behaviour change
- the ~65 chip conditionals being *reduced* rather than merely **relocated** — consolidating them is a later slice
- `src/board_ui.cpp` remaining a thin seam: it is **32 lines with one `MR_FEAT_OLED` guard and no board conditionals
  today**, and it stays that way. The review's *"is likely to accumulate V3/V4 conditionals"* was a **prediction**, not
  a present defect

## 5. Process rules the implementation was held to

- ★ **D4: nothing is committed.** Green work is left uncommitted; the owner commits. `git add -A` is forbidden.
- ⚠ **`git checkout --` is forbidden in this tree** — it destroyed an uncommitted slice's work earlier in this arc.
- ★★ **Every premise in the implementation brief was declared a hypothesis**, and the report is expected to name the
  ones that turned out wrong. **A report with no disproven premises is itself worth questioning** — every slice in the
  preceding arc returned at least one.
- **A 0/N result means "cannot reach", never "inert."** Every negative needs a positive control proving the instrument
  can fire.
