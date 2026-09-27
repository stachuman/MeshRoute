<!-- Author: Codex, independent Quality Agent; owner rules and commits -->
# Standalone Home W4a — independent QA

**Verdict: INDEPENDENT SOFTWARE QA PASS — 2026-09-27.** The frozen candidate satisfies the approved revision-2 brief and its independent `src`-only gate (§4.2, P6). No production, test or tool correction is required for W4a. **B441, B449 and B455 close.** B456 records the probe's separate missing-control false-PASS defect; this QA run accounts for every control independently. Panel glyph appearance remains metal-only and OWED.

Contract: [approved brief revision 2](../plans/2026-09-26-standalone-mobile-home-w4a-identity-labels.md), SHA-256 `7fafe9ef08aa50842a30240b08fa079cf8ce67c608f46d8ebe390ed879fff067`.
Coder freeze: [receipt](2026-09-26-standalone-mobile-home-w4a.md), SHA-256 `9427e4a6ff834ad1f43b09fe4631fe3bd70662bcd12e62801525eded26641b01`.
QA evidence: [checksum inventory](2026-09-27-standalone-mobile-home-w4a-qa/SHA256SUMS).

## 1. Inputs and preservation

MeshRoute HEAD is `8360802904f7bd0023279d3da844453d61207ede`, including the uncommitted W1c, W3 and W4a candidates. Simulator HEAD is `6585649ea5a780f0542b2931853a667be56a5b2b`, clean. HEAD alone does not identify the tested source.

The [initial inventory](2026-09-27-standalone-mobile-home-w4a-qa/inputs-start.json) records **1,548 MeshRoute paths and 285 simulator paths**, including untracked files and symlink identities. All ten implementation hashes match the coder's freeze, all six preparation inputs match the brief, and the coder/pre-check/review checksum indexes verify **44/30/12** files. Against the pre-check's 1,455 paths there are exactly **14 changes: four preparation documents and ten fenced files**, no missing or unexplained paths. Every W1c/W3 input outside the W4a fence is preserved. The coder's 202 listed raw artifacts also verify, including the discarded chain and archived board outputs. See [preflight](2026-09-27-standalone-mobile-home-w4a-qa/preflight.json) and [raw verification](2026-09-27-standalone-mobile-home-w4a-qa/coder-raw-verification.json).

QA ran on the frozen shared tree. The board pair ran alone, gateway then mobile, with `--jobs=1`, before the other gate builds. No nonignored QA file or documentation landing was created until all builds and mutations finished. [Pre-landing preservation](2026-09-27-standalone-mobile-home-w4a-qa/preservation-prelanding.json) confirms every original path and both Git inventories remained unchanged through the last gate step. The [landing record](2026-09-27-standalone-mobile-home-w4a-qa/landing.json) distinguishes the five authorized documentation updates and new QA evidence. No staging, commits, reset, cleanup, simulator edits or production/test/tool edits by QA.

## 2. Independent execution

The [run ledger](2026-09-27-standalone-mobile-home-w4a-qa/run-ledger.json) records commands, exits, durations and log hashes. These figures are QA executions, not the coder's numbers adopted as evidence.

