<!-- Coder: Codex; independent QA/Author: Claude; owner commits -->
# B434+B435 mutation-harness tool follow-up — revision-1 coder checkpoint

**2026-09-20 — STOP-1 / HOLD (B436). Candidate preserved and frozen for QA review; the implementation gate is NOT complete.**

## 1. Source-validation and permitted preparation

MeshRoute base `e680271791397dbdaa3a7a34b368ebc8851e60a2`; simulator `6585649ea5a780f0542b2931853a667be56a5b2b`, clean and unchanged. Brief revision 1 SHA-256 `c947d0c2953a30990e36726d00496baf4832a773b7862c8573435d12cc6d1d48`, unchanged throughout. All named repository seams matched before editing: the stale literal at original line 718, `run_suite` at 11776, its ignored return code and truncated failure output, the merged mutant label, argument registration, shard transport and parent merge. Initial named-anchor source-validation passed; it missed the important behavior of the installed `pio test` command, exposed by the real gate below. No conflicting source anchor was knowingly bypassed.

The permitted entry preparation set was the existing uncommitted `MEMORY.md`, bug register, `tracker.md`, and untracked revision-1 brief. `preparation-inputs.json` records their full hashes; `preflight-inputs.json` records all entry inputs. No commit was required. No reset, clean, staging or commit occurred.

## 2. Preserved implementation

Only the two fenced tool paths implement the candidate:

- `tools/probe_ui_model_mutations.py`: advisory pin and one derivation comment; typed `SuiteFailure`; nonzero build-return-code / lowercase `error:` detection; full stdout+stderr capture; pure arm-specific classifier; bounded inline excerpt and last 64 KiB capture; capture sent through shard JSON to parent-owned `.pio/mutation-unusable/<target>/<entry-id>.log`, with the retained path printed last; in-process `--selftest-unusable` and argument/usage registration.
- `tools/test_mutation_unusable_reason.py`: seven discovery tests, including both executed temporary-copy controls, full self-test excerpts/files, real `run_suite` with subprocess doubles for build/run failures and unchanged successful doctest parsing. The self-test runs with an empty `PATH`, so no compiler/PIO/rsync is available. Deleted retention cannot borrow stale self-test files because the exact prior output is removed before each write.

The clean-baseline failure consumer reads the typed result's `.text`; its abort semantics remain unchanged. Narrow comments now identify the parent's ignored diagnostic writes. All mutation assignments and `TARGET_SRC` are byte-identical; worker formula, existing explicit exits and RED/FAIL/VACUOUS calls remain unchanged. The old successful return tuple and stdout are unchanged. This source preservation does not imply unchanged runtime classification: B436 demonstrates the contradiction in the newly mandated build-failure condition.

PIN re-synced? YES — Slice 10 adds two assertions and zero cases: 2950/195768 → 2950/195770. Every clean worker baseline reached in this fresh attempt derived 2950/195770/0, with zero B217 banner lines in the candidate logs. The stopped attempt is not a full-union verification of B434.

## 3. B436: the “build” subprocess runs the tests

The revision-1 command is still `pio test -e native`. This is build **and test**. The new `b.returncode != 0` rule classifies a successfully compiled, assertion-failing mutant as an unusable build before the existing explicit binary execution can classify it RED.

| Same existing `radmin8node` N01 mutant | Measured outcome |
| --- | --- |
| Revision-1 candidate, requested `--workers=3` (one-entry battery uses one worker) | `0 RED / 1 unusable`, exit 1; retained output says `Building...`, `Testing...` and failed checks |
| Pristine `e680271` harness in a separate diagnostic rsync copy | `1 RED / 0 unusable`, exit 0 |
| Direct execution of that reference worker's freshly built mutant binary | exit **+1**, **2950 cases / 195770 assertions / 5 failed cases / 22 failed assertions / 0 skipped** |

