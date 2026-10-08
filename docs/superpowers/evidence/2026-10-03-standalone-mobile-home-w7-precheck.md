# Standalone Home W7 — independent pre-check ledger

Date: 2026-10-03. Role: Codex QA; Claude authors the brief; a separate coder implements. **Pre-check complete, ready for authoring after the allocation/order decisions below. This is not a brief PASS or implementation authorization.** Reviewed input: standalone Home design r2.24. No production, test, tool, design, register, tracker, MEMORY or simulator edit. No staging, commit, reset, clean or removal. Only this report and its checksummed evidence directory were added.

The source supports a separate W7 name feature. W0's rename transaction is already implemented; W6's frozen page storage can hold the editor window and name review without another text array. The two measured proposals are **+48 B board objects / +56 B host objects** for a 32-byte name draft, or **+192 B / +200 B** for a 163-byte shared draft including W8's ID/lock. Neither is linked RAM or an approved allocation. The requested baseline instruments passed independently.

## 1. Base and preservation — Q1

MeshRoute HEAD: `4c1a000bc71706ac438e64161a9452966a66615f`, parent `10f3332`, owner subject `bugs`. Simulator HEAD: `6585649ea5a780f0542b2931853a667be56a5b2b`, clean. No staged paths.

The [entry inventory](2026-10-03-standalone-mobile-home-w7-precheck/inputs.json) hashes **2,468 MeshRoute files and 285 simulator files**, including untracked evidence. [Preflight](2026-10-03-standalone-mobile-home-w7-precheck/preflight.json) reconciles every file in the preceding QA entry inventory against its explicit four-document landing. The only added paths are that QA receipt and its evidence. Its **40-entry** seal and the coder return's **29-entry** seal verify; the coder's outer report is independently present in the entry inventory. Nothing relies on the coder's count of 28.

Inherited dirty tracked paths, all accounted for:

- `tools/probe_ui_model_mutations.py`: bounded F07 replacement and its comment, final hash `27807b320427fb090840e927bf559bfff909f78761d98b29e3aff8b3cb4d177d`.
- Design, register, tracker and MEMORY: the previous independent PASS landing, matching all four `final_status_hashes` in `qa-landing.json`.

Inherited untracked paths: the tool-package return report/directory and QA re-gate report/directory. They are permitted preparation inputs, not files to replace. The tool-package five final hashes match the independent gate through the full inventory. No commit is required. A later owner commit must be reconciled by blob content, with the new hash recorded, before measuring.

For the author's pins, use this commit plus the complete SHA-256 inventory, the design hash, this receipt/seal, and the register/tracker/MEMORY hashes. New brief/status documents are a separately declared preparation delta. Production fingerprints cannot be silently refreshed.

## 2. Existing seams and the editor's home — Q2

Recommended new pure header: `src/firmware_ui_editor.h`. Own the repertoire/ring tables, counted draft operations, pure transition result and ring/header formatting there. Include only standard pure dependencies and `firmware_ui_input.h` if Gesture is used. **Do not include `firmware_ui_model.h` or device `firmware_config.h` from it**: the first creates an include cycle, the second imports Arduino. Model integration, origins, page projection, dirty/blank/overlay handling and requests remain in `UiModel`. Firmware integration and drawing remain in `firmware_ui.cpp`. No new production TU, source-list change or simulator edit is needed.

Reuse, by symbol:

| Existing authority | Current source | W7 use and preserved boundary |
| --- | --- | --- |
| `Gesture`, `InputFsm` | `firmware_ui_input.h:17,29` | Consume classified gestures; keep debounce 25 ms, double gap 350, arm 800, fire 3500. Production already polls it in `mr_ui_tick:2626`. |
| `ui_display_byte`, `ui_fmt_identity` | `firmware_ui_model.h:1470,1495` | Old-name display/WAS sanitization and abbreviation. Draft bytes are already repertoire ASCII; never sanitize a generated marker again. |
| `detail_page_count`, `detail_page_rows` | `model.h:1440,1451` | Counted fixed-byte projection, no strlen. For a sliding adjacent-row window, two one-row slices at successive row indices reuse the existing helper without moving it. |
| `ReviewPhase`, `on_review_captured`, `review_action_line` | `model.h:2666,3830,2694` | Existing saved-phrase lifecycle and safe BACK selection. Reuse the visual review's page geometry/storage; do not send a name through its preset binding or copy the draft into `_detail_body`. |
| `draw_review` | `firmware_ui.cpp:2388` | Extend the shared review renderer for the name's SAVE/EDIT actions, no LOC and no page token. Phrase renders and their request semantics stay unchanged. |
| `ui_nav_slot`, `ui_chrome` | `chrome.h:325,558` | Name editing/review/result box STATUS; emergency suppresses the rail first. Explicitly cover any new HomeView arm. |
| `FrameGate::step/on_page` | `model.h:6381,6422` | Freeze before page replay; retain dirty while dark; keep the complete-visible-Inbox read watermark rule. |
| `ui_allows_sleep` | `model.h:6474` | A blanked editor may sleep once input is inactive and no frame is open. Retaining a RAM draft must not hold the CPU awake forever. |