| Instrument | QA result |
| --- | --- |
| Native | Fresh `pio test -e native`, then `./.pio/build/native/program`: **2986 cases / 197299 assertions / 0 failed / 0 skipped** |
| Fresh stock simulator and corpus | **36/36 anchors PASS**; all **14 fields per scenario** identical to both pre-check and coder. s18 **269517 events**, MD5 **`32afbf11e43b4bf9d0bd470ad502ba0a`**, read against current `simulation/BASELINE.md` |
| Stock board ABI | **290 checks / 9 of 9 controls RED / 0 unusable**; every pin unchanged |
| Supplemental ABI | Current `TeamRow` **40**, `InviteIdRows` **26**, `OutcomeView` **52**, unchanged on native, mobile and gateway; separate measurement, not claimed as stock-probe entries |
| Firmware-UI default gate | **529/529 l2**, **980/980 v3**, **529/529 BLE-row**; **236 verified controls / 0 unusable** |
| Independent control-completeness audit | **236 defined labels = 236 distinct observed verdicts**, no missing, duplicate or unexpected label; no shell/helper errors. C0 fails to build as required; the other 235 compile and fail checks |
| Required touched mutation batteries | **301 RED / 0 unusable / 0 vacuous**: model 239, sliceCbudget 1, uiteam 20, uiinvite 32, w4aident 9. Every worker baseline **2986/197299/0** |
| Stock board pair | Own `.pio-measure/w4a-qa-20260927/`; both ELFs, payloads and every measurement equal coder `final-1` |
| Additional tools discovery | **356 tests OK, no skips** |
| Supplemental board-UI `--no-neg` | Expected exit 1, **only W49/W51/W54** (existing B418), unchanged from baseline; not a full board-UI PASS |
| Hygiene | `git diff --check` clean in both repositories |

**PIN re-synced? YES — 2973/196111 + 13 cases / 1188 assertions = 2986/197299.** The harness literal matches the directly executed binary. The derivation is against the verified uncommitted W3 base, not HEAD's older suite.

The [fresh corpus manifest](2026-09-27-standalone-mobile-home-w4a-qa/corpus-manifest.json) and [comparison](2026-09-27-standalone-mobile-home-w4a-qa/corpus-layout-comparison.json) preserve all stream results. The simulator was configured in a new external build directory against the full working tree. Its `lus` SHA-256 is `e304147d99ae166fb815e6a2ef06c5ff905579b68add3b0ba142d7031e26caa2`.

The supplemental ABI driver extracts the current `OutcomeView` declaration exactly once, generates a temporary TU, and invokes the three real ABI toolchains through the stock probe's measurement function. The [driver](2026-09-27-standalone-mobile-home-w4a-qa/layout-measure.py), [generated TU](2026-09-27-standalone-mobile-home-w4a-qa/layout-query.cpp), input hashes and [results](2026-09-27-standalone-mobile-home-w4a-qa/layout.json) are retained. `UiState` stays 504, `UiSnapshot` 1336, `UiModel` 928/912/912, `InviteMember` 20 and Node 235208/122176/157304 (native/mobile/gateway). Layout and linked residency are separately measured.

Per P6, QA did not repeat the six-environment warning census or the five dependency batteries (`uisend`, `sliceCsend`, `chrome`, `uinearbyrow`, `uigeo`). Their checksummed coder captures were inspected: six unchanged warning pins, zero `-Wswitch`, and the full ten-battery union **388 RED / 0 unusable**. The independent 301 above excludes those additional 87 entries. Discovery was voluntarily rerun; its existing `/dev/null` ResourceWarning is also recorded by W1c/W3 QA and is not a failed or skipped test.

## 3. Product and expectation review

The W4a-only review uses all ten preflight copies after verifying their hashes against the brief; a HEAD diff alone would mix in W1c/W3. The [source audit](2026-09-27-standalone-mobile-home-w4a-qa/source-audit.json) records source/anchor and selector facts.

