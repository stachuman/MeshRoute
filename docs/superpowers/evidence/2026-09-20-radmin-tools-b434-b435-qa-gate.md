<!-- QA/Author: Claude; production coder: Codex; owner rules and commits -->
# Tool dispatch B434 + B435 (+ B436) — the mutation harness — independent QA gate — 2026-09-20

**Verdict: INDEPENDENT QA PASS.** The revision-2 candidate (brief SHA-256 `f6eafd39…`, base `e680271`, simulator
`6585649` untouched) is tool-only and does exactly what the brief contracts: the stale advisory pin is re-synced, an
unusable mutant is now reported with its arm and return code, a bounded excerpt and a retained full capture, the
build step is build-only, and the full 61-battery union reproduces its floor exactly with zero B217 banner lines.
No product file changed. With this the remote-admin v2 arc has **no software residue**; only the owner's metal parts
remain.

## 1. State gated

- Uncommitted on `e680271`: `tools/probe_ui_model_mutations.py` (SHA-256 `61f1cd06…`) and the new
  `tools/test_mutation_unusable_reason.py` (`f6ac8119…`) — both equal to the coder's frozen hashes; no other file
  outside `docs/` differs from base (`git diff e680271 --name-only`). Tree unchanged after the gate.
- **Harness diff read in full.** `PIN_CASES, PIN_ASSERTS = 2950, 195770` with one derivation comment; a
  `SuiteFailure(reason, rc, text)` result; `run_suite` = `pio test -e native --without-testing` (non-zero rc OR
  `error:` ⇒ build failure with the full output) then the explicit binary (rc + full stdout/stderr; no summary ⇒ run
  failure); the pure `unusable_verdict` (build: up to three `error:` lines or the last five; run: the last twelve,
  signal named for a negative rc; capture bounded to the last 64 KiB); the parent writes
  `.pio/mutation-unusable/<target>/<entry-id>.log` and appends the path to the entry's `extra`; `--selftest-unusable`
  registered in `_KNOWN_FLAGS` and the usage text, builds nothing, mutates nothing; the orchestrator's "never writes
  the repository" comment corrected to name the one diagnostics directory. **No mutation table, pattern, control,
  target or exit-code line is in the diff.** The RED / FAIL / VACUOUS arms and the `ok` / `bad` accounting are
  byte-for-byte unchanged.
- **Test read in full.** Runs the self-test and asserts both labels, the excerpts, the retained paths and their full
  contents; two executed controls mutate a TEMP COPY of the harness (collapsed arm; deleted retention) and require
  `SELFTEST FAILED` with a non-zero exit — the copy resolves its own root, so its retained files stay in the temp
  tree; asserts the merged label is gone from the source; and three tests exec the real `run_suite` definition
  (extracted by AST) against a mocked subprocess: a build failure never runs a stale binary, a run failure retains
  the code and both streams, and the existing doctest verdict path is unchanged.

## 2. Instruments QA executed (stock tools)

| Instrument | Result |
| --- | --- |
| Whitespace | `git diff --check` clean |
| `--where` | unchanged five-field shape (model / 239 entries / key / backup dir / workers 8) |
| `--selftest-unusable` | exit 0; `UNUSABLE build — the mutant does not compile (build rc 1)` with three `error:` excerpt lines; `UNUSABLE run — the suite ran without a verdict (binary rc -11 / killed by signal 11)` with the last twelve lines; both `retained:` paths present under `.pio/mutation-unusable/selftest/` (704 B / 510 B = the full fabricated captures); `SELFTEST OK` |
| Full tools discovery | **356 tests / OK / 0 skipped** (349 prior + 7 new), 686 s |
| Full 61-battery union (`mut.sh`, stage `/home/staszek/mr-tools-qa-stage`, `--workers=3`) | **61 batteries / 983 RED / 1 known unusable (B342, `sliceBmac` M04, its battery exits 1 as always) / 984 configured / 0 vacuous**; every other battery exit 0; **0 `re-pin PIN_CASES` banner lines** across all 61 logs; **163 worker baselines** all `2950 / 195770 / 0`; stage hashes identical before and after; no `UNUSABLE` verdict occurred, so `.pio/mutation-unusable/` holds only the self-test's directory; wall clock **31 min** (Slice 10's union: ~50 min — the suite now runs once per entry) |

Per brief §5 (tools-only, P6) no standalone native, corpus, ABI, census or board run was made; the native suite was
built and executed inside the union 163 times on the clean tree plus once per mutant.

## 3. Findings

- **B434 — CLOSED.** The pin reads 2950 / 195770; zero banner lines in 61 batteries.
- **B435 — CLOSED.** Arm, return code, excerpt and retained capture; self-test + two RED controls + three
  `run_suite` tests.
- **B436 — CLOSED.** The build step is build-only; `radmin8node` N01 is RED again (1 RED / 0 unusable in this union).
- No new finding. B342 unchanged (the one known unusable, a compiled survivor printed as `FAIL`).

## 4. Not independently reproduced (D3)

Nothing in the software gate was skipped. The owner's metal parts are unaffected by this dispatch.

## 5. Verdict and landings

**PASS.** Landed by QA: this file + `…-tools-b434-b435-qa/` (union script/log, all 61 battery logs, discovery and
self-test logs, frozen hashes); register §0 rewritten and B434/B435/B436 closed in place; design §19.1 row 10 note;
ledger one-line note; tracker; MEMORY. **Owner:** commit the two tool files and the QA landings (the simulator repo
is unchanged). **Next:** nothing in software — the remote-admin v2 arc is complete except the owner's metal backlog
(Parts 54 / 55a / 55b / 56 / 57a–57f / 58 / 59 / 61 / 62 / 63).