The installed small font is `u8g2_font_6x10_tf` (`board_ui.cpp:306`), drawn with `drawStr` (:309); no high-byte filter is introduced. A fresh [font measurement](2026-10-03-standalone-mobile-home-w7-precheck/font-measure.json) finds each of the **42 repertoire bytes plus `_`** present with a six-pixel advance. The fixed order is `ABCDEF / GHIJKL / MNOPQR / STUVWX / YZ0123 / 456789 / SPACE.,?!-`. The editor's group ring has 8 items, each character ring 7, and the control ring 6. Group/character/control worst widths **14/17/18** fit 19 cells. SPACE remains an actual space in the draft, `_` only in the ring, and `ADD SPACE` is nine cells; `BACK TO GROUPS` is 14.

The body starts at x=12; baselines are y=19/29/39/49/59, stride 10 (`ui.cpp:1035–1074`). Draft rows occupy body rows 1–2. A proposed underline is x=`12 + 6*cursor_col`, y=the visible draft row's baseline+1, width 6, height 1 via the existing `draw_hline`; even column 18 ends inside the body. Freeze the row/column with the visible bytes, never derive them from the live draft during later OLED pages. Physical visibility is metal-only.

## 3. Draft storage and measured proposals — Q3

[Measurement source/results](2026-10-03-standalone-mobile-home-w7-precheck/layout-measure.json) compile **struct-only scratch copies** with `probe_board_abi.py::measure`, using each env's real PlatformIO flags/toolchain and its matching nm. No editor implementation exists in those copies. Current sizes independently reproduce:

| Type | Native | heltec_mobile | gateway ABI |
| --- | ---: | ---: | ---: |
| UiState | 568 | 560 | 560 |
| UiModel | 1016 | 1000 | 1000 |
| UiSnapshot | 1368 | 1368 | 1368 |
| UiChrome | 20 | 20 | 20 |
| HomeCapture | 9 | 9 | 9 |
| HomeView / SetupOrigin / ReviewPhase | 1 / 1 / 1 | 1 / 1 / 1 | 1 / 1 / 1 |
| SendReq | 16 | 16 | 16 |
| InputFsm | 28 | 28 | 28 |

Priced shape, with its fields explicit so the author can bind the grant:

- A single counted `Draft` in UiModel: bytes[32] or bytes[163], len, cursor, caller and caller cap. The 163 shape additionally contains u32 draft_id and bool locked. **No terminating byte is required in the owned draft**, because all operations and the service use counted lengths. This is not license for a C-string read at full capacity.
- A one-byte typed NameOrigin in UiModel, including an inactive value.
- A 10-byte frozen editor descriptor in UiState: phase, group, ring item, used length, cap, visible cursor row/column, note, panel result, and primary-action selection. Typed enums have u8 underlying types. Request states (requested/taken/result) can occupy the phase; there is no separately owned request object in this measured shape.
- Alias two 20-byte editor rows into the existing 60-byte review/detail union. Keep old WAS display in the existing 20-byte review_header during the name flow. Ring strings and header strings are generated from frozen indices/length, not stored twice. No own-name copy in another snapshot, no full draft in the doubled state, no full draft stack copy.

| Proposal | Draft size/alignment | UiState N/M/G | UiModel N/M/G | Delta of UiModel + separate frozen UiState, N/M/G |
| --- | --- | --- | --- | --- |
| name32 | 36 / 1 | 576 / 568 / 568 | 1064 / 1040 / 1040 | **+56 / +48 / +48 B** |
| shared163 + ID/lock | 176 / 4 | 576 / 568 / 568 | 1208 / 1184 / 1184 | **+200 / +192 / +192 B** |

The editor descriptor is 10/1, NameOrigin 1/1, unchanged UiState/UiModel alignment 8; UiSnapshot and UiChrome do not grow. Existing padding explains why descriptor bytes do not equal the aggregate delta. `s_model` and `s_frame_state` are separate statics (`ui.cpp:191,390`); count the embedded state once inside UiModel, then add the frozen state once. This is **object pricing**, not linked-section or flash measurement. Stock `gateway` does not compile the OLED TU; its hypothetical measured type growth is not resident RAM. OLED gateway variants use the mobile's Xtensa layout. The ruled board pair remains gateway + heltec_mobile.

