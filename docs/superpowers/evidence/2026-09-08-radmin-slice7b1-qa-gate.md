<!-- Independent Quality Agent / specification author: OpenAI Codex -->
# Slice 7b-1 revision 6 — independent implementation gate

**Verdict: PASS — Slice 7b-1 revision 6, independent software gate, 2026-09-08.** QA independently executed every required instrument against the complete frozen coder handoff. The sole unusable mutation remains the previously recorded B342 exception; it is neither closed nor counted RED. Final implementation changes and the QA documentation landing remain uncommitted. The takeover's focused results are historical and do not substitute for this full gate.

## 1. Frozen inputs and independence

The owner handed over coder evidence §10 at MeshRoute HEAD `146569a33b6451852555ac9ffcfd5079cd843393`. That owner commit preserved the partial implementation and QA documents; it is not the slice attribution base. The original base remains `d467787f5e02836ad19b79624d97729c81ebacf8`. Handoff status was 13 modified tracked files, no untracked files. The total slice diff against the original base spans 34 paths, including the implementation originally committed by the owner in `146569a`.

QA captured **all 921 tracked and nonignored untracked paths**, preserving symlink text and every uncommitted implementation change, at `2026-09-08T18:08:43Z`. The before/after snapshot hashes agreed. Build products were not copied as source. Evidence root: **`/tmp/mr-qa-s7b1-full-0a_x80i4`**. Its `input-manifest.json` SHA-256 is `905424d8ff6c0f37033219ea3873266e9d52481dc320898a23ddeda90ede9b6d`; `receipt.json`, `status.txt`, both binary patches and `changed-from-base.txt` record provenance.

Bindings (SHA-256):

| Input | Digest |
| --- | --- |
| Consumed revision-6 brief | `b68785d099cac9fa85361c4d9c3da62ac8d5b938bfd752d2380391eb93c76b9c` |
| Frozen coder report | `a934b6ed4fee849a0738baedaf1bdf8708b14a7fdc0c2ab78580c536b7e4192e` |
| `lib/core/remote_session.cpp` | `8db598428c9b4541f44a7f9d7b12abfac2e2ec9e6e78052cf1db57ade3d205e8` |
| `lib/core/remote_session.h` | `289c58cb243c9a95435f8f9f2ef50ab65ce8dfbfa4ec73cf1416e4e0310a90a0` |
| `lib/core/node_mac_rx.cpp` | `71b216f1a7f0e8063a0e7541340a64953d8096c29f9d3935332ad282fb089f1d` |
| `test/test_node_remote_session.cpp` | `a784ac24d35ea51ce39e5d354ed74602f498109bdccc4fe211399594808c78b0` |
| Mutation harness | `abfeedfae6d53d50f503333371cff50c9402ed590a51ac44da5d2f050ebe168d` |

`final/` and `mutations/` contain independent copies of the complete final source; `base/` contains the original attribution commit. `boards/` was built first with the base, then overlaid with the frozen final inputs and checked against the manifest before the second measurement. This preserves identical board build paths across the comparison. No board measurement overlaps a source edit in its tree. Mutation workers own separate scratch copies. The shared checkout receives QA documentation only; no production fix, reset, clean or commit was performed there.

Simulator source remains `/home/staszek/lora-universal-simulator` at `06746a97de5764415d6fcef10b97bca90569b9c7`, clean. Both simulator build directories are new QA-owned directories configured with the corresponding MeshRoute source path. No simulator source or existing build tree was modified.

## 2. Source review and B374/B375 proof

The implementation matches the revision-6 lifecycle: reserve one header and one chunk before dispatch; retain bounded carrier-sized output; complete once; seal/send without rewriting the completed result; retain executed fingerprints across ACK; abandon work on normal invalidation. The ACK is authenticated and consumed without an ingress reservation (`remote_session.cpp:942`), so an owner waiting in CONTROL cannot prevent capacity release. B369 clears only expired, never-executed admission rows; completed/acknowledged rows retain their replay protection.

`Node::radmin_send_frame()` (`node_mac_rx.cpp:2119`) checks queue capacity, invokes the real encoder with the full-size buffer, and separates encode refusal from transport refusal. Seal failure wipes the temporary buffer, increments only the saturating seal counter, emits only its scalar event and returns without enqueue. Successful queued/parked ownership advances the cursor; refusal preserves it. `radmin_service_once` (`firmware_remote_executor.h:49`) retries pending work next eligible pass. Authenticated exact request retry (`remote_session.cpp:1013`) independently resets completed replay to sequence zero.

