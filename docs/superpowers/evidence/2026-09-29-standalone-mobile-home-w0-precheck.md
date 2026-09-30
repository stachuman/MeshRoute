# W0 pre-check — identity-record reconstruction and name refusal

**2026-09-29 · independent QA → Author · PRE-CHECK COMPLETE; no implementation authorization.**

Base: MeshRoute `0f23aee8f5eeaa6e176e3ea37b2acaffb33622ec` (owner’s W6 commit); simulator `6585649ea5a780f0542b2931853a667be56a5b2b`. Both were clean on entry. Design input: standalone Home r2.23, §4.3 and §13 W0. Applied V1/V2, U1/U2, C1, D3/D4 and P6/P7. This is a measured before-state, not a W0 gate or approval of a brief.

**Recommendation:** one W0 fix package, with one pure identity-record conversion and one stateless rename service. Use live identity values to build every candidate, including when NV returns a valid but different record. Read NV only for rename’s exact-record no-op test. Keep `regen`’s admission, entropy, checked write and subsequent installs in their current order. No resident allocation, NV version, wire or simulator change is needed.

Two important corrections to the request: **failed `load_id` does not promise an untouched/zero destination**, and **inbox-verbs currently stubs the cfg handler**. The latter needs an explicitly fenced real-config probe arm, not merely extra calls through its present stub. Separate findings B482–B486 are registered; they do not silently enlarge W0.

## 1. Base and preservation (Q1)

[inputs.json](2026-09-29-standalone-mobile-home-w0-precheck/inputs.json) records HEAD, initial status, and all **2123 MeshRoute entries** and **285 simulator entries** from `git ls-files --cached --others --exclude-standard`: file SHA-256/length, and the gitlink identity for `spec/docs`. This inventory was taken before creating this evidence. There is no uncommitted W6 candidate to reconstruct.

The only existing file changed by this pre-check is the register: five new findings and its next-number marker. B440/B448, the design, earlier evidence, tracker, MEMORY, source, tests, tools and simulator are preserved. The Author should pin the resulting register as preparation, alongside this report and its checksum inventory. Nothing staged or committed. The final comparison is `preservation.json`; the exact register delta is `register.diff` beside this report.

Raw build/run output lives under ignored `artifacts/2026-09-29-standalone-mobile-home-w0-precheck/`. Retained scripts and compact results are in the [checksummed evidence folder](2026-09-29-standalone-mobile-home-w0-precheck/SHA256SUMS). Counterfactual/fault runs use temporary host files, not edits to product files or physical NV.

## 2. Independently executed baselines (Q7/Q9)

| Instrument | Fresh result |
| --- | --- |
| Native: `pio test -e native`, then the binary | **3055 cases / 199532 assertions / 0 failed / 0 skipped** |
| Fresh simulator and full anchored corpus | **36/36**, byte-identical to W6 QA on all 14 comparison fields; fresh lus hash also identical |
| Stock ABI | **290 checks; 9/9 controls RED; 0 unusable** |
| ABI plus IdBlob overlay | **299 checks; 9/9 controls RED; IdBlob 80/4 on all three ABIs** |
| Firmware-UI default | **591 / 1059 / 591** checks; **240 controls**, all accounted exactly once, no guard failures/unusable controls |
| Board-UI default | **592 identities accounted**; canvas **124/110**, structural **23**, wiring **60** with **186 RED** wiring controls, canvas mutation controls **60/3** |
| Console-sink default | **720** checks, structural **84**, BLE guards **905**, ownership **6 / 3 controls**, **152 controls**, zero unusable |
| Inbox-verbs default, three arms | ACCEPT **1400/63**, CLIENT **483/71**, OLED **27/7** checks/controls; zero failures/unusable |
| Device-radio default | **96** device + **41** board checks; **25** structural; **72 controls**, zero unusable |
| Prov-tx default / feature ownership | PASS; **20 structural / 47 RED controls** for prov-tx; ownership scan passes |
| Command inventory | **197 rows**, byte-identical to fresh generation |
| Two back-to-back stock board pairs | Both repeatability comparisons PASS. Gateway **203740 B RAM / 571936 B flash**; heltec_mobile **219540 B RAM / 1400276 B flash** |