- `ui_fmt_identity` consumes counted raw bytes. It sanitizes each retained byte through the existing `ui_display_byte` before appending generated 0xBB. Capacity and zero-budget paths return empty/no-fit safely; a nonzero unnamed hash is full uppercase at ten or more cells, a fingerprint at six through nine, and never clipped below six. Names which happen to begin `0x` remain names.
- Both raw-name reads use the full 32-byte source before formatting. The six current device-name sites use TEAM 6, compose header 15, DELIVERED 19, REPLY 14, invite carrier/NEW MEMBER 14 and candidate row 6. The invite's absent name stays blank because its fingerprint has a separate column. Unknown identities retain the caller's `id <n>` spelling. Team tokens, own-ID status, Inbox numeric headers, geo and NEARBY remain in their existing namespace.
- The sole second projection is the allowed name-only **14 to 6** invite path. Its first five cells contain no generated marker; truncation replaces the discarded tail with a fresh marker. The native equivalence matrix exercises raw high bytes and lengths through 32. NEW MEMBER and the REPLY carrier copy the already formatted bytes unchanged. No label flows to console, JSON or companion output.
- Native cases sweep all 256 byte values, high-byte/UTF-8 examples, embedded NUL in counted synthetic input, exact/over-budget and 32-byte names, hash boundaries, no identity, tiny/zero capacities and canaries. The real renderer's P30 gives literal long-name and high-byte oracles at every affected site, alongside the existing unnamed cases. Expected bytes are not generated with the formatter under test.
- The Stage A to final probe diff fits the brief's closed expectation ledger: named abbreviation and unnamed hash spellings change; topology, selected identity, timing, whole-carrier checks, blank invite name and full-hash confirmation checks remain. P30 is additive. Existing absence checks are strengthened, not weakened.
- The W3 pager/geometry test that feeds `Wolfgangetta` directly to the row helper deliberately retains `>Wolfga T200 BBCCDD`: it exercises the row's independent bound with synthetic prepared input, not production identity projection. The independent T01/T09/I09 synthetic fixtures retain their original off-screen/bounding properties. The I07/I08/I09 anchor adaptations preserve meaning; no existing mutation is dropped. F08 and F09 are permitted additions beyond the seven required formatter entries.

No wire, NV, `lib/`, resident member, selection or action semantics change. Stack-local growth is explicit: 32 raw bytes per lookup, 32 in the invite publication loop, compose buffer +1, DELIVERED buffer +5 and candidate `name6[7]`; REPLY's 15-byte carrier is unchanged. These are source-local allocations, not a measured peak stack claim.

## 4. Controls, findings and dispositions

The full stock run is supplemented by [named-control accounting](2026-09-27-standalone-mobile-home-w4a-qa/control-audit.json) and [focused control replays](2026-09-27-standalone-mobile-home-w4a-qa/focused-controls.json).

For historical Stage A, QA copied all 1,548 tracked/untracked paths, then overlaid the ten verified preflight sources and the frozen Stage A probe. This includes W1c and W3; it is not a HEAD-only reconstruction. The focused driver reuses the stock build functions and substitutions, with one diagnostic row-hex print outside the repo and no oracle changes. These focused runs are diagnostic proofs; the untouched stock script supplies the full current gate.

| Control/proof | Independent observation |
| --- | --- |
| Historical Stage A live v3 | 964 checks pass |
| O8 Stage A | Both substitutions apply once; actual row `>0x00be T221 BEDEAD`; fails **only the two P23b/P23d blank-name checks** |
| O8 final | Actual row `>0x00B» T221 BEDEAD` (0xBB in the name field); fails **the same two blank-name checks**. Width 19 and separate fingerprint preserved |
| O6 historical | One failure, P23d request-screen full hash, confirming its old label was wrong |
| O6 retirement / O20 retained | O6 explicitly absent; O20 current stock run fails 5 checks, preserving the real confirmation-name-versus-hash property |
| B241a / B241b | 42 / 40 check failures, including poisoned short-name/rename assertions; compile and fail checks, no crash |
| W4a-S1/S2/S3/S4/S5/S6 | 17 / 5 / 1 / 1 / 4 / 2 check failures on their intended properties |
| C0 | Required build failure, still through the wrapper including the file under test |

**B441 CLOSED (software).** Sanitization, visible abbreviation and unnamed identities are verified at every current device-label site. This closure does not claim physical glyph legibility.

**B449 CLOSED.** O8 now reaches the invite row with the forbidden hash-derived name, proved before and after the product change. Its failure is correctly attributed to the blank-name rule.

**B455 CLOSED.** O6 is explicitly retired as approved, with O20 and the full-hash expectations retained. No silent GREEN or unusable control is accepted.