**Owner allocation requested:** approve one measured shape (or ask for another measurement). Recommendation: W7 alone with the name32 interim shape, then remeasure the widening/ID/lock with W8. That requires the author to state the interim exception to design §5.6's final 163-byte shape explicitly. If the owner wants the final shared draft allocated now, approve +192 board/+200 host without authorizing any W8 send behavior. Any retained field outside the selected measured shape is STOP-1 for measurement. A pointer/copy request carrier retained in the model, a separate old-name array, or a second editor text page is not included.

Preload reads counted `UiSnapshot::own_name/own_name_len` (`model.h:2232`) published from `g_node.effective_name`. Test each repertoire byte, empty/all-spaces, lowercase, quote/backslash, high bytes and UTF-8. Preload **only if every byte is representable**; unsupported text opens empty, with old identity still available for WAS. Do not uppercase or retain only a prefix.

SAVE NAME? can fit without paging: row 0 `SAVE NAME?`; rows 1–2 the full draft (19 + at most 13 bytes); row 3 `WAS ` plus a 15-cell old label; row 4 ` SAVE >EDIT`, safe action first. For an unnamed original, propose `WAS NO NAME SET`. These are author choices to freeze, not implementation facts. Reuse the counted pager at 19 cells; no word-wrap is required for this two-row full-name display.

Recommended request seam: a primary SAVE changes the name phase to requested; `take_name_request` marks it taken once and exposes a counted read-only view of the model-owned bytes. A `ui_service_name_request` in the tick calls the **real `mrfw::rename_node` exactly once**, then feeds a typed panel answer back. No SendReq/SendKind, normal/emergency send slot, console string, crypto install or separate record conversion is involved. The request cannot outlive or refer to a released draft. Require a stale-answer/duplicate-drain test and no save from redraw, wake, cancellation or acknowledgement. Place the bounded service outside the normal radio-send busy gate: a pending DM must not prevent a name save. Keep emergency send priority and all existing drain call counts.

`RenameResult` is in the device header (`firmware_config.h:235`); the pure editor can own panel result categories, mapped exhaustively in the firmware TU without importing Arduino into the model. Map saved and unchanged to `NAME SAVED`; nv_save_failed to `NAME NOT SAVED` / `NV WRITE FAILED`; propose too_long → `NAME NOT SAVED` / `NAME TOO LONG`, bad_args → `NAME NOT SAVED` / `BAD NAME`. The last two are unreachable for a valid nonempty/non-all-space <=32-byte draft, but scripted answers must prove fail-loud refusal and retained draft. They must not be mislabeled as NV failures. W0 already proves durable-before-live and zero-write unchanged; its implementation stays read-only.

A console rename while editor/review is open must not alter the draft, its cursor or the bytes being saved. Recommend capturing WAS at editor opening, retaining it through review/failure, and letting W0 gather the current live seed/position at the eventual save. Do not invent a new name-conflict transaction. The author must freeze whether WAS means opener-name or current review-time name, and test that exact race. A saved result is never rolled back by an alarm.

## 4. Name prompt and typed returns — Q5

Current Home JOIN/CREATE performs `provision_admit` at activation before `enter_setup_from_home` (`model.h:4364–4380`). The latter sets SetupOrigin::home, Screen::settings and interactive focus (:4396). `settings_follow_screen` resets provisioning and `_setup_origin` whenever the screen leaves SETTINGS (:4579–4595). Therefore **a name origin cannot be inferred from or solely stored in SetupOrigin**: the name editor boxes STATUS and the generic Settings cleanup legitimately retires that setup context.

Recommend a Home identity sub-view for the prompt, with `NameOrigin {none,my_device,setup_join,setup_create}` preserved independently until the name flow finishes. No provisioning sub-view should be half-open behind it. Admit first, then show the prompt only for an empty counted own name. After naming, re-admit and call the existing setup entry, reconstructing its normal typed home origin from NameOrigin. Preserve `_home_return` for JOIN or CREATE. A new HomeView arm requires updates to all exhaustive HomeView switches in model, renderer and chrome; do not retain a dead enum helper solely for mutants.

**Author must settle prompt placement/rail:** design §6.5's JOIN/CREATE row currently labels the combined gate/prompt/setup flow SETTINGS, whereas the name editor explicitly boxes STATUS (§5.4). Recommended prompt is STATUS-owned identity before actual SETTINGS provisioning; if chosen, qualify the design table. A SETTINGS prompt is also feasible but needs explicit transitions into/out of the STATUS name body. Do not weaken the rule that leaving SETTINGS closes provisioning. This is a requested author detail, not a newly granted allocation or owner-policy change.

