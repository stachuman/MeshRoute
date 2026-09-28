<!-- Author: Codex, independent Quality Agent -->
# Standalone Home W2 — revision-2 brief review

2026-09-28. **HOLD — one required correction to the W49 relocation guidance and its gate; two minor textual fold-ins.** No owner ruling is requested. Review only the changed passages and refreshed pins on return. The four-file fence, the closed 60-check/186-control ledger, the focused gate, and the owner's separate B459 dispatch are otherwise accepted.

Reviewed [brief revision 2](../plans/2026-09-28-standalone-mobile-home-w2-board-ui-readers.md), **432 lines**, SHA-256:

`0b8606ea274c7ab950f1cb4b2ad5bcc297e5a5d29ac654037ea99fdad62960c0`

This hash identifies the reviewed draft; **it is not authorized for coder dispatch**. Revision 1 was superseded before this review. Its separately hashed contents were not available as an identified retained input, so the claimed exact revision-1 delta was not reconstructed; this review covers revision 2 in full.

## W2R-1 — MAJOR: make both halves of the relocation fail closed

**Anchors:** brief §2.1, lines 130–134; §4.1 step 4, lines 311–312; §4.3 control STOP, lines 350–351. Real source: `firmware_commands.cpp::handle_teststatus` (:1534), `dispatch` (:1548), guarded `ui` block (:1635–1637). Instrument: `probe_board_ui/run.sh::wchk_in` (:261) and `w49` (:1260).

The requirement is sound: if either relocation anchor is absent, the output must equal the input. The suggested recipe does not establish that requirement. Inserting at the earlier destination and deleting the later original only when an insertion flag is set protects against **missing destination → deletion**, but not **missing source → insertion**. The prescribed scratch test exercises only the first direction.

Fresh labelled synthetic measurement: execute a sed insertion-flag recipe of the described shape against the real source, then evaluate the stock W49 predicate with only its extraction signature corrected. No repository production/tool file is changed. This is an illustration of the brief's suggested algorithm, not a claim that a W2 implementation already exists.

| Fixture | W49 before mutation | Output identical? | Arm count before → after | W49 after mutation |
| --- | --- | --- | --- | --- |
| Both anchors present | GREEN | No | 1 → 1 | RED, placement only |
| Destination signature renamed | GREEN | **Yes** | 1 → 1 | GREEN; mutation is correctly a no-op |
| Source block's marker comment changed; executable guard and handler unchanged | **GREEN** | **No** | **1 → 2** | RED on file-wide uniqueness |

The third fixture changes only the marker comment after `#if MR_FEAT_OLED`. W49 strips that comment and continues to accept the real wiring. The defective control inserts a second arm and cannot delete the differently anchored original. `wchk_in` sees changed bytes and RED and would credit the wrong fault. The existing real-input relocation proof and the one prescribed missing-destination test do not distinguish this algorithm from a correct one.

**Required correction:**

1. Remove or correct the insertion-flag-only recipe. Require the mutation to establish both unique anchors before emitting any changed output, or use an equivalent whole-input buffered transformation. This is achievable inside the existing runner fence; no production marker, `wchk_in` change or B459 logic is required.
2. Extend the scratch gate to exercise **each missing anchor independently**, preserving the live W49 predicate where possible: destination signature renamed, and the exact source anchor altered by a comment-only change. In both cases require byte-identical output. Keep the existing positive relocation proof: one arm file-wide, none inside dispatch, only placement fails.
3. Align the STOP line with the already intended two-sided contract: a relocation that becomes either insertion-only or deletion-only is rejected. The general exactly-once anchor rule remains; this request does not authorize new product behavior or a wider fence.

Evidence: [reusable synthetic proof](2026-09-28-standalone-mobile-home-w2-brief-review/relocation-recipe-proof.py) and [literal sed recipe, stock predicate, results](2026-09-28-standalone-mobile-home-w2-brief-review/relocation-recipe-proof.json). The first scratch invocation had a sed delimiter-escaping error and produced no result; the corrected script was run completely for the three results above. No failed setup attempt is credited as a control.

## Minor fold-ins

**W2R-2 — required receipt line omitted.** Brief §5's report shape (lines 376–393) omits the `PIN re-synced? YES — <derivation>` line required by [agent roles](../../2026-09-02-agent-roles.md) (line 57). Add it without changing the native PIN. An appropriate derivation is: native **3031/198613 unchanged**, model **239 − 1 retired M103 = 238**, wiring **59 + 1 = 60**, wiring controls **171 + 6 + 4 + 4 + 1 = 186**. The coder must verify these figures in its actual run.

**W2R-3 — dispatch revision pointer.** The register's §0 W2 paragraph (:17) still says **“brief revision 1 awaits QA review”**, while the design §13 correctly points to revision 2. Update that one status phrase to the next issued revision/current review state before re-hashing the preparation set. Keep the settled B459 ruling unchanged. No register edit was made during this review.

