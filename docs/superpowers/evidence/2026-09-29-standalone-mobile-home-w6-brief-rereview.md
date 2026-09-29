<!-- Author: Codex, independent QA; scoped brief re-review, no implementation -->
# W6 brief revision 2 — independent scoped re-review

**Verdict: PASS — authorized for coder source-validation and implementation as written.** W6R-1, W6R-2 and W6R-3 are resolved as brief corrections. No further fold-in or owner ruling is required.

Authorized [brief](../plans/2026-09-29-standalone-mobile-home-w6-phrases.md): **revision 2, 692 lines**, SHA-256:

```text
cdcef1ee1826c14a8e941e05ef4b4bfdb8e7004926cfd22f7e2a44025716828e
```

Base: MeshRoute **`70ff486b40c9b07001b33bcbcdf640ad494c1149`**; simulator **`6585649ea5a780f0542b2931853a667be56a5b2b`**, clean. No commit is needed before the coder starts. This receipt carries the approval: **leave the brief header and all pinned preparation files unchanged**, including status pointers. Editing a status line would change the authorized input.

## 1. Pins and preservation

- Independently verified **32 full table hashes and 26 line counts**, plus both read-only deferred-action inputs. Their abbreviated hashes in §1 resolve to the full hashes in the sealed pre-check inventory; both match.
- Both repository commits match. The index is empty; the simulator is clean.
- Verified the pre-check's **45/45** checksum entries and the first review's **14/14**, including their receipts and inventories.
- Against the pre-check inventory, only the four declared preparation documents differ: design, register, tracker and MEMORY. All production, test and tool inputs remain unchanged.
- Against the first review's entry inventory, only the brief, design and register differ. Tracker and MEMORY are unchanged. The register comparison includes QA's original B477 addition and the author's subsequent refresh; it is not a claim that QA's entry inventory already contained B477.
- All seven Markdown links in the brief resolve. New paths are the already declared pre-check/review artefacts and this re-review's own receipt/evidence.

Evidence: [verification.json](2026-09-29-standalone-mobile-home-w6-brief-rereview/verification.json), [input inventory](2026-09-29-standalone-mobile-home-w6-brief-rereview/inputs.json), [source checks](2026-09-29-standalone-mobile-home-w6-brief-rereview/source-checks.json), and [final preservation check](2026-09-29-standalone-mobile-home-w6-brief-rereview/preservation.json). The receipt and evidence are sealed by the folder's `SHA256SUMS`.

## 2. Findings resolved

| Finding | Verdict | Independently checked resolution |
| --- | --- | --- |
| **W6R-1 — fake capacity, persistence and dependent probe** | **Resolved** | §1 pins `Preferences.h`; §2.8 explicitly fences capacity and truthful write results, preserves the two deliberate dishonest modes, requires exact-capacity/over-capacity regressions, and proves both the exact 2852-byte medium contents and a real boot-restore reload after a routed long set. §§2.9/3/4 carry the same scope. The deferred-actions builder and included fixture are explicitly protected, and its default run is required at baseline and in both final gates. |
| **W6R-2 — reply maximum and page coverage** | **Resolved** | §2.2 says **1517 bytes** for a ten-digit resulting generation and **1508** for one digit. §2.8 requires the ten-digit and wrap cases, exactly-once slot coverage across all pages, and generation-change/equality cases. The bounded interval makes page 5 exactly `channel8`. The design's erratum and B475 now agree. These values match the hash-verified independent measurement from the first review. |
| **W6R-3 — comment fence** | **Resolved** | §2.2 permits the specifically named reason-count, read-state and catalog-size comment corrections; §3 repeats that permission. The 1915-line constraint, board-UI anchors, command inventory and historical stack qualification remain. No executable command-glue change is authorized beyond the explicitly permitted usage-string text. |

For W6R-1, I re-read the real fake, its C12/C13 controls, the router/store adapters and the deferred-actions builder. **Twenty focused source/anchor checks pass.** A fresh disposable host replay of the unchanged fake again reports 2305/2852 bytes written without a retained record; the 2304-byte control stores correctly. This confirms B477 still needs implementation, not that W6 has already fixed it.

The ordinary fake's honest write-result requirement is read with the explicit exception for armed `retain_on_fail` and `drop_on_ok` fault controls. Their deliberate dishonesty and must-fail properties are preserved. Capacity rejection must not be disguised by a successful output line. Enlarging this host fake does not add device-resident state or reopen D15.

The reusable boundary is still exactly one `rc=0\nif ! build_support;`; `build_support` and `build_variant` exist, and the deferred-action fixture still includes the inbox probe. The revised fence allows the producer-side work needed for the new OLED arm and makes any deferred-action source repair a STOP-1. Its inclusion in both gates closes the omitted-reader problem under D6/P7.

## 3. Other fold-ins and scope

The previously accepted contract remains: D14's stateless four-record pages; D15's exact allocation; full review before queueing; separate review/pending bindings; shared page lifetime; generation/team/peer re-gating; the one static send-line owner; the closed per-assertion ledger; and P6's separate coder/QA chains.

The minor clarifications are present: the actual router order, a labelled synthetic known-zero-hash test without a resolver/core change, fixture-qualified emergency lengths, and preservation of earlier packages' independent properties subject to the explicit W6 expectation ledger. The scoped fence, persistence proofs, gates and STOP list agree. No remaining dispatch-blocking contradiction was found.

I checked the complete current brief and the earlier report's required changes. The first review preserves revision 1's hash and findings, not a separate full copy of its text; this receipt therefore does **not** claim an independently reproduced byte-for-byte revision-1-to-2 textual diff.

No native suite, corpus, full probe, ABI/board build, mutation union, discovery, warning census, stack measurement or metal test was rerun for this scoped **brief review**. Historical measurements were checksum-verified; the only fresh compilation was the external, unmodified-fake reproduction. The implementation must still satisfy every gate in §4 and return its own frozen receipt.

## 4. Handoff

- The owner may pass the exact authorized hash to the separate coder. The coder first performs §1's source-validation and inventories this receipt as an explained QA addition, then implements within §3 and freezes after §4.1.
- **B335, B475 and B477 remain OPEN** until implementation and independent QA pass. B476 remains separate. No new finding was registered; next free remains B478.
- Only this new receipt and its evidence folder were written. The reviewed inputs, prior evidence, register, status pointers, production, tests, tools and simulator are preserved. Nothing is staged or committed.
