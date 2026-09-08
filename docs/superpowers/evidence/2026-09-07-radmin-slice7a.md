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

## 7. Feature-phase receipt and STOP-1 — config-loader execution coverage (2026-09-07)

The owner has now committed the independently passed refactor as
`89071fb36ea74ca354ce4c830e000e3c58e4763c` (`slice 7a-0 wait-window refactor`). Both repositories
had EMPTY `git status --short` at this feature preflight. The simulator remains
`06746a97de5764415d6fcef10b97bca90569b9c7`. The committed brief, including QA's §10a, has SHA-256
`8d3516e69ae75ba379bd1acda67e0bfbadd2b0bd63df7d71ec7e3b34ad89f908`.
QA's §10a is the feature baseline; the Slice 6 figures in §1 are historical inputs, not the new
board/native/simulator comparison base. Nothing here amends the 7a-0 PASS.

### 7.1 Proposed B360 — native cannot execute the successful typed config load required by §6.4

**STOP-1 before feature edits.** Brief §6.4 (`:263`) requires native to reject a same-size v24 record
through `load()` and accept a v25 one. But native's `read_slot` returns `-1` unconditionally
(`src/device_nv.h:1406`), and the real `load` feeds that result into `blob_valid_range`
(`:1418-1420`). It cannot accept any record in this build. `test/test_device_nv.cpp:64` exercises
the pure predicate with a supplied byte count, not a successful typed load. The separate native
stub-contract case at `:461-470` explicitly requires even the current-version load to fail.

Executed read-only confirmation with the existing native gate binary:

```text
./.pio/build/native/program --test-case='device_nv: with no backend compiled every load and save FAILS LOUD; factory_erase is the no-op success'
test cases: 1 | 1 passed | 0 failed | 2842 skipped
assertions: 18 | 18 passed | 0 failed
exit 0
```

This is a targeted preflight check, not a fresh full feature gate. A native v24 rejection alone
would be vacuous here: even a mistakenly retained loader floor of 2 would reject on the absent
read. Testing `blob_valid_range(..., kVersionMinLoad, ...)` proves the predicate/constant but does
not prove that `load()` actually passes that floor.

The existing inbox-verbs probe already has the required mechanism: its byte-backed Preferences
medium (`tools/probe_inbox_verbs/fakes/Preferences.h:46-114`) runs the production typed wrappers.
Its runner explicitly records this native-versus-wrapper distinction for the earlier store tests
(`tools/probe_inbox_verbs/run.sh:197-202`, `:241-244`, `:504-514`). No new fake backend or production
test seam is needed. However, the brief permits `probe_main.cpp` only for the new fake global
(`:244-245`), excludes its runner from the feature fence, and predicts unchanged inbox pins
(`:273-274`). Silently adding executed rows/controls would therefore exceed the dispatch fence.

**Recommended QA fold-in:**

- Keep native coverage of the v25 predicate, 280-byte layout, offset 276, and the no-backend contract.
  Correct §6.4 to attribute successful typed-load coverage to the inbox probe, not native.
- Authorize `probe_main.cpp` to seed the existing medium with complete same-size config records:
  v25 accepts and preserves the new raw value; v24 and v26 refuse. Run these shared checks in both
  existing profile arms. No change to the fake medium is required.
- Add `tools/probe_inbox_verbs/run.sh` to the fence for controls on the actual `load()` wrapper
  (restored old version floor, bypassed validation, unconditional refusal), using its existing
  `nvh` shadow mechanism. Derive new per-arm check/control pins and retain all previous rows.
- While updating the already-fenced native test file, also correct its sibling test at
  `test/test_device_nv.cpp:392-403`: it still describes accepting v2 as the current config policy.
  Preserve that old intent visibly as superseded, or explicitly label a retained generic range
  demonstration synthetic; it must not continue claiming that production upgrades old configs.

B360 was checked as the next free register number before this proposal. QA owns registration and
brief changes; Codex has changed only this evidence file. No feature production, test or tool edits,
full gate run, or commit were performed. Resume the feature after QA resolves this test-fence gap.

## 8. B360 resolution receipt; further boot-order preflight STOP-1 (2026-09-07)

The owner relayed QA's B360 fold-in as revision 3. Its substantive changes are present in §0.1,
§5 and §6.4 of the brief: native owns predicate coverage, and the inbox probe owns executed typed
loads with lower/upper floor controls and re-derived per-arm pins. Those explicit amendments
supersede the older §7 prediction of unchanged inbox counts; the status heading still reads
REVISION 2. The received brief SHA-256 is
`c075d0d1c9923c1bbf3dea7c585a6a95c65fd2d9a656a077c3eb54f28df12fdb`.
This is the same implementation session resumed after a QA fold-in, not a new clean-start claim:
HEAD remains `89071fb36ea74ca354ce4c830e000e3c58e4763c`, with only the named QA register/brief
amendments and this coder evidence already modified. No production, test or tool file changed.

The isolated pre-feature board capture completed with exit 0:

```text
python3 tools/measure_board.py pair --output .pio-measure/s7a-base-89071fb --jobs 1
```

Artifacts are under that directory; command log is `/tmp/mr-s7a-1xIv1G/base-boards.log`.
No source/docs edits or normal `.pio` build ran concurrently with this capture. This baseline
measurement is not a completed feature gate.