| Path | Required destination/effect |
| --- | --- |
| Initial admission refuses | Existing frozen SETTINGS-slot note; no name prompt and zero settings save/apply. Either press → Home opener. |
| Named Home JOIN/CREATE | Existing direct NEARBY/create confirmation path, no prompt. |
| Unnamed, SKIP default | Explicit double continues the chosen existing setup; no rename. Author should state whether SKIP rechecks the gate if console settings changed while the prompt was open. Rechecking is recommended; the entry must not admit stale clean settings. |
| SET NAME | Open counted name editor, typed setup_join/setup_create origin. |
| Unsaved DISCARD | Return to the same prompt, SKIP safe default; leave durable/live name untouched. |
| NAME SAVED acknowledged | Release draft; ask provision_admit again. Success → original JOIN or CREATE step. Refusal → existing frozen setup-block note, then Home. Never auto-save configuration. |
| NAME NOT SAVED acknowledged | Editor with identical draft, no automatic retry. |
| Review long_fire | Close review; after alarm return to editor with draft for fresh review. |
| Saved result long_fire | Release draft; My device for its origin, Home for either setup origin. No setup resumption after alarm. |
| Not-saved result long_fire | After alarm editor with draft retained. |
| Prompt long_fire | Author must specify destination; recommend Home, retiring pending setup intent, with no automatic continuation. |

The exact SKIP re-admission and prompt-alarm destinations are underspecified in the current text; settle them in the brief/design. The saved-name re-admission and alarm result returns are already specified (§7.4.1), not reopened.

Gateways' `team_build=false` selects HomeProfile::no_plane and its list contains no JOIN/CREATE (`home_profile/home_items`); My device remains available. Probe the l2/no-team arm's rename and absence of prompt. No Settings-origin prompt is authorized: W7's prompt is only Home JOIN/CREATE; INVITE remains ungated and has no naming step.

## 5. Interruptions and current priority boundaries — Q6

`on_gesture` handles long gestures first, then consumes a waking press, then absorbs short/double while emergency owns the body (:3046,3049,3072,3091). Ordinary modal dispatch follows (:3096–3124). The name view must own its gestures before underlying Home/Settings/list activation, while remaining below these safety guards.

`on_tick` advances phrase/detail pages only while lit and before blank is due (:3232), syncs Settings/Home, and blanks at :3295. Blank resets the phrase review to BACK (:3297), cancels unfinished key grants/saved-key offers as before, and calls ConfigService::on_blank (:3331). W7 adds editor retention and resets name review to EDIT without changing those existing cancellations. Do not let the old Settings field-value editor's long-arm cancellation apply to the new name editor.

`emergency_gesture` currently closes detail at :6254, provisioning at :6280, the Settings field editor at :6282, resets phrase review selection at :6281, and closes phrase review/compose only on committed long_fire (:6301). Join these boundaries explicitly: name E1–E4 retained across arm/cancel/fire; name review kept/reset-to-EDIT on arm, closed-to-editor on fire; name result dispatched by its typed origin and success/failure; prompt handled by the author-selected rule. Saved phrase review still returns to its list and its confirmed request still waits in existing slot order.

`on_msg_wake` (:3544) changes the wake deadline and, if dark, unblanks without navigation. `ui_route_recv_push` owns arrival counters/accepted reply classification; a plain receive is not editor navigation. Freeze/read-watermark semantics stay unchanged. `FrameGate` sees an editor on STATUS, so it must never credit an Inbox read merely because review shares the row array.

Native matrix: E1/E2/E3/E4, name review (both selected actions), name result (each answer) and prompt (both selections) × blank/wake/receive/arm/cancel/fire. Assert draft bytes, cursor, ring, phase, origin, request count and safe review selection; overlay short/double must touch no underlying item. Retention of E4's selection follows §5.5's editor rule; do not silently apply key-offer cancellation to draft discard confirmation. Cover millis wrap, same-tick blank/page boundary, and sleep after inactivity. A save during a bounded synchronous service call completes before the next classified gesture; the following alarm owns the panel, with no second save.

## 6. Rendering, real wiring and affected instruments — Q7

Firmware-UI compiles the real UI TU but **not firmware_config.cpp**. Existing device config accessors are fake seams (`probe_main.cpp:712`); the executor fake records calls/bytes (:715). Add a signature-correct `mrfw::rename_node` recorded fake in this probe only: scripted five results, exact counted bytes/length, call count, and live-name publication on saved/unchanged only. Name that substitution honestly. W0's inbox identity arms are the independent real-config/NV proof; do not claim this fake repeats it. A render check must reach SAVE by raw samples through `tick/short_press/double_press` (:1185–1250), not by directly invoking the fake or only mutating UiState.

Require exact panel records for all ring selections, wrap, FULL/EMPTY, discard confirmation, both full-name rows, WAS, safe SAVE/EDIT review, all result reasons and both prompt choices. Include unsupported preload, 32-byte name, console rename during a frame and during an open review, no-save cancellation and retry only after fresh explicit review. Compare every OLED page of one frame against frozen visible bytes while the live draft/name changes; never read live model bytes while drawing.

