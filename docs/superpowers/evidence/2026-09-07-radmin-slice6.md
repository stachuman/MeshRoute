<!-- Coder: OpenAI Codex -->
# Remote-admin v2 Slice 6 — pre-implementation STOP report, 2026-09-07

**Status: STOP before implementation/base measurements.** This is the coder's source preflight required
by the brief's opening paragraph, not a software report, QA verdict or completed implementation. No
production, tests, tools, inventory, ruling, QA brief/ledger, maintained register or simulator file changed.
The only coder-created file is this report. Proposed findings below are for QA's maintained-register
landing under the owner's new roles; the coder does not alter classifications or repair the brief.

## 1. Observed inputs and dispatch prerequisites

- MeshRoute: /home/staszek/MeshRoute, HEAD d226189831420440b2a089e846114ebfbafb1042.
  Initial status contained only the untracked Slice 6 brief. Thus the brief's requirement for an empty
  status was not satisfied; no measured-start baseline was taken.
- Brief: docs/superpowers/plans/2026-09-07-radmin-slice6-dispatcher-authority.md, SHA256
  30ab6f4c7d2309598fbdd7add62d0191c9d8f4c6bdbe51fc9cd55a74358118d2 (344 lines inspected completely).
- Simulator: /home/staszek/lora-universal-simulator, HEAD
  06746a97de5764415d6fcef10b97bca90569b9c7, clean; CMakeLists.txt diff empty. Its Slice 5 commit exists.
  Brief :29–32 still pins 8688884 plus a pending uncommitted change. QA must bind the actual commit;
  the coder may not silently repin or measure around a placeholder (STOP-1).
- Owner has assigned Codex implementation and the other agent specifications/briefs, independent
  implementation Quality Gate and documentation landings; owner retains rulings/commits. The brief :6
  still says model: opus, and docs/2026-09-02-agent-roles.md:8–23 still describes the old division.
  QA should reconcile these preparation documents and their dispatch status with the already-made
  owner decision. This is not a request for a new owner ruling or permission to self-author Slice 6.
- After QA resolves the source disagreements below, the preparation package and a clean measured-start
  arrangement need explicit final base bindings. No implicit dirty-document exception is assumed.

## 2. Findings proposed to QA (M1; next-free number checked as B343)

### B343 — the seam-only validator misses BLE's earlier executing handlers

Brief §§4.1/5 limits validation to exec_console_line and exec_command; fw_main may only build contexts
and name the USB buffer bound. This cannot establish its whole-BLE embedded-NUL refusal promise.
At src/fw_main.cpp:543–547, ble_dispatch_line calls handle_cfg_set directly and returns before the
fallback seam; :563–565 likewise directly execute inbox handlers, and :514–516 calls handle_rcmd.
A source-path witness is a byte span spelling cfg set name X followed by NUL and a suffix: the prefix
matches :543 and handle_cfg_set's name arm (firmware_config.cpp:272–279) uses strlen on the value.
Nothing in the proposed seam validator reaches that earlier call. This is source-derived, NOT an
executed reproduction or a claim of an external exploit.

**Required author resolution:** explicitly place the same validator before every BLE command-owning
branch, define refusal precedence/output, and authorize/prove that actual entry wiring. Preserve valid
local bytes and the existing transport guards. A direct exec_console_line probe alone cannot prove
this BLE path. Expanding fw_main's fence or its instrument is QA's brief decision, not the coder's.

### B344 — semantic authority key collides with legacy surface classification

Brief :179–183 and :218–221 require remote_encode/remote_exec inventory rows to be legacy. But
:198–205 keys the normalized table/header/checker solely by (verb, subverb), one class per key.
The live inventory has status / bare on dispatch (:55), ble_dispatch_line (:219) and remote_encode
(:231); routes likewise occurs at :53/:218/:230, and duty at :26/:207/:228. The ordinary semantic
status/routes rows must be open, while those same keys on remote_encode are required to be legacy.
No one-class table keyed as specified can satisfy both obligations.

**Required author resolution:** specify a source-bound distinction between inventory surface
eligibility and semantic command authority, including inheritance/checker rules and refusal controls.
Do not choose one of the conflicting classes, silently drop historical surfaces or invent an owner
classification. This is a STOP-2 representation conflict, not an objection to R-RA-32/33's policy.

### B345 — peers normalization prediction does not follow the prescribed edits

Inventory :44–46 contains all plus two bare dispatch rows; :213–214 contains two bare BLE rows.
Brief :211–217 removes the dispatch level-guard row but RETAINS the BLE refusal row with a discriminator.
Those described peers edits remove one row and relabel another: from 204, that predicts 203, not 202.
If another row is meant to disappear, the author must name it and preserve its semantic/transport
coverage. Also bind the newly introduced <args> refusal discriminator to the normalized authority
representation; it is not a new executable subcommand to feed to the native lookup test.

**Required author resolution:** correct the predicted arithmetic or specify the second justified
removal, with exact before/after row mapping and controls. No generator was run or edited by this
preflight; 203 is the inference from the written operations, not a measured regenerated inventory.

## 3. Handoff and verification status

