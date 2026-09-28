# B459 revision 2 — independent scoped brief re-review

**2026-09-28 — QA verdict: PASS.** B459R-1, B459R-2 and B459R-3 are resolved. This authorizes coder source-validation and implementation under the approved brief; it is not an implementation gate result.

## Authorized input

- Brief: [2026-09-28-b459-board-ui-accounting.md](../plans/2026-09-28-b459-board-ui-accounting.md), revision 2, 545 lines.
- **Authorized SHA-256: `443b95ea697353d36ef595975a002c1c1b3335dcb21cbe46886aea500d57de5a`.**
- MeshRoute base: `8079e545fb2e1065c2bbc347eb9a66160f6cbf8e` plus the uncommitted, QA-passed W2 candidate and inventoried preparation documents. HEAD alone is not the reviewed input.
- Simulator: `6585649ea5a780f0542b2931853a667be56a5b2b`, clean and unchanged.
- Entry inventory: [inputs.json](2026-09-28-b459-brief-rereview/inputs.json). The coder must reconcile any subsequently added QA evidence when making its own frozen inventory.

The approved file and its pinned preparation inputs were left untouched. Its review-status header does not need editing: this receipt carries the approval, preserving the authorized hash (P4). No commit is a prerequisite.

## Scoped findings

| Finding | Disposition | Verified correction |
| --- | --- | --- |
| B459R-1 — increment 1 discovery | **Resolved** | Section 4.1 now requires full tools discovery on increment 1's own candidate, after extraction and before its freeze: 375 tests expected, OK, zero skips and no import failure. The run is bound to its three source-file hashes, and first-freeze evidence stays separate from the final candidate's evidence. Final-chain discovery remains required. |
| B459R-2 — conflicting test/evidence fences | **Resolved** | The input table, section 2.1, section 3 and STOP list consistently permit only temporary-copy library resolution changes in the B456 test. `ACCOUNTING_STATEMENT` stays byte-identical and occurs exactly once; the adapter retains its name/signature. No assertion is weakened or test retired. Report/evidence are explicitly permitted in both increments and excluded from the three-source-file comparison. |
| B459R-3 — ambiguous paired removal | **Resolved** | Section 2.2.7 now removes the source call and declaration while keeping the checked-in manifest intact; the census must reject that mismatch. Removing both source and manifest remains the explicitly stated review-time limitation in section 2.2.1. |

The existing [revision-1 review](2026-09-28-b459-brief-review.md) dispositions otherwise stand. The checked-in manifest, static declaration census, 592 full-run identities, 529 active/63 skipped diagnostic identities, outcome contracts, two-increment separation, B461 fold-in and focused gates are unchanged. No new owner ruling is needed.

## Verification and preservation

- Independently reversed only the declared edits and reproduced revision 1's exact SHA-256, `f485c49427838f1480fb5a8b32a0dd454f3e88a4c7eab65b54aaeeb46c7e1453`. This proves the revision contains no additional brief delta. See [delta-verification.json](2026-09-28-b459-brief-rereview/delta-verification.json) and [brief-delta.diff](2026-09-28-b459-brief-rereview/brief-delta.diff). The archived revision-1 text in this folder is a reconstruction verified by hash.
- Reversing the register's single dispatch-status replacement reproduced its prior hash. The current register hash is `ebed97ce326b3a0d2be664d76e756388a5d9756462a12b3dbb8703d78f19bab5`; no other register content changed. See [register-delta.json](2026-09-28-b459-brief-rereview/register-delta.json).
- All **11 explicit input hashes** in the brief match. The pre-check's **19/19** and prior review's **14/14** checksum entries verify. Every brief link resolves. See [preflight.json](2026-09-28-b459-brief-rereview/preflight.json).
- Against revision 1's entry inventory, the only changed pre-existing paths are the brief and register. Additions are the prior review's report/evidence. The W2 candidate, source, tools, design, tracker and MEMORY are preserved. The four proposed new tool paths are still absent.
- Source-read the actual B456 test constant and confirmed its exact final-call string occurs once in the current firmware-UI runner: `account_controls "$1" "$2" "$CTL_VERDICTS" "$CTL_GUARDS" "$3" || rc=1`. Retaining that adapter permits the stricter fence without changing the bypass binding (D6).
- Final entry-input preservation, repository state and whitespace checks are recorded in [final-preservation.json](2026-09-28-b459-brief-rereview/final-preservation.json). Review outputs are the only additions made by this re-review; nothing is staged or committed.

No builds, probes, mutation runs or test suites were rerun for this scoped documentation review. Runtime measurements in the prior review and pre-check remain historical evidence, verified here by checksums and unchanged source inputs; they are not claimed as fresh runs. The coder and independent QA must run the approved gates on the implementation.

## Dispatch

The coder may start against the authorized hash above, verify the commit-plus-inventory base, complete and gate increment 1, freeze its three source files, then implement increment 2 without altering those files. Both freezes and the complete final receipt are required by the brief. Any source disagreement or fence violation follows its STOP conditions.

B459 and B461 remain open until implementation passes independent QA. B460 and B462–B473 remain outside this package. No new finding was discovered in this scoped review; next free register number remains B474. No authority/status documents were changed by QA during this approval.

Evidence: [checksummed review folder](2026-09-28-b459-brief-rereview/), including [SHA256SUMS](2026-09-28-b459-brief-rereview/SHA256SUMS).
