<!-- Coder: OpenAI Codex -->
# Remote-admin v2 Slice 7a — coder preflight STOP, 2026-09-07

**Status: STOP-1 before implementation.** The rev-1 brief has source/fence disagreements below. This is
not a software-completion report or a QA verdict. No production, tests, tools, inventory, brief, register,
bench, design, rulings or simulator file has been edited by the coder. This evidence file is the sole
coder-created repository file. QA owns the brief corrections and maintained-register landings (M1).

## 1. Observed inputs

- MeshRoute `/home/staszek/MeshRoute`: HEAD `dd593a2b67209f36c5baeb77ed29c27077fd2be5`,
  `slice 7a spec`; ancestry contains `eb6d46b` (`Slice 6`). The new commit changes only the register.
  Initial status is exactly the untracked brief below, not the empty committed-preparation status described
  in its opening paragraph. This is recorded provenance; the source disagreements independently require STOP.
- Brief: `docs/superpowers/plans/2026-09-07-radmin-slice7a-activation-config.md`, rev 1, read completely.
  SHA-256 `873b93ae086f2b52e405c09820a46df4a11e7707cde8a06e6b82a3ee19cc287e`.
- Simulator `/home/staszek/lora-universal-simulator`: clean at
  `06746a97de5764415d6fcef10b97bca90569b9c7`; existing `build/orchestrator/lus` MD5
  `db6582a171a6d785720e324c2cfe43f1`, matching the brief. No rebuild performed in this preflight.
- Roles: Codex implements and reports evidence; QA authors, gates and lands documentation; owner rules
  and commits. C1's refactor/feature separation and D4's no-coder-commit rule both remain binding.
- Register next-free checked at `docs/2026-07-30-open-bug-register.md:272`: **B353**. The identifiers
  B353–B358 below are proposals for QA to land, not already registered rows.

## 2. Proposed findings and requested fold-ins

### B353 / S7a-C1 — the append occupies old padding; v24 is not rejected

Brief §3, §4.3 and §6.4 predict `sizeof(Blob)` +4, automatic rejection of v24 by size, and board RAM +8
(`static Blob cur` +4 plus the new global +4). The actual record has alignment 8 and seven tail-padding
bytes: `src/device_nv.h:131` puts `team_key_team_id` at 268, and `:132` puts `team_key_active` at 272.
Appending the proposed `uint32_t` puts it at **276**, consuming existing padding, with **no size increase**.

Measured by compiling the current declaration and a disposable appended candidate (no repository edit):

| Compiler ABI | Current size | Appended size | New field offset |
| --- | --- | --- | --- |
| native | 280 | 280 | 276 |
| gateway / ARM Cortex-M4 | 280 | 280 | 276 |
| heltec_mobile / Xtensa | 280 | 280 | 276 |

The board compilers were taken from the Slice 6 ruled-pair manifests at
`.pio-measure/s6-final-qualified-pair/{gateway,heltec_mobile}/manifest.json`. These are layout-only
compilations using the real record declaration above the backend gates, not new firmware builds.
The candidate declaration was extracted from `device_nv.h`, renamed and appended in compiler stdin;
it was not reconstructed field by field. ARM used its Cortex-M4/thumb/hard-float ABI flags.

`load()` at `src/device_nv.h:1418` calls `blob_valid_range(..., 2, kVersion)` (`:1420`), not exact-version
validation. The predicate at `:709` accepts an equal-size v24 record when its upper bound becomes 25.
An executable demonstration using that real predicate and the appended candidate produced:

```text
Blob size=280 align=8 team_key_team_id@268 team_key_active@272 last_end=273
current=280 appended=280 offset=276 v24_accepted_by_v25_range=1 interpreted_old_padding=305419896
```

The final number is an intentionally injected old-padding pattern, `0x12345678`, not a value observed on
hardware. It proves the loader has no barrier against treating former padding as a persisted setting.
`nv_load_stamped` (`src/firmware_config.cpp:94`) would then stamp the accepted record with the new version.

Requested fold-in: explicitly authorize the cfg typed-loader's version policy change, preferably reusing
`blob_valid_exact` (`device_nv.h:713`) for v25, so reprovisioning does not depend on incidental size growth.
Pin equal-size v24 refusal as well as v25 acceptance, update the touched policy comments, and measure/pin
size 280 and offset 276. Re-derive RAM: the named static Blob contributes **zero growth**; the new raw
global contributes four bytes before any other compiler/linker effects. Do not manufacture record growth
or introduce a migration arm just to satisfy the mistaken prediction. The current fence authorizes only
the field/version/comment, not this necessary typed-loader behavior change.

