# P1 frozen coder evidence

The coder gate passed at MeshRoute `c591721c2e09bb4e7583f49e458da9e5155cf822` plus the
complete uncommitted implementation. Independent QA has not run. Commits are not
gates. See [the receipt](../2026-09-15-radmin-slice7b3-p1.md#11-frozen-coder-handoff--2026-09-15).

`freeze.json` records every tracked/non-ignored untracked input, its SHA-256 and
mode, plus the actual HEAD and brief hash. Only that manifest excludes itself.
`artifact-sha256.json` separately hashes the archives and supporting records.
No ignored build cache or Git-internal directory is presented as a source input.

## Archives

- `measurements.tar.xz`: complete deterministic base/final gateway and mobile
  outputs, pristine ELFs/payloads, manifests, sections/symbols, compiler commands,
  before/after owning-TU objects, `.su` stack reports and ELF attribution.
- `corpus.tar.xz`: both complete validated 36-stream corpora, their inputs,
  manifests/logs and actual-byte comparison. Nothing was re-anchored.
- `instruments-and-logs.tar.xz`: full native/ABI/probe/tools/census/mutation logs,
  generated action-probe/control sources and compile logs, original action-owning
  source, rejected reference-corruption attempt and corrected control, exact
  orchestration scripts. Original failed iterations are retained. Those scripts
  document their original absolute paths; review and adapt them before replaying.
- `source-state.patch`: complete tracked working-tree patch against c591721,
  including the preserved owner preparation and coder changes.
- `source-overlay.tar.xz`: all untracked inputs outside this artifact directory,
  including the new implementation header, tests, probe, receipt and preflight
  evidence. `untracked-overlay-inputs.json` hashes them. Together with c591721 and
  the tracked patch, this preserves the entire uncommitted source state. Copy this
  artifact directory as well when reconstructing the complete documented freeze.
- `preparation-overlay.tar.xz`: the pre-implementation dirty preparation overlay
  on c591721, verified against all 1477 entries in `inputs-base.json`. It includes
  the consumed revision-2 brief and original preflight reporting inputs. No source
  implementation was dirty at that baseline.

## Results and provenance

- `base-identity.json`, `inputs-base.json`, `base-results.json`: reconciled base,
  complete permitted preparation and fresh native/simulator/board baseline.
- `chain-results.json`, `chain-remaining-results.json`: original full chain,
  including B400 source-reader failures and the successful unaffected gates.
- `reader-final-inputs.json`, `reader-results.json`: complete corrected reader
  snapshot and successful console/inventory/authority/full-tools rerun.
- `mutation-selectors.json`, `mutation-native-inputs.json`, `mutation-results.json`,
  `mutation-summary.json`: S/H union, all consumed native inputs, every actual
  run, 143 worker baselines/restoration checks, 826 assertion RED and only B342.
- `simulator-results.json`, `corpus-byte-comparison.json`: fresh normal/gateway
  compile/link provenance and validated byte-identical streams.
- `action-transcript-comparison.json`: all 156 local output/effect transcripts
  match. Final controlled/no-neg/baseline probe result files are in the log archive.
- `inventory-baseline-rows.json`, `inventory-relocated-rows.json`,
  `inventory-source-drift.json`, `inventory-semantic-comparison.json`: reproduced
  B400, the five relocated provenance rows and all 204 unchanged semantic rows.
- `elf-attribution.json`, `production-input-preservation.json`,
  `final-preservation.json`: actual linked changes, matching production snapshots,
  preserved native inputs, unchanged owner preparation and all 285 simulator paths.

## Independent replay

Use an isolated complete copy of this dirty tree, including all untracked files.
Set `MR_LUS_SRC=/home/staszek/lora-universal-simulator`; do not edit that repository.
Keep builds/mutations away from any checkout undergoing coder edits.

The exact arguments/results live in the JSON records. The required instruments are:

1. `pio test -e native` and the actual `./.pio/build/native/program`.
2. The committed September-13 extended reference script with `--freeze-check
   --compare test/test_remote_codec.cpp --selftest` in the PyNaCl environment,
   plus a separate named-reference-array one-byte corruption refusal.
3. Fresh Release/Ninja normal/gateway simulator compilation against that complete
   source snapshot; both validated 36-stream runs and actual-byte comparison.
4. Both ABI probes, all six standing probes in controlled and `--no-neg` modes,
   explicit inbox CLIENT, and the action probe in both modes. For original local
   transcripts, use `--baseline-source` with the three pristine owning sources.
5. Full tools discovery with a real measured ELF under the private `.pio-measure`
   tree; inventory write/bare/check and 204-row semantic comparison; authority
   and six selftests; A0; DataType literals; whitespace/integrity in both repos.
6. Deterministic base/final gateway then heltec_mobile, sequentially, at identical
   fixed-identity/private paths; the warning census's six pinned environments;
   all 53 mutation batteries named in the selector manifest.

B342 is an unusable green control, not RED. B350's positive-only firmware-UI PASS
wording does not replace its controlled run. The frame-level stack measurements
are not live task high-water. All other standing limitations remain in the receipt.
QA issues its own PASS/HOLD before the behavior brief is refreshed at this frozen
base. R-RA-40 is approved; P1 consumes none of its resident allocation.
