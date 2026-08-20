<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §UI-15 slice 4 — pure provisioning state model + gates · dispatch brief · 2026-08-19

**Status: DISPATCHED.** ⛔ **NO DEVICE CONTACT.** ⛔ Build on the uncommitted UI-15 slice 1-3 tree (all QG-passed
2026-08-19); do not revert or re-derive any of it.
**Normative:** the UI-15 plan (`docs/superpowers/specs/2026-08-18-ui15-provisioning-implementation-plan.md`) —
**§4, §5 (including its closing pins), §6**, slice table §9 row 4: *"Pure provisioning state model + unsaved/conflict
gate + platform hiding — model + gating, no screens."* ⚠ The plan keeps withdrawn wording visible; **the latest
correction always wins.** Where the plan cites as-built `file:line`, re-verify before relying on it (V2) — the tree
has moved since it was written.

---
## Scope — model + gating ONLY

Home: `src/firmware_ui_model.h` (pure, native-tested via `test/test_firmware_ui_model.cpp`, mutation target
**`model`**). As-built today: `Settings{closed,browsing,editing}` (`:155`); `CfgRow::provision` (`:162`) is rendered
and **activating it refuses** — the §3.6.3 precondition is deliberately unimplemented (recorded in the design doc).
This slice replaces that refusal with the real model.

⛔ **NOT in this slice:** rendering/screens, chrome, adapters to `ProvisioningService`/the join service, live seams,
any console change. Those are slices 5/6. C1: no refactors ride along.

## Requirements (each is a plan section — read it there; summarised for the pins)

1. **§5 state model:** add **`Settings::provisioning`** *plus a separate explicit `Provision` enum* with EXACTLY the
   adopted arms: **`closed · menu · create_confirm · create_result · join_select · join_confirm · join_waiting ·
   join_result`**. ⛔ **Never a `bool in_provision`** — `:155`'s own warning (the binary-test-over-a-ternary-domain
   defect, five occurrences this arc). Enum dispatch stays `switch` with no `default:` where the file's idiom does
   (-Wswitch is gate-blocking).
   - Implement in THIS slice the transitions the model can honestly own without screens: entry (gated, below),
     `closed ↔ menu`, and the close-on-leave invariant. Arms whose flows arrive with the slice 5/6 adapters are
     DEFINED now but their flow transitions land there — **state done-vs-missing in code**: which arms are live,
     which await slices 5/6, and why.
2. **§5 pins (plan lines 200-203):**
   - ★ **provisioning is CLOSED whenever SETTINGS is left** — the same rule `editing` already obeys
     (`sync_settings`). Mutation-tested.
   - ⛔ **No sixth cycle slot** — PROVISION stays inside SETTINGS; `Screen` does not change.
   - **Confirmations open with BACK selected** — wherever this slice adds a confirm-state cursor/default, its
     initial value is BACK (the flow that reads it may arrive in 5/6; the DEFAULT is model state and lands now).
3. **§4 unsaved-draft gate, two DISTINCT states — the remedy differs and v1 conflated them:**
   - **`conflict()`** ⇒ the note says **`RELOAD OR DISCARD`** (⛔ never suggest SAVE — that operation refuses);
   - otherwise **`config_unsaved()`** ⇒ **`SAVE OR DISCARD`**;
   - only when neither stands does PROVISION open.
   - ⛔ **PROVISION never silently saves on the operator's behalf** (C2) — pin with a COUNTED discriminator (zero
     save calls through the gate), not a state assertion.
   - U1: consume the EXISTING `conflict()` / `config_unsaved()` predicates (`firmware_config_service.h`); ⛔ never a
     second spelling of either.
4. **§6 availability / platform hiding:**
   - govern children by **the ACTUAL CHILD PREDICATE — principally `MR_N_LAYERS < 2`**, which is what gates
     `join`/`create`/`team`;
   - ⛔ **do NOT hide static join merely because `MR_FEAT_TEAM` is off** — static join has nothing to do with the
     team plane;
   - per [[B209]]: a child with no support is **HIDDEN** (the conditional-row pattern `CfgRow::reload` already uses,
     `firmware_ui_model.h:201`), ⛔ **never a refusing stub**;
   - `MR_FEAT_OLED` gates the screens; the child predicates gate the children — ⛔ do not conflate. C3: the model is
     pure — platform facts arrive as PARAMETERS (the `settings_rows(bool ble_row, bool conflict)` pattern), never as
     `#if` inside the model.

## Pins

1. The `Provision` enum has exactly the eight adopted arms; no boolean shadow state exists.
2. **Close-on-leave:** leaving SETTINGS from ANY provisioning state returns `Provision::closed` — mutation dropping
   the reset goes RED.
3. **Gate matrix, all three cells:** conflict ⇒ `RELOAD OR DISCARD` note + PROVISION does not open; unsaved (no
   conflict) ⇒ `SAVE OR DISCARD` note + does not open; clean ⇒ opens to `menu`. The conflict-vs-unsaved PRIORITY
   (conflict wins) is its own case — that is the §4 conflation, re-armed.
4. **Zero saves through the gate** — counted, under both refusal cells.
5. **Hiding:** each child's visibility follows its own predicate; the static-join child survives `MR_FEAT_TEAM`-off
   (as a model parameter); no refusing stub exists for a hidden child.
6. **Confirm-state defaults are BACK.**
7. Defect-specific mutations RED at match count 1 (at minimum: close-on-leave dropped; gate inverted or a cell
   collapsed; conflict/unsaved remedies swapped; a child predicate widened); unchanged positive controls stay GREEN.

ⓘ ★★ **[[B217]] STANDS: re-pin `BASE_CASES`/`BASE_ASSERTS`** in `tools/probe_ui_model_mutations.py` (current pin
**1767 / 85636**) when the native counts move, derivation recorded in place, and **confirm the battery actually
RAN**. Restore mutation sources exactly; `git diff --check` clean.

## Verification you run (QG runs the full gate separately — no boards, no corpus)

1. `pio test -e native`, **then RUN `./.pio/build/native/program`** — report the binary's real counts, 0 failed.
2. The **`model`** mutation battery — RED count + proof it ran (and that the pre-existing entries stayed RED).
3. `git diff --check` clean.

## Report

The exact model surface added (enum, fields, functions, signatures) · which transitions are live now vs deferred to
5/6 and where that is stated in-source · each pin with its test-case name · each mutation with its match count ·
final native counts and the new pin · proof the battery ran · exact final `git status --short`.

⛔ **NEVER `git commit` / `git add` / `git checkout --`.** ⛔ Do not touch the bug register, the bench script, any
plan/brief, `tracker.md`, `B164.md` or `docs/manual/`. ⛔ Evidence lands IN THE REPO. ⛔ **Stop and report** if the
plan's §4/§5/§6 requirements conflict with the as-built model in a way the plan does not already resolve — do not
rule on it yourself.