STOP-1 covers the dispatch inputs and source/brief disagreements; B344 additionally invokes STOP-2.
QA authors the corrections and records these findings in the maintained register; the owner commits
preparation. Codex resumes against the corrected, explicitly pinned clean-start contract.
Native, corpus, probes, mutations, boards and tool sweep were NOT run. No Slice 5 figure has been
relabelled as a coder-derived Slice 6 baseline, and no software PASS or PIN re-sync is claimed.

## 4. Revision 2 follow-up preflight — clean inputs; new authority conflict (2026-09-07)

The original preflight record above is preserved. B343–B345's author fold-ins and the new roles are
present. Measured-start input checks, before this report append:

- MeshRoute HEAD f948c2558bcdb03934a3fc73b8c6ee7f61f88772, clean. The successor diff against d1a2906
  names only the brief; it contains the dispatch pin and the requested B346/six-selftests wording fixes.
- Simulator HEAD 06746a97de5764415d6fcef10b97bca90569b9c7, clean. MESHROUTE_DIR in build/CMakeCache.txt
  points to /home/staszek/lora-universal-simulator/../MeshRoute, the intended checkout.
- Current brief SHA256 c964a0731b4fbe79d756ecc2d6e619c898b82221ebdc0b11b65759803c89f180.

**STOP-2 before baseline runs or implementation: proposed B346 — acl list test classification contradicts
the owner's remote-authority ruling.** This emerged while reading the complete ruled classification
for its normalized transcription, not from a runtime measurement.

| Authority | Required behavior |
| --- | --- |
| R-RA-33, rulings ledger :743–753, judgment call 8 | acl list and admin-id show are owner when remote, physical when local |
| Ruled classification proposal :113–117 | acl list/add/set/remove admit remote owner; acl reset and admin-id generate/rotate/reset remain physical; admin-id show is owner |
| Slice 6 revision 2 brief :331 | the native table test must classify acl list as physical |
| Same brief :343–344 | the real-seam probe must refuse acl list at ALL three remote authorities |

Those test expectations cannot coexist with the ruled semantic table. R-RA-29's existing local BLE
whole-family refusal does not prohibit the separately ruled remote-owner access; the brief itself
keeps local contexts off the authority table and preserves that BLE guard.

**Required author correction, not a new product ruling:** align the table examples and probe expectations
with R-RA-33. Use acl list as owner-only remotely (open/operator refused, owner admitted); a genuinely
physical operation such as acl reset confirm can retain the all-remote-refused control. Check the
sibling admin-id show and owner ACL mutation rows against the same approved table. Do not widen local
BLE or implement future remote-only execution semantics outside the slice's fence.

QA owns the brief correction and maintained-register landing for B346. The coder has not silently
chosen between the conflicting obligations, changed production/tests/tools, committed, or run a software
gate. Only this evidence append changed the tree. Baseline collection and implementation remain pending
the corrected dispatch input contract.

## 5. Implementation start and predictions — 2026-09-07

The owner instructed the coder to “ignore commit status”. This supersedes the old pin-only-successor
and clean-status dispatch blocker, not the source fence or verification requirements. Actual input
revisions are MeshRoute `356dad7f2a16cbc9afb6ae74e35b936ca45f76e8` and simulator
`06746a97de5764415d6fcef10b97bca90569b9c7`. B343–B346 are folded into the current brief;
the older STOP sections above are historical. No implementation or baseline result is claimed yet.

Predictions recorded before measurement: board RAM unchanged on both ruled builds; flash growth
attributed to validator, table, seam and BLE-head code; Node ABI unchanged; simulator no-op build
and identical executable; all 36 anchored corpus streams unchanged. Inventory 204 → 203 through
one removed peers level-guard row and one relabelled BLE refusal discriminator; help remains 53.
Native case/assertion, tools-test and console/inbox probe increments will be derived from executed
new cases and controls, not fitted to expectations. All other standing pins should remain fixed.

Artifacts for this run: `/tmp/mr-s6-NPTViQ` (logs/corpus), with deterministic board captures beneath
the measurement runner's required `.pio-measure` root. Evidence and source stay frozen during each
board capture. QA retains the independent verdict and documentation landings; no commits by coder.

## 6. Baseline measurements completed before source edits

- Native build and real executable: both exit 0; **2825 cases / 119784 assertions / 0 failures**.
  Logs: `base-native-build.log`, `base-native.log` under the artifact directory above.
- Ruled board pair, runner `pair --jobs 2`, exit 0: gateway **RAM 197180 / flash 544668 / 285 objects**;
  heltec_mobile **207748 / 1367444 / 329**. Fixed identity `Jan 1 2000 00:00:00`, git `b206b206b206`.
  Manifests: `.pio-measure/s6-base-pair/{gateway,heltec_mobile}/manifest.json`; no source or evidence
  edit occurred during the capture. Both reproduce the brief's baseline pins.
- Simulator checked no-op build, exit 0: no compilation/link actions; executable MD5 before and after
  **db6582a171a6d785720e324c2cfe43f1**. Corpus runner exit 0: **36/36 streams, 36/36 anchors**;
  s18 **32afbf11 / 269517 / 0**, compared with the current `simulation/BASELINE.md` authority.
