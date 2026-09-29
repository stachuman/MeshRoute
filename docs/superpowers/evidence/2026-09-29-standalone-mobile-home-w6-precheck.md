# W6 pre-check — saved phrases, catalog v2 and mandatory review

**2026-09-29 · QA → Author · PRE-CHECK COMPLETE; implementation not authorized.**

Base: MeshRoute `70ff486b40c9b07001b33bcbcdf640ad494c1149`; simulator `6585649ea5a780f0542b2931853a667be56a5b2b`. Design input: standalone Home r2.22. This is a source ledger and independently executed baseline, not a review of a W6 brief or an implementation gate. D5–D9 are not reopened.

**Author can write the brief after settling two decisions:** B475's bounded reply contract, and the non-catalog allocation. The catalog itself measures **+7440 B**, exactly, on all three ABIs. A concrete shared-storage review candidate measures **+303 B on the two board ABIs / +319 B host**, including the proposed static 199-B send line, beyond the catalog. These are measured type/object allocations, not granted or linked RAM increases. An asynchronous list emitter would need an additional, separately measured allocation.

The pre-check also corrects three assumptions: `reset all` emits the defaults, not the previous maximum-length catalog; the USB stage **does** pump between lines but can still overflow; and compose rows need **no new length field** to display the abbreviation.

## 1. Base, authority and preservation (Q1)

Read `AGENTS.md`, `docs/CODE_GUIDELINES.md`, the roles/process rules, `MEMORY.md`, the named design sections, current register and relevant instruments. Applied V1/V2, D3/D4, P6/P7 and M1. The role assignment for this arc remains QA pre-check / Claude Author / separate coder.

The owner commit includes W2 and B459/B461. There is no stacked uncommitted implementation. The incoming preparation change is the author's B475 register row and next-number update; [preparation.diff](2026-09-29-standalone-mobile-home-w6-precheck/preparation.diff) preserves it. [inputs.json](2026-09-29-standalone-mobile-home-w6-precheck/inputs.json) pins all **1977 MeshRoute** and **285 simulator** git-visible paths by SHA-256 and byte count (own evidence excluded), not just HEAD. Its status includes the pre-check folder created to hold the inventory.

Only this receipt/evidence and a new **B476** register entry were written by QA. B475, B335, design, tracker, MEMORY, source, tests, tools, NV data and simulator were not edited. Nothing staged or committed. The final preservation comparison is recorded in `preservation.json`. The author's next preparation set must include the register with B476; next free finding is **B477**.

All counterfactual measurements below use disposable `/tmp` copies or generated queries. They are labelled measurements, not a production candidate. Scripts, compiler invocations and results are supplied. No counterfactual executable is counted as a stock baseline.

## 2. Independently run baselines (Q8/Q12)

| Instrument | Fresh result |
| --- | --- |
| `pio test -e native`, then `./.pio/build/native/program` | **3031 cases / 198613 assertions / 0 failures / 0 skipped** |
| Fresh out-of-tree simulator build; corpus `--require-anchors` | **36/36 anchored**, s18 **269517 events**, MD5 **32afbf11e43b4bf9d0bd470ad502ba0a** |
| `tools/probe_board_abi.py` | **290 checks; 9/9 controls RED; 0 unusable** |
| Firmware-UI default, all three arms | **557 / 1023 / 557 checks**, zero failed; **238 declared controls, 238 accepted exactly once**, zero guard failures |
| Board-UI default, including canvas mutations | **592 declared identities / 592 accepted exactly once**; V3 124, V4 110; traits 14, missing-trait 12, structural 23, wiring 60, wiring controls 186, canvas controls 60+3 |
| Console-sink default | **720 profile checks**, structural 84, BLE guards 905, ownership 6 / 3 controls; **152 controls, 0 unusable**, PASS |
| Inbox-verbs default (both arms) | ACCEPT **1400 checks / 63 controls**; CLIENT **483 / 71**; zero failures/unusable, BOTH ARMS PASS |
| Command inventory | **197 rows**, byte-identical to fresh generation |
| Stock board measurement, two consecutive ruled pairs | gateway **203740 B RAM / 571936 B flash**; heltec_mobile **211788 / 1397696**; both exact repeatability comparisons PASS |

The full stream manifest is [corpus-manifest.json](2026-09-29-standalone-mobile-home-w6-precheck/corpus-manifest.json), with current `BASELINE.md`, source and executable hashes. It is the W6 before-state. **Prediction:** no core, wire or simulator edits ⇒ all 36 streams byte-identical, including every manifest per-scenario field; still rerun at the gate per P6.

`runs.json` records commands/exits. The inbox runner already runs both arms. My orchestration mistakenly scheduled a second invocation with `--client` (not a supported selector); I stopped that redundant invocation, use no result from it, and ran the remaining inventory command separately. The completed default run above is unaffected. Initial scratch-query compile/fixture errors are not product failures; the final real-parser measurement uses maximum valid DM ID 254, not reserved 255.