### B354 / S7a-C2 — the new cfg key never reaches its arm; capacity also hides an existing key

At `src/firmware_config.cpp:251`, `char key[20]` and the loop at `:252` admit at most **19 characters**.
`remote_action_activation_ms` has **27**. Compiling the five actual tokenizer lines extracted from the
handler, with the brief's input, produces:

```text
requested_key_length=27 parsed_key=remote_action_activ parsed_value=ation_ms 20000 exact_key_match=0
```

Adding only the new `strcmp` arm cannot work. Brief §4.5's restriction on other handler edits and §5's
new-arm-only fence need to authorize key admission/capacity work and its proof.

There is also an existing affected arm at `firmware_config.cpp:396`: `gw_announce_interval` is 20
characters. The same executed extraction with that input produces:

```text
parsed_key=gw_announce_interva parsed_value=l 20000
```

Thus enlarging the common buffer also makes an existing unreachable arm reachable. That behavior change
must be stated and tested, not described as only accommodating the new key. QA may keep both manifestations
in this row or give the historical arm its own row when landing the finding.

Requested fold-in: choose the bounded key-admission mechanism, cover the full new token and overlength
tokens without silent truncation, explicitly account for the existing 20-character arm, and authorize the
required tests/structural wiring. Define strict numeric admission for the new zero-sentinel setting too;
malformed text must not silently become `0=default`. U1 candidates already exist, notably
`src/firmware_config_parse.h:524` (`parse_seq_arg`, decimal/u32 overflow/tail checks) and `:134`
(`parse_index_strict`, whole-token decimal with a signed-32-bit bound). Their different grammars/bounds
must be considered before selecting or extending one; no new parallel parser is presumed authorized.

### B355 / S7a-C3 — the specified live-PHY binding calls a private Node method

Brief §4.2/§4.4 names `g_node.max_data_sf()`. Its declaration at `lib/core/node.h:2322` is under the
`private:` label at `:2258`. A disposable syntax-only compilation against the actual header fails:

```text
private-access compile exit: 1
error: 'uint8_t meshroute::Node::max_data_sf() const' is private within this context
lib/core/node.h:2322:13: note: declared private here
```

The diagnostic came from compiling this caller, not from a full firmware gate:

```cpp
#include "node.h"
uint8_t read_live_max_sf(const meshroute::Node& n) { return n.max_data_sf(); }
```

`node.h` is expressly out of fence. Reimplementing its bitmap scan in firmware would fork the authority
the slice is supposed to reuse. Requested fold-in: authorize a minimal public const exposure of the
existing query, with no Node field/layout change, and assign it to the proper attributable step.

The same binding should explicitly use the already-public `active_bw_hz()` / `active_cr()` at
`node.h:547` / `:551`. The MAC's ACK timer uses those (`node_mac.cpp:2485`); raw global config BW/CR can
miss a per-layer override. `max_data_sf()` itself (`node_mac.cpp:1050`) uses the active-layer-mirrored
bitmap and returns 0 when it is empty. Specify fail-closed handling of that no-admitted-SF state before
airtime/slop evaluation, rather than inventing a fallback SF or reporting a usable budget for SF0.
`airtime_ms` (`lib/core/airtime.cpp:14`) does not validate its inputs. This clarifies the live-input
contract; the coder has not added a new state or altered the five-state policy.

### B356 / S7a-C4 — the inbox probe needs the new global despite stubbing cfg set

Brief §7 predicts the inbox probe unchanged because it does not compile `firmware_config.cpp`. It does
compile **real `firmware_commands.cpp`** (`tools/probe_inbox_verbs/run.sh:93`), including `dump_cfg`
(`src/firmware_commands.cpp:640`) and `make_cfg_extras`. The proposed read-out and live binding introduce
a reference to `g_remote_action_activation_ms`, whose production definition is in `fw_main.cpp`.

That file is not linked into the probe. Its fake firmware globals live in
`tools/probe_inbox_verbs/probe_main.cpp:189` onward: the corresponding BLE globals are at `:241`, and
there is no definition of the proposed global. The stub `handle_cfg_set` at `:254` cannot satisfy this
different dependency from the real command translation unit.

