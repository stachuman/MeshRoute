# Revision-5 focused source-validation — STOP-1

Base: 7442e6f570abdcd74ceed20d4c0cb9e2855d0719. No production edits or full gate.

`state.json` binds the consumed brief. `permitted-preparation.json` inventories every one of the 59
permitted dirty documentation files individually; `inputs-before.json` inventories 1,571 checkout inputs.
The existing dirty documents are preserved, not rewritten by this preflight.

`checked-anchors.json`: 23 of 24 focused anchors match. Revision 5 §3.2 names remote_codec.cpp:654,
but the unchanged decoder detail assignment is at :652. This is an anchor error, not a decoder defect.
The complete source-validation remains unfinished at STOP-1; these 24 checks are not a claim to have
validated every referenced design section or every anchor in the brief.

`policy-check.json`: unchanged policy SHA, 180 entries, 48 disruptive rows; all 48 historical dispositions
match source (12 selected, 36 retained refusals). This is static coverage, not executed behavior proof.

`action-plan-definitions.json` identifies the sole ActionPlan definition. The dependency compile fixtures
are intentionally tiny language probes, not proposed production edits or native/board gates:

- core_baseline: existing RemoteSessionState 8824 and TranscriptHeader 24 compile unchanged.
- core_only: a forward declaration cannot support an embedded ActionPlan; expected incomplete-type error.
- firmware_complete: the actual P1 definition compiles and measures 2 bytes. Its compiler dependency file
  includes src/firmware_action_effects.h and src/firmware_config_parse.h.

That dependency makes the requested embedded carrier's permitted implementation route unclear: the brief
allows a pure shared carrier but also says core must not include firmware and P1 must stay unchanged.
Explicitly permitting a source-only extraction of ActionKind/ActionBackend/ActionPlan to a pure shared
header, with the original firmware header including it and all qualified names/values/layout/API/behavior
unchanged, would resolve the fence ambiguity without changing R-RA-40. This is a proposal for QA's fold-in,
not permission inferred or a production change made here. A forward-declaration/pointer workaround does
not provide the required complete owned value; duplicating the definition is not proposed.

The expected failing fixture is NOT a production build failure, effective mutation RED, full ABI result,
new allocation measurement, independent QA result, or proof that the existing firmware is defective.
No baseline figures from the earlier P1 report are represented as rerun here.

Run reproduce.py to recreate the focused read-only check in a new /tmp directory (it expects the pinned
preparation set and must be adapted after an author reissue). Every attempted command and compiler output
is retained. See the appended checkpoint in ../2026-09-13-radmin-slice7b3.md.
