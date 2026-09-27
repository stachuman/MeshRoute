<!-- Author: Codex, independent Quality Agent; owner rules and commits -->
# Standalone Home W3 — independent QA

**Verdict: INDEPENDENT SOFTWARE QA PASS — 2026-09-25.** The frozen candidate satisfies revision 2’s C1 contract and
independent `src`-only gate (§4.2, P6). No production, test or tool correction is required.

The two counted-byte pager helpers preserve the detail modal’s current rows and cadence. Compose display width is
now independent of the record limit, at the same 17 columns. The shared settings opener and provisioning admission
preserve the current SETTINGS path, including refusal order and zero implicit saves. There is no new resident state,
Home entry, three-row modal, wire or NV change.

Contract: [approved brief revision 2](../plans/2026-09-25-standalone-mobile-home-w3-ui-model-seams.md), SHA-256
`9a84d03b0f2e9826df67feba52ff87c50644383bfb85ef8bc06af7ed8e25d825`.
Coder freeze: [receipt](2026-09-25-standalone-mobile-home-w3.md), SHA-256
`94aa5d341229ac617fc3a2c86b43a4a7820d87f098449d6b8066125bc5d9a041`.
QA records: [checksums](2026-09-25-standalone-mobile-home-w3-qa/SHA256SUMS).

## 1. Frozen inputs and preservation

MeshRoute HEAD is `8360802904f7bd0023279d3da844453d61207ede`, with the uncommitted W1c and W3 candidates.
Simulator HEAD is `6585649ea5a780f0542b2931853a667be56a5b2b`, clean and unchanged. QA worked on the frozen shared
checkout. No staging, commit, reset, cleanup or production/test/tool edit was performed.

All five W3 implementation hashes, the unchanged renderer, approved brief, coder receipt and preparation documents
match. The coder’s 31-file, pre-check’s 23-file and brief-review’s 12-file checksum inventories verify, as do all
73 retained coder raw artifacts. The [initial inventory](2026-09-25-standalone-mobile-home-w3-qa/inputs.json) records
**1,425 MeshRoute paths and 285 simulator paths**, including untracked inputs and symlink identities. Against the
pre-check there are exactly nine changed paths: the four permitted preparation documents and five fenced files.
See [preflight](2026-09-25-standalone-mobile-home-w3-qa/preflight.json).

Every original input and both Git status inventories remained identical through the last gate step:
[preservation before landing](2026-09-25-standalone-mobile-home-w3-qa/preservation-prelanding.json).
No nonignored QA file or landing edit was created until the board pair finished. It ran alone, gateway then mobile,
with `--jobs=1`, after the mutation processes had exited. The final preservation record distinguishes the four
subsequent authorized documentation edits and new QA evidence from the untouched candidate and historical receipts.

## 2. Independently executed gate

Commands, exits, durations and raw-log hashes are in the [run ledger](2026-09-25-standalone-mobile-home-w3-qa/run-ledger.json).
Every result in this table is from QA execution.

| Instrument | Result |
| --- | --- |
| Native | Fresh `pio test -e native`, then the binary: **2973 cases / 196111 assertions / 0 failed / 0 skipped** |
| Fresh stock simulator + corpus | **36/36 anchors PASS**, all **14 fields of each scenario row identical** to both pre-check and coder; s18 **269517 events**, MD5 **`32afbf11e43b4bf9d0bd470ad502ba0a`** |
| Board ABI, full controls | **290 checks**, **9/9 controls RED**, zero unusable; every pinned layout unchanged |
| Firmware-UI, full controls | l2 **518**, v3 **964**, BLE-row **518** checks, zero failed; **231 controls verified / 0 unusable**; C0 retains its required build-failure result |
| Required touched batteries | `model` **239 RED** + `sliceCbudget` **1 RED**; **240 total, zero unusable, zero vacuous**, every entry matches once; every worker baseline is **2973/196111/0** |
| Stock board pair | `.pio-measure/w3/qa-1`: all `measurements.*`, toolchain, fixed identity, paths, **ELF and payload hashes equal coder `final-1` on both boards** |
| Additional tools discovery | **356 tests, OK, no skips** |
| Additional ownership scanner | PASS |
| Supplemental board-UI `--no-neg` | Expected exit 1: **only W49/W51/W54**, identical to B418’s baseline; structural **23/23**, wiring **56/59**, **171 wiring controls RED**. This is not a full board-UI PASS |
| Hygiene | `git diff --check` passes in both repositories |