This is a source-derived link requirement, **not a claimed observed post-edit linker failure**: no
implementation was written to provoke it. Requested fold-in: add the probe's fake-global definition and
any initialization to the fence, retain both accept/client arms, and specify executed read-out/live-input
coverage with re-derived pins if checks are added. Also name where the single live binding is declared
for the config handler and boot caller; do not fork it per translation unit.

### B357 / S7a-C5 — mutation union and per-step gate pins need correction

The feature changes both `lib/console/console_json.h` and `.cpp`. The actual `TARGET_SRC` map in
`tools/probe_ui_model_mutations.py` assigns **four** existing batteries to them:

- `sliceAjson` (`:368`) and `sliceGjson` (`:489`) cover `.cpp`.
- `sliceDack` (`:423`, one control) and `b134ack` (`:521`, two controls) cover `.h`.

Brief §7 lists only the first two. Add the header pair to the changed-source selector and gate the union.
`cmdauthority` (`:114`) is also changed-source, since the feature edits its policy header, whether or not
it is additionally selected as a dependency. Report both selectors and deduplicate only the executed union.

The two steps also need separate prediction rows. §7 currently asks for **204 inventory rows after both
steps**, but 7a-0's fence cannot add the cfg row: its inventory stays **203**, and 7a changes it to 204.
Likewise distinguish old native cases staying intact from the new helper tests' count/assertion increase.
Rebuild actions prove compilation; the pure refactor's binary hash should be measured, not forced to
change as a substitute for rebuild evidence. Corpus bytes must stay exact in either case. The feature's
JSON emitter really is retained in the current simulator binary (confirmed below), so its predicted
movement has stronger support than merely saying a source file is compiled.

Requested fold-in: give each step its own selectors, pins and prediction table, and state the owner-commit
checkpoint after the refactor gate. The coder cannot create the two commits. Preserve B342's honest M04
classification; reconcile its named pre-existing exception with the unconditional unusable-control STOP
wording rather than silently counting it red or repairing it out of fence.

### B358 / S7a-C6 — operator rationale overstates the existing authority; a quote is a paraphrase

Brief §4.7 says an operator can already run every disruptive command bounded by the setting. That is
false in the implemented table: `src/firmware_command_authority.h:147` classifies `ota` as owner and
disruptive; `:159` does the same for `regen`; the `factory_reset` rows at `:119` and `crashtest` rows at
`:104` are further counterexamples. This does **not** itself overturn the proposed operator class for
ordinary bounded configuration. QA should correct the rationale and retain the owner's classification
authority, without presenting a broader privilege as existing policy.

The quoted closing rule at brief `:186` is also a paraphrase. Its actual authority is
`docs/superpowers/plans/2026-09-06-radmin-authority-classification-proposal.md:143`:

```text
Rows that Slice 4/5/6 add later are classified at
their closure under the same policy and appended here by the Author; the checker refuses an unclassified row.
```

Use an exact excerpt or label the generalization as the Author's reading, not a verbatim quote. The
opening reference to later slices is specifically 4/5/6, not an already-quoted universal rule. No owner
ruling, policy row or quoted authority was changed by the coder.

## 3. Checks completed; limits of this report

- Read the current guidelines/roles, brief, source anchors, relevant rulings and authority table. Verified
  the MAC's existing CTS-window operands include `terminal_cts_wire_len`, while the on-air exchange CTS
  is the ordinary three-byte CTS. That distinction must survive the refactor and production budget.
- Re-ran the existing native binary with
  `MR_RADMIN0E_TABLE=1 ./.pio/build/native/program --test-case='radmin 0e-D*'`:
  **2 cases / 58 assertions / 0 failures; 2837 cases skipped**. This confirms the already-registered B352,
  not Slice 7a completion. The binary was not rebuilt during preflight. It prints:

  ```text
  RTS 88; CTS 78; terminal DATA 221; ACK 78; gap 5; busy retries 60
  CTS wait attempt 2: 665; attempts 0/1/2: 167 / 333 / 665
  ACK wait 311; requeue 5000; budget 6506; default 13012; ceiling 299999
  ```

  Substituting the ruled sum 1165 for 665 yields 7006/14012 as the brief requires.
- Ran the native and two board-ABI layout compilations, the equal-size-v24 predicate demonstration,
  both extracted-tokenizer examples and the expected-failing private-access compilation above.
  Disposable outputs: `/tmp/mr-s7a-preflight-DIYHlf/` (`blob-layout`, `append-policy`, three
  `*-blob-layout.o` files, `cfg-key`, `cfg-key-existing`). Object symbol sizes are `0x118` (280) for
  both layouts and `0x114` (276) for the offset carrier. These are diagnostics, not replacements for
  the ABI probes or board pair.
