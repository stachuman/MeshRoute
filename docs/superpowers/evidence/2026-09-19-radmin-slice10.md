<!-- Production coder: Codex; independent gate: QA/Author; owner rules and commits. -->
# Slice 10 — coder source-validation, revision 1

**2026-09-19 — STOP-1 before implementation: B430, incomplete console-instrument fence.**
Both repository pins and the brief hash agree. The intended layout changes reproduce on all three ABIs.
The required additive console checks/controls need a runner re-pin that §5 does not fence. No production,
test, tool, brief or simulator source was edited. This is not an implementation freeze or a full gate.

## 1. Frozen preparation inputs

- MeshRoute: `4ad9c343b42bab4c91bd8e6721b45b2dfb769e7f`.
- Simulator: `6585649ea5a780f0542b2931853a667be56a5b2b`, clean.
- Brief revision 1: `3af4f66ac2044bcaf1d5c11dc06969429051680023f993d4ccfba410c4909335`.
- Five tracked QA edits plus the untracked brief were accepted as the permitted preparation set; no
  production/test/tool change was present. The original bytes are preserved in the preflight directory.
- R-RA-6 is settled. This STOP needs an author fence correction, not a new owner product ruling or commit.

| Preparation path | Initial SHA-256 |
| --- | --- |
| `MEMORY.md` | `6180e935a3cbffa88726c940813b24e615fe55c1a84359d74ce0ce74e2769579` |
| `docs/2026-07-30-open-bug-register.md` | `40d5fd2f5a13c1dfc76fe3bfc98c0b2a0d6487283ce843c04e562e0f46a42a71` |
| `docs/2026-07-31-bench-test-script.md` | `e6a7de242be6180a5bf358b956652bb6daebbe09cebcc28866a63bbd5213a148` |
| `docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md` | `9ca6b1aa07a86c65e4f421009443a4e6b5d85802cc7916545f57ad9e15930ca0` |
| `tracker.md` | `a22ddb7938d751b19eddaaaba55ef29d1e709ca704a7145fb63b093d078542e2` |
| `docs/superpowers/plans/2026-09-19-radmin-slice10-main-nv-cleanup.md` | `3af4f66ac2044bcaf1d5c11dc06969429051680023f993d4ccfba410c4909335` |

[Evidence directory](2026-09-19-radmin-slice10-preflight/README.md) contains the source inventory,
commands, flag sets, output, shadow headers and reproducer. All **391** tracked implementation/test/tool/
scenario inputs in `implementation-inputs.json` were preserved. The six incoming QA documents were
hash-checked before adding the receipt and updating only the current register/design dispatch status.

## 2. Source facts and independent measurements

Verified by symbol, per V1/P4:

- `mrnv::Blob` has exactly the three named legacy fields; both main-config version constants are 25.
  `mrnv::load` delegates to `read_slot(kSlotCfg, ...)` then the size/magic/version predicate. The four
  administration stores have distinct slots and typed wrappers.
- `Node` has the ACCEPT-gated mirror block, both public API arms, and the native 235248 pin. The only
  executable `admin_load` caller is `setup()` in `src/fw_main.cpp:929`. The only other executable Blob
  field users are the six named service-test assignments/assertions. `legacy-symbol-uses.txt` retains
  the full scoped grep, including historical comments and source-reader forbiddance patterns.
- `nv_load_stamped` is in `src/firmware_config.cpp:94`; `seed_blob_from_live` is file-static at :776.
  The boot load is at `src/fw_main.cpp:830` and currently emits no schema-outcome line.
- The two service fixtures use the removed floor solely as an unrelated-field preservation sentinel;
  `remote_action_activation_ms` is not modified by either service, so the proposed retarget is valid.
- The existing console structural instrument freshly passes **83/83** on the untouched base.