The full before-stream manifest is `corpus-manifest.json`. The fresh simulator was built out of tree from its current checkout and the current MeshRoute core. **W0 prediction: all 36 streams byte-identical on every comparison field**, including the s18 keystone from `simulation/BASELINE.md`: **269517 events / MD5 `32afbf11e43b4bf9d0bd470ad502ba0a`**. P6 still requires a fresh corpus at both gates.

The board pairs were run under `.pio-measure/w0-precheck/base-1` and `base-2`, with no intervening checkout or normal `.pio` edits/builds. All four manifests and both stock comparisons are retained. Evidence assembly resumed only after the second pair and comparisons completed.

No mutation battery or warning census was run for this pre-check; those instruments were audited. No hardware flash, power-cut, radio or BLE notification test was run. The board-UI baseline is its full default run, not the retired W49/W51/W54 exception. Board measurements are qualification artifacts, not firmware to flash; the coder captures its own stable before/after pairs under `.pio-measure/w0/`.

Initial measurement setup errors are retained, not counted as product failures: the first inventory attempt encountered the `spec/docs` gitlink; the first corpus invocation named `sim/lus` rather than the built `sim/orchestrator/lus`; early scratch-config builds exposed missing platform defines/type aliases and one missing board global. The final reusable real-TU reproduction builds and reproduces its recorded output. No failed attempt supplies a baseline figure.

## 3. `/mrid` authorities, read failures and reproduced loss (Q2)

The carrier is `mrnv::IdBlob` (`src/device_nv.h:146`): **80 B, alignment 4** on native, mobile and gateway. Offsets: magic 0, version 4, name_len 6, seed 8, name 40, latitude 72, longitude 76. There is no implicit tail padding. `kIdMagic=0x4D524944`, `kIdVersion=1`; name is counted, at most 32 bytes, not a C string. This type is not in the stock ABI pin set; the additive `identity-abi.json` measures it without changing the stock probe.

| Reader/writer | Actual behavior at this base |
| --- | --- |
| `firmware_config.cpp::handle_cfg_set`, lat/lon (:262–270) | Zero-init candidate; load NV; on false copy only running seed; change one coordinate; publish that coordinate to global and NodeConfig **before** the write; stamp; one `save_id`; report success/failure. |
| Same, name (:274–282) | Zero-init; load; on false copy only running seed; clamp `strlen(val)` to 32; overwrite counted name; stamp; one save; publish live name only on successful save. |
| `firmware_commands.cpp::do_regen` (:1067–1107) | CLIENT remote-debt admission first; zero-init; unchecked `load_id`; draw a new seed; stamp; checked save; only then derive/install messaging identity and crypto identity. Prints the saved record’s name. It does **not** reload the Node’s live name or position. Its CLIENT warning and preservation obligations remain. |
| `fw_main.cpp::setup` (:915–929, :1024) | Successful load uses the record. Failure mints a seed, stamps, sets name_len to zero, attempts save, then installs the identity even if volatile. Position is taken from the candidate; failed/partial reads need not have left it zero. Boot then copies position into config and calls `on_init`. **Boot policy is outside W0.** |
| `fw_main.cpp::ble_dispatch_line`, whoami (:525–546) | Unchecked `load_id`; bounded name length, but uses rejected record bytes if they were read. No live-name lookup. This is the `ready` response path; B485. |
| `firmware_commands.cpp::handle_whoami` (:1399) | Uses live `Node::effective_name`, unlike the BLE-specific arm above. `print_identity` (:1049) formats a supplied record; it does not read storage. |
| `device_nv.h::{load_id,save_id}` (:1470–1474) | Exact returned-byte-count/magic/version predicate; no field/seed/name-content validation. `save_id` directly calls `write_slot`, with **no coalescing**. |
| Whole-store destruction | `factory_erase` and nRF `mount_or_repair` can erase `/mrid`; the latter probes `/mrid` in its six-file list. These are not additional identity conversion writers. |

The real bool load calls `read_slot` **without** `SlotIo`. ESP32 can fail on namespace open, absent key, an oversized blob (Preferences returns zero), short/failed reads, bad magic, or bad version. nRF can fail on mount/open, negative/short reads, magic or version. A full read with a rejected header leaves the payload in `out`; a short read can leave a prefix. The caller’s initial `{}` is not a guarantee about the post-read object.

