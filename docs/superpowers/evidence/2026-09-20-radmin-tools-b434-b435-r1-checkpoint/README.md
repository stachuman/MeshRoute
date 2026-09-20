# Revision-1 HOLD checkpoint — B436

See the adjacent coder receipt. This archive does not claim the union passed.

- `frozen-overlay/` holds the two candidate tool files and the exact four permitted QA inputs used by the gate. Apply it over MeshRoute `e680271`; simulator pin is `6585649`.
- `gate-inputs-before.json` and `gate-inputs-after.json` match all 4,488 shared inputs during the attempt. `stage-before.json` / `stage-after.json` match 4,831 copied files, including ignored input files.
- `frozen-tree-inputs.json` covers the post-receipt tree outside this archive. The register differs only by mandatory B436 bookkeeping (`m1-register.diff`); the brief and implementation remain frozen.
- `tools-discovery.log`: 356 OK, no skips. `focused-discovery.log` names both executed RED controls. `selftest.log` and `retained/selftest/` retain the complete fabricated outcomes.
- `mut_radmin8node.log`: the completed off-floor battery; `retained/radmin8node/N01.log` is the actual parent-retained PIO output. Three other `mut_` logs end at interruption. No result from them counts as PASS.
- `b436-base-harness.log` and `b436-mutant-binary.log` prove that this same mutant is usable and fails real assertions. `base-harness.py` is byte-identical to the pinned base.
- `platformio-source-evidence.json` and `pio-test-help.log` support the build-plus-test diagnosis and proposed `--without-testing` correction. No such correction is landed.
- `gate.py`, `reproduce_b436.py` and `stop_audit.py` preserve the commands used. They name the original scratch paths; these are evidence drivers, not new repository tools. Do not run `land_checkpoint.py` again: it is the completed receipt/registration operation.
- `SHA256SUMS` hashes every archive file except itself.
