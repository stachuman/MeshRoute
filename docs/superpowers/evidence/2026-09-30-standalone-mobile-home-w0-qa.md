Author: Codex — independent Quality Agent, 2026-09-30.

# W0 — independent implementation gate

**Verdict: INDEPENDENT SOFTWARE QA PASS.** The complete revision-3 §4.2 gate was rerun on the frozen, uncommitted W0 candidate. **B440, B448 and B482 close.** The implementation preserves live identity fields when building `/mrid`, refuses overlength names, publishes only after durability, and coalesces identical records without leaving a stale live name. No production, test, tool, approved-brief or coder-evidence file was edited by QA. Nothing was staged or committed; the simulator is unchanged.

One supplemental evidence claim is corrected below: **B489**, a stack extractor that omitted a 288-byte gateway compiler clone. It is not a production failure or a new resident allocation. The approved transcript deferral remains in force. Hardware qualification remains **OWED**.

## Frozen inputs and scope

- MeshRoute HEAD `0f23aee8f5eeaa6e176e3ea37b2acaffb33622ec` plus the complete working tree, including the uncommitted and untracked implementation; simulator `6585649ea5a780f0542b2931853a667be56a5b2b`, clean.
- Authorized [brief revision 3](../plans/2026-09-29-standalone-mobile-home-w0-identity-record.md), 585 lines, SHA-256 **`2197165149e58a25b3eeccfc88906e652e69641734d8a532a123ecc42e56a6d8`**. The [approval receipt](2026-09-30-standalone-mobile-home-w0-brief-rereview-2.md) and brief remain unchanged.
- [Coder freeze](2026-09-29-standalone-mobile-home-w0.md): all **11 fenced hashes**, **6 preparation hashes** and **7 read-only pins** matched. Nothing was staged. Against the prior approval inventory, the nine existing fenced files changed, the two new fenced probe files and declared evidence were present, and nothing was missing or unexplained.
- Checksum verification: coder **42/42**, pre-check **36/36**, first review **7/7**, first re-review **17/17**, second re-review **6/6** — **108 entries** verified. The coder's **169** referenced local raw artifacts also matched their recorded hashes; this verification is not represented as execution of those runs.
- The complete QA entry inventory contains **2239 MeshRoute paths / 285 simulator paths**. Every one remained unchanged through the last gate step, before documentation landing. [Inputs](2026-09-30-standalone-mobile-home-w0-qa/inputs-entry.json), [preflight](2026-09-30-standalone-mobile-home-w0-qa/preflight.json), [preservation before landing](2026-09-30-standalone-mobile-home-w0-qa/preservation-before-landing.json).
- This is the **P6 `src`-only independent gate**, as specified by §4.2. Selector (b), full tools discovery and the six-environment warning census remain in the coder's chain. No `lib/`, wire, NV-layout, boot-recovery or simulator change is included.

## Independently executed results

Each required instrument was run by QA. The native wrapper was followed by the actual binary. Simulator configuration/build and the corpus used fresh directories outside both repositories. Mutation workers used isolated copies; the board pair ran sequentially after those workers finished, without overlapping edits or builds.

| Instrument | Independent result |
| --- | --- |
| `pio test -e native`, then `./.pio/build/native/program` | **3059 cases / 199638 assertions / 0 failed / 0 skipped** |
| Fresh stock simulator; corpus `--require-anchors`, then `--validate` | **36/36**, byte-identical to both pre-check and coder on all **14 per-stream fields** |
| ABI stock / `IdBlob` overlay | **290 / 299 checks**, each **9/9 controls RED**, no unusable controls; `IdBlob` **80 B / alignment 4** on all three ABIs |
| Inbox-verbs default, all five arms | ACCEPT **1400/63**; CLIENT **483/71**; OLED **27/7**; identity ACCEPT **175/13**; identity CLIENT **176/13** (checks/controls), zero failures/unusable |
| Console-sink default | **720** checks, **84** structural, **905** BLE-guard, ownership **6 + 3 controls**, **152** total controls; PASS |
| Board-UI default | **592** identities, exactly once; canvas **124/110**, traits **14**, missing traits **12**, structural **23**, wiring **60**, wiring controls **186**, negctl **60/3**; PASS |
| Deferred-actions, unchanged source | **150/151/158/158** checks, **39** transcript rows per profile; **40** controls compile and fail assertions; PASS |
| Device-radio | **96 + 41** checks, **25** structural, **72** controls, zero unusable; PASS |
| Provisioning-TX | **20** structural checks, **47 distinct controls RED**; PASS |
| Ownership scanner and controls | **218** files; **43 controls / 0 unusable**, PASS |
| Inventory, authority, literals | **197** inventory rows, stock check PASS; authority PASS and **6/6** self-tests RED; literals PASS |
| Selector (a), `devicenv` | **56/56 RED**, zero unusable/vacuous, every anchor matches once; all **8** worker baselines **3059/199638/0** |
| Stock board pair, `--jobs=1` | Both PASS; final ELF, payload, symbols, sections and every measurement field match the coder's `final-1` |

