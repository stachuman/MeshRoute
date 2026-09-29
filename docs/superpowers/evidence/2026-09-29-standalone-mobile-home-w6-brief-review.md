<!-- Author: Codex, independent QA; review only, no implementation -->
# W6 brief revision 1 — independent review

**Verdict: HOLD.** W6R-1 requires a fence/proof correction before dispatch. W6R-2 and W6R-3 are required text/fence fold-ins; re-review the changed sections only. **No authorized implementation hash is issued.**

Reviewed brief: [2026-09-29-standalone-mobile-home-w6-phrases.md](../plans/2026-09-29-standalone-mobile-home-w6-phrases.md), revision 1, **622 lines**, SHA-256 **`b89b7743f3843f5470620e483c7c93840ad6b478e3f9657e4a968b4aa46f066b`**.

MeshRoute base **`70ff486b40c9b07001b33bcbcdf640ad494c1149`**; simulator **`6585649ea5a780f0542b2931853a667be56a5b2b`**, clean. D14's four-record pages and D15's allocation are accepted owner rulings; neither is reopened. The feature scope, one-package recommendation and absence of a commit prerequisite stand.

## 1. Inputs and review method

- All **31 SHA-256 pins** and **25 line counts** in §1 matched at review entry.
- All **45 pre-check checksum entries** verified, including the pre-check receipt and whole-tree inventory.
- Relative to the pre-check inventory, exactly the four declared preparation documents changed: design, register, tracker and MEMORY. Production, tests and tools match that inventory. The pre-check evidence and this brief are explained additions.
- Both repository bases match; the simulator is clean. Nothing staged.
- Re-read the relevant source, D14/D15 and r2.23 changes, the record/verb/catalog/renderer/send seams, existing fake and probe builders, source-reading checks and process rules. This is V1/P7 review, not acceptance of the Author's source assertions.
- Ran small **disposable host measurements outside the repository**. The NV-fake query uses the unmodified real fake. The page-size query uses the pre-check's hash-verified T=163 writer overlay and a labelled synthetic grouping/metadata adapter; it is not W6 implementation or a router gate.
- Reconciled D15 against the original independent three-ABI measurement and exact measured scratch-header hashes; did **not** claim a fresh ABI or linked-RAM run.

Evidence: [inputs.json](2026-09-29-standalone-mobile-home-w6-brief-review/inputs.json), [measurements.json](2026-09-29-standalone-mobile-home-w6-brief-review/measurements.json), [allocation-review.json](2026-09-29-standalone-mobile-home-w6-brief-review/allocation-review.json). The receipt and folder are sealed by `SHA256SUMS`.

## 2. Required corrections

### W6R-1 — MAJOR: the real-router proof cannot honestly persist W6's blob within the current fence

**Brief anchors:** §2.8, lines 361–379; §3, lines 437–454; §4.1/§4.2 affected-probe gates.

**Source:** `tools/probe_inbox_verbs/fakes/Preferences.h:47,74–82,115–120`; `src/firmware_commands.cpp::DeviceUiPresetStore` at :179–181; `tools/probe_inbox_verbs/run.sh::INCS/build_variant` at :108/:331.

The required OLED-enabled router arm uses the real `DeviceUiPresetStore` and this Preferences fake. Each fake record has **2304 bytes** of storage, while W6's record is **2852 bytes**. Worse, this does not produce an honest write refusal: `MrProbeNv::put` silently declines the oversized record, but `Preferences::putBytes` reports the requested length anyway.

Executed against the unchanged fake with writable/open storage and **neither dishonest switch enabled**:

| Requested | Reported written | Key exists | Stored length | Exact bytes retained |
| ---: | ---: | --- | ---: | --- |
| 2304 | 2304 | yes | 2304 | yes |
| 2305 | 2305 | no | 0 | no |
| **2852** | **2852** | **no** | **0** | **no** |

Thus a success reply/live-catalog assertion can appear to pass without a durable record. A reload proof exposes it. The fake is not in §1 or the IN fence; §3 excludes every other tool file. Neither the emergency stub nor an alternate Print sink fixes the storage ceiling. A capacity override hidden inside a generated shadow fake would evade the declared fence rather than satisfy it.

**Required revision:**

1. Explicitly fence and pin `tools/probe_inbox_verbs/fakes/Preferences.h` for the measured W6 capacity and truthful retention result. Keep fake policy out of production and preserve the existing deliberate `retain_on_fail` / `drop_on_ok` controls. Specify the fake's capacity bound, not an arbitrary production allocation.
2. Add an exact persisted-blob/readback or catalog-reload proof after a successful **163-byte set through the real router**, plus capacity-boundary/over-capacity regressions. Retain failure/no-publish tests and the existing dishonest-fake controls. Do not substitute a captured success line for stored bytes.
3. Audit the real dependents. `tools/probe_deferred_actions/run.py:140–172` consumes the inbox runner's builder prefix and fake, and `probe_deferred_actions/probe.cpp:4` includes its `probe_main.cpp`. Keep the reusable builder contract, and add the affected deferred-action default probe to coder and QA gates when changing that shared fixture/builder. Its source need not change merely to rerun it; if a source-reader repair becomes necessary, name it in the revised fence.
4. Refresh the new-file pin/fence, disposition ledger and reader/gate list consistently. The shared Arduino/radio/console fakes can remain excluded; this correction concerns the actual NV medium used by the new proof.