The new named **SYNTHETIC** Node case (`test_node_remote_exec.cpp:33`, fixture at `test_node_remote_session.cpp:559`) passes **142 assertions**. It publishes sequence zero, injects a scoped retained-epoch mismatch at pending sequence one, compares the entire session except the expected counter bytes, distinguishes counter/event/transport behavior and saturation, restores the epoch, resumes sequence one without a request, then authenticates an exact retry and compares every replayed body. The fault is explicitly artificial: real epoch installation invalidates this state. This is real Node/encoder accounting under an injected guard refusal, not a demonstrated production crypto failure or nonce-reuse vulnerability.

The three new mutation batteries independently pass **44/44 RED**: transcript 20, executor 11, Node sender 13. The sender controls discriminate missing/wrong/wrapping counters, misclassified events, enqueue after encode failure, cursor advance/reset, terminal replacement, queue pacing and successful cursor advancement. The pure one-byte-buffer B374 case remains separate from the Node proof.

The firmware probe, not native's counting fake executor, owns real-handler claims. Its real command/inbox translation units and real core drive authenticated radio flights, local/remote `status` and `version` byte equality, unknown/disruptive refusals, authenticated ACL self-slot protection and no console/BLE transcript leak. Separate executed inbox rows preserve channel/E2E/custody/end records while hiding private application DMs for operator and owner; shared DM cursor/delete refusals remain.

## 3. Independently executed instruments

All logs below are under the evidence root in §1. No reported coder count is substituted for an execution.

| Instrument | QA measurement | Receipt |
| --- | --- | --- |
| Native wrapper **and actual binary** | **2883 cases / 127709 assertions / 0 failed / 0 skipped** | `native-build.log`, `native.log` |
| Original-base wrapper and binary | **2855 / 121999 / 0** | `base-native-build.log`, `base-native.log` |
| Simulator | Fresh Release builds, **70 compile/link actions per arm**; both core variants executed | `sim-{base,final}-build.log`, `sim-compile-actions.json` |
| Corpus | **36/36 anchors; 36/36 streams byte-identical to fresh base; zero assertion failures** | `corpus-{base,final}/manifest.json`, validation logs, `corpus-byte-comparison.json` |
| s18 | **269517 events**, MD5 **`32afbf11e43b4bf9d0bd470ad502ba0a`** | Actual streams; current `simulation/BASELINE.md` table |
| Full board ABI | **191 checks / 9 controls RED / 0 unusable**; native Node **225920/8**, gateway **152288/8**, mobile **117912/8** | `abi.log` |
| B278 ABI | **42 measurements / 6 controls RED** | `b278.log` |
| Console sink | **720 executed / 82 structural / 905 BLE guard / 6 ownership + 3 controls; 146 controls RED**, zero unusable | `probe-console_sink.log` |
| Inbox verbs | ACCEPT **771 / 50 RED**, CLIENT **378 / 45 RED**, zero unusable | `probe-inbox_verbs.log` |
| Firmware UI | **433 / 868 / 433** executed checks; **223 controls RED / 0 unusable**; coverage **703/840** labels, prior uncovered set retained | `probe-firmware_ui.log` |
| Custody USB / BLE line | **27 / 10 controls RED**; **40 / 8 RED**, zero unusable | `probe-custody_usb.log`, `probe-ble_line.log` |
| Features / ownership | **9 cells / 120 checks / 59 controls RED**; nine-file ownership contract with **40 controls**, zero unusable | `probe-features-configured.log` |
| Warning census | **173/178/177/177/182/182**, zero switch warnings, all six derived environments pass | `census.log` |
| Board pair, base and final | Both sequential pairs pass; attribution below | `boards-{base,final}.log`, `boards/.pio-measure/qa-{base,final}/` |
| Mutation union | **47 batteries / 675 RED / 1 unusable**, solely B342 | `mutation-summary.json`, `union/` |
| Tools sweep | **343 tests OK / zero skips**, correctly configured complete rerun | `tools-configured.log`, `configured-results.json` |
| Inventory / authority | **204 rows**, `--write`/bare/`--check` pass; authority table/header/inventory agree; **6/6 selftests RED** | `inventory*.log`, `authority*.log`, `inventory-scope.json` |
| A0 / literals / whitespace | **21 enum members**, **215 active source files**, both repositories' `git diff --check` pass | `a0.log`, `literals.log`, `whitespace.log`, `final-integrity.json` |

