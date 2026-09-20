# Bench-plan consolidation receipt — 2026-09-20

Documentation audit against MeshRoute `d11b5a90d151f71e1c0922fb0679521c3191ced1`. The owner authorised a new scenario-based authority, archival preservation and a full re-triage of 69 Parts for two Heltec V3s, a XIAO ESP32-S3, a Heltec V4 and a XIAO nRF52840/SX1262. This task resumes Claude's interrupted authoring work; none of the seven exhausted triage agents supplied a completed report. Their conclusions were not assumed.

The [metal test plan](../../2026-09-20-metal-test-plan.md) owns current hardware results. The [disposition map](../../2026-09-20-metal-test-triage.md) accounts for the original Parts and companion checks, distinguishes the actual retirement instruments from their physical limits, and records source corrections under B438. The [archive manifest](../../archive/2026-09-20-bench-records/manifest.json) identifies eight original documents and the two relative-link relocations in the readable copies. The archive's `originals.tar.gz` preserves the original bytes, including every historical result.

The task changed documentation only. No firmware, native, simulator, mutation or hardware gate was run; source validation of an existing instrument is not a claim to have rerun it. All 63 physical scenarios start OWED for a newly identified image. Named historical passes stay attached to their original runs, not silently upgraded or discarded. Twelve retirement groups apply only to named pure subchecks. Missing physical fixtures remain explicit; the external MeshRouteKit 7.34 check is unverified and is not retired.

Validation completed:

| Check | Result |
|---|---|
| Full Part mapping | 69/69 original Part IDs, each exactly once; companion MH/H5–H9 groups and the overlapping sheets mapped explicitly |
| Scenario structure | 63 unique procedures, each with 2–6 sequential steps, one PASS criterion, one STOP rule and one OWED current-result row |
| Retirement references | 12 groups, 77 links to 73 distinct existing instrument files; scoped limits stated beside every group |
| Archive preservation | 8/8 originals, 7243 lines / 493655 bytes, SHA-256-identical to both the pre-edit inventory and HEAD; readable copies differ only by the two recorded link destinations |
| Navigation | All 379 old heading anchors retained by stubs; scoped Markdown targets/fragments validated; no additional legacy aliases required |
| Markdown | Table column counts consistent; `git diff --check` clean |
| BLE input arithmetic | Independently reconstructed 274-, 268- and 265-byte vectors, excluding newline |
| Input preservation | 4740 tracked files inventoried before editing; all 399 production/test/tool/variant/simulation inputs in the guard set unchanged; tracked differences limited to the 13 documentation paths listed in the validation report |
| Concurrent work | Three manual documents hash-identical to their pre-edit versions; prior register byte-identical after subtracting this task's B438 block, dispatch pointer and next-free increment |
| Simulator | `6585649ea5a780f0542b2931853a667be56a5b2b`, clean; no edits/builds |
| Repository index | Empty staged diff; no staging or commits performed |

Raw [validation results](2026-09-20-metal-test-plan-rewrite/validation.json), [protected-input hashes](2026-09-20-metal-test-plan-rewrite/protected-inputs.json), [retirement instrument hashes](2026-09-20-metal-test-plan-rewrite/instrument-inputs.json) and [source locators](2026-09-20-metal-test-plan-rewrite/source-locators.json) are retained together. Locator matches identify the audited code/tests; they are not test-execution results. The [documentation freeze inventory](2026-09-20-metal-test-plan-rewrite/frozen-documentation.json) identifies this landing, excluding itself to avoid a self-referential hash.

The concurrent work was B437's manual audit (`docs/manual/README.md`, `command-reference.md`, `review-notes.md` and its register changes). It was preserved, not attributed to this consolidation. B438 closes documentation drift only; next free finding is B439. B278, B392 and other physical obligations are not closed here, nor are existing unrelated software findings such as B178/B118. Historical source pins inside frozen briefs remain untouched; their old bench paths lead through compatibility stubs to the preserved record and the current disposition.

There is no claim of freshly executed firmware gates or current-image hardware PASS. In particular, fake flash/TxDone, structural probes and core-only time tests do not qualify real writes, radio completion, actual display behavior or the config-handler wide-age boundary. External MeshRouteKit qualification and the explicitly missing fault/clock/RF fixtures remain visible. Commits are not prerequisites for a bench run: the plan accepts an identified base plus complete dirty/untracked build inputs and the actual flashed-image hash.
