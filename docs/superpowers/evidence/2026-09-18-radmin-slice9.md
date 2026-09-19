<!-- Production coder: Codex; independent gate: QA/Author; owner rules and commits. -->
# Slice 9 — coder source-validation, revision 2

**2026-09-18 — STOP-1, before implementation. B420–B422 are brief/instrument inconsistencies.**
MeshRoute and simulator pins match. No production, test, tool, brief or simulator file was changed.
This receipt records source-validation and focused reproductions, not an implementation freeze or a full gate.
The requested corpus identity, unchanged Node sizes and board RAM/flash decreases remain predictions.
The inventory prediction disagrees with the current generator: the specified deletion implies **197**, not 202.

## 1. Pinned inputs and preservation

- MeshRoute: `84edd3ebfb08d807645d077269bace145ac2a2ab`.
- Simulator: `6585649ea5a780f0542b2931853a667be56a5b2b`, clean before and after.
- Brief revision 2: `bd538cae5e85b4bb92e2a3ae4fd118d1292f4363b090b8c327b6d22a7c4a1353`.
- R-RA-50 is recorded and settled. No commit prerequisite or new owner ruling is introduced here.

The five tracked QA edits and the untracked brief were accepted as the permitted preparation set:

| Path | Initial SHA-256 |
| --- | --- |
| `MEMORY.md` | `2890c33a1f608342aa6e172e9d11dad552eea2c64da595db26a6b816bb3ab369` |
| `docs/2026-07-30-open-bug-register.md` | `7102226dfe4471f533cd8edf9f715efd6b3657565e6194d2ffa539949cacf519` |
| `docs/superpowers/plans/2026-09-03-remote-admin-v2-rulings.md` | `1da510e8d3a9019f6d5ee59c1c5984971b4d730e96a19096aea30a7cdc8bfa59` |
| `docs/superpowers/plans/2026-09-18-radmin-slice9-legacy-deletion-and-protocol-docs.md` | `bd538cae5e85b4bb92e2a3ae4fd118d1292f4363b090b8c327b6d22a7c4a1353` |
| `docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md` | `c002ca518be47c290e692579dc16d6a3ebe9092a0b4b4d17b7cdb1ca25ad8720` |
| `tracker.md` | `d6d2d47b55b49f110329748aeaff8ac09eb6a5fdc52d0bac11b33d84989baa8a` |

[Preparation hashes](2026-09-18-radmin-slice9-preflight-r2/preparation-inputs.json),
[complete input inventory](2026-09-18-radmin-slice9-preflight-r2/input-inventory.json),
[incoming tracked documentation patch](2026-09-18-radmin-slice9-preflight-r2/preparation.patch), and
[revision-2 brief copy](2026-09-18-radmin-slice9-preflight-r2/brief-r2.md) preserve provenance.
The inventory contains **3,419 regular files and four symlinks**, including all untracked inputs;
**361** paths are under `lib/`, `src/`, `test/`, `tools/` or are `platformio.ini`.
Every initial input was checked unchanged after the reproductions. The only later edit to an existing file is
the register's STOP dispatch and B420–B422 entries; all other preparation inputs remain byte-identical.

## 2. B420 — whole-feature deletion removes eleven inventory rows

The live generator and `--check` both produce **208 rows**. Its records, including source and surface identity,
show these eleven rows belong to the deletion:

| Source surface | Rows removed | Count |
| --- | --- | --- |
| `firmware_commands.cpp::dispatch` | `lock`, `password`, `rcmd`, `unlock` | 4 |
| `fw_main.cpp::ble_dispatch_line` | `rcmd` | 1 |
| `firmware_remote.cpp::remote_encode` | `duty`, `limits`, `routes`, `status` | 4 |
| `firmware_remote.cpp::remote_exec` | `password rotate`, `reboot (alias: prep-restart)` | 2 |

Thus **208 − 11 = 197**. The brief subtracts only the six rows whose authority class is `legacy`; it misses
five rows whose **surface** is legacy but whose authority class is `open` or `operator`.
Their ordinary local/v2 rows remain independently present. Keeping these five radio rows would describe
functions and a protocol the same brief requires deleting.

This is a derivation from the unmodified source, not a claimed post-implementation generated result.
[All eleven complete records](2026-09-18-radmin-slice9-preflight-r2/inventory-deletion.json).
**Required fold-in:** reconcile §§1/7/9 and the dispatch expectation with 197 while retaining the whole-feature
deletion. No new command or replacement inventory row is needed.

## 3. B421 — three omitted instrument dependencies

Per P7 and brief §6, an unlisted user is STOP-1. These are executable instrument dependencies, not line drift:

| Omitted path | Verified dependency and consequence |
| --- | --- |
| `tools/test_probe_console_sink.py:347` | Two real tests require the deleted macro axis and the `lock/password/unlock` projection. Both pass on the base. Running those same tests with only the specified row/axis deletion projected in memory yields **one error and one assertion failure**. |
| `tools/test_probe_features.py:274` | Requires three header diagnostics, A3/A4 consistency refusals and the `REMOTE_MGMT=1` output column. The prescribed switch/agreement deletion removes that diagnostic and column. Its class-C minimum also needs review if C3/C4 are retired rather than retargeted. |
| `tools/probe_board_ui/run.sh:543` | `CFG_NOTIFY_SITES=7`, the W19 password success/wipe sequence and five controls, and W20's shared writer census depend on `handle_password`. Removing only that function in memory changes direct saves **7→6** and notifications **7→6**. The shared `nsite` census is used by other surviving checks too. |

[Measurements](2026-09-18-radmin-slice9-preflight-r2/omitted-instrument-users.json) and
[two-test failure output](2026-09-18-radmin-slice9-preflight-r2/omitted-console-tests-projected-deletion.log).
No shared candidate or production file was edited to obtain these results.

