# Repository cleanup — 2026-09-20

Owner request: remove accumulated temporary files/logs and clean the repository. Started from clean MeshRoute `0795225486c627a0ab834c94ee25e6c0751c80af`; simulator `6585649ea5a780f0542b2931853a667be56a5b2b` was clean. No uncommitted work existed at the start. No active build/gate was found; the measurement lock was held during local moves.

## Changes

- Retired **3631 files / 626459333 bytes** from 40 completed remote-admin run directories. They include 2086 logs, generated measurement JSON, compressed build/corpus/freeze bundles and historical copied-source/probe-driver snapshots. Live source, tests, tools and their input documents were outside the set.
- Retained every Markdown report and directly linked payload: **140 files** within those historical directories. Current reference scripts, command inventory/authority, A0 matrix, simulator baselines, bench records and the eight-original bench archive remain in place. Small older proof collections (`b183`, `b186a`, `b188`) and the current bench rewrite's inventories were not retired.
- Recorded exact paths, Git blob IDs, byte counts and SHA-256 values in the [recovery inventory](../../archive/2026-09-20-generated-evidence.jsonl). The [catalog](../../archive/2026-09-20-generated-evidence.md) gives tested Git recovery commands; existing history is the portable archive. No additional large archive was added to Git.
- Moved **4211 untracked local generated files / 1654722828 bytes** from 134 paths outside the checkout. These include old measurement/build output, the old compilation database/map, Python/pytest caches and Finder sidecars. They are preserved with per-file hashes in `/home/staszek/MeshRoute-artifacts/2026-09-20-cleanup-0795225/manifest.json`, alongside a safety copy of the retired committed files.
- Kept `.pio/build/`, downloaded dependencies and `.pio-measure/qa-s10-final/` in place. The latter preserves the maintained real-ELF test prerequisite. No worktree, simulator, application settings or Git objects were deleted.
- Added [retention guidance](README.md), a MEMORY pointer, and narrow ignore rules for `artifacts/`, pytest cache and raw evidence logs/firmware bundles. New source/fixtures/receipts remain visible to Git.

Before cleanup the tracked payload was **671842282 bytes in 4762 files**. Removing the retired set leaves **45382949 bytes in 1131 existing tracked files**, before the small policy edits and new recovery index/receipt. The new index is about 0.90 MB. Local `du -h` fell from **3.5G to 1.3G** including `.git` and retained build/dependency caches. This is a checkout reduction, not a claim of equivalent disk space freed: the local safety archive now holds the moved outputs, and `.git` retains historical blobs. Future full-history clones still carry that history.

## Verification

| Check | Result |
|---|---|
| Candidate integrity before moving | Every file matched the starting commit's blob ID and SHA-256 |
| Git recovery | `git archive` streamed all 40 original bundles; **3631/3631** retired files recovered with exact byte count, SHA-256 and Git blob ID |
| Local safety copy | Every moved file hashed again at its destination; exact match, including retained ELF data |
| Remaining original files | **1129 byte-identical**, excluding only the deliberately edited `.gitignore` and `MEMORY.md` |
| Existing file links | **1198 checked; zero new broken links** |
| Active instrument dependency audit | No live `src/`, `lib/`, `test/`, `tools/` or CI file referenced a retired bundle prefix |
| Command inventory | Stock `gen_command_inventory.py --check`: PASS, **197 rows**, byte-identical |
| Authority | Stock `check_command_authority.py`: PASS |
| A0 matrix | Stock `check_a0_matrix.py`: PASS, **21 enum members + 6 special rows** |
| Real ELF prerequisite | `test_measure_board.RuledPairTests.test_symbol_inventory_reads_a_real_elf_if_one_has_been_measured`: **1 test, OK, no skip** |
| Ignore rules | Four raw-output examples ignored; four source/fixture/receipt controls still visible |
| Whitespace/index | `git diff --check` clean; staged diff empty |
| Simulator | Same `6585649…` hash, clean; untouched |

No firmware behavior was changed. Native, corpus, board builds, mutation union and full tools discovery were not rerun for this storage-only cleanup; the checks above exercise the affected document/measurement dependencies. Historical gate claims are preserved with their original scope. All cleanup changes remain uncommitted for the owner.
