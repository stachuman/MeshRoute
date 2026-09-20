<!-- QA/Author: Claude (revision 1); production coder: Codex; owner rules and commits -->
# Remote-admin v2 tool follow-up — B434 + B435, the mutation harness (no product change)

**Revision 1 — 2026-09-20 — READY FOR CODER SOURCE-VALIDATION AND IMPLEMENTATION; no ruling is requested (tool-only;
B217's owner ruling that the clean baseline is DERIVED and the pinned figure is ADVISORY stands unchanged).**
Base **`e680271`** (owner commit `Slice 10` = the Slice 10 instrument half + QA landings; [Slice 10 gate](../evidence/2026-09-20-radmin-slice10-qa-gate.md)),
clean; simulator **`6585649`**, clean. The coder pins this brief by content hash. A brief under implementation is
frozen (P4). This is the arc's last software item; nothing here touches `lib/`, `src/`, `test/`, the wire, the
corpus or any board.

## 0. What this is, in one paragraph

Two findings from the Slice 10 gate, both in `tools/probe_ui_model_mutations.py` and both about the harness's own
bookkeeping, not about any mutation: **B434** — its advisory cross-check literal is stale (`PIN_CASES, PIN_ASSERTS =
2950, 195768`; the suite derives 2950 / 195770 since Slice 10 added two `test_device_nv` assertions), so every battery
prints the B217 banner at both ends; **B435** — when a mutant's suite yields no verdict the harness says "the mutant
does not compile / did not run" for two different failures and keeps almost nothing (only the first line containing
`error:`, so a binary that crashed or was killed leaves NOTHING), and it does not treat a non-zero build return code
without that string as a build failure. One transient of exactly that shape (`radmin5session` S15 under a concurrent
full chain; solo re-run 25/0) cost a re-run to attribute. Fix both in one tool dispatch, with an in-process self-test
and two executed controls, and prove nothing else moved with a full union.

## 1. Verified seams at `e680271` (pin by symbol; lines are hints)

| Seam | What changes |
| --- | --- |
| `PIN_CASES, PIN_ASSERTS = 2950, 195768` (`:~718`) with the derivation comment lines above it (the Slice 9 line reads "… = 2950/195768"); the advisory banner sites `:~12047–12049` and `:~12117` ("re-pin PIN_CASES/PIN_ASSERTS") | **B434:** the literal becomes `2950, 195770` and ONE comment line in the existing shape is added above it: "Slice 10: + 2 assertions (`test_device_nv` v25-floor and 280-byte-size refusals), 0 cases = 2950/195770; measured by the full native binary." Nothing else in the block moves; the banner code is untouched (it must keep printing when the figure is stale). |
| `run_suite()` (`:~11777`): `pio test -e native` → `if "error:" in b.stdout + b.stderr: return None, text`; then the binary → `if not m or not c: return None, r.stdout[-800:]`; returns `(failed, cases, asserts), stdout` | **B435 (i):** the two failure arms become distinguishable and complete: the build arm triggers on `b.returncode != 0` OR the `error:` text (a pio-level failure with a capitalised `Error:` currently slips through to run a stale binary) and returns the FULL build output; the run arm returns the binary's return code (a negative code = killed by that signal) and its FULL stdout+stderr. Shape: a small typed result (e.g. `reason in {"build", "run"}`, `rc`, `text`) — the coder names it; the success arm is unchanged. |
| The verdict block (`:~12245–12249`): `_err = [first line containing "error:"][:1]`; `_verdict("UNUSABLE", f"  UNUSABLE {label} — the mutant does not compile / did not run", _err)`; `_verdict` prints the line + `extra` and records them in the shard entry (`:~12225–12231`); the parent re-prints `extra` (`:~12062`) | **B435 (ii):** the label names the arm — `… — the mutant does not compile (build rc N)` / `… — the suite ran without a verdict (binary rc N / killed by signal S)`; `extra` carries a bounded excerpt: build → up to 3 lines containing `error:` (or the last 5 lines when none); run → the last 12 lines; and BOTH arms retain the full captured text in a file under the orchestrating tree's git-ignored `.pio/mutation-unusable/<target>/<label>.log` (bounded, e.g. the last 64 KB; the shard result carries it back to the parent, which writes it — the worker trees are rsync'd without `.pio` and are removed on exit), with the path printed as the last `extra` line. The RED / FAIL / VACUOUS verdicts, `ok += 1` / `bad += 1` accounting and the exit-code semantics are byte-for-byte unchanged. Factor the classification into one pure function (e.g. `unusable_verdict(reason, rc, text) -> (line, extra, capture)`) used by the real path and the self-test below. |
| `_KNOWN_FLAGS = ("--where",)` (`:~550`), `--where` handling (`:~11792`), the usage text (`:~556–566`) | **B435 (iii):** a value-less `--selftest-unusable` flag, registered in `_KNOWN_FLAGS` and in the usage text: it applies NO mutation and builds NOTHING; it feeds the pure classifier two fabricated outcomes through the real code — a build failure (rc 1, three `error:` lines inside 40 lines of noise) and a run failure (rc −11, 30 lines of output with no doctest summary) — prints both verdict lines and their `extra`, writes both retained files under `.pio/mutation-unusable/selftest/`, verifies the files hold the full text, prints `SELFTEST OK — build and run failures are told apart and retained`, exit 0; any discrepancy prints `SELFTEST FAILED` and exits non-zero. |
| `tools/test_worker_formula_derived.py` (reads the harness SOURCE for the worker-formula banners; pins no label, no PIN literal) | Not edited; re-run by discovery. |
| Discovery: `python3 -m unittest discover -s tools -p "test_*.py"` = 349 today | **New `tools/test_mutation_unusable_reason.py`**: (a) runs `--selftest-unusable` on the real harness and asserts exit 0, both distinct labels, the excerpt lines, and the two retained files with the full text; (b) TWO EXECUTED CONTROLS on a temp COPY of the harness (the `ble_guard.py` idiom: mutate a copy, run it, expect the failure): the arm discriminator collapsed to one label → the self-test must exit non-zero; the retained-file write deleted → the self-test must exit non-zero; (c) asserts the merged label string ("does not compile / did not run") no longer occurs in the harness source. The tests must not build anything and must finish in seconds. |