**Required fold-in:** add the three paths with narrow retirement/re-derivation instructions and preserve controls
for surviving decisions. The board-UI W19/census dependency is additional to **B418's pre-existing W49/W51/W54**;
this preflight neither fixes nor closes B418. QA should specify the supplemental probe's treatment so those
known failures cannot be confused with a new deletion regression.

## 4. B422 — the required legacy-literal reference proof has no fenced instrument

Brief §§7/9 require the captured legacy literal to join the compared set and require removal of the legacy
case to turn the reference's freeze check RED. The current extended reference imports a strict literal comparator
and expects exactly **94** arrays; it does not check whether §8's test case exists.

Fresh executions of the real reference (`--freeze-check --compare --selftest`) show:

| Input | Result |
| --- | --- |
| Unmodified `test_remote_codec.cpp` | **94/94**, old 89 identical, five comparator controls RED, exit 0 |
| Temporary copy with the entire §8 legacy `TEST_CASE` removed | **94/94**, the same five controls RED, **exit 0** |
| Temporary copy with the captured 40-byte `kRefLegacySealed` literal added to the compared namespace | **exit 1: `EXTRA kRefLegacySealed`** |

The case-deletion variant is a **surviving control demonstrating the missing proof**, not a successful gate.
No reference extension is in §6's fence. A literal outside the comparator's recognized namespace would escape
comparison and would not meet the requirement either.

**Required fold-in:** fence a Slice-9 reference extension (keeping the historical reference files immutable),
report **94 unchanged reference arrays plus one captured legacy frame**, and specify the case-removal control.
For example, put the compared legacy literal inside the retained case, so deleting that whole case removes a
required array. No codec behavior change is proposed.

The live sealer at the pinned base was compiled separately with the exact §8 inputs, then its result was opened
by the live legacy decoder: **seven checks pass**, length **40**, bytes:

```text
deadbeef010203040100e2136ffc9b0472121a6259e8bd6ba0af59505d903bccbcf3fb1f515069ea
```

SHA-256: `f8cf9ec1971840b136c72745bc692cb3727de6b2f10493c6d87a86a51fb38138`.
[Capture source](2026-09-18-radmin-slice9-preflight-r2/capture-legacy.cpp),
[capture data](2026-09-18-radmin-slice9-preflight-r2/legacy-capture.json),
[exact commands and log hashes](2026-09-18-radmin-slice9-preflight-r2/commands.json).
This records the required pre-deletion output; it does not claim a new v2 runtime proof.

## 5. Confirmed seams, limits and return

Source agrees that the two removed library TUs are absent from the simulator's CMake source list; no simulator
edit is needed. `Node` has the two legacy-gated blocks named by the brief. The boot `admin_load` call exists once
at `fw_main.cpp:972`; the current live accessor calls are confined to the deleted executor/password handler.
The board agreement `#error` is present. These source facts support the unchanged-Node prediction, not a fresh
ABI measurement. R-RA-50's lab-command deletion is inside the fence.