**Existing backend distinction:** nRF’s bool path does not ask the file size; an overlong file with a valid 80-byte prefix can pass. The strict predicate checks the returned count, not the whole file length. Do not generalize the ESP32 oversized-blob refusal to nRF; the analogous `/mrjoin` problem and its separately scoped remedy are B219. A synthetic 81-byte file passed through the real `fs_read_slot` and `blob_valid_exact` measures 80 bytes read, zero size queries and acceptance (`oversize-query.cpp`); registered separately as **B486**. This is not a full nRF adapter or hardware-filesystem run. W0 must not change shared read/validation or boot policy under the carrier fix.

After a successful boot, later slot loss, read-open failure, a refused/truncated previous write or corrupt headers can reach B440. On nRF a write removes the old file before opening/writing the replacement, so write failure itself can leave the next read absent/short. The evidence simulates these medium facts; it does not assert their incidence on hardware.

### Executed characterization

`reproduce-identity.py` compiles the **unchanged real `firmware_commands.cpp` and `firmware_config.cpp`**, core/console and the existing platform/NV fakes. It removes the probe’s eight competing config-handler stubs only in its temporary driver. Each fixture first saves and successfully reloads a valid record, installs its live identity/name/position, then injects a medium fault. Four commands × eight conditions = **32 recorded cases**, plus grammar, BLE-branch and leaf-label measurements; **54 before-state characterization checks** passed. This is proof of defects, not a fix PASS.

Starting live values: name `Live Name`, latitude **521234567**, longitude **211234567**.

| Command after absent/unreadable/oversized ESP32 record | Saved result | Live residue |
| --- | --- | --- |
| `cfg set name New Name` | New name, correct running seed, **lat/lon 0/0** | Old live position remains. |
| `cfg set lat 53.25` | Correct running seed, lat **532500000**, **empty name/lon 0** | Live name and old longitude remain. |
| `cfg set lon 22.5` | Correct running seed, lon **225000000**, **empty name/lat 0** | Live name and old latitude remain. |
| `regen` | Fresh seed, **empty name/lat/lon 0** | Live name/position remain until reboot, despite the emptied record. |

A bad-magic full record in this fixture happens to retain name/position; a 50-byte short read retains the old counted name but not the coordinates. Thus “all failures reset all other fields to zero” would be an incorrect brief. The invariant is **no candidate field comes from failed/rejected NV bytes**.

Failed-write controls also characterize B482: name and regen leave live identity unchanged, but lat/lon already publish their new coordinate while emitting `> cfg err nv_save_failed`. That ordering is separate from B440; preserve it unless explicitly added to W0’s contract.

## 4. One conversion and the W7 service boundary (Q3/Q5)

The current product authorities are:

- Seed: `g_identity.seed`, installed at boot and after successful regen. Do not recover it from a later `/mrid` read.
- Name: `g_node.effective_name(out, 32)` → counted live stored-name bytes. W1c removed the synthetic default. Do not reserve a terminator byte or use `strlen` on this copy.
- Position: `g_lat_e7/g_lon_e7`, mirrored to `g_node.config().lat_e7/lon_e7` at boot and in the two setters. Census finds no other production writer of these own-position fields; `provision_apply_live`, the settings service and team transitions do not replace them. They agree at stable product boundaries today, including after a failed coordinate save. Host fixtures can deliberately make them differ; that is not a product GPS writer. The globals are the proposed firmware snapshot authority, with equality tests for their NodeConfig mirrors.

No existing identity conversion was found. `firmware_config.cpp::seed_blob_from_live` builds **`mrnv::Blob` (`/mrcfg`)**, not IdBlob; `join_profile_put`, peer merging and admin identity initializers own different carriers. Do not adapt one by dropping its policy into `/mrid`.

**Smallest proposed seam:** a pure, inline conversion next to `IdBlob` and its constants in the existing `device_nv.h`, above platform conditionals. Suggested interface (a recommendation, not code landed):

`bool id_blob_from_live(IdBlob& out, const uint8_t (&seed)[32], const char* name, size_t name_len, int32_t lat_e7, int32_t lon_e7)`.

It validates the counted length before narrowing, permits an empty live name, initializes the complete 80-byte candidate deterministically, stamps the current constants and copies all fields. Refusal must not yield a partially usable candidate. No NV, globals, Node, Arduino or side effects in this helper. The native test reaches the real helper in `test_device_nv.cpp`, including length 32/33/256, all fields, zero unused name bytes, and arbitrary non-ASCII bytes. Use integer/hex diagnostics, not raw malformed UTF-8.

