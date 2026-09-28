<!-- Author: Codex (independent QA); 2026-09-28; scoped review of Claude's W2 brief revision 3 -->
# W2 brief revision 3 — scoped QA re-review

**PASS.** Revision 3 is authorized for coder source-validation and implementation within its fence. W2R-1,
W2R-2 and W2R-3 are resolved. This approves the brief; it does not gate an implementation or close B418, B451 or B458.

Authorized brief: [W2 board-UI readers, revision 3](../plans/2026-09-28-standalone-mobile-home-w2-board-ui-readers.md),
458 lines, SHA-256 **`341d4bc0c401c8fd254107eea90915cefb2dee94580cdcaf912a99f015c89737`**.

Base: MeshRoute `8079e545fb2e1065c2bbc347eb9a66160f6cbf8e`; simulator
`6585649ea5a780f0542b2931853a667be56a5b2b`, clean. Both branches are `main`. No commit is needed for dispatch.

## Scope and input verification

This is the scoped return from the [revision-2 HOLD](2026-09-28-standalone-mobile-home-w2-brief-review.md), using
the [W2 pre-check](2026-09-28-standalone-mobile-home-w2-precheck.md). The accepted source census, four-file fence,
control ledger and focused gate remain in force.

- All 10 explicit SHA pins and all 19 unchanged dependency pins match. All four fenced line counts and all six
  brief links match. The pre-check checksum index verifies 26/26; the revision-2 review index verifies 8/8.
- Against the revision-2 review inventory, only the brief, design and register changed. The latter two match the
  refreshed pins. `tracker.md` and `MEMORY.md` are unchanged. Added paths are the earlier QA review artefacts.
- Reversing exactly the declared revision-3 edits reconstructs revision 2's 432-line text
  and its exact SHA-256 `0b8606ea274c7ab950f1cb4b2ad5bcc297e5a5d29ac654037ea99fdad62960c0`.
  The reconstruction and unified diff are retained, proving that there is no undeclared brief delta.

## Disposition

| Finding | Verdict | Verified correction |
| --- | --- | --- |
| W2R-1 — relocation can become insertion-only | PASS | §2.1 requires both anchors before any edit, including the exact guarded source block and its marker. §4.1 tests each missing anchor separately while live W49 remains GREEN. Positive relocation still requires one global arm, zero inside `dispatch()`, and placement-only failure. §4.3 stops either partial transformation. |
| W2R-2 — missing pin receipt line | PASS | §5 requires `PIN re-synced? YES — <derivation>` from the coder's own run: native 3031/198613 unchanged; model 239 − 1 = 238; wiring 59 + 1 = 60; controls 171 + 6 + 4 + 4 + 1 = 186. These are implementation expectations, not fresh gate results from this review. |
| W2R-3 — stale dispatch pointer | PASS | Register §0 and design §13 now name revision 3; both refreshed hashes match. |
| Comment-only proof precision | PASS | The preprocessing alternative compares tokens with `-P`, excluding temporary file names and line markers. The approved-comment-delta comparison remains available. |

The accepted ledger remains **60 wiring checks / 186 controls**. The fence and gate chain are unchanged apart
from the explicitly strengthened relocation proof. B459 remains a separate package immediately after W2 and
before W6; B460 remains outside W2. No owner ruling is required and no new bug finding was identified.

## Independent relocation feasibility check

A labelled synthetic experiment used the real `firmware_commands.cpp`, the stock W49 predicate with the current
dispatch signature, and actual GNU sed on temporary files outside both repositories. A whole-input substitution
spanning both anchors demonstrates that the revised requirement is implementable without production markers.

| Input | Live W49 | Output equals input | Arms: whole file / dispatch | Output W49 |
| --- | --- | --- | --- | --- |
| Both anchors present | GREEN | No | 1 / 0 | RED, placement only |
| Destination signature renamed | GREEN | Yes | 1 / 1 | GREEN |
| Source marker comment changed | GREEN | Yes | 1 / 1 | GREEN |

This is feasibility evidence, not an installed control or an implementation gate. The coder must implement and
prove the actual frozen control through the stock runner, including both missing-anchor cases and the independent
declared-versus-executed reconciliation while B459 remains open.

## Preservation and dispatch

All 1,739 MeshRoute input paths and 285 simulator input paths, including symlinks, are preserved. Both HEADs are
unchanged; the simulator remains clean and nothing is staged. This review adds only this report and its evidence
directory. No production, test, tool, brief, design, register, tracker or memory file was edited.

The verdict lives in this receipt. **Do not edit the brief's review-status header:** it would invalidate the
authorized hash. The coder inventories this receipt and its evidence as explained QA preparation additions under
brief §1. All pinned preparation inputs remain frozen through the implementation handoff (P4).

No native suite, corpus, board build, full probe or mutation gate was rerun here. Those results belong to the
pre-check or the future implementation gates, as explicitly distinguished above. Nothing was committed.

Evidence: [checksum index](2026-09-28-standalone-mobile-home-w2-brief-rereview/SHA256SUMS), including the entry
inventory, pin checks, byte-identical approved brief copy, reversible delta proof, relocation experiment and final
preservation check.