**Necessary instrument fence:** `probe_main.cpp::draw_hline(int x, int y, int width)` currently increments only a counter (:629); unlike draw_text/bitmap/rect, it records no geometry. Record x/y/width and the primitive's fixed height of one in the existing Canvas path before claiming a 6×1 cursor proof. Controls must omit/move/widen the underline and fail a geometry check, without collateral canvas overflow. This is an instrument gap for the new behavior, not a newly registered existing product defect.

Board-UI reads relevant UI TU invariants via S2/S3/S4/S5 and W1–W8, W27/28, W38–W43 and W53. Preserve frozen draw-frame arguments, one FrameGate, no UI-owned dirty clear/read-watermark mutation, current emergency/normal request drains, body origin/width and exhaustive rail-slot layout. New rename servicing must not duplicate `take_send_request` or fall inside the normal tracker-idle gate. **No existing board-UI identity needs to be removed or added for this feature** if these anchors remain; expected.tsv remains 592 identities. The default run must pass. If a new wiring check is proposed, fence run.sh and expected.tsv together and explicitly account the identity/control delta; current pre-check does not authorize an unmentioned manifest edit.

W0's real inbox identity arms, five inbox runner arms, shared Preferences fake, transcript builder/comparator and deferred-actions sources are unaffected: none consumes the UI model/new editor, and rename_node/service signatures remain unchanged. A changed shared fake/builder/production config seam is a STOP, not an incidental probe repair.

## 7. Readers, tests, mutation selectors and fence — Q8

The [symbol-reader ledger](2026-10-03-standalone-mobile-home-w7-precheck/symbol-readers.json) retains **974 exact source lines** across lib/src/test/tools; [case ledger](2026-10-03-standalone-mobile-home-w7-precheck/native-cases.json) retains 309 candidate native cases. These are a census, not claims that every comment is an executable reader or that every case was run separately.

HomeView readers: model, UI renderer, chrome, native model/chrome/send/team cases, ABI tool commentary, and model battery. SetupOrigin readers: model, its native cases, and model battery. ReviewPhase readers: model, UI renderer, send helper, native model/chrome/send cases, ABI tool commentary and model battery. If ReviewPhase is extended rather than keeping a separate name editor phase, **firmware_ui_send.h and test_firmware_ui_send.cpp join the fence**, and every phrase-only equality/switch must be classified. Reusing page storage does not require reusing the phrase request lifecycle; recommendation keeps it unchanged.

Specific obligations:

- My device's old `>BACK` render (:ui.cpp1452) and model's short-no-op/double-return (:4324–4326), W4b H24, plus the existing probe My-device exact-row expectation must change deliberately to the two-action ` CHANGE NAME >BACK`, BACK first. H24 should retain its no-hidden-leave meaning with a corresponding replacement check; do not delete it by range.
- New HomeView prompt arm needs the model/renderer/chrome exhaustive switch cases. Keep Screen unchanged. SetupOrigin remains the actual provisioning origin; NameOrigin is separate.
- Pager/review union tests (`test_firmware_ui_model.cpp:12465`) retain two detail rows and three phrase rows. Add the editor alias and frozen-window size/offset cases; do not widen Inbox detail.
- InputFsm and Gesture remain read-only; existing input suite runs in native. Add new editor tests in `test/test_firmware_ui_editor.cpp`, integration in model tests, and chrome tests for STATUS ownership and emergency suppression.
- Tool readers: stock ABI pins, firmware-UI runner/control literals, board-UI structural/wiring predicates, mutation AST/source readers (`test_mutation_unusable_reason.py`, `test_worker_formula_derived.py`), warning census and feature ownership's whole-tree scan. No console/device-radio/prov-TX predicate depends on these UI semantics. Audit whole-tree scanners as well as filename mentions.

[Mutation census](2026-10-03-standalone-mobile-home-w7-precheck/mutation-census.json) was AST-read without importing the executable harness. All **348 existing selector-(a)** entry patterns match exactly once at entry:

| Selector (a): proposed changed production files | Existing entries |
| --- | ---: |
| model → firmware_ui_model.h | 264 |
| sliceCbudget → firmware_ui_model.h | 1 |
| w4aident → firmware_ui_model.h | 9 |
| w4bhome → firmware_ui_model.h | 26 |
| chrome → firmware_ui_chrome.h | 48 |
| uieditor → new firmware_ui_editor.h | new, derive at freeze |