One thin firmware snapshot adapter in `firmware_config.cpp`, declared in `firmware_config.h`, gathers the three authorities and calls this helper. All four writers use that conversion path with only their intended field replaced. The rename service must pass the **requested counted name** through the same pure conversion/length admission tested natively; a helper that only validates the old live name would not prove B448. The small device adapter may accept that explicit name override without adding retained state. Regen may snapshot with the current seed and overwrite just the seed through its existing RNG call, before its existing checked `save_id`. Keep the actual save in `do_regen` so its write-set readers retain their meaning.

**Introduce the rename service in W0**, declared in `firmware_config.h` and implemented in the same TU. A small typed result (`saved`, `unchanged`, `too_long`, `bad_args`, `nv_save_failed`, or equally explicit names) lets the console print its current success/failure bytes and lets W7 map results to its panel without copying console logic. This is part of the identity fix, not a separate refactor or new resident service object.

Implement design §4.3’s already-reviewed transaction: construct from live values, replace counted name, compare against a successfully loaded durable record **only to coalesce an exactly equal candidate**, otherwise attempt exactly one save, then publish the name. No write on overlength/empty refusal; no live change on failed save; identical durable candidate = successful no-op and zero writes. **Live-name equality alone is insufficient:** if NV is absent, invalid, partial or different, even an unchanged requested name must repair it. Compare complete initialized bytes; do not resurrect rejected record fields.

The raw `save_id` wrapper stays unconditional: regen must always write its fresh seed, and coalescing belongs to the rename service. No retained object or Node/layout change is required. Two 80-byte local candidates plus a counted name are bounded stack scratch, **not** an assertion about optimized peak stack; measure the compiled effect and report it. No new RAM grant is implied.

The console becomes the first service caller; W7 adds the panel caller later. W0 does not add the editor, a UI notification path, or any team/key/routing writes. Home’s existing snapshot publication already reads the live name on its next tick.

## 5. B448 grammar, output and BLE limit (Q4)

`handle_cfg_set` takes everything after the first key-separating space as `val`, through the line’s terminating NUL. It does **not** use `kv_next` and does **not** remove quotation marks. Spaces, additional leading spaces and trailing spaces remain name bytes. Empty value gives `> cfg err bad_args`; all-space nonempty input is currently accepted. Preserve that grammar; W0 is not a new ASCII/editor-repertoire or Unicode-validation policy.

Fresh real-TU measurements: 32 ASCII bytes save all 32; 33 save only 32 and still succeed; **31 ASCII bytes + UTF-8 `ł` (`C5 82`)** saves the trailing **C5 without 82** and succeeds. ` Two words  ` is stored as all 12 bytes; `"Two words"` is stored as all 11, including the quotes.

Recommend exact handler refusal **`> cfg err too_long\r\n`**, before any candidate write or `Node::set_name`. This matches the cfg error prefix and the existing `too_long` vocabulary used by peername/peerkey and `parse_grant_args` (`firmware_config_parse.h:403`; core admission at `node.cpp:2169,2187`). The helper/service must receive a `size_t` typed length, never a length already narrowed to u8.

Required cases: empty; spaces and literal quotes; 31/32/33 ASCII; a multibyte sequence wholly within 32; a 2- or 3-byte sequence crossing byte 32; 32 high bytes; and a length exceeding 255 to catch narrowing before admission. For refused input prove zero writes and unchanged seed, name, both live coordinate mirrors, membership and unrelated NV slots. Name/regen write failures preserve live identity. Keep raw `Node::set_name` and core name-capacity contracts unchanged.

USB’s validated input bound is 1023 bytes (`console_line.h`, `service_console`); BLE derives its larger-than-name command capacity in `device_ble.h`. Both comfortably carry the 46-byte `cfg set name ` + 33-byte test. USB strips transport CR/LF; the shared validator refuses embedded NUL/CR/LF and otherwise admits byte values above ASCII. It does not trim or unquote.

**B483: BLE’s dedicated cfg adapter hides handler errors.** It calls `handle_cfg_set(..., mrcon)` and always emits a fresh cfg JSON object to BLE. The companion’s `AppModel.renameNode` sends cfg-set then whoami; there is no explicit refusal on its link. Source-extracted, verbatim BLE branches invoking the real handler measured cfg JSON only (491 B in this fixture), with `nv_save_failed` / `unknown_key` on USB. A future `too_long` follows the same sink unless that adapter changes. Recommend a **separate transport-contract fix**, not a fw_main change hidden in W0. B448 can prove no truncation and the handler’s loud refusal without claiming a companion-visible error.

