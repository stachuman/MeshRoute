<!-- Author: Codex, independent QA reviewer; brief author: Claude -->
# B459 revision-1 brief review — 2026-09-28

**Verdict: HOLD — B459R-1 must be corrected before dispatch.** B459R-2 and B459R-3 are small consistency fold-ins to make in the same revision. No owner ruling is requested, and the accepted accounting design below does not need reopening. Re-review can be scoped to the changed gate/fence/wording and refreshed pins.

Reviewed: [B459 brief revision 1](../plans/2026-09-28-b459-board-ui-accounting.md), **523 lines**, SHA-256 **`f485c49427838f1480fb5a8b32a0dd454f3e88a4c7eab65b54aaeeb46c7e1453`**. **This hash is reviewed, not authorized for coder dispatch.** The brief and every preparation input remain untouched.

Evidence: [2026-09-28-b459-brief-review/](2026-09-28-b459-brief-review/), sealed by `SHA256SUMS`. Raw compiler/preprocessor and probe output is local under ignored `artifacts/2026-09-28-b459-brief-review/`, hashed in `raw-logs.json`.

## 1. Pins and base — PASS

[Preflight proof](2026-09-28-b459-brief-review/preflight.json): all **11 explicit existing-file pins**, all **19 pre-check checksum entries**, all listed links and the brief hash/line count match. All four proposed new tool paths are absent. MeshRoute remains **8079e545fb2e1065c2bbc347eb9a66160f6cbf8e** plus the preserved W2 candidate; simulator **6585649ea5a780f0542b2931853a667be56a5b2b**, clean. Nothing staged.

Against the pre-check's entry inventory, only the three declared existing preparation files changed: register, tracker and MEMORY. The design is unchanged. Additions are the pre-check's own report/evidence and this brief; there is no unexplained tool/product input. W2's four tool hashes still match their QA freeze. `inputs.json` captures this review's complete entry inventory rather than substituting HEAD for the working tree.

The register records the owner's B461 fold-in and the separate B473 limitation. Those boundaries are accepted. No register/design/tracker/MEMORY changes were made by this review, and no B474 number was consumed.

## 2. Required correction and consistency fold-ins

### B459R-1 — MAJOR — increment 1 freezes before its required discovery gate

**Brief anchors:** §4.1, lines 385–392 (increment-1 gate); line 383 (discovery before edits); final-chain step 8, lines 414–415 (discovery after increment 2).

Increment 1 changes a live runner and its discoverable test. Its own gate currently runs the full firmware-UI probe, the 19 focused tests and two scratch proofs, then freezes. Full tools discovery is run on the untouched baseline and later on increment 2, but **never on increment 1's own candidate before it is declared separately gated**.

This omits the standing **D5** requirement at that independently frozen increment. The accepted pre-check explicitly states in Q4/§4, line 98: “Rerun firmware-UI's full default three-arm/control gate, all its 19 accounting regressions, and discovery after each affected freeze.” The tools baseline cannot validate the subsequent extraction. A later combined-tree result is useful for the final gate, but does not establish the promised separate first-increment gate.

**Required change:** add `python3 -m unittest discover -s tools -p 'test_*.py'` to the increment-1 gate **before its freeze**. Expect the same **375 tests, OK, zero skips/import failures**, because this increment permits only extraction and library-resolution/bypass binding maintenance, not new tests. Record that run against the three frozen input hashes. Keep final-chain discovery with the increment-2 additions and its derived count. No native, corpus, board pair or new unrelated probe is requested.

The existing final QA rerun remains necessary; its receipt must distinguish this first-freeze evidence from the later complete candidate.

### B459R-2 — MINOR — make the increment-1 fence agree with its bypass and evidence obligations

**Brief anchors:** §1 executable-input table (`tools/test_probe_firmware_ui.py`, “library resolution only”); §2.1 (`ACCOUNTING_STATEMENT` may change with the real call); §3 increment-1 fence (lines 347–350), and evidence listed only under increment 2 (line 358).

The contract explicitly permits changing the bypass test's exact call-string binding when the real final call changes, but the table and fence authorize only library resolution. Also, increment 1 must capture baselines and write its freeze snapshots under §5, while the explicit report/evidence fence appears only under increment 2. These are avoidable scope ambiguities for a coder working under the “only increment-1 files differ” check.

**Fold-in:** consistently allow only (a) library resolution for the scratch copy and (b) any strictly necessary `ACCOUNTING_STATEMENT` binding to the actual final call, with no assertion weakening or test retirement. Alternatively require the call text to stay unchanged and remove the conditional permission. Make §5 report/evidence outputs common to both increments and exclude those authorized outputs from the “three source files only” comparison. No additional executable path is needed.

### B459R-3 — MINOR — disambiguate the paired-removal regression

**Brief anchors:** §2.2.1 honest limit, lines 201–203; §2.2.7 Coverage, line 297.

The manifest is now the checked-in declaration authority. The honest limit correctly says removing a check and its manifest row together is not runtime-detectable. The test list's “a declaration and call removed together, which the manifest comparison rejects” could be read as demanding the opposite.

**Fold-in:** say that the **source call/declaration is removed while the checked-in manifest stays intact**; the census must reject that mismatch. Keep the separately stated paired source-plus-manifest deletion limit, checked by the frozen coverage diff/review. No new accounting behavior or extra test family is requested.

## 3. Technical decisions accepted

