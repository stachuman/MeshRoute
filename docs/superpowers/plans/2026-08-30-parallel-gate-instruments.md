<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §GATE-SPEED — parallel corpus runner + gate-instrument speedups · dispatch brief · 2026-08-30

**Status: QUEUED — awaiting (1) Slice B's closure (it holds the corpus tooling hot) and (2) the Quality
Agent's review of THIS BRIEF before dispatch (standing process).** Owner ruling 2026-08-30: the parallel
corpus runner is approved as a slice, and **the D1 "boards sequentially" rule is RELAXED for the ruled pair**
— the two envs may build in parallel provided isolation + determinism are proven. ⛔⛔ **AN INSTRUMENT SLICE:
zero behavior change — nothing under `lib/`/`src/` may change; tools/harness only.** ⛔ NO DEVICE CONTACT.
Motivation, measured: a full closure round is 30-60+ min and gate execution dominates (the 36-scenario corpus
runs 2-4× per round, sequentially; the touched-target ledger reached 21 batteries; two board builds run
serially).

## Scope — three instruments, one determinism bar
1. **Parallel corpus runner (the big win).** Phase 0: locate the sequential 36-scenario execution loop
   (where `lus` iterates the corpus — sim-side script or this repo's tooling; file:line) and measure the
   current wall-clock for one full pass. Then: run scenarios N-wide (worker pool, N from CPU count with a
   cap; each scenario already writes its own stream — verify no shared mutable output/tmp/RNG state between
   scenario processes, and isolate per-worker anything found). ⛔ **The determinism gate is absolute:** a
   parallel full pass must be BYTE-IDENTICAL to a sequential pass on all 36 streams (md5 per scenario),
   proven on ≥3 repeated parallel runs (schedule jitter shaken out); the runner REFUSES (loud) if a worker
   dies rather than reporting a partial pass as complete (the B237 class: a missing scenario is a FAILURE,
   never "measured"). Sequential mode stays available (one flag) as the arbiter.
2. **Battery harness worker-count audit.** `tools/probe_ui_model_mutations.py`'s parallel default vs the
   machine's cores: measure, and if conservative, raise it (respecting the battery-private ccache and the
   scratch-tree memory footprint — state the RAM/disk headroom math, don't just crank it). One targeted
   change; the harness's own A/B verdict-identity check (the harness-slice precedent) re-run after.
3. **Parallel ruled-pair board builds (the new ruling, enabled).** The deterministic runner
   (`tools/measure_board.py`) already builds in isolated scratch trees — verify that isolation actually
   holds two concurrent builds (distinct scratch roots, distinct ccache handling — ⚠ check whether the two
   envs share a ccache dir and whether concurrent access is safe or needs per-env dirs), then allow the two
   ruled envs concurrently behind a flag. ⛔ Determinism gate: every reported figure (RAM/flash/sections/
   objects/symbol tables — and payload hashes where same-path, per B262) IDENTICAL between parallel and
   sequential modes, both envs, proven on a repeat. The RULING is recorded in the runner's header: "the
   2026-08-30 owner ruling relaxes D1's 'sequentially' for the ruled pair, conditional on this proven
   isolation; any future env addition re-proves it."

## Gate
- The three determinism proofs above (the slice's whole point is speed WITHOUT trust erosion — a flaky
  parallel gate is worse than a slow one). Measured before/after wall-clock for each instrument, reported
  honestly (including the machine's core count so the numbers transfer).
- Native suite untouched (RUN the binary — counts unchanged); no battery owed beyond #2's own A/B re-check
  (tools-only — verify TARGET_SRC, state it); corpus content untouched (the runner changes, the streams may
  not — that IS the determinism gate); `git diff --check` clean; diff confined to tools/runner files.
- ⛔ NEVER `git commit`/`git add`/`git checkout --`. ⛔ No maintained docs (the D1 wording change in
  CLAUDE.md is the OWNER's edit; the memory/register notes are the supervisor's), no `tracker.md`, no
  `platformio.ini`, no parallel-session files. No pollers; never pipe the runner. Metal residue: none (host
  tooling) — say so.

## Report
Phase 0 (the loop located + baseline timings) · each instrument's change + its determinism proof (runs,
md5s/figures, repeats) · the before/after wall-clock table · the worker-count math · exact final
`git status --short`.