**B485: BLE ready can report rejected/stale name bytes.** With a bad-magic record holding `BAD NAME!`, the source-extracted whoami branch publishes that name although the live Node is `Live Name`; when the record is absent it omits the live name. W0’s reconstruction repairs the record on a successful save, but does not close this independent reader defect. Neither BLE proof compiles all fw_main.cpp or measures physical notifications.

## 6. Other setters and scope (Q6)

| Surface | Classification |
| --- | --- |
| `cfg set name` | B448, W0: unchecked operator length, 32-byte clamp. |
| `cfg set leaf_name` (:416–418) | **B484**, outside W0: clamps to **10** bytes, then echoes the full supplied text as successful. |
| `create name=` (:1189, :1205) | **B484**, outside W0: clamps decoded token to **10** bytes; real 17-byte input produced a successful join_started with only `ABCDEFGHIJ`. Its separate 192-byte argument-copy ceiling is unchanged by W0. |
| `joinprofile set ... name=` | Counts typed bytes, copies only bounded bytes, then the service refuses `name_too_long` beyond 12. Its later defensive copy clamp is behind validation, not silent operator acceptance. |
| `team grantkey name=`, `peerkey` optional name, `peername` | Over-cap input is refused; keep their contracts and shared raw core capacity. |
| `Node::set_name`, constructor’s optional name | Low-level bounded setters; not fail-loud operator admission. Firmware’s own callers are boot and cfg name. Simulator constructor/config is outside W0. |
| `peer_key_set`, received INTRO/key names, leaf codec/hash builders, NV peer merge | Defensive bounded carriers/received advertisements, not additional user-input setters to “repair” in this slice. `peer_name_set` refuses over-cap input. |
| UI label projection/copy helpers | Display budgets and abbreviation, not persistent name setters. W1/W4a behavior stays. |

See `symbol-readers.json` and the source-reader census for exact users. Register B482 also records coordinate apply-before-save; neither it nor B483–B486 is automatically folded in. No change to `create`, leaf naming, boot, BLE response grammar or peer-name precedence is authorized by this pre-check.

## 7. Instrument seam, readers and recommended fence (Q7)

### Real behavior versus structural witnesses

- Native compiles neither config.cpp nor commands.cpp. It can test the pure conversion/refusal in `device_nv.h`, plus any pure types placed in an existing native header. Native tests alone cannot close B440/B448’s real wiring.
- **Inbox-verbs** compiles real commands.cpp and executes regen, but `probe_main.cpp:271–278` stubs cfg and its seven sibling handlers. **No existing probe executes real `cfg set name/lat/lon` with a failing slot.** Device-radio, prov-tx and console-sink read config structurally; they are not substitutes.
- The supplied reproduction proves a real-config arm is feasible. It needed normal LORA build defaults, the platform’s `default_output_dbm` header, a host `__FlashStringHelper` type alias compatible with the existing fake `F`, and the missing `g_persist_team_local_id` definition. All are harness/platform scaffolding; no product change was needed. The NV fake already stores the 80-byte carrier truthfully. Keep its `retain_on_fail` / `drop_on_ok` controls.
- Recommend an explicitly named identity/config arm under inbox-verbs, reusing its real router/core/store support. Fence its driver, local platform shim, builder and the narrow stub exclusion in the shared fixture; do not silently turn the old stub-based dispatch-ownership cases into different cases. Run the new real-config proof under both ACCEPT and CLIENT role defines; preserve the CLIENT regen-debt rejection. Exercise USB-shaped routed execution and a supplied LineSink for regen. Do not call the new cfg arm “the real BLE adapter”: B483’s dedicated branch remains outside that proof.
- Existing regen R12/R21/R22/R25/R26 fixtures seed **only NV** via `seed_id`; they do not install the corresponding live name/coordinates. Authorize fixture corrections that establish the actual boot/live precondition while keeping every expected successful output and persisted field. **Do not globally make `seed_id` install crypto:** R1–R17 intentionally observe crypto-not-ready becoming ready. The transcript driver also seeds only NV; audit/update its live-name precondition separately, preserving its sink-fidelity meaning.
- Pin the inbox builder’s public prefix/boundary consumed by `tools/probe_deferred_actions/run.py`: exactly one `rc=0\nif ! build_support;`, and the `build_support` / `build_variant` calling contract. Add the identity arm without leaking it into the deferred-action build. Run that stock consumer at both gates. Its source need not change.