- Console-sink base probe: **profiles=6 checks=720 structural=52 ble_guard=905 ownership=6
  ownership_controls=3 controls=101 unusable_controls=0**, source hashes restored.

These are baseline results, not implementation verification. The other already-started baseline
instruments are accounted for below when they complete; no final gate or PIN re-sync is claimed.

## 7. Proposed B347 — the new semantic duplicate rule also rejects the two joinprofile arms

**STOP-2: source/brief disagreement, unrelated to commit status.** Brief §4.5 requires rejection of
equal `(surface, verb, subverb)` regardless of source location and prescribes only the peers merge
and BLE refusal discriminator. The live generator reports THREE colliding groups, not two:

| surface | semantic cells | source arms | gates |
| --- | --- | --- | --- |
| `src/firmware_commands.cpp::dispatch` | `joinprofile`, `—` | `src/firmware_commands.cpp:1454` and `:1468` | `MR_N_LAYERS < 2` and `!(MR_N_LAYERS < 2)` |
| `src/firmware_commands.cpp::dispatch` | `peers`, `—` | `src/firmware_commands.cpp:1434` and `:1435` | both ungated |
| `src/fw_main.cpp::ble_dispatch_line` | `peers`, `—` | `src/fw_main.cpp:560` and `:561` | both ungated |

Executed read-only reproduction: import `tools/gen_command_inventory.py`, call `generate(REPO_ROOT)`,
group the returned rows by exactly `(row.surface, row.verb, row.subverb)`, and print groups of size >1.
It returns 204 rows and the three groups above. After the two prescribed peers edits, the joinprofile
collision necessarily remains. Source verification: :1454 executes `handle_joinprofile`; :1468–1470
prints `> err gateway_build (joinprofile is normal-node only)` and returns. They are opposite-profile
execution/refusal arms, not an accidental duplicate handler to delete.

**Recommended QA fold-in:** extend the existing refusal-discriminator mechanism to the gateway-build
joinprofile row, preserve BOTH source anchors and gates, and bind its non-executable discriminator to
the same bare joinprofile authority row. This keeps the predicted 203 rows and the global duplicate
refusal intact. Add a fixture/control for this execution-versus-refusal pair. Alternatively QA must
explicitly authorize and define a checked disjoint-profile exception; the coder has done neither.

No production, test, tool or generated-inventory edit has been made. The coder does not weaken the
duplicate rule, remove a refusal row, change the prediction or edit QA's brief/register. QA owns
B347's registration and the brief correction; owner authorization to ignore commit status remains
honored. This is not a request for another commit or a new product classification.

### Instrument disposition at this STOP

After the completed native, board-pair, simulator/corpus and console-sink measurements above, the
remaining two baseline batches were intentionally cancelled (SIGTERM to their verified process
groups): the inbox/remaining-probes batch and the tools-sweep/checkers batch. Inbox and tools logs
are PARTIAL, not PASS or failed-gate results. Their later chained steps did not run. No ABI probes,
warning census, mutation union or final measurements were started. Those gates remain owed after
the brief correction. Both repositories pass `git diff --check`; simulator status/diff are empty.
Only this coder evidence file is modified. No production implementation exists yet.

## 8. B347 resumed — implementation present; B348 board integration STOP (2026-09-07)

**Current status: implementation INCOMPLETE / NOT READY FOR QA SOFTWARE PASS.** Sections 1–7 remain
the historical preflight record. The owner relayed QA's B347 fold-in; the preparation brief and register
are concurrent QA-owned Markdown edits, preserved. Commit status is not a blocker. The actual MeshRoute
revision remains `356dad7f2a16cbc9afb6ae74e35b936ca45f76e8`; simulator remains
`06746a97de5764415d6fcef10b97bca90569b9c7`, unchanged. No coder commit or QA-document edit.

### 8.1 Resumed baseline accounting

The completed §6 captures remain valid: no existing source changed between those captures and resumption.
Resumed baseline artifacts under `/tmp/mr-s6-NPTViQ`:

| instrument | measured result | log |
| --- | --- | --- |
| inbox verbs | accept 180/30, client 178/33, zero unusable | `base2-inbox_verbs.log` |
| firmware UI | 223 controls, zero unusable | `base2-firmware_ui.log` |
| custody USB / BLE line | 27/10 and 40/8 | `base2-custody_usb.log`, `base2-ble_line.log` |
| tools sweep | 329 tests, OK | `base2-tools.log` |
| inventory | bare and check forms pass, 204 rows | `base2-inventory.log` |
| a0 and DATA-type checkers | both pass | `base2-a0.log`, `base2-datatypes.log` |
| board ABI / B278 ABI | 191 checks + 9 controls; 42 measurements + 6 controls | `base2-board-abi.log`, `base2-b278-abi.log` |
| warning census | all six original warning pins, zero switch warnings | `base2-warning-census.log` |
| dependency mutation selector | 86 RED, zero unusable: codec 66 + sliceAjson 1 + sliceDack 1 + sliceGjson 16 + b134ack 2 | `base2-mutation-*.log` |

