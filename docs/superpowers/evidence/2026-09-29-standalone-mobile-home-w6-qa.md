Author: Codex — independent Quality Agent, 2026-09-29.

# W6 — independent implementation gate

**Verdict: INDEPENDENT SOFTWARE QA PASS.** The complete revision-2 §4.2 gate was rerun on the frozen, uncommitted W6 candidate. B335, B475 and B477 close in place. No production, test, tool, approved-brief or coder-evidence file was edited by QA. Nothing was staged or committed; simulator unchanged. Four separate follow-ups are registered as B478–B481; their limitations and disposition are below. Hardware qualification remains OWED.

## Frozen inputs and scope

- MeshRoute `70ff486b40c9b07001b33bcbcdf640ad494c1149` plus the complete working tree, including uncommitted and untracked inputs; simulator `6585649ea5a780f0542b2931853a667be56a5b2b`, clean.
- Authorized [brief revision 2](../plans/2026-09-29-standalone-mobile-home-w6-phrases.md), SHA-256 `cdcef1ee1826c14a8e941e05ef4b4bfdb8e7004926cfd22f7e2a44025716828e`, 692 lines. The brief remains unchanged; its authorization is the [scoped review](2026-09-29-standalone-mobile-home-w6-brief-rereview.md).
- [Coder freeze](2026-09-29-standalone-mobile-home-w6.md): every one of its 26 fenced hashes, six preparation hashes and two read-only rerun inputs matched before QA. The index was empty. Against the pre-check inventory, only the 21 edited fenced files and four declared preparation files differed; nothing was missing or unexplained.
- Checksum verification: coder evidence **43/43**, pre-check **45/45**, brief review **14/14**, scoped re-review **9/9** — **111 entries**, all matched. Their captures and the full **2097-file MeshRoute / 285-file simulator** input inventory are in [the QA evidence folder](2026-09-29-standalone-mobile-home-w6-qa/inputs.json).
- This is the P6 `src`-only QA gate specified by §4.2, despite the separate NV record format change. `lib/`, the wire, simulator, partitions and `/mrcfg` are unchanged. The coder owns selector (b), discovery and the warning census; those are not represented here as independently rerun.

## Independent runs

Every row below is from this QA run, not copied from the coder's recommendation. Exact commands, exit codes, log hashes and control reconciliation are in [first-results.json](2026-09-29-standalone-mobile-home-w6-qa/first-results.json); raw captures remain under ignored `artifacts/2026-09-29-standalone-mobile-home-w6-qa/`.

| Instrument | Independent result |
| --- | --- |
| `pio test -e native`, then `./.pio/build/native/program` | **3055 cases / 199532 assertions / 0 failed / 0 skipped** |
| Fresh stock `lus`, full corpus with `--require-anchors`, then validation | **36/36**, all **14 fields** identical for every stream to the pre-check and coder manifests |
| Stock ABI | **290 checks, 9/9 controls RED, 0 unusable** |
| ABI with the new-type supplemental pins | **308 checks, 9/9 controls RED, 0 unusable** |
| Firmware UI default | **591/1059/591**, zero failures; **240/240 controls** accounted once, no guard failures/unusable entries |
| Static send-line ownership within firmware UI | One writable **199-B** `s_send_line`; all **three** stack/undersize/duplicate controls compile and reject the property with exit 1 |
| Board UI default | **592/592 identities**; live wiring **60 × exit 0**, mutant wiring **186 × exit 1**; V3/V4 canvas controls **60/3 RED** |
| Console sink default | **720 checks / 84 structural / 905 BLE guard / 152 controls**, zero unusable; six profiles |
| Inbox verbs ACCEPT / CLIENT / OLED, default | **1400/63**, **483/71**, **27/7** checks/controls; zero failures/unusable on all three arms |
| Deferred actions default, source unchanged | **150/151/158/158 checks**, 39 transcripts each, **40 controls RED** |
| Command inventory `--check` | **197 rows byte-identical** |
| Nine selector-(a) batteries | **437/437 RED**, zero unusable/vacuous; **58** independent clean worker baselines **3055/199532/0** |
| Own stock board pair, `--jobs=1` | Both final ELFs, payloads and every measurement field match the coder's final-1; details below |

The independent firmware-control census matches each of the 240 source declarations to exactly one accepted log verdict; C0 keeps its required build failure. The three line-ownership controls are additional checks, not silently added to that 240 total. The inbox runner's legacy aggregate wording still says `BOTH ARMS PASS`; the three actual arms above were executed and reconciled (B479).

Fresh simulator binary SHA-256: `e304147d99ae166fb815e6a2ef06c5ff905579b68add3b0ba142d7031e26caa2`, also byte-identical to the pre-check binary. s18: **269517 events**, MD5 **`32afbf11e43b4bf9d0bd470ad502ba0a`**. Full [manifest](2026-09-29-standalone-mobile-home-w6-qa/corpus-manifest.json) and [comparison](2026-09-29-standalone-mobile-home-w6-qa/corpus-compare.json) retained. No source-only inference substitutes for this fresh corpus run.