### 8.1 Proposed B361 — the specified boot-report position precedes installation of its live PHY

Brief §4.6 asks for the activation line beside the admin-session boot line, while §4.4 requires
the one input binding to use `g_node.config()`, `max_data_sf()` and HAL slop. These cannot describe
the restored PHY at the specified position:

- `src/fw_main.cpp:795-799` restores the NV PHY into the local `cfg`, not `g_node`.
- `src/fw_main.cpp:898` calls `admin_stores_boot_report_console`; its session line is emitted at
  `src/firmware_commands.cpp:356-359`.
- `g_hal.configure` using the restored `cfg` runs later, at `src/fw_main.cpp:979`.
- `g_node.on_init(cfg)` runs later still, at `:987`; the assignment `_cfg = cfg` is in
  `lib/core/node.cpp:518`. The constructor at `:30-37` does not install that boot configuration.
- The data-SF accessor reads `_cfg.allowed_sf_bitmap` (`lib/core/node_mac.cpp:1054-1056`), and
  `active_bw_hz` / `active_cr` likewise read `_cfg` (`lib/core/node.h:547-554`).

A read-only source-order assertion confirmed the call order **898 < 979 < 987**. Thus the new
line at the prescribed position would price constructor configuration rather than the restored
configuration; with a non-default persisted PHY its floor/default, and possibly its usability
state, could disagree with the later `cfg` read-out. This is a prospective integration defect,
not a claim that an existing activation report is broken: the feature has not been written.

**Recommended QA fold-in:** place only the NEW activation report inside the successful
`g_node.on_init(cfg)` branch, after the existing counter/lease restores. Preserve the existing
admin-session install/report and all RNG ordering. Keep the ACCEPT guard, the exact activation
envelope, and the single live-input binding. If initialization refuses, retain the existing
explicit config-refusal line and do not emit a misleading usable activation report. Extend the
already-authorized console-sink structural rows/controls to require both the post-initialization
position and the successful branch (moving it before initialization or outside that branch must
be RED). No new production path beyond §5 is needed, and no existing boot call needs relocating.

STOP-1 is the brief's source-disagreement rule, not a request to change an owner ruling. QA owns
the placement amendment and B361 registration (B361 was checked as next free). Codex has edited
only this evidence file; the feature implementation remains paused pending that correction.

## 9. Revision-4 implementation start; STOP on the existing cfg JSON golden (2026-09-08)

B361 is resolved by the owner's QA relay and the amended §4.6: the new activation line belongs
only in the successful `g_node.on_init(cfg)` branch. The admin-session call/report is unchanged.
Received brief SHA-256: `79bcc2032cbb3c9aaacd7be1b3cc90b088ec39a979ccb9c57750a6308ed5a575`.
HEAD remains `89071fb36ea74ca354ce4c830e000e3c58e4763c`; the same named Markdown fold-ins and
coder evidence were present on receipt. This is a resumed fix round, not a new empty-status claim.
Simulator HEAD remains `06746a97de5764415d6fcef10b97bca90569b9c7`, status EMPTY.

### 9.1 Implemented so far — NOT a completed slice

- New `lib/core/remote_activation.h`: pure source-derived budget; three CTS attempts through
  the committed wait helper, separate ACK helper, checked arithmetic, derived terminal length,
  named floor/default/ceiling. Invalid/empty PHY refuses rather than selecting a fallback SF.
- New `src/firmware_remote_activation.h`: the five states, on-demand resolution and names;
  one live-input binding in `firmware_commands.cpp` uses the active-layer BW/CR, public
  `max_data_sf()` and HAL slop. No effective-value global or scheduling consumer.
- NV v25 append, explicit load floor 25, 280-byte/offset-276 static assertions; raw firmware
  global/restore, canonical seed, cfg arm with the B354 key bound, text/JSON readouts and the
  B361 success-only boot line. `node.h` changes only the authorized declaration location.
- The one operator/non-disruptive authority row in the production header and allowed Markdown
  table; the inbox probe's new fake global. The inventory is NOT regenerated yet.
- Native v25 predicate/layout tests and the corrected 0e sum; twelve new activation cases
  in `test/test_remote_activation.cpp` and `test/test_firmware_remote_activation.cpp`.

The first build (`/tmp/mr-s7a-1xIv1G/native-build.log`) failed because the new tests used doctest
`REQUIRE` under the repository's no-exceptions configuration. Corrected in the new test files
to the surrounding `CHECK` idiom, with an explicit guard before using a refused codec result.
The second build compiled, then the pio test runner failed at the pre-existing exact cfg golden
described below (`native-build2.log`). No build/test failure is being reported as a PASS.

### 9.2 Proposed B362 — the existing exact cfg JSON golden needs an explicit test-fence entry

The brief §4.6 requires two unconditional JSON fields, but §5 does not include
`test/test_console_json.cpp`. Its existing case at `:341`, exact comparison at `:359-364`, pins
the entire pre-7a cfg object without those fields. It necessarily fails when the authorized
emitter gains them. The test must not be weakened to a substring comparison or removed.

Executed the binary directly after the wrapper failure, as D1 requires:

