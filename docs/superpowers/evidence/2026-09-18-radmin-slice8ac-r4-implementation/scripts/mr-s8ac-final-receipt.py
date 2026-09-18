from pathlib import Path
import json
r=Path('/home/staszek/MeshRoute');q=Path('/tmp/mr-s8ac-r4-b1edsao2');s=json.loads((q/'final-gate-summary.json').read_text());assert s['tools_tests']==351 and s['mutation_red']==917
assert json.loads((q/'corpus-byte-comparison.json').read_text())['byte_identical']==36
p=r/'docs/superpowers/evidence/2026-09-17-radmin-slice8ac.md';body=p.read_text();assert '\n## 7. Revision 4 final coder freeze' not in body
body+='''

## 7. Revision 4 final coder freeze (2026-09-18)

**Implementation frozen for independent QA.** The final code/test/tool inputs match the complete isolated
candidate used below. The only proposed semantic documentation delta in that candidate is the four-row
R-RA-42 authority-table transcription reserved to QA by brief §8 R2; its patch is supplied and remains
unapplied in the shared checkout. The generated 208-row inventory has been copied back. Accordingly,
three-artifact agreement is proven on that candidate, and requires QA's table landing in the shared tree.
This is a coder receipt, not independent QA PASS; B292/B312 remain QA-owned closures.

The source base, admission HEAD, revision-4 brief hash and six QA preparation hashes are exactly those in
§6. The brief, ledger, register, design, tracker and MEMORY preparation inputs were not edited. No ruling,
commit or allocation request remains open. All work remains uncommitted; simulator HEAD is still
`06746a97de5764415d6fcef10b97bca90569b9c7`, clean.

### 7.1 Final measured gates

| Instrument | Final result |
| --- | --- |
| Fresh base native wrapper + actual binary | 2931 cases / 189998 assertions / 0 failed / 0 skipped |
| Fresh final native wrapper + actual binary | **2947 cases / 193734 assertions / 0 failed / 0 skipped** |
| Independent reference | **94/94** strict arrays; old 89 unchanged; five comparator controls RED; separately executed one-byte corruption rejected |
| Controller key proofs | Independent hashlib/PyNaCl KAT matches public keys, base/session keys and sealed request; all eight canonical low-order peers: **264 checks / 0 failed**, compiled false-success control RED |
| Simulator and corpus | Fresh normal/gateway compile/link for base and final; both sets **36/36** anchors, validated; direct `cmp` proves all 36 actual streams byte-identical |
| s18 | **32afbf11e43b4bf9d0bd470ad502ba0a**, 269517 events, zero assertions |
| ABI | **290 checks / 9 controls RED**; B278 **42 measurements / 6 controls RED**; no unusable controls |
| Console sink | 720 executable, 83 structural, 905 BLE guard and 6 ownership checks; **149 controls**, no unusable |
| Inbox verbs | ACCEPT **1374 / 60 controls**; CLIENT **439 / 64 controls**; explicit separately compiled CLIENT arm also passes |
| Firmware UI | 433 / 868 / 433 across the three arms; **223 controls**, no unusable |
| Custody USB / BLE line / features | **27/10**, **55/12**, **121/62** positive/control pins respectively; all controls usable |
| Deferred actions | Remote **416/518/534/464**, radio **3160/3485/3689/3695**; existing local **150/151/158/158**, 39 transcripts per arm; **40 compiled assertion controls RED**, source/placement controls preserved |
| Probe invocation coverage | Every standing probe and deferred-actions probe ran default and `--no-neg`; default runs supply the control evidence |
| Full tools discovery | **351 tests, OK, zero skips**, using measured real ELFs in the isolated `.pio-measure` tree |
| Inventory / authority | Generated **208 rows**; write/bare/check pass; candidate ruled table/header/inventory agree; **6/6 authority controls RED** |
| A0 / literals / whitespace | All pass; both repositories checked; simulator source unchanged |
| Warning census | Six environments at **173/178/177/177/182/182**, zero `-Wswitch`; no `-Wreorder` warning or suppression |
| Mutation union | **59 batteries / 918 configured: 917 RED, 1 known unusable B342, zero vacuous**; all final worker baselines 2947/193734/0 |

The canonical corpus comparator correctly refuses the different base/final `lus` identities. Its refusal,
both validated manifests and the separate byte comparison are archived; neither manifest was rewritten.
The final simulator build uses the disclosed external CMake source-list hook from §6, with both actual
controller/codec variants compiled. No simulator source file was changed.

The union is the measured S ∪ H, extending the 56-battery/880-entry floor with `radmin8client` (29),
`radmin8verbs` (8) and `radmin8rng` (1). B342 is specifically `sliceBmac` M04: it compiles but the suite stays
GREEN, so that battery returns 1 and the control is **not** counted RED. All other selected batteries return 0.
The archived union driver's final aggregate assertion reports this known nonzero result; the explicit
reconciliation accepts only that named standing exception. No failed new control is hidden by the exception.

PIN re-synced? YES — measured base 2931 cases / 189998 assertions + 16 cases / 3736 assertions = final 2947 cases / 193734 assertions; zero failed and zero skipped.

### 7.2 Final allocation and linked measurements

The pointer-free controller block remains **4512 bytes / alignment 8** on all three ABIs. Node is
**235248 native / 122176 mobile / 157344 gateway**, with the controller member absent on gateway.
No owned state beyond R-RA-45 was added. Transient identity, key and delivery adapters remain call-scoped;
no worst-case stack-depth or metal reliability claim is made by the resident-state measurements.

| Deterministic board pair | Base RAM | Final RAM | Delta | Base flash | Final flash | Delta |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Gateway | 204036 | **204036** | **0** | 574752 | **574896** | **+144** |
| Heltec mobile | 207756 | **211772** | **+4016** | 1373604 | **1392832** | **+19228** |

Gateway then mobile ran sequentially with fixed identity/stamp in isolated measurement paths. Final
`xiao_mobile` is **176604 RAM / 696940 flash**. Section/symbol attribution still gives mobile Node +4264,
removed legacy `ri` −245 and linker padding −3 = **+4016**. The two existing 512-byte dispatch buffers have
changed symbol names only; their count and sizes are preserved. Gateway resident-object net change is zero.
Final board inputs match all 141 production source/header files and `platformio.ini` in the shared tree.
The earlier §6 flash values are superseded; pristine base/final ELF hashes and raw measurements are retained.

### 7.3 Self-review corrections and failed attempts

Two recovery defects in the in-progress implementation were fixed before this final freeze, without
changing the allocation. Explicit `remote-result show` now adopts the caller's transport, and the assembly
fallback preserves completed local acceptance while marking reconnect re-offer separately. Three compiled
controls (C25–C27) each go RED. A full eight-row ACK debt table now permits safe/force session controls;
only authenticated execution reserves transcript ACK debt. Session controls reject execution-terminal
responses. C28/C29 go RED, with 26/24 failing assertions respectively. These are corrected implementation
findings for QA's register landing, not new requested policy or silently added resident state.

The real two-Node proof now uses selected credential `key4` against target slot 3, while the ordinary
messaging identity is different. It exercises real target admission/codec/MAC and real controller MAC intake
for every response domain; labelled synthetic pressure/tombstone fixtures remain explicit. The final five
new recovery controls run in the full union, not just filtered tests.

Five inventory-test expectations still described 204 rows, 48 mobile-OLED primary verbs and two CLIENT
families. They failed against the four new forms. The tests now require 208 rows, 52 primary verbs and the
exact six-family set, preserving serial-only key stores and serial/BLE controller verbs. All **78** focused
inventory tests and then all **351** discovered tool tests pass. No test or expected authority obligation was
removed. One diagnostic was accidentally run against the shared tree before QA's table landing and correctly
reported unclassified controller rows; that output is historical, not passing evidence.

The first full-tools attempt was interrupted after those stale expectations were identified; its exit −15
and log are retained. The final discovery rerun is complete and clean. A missing-real-ELF skip in that earlier
attempt was eliminated by supplying the actual measured final board ELFs, not by altering the skip/test logic.
The earlier source-reader repairs, scheduled-detail coverage gap, first pair's preservation rejection and
incorrect first reference nonce are preserved in §6/development logs. The final independent reference was
reproduced in a private PyNaCl 1.5.0 environment; system Python lacked that dependency, so its import failure
was not counted as a run.

B350 remains a known wording limitation: firmware-UI `--no-neg` prints bare PASS with zero controls; only
its controlled default run is used as gate evidence. B315/B359/B364 remain existing separate instrument or
fixture limits. No new claim is made about physical BLE, flash wear, reset delivery or radio reliability.

### 7.4 Frozen handoff and QA boundary

[Freeze metadata](2026-09-18-radmin-slice8ac-r4-implementation/freeze.json) pins the final receipt and
[complete input inventory](2026-09-18-radmin-slice8ac-r4-implementation/freeze-inputs.json).
[Code input hashes](2026-09-18-radmin-slice8ac-r4-implementation/freeze-code-inputs.json) and
[parity proof](2026-09-18-radmin-slice8ac-r4-implementation/freeze-code-parity.json) distinguish the actual
uncommitted source/test/tools from HEAD alone. The archive README links commands, logs, mutation selectors,
reference reproductions, board manifests/sections/symbols and both corpus manifests. All 2327 preparation
paths survive; the six QA documents remain hash-identical.

Before its independent gate, QA lands the supplied R-RA-42 table patch (the sole candidate semantic-doc delta).
The board carrier remains explicitly unavailable until 8b; the real command path refuses without transmitting.
No on-air controller or physical-device PASS is claimed. QA retains register/design/bench landing, Part 57d
and B292/B312 closure. Coder production/test/tool inputs are frozen from here; nothing was staged or committed.
'''
p.write_text(body);print('Appended final coder freeze receipt §7')