The retained PIO text includes `Program received signal SIGHUP (Hangup)`. It is misleading: installed PlatformIO's `NativeTestOutputReader.raise_for_status` calls `signal.Signals(abs(return_code))` for any nonzero return code, so positive **1** is rendered as SIGHUP. The direct execution proves this observed run is an ordinary failed-test exit. `platformio-source-evidence.json` archives the installed version, relevant source snippets and full source-file hashes.

The union driver stopped on the first completed off-floor battery. Three already active batteries (`devicenv`, `radmin8client`, `radmin8verbs`) were interrupted; the remaining 57 were not started. None is counted as a passed battery. The required **61 / 983 RED / 1 known B342 / 984 / 0 vacuous** floor is **not reproduced**. The real parent did retain N01's output outside the removed worker tree; that complete file is archived under `retained/radmin8node/N01.log`.

**Required QA fold-in:** make the first command explicitly build-only (the installed CLI supports `pio test -e native --without-testing`), then retain the strict nonzero-build refusal and existing separate doctest execution. Add a regression proving a real compiled assertion-failing mutant still reports RED; preserve controls for actual failed builds and verdict-less runs. This changes the brief's named command beyond its “two failure arms” wording, so no such repair has been applied silently. The frozen brief is untouched; B436 is registered per M1. No product ruling is proposed.

## 4. Fresh checks and limits

| Check | Result |
| --- | --- |
| `git diff --check` | PASS |
| `--where` | PASS; same five-field output shape |
| `--selftest-unusable` | PASS, exit 0, both complete fabricated files retained |
| Focused discovery | 7 tests PASS; collapsed discriminator and deleted write both RED |
| Full tools discovery | **356 tests OK / 0 skipped** (349 prior + 7 new), exit 0 |
| Union inventory before launch | Exact QA `mut.sh` selectors: 61 batteries / 984 entries; every source pattern matches once |
| Full union | **STOP-1 / HOLD**, as above; no aggregate PASS |
| Mutation stage before/after | **4,831 file hashes identical**, including the complete candidate overlay |
| Shared gate inputs before/after | **4,488 file hashes identical** before the M1 finding/receipt landing |
| Mutation tables and target map | Byte-identical to base; source-preservation reports retained |
| Simulator | Same HEAD, clean, no edits or runs |

Per brief §5, no standalone native, corpus or board gate was run. Native builds/runs occurred only inside the mutation instrument and its bounded failure reproduction. No RAM, flash, ABI or corpus figures are inherited as fresh results. B434/B435 remain open pending QA; no design, bench, ledger, tracker or MEMORY landing was made.

## 5. Frozen inputs and evidence

The evidence directory `2026-09-20-radmin-tools-b434-b435-r1-checkpoint/` contains commands, exact return codes/timings, full logs, the two self-test files, the actual N01 retained failure, the reference and direct-binary comparison, source-preservation audits, preparation hashes, complete before/after input manifests, and copies of the two frozen tool files plus permitted preparation. The rsync stage included uncommitted and untracked files; it was not a HEAD-only snapshot. The repository's tracked relative simulator symlink was preserved by providing the same sibling layout outside the stage.

The only subsequent non-tool edit is mandatory M1 bookkeeping: B436's finding row, a STOP note in §0, and the next-free marker. Existing B434/B435 closure text and the brief remain untouched. The complete post-receipt input manifest is archived with the freeze hashes. The evidence inventory excludes itself to avoid a circular hash.

Candidate SHA-256 values:

- `tools/probe_ui_model_mutations.py`: fa4720976b46128432603a26618142701ecc4b7c7076ae52bf7f7461cb81b12a
- `tools/test_mutation_unusable_reason.py`: 1c79219d74c2a658b927fb11828fd6c85e3025ff7a9be6be12ce1e169a6be18b

This is a reproducible **HOLD checkpoint**, not a completed implementation handoff. QA owns the brief correction and independent gate; all work remains uncommitted.