```text
./.pio/build/native/program
test cases: 2855 | 2854 passed | 1 failed | 0 skipped
assertions: 121999 | 121998 passed | 1 failed
exit 1
```

Full output: `/tmp/mr-s7a-1xIv1G/native-direct.log`. Its ONLY failure is
`test/test_console_json.cpp:359`, whose actual object adds:

```json
"remote_action_activation_ms":0,"remote_action_activation_state":"impossible_phy"
```

Those are the pure extras' unprovided-resolution defaults; firmware readouts supply the actual
live resolution. The existing 512-byte fixture buffer still fits this actual object: **496 bytes
including newline**. No buffer-overflow failure was observed, and no buffer widening is proposed
as required by this measurement.

The new tests were also run separately (not a replacement for the failed full gate):

```text
./.pio/build/native/program --test-case='remote activation:*,firmware activation:*'
test cases: 12 | 12 passed | 0 failed | 2843 skipped
assertions: 126 | 126 passed | 0 failed
exit 0
[activation search] SF10 BW7800 CR8 preamble=16 slop=0 floor=155904 default=311808 ceiling=299999
```

This search found a real admitted PHY where the default cannot fit; no fake ceiling or invalid
SF/BW was needed. These are development figures, NOT final PINs: the remaining instruments and
their new coverage have not been implemented/run yet.

**Recommended QA fold-in:** authorize `test/test_console_json.cpp` solely to extend the existing
full-object cfg golden by the two new fields, preserving all prior bytes and assertions. Prefer
setting a nonzero resolved value/state explicitly in its `CfgExtras` and pinning both in the
literal, so this case also catches accidental zero/default substitution. Keep the new native
state-by-state JSON tests. No emitter fallback, omitted field, weaker comparison, or unrelated
test rewrite is needed. B362 was checked as next free; QA owns registration and the brief edit.

### 9.3 Remaining work / stopped tree

STOP under §8.1 (out-of-fence fixture) and §8.5 (failed gate). The out-of-fence test is untouched.
Pending: that golden correction after QA authorization; B360 inbox executed rows and two controls;
console-sink structural rows/controls; generator-backed key-buffer test; new mutation batteries
and derived PIN; inventory regeneration; the full instrument chain, corpus, ABI, warning census,
post-feature board pair/attribution and mutation union. No post-feature board or simulator result
is claimed; the pre-feature board capture remains §8's baseline.

New untracked files are exactly the two headers and two native test files named in §9.1. Modified
implementation paths are `lib/console/console_json.{h,cpp}`, `lib/core/node.h`, `src/device_nv.h`,
`src/firmware_command_authority.h`, `src/firmware_commands.cpp`, `src/firmware_config.cpp`,
`src/fw_context.h`, `src/fw_main.cpp`, `test/test_device_nv.cpp`,
`test/test_radmin_characterization_0e.cpp`, `tools/probe_inbox_verbs/probe_main.cpp`, and the
authorized command-authority Markdown table. QA's brief/register edits are preserved; Codex has
not edited those or committed anything. Tracked whitespace checks pass. This tree is NOT ready
for the independent slice gate or an implementation commit.

## 10. Feature implementation after B362 — STOP for the ownership-census fence (2026-09-08)

**Not a completed feature gate.** B362's authorized exact-golden update is implemented. The next
full tools sweep exposes one omitted instrument path: the required ACCEPT-only boot line adds a
site to the feature ownership contract. That contract is working correctly by refusing it, but
its census file is outside §5. Codex stops under §8.1/§8.5; the production guard is NOT removed,
widened, moved back before initialization, or hidden behind a different spelling to evade the
instrument. QA's revision-5 brief and register changes remain untouched.

### 10.1 Resume binding and new findings for QA registration (M1)

This is a continuation of the previously captured clean-start run, **not a new clean-start
claim**. MeshRoute HEAD remains `89071fb36ea74ca354ce4c830e000e3c58e4763c` (7a-0); simulator HEAD
remains `06746a97de5764415d6fcef10b97bca90569b9c7`, clean. The revision-5 brief SHA-256 is
`ccebb6eaf28b72d4f4420348b6af3d6fde74e61f7800f4c51cc2bbeed5f85d99`. Logs below are in
`/tmp/mr-s7a-1xIv1G/`. The maintained register still names B363 as next free at this checkpoint.