Board output is under `.pio-measure/w6-precheck/`, not docs. Neither checkout files nor `.pio` were changed between its two runs. Per-board manifests, repeatability comparisons and raw-log checksums are retained after the pair. Firmware hash/section/object baselines are in those manifests; new code needs new measurements. No mutation union or warning census was run for this pre-check; their readers were audited. No physical hardware, NVS occupancy dump or power-cut test was run.

## 3. Catalog layout, old-record recognition and writes (Q2)

Source authorities: `src/device_nv.h::{UiPresetSlot,UiPresetBlob,kUiPresetTextMax,kUiPresetMagic,kUiPresetVersion,UiPresetRead,ui_preset_blob_state,load_ui_presets}` (currently :340–389, :926–960, :1504); `src/firmware_ui_presets.h::PresetCatalog::{begin,read_store,commit}` (:511, :615, :646).

| Field / type | Current | W6 measured shape |
| --- | --- | --- |
| Slot fields | enabled/loc/len: three u8; text[18] | same three u8; text[164] |
| `UiPresetSlot` | 21 B, align 1 | **167 B, align 1** |
| Blob header | magic/version/padding/generation, 12 B | unchanged 12 B |
| Slots | 17 × 21 | 17 × 167 = 2839 B |
| Named tail | 3 B | **1 B** at offset 2851 |
| `UiPresetBlob` | 372 B, align 4 | **2852 B, align 4** |
| `PresetCatalog` (live + current durable + candidate blobs) | 1144 host / 1132 boards | **8584 / 8572**, exactly **+7440** |

Keep the magic constant `0x4D525531` (symbolically `'MRU1'`); set version **2**. The magic identifies the store family, not the accepted version. No wire version or `/mrcfg` change is needed. A u8 length can represent 163; the canonical-tail loop can represent index 164.

A distinct old-v1 read result is the smallest explicit shape: backend failure first; absent/oversize as today; **size == 372 && magic == MRU1 && version == 1** identifies old v1 before the exact-v2 check. Do not inspect old flags, generation, text or padding, and do not instantiate a migration struct/parser. The backend necessarily reads raw bytes to obtain the header; “no reader of old contents” means no interpretation/conversion of old slot contents. Test an old header with deliberately nonsensical payload and obtain the same old-v1 diagnostic.

`UiPresetRead` currently has four values. Adding `old_v1` makes five classified read results without a data-size increase. The design's “four-state rules unchanged” must be clarified by the Author: existing absence/invalid/io/canonical semantics remain; the old-record diagnostic adds a distinct classification. It is not a new write/migration policy. `PresetDiag::boot` already holds the read enum; no extra retained counter is necessary.

Required table, driven through the real classifier, catalog and boot emitter:

| Input | Live state at boot | Boot writes | Later mutation |
| --- | --- | --- | --- |
| absent | new compiled defaults | 0 | Existing absent/default coalescing remains; a real change saves v2 |
| canonical v2 | exact stored values | 0 | Existing equality/coalescing and save-before-publish |
| recognized old v1 | new defaults; distinct old-v1 line every boot | 0 | Treat as repair, even if requested values equal defaults: first successful mutation replaces old store |
| any other size/header/canonical failure | defaults; existing invalid line | 0 | Repair may write; never compare corrupt data as an unchanged baseline |
| I/O failure | defaults; existing unreadable line | 0 | Mutations refuse `store`, no writes |
| save fails | prior live bytes/generation | 0 boot; failed save is not a no-physical-write claim | No publish; retry as existing contract |

The absence row's design shorthand “first successful change writes v2” must not eliminate today's honest zero-write re-statement of defaults. Repeated boots over v1 continue diagnosing until replacement; the B335 row's historical “reset once” phrase is stale and should be corrected by the Author on landing, not interpreted as permission to write at boot.

Pins/readers: `test_firmware_ui_presets.cpp:98–114,897–903`; `test_firmware_ui_preset_verbs.cpp:507–519`; `test_firmware_ui_presets.cpp:136–161` preset classifier fixtures, plus `test_device_nv.cpp` shared NV helper fixtures; `tools/probe_board_abi.py` all three type tables. The complete symbol/line list, including literal mutation patterns, is in `symbol-readers.json` / `mutation-census.json`. Keep **17 slots**; update 21/372, text[18], tail[3], 1116 owned-blob bytes, 1144/1132 catalog and 160 reply-buffer assertions by derivation. Do not replace every literal 17: display width and slot count remain 17.

## 4. NVS budget and physical limits (Q3)

All six OLED profiles resolve to a **20,480-B NVS partition**, offset 0x9000, size 0x5000: V3 variants use `default_8MB.csv`, V4 variants `default_16MB.csv`. Effective PlatformIO configuration, actual installed framework path and partition hashes are in `nv-capacity.json`. Installed ESP-IDF headers report 5.3.2.

