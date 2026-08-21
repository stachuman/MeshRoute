<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# [[B217]] — the mutation harness derives its baseline · dispatch brief · 2026-08-20 (fires after §UI-17 S1 lands)

**Status: QUEUED behind S1 (same-file collision on `tools/probe_ui_model_mutations.py`); the [[B217]] register row
is the AUTHORITY — read it first.** ⛔ **NO DEVICE CONTACT. HARNESS-ONLY: no `src/`, `test/`, or `lib/` change.**

## The defect (from the row)
The harness gates every battery on a HAND-PINNED `BASE_CASES, BASE_ASSERTS` literal; a stale pin **aborts every
battery without applying a single mutation** (`sys.exit(2)`, quiet), which reads exactly like a battery with
nothing to find — **it disarmed all four targets once already (measured)**. The row rules: *re-pinning by hand is
NOT the fix — that is what failed.* Shapes offered: **(a) derive the baseline from a clean run** · (b) make a
stale pin LOUD and unmistakable · (c) a standing re-pin gate step. `MR_MUT_BASE` (`:128`-area) is an existing env
hook a derived-baseline fix can reuse.

## Required (owner-ruled 2026-08-20: fix it — shape yours to choose, justify)
Recommended: **(a), with (b)'s loudness kept as a belt**: the clean run's own counts become the baseline every
invocation (0-failed still mandatory — a red clean tree still ABORTS, loudly); the hand pin becomes either
GONE or an OPTIONAL cross-check that, when present and mismatched, prints an UNMISSABLE warning and
**continues with the derived value** (⛔ never the quiet exit-2). Whatever shape you pick:
1. ⛔ A stale expectation can NEVER again silently zero a battery.
2. The clean-tree failure arm stays loud (0 failed required; a failing clean tree aborts with its output).
3. The per-run derivation is printed (the "clean baseline X / Y / 0" line stays, now derived).
4. The derivation-vs-pin history comments in the file are PRESERVED as history (correction-in-place idiom if you
   retire the pin — the derivations are this arc's audit trail, not clutter).
5. `MR_MUT_BASE` keeps working (or is retired with a stated reason).

## Verification you run (QG runs the full gate)
1. **Every battery target once, sequentially** (this changes the gate ALL of them stand on): each must run its
   full entry list to its known RED count / 0 unusable, with `source restored (MATCHES)` and the exit gate.
2. **The defect's repro, three controls:** (i) a deliberately WRONG pin/expectation ⇒ the battery still RUNS
   (derived) with the loud warning — ⛔ not exit-2-quiet; (ii) a deliberately broken clean tree (one failing
   assertion, temporarily, restored after) ⇒ loud ABORT with output; (iii) the normal clean tree ⇒ identical RED
   counts to today. Restore everything exactly; `git diff --check` clean; native counts unmoved.
3. ⚠ Never run a probe concurrently with any battery run.

## Report
The shape chosen and why · the three controls' outputs · every target's RED count vs its pre-fix value (must be
identical) · exact final `git status --short`.
⛔ NEVER `git commit`/`git add`/`git checkout --`. ⛔ Only `tools/probe_ui_model_mutations.py` changes. ⛔ Do not
touch the register, bench script, plans, specs, `tracker.md`, `B164.md`, `docs/manual/`.
