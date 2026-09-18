# 8a+8c revision-3 coder checkpoint — 2026-09-18

See [receipt §5](../2026-09-17-radmin-slice8ac.md). This is the R-RA-45 pointer-free measurement and B405 fence STOP, not a production implementation or gate.

- `state.json`, `inputs.json`: exact source/HEAD/brief/simulator identities and 2259 starting inputs, including tracked and untracked work. `preparation-inputs.json`, `preparation-files/`, `resume-changed-inputs.json`: the six QA inputs and the five changed originals since the checkpoint (brief plus four announced QA documents).
- `layout/summary.json`, `measurements.json`, `offsets.json`: final 4512-byte block, measured Node sizes and exact compiler arguments. Private headers keep the historical model and counters-only controls. `layout-driver.py` and `layout-audit-driver.py` reproduce the measurements; the latter uses `-fno-access-control` only for model offset inspection.
- `fence-proof/compile-results.json`: six exact production-TU compiler runs. The source copy is byte-identical to `lib/core/node_mac.cpp`. Native/mobile with the slot removed deliberately fail; their `.log` files show the missing member. Gateway is the unchanged control. No full firmware link is represented.
- `fence-proof/ownership-results.json`: baseline `run_checks` 25/25, private helper removal rejected by O6c. The private removal does not implement a new controller; it isolates the stale helper requirement.
- `fence-proof/legacy-test-census.json`: six actual test bodies/twelve calls, counted after stripping comments. The initial seven-case census and its correction are retained, labelled. No runtime claim is made from this source census.
- `source-anchors.json`: 84 existing named anchors, all found. This existence scan does not replace the semantic fence proof.
- `mr-s8ac-r3-prepare.py`, `mr-s8ac-r3-fence-proof.py`: investigation drivers with absolute local paths. The proof driver preserves the initial seven-case comment-inclusive census; the final corrected census is separately identified. Review paths before reuse. Pointer `/tmp/mr-s8ac-r3-active` names the private archive `/tmp/mr-s8ac-r3-zgobkgs0`.
- `private-objects.json`: hashes of generated model/compile objects retained in that private archive; repository artifacts carry sources/commands/logs instead of binary objects.
- `register-intake.patch`, `receipt-append.patch`: exact shared-document edits from this coder checkpoint. `preservation.json` checks that every other original input remains unchanged; `SHA256SUMS` covers this directory except itself.

No production/test/tool edit, no new owner ruling, no full gate, no commit. The initial directory-symlink inventory error was corrected before snapshotting; it changed no shared file.