**Measurement mistake, not hidden:** `base2-features.log` failed the ownership runner's whole-source
hash guard (39 ownership controls verified, 1 unusable; top-level 58/2) because the coder added initially
unconsumed headers/tests during the run. Although the consumer census did not move, the run is INVALID.
No check was weakened. With edits frozen, `base3-features.log` exited 0: 9 cells, 120 checks, 59 controls,
zero unusable; the nine-file/40-control ownership contract holds. The base firmware-UI run still read
its unchanged input files; no existing firmware header/source was changed until all these baseline
runs completed. The dependency batteries ran in `/tmp/mr-s6-base-mutations-CnYquB`, never in the measured
checkout, with two workers and source restoration verified.

### 8.2 Implemented shape (not a completed gate)

- Shared allocation-free validator in `lib/console/console_line.h`: length first, then first NUL/CR/LF;
  USB 1023 and remote 201 named bounds. Native test binds 201 to the live codec's 226 minus 25.
- Required typed context and result fields on `exec_console_line`; local contexts never consult policy.
  Local validation writes exactly `> err bad_line <reason>\n` or the existing `write_err` NDJSON envelope
  **including its trailing LF**. All remote refusals write no envelope. Panel validation is on its real
  `exec_command` path. No scheduled/internal-failure producer, remote consumer, wire/NV or core edit.
- 179 semantic policy rows, transcribed with row references to the ruled proposal; aliases retain their
  inventory cells. Owner/physical dual readings follow B346. Lookup matches the longest verb/subcommand,
  and conservatively recognizes subcommand prefixes: `handle_team`'s real `mint_form` at
  `firmware_config.cpp:2041` accepts a `new` prefix, and its grant/forget arms also use prefixes. Such
  spellings must not downgrade to the weaker bare-team operator row. Tests pin this as authorization,
  **not** a claim every malformed suffix executes successfully. No handler was altered.
- Generator: the peers level guard is removed; BLE peers and gateway joinprofile refusal discriminators
  retain their anchors and map to bare semantic rows. Duplicate key includes gate per B347. Generated
  inventory is **203 rows**, with classes/flags and separate non-target surface marks. Help unchanged.
- New positional three-artifact checker and six actual scratch-file refusal controls. New generator
  fixtures preserve the joinprofile pair and reject equalized gates plus a removed discriminator.
- Console-sink adds S53–S62 and 13 controls. Inbox probe adds 182 checks on each arm and 9 controls;
  it observes the REAL Node::on_command via the existing linker-interposition idiom (not a fake executor),
  separately observing router stubs and NV/store effects. Admission and completion remain distinguished.

### 8.3 Post-change measurements completed so far

| instrument | measured result | artifact |
| --- | --- | --- |
| native build + actual binary | **2839 cases / 121831 assertions / 0 failed** | `final-native-build.log`, `final-native.log` |
| new validator file alone | 6 cases / 793 assertions | `native-consoleline.log` |
| new authority/context file alone | 8 cases / 1254 assertions | `native-authority.log` |
| simulator | checked no-op build, executable MD5 unchanged `db6582a171a6d785720e324c2cfe43f1` | `final-simulator-build.log`, `final-lus-{before,after}.md5` |
| corpus | 36/36 anchors; all streams byte-identical by MD5, SHA256, size, events/assertions; s18 `32afbf11 / 269517 / 0` | `final-corpus.log`, `corpus-compare.log` |
| ABI probes | both pass unchanged: Node native224136/8, heltec117912/8, gateway150504/8; 191/9 and 42/6 | `final-board-abi.log`, `final-b278-abi.log` |
| console-sink | **720 checks / 62 structural / 905 BLE guard / 6 ownership / 3 ownership controls / 114 controls / 0 unusable**, 6 profiles | `console2-iteration.log` |
| inventory | regenerated 203, check byte-identical | tracked generated inventory |
| generator unit tests | 69 tests, OK (64 base +5) | `generator2-tests.log` |
| authority checker tests | 8 tests, OK; all six selftests RED | `checker-tests.log` |
| mutation union | **114 RED / 0 unusable**, seven complete batteries, each match count one and all source hashes restored | `final-mutation-*.log` |
| deterministic board pair | **FAILED**, heltec_mobile cannot compile; no usable final pair or RAM/flash attribution | `.pio-measure/s6-final-pair`, `final-boards.log` |

Mutation selectors, derived separately from `TARGET_SRC`: changed-source = `consoleline` (12) and
`cmdauthority` (16); context is declarations only, so no decision battery. No existing native battery
targets firmware_commands.cpp/fw_main.cpp: their coverage is the two real inbox arms and console-sink
structural controls. Dependency = full `radmin2codec` (66), `sliceAjson` (1), `sliceDack` (1),
`sliceGjson` (16), `b134ack` (2), covering every pre-existing lib/console target. Union = 28 + 86 = 114.
All seven ran with `--workers=2` from `/tmp/mr-s6-final-mutations-1mz59V`; no build or mutation ran in the
shared checkout. Their production/test inputs still match the current implementation; subsequent
changes were only the inbox runner's control anchor/labels.

PIN re-synced? YES — 2825/119784 + 6/793 + 8/1254 = 2839/121831; no pre-existing native case changed.
This is the native counter binding only, **not** an overall software PASS.