- Checked simulator build flags/link input and `nm -C build/orchestrator/lus`: `write_cfg` is present
  at `0x190370`, along with `pushkind_name`. The console source is compiled and its cfg function is
  retained, although corpus code does not call `write_cfg`. No simulator edit is needed for that emitter.
- No full native build/run, corpus replay, mutation union, standing-probe chain, inventory regeneration,
  census or board pair was launched: implementation is stopped at preflight, not declared ready (D3).
  No metal work performed; Part 57a remains a future QA-authored residue.

Resume after QA lands the source corrections/fence changes and supplies the revised dispatch contract.
At that point re-check its content binding and anchors before the measured baseline and 7a-0 work.

## 4. Revision 2 receipt — preparation commit still pending, 2026-09-07

Read revision 2 completely and checked the six registered fold-ins. Its SHA-256 is
`7526fbc89a3745473f661ebdde8be6bea6814cbc18dc99d1e56359f7d3652d26`.
The Author chose a named load-version floor of 25 while retaining the range predicate (B353), the
longest-key-derived buffer and generator-backed bound check (B354), the public declaration move (B355),
the inbox fake global (B356), the missing JSON-header batteries and 203/204 stage distinction (B357),
and corrected classification wording without changing operator admission (B358).

The six source files hashed during the original preflight still have identical hashes. In addition,
`git diff eb6d46b -- lib src test tools platformio.ini` is empty. These are brief/fence corrections,
not yet implemented fixes. QA has landed B353–B358 in the working register; next free is B359.
No new finding number is allocated by this receipt.

The revised opening paragraph expressly requires the owner commit that **adds the brief**, and an
empty starting status. Observed HEAD remains `dd593a2b67209f36c5baeb77ed29c27077fd2be5`; status is:

```text
 M docs/2026-07-30-open-bug-register.md
?? docs/superpowers/evidence/2026-09-07-radmin-slice7a.md
?? docs/superpowers/plans/2026-09-07-radmin-slice7a-activation-config.md
```

Simulator remains clean at the same full hash recorded in §1. No production edit, baseline build,
gate run or commit was performed on this resume. Only this receipt was appended to the evidence.
Dispatch remains at the revision-2 preparation-commit checkpoint, before 7a-0 implementation.

## 5. Slice 7a-0 measured start and implementation scope — 2026-09-07

**Current phase: the wait-window refactor only; implementation gate pending.** The owner completed
preparation commit `d51d62b063107c6d8c7e2e1268ef654bff099daf`, which adds the brief and this evidence.
The two additions were still staged at the first status check and committed during read-only preflight;
the final check was clean before any build or implementation. Revision-2 content hash is unchanged
from §4. Simulator remains clean at `06746a97de5764415d6fcef10b97bca90569b9c7`.

Logs for this phase: `/tmp/mr-s7a0-6Om1sn/`. Measured before production edits:

- `base-native-build.log` then `base-native.log`: **2839 / 121831 / 0** (all cases executed).
- `base-simulator-build.log`: canonical rebuild check; `base-corpus.log` and
  `base-corpus/manifest.json`: **36/36 validated, 36/36 anchors**, s18 `32afbf11` / 269517 / 0.
  The s18 value was read from the current `simulation/BASELINE.md:10514` authority.
- `base2-boards.log`, `.pio-measure/s7a0-base2-d51d62b/{gateway,heltec_mobile}/manifest.json`:
  sequential pair PASS, gateway **197180 RAM / 550204 flash / 285 objects**, heltec_mobile
  **207748 / 1371260 / 329**. Both ELFs reproduce the Slice 6 final capture hashes exactly.

**Execution incident, not a passed capture:** the first board invocation (`base-boards.log`, exit 2)
refused with `ERROR: normal .pio/ changed during measurement`. The coder had launched the native
build concurrently; it changes the normal `.pio` metadata that `measure_board.py:698` deliberately
guards. No source changed and the guard was not weakened. After native completed, the pair was
re-run sequentially and alone under a fresh output path as recorded above. The refused invocation
is retained and is not used as a baseline. All subsequent board captures must be isolated from
native/other commands that change normal `.pio` metadata, not only from source edits.