Measured record sizes: `/mrcfg` 240, identity 80, peers 1160, join profiles 104, team keys 296, fault log 464 (conservative), inbox metadata 28 each; ACCEPT admin identity 40 + ACL 368; CLIENT management identities 368 + targets 2056. This is a **source-sized storage budget, not measured occupancy on the owner's flash**. Historical keys can survive a profile change; system keys and fragmentation are additional.

ESP-IDF documents a binary blob's entry use as two overhead entries plus one per 32 data bytes; 2852 B requires **92 entries**, versus 14 for 372 B. Five 4096-B pages, with 126 entries/page and a page reserved for GC, give a conservative 504-entry working budget. The installed `nvs.h` agrees with the [ESP-IDF 5.3.2 page layout](https://raw.githubusercontent.com/espressif/esp-idf/v5.3.2/components/nvs_flash/src/nvs_page.hpp); the [storage implementation](https://raw.githubusercontent.com/espressif/esp-idf/v5.3.2/components/nvs_flash/src/nvs_storage.cpp) writes a replacement before erasing the prior version.

| Populated record set | Current payload / estimated entries | v2 payload / entries | v2 plus one replacement blob / entries | Entries remaining out of 504 |
| --- | --- | --- | --- | --- |
| ACCEPT | 3180 / 129 | 5660 / 207 | 8512 / 299 | 205 |
| CLIENT | 5196 / 192 | 7676 / 270 | 10528 / 362 | 142 |
| Both endpoint stores retained across profile changes | 5604 / 210 | 8084 / 288 | 10936 / 380 | 124 |

These estimates include two namespaces and known records, not arbitrary fragmentation or Wi-Fi/system keys. A fresh partition holding those records has capacity for the write and overlap. **Actual used/free entries and aged-partition success remain unmeasured**; do not present these figures as a flash occupancy reading. The design's older ~4.7-KB mobile estimate is not a complete current-record census.

Metal plan `docs/2026-09-20-metal-test-plan.md::NV-06` already requires cuts during phrase replacement and complete old-or-new recovery. Extend it to repeated 2852-B replacements on a populated/aged partition, record NVS stats before/after, and try the full power-cut window; no invalid/partial catalog is acceptable. UI-12 needs the ruled response shape and repeated old-v1 boot diagnostic. The nRF file backend removes before writing, but no stock nRF profile has this OLED catalog: do not generalize ESP32 NVS atomicity to LittleFS. No metal-plan edit made in pre-check.

## 5. Whole response, real sink and B475 (Q4)

[reply-measure.json](2026-09-29-standalone-mobile-home-w6-precheck/reply-measure.json) uses the **real** record writer, list/reset verb, catalog, extracted `PresetPrintLines`, `GuardedConsole` and `LineSink`. Scratch changes are limited to the proposed record constants, 244-B reply buffer, `text_max`, and D9 defaults. Generation is the widest u32 decimal value; strings obey the actual grammar.

| Response component | Bytes, including newline |
| --- | --- |
| emergency maximum record | 243 |
| each maximum dm record | 237 |
| each maximum channel record | 242 |
| end record with `text_max:163`, counts 8/8, max generation | 110 |
| **Full list: 17 records + end** | **4185** |
| Emergency-only + same end | 353 |
| Eight DM records + same end | 2006 |
| Eight channel records + same end | **2046** |
| **Actual D9 `reset all` response** | **1508**, 18 complete lines, one save |

The last row corrects the request/B475 arithmetic: `reset all` installs defaults **before** listing, so it cannot return seventeen maximum custom phrases. It still needs sink tests and truthful completion semantics.

`src/console_sink.h::commit` calls `pump()` between complete lines. Thus a responsive host can drain the stage during one handler. With a fake FIFO draining 128/256 B on polls, all 4185 B arrived and there was no drop. With **zero transport capacity**, nine whole lines dropped and the later output included `!! CONSOLE_DROP`; finite 128/256-B capacity without draining also lost lines. This is an executed sink fault proof, not a prediction from byte count alone. No command echo was added to the budget.

**B475 is confirmed.** Never grow `MR_CONSOLE_STAGE_BYTES` (2048, B208), truncate records, omit the terminal silently, or block indefinitely flushing a stalled host.

Options for the Author/owner:

1. **Recommended: bounded, request-driven reads** (per-slot or an explicitly limited page, e.g. at most four records). Define page completion/cursor and generation consistency in the companion contract. Four worst channel records + this end are 1078 B. This bounds each response without persistent emitter state; it does not magically guarantee delivery if unrelated output already fills the stage. Test clean-stage and interference/admission behavior, with the accepted overload policy explicit.
2. **Per-kind list:** today's maximum channel group is 2046 B, only two bytes below the empty stage. It fits that narrowly defined case but has essentially no margin and does not solve already-staged output. A short compact end could increase margin, but is another explicit contract change. Do not approve it merely because 2046 < 2048.
3. **Service-pass streaming:** retain a cursor, captured generation/destination, cancellation and transport/backpressure policy; emit only when the sink can admit the next complete line. This requires more owned state, lifetime rules and measurements than the allocation in §9. Decide whether changes during enumeration restart, refuse or bind a snapshot. A naive “one line each pass” still drops on a persistently full stage.