Iteration failures retained honestly: the first native build lacked the direct frame_codec.h include in
the new test (fixed); initial inbox expectations omitted write_err's actual trailing LF (fixed, formatter
unchanged). Two old console-sink control anchors no longer matched the required context signature;
re-aimed, then all 114 passed. Both inbox positive arms now execute 362/360 checks with zero failures,
and all nine new controls fire, but its old C20 anchor still targeted the pre-outcome router line: full
iteration measured 38/39 and 41/42 controls, one VACUOUS each. C20 is now re-aimed without changing its
purpose; the full rerun is **PENDING**, not reported green. New controls are uniquely named S6-C1..S6-C9
to avoid the pre-existing client's C36..C40 labels. No regression control was deleted.

### 8.4 Proposed B348 — the prescribed BLE bound is unavailable on non-BLE boards

The deterministic `heltec_mobile` build executes the failure; this is not a speculative source concern:

```text
src/fw_main.cpp:481:63: error: 'kLineStorageBytes' is not a member of 'mrble'
src/fw_main.cpp:643:53: error: 'kLineStorageBytes' is not a member of 'mrble'
*** [.pio-measure/env/heltec_mobile/build/heltec_mobile/src/fw_main.cpp.o] Error 1
```

Source facts: `device_ble.h:24–25` defines `MRBLE_NRF52` only for the Arduino nRF52 targets. Its
implementation is behind that gate at :53, and the capacity constant is inside it at :183. The
ESP32/native branch (:41–47) supplies inert transport stubs but NO capacity constant. In contrast,
`fw_main.cpp:478` defines the whole `ble_dispatch_line` unconditionally, and setup passes its address
to `mrble::begin` at :1085 even when that begin is the inert stub. Thus both unconditional references
required by brief §§4.1/4.2 fail on the ruled Heltec board. Host probes cannot find this: none compiles
fw_main.cpp, as the brief correctly states.

**Recommended QA fold-in:** explicitly authorize compile-time gating of the real BLE dispatcher body
under the EXISTING `MRBLE_NRF52`, with an inert no-output stub for the absent transport so setup's
callback signature stays stable. Preserve every live nRF52 arm/envelope/order and use its one device
constant; expose the gate in generated BLE inventory rows and re-verify the structural/guard extractor
and controls. Do not add a guessed Heltec BLE cap, a local-cap fallback or a second derivation. QA may
choose another mechanism, but `device_ble.h` is explicitly out of fence and the current fw_main fence
does not authorize wrapping its existing dispatcher body or changing setup. The coder has done none
of those expansions and has not silently widened the brief.

STOP under §§5/8: resolve the integration fence with QA, then finish/re-run the gates. No new owner
product ruling or preparation commit is requested. The partial implementation is preserved for QA;
the maintained register remains QA-owned (B348 is proposed here, not self-landed).

### 8.5 Remaining work / exact path inventory

After B348: implement the approved non-BLE compilation shape, regenerate the inventory for moved
anchors/gates, re-gate the current inbox C20 correction, and run the complete final suite including
all six probes, no-control classifications, full tools sweep, both existing checkers, warning census,
deterministic pair with section/symbol/flash attribution, and source/diff checks. Re-run any affected
mutation batteries. Bench residue remains QA's exact BLE-NUL line, not host-proven hardware behavior.

Coder-modified tracked paths: `src/firmware_commands.{h,cpp}`, `src/fw_main.cpp`,
`tools/gen_command_inventory.py`, `tools/test_gen_command_inventory.py`,
`tools/probe_console_sink/{structural.py,negctl.py,run.sh}`,
`tools/probe_inbox_verbs/{probe_main.cpp,run.sh}`, `tools/probe_ui_model_mutations.py`,
the generated command inventory and this evidence. New paths: `lib/console/console_line.h`,
`src/firmware_command_context.h`, `src/firmware_command_authority.h`,
`test/test_console_line.cpp`, `test/test_command_authority.cpp`,
`tools/check_command_authority.py`, `tools/test_check_command_authority.py`,
`docs/superpowers/evidence/2026-09-07-radmin-command-authority-table.md`.
QA's existing brief/register edits are NOT coder edits. No lib/core, simulator, device_ble, handler,
UI, help, manual, design, bench, ruling or baseline file was edited by the coder. Nothing committed.

## 9. B348 resumed — implementation complete, coder gate GREEN; independent QA pending (2026-09-07)

**This is the CURRENT report.** The STOP records in §§1–8 remain visible as history; §8's incomplete status
is superseded here. QA/owner authorized the B348 fold-in in brief §§4.1/5: hoist the constant derivation,
NOT the previously suggested MCU-gated dispatcher. B343–B348 remain QA-owned register rows; proposed new
findings are in §9.5 below. No owner ruling is requested and no independent QA PASS is claimed.

Measured revisions remain MeshRoute `356dad7f2a16cbc9afb6ae74e35b936ca45f76e8` and simulator
`06746a97de5764415d6fcef10b97bca90569b9c7`. The owner explicitly waived commit-status blocking; nothing
was committed or repinned by the coder. QA's brief/register edits were preserved. All logs named below
are under `/tmp/mr-s6-NPTViQ` unless another root is explicit.