Fresh simulator MD5s reproduce the coder's independently: base `9f5a4b9872c4fe407c319f767b9236b6`, final `1c1a7435713ea2dfb50457aac6b1db50` (final SHA-256 `7ad152073f13a76192ff2185488326910d28a759c5c882b1cfa59b012b95e2b9`). QA counted **22 actual compile commands per core variant**, plus shared monocypher/console compilation. The verbose log agrees with `compile_commands.json` after removing only CMake's generated dependency-output flags. The larger action count than the coder's 36 is a fresh-build versus incremental-build distinction. Both corpus directories validate; all scenario input hashes and baseline hashes agree; all 36 stream files were directly byte-compared. The generic identical-binary corpus comparator is not used across a changed executable.

Native pin derivation from fresh, filtered XML runs (unrelated cases explicitly skipped in those filtered runs): new transcript **12/4460**, pure executor **7/115**, Node exchange **9/933**; existing session cases stay 32 with **854 → 1056 assertions (+202)**; existing Node session cases stay **12/188**. Their sum is **+28 cases / +5710 assertions**. The full ordinary binary has zero skipped cases.

**PIN re-synced? YES — 2855/121999 + 28/5710 = 2883/127709**, independently measured and equal to the live harness pins. `native-pin-derivation.json` and the ten filtered XML receipts carry the arithmetic. No whole-suite XML PASS is claimed over the known B364 fixture over-read.

All six probes also ran in `--no-neg` mode. Those runs are observation-only; B350's firmware-UI wording remains an open defect. Only the full controlled runs supply the gate. The feature probe's corrected full and no-controls runs both use the documented simulator path. The final tools sweep executes its existing real-ELF test using hash-verified copies of the freshly measured QA board captures.

Inventory regeneration is byte-identical to the frozen final inventory. Against the original base, removing only `.cpp`/`.h` source-line anchors yields byte-identical complete content across all 204 rows (normalized SHA-256 `2835da287a91244f78078574029c1c57711f6eb0bf2712a8b668ed5ce14a7d70`). There is no verb/authority change. QA's brief closure addendum explicitly accepts that mechanical anchor refresh under the required inventory gate; no table/header change is needed.

## 4. Mutation acceptance set

QA independently parsed the frozen `TARGET_SRC`/mutation tables and derived the source diff against original `d467787`. The changed-source selector is **16 batteries / 223 entries**; the separate dependency/historical selector is **31 / 453**. Their intersection is empty here; the required union is **47 batteries / 676 entries**. All 676 patterns match exactly once. `union/selectors.json` records the groups and every count; `qa-union.py` is QA's independent orchestrator.

Changed source: `a0rx`, `b159map`, `b159rx`, `b161rx`, `b251rx`, `radmin3verbs`, `radmin5rx`, `radmin5session`, `radmin7exec`, `radmin7rx`, `radmin7transcript`, `sliceBnode`, `sliceBrx`, `sliceEnode`, `sliceGrx`, `teamgrant`.

Dependencies/history: codec/line/authority (`radmin2codec`, `consoleline`, `cmdauthority`); acting ACL/root/runtime/NV/key wiping (`radmin3acl`, `radmin3id`, `radmin5runtime`, `devicenv`, `teamkeyring`); by-hash transport, queue/park and receive/return history (`grantpark`, `grantadmit`, `b161hash`, `b251hash`, `b161mac`, `b159mac`, `b20mac`, `b20codec`, `sliceBmac`); inbox storage/read/delete/clear and JSON (`sliceAinbox`, `sliceGinbox`, `sliceCinbox`, `sliceCpull`, `sliceDclear`, `sliceDstore`, `sliceDtoken`, `b134inbox`, `b134store`, `b134ram`, `sliceAjson`, `sliceDack`, `sliceGjson`, `b134ack`). Real firmware handler wiring is independently attacked by probe controls; the native pure executor battery does not claim it.

