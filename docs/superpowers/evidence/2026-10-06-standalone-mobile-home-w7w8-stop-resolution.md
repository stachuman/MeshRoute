<!-- Author: Codex (independent QA checkpoint disposition); Claude owns the implementation; the owner rules product policy and commits. -->
# W7+W8 checkpoint return — five unusable mutants

2026-10-06. **Option A APPROVED: scoped test repair, followed by the complete final chain. No implementation freeze or software QA PASS exists.** The shared tests and production code were not edited by QA.

## 1. Authorized checkpoint return

The original [revision-2 brief](../plans/2026-10-04-standalone-mobile-home-w7w8-editor-rename-messages.md) remains untouched at **`47cb5a461d07cc3cbaf0ab6ae81fe535e206eda996201885f4dbe878dd11cb8e`**. This sealed QA return is its narrowly scoped checkpoint supplement, not a new product ruling or permission to rewrite the feature. The resume authorization consists of that brief **plus this return's SHA-256**, issued with the handoff. Preserve the candidate at HEAD **`4c1a000bc71706ac438e64161a9452966a66615f`** and the [checkpoint inventory](2026-10-06-standalone-mobile-home-w7w8-stop-resolution/inputs.json), including untracked implementation. Simulator **`6585649ea5a780f0542b2931853a667be56a5b2b`** remains clean/read-only. No commit is required.

Exact fence delta:

1. Add **`test/test_firmware_ui_presets.cpp`**, starting SHA-256 **`8811f177de4d94467505758a1b09f294e5f30283f8387865e3ce95e211831c03`**, for **exactly two null-safe comparison guards**: the emergency-default pointer in the projection/default relation, and the old-v1 boot diagnostic comparison. No other edit to this newly fenced file.
2. In the already fenced **`test/test_firmware_ui_model.cpp`**, guard the empty-compose diagnostic comparison. Preserve its preceding non-null assertion and every other assertion.
3. Implement exactly the three short-circuit comparisons in [guards.patch](2026-10-06-standalone-mobile-home-w7w8-stop-resolution/guards.patch), also recorded in [proposed-guards.json](2026-10-06-standalone-mobile-home-w7w8-stop-resolution/proposed-guards.json). The doubled parentheses preserve ordinary C++ short-circuit evaluation inside CHECK. No REQUIRE/exception dependency, assertion deletion, new fallback, helper, case, mutation exemption or verdict relaxation.
4. Repair the evidence-only `run-final.sh::step` error propagation (B499) before using it. This helper already belongs to the report/evidence work. Record each command's exit, then terminate nonzero on a failed step, with neither later work nor DONE. Add a small synthetic fault/healthy proof against the real helper; bypassing/removing the stop must make that proof fail. Do not run firmware gates in that synthetic test.
5. Update the coder's evidence-only scope checker for these **named** deltas: the new test path at its checkpoint hash, this QA receipt/directory as explained inputs, and the register's authorized new hash below. Preserve the original preflight and baseline evidence. Do not relax unrelated path or preparation checks.

The register alone changed among existing preparation inputs, as the checkpoint ledger/finding record permitted by P4. Its authorized SHA-256 is **`5ad4e8acb1e9a686d268453018a6e7824a450d4d6a2484aaa2ecb60822d0c835`**. Design, tracker, MEMORY, the original brief, and all previous receipts/seals stay unchanged. Verify this supplement and the checkpoint inventory at resume; no silent refresh of a production fingerprint.

**Gate delta:** none relaxed. Apply the guards and helper repair, derive the unchanged healthy native counts, rerun the five entries on the final candidate, then run the **entire §4.1 final chain fresh on one freeze**, including all **16/674** configured union entries (or a separately justified, derived count if further authorized entries are added). Every entry must give its required RED verdict; zero unusable, vacuous, missing or crashed entries. Do not credit the development union or these checkpoint experiments as final-chain results. Preserve the coder's existing baselines; the guards do not change healthy outcomes or counts. Final evidence still needs the exact PIN re-synced? YES derivation and a final frozen inventory.

## 2. Independent reproduction of B498

QA first copied **all 2,569 nonignored tracked/untracked checkpoint paths** to an isolated candidate, including both new editor files. The separate base was the owner commit plus the independently reconciled bounded-F07 input; its harness hash exactly matches the preflight **`27807b32…177d`**. Shared source/build directories were not used for mutation or compilation.

The reproduction extracts only the real harness's `SuiteOutput`, `SuiteFailure` and `run_suite` definitions by AST; it never imports/executes the mutation orchestrator. The actual function builds native with `pio test -e native --without-testing` and executes the binary. Native child exits and both raw native streams are retained. Each mutation's unchanged configured pattern matched once. [Reproducer](2026-10-06-standalone-mobile-home-w7w8-stop-resolution/reproduce.py), [results](2026-10-06-standalone-mobile-home-w7w8-stop-resolution/results.json), [copy proof](2026-10-06-standalone-mobile-home-w7w8-stop-resolution/copy-proof.json).

