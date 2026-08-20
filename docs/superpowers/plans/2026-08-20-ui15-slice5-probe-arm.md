<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §UI-15 slice 5 — CORRECTION after a QG HOLD: the child-enabled probe arm ([[B225]]) · 2026-08-20

**Status: DISPATCHED. One coverage blocker; the implementation itself passed QG** (native 1800/86200/0, all three
batteries, boards, corpus — all green). ⛔ **NO DEVICE CONTACT.** ⛔ Build on the current tree (HEAD `9cef214` + the
uncommitted, QG-passed-except-this slices 4-5); revert nothing.

---
## The gap — [[B225]], verified at the probe
⚠ **CORRECTED IN PLACE 2026-08-20 (QG): this section originally called the existing `DEFS` a `heltec_v3` mirror —
that repeated the probe's own drifted comment.** The truth: the old arm carries `-DMR_N_LAYERS=2` and represents
the **LAYERED/GATEWAY shape**; the arm added by this correction represents the **`heltec_v3` UI-relevant
configuration** (`MR_N_LAYERS` unset ⇒ 1). Under the layered shape both provisioning children hide (slice 5's own
parent-row rule), so **`draw_provision_screen` (`src/firmware_ui.cpp:1143`) — real shipped rendering — was
structurally unreachable by the probe.** The slice added only the hidden-parent check (P7).
Model and chrome tests cannot detect an omitted, swapped, or mis-wired draw call in the production TU — that is the
probe's whole reason to exist (its header says so).

## Required correction (QG-ruled, verbatim in intent)
**A child-enabled feature-probe arm** that exercises the real renderer:
1. **A SECOND compile of the TU mirroring a REAL leaf env's UI-relevant `-D` flags** (read `platformio.ini`; pick a
   leaf env with OLED + team support and state which). ⓘ "UI-relevant", ⛔ not "exact": the env's radio, board-wiring
   and telemetry flags are read by no TU in the probe's link and are **intentionally omitted**. ⛔ The probe's own
   drift warning forbids an invented configuration — "the probe measures a configuration the board never builds" is
   the vacuous-instrument failure its controls exist to catch. Keep the existing layered arm; the two arms assert
   their own expectations.
2. Drive **menu → CREATE TEAM confirmation → result** through the REAL `draw_provision_screen` + the real gesture
   path. The natural seam for outcomes is `UiModel::attach_provision(IUiProvision&)` — a scripted probe fake
   returns `created` / `phy_differs` / `save_failed` / `refused` etc.; ⛔ the fake mimics the ADAPTER's answers,
   not invented shapes.
3. **Pins:**
   - the confirmation opens with **BACK selected**, and BACK performs **ZERO transaction / write / apply** (the
     probe already counts writes/applies — extend the same counters);
   - the optional **`REPLACES <fp>`** warning renders exactly when already in a team, absent otherwise;
   - success output: **`TEAM CREATED` · the full 8-hex id · the SHARED fingerprint** (must equal
     `ui_fmt_team_fingerprint` of the id — assert the value relation, not just presence) · the back instruction;
   - at least **PHY-mismatch** (`PHY DIFFERS` / `USE SERIAL`) and **persistence/refusal** results render their
     ruled/drafted text.
4. **Controls (the probe's discipline — tempting wrong fix, all three worthlessness checks apply):** mutations that
   **remove or swap the renderer lines** — at minimum: the success headline dropped; id and fingerprint lines
   swapped/dropped; the `REPLACES` warning unconditional; the refusal detail dropped; a result drawn on the wrong
   arm. Each must turn the probe RED; sed must match; the mutant must compile.

## Constraints
- ⛔ **Renderer/model/chrome behaviour does NOT change** — this is a probe-only correction. If probing reveals a
  REAL renderer defect, **STOP and report** (register first, fix in its own dispatch).
- `run.sh`'s md5 tree-guard covers the files it reads — extend the list if you add probe sources.
- The existing arm's checks and controls stay green/RED; report both arms' counts.
- Native suite and batteries are untouched by intent; if a count moves anyway, [[B217]]'s re-pin duty applies
  (current pin **1800 / 86200**).

## Verification you run (QG re-runs the full gate)
1. `tools/probe_firmware_ui/run.sh` — both arms, checks + controls, full output counts.
2. `pio test -e native` + run the binary — confirm 1800/86200/0 unmoved (or re-pin with derivation if moved).
3. `git diff --check` clean.

## Report
Which leaf env the new arm mirrors and its UI-relevant `-D` flags (omissions stated) · each QG-required pin with its check name · each control
with proof it reddens (and compiles, and seds) · both arms' final counts · native counts · exact final
`git status --short`.

⛔ **NEVER `git commit` / `git add` / `git checkout --`.** ⛔ Do not touch the bug register, the bench script, any
plan/brief, `tracker.md`, `B164.md` or `docs/manual/`. ⛔ Evidence lands IN THE REPO.