### 9.1 B348 implementation and proof

- `src/device_ble.h` now exposes the grammar atoms, producer maxima, `larger_of`, product/storage maxima
  and constant-only assertions in the always-compiled **named `mrble` namespace**. The protocol include
  moved with them. `g_line`, its intake state, and every Bluefruit/device implementation remain gated.
- `kRemoteCommandMaxBytes` remains 201 and is statically bound to
  `meshroute::console::remote_command_max_bytes`. Only the authorized mirror comment was corrected;
  no capacity value changed. The validator include is explicitly `../lib/console/console_line.h` so the
  standalone header/probe does not require an additional console include-search directory. The first
  attempt with a bare `console_line.h` include correctly failed the BLE probe build; it is retained in
  `resume-ble_line.log`. The repaired real-header run is `resume2-ble_line.log`.
- `resume-hoist-proof.log`: the moved constant block is identical to `356dad7` after stripping comments
  and the ONE new static assertion; the entire implementation from `g_line` to EOF is **byte-identical**.
  A standalone non-nRF52 C++17 header compile also passed, asserting storage 275 and the 201 equality.
  The real nRF52 intake probe remains **40 checks / 8 controls / 0 unusable**.
- No MCU gate was added to `ble_dispatch_line`, no fallback cap exists, and BLE inventory gate provenance
  did not acquire an MCU axis. The regenerated inventory remains **203 rows**, with the B347 pair intact.

### 9.2 Final gate — executed, not inferred from the pre-STOP run

| instrument | final result | artifact |
| --- | --- | --- |
| native build THEN actual binary | **2839 cases / 121831 assertions / 0 failed** | `resume-native-build.log`, `resume-native.log` |
| simulator checked no-op | 0 compile/link actions; executable MD5 `db6582a171a6d785720e324c2cfe43f1` unchanged | `resume-simulator-build.log` |
| corpus, `--jobs=8 --require-anchors` | **36/36 anchors**; s18 **`32afbf11` / 269517 / 0**, read from current `simulation/BASELINE.md:10514` | `resume-corpus.log`, `resume-corpus/manifest.json` |
| corpus before/after comparator | both manifests/streams validated; 36/36 identical on MD5, SHA256, size, events and assertions | `resume-corpus-compare.log` |
| board ABI | 191 checks, 9/9 controls RED, 0 unusable; Node native **224136/8**, heltec **117912/8**, gateway **150504/8**, all unchanged | `resume-board-abi.log` |
| B278 ABI | 42 measurements, 6/6 controls RED, unchanged | `resume-b278-abi.log` |
| console sink | 6 profiles / 720 checks / 62 structural / 905 BLE guard / 6 ownership / 3 ownership controls / **114 controls / 0 unusable** | `resume-console_sink.log`; the final wrapper rerun is included in `resume2-tools.log` |
| inbox verbs, ACCEPT | **362 checks / 39 controls / 0 unusable** | `resume-inbox_verbs.log` |
| inbox verbs, CLIENT | **360 checks / 42 controls / 0 unusable**; both independently compiled arms PASS | same log |
| firmware UI | l2 404 checks, v3 839; **223 controls / 0 unusable**; existing uncovered set retained (703/840 union checks reddened) | `resume-firmware_ui.log` |
| custody USB | **27 checks / 10 controls / 0 unusable** | `resume-custody_usb.log` |
| BLE line | **40 checks / 8 controls / 0 unusable** | `resume2-ble_line.log` |
| features | **9 cells / 120 checks / 59 controls / 0 unusable**; approved nine-file ownership census and 40 ownership controls unchanged | `resume-features.log` |
| tools unit sweep | **342 tests, OK** = 329 base + 5 generator cases + 8 new checker cases | `resume2-tools.log` (593.737 s) |
| inventory | `--write`, bare and `--check`; **203 rows**, generated bytes exact; help union remains 53 | `resume-inventory-{bare,check}.log` and tracked inventory |
| new authority checker | ruled Markdown / production header / inventory agree; **6/6 selftests RED** | `resume-authority-check.log` |
| a0 checker | PASS, **10/10 selftests RED** | `resume-a0.log` |
| DATA-type checker | PASS, 205 active source files scanned | `resume-datatypes.log` |
| warning census | all six envs build; **173/178/177/177/182/182**, respectively gateway_heltec / gateway_heltec_v4 / heltec_mobile / heltec_v3 / heltec_v4 / heltec_v4_mobile; **zero switch warnings** | `resume-warning-census.log` |
| deterministic ruled pair | both PASS; RAM/object counts unchanged; flash attributed in §9.3 | `qualified-boards.log`, `.pio-measure/s6-final-qualified-pair/` |
| mutation union | **114 RED / 0 unusable**, every source match count one; all source hashes restored | `resume-mutation-*.log`, §9.4 |
| whitespace / simulator diff | both repositories whitespace-clean; simulator status/diff EMPTY | final read-only checks |