The native pin remains the bare `PIN_CASES, PIN_ASSERTS = 2973, 196111`. Its derivation is
**2962/195904 + Stage A 7/74 + Stage B 4/133 = 2973/196111**. QA reran the final suite; Stage A’s native counts are
retained historical evidence, with the seven unchanged characterization cases independently checked in the diff.
The optional tools rerun emitted the existing `/dev/null` ResourceWarning also recorded by W1c QA; no test failed
or skipped.

Per P6, QA did **not** rerun the six-environment warning census or the four dependency batteries
`uipresets`/`uisend`/`config`/`uiprov`. Their checksummed coder logs were inspected: six unchanged warning pins and
zero `-Wswitch`; the full six-battery coder union is **364 RED / 0 unusable**. These are explicitly coder-owned
results, not added to QA’s independent 240. No full board-UI gate or metal test was run.

## 3. Refactor and instrument review

[Source review](2026-09-25-standalone-mobile-home-w3-qa/source-review.json) and
[Stage B native diff](2026-09-25-standalone-mobile-home-w3-qa/stage-b-native.diff) record the scope audit.

- Pager geometry is constrained at compile time. Offsets and ceiling arithmetic use 32 bits; lengths through 255
  and valid page indices do not truncate. The helper writes counted bytes and exactly one NUL per row, leaving the
  tail untouched. Sanitizing, timers, blank/wake, deletion and modal state remain with their existing callers.
- `kComposeTextCols` is 17, derived from the 19-column body less two markers. Only display sizing and projection
  move to it; the record limit, live catalog send path and generation checks remain unchanged.
- `ensure_config_open` preserves failed-open retry and successful-open idempotence. `provision_admit` keeps conflict
  before unsaved, and only the caller transitions. Existing note clearing and dirty timing remain in place.
- The Stage B native diff adds only four helper cases, restates the two permitted display-bound assertions and
  updates related comments. All seven Stage A characterization cases remain unchanged.
- The unavailable fixture is correctly placed before the service’s first successful open. P29 does not send;
  P26 remains last because it leaves the alarm active. RELOAD/DISCARD through the service correctly avoid a
  navigation press clearing the very note the native fixture measures.

**Independent Stage A replay:** QA copied the complete frozen checkout, including uncommitted and untracked work,
then restored only the model and native test to their verified Stage A bytes. The unchanged frozen probe ran with
`--no-neg` against that original model: **518/964/518, zero failed**, matching the independent final probe.
This is a controlled reconstruction and positive characterization run, not a claim to have replayed the whole
historical gate. [Inputs and result](2026-09-25-standalone-mobile-home-w3-qa/stage-a-reconstruction.json).
The final full-control run remains the independent control gate.

All **238 ordered outcome lines** in QA’s final UI log equal the coder’s Stage A and final logs. The **149 uncovered
check names** also match as a multiset; only their `sort`/`comm` listing order differs.
[Comparison](2026-09-25-standalone-mobile-home-w3-qa/ui-comparison.json). The six new controls fail their intended
checks: D1 **20**, D2 **17**, C1 **8**, C2 **15**, U1 **2**, N1 **3**. An additional synthetic check of the exact frozen
`once` guard accepts a unique substitution and refuses missing, duplicate, ineffective and two-line substitutions:
[5/5 guard checks](2026-09-25-standalone-mobile-home-w3-qa/control-guard-checks.json).

The mutation registry was AST-read, never imported. Exactly eight entries changed; all other entries retain their
W1c bytes. All 364 needles in the coder’s union match once. QA’s full touched-battery runs reproduce:

| Re-anchor | Intended defect | Failed assertions |
| --- | --- | --- |
| M14 | Floor away a partial page | 25 |
| M15 | Zero pages for an empty body | 6 |
| M18 | Repeat row zero | 15 |
| M55 | Test unsaved before conflict | 4 |
| M56 | Admit unsaved | 8 |
| M57 | Admit conflict | 10 |
| M59 | Implicitly save and then enter | 17 |
| M100 | Defer opening until the menu is entered | 986 |

M100’s guarded-call shape retains its original effect; no weakened control or additional ruling is needed.
[Full mutation results](2026-09-25-standalone-mobile-home-w3-qa/mutations.json).