| proposed finding | verified source and measurement | disposition requested from QA |
| --- | --- | --- |
| **B363 — required activation boot guard omitted from the feature ownership census fence** | `src/fw_main.cpp:994` adds the ruled `#if MR_FEAT_RADMIN_ACCEPT` after successful initialization. `tools/probe_features/ownership.py:146-152` pins five existing sites in that file (two ACCEPT, three CLIENT); the new site makes six (three ACCEPT, three CLIENT). Its multiset comparison at `:337-351` refuses O4i. Direct `python3 tools/probe_features/ownership.py` exits 1 (`ownership-stop.log`); eight tools-unit failures stem from this same refusal or its intentionally aborted control run. §5 authorizes neither this file nor a census update. | Add **only `tools/probe_features/ownership.py`'s `APPROVED_SITES[FW_MAIN]` entry** for the ACCEPT-only activation boot line to the instrument fence. Keep the exact multiset and all existing controls. There are still nine approved files and the same check/control definitions, so the predicted feature pins stay **9 / 120 / 59**, with 40 ownership controls; verify, do not mechanically re-pin. Console-sink's new boot rows/controls already prove the line's placement and guard. Re-run ownership, controlled/probe-only features and the full tools sweep. |
| **B364 — pre-existing invite fixture passes count 200 over a two-element array** | `test/test_firmware_ui_invite.cpp:406` declares two `InviteMember` objects, but `:419` calls `rows(empty_open, live, 200)`. `rows` (`:70`) forwards the count. `src/firmware_ui_invite.h:348-357` caps it at `kMaxInviteRows` (8), then reads those entries; the caller supplied only two. An additional XML-reporter diagnostic fails with `4 == 3` at line 419 on **both** the unmodified 7a-0 base and this feature tree. Default-reporter native gates pass on both. | Register as a pre-existing test-fixture out-of-bounds read, not a new activation regression and not a green XML run. Repair requires a separately authorized fixture edit: supply an actual capacity-sized array when testing count clamping. Neither the test nor production invite code was changed here. QA decides its disposition; no waiver is inferred from the ordinary run passing. |

The XML diagnostic was used to attribute assertions per case, not as a replacement for D1's
normal executable run. Its failure is retained in `feature.xml` and `pinbase.xml` (both exit 1),
with the base build in `pinbase-build.log`. The disposable, unmodified detached worktree
`/tmp/mr-s7a-pinbase-s4ukW3` is at `89071fb`; its native build passed before the XML run. Reproduction:

```sh
./.pio/build/native/program --reporters=xml
/tmp/mr-s7a-pinbase-s4ukW3/.pio/build/native/program --reporters=xml
```

### 10.2 Completed implementation and pin derivations

The cfg JSON golden retains its exact full-object comparison, its **512-byte** buffer, and the
entire pre-7a literal in a comment. The two new fields are added at the emitter's positions;
unresolved pure extras emit `0` / `impossible_phy` as authorized by B362.

**PIN re-synced? YES — 2843/121859 → 2855/121999.** Twelve new cases execute 126 assertions
(seven budget cases / 53 assertions, five resolver/emitter/parser cases / 73). Existing cases
gain four NV layout/floor assertions and four 0e numeric assertions. The existing table-driven
command-authority case executes six additional assertions for the one new row (1128 → 1134).
Thus `12` cases and `126 + 4 + 4 + 6 = 140` assertions account for the entire move. The two
renamed NV predicate cases keep their old assertion counts. The exact cfg golden changes its
expected bytes, not its assertion count. XML per-case totals corroborate this attribution even
though that optional reporter exposes B364's unrelated failure.

* Inbox ACCEPT **362/39 → 370/41**, CLIENT **360/42 → 368/44**: eight executed rows in each arm
  pin the 280-byte layout and real `mrnv::save`/`mrnv::load` behavior for v24 rejection, v25
  acceptance plus field read-back, and v26 rejection. Two floor controls lower to 24 and raise
  to 26. Both arms execute all rows and controls, with zero unusable.
* Console-sink **62 → 76 structural rows**, **114 → 136 controls**: S63–S76 bind parsing,
  resolution/refusals, persistence/notification, live PHY, text/JSON, raw boot restore,
  post-success ACCEPT-only boot reporting, the derived key capacity and the previously
  unreachable gateway interval arm, and canonical NV seeding. Twenty-two new controls each
  redden a named row; all previous controls remain. No production source is mutated by them.
* Inventory **203 → 204**, regenerated with `--write`. One tools test is added for the longest
  cfg key versus the generated sub-verbs. The existing Slice 6 normalization test also had an
  exact 203-row assertion; it now expects 204, with every semantic assertion preserved.
  `inventory-tests.log` retains its initial 70-test / one-failure result. The full tools sweep
  had already imported that old count before it was updated, so `tools.log` contains that
  same failure **plus eight B363 failures**: **343 tests / 9 failures**, NOT a passing sweep.
  No full sweep after the row-count correction is claimed.
* New mutation batteries: `remoteactivation` **22/22 RED**, `fwactivation` **10/10 RED**,
  each with zero unusable and each source restored by hash.

### 10.3 Measured checkpoints (not a full PASS)

