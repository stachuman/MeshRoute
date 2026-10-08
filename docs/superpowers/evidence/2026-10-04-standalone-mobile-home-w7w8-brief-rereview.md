<!-- Author: Codex (independent QA scoped brief review); Claude authors; a separate coder implements; the owner rules and commits. -->
# W7+W8 brief revision 2 — scoped re-review

2026-10-04. **PASS — W7W8R-1, W7W8R-2 and W7W8R-3 are closed.** The accepted portions of the [revision-1 review](2026-10-04-standalone-mobile-home-w7w8-brief-review.md) stand. No implementation was performed or gated in this review.

**Authorized brief:** [2026-10-04-standalone-mobile-home-w7w8-editor-rename-messages.md](../plans/2026-10-04-standalone-mobile-home-w7w8-editor-rename-messages.md), **revision 2, 732 lines**, SHA-256:

`47cb5a461d07cc3cbaf0ab6ae81fe535e206eda996201885f4dbe878dd11cb8e`

The coder may source-validate and implement against exactly that hash and §1's commit-plus-inventory base. The brief's status header stays untouched. P4 freezes the brief and pinned preparation set from coder preflight PASS through the implementation freeze. A later owner commit is reconciled by the brief's content rule; no commit is required to proceed.

## 1. Pin and scope checks

MeshRoute independently remains on `main`, HEAD **`4c1a000bc71706ac438e64161a9452966a66615f`**, nothing staged. Simulator HEAD **`6585649ea5a780f0542b2931853a667be56a5b2b`**, clean; all **285 inventoried simulator inputs** are unchanged.

The entry inventory contains **2,538 MeshRoute paths**. Compared with the first review's entry, exactly five existing paths changed: the brief and the declared four preparation documents. The eleven additions are exactly the first review receipt and its sealed evidence directory. No input is missing. All source, tests, tools and other inherited files match that entry, including the bounded F07 candidate. Compared with the W8 inventory, only the four declared preparation documents differ among existing paths; all subsequent evidence/brief additions are explained.

All **44 named pin records** pass: 14 existing fenced files and their line counts, the two absent new files, 24 read-only dependencies, and the four preparation documents. The four seals verify **26/26, 30/30, 10/10 and 40/40**. The full preparation hashes in §1 are the authority; they match disk. The user's abbreviated MEMORY suffix differs from the actual full pin, which ends `740ad`; there is no full-pin mismatch.

Evidence: [preflight.json](2026-10-04-standalone-mobile-home-w7w8-brief-rereview/preflight.json), [inputs.json](2026-10-04-standalone-mobile-home-w7w8-brief-rereview/inputs.json). An exact copy of the reviewed revision is retained as [reviewed-brief.md](2026-10-04-standalone-mobile-home-w7w8-brief-rereview/reviewed-brief.md).

## 2. Disposition of the three corrections

| Finding | Verdict | Verified correction |
| --- | --- | --- |
| W7W8R-1 | **CLOSED** | Brief §2.1 gives a note row 0 alone, left-aligned, with the counter hidden. All five note strings fit the 19-cell row; the longest is 17 cells. Clearing is still an eligible lit, non-overlay press which also acts; the wake press remains consumed. §2.8 requires exact real-renderer note rows including full-draft coverage. Design r2.27 §5.3 carries the same operative rule. |
| W7W8R-2 | **CLOSED** | Brief §2.4 names review and execution as separate phases. Phrase `ui_review_capture`/`ui_service_review`/`review_note_of` and written DONE admission never apply D19. Execution applies the existing shared validation, then the no-ID check for both ordinary team kinds. Neither duplicated validation nor a fictitious positive ID is allowed. Native and real firmware-UI traces cover both pre-ID reviews, explicit SEND refusal, zero submissions, caller-specific returns, ID loss before drain, success and emergency exemption. Design §7.4 agrees. |
| W7W8R-3 | **CLOSED** | Final native explicitly rebuilds the frozen source with `pio test -e native`, then runs the binary with zero failures/skips. Baseline and final each require two same-environment stock manifest comparisons, four PASS results in total. The two-run no-edit/no-evidence-write/no-build/no-normal-`.pio/`-change rules and eight durable manifests are explicit. Base-to-final attribution remains separate from same-source comparison. QA rebuilds and reconciles its own measurement/qualification fields. |

The phase repair is supported by current source: `ui_review_capture` still calls `send_gate_of` at `firmware_ui_send.h:688`, translating its answer through `review_note_of`; `ui_service_review` still supplies it before the frame freezes. The stock `measure_board.py::compare_command` still takes two per-environment manifest files and applies `compare_qualification`. Its source and normal-`.pio/` snapshot fields remain part of repeatability. The corrected instructions fit those existing interfaces; they do not require an additional fence or retained field.

The allocation, five product-file fence, new pure editor header, failure/outcome contract, B480/B481 fold-ins, P6 gate split and selected mutation union are unchanged in this scoped revision. D16–D19 are not reopened. The previous struct-only **+208 B per ABI** measurement remains applicable because its inputs and the priced shape are unchanged; no new layout experiment was needed.

**Minor prose erratum, nonblocking:** the new explanatory sentence “no note fits beside `163/163`” is too broad: short notes such as FULL, EMPTY and BUSY would fit. The longest notes motivate the rule; the rule deliberately hides the counter for every note. This does not change or obscure the specified layout, so no pre-dispatch edit is required. Correct that rationale at a future authorized documentation refresh; do not alter this authorized hash in place.

## 3. Preservation and limits

Only this re-review receipt and its evidence directory were added. The brief, preparation documents, earlier receipts/seals, inherited implementation and simulator are unchanged across the review. Nothing was staged, committed, reset, cleaned or removed. [Preservation proof](2026-10-04-standalone-mobile-home-w7w8-brief-rereview/preservation.json).

This is the scoped brief PASS. Native, corpus, board firmware measurements, probes, mutation batteries, tools discovery, warning census and metal were **not rerun** here. The coder must run the authorized §4.1 chain and freeze; QA then independently gates the implementation under §4.2. No bug register or status landing was made during this review, and next free remains B498.

Evidence seal: [SHA256SUMS](2026-10-04-standalone-mobile-home-w7w8-brief-rereview/SHA256SUMS).