Changing request grammar or full-list completion is a companion-contract owner ruling. Keep transport overload separate from the catalog mutation verdict: a successful save is not a failed save merely because its response could not be delivered.

BLE: the real `LineSink` delivered all **4185 B / 18 callbacks** exactly, with no whole-response buffer. The backend is nevertheless bounded: the installed Bluefruit configuration has three HVN credits; acquisition waits up to 100 ms, and a failed notify can return zero. `device_ble.h::tx_line` ignores that return. A labelled synthetic test of the verbatim function accepts three writes then returns zero: **717 B retained, 3468 B lost**. This is newly registered **B476**, separate from B292's MTU fix. Healthy concurrent draining can succeed; no claim of loss on actual hardware is made. ESP32 OLED profiles currently have no BLE backend, and nRF BLE profiles have no OLED catalog, so this defect is not a W6 production dispatch blocker. Do not fold a transport retry redesign into W6 without a separate ruling/fence.

## 6. Reply buffer, stack, validator and companion (Q5/Q6)

Four `kPresetLineMax` users are `preset_emit_record`, `preset_emit_list`, `preset_emit_err`, `preset_boot_restore` in `src/firmware_ui_preset_verbs.h:183/193/200/234`. `firmware_commands.cpp::PresetPrintLines` supplies the actual sink adapter. The real maximum writer returns **243**, needing **244 including NUL**; with 160 it returns **0**. Derive the bound from the field spellings, max slot name, u32 generation digits and `kUiPresetTextMax`, and assert it. Do not reinterpret capacity as text bytes.

`stack-measure.json` contains **12 successful real-TU compilations** with `-fstack-usage`, base/v2/short-boot, both board compilers, commands and complete `.su` entries. LTO was disabled for attributable per-function frames. These are compiler frames and call-chain subtotals, **not measured task high-water marks or a complete libc/NVS/interrupt stack bound**.

| Compiler path | Base → all four 244-B buffers | Separately bounded boot buffer |
| --- | --- | --- |
| Actual heltec_mobile Xtensa, `-Os`: emit_record | 240 → 320 | unchanged from v2 |
| Same: emit_list / emit_err | 224 → 304 each | unchanged from v2 |
| Same: simultaneous emit_list + emit_record frames | **464 → 624 (+160)** | same |
| Same: preset boot wrapper | 208 → 288 | **128** with 81-B bound |
| Same: setup frame | 992 → 992 | 992 |
| ARM gateway compiler, forced OLED for pricing only: handler/verb/list/record subtotal | **504 → 752 (+248)** | same |
| Same hypothetical ARM: boot wrapper | 208 → 368 | **224** |
| Same hypothetical ARM: setup frame | 1040 → 1040 | 1040 |

The ARM baseline inlines functions that become out-of-line in the larger variant; do not add their frames twice. Stock gateway is headless, so its actual image has **no OLED phrase command/boot stack**. Likewise no stock OLED+BLE image exists: the forced-OLED ARM compile prices that possible path, not an enabled product profile. On Xtensa the measured command-chain subtotal `mesh_service_once + exec_console_line + dispatch + handle_ui + preset_verb + emit_list + emit_record` is 3472 → 3632 B before lower writer/sink/library frames (loop adds 32). Setup + boot-wrapper alone is 1200 → 1280 B, or 1120 with the short boot buffer, before deeper load/output frames. These are explicitly partial peaks; a final implementation's compiler/inlining and whole task peak must be remeasured.

The request's “two buffers live at once” is not a language-lifetime guarantee: the end-record buffer is declared after the record loop. The measured Xtensa frame still reserves its space across the record call. This is why two × 84 is not the compiled peak delta.

A small boot bound is appropriate: the existing longest invalid diagnostic emits **80 UTF-8 bytes including newline**, requiring 81 with NUL. The proposed old-v1 line is shorter. Derive it using `sizeof` on all actual diagnostic strings (including the existing two leading spaces), and test old/invalid/I/O/quiet states. Prefer this to paying 244 B for a diagnostic. Stack peak remains a final-candidate measurement, not a no-growth promise.

`validate_preset_text` (`firmware_ui_presets.h:329`) accepts non-null 1..T bytes, printable ASCII 0x20..0x7e, excluding quote and backslash, with a non-space. Its canonical-validation and mutation callers must agree at 163/164, including padding and empty disabled slots. `PresetArgs` holds a slice, not a 17-B input copy. The longest canonical setting command (`ui preset set emergency loc=off "<163>"`) is **197 B** and is accepted intact through the actual handler helper. USB's 1023-byte local-line limit and BLE's **274-byte payload / 275 storage** both fit it. Deliberately padded noncanonical command spellings still obey each transport's existing total-line limit.

