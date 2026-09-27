<!-- Author: Codex, independent Quality Agent; review of Claude's W4a revision 1; no implementation edits -->
# W4a brief review — PASS with fold-ins

**2026-09-26: PASS with three MINOR fold-ins, W4R-1–W4R-3.** The display contract, six site budgets, zero resident growth, instrument-first O8 repair, O6 retirement, source fence and gate split are accepted. These corrections make the instructions and proof obligations consistent; they require no owner ruling or substantive re-review. This is a **brief verdict**, not an implementation or metal PASS. The brief and every preparation input remain unchanged.

## Authorization and inputs

Reviewed [revision 1](../plans/2026-09-26-standalone-mobile-home-w4a-identity-labels.md), SHA-256:

`6db3cfa2cf982a0334d5e4e6c3cca395dc8efb5fed7023ee5f161b0afa3340ef`

MeshRoute is at `8360802904f7bd0023279d3da844453d61207ede`, with the preserved uncommitted W1c/W3 candidates and landings. The simulator is at `6585649ea5a780f0542b2931853a667be56a5b2b`, clean. **All 16 pinned file hashes match**, as do all ten stated source line counts. The pre-check folder verifies **30/30** checksums; all five brief links resolve.

The full pre-check inventory reconciles: of its **1,455 MeshRoute paths**, exactly the four declared preparation documents changed (design, register, tracker and MEMORY), with no missing path. The **33 additions** are the pre-check report and its folder, plus this brief. All **285 simulator paths** are unchanged. There is no unexplained source/test/tool change; the base is the complete working tree, not HEAD alone. Nothing is staged.

The author can apply the [exact required fold-in patch](2026-09-26-standalone-mobile-home-w4a-brief-review/required-fold-ins.patch). **`git apply --check` passes; the patch has not been applied.** The resulting revision-2 bytes are authorized for coder source-validation and dispatch at:

`7fafe9ef08aa50842a30240b08fa079cf8ce67c608f46d8ebe390ed879fff067`

This is a **prospective** SHA-256, calculated from the exact patch. Revision 1 remains on disk and is not authorized as written. Verify the resulting hash after applying the patch, then freeze it and its preparation set through the implementation handoff. No commit is required. Any additional change is outside this prospective authorization. This receipt and its evidence are explained preparation additions under brief §1.

## Required fold-ins

### W4R-1 — MINOR: make edge cases, the STOP rule and O8 attribution agree with the contract

**Brief:** §2.1 line 146; §2.2 lines 177–178; §2.3 lines 192–197; §2.6 lines 262 and 281–285; §4.3 lines 435–436.

The required invite path formats the full raw name at 14, then formats that name-only carrier at 6. The blanket STOP on a pre-clipped input or sanitizing twice would stop that exact implementation. Exempt **only** the explicitly approved, strictly narrower name-only 14→6 projection. Keep STOP for truncating the raw name before its first projection, re-sanitizing a retained marker at an equal/wider budget, or exporting panel bytes to non-panel paths. A fresh mathematical measurement checks **8,976** inputs, including every repeated byte and a raw 0xBB at every possible position: 14→6 equals direct-to-6. This measures the contract's composition, not unimplemented production code.

The small-budget test instruction also needs its input kind:

- **Unnamed with a nonzero hash:** budgets 0–5 return `no_fit` and an empty output when capacity is positive.
- **Named:** budget 0 returns `no_fit` and an empty output when capacity is positive. Budgets 1–5 obey the existing name rule; an overlong name at budget 1 is a marker alone. They do not all return `no_fit`.
- Capacity 0 still writes nothing. Explicitly emptying a positive-capacity output on the named zero-budget arm preserves §2.2's W1 termination guarantee.

The “every output buffer is budget + 1” sentence must distinguish new stack buffers from the deliberately retained resident carriers: TEAM keeps its 15-byte carrier while using six columns. Require sufficient capacity, keep the resident sizes, and prove the production budgets/capacities at compile time.

For post-change O8, distinguish a failing **blank-name assertion** from invariants that must remain correct. Its injected hash-derived name must fail P23b/P23d's blank-field assertions; the row should remain 19 columns with its separate fingerprint intact. Those are conjuncts of today's checks (`probe_main.cpp`:5437–5439, 5477–5479), not three independently required failures. The patch removes that ambiguity without weakening the forbidden-fallback proof.