No battery targets firmware_ui.cpp; real-renderer controls supply its mutation proof. Recommended selector (b): uistatus 19, uiteam 20, uiinvite 32, uiprov 45, uijoin 26, uipresets 39, uisend 26, config 32. Existing union is **13 batteries / 587 entries**, before new editor/model entries. The wider surveyed candidate union is 721; exclude devicenv 56, teamkeyring 68 and provservice 10 with the stated reason that W0's record transaction, key/grant store and provisioning service stay unchanged. If their source/contract changes, rederive the selection/fence, not merely the total.

Literal anchor candidates in moved/adjacent model boundaries: M04, M16/M17, M27/M28, M35–M37, M39/M48, M63/M66/M84, M94/M95, S03/S05/S07, N04/V34, W6-M01/M04–M09/M17–M19/M22; Home H06–H11/H13–H17/H19/H20/H23/H24. Their exact patterns, positions and replacements are in the census. M36/M37 still target the **Settings field editor**, not the new text editor. Preserve that property. S03/W6-M08/M09 need phrase close semantics preserved when a name branch is added. H16's settings gate must now occur before the prompt; H20 must still retire provisioning origin on a genuine Settings leave. Re-anchor only when the old statement actually moves, prove one match, compile and fail its old property. The appendix includes other affected model/chrome anchors for final diff classification.

Renderer control candidates: C0 stays required compiler refusal; C1–C11 page/MAC gate and C35 etc. retain their actual contracts; W3 controls retain detail/blocked-note/projection proofs; W4b-N5/W1–W3 retain full-name publication, frozen pages and invalidation; W6-D1/D2 retain review-does-not-send and capture-served proofs; B241/W4a/N/O label controls retain their labels. New name cases must not quietly weaken their assertions. The [control-candidate list](2026-10-03-standalone-mobile-home-w7-precheck/probe-control-candidates.json) is for explicit disposition against the implementation diff, not permission to retire a range.

New uieditor entries should attack fixed repertoire/order/uniqueness, each ring bound/wrap, insertion returning to group 1, E2 BACK preserving its group, E3 BACK returning to group 1, counted insertion at cursor, DEL-before-cursor, cursor clamp, FULL without write, EMPTY/all-space DONE, empty/nonempty discard, safe discard action and preload all-or-nothing. Name origin, gate ordering, request once-only, alarm returns and result semantics are model-battery entries; service invocation and frozen geometry are renderer controls. Every control must remain **memory-safe**: change a bounded choice or state transition, not a copy/shift bound that can write outside the draft. Canary/sentinel tests cover capacity 0/exact/one-short/roomy destinations and both ends of all draft operations. Credit no crash/build failure/vacuous control as RED (C0 is its named exception).

Recommended initial fence:

- production: new `src/firmware_ui_editor.h`, `src/firmware_ui_model.h`, `src/firmware_ui.cpp`, `src/firmware_ui_chrome.h`;
- tests: new `test/test_firmware_ui_editor.cpp`, `test/test_firmware_ui_model.cpp` and `test/test_firmware_ui_chrome.cpp`;
- tools: firmware-UI probe_main.cpp/run.sh, mutation harness for uieditor/new integration controls/pin derivation, stock ABI tool's changed rows, plus an extra-pins manifest for new draft/editor/origin types;
- receipt/evidence, permitted preparation inventory, and final QA metal/status landing.

`firmware_ui_status.h`, `firmware_ui_send.h`, config/NV/service files, board canvas/fonts, shared fakes and simulator are read-only by this minimal shape. Add a dependent test to the fence only if its existing expectation is actually affected, naming it before dispatch. No blanket authorization for unrelated cleanup or a sanitizer/pager move.

## 8. Fresh baseline and proposed gates — Q9

[Run receipt](2026-10-03-standalone-mobile-home-w7-precheck/runs.json), [summary](2026-10-03-standalone-mobile-home-w7-precheck/baseline-summary.json), [raw hashes](2026-10-03-standalone-mobile-home-w7-precheck/raw-files.json):

| Instrument, independently run | Result |
| --- | --- |
| pio native + directly executed binary | **3059 cases / 199638 assertions / 0 failed / 0 skipped** |
| Stock ABI | **290 checks / 9 controls RED / 0 unusable** |
| Supplemental current-layout and two candidate shapes | All three real ABI toolchains; figures in §3 |
| Firmware-UI default | **591 / 1059 / 591 checks**, 240 controls accepted exactly once, zero unusable/guard failures |
| Independent firmware-UI control census | All 240 declared labels equal all 240 terminal observations, including C0's different contract |
| Board-UI default | **592 identities**; V3 124, V4 110; 14 traits, 12 missing-trait controls, structural 23, wiring 60 + 186 controls, negctl 60 + 3; PASS |
| Fresh stock simulator configure/build + corpus --require-anchors | **36/36**, all 14 per-stream fields equal W6 pre-check, zero assertion failures |
| s18 | **269517 events**, MD5 **32afbf11e43b4bf9d0bd470ad502ba0a** |
| Fresh lus | SHA-256 **e304147d99ae166fb815e6a2ef06c5ff905579b68add3b0ba142d7031e26caa2**, matches W6 pre-check |
| Command inventory --check | **197 rows**, unchanged |