Prediction before the post-edit gate: no state, Node layout, NV, wire, command, inventory or scheduler
change. All 36 streams remain byte-identical; board RAM and object counts remain unchanged. Record
actual flash/ELF/payload differences with attribution rather than treating debug metadata as runtime
movement. Inventory remains **203** and all standing probe/tool pins remain unchanged. Four new
native cases add **28 assertions** (7 reference + 10 retry/slop + 6 ACK operands + 5 synthetic unsigned
arithmetic controls), predicting **2843 / 121859 / 0** without changing any old native case.

Authorized production edits are only `lib/core/mac_wait_windows.h` and the two call sites plus its
include in `node_mac.cpp`. The M-broadcast arm, flight operands, timer IDs and all existing comments
are retained. ACK operands are evaluated into locals in their source order, then the helper's one
delay is used for both arming and the stored timeout deadline. The helpers retain unsigned arithmetic;
the overflow controls are synthetic refactor checks, not admissible-PHY examples. No saturation or
configuration policy is added to the MAC. The feature and `max_data_sf` declaration move wait for 7a.

Mutation selectors for this phase:

- Changed-source: new `macwait` (10) and all five existing `node_mac.cpp` targets, re-derived from
  `TARGET_SRC`: `b159mac` (2), `b161mac` (1), `b20mac` (11), `grantadmit` (1), `sliceBmac` (4).
- Dependency/historical: retain the brief's named `radmin2codec` (66) and `cmdauthority` (16) as
  conservative arc checks even though this refactor adds neither a codec nor policy consumer.
- Execute the deduplicated union: **111 entries**, with B342's pre-existing `sliceBmac` M04 reported
  honestly as its documented non-discriminating entry, never relabeled RED or deleted. No other
  unusable/surviving entry is predicted. Future-feature targets absent from this tree are not claimed
  as executed 7a-0 tests; the 7a union is re-derived when that feature is implemented.

This phase stops for independent QA and the owner's refactor commit before any 7a feature edit.

## 6. CURRENT REPORT — 7a-0 implemented and measured; independent QA pending

**The wait-window refactor is complete for QA intake, not the Slice 7a feature.** All required
instruments have finished. The mutation result retains the brief's explicitly named B342 exception:
110 RED / 1 pre-existing unusable, not an all-red union. No new control failed or survived. The optional
old-0e ABI-manifest diagnostic found a separate pre-existing stale pin (§6.4), which is not repaired
out of fence. No independent QA PASS, owner approval or metal result is claimed.

Base and content bindings remain those in §5. No production correction was needed after the initial
refactor edit. Only the two MAC wait-window call sites and the include changed in existing production
code. `source-audit.log` mechanically proves the rest of `node_mac.cpp`, its M-broadcast arm and all
old comments unchanged. The new helper file contains only the two pure, inline arithmetic functions.
No field, timer, state owner, remote event producer or configuration consumer was added.

### 6.1 Executed gate

Artifacts below are under `/tmp/mr-s7a0-6Om1sn/` unless a repository path is given.

