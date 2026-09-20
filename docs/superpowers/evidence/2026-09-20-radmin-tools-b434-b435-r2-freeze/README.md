# Revision-2 coder freeze — independent QA pending

See §6 of the adjacent coder receipt. All results here were rerun on revision 2.

- Base `e680271`, simulator `6585649`; authorized brief `f6eafd394894fd65512fcd66ba9613ff7c94308763de3ef4af486daffa3a1048`.
- `frozen-overlay/` contains the two final tool files and all four permitted QA documents. Apply over the named base; include the existing revision-1 checkpoint/receipt as recorded in the complete manifests. Never measure HEAD alone.
- `preflight-inputs.json` records entry; `gate-inputs-before.json` / `gate-inputs-after.json` are identical gate inventories; `stage-before.json` / `stage-after.json` are identical rsync-stage inventories.
- `frozen-tree-inputs.json` covers the final shared tree outside this archive, including the updated receipt. `final-scope.json` confirms the two tool files plus receipt are the only changes from revision-2 entry outside this archive.
- `mut_*.log` and matching JSON files contain every fresh battery command, return code and elapsed time. `union-audit.json` reconciles every configured label, baseline, restoration, exit and banner count. The only known unusable is B342 `sliceBmac` M04, a compiled survivor printed as FAIL.
- `tools-discovery.log`: 356 OK, zero skipped. `selftest.log`, `focused-discovery.log` and `retained-selftest/` cover the real classifier/retention and required controls. `b436-removed-build-only-control.*` records the additional executed B436 regression control (no build).
- `probe_ui_model_mutations.py.r1-to-r2.diff` is one argv correction; the test diff strengthens the existing verdict test. Files prefixed `r1-input-` are the preserved incoming candidate, not the final tools. `base-harness.py` is the original committed harness.
- `gate.py`, `audit_union.py` and `freeze.py` are archived evidence drivers with the original scratch paths. Do not rerun the completed freeze operation. The canonical tools remain under repository `tools/`.
- `SHA256SUMS` hashes every archive file except itself. No results from the failed revision-1 union are counted here.
