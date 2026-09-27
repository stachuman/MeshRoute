# Standalone Home W3 — QA pre-check ledger

Date: 2026-09-25. Role: independent QA; Claude authors the brief; a separate coder implements.

**Pre-check complete — suitable for brief authoring, not implementation authorization.** One C1 slice is feasible: extract counted-byte paging, decouple the compose display bound, and share the provisioning admission gate. No product change, resident state, layout, wire, NV, `lib/` or simulator change is required. Keep the live detail buffer at **2 × 20 bytes**, the record maximum at **17**, and Home call sites out of W3. No owner ruling is requested.

Two qualifications belong in the brief: the extracted fixed-byte pager is **not** the later review screen's word-wrap algorithm; and current host rendering checks do **not** prove the maximum-length detail sequence, 17-character compose rows or blocked-PROVISION note placement. Add targeted real-render checks before changing production, then compare the same fixtures afterwards. Zero resident-state growth is an enforceable requirement; identical linked RAM is a prediction to measure, not a result of this pre-check. Flash neutrality is not implied by C1.

Evidence: [checksums](2026-09-25-standalone-mobile-home-w3-precheck/SHA256SUMS), [whole-tree inputs](2026-09-25-standalone-mobile-home-w3-precheck/inputs.json), [source pins](2026-09-25-standalone-mobile-home-w3-precheck/source-pins.json), [run ledger](2026-09-25-standalone-mobile-home-w3-precheck/runs.json). Line numbers below are observations at this inventory; symbols are the anchors (P4/V2).

## Q1 — base and permitted preparation set

| Input | Independently verified state |
| --- | --- |
| MeshRoute | `8360802904f7bd0023279d3da844453d61207ede`, W1c uncommitted |
| Simulator | `6585649ea5a780f0542b2931853a667be56a5b2b`, clean |
| W1c candidate | All 11 frozen source/test/tool files match the QA-passed candidate, including line counts |
| W1c QA landing | Six documentation hashes and QA receipt match; 29/29 files in its checksummed evidence folder verify |
| W1c QA receipt | SHA-256 `a448172191f45936b3aa59a0c9c4c868a7b2cf3b29805a959204ca8c334f76ad` |
| Initial inventory | 1,352 MeshRoute paths plus 285 simulator paths; tracked and nonignored untracked files, with symlink targets recorded |

[Reconciliation](2026-09-25-standalone-mobile-home-w3-precheck/base-reconciliation.json) distinguishes the W1c implementation freeze from its subsequent authorized QA documentation landing. There is no unexplained change or newer owner commit.

