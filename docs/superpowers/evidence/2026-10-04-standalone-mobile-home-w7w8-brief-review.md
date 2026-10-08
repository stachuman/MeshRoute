<!-- Author: Codex (independent QA brief review); Claude authors the brief; a separate coder implements; the owner rules and commits. -->
# W7+W8 brief revision 1 — independent review

2026-10-04. **HOLD — W7W8R-1 through W7W8R-3 below require corrections before dispatch.** This is a brief review, not an implementation gate. No authorized implementation hash is issued.

Reviewed brief: [2026-10-04-standalone-mobile-home-w7w8-editor-rename-messages.md](../plans/2026-10-04-standalone-mobile-home-w7w8-editor-rename-messages.md), revision 1, **680 lines**, SHA-256 **`6890e3cf596071f0012e92caf9541db10f7e9980023218df361ae49b645aafbd`**. The brief, its status header and all pinned inputs remain untouched. Only this receipt and its evidence were added. No staging, commit, reset, clean or removal.

## 1. Inputs and preservation

MeshRoute HEAD independently matches **`4c1a000bc71706ac438e64161a9452966a66615f`**; nothing is staged. Simulator HEAD matches **`6585649ea5a780f0542b2931853a667be56a5b2b`**, with a clean status.

The W8 inventory reconciles: **2,495 → 2,527 MeshRoute paths**, no missing input, and exactly four changed existing files: design, register, tracker and MEMORY. All 32 additions are the W8 receipt and its sealed evidence directory, plus this proposed brief. All **285 simulator paths** match. The inherited bounded-F07 candidate is preserved. See [preflight.json](2026-10-04-standalone-mobile-home-w7w8-brief-review/preflight.json).

All **14 existing fenced-file hashes** match; both new paths are absent. Named dependencies and the four preparation-document hashes match their named paths, not merely some file with the same digest. The W7, W8 and inherited tool-package re-gate seals verify **26/26, 30/30 and 40/40**. All 48 full hashes stated in the brief are present and accounted for. See [dependency-pins.json](2026-10-04-standalone-mobile-home-w7w8-brief-review/dependency-pins.json).

## 2. Required corrections

### W7W8R-1 — MAJOR: define a fitting row for editor notes

**Anchor:** brief §2.1, lines 202–203; design §5.3, lines 374–401.

The header keeps used/cap right-aligned and says a note replaces the caller part. That does not specify a possible 19-cell layout for the longest notes. `RECIPIENT CHANGED` is **17 cells**: together with `1/163` it needs at least **22 cells even with no separator**, or 23 with one. With `163/163` it needs at least 24, or 25 with one. `TEAM CHANGED` plus a separating space and `163/163` needs 20.

The new formatter therefore cannot simultaneously preserve the full note, preserve that counter, and obey the stated row width. Silent truncation or overlapping text would be a product choice absent from the contract. These are reachable message-editor notes; this finding does not assert that recipient notes occur in the name editor.

**Required:** the author specifies the exact note-row rule in the brief and corresponding design passage. Recommended: while a note is shown, it owns row 0 and the counter is temporarily hidden; the next eligible press restores the normal header and still performs its normal action. Another fitting layout is the author's choice. Require exact real-renderer rows for all five notes, including a full-length draft, and retain the wake/overlay press rules. No owner ruling or allocation change is requested.

Evidence: [row-width-measurement.json](2026-10-04-standalone-mobile-home-w7w8-brief-review/row-width-measurement.json), labelled cell arithmetic, not an implemented render.

### W7W8R-2 — MAJOR: separate D19's execution refusal from phrase-review admission

**Anchor:** brief §2.4, lines 332–345, and §2.8's D19 proof; current `ui_review_capture`, `send_gate_of` and `review_note_of` in `src/firmware_ui_send.h:518,674,683–691`.