Baselines (Slice 10 gate 2026-09-20): union **61 batteries / 983 RED / 1 known unusable (B342 `sliceBmac` M04) / 984
configured / 0 vacuous**, every worker `ok clean baseline 2950 / 195770 / 0`; the B217 banner printed on all 61
batteries; tools discovery 349 / OK / 0 skipped.

## 2. Scope

**IN:** the two files in §1 (the harness; the new discovery test). **OUT:** every mutation entry, pattern, control,
target list, `TARGET_SRC`, the worker formula, the exit-code semantics (B217: a stale figure must NEVER become an
exit condition), the scratch-tree lifecycle (`MR_MUT_KEEP_SCRATCH` unchanged), `.gitignore` (`.pio/` is already
ignored), every production / test / wire / doc file (the register row closures are QA's landing). C1: no refactor
beyond the one pure classifier the fix itself needs.

## 3. Contract

1. **B434:** `PIN_CASES, PIN_ASSERTS = 2950, 195770` with its one derivation comment line; a full union prints
   ZERO B217 banner lines (`grep -c "re-pin PIN_CASES" = 0` across all 61 battery logs) and every worker still
   derives `2950 / 195770 / 0`.
2. **B435:** an unusable mutant is reported with its arm and return code, a bounded excerpt inline, and its full
   captured output retained outside the scratch root at a printed path; a non-zero build return code is a build
   failure regardless of text; RED / FAIL / VACUOUS verdicts, counts and exit codes are unchanged.
3. **Self-test + controls:** `--selftest-unusable` exercises the real classifier without building; the new discovery
   test runs it and its two executed controls (collapsed label; deleted retention) both RED.
4. **Nothing else moves:** the full union reproduces the floor exactly (61 / 983 / 1 / 984 / 0); every prior label,
   entry and control is retained; discovery = 349 + the new tests, 0 skipped.

## 4. Fence

`tools/probe_ui_model_mutations.py` (the PIN literal + one comment line; `run_suite`'s two failure arms; the
classifier + verdict block; `_KNOWN_FLAGS` + usage text + the `--selftest-unusable` handler), new
`tools/test_mutation_unusable_reason.py`. Nothing else. **OUT:** everything in §2.

## 5. Gate (tools-only per P6: discovery + the affected instrument; no `lib`/`src` change ⇒ no native / corpus / board run)

Whitespace (`git diff --check`); `python3 tools/probe_ui_model_mutations.py --where` (unchanged output shape);
`--selftest-unusable` → `SELFTEST OK`, exit 0, the two retained files present; full tools discovery (349 + N, OK, 0
skipped) including the new test's two RED controls; **the full 61-battery union** from an rsync stage (`--workers=3`,
the Slice 10 `mut.sh` list) → `61 / 983 RED / 1 known unusable B342 / 984 / 0 vacuous`, ZERO banner lines, every
worker baseline `2950 / 195770 / 0`, stage hashes identical before/after. **STOP:** any battery off the floor; any
change outside the two files; any entry / pattern / control / exit-code change; a self-test that builds or mutates.
Receipt: `docs/superpowers/evidence/2026-09-20-radmin-tools-b434-b435.md`. On PASS QA closes B434 and B435 in place
and rewrites the register §0 dispatch line; no design, bench or ledger change (nothing metal, nothing ruled).
