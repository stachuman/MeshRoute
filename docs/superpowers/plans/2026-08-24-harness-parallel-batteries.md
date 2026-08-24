<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# Harness slice — parallel mutation batteries + compiler cache · plan · 2026-08-24

**Status: QUEUED — ⛔ do not dispatch until QG's N6b gate VERDICT lands** (N6b's code is in-tree, but QG's gate
re-runs the batteries with this very runner and owns `.pio` while it does; a runner edit or a scratch-tree
battery storm under a live gate makes its verdicts ambiguous). ⛔ Harness-only: **zero firmware behaviour
change** — `src/`, `lib/`, `test/` are untouched except where Part B touches `platformio.ini`'s native env.
ⓘ **[[B238]] (the probe clock-wrap trap) stays a SEPARATE owed slice** — bundling it here was considered and
rejected (C1: it is a different harness, `probe_firmware_ui`, with its own risk).

★ **BOTH PARTS LAND AS THE DEFAULT PATH, NOT AN OPT-IN** (owner instruction 2026-08-24: "ensure both will be
used"): the runner's default becomes parallel workers (serial only via an explicit `--workers 1`), and ccache
is wired into the native env itself — so every future battery run, **coders' and QG's alike**, uses both with
zero brief changes, because the runner and the env are the single entry points everyone already invokes.
ⓘ Environment facts, measured 2026-08-24: ccache **4.7.5 installed** (owner ran the install), cache empty,
5 GB default cap; **24 hardware cores** ⇒ default `--workers min(cores - 2, 6)` (each worker's `pio` build is
itself parallel — measure before raising it). All workers and the real tree share the default `CCACHE_DIR`, so
every scratch tree after the first builds against a warm cache — state and measure this in the report.

## Why (measured 2026-08-24)
Per-slice wall-clock is dominated by the battery full passes: each entry = mutate → `pio test -e native`
(`tools/probe_ui_model_mutations.py:4097`) → run → restore, SERIAL, in the REAL tree. The `model` target is
167 entries over a header included by many test TUs; `:4002`'s own comment warns a `lib/core` target rebuilds
the whole suite per entry. `ccache` is **not installed** and no compiler wrapper is configured — so the
restore-rebuild half of every cycle recompiles content the compiler has already seen.

## Part A — parallel battery workers (the big win)
- `--workers N` (default `min(physical_cores - 2, 6)` per the environment facts above; `--workers 1` = the
  exact current serial path, kept as the reference implementation). ⛔ **ALL execution — including the
  `--workers 1` serial reference runs of the A/B proof — happens in scratch trees**; the real tree receives
  only this slice's file edits, never a build or a battery run.
- Each worker gets a SCRATCH TREE under the session scratchpad: an **rsync of the WORKING tree** (⛔ never
  `git archive` — the tree is dirty by design; uncommitted slices are the normal state), excluding `.git` and
  `.pio`; each worker owns its own `.pio` build dir.
- Entries shard across workers; each worker derives its OWN clean baseline first (the derived-baseline B217
  discipline, per tree), then runs its entries' mutate/build/run/restore cycles entirely in its own tree.
- ★★ **THE REAL TREE IS NEVER TOUCHED.** This deletes three standing hazards in one move: the
  edit-a-target-mid-battery incident (N2), the parallel-session board-build wiping `.pio/build/native`
  mid-battery (N5-fix, 2026-08-24), and the stale-mutant-binary exit gate (`:4219` — the real tree's binary is
  never a mutant's again). A cheap end check asserts the real tree's targets are byte-identical to launch
  (md5), replacing the rebuild-on-exit gate.
- Everything semantic is PRESERVED per worker: match count exactly 1 (VACUOUS refusal), RED classification,
  md5-verified restore, the PIN cross-check (warn-only, against the merged result), exit codes 6/7/8, the
  INFLIGHT marker (now: refuse to START if the real tree's target changes between launch and shard copy).
- Merged report identical in shape to today's (per-entry verdicts, counts, restore lines) + a per-worker
  timing line.
- **Gate (the harness's own):** an A/B proof — one full `model` run serial (`--workers 1`) vs parallel on the
  same tree produces **identical per-entry verdicts**; the three fixture classes still classify correctly in
  parallel (a deliberate VACUOUS via a duplicated anchor · a GREEN mutant · a normal RED); a worker crash
  mid-entry leaves the real tree untouched and the run FAILS LOUDLY (never reports the missing shard as
  passed — the B227/B237 laundering class). Timing before/after reported.

## Part B — compiler cache (cheap, measure-then-enable)
- **Owner pre-step (needs sudo, cannot be done by a coder): `sudo apt install ccache`.**
- Wire it into the native env only (CC/CXX wrapper or `platformio.ini` native-env option — whichever the
  native platform actually honours; verify, don't assume) and prove behaviour identity: the suite builds to
  the SAME counts with zero new warnings, and `pio test -e native` output is otherwise unchanged.
- **Measure and report** (the point of the part): cold full build · rebuild after touching
  `firmware_ui_model.h` · a 10-entry `model` battery sample, each before/after ccache and (with Part A) at
  `--workers 1` vs `--workers N`. Numbers in the report, not adjectives.
- ⓘ Expected shape of the win: the mutate-build still pays real compilation for the mutated header's
  dependents, but the RESTORE-build becomes a cache hit (same content returns), roughly halving per-entry
  compile cost — plus every worker's initial scratch-tree build after the first is warm.

## Operational contract
- One Opus dispatch, Parts A then B, independently proven. ⛔ No firmware file changes; `git diff` stays
  `tools/probe_ui_model_mutations.py` + `platformio.ini` (native env only) + any new helper under `tools/`.
- ⛔ NEVER `git commit`/`git add`/`git checkout --`. ⛔ Do not touch the register, bench script, specs, plans,
  design docs, `tracker.md`, or parallel-session files. ⛔ NO DEVICE CONTACT. No background pollers outliving
  the final report.
- The scratch trees live under the session scratchpad and are cleaned on exit (⛔ scratch is volatile — the
  memory rule: nothing durable may live there; all evidence goes in the report/repo).

## Report
The A/B verdict-identity proof (full `model`, serial vs parallel) · the three fixture classifications ·
the crash-mid-entry loud-failure proof · all Part-B timing numbers (cold / header-touch / 10-entry sample,
± ccache, ± workers) · the real-tree untouched proof · exact final `git status --short`.
