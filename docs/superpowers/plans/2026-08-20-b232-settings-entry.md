<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# [[B232]] — SETTINGS single-entry · dispatch brief · 2026-08-20

**Status: DISPATCHED. Owner-ruled UI change; read the [[B232]] register row first — it carries the mechanism and
the four-item ripple list.** ⛔ **NO DEVICE CONTACT.** Build on the current tree (B230/B231/B233 are QG-passed and
uncommitted — revert nothing).

## The ruling (reported form)
The SETTINGS screen presents **ONE visible entry** (owner's suggested lexeme: *"enter settings"* — match the house
label idiom and report the exact string); **`short` passes the screen in ONE press** like every other screen;
**`double` enters the row-browsing** — the enter-by-double idiom the PROVISION child menu already uses.
As built: `sync_settings` auto-enters `browsing` on arrival (`src/firmware_ui_model.h:2036`) and `advance_or_next`
walks every row before the screen advances — up to 9 presses to pass.

## Scope + the ripple list (all four are REQUIRED, from the register row)
1. ⛔ **The ConfigService still OPENS ON ARRIVAL** (`:2041`) — the §3.6.1 baseline, the conflict latch and the rail
   badge all depend on it; deferring `open()` to browsing-entry is the tempting wrong fix (mutation-test it).
2. **The remedy words stay visible from the CLOSED single-entry view**: when unsaved/conflict stands, SETTINGS
   still PRINTS `CFG* UNSAVED` / `CFG! RELOAD` (design §6 forbids icon-only errors) — the badge table's every cell
   re-proved from the closed view.
3. **Close-on-leave and the [[B223]] guard are entry-state-dependent** — re-verify their mutations still redden
   (the eight-arm `provision_reset_on_leave` coverage, M54/M67, and the editor-never-outlives-screen rule).
4. **It reverses a documented §UI-14 behaviour** ⇒ the design doc needs a correction-in-place — ⛔ the design doc
   is QG-owned: **DRAFT the correction text in your report, do not edit it.** Same for the bench script: Part
   25.4 / Part 20 press sequences change — **draft those edits in the report** (⛔ the bench script is
   supervisor-landed after PASS).

## A derived default, stated rather than silently chosen — flag it in your report for the owner:
**Exiting browsing (the BACK row, or the walk off the last row) returns to the CLOSED single-entry view** — the
PROVISION-child containment idiom — so one more `short` then passes the screen. ⛔ Not straight off the screen:
that would re-create the "where am I" jump the ruling exists to remove. If you find this conflicts with an
existing documented rule, STOP and report instead of choosing.

## Pins
1. Arrival on SETTINGS = the closed view; `short` moves to the NEXT SCREEN in one press (cycle parity). The
   mutation re-arming auto-browse-on-arrival goes RED at match count 1.
2. `double` enters browsing; everything inside browsing/editing is UNCHANGED (existing cases stay GREEN).
3. `open()` still called on ARRIVAL — counted (the service records the open; the conflict latch fires while the
   closed view is up) — the defer-to-browsing mutation RED.
4. Unsaved/conflict remedy text renders from the closed view; all badge-table cells re-proved.
5. Browsing exit → closed view; close-on-leave holds from EVERY state (existing guard mutations re-run RED).
6. The PROVISION flows are unchanged once inside browsing — the probe's P7/P15/P16 phases updated for the new
   press prefix and still green, both arms, all controls.
7. Every defect mutation RED at match count 1; unchanged positive controls GREEN.

ⓘ ★★ **[[B217]]: re-pin `BASE_CASES`/`BASE_ASSERTS`** (read the CURRENT pin — 1837/86986 at last report — from
the harness) when counts move, derivation in place; confirm each battery RAN; restore mutation sources exactly.
⚠ Never run a probe and a battery concurrently.

## Verification you run (QG runs the full gate — no boards, no corpus)
1. `pio test -e native` + RUN the binary — real counts, 0 failed.
2. The `model` battery (+ any target whose source you touch) — RED counts + proof each ran.
3. `tools/probe_firmware_ui/run.sh` — both arms + all controls (the press sequences change; update expectations),
   no stray shell output.
4. `git diff --check` clean.

## Report
The exact entry-row label · the browsing-exit decision as flagged · each pin with case name and match count ·
the DRAFTED design-doc correction text and the DRAFTED bench-script edits (⛔ neither file touched) · final native
counts and the pin · battery/probe proofs · exact final `git status --short`.

⛔ **NEVER `git commit` / `git add` / `git checkout --`.** ⛔ Do not touch the bug register, the bench script, any
plan/brief, `tracker.md`, `B164.md`, `docs/manual/`, the design doc, or anything under `lib/`. ⛔ C1: no refactors
ride along. ⛔ New strings beyond the entry label: STOP and report.