| instrument | measured result / log |
| --- | --- |
| Native build + ordinary executable | **2855 cases / 121999 assertions / 0 failed**; `native-build3.log`, `native3.log` |
| Budget search | Real PHY **SF10 / BW7800 / CR8 / preamble16 / both slops0** gives floor **155904**, default **311808**, ceiling **299999**; resolver refuses as impossible |
| Simulator rebuild | **34 compile/link actions**, both core variants rebuilt through `node.h` and `console_json.cpp`; `sim-build.log`. `lus` md5 **`9f5a4b9872c4fe407c319f767b9236b6`**, SHA-256 **`46799b896722ccacd7fe2aac271d734eba9fc89f8765a4da9c256ee7f49c4375`** |
| Corpus | **36/36 validated, all anchors reproduced**, s18 **`32afbf11` / 269517 / 0** from current `simulation/BASELINE.md`; `corpus.log`, `corpus/`. Explicit before/after stream comparison and zero-remote-event census remain to be recorded; no producer-hash mismatch is waived in a stock comparator |
| ABI probes | Default **191 checks / 9 controls RED**, B278 **42 / 6**; Node unchanged on all three targets; `abi.log`, `abi-b278.log` |
| Console-sink controlled | **6 / 720 / 76 / BLE905 / ownership6 / ownership-controls3 / controls136 / unusable0**, PASS; `console-controlled.log` |
| Inbox controlled | ACCEPT **370/41**, CLIENT **368/44**, both PASS; `inbox-controlled.log`. Probe-only execution also recorded, not substituted for controls |
| Firmware UI controlled | **223 controls / 0 unusable**, PASS; `ui.log`. `ui-only.log` executes zero controls and prints PASS (existing B350 wording), not a controlled-gate result |
| Custody / BLE line | Controlled **27/10** and **40/8**, PASS, zero unusable; both probe-only runs correctly end PROBE-ONLY; `custody*.log`, `ble*.log` |
| Inventory / authority / a0 / literals | Inventory bare and `--check` PASS at 204; authority PASS and **6/6 selftests RED**; a0 **10/10 RED**; literals PASS across 211 source files; respective logs |
| OLED warning census | All six match **173 / 178 / 177 / 177 / 182 / 182**, zero switch warnings; `census.log` |
| Tools / feature ownership | **NOT PASS**, B363 above; `tools.log`, `ownership-stop.log`. The separately launched default feature probe also fails at O4i (`features.log`): 9 cells / 120 checks / 1 failed, 19 controls verified / 2 unusable because ownership's clean control baseline refuses. Its probe-only successor was not launched by that chain; no pins were changed to absorb this failure |

Deterministic board captures (base `.pio-measure/s7a-base-89071fb/`, feature
`.pio-measure/s7a-final-89071fb/`) completed with the pair runner's source/normal-build guards
passing; `boards-final.log`, 52.1 seconds, one build job per environment:

| board | RAM before → after | flash before → after | objects |
| --- | --- | --- | --- |
| gateway | 197180 → **197188 (+8)** | 550220 → **555292 (+5072)** | 285 → 285 |
| heltec_mobile | 207748 → **207756 (+8)** | 1371256 → **1372732 (+1476)** | 329 → 329 |

The RAM prediction **+4 was not the measured +8**: the only sized RAM-symbol change on either
board is `g_remote_action_activation_ms` (+4); BSS grows by 8, with four more bytes of alignment
residue. The adjacent `g_ble_mode` moves by 4 while the unchanged 8-aligned `g_node` moves by 8
(gateway `0x20008d90 → 0x20008d98`; mobile `0x3fca1bc8 → 0x3fca1bd0`). No Node or NV record size
growth is hidden in that delta. Gateway `.text` grows 5072, with `.data`/`.ARM.exidx` unchanged;
mobile `.flash.text` grows 1188 and `.flash.rodata` 288, other loadable sections unchanged.
The manifests, sections and symbol tables retain the full measurement; detailed flash-symbol
attribution remains part of the resumed final report, not a claim completed here.

### 10.4 Remaining work after QA's fence fold-in

Apply only the authorized ownership-census update, then execute its controlled and probe-only
gates and a fresh full tools sweep. Finish the remaining standing probes, explicit corpus
byte comparison/remote-event census, full board requirements and flash attribution, then a final
native/whitespace/provenance check. No bench result is claimed; Part 57a remains QA's landing.

Mutation selection is still the complete 15-target union: changed-source (the whole 7a arc)
`macwait, remoteactivation, fwactivation, b159mac, b161mac, b20mac, grantadmit, sliceBmac,
devicenv, sliceAjson, sliceDack, sliceGjson, b134ack, cmdauthority`; historical/dependency
`radmin2codec, cmdauthority`. The authority target belongs to **both** selectors, because its
header actually changed. `node.h` has no target in the runner's map. The six MAC targets have
finished (28 RED / the one known B342 M04 unusable); the two new targets add 32 RED / 0 unusable.
The already-launched remainder was still in its isolated `devicenv` workers at discovery of
the STOP. While this report was being written, `devicenv` finished **42 RED**, `sliceAjson`
**1 RED**, `sliceDack` **1 RED**, and `sliceGjson` **16 RED**, each with zero unusable and source
restoration confirmed. The orchestration shell was then terminated (exit 143) to prevent the
remaining launches. Its already-started isolated `b134ack` workers have no completion summary
in the retained log and no process remains: that target is **interrupted/unscored and must be
re-run**, not credited with a result. Partial output is not scored as a completed battery or
union. `radmin2codec` and `cmdauthority` still need launching. Reconcile each completion log
before claiming the final union; never count an interrupted target as green.

No out-of-fence implementation or instrument repair has been made, no QA-owned landing file
has been edited by Codex, and nothing has been committed. **This is a STOP handoff, not readiness.**

## 11. Revision-6 resume — final feature gate (2026-09-08)

**Coder gate complete — ready for QA's independent gate**, with the explicitly retained
pre-existing B342 M04 exception and B364 left OPEN/out of fence. B363's repair, the full tools
sweep, the complete mutation union, all thirteen board builds and the repeated deterministic
pair are complete. This is not QA's verdict or a metal PASS. Earlier failed/partial reports
above remain historical evidence.