### W4R-2 — MINOR: identify and repeat the supplemental layout instrument

**Brief:** §1 line 76; §2.8 lines 321–322; §4.1 lines 384–385; §4.2's ABI bullet.

The listed sizes are consistent with the pre-check, but the stock `tools/probe_board_abi.py` sweep does **not** measure `TeamRow`, `InviteIdRows` or `OutcomeView`. Its `PINNED` list begins at line 143 and includes `UiState`, `UiSnapshot`, `UiModel` and `InviteMember` at 170–175. The other three figures came from the pre-check's separate compile-only measurement using the real three ABI toolchains. Calling the stock probe cannot reproduce those three results.

The patch corrects the attribution and explicitly repeats the existing pre-check **base** recipe before implementation, on the final coder tree, and at independent QA. It reads the current headers, extracts the current `OutcomeView` declaration exactly once, emits size/alignment symbols in a temporary TU, and uses the stock ABI module's real target flags/compiler/`nm` path. No counterfactual capacity edits, hand-maintained mirror, stock probe edit or re-pin are authorized. Preserve the small driver, generated TU, input hashes and results. This supplements the full stock probe and its controls; it does not replace them or the linked RAM measurements.

The size contract stays unchanged: `TeamRow` 40, `InviteIdRows` 26, `OutcomeView` 52 on all three ABIs. The pinned Node sizes are made explicit as 235208 / 122176 / 157304 for native / mobile / gateway. These are verified prior pre-check measurements, **not fresh board measurements in this review**.

### W4R-3 — MINOR: retain independent TEAM width and invisible-tail controls when fixtures are projected

**Brief:** §2.6 TEAM fixture ledger and native cases; §2.7 lines 309–315.

The pre-check explicitly identified the risk to invite I09, but did not call out the same risk to TEAM **T01**. This review corrects that omission. T01 removes the row's six-column precision (`probe_ui_model_mutations.py`:6450–6452). If every fixture has already become a six-cell label, correct and mutant rows are indistinguishable. T09 broadens the comparison from six bytes to the whole 15-byte carrier (:6478–6480); two identical, zero-padded projected labels cannot expose it either.

A focused C++20 measurement compiled the **current real TEAM header** and isolated copies with those existing mutations. All three variants compiled and finished:

| Input | Correct | T01 | T09 |
| --- | --- | --- | --- |
| Already formatted `Wolfg` + 0xBB | Same 19-byte row | Same row: cannot distinguish the missing bound | Same row |
| Synthetic direct `Wolfgangetta` carrier | ` Wolfga 12s        ` | ` Wolfgangetta 12s        `: bound loss visible | Correct row |
| Same first six bytes, synthetic difference at label[10] | Rows equal | Rows equal | Rows unequal: excessive comparison visible |

Retain or add labelled synthetic inputs for these two existing contracts, alongside the reachable rename/projection cases. Do not project the overlong defensive fixture before passing it to the row function. Keep the ordinary new display expectations literal. This is the same treatment already required for I09; it adds no product behavior or state. Retain existing mutation anchors verbatim when their production statements remain unchanged, and re-anchor only where necessary, with meanings and entry counts preserved.

These are focused discrimination measurements, **not a fresh native suite or a completed mutation battery**. They establish why the fixture requirements matter; the coder's union and QA's touched batteries still owe their execution.

## Independently checked and accepted