| Instrument | Measured result | Artifact |
| --- | --- | --- |
| Native build then actual binary | **2843 cases / 121859 assertions / 0 failed**, all cases executed | `final-native-build.log`, `final-native.log` |
| New tests in isolation | **4 cases / 28 assertions / 0 failed**, 2839 old cases skipped by this diagnostic filter only | `new-native.log` |
| Simulator rebuild | **5 compile/link actions**: both MAC variants compiled, both core archives linked, executable linked | `final-simulator-build.log` |
| Simulator executable | MD5 **db6582a171a6d785720e324c2cfe43f1 → 10b649123db83486adfdedd5c5a6ca2c** | retained corpus input snapshots |
| Corpus | **36/36 validated / 36/36 anchors**, every stream byte-identical; s18 **32afbf11 / 269517 / 0** | `final-corpus.log`, `final-corpus/manifest.json`, `corpus-stream-compare.log` |
| Board ABI, required default set | **191 checks / 9 controls RED / 0 unusable**; Node native **224136/8**, heltec **117912/8**, gateway **150504/8**, unchanged | `board-abi.log` |
| B278 row ABI | **42 measurements / 6 controls RED** | `b278-abi.log` |
| Console sink | **6 profiles / 720 checks / 62 structural / 905 BLE guard / 6 ownership / 3 ownership controls / 114 controls / 0 unusable** | `probe-console_sink.log` |
| Inbox verbs | ACCEPT **362 checks / 39 controls**; CLIENT **360 / 42**; both arms pass, zero unusable | `probe-inbox_verbs.log` |
| Firmware UI | **223 controls / 0 unusable**, existing l2/v3/BLE arms and coverage report retained | `probe-firmware_ui.log` |
| Custody USB | **27 checks / 10 controls / 0 unusable** | `probe-custody_usb.log` |
| BLE line | **40 checks / 8 controls / 0 unusable** | `probe-ble_line.log` |
| Features | **9 cells / 120 checks / 59 controls / 0 unusable**, plus **40 ownership controls**; nine-file ownership census unchanged | `probe-features.log` |
| Tools unit sweep | **342 tests, OK**, exit 0, 645.167 seconds | `tools.log` |
| Inventory | bare, `--write`, `--check`: **203 rows**; generated file remains byte-identical to the committed base | `inventory-{bare,write,check,check-final}.log` |
| Authority checker | table/header/inventory agree; **6/6 selftests RED** | `authority.log` |
| a0 checker | **10/10 selftests RED**, gate passes | `a0.log` |
| DATA-type checker | **207 active source files**, no forbidden numeric type use | `literals.log` |
| Warning census | all six builds pass; **173/178/177/177/182/182** respectively gateway_heltec / gateway_heltec_v4 / heltec_mobile / heltec_v3 / heltec_v4 / heltec_v4_mobile; **zero switch warnings** | `warning-census.log` |
| Ruled board pair | sequential pair PASS; RAM/object counts unchanged, flash attributed below | `final-boards.log`, `.pio-measure/s7a0-final-d51d62b/` |
| Mutation union | **111 executed: 110 RED / 1 unusable (existing B342 M04 only)**; source match counts one, real target files hash-restored | `mutation-summary.log`, `mutation-*.log` |

PIN re-synced? YES — 2839/121831 + `test/test_mac_wait_windows.cpp` 4/28 = **2843/121859**.
No existing native test file was edited. The source-level count derivation and the actual isolated
four-case run agree; no assertion was removed or reclassified to preserve the pin.

All six `--no-neg` modes were also executed (`only-*.log`) and exited 0. Those are diagnostics, not
controlled gate results. Existing B303 remains visible in inbox's combined PASS/probe-only wording;
existing B350 remains visible in the firmware-UI probe-only run ending in bare PASS with zero controls.
Neither wording is used as controlled-gate evidence. The tools sweep includes deliberately printed
negative-control FAIL messages and existing ResourceWarnings; its actual unittest result is 342/OK,
not a verdict inferred from a grep of those subtest messages.

### 6.2 Corpus comparison scope and byte identity

The stock `run_corpus.py --compare` is a **same-input determinism** comparator: it requires equal
`lus_sha256` as well as equal streams. Applied to this before/after refactor, it correctly returned 1
with exactly one difference, **`lus_sha256`** (`corpus-compare.log`). That invocation is not reported
as PASS and neither manifest was edited to conceal the changed producer.

For the cross-revision stream claim, `corpus-stream-compare.log` records both calls to the existing
`run_corpus.validate_run` (retained inputs, stream rehashes, complete successful 36-row sets), followed
by comparison of every `COMPARISON_TOP` field except the explicitly changed producer hash and every
`COMPARISON_SCENARIO` field. All full MD5/SHA256 hashes, sizes, event/assertion counts and scenario
inputs agree. A deep `diff -rq` of the two stream directories also returned 0. The comparison's
positive control changes only an in-memory copy of s18's expected output SHA-256 and is detected as
exactly `s18_meshroute.output_sha256`; the retained inputs/manifests/streams stay untouched.

The final streams contain no `radmin*` or remote-activation event in their `type`/`emit_type` fields
(`remote-events.log`, no matches). The simulator repository has no source or CMake diff. Its rebuild
is caused by the authorized MAC edit, not a simulator edit or a forced output rewrite.

### 6.3 Ruled-board cost, fully attributed

Both captures use the same qualified per-environment build paths, toolchain state and fixed build
identity. These are measurement images, not provenance-bearing images to flash. The starting capture
is `.pio-measure/s7a0-base2-d51d62b/`; the final capture is `.pio-measure/s7a0-final-d51d62b/`.