D19 rules an **execution-time** refusal with `NOT SENT` / `NO TEAM ID YET`, also applying to saved team phrases. The existing phrase-review capture already calls the same `send_gate_of` before opening the review, then translates its answer through `review_note_of`. This is real runtime wiring: `ui_service_review` in `firmware_ui.cpp:602–609` supplies the live answers and `mr_ui_tick` calls it before freezing the frame.

Simply adding the no-ID arm to the shared gate would make a pre-ID phrase fail during review capture. It would not reach SEND or the required result. The brief does not state how that existing consumer treats the new execution-only arm; preserving all old review behaviour while adding that arm needs an explicit phase contract. This is a gap in the brief, not a claim that an implementation already made that mistake.

**Required:** state that a valid saved team phrase can open its ordinary review before the local ID exists, and that the new no-ID check belongs to execution. Keep the current catalog/team/recipient review validation; do not invent a positive live ID or weaken those checks. Name the affected gate consumers and specify how they share the existing validation without applying the execution-only refusal at review.

Require native and real firmware-UI traces for both ordinary team kinds: pre-ID selection/edit → review → explicit SEND → `NOT SENT` / `NO TEAM ID YET`, zero executor/core submissions, and each caller's prescribed acknowledgement return. Also check ID loss between review and drain, success with an ID, and the emergency exemption. This implements D19 as ruled; it does not revisit B444's non-UI disposition.

Evidence: [source-review.json](2026-10-04-standalone-mobile-home-w7w8-brief-review/source-review.json), including the existing shared-gate consumers.

### W7W8R-3 — MAJOR: make the final native and board-repeatability gate explicit

**Anchor:** brief §4.1, lines 533–567, and §4.2, lines 591–597.

The baseline explicitly builds native, but the final chain's native step names only the direct binary. A binary from an internal checkpoint does not establish the frozen source's result. Per D1, final validation must explicitly build the frozen candidate and then run the binary.

Board runs are named, but the baseline does not require repeatability comparisons, and the final recipe says each “pair” is repeatable without naming the two same-environment manifests to compare. A `pair` invocation builds two different environments; it does not compare repeat builds. `measure_board.py::compare_command` compares two manifest files for the same environment. Its qualification includes the whole source snapshot and normal `.pio/` metadata, alongside payload, size, symbol and toolchain fields.

**Required:**

- Final native: `pio test -e native`, followed by `./.pio/build/native/program`, against the frozen tree; derive its counts and require zero failures/skips. QA independently rebuilds too.
- After baseline runs, require stock `compare` for `base-1/gateway/manifest.json` against `base-2/gateway/manifest.json`, and the analogous mobile pair. After final runs, require both comparisons for `final-1` against `final-2`: **four comparison PASS results**.
- Within each two-run sequence, no checkout/evidence additions or edits, native/probe builds, or normal `.pio/` changes. Store measurement outputs below the already specified ignored `.pio-measure/w7w8/`; copy the eight per-environment manifests into durable evidence after the runs.
- Retain the existing rule that base-to-final attribution is read from manifests/sections/objects/symbols; it is not a same-source `compare`. QA's own final manifests must reproduce the coder's qualification/measurement fields, with any explained provenance difference identified.

These repairs change the gate recipe, not the product contract, allocation or chosen environments. The tool is read-only in this package; no tool edit is requested.

## 3. Accepted portions and independent measurements

**Allocation passes review.** A fresh struct-only measurement compiled scratch headers with the native, Xtensa and ARM configurations through the stock ABI measurement API. It adds exactly the proposed draft, descriptor, name origin, both carrier IDs and an explicit four-byte outcome record; a live-ID boolean occupies `SendLive` padding. Nothing was inserted into production. This is object pricing, not linked RAM or an implementation proof.