### 11.1 Binding, scope and B363

MeshRoute `89071fb36ea74ca354ce4c830e000e3c58e4763c`; simulator
`06746a97de5764415d6fcef10b97bca90569b9c7`, still clean. Revision-6 brief SHA-256:
`7ed7fdbdcf119c6846129b09d5877abc947df042afe027a6db816084797b1883`.
The known QA brief/register fold-ins ride with the existing coder tree; this is a resumed
run, not a fabricated clean-start receipt. No new production edit followed B362. The only
revision-6 implementation edit is one entry in `APPROVED_SITES[FW_MAIN]` in
`tools/probe_features/ownership.py`, describing the post-init activation boot line.

The exact site census is **five → six sites in that TU: two → three ACCEPT, three CLIENT
unchanged**, not six ACCEPT sites. The approved-file count stays nine. The feature probe
reproduces **9 cells / 120 checks / 59 controls**, zero failed/unusable, including **40 ownership
controls**. No pin, check, control, production guard or boot placement was changed.
`features-r6.log` and `features-only-r6.log` record the controlled PASS and the honest PROBE-ONLY
success respectively. The full tools sweep is separately required; the direct probe's PASS
does not stand in for it.

B364 is QA-registered OPEN and untouched. The optional XML failures in §10.1 remain failures;
they are not erased by the normal reporter passing. The registered B342 M04 exception also
remains visible below. No new finding number beyond B364 is allocated by this resume.

### 11.2 Exact corpus and native proof

The final ordinary native build and executable pass **2855 cases / 121999 assertions / 0 failed**
(`native-build-r6.log`, `native-r6.log`). **PIN re-synced? YES**, with the complete +12/+140
derivation in §10.2; the six extra existing authority-table assertions are included, not hidden
inside the new-test count. The isolated mutation workers independently reproduce that baseline.
The final D1 build-and-run repeated after every board capture gives the same result
(`native-build-final.log`, `native-final.log`, both exit 0).

The preserved pre-feature corpus is `/tmp/mr-s7a0-6Om1sn/final-corpus`; the feature corpus is
`/tmp/mr-s7a-1xIv1G/corpus`. `run_corpus.validate_run` re-validates **both** complete runs and
their actual files. A read-only comparison checks all `COMPARISON_SCENARIO` fields and all
`COMPARISON_TOP` fields except the explicitly changed producer hash, then `diff -rq` compares
the actual stream directories: **36/36 byte-identical**. A poisoned s18 output-SHA in a copied
in-memory manifest is detected as the one mismatch. Neither on-disk manifest nor the stock
comparator was changed or presented as passing a producer mismatch.

`corpus-compare-r6.log` records this comparison and a parsed `type`/`emit_type` census: **zero
radmin/activation events on all 36 streams**. The current `simulation/BASELINE.md` SHA-256 is
`71f140988e9d1a5bf1be49dd1feb8fc7ebb8e46c7a4b119e16db62f0df76962f`, equal to the validated
run's authority snapshot. s18's full md5 is **`32afbf11e43b4bf9d0bd470ad502ba0a`**, SHA-256
`27ecfa988f062d3b27ed9b19159965ac393a0f03e2499011d87d9797fe64cc54`, **269517 events / 0 failures**.
The simulator build's **30 C++ compilations + 4 links = 34 actions** and moved producer hashes
remain §10.3's measurements. No simulator repository edit, baseline edit or re-anchor occurred.

The old 0e characterization is independently executed with `MR_RADMIN0E_TABLE=1`:
`0e-budget-r6.log`, two cases / 62 assertions / zero failed. It prints RTS 88, CTS 78, terminal
DATA 221, ACK 78, gap 5, busy retries 60, CTS waits **167+333+665=1165**, ACK wait 311 and requeue
5000: **7006 floor / 14012 default / 299999 ceiling**. The withdrawn 6506/13012 is no longer
a green numeric reading. The searched impossible PHY remains §10.3's real SF10/BW7800/CR8 case.

### 11.3 Mutation union — complete, with the one pre-existing exception

Changed-source selector: `macwait, remoteactivation, fwactivation, b159mac, b161mac, b20mac,
grantadmit, sliceBmac, devicenv, sliceAjson, sliceDack, sliceGjson, b134ack, cmdauthority`.
Historical/dependency selector: `radmin2codec, cmdauthority`. Selection is the whole 7a arc,
including 7a-0's reused helpers; `cmdauthority` genuinely occurs in both selectors. `node.h`
has no runner target. Gate the union once, not each selector independently.

| target | RED | unusable |
| --- | --- | --- |
| macwait | 10 | 0 |
| remoteactivation | 22 | 0 |
| fwactivation | 10 | 0 |
| b159mac | 2 | 0 |
| b161mac | 1 | 0 |
| b20mac | 11 | 0 |
| grantadmit | 1 | 0 |
| sliceBmac | 3 | **1 — M04, existing B342** |
| devicenv | 42 | 0 |
| sliceAjson | 1 | 0 |
| sliceDack | 1 | 0 |
| sliceGjson | 16 | 0 |
| b134ack | 2 | 0 |
| radmin2codec | 66 | 0 |
| cmdauthority | 16 | 0 |
| **15-target union** | **204** | **1 known, zero new** |