Companion `INBOX_SYNC_CONTRACT.md::ui preset` (:1215–1310) changes together:

- Record version 2; text range 1..163 / advertised `text_max`, same byte grammar.
- Explain abbreviation plus mandatory full-text review; withdraw the old single-row reason for the storage limit.
- End example/schema: **capacity remains 17 slots**, add `text_max:163`; retain equality-based nonzero generation and counts.
- Overlength `bad_text` uses `text_max` as its limit; empty/all-space/forbidden bytes still give `bad_text`, not a new error name.
- Repeated old-v1 boot diagnostic, zero boot writes, no migration, first replacement semantics.
- The owner-selected B475 request/reply and enumeration-generation contract; preserve busy/location/save-error/coalescing semantics unless explicitly changed.

## 7. Projection, defaults, review and transitions (Q7/Q8/Q11)

**Projection stays 20/161 B.** `ComposeSlot` is text[18], slot, loc; `ComposeList` is eight rows plus n. `compose_project` already sees the full live slot length. At projection, copy all bytes if len ≤ 17; otherwise copy 16 and append Latin-1 0xBB and NUL. No later consumer needs a source-length field. The Author should remove the now-unnecessary “and a length” requirement; this is a representation choice, not a change to ruled display bytes. Neither the formatter nor another sanitization pass should turn the appended `»` into a dot.

The renderer uses `ui_fmt_compose_row` / `draw_compose`, and the sender reads the live catalog, never projected text. W3's `kComposeTextCols` remains **17**. W4a's identity formatter is for the destination label; phrase ASCII validation remains distinct.

D9 preserves emergency `I'm in danger` (loc on), DM1 `Are you OK?`, DM2 `I'm OK`, channel1 `Got your message`, channel2 `All good`. Add DM3 `Where are you?`, channel3 `Return to base now`, channel4 `On my way`, all loc off. Counts become **3 DM / 4 channel**, plus emergency. `Return to base now` is **18 B** and projects to the 17 cells `Return to base n»`; bytes sent/reviewed remain all 18. Canonical v2 custom records retain their own values rather than receiving new defaults.

Current path: `UiModel::compose_gesture` (:5838) handles a text-row double by `queue` (:3907), setting `_req_pending`; `take_send_request` (:3436) drains emergency first then normal and starts the outcome view. `mr_ui_tick` invokes the real executor only after this, with normal/emergency tracker isolation. W6 inserts review **before queueing**, not a second send action after an already-queued request.

Review geometry: row 0 team `TO TEAM <8 hex>` (16 cells); verified DM `TO <label ≤7> <8 hex>` (≤19); unverified `TO T<n> UNVERIFIED` (≤19). Use the full raw name with `ui_fmt_identity` at 7; do not enlarge/reformat a six-cell TEAM label containing `»`. Three body rows of 19 cells; action row SEND/BACK, BACK initially selected, **LOC at zero-based columns 12–14** only when the bound phrase requests it; page token fits 19 cells. LOC does not promise a current fix or suppress the core's existing no-fix refusal.

Implement word wrap as a pure, counted-byte helper following §7.2's exact branch order. W3's `detail_page_rows<19,2>` is byte slicing and must stay unchanged for Inbox; a word-wrapped review is a different presentation over the same payload bytes. My independent spec calculation ran **4,236,928 property checks**, including all 20-cell space/non-space masks plus targeted and random bodies. A 163-B witness yields 17 lines / **6 pages**; concatenated lines equal the input exactly. Evidence: `wrap-measure.py/json`. These checks validate the proposed rule, not nonexistent W6 code.

| Event / path | Required W6 behavior; current behavior to change or retain |
| --- | --- |
| Phrase double | Open bound review, BACK selected, no pending request/core send; today's direct queue and result transition changes |
| Review short / double | Short toggles action only; double BACK returns to that phrase; double SEND queues exactly once via existing path |
| Page deadline | Same `kDetailPageMs` 2000-ms cadence/cycling, no press or send; time-only page advances must not count as input |
| Blank / wake | Keep bound review and same page; reset action to BACK on blank; suspend cadence; wake press consumed, restart cadence |
| `long_arm` | Keep review, reset BACK, retain existing emergency precedence; no normal send |
| `long_fire` before SEND | Close review; after alarm return to phrase list; never create a normal request |
| Team changes | Close review, phrase list with `TEAM CHANGED`; Home if no team remains; check both tick-only and same-tick press |
| Known DM identity changes/disappears | Close review with `RECIPIENT CHANGED`; removed teammate uses existing Team-list gone/pick behavior |
| Generation changes | Close review, list with `PRESET CHANGED`; refresh selection safely; today channel already rereads with a note while DM closes |
| Confirmed request awaits execution | Re-gate generation, team and known peer identity; no stale recipient retargeting |
| Press / emergency after SEND | Keep saved-phrase contract: result closes but pending request remains and follows today's slot order; do not import W8's draft withdrawal |
| Result acknowledgment / refusal | Return to phrase list, no editable phrase draft; existing outcome/tracker semantics remain |
| Reboot | RAM review/request lost; catalog restored by v2 rules |