Registered **B477** for the demonstrated existing tool defect, separately from B475's product reply size. No tool fix was made. This also corrects an omission in **my pre-check**: I called the OLED router arm feasible but did not check the fake's record ceiling.

### W6R-2 — MINOR: 1508 bytes is one reset-all fixture, not its maximum reply

**Brief anchors:** §2.2, line 194; §2.8 stage proof, lines 368–376. **Design:** r2.23 §7.7 and revision history repeat the figure. **Source:** `write_ui_presets_end` emits the decimal generation; `preset_verb(reset all)` renders after `PresetCatalog` has decided the resulting generation.

The pre-check reset the maximum-generation fixture, so generation wrapped from `4294967295` to **1**. Its measured 1508 bytes were correct for that run. A reset that leaves a ten-digit generation emits **1517 bytes**, still safely below 2048. I independently reproduced both through the real reset verb/writer in the same labelled T=163/default overlay:

| Before generation | Resulting generation | Records + end | Reply bytes |
| ---: | ---: | ---: | ---: |
| 4294967295 | 1 | 18 | 1508 |
| 999999999 | 1000000000 | 18 | **1517** |
| 4294967294 | 4294967295 | 18 | **1517** |

**Required revision:** qualify 1508 as the one-digit fixture, use **1517 as the maximum** under the pinned grammar/defaults, and make the clean-stage reset proof cover a ten-digit resulting generation as well as wrap. Include generation-restart/equality cases for the paged companion reader. Keep mutation replies and D14's semantics unchanged.

This is an erratum to my pre-check's insufficiently qualified reset-all figure, **not** a defect in D14. The sealed pre-check remains unchanged; this report records the correction. The Author should update the design's current bound when folding in the brief.

### W6R-3 — MINOR: the usage-only fence leaves comments that W6 itself makes false

**Brief anchors:** §1 command seam at line 70; §2.2 line 211; §3 line 437. **Source:** `src/firmware_commands.cpp:171–175,201–205`.

W6 makes these current binding comments false:

- “six reason spellings” becomes seven with `bad_page`;
- “FOUR-valued read” becomes five with `old_v1`;
- “three 372-B records” / “≈1.15 KB” becomes three 2852-B records / the measured catalog size.

The strict “only the usage text” fence prevents correcting them. A historical stack rationale can remain explicitly historical; it should not describe the enlarged catalog as a current 1.15-KB allocation or imply stock headless nRF owns this OLED instance.

**Required revision:** permit these narrowly named, **line-preserving comment edits** alongside the usage text. Keep executable command glue unchanged, the total line count at 1915, inventory at 197 rows, and all board-UI executable/marker anchors intact. Apply D6 to the final text; do not relax an instrument simply to accommodate prose. No owner behavior or allocation ruling is needed.

## 3. Accepted contract and evidence

### Paged output and stage proof

The named page size, derived five-page count, canonical `1`..`5` grammar, `bad_page`, field order, unchanged single-record mutations and unpaged reset-all reply are coherent with D14. The final page must contain **only slot 16 (`channel8`)**: spell its interval as `[4(n−1), min(4n,17))` to make the last-page bound explicit, and test that disabled slots remain present exactly once across all five pages.

Maximum-length page responses, including the specified `text_max`, final `page/pages`, newline and ten-digit generation:

| Page | Records | End bytes | Whole reply |
| ---: | ---: | ---: | ---: |
| 1 | 4 | 129 | 1083 |
| 2 | 4 | 129 | 1077 |
| 3 | 4 | 129 | 1092 |
| 4 | 4 | 129 | **1097** |
| 5 | 1 | 129 | 371 |

Each was staged through the actual 2048-B `GuardedConsole` and real `PresetPrintLines` adapter with a connected transport offering **zero available capacity**. All had zero drops and zero immediate wire bytes; after enabling drain, each arrived byte-identically. The page grouping and end-field insertion in this measurement are explicitly synthetic; the coder must implement and prove the real grammar/router path as §2.8 requires. A clean-stage guarantee is appropriately bounded and does not promise no loss under arbitrary pre-existing congestion. The 160-B-line and restored full-list controls are appropriate and must fail their intended output assertions.

### Record, projection and allocation

Accepted: same magic/version 2, old-v1 recognition only by exact size/header, backend-error/oversize precedence retained, no migration/boot writes, old-record repair even when restating defaults, existing absent/default coalescing, canonical v2 custom values preserved, 163/164 validation and eight D9 defaults.