| Type | Native before → proposal | Mobile before → proposal | Gateway ABI before → proposal |
| --- | ---: | ---: | ---: |
| UiState | 568 → 576 | 560 → 568 | 560 → 568 |
| UiModel | 1016 → 1216 | 1000 → 1200 | 1000 → 1200 |
| SendReq | 16 → 20 | 16 → 20 | 16 → 20 |
| UiSnapshot | 1368 → 1368 | 1368 → 1368 | 1368 → 1368 |
| UiChrome / SendLive / SendTracker | 20 / 12 / 16 unchanged | 20 / 12 / 16 unchanged | 20 / 12 / 16 unchanged |

Draft **176/4**, descriptor **10/1**, NameOrigin **1/1**, outcome **4/1**. Counting UiModel and the separately frozen UiState gives **+208 B on each ABI**, exactly D16+D18. Gateway type pricing is not a claim that the headless image owns those OLED objects. See [layout-measure.json](2026-10-04-standalone-mobile-home-w7w8-brief-review/layout-measure.json) and its scratch measurement script.

Other accepted portions:

- The single counted draft, visible-page union, WAS/header reuse and separate request/outcome ownership fit the measured shape; no new persistent carrier or complete-draft copy is authorized. §4.3's “text copy” prohibition is read together with §2.2 as forbidding another payload copy, not the explicitly required visible-window projection.
- The pure editor header avoids the model/config include cycles. W0's counted `rename_node` and five typed answers exist as pinned. The prompt's typed origin, both gate asks and the console-race/WAS capture rule match the reviewed design.
- Written binding begins at WRITE, with no later silent rebinding. Kind-scoped catalog/draft validation, no `-l`, raw counted composition and the shared static 199-byte line agree with source and the pre-checks. The quoted worst-line arithmetic fits that line.
- The failure table preserves attribution before classification and treats possible-air failures conservatively. Accepted-open `ctr == 0`, nonterminal aired evidence, late ACK, the existing channel matcher limitation and emergency attempt ordering are covered. The explicit outcome record must remain independent of emergency-overwritten presentation fields; the content lock lasts through execution, while result ownership lasts through acknowledgement/pre-emption, per design §5.6.
- The family audit must include device glue as well as the named pure tracker methods: `mr_ui_tick` currently has an exact-DM test when deciding whether an alarm abandons ordinary channel tracking. The brief's every-`SendKind` reader audit covers that consumer.
- The frozen-display, raw-input, request-drain and scripted-rename proofs belong to the real firmware-UI probe; the scripted service is not claimed to prove actual persistence. W0's real identity arms remain the dependency witness.
- AST-only independent census reproduces selector (a) **375**, selector (b) **225**, union **15/600**. New entries must cover the named rule families; derive the resulting counts. Existing bounded F07 remains unchanged. See [mutation-selection.json](2026-10-04-standalone-mobile-home-w7w8-brief-review/mutation-selection.json).
- The product/tool fence, B480 direct-action mutation, B481 honest retitle, board-UI accounting additions, caller-sensitive results, P6 gate split and metal-plan landing are accepted subject to the three corrections. No new owner decision or extra retained field was found necessary.

## 4. Limits, disposition and next step

This review did **not** rerun native tests, the corpus, full probes, mutation batteries, tools discovery, warning census, board firmware measurements or metal. Their prior receipts are source/baseline authorities, not results of this review. Only the struct-only ABI experiment and read-only inventories/censuses/calculations were run here.

No B-number was allocated: these are brief corrections in this receipt, not new production or instrument defects. The pinned register and its next-free **B498** were preserved. D16–D19 remain settled; the implementation is not authorized by this HOLD.

Author folds W7W8R-1–W7W8R-3, updates only the affected preparation pins as needed, and requests scoped re-review of that delta. All other portions above are accepted. The revised brief must receive a new authorized SHA-256 before coder preflight.

Evidence: [SHA256SUMS](2026-10-04-standalone-mobile-home-w7w8-brief-review/SHA256SUMS); [preservation.json](2026-10-04-standalone-mobile-home-w7w8-brief-review/preservation.json).