Existing anchors affected: `on_gesture`, `on_tick`, `compose_gesture`, `preset_catalog_moved`, `close_compose`, `emergency_gesture`, `queue`, `take_send_request`, `unblank`, `refresh_detail_page`, renderer frame-copy dispatch. Preserve grant rows, top-level MENU, body/rail identity, overlay precedence, late ACK handling and FrameGate's Inbox read watermark. Review must not make Inbox appear read or change its two-row/241-B pager.

The model receives only projected rows today. A concrete full-text seam is required: use a request/answer phase in the UI tick (like Inbox open), resolve slot/generation against the live catalog, obtain the raw destination name/hash, and copy exact review bytes before starting a frame. Keep no pointer into a mutable catalog. Freeze header/page/action/LOC in the frame state; do not read live catalog or identity while drawing multiple OLED pages. A read-only capture callback must not mark `_req_pending` or call the executor.

## 8. Send binding and line capacity (Q9)

`src/firmware_ui_send.h::send_gate_of` (:493) currently checks emergency first, then catalog generation, slot validity, enabled, and kind; `ui_compose_send_line` (:534) reads live slot text/loc; `ui_perform_send` (:576) has `char line[kSendLineCap]` (96). W6 adds ordinary team equality and DM known-hash equality **before submission**, with typed notes/refusals. Preserve emergency's first unconditional gate result.

Live team source is `g_node.config().team_id`. Live DM resolution is the existing `Node::team_key_of_id` boolean result plus hash (or its public peer-book adapter), the same authority used by the UI/core. Do not use an ID-indexed static array or a cached six-cell label. `team_member_hash_of` currently uses zero as unknown; the brief must specify whether it keeps that existing convention or carries the resolver's separate known bit. The measured 16-B request can hold a known bit in existing padding; do not silently reject or retarget a hash merely because its numeric value is zero.

A phrase request needs existing kind/peer/slot/generation plus bound **team_id and peer_hash**. It does not need `draft_id`, `dm_text` or `channel_text`; those are W8. Keep a separate review binding from the pending request: after confirmation a view may close while the queued request must survive, including an alarm. Reusing `_req` for a new review would overwrite that owed request.

The design's safe arithmetic is **199 B**: `send_channel ` + 10 promoted unsigned digits + space/quotes + 163 + ` -t -l -e` + NUL. Actual u8 channel 255 needs **192 storage**, and maximum valid DM ID 254 needs 184. The real composer/parser scratch test accepted all six kind/fix combinations with **163 body bytes intact**; old96 returned refusal for each. Emergency with no fix omits `-l` (188 command bytes); with fix, 191; no change to its location policy.

Put one 199-B send line in the OLED firmware owner and pass it to the pure operation, or demonstrate a single ODR instance if keeping it in an inline helper. It must not become one static array per TU/test instantiation or be live through a recursive executor. The resulting mutation/probe must prove both capacity and single owned allocation; moving the existing local array saves source stack but its exact compiled saving depends on inlining. No exact −96 compiled-stack claim is justified before W6 code exists.

## 9. Allocation for owner decision (Q10)

[layout-measure.json](2026-09-29-standalone-mobile-home-w6-precheck/layout-measure.json) compiles supplemental `sizeof`/alignment symbols using all three real toolchains/flag sets. These are **struct-only counterfactuals**, not compiled W6 behavior. Stock ABI pins independently passed first.

| Type | Base host / boards | Proposed shared-page host / boards |
| --- | --- | --- |
| UiPresetSlot / Blob | 21 / 372 on all | 167 / 2852 on all |
| PresetCatalog | 1144 / 1132 | 8584 / 8572 |
| ComposeSlot / ComposeList | 20 / 161 on all | **unchanged** |
| SendReq | 8 on all | **16** on all |
| UiState | 520 on all | **568 / 560** |
| UiSnapshot | 1368 on all | **unchanged** |
| UiModel | 944 / 936 | **1016 / 1000** |
| UiChrome | 20 on all | unchanged |

Measured arrangement: keep Inbox `detail_line[2][20]`'s type through a union with `review_line[3][20]`; retain a 20-B review header, one phase byte and two flags in UiState; reuse the existing model's 242-B body, length, page/page-count and timer while detail/review are mutually exclusive; retain a separate 16-B review binding and grow the existing `_req` by 8 B. A requested/open/note phase can carry the capture handshake; no service pointer or full snapshot/catalog copy is assumed.

`UiState` exists in UiModel and again in `s_frame_state`. Count it **twice**, but do not count the model's copy a third time:

- OLED boards: model +64 + frozen state +40 + static send line 199 = **+303 B** beyond catalog.
- Host: model +72 + frozen state +48 + 199 = **+319 B**.
- Catalog plus this candidate: **+7743 B board objects**, before linked-section alignment/other implementation effects.