All entries ran in full, with the runner using two workers where the target has enough entries
(the one-entry console targets correctly use one). Each completed log reports restored source
hashes and all **55 real target files unchanged**. `mutation-summary-r6.log` reconciles the
completion records. The interrupted `mut-b134ack.log` is NOT counted: its complete re-run is
`mut-b134ack-r6.log`. The other two new completion logs are `mut-radmin2codec-r6.log` and
`mut-cmdauthority-r6.log`; the earlier twelve completed target logs retain their plain names.
M04 is not counted as RED, repaired out of fence, or relabelled usable.

### 11.4 Board attribution

The base and feature captures in §10.3 remain the before/after pair. Full per-symbol changes
are in `board-attribution-r6.log`, and `board-coverage-r6.log` independently unions **actual ELF
symbol address ranges** (aliases counted only once; ARM Thumb address bit normalized) against
section lengths. This distinguishes named storage/code from bytes without sized symbols.

Both boards: **RAM +8**, comprising the **4-byte raw global +4 bytes alignment**, not the brief's
predicted +4 total. Node's size/alignment and the NV blob's **280 / alignment8 / offset276** are
unchanged. There is no resident effective-value copy and no new object file.

Gateway: flash **+5072**, all in `.text`; `.data` and `.ARM.exidx` unchanged. Unique sized-symbol
coverage grows **4650 bytes**, with **422 additional bytes outside those ranges** (constant-pool,
string/alignment residue, not extra RAM). Major named changes: dump_cfg +1060, make_cfg_extras
+916, resolver +884, setup +580, cfg-set +364, budget +364, live binding +156, JSON writer +188,
decimal parser +124, authority table +12. Existing inline services also change code generation:
AdminIdService::mint_ gains an out-of-line 1244-byte body while its handler shrinks 852;
mesh_service_once shrinks 628; SegmentedInboxStore::append shrinks 572; GuardedConsole::stage
gains 320 while compact loses 58. The complete symbol list retains these changes rather than
attributing the entire flash delta to the new helpers. No corresponding service source edit
is in the diff. Raw symbol-size sums double-count four bytes of destructor aliases; the
address-union calculation, not that raw sum, reconciles the section.

Heltec mobile: flash **+1476 = .flash.text +1188 + .flash.rodata +288**; other loadable sections
unchanged. Code's unique sized-symbol coverage grows **1069**, with **119** outside those
ranges; read-only data adds **12** named authority-table bytes plus **276** without sized
symbols. Major code changes: budget +362, cfg-set +248, live binding +155, dump_cfg +128,
resolver +79, state-name mapper +46, two airtime lambdas +26 each. The complete log also lists
the small movements in existing functions. These are actual section/symbol measurements,
not a claim that every anonymous byte has been assigned to a particular source literal.

### 11.5 Final gate receipt and QA landing boundary

| instrument | final software figure | retained log under `/tmp/mr-s7a-1xIv1G/` |
| --- | --- | --- |
| native | **2855 / 121999 / 0** | `native-build-r6.log`, `native-r6.log` |
| simulator / corpus | **34 build actions; 36/36 byte-identical; all anchors; zero remote events** | `sim-build.log`, `corpus.log`, `corpus-compare-r6.log` |
| Node ABI | **224136/8 native, 117912/8 heltec_mobile, 150504/8 gateway**, unchanged; **191 checks / 9 controls RED** | `abi.log` |
| B278 ABI | **42 measurements / 6 controls RED** | `abi-b278.log` |
| console-sink | **6 profiles / 720 executed / 76 structural / BLE905 / ownership6 / ownership-controls3 / 136 controls / 0 unusable** | `console-controlled.log` |
| inbox-verbs | **ACCEPT 370/41; CLIENT 368/44; zero failed/unusable** | `inbox-controlled.log` |
| firmware UI | **223 controls / 0 unusable**, all three compiled arms pass | `ui.log` |
| custody / BLE line | **27/10; 40/8**, zero failed/unusable | `custody.log`, `ble.log` |
| features | **9 / 120 / 59; 40 ownership controls**, zero failed/unusable; nine approved files | `features-r6.log` |
| probe-only modes | Executed separately, never substituted for controlled runs; UI's PASS wording is existing B350 and inbox's BOTH ARMS PASS is existing B303 | `console-first.log`, `inbox-first.log`, `ui-only.log`, `custody-only.log`, `ble-only.log`, `features-only-r6.log` |
| tools sweep | **343 tests, OK** (342 old + one new cfg-key-capacity test); the old 203-row fixture pin is updated to 204 with its other assertions intact | `tools-r6.log` |
| inventory | **204 rows**, bare and `--check` byte-identical to the generated file | `inventory-bare-r6.log`, `inventory-check-r6.log` |
| authority checker | PASS, **6/6 selftests RED** | `authority-r6.log` |
| a0 / literals | **10/10 controls RED**; **211 active source files**, no numeric DataType violations | `a0-r6-corrected.log`, `literals-r6.log` |
| OLED clean-build census | **173/178/177/177/182/182**, every pin exact; zero switch warnings | `census.log` |
| mutation union | **15 targets / 204 RED / 1 known B342 unusable / 0 new unusable** | `mutation-summary-r6.log` and the target logs in §11.3 |
| all board environments | **13/13 PASS, sequential**, zero switch warnings and zero project-source reorder warnings; pre-existing vendored diagnostics stated below | `all-boards-r6.log`, `board-<env>-r6.log` |
| repeated deterministic pair | **Both actual ELFs and payloads byte-identical**, every size/section/object/symbol/toolchain/identity field equal to the preceding feature capture | `boards-repeat-r6.log`, `pair-repeat-compare-r6.log`; `.pio-measure/s7a-r6-89071fb/` |