The 17 modified tracked inputs are the 11 W1c files (`lib/core/node.cpp`, `node.h`, `lib/console/console_parse.cpp`, `src/firmware_commands.cpp`, `fw_main.cpp`, three native test files, the inbox-verbs probe's two files, and the mutation harness), plus `MEMORY.md`, the register, the address-book design, the standalone Home design, the companion contract and `tracker.md`. The complete paths/hashes and untracked W1c brief/pre-check/review/coder/QA artifacts are in `inputs.json`; do not reduce this base to HEAD or copy only tracked files. No tracked deletion or staged change was present.

The author should pin this commit-plus-inventory base and these new evidence artifacts. Any author documentation landing becomes a newly inventoried preparation input; it must not be mistaken for production drift. A commit is not a prerequisite. Final preservation is recorded in [preservation.json](2026-09-25-standalone-mobile-home-w3-precheck/preservation.json).

## Q2 — exact pager behavior and smallest pure seam

All production policy lives in `src/firmware_ui_model.h`; rendering is in `src/firmware_ui.cpp`. [Pager census](2026-09-25-standalone-mobile-home-w3-precheck/pager-users.txt) and the broader [symbol census](2026-09-25-standalone-mobile-home-w3-precheck/symbol-users.txt) retain individual users.

| Symbol / current anchor | Source fact to preserve |
| --- | --- |
| `kDetailCols`, `kDetailBodyRows`, `kDetailPageChars`, `kDetailMaxPages`, `kDetailPageMs`, model:1399–1404 | 19 columns, 2 rows, 38 bytes/page, ceil(241/38) = 7 pages, 2,000 ms |
| `UiState::detail_line`, model:2444 | Two terminated 19-column rows, `char[2][20]`; no third resident row |
| `_detail_body`, `_detail_len`, `_detail_page_at_ms`, model:3741 onward | Counted, model-owned body and cadence clock; keep their storage and ownership |
| `UiModel::on_inbox_opened`, model:3197 | Validates the pending request's identity first. Null body means length zero; clamp to `protocol::inbox_max_body` (241), sanitize counted bytes using `ui_display_byte`, terminate backing body. Pages are ceil(count/38), with empty mapped to one. Start page zero, select BACK, stamp time, refresh, mark dirty |
| `refresh_detail_page`, model:5341 | Offset = page × 38; row offset = row × 19. Copy only counted bytes and write one NUL at each row's end. Does **not** clear unused trailing row bytes |
| `close_detail`, model:5326 | Resets modal/action/failure/page/identity/body length and leading NULs, invalidates neighbor, marks dirty; it is a state transition, not page slicing |
| Paging branch of `on_tick`, model:2841 | Only when body is visible, not blanked, no blank is due, pages > 1, and elapsed ≥ 2 s. Advances once, modulo page count, sets clock to now, refreshes and marks dirty. No multi-page catch-up; no inactivity extension |
| `unblank`, model:5261 onward | Retains the current page; restarts its clock. User wake, message wake from darkness and applicable reply wake share it |
| `on_msg_wake`, model:3138 onward | Arms the message-wake deadline; calls `unblank` only when dark. An already-lit message does not restart the detail clock |
| `draw_inbox_detail`, UI:1560 | Header on body row 0, two detail rows on rows 1–2, BACK/DELETE on 3–4. Gone/refusal rendering is separate. UI:1012 asserts renderer body width equals model detail width |

Recommended seam: two small pure operations in the existing model header near its geometry: **page count from a counted length and geometry**, and **one page's row slices from a body, count, page and geometry**. Parameterize both columns and row count (template dimensions are a small, type-safe option). Require nonzero geometry, bound arithmetic without truncation, and preserve the existing counted-buffer contract. No `strlen`, allocation, sanitization policy, timer, modal identity or dirty-state ownership moves into these helpers. Invalid-geometry fallback is unnecessary; constrain that domain explicitly.

`on_inbox_opened` keeps normalization/sanitization, start state and clock; `refresh_detail_page` becomes the two-row adapter; `close_detail`, tick, blank/wake and deletion retain their state transitions. Keep the existing constants available to readers. Preserve writes as copied bytes plus the terminating NUL, including empty rows, rather than introducing a whole-buffer clear.

The present outputs are: length 0 → one page/two empty strings; 38 → one full page; 76 → two full pages, **no extra empty page**; 241 → seven pages, with page 6 starting at byte 228, 13 bytes on its first row and an empty second row. Embedded NUL/control/high bytes have already become `.` before slicing. A 3-row helper exercise can use a **local test buffer** without widening `UiState`.

Native coverage is substantial: `test_firmware_ui_model.cpp` `ui7d-modal` cases at 2358–2510 cover geometry, lengths 0/1/38/39/42/76/77/241, null and zero-length bodies, row copying, sanitization, cadence/cycling and blank priority; `ui17-hold` at 5923/5971 and `ui17-wake` at 6354 cover retention and restart through the real model call order. The call/field users also include detail gestures, header formatting, deletion/error paths and emergency closure in this same header, its renderer, native model tests, the mutation harness, and board-UI W43. No second production pager was found.

**Downstream boundary:** design §7.2 explicitly leaves Inbox byte slicing unchanged but specifies *word-wrapped* review lines, three per page (up to six pages for 163 bytes). A 3 × 19 byte slicer alone would not implement that contract. Word-break selection and future review state/cadence wiring belong to W6/W8. The optional “share `detail_line` widened to 3 rows (W3)” parenthesis in §11.1 is outside this explicitly narrowed W3 request; the author should clarify it in the brief/design landing, not implement it here.

## Q3 — compose display width versus record capacity

[Compose census](2026-09-25-standalone-mobile-home-w3-precheck/compose-users.txt) and `symbol-users.txt` contain the literal users. Their roles are:

| File / symbols | Meaning and W3 treatment |
| --- | --- |
| `src/device_nv.h` `UiPresetSlot`, `kUiPresetTextMax` (347–356) | Record capacity = 17, `text[18]`. **Unchanged**; W6 owns the increase |
| `src/firmware_ui_model.h` `ComposeSlot` (1469), `compose_project` (1495) | The two executable UI couplings: array bound and copy clamp. Replace with a display-width constant. Keep slot identity, location flag, enable/kind filtering and ordering |
| Same header `compose_row_text` (1616), `compose_row_line` (1648) | Reads the copied row only for display; preset rows have selection + `L`/`-` + text; GRANT KEY/BACK have only selection. Correct coupling comments at 1470 and 1627 |
| Same header `UiSnapshot::preset_dm/preset_ch`, `ui_snapshot_publish_presets` (2036–2046) | Owns two projections and their generation. Keep frame freezing and carrier conversion unchanged |
| `src/firmware_ui.cpp` snapshot publication (~917), `draw_compose` (~2292–2305) | Publishes the catalog once and renders copied rows through `compose_row_line`; no independent text copy or clamp |
| `src/firmware_ui_presets.h` `validate_preset_text` (332), `preset_slot_put` (414) | Record validation and storage clamp; continue using the **record** maximum |
| `src/firmware_ui_send.h` `send_gate_of` (493), `ui_compose_send_line` (534) | Sends from the **live catalog**, using stable slot and generation, then its length/text; never sends `ComposeSlot::text`. The bound comment near 470 concerns the current send record, not a display buffer |
| `test/test_firmware_ui_model.cpp` | Projection/default/gap/kind tests, row/action formatting, frozen-generation tests, budget tests at 5254/5260 and layout tests at 9906 onward |
| `test/test_firmware_ui_presets.cpp` | Record pin at 102; default projections at 203–211; 273-byte overflow/clamp tests at 702/726. These remain record facts |
| `test/test_firmware_ui_send.cpp` | Exact live-catalog lines, maximum stored phrase, stale generation and send validation. Keep these independently of display width |
| `tools/probe_board_abi.py` | `ComposeSlot`/`ComposeList` size/alignment/feature-TU pins |
| `tools/probe_ui_model_mutations.py` | `model` Y projection/formatting/generation controls; `uipresets` patterns at 7399/7415/7417 still target record-limit validation |
| `tools/probe_firmware_ui/` | Catalog publication, exact compose rows/actions, catalog changes and send rejection through the device TU; controls C137–C141 and K7 grant controls |

A source search found no other executable `kUiPresetTextMax` consumer in `src`, `test` or `tools`; ABI references and indirect row consumers are included above. `firmware_ui_chrome.h` consumes modal state for navigation, not phrase payload or its row bound.

Recommended authority: a named **compose text-column** constant derived from the 19-column body budget minus the two marker columns. Reuse the current model/body width relationship; retain `kDetailCols` and its renderer assertion. A small common body-column constant with `kDetailCols` as its unchanged alias is possible, but do not turn this into a geometry cleanup or change W43 unnecessarily. The derived display value happens to equal 17 today; do not retain a dependency on the record maximum.

Restate the model budget test as `1 + 1 + display_text_cols == body_cols`, with the 17-byte fixture asserting the **display** bound. Keep the separate record test `kUiPresetTextMax == 17` and record-layout assertions unchanged. Correct the surrounding “widest phrase the catalog can hold”/“can never be clamped” comments so they distinguish current record validity from permanent display geometry; don't prematurely claim the W6 truncation marker exists.

Preserve `sizeof(ComposeSlot)=20`, alignment 1, offsets text=0/slot=18/loc=19; `ComposeList=161`, alignment 1; both lists and generation retain their `UiSnapshot` offsets (1012/1173 and 1008). The stable slot selected by `compose_gesture` and `ui_perform_send` at UI:551 still use the live catalog. Future 163-byte storage, clipped-row `»`, review and send-buffer changes remain out of scope.

## Q4 — provisioning entry, opening and observability

`UiModel::settings_activate` (4106) clears the preceding settings note before resolving the selected row. Its `CfgRow::provision` arm (4142–4147) checks attached/open service, then conflict, then unsaved, then calls `enter_provision(Provision::menu)`. It never saves. The outer activation path marks dirty; retain note clearing, highlight/row identity and dirty timing.

`ProvBlock` remains `none`, `unsaved`, `conflict`. `settings_note` (2636 onward) returns exactly `RELOAD OR DISCARD` for conflict (17 columns), `SAVE OR DISCARD` for unsaved (15). `draw_settings_tail` (UI:2094) places the note at body row 4, before any reboot note. Unavailable service is **not** a third `ProvBlock`: `draw_settings_screen` (UI:2115) draws `CFG UNAVAILABLE` on row 2 and returns.

The important reachability boundary is `activate`'s closed SETTINGS arm (model:3917–3934): without an open service, the menu itself never opens. Therefore the deeper PROVISION unavailable guard is defense in depth on today's SETTINGS path. Do not describe a user traversing that menu while unavailable, or count a private-state injection as real navigation coverage.

`sync_settings` (4022) calls `_cfg->open()` only when attached and not open, **before** returning for the closed passive view (4039). Arrival snapshots the config baseline even before the user enters SETTINGS. This is a behavior to preserve, not work to move exclusively into the new gate. `ConfigService::open` (`src/firmware_config_service.h`:329) returns `already_open` before touching the store if already open. After `load` fails, it stays closed; subsequent `sync_settings` calls can attempt another load. Do not add an “open attempted” latch or change this retry behavior.

Recommended shape: a private repeat-safe **ensure-config-open** helper, shared by the existing `sync_settings` location and one provisioning admission function. The admission function ensures opening, refuses unavailable, checks conflict before unsaved and records the existing note; its caller performs the requested transition. SETTINGS calls it and, on success, retains its existing `enter_provision(menu)` call. The future Home caller can ask the same gate before choosing JOIN/CREATE; it is not added now. No saved origin, new resident member, Home note screen or settings save belongs in W3.

**Yes, include the repeat-safe opening step in that shared admission path**, to match §6.6 item 1. Keep the original arrival call too through the same opener. On reachable current SETTINGS activations the service is already open, so this adds no load/write/apply and does not resnapshot a draft. Test that fact; do not infer that a failed `open()` is also a no-op. A direct closed-service gate test is a future-caller/synthetic seam test, distinct from current SETTINGS reachability.

Existing evidence and remaining instrumentation need:

- `test_firmware_ui_model.cpp` `b232-open` (3340) pins passive opening and conflict detection; `ui15-gate` (3802, 3813, 3840, 3863, 3891) pins clean/unsaved/conflict/both/unavailable, exact notes, zero writes/applies and retained draft. `UiFakeStore` (3053) counts writes **but not loads**. Add counted-load coverage for this refactor rather than claiming that existing fixture already observes it.
- `test_firmware_config_service.cpp` pins `already_open` and draft preservation (664–686); its fake has a load counter, but that re-entry test does not assert it. The implementation's early return establishes no extra load; a strengthened count is appropriate in the W3 model fixture without changing the service.
- Firmware-UI probe P0c pins lazy untouched configuration before arrival, P7/P8 exercise settings changes/conflicts, P15a exercises clean PROVISION, and P15b checks zero store activity for a confirmation display. These are not a complete counted open/failed-open sequence through the extracted entry gate.
- Mutations M55–M59 cover refusal order/remedies/no-save; M100 covers passive arrival opening; M105 covers unavailable menu entry. Keep every effect and reachability claim intact.

## Q5 — real-render instrument, current gaps, and before/after proof

The instrument is **`tools/probe_firmware_ui/run.sh`**, which compiles the real device TU through W1's wrapper and drives the real renderer with a canvas fake. Native tests drive the model and pure formatters; the simulator does not compile this TU.

| Surface | What the current probe actually observes | Gap for W3 |
| --- | --- | --- |
| Inbox detail | P6b/P6e use six-byte `ch-one`/`dm-one`, P6b2 blank/wake that short modal; P14d retains its rail; P14f checks ordinary-screen width and this short detail | No seven-page maximum body render, second full row, partial final page, exact-multiple boundary or multi-page wake render |
| Compose | P27b exact gapped DM rows including `>-Are you OK?`, ` LMEET AT THE COL` (15 text bytes), ` -ON MY WAY`; P27b2 and P24k7 render GRANT KEY/BACK; P27c exercises stale generation | The native budget fixture has a 17-byte phrase, but the device render fixtures do not exercise that width in both DM/channel and marker states |
| Blocked PROVISION | P14g checks config marker/badge; P15a reaches a clean provisioning menu | Neither `SAVE OR DISCARD` nor `RELOAD OR DISCARD` is asserted by the current probe main. Native exact strings don't prove row placement or priority over reboot text |

Fresh measured probe totals: **l2 467/467, v3 902/902, BLE-row 467/467; 225 controls verified, zero unusable**. The runner also prints a de-duplicated coverage rollup (`438`/`873`); that is not the executed binary check count. C0 retains its required build-failure meaning; do not blanket-convert existing controls to must-build.

Recommend extending fixtures **before the production extraction**, rerunning them against that unchanged source, then retaining their exact expectations afterwards:

1. Detail lengths 0/38/39/76/241, every page's two rows and header, final partial row, cycle, and blank/wake on a page other than zero; include existing sanitizer/null-body model coverage.
2. Seventeen-byte DM and channel phrases, selected/unselected with both location markers, plus unchanged GRANT KEY/BACK/empty rows. Preserve generation/frozen-frame checks.
3. Unsaved, conflict-only and both flags on the real SETTINGS → PROVISION path: exact row-4 note, no menu transition, no implicit save/apply; unavailable stays on the closed view. Include note-clearing behavior and counted opening in native fixtures.

Add controls for any new proof that existing controls cannot redden: e.g. wrong second-row stride/drop or shifted rendered row, loss of a final-page byte, wrong 17th-column/marker render and dropped/swapped blocked note. Each new control must match the intended target, compile when that is its contract, and fail the named new checks; no vacuous count or crash substitutes. Keep the wrapper including the **file under test**, including a mutant, not an absolute original path. Exercise PROVISION render fixtures on capability-bearing arms (currently the child-enabled v3 arm); do not invent a reachable PROVISION menu on an arm whose capability table omits it.

A before/after transcript is feasible but **does not exist as a complete artifact today**. The canvas in `probe_main.cpp` (~244) already stores per-frame text plus draw records (`page`, coordinates, dimensions, bitmap/text data), reset at frame start (~565). Normal stdout contains selective diagnostics, not every frame; comparing two ordinary PASS logs would not prove render identity. Prefer focused deterministic frame transcripts or explicit exact row/coordinate assertions. If adding a transcript, record the selected fixtures across OLED page replays; normalize bitmap pointers to content/identity (addresses vary), and detect recording-buffer truncation. This proves host-observed draw operations and text, not physical pixels or a new metal result.

## Q6 — mutation selectors and re-anchoring ledger

The census was derived by **AST literal parsing**, never importing the executable harness (which starts runs). See [mutation-census.json](2026-09-25-standalone-mobile-home-w3-precheck/mutation-census.json). These are configured counts and literal-match checks, **not fresh mutation-run outcomes**.

Selector (a): `model` **239** and `sliceCbudget` **1**, both targeting `src/firmware_ui_model.h`; no native battery targets `src/firmware_ui.cpp`. All 239 model anchors currently match exactly once, including all eleven requested anchors.

Recommended selector (b): `uipresets` **32** (live catalog/record), `uisend` **15** (payload and stale-generation gate), `config` **32** (opening/draft/refusal semantics), `uiprov` **45** (provision adapter). Union: **6 batteries / 364 configured entries**. None is replaced by the real-render controls. `chrome` (44), `uistatus` (13) and `uiinvite` (32) are nearby dependencies but need not be added solely because the same model owns their state: their formatting/navigation/invitation algorithms are unchanged by the recommended extraction. Re-census if the brief expands that fence. QA must at least rerun selector (a) plus affected probes under P6; the author should name which dependency set it additionally requires, keeping the coder's complete required union explicit.

| Anchor | Current model line / matches | Extraction guidance; mutant meaning must survive |
| --- | --- | --- |
| M12 | 3203 / 1 | Leave counted body normalization at admission; keep exact anchor if untouched. Mutant still replaces authoritative length with NUL scanning |
| M14 | 3209 / 1 | Re-anchor ceil arithmetic in generic count helper if moved/parameterized. Mutant still floors partial pages |
| M15 | 3210 / 1 | Re-anchor the empty-to-one rule where it ends up; mutant returns zero for empty |
| M18 | 5347 / 1 | Re-anchor generic row-stride expression; mutant repeats row zero on later rows |
| M20 | 3212 / 1 | Opening still chooses BACK at the original state transition; keep exact statement/anchor |
| M55 | 4144 / 1 | `break` cannot be transplanted into an ordinary admission function. Re-anchor its two adjacent guards and swap them, preserving both-flags wrong remedy |
| M56 | 4145 / 1 | Drop the new unsaved refusal, still allowing an unsaved draft through |
| M57 | 4144 / 1 | Drop conflict refusal; retain its distinct conflict-only case |
| M59 | 4145 / 1 | New mutant must save the unsaved draft and report admission success, so the existing caller enters. Do not substitute an uncompilable `break` or unrelated failure |
| M100 | 4029 / 1 | Preserve one open site in the common opener when feasible. Mutant must still defer opening until browsing, losing passive arrival's baseline. If moved out of the model, retarget this behavior rather than producing an inaccessible `_st` compile failure |
| Y02 | 1499 / 1 | Enable filtering stays at the projection; keep its exact statement. Mutant still renders disabled slots |

Audit beyond the eleven: M52/M53 pin 19-column geometry/derived page capacity; M13 sanitization, M16/M17 cycle/inactivity, M58 remedy strings, S05/S06/S07 dark cadence/wake/blank priority, and the other Y compose controls remain relevant. A new common width alias can change M52's text; don't leave an anchor on a dead constant or claim that all readers are the eleven named ones. Keep new helpers in the same header to avoid splitting this battery's mutation target across files.

## Q7 — remaining text readers and structural tools

[Reader census](2026-09-25-standalone-mobile-home-w3-precheck/literal-readers.txt) covers filename references; symbol/call censuses distinguish executable readers from historical comments.

- `tools/probe_ui_model_mutations.py` is the direct model statement mutator (above). Test fixture changes may also require its expected native-count update; derive the new count, keep the exact `PIN re-synced? YES — <derivation>` receipt line and run its discovery tests if the tool changes.
- `tools/probe_firmware_ui/run.sh` mutates the **device cpp**, not the model header. C22–C24 pin inbox request/body handoff and modal dispatch; C29–C36 pin settings drawing/service/notes; C137–C141 pin catalog publication, list selection, location column, empty and stale-generation rendering; grant controls pin action rows. No current `ctl` pattern edits the three model implementations. Leaving cpp call sites/geometry unchanged avoids re-anchoring these controls; all were exercised in this pre-check.
- `tools/probe_board_ui/run.sh` **W43** is the nearby structural reader: it requires the literal `static_assert(kBodyCols == mrui::kDetailCols,` and `constexpr int kBodyCols = 19;` in the renderer. Keep both. S4a/S4-family layering checks forbid model dependencies in the low-level board UI; a pager stays in the existing pure model layer.
- **W49** (`run.sh`:1262) pins OLED-gated `handle_ui(line + 2, len - 2, out)` inside `firmware_commands.cpp::dispatch`; **W51** (1309) pins the shared fallback and no UI fork in `fw_main.cpp::ble_dispatch_line`; **W54** (1384) pins the guarded preset include/instance/boot/handler/status/help in `firmware_commands.cpp`. None targets the pager, compose projection or provisioning gate; those source files are outside the recommended W3 production fence. W50's boot hook is likewise unchanged. These are not reasons to repair B418 inside W3.
- `tools/probe_board_abi.py` compiles the header and checks sizes/alignments plus feature-TU inclusion from PlatformIO; its Python tests exercise the instrument, not a textual copy of the moved arithmetic. `tools/warning_census.sh` observes compiled diagnostics, not these function spellings.
- Broad scans still read these files: feature ownership scans `lib/src/test` for the remote-admin feature pair; data-type literal checker scans protocol type literals. No moved W3 statement belongs to their rules. Command inventory/authority/A0 and console-sink ownership readers target command surfaces, not these UI helper bodies. No command-inventory regeneration or line-count-neutral workaround is needed for this model-header extraction.
- The board-UI `negctl.py`/fakes and inbox-verbs probe contain historical UI filename references; they do not mutate these model bodies. No separate line-number-driven test reader of the moved statements was found.

The full board-UI probe was **not run or repaired** (B418 belongs to W2). Its relevant source predicates were read; this is not a claim that the whole instrument is green. Coder final D6/P7 census must be repeated after the chosen extraction, including comments that remain in mutation needles.

## Q8 — fresh baselines and limits

All commands and exit codes are in `runs.json`; raw local log hashes are in [raw-artifacts.json](2026-09-25-standalone-mobile-home-w3-precheck/raw-artifacts.json). Raw logs remain under ignored `artifacts/2026-09-25-standalone-mobile-home-w3-precheck/`; durable summaries and full corpus stream hashes are in this evidence folder.

| Instrument, actually run | Result |
| --- | --- |
| `pio test -e native`, then **`./.pio/build/native/program`** | **2,962 cases / 195,904 assertions / zero failed / zero skipped** |
| `tools/probe_firmware_ui/run.sh` (default, controls enabled) | **467 / 902 / 467**, zero failures; **225 verified controls / zero unusable** |
| `python3 -B tools/probe_board_abi.py` | **290 checks, 9/9 controls RED, zero unusable** |
| Fresh stock simulator CMake Release build outside both repos, then `tools/run_corpus.py --require-anchors` | **36/36 anchors reproduced; 36/36 byte-identical to W1c QA** |

Corpus comparison uses scenario set and SHA-256, output MD5/SHA-256/bytes/events, exit status, assertion failures and anchor match—not only the MD5 prefix. Full [manifest](2026-09-25-standalone-mobile-home-w3-precheck/corpus-manifest.json) and [comparison](2026-09-25-standalone-mobile-home-w3-precheck/corpus-comparison.json) are retained. Current `simulation/BASELINE.md` keystone independently reproduced: **s18 269,517 events, MD5 `32afbf11e43b4bf9d0bd470ad502ba0a`, zero assertion failures**. Simulator source stayed read-only; build/output directories were under `/tmp/meshroute-w3-precheck-so1m4f6l/`.

Measured size/alignment pins (not linked RAM):

| Type | native | heltec_mobile | gateway |
| --- | --- | --- | --- |
| `UiState` | 504 / 8 | 504 / 8 | 504 / 8 |
| `UiSnapshot` | 1,336 / 8 | 1,336 / 8 | 1,336 / 8 |
| `UiModel` | 928 / 8 | 912 / 8 | 912 / 8 |
| `ComposeSlot` | 20 / 1 | 20 / 1 | 20 / 1 |
| `ComposeList` | 161 / 1 | 161 / 1 | 161 / 1 |
| `Node` | 235,208 / 8 | 122,176 / 8 | 157,304 / 8 |

The ABI probe deliberately measures types even where their feature TU is not present. For the OLED types above, its feature-TU marker is true only for `heltec_mobile`; native unit tests instantiate the header without compiling the device UI TU. Gateway layout measurements are **not** claims that gateway allocates these UI objects. `test_firmware_ui_model.cpp` also pins native aggregate sizes and relevant compose offsets; no re-pin is predicted.

**Corpus necessity:** W3 cannot move it by construction if the fence holds. Nevertheless, roles “Cycle”, step 1 requires native+corpus at pre-check, and the 2026-09-16 P6 addendum explicitly retains corpus at the independent gate for a `src`-only slice. Therefore it was run here and remains in the brief's gate. Prediction: 36/36 streams and s18 unchanged; no anchor edit authorized.

Not run: board size pairs, warning census, mutation batteries, tools discovery, board-UI probe or metal. They are not claimed as green W3 implementation gates. Board before/after measurements belong to the coder under `.pio-measure/w3/`, with the stock tool's paired repeatability rules and no intervening checkout/build changes inside a pair. No new metal behavior is proposed, so no bench edit is needed for W3.

## Q9 — package recommendation and fence

**One brief, all three C1 refactors.** They share the UI model, invariant render/layout requirement and the same baseline/instruments. None requires a behavior or storage change to provide a usable seam. No production fix or future feature should be bundled into this slice.

Recommended edit fence:

| Path | Permitted purpose |
| --- | --- |
| `src/firmware_ui_model.h` | Pure helpers/adapters; display-bound decoupling; one shared admission gate/opener; directly affected explanatory comments. No owned-state addition |
| `test/test_firmware_ui_model.cpp` | Preserve/restate existing contracts; generic pager local-buffer coverage; distinct display/record assertions; counted SETTINGS opening and unchanged gate behavior |
| `tools/probe_firmware_ui/probe_main.cpp` | Targeted current-behavior render fixtures and, if chosen, deterministic focused transcript output |
| `tools/probe_firmware_ui/run.sh` | Required focused control/transcript integration; wrapper/file-under-test and existing control meanings preserved |
| `tools/probe_ui_model_mutations.py` | Necessary equivalent re-anchoring and derived native-count pin; no weakened mutants or unexplained retirement |
| W3 evidence report/folder | Before/final hashes, measurements, reader audit, exact outcomes, controls and unchanged-input statement |

`src/firmware_ui.cpp` need not change: its renderer already reads the same model fields and constants. Keep it a pinned consumer unless the author demonstrates a necessary narrow edit, which then brings its literal readers into the active fence. Keep `device_nv.h`, config service, presets/send implementation, `lib/`, simulator, PlatformIO and ABI pins outside. Record-capacity tests in `test_firmware_ui_presets.cpp` do not need an edit simply to separate the row fact. Documentation updates belong to the authorized author/QA landing, not silent coder changes to pinned inputs.

Brief obligations: pin the W1c candidate and preparation set; add the missing render proof before production changes; give exact helper domains and no-state/no-layout requirements; retain current timer/cadence and unavailable-service reachability; enumerate (a)/(b) mutation union plus new probe controls; specify full coder gate versus P6 QA rerun; predict zero RAM/layout change and corpus identity while measuring flash; list any unexpected source-reader, state, layout or behavior need as a STOP for reconciliation.

No register/design/test/tool/production edits were made by this pre-check. No new production defect is asserted or registered; the coverage limitations and the §11.1 optional-buffer clarification above are brief inputs. The existing B418 and other findings remain with their assigned packages. Nothing is staged or committed.