**Corpus keystone:** s18 **269517 events**, MD5 **`32afbf11e43b4bf9d0bd470ad502ba0a`**, SHA-256 `27ecfa988f062d3b27ed9b19159965ac393a0f03e2499011d87d9797fe64cc54`. These were checked against the current `simulation/BASELINE.md`, not inferred from the handoff. Fresh `lus` SHA-256 **`e304147d99ae166fb815e6a2ef06c5ff905579b68add3b0ba142d7031e26caa2`**, identical to the pre-check and coder binaries. [Full manifest](2026-09-30-standalone-mobile-home-w0-qa/corpus-manifest.json), [field comparison](2026-09-30-standalone-mobile-home-w0-qa/corpus-compare.json).

**PIN re-synced? YES —** the independently established pre-check baseline **3055/199532** plus the four new `test_device_nv.cpp` cases and **106** assertions gives **3059/199638**, matching the fresh binary, the harness constants and all eight QA worker baselines. `devicenv` **46 + 10 = 56** entries. The two identity arms derive **175 = 140 storage-matrix + 12 rename + 18 grammar + 4 coordinate + 1 regen** checks, CLIENT **176** with its extra admission check, and **13 = 12 W0 controls + 1 crash-classifier control**. Legacy arm pins are unchanged.

The mutation reconciliation compares actual worker verdict labels with all 56 source-declared entries. The consolidated baseline summary is not counted as a ninth worker. Inbox reconciliation separately accounts each arm's assertion controls and crash-classifier control; it does not rely on the stale `BOTH ARMS PASS` banner. [Mutation reconciliation](2026-09-30-standalone-mobile-home-w0-qa/mutations-reconciliation.json), [inbox reconciliation](2026-09-30-standalone-mobile-home-w0-qa/inbox-reconciliation.json).

## Source and proof review

The four production diffs were independently reviewed against the base:

- `mrnv::id_blob_from_live` clears the entire candidate, admits the original `size_t` length before narrowing, then stamps/copies all identity fields. A refusal leaves a zero record. `IdBlob` layout, magic/version, readers and boot behavior are unchanged.
- `mrfw::id_candidate_from_live` is the one gatherer of the running seed, full counted name and live coordinate globals. The three cfg arms and `regen` use it. No field comes from rejected NV bytes.
- `rename_node` uses a successful whole-record read only for the byte-identical durability decision. Both success paths make the requested name live; a failed save publishes nothing. The divergent live/durable-name case is covered, as is repair when live name equality cannot prove durability.
- Coordinate setters replace one field of the live candidate, perform one save, then publish both mirrors. `regen` preserves CLIENT admission, entropy, the checked save in `do_regen`, installation and output order. No extra owned state was introduced.

The new identity arms link the **real config and command TUs**. Their labelled storage faults cover four writers across seven conditions, with a deliberately divergent durable fixture. The legacy stand-in is excluded from these arms; it exists only for the legacy command-TU builds. No proof here is satisfied through that stand-in. All twelve W0 controls match exactly once and fail their intended assertions on **both** identity arms:

| Control | Intended failure count per arm |
| --- | --- |
| C1 / C2, failed-read reconstruction in coordinates / regen | **15 / 14** |
| C3 / C4, removed length admission / console clamp | **8 / 8** |
| C5 / C6, publish name / coordinates before save | **1 / 2** |
| C7, omit the NodeConfig coordinate mirror | **7** |
| C8 / C9, write a no-op / coalesce from live equality | **2 / 7** |
| C10, identical-record return omits live-name publication | **1** |
| C11, reserve a byte in the counted-name adapter | **1** |
| C12, adapter reconstructs seed/position from NV | **35** |

