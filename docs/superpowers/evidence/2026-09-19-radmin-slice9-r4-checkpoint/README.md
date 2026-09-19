# Slice 9 revision 4 — preserved checkpoint, HOLD B426/B427

See [receipt §9](../2026-09-18-radmin-slice9.md#9-revision-4-completed-candidate--stop-1-b426b427-2026-09-19).
This archive does not claim an implementation or independent QA PASS.

- Base: `84edd3ebfb08d807645d077269bace145ac2a2ab`; simulator:
  `6585649ea5a780f0542b2931853a667be56a5b2b`, unchanged.
- Frozen revision-4 brief: `6ae75dfc1b7da467413b35778604e1dd31c98188b3c3cbf9352af460193a7676`.
- `preparation-inputs.json` inventories the entire resumed candidate, including dirty and untracked inputs.
  `resume-changed-inputs.json` names the five QA inputs changed since the prior checkpoint.
- `final-gate-inputs.json`, `final-boards-inputs.json` and `union-inputs-inputs.json` identify the three
  tested copies. `frozen-primary-inputs.json` records all 361 production/test/tool/config records,
  including eight deletions. All 360 other records match the shared tree byte for byte; the later B428
  arithmetic-only repair in `tools/test_probe_features.py` is recorded separately with its focused run.
- `source-audit.json` records the v2 owners, comment-only token comparisons, absence exclusions and
  inert mirror proof. `post-gate-deltas.json` separates that one tool-test repair from receipt/register
  reporting and removal of an extra Part 55a bench wording edit to keep the final bench fence at 9.9/57e.
  The unfenced B426 string fixture remains a separately disclosed failure.
- `final-logs/` contains command/result JSON and unabridged final outputs. `final-union/` contains the
  mechanical S ∪ H selector, native input hashes and all 61 battery logs. `gate-summary.json` is an
  output audit, not a replacement for the logs.
- `board-builds.tar.xz` contains both deterministic board pairs, including pristine/final ELFs,
  payloads, sections, normalized symbols, source/toolchain manifests and complete build logs.
  `boards/` exposes the small manifests and symbol tables. `board-attribution.json` lists deltas.
- `corpus/` contains fresh before/after manifests; `corpus-byte-comparison.json` records actual full-stream
  byte comparison. Full streams remain under `/tmp/mr-s9-r3-mg75mxe0/corpus-base` and
  `/tmp/mr-s9-r4-j37ybea_/corpus-final`.
- `warning-attribution/` retains fresh V3 and V4 before/after logs. The actual six-cell census fails its
  unchanged pins (B427). No override result is passed off as the standing gate.
- `deferred_actions/` and `deferred_actions-no-neg/` contain the real probe's generated inputs/results/logs.
- `B426-private-proposal.patch` is a review proposal, **not landed**. Its in-memory focused run is separate
  from `final-logs/tools.log`, which measures the unchanged failing test.
- `measurement-index-projection.json`, `measurement-index-verification.json` and the scoped Git wrapper
  document B425. Only isolated read operations use a private index omitting already-deleted entries.
  Shared/staged source entries were never changed. `discarded-attempt/` records rejected/interrupted
  attempts; none contributes final counts.
- `drivers/` retains the orchestration scripts. Their absolute paths identify this run; adjust scratch
  paths when reproducing. Gates themselves are the checked-in commands recorded in each result JSON.

`checkpoint-overlay.tar.xz` contains every dirty tracked file and every prior untracked input outside
this evidence directory; `checkpoint-deletions.json` names the eight deleted paths. These plus the base
commit reconstruct the checkpoint inputs in a new private checkout. This includes the implementation,
not just HEAD. Do not apply an overlay to a working checkout containing other work. The complete
`checkpoint-inputs.json` and `artifact-sha256.json` identify the final preserved state; they explicitly
exclude their own circular metadata as documented in the input manifest.

Independent QA, a successful implementation freeze, the paired warning re-pin, the B426 fixture fold-in,
and all physical bench checks remain pending. No commit is a progress prerequisite.
