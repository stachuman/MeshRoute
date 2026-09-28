<!-- Author: Codex, independent Quality Agent; P4 checkpoint review, not implementation QA -->
# W4b revision 3 — checkpoint re-pin, 2026-09-27

**PASS — resume the preserved candidate from STOP-1.** The settled transition and its proof obligations are consistent with the source and the stated revision-2 → revision-3 delta. No further brief edit or owner ruling is required for this re-pin.

**New authorized brief:** [revision 3](../plans/2026-09-27-standalone-mobile-home-w4b-home-navigation.md), SHA-256:

`a89c075e6dab5184d68c06a31b9fd262c3bab5656bc140146a879e2608d8ca5a`

**Previous authorized brief:** `a20e2dbf70ddf10aaeb67590ed7de1d816ec1e64ed520b3d69ac1713709b4f64`.

**Implementation contract affected: YES.** This is a bounded behavioral correction and additional proof, not a documentation-only refresh. It does not authorize additional retained state, another production file, a different gate, or unrelated cleanup. The brief is frozen again from this re-pin through the implementation freeze (P4).

## Exact delta accepted

1. **§2.5 / design r2.22 §6.1, B457:** Settings browsing requires an open configuration service. If unavailable, the ordinary Settings landing is the closed `CFG UNAVAILABLE` view in menu mode: no arrow, cue beside SETTINGS, short walks the rail, double refuses until opening succeeds. One authority applies the rule across entry/return paths. Existing pre-emption policy and the open-service landing remain unchanged; the rule does not gate an active, otherwise ungated invitation or roster-grant flow.
2. **§2.9:** native cases cover closed-service Home INVITE and TEAM-roster grant followed by emergency pre-emption, recovery and the open-service counterpart. A real-renderer case proves the unavailable preview, cue, no arrow and rail escape. `w4bhome` gains the B457 property; its minimum becomes eleven entries. The two design clarifications are explicit native obligations: the press coincident with raising `OPTIONS CHANGED` is consumed without clearing it; an empty Send list's `PRESET CHANGED` temporarily covers its sole MENU row.
3. **§7:** B457 closes only after independent implementation QA, alongside B456's separately required instrument proof.
4. **§1:** the design and register pins refresh. Revision/status/history text records the checkpoint and these changes.

The byte comparison with the previous authorized brief changes only the header, §1 pins, §2.5, §2.9, §7 and revision history. The allocation, fence, §4 gate chain, §2.10 renderer obligations, and other contract sections are byte-identical. The preserved delta is in the evidence folder.

## Base and checkpoint reconciliation

The actual MeshRoute HEAD is now owner commit **`c8e36d8d5d32f29edf408b36b60d8977993f5ad4`**, whose parent is `8360802904f7bd0023279d3da844453d61207ede`. Its stored brief has the exact previous authorized hash above. This reconciles the older `8360802` wording in the brief's original startup instructions; do not reset to that commit or compare the partial implementation with the original source hashes as if it were an untouched startup.

Independently checked:

- All **14 original executable hashes and line counts match at `c8e36d8`**. Against the full 1,576-path original pre-check inventory, that commit differs only in the four previously permitted preparation documents. The coder's recorded owner-commit reconciliation is therefore supported by content, not merely the commit label.
- All **six current preparation hashes match revision 3**, including design r2.22 and register B457. `tracker.md`, `MEMORY.md` and the pre-check evidence remain at their existing pins.
- Current tracked changes comprise **13 fenced implementation/test/tool paths and the three author documents** (brief, design, register). The new B456 test and coder evidence paths are accounted for. No unexplained path or out-of-fence implementation edit was found in this inventory check. This is not a semantic approval of those edits.
- The simulator remains **`6585649ea5a780f0542b2931853a667be56a5b2b`**, clean. Nothing is staged in either repository.
- Prior pre-check/review/re-review checksum sets verify **14 + 8 + 6 files**; all seven brief links resolve.

The resume base is **`c8e36d8` plus this receipt's `inputs.json` SHA-256 inventory of the partial candidate**, including untracked inputs. This inventory captured **1,621 MeshRoute paths and 285 simulator paths**. The authorized new preparation hashes replace the old design/register pins; all other unexplained changes remain STOP conditions. Re-inventory at resume, then implement revision 3 within the existing fence and run the required final chain before freezing. Existing stage-A/baseline records are historical evidence, not a substitute for that final chain. No commit is required.

## Source checks and M103 disposition

The B457 source trace is real. At the preserved candidate, `UiModel::emergency_gesture` calls `close_provisioning()` (`model.h:5993`), which sets `Settings::browsing` (`:4701`). `sync_settings` (`:4446`) retries opening but does not normalize failed-service browsing to the closed preview. `draw_settings_screen` (`firmware_ui.cpp:2185`) returns after drawing only `CFG UNAVAILABLE`. The corresponding close/pre-emption/renderer statements also exist at the owner commit. These are source checks; no new runtime B457 reproduction or gate PASS is claimed here.

The two clarifications fit the current candidate's explicit seams: `on_gesture` captures `note_was_up` before `sync_home` (`model.h:2991`), and `home_gesture` clears an existing note only when that captured value was true (`:4157`). `send_list_row_override` checks the changed-note case before the exit-row case (`:1773`). Their required final tests remain mandatory.

**M103: retirement is deferred to the final implementation gate; it does not block this re-pin.**

- The current entry at `tools/probe_ui_model_mutations.py:3514` matches once. It removes **both** `_st.cursor = 0` and `_cfg_sel_valid = false` from `close_settings_menu`, not only the cursor reset.
- In the current executable source, the helper has one caller (`model.h:4608`), immediately followed by `go_menu_home`. That path resets the cursor at `:4110` and the selection-valid flag through `settings_follow_screen` at `:4406`. The duplicate-reset explanation is supported at this checkpoint.
- The coder's retained log reports M103 non-RED. I inspected that capture; I did **not** independently rerun the mutation or accept its suite figures as QA results. Worker output and the aggregate summary repeat the same failure, not two independent runs.
- B457's new normalization authority may add a caller or change this reachability. Therefore the current call graph is insufficient to authorize permanent retirement before that fix is complete. Keep M103 visible in the disposition ledger and report its result in the fresh final chain. At the freeze, QA will decide from the final call graph, both removed effects, the preserved cursor/context expectations and the independently rerun battery whether the entry is redundant or still needs a meaningful control. Do not label it RED, silently drop it, or inherit its disposition from this intermediate candidate.

This re-pin approves no mutation exemption and does not close B456 or B457. The full implementation and independent QA gates remain pending.

## Evidence and preservation

The [evidence directory](2026-09-27-standalone-mobile-home-w4b-r3-repin/) holds the checkpoint inventory, baseline/pin reconciliation, exact brief delta, source witnesses, machine-readable authorization and final preservation check, covered by `SHA256SUMS`. Source and shell-pattern inspection only; no builds, mutation runs, corpus, board measurements or other implementation gates were run during this checkpoint review.

Only this receipt and its evidence were added. The partial candidate, all earlier evidence, brief, design, register and simulator were preserved. No production fix, staging, reset, cleanup or commit was performed.