Crashes, compilation failure and vacuous edits do not count as these assertion failures. High-byte and UTF-8 boundary diagnostics use hex. These are host fault-injection proofs, not observations of damaged physical flash.

The additional R23 live-fixture initialization is accepted: it is the USB repetition of R12's output proof and needs the same live-name/position precondition. Its expected output remains unchanged; `seed_id` still installs no crypto. Excluding the legacy R7/A7/A10 mutation block from the identity-only arms leaves those controls active and RED in the legacy arms.

The source-reader census confirms selector (a) is `devicenv` alone. All affected stock readers ran, including the unchanged deferred-actions builder. The regenerated inventory is identical to the base after normalizing source line anchors: **197 rows, no semantic change**. [Source review](2026-09-30-standalone-mobile-home-w0-qa/source-review.json).

## Boards and allocation

QA used `.pio-measure/w0-qa/2026-09-30/`, with the stock tool and fixed build identity. Comparison with the coder's preserved `final-1` matches **artifacts, all measurement fields, toolchain, build identity, paths, host and concurrency metadata**. `source` metadata differs because the freeze receipts and review files now exist; ordinary `.pio` metadata differs after the independently run native build. Neither is hidden or passed to the same-source repeatability comparator.

| Environment | RAM, base → QA | Flash, base → QA | Attribution reproduced |
| --- | --- | --- | --- |
| gateway | **203740 → 203740 (+0)** | **571936 → 572096 (+160)** | `.text` +160; six changed function symbols net +144; remaining section bytes are outside named function sizes |
| heltec_mobile | **219540 → 219540 (+0)** | **1400276 → 1400540 (+264)** | `.flash.text` +248, `.flash.rodata` +16; fourteen changed function symbols net +234 |

The complete symbol/section output is byte-identical to the coder's final output. Against the preserved base, writable `OBJECT` symbol multisets remain identical (**235 gateway / 2338 mobile symbols**), including duplicate identities; no compensating added/removed RAM object is hidden by the equal totals. Function deltas, including the neighboring compiler-generated size changes, are listed without truncating their names in [the independent comparison](2026-09-30-standalone-mobile-home-w0-qa/boards-compare.json). Equal final ELFs reproduce the coder's compiled result; they do not make a function-size difference a runtime stack bound.

QA final payload hashes:
- gateway: `7fa27c2b884cf4b4b9869b3c9937ef264e40cd9369c5926c006aaf64f3fb837d`;
- heltec_mobile: `045d4e144a60c950ea0895fa136a000e63dbf7bbd17c9e193b2d4460a452b98e`.

[Gateway manifest](2026-09-30-standalone-mobile-home-w0-qa/board-gateway.json), [mobile manifest](2026-09-30-standalone-mobile-home-w0-qa/board-heltec_mobile.json). Raw ELFs, images, symbols, sections and build logs remain in the measurement directory, hashed in [raw artifacts](2026-09-30-standalone-mobile-home-w0-qa/raw-artifacts.json).

## Evidence corrections and open follow-ups

**B489 — gateway stack clone omitted by the supplemental extractor.** The coder's §5 says the zero-byte `rename_node` wrapper means its body was inlined into `handle_cfg_set`. That statement is false. The extractor's name regex misses the raw `.su` row `firmware_config.cpp:260:14:0(const char*, size_t)`, **288 bytes**.

A fresh QA gateway compile using `-fstack-usage -fno-lto` reproduced that row. Object disassembly shows a direct call from `handle_cfg_set` to `rename_node [clone .part.0]`: **24 bytes of saved registers + 264 local bytes = 288**. The public wrapper tail-branches to that clone. `id_candidate_from_live` is inlined into the clone, so its standalone 64-byte frame must not be added again on this path. The local caller-plus-clone subtotal is **448 + 288 = 736 bytes**, excluding ancestors and other callees. This is **compiler-frame evidence**, not a task high-water mark or a complete path bound.