**Final union: 675 RED / 1 unusable across all 47 batteries.** QA inspected every final summary, required the derived 2883/127709/0 baseline in every battery, reconciled every entry count and exit status, and verified all 56 configured source-file hashes after each battery. The sole nonzero exit is `sliceBmac`: 3 RED / 1 unusable, M04/B342. Its explicit failure is “the suite still PASSES; nothing measures this.” It remains open under the same recorded exception as the prior Slice 5/7a gates, never counted RED or removed. The full per-battery roll-up is `mutation-summary.json`.

## 5. Independent board attribution

Both arms use the same private checkout/build paths, fixed identity, toolchains and sequential runner. All ELF/payload hashes match their measurement manifests before and after read-only analysis. `attribution.json`, `attribution.log`, normalized `symbols.txt`, section reports and the raw QA readelf outputs retain the full derivation.

| Metric | Gateway base → final | Mobile base → final |
| --- | --- | --- |
| Node | 150504 → **152288 (+1784)** | 117912 → **117912 (0)** |
| RAM | 197188 → **198980 (+1792)** | 207756 → **207756 (0)** |
| Flash | 555292 → **562812 (+7520)** | 1372732 → **1372992 (+260)** |
| Objects | **285 → 285** | **329 → 329** |

Gateway RAM is fully attributed: `g_node` **+1784 = 1776 pool + 6 counters + 2 state alignment**; active-context pointer **+4**; `.bss` alignment **+4**. Actual object-address coverage has **113 → 117 bytes of gaps**, with the new four-byte gap between `factory_erase`'s `fl` and `mrnv::save`'s eight-aligned `cur`. `.bss` is **196180 → 197972**; `.data` **976** and `.noinit` **32** are unchanged. Mobile mutable symbols and RAM sections do not move; no target executor or mutable active-context pointer is present.

Gateway flash is `.text` **+7520**, split into **+7500 named symbols +20 literal/alignment bytes**. The recorded per-symbol deltas include the new transcript/executor functions and compiler outlining of the ACL handlers; no source refactor is inferred from those symbol changes. Mobile flash is `.flash.text` **+212** (**+195 named +17 other**) plus `.flash.rodata` **+48** (**24-byte immutable local context +24 strings/alignment**). The named mobile code delta comes from inbox refusal/view, scope access and seam code. This corrects the brief's narrow zero-flash/`mesh_service_once` prediction; the ruled mobile **Node and RAM** control still holds. No new allocation ruling is made.

## 6. Findings, limitations and execution corrections

- **B376 CLOSED:** independently confirmed the restored live retry-route assertions and **25/25 RED** session battery, including unchanged S22. Registered and closed in place with the complete slice gate.
- **B377 CLOSED:** pin comments precede plain assignments; the parser/pins remain unchanged. The independent console gate and full **343-test tools sweep, zero skips**, pass. Registered and closed in place.
- **B315 remains open:** QA reproduced its known output-path validation defect on the first board launch: external output was accepted initially, then the compiler-state guard rejected it after **18.15 s**. No measurement was accepted. `boards-initial.log` / `boards-base-initial.log` preserve the error; corrected output under the private checkout's `.pio-measure/` produced both complete pairs. No tool guard was weakened.
- **QA configuration correction:** the first feature runs in the isolated checkout lacked the expected sibling simulator directory. Env-map E0 refused with `simulator CMakeLists not found ... (set MR_LUS_SRC)`, leaving 115 checks versus 120; those runs are failed, not partial passes. The first tools sweep also failed: **343 tests / four feature-wrapper failures / one real-ELF skip**, exit 1 (`tools.log`). The documented `MR_LUS_SRC=/home/staszek/lora-universal-simulator` setting restores all 120 feature checks in both full and no-controls runs. The complete corrected tools sweep passes **343 tests / zero skips**, exit 0, in 606.621 s. Verified copies of QA's fresh board captures supply its existing real-ELF check (`tools-measured-elf-input.json`). Neither production nor instrument source was changed for these setup corrections; failed receipts remain visible in `instruments-results.json` alongside their replacement runs in `configured-results.json`.
- **B342, B350, B359 and B364 remain separate open limitations.** B350's no-controls wording is not gate evidence; only the full controlled firmware-UI run can supply that gate. The optional stale 0e ABI overlay (B359) is not the default ABI probe. The known invitation fixture over-read (B364) is untouched; filtered remote XML is used only for pin derivation.
- The simulator's normal-arm `set_window_anchors` array-bounds warning independently reproduces in both fresh builds (base `node.cpp:1087`, final `:1111`), with no new suppression. The board warning census passes its existing pins. No new Node reorder warning was introduced.
- QA's initial compile-action receipt comparison was overly literal: verbose CMake commands add `-MD/-MT/-MF` dependency-output flags absent from the compile database. Comparing tokenized commands after removing only those generated flags verifies all 46 relevant compile actions; no build result was replaced.