Alternative measured layouts: separate review page (reuse body) is **+383 B on all ABIs**; separate page **and** 164-B review body/timer is **+551 B boards / +559 host**. All preserve 20/161-B compose rows and 1368-B snapshot. The shared-page union saves state while keeping Inbox geometry intact; a wholesale change of `kDetailBodyRows` to 3 would violate the design and is not recommended.

**Owner ruling requested:** the chosen non-catalog shape and exact limits. This is not an allocation grant. Additional note payloads, observers, asynchronous reply cursors, pointers or persistent label copies require another measurement; do not absorb them into an approximate allowance. In particular the shared-body lifetime, frozen frame and pending-request coexistence must be proved by tests, not justified by size alone. Headless gateway compiles these types for the ABI probe but does not instantiate the OLED catalog/model; predict its linked image unchanged if the fence is respected.

## 10. Reader ledger, tests, probes and proposed fence (Q12)

Machine-readable ledgers: `symbol-readers.json` (**911 occurrences / 18 files**) covers every named requested symbol in src/test/tools; `source-readers.json` covers **374 tool reader lines** for the affected headers/TUs; `native-cases.json` lists **395 candidate native cases** with their line spans. These are discovery inventories, not a claim all cases must change. `mutation-census.json` preserves each selected entry's literal old/replacement strings, source and match count. No mutation module was imported/executed to obtain the census.

Direct test inputs include `test_device_nv.cpp`, `test_firmware_ui_presets.cpp`, `test_firmware_ui_preset_verbs.cpp`, `test_firmware_ui_model.cpp`, `test_firmware_ui_send.cpp`, and **`test_firmware_ui_chrome.cpp:850–882`** (its real outcome/rail test currently sends on one phrase double). Existing ABI/native pins explicitly cover slot/blob/catalog, ComposeSlot offsets 0/18/19, ComposeList, snapshot, state/model and SendReq. Keep layout/offset assertions derived, not relaxed. Defaults, row positions, direct-double sends, generation races, location refusal and emergency priority are behavioral expectations requiring a closed change ledger.

**Selector (a) is nine batteries, not five:**

| Battery | Base entries |
| --- | --- |
| model | 238 |
| devicenv | 42 |
| uisend | 15 |
| uipresets | 32 |
| uipresetverbs | 19 |
| sliceCbudget | 1 |
| sliceCsend | 1 |
| w4aident | 9 |
| w4bhome | 26 |
| **Total** | **383** |

All **383 literal anchors match exactly once** at this base (static measurement, not RED results). The new state can affect budget/Home/identity batteries even if their predicates are intended unchanged. Sender/generation/queue/review and blob/default/validation mutations must retain their mutant meaning; changing a limit or moving a statement is not grounds to retire a safety property. Add controls for old-v1 recognition/no writes, 163/164 limits, whole output, full-text capture, BACK default, no-send paging, team/hash/generation races and pending-phrase/alarm coexistence.

Recommended initial selector (b): **chrome (48), uiteam (20), uiinvite (32), consoleline (12)** — overlays/frame freezing, label/source paths, DM grant/action rows, and command bounds: 112 entries, **495 total with (a)** before W6 additions. The broader candidate ledger also lists uistatus19/uiprov45/uijoin26/provservice10/config32/teamkeyring68. Include them if the chosen implementation touches their shared transitions or field carriers; otherwise explicitly exclude unchanged services rather than quietly treating them as (a). The Author must name the final union after choosing the exact fence. Neither this pre-check nor the brief can inherit historical RED totals across edits.

Probe obligations and existing readers:

- **Firmware-UI:** P26 real boot/busy adapter, P27 catalog projection and race/execution paths, P29b 17-column rows, P28/P30 identity renders on result/reply paths. P29a's 241-byte Inbox pages must stay two-row byte-identical. C134–C141 and other preset/generation controls, W3 row/page controls and W4a identity controls require individual audit when their anchors/fixtures change. Existing direct-double gesture fixtures must enter review and deliberately confirm; do not merely lower counts or redirect controls to unrelated failures. Add real review page/header/LOC/frozen-frame controls and exact long send lines.
- **Board-UI:** W49 dispatch placement, W50 boot restore, W51 shared BLE seam/flush, W54 command-TU `/mrui` guards, W54-help. Default run with controls is now the gate, **592 identities**, not the retired B418 known-failure exception. Re-anchor through actual symbols if a fenced edit moves text; preserve each property and account any explicit identity-set change.
- **Console-sink:** compiles the real stage/LineSink and help matrix, but its present help proof does **not** execute a long preset list through the OLED router. Add a bounded-output proof using the real writer/adapter/sink, including stalled and draining host, and the final owner-approved request shape. Synthetic “sum lengths” alone is insufficient.
- **Inbox-verbs:** compiles the real command TU, but both current arms have OLED off, so **no `ui preset` path is exercised**. It already documents the `ui_emergency_active` seam. An explicitly OLED-enabled focused router arm is feasible and should prove long set/list/error/end behavior through `dispatch`/`exec_console_line`, with a labelled emergency stub. Fence its files if selected. The UI probe substitutes the catalog seam and does not compile `firmware_commands.cpp`; it is not a substitute for this command wiring proof.
- **Stock ABI:** all existing requested types are covered; add supplemental new enum/view/request type measurements if introduced. Repin exact state/catalog/request values on all three ABIs only after owner allocation.
- **Other readers:** tools discovery's strict count readers, warning census's six OLED builds, command inventory, ownership/feature scanner and probes compiling `device_nv.h` remain in the P7 input audit. This is not permission to run only files containing a literal symbol; the source-reader ledger includes transitive/include and structural readers.