The absence proof also needs its scope stated precisely: retained negative checks intentionally name forbidden
symbols, and historical comments name the old sealer/switch (including `remote_codec.h:23`,
`firmware_admin_identity.h:32`, the native layout ledger and the mutation runner's historical derivation).
An unqualified raw grep returning zero conflicts with retaining those absence checks/history. Check executable
uses separately; identify comment corrections explicitly without changing surviving v2 code.

Fresh successful instruments: inventory `--check` (**208**, unchanged), authority checker with **6/6 controls
RED**, reference baseline **94/94**, the two unmodified projection tests, the seven-check legacy capture, input
preservation, and whitespace checks in both repositories. The projected failures and surviving reference
control above are retained evidence of the STOP conditions.

**Not run:** native wrapper/full binary, simulator build/corpus, ABI probes, full probe set, full tools discovery,
board pair/xiao, census or mutation union. No new native totals, Node sizes, RAM/flash deltas, corpus result or
implementation PASS is claimed. No pins were re-synced because implementation did not begin.

Reproduce the focused checks on the pinned preparation tree with a Python containing PyNaCl:

```sh
python3 docs/superpowers/evidence/2026-09-18-radmin-slice9-preflight-r2/reproduce.py \
  --root /home/staszek/MeshRoute \
  --simulator /home/staszek/lora-universal-simulator \
  --reference-python /tmp/mr-s8ac-r4-b1edsao2/reference-env/bin/python \
  --output /tmp/mr-slice9-r2-recheck
```

QA/Author can fold B420–B422 into the brief and re-pin it at the same base. Implementation then resumes without
a commit prerequisite. The full coder freeze, independent QA gate and subsequent Slice 10 brief remain pending.


## 6. Revision-3 resume — source-validation PASS, implementation begins (2026-09-19)

Base `84edd3ebfb08d807645d077269bace145ac2a2ab`, simulator
`6585649ea5a780f0542b2931853a667be56a5b2b` (clean), brief revision 3
SHA-256 `b90dc0ca21d9077d74292d2a7bf80c3d2ab506b783bb963e4838a1ff359f7001`.
The complete initial preparation inventory includes all 36 dirty/untracked inputs, including the prior
preflight receipt/artifacts; the complete base export contains all 3,453 inputs, not just HEAD.
B420's 197-row derivation and B421's three instrument dependencies are now fenced.
B422 is implementable with `constexpr uint8_t kRefLegacySealed[40]` inside the retained case: the historical
comparator recognizes `const uint8_t` declarations, and an executed scratch comparison with this `constexpr`
shape still reports 94/94 and five controls RED. The new Slice-9 extension owns the separate literal and
requires its actual use in the case's v2 decode loop. The historical scripts remain immutable.

Predeclared instrument choices: retire feature controls A3/A4/C3/C4 with the deleted agreement diagnostic;
retarget the ownership widening controls to `|| 1` while retaining exact per-owner guard/census checks;
retarget W-UNKNOWN to another unapproved production file because its former file is deleted; retire
board-UI W19/five controls and re-derive its shared writer census. The BEFORE supplemental board-UI run
reproduces exactly B418's W49/W51/W54, structural 23/23 and wiring 57/60. The independent base export
continues the fresh native, board-pair and corpus measurements while production edits occur only in the
shared checkout. No base result is inherited as a candidate result.

Implementation and the full final gate are pending; this is preflight PASS, not a freeze.

## 7. Revision-3 implementation checkpoint — STOP-1 B423 (2026-09-19)

**HOLD. The source-validation PASS in §6 missed a transitive policy-census dependency and is withdrawn.**
The partial implementation is preserved uncommitted. This is a resumable checkpoint, not an implementation
freeze or a request for owner permission. The brief remains byte-identical at
`b90dc0ca21d9077d74292d2a7bf80c3d2ab506b783bb963e4838a1ff359f7001`.
MeshRoute remains `84edd3ebfb08d807645d077269bace145ac2a2ab`; simulator remains
`6585649ea5a780f0542b2931853a667be56a5b2b`, clean. No staging, commit, reset or cleanup was performed.

### 7.1 B423 — a deleted inventory alias is also a deferred-action census input

The old executor owns the inventory row `reboot (alias: prep-restart)`. Removing that radio surface while
retaining the matching semantic row in the header/table fails the unchanged orphan-row rule:

```text
orphan ruled row: ('reboot (alias: prep-restart)', '—')
```

The candidate therefore removes **six semantic rows**, not the five legacy-class rows named by §§1/3.
The six are `lock`, `password`, `password rotate`, `rcmd`, `unlock`, and the radio-only reboot alias.
The generated inventory removes eleven source rows and has the required **197**. With all six semantic
deletions the authority checker passes, including all six controls; restoring the alias in memory makes
exactly the orphan check fail. No exception was added to that rule.

However, `tools/probe_deferred_actions/remote_rows.h::run_remote_actions` enumerates the policy array and
asserts `scheduled_scope==12`. Compiling against the actual base and candidate headers measures
**12→11 scheduled policy rows**, with **36→36 refused policy rows**. All twelve command strings in the
probe's executed examples retain identical operator/owner/disruptive lookup results. The removed metadata
alias is not an additional action mode, and no surviving action implementation changed.

The actual candidate probe (`--no-neg`, first backend `b0-p1`) confirms the failure:

```text
ACTION CHECKS 150 FAILED 0
  FAIL remote action 265: scheduled_scope==12
REMOTE ACTION CHECKS 416 RADIO 3160 FAILED 1
```

The probe stops at this backend; later variants and its negative controls did not run. Its original full
output is retained as [deferred-b0-p1-full.log](2026-09-19-radmin-slice9-r3-stop/deferred-b0-p1-full.log).
The runner's shorter tail omitted the individual failing assertion; that tail alone is not the diagnosis.

**Required QA fold-in:** explicitly account for the sixth semantic metadata row and fence the narrow
`tools/probe_deferred_actions/remote_rows.h` census change (12→11 policy rows). Preserve the twelve executed
examples, all 36 retained refusals, and all existing controls. No new owner ruling, action behavior,
allocation change, replacement inventory row or orphan-check exemption is proposed. The probe file and
runner remain untouched. Their aggregate check-count pins need no assumed change: remeasure them at resume.

The standalone [reproducer](2026-09-19-radmin-slice9-r3-stop/reproduce.py) builds two small programs against
the actual headers, compares the twelve lookups, exercises the real authority checker and verifies the
probe file stayed byte-identical. [Output](2026-09-19-radmin-slice9-r3-stop/b423-reproduction.log):

```sh
python3 docs/superpowers/evidence/2026-09-19-radmin-slice9-r3-stop/reproduce.py \
  --root /home/staszek/MeshRoute --base /tmp/mr-s9-r3-mg75mxe0/base
```

### 7.2 Preserved implementation and unfinished iteration repairs

The eight old implementation/test files are deleted; the old sender bodies, dispatcher/help arms,
issuer/deferred globals, boot-loop legacy consumer, password handler and feature switch are removed.
The Node mirror fields and boot restore remain, re-gated on ACCEPT. Authority/inventory and the fenced
instrument retirements are partly adapted. The old reference scripts remain unchanged; the new Slice-9
reference checks the literal inside its retained rejection case and its use by the v2 decoder.

Two ordinary implementation corrections remain alongside B423:

- The candidate native wrapper fails to compile the two replacement `enqueue_data` calls in
  `test_node_r3.cpp`: that member is private. These were my unfinished adaptation of the brief's requested
  replacement, not a production failure. The existing receive/refusal assertions are still present.
  No production access change or test-access workaround was made after STOP. Resolve the fixture within
  its fence at resume; do not claim a native result from this candidate.
- Console ownership is correctly disjoint/complete on every profile but still has old numeric pins.
  Measured router/parser counts are **39/7, 40/7, 37/7, 38/7, 43/7, 44/7** for full_headless, full_oled,
  gateway, gateway_oled, mobile, mobile_oled. The four ACCEPT-side old verbs and the one mobile `rcmd`
  explain the reductions. Updating these pins is already authorized in `ownership.py`; it is unfinished.

The durable `frames.md`, `protocol.md`, companion-contract and bench edits have not landed. Comment cleanup
and the scoped absence audit remain incomplete. Mutation native pins have not been re-derived. The current
tree is intentionally preserved with these failures visible, not presented as ready to build or gate.

### 7.3 Fresh measurements, with their attribution

| Instrument | Measured result | Attribution / limit |
| --- | --- | --- |
| Base native wrapper + real binary | 2970 cases / 195942 assertions / 0 failed / 0 skipped | Complete pre-edit export, including all dirty preparation inputs |
| Base simulator rebuild + corpus | 36/36 current anchors reproduced; s18 `32afbf11e43b4bf9d0bd470ad502ba0a` | Base only; candidate corpus has not run |
| Base deterministic pair, replay | Gateway 204036 RAM / 575008 flash; mobile 211772 / 1395276 | Baseline only; candidate board sizes unmeasured |
| Candidate native wrapper | Build fails on two private-member calls | Binary not run |
| Candidate inventory / authority | 197 rows; six authority controls RED | Focused checks, not full gate |
| Candidate extended reference | Delegate 94/94, old 89 identical, five comparator controls RED; one frozen legacy literal and five additional controls RED | New extension executes the immutable old comparator; both standalone invocations still belong to the final chain |
| Feature probe | Nine cells, 112 checks, 58 controls; ownership 43 controls / zero unusable | Full focused feature run, source-preservation checks pass |
| Inventory generator unit suite | 76 tests pass | Two switch-only cases retired; full tools discovery pending |
| Inbox `--no-neg` | ACCEPT 1374 / CLIENT 457, zero failures | Negative controls skipped; no full probe gate |
| Console `--no-neg` | Behavioral runs 120/0 each; structural 83/0; ownership 0/6 because stale pins | Probe fails overall |
| Supplemental board-UI before / after | Before 57/60 and 176 controls; after 56/59 and 171 controls; structural 23/23 both | Identical failure set W49/W51/W54 (B418); W19 and its five controls retired |
| Deferred actions `--no-neg` | First backend local 150/0, remote 416 + radio 3160 with one failure | B423; remaining backends/controls did not execute |

The inbox unknown-verb proof drives the real common seam, then a **caller-shaped adapter** emits the exact
USB `> parse error\r\n` and BLE `{"err":"parse","msg":"unknown_cmd"}\n` replies. It does not compile the
whole board `fw_main.cpp`; console structural checks separately pin the actual caller expressions. This
is not a claim of a physical transport round trip.

**B424, closed run-setup incident:** the first base board pair overlapped native dependency writes in the
same snapshot and correctly stopped with `ERROR: normal .pio/ changed during measurement` (exit 2).
Native's external build directory did not isolate its normal `.pio` dependency writes. After native ended,
the **whole pair** was replayed sequentially and passed. Both logs are retained. Final board builds must
use an exclusive snapshot, separate from native; the eventual union uses a third input-only snapshot
without transient `.pio-measure` data (B419). No failed measurement was relabelled as successful.

**Not run on the candidate:** complete native binary; candidate simulator/corpus; ABI; complete standing
probe/control set; full tools discovery; A0/literal sweep; warning census; board pair/xiao; mutation union.
No candidate Node, linked RAM/flash decrease, full-chain PASS or QA PASS is claimed.

**PIN re-synced? NO — checkpoint only; native and console repairs plus the B423 fold-in remain pending.**

### 7.4 Preservation and resume

The [artifact directory](2026-09-19-radmin-slice9-r3-stop/) retains the initial 3,453-input inventory,
36-path preparation inventory, iteration snapshot inventory, baseline/candidate logs, B423 reproducer,
board baseline manifests and source-preservation checks. Nineteen surviving v2 owner files match the
pre-edit inputs byte for byte. All 360 non-symlink production/test/tool input records match the isolated
iteration snapshot at STOP, including deletion markers; no code was changed after the B423 diagnosis.
`checkpoint-inputs.json` inventories the complete preserved tree, including untracked files and deletion
markers, excluding only that self-referential manifest and its artifact-hash index. `artifact-sha256.json`
hashes the retained evidence files except itself. These identify a checkpoint, not a completed freeze.

Base export and build artifacts remain under `/tmp/mr-s9-r3-mg75mxe0`; the iteration snapshot includes all
uncommitted/untracked inputs, not merely HEAD. QA can fold B423 into a re-pinned brief at the same base.
Resume this candidate, finish the named repairs/docs, then run the entire required chain fresh. A commit
is not a prerequisite. Independent QA and the later Slice 10 brief remain pending.


## 8. Revision-4 resume — source-validation PASS (2026-09-19)

Brief revision 4 SHA-256 `6ae75dfc1b7da467413b35778604e1dd31c98188b3c3cbf9352af460193a7676`,
base `84edd3ebfb08d807645d077269bace145ac2a2ab`, simulator
`6585649ea5a780f0542b2931853a667be56a5b2b` clean. The 3,508-input preparation inventory includes the
preserved candidate and all tracked/untracked QA inputs. Relative to §7's checkpoint, only five QA inputs
changed: the brief, register, design, tracker and MEMORY. No code/test/tool input changed externally.
B423's sixth semantic-row deletion and eleven-row probe census now agree with source and are fenced.
The twelve executed action examples and all 36 refused rows stay intact. The frozen brief is not edited.

The native fixture reuses `Node::test_do_send_typed` (already used throughout the same TU), which calls
`do_send -> enqueue_data` with an explicit type; existing queue readers verify both types were queued.
No production access change or new test seam. The independent authority-list fixture also drops only the
retired alias. First repair full native binary: 2950 cases / 195768 assertions / zero failed or skipped.
The measured delta from 2970/195942 is −20 cases, −174 assertions: retired admin_auth −17,
console_binary −111, authority −45, sealer fixture −6, typed queue proof +5. Both pre/post binaries were
also run by source-file filter to attribute each term. Mutation pins are re-synced to that measured result.
The final chain runs fresh on complete isolated snapshots, including all dirty/untracked inputs.

Implementation checks before that chain: all four deferred-action variants pass in `--no-neg` mode;
console ownership is now at the derived six-profile pins; B418 remains exactly W49/W51/W54. The earlier
iteration's stale authority-list failure remains in the logs. These checks are not the final gate.

## 9. Revision-4 completed candidate — STOP-1 B426/B427 (2026-09-19)

**HOLD; this is a preserved implementation checkpoint, not a successful implementation freeze or an
independent QA PASS.** The source-validation PASS recorded in §8 is withdrawn: it missed the authority
unit fixture's legacy-surface string and the paired warning-pin dependency. All authorized fixture, pin,
durable-documentation and bench edits are complete. The full requested chain was invoked; its actual
failures are retained below. No owner ruling, production behavior change, new allocation or commit is
needed to resolve these two brief dependencies. The revision-4 brief remains byte-identical at
`6ae75dfc1b7da467413b35778604e1dd31c98188b3c3cbf9352af460193a7676`.

Evidence: [revision-4 checkpoint directory](2026-09-19-radmin-slice9-r4-checkpoint/). Its scripts, commands,
logs, input inventories, mutation selectors and artifact hashes identify the tested candidate. All builds
and mutation runs use isolated copies containing the uncommitted and untracked implementation. No shared
source was changed by an instrument; no simulator file, shared index, commit or branch was changed.

### 9.1 Completed changes and derivations

The native refusal fixture now uses the existing public `test_do_send_typed` seam, suspends draining, and
checks that both explicit legacy DATA types were queued before retaining the original receive/refusal
checks. No new production seam or access change. The independent authority fixture removes the retired
radio alias. Native counts are derived in §8 and `native-pin-derivation.json`: **2970/195942 → 2950/195768**,
−20 cases and −174 assertions. `tools/probe_ui_model_mutations.py` carries those exact measured pins.

Console ownership router/parser pins are **39/7, 40/7, 37/7, 38/7, 43/7, 44/7** for full_headless,
full_oled, gateway, gateway_oled, mobile and mobile_oled. The four retired ACCEPT verbs and the mobile
`rcmd` explain the reductions. The deferred census is **11 distinct scheduled policy rows**, with the
same twelve executed spellings and 36 refused rows. Inventory is **197**, with the six semantic deletions
in both table and header. The alias-restoration control from §7 still demonstrates the orphan-row rule;
no exemption was added.

Instrument retirements remain exactly those authorized: two inventory tests that separated the retired
feature switch; feature A3/A4 and C3/C4, whose deleted consistency diagnostic no longer exists; board-UI
W19 and its five controls, whose password persistence site is gone. The feature diagnostic count is two,
class-C floor three, cfg notify-site count six. The mutation harness corrects the obsolete feature-switch text in control 1b-06's label; its
mutation pattern and replacement are unchanged. All mutation bodies and standing batteries remain intact.

`frames.md` now specifies the twelve opcode/direction pairs, nine envelope layouts, result domains,
admission tuples, exact KDF/nonce/AAD bytes and carrier bounds. `protocol.md` §15 records the current
provisioning/session, immutable replay, deferred-action, controller retry, carrier and local-delivery
contracts with source symbols. Companion legacy contract passages point to v2; its controller-carrier
availability text agrees with completed 8b. Bench edits are confined to 9.9 and new Part 57e: real help,
USB/BLE unknown-command replies, and the existing v2 round trip. **Metal checks are pending/not run.**

### 9.2 Two unfenced gate dependencies

**B426 — authority unit fixture.** `tools/test_check_command_authority.py:44` still requires
`open · surface:legacy` for `status`. The whole deletion correctly leaves only `open` and
`open · surface:transport`. The focused real suite reports **7 passed / 1 failed**. This file is absent
from revision 4 §6 and is unchanged. The actual failure is:

```text
AssertionError: Items in the first set but not the second:
'open · surface:legacy'
```

QA's narrow fold-in should retain the surviving-multiple-surfaces/one-semantic-class proof and assert that
no inventory row has the removed surface. The [private proposal](2026-09-19-radmin-slice9-r4-checkpoint/B426-private-proposal.patch)
was executed in memory against real candidate inputs: **8/8 pass**, with no test case removed and no file
written. That proposal is not part of the tested implementation. The authority checker itself passes all
six controls with 197 rows; it is not weakened to accommodate the stale fixture.

**B427 — warning pins.** All six clean census builds compile and report zero `-Wswitch`, but the unchanged
pins correctly make the instrument exit 1:

| Environment | Old pin | Actual | Objects | RAM | Flash |
| --- | ---: | ---: | ---: | ---: | ---: |
| gateway_heltec | 173 | 171 | 329 | 238932 | 1337876 |
| gateway_heltec_v4 | 178 | 175 | 330 | 239204 | 1335936 |
| heltec_mobile | 177 | 175 | 329 | 211764 | 1394520 |
| heltec_v3 | 177 | 175 | 329 | 214148 | 1392156 |
| heltec_v4 | 182 | 179 | 330 | 214420 | 1390156 |
| heltec_v4_mobile | 182 | 179 | 330 | 212036 | 1392568 |

The paired pin owners are `tools/warning_census.sh` and
`docs/superpowers/plans/2026-07-31-onboard-oled-ui-phase-a.md` §B87. Neither is fenced by revision 4; both
remain unchanged. This is a derived re-pin after deletion, not permission to accept an unexplained warning
change or relax the gate. Fresh before/after attribution is retained under `warning-attribution/`.

### 9.3 Measured resources and attribution

| Environment | Base RAM / flash | Candidate RAM / flash | Delta RAM / flash |
| --- | ---: | ---: | ---: |
| gateway | 204036 / 575008 | **203820 / 572224** | **−216 / −2784** |
| heltec_mobile | 211772 / 1395276 | **211764 / 1394520** | **−8 / −756** |
| xiao_mobile | 176604 / 700588 | **176596 / 699548** | **−8 / −1040** |

The ruled pair uses the freshly rebuilt §7 baseline and identical deterministic measurement identity;
`board-builds.tar.xz` contains both sets of ELFs, payloads, full logs, manifests, sections and symbols.
The xiao row is the required one-off compared with the committed 8b baseline, not a new deterministic
A/B pair or a byte-identity claim. Pair object counts fall **288→285** and **332→329**: the three retired TUs.

Gateway RAM symbol removals total 212 bytes: `g_admin_id` 196, `g_admin_tx_ctr` 4, `g_admin_unlocked` 1,
`g_remote_action` 1, `g_remote_action_at` 8, and `handle_rcmd`'s static nonce counter 2. Linked RAM falls
216; the remaining four bytes are section/layout overhead, not additional claimed state deletion. Mobile
removes the two action symbols (9 bytes), with a net 8-byte linked reduction after packing. `g_node` and
all three measured Node sizes are unchanged.

Gateway flash is entirely `.text` −2784; normalized symbol-size changes total −2046, leaving −738 bytes
outside that sum. Removed legacy functions and policy rows are listed in `board-attribution.json`; linked
changes also include compiler inlining/layout consequences in unchanged-source functions, including
`SegmentedInboxStore::append`, `acl_verb`, console helpers and `setup`. No claim assigns every `.text` byte
to one deleted function. Mobile `.flash.text` is −484 (symbol-size delta −440, remainder −44), and
`.flash.rodata` is −272 (policy array −72, remainder −200); other flash sections are unchanged. These
residuals are explicit section-versus-symbol accounting limits, not hidden production edits.

**B425 — measurement instrument limitation/workaround.** The first pair invocation refused before compile:
`source input disappeared while hashing: lib/console/console_binary.cpp`. The tool lists cached plus
untracked files and cannot represent an unstaged tracked deletion. The production measurement tool stays
unchanged. A private external index removes exactly the eight already-deleted entries. A PATH wrapper
applies that index only to root-level `git ls-files` / `git status` reads inside the two measurement
snapshots; dependency Git and every mutation use their own original index. The projected live input set
is proven equal to the original set minus those eight paths. Complete inventories separately record all
eight deletions. Original staged entries still equal HEAD and `git diff --cached` is empty; one snapshot
index had an ordinary metadata/stat-cache refresh, so byte identity of that index is not claimed.

An initial attempt exporting `GIT_INDEX_FILE` globally was rejected after dependency Git contaminated the
private index with `keywords.txt`; no result from it is used. The complete final pair was then replayed
through the scoped wrapper. Earlier interrupted/review attempts are under `discarded-attempt/` and are
excluded from all final totals. B425 remains a separate tool finding; no simulator edit or commit was
needed for this workaround.

Fresh warning-multiset A/B confirms B427's cause: gateway_heltec **173→171** removes exactly one RadioLib
God-mode `-Wcpp` and one `device_radio.h` volatile-increment `-Wvolatile`. Heltec V4 **182→179** removes those
same two plus one RadioLib native-USB `-Wcpp`. There are **zero added warnings** in either comparison.
Deleting `firmware_remote.cpp` removes one including TU. The complete six-cell census above supplies the
other four actual counts. A [paired private proposal](2026-09-19-radmin-slice9-r4-checkpoint/B427-private-proposal.patch)
is retained for QA; it was not applied and no override census is claimed as the standing gate.

### 9.4 Complete chain results and the in-fence B428 repair

| Instrument | Actual result |
| --- | --- |
| Native wrapper + real binary | **2950 cases / 195768 assertions / 0 failed / 0 skipped** |
| Original extended reference | **94/94**, original 89 unchanged; five comparator controls RED |
| Slice-9 reference | Same 94 plus one frozen 40-byte legacy frame; five additional legacy controls RED |
| Fresh simulator build / corpus / validate | **36/36 current anchors**, **36/36 actual streams byte-identical** to fresh base; s18 `32afbf11e43b4bf9d0bd470ad502ba0a`, 269517 events |
| Node / standing ABI | **235248 native / 122176 mobile / 157344 gateway**, unchanged; 290 checks, nine controls RED |
| B278 ABI | 42 measurements, six controls RED |
| Inbox default + `--no-neg`, explicit CLIENT | ACCEPT **1374 / 60 controls**, CLIENT **457 / 68**, zero failed/unusable |
| Firmware UI both modes | 223 controls, zero unusable; 703/840 checks covered by at least one control |
| Console sink both modes | Six profiles, 720 checks, 83 structural, 905 BLE guard, six ownership cells/three ownership controls, 149 main controls; zero unusable |
| Custody USB both modes | 27 checks / ten controls, zero failed/unusable |
| BLE line both modes | 55 checks / twelve controls, zero failed/unusable |
| Features both modes | Nine cells / 112 checks / 58 controls (43 ownership controls), zero failed/unusable |
| Deferred actions both modes | Local 150/151/158/158, remote 416/518/534/464, radio 3160/3485/3689/3695; 39 transcripts per variant, all 40 controls RED |
| Inventory write / bare / check | **197 rows**, fresh generation byte-identical |
| Authority / selftests | PASS / all six controls RED |
| A0 / literals | PASS: 21 allocated enum members; 218 active source files scanned |
| Supplemental board UI | Expected B418 only: **56/59** with W49/W51/W54 failing; 171 controls RED; structural 23/23. Base **57/60**, same failure set, 176 controls |
| Deterministic board pair / xiao one-off | PASS, decreases in §9.3 |
| Warning census | **FAIL B427**, six old pins disagree with attributed lower counts; zero `-Wswitch` |
| Full tools discovery | **349 run / 347 passed / 2 failed / 0 skipped**: B426 and the initial B428 test below |
| Whitespace / simulator state | Both repositories clean under `git diff --check`; simulator HEAD and worktree unchanged |

The `--no-neg` results are probe-only measurements; the separate default runs supply the mandatory controls.
The full tools discovery uses the actual gateway ELF fixture and configured simulator source; no missing-fixture
skip was substituted for a test.

**B428, coder correction within the existing fence:** the full tools sweep exposed a second stale assertion
in `tools/test_probe_features.py::test_the_two_pins_reconcile_with_what_the_contract_actually_emits`:

```text
AssertionError: 87 != 96 : pre-1b checks were 97 with S3; S3 is replaced, so 96 must remain beside the contract
```

Both independent arithmetic pins must reflect the authorized retirements: **87 = 97 − S3 − nine legacy-column
checks**, and **15 = 19 − A3/A4/C3/C4**. The real ownership instrument supplies 25 checks and 43 controls, giving
112 and 58. The shared file now changes only these two expected values and their derivation text; the assertion
and real ownership/control calls remain. A focused invocation executes the corrected test against the same
complete tested production/probe copy: **one test PASS**, including both arithmetic assertions and the real
ownership controls. The full 349-test sweep above is its actual pre-correction result and is
not relabelled as a new pass. Full tools and census must be green after QA folds B426/B427 into the brief.

PIN re-synced? YES

This line reports the measured native/console/deferred/feature pins. It does **not** claim that the unfenced
warning table has been re-pinned or that the implementation gate passed.

The complete mutation union finishes **61 batteries / 983 RED / 1 known unusable B342 / 984 configured /
zero vacuous**. `selectors.json` derives S from `TARGET_SRC` intersected with changed inputs and unions it
with all 61 batteries from the 8b freeze. Every configured pattern matches once. Every worker's clean
baseline is 2950/195768/0. The sole unusable result is the existing `sliceBmac` M04 compiled survivor;
its exit 1 and exact output are preserved. All other batteries exit 0. No battery, failed control or
interrupted run was omitted from the final total.

### 9.5 Preserved input identity and resume

`final-gate-inputs.json`, `final-boards-inputs.json` and `union-inputs-inputs.json` retain the actual full
input inventories used by those instruments. The complete 361 production/test/tool/config records
include eight deletion markers. **360 records remain identical across all three tested snapshots and the
shared checkout.** The only later primary-input change is B428's narrow `tools/test_probe_features.py`
repair; both hashes, its exact diff and focused test output are retained. All production, native, corpus,
board, reference, probe and mutation inputs are unchanged by that repair. Nineteen surviving v2 owner
files remain byte-identical to the fresh base; comment-only edits in the four explicitly permitted core/
fixture files preserve every non-comment token. The old reference scripts and all three unfenced B426/
B427 files remain byte-identical to base.

The source audit finds no executable use or include of the deleted implementation. Explicit exclusions
are recorded by file: historical/comment text and negative absence-check strings in
`tools/gen_command_inventory.py` and `tools/probe_console_sink/structural.py`; B426's positive legacy-surface
expectation is separately reported, not exempted into a full absence PASS. The three kept mirror accessors
have only their real/stub definitions, with no reads; `admin_load` has those definitions plus the one
boot call.

`post-gate-deltas.json` also lists receipt/register reporting and the bench-only restoration of an extra
Part 55a wording edit, so the final bench edit is confined to the brief's 9.9/57e fence. No other previously
inventoried input drifted. All newly added checkpoint artifacts are reporting material, not gate inputs.
The brief and five re-inventoried QA preparation inputs were preserved; only the maintained register is
subsequently updated by the coder's required findings.

`checkpoint-overlay.tar.xz` includes **all dirty tracked contents and all prior untracked inputs** outside
its own evidence directory. Together with the base commit and `checkpoint-deletions.json`, it reconstructs
the complete candidate in a new private checkout. `checkpoint-inputs.json` and `artifact-sha256.json`
identify the final state, with explicit exclusions for circular metadata. Both HEADs remain pinned,
`git diff --cached` is empty, and the simulator stays clean. Nothing was committed or staged in either
shared repository. The underlying isolated trees/builds also remain at `/tmp/mr-s9-r4-j37ybea_`.

**Next dispatch:** QA folds B426 and B427 into a re-pinned brief at the same base. Their concrete narrow
patches are already available for review; they require no new owner decision. Resume the preserved
candidate, apply those fenced updates, run the required chain and freeze successfully before independent
QA. Do not infer a full tools PASS from B428's focused correction, or a census PASS from the attributed
lower counts. Independent QA, Part 57e on metal and the subsequent Slice 10 brief remain pending.

## 10. Revision-5 scoped return — source-validation PASS (2026-09-19)

Owner dispatch: apply the three fenced instrument changes, rerun tools discovery, census and the stock
board pair, then freeze for independent QA. Revision 5 SHA-256
`6eb070f1ada27081e96d84564a0655a53e3b011f71a43a03109a1aebcb2a2b9e` at unchanged MeshRoute
`84edd3ebfb08d807645d077269bace145ac2a2ab` / simulator `6585649ea5a780f0542b2931853a667be56a5b2b`.
The complete preserved checkpoint was compared before editing: only five QA inputs changed (brief,
register, design, tracker, MEMORY); every implementation/test/tool input matched. These inputs are
inventoried in the revision-5 preparation snapshot. No commit is a prerequisite.

The three folded changes match the actual source: B426 removes the unit fixture's positive legacy-surface
expectation while retaining both surviving surfaces; B427 re-pins the six explicit environment names in
the runner and §B87; B425 captures Git's NUL-delimited porcelain status before hashing and accepts only a
listed, absent path already reported as an unstaged tracked deletion (` D`). A deletion has an explicit
presence tag in the tree hash and a `deleted_files` entry in the manifest. The independent qualification
field inventory also compares that list. Other missing inputs retain the original fail-loud message.

B425 controls extend the existing source-identity test (no test retired or extra top-level case): real
Git deletion/restoration and repeatability, deleted versus empty-file identity, then actual listed inputs
removed before hashing (both untracked and tracked), each required to fail. The three focused identity/
qualification tests pass. The preserved B428 arithmetic repair is unchanged. These are instrument-only
edits; all production, native, corpus and mutation inputs remain at the §9 checkpoint.

The owner's scoped reruns replace §9's tools/census results and its wrapper-assisted pair. Native,
references, corpus, ABI, probes, xiao and full mutation union are inherited only after input-preservation
checks, and will be labelled as such in the freeze. The old failures remain historical evidence.

## 11. Revision-5 implementation freeze — ready for independent QA (2026-09-19)

The three authorized instrument changes are implemented. The owner's scoped return is complete:
**tools discovery 349/349, zero failures or skips; census six/six at the new pins, zero `-Wswitch`; stock
board pair PASS.** Independent QA and metal remain pending. No production, native fixture, protocol or
allocation change was added during this return. Nothing is staged or committed; simulator unchanged.

The frozen brief is revision 5, SHA-256
`6eb070f1ada27081e96d84564a0655a53e3b011f71a43a03109a1aebcb2a2b9e`. Base remains
`84edd3ebfb08d807645d077269bace145ac2a2ab`; simulator remains
`6585649ea5a780f0542b2931853a667be56a5b2b`. The [freeze directory](2026-09-19-radmin-slice9-r5-freeze/)
contains the exact commands/results, full logs, preparation and tested-input inventories, final source
hashes, complete candidate overlay and artifact index. The prior failures in §9 remain historical.

### 11.1 Fresh scoped measurements

| Instrument | This return |
| --- | --- |
| Full tools discovery | **349 tests / 0 failed / 0 skipped**; includes B426's surviving-surface/absence proof, B428's 87/15 arithmetic, and B425's new subtests |
| Focused board-tool suite | **46/46**, no skips; existing cases retained |
| B425 private controls | Three matched once and failed: deletion arm removed (expected `MeasureError` breaks the positive test), every missing input accepted (two negative subtests fail), deletion marker omitted (assertion fails) |
| Stock board pair, sequential | **gateway 203820 RAM / 572224 flash / 285 objects; mobile 211764 / 1394520 / 329** |
| Census, sequential | gateway_heltec **171**, gateway_heltec_v4 **175**, heltec_mobile **175**, heltec_v3 **175**, heltec_v4 **179**, heltec_v4_mobile **179**; all six PASS, `-Wswitch` zero |
| Whitespace and preservation | Both repos clean under `git diff --check`; all frozen primary inputs preserved; simulator clean |

The pair used the checked-in stock `tools/measure_board.py` and `/usr/bin/git`, with ordinary snapshot
indexes, no `GIT_INDEX_FILE`, no PATH wrapper and no staging. Each manifest records all **eight** tracked
deletions. Its source identity includes an explicit presence tag and a `deleted_files` list; the list
is also in the independently pinned qualification-field controls. Git's NUL-delimited porcelain status
is captured **before** listing/hashing. Only an absent path already marked ` D` is admissible. The real-Git
unit controls remove both untracked and tracked inputs after listing; both retain the existing
`source input disappeared while hashing` refusal. Restoring contents restores the tree hash; a deletion
cannot substitute for a live empty file. No test was dropped to keep the 349-case discovery pin.

Fresh RAM, flash, objects, loadable sections, normalized symbol inventory and symbol totals are exactly
equal to the §9 candidate pair. Consequently the measured base deltas remain **gateway −216 RAM / −2784
flash; mobile −8 / −756** with the §9.3 symbol/section attribution and its explicitly disclosed residuals.
Gateway payload is byte-identical to the checkpoint. The mobile payload hash differs across the two
scratch paths (the known B262 path-dependent Xtensa artifact); no cross-path payload identity is claimed.
The pristine base/checkpoint ELFs remain in §9's archive; this return archives the fresh stock pair too.

B427's paired §B87 record names the deleted including TU and the exact removed diagnostics. The earlier
fresh V3/V4 A/B logs still provide that attribution; this census independently reproduces all six final
counts. Revision 5 §1's prose says “two V4 envs” while its explicit table correctly names three; **B429**
is a non-blocking author counting typo for the QA landing. The frozen brief is unchanged; the implemented
six-name table follows the explicit values.

### 11.2 Inherited results, source-bound rather than rerun

Per the owner's scoped return, the other §9 measurements were **not rerun**. Hash comparison to the
actual full-chain inputs proves all production, native-test, reference, corpus, ABI, probe and mutation
inputs unchanged. Of the 361 primary records (including eight deletions), 356 match the full-chain tree;
the five differences are the four newly exercised instrument files plus B428's already corrected feature
unit test. The three revision-5 changes also include the paired §B87 documentation update. All remaining
changes since the tested snapshots are receipt/register reporting and this evidence directory.

The valid inherited results are: native **2950/195768/0 with zero skips**; both references **94/94** plus
the frozen legacy frame and their controls; corpus **36/36 byte-identical**, s18
`32afbf11e43b4bf9d0bd470ad502ba0a`; Node **235248 / 122176 / 157344** unchanged; all mandatory probes and
controls at their §9 pins; inventory **197**; authority six controls RED; xiao_mobile **176596 / 699548**;
mutation union **61 batteries / 983 RED / 1 known unusable B342 / 984 configured / 0 vacuous**. The known
supplemental B418 board-UI failure set remains W49/W51/W54. No new native, corpus or mutation run is implied
by this return's tools/census/pair PASS.

PIN re-synced? YES

### 11.3 Handoff identity

The preparation and both tested snapshots include every tracked and untracked candidate input, not just
HEAD. `inherited-gate-input-proof.json` names the exact differences from the full-chain tree.
`frozen-primary-inputs.json` binds the current production/test/tool/config records to the shared tree and
both tested snapshots. `freeze-inputs.json` inventories the final complete tree, with explicit circular
metadata exclusions; `artifact-sha256.json` hashes the archived evidence. `freeze-overlay.tar.xz` includes
all dirty tracked contents and prior untracked inputs outside its own evidence directory, and
`freeze-deletions.json` lists the eight removals. Together with the base commit these reconstruct the
complete uncommitted candidate in an isolated checkout.

The three stock reruns supersede §9's tools/census failures and wrapper-assisted board measurement.
B425/B428 implementation closure awaits the independent gate; B426/B427's author fold-ins are implemented.
The owner now runs independent QA on this freeze. Part 57e and the other metal obligations remain pending;
no firmware was flashed and no metal PASS is claimed.