### P7 readers that constrain the edits

| Reader | W0 obligation |
| --- | --- |
| Board-UI W12–W18 and siblings | These check `/mrcfg` notification placement/feature neutrality in cfg/provisioning functions, not identity field preservation. Keep statements and function boundaries they own. Default run must keep all **592 identities**; no manifest delta is needed for W0. W49–W54/help still guard dispatch/OLED sites. |
| Console-sink `structural.py` S37/S48 + S-C37/S-C48 | Both demand `save_id` inside `do_regen` and forbid administration-store writes. Keep the actual checked save in that function. New snapshot conversion does not require re-anchoring these predicates. S67’s activation cfg admission and its controls are separate; leave them intact. |
| Device-radio `structural.py` S16/S17 | Shared radio apply/validation and absence of direct frequency bypass; identity edits do not alter their predicates. Full runner baseline green. |
| Prov-tx | Reads the team/config transaction binding, ordering and one-write/notification properties. Preserve those functions. Baseline green; rerun if changes cross their bodies/anchors. |
| Feature ownership scanner | Reads config and command feature ownership; a neutral identity helper should introduce no feature gate. Run stock scanner, preserve control outcomes. |
| Inbox C8/C9/C10, C12/C13 and CLIENT C8-C10 | Supplied sink, output/count/storage honesty and live regen admission. Keep/re-anchor only with an explicit property ledger; do not weaken to accommodate fixture drift. |
| `gen_command_inventory.py` | Scans both **config.cpp and commands.cpp**, as well as their caller hops. Preserving only commands.cpp’s line count is insufficient if config edits shift its rows. |
| ABI/native/UI/other header includers | `device_nv.h` is widely compiled. New pure helper is additive; layouts, schema constants and existing wrappers stay. Stock ABI plus IdBlob overlay and ordinary native rerun detect accidental drift. |

`readers.txt` lists the 50 direct test/tool source readers or includers found by the filename census; `symbol-readers.json` adds symbol users. Include readers matter even when no textual predicate matches the new helper.

**Inventory recommendation:** permit normal regeneration of the tracked command inventory **for line-anchor movement only**, requiring **197 semantic rows unchanged**, identical authority/transport/disruptive properties, and the real generator/checker/control gates. The main regeneration output is `docs/superpowers/evidence/2026-09-04-radmin-command-inventory.md`. Do not force compressed code to preserve historical lines. If the Author instead freezes all relevant line numbers, it must prove that in both edited TUs; changing just a usage/comment line is not W0’s situation.

**Proposed product fence:** `src/device_nv.h` (pure identity helper only; no read/write/schema change), `src/firmware_config.h` (typed rename result and shared adapter/service declarations), `src/firmware_config.cpp` (three cfg arms/adapter/service and accurate touched comments), `src/firmware_commands.cpp` (regen snapshot and touched comments). No fw_main, lib, variants, platformio, wire or simulator edits. There are **three cfg keys, four commands total**, not four separate cfg keys.

**Proposed test/tool fence:** `test/test_device_nv.cpp`; `tools/probe_inbox_verbs/{run.sh,probe_main.cpp,transcript_main.cpp}`; explicitly named identity driver and local platform shim there; `tools/probe_ui_model_mutations.py` for new identity-helper entries and native floor synchronization; generated command inventory for allowed line changes. Add another existing native test file only if the Author selects a different pure-header home. Do not grant broad shared-fake or reader rewrites speculatively; name any necessary changes before dispatch.

**Mutation selectors:** with the above product fence, existing selector (a) is **devicenv, 46 entries**. No native battery targets either .cpp or firmware_config.h. Add identity-helper mutants (dropped seed/name/either coordinate/stamp, accepted overlength/narrowing) and real-router controls for each producer, overlength early return, failed save publication, and no-op/write honesty. Suggested selector (b): **config 32**, **w1cname 4**, **consoleline 12**, **radmin4verbs 30** (regen debt/admission contract): existing union **124**, before new entries. Justify final selection in the brief. If config_parse.h is touched, selector (a) additionally includes **cfgparse 8 + sliceDtoken 2**; do not miss the second target on that same file. No existing mutation anchor needs moving for an additive helper and unchanged legacy wrappers. Native PIN counts are derived fresh, never copied from this ledger.