**B456 OPEN — firmware-UI can print PASS after skipping controls.** The script counts 236 call sites for label safety but never compares that count with completed control verdicts. A guard that fails before `ctl` can skip a control without incrementing `n_bad`. The coder's invalid first chain is hash-verified: seven undefined-`once` errors, **229/236 executed**, nevertheless PASS. QA independently reproduced the mechanism using the exact current verdict tail plus labelled synthetic dispatch: a false guard and an undefined guard each yield **235/236, exit 0, PASS**; an evidence-only completeness guard makes both fail and preserves the 236/236 success case. See [proof](2026-09-27-standalone-mobile-home-w4a-qa/control-completeness-proof.py) and [results](2026-09-27-standalone-mobile-home-w4a-qa/control-completeness-proof.json). This is a shell-plumbing proof, not a second full-probe run. No tool repair was made by QA. Close in a separately fenced instrument fix with missing-control regressions. Until then, independently reconcile defined labels and observed verdicts whenever relying on this probe's PASS.

B456 does not invalidate this W4a freeze: `once` is now defined before use, all **236 distinct controls actually ran in QA's stock run**, and the intended new/re-anchored failures were independently examined. Nothing from the coder's discarded chain is used for the PASS. The evidence driver/parser adjustments in QA affected only ignored analysis files, not frozen inputs or gate sources.

The same-width reformatting observation is **disposed as the documented input contract**, not a new production bug: sanitizing a previously generated 0xBB would turn it into a dot, so callers must not reformat at an equal/wider width. Only the proven 14-to-6 projection occurs; S5 exposes a wrong first width. The unrelated E14 stale anchor remains **B453 OPEN**, outside W4a's selector and unchanged.

## 5. Board evidence

[QA manifests](2026-09-27-standalone-mobile-home-w4a-qa/manifests/) and the [field comparison](2026-09-27-standalone-mobile-home-w4a-qa/board-comparison.json) show exact equality with coder `final-1` for all measurements, artifact hashes, toolchain, build identity, paths, schema, host and concurrency.

| Environment | RAM | Flash | Versus coder's verified before-state | QA payload SHA-256 |
| --- | ---: | ---: | --- | --- |
| gateway | 203740 B | 571936 B | RAM/flash/image unchanged | `97deca60b8f3ca94d790986f016a0cefb43a82cd2aeb99ace7db6ed830682886` |
| heltec_mobile | 211724 B | 1394840 B | RAM unchanged; flash **+252 B** | `820b8bd30655d698abbf78c9c4d752de62ff7b732c705fa4b5aec73b66fe8a11` |

Only the source inventory differs between coder final-1 and QA: 1,502 versus 1,548 paths, from the coder's subsequently added report/evidence. The executable inputs match. QA did not rebuild a pre-change board baseline; it verified the retained baseline/final manifests, repeatability receipts and archived ELF/symbol hashes, and independently rebuilt the final images.

The mobile delta is `.flash.text` +268 and `.flash.rodata` −16. QA's entire final symbol table matches the coder's. The recorded attribution covers the new formatter, adapter changes and six unchanged-source functions whose sizes move 1–4 bytes. The retained scratch object comparison shows those six differences already before linking; describing all of them as linker relaxation would be inaccurate. Link-time movements explain the remaining linked layout effects. No RAM increase or unexplained final measurement remains.

## 6. Landing, limits and next step

QA closes B441/B449/B455 in place, registers B456, updates register §0, design §13, tracker and MEMORY, and adds **UI-20 OWED** to the [metal plan](../../2026-09-20-metal-test-plan.md#ui-20). The authorized brief and all historical reports/inventories remain byte-identical. The final preservation record lists only those five documentation changes plus QA evidence.

No metal test, full board-UI gate, other unaffected firmware probe or owner commit was performed. The actual `6x10_tf` glyph appearance, cell spacing and legibility of `»` still need UI-20 on physical OLEDs. B418 remains W2's, B453 remains separate, and B447's named-peer precedence is outside W4a.

**Next: W4b (Home and navigation) QA pre-check, then the author's brief.** W4a/W3 prerequisites are satisfied; a commit is not a progress gate. This receipt authorizes no W4b code before that brief is reviewed.