The keystone is taken from the fresh manifest and required current BASELINE anchors, not recalled history. Raw simulator streams/build live under the scratch path in artifacts/scratch.json; the compact full manifest is retained here. No raw output is decoded by dropping invalid bytes. The first attempt to read the probe log as strict UTF-8 failed on its legitimate raw 0xBB; the evidence-only reconciler now uses reversible surrogateescape. That did not restart or alter a probe/source run.

Coder gate proposal: source-validation/preserved-input census; repeatable pre/post board measurements below `.pio-measure/w7/` with the stock pair tool's exact rules; native build+direct binary; fresh anchored corpus/per-stream comparison; stock and supplemental ABI pins; firmware-UI **default including controls** plus B456 accounting tests; board-UI **default including controls** and B459 manifest/census; inventory/authority/ownership/literal checks; full tools discovery after runner edits; pinned six-env warning census; selector-(a)/(b) union with new entries derived, every baseline at the new native floor; final sealed source hashes and exact `PIN re-synced? YES — <derivation>` line. Preserve service/config/NV/console/simulator inputs throughout. Compare board repeatability within each same-source pair; base-to-final is attributed manifest/section/object/symbol comparison, not measure_board compare between different sources.

Independent QA per P6: rerun native, corpus, ruled board pair, changed-file batteries plus new target, stock/supplemental ABI, firmware-UI/default controls/accounting and board-UI/default. Rerun changed tool/accounting regressions and verify frozen source/coverage deltas. Coder carries the full dependency union and warning census; QA can add a dependency battery when a concern warrants it. No full feature-matrix rebuild or extra board env is implied.

Prediction: Node/wire/NV/console grammar and 197 inventory rows stay unchanged; **all 36 streams byte-identical**. gateway payload/RAM/flash predicted unchanged because its OLED TU is absent; mobile linked RAM/flash must be measured against the approved object shape, including section alignment. No flash allowance is assumed. No fw_main caller change is needed.

The repaired two-profile stock transcript is a useful inexpensive coder sanity run (202/206 rows, pairs equal), but neither profile compiles the panel editor. It is not editor reachability or rename-storage proof and need not expand QA's mandatory affected-probe set when all its inputs remain hashed read-only. W0's identity arms remain the service witness; rerun them if the service signature or any linked input changes, which is otherwise STOP.

## 9. Author decisions and package order — Q12

Recommend **W7 alone**, with all name entry/review/save/result/prompt/return paths completed and independently gateable. W8 is a separate product extension with radio admission, draft locking/IDs, send outcomes and different retain/release rules. They can reuse one parameterized editor without shipping a half-navigable state. P6's pairing rule is not a requirement to combine these two: rename can work completely with written-message rows absent. If paired, W8 needs its own pre-check facts, SendReq/allocation pricing, and a reviewed combined contract before code; this pre-check is not a W8 gate.

Owner decisions needed: the **allocation choice** and **W7 alone versus paired W7/W8**. Author choices still to freeze consistently:

1. Prompt body/rail placement; prompt long_fire return; whether SKIP re-admits after a console settings change. Recommendation in §4.
2. Sliding two-row window near the cursor and exact end-of-draft underline; propose first visible row max(0, cursor/19−1), keeping the cursor on row 2 except the first text row. At exact multiples of 19 the next empty cell must be visible. This rule is not yet explicit in §5.3.
3. Header FULL/EMPTY dismissal: does the next press also perform its normal ring action? The current text says “until the next press” but gives no consumption rule. Specify it, including cap-full insertion's return to group 1. Do not import OPTIONS CHANGED's different consumed-press rule accidentally.
4. E4 BACK and failed-save acknowledgement ring landing, preserving the cursor; the general first-item rule suggests DEL for newly entered control ring and group 1 for a returned editor. Freeze these instead of relying on tests to invent them.
5. WAS capture point, exact name-review rows and unreachable-service-refusal strings. No new last-writer conflict policy is implied.
6. If name32 chosen, the explicit interim resource/draft-ID exception in §5.6. No W8 SendKinds/locked-send path in W7.

No wire/NV ruling or console change is required. Owner D1–D15 are not reopened. These details must be settled before the brief is authorized; this report does not dispatch the coder.

## 10. Proposed metal residue — Q10

Do not edit the metal plan during this pre-check. On implementation approval/PASS, freeze the chosen panel rows and add only physical residue:

- **EDIT-01a (W7):** uncoached wearer opens My device's ` CHANGE NAME >BACK`, selects CHANGE NAME and types `STAN`, including one mistake and DEL correction. Observe `NAME           4/32`, `SAVE NAME?`, `STAN`, `WAS <old label>` and ` SAVE >EDIT`; explicit save → `NAME SAVED`, then My device shows `STAN`. Read `whoami` to confirm `name="STAN"`, reboot and confirm persistence. Record gestures/time/accidental doubles; ring spacing, `_`/ADD SPACE distinction and the 6×1 cursor on glass. No typing-speed claim from the host measurement.
- **EDIT-01b (W8, still OWED):** `RETURN TO BASE NOW` with a correction, review and real received bytes. W7 cannot pass this message half by changing a name or selecting D9's mixed-case saved phrase.
- **UI-04/UI-06:** from an unnamed Home, gate refusal shows only its existing note; clean JOIN/CREATE shows `NO NAME SET / SET NAME / >SKIP`. SKIP continues the original setup. Saving `STAN` then acknowledging continues the selected flow, or shows a fresh `SAVE OR DISCARD / IN SETTINGS`, `RELOAD OR DISCARD / IN SETTINGS` or `CFG UNAVAILABLE` note and returns Home. No setup continuation after an alarm.
- **UI-13:** after a real saved rename My device/Home update; unpinned peers learn it at later key exchange, pinned peers may retain old labels. Observe that caveat, do not require instant peer propagation. Existing label/font checks remain.
- **UI-16/UI-17:** deliberate arm/cancel/fire in each editing/review/result origin; `RELEASE!`/`CANCELLED`/emergency takes precedence, draft survives, review needs fresh approval, zero hidden rename/send. Verify a saved name is not undone; setup-origin saved-result fire returns Home.
- **POWER:** separate quiet boots without a console sleep latch; sustained real typing remains responsive while radio service continues, blank/wake retains ring/cursor/draft and consumes first wake press, idle sleep resumes without panic/wake-arm faults. Reuse current POWER-01 diagnostic spellings; no invented new console line.

Rename endurance/power-cut qualification is physical, not covered by the scripted fake. W0's USB-BLE-03 persistence residue remains; reuse that storage obligation rather than duplicating the whole console gate.

## 11. Candidate fold-ins, not authorized — Q11

| Finding | Bounded edit and cost | Recommendation |
| --- | --- | --- |
| B480 | One direct, memory-safe model mutation deleting `if (preset_generation_moved(s)) close_compose();` (:3157), with exactly-one guard and existing DM generation-close assertions RED. Same harness W7 already edits; **+1 model entry** and derived counts. No production change. | Fits technically if owner folds it; otherwise keep it open. Predicate Y06 is not a replacement for deleting the action. |
| B481 | The legacy case at test/model:1165 fires emergency before its purported compose gestures, so overlay absorbs them. Smallest honest repair: retitle/recomment it as overlay-absorbed compose gestures preserving the alarm; retain the neighboring real queued-ordinary-before-alarm priority case. If retaining the old claim, rebuild a genuinely competing queue fixture and add a discriminating overwrite control instead. | Accurate title/comment repair is cheaper and separate from any production change; owner decides. Audit exact title/comment readers. Do not fabricate a competing request while the overlay owns input. |

Neither is folded by this pre-check. B493–B496 remain unrelated tool/test follow-ups. **No new finding was discovered or registered; next free remains B497.** Missing W7 instrumentation and unsettled proposed design details are listed above for the brief rather than mislabeled as existing implementation bugs.

## 12. Evidence and limits

The [pre-report preservation check](2026-10-03-standalone-mobile-home-w7-precheck/preservation-before-report.json) proves every entry input unchanged after all runs. The [final preservation check](2026-10-03-standalone-mobile-home-w7-precheck/preservation-final.json) names only this new report/directory as added paths. Raw runs are in ignored `artifacts/2026-10-03-standalone-mobile-home-w7-precheck/`; ABI pricing copies and simulator build/streams are in named /tmp paths in the evidence. Nothing was removed. The receipt/compact measurements/proof sources are retained; raw paths are local captures, not an owner backup. The [seal](2026-10-03-standalone-mobile-home-w7-precheck/SHA256SUMS) covers this report and every compact evidence file. To replay captured scripts, copy them to a scratch output directory with the recorded raw inputs; do not run them in the sealed directory.

Not run: board measurement/build pair (coder responsibility at this stage), warning census, mutation executions/full tools discovery, W0 real identity arms/transcript/deferred-actions, and metal. The static census is not a RED claim. The proposed allocation is not firmware implementation, a stack high-water, linked RAM or approved scope. The independent native/probe/corpus figures above were actually rerun.
