<!-- Author: Codex, independent Quality Agent -->
# W1 brief revision 3 — final scoped QA review

**QUALITY-AGENT PASS — 2026-09-24 — AUTHORIZED FOR DISPATCH.** WR-1–WR-4 are resolved. There are no remaining
fold-ins or owner rulings for this brief. This is approval of the implementation contract, not an implementation
gate or closure of B241.

## Authorized inputs

- [W1 brief revision 3](../plans/2026-09-24-standalone-mobile-home-w1-label-termination.md), SHA-256:
  **`617c2be56cdf2db21981373aac3f8dd5a5baa09235d8b753f2a9210ccab5d018`**.
- MeshRoute base: **`4a230f4501c6a19d71b9234968bf1389484d8295`**, branch `main`.
- Simulator: **`6585649ea5a780f0542b2931853a667be56a5b2b`**, clean.
- The brief's **15/15** input hashes independently match disk; its five executable-input hashes also match the
  committed base. All **21** files covered by the three pinned evidence checksum files verify. All explicit
  Markdown links in the brief resolve.

This PASS applies to those exact brief bytes, whose header still says DRAFT because review did not edit the
artifact. This receipt supplies the QA verdict. Any later status-only edit still changes the brief hash and needs
an explicit replacement pin before coder preflight; do not silently substitute it for the hash above.

This report and its companion evidence directory are QA review outputs added after the brief was pinned. Classify
and inventory them as explained preparation additions under §1 preflight item 4; they do not alter any pinned
authority, executable input, implementation requirement or gate. No commit is required to proceed.

## Scoped dispositions

| Finding | Verdict | Independently verified resolution |
| --- | --- | --- |
| WR-1 | **PASS** | The coder runs the justified union of its two mutation selectors and the stock six-env warning census. QA's separate P6 gate reruns the touched-file selector, currently empty, with discretion to add relevant dependency batteries. The two gate descriptions now agree. |
| WR-2 | **PASS** | The whole preparation set, tracker and MEMORY included, remains unchanged from preflight through freeze. The contradictory pointer-update exception is gone. The prior re-review report and its checksummed evidence are pinned. Preflight classification, input stability and final freeze inventory remain required. |
| WR-3 | **PASS, retained** | The production and wrapper contract remains as accepted. The two new B241 controls must compile and fail their intended poisoned-buffer assertions. C0 retains its intentional compile-failure contract. Runner controls and justified re-anchoring are permitted; native mutation entries remain outside the edit fence. |
| WR-4 | **PASS** | Raw measurement outputs now use permitted `.pio-measure/w1/` paths. Base and final each receive two consecutive measurements with no intervening checkout or ordinary `.pio` changes. Four per-env manifest-file comparisons establish same-source repeatability. Base-to-final attribution reads fields separately, expects recorded source changes, and requires compatible toolchain, identity and build paths. Durable copies follow the measurements. QA compares its final measurements with the coder's final manifests without misusing strict `compare`. |

The tool checks were against `tools/measure_board.py::validate_output_dir`, `run_pair`, `collect_manifest`,
`QUALIFICATION_FIELDS` and `compare_qualification`. The manifest's `paths` fields describe stable build locations,
not the varying artifact-output directories, so the brief's base/final path-compatibility requirement is consistent
with the tool. `gateway` remains the unchanged-image control; the mobile delta must be measured and attributed.

The four prescribed output paths were passed directly to the actual read-only validator and all were accepted:
`.pio-measure/w1/base-1`, `base-2`, `final-1`, `final-2`. None existed and none was created by this check.
The prior review already proved the manifest-file and same-source comparison behavior with labelled synthetic
fixtures; the measurement tool is unchanged, so those proofs were not needlessly repeated.

## Evidence and limits

[Companion evidence](2026-09-24-standalone-mobile-home-w1-brief-rereview-2/) contains the fifteen hash checks,
four path-validation receipts, measurement-tool hash and preservation manifest, with a checksum file.
Compared with the revision-2 review's starting inventory, the only pre-existing input changes are this brief,
tracker and MEMORY. Production, tests, tools, design, register and earlier evidence are unchanged.

During this review all starting input hashes and both repository HEADs were preserved. Only this receipt and its
evidence directory were added. No production, test or tool edits, staging, commits, resets or cleanup occurred.

No board build, native/corpus rerun, wrapper rebuild, full probe, mutation run, warning census, tools-discovery gate
or metal test ran in this scoped review. The earlier independently executed pre-check remains the baseline;
the coder must derive all implementation figures and execute the brief's complete chain afresh.

**Next:** the separate coder source-validates revision 3 at the hash above, captures the required baselines before
editing, implements only within §3, runs §4.1 and freezes the §5 receipt/inventory. QA then runs §4.2 independently.
B241 remains OPEN until that implementation gate passes.