The original inbox C20 stale-anchor failure is resolved: the corrected control fires on BOTH arms; none
was removed. The original 101 console controls plus 13 new controls remain present. The full tools sweep
also executes the console wrapper's current runner and its probe-only mode after the pin-format repair
in §9.5; the wrapper itself was **not edited**.

**Probe-only modes are not software gates.** All six were executed with `--no-neg` on the final tree
(`resume-only-*.log`), and retrospectively against a disposable `git archive` of the exact source base
(`/tmp/mr-s6-base-no-neg-DV4eNH`, `base-only-*.log`). Console sink, custody USB, BLE line and features
end PROBE-ONLY. Inbox's per-arm runner says PROBE-ONLY but its combined output still includes PASS
wording (existing B303). Firmware-UI still ends with bare `PASS` and zero controls (proposed B350 below).
Neither wording is used as full-gate evidence here; their default controlled runs above are the gate.
The archived base's first features invocation lacked its sibling simulator path and correctly refused
the environment map (115 checks, one failed, rather than the 120 pin). Rerun with the supported
`MR_LUS_SRC=/home/staszek/lora-universal-simulator` setting passed 9/120/0 and ended PROBE-ONLY
(`base2-only-features.log`). No environment-map check or pin was weakened.

PIN re-synced? YES — 2825/119784 + 6/793 (`test_console_line.cpp`) + 8/1254 (`test_command_authority.cpp`) = 2839/121831; no pre-existing native case changed.

### 9.3 Board cost and attribution

Predictions were recorded before measuring (§5): RAM ±0, flash growth, no new object or Node member.
Base manifests remain `.pio-measure/s6-base-pair/`. Final manifests are
`.pio-measure/s6-final-qualified-pair/{gateway,heltec_mobile}/manifest.json`, with the preserved ELF,
payload, sections, normalized symbol inventory and full build log beside each.

| board | RAM base → final | flash base → final | objects base → final |
| --- | --- | --- | --- |
| gateway (ARM) | 197180 → **197180** (0) | 544668 → **550204** (**+5536**) | 285 → **285** |
| heltec_mobile (Xtensa) | 207748 → **207748** (0) | 1367444 → **1371260** (**+3816**) | 329 → **329** |

Both toolchains and fixed build identities match their base captures (`Jan  1 2000 00:00:00`, revision
`b206b206b206`). These are measurement images, not firmware to flash. Final payload SHA-256:
gateway `57a00a3b0dd2ed9e15ae8edf4d88bc8b442c29ffd43ea435e6d6d455d8f722ee`;
heltec `0bc0193a02c8b7ec5c4a9448c9b6f7c19e1e8b1971f0100830eba74b55a4f825`.

`resume-board-attribution.log` lists **every normalized symbol-size movement**, including changes in
source-untouched inline services, rather than assigning the whole increase to the new table:

- ARM: `.text` **+5536**; `.data` 976 and `.ARM.exidx` 8 unchanged. Table **2148** (= 179 × 12),
  lookup **2176**, seam **964 → 1272 (+308)**, BLE head **2316 → 2368 (+52)**. Other measured emission
  changes: `GuardedConsole::compact` +58, `Sx1262Radio::arm_rx` +48, string-view support throw stub +8,
  `mesh_service_once` −16, `setup` −20; switch-table symbol renumberings net to zero. Sized-symbol net
  **+4762**; the remaining **774 allocated bytes** are not represented by sized symbols. This residual
  is reported as such (embedded strings/literal pools/alignment), not invented as another function.
- Xtensa: `.flash.rodata` **+2608**, `.flash.text` **+1208**; all six other loadable sections unchanged.
  Table **2148** in rodata; seam **496 → 771 (+275)**, panel **138 → 206 (+68)**, lookup/helpers
  **678**, line-error mapper **46**, string-view helpers **71**, `mesh_service_once` **+20**. The
  remaining named text changes net **−20** across parse_hex32, preset/key/target/layer helpers,
  RF diagnostics and inbox-store emission; their source bodies are untouched. Sized-symbol text net
  **+1138**. Residual allocated bytes outside sized symbols: **460 rodata + 70 text**.

The final pair was repeated after the console-runner comment-placement correction, at the SAME build
paths: every measurement, artifact (including ELF/payload SHA-256), toolchain and fixed-identity field
equals the preceding successful pair (`.pio-measure/s6-resume-final-pair`). See
`qualified-board-repeat.log`. This is NOT a same-source comparator PASS: source snapshots differ only
because the two tool comments moved. Final capture source SHA-256 is
`dd40662e9142abcb3dbf26350b45ac53970999810bb1ddfb2c8f89d75536abe2` over 904 files. The evidence append
you are reading is after all source-sensitive measurements; no build input changed during a capture.

### 9.4 Two mutation selectors, union and restoration

Re-derived from the current `TARGET_SRC` map and the full modified/untracked path set:

- **Changed-source:** `consoleline` 12 + `cmdauthority` 16 = **28**. Context is declarations only.
  No configured battery names `firmware_commands.cpp`, `fw_main.cpp` or `device_ble.h`; their coverage
  is the real inbox execution, console structural/guard controls and the real BLE intake controls.