- **Checked-in manifest:** accepted. It makes check deletion review-visible and avoids deriving the expected set from whichever calls executed. The static source census and frozen pre-change identity/property comparison are both required and present. A numeric floor alone is insufficient; the brief does not substitute one for identities.
- **592 identities / 529 diagnostic identities / 63 mode exclusions:** the arithmetic holds. Independently preprocessing the pinned canvas source under the runner's actual V3 and V4 defines yields **115 and 101 literal CHK sites**. Expanding P4a's ten iterations gives **124 and 110 unique arm-qualified identities**. All negctl first-token IDs are unique within their 60/3 lists. [Identity census](2026-09-28-b459-brief-review/identity-census.json). The shell layer/ordinal mapping remains tied to the already pinned pre-check and W2 reconciliation.
- **Shared generic comparator:** accepted, with recorder/guard bookkeeping separated from firmware-UI's extractor, C0 mapping, counters and modes. Parameterized expected outcome strings are sufficient; there is no need to migrate the eleven other runners. B456 output equivalence and the real-call bypass proof remain binding.
- **Two increments:** accepted. Keeping increment 1's three files hash-frozen through increment 2 makes attribution reviewable. Neither increment waits for a commit. B459R-1 completes its stated gate discipline.
- **Outcome representation:** a negctl observation may use accepted status `red` plus a distinct `compile_fail` or `assert_red` detail. That is compatible with one accepted outcome per manifest row and the current two accepted paths. It must not erase the kind or silently tighten B473. Canvas failures and harness errors are not accepted observations.
- **Harness-error handling:** accepted. I measured the proposed boundary on all current wiring evaluations, rather than assuming the recommendation was achievable: **60 live predicates returned 0; 186 distinct mutant evaluations returned 1; no exception**. [Status receipt](2026-09-28-b459-brief-review/predicate-exits.json) and [per-evaluation rows](2026-09-28-b459-brief-review/predicate-exits.tsv). A failed sed or status outside 0/1 can be rejected without changing any existing successful control in this baseline.
- **Modes:** accepted. Default accounts everything before a final verdict. No-neg continues the positive and inline-control layers, expressly skips only the 63 canvas mutants and announces that it is not a gate. Its failures must still return nonzero, as required by the missing-control regressions. No B456 mode policy is copied indiscriminately onto board-UI.
- **B461:** the owner-authorized comment correction is technically safe and remains subject to the specified W50 checks. B473's current crash/exit-status weakness stays outside this package.

## 4. Fence and reader review

The selected executable paths cover the actual new consumers: the shell coordinator, the positive canvas emitter, negctl's child-result producer, the optional static extractor, the manifest, the board accounting tests, the common helper and B456's runner/test binding. There is no missing product or simulator dependency. The small ambiguity in the test/evidence permission is B459R-2, not a request to widen the production fence.

The critical readers remain the ones established in the pre-check: negctl parses canvas FAIL lines; board-UI launches both arms and trait controls from that source; B456's tests inspect the exact bypass statement and ctl extraction; W2's evidence parsers consume the old section headers and control results. The brief preserves those forms or permits a newly versioned independent evidence reader without rewriting historical evidence. No shared fake change is needed. Ordinal controls remain tied to their exact substitutions/properties, and no controlled product statement needs a marker edit.

The final focused gate otherwise covers the changed instruments honestly: stock default and diagnostic board runs, runner-level regressions, independent live reconciliation, final firmware-UI plus B456 tests, discovery, byte-identical command inventory, all-input preservation and reader audit. Excluding native/corpus/board measurements/ABI/census/mutation batteries is justified by the tool-only fence and preservation proof, not by inheriting their old results as new runs.

## 5. Work performed and limits

Fresh for this review:

- verified both bases, preparation changes, eleven pins, nineteen pre-check checksums and all brief links;
- reread the relevant rules, both runners, canvas macro/branches, negctl verdict paths and B456 tests;
- ran the **19 B456 tests: OK, zero skips**;
- preprocessed both real canvas probe arms and independently extracted their identities;
- ran a **labelled temporary copy** of the board runner in `--no-neg` mode, changing only scratch path setup and status logging around its original predicates, to obtain the 60/186 exit measurements. This also compiled the live/trait/missing-trait probe paths; it is **not** a stock default release gate;
- checked whitespace and whole-input preservation at the end.

One evidence-extractor setup error was caught by its own zero-count assertion: an initial ad-hoc regex assumed the wrong spaces/parentheses in the CHK macro expansion. It was corrected to the actual source spelling; no zero-count result was accepted. The corrected script and source-derived counts are retained.

Not run in this brief review: full tools discovery, full firmware-UI default, stock board-UI default with negctl, native, corpus, board pair, ABI, warning census, mutation batteries or metal. The pre-check's prior runs are identified as prior evidence, not claimed anew. This review has not implemented the shared library or any part of B459.

No new production/tool defect was discovered beyond already registered B459/B461/B473 and the pre-check census. The three findings above concern the reviewed brief and are returned to its author here; the pinned register is unchanged.

## 6. Handoff

Claude should fold B459R-1 through B459R-3 into the brief and issue the new hash. QA's scoped re-review need only check those passages and any declared pin refresh. The manifest choice, identities, outcome boundaries, B461 fold-in and the tool-only gate scope are accepted and do not need another design discussion.

**No dispatch authorization is issued for revision 1.** W2 and all pre-existing work remain preserved and uncommitted; simulator unchanged. Final hashes and preservation proof are in this review's evidence directory.
