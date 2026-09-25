<!-- Author: Codex, independent Quality Agent -->
# W1 brief revision 2 — scoped QA re-review

**Verdict: HOLD — WR-4 remains open.** WR-1 and WR-3 pass. WR-2's inventory/freeze mechanism passes with the
small consistency fold-in below. The accepted UI fix, production fence and generated-wrapper approach remain
accepted. No owner ruling or additional product work is requested.

## Pins and scope

- MeshRoute: `4a230f4501c6a19d71b9234968bf1389484d8295`.
- Simulator: `6585649ea5a780f0542b2931853a667be56a5b2b`, clean.
- [Brief revision 2](../plans/2026-09-24-standalone-mobile-home-w1-label-termination.md), SHA-256
  **`0bd2271329a8614be44b6151b62d604ce8f5af78ff4c5a31f0ff892d00e6e825`**.
- All **13/13** hashes in its tables match disk. The five executable-input hashes also remain the committed
  source hashes verified in the first review. Both pinned `SHA256SUMS` files verify all their contents
  (**10 + 6 files**); all explicit Markdown links in the brief resolve.

This review checks the changes addressing WR-1–WR-4 and the cited instrument contracts. It does not reopen the
accepted product design or substitute a new implementation gate. The brief, its pinned inputs and existing
tracked/untracked work were preserved. Proof receipts are in the companion
[evidence directory](2026-09-24-standalone-mobile-home-w1-brief-rereview/).

## Disposition of the four corrections

| Finding | Verdict | Evidence and remaining action |
| --- | --- | --- |
| WR-1 | **PASS** | §4.1 now requires the stock six-env warning census, unchanged pins and zero `-Wswitch`, and the justified union of both mutation selectors. §4.2 separately describes QA. Small wording fold-in: it lists QA rerunning that union, then says the full union stays in the coder's chain. State whether the extra QA union run is deliberately required; the current explicit list is acceptable additional coverage, but the following sentence should agree. |
| WR-2 | **PASS with fold-in** | The hashes, preflight classification, final inventory and stability statement are present. Resolve the pointer-change contradiction: §1:88–89 permits tracker/MEMORY changes without stopping, whereas §4.1:254–256, §4.3:278 and §5:303 require the preparation set to remain unchanged. Simplest correction: keep the pinned preparation set unchanged through the measured chain/freeze and remove that exception. No production or test change is involved. |
| WR-3 | **PASS** | §2.5 scopes assertion RED to the new B241 controls and existing `must_build=yes` controls, preserves C0's required compile failure, and requires the new controls to fail the poisoned-buffer checks. §3 explicitly permits the new runner controls and justified re-anchoring; the native mutation entries stay outside the fence. |
| WR-4 | **HOLD** | The added workflow cannot run as written: it specifies a forbidden output directory, describes manifest-file arguments as directories, and applies a same-source equality checker across a deliberate source change. Details and executable reproduction below. |

Tracker now names r2.18, and MEMORY no longer calls the reviewed design a DRAFT. Those housekeeping corrections
are verified. B241 remains open until implementation QA.

## WR-4 — correct the board-measurement recipe

### 1. Direct output into the named evidence directory is rejected

The brief's §4.1:226–227 writes `pair --output <evidence dir>/boards/base-1`; §5:282 defines that directory under
`docs/superpowers/evidence/`. `tools/measure_board.py::validate_output_dir` (`:141–156`) allows repository-local
output only below the ignored `.pio-measure/`, outside its reserved per-environment build hierarchies. It also
accepts paths outside the repository.

Executing the exact proposed shape returned **exit 2 before any lock, build or output-directory creation**:

```text
ERROR: an output inside the repository must be below the gitignored .pio-measure/
```

**Correction:** run measurements into fresh directories under `.pio-measure/` (outside `.pio-measure/env/`) or
outside the repository. Retain the raw logs there during measurements, then copy the required receipts/manifests
into the fenced evidence directory. Do not write nonignored evidence into the checkout between same-source
repeatability runs: `source_snapshot` (`:186–231`) includes every nonignored untracked file and Git status.
Do not change the tool's path or preservation guards.

### 2. `compare` takes per-environment manifest files, not directories

The seam table at brief §1:56 says `compare <first> <second>` compares artifact directories. The tool instead calls
`load_manifest` on each argument and requires it to be a file (`measure_board.py:773–780,865–868`). A pair produces
separate `gateway/manifest.json` and `heltec_mobile/manifest.json` files (`:740–766`).

Passing two disposable directories, each containing a valid synthetic manifest, returned **exit 2** with
`manifest does not exist: <directory>`. Passing those manifest-file paths returned **exit 0**.

**Correction:** show the two explicit same-source comparisons, once for each environment:

```text
python3 tools/measure_board.py compare <raw>/base-1/gateway/manifest.json <raw>/base-2/gateway/manifest.json
python3 tools/measure_board.py compare <raw>/base-1/heltec_mobile/manifest.json <raw>/base-2/heltec_mobile/manifest.json
```

Keep the checkout and ordinary `.pio` metadata unchanged between the repeated measurements. This is the stock
repeatability contract, not an extra board profile or an instrument edit.

### 3. Base-to-final attribution is not the stock `compare` operation

Brief §4.1:240 directs the coder to compare the final result against `base-1` with that command.
`QUALIFICATION_FIELDS` (`measure_board.py:790–856`) includes the source tree hash, Git status, RAM, flash, symbols,
payload and ordinary `.pio` metadata. `compare_qualification` requires equality of **all** those fields (`:859–862`).
A correctly changed source tree must therefore fail, including on gateway even when its binary is unchanged.

**Synthetic contract proof:** two fabricated, identical manifest dictionaries pass the actual
`compare_qualification` function. Changing only `source.tree_sha256`, while keeping every measurement identical,
raises:

```text
repeatability mismatch in: source.tree_sha256
```

**Correction:** reserve the stock `compare` command for repeatability of the same frozen source. Across base and
final, report the manifest RAM/flash/section/object/symbol differences and their attribution, with source identity
changes explicitly expected and recorded. Compare toolchain, fixed build identity and paths for compatibility;
do not reinterpret or weaken a failed strict-repeatability verdict. Apply this distinction to QA's base comparison
in §4.2 and to §5/§8 as well. If final-source repeatability is also measured, compare two runs of that final source
with one another, not against the base.

## Executed evidence and limits

The focused checks produced **5/5 expected outcomes**: forbidden output path rejected; identical synthetic
qualification accepted; source-only change rejected; directory arguments rejected; manifest-file arguments
accepted. The synthetic manifests are labelled fixtures, **not board measurements**. Full commands, both output
streams and expected exits are retained in `measurement-contract-checks.json`.

The first ad-hoc capture checked stdout for the path-rejection diagnostic and its assertion failed; the tool
correctly writes that diagnostic to stderr. The capture was corrected, and all five checks above completed.
No board build started in either attempt.

No native/corpus rerun, wrapper rebuild, full probe, mutation run, warning census, tools-discovery gate or metal
test was needed or performed for this scoped document review. The prior pre-check and wrapper evidence retain
their original scope. No production, test or tool input changed; nothing was staged or committed.

Return the corrected measurement passages plus the two consistency fold-ins for scoped review. The remaining
HOLD concerns the brief's use of existing tools, not B241's fix or the approved wrapper design.
