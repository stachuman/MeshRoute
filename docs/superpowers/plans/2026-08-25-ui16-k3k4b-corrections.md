<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §UI-16 slice 8b — K3+K4 corrections (B243 hook · B244 re-anchor · count fence) · dispatch brief · 2026-08-25

**Status: DISPATCHED. This is the QG-ruled corrective slice for K3+K4** (QG HOLD 2026-08-25, three blockers,
one recommended slice). Authorities: the K3+K4 brief + spec §K3/§K4 (:889-936) as before, plus the register
rows **[[B243]]** and **[[B244]]** (`docs/2026-07-30-open-bug-register.md:133-134`). ⛔ **NO DEVICE CONTACT.**

## Fix 1 — B243: the failed-save note gets its device path (K4 pin 2 satisfied for real)
- ⛔ The block was structural: F-10 forbids forwarding a failed push through `mr_ui_on_push`, the only door in
  `lib/hal/mr_ui.h`. ⇒ **a SECOND, narrow door**: `mr_ui_on_team_key_unsaved(...)` beside the existing six
  (the K3 coder sized it at ~4 lines there + 1 call in the drain loop's `else` — `src/fw_main.cpp:1344` area).
  fw_main stays glue: the call, ⛔ never a decision (U3).
- The hook lands on the ALREADY-BUILT K4 failure arm (`TEAM KEY ACTIVE` / `NOT SAVED` / `LOST ON REBOOT` —
  the wording, negatives and mutations exist; this fix is the WIRE). Every K4 negative applies to the new door
  equally: ⛔ no navigation, no cursor, no emergency write, **no wake** — pin them through the new path too.
- ⚠ **This touches `lib/hal`** — the corpus/D2 argument is owed: state precisely what the simulator compiles
  (verify whether `lib/hal` is in the sim build at all; the expectation is that `mr_ui.h` stubs to no-ops off
  MR_FEAT_OLED and the sim never links it — ARGUE it from the build files, don't assert it) and why no emitted
  event can move. If you cite the keystone, READ it from `simulation/BASELINE.md`. QG runs the two board envs
  after this slice — your half is the argument + a clean native build.
- Mutations (each RED at match count 1): the hook dropped from the drain loop (⇒ B243 restored — the headline)
  · the hook called on SUCCESS too (⇒ both notes race) · the else inverted (failure forwards the raw push —
  F-10 broken from the new side) · the hook navigating/waking (through the new path).
- The probe proves it on the REAL path: a real failed persist (the fake store refusing) reaching the panel as
  the ruled three rows, and the success path unchanged.

## Fix 2 — B244: W7/W9 re-anchored — the WHOLE board probe leaves green
- Re-pin `tools/probe_board_ui`'s W7 and W9 to the AS-BUILT §UI-17 S1/S3 text (`ui_status_have_fix`, the
  passive/entered TEAM rework) at match count 1 each, controls intact and RED. ⛔ Re-anchor, never delete or
  weaken — the wiring facts they pin still hold, only their regexes went stale.
- Exit criterion: `tools/probe_board_ui/run.sh` exits **0** with **54/54** wiring + all controls RED. A
  standing-red gate is the normalization-of-red shape (B244) — this slice touched that instrument, so it
  leaves it whole.

## Fix 3 — the GrantSave count fence (the N6b round-2 precedent, applied again)
- `GrantSave` (`src/firmware_team_keyring.h:480` area) gains a **terminal `count` sentinel**; the totality case
  (`test/test_firmware_team_keyring.cpp:1001` area) iterates `0 .. count-1` — ⛔ no hand-written last-enumerator
  bound. Wordedness stays compiler-bounded: the rendering switch stays `default`-less (`-Werror=switch` is a
  blanket flag), with the sentinel's arm written explicitly, ⛔ never via `default:`.
- A state added without a word ⇒ build failure; added with a word ⇒ swept automatically. Say so in-source.

## Operational contract (standing)
- C1: these three fixes only. Everything landed survives. The battery runner is the parallel one — invoke per
  its header; iterate; ONE full pass per touched target (`teamkeyring` at least; `model`/`uisend` only if
  their target files change); sync `PIN_*` with derivation; ⛔ never pipe the runner; ⛔ no background pollers;
  ⛔ never edit a battery's target mid-run.
- Verification you run: native (RUN the binary, 0 failed, 0 warnings) · touched-target battery full passes ·
  **BOTH probes** (`probe_firmware_ui` both arms; `probe_board_ui` to exit 0, 54/54) · `git diff --check`
  clean · `git diff --stat -- lib/` shows EXACTLY the `mr_ui.h` hook and nothing else under `lib/`.
- ⛔ NEVER `git commit` / `git add` / `git checkout --`. ⛔ Do not touch the register, the bench script, any
  plan/brief, either UI spec, the design doc, `tracker.md`, `docs/manual/`, `platformio.ini`,
  `tools/ccache_native.py`, or parallel-session files. DRAFT in your report: the now-writable §7.5
  failed-save bench step (the B243 close makes it testable — a forced save failure on metal if one can be
  safely provoked, else the not-run rule).

## Report
The hook's exact shape (`mr_ui.h` lines + the else call) · the corpus argument for the `lib/hal` touch · the
failure path proven on the real probe path + the success path unchanged · W7/W9 re-anchored with match counts
+ the probe's clean exit line · the count fence's mechanism · the mutation ledger + full passes · native +
PIN derivation · the DRAFTED bench step · exact final `git status --short`.
