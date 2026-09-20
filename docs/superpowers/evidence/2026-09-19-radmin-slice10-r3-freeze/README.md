# Slice 10 revision-3 coder freeze — ready for independent QA

Base MeshRoute `4ad9c343b42bab4c91bd8e6721b45b2dfb769e7f`, simulator `6585649ea5a780f0542b2931853a667be56a5b2b` (clean, unchanged). Brief SHA-256 `5eab4af5cd08666368a58a0196bb034e7a8822b563175378b519fc136de71e9b`. Nothing is staged or committed. Independent QA and Part 57f on metal remain pending.

## Candidate and reconstruction

The base hash alone is not the implementation. `freeze-overlay.tar.gz` contains every dirty tracked and untracked input outside this archive, including all implementation, QA preparation, the brief and historical checkpoint evidence. Overlay it onto a separate checkout of the pinned base. No source file is deleted. `overlay-inputs.json` verifies every overlaid byte. This evidence directory is retained separately beside that overlay, avoiding a recursive archive. Never reset or clean the shared checkout to reconstruct it.

`freeze-inputs.json` covers the complete shared tracked/untracked input set, including this archive except its own record and `artifact-sha256.json` (the two stated circular-metadata exclusions). `artifact-sha256.json` pins all archive files except itself. `final-*-inputs.json` and `union-inputs-inputs.json` are complete pre-gate snapshots, not HEAD-only exports. `resume-inputs.json`, `repairs.json` and `source-audit.json` prove the five incoming QA-document deltas and the exact two additional fenced repairs. All production/test/tool inputs stay frozen throughout.

`inventory-generation-delta.json` records the required generated inventory's single source-location update: `peerkey` / `service_console`, `fw_main.cpp:1215` to 1214, zero semantic row changes (197). It is the one expected generated-document difference between the pre-gate snapshot and final candidate. `post-run-report-delta.json` separately identifies final reporting edits. The frozen brief itself never changes.

## Fresh evidence only

Receipt §8 in `../2026-09-19-radmin-slice10.md` is the current result. No interrupted revision-2 result is inherited. `gate-summary.json` reconciles the final runs: native 2950/195770/0/0 skipped; full tools 349/0/0; corpus 36/36 whole-file byte-identical; union 61 batteries /983 RED /1 known B342 /984 configured /zero vacuous. Each final command has a log and result JSON with command, cwd, exit and SHA-256. The 38 final command records include the separate simulator whitespace check; base attribution measurements and 61 union commands are additional. Mutation logs retain each fresh clean baseline and every verdict; B342 / `sliceBmac` M04 is not counted RED.

`tools-first-attempt/` is the failed first discovery (349 run, one failure, no skips), never the final verdict: the coder started discovery before inventory regeneration, so the table test saw the old line hint. B433 records the ordering error. The entire discovery was rerun after generation; `final-logs/tools.log` is the complete subsequent green run. No instrument was weakened or changed for this correction.

`fresh-base-board-pair.tar.gz` and `fresh-candidate-board-pair.tar.gz` contain both stock deterministic pairs, including ELF, payload, sections, symbols, manifests, compiler state and build logs. Their independently verified member hashes are in `*-board-artifacts.json`. `board-attribution.json` closes RAM to the shrinking save buffer and gateway Node, and flash to the named loadable sections. `native-binary.tar.gz` retains the actual freshly executed native binary; its SHA is separately recorded.

`corpus-base.tar.gz` and `corpus-final.tar.gz` retain both freshly generated 36-stream corpora, input snapshots, manifests and per-scenario logs. `corpus-byte-comparison.json` records actual whole-file equality and each full digest. Fresh simulator build logs show both codec variants recompiling; baseline authority remains `simulation/BASELINE.md`. s18 is `32afbf11e43b4bf9d0bd470ad502ba0a`, 269517 events, zero assertion failures.

`deferred-actions-details.tar.gz` retains the executed local, remote and radio variants and 40 controls in both modes. Other probes' checks/controls are in `final-logs/`. The real NV migration proof uses the existing ESP32 NV arm over a labelled synthetic byte medium; it does not execute real flash or `nv_load_stamped`. Part 57f remains NOT RUN.

Scratch root `/tmp/mr-s10-r3-j6pcxvmv`; driver copies describe the orchestration, with `MR_LUS_SRC` set to the pinned simulator for discovery. The tools fixture is the fresh gateway ELF. No git wrapper, index override, staging or simulator edit is used. To bound scratch storage, only this run's completed generated corpus/build copies were discarded after their archives were verified; all measured artifacts and all shared work are retained here.