| Board | RAM before → after | Flash before → after | Objects before → after |
| --- | --- | --- | --- |
| gateway / ARM | **197180 → 197180 (0)** | **550204 → 550220 (+16)** | **285 → 285** |
| heltec_mobile / Xtensa | **207748 → 207748 (0)** | **1371260 → 1371256 (−4)** | **329 → 329** |

`board-attribution.log` enumerates every normalized symbol-row difference. On ARM, the only sized
function movement is `Node::start_ack_timeout`, **396 → 408 bytes (+12)**. Two compiler-generated
switch-table labels are renumbered, with their sizes unchanged at 20 and 4 bytes. The remaining
**+4** is located by symbol addresses: the gap between `host_row_live_direct` and `handle_rts` grows
**0 → 4** (`gateway-alignment.log`). `.text` alone grows 16; `.data` and `.ARM.exidx` do not move.

On Xtensa, the only normalized symbol movement is the same ACK function, **197 → 195 bytes (−2)**.
Its following alignment gap shrinks **3 → 1 bytes (−2)**, so `duty_defer_fire` starts four bytes
earlier (`ack-symbol-layout.log`). `.flash.text` alone shrinks four bytes; the other allocated
PROGBITS sizes are unchanged. No state or new object accounts for either flash delta.

Final payload SHA-256: gateway
`159ffba8fe1fa6681a1ae59c96f0f647885afcfaf29077229c1b2525d37a260f`;
heltec_mobile `08cb4671a68218e1d23ab45316096a5819fbd8d21495e9e8691d992b47781f67`.
The changed ELF/payload hashes are real code-generation movement, not dismissed as debug noise.

### 6.4 Finding proposed for QA's maintained-register landing

**B359 / S7a0-C1 — optional 0e ABI manifest still pins the pre-Slice-5 live timer wheel.**
`tools/radmin_0e_abi_pins.json:196` (native), `:303` (heltec) and `:410` (gateway) still give
`meshroute::TimerWheel` size 824. The extra-manifest diagnostic
`python3 tools/probe_board_abi.py --extra-pins tools/radmin_0e_abi_pins.json` fails at its native check:

```text
FAIL: [native] sizeof(meshroute::TimerWheel) = 832, pinned 824 (delta +8)
```

This is `diagnostic-extra-abi.log`, exit 1, **not** the required default ABI probe's result. Only that
native failure is claimed from the extra-manifest invocation; it did not establish successful extra
measurements on both boards. The real wheel has `kCap = 92` since Slice 5
(`lib/hal/timer_wheel.h:25`), and the native fixture already compares it with the 92-slot mirror
(`test/test_radmin_characterization_0e.cpp:338`). The manifest and wheel header are byte-identical to
the committed pre-refactor base, so this drift was not introduced by 7a-0. The required default probe
was rerun without this optional overlay and passed its 191 checks and 9 controls as listed above.

Suggested closure: a separately authorized update of the live-wheel overlay pins, measured on all
three ABIs and distinguished from the deliberately historical 91-slot candidate. No manifest or
timer-wheel edit is folded into this refactor. The register is still QA-owned and untouched by Codex;
B359 is proposed here after re-checking that it remains the next free number.

### 6.5 Handoff and remaining work

The refactor's five-file package is:

- Modified `lib/core/node_mac.cpp` and `tools/probe_ui_model_mutations.py`.
- New `lib/core/mac_wait_windows.h` and `test/test_mac_wait_windows.cpp`.
- This evidence file, with all preflight/receipt history retained.

No production/config/test/tool path outside that phase's fence changed. The generated inventory is
unchanged despite executing `--write`. The firmware NV version remains 24; the raw setting/global,
cfg arm, text/JSON output, public SF declaration and boot line have **not** been implemented yet.
Therefore no claim is made that B352–B354's feature obligations have closed or that Part 57a is ready.
The refactor adds no new metal-only behavior and no bench part of its own.

Next: QA independently gates **7a-0**, the owner commits that refactor, then Codex records that new
base and implements the **7a feature** under the second fence. Nothing was committed by Codex.

Final read-only confirmation: `final-native-confirm.log` again reports **2843 / 121859 / 0** after
all tools and mutations; `final-simulator-confirm.log` is a zero-action rebuild with the final binary
MD5 unchanged. The exact five-file fence is verified, including the two untracked additions, and
tracked/untracked whitespace checks pass. MeshRoute HEAD remains `d51d62b`; simulator HEAD/status
remain the pinned commit and empty. All five refactor-package files are left uncommitted for QA/owner.