- **Dependency:** `radmin2codec` 66 (the live 201 derivation binding), plus EVERY existing lib/console
  target: `sliceAjson` 1, `sliceDack` 1, `sliceGjson` 16, `b134ack` 2 = **86**.
- **Gate the union:** all seven complete batteries rerun from
  `/tmp/mr-s6-resume-mutations-V8bnO4` with `--workers=2`: **114 RED, 0 unusable**, every match count
  one and every worker source restored by hash; the harness confirms all 52 configured target files
  unchanged. The staging copy excluded `.pio-measure` before invoking the unchanged rsync harness.
  No mutation or mutation build ran in the measured checkout. Final production/test inputs and the
  mutation/inbox instruments were compared byte-for-byte with that staging copy and still match.

### 9.5 Findings proposed to QA (register remains QA-owned)

| proposal | source / measurement | disposition requested |
| --- | --- | --- |
| **B349 — coder pin-comment formatting prevented wrapper discovery** | The partial implementation put inline comments after `PIN_STRUCTURAL=62` and `PIN_CONTROLS=114`. `tools/test_probe_console_sink.py:112` deliberately recognizes bare integer assignment lines only; import failed at :121 with `run.sh no longer declares PIN_STRUCTURAL`. `resume-tools.log`: **321 tests, one import error**, not a partial PASS; the missing 22-test module was replaced by unittest's one failed-import pseudo-test. | **Fixed in-slice:** comments moved ABOVE the two assignments (`tools/probe_console_sink/run.sh:194–200`), values/parser unchanged. Full rerun **342 OK**; 321 − 1 + 22 = 342. Proposed close on QA verification, not a new pin relaxation. |
| **B350 — firmware-UI probe-only mode still prints a gate-shaped PASS** | `tools/probe_firmware_ui/run.sh:435` skips controls under `--no-neg`, but :1662 prints bare `PASS` whenever `rc==0`, without considering mode. Both `base-only-firmware_ui.log` and `resume-only-firmware_ui.log` end `controls: 0 verified / 0 unusable` then `PASS`. The entire script is byte-identical to `356dad7` (SHA-256 `f51a3e931ff4096163e1c89f3ae85e4dcddca7cbd46cf9c765aec872d574eb36`). | **OPEN, pre-existing instrument-verdict ambiguity**, related to B303 but a different runner. No firmware-UI tool edit is authorized here. Its default 223-control gate passes; this report does not count the no-control PASS as a gate. A later authorized repair should label that mode PROBE-ONLY and control the distinction. |
| **B351 — preserved BLE target-width note still calls the landed book deferred** | `src/device_ble.h:115–120` retains the transitional 32-byte target-label mirror and says the target-book record is deferred. `src/device_nv.h:567–568` already defines the occupied target label as 1..16 bytes, and `src/firmware_admin_client_verbs.h:460` validates against that row extent. The NV/verb sources are unchanged by Slice 6. | **OPEN, pre-existing documentation/mirror follow-up for the remote-wrapper slice.** B348 explicitly requires preserving the derivation wording except the 201 mirror, so this was not silently rewritten or rebound to the NV layer. This is NOT a claim that the current 275-byte buffer is wrong: `send_layer` still binds. Reconcile the actual future selector grammar with the mirror when that producer lands; do not substitute the 16-byte label as the whole selector by assumption. |

The first bare-include attempt, the base archive's missing simulator-path setting, and §8's earlier
iteration failures remain visible rather than being presented as green runs. The tools sweep's Python
`ResourceWarning` for an unclosed `/dev/null` handle also appears in the recorded BASE sweep
(`base2-tools.log`); it is not a new board warning or a failed final test.

### 9.6 Handoff inventory and QA-owned landing residue

The §8.5 exact coder path list still applies, **plus `src/device_ble.h` for B348 only**. Eight new files:
`lib/console/console_line.h`, `src/firmware_command_context.h`, `src/firmware_command_authority.h`,
`test/test_console_line.cpp`, `test/test_command_authority.cpp`, `tools/check_command_authority.py`,
`tools/test_check_command_authority.py`, and
`docs/superpowers/evidence/2026-09-07-radmin-command-authority-table.md`.
The console/inbox runner edits and generated inventory are tracked changes, not missing instruments.
`qualified-status.txt`, `qualified-working-tree.patch` and `qualified-new-files.sha256` capture the
implementation before this report append. QA's brief/register changes are separate, not coder edits.

No `lib/core`, simulator, handler, UI, help, NV, manual, design, bench, ruling or baseline file was edited.
No remote consumer, scheduled/internal-failure producer, second validator, fallback capacity or local
authority-table lookup was added. The prefix classification explicitly accepted by QA is retained.

QA owns the independent gate and all landings: verify the ruled table transcription; close proven
B343–B349 obligations; decide the proposed B350/B351 rows; land the design/manual/status updates. The
one physical residue remains the brief's BLE-NUS embedded-NUL line `cfg set name AB␀CD`, expecting
`{"err":"bad_line","msg":"embedded_nul"}` (with its normal NDJSON LF) and the node name unchanged.
No host instrument compiles `fw_main.cpp`; that hardware result is **not claimed here**. The owner
commits and bench-verifies. The coder leaves the implementation uncommitted, **ready for independent QA**.