Accepted: 17-column projection without added row length; append `0xBB` after the first sixteen bytes only for over-budget phrases. Neither `ComposeSlot` nor the snapshot grows with the stored text. The larger record and reply do not justify changes to wire, `/mrcfg`, partition size or lib/.

D15 reconciles exactly with the pre-check's measured shared-page shape:

- slot/blob **167/2852**, catalog **8584 host /8572 boards**, **+7440**;
- SendReq **16**; UiState **568 host /560 boards**; UiModel **1016/1000**;
- ComposeSlot/List **20/161**, UiSnapshot **1368**, UiChrome **20**, unchanged;
- model delta + frozen-state delta + one static 199-B line = **319 host /303 boards**.

The allocation remains a constraint on the implementation, not proof the new state machine works. The retained header/phase/flags/binding must cover the chosen notes and return states without hidden extra state. Per-ABI alignment remains as the pinned measurement (slot 1, blob/request 4, catalog host 8/board 4, state/model/snapshot 8, compose rows 1, chrome 2). Final object and linked-section attribution remain required.

### Review and send semantics

Accepted: capture before queueing, complete raw phrase and identity input, distinct review/pending bindings, shared body/page storage only while mutually exclusive, frozen render inputs, BACK selected, LOC as intent, word wrap preserving every byte, Inbox's original two-row byte pager, no send on page advance, blank/wake cadence and safe action reset, emergency precedence and saved-phrase pending-request behavior, generation/team/known-peer re-gating before execution. No W8 draft logic or new kinds belong here.

The separate known bit avoids treating the value field itself as an optional marker. A reachability qualification for its tests: today's `Node::team_key_set` rejects hash zero (`lib/core/node_routing.cpp:861`), and today's `ui_fmt_identity` returns `none` for an unnamed zero hash (`firmware_ui_model.h:1509`). A synthetic known-zero gate case is useful, but do not claim the current real resolver produces it or change core to manufacture it. The author can state the desired synthetic/header edge explicitly without reopening D15.

The static buffer owner and pure-operation parameter approach are appropriate. The “188/191 command bytes” statement describes a **maximum 163-byte emergency with a three-digit channel** (the pre-check fixture); actual board channel/default phrase lengths can be shorter. Qualify that wording rather than treating those lengths as every alarm's bytes.

### Fence, readers, ledger and gates

Apart from W6R-1/W6R-3, the seven production files and six native test files cover the inspected paths, including the chrome test's actual direct-double send. The ABI tools/tests, mutation harness, UI/router/sink probes, companion and command manual are correctly named. No new production header/TU or test file is necessary on the verified evidence.

The nine selector-(a) batteries total **383**; the four dependency batteries bring the specified baseline union to **495**, before new mutations. The current pre-check anchor census is hash-preserved. Require the coder's final live census and RED run; do not inherit old totals across re-anchoring. The evidence ledger must classify each actual changed assertion/control, not replace individual dispositions with the broad category “direct-double fixtures changed.”

P6's separate coder and QA chains, fresh native binary, anchored corpus comparison, stock ABI, default board-UI with 592 identities, warning-census ownership, paired board repeatability, final attribution, PIN line, input stability and evidence retention are sound. W6R-1 adds the relevant fixture-dependent gate. Clarify the small arrow-order typo in §2.8: the routed order is **`exec_console_line → dispatch → handle_ui → PresetPrintLines`**, not dispatch calling exec_console_line. This does not change the required real-TU proof.

The “W1c through W4b unchanged” wording is read together with the explicit W6 ledger: preserve their independent properties; do not demand that old direct-send/default-count assertions remain byte-identical while intentionally changing those contracts.

## 4. Findings, scope and handoff

- **B477 registered**, open: the existing Preferences fake reports successful oversized writes without retaining data. Next free finding **B478**. No implementation fix or closure claim.
- W6R-1–W6R-3 are brief-review findings in this report. B475/B335 remain open; B476 and D14/D15 are unchanged.
- The register pin matched at entry. Under **M1**, QA then added only B477 and advanced the next-number marker. This is the sole reviewed-input change made by QA, recorded in `register-review-change.json`; the Author must refresh the register pin in revision 2. No dispatch/status rewrite was made.
- The brief, design, tracker, MEMORY, all existing evidence, production, tests, tools and simulator remain unchanged. Nothing staged or committed.
- No native, corpus, full probe, mutation union, warning census, board gate or physical test was rerun during this **brief review**. Those pre-check results remain verified historical inputs. The executed new work was the small fake and response-bound measurements described above, plus hash/source checks.

**Return to Author:** revise the fake fence and persistence/reader proof; correct the reset reply bound; permit the named comment edits; include the small wording clarifications above. Reissue with updated pins/hash for a scoped re-review. The present hash is **not authorized for coding**.