The complete board set is `xiao_sx1262, heltec_v3, heltec_v4, xiao_esp32s3, gateway,
gateway_heltec, gateway_heltec_v4, gateway_esp32s3, production, xiao_mobile, heltec_mobile,
heltec_v4_mobile, xiao_esp32s3_mobile`, in that order. The sequence's final exit is 0; none was
skipped. Each of the four nRF52 environments emits the three diagnostics for the same existing
`CustomLFS_QSPIFlash` initialization-order issue (`.h:173`, `.h:103`, `.cpp:262`), also present
in the **pre-feature** deterministic gateway log. There is no Node/project-source reorder
warning. An initially overbroad one-off receipt assertion against *all* reorder diagnostics
therefore failed; inspecting the diagnostics separated this unchanged dependency from the
project code. No compiler warning was suppressed or production gate loosened.

The repeated pair ran **after** the thirteen-board sequence, with `--jobs 1`, in **42.1 s**.
Both source-stability and normal-`.pio` guards passed. Gateway remains **197188 RAM / 555292
flash / 285 objects**; heltec_mobile **207756 / 1372732 / 329**. The captured, byte-compared
ELF/payload hashes are:

| target | ELF SHA-256 | flashed payload SHA-256 |
| --- | --- | --- |
| gateway | `e5549b66b507704912ea8fb7ed31af521b1d2e94914abed2ae9adffc8242731a` | `a5cfac5964a103d2c3d86d11c057a13a054e2434716734ef11aae96632e3b214` |
| heltec_mobile | `9bd791b84da3dfc30d76cf48abe8b0fde3a5fcc1e8c756469b375512cf2b434c` | `06b3b971e39af9b86379c0aca312f9a5f2c434b59020805e8391b196b60e61a1` |

This is a read-only comparison of the actual files plus complete `artifacts`, `measurements`,
`fixed_identity` and `toolchain` records, not a forged same-source invocation of the stock
comparator. The whole-tree snapshot changes from `67fa41e3…` to `785418c5…` because of the
authorized ownership-tool and Markdown edits (B337's document-inclusive snapshot); both
original manifests retain their real full hashes. The evidence completion below is also a
Markdown edit after capture, not an assertion that the final report hashed itself.

Console/inbox/UI/custody/BLE/ABI/census results are the completed feature runs from before the
B363 stop: their production and instrument inputs have not changed in this resume. B363's
ownership file is the one additional tool edit; features and the full tools suite were re-run
after it. No earlier failed tools sweep or interrupted mutation is being reused as a PASS.

One coder orchestration typo is retained: the first repeated checker chain tried the nonexistent
`tools/check_a0_datatype_matrix.py` (`a0-r6.log`, Python exit 2, not a checker verdict), so its
following literal check did not run. After resolving the actual filenames, `check_a0_matrix.py
--selftest` and `check_data_type_literals.py` both executed successfully in the logs above.
No tool was renamed or source changed to accommodate the typo.

No change to the admin-session boot sequence, scheduler/deferred-action code, Node members,
`NodeConfig`, 7a-0's committed MAC helpers/call sites, or the invite fixture is included in this
feature. QA owns register closures, design/manual/tracker/memory and Bench Part 57a. The exact
new console strings below are **source-derived bench expectations, not metal observations**:

```text
> cfg remote_action_activation_ms=20000 ok (live + saved)
> cfg remote_action_activation_ms=0 ok (live + saved)
> cfg gw_announce_interval=7200000 ok (live + saved)
> cfg err bad_value (remote_action_activation_ms <live-floor>..299999 ms or 0=default)
> cfg err impossible_activation (floor <f> default <d> exceed 299999 ms at this PHY)
> remote-activation state=configured ms=20000
> remote-activation state=default_derived ms=<2*live-floor>
  radmin: activation_ms=20000 state=configured cfg=20000 floor=<f> default=<d> ceiling=299999
```

The 20000 expectation requires a live PHY whose floor admits it. Test below-floor with the
observed floor minus one (6000 is only a below-floor example at a PHY where it really is below).
The upper refusal is exercised with 300000. The version-floor reprovision and reboot behavior,
the firmware handler's hardware wiring and the successful post-init boot line remain Part 57a
metal residue; native and structural proofs are not represented as hardware execution.

The final fence audit matches **27 coder-owned paths + two known QA-owned paths** (brief and
register). The four untracked files are exactly the two new headers and their two native test
files. Tracked and untracked whitespace checks pass, the simulator checkout remains clean,
and the committed 7a-0 files and B364's invite files have zero diff. QA owns the software closure
and the owner owns the commit and metal verification. Nothing has been committed.