## 8. B478 fit (Q8)

The defect reproduces on this base: the actual AST-extracted `run_suite` with a real child emitting raw `0xBB` raises **UnicodeDecodeError** before producing a verdict. Only its build response is stubbed. `b478-reproduction.json` records the current function hash. The orchestrator rejects the lost worker, so this remains an **unusable-run bug, not false PASS**.

Small tool fence: `tools/probe_ui_model_mutations.py::run_suite` and its output/retention path as needed, plus `tools/test_mutation_unusable_reason.py` (already AST-tests the function and failure kinds). Audit dependent tests such as `test_worker_formula_derived.py` without changing unrelated behavior. A byte-safe policy must retain evidence and preserve existing ASCII doctest parsing, build-vs-run classification, exit/signal handling, worker pins and completeness checks. Suggested policy: capture raw bytes, preserve them, and produce an escaped diagnostic view; do not silently drop undecodable bytes or accept a crashed child.

Required tests include malformed stdout **and stderr**, raw C5 and BB, valid UTF-8, valid doctest failure summaries alongside raw bytes, no-verdict/signal/build failures, and regressions that restore strict decoding or drop evidence. Run the harness’s selftest-unusable, dedicated tests, full tools discovery, then the full configured mutation union at the current measured native floor, with zero lost workers/banners/vacuous entries and the known unusable disposition explicit. This is global harness code; a single green native run is not its gate.

W0 **can** encounter B478 if a mutant’s assertion prints the deliberately split/high-byte name. It is not limited to W7’s `»` renders. W0 can avoid depending on raw text diagnostics by comparing byte arrays/counts with hex diagnostics, but that does not fix B478. **Recommend a separate tool package; the owner decides any fold-in**, as requested. If folded, fence/gate an instrument-first increment before the product fix; do not quietly change the harness decoding policy under a PIN update.

## 9. Packaging and gate recommendation (Q9)

One W0 fix package is coherent: B440’s complete live reconstruction, B448’s admission, and the already-reviewed rename transaction share one identity surface. A mandatory helper to eliminate partial reconstruction is part of that fix; an unrelated config extraction or file move is not. W7 then consumes the service. No wire, NV layout/version or core change; no resident allocation requested. **No new owner product ruling is needed for that scope.** B478’s optional tool fold and any decision to add B482/B483/B485 are separate scope decisions, not prerequisites silently imposed here.

Coder: source-validation against commit plus this preparation inventory; baseline real probe/board figures before editing; native build **and binary**; corpus with stream comparison; stock ABI + IdBlob overlay; full default inbox-verbs including the new real-config arm and unchanged legacy controls; console-sink, board-UI and deferred-actions consumers; relevant ownership/radio/provisioning readers; tools discovery when tools change; inventory/authority/literals/whitespace checks; warning census’s six pinned environments; only gateway then heltec_mobile board measurements; justified mutation union. Preserve unrelated store contents, live remote debt/refusals and transport authorities.

Independent QA per P6: fresh native, full corpus, the two measured boards, **all touched batteries**, the new real-config probe and affected stock probes/readers. Full dependency union and warning census remain coder-owned unless the final brief explicitly calls for more. The report must include exact failure outputs, coverage limits, initial/final hashes, simulator preservation and **`PIN re-synced? YES — <derivation>`**. Existing ABI pins should remain; explain all linked RAM/flash movement rather than assuming a byte-neutral fix.

Expected proof matrix: healthy/absent/unreadable/short/bad-header storage × name/lat/lon/regen; live-vs-durable disagreement; exact full 32-byte/high-byte name; new-name save success/failure/no-op/repair; caller-specific field overrides; no writes on refused input; one checked regen write and post-save installs; preserved client busy guard and administration stores; no mutation of boot read/re-mint policy. The physical flash-failure/power-loss residue stays with existing NV metal procedures; a host fake is not a metal PASS.

The Author can write the W0 brief from this ledger. B440/B448 remain open until implementation passes independent QA. B482–B486 remain separately scoped; next free finding **B487**.