Session-control/open-response behavior remains 7b-2; scheduling/deferred actions remain 7b-3; the product controller remains 8a/8b. No new metal result is claimed. The brief defers the target `status` round trip to 8b's controller/carrier metal gate; no placeholder bench part is added for an unavailable controller.

## 7. Final verdict and documentation landing

**PASS.** The implementation satisfies the consumed revision-6 contract. No remaining production/brief contradiction blocks this slice: send-time failures preserve completed results and cursor; an eligible pass resumes the pending frame, while an authenticated exact request retry restarts seq0; all three counters are distinct and saturating. The synthetic real-Node proof, ordinary radio flights, real-handler probe and complete mutation union establish their separate claims. Mobile flash prediction drift and mechanical inventory anchors are resolved in the QA closure addendum with measurements, without changing an owner ruling.

QA checked all **921 original input hashes** after the instruments completed: `final/` and `mutations/` match exactly, with no additional nonignored inputs. In the shared checkout, only the five QA documentation paths below differ from the frozen inputs. `final-integrity.json` records the final check, document hashes, unchanged MeshRoute HEAD and clean simulator HEAD; `status-after-qa.txt` records the complete resulting worktree. The coder report retains its frozen SHA-256 from §1. No production/test/tool input, anchor table or simulator file was changed by QA.

The documentation landing comprises this report; maintained register §0 and closure in place of B365–B377 (B373's comment closure retained); MEMORY's durable completion/contract line; design §§10/11/15/19/19.1 retention, retry, counters and split ownership; and a current-status/closure addendum to the consumed revision-6 brief. Its original consumed hash remains explicit, and historical preflight/checkpoint statements are labelled as such. The owner alone commits. Next is the 7b-2 brief against the ensuing owner commit, not a dispatch of next-slice implementation.

No hardware PASS is claimed. The target `status` round trip is deferred to 8b's controller/carrier metal gate, as the brief requires; no placeholder bench part is added. B315/B342/B350/B359/B364 remain open with the limits above.

## 8. Reproduction entry points

Use a fresh copy of the full captured inputs, not a checkout of `HEAD` alone. The evidence root retains the exact commands and exit statuses in `instruments-results.json`, `configured-results.json`, `simulator-results.json`, `boards-results.json` and `union/results.json`.

- Native: `pio test -e native`, then `./.pio/build/native/program`; base and final independently. Filtered XML runs derive pins only.
- Simulator/corpus: `qa-simulator.py` configures separate fresh Release builds with `MESHROUTE_DIR` pointing at each arm, builds verbose `lus`, runs `tools/run_corpus.py --require-anchors --lus <that-arm-binary>` and validates both corpora. `corpus-byte-comparison.json` records direct stream comparison; `sim-compile-actions.json` binds actual compiler actions to both core variants.
- Boards: `qa-boards.py` runs `tools/measure_board.py pair --jobs=1 --output <private-root>/.pio-measure/qa-base`, overlays all captured final inputs only after completion, runs the final pair at the same build paths, then the warning census. `qa-attribution.py` reads preserved ELFs/symbols/sections and verifies artifact hashes before and after analysis.
- Instruments: `qa-instruments.py` records the complete named probe/checker chain. `qa-configured-followthrough.py` supplies `MR_LUS_SRC` and repeats both feature modes and the entire tools sweep after measured-ELF inputs are present. The source and pins are unchanged.
- Mutations: `qa-union.py` independently derives and records both selectors, runs each full battery in an isolated worker tree, and checks source restoration. All 47 logs remain under `union/`; `mutation-summary.json` reconciles every result, including B342.
- Input preservation: `qa-final-integrity.py` compares all captured input hashes and nonignored path sets, permits only the named QA documentation landing in the shared checkout, verifies both HEADs and simulator cleanliness, and runs both repositories' whitespace checks.