Suggested production fence: `src/device_nv.h`, `src/firmware_ui_presets.h`, `src/firmware_ui_preset_verbs.h`, `src/firmware_ui_model.h`, `src/firmware_ui_send.h`, `src/firmware_ui.cpp`, and `src/firmware_commands.cpp` for the command/boot adapter and routed proof. Keep helper code in the existing pure headers where cohesive (U1); a new header/TU must be named before dispatch. `fw_main.cpp` is **not presumed necessary** for synchronous bounded replies; service-pass streaming would expand that fence and add state.

Test/tool/document fence: the six native test files above; affected probe source/runners and new regression files explicitly chosen by the Author; mutation harness; `probe_board_abi.py`; the generated command inventory if command-TU line anchors move; companion contract; receipt/evidence. The current inventory is 197 rows: grammar extensions may add subforms but must not silently change transport/authority classification. QA's later landing owns the register/design/status and metal-plan update. No lib/, wire, simulator, `/mrcfg`, board partition or platformio edit is justified by the measured requirements.

## 11. Packaging, decisions and next gate (Q13)

Recommend **one coherent W6 feature package** after the two rulings. The capacity, projection, mandatory review and send-buffer/binding behavior share one product surface. A catalog-first release would accept 163-byte phrases while the current panel clips, directly sends on double, and refuses the composed line at 96 B. A panel-first mandatory-review subset could be made safe with 17-byte storage, but still needs most of the binding/state/instrument work and another full surface gate. No C1 extraction is required merely to add these feature seams. If the Author chooses a split, specify and gate the complete intermediate contract; never expose enlarged phrases without full review and send support.

Before dispatch, the Author should:

1. Obtain the **B475 reply/companion ruling**, including behavior with a full stage and generation changes during enumeration.
2. Obtain the **non-catalog allocation ruling** from §9; incorporate any reply-emitter state separately if selected. Catalog +7440 is already within D7.
3. Clarify five read classifications vs the design's old four-state wording, zero-growth row projection, exact boot-line indentation, and the review capture/return-note/binding semantics. These are author contract completions, not reopened product rulings.
4. Name the full fence and expectation/control ledger, including B475's correction that reset-all returns defaults. B476 stays separate unless the owner dispatches it.
5. Require no W7/W8 editor/draft/new send kinds and no Home/inbox-order feature expansion.

Coder gate: source-validation and frozen inputs; native binary; freshly rebuilt anchored 36-stream comparison; stock plus supplemental ABI; all affected real-TU probes (including **board-UI default**), the approved mutation union with live match/RED accounting, tools discovery after tool changes, current command/authority/ownership checkers, six-profile warning census, and stock `.pio-measure` board pair with before/after attribution. Static catalog/state growth does not waive stack/flash measurement. QA's independent gate follows P6: native/corpus/boards, selector-(a) batteries and affected probes, plus required new instrument regressions and exact preservation proofs. Never substitute this pre-check's runs for either gate.

Metal residue: update UI-12 for the ruled response and eight defaults, add full review/LOC/abbreviation/page/wake/alarm checks with a maximum phrase, and extend NV-06 to v2 replacement, populated-partition capacity and repeated v1 boots. Those checks remain OWED after software PASS. This pre-check did not close B335 or B475 and did not approve an implementation.

## 12. Evidence closure

Evidence folder: [2026-09-29-standalone-mobile-home-w6-precheck/](2026-09-29-standalone-mobile-home-w6-precheck/). Its `SHA256SUMS` seals this receipt and retained measurements/manifests; `raw-logs.txt` seals locally retained ignored artifacts. `baseline-summary.json` and `preservation.json` provide compact machine-readable conclusions. Measurement scripts disclose each scratch substitution and compiler command. Absolute temporary paths identify runs; no reliance on HEAD-only snapshots or untracked work being absent.

Final comparison: **1977/1977 MeshRoute input paths retained**, only the allowed B476 register addition differs; **285/285 simulator paths unchanged**, simulator clean at its pinned hash, both HEADs unchanged, index empty. The checksummed evidence is ready for the Author; B335/B475 remain open and no W6 production change is authorized.