PIN re-synced? YES — approved base 3031/198613 + 24 cases / 919 assertions = independently executed 3055/199532; all 58 QA mutation-worker baselines agree. The native per-file breakdown remains the coder's derivation; QA independently confirms the total and the stock harness pin. OLED pins 27/7 and ABI re-pins also reproduce.

### Mutation reconciliation

| Battery | Configured | RED | Unusable/vacuous |
| --- | --- | --- | --- |
| `model` | 264 | 264 | 0 |
| `devicenv` | 46 | 46 | 0 |
| `uisend` | 26 | 26 | 0 |
| `uipresets` | 39 | 39 | 0 |
| `uipresetverbs` | 25 | 25 | 0 |
| `sliceCbudget` | 1 | 1 | 0 |
| `sliceCsend` | 1 | 1 | 0 |
| `w4aident` | 9 | 9 | 0 |
| `w4bhome` | 26 | 26 | 0 |

The [literal anchor census](2026-09-29-standalone-mobile-home-w6-qa/mutation-census.json) found exactly one match per configured entry before execution. The [execution reconciliation](2026-09-29-standalone-mobile-home-w6-qa/mutation-results.json) independently matches the exact configured label sets to the merged RED records, each with positive failed-assertion count and match count one. There were no stale-pin banners or custom baseline/selection overrides. Every orchestrator proves the real checkout's 62 target files unchanged. Selector (b)'s additional 112 entries and the coder's optional 200-entry supplemental run were not rerun by QA.

## Board objects, images and stack limits

Stock command: `python3 tools/measure_board.py pair --jobs=1 --output .pio-measure/w6-qa/final-1`. It ran after all host probes and mutation batteries ended, with no concurrent source edits, builds or `.pio/` changes. Both environments built sequentially under the stock lock. The fresh directory and fixed identity/path recipe were used without overrides.

| Environment | QA RAM | QA flash | Delta from preserved base |
| --- | ---: | ---: | --- |
| gateway | 203740 B | 571936 B | RAM 0 / flash 0; ELF and payload unchanged |
| heltec_mobile | 219540 B | 1400276 B | **RAM +7752 B / flash +2580 B** |

Every `measurements.*` field, artifact hash, toolchain, environment, fixed identity, host, concurrency contract and path field matches coder final-1. Source-snapshot and ordinary `.pio/` metadata are recorded separately because QA adds evidence and runs native; they are not disguised as identical whole manifests. Stock `compare` is deliberately not used between different source inventories. [Field comparison](2026-09-29-standalone-mobile-home-w6-qa/board-comparison.json) and copies of both QA manifests are retained.

The independent writable-object comparison against the retained baseline has exactly four changes:

| Object | Base | QA | Delta |
| --- | ---: | ---: | ---: |
| `preset_catalog()::cat` | 1132 | 8572 | +7440 |
| `s_model` | 936 | 1000 | +64 |
| `s_frame_state` | 520 | 560 | +40 |
| `s_send_line` | absent | 199 | +199 |

Object total **+7743 = +7440 catalog +303 D15**. The remaining **9 bytes** are linked alignment: `.dram0.bss` grows 7640 for 7639 object bytes, `.dram0.data` grows 112 for 104 object bytes. This is not an additional retained carrier. Mobile flash sections reproduce the coder attribution: `.flash.text` +2212, `.flash.rodata` +256, initialized data +112 = +2580. The complete symbol inventory hash matches the frozen final artifact; no extra object or type growth was found.

On all three ABIs, slot/blob **167/2852**, ComposeSlot/List **20/161**, SendReq **16/4**, SendLive **12/4**, ReviewPhase **1/1**. Catalog **8584 host / 8572 boards**; UiState **568 host / 560 boards**, UiModel **1016 host / 1000 boards**; UiSnapshot **1368**, UiChrome **20**, Node **235208/122176/157304** unchanged. The ABI table also preserves feature-TU inclusion distinctions: a measured type is not automatically a resident object in the headless image.

The coder's `-fstack-usage` evidence was read, not recompiled by QA: command-chain subtotal **3472→3632**, setup plus boot wrapper **1200→1120**, send path **1872→1744**. These are compiler-frame sums, not a task high-water mark or complete runtime-stack bound. No physical NVS occupancy, wear, power-cut or radio/glass qualification is claimed.

## Contract and evidence review

[Source-review notes](2026-09-29-standalone-mobile-home-w6-qa/source-review.md) record the reviewed diff and reader boundaries. In particular:

- v1 recognition uses only size/magic/version; old contents are not interpreted; every old-v1 boot produces its own diagnostic with zero writes. A first successful mutation replaces it, including a restated default. Failed writes preserve the published catalog.
- The real routed OLED arm proves exact 2852-byte retention, fresh catalog and boot reload, capacity-plus-one refusal, and the intentional dishonest controls. The reused deferred-action builder remains intact and independently passes.
- Every maximum page fits a clean, non-draining 2048-B stage; the largest is **1097 B**. A draining host receives it whole. Reset-all is **1517 B** with ten-digit generation and also passes wrap. The restored 160-byte line and unpaged 17-record list controls fail the relevant checks.
- The real firmware path opens review without sending, defaults to BACK, captures complete text separately from an owed request, freezes page/header/LOC, and rechecks generation/team/known peer identity at execution. Known zero hash remains a labelled synthetic case. Emergency admission remains independent of ordinary gates.
- The expectation/control/mutation ledger accounts for the changed direct-double fixtures, defaults, record sizes, new refusals and render expectations. The prior Inbox two-row pager and other package properties remain covered. Unreddened preconditions and defence-in-depth assertions are explicitly not claimed as individually mutation-proven.
- The manual and companion contract carry pagination, capacity versus text_max, 163-byte validation and v1 reset policy. No command-inventory row or board-UI anchor moves; `firmware_commands.cpp` remains 1915 lines.

The coder's first two chain attempts and discarded board-output reuse are disclosed in its receipt; none supplies a result to this independent gate. This QA gate used new captures and a fresh board directory. A QA receipt-collection script initially expected C0's log to use the assertion-RED format; it was corrected to recognize its mandatory build-failure format. The initial mutation collector also counted nine aggregate baseline summaries as workers; it now counts distinct `[wN]` records and reconciles each declared worker set, yielding 58. No maintained instrument, input or executed result changed, and neither failed collection supplied a verdict.

## Follow-up findings

These are registered in place under M1 and do not invalidate W6's measured software contract:

- **B478 — mutation-output decoding.** A labelled synthetic child emits raw `0xBB` through the actual AST-extracted `run_suite`; strict UTF-8 decoding raises `UnicodeDecodeError`. Only the build result is stubbed. This is a harness failure that rejects an incomplete run, not a false PASS. W6's final tests and all 437 QA mutations complete. [Reproducer](2026-09-29-standalone-mobile-home-w6-qa/decode-reproducer.py), [result](2026-09-29-standalone-mobile-home-w6-qa/decode-result.json).
- **B479 — retained count wording.** Five unchanged comment lines still describe old state/reason counts; the inbox aggregate banner also says “both” after three arms. [Exact locations](2026-09-29-standalone-mobile-home-w6-qa/comment-drift.json). This is nonblocking wording debt; no classifier, emitted record, control or execution was absent. A future correction needs the D6 reader audit, including the fenced-out commands header; QA has not silently repaired production or tools.
- **B480 — direct mutation gap.** The 264-entry model census contains no direct deletion of the DM compose generation-close action. Y06 changes the predicate, not this call. Native closure cases remain. This is a static coverage finding, not a measured surviving mutation or new runtime defect.
- **B481 — legacy alarm fixture.** The unchanged “compose send cannot overwrite a queued alarm” case never queues an ordinary send: the active emergency absorbs its gestures. The fresh real-model reproduction prints `compose_none=1 emergency_first=1 ordinary_request=0`. The neighboring queue-priority/retention case remains meaningful and passes. [Query](2026-09-29-standalone-mobile-home-w6-qa/alarm-fixture-query.cpp), [build/result receipt](2026-09-29-standalone-mobile-home-w6-qa/alarm-fixture-result.json).

B476's BLE short/failed-write issue remains open and separate. No stock profile combines this OLED catalog with the nRF BLE backend; this gate does not close that transport policy or its physical qualification.

## Landing, preservation and limits

The final pre-landing inventory proves all original inputs unchanged, the simulator clean at its pin, and no staged files. Only after all instruments finished did QA update the five maintained documents: register, design §13, tracker, MEMORY and metal plan. [Before](2026-09-29-standalone-mobile-home-w6-qa/preservation-before-landing.json) and [after](2026-09-29-standalone-mobile-home-w6-qa/preservation-after-landing.json) explicitly separate the frozen gate from that authorized documentation landing. The brief and coder report/evidence remain unchanged.

B335/B475/B477 are CLOSED for software. Metal UI-12, UI-15 and NV-06 are refreshed, and UI-22 adds full phrase review on the physical panel/radio; their status remains OWED. The approved design is not authority to start another implementation: the next package follows the owner's order and its own pre-check/brief. W7 still depends on W0. A commit is not a progress prerequisite.

Not rerun, by §4.2/P6: selector (b), full tools discovery, six-environment warning census, the coder's extra dependency union, and compiler stack pricing. Not run: metal, physical NVS stats/power cuts, actual BLE delivery or real-radio maximum-phrase delivery. The software PASS does not qualify those observations.

The [SHA256SUMS](2026-09-29-standalone-mobile-home-w6-qa/SHA256SUMS) seals this report and the compact evidence. [Raw-log inventory](2026-09-29-standalone-mobile-home-w6-qa/raw-logs.json) records ignored local captures and hashes. Board ELFs, payloads and symbol/section inventories remain under `.pio-measure/w6-qa/final-1/`; simulator artifacts remain at `/tmp/mr-w6-qa-rkz9ymlg/`. These locations are not an owner backup. No cleanup was performed.
