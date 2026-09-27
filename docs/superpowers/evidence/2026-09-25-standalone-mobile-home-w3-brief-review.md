<!-- Author: Codex, independent Quality Agent; reviewed Claude's W3 revision 1; no implementation edits -->
# W3 brief review — PASS with fold-ins

**2026-09-25: PASS with three MINOR text fold-ins, W3R-1–W3R-3. No substantive re-review or owner ruling is needed.** The three C1 extractions, two-stage characterization, fence, mutation selection and gate split are accepted. This is a brief verdict, not an implementation verdict. All inputs under review, including the brief, remain unchanged.

## Authorization and input verification

Reviewed [revision 1](../plans/2026-09-25-standalone-mobile-home-w3-ui-model-seams.md):

`bf90a44ee92e202b4fa20e8de590ffd1c11bf6eb20454f8d9f1c0b87821f1d80`

Base: MeshRoute `8360802904f7bd0023279d3da844453d61207ede` plus the uncommitted QA-passed W1c candidate; simulator `6585649ea5a780f0542b2931853a667be56a5b2b`, clean. **All 12 pinned file hashes match**, including the five editable code/test/tool files, the frozen renderer, and six preparation/evidence files. The pre-check folder verifies **23/23** checksums. All existing brief links resolve.

The full pre-check inventory reconciliation finds exactly the four declared changed documents: design r2.19, register, tracker and MEMORY. All original source/test/tool inputs are unchanged. The 26 added paths are the pre-check report, its 24-file folder including its checksum index, and the W3 brief; there is no unexplained addition. The inventory includes untracked W1c work, not just HEAD.

The Author can apply the [exact fold-in patch](2026-09-25-standalone-mobile-home-w3-brief-review/required-fold-ins.patch). It has passed `git apply --check` without being applied. It makes only the corrections below plus the revision/header/history update. **The resulting revision-2 bytes are authorized for coder source-validation and dispatch at:**

`9a84d03b0f2e9826df67feba52ff87c50644383bfb85ef8bc06af7ed8e25d825`

This is a **prospective** SHA-256, derived from the exact patch; revision 1 remains on disk. Verify the resulting hash before dispatch. Leave that brief and its preparation set frozen afterwards. No commit is required; no other text changes are covered by this prospective authorization. This receipt and its evidence are explained preparation additions under brief §1.

## Required fold-ins

### W3R-1 — MINOR: make test applicability and regression stops agree with the planned proof

**Brief:** §2.4 lines 163–164, §4.2 line 341, §4.3 lines 354–355.

Three wording points have unambiguous resolutions already implied by the rest of the brief:

- The detail fixture instruction applies “cycle back” and blank/wake on a nonzero page “for each” length, but lengths **0 and 38 have only one page**. `UiModel::on_inbox_opened` computes one page for both, and tick explicitly requires `detail_pages > 1` (`src/firmware_ui_model.h`:2841, 3209–3210). Test stable page zero across time and blank/wake for single-page bodies; apply cycling and nonzero-page wake to multi-page bodies.
- STOP on “any native, probe or corpus delta” would include the new check/case totals that §2.4 requires. Keep STOP for changed existing expectations, stage-A outcomes changed by stage B, corpus bytes or ABI/layout. Permit only the planned added counts and named assertion restatements, with derivation and no weakening. Frozen probe expectations remain frozen.
- QA's “Native — the binary run” should explicitly name a **fresh `pio test -e native` followed by the binary**, as D1 and coder §4.1 already do. An inherited executable is not independent QA. This makes the existing gate obligation explicit rather than adding another instrument.

The patch makes those distinctions. The production behavior contract and stage-A freeze are unchanged.

### W3R-2 — MINOR: feature ownership does read fenced files

**Brief:** §5 item 9, lines 404–405.

The blanket statement that the omitted probes “compile or read none of the fenced files” is false for the feature probe's ownership scanner. `tools/probe_features/ownership.py` defines `SCAN_DIRS=("lib", "src", "test")` at line 55; `scan_files` at 226 walks those trees; `run_checks` at 290–291 reads every selected file. That includes `src/firmware_ui_model.h` and `test/test_firmware_ui_model.cpp`.

Its predicates concern `MR_FEAT_RADMIN_*` ownership, not the pager, compose bound or settings gate. A focused stock run here passes, including its **218-file** integrity census. The proposed edits do not affect those predicates, so the full feature-matrix omission is justified by **unaffected scope**, not absence of a reader. The patch separates it from the other omissions and requires the final D6/P7 audit to confirm this and rerun affected checks if necessary. No additional full feature-matrix gate is imposed.

This is the same reader-census distinction previously recorded in W1c review WCR-2; filename-only grep is insufficient for directory scanners.

### W3R-3 — MINOR: align artifact wording with retention policy and stock build roots

**Brief:** §5 lines 377 and 382.

“The evidence directory holds logs” and “build trees stay outside the repository” conflict with the tools/policy the brief otherwise selects:

- [Evidence retention](README.md) keeps compact receipts, hashes and reusable proofs in Git; new raw runs/logs belong under ignored `artifacts/` or an explicit external archive.
- `tools/measure_board.py`:66–85 fixes board build/libdeps/workspace roots under **`.pio-measure/env/<env>/` inside this repository**. Those paths must stay stable: the module's B262 explanation identifies mobile payload path dependence. Normal native builds use `.pio/`.

The patch puts raw logs under a named ignored W3 directory or external archive, retains compact summaries/manifests and raw-file hashes in the evidence folder, and explicitly preserves the stock board/native roots. Simulator and mutation scratch builds remain outside both repositories. No board command, repeatability comparison, manifest field or artifact-preservation obligation is weakened.

## Independently verified and accepted

