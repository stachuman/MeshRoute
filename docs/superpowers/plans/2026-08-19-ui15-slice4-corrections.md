<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §UI-15 slice 4 — CORRECTIONS after a QG HOLD · dispatch brief · 2026-08-19

**Status: DISPATCHED. Two blockers ([[B222]]/[[B223]]), both verified at the code.** ⛔ **NO DEVICE CONTACT.**
★ The slice's mechanics are sound — the eight-arm enum, the `ProvRow` model, the §4 gate matrix, the §6 hiding and
their mutations all passed QG. **These two are scope and instrument defects, not behaviour defects.**
⛔ Build on the current tree (HEAD `9cef214` + the uncommitted slice 4); do not revert or re-derive the slice.

---
## Blocker 1 — [[B222]] ⛔ TWO SLICE-5/6 TRANSITIONS LANDED EARLY
`provision_menu_gesture` (`src/firmware_ui_model.h:1942-1943`) enters **`create_confirm`** on CREATE TEAM and
**`join_select`** on JOIN NETWORK. The slice-4 brief permitted only gated entry, `closed ↔ menu` and close-on-leave;
these are the child FLOW entries deferred to slices 5/6, and their flow tests rode along
(`test/test_firmware_ui_model.cpp:3238` and neighbours).

**Required (QG-ruled):**
- **KEEP** the eight states, `ProvRow`, `provision_rows`, the labels, `ProvConfirm`, and the menu's cycling/BACK.
- **MOVE OUT** the two child-entry transitions and every test that flows through them. In slice 4, activating
  `create_team` / `join_static` does **nothing** — with the done-vs-missing statement in code naming slice 5/6 as
  the owners of those entries. (⛔ Not a refusing stub in the B209 sense: the rows are VISIBLE because their
  predicates hold; only the flow behind them is pending. Say exactly that in-source.)
- **Test the BACK confirm-default DIRECTLY as model state** — the initial `ProvConfirm` value and what
  `enter_provision` establishes — not by flowing through an entry that no longer exists this slice.
- Re-aim or move the mutations that depended on the removed flows (M62/M63/M66 and any other): every mutation KEPT
  in slice 4 must target behaviour REACHABLE in slice 4 and stay RED at match count 1; mutations belonging to the
  moved flows move to a clearly-marked pending block or are deleted with the flows' tests (state which, and why).
  ⛔ No kept instrument may be satisfiable-by-construction — that is Blocker 2's disease.

## Blocker 2 — [[B223]] ⛔⛔ THE CLOSE-ON-LEAVE GUARD GUARDS A DIFFERENT PATH
The brief required: *leaving SETTINGS from ANY provisioning arm returns `Provision::closed`, with a mutation
dropping THAT reset going RED.* As landed:
- `settings_follow_screen()`'s reset is **unreachable and unmutated** (`src/firmware_ui_model.h:1721-1730`) —
  honestly stated, but stating a gap does not discharge a required guard;
- **M54 mutates `close_provisioning()`** — a different path (`tools/probe_ui_model_mutations.py:669`);
- the test *"walking OFF the SETTINGS screen closes provisioning"* **closes the sub-view before leaving**, so it
  cannot exercise the reset it names (`test/test_firmware_ui_model.cpp:3293`);
- only 3 of the 8 arms were checked.

**Required (QG-ruled):**
1. **Extract the off-screen reset into a small PURE helper** used by `settings_follow_screen()` — the
   [[B212]]/[[B220]] move, fourth time: hoist the decision so the suite can drive it, leave the call site a forward.
2. **Drive ALL EIGHT `Provision` arms through that helper directly** in the native suite — each arm comes back
   `closed` (and the confirm cursor re-anchors to BACK, matching the invariant the in-source comment states).
3. **Mutate the helper's actual `Provision::closed` assignment** — RED at match count 1. Retire or re-title the
   vacuous "walking OFF" test so no test claims a path it does not take; M54 (the `close_provisioning` half) may
   stay as the reachable-path control, clearly labelled as such.

## Documentation correction (QG-ruled, recorded here — the latest correction wins over the slice-4 brief)
The slice-4 brief's "⛔ NOT in this slice: chrome" **did not account for the -Wswitch reader sweep**: adding
`Settings::provisioning` NECESSARILY updates every exhaustive `switch` reader of that enum, wherever it lives —
`src/firmware_ui_chrome.h:270`'s nav mapping included. **That mechanical arm is KEPT and is hereby recorded as a
required part of the slice: an enum-arm addition always carries its reader sweep** (the -Wswitch gate is the point,
not an exception to scope). ⛔ Nothing beyond the mechanical arm is authorised in chrome.

## Pins
1. Slice 4's REACHABLE surface after the correction: gated entry → `menu`, menu cycling, BACK, close-on-leave —
   and nothing deeper. `create_confirm`/`join_select` are UNREACHABLE by gesture this slice (asserted, not argued).
2. The eight-arm enum, gate matrix (all three cells + priority + counted zero-saves), and §6 hiding cases are
   UNCHANGED and stay GREEN — unchanged positive controls.
3. The pure close helper: all eight arms → `closed`, mutation on its assignment RED at match count 1.
4. BACK confirm-default asserted directly as model state.
5. Every kept mutation still RED at match count 1; none aimed at removed flows; the battery has no entry that
   cannot fail.

ⓘ ★★ **[[B217]] STANDS: re-pin `BASE_CASES`/`BASE_ASSERTS`** (current pin **1782 / 85815** — it WILL move, both
directions are in play: removed flow tests, added helper cases), derivation recorded in place, and **confirm the
battery actually RAN**. Restore mutation sources exactly; `git diff --check` clean.

## Verification you run (QG runs the full gate separately — no boards, no corpus)
1. `pio test -e native`, **then RUN `./.pio/build/native/program`** — real counts, 0 failed.
2. The **`model`** mutation battery — RED count + proof it ran; all kept entries RED.
3. `git diff --check` clean.

## Report
What moved out and where its pending half is stated in-source · the helper's signature and call site · each pin
with its case name and match count · the mutation ledger delta (kept / re-aimed / moved-out, each with why) · final
native counts and the new pin · proof the battery ran · exact final `git status --short`.

⛔ **NEVER `git commit` / `git add` / `git checkout --`.** ⛔ Do not touch the bug register, the bench script, any
plan/brief, `tracker.md`, `B164.md` or `docs/manual/`. ⛔ Evidence lands IN THE REPO. ⛔ **Stop and report** if
moving the transitions out breaks an invariant the kept surface needs — do not widen scope to fix it.