These are text corrections; they do not reopen the package scope, B458 disposition, B459 sequencing or B460 exclusion.

## Independent verification and accepted decisions

**Base/preflight.** MeshRoute is on `main` at `8079e545fb2e1065c2bbc347eb9a66160f6cbf8e`; simulator is on `main`, clean at `6585649ea5a780f0542b2931853a667be56a5b2b`. The current MeshRoute tree contains the four declared preparation edits, the earlier pre-check/evidence and the new brief. No product/tool changes are present. Against the pre-check: all 1699 file-content hashes were checked; only those four preparation documents differ, all four symlink targets agree, and all **285 simulator inputs** remain unchanged. The review entry inventory covers **1730 MeshRoute paths**, including links and the preparation additions.

**Pins.** All **10 explicit SHA-256 pins** in the brief resolve and match their named files; all **19 remaining read-only dependency pins** match; all **four fenced file line counts** match; all **26 pre-check checksum entries** verify. All five Markdown links resolve. The four declared preparation changes are exactly `MEMORY.md`, tracker, design and register. Current input preservation is recorded separately so none of these pinned files is changed by this review.

**Fence/P7.** The added fourth path, `tools/probe_inbox_verbs/transcript_main.cpp`, is accepted for the single S24 → S27 comment correction. Source census finds its executable consumer in `transcript.py`'s compile command (:359); no predicate or mutation matches that fidelity-note text. The other `transcript_main.cpp` references are descriptive. This is the same comment-only proof class as B451, without a new executable dependency. Use the allowed exact approved-comment-delta comparison; if choosing the preprocessing alternative, compare tokens rather than filename/line directives from two temporary paths. No change to a shared numeric overload is authorized.

**Source facts and ledger.** The fenced source hashes remain identical to the independently measured pre-check. The current dispatch prefix, the single guarded UI arm, one seam call and one streamed flush, five command-file OLED tokens, and the separate three-line help guard all exist as named. W49 keeps its offsets/guard and placement clauses. W51's exact occurrence counts avoid the flattened-function `grep -c` trap. Its local flush control complements the controlled S23/S24 delegation. W54-help preserves a literal while removing its guard, so its intended fault remains distinguishable from deleting the help row. The original 14 dormant control dispositions and the additive flush control account for the predicted **60 checks / 186 controls**; other stated counts are unchanged. These are checked contract derivations, not new implementation results.

**B451/B460.** The source matches the fidelity note's limited claims: the shared fake ignores bases; inbox X21–X26 on ACCEPT and CLIENT compare decimal fake-rendered `whoami` hashes. The panel formatters do not use those numeric `Print` overloads. Console-sink S27 names `cr.dst_hash` and `cr.layer_path`; X20 removes the former's `HEX`. It does not pin `handle_whoami`'s radix. B460 is therefore correctly excluded and must not be disguised as an existing witness. This review adds no new executed B460 claim; its registered observation remains a static census.

**B458.** The active M103 tuple remains unique. Its authorized retirement, old effect, live witnesses and unchanged historical worker-speed comments are correctly specified. No other tuple, target, worker logic or native PIN needs to move. The earlier **238 RED** scratch feasibility result remains pre-check evidence, not a substitute for the W2 coder/QA run.

**Gate and exclusions.** The focused chain is accepted on both sides: preservation/comment and AST proofs, full default board-UI, independent completeness reconciliation, clause-specific controls, full console-sink, full model battery, tools discovery, command-inventory check and final reader audit. These instruments cover the changed tools. Product/source equality justifies not rebuilding corpus/boards/ABI/census for this fence; the battery still rebuilds native in its workers. The no-rerun decision for firmware-UI/inbox is conditional on comment-only equality and unchanged readers. Independent accounting is correctly evidence work while B459 remains outside the runner fence. Any mid-chain input change restarts the chain.

**Owner ruling.** B459 remains its own tool package, immediately after W2 and before W6. B460 stays out. No unresolved owner decision was found. The proposed QA landing closes only B418/B451/B458 after implementation PASS and leaves metal untouched.

## What ran and what did not

Ran: repository/inventory/hash/line-count/link checks; pre-check checksum verification; current source/readers census; the three-case synthetic relocation example through actual sed and the extracted stock W49 predicate; whitespace and final preservation checks.

Not run: native, model battery, full board-UI, console-sink, other probes, tools discovery, corpus, board builds, ABI, census or metal. This is a **brief review**. The previous pre-check measurements are cited at their original scope, and the instruments listed in §4 still must run after implementation.

No new production/tool finding was allocated: W2R-1 is a defect in this proposed brief's guidance/gate, W2R-2/W2R-3 are textual corrections. Existing B418/B451/B458–B460 remain unchanged. No files under review were edited; only this QA report and its checksummed evidence directory were added. Nothing staged or committed. The simulator is unchanged.