## 4. Layout and image measurements

| Quantity | Native | Mobile | Gateway |
| --- | --- | --- | --- |
| `UiState` | 504 | 504 | 504 |
| `UiSnapshot` | 1336 | 1336 | 1336 |
| `UiModel` | 928 | 912 | 912 |
| `ComposeSlot` / `ComposeList` | 20 / 161 | 20 / 161 | 20 / 161 |
| `Node` | 235208 | 122176 | 157304 |

All are unchanged at the ABI pins. The probe distinguishes a measured declaration from a type actually compiled
into an environment’s feature TU; gateway does not acquire OLED state.

| Board | QA RAM | QA flash | W3 delta from verified coder base archive |
| --- | --- | --- | --- |
| gateway | 203740 B | 571936 B | RAM 0, flash 0; identical image |
| heltec_mobile | 211724 B | 1394588 B | RAM 0, flash −4 B |

QA independently built the final pair and compared every measurement and payload field, not just size totals.
[Comparison](2026-09-25-standalone-mobile-home-w3-qa/board-comparison.json). QA also reran all four stock
repeatability comparisons on the verified coder archives. The coder’s board source digest reconstructs exactly by
excluding only the **33 later receipt/evidence files**, leaving its original **1392 inputs**:
[source reconciliation](2026-09-25-standalone-mobile-home-w3-qa/coder-board-source-reconciliation.json).

The archived mobile symbol inventories independently yield 15 changed symbol rows, one added symbol and net **−5 B**:
new `ensure_config_open` **110**, `sync_settings` **−102**, `settings_activate` **+8**, `inbox_detail_cb` **+8**,
`refresh_detail_page` **−2** (net **+22**); the other unchanged-source functions in that TU total **−27**.
The loadable `.flash.text` delta is **−4**, with no other loadable-section or RAM delta. These totals support the
coder’s code-generation/link-placement attribution; QA did not independently rebuild the pre-W3 ELF or reproduce
its original object-generation step. [Archive-derived deltas](2026-09-25-standalone-mobile-home-w3-qa/archived-board-deltas.json).

## 5. Findings, limits and landing

- **B453 OPEN:** `sliceEcascade` E14 has **zero matches at HEAD and at the freeze**. Its tuple and
  `lib/core/node_cascade.cpp` are identical to HEAD. The old Slice-E insertion-point contract predates the now-landed
  custody enqueue; it needs separate tool work and a current-contract audit. It is outside W3’s required and coder
  union sets. No execution of that unrelated battery or inherited RED is claimed.
  [Independent reproduction](2026-09-25-standalone-mobile-home-w3-qa/e14-finding.json).
- **B454 CLOSED, documentation only:** the tracker’s draft-status sentence contradicted the reviewed design.
  Corrected, with its refresh date, during the authorized status landing.
- **Declared negative space accepted:** five record preconditions and three no-save/apply/load checks have no
  renderer control. Native characterization and M59 observe the relevant model effects. No claim of full
  renderer-control coverage is made.
- Unattached/still-closed admission cannot be reached through today’s SETTINGS path. Its first reachable test stays
  with W4b’s new caller, as the approved brief requires. Three-row buffers remain local tests, not resident UI state.
- Host render assertions cover the characterized rows and existing probe surfaces; they are not physical-pixel
  proof or an exhaustive screen transcript. B418 stays with W2, B449 with W4a, and B350’s diagnostic-mode wording
  remains open. W3 adds no metal behavior, bench row or metal PASS.

QA lands register §0 and B453/B454, the design’s W3 status, tracker and MEMORY pointers. The implementation,
approved brief, coder receipt and prior evidence remain unchanged. **Next: W4a pre-check, then the author’s brief,
with B449 folded in. W1/W1c/W3 prerequisites are satisfied; commits do not block progress.**

Raw QA logs are retained under `artifacts/2026-09-25-standalone-mobile-home-w3-qa/`; fresh simulator streams and the
Stage A reconstruction are under `/tmp/meshroute-w3-qa-n5ab83ph/`. Board archives are under `.pio-measure/w3/qa-1/`.
The [raw artifact inventory](2026-09-25-standalone-mobile-home-w3-qa/raw-artifacts.json) hashes the local logs and board
images. Ignored local output is not a backup; no cleanup was performed. Compact records and all source hashes remain
with this receipt.
