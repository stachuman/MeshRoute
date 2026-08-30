<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §GATE-SPEED — parallel corpus runner + gate-instrument speedups · dispatch brief · 2026-08-30

**Status: QG-REVIEWED 2026-08-30 (brief round: three load-bearing corrections; implementation rounds: a
four-item authority-chain HOLD — frozen inputs · RESULT-VALIDATED promotion (exactly one parsed result line +
zero assertion failures before promotion, not merely successful process exit) · validated manifests ·
validated compare — then a two-item trust-gap HOLD (snapshot validation + symbol visibility)). Slice B is
CLOSED and committed; the corpus tooling is free.** Owner ruling 2026-08-30: the
parallel corpus runner is approved as a slice, and **the D1 "boards sequentially" rule is RELAXED for the
ruled pair** — the two envs may build in parallel provided isolation + determinism are proven. ⛔⛔ **AN
INSTRUMENT SLICE: zero behavior change — the production-diff fence covers `lib/`, `src/`, `variants/`,
`platformio.ini` AND `simulation/*.json` (★ QG-strengthened); tools/harness only. The native C++ suite stays
byte/count UNCHANGED; the new runners get their own Python unit/self-tests under `tools/` (★).** ⛔ NO DEVICE
CONTACT.
Motivation, measured: a full closure round is 30-60+ min and gate execution dominates (the 36-scenario corpus
runs 2-4× per round, sequentially; the touched-target ledger reached 21 batteries; two board builds run
serially).

## Scope — three instruments, one determinism bar
1. **The canonical corpus runner — CREATED, not parallelized (★ QG correction: there is NO maintained
   sequential runner to parallelize; the current authority is the ad-hoc shell loop recorded at
   `docs/superpowers/evidence/2026-08-30-custody-slice-b-corpus.md:218`).** This slice creates the canonical
   runner, and parallelism is a property of it, not a bolt-on:
   - `--jobs=1` and `--jobs=N` run THE SAME implementation — jobs=1 IS the sequential arbiter.
   - ★ The scenario set is BOUND to the canonical 36-row `### 36/36 corpus` section in
     `simulation/BASELINE.md:10408` (parsed, not hand-listed): a missing, duplicate, or unexpected scenario
     JSON causes REFUSAL, never a partial pass.
   - ★ Fresh/empty output directories per run; each scenario's stream is written to a temporary file and
     **atomically renamed only after the result is VALIDATED — exit 0 AND exactly one parsed result line AND
     zero assertion failures — never on process exit alone** (wording corrected at QG's round-3 PASS): a
     crashed OR lying scenario can never leave a plausible-looking promoted stream.
   - ★ Per-scenario logs + a MANIFEST carrying: `lus` path+hash, scenario path+hash, engine, exit status,
     output hash, event count and assertion-failure count — the run is self-describing evidence.
   - ★ Controls (each proven): a failed worker ⇒ loud refusal · partial output ⇒ refusal · a missing result
     ⇒ refusal · an INTERRUPTED PARENT kills and reaps every child (no orphan `lus` processes — verify with
     a real SIGINT test). Phase 0 still measures the current shell-loop wall-clock as the baseline.
   - ⛔ The determinism gate is absolute: a `--jobs=N` full pass BYTE-IDENTICAL to `--jobs=1` on all 36
     streams (md5 per scenario), proven on ≥3 repeated parallel runs.
2. **Battery harness worker-count audit — ★ NEUTRAL premise (QG correction): the default is ALREADY
   `min(usable cores − 2, 6)` and was previously measured** (`tools/probe_ui_model_mutations.py:408`).
   Measure at several counts — 1/2/4/6/8 — and **lower, retain, or raise on the measurement** (respecting
   the battery-private ccache and scratch-tree RAM/disk footprint; state the headroom math). ★ Scope stated
   honestly: this optimizes ONE battery invocation; it does NOT parallelize a 21-target ledger — concurrent
   independent battery orchestrators are OUT OF SCOPE unless a single shared worker budget is designed
   (which this slice does not attempt). The harness's A/B verdict-identity check re-run after any change.
3. **Parallel ruled-pair board builds — ★ the isolation premise CORRECTED (QG): `measure_board.py:30` uses
   ONE stable `.pio-measure` hierarchy and ONE global lock, so two current invocations CANNOT run
   concurrently.** The required shape:
   - ★ ONE internal pair command that takes the global lock ONCE and runs both envs itself (never two racing
     invocations).
   - ★ Stable, SEPARATE per-environment build/libdeps/workspace roots.
   - ★ `--jobs=1` as the sequential arbiter, `--jobs=2` as the parallel mode — same implementation.
   - ★ **Identical per-env paths in BOTH modes** — essential because §B262 makes the ESP32 payload hash
     path-sensitive; the mode must never change where an env builds.
   - ★ Sibling cancellation + complete process cleanup if either build fails or the runner is interrupted.
   - ⚠ ccache handling checked explicitly (shared dir concurrent-safe, or per-env dirs — measured, stated).
   - ⛔ Determinism gate: every reported figure (RAM/flash/sections/objects and the NORMALIZED SYMBOL
     INVENTORY whose property set is exactly **name · type · bind · visibility · section-name · size** —
     "symbol tables" was the over-claim, corrected at QG's round-3 PASS — and payload hashes, now comparable
     across modes because the paths are identical) IDENTICAL between `--jobs=1` and `--jobs=2`, both envs,
     proven on a repeat. The RULING recorded in the runner's header: "the 2026-08-30
     owner ruling relaxes D1's 'sequentially' for the ruled pair, conditional on this proven isolation; any
     future env addition re-proves it."

## Gate
- The three determinism proofs above (the slice's whole point is speed WITHOUT trust erosion — a flaky
  parallel gate is worse than a slow one). Measured before/after wall-clock for each instrument, reported
  honestly (including the machine's core count so the numbers transfer).
- Native suite untouched (RUN the binary — counts unchanged); ★ the new Python runners carry their OWN
  unit/self-tests under `tools/` (the controls above ARE those tests — runnable standalone, each RED-able);
  no battery owed beyond #2's own A/B re-check (tools-only — verify TARGET_SRC, state it); corpus content
  untouched (the runner changes, the streams may not — that IS the determinism gate); `git diff --check`
  clean; ★ diff confined to tools/runner files — `lib/`, `src/`, `variants/`, `platformio.ini` and
  `simulation/*.json` all byte-untouched, verified at the diff.
- ⛔ NEVER `git commit`/`git add`/`git checkout --`. ⛔ No maintained docs (the D1 wording change in
  CLAUDE.md is the OWNER's edit; the memory/register notes are the supervisor's), no `tracker.md`, no
  `platformio.ini`, no parallel-session files. No pollers; never pipe the runner. Metal residue: none (host
  tooling) — say so.

## Report
Phase 0 (the loop located + baseline timings) · each instrument's change + its determinism proof (runs,
md5s/figures, repeats) · the before/after wall-clock table · the worker-count math · exact final
`git status --short`.