The following are **actual compiler/object measurements**, using each target's own `pio run -e <env>
-t idedata` flags and matching `nm`, with base versus private shadow headers. No board was linked.
The shadow changes remove the intended blocks and adjust their layout asserts; they are not a candidate.

| ABI | Base Node | Shadow Node | Delta | Base Blob / alignment / activation offset | Shadow Blob / alignment / activation offset |
| --- | ---: | ---: | ---: | --- | --- |
| native | 235248 | 235208 | −40 | 280 / 8 / 276 | 240 / 8 / 236 |
| gateway ARM | 157344 | 157304 | −40 | 280 / 8 / 276 | 240 / 8 / 236 |
| heltec_mobile Xtensa | 122176 | 122176 | 0 | 280 / 8 / 276 | 240 / 8 / 236 |

On every ABI, `intro_attach` moves 201→162, `team_ch_pub` 202→163, and `team_key_team_id` 268→228.
The native QA prediction and gateway −40 prediction agree. These are layout facts, **not linked RAM or
flash results**. In particular, `mrnv::save` also owns a static `Blob cur`; the final RAM attribution must
include every linked Blob instance, not assume the Node delta is the whole-image delta.

## 3. STOP-1 — B430: the required console proof exceeds the instrument fence

Brief §§1/3/6 requires a new boot formatter golden and additive structural/control coverage for one call,
after the load, with the explicit sink; removal, duplication and sink substitution must turn RED.
Section 5 names `tools/probe_console_sink/structural.py` and `negctl.py` but omits the runner.

The stock `tools/probe_console_sink/run.sh` owns and enforces `PIN_STRUCTURAL=83` (:198) and
`PIN_CONTROLS=149` (:206), with `pin_cmp` at :209 and the mandatory comparisons at :438/:442.
`tools/test_probe_console_sink.py::_run_sh_pins` correctly reads those runner pins dynamically; it should
not be weakened or re-pinned separately. Adding a new row/control while keeping this runner unchanged
cannot pass the required console gate and tools discovery.

A private reproduction executes the **actual unmodified shell comparator**, extracted uniquely from
`run.sh`; it does not invent a replacement comparator or change any source:

| Comparator input | Exit | Result |
| --- | ---: | --- |
| structural 83 / pin 83 | 0 | accepted, current positive control |
| structural 84 / pin 83 | 1 | `!! PIN structural: observed 84, expected 83` |
| controls 149 / pin 149 | 0 | accepted, current positive control |
| controls 152 / pin 149 | 1 | `!! PIN controls: observed 152, expected 149` |

84 and 152 illustrate one additive row and the three requested controls; **they are not proposed final
pins**. Final counts must be derived from the implemented instrument, preserving every existing row and
control. Packing the new proof into an unrelated old row to avoid a legitimate re-pin is not proposed.

**Concrete author correction:** add `tools/probe_console_sink/run.sh` to §5 for the derived check/control
re-pins and wiring; explicitly name `tools/probe_console_sink/probe_main.cpp` for the boot-line goldens
(the current fence mentions a golden only generically). Keep D5's bare numeric assignments and the
unchanged full tools-discovery obligation. No production scope extension is needed for these changes.

## 4. Migration proof plan to make explicit in the reissue

The required v25-image → real load/reseed/stamp → persist → loaded proof cannot be claimed from the
existing service fakes. `platformio.ini:78` sets `test_build_src = no`; the native suite never compiles
`firmware_config.cpp`. The native backend in `src/device_nv.h:1412` always reports no slot and refuses
writes. A fresh host program calling the real wrappers on a valid current stamped record prints
**`host load=0 save=0`**. The service fixtures model already-loaded Blob values and do not execute
`nv_load_stamped`. This is an instrument reachability fact, not a production defect.

Proposed bounded implementation: keep the schema and sentinel tests in their named native files; use
`test/test_firmware_config_service.cpp` for any service-level preservation case. Explicitly fence and wire
a dedicated host migration probe under `tools/probe_console_sink/` (for example `nv_migration.py`) that
compiles uniquely extracted production load/save, reseed/stamp and boot-report bodies with a labelled
in-memory slot backend, preserving and comparing provisioned bytes for all four remote-admin slots.
The seed/conversion path must come from the real source, not be hand-copied. Give it controls that alter
the actual extracted behavior and prove the detector fails, including store clobber and wrong load outcome.
Label it a source-extracted host proof, not execution of the complete board TU or a native-doctest case.
The full formatter goldens belong in the real-header console probe. No production refactor, new production
TU, platformio change or simulator edit is proposed. Actual flash behavior remains Part 57f.

## 5. Disposition

**Implementation remains HOLD at source-validation, B430.** The brief remains byte-identical at its
supplied hash. The register §0 and design §19.1 row 10 now point here; other incoming QA work is preserved.
Next: QA reissues the fence/proof wording at an explicit hash, then the coder resumes implementation and
runs the complete §8 chain fresh. Commits are not a prerequisite.

Only the six compiler layout measurements, base structural checks, four comparator checks and the host
no-backend reproduction ran. Native doctest, references, simulator rebuild/corpus, full ABI controls,
remaining probes, tools discovery, inventory/authority, A0/literals, census, deterministic boards,
xiao_mobile and mutation union were **not run**. No software PASS, freeze, RAM/flash result or metal PASS
is claimed. No staging, commits, production changes or simulator edits.

## 6. Revision 2 source-validation — PASS, 2026-09-19

Resume pins match: MeshRoute `4ad9c343b42bab4c91bd8e6721b45b2dfb769e7f`, simulator
`6585649ea5a780f0542b2931853a667be56a5b2b` (clean), brief revision 2 SHA-256
`3fe744dcd09de770453bf9c55cee136d7e0eb4a69bffbb0b8d5739f984f4819c`.
The 391 implementation input hashes recorded at revision 1 are unchanged. All 43 modified/untracked
preparation files, including the earlier report/artifacts and revised QA documents, are inventoried and
copied into the private preparation snapshot before implementation.

B430's fence is complete: the console runner, structural row and controls are named; the existing inbox
probe owns the two formatter goldens and the real ESP32 typed-wrapper migration proof. Source-validation
confirmed its byte-backed medium has 2304-byte slots and a counted write primitive; A7-1..A7-8 and A7-C1/C2
carry exactly the literal v25 assumptions identified by the reissue. The new shared wrong-slot control
fits the existing `nvh` mutation path. No new production or instrument seam is required. The ABI figures
remain the six actual compile measurements in §2; no production input moved between those measurements
and this preflight. All other revision-1 source facts stand.

Implementation proceeds within revision 2 §5. No executed `nv_load_stamped` proof will be claimed from the
host; it is unchanged and Part 57f owns the metal execution. The phrase “all four” in §5 introduces five
explicitly named console/inbox files; the explicit path list governs (a counting typo, no scope ambiguity).
The brief remains frozen at the hash above until the implementation freeze.


## 7. Revision 2 preserved implementation checkpoint — HOLD, B431/B432

**The §6 source-validation PASS is withdrawn.** The new code and in-fence proofs are implemented, but the
full gate found two more instrument users omitted from the fence and missed in my source-validation.
This is a preserved checkpoint, **not a completed implementation freeze for independent acceptance**.
Brief hash remains `3fe744dcd09de770453bf9c55cee136d7e0eb4a69bffbb0b8d5739f984f4819c`; both repository
HEADs remain as §6. No commit, staging, reset, shared cleanup, simulator edit or unfenced fix was made.

### 7.1 Implemented within the fence

- Main Blob loses exactly the three legacy fields; version/floor are 26; the ABI assertions are 240/8/236.
  All other field declarations and order are unchanged, and everything after the main-record section of
  `device_nv.h` is byte-identical to base, including every other store and wrapper.
- Both Node API arms and the mirror block are gone; native/gateway pins are 235208/157304, mobile 122176.
  Boot captures the real load bool, passes it once to the new inline explicit-sink formatter after applying
  loaded config and before the administration-store gate, and no longer calls `admin_load`.
- Both service-test sentinels use the same 99/4096 values in `remote_action_activation_ms`.
  The native schema case retains every old assertion and adds the separate floor and old-size refusals.
- Console S84 + three controls, pins **84 /152**; inbox A7-1..A7-8 re-fixtured, **20 shared additive
  checks** for migration/formatter/four-store isolation, one wrong-slot control, and floor controls 25/27.
  Inbox pins are **1394 /477 checks**, **61 /69 controls**, ACCEPT/CLIENT. Check counts are freshly
  reproduced; the complete 61/69 control run was interrupted, so those control totals are not gate results.
- Part 57f is written with the exact boot lines and real-flash/reseed/store-survival residue. Metal NOT RUN.
- `firmware_config.cpp`, all other stores, all wire/reference inputs, and the brief remain unchanged.

The initial 391-input audit proves that exactly **13** code/test/tool files changed and all are in §5.
The source and snapshots agree; the only later implementation delta is the in-fence four-assertion
correction described below. Every state is named in the archive; no initial log is relabelled as final.

### 7.2 B431 — an additional native Node size pin is outside §5

`test/test_custody_receive_g.cpp:2315` still asserts `CHECK(sizeof(Node) == 235248)`. This assertion is
independent of the production-header and ABI-probe pins named in the brief. Removing the approved mirrors
makes it fail at the measured 235208; the neighboring custody action size/alignment assertions stay valid.
The file is not fenced, so it was not edited in the shared tree.

The first full-gate attempt's mutation baselines report **2950 cases /195770 assertions /5 failed**.
Four failures were **my implementation omission in the already fenced `test/test_device_nv.cpp:526–530`**:
its separate layout case still pinned version/floor 25, size 280 and offset 276. I corrected those four to
26/26/240/236 without dropping assertions. A fresh wrapper build and explicit binary run on that candidate
now reproduce **2950 cases /195770 assertions /1 failed /0 skipped**, solely the unfenced custody pin.
The wrapper's own “2 test cases” report is not the native denominator.

**Exact proposed fold-in:** fence `test/test_custody_receive_g.cpp` for this one literal **235248→235208**
and its derivation comment, with every custody-specific assertion unchanged. This does not change a test
obligation or any production behavior.

### 7.3 B432 — the feature-ownership census still requires the two deleted blocks

`tools/probe_features/ownership.py::APPROVED_SITES['lib/core/node.h']` expects **10 sites**, including
**five ACCEPT-only** guards. The implemented deletion correctly leaves **8 sites**, with **three ACCEPT-only**,
four CLIENT-only and one shared guard. O4c compares multisets and refuses the missing two sites. The normal
feature runner consequently reports **112 checks /1 failed**, then refuses to judge its ownership controls
because their untouched starting snapshot is already red. This is not a failed capability decision.

**Exact proposed fold-in:** fence that one approved-site list and its derivation comment, removing only
the two legacy guard entries. Keep O4c's exact multiset comparison, all other approved lists, all controls
and every runner pin unchanged. `tools/test_probe_features.py` needs no change for this correction.

### 7.4 The concrete private proposal is verified, not landed

[Two-file patch](2026-09-19-radmin-slice10-r2-checkpoint/proposed-repairs/proposal.patch), in a separate
complete working-tree snapshot, applies only §7.2 and §7.3's proposed corrections after the in-fence repair.
Fresh results on that **private proposal**:

- Native wrapper + explicit binary: **2950 cases /195770 assertions /0 failed /0 skipped**.
- Full feature probe: **9 cells /112 checks /0 failed; 58 controls /0 unusable**.
  Its ownership subset is **43 controls /0 unusable**, with all existing violations still RED.

The shared copies of both proposed files are verified byte-identical to `4ad9c34`. These green results
must not be read as a green shared candidate. No new owner policy ruling or commit is needed: QA must
reissue the two instrument paths at an explicit brief hash, then the coder can land the exact scoped
repairs and re-run the full chain fresh.

### 7.5 Completed measurements and unfinished work (D3)

Fresh and completed on the implemented production inputs:

| Instrument | Measured result |
| --- | --- |
| Simulator builds | Base and candidate stock `lus` built, both core variants compiled |
| Corpus | **36/36 whole-file byte-identical**, anchors pass; s18 `32afbf11e43b4bf9d0bd470ad502ba0a`, 269517 events |
| Console default + `--no-neg` | **84 structural**, **152 controls**, all prior controls retained; full run PASS |
| Inbox iteration, both arms + explicit CLIENT `--no-neg` | **1394 /477 checks**, zero failures; probe-only, not a control gate |
| Custody USB and BLE-line, default + `--no-neg` | PASS at their unchanged pins |
| Stock board pair, fresh base and candidate | Both PASS, no index/PATH workaround; jobs=1 |
| Gateway RAM /flash | **203820→203740 (−80)** / **572224→572240 (+16)** |
| Mobile RAM /flash | **211764→211724 (−40)** / **1394520→1394704 (+184)** |

The RAM symbol diff closes exactly: `mrnv::save(...)::cur` **280→240** on both; `g_node` **157344→157304**
on gateway and **122176 unchanged** on mobile; no other RAM symbol moves. Flash section deltas are gateway
`.text +16`, mobile `.flash.text +72` and `.flash.rodata +112`; all other loadable sections stay constant.
Full symbols, sections, ELFs, payloads and manifests are retained for further attribution and QA.

The **61-battery /984-pattern** selector audit passes with every pattern matching once; S is
`{devicenv, radmin8node}`, H is the existing 61-battery floor. The actual union was stopped after its workers
correctly refused their failing clean baseline. **No mutation result is claimed**, including no borrowed
983-RED total. The dependent probe chain was stopped, as was the remaining board tail after the completed
pair; XIAO and census did not complete. Tools discovery, the complete ABI sweep, references,
inventory/authority/A0/literals, remaining probe/control runs and a valid full union remain required.
No full-gate PASS, completed freeze, independent QA PASS or metal result is claimed.

**PIN re-synced? NO — the shared custody size pin and ownership-site census still require B431/B432's
fence fold-ins.** All currently fenced production and runner re-pins are implemented; this statement
is deliberately not the final receipt's required YES line.

### 7.6 Preserved inputs and dispatch

[Checkpoint archive and manifest](2026-09-19-radmin-slice10-r2-checkpoint/README.md) contains the complete
uncommitted overlay, source inventories, failed/interrupted logs, positive measurements and the verified
private proposal. Register §0 and design §19.1 row 10 carry **HOLD B431/B432**; other QA preparation edits
remain intact. The brief is untouched. Resume requires QA's explicit instrument-fence reissue, not a commit.

## 8. Revision 3 implementation freeze — ready for independent QA (2026-09-19)

**Coder gate PASS with the standing B342 exception. Independent QA is pending.** This section supersedes
§7's implementation HOLD; its failed and interrupted results remain historical. **Every §8 instrument
was re-run fresh on the repaired candidate. No result from the interrupted revision-2 run is inherited.**
Nothing is staged or committed; simulator source is unchanged and clean. Part 57f on metal is **NOT RUN**.

### 8.1 Resume validation and the exact two repairs

- MeshRoute base: `4ad9c343b42bab4c91bd8e6721b45b2dfb769e7f`.
- Simulator: `6585649ea5a780f0542b2931853a667be56a5b2b`.
- Authorized revision-3 brief: SHA-256
  `5eab4af5cd08666368a58a0196bb034e7a8822b563175378b519fc136de71e9b`, unchanged through this freeze.
- Compared with §7's complete checkpoint, exactly five preparation documents changed before resume:
  the brief, register, design, `MEMORY.md`, and `tracker.md`. They were inventoried and preserved.
  All preserved implementation inputs matched; no anchor disagreement or new ruling was found.
- Applied the checkpoint's exact two-file `proposal.patch`: the custody test's **235248→235208** literal
  and derivation comment; the two obsolete ACCEPT guard entries in the ownership census and its comment.
  Every custody-specific assertion, census comparison, other approved-site list, control and feature-runner
  pin is unchanged. **No production edit was made during this resume.**

The full Slice 10 candidate changes **15 fenced production/test/tool paths** relative to base. Complete
snapshots included **4225 tracked and untracked input records**, including the QA preparation and prior
checkpoint evidence. All compiled, test and tool inputs remain identical across the shared checkout and candidate snapshots.
The mandated inventory regeneration moves exactly one source-location hint (`peerkey` / `service_console`,
`fw_main.cpp:1215→1214`); all 197 rows are semantically unchanged. That generated document is copied back
to the shared tree. This expected P4 anchor relocation and the final reporting delta are separately hashed.

### 8.2 Fresh full-chain results

| Instrument | Fresh result |
| --- | --- |
| Native wrapper, then explicit binary | **2950 cases /195770 assertions /0 failed /0 skipped**; the wrapper's “0 test cases” is not the denominator |
| Extended reference | **94/94 strict**, old **89/89 byte-identical**, all **5 comparator controls RED** |
| Slice 9 reference | **94 unchanged arrays +1 frozen 40-byte legacy frame**, comparator and legacy controls RED |
| Simulator and corpus | Fresh base and candidate stock CMake builds compile both codec variants; **36/36 anchors PASS and 36/36 whole-file byte-identical** |
| s18 | **`32afbf11e43b4bf9d0bd470ad502ba0a`**, **269517 events**, zero assertion failures; authority remains `simulation/BASELINE.md` |
| Board ABI / B278 row ABI | **290 checks /9 controls RED** and **42 checks /6 controls RED**; zero unusable |
| Inbox default (both arms), `--no-neg`, explicit CLIENT | **1394 ACCEPT /477 CLIENT checks**, **61 /69 controls**, zero failures or unusable controls |
| Firmware UI, default and `--no-neg` | **404 /839** checks by arm; **223 controls**, zero unusable; control coverage **703/840** unioned checks, remaining labels disclosed by the unchanged instrument |
| Console sink, default and `--no-neg` | **6 profiles /720 executable /84 structural /905 BLE /6 ownership /3 ownership controls /152 controls**, all PASS |
| Custody USB / BLE-line, both modes | **27 checks /10 controls** and **55 /12**, PASS |
| Feature matrix, both modes | **9 cells /112 checks /58 controls**, ownership subset **43 controls**; zero failures or unusable |
| Deferred actions, both modes | Local **150/151/158/158**, remote **416/518/534/464**, radio **3160/3485/3689/3695**; **40 controls RED**; detailed outputs archived |
| Full tools discovery | **349 tests /0 failures /0 skipped**, using the fresh stock gateway ELF fixture and the pinned simulator source |
| Inventory write /bare /check | **197**, semantically unchanged; one source-location hint moves by −1 line; authority + selftests, A0, literals and whitespace in both repos PASS |
| Warning census | All six environments at **171/175/175/175/179/179**, **zero -Wswitch**; no re-pin |
| Stock board pair /one-off XIAO | Gateway then heltec_mobile, jobs=1, stock tool; XIAO **176556 RAM /699548 flash**; every build PASS |
| Mutation union S ∪ H | **61 batteries /983 RED /1 known unusable B342 /984 configured /zero vacuous**; every pattern matches exactly once; every worker's clean baseline is **2950/195770/0** |

**B433 — corrected coder gate ordering.** My first full tools discovery started before the mandated
inventory `--write`, so `test_tracked_table_equals_fresh_generation` saw the old `fw_main.cpp:1215`
hint and failed against 1214: **349 run /348 passed /1 failed /0 skipped**. This was a sequencing error,
not a changed command row or a new instrument repair. The generated one-anchor update is preserved in
the shared tree, the first failure log is archived, and the **entire 349-test discovery was then rerun**
after inventory generation completed. Only that complete green rerun is the final tools verdict.

S is freshly derived as `{devicenv, radmin8node}`; H is the existing 61-battery set. The mutation harness is
byte-identical to base. Its B217 **advisory** cross-check still names the base **2950/195768** and prints
its non-blocking discrepancy banner; the actual baseline is derived as **2950/195770/0** in every worker,
and all 984 entries execute. No pattern moved, so the brief's conditional harness-edit fence is unused.
This advisory is not a failing baseline gate or an unusable mutation (B434, record-only). `sliceBmac` M04 remains the single known unusable control and is **not counted RED**.
The full selector audit, per-battery logs, exit codes and count reconciliation are in the freeze archive.

### 8.3 Layout, linked footprint and preservation

| Measurement | Base → frozen candidate |
| --- | --- |
| Main Blob, native /ARM /Xtensa | **280→240 bytes**, alignment **8**, activation offset **276→236**; version/floor **26/26** |
| Node native /mobile /gateway | **235248→235208 /122176 unchanged /157344→157304** |
| Gateway RAM /flash | **203820→203740 (−80) /572224→572240 (+16)** |
| heltec_mobile RAM /flash | **211764→211724 (−40) /1394520→1394704 (+184)** |

Both board-pair halves are fresh measurements from this run. The RAM symbol diff closes exactly:
`mrnv::save(...)::cur` **280→240** on both boards; `g_node` **157344→157304** on gateway and **122176
unchanged** on mobile; no other RAM symbol moves. Flash closes at the loadable-section level: gateway
`.text +16`; mobile `.flash.text +72` and `.flash.rodata +112`; every other loadable section is unchanged.
The complete symbol-size deltas, sections, payloads, manifests, compiler state and both pairs' ELFs are
retained. Object counts stay **285 gateway /329 mobile**. Project-source `-Wreorder` diagnostics are zero; the three CustomLFS vendor diagnostics appear in both fresh gateway halves and XIAO and are retained in `warning-audit.json`. These net flash increases are the explicitly
recorded revision-3 §4 expectations, not an extra allocation or an unreported assumption.

The source audit proves every remaining Blob field's declaration and order is unchanged. The entire
`device_nv.h` suffix from the separate Identity record onward is byte-identical, including all other
stores and wrappers. `firmware_config.cpp`, wire/reference inputs, the two probe-discovery test files and
the mutation harness are unchanged. No legacy mirror member, accessor, stub or call remains; the five
remaining production comment lines are classified historical/version notes and existing forbidden-symbol
or restore-analogy comments, not executable uses.

The inbox probe executes the real ESP32 NV arm over its byte-backed medium: a 280-byte v25 record is
refused, v26 save/reload succeeds, both exact schema lines execute through the real formatter, all four
separately filled remote-admin stores retain their bytes, and only the one main-record save increments
the write count. Both arms retain A7-1..A7-8, add **20 shared checks**, and execute the new wrong-slot
control. The four store witnesses are labelled synthetic bytes. **This does not claim execution of
`nv_load_stamped`, real flash or the full boot/reseed flow**; that unchanged service and the on-metal
sequence are Part 57f.

### 8.4 Re-pins and independent handoff

PIN re-synced? YES — native/gateway Node 235208/157304, mobile 122176 unchanged; Blob 240/8/236 and version/floor 26/26; custody whole-Node literal 235208; ownership census ten→eight sites, ACCEPT five→three, feature pins 9/112/58 unchanged; console structural 83+1=84 and controls 149+3=152; inbox checks 1374+20=1394 /457+20=477 and controls 60+1=61 /68+1=69, with floor controls re-fixtured to 25/27. All other gate-enforced pins and every prior row/control are retained; all were re-measured in the fresh chain. The non-gating B217 mutation cross-check remains at its base assertion figure, as disclosed above (B434).

[Freeze archive and reconstruction notes](2026-09-19-radmin-slice10-r3-freeze/README.md) retain the complete
uncommitted overlay, pre-gate and final SHA-256 inventories, exact repair hashes, source/preservation
audits, fresh logs and results, both corpus runs, both board pairs, native binary and detailed action
proofs. The base hash alone is not the candidate: use the complete overlay and its inventory.

**Dispatch: independent full QA on this freeze.** B430–B432 await QA closure in place; no arc-complete
ruling or independent PASS is claimed by the coder. Part 57f remains metal-pending. The brief and simulator
are untouched; there is no commit prerequisite.