- **Formatter and data path.** The existing `ui_display_byte` at model:1446 is the correct single sanitizer. Model includes the invite header, so placing the helper after the sanitizer can reuse both existing uppercase hash helpers without creating an include cycle. Counted full 32-byte reads preserve W1's raw API contract. Source name bytes, including raw 0xBB or embedded NUL, are sanitized before the generated marker. The existing reply copy, frame copies and optional confirmation name preserve that marker; console, JSON and companion paths do not consume these panel labels.
- **Sites and budgets.** TEAM 6; compose header 15; DELIVERED peer row 19; REPLY 14; invite publication/confirmation name 14 and candidate name 6. The design r2.20 states these same choices and corrects the result-row hash example. Existing resident carriers suffice; only stack scratch/output sizes change. Own Home/My device, new review pages, team identities, routing, peer-cache precedence and frame lifecycle stay outside this slice.
- **Repaint.** `ui_team_rows_equal` compares the visible six bytes (:233–242). Publishing the formatted label before comparison makes exact-six to longer-with-the-same-prefix observable, while equal formatted names remain equal. No new resident copy or comparison policy is needed.
- **Controls.** Stage A repairs O8 against unchanged production and records its actual blank-name failures before the product change. The pre-check's O8/O6 reproduction evidence verifies by checksum; it was not rerun here. B455 now records O6's actual two request-screen targets and sole lowercase failure. Retiring that misleading duplicate while retaining O20 and the full-hash checks is acceptable. W1 poison/canary controls, the file-under-test wrapper, C0's build-failure meaning, per-substitution guards and the closed expectation ledger remain required.
- **Mutation selection.** Fresh safe AST inspection, without importing the executable harness, derives **292 selector-(a) + 87 selector-(b) = 379 existing entries**. Every configured needle matches exactly once. The new `w4aident` raises the floor; its seven required meanings, the four renderer controls, native count derivation and tools discovery are appropriate. No unrelated battery repair is authorized.
- **Gate and evidence.** The stock board recipe keeps same-source repeatability comparisons separate from cross-source attribution. Paired runs remain isolated from edits/builds/evidence additions. Gateway image identity and unchanged mobile RAM are predictions with STOP conditions; mobile flash/stack differences must be derived. Coder census/full union and the narrower independent P6 gate are correctly separated. B418 remains a diagnostic limitation, not a board-UI PASS.
- **Metal.** A new row in the current metal plan checks physical `»` legibility, high-byte sanitization and unnamed identities on controlled caches. The host canvas proves bytes, not panel pixels. No metal result is claimed; QA adds the row only at implementation landing.

## Checks run and preservation

| Check in this review | Result |
| --- | --- |
| Pins, line counts, evidence checksums, inventory, links | All pass as detailed above |
| Mutation source census | 379/379 needles match once; no battery execution |
| Name-only 14→6 composition measurement | 8,976 checks, zero differences |
| Real-header TEAM fixture discrimination | Baseline, T01 and T09 compile and finish; results above |
| Prospective fold-in patch | `git apply --check` passes; resulting hash calculated; not applied |
| Preservation and whitespace | Every initial inventoried input unchanged; only this review's evidence added; both repository whitespace checks clean; simulator clean; nothing staged |

One initial scratch compile used C++17 and failed because current core headers use `std::span`. The scratch driver was corrected to C++20 and all three variants compiled. This was a review-driver invocation error, not a product failure or a control result; its output is preserved under the raw artifact directory.

**Not rerun:** native suite, corpus, full firmware-UI probe, ABI sweep, board measurements, warning census, tools discovery, full board-UI probe or mutation union. The fresh pre-check baselines remain historical: native 2973/196111/0 with no skips; UI 518/964/518 with 231 controls and zero unusable; ABI 290 checks and nine controls RED; corpus 36/36 byte-identical. Unchanged executable inputs support using them as baselines, not substituting them for either implementation gate.

No production, test, tool, brief, design, register, tracker or MEMORY input was edited. No new production bug or register entry is asserted: W4R-1–W4R-3 are dispatch-brief corrections for the author. B441/B449/B455 remain open until the implementation passes independent QA. Nothing is staged or committed.

Evidence: [authorization](2026-09-26-standalone-mobile-home-w4a-brief-review/authorization.json), [input check](2026-09-26-standalone-mobile-home-w4a-brief-review/input-check.json), [ABI coverage](2026-09-26-standalone-mobile-home-w4a-brief-review/abi-coverage.json), [mutation census](2026-09-26-standalone-mobile-home-w4a-brief-review/mutation-check.json), [focused measurements](2026-09-26-standalone-mobile-home-w4a-brief-review/team-fixture-measurement.json), [preservation](2026-09-26-standalone-mobile-home-w4a-brief-review/preservation.json), [checksums](2026-09-26-standalone-mobile-home-w4a-brief-review/SHA256SUMS). Raw review inputs and the failed scratch invocation are retained under ignored `artifacts/2026-09-26-standalone-mobile-home-w4a-brief-review/`, indexed by hash.