The mandatory `heltec_mobile` stack evidence does not contain this omitted anonymous clone and is unaffected: the reviewed coder frames are `handle_cfg_set` 352, `rename_node` 240 and adapter 80; its stated name/coordinate/regen compiler-frame subtotals are 3536/3296/3264. QA reviewed those preserved captures; it did **not** rerun that optional-for-QA mobile stack measurement. No stack-limit violation or new resident allocation is inferred.

**Disposition:** software PASS stands; the supplemental gateway claim is superseded by this correction. **B489 remains OPEN** for a separately fenced clone-aware extractor and regression. The frozen coder receipt/helper are preserved. B489 is not silently added to the owner-ruled B478/B487/B488 package. [Fresh compile receipt](2026-09-30-standalone-mobile-home-w0-qa/stack-compile.json), [raw `.su` diagnostic](2026-09-30-standalone-mobile-home-w0-qa/gateway-config.su), [disassembly excerpt](2026-09-30-standalone-mobile-home-w0-qa/stack-disassembly-excerpt.txt), [reproducer](2026-09-30-standalone-mobile-home-w0-qa/stack-clone-proof.py).

**B479 — existing aggregate wording drift, extended in place.** The runner now executes five arms but still says `BOTH ARMS PASS`; its shared summary names `handle_clear_inbox` even for the identity-only driver. Independent reconciliation confirms all five ran. This is retained wording, not evidence that a control was skipped. No duplicate finding is opened.

**Two extra coder comparison attempts were correctly rejected.** The raw recipe also invoked stock `measure_board.py compare` on `base-1` versus `final-1` for each environment; both exited **2** because the sources/images intentionally differ. These calls are not PASS and are not the four required within-baseline/within-final repeatability comparisons, which passed. QA performed the required field-by-field attribution and its own final build comparison. This correct tool refusal is not a product defect.

## Limits, landing and next step

Not run by QA: selector (b), full tools discovery and warning census (coder duty under P6/§4.2); the transcript comparator (explicit owner deferral); unaffected UI/BLE/other probes; any metal test. The affected-reader census and independent runs above define this verdict's coverage. In particular, no claim is made that the legacy transcript driver currently links, that its comparator passed, or that its coverage is unchanged.

B483–B486 remain separate: BLE refusal visibility, leaf-label truncation, BLE identity-read authority and NV overlength validation are not fixed by W0. The identity probe's supplied `LineSink` proves its stated sink path, not the untouched dedicated BLE cfg adapter.

QA landing changes only the register, design W0 status, tracker/MEMORY pointers, [metal USB-BLE-03](../../2026-09-20-metal-test-plan.md#usb-ble-03), and this QA evidence. **B440/B448/B482 close in place**, B479 is extended, B489 is registered; next free finding **B490**. The metal row retains OWED and now covers exact name/coordinate commands, rejected overlength rename, physical restart and regen preservation on ESP32 Preferences and nRF52 LittleFS. Existing NV procedures retain power-cut/failure qualification.

[Final preservation](2026-09-30-standalone-mobile-home-w0-qa/preservation-after-landing.json) checks the entire entry inventory: only those five documentation paths change; new files are confined to this QA receipt/folder. All coder inputs, the approved brief and coder evidence remain hash-identical; all 42 coder checksum entries still verify; simulator clean, index empty.

**Next:** the separately briefed **B478 + B487 + B488 tool package**, then W7, per the owner's ruling. This independently passed uncommitted tree may be the next base under commit-plus-SHA inventory; a commit is not a progress gate.

Evidence is sealed in [SHA256SUMS](2026-09-30-standalone-mobile-home-w0-qa/SHA256SUMS), including this report. Commands/exits/log hashes are in [runs.json](2026-09-30-standalone-mobile-home-w0-qa/runs.json); native and supplemental reconciliation are in [verification.json](2026-09-30-standalone-mobile-home-w0-qa/verification.json). Raw QA logs remain under ignored `artifacts/2026-09-30-standalone-mobile-home-w0-qa/`, simulator/stack scratch under `/tmp/mr-w0-qa-akh045fd/`, and board outputs under `.pio-measure/w0-qa/2026-09-30/`. No frozen evidence file is rewritten to conceal the corrections above.