| Entry | Untouched base | Base with the three guards |
| --- | --- | --- |
| model Y02 | SIGSEGV, exit -11, UNUSABLE | exit 1, **157 failed assertions**, RED |
| model Y07 | SIGSEGV, exit -11, UNUSABLE | exit 1, **3 failed assertions**, RED |
| uipresets U13 | SIGSEGV, exit -11, UNUSABLE | exit 1, **163 failed assertions**, RED |
| uipresets W6-P1 | SIGSEGV, exit -11, UNUSABLE | exit 1, **7 failed assertions**, RED |
| uipresets W6-P3 | SIGSEGV, exit -11, UNUSABLE | exit 1, **10 failed assertions**, RED |

The clean base independently gives **3059 cases / 199638 assertions / 0 failures** both before and after the guards; its raw successful output is byte-identical. The similarly guarded preserved candidate independently gives **3123 / 203168 / 0**. Guard changes preserve all clean assertions and their counts. Mutated assertion totals can vary with the changed execution path; only the derived **clean worker baseline** must equal the frozen native floor.

CHECK does not abort after its failure. The next unguarded strcmp is therefore unsafe when these valid mutation shapes supply null. The fix preserves the comparison whenever the pointer is valid and explicitly fails when it is null. It repairs the test, not the production behavior or B490's correct rejection of a crashing child. For U13, the null argument is the **expected compiled-default pointer**, not the destination text array. For W6-P3 the second boot observes the mutant's successful replacement store, so the boot line becomes null; the guard permits the zero-write and state assertions to finish failing normally.

The five RED assertion counts above are measurements on the base, **not new fixed-count acceptance pins for the feature**. The coder and independent final gate derive the candidate's counts.

## 3. Other checkpoint dispositions

**W8-M04:** the replacement is accepted as a meaningful, memory-safe alarm-state control. On the guarded candidate QA reproduced **exit 1, one failed assertion**, precisely `CHECK(m.emergency() == Emergency::firing)` in the `w8-withdraw` case (`test_firmware_ui_send.cpp:3697`). The previous pending-flag deletion was masked by the subsequent emergency queue operation; the new tuple does not claim to be a direct mutation of that request flag. Request isolation and emergency-first draining remain separate native obligations. No control contract is waived.

**B499:** a labelled synthetic execution of the actual final runner's `step()` with false, then true, records TSV exits **1,0**, runs both, prints DONE and returns **0**. It cannot by itself signal chain failure. QA did not run or falsely credit the whole chain. A scratch-only stop-after-recording form instead gives fault **exit 1**, no continuation/banner, and healthy **exit 0** with both steps/DONE. [Original fault](2026-10-06-standalone-mobile-home-w7w8-stop-resolution/runner-fault.json), [scratch corrected proof](2026-10-06-standalone-mobile-home-w7w8-stop-resolution/runner-fixed-proof.json). The coder repairs its own helper; QA has not edited it.

**M52, W8-M05, W8-S12 and the extra timing cases:** preserved, not independently gated in this scoped return. They still belong to the fresh full chain. This return does not inherit their reported RED figures or the reported probe/ABI figures.

**Four older probe checks:** recorded separately as **B500**, nonblocking. The [checkpoint attribution](2026-10-06-standalone-mobile-home-w7w8-stop-resolution/coder-probe-attribution.json) is the coder's measurement, not a new QA replay. It correctly distinguishes incidental earlier failures and an inferred P29 cause from a proven direct control. Existing controls must retain their actual declared properties/outcomes at the final gate. Loss of incidental failures alone does not justify retiring assertions or weakening controls. No new full attribution or product defect is claimed here.

## 4. Register, preservation and limits

Registered **B498** (the three unsafe comparisons/five unusable entries), **B499** (final evidence-runner error propagation), and **B500** (reported probe negative space); next free is **B501**. All remain open pending their stated closure proofs. The register's §0 is rewritten in place to give this scoped return, not a software PASS. No product policy, allocation or owner ruling changed.

Only that permitted register update and this QA receipt/evidence were written in the shared checkout. Every production, test, tool and previous evidence input is preserved, as are the simulator, brief, design, tracker and MEMORY. Nothing was staged, committed, reset, cleaned or removed. [Preservation proof](2026-10-06-standalone-mobile-home-w7w8-stop-resolution/preservation.json).

This is a **checkpoint fix-return authorization**. No full implementation gate, corpus, board firmware measurement, full probe run, ABI run, tools discovery, warning census or full union was performed here. QA performed the fourteen scoped native experiments and two synthetic runner proofs; the coder still owes the final chain and freeze, followed by independent QA under §4.2.

Raw native streams are local under ignored `artifacts/2026-10-06-standalone-mobile-home-w7w8-stop-resolution/`, hashed by [raw-files.json](2026-10-06-standalone-mobile-home-w7w8-stop-resolution/raw-files.json). Durable evidence seal: [SHA256SUMS](2026-10-06-standalone-mobile-home-w7w8-stop-resolution/SHA256SUMS).