**Pager and state.** Source confirms 19 columns × 2 rows, 38 bytes/page, seven maximum pages, 2,000 ms cadence; counted-byte normalization/sanitization before slicing; exactly one row terminator without clearing unused tails; empty-to-one page behavior; retained page and restart clock on wake. The brief leaves timer/identity/deletion transitions and `UiState::detail_line[2][20]` unchanged. Parameterized helper tests use local buffers. The r2.19 clarification correctly assigns any resident third row and word-wrap behavior to W6/W8.

**Compose.** The two display couplings are `ComposeSlot::text` and `compose_project`'s clamp. Deriving 17 columns from `kDetailCols - 2` preserves the 20-byte slot and 161-byte list while keeping record validation/storage/send bounds independent. `send_gate_of` and `ui_compose_send_line` still use the live catalog and generation. Leaving `kDetailCols` and the renderer's literal equality intact avoids unnecessary M52/W43 changes. Record-limit tests and ABI pins remain outside the edit fence.

**Provisioning.** `sync_settings` opens before the closed-view return; `ConfigService::open` returns early when already open and retains its retry behavior after a failed load. `activate` prevents current SETTINGS menu entry without an open service. Thus calling the shared repeat-safe opener from admission adds no load on reachable existing PROVISION activations. The brief retains conflict-before-unsaved, exact notes, no implicit write/apply, note clearing and dirty timing; it labels the future caller's closed-service admission as currently unreachable. Native counted-load characterization closes the specific pre-check gap without adding a production hook.

**Two-stage proof.** Adding exact renderer checks and controls against unchanged production, then freezing both probe files, prevents the refactor from repairing its own expected output. Native stage-A cases remain unchanged except explicitly named budget restatements; helper cases are additive. New controls mutate scratch copies of the renderer, so keeping production `firmware_ui.cpp` outside the fence is consistent. Exactly-one guards, must-build/new-check attribution and existing C0's build-failure meaning are preserved. The real config fake has a load-failure seam (`ProbeCfgStore::can_load`, probe main:661–668); unavailable render testing must occur while the service is still closed, not after a successful opening. The brief already requires a reason if that case cannot be reached in its fixture sequence.

**Existing limitations.** B449's O8 misattribution is not repaired by W3 or recertified as its advertised fallback proof; it keeps its actual existing effect, with repair assigned to W4a. B418 remains W2's work. Frozen probe files may stop implementation if stage A was incomplete; that is the intended characterization boundary, not permission to weaken them later.

**Mutations.** Independent AST parsing (no executable-harness import) derives **6 batteries / 364 entries**: `model` 239, `sliceCbudget` 1, `uipresets` 32, `uisend` 15, `config` 32, `uiprov` 45. Every baseline needle matches exactly once in its configured source. The eleven named anchors have the expected meanings; M100's new placement mutation still targets passive-arrival opening. The broader M52/M53, timer/wake and Y-entry obligations are explicitly retained. These are source-match measurements, **not fresh RED results**.

**Board recipe.** Stock `pair --jobs=1 --output .pio-measure/w3/...` is valid; comparing per-board same-source manifests for base repeatability and final repeatability is correct. Cross-source attribution is field-by-field, not a repeatability `compare`. Copying the eight manifests only after measurement avoids polluting the source snapshot between paired runs. Gateway payload identity, mobile unchanged RAM, attributed flash and no ABI re-pin are appropriate predictions, not verified W3 results. The six-environment warning census stays with the coder; P6 QA independently reruns native, corpus, boards, the two touched batteries and affected UI/ABI probes.

## Checks actually run for this review

| Check | Result and scope |
| --- | --- |
| Input/base reconciliation | 12/12 brief hashes; 23/23 pre-check evidence checksums; exactly four declared document changes; W1c code/test/tool state preserved |
| Mutation source census | All 364 configured needles match once; no mutation execution |
| `tools/probe_board_ui/run.sh --no-neg` | Exit **1**; exactly **W49, W51, W54** fail, as predicted. Structural 23/23; wiring 56 passed / 3 failed; 171 wiring controls verified. V3 124/124 and V4 110/110 executable checks. This is a diagnostic with known failures, **not a full board-UI PASS** |
| `python3 -B tools/probe_features/ownership.py` | Exit 0; ownership checks and 218-file source integrity pass; no full feature-matrix or ownership-control run |
| Fold-in patch | `git apply --check` passes; prospective hash calculated without editing the brief |
| Preservation and whitespace | Every initial inventoried input unchanged; only this review's evidence added; `git diff --check` clean; no staged files; simulator unchanged and clean |

No native rebuild, corpus, full UI probe, ABI probe, board measurement, warning census, tools discovery or mutation battery was rerun for this **brief review**. Their fresh pre-check results remain explicitly historical baselines, backed by unchanged source inputs; the coder and independent implementation gate still owe their runs.

No code/test/tool/design/register/tracker/MEMORY input was edited. No new production bug or register entry is asserted; W3R-1–W3R-3 are corrections to the dispatch brief for the author to land. Nothing is staged or committed.

Evidence: [checksums](2026-09-25-standalone-mobile-home-w3-brief-review/SHA256SUMS), [authorization](2026-09-25-standalone-mobile-home-w3-brief-review/authorization.json), [input check](2026-09-25-standalone-mobile-home-w3-brief-review/input-check.json), [mutation census](2026-09-25-standalone-mobile-home-w3-brief-review/mutation-check.json), [focused runs](2026-09-25-standalone-mobile-home-w3-brief-review/focused-runs.json), [preservation](2026-09-25-standalone-mobile-home-w3-brief-review/preservation.json). Raw logs stay in ignored `artifacts/2026-09-25-standalone-mobile-home-w3-brief-review/`, with hashes in the evidence index.
