<!-- Author: Claude (W7+W8 coder) — working ledger; the frozen report is ../2026-10-04-standalone-mobile-home-w7w8.md -->
# W7+W8 coder ledger

Brief: `docs/superpowers/plans/2026-10-04-standalone-mobile-home-w7w8-editor-rename-messages.md`, revision 2,
SHA-256 `47cb5a461d07cc3cbaf0ab6ae81fe535e206eda996201885f4dbe878dd11cb8e` (732 lines).

## Preflight and baselines

- Preflight PASS — `preflight.json` (scope.py).
- Baselines §4.1 steps 2–10 on the unmodified tree — `baselines-base.json`, `union-census-base.json`,
  `boards/base-*`, `receipts/corpus-manifest-base.json`. Every figure equals the brief's: native 3059/199638/0;
  corpus 36/36, s18 269517 / `32afbf11e43b4bf9d0bd470ad502ba0a`, lus `e304147d…caa2`; base-1/base-2 + both stock
  compares PASS (gateway RAM 203740 / flash 572096; heltec_mobile RAM 219540 / flash 1400540); warning census PASS;
  ABI 290 / 9/9; firmware-UI 591 / 1059 / 591, 240 controls accounted; board-UI 592 (canvas 124/110, traits 14,
  structural 23, wiring 60 + 186, negctl 60/3); discovery 447 OK; inventory 197; census 15 batteries / 600 entries.

## Checkpoint 1 — the pure editor

- New `src/firmware_ui_editor.h`, new `test/test_firmware_ui_editor.cpp` (17 cases).
- New battery `uieditor` → `src/firmware_ui_editor.h`, registered in `TARGET_SRC`, `MUTS_UIEDITOR`, `MUTS_BY_TARGET`:
  25 entries, development run 25 RED / 0 unusable (`artifacts/…/cp1-uieditor.log`).

## Rulings (coder, within the brief)

- R1 — E4's `>BACK` / ` DISCARD` sit on rows 3–4 (the ring rows), with `DISCARD DRAFT?` on row 0 and the first
  19 bytes on row 1. The brief lists E4's rows without row numbers; rows 3–4 are where every ring is drawn (§5.3).
- R2 — the control ring's items each carry their own marker cell with no extra separator (`>DEL LEFT RIGHT` /
  ` DONE DISCARD BACK`, 15 / 18 columns), exactly the design's sample rows and the brief's worst width 18.
- R3 — the native suite is built without exceptions: the new test uses `CHECK` only (no `REQUIRE`).

## Checkpoint 2 — the name flow (W7)

- Model: the draft, `NameOrigin` and the descriptor at §2.2's sizes; My device's ` CHANGE NAME >BACK` row (short
  toggles, double on BACK returns, double on CHANGE NAME opens the editor preloaded all-or-nothing, cap 32); the
  review `SAVE NAME?` / two name rows / `WAS …` (15 columns, `»` past 15, frozen at review open) / ` SAVE >EDIT`;
  `take_name_request` once → `on_name_result`; the prompt `NO NAME SET` / ` SET NAME` / `>SKIP` after
  `provision_admit`, SKIP re-asking the gate.
- `firmware_ui.cpp`: `name_result_of` maps `mrfw::RenameResult` exhaustively; `ui_service_name_request` calls
  `mrfw::rename_node` once, placed after the emergency/ordinary drain block and before `ui_service_inbox_request`;
  the name flow and the prompt are drawn by `draw_name_flow` and the Home arms; `firmware_ui_chrome.h` gains
  `case HomeView::name_prompt: break;` (rail STATUS).
- Native 3076/201482 → **3091/201858** (+15 `w7-` cases, +376).
- Re-expressed (recorded behaviour changes, brief §2.9): My device's case (`w4b-items: MY DEVICE — a short toggles
  CHANGE NAME / BACK …`); the `nearby_snap` / `home_snap` fixtures now carry a name (`named_device`), because a Home
  JOIN/CREATE on an UNNAMED device now asks for a name first — every pre-W7 Home setup case meant a device that goes
  straight on.
- `kNoNameSetText` already exists in `firmware_ui_status.h` (the STATUS identity row); the renderer reuses it (U1)
  rather than adding a second lexeme.

## Checkpoint 3 — written messages (W8)

- `SendKind::dm_text` / `channel_text` with the family helpers; `ComposeRow::write` at index `n` (GRANT at `n + 1`,
  back last); `SendReq.draft_id` appended (16 → 20); `SendLive.team_local_id` in the padding (12 unchanged);
  `send_dest_gate_of` (the shared destination questions) / `send_gate_of` / `send_exec_gate_of`; the written composer
  branch (`send <id> "<draft>" -t -a`, `send_channel <ch> "<draft>" -t -e`, never `-l`, all-space refused); the
  outcome record and its classification of all 18 `SendFailReason` values; `normal_tracking_open()` for
  `ui_pump_trackers`; `long_fire` withdrawing only a written request; `panel_reasons()` for `freeze_outcome`.
- Native 3091/201858 → **3118/202782** (+26 `w8-` send cases, +1 `w7w8-chrome` rails case; +924).
- Re-expressed (recorded behaviour change: the WRITE row and GRANT KEY's index): 16 compose-row positional cases,
  including `ui10-p3-empty` (retitled: the empty catalog offers WRITE MESSAGE and `back`), `w4b-send … PRESET CHANGED
  covers … item 1 (WRITE MESSAGE)`, `ui7-B66` (n = enabled + 1 + grant + 1, WRITE at `enabled`) and the declared-bounds
  case (5 / 6 rows); the `SendLive` aggregate initialisers gained the fourth member, and the executor calls pass
  `kLiveWithId` (the D19 fixture: an ID exists).
- B481: `ui-model: under the alarm overlay the compose gestures are ABSORBED, and the queued alarm is kept` — retitled
  and recommented, assertions unchanged; the neighbour (`a queued alarm drains BEFORE a queued DM …`) unchanged.
- Mutation: B480 (development run 1 RED); 33 model, 14 `uisend` and 2 `chrome` entries added; **9 re-anchors**, each
  keeping its attacked property and match count 1 — `model` S03, W10, Y05, Y06, W6-M08, W6-M09; `uisend` W6-S02,
  W6-S10; `w4bhome` H24 (measured by AST diff against the preflight harness `27807b32…`; `w4aident`'s table is
  byte-identical to base — its bounded F07 is B492's, already in the base).

## Checkpoint 4 — instruments

- Firmware-UI probe: the `draw_hline` geometry recorder; the honest `mrfw::rename_node` stand-in (`RenameLog`); the
  D19 fixture (`g_node.set_team_local_id(50)` in P9; P31e sets and restores it); phase P32 (a–l); re-expressions
  P17m/P17r (` CHANGE NAME >BACK`; a keep-alive on My device is now TWO shorts), P24k7b/d/f/e (WRITE before GRANT),
  P27b/b2/e (row 4 is WRITE; empty catalog offers WRITE), P29b (WRITE at row 3, the exit row still row 4). Controls
  240 → **263** (W7-C1…C13, W8-C1…C7, C9, C10, C12), accounting complete.
- Board-UI probe: W55 (the name-request drain, 4 controls) and W56 (the draft-source wiring, 5 controls); `expected.tsv`
  +11 rows (592 → 603); `accounting.py` unchanged.
- ABI: stock pins re-pinned (`UiState` 576/568/568, `UiModel` 1216/1200/1200, `SendReq` 20 ×3) — 290 checks, controls
  RED; supplemental manifest `abi-supplemental.json` (Draft 176/4, EditorView 10/1, NameOrigin 1/1, WrittenOutcome 4/1,
  SendLive 12/4) — 335 checks.
- Coverage roll-up (firmware-UI): 872 of 1031 → 939 of 1102 checks reddened. Four pre-existing checks are no longer
  reddened by any control: P25e "…NOTHING was evicted…", P25e "…DELETED NOTHING and REPLAYED NOTHING…", P25h "…TEAM
  KEY RECEIVED is nowhere…", P29 "…nothing in this phase reached the executor". Every control's contract (its
  classified outcome) holds; the attribution of the base reddening is measured in `probe-attribution.json`.
- §2.5 edge timing found uncovered at the pre-freeze review and added (+4 cases): `w7-interrupt` (prompt, name
  review, name result under blank / wake / receive / arm-cancel; sleep), `w8-interrupt` ×3 (the written review in the
  dark incl. the both-due tick; the written editor and result incl. sleep; `millis()` wrap).

## Rulings (coder, within the brief) — continued

- R4 — the DM editor's binding and label are CAPTURED by the device in the tick (`EditorPhase::bind` at WRITE
  MESSAGE, `relabel` on every return), served by `ui_editor_capture` from `ui_service_review`. Why: the label is the
  peer's FULL raw name at 8 columns (§2.4), which only the device reads (no peer names are in `UiSnapshot`), and the
  known bit + hash must come from the resolver once (`Node::team_key_of_id`). `relabel` re-labels the BOUND hash and
  never re-resolves. A press during the one capture tick is ignored. Cost if wrong: one tick of ignored input on a
  DM editor's entry/return.
- R5 — `EditorNote::binding_seen` (never drawn, inside the 10-B descriptor, no new storage) records that a broken
  binding's note was shown and cleared, so the per-tick health check does not raise it again on the next tick; DONE
  re-shows it while the binding stays broken; it clears when the binding heals. Why: r2.26 clears a note at the next
  press, and without the marker the tick would re-raise it immediately. Cost if wrong: the note shows once per break
  (plus every DONE) instead of persistently.
- R6 — "back to the list WRITE MESSAGE came from" (accepted-final acknowledgement and DISCARD) lands the arrow on the
  WRITE MESSAGE row, re-derived from the live catalog (`write_row_of`): the Send list re-opens (`open_send_list`), a
  person's list re-seals its generation. Accepted-OPEN acknowledgement follows the existing ordinary result rule (a DM
  list closes; a Send-list result re-opens the Send list, W4b §6.5). Cost if wrong: the arrow's landing row.
- R7 — D19's mechanism (W7W8R-2 leaves it to the coder): `send_exec_gate_of(req, cat, live, draft)` asks the written
  draft check (lock + `draft_id` + bounds), then the unchanged shared `send_gate_of` (whose destination questions are
  `send_dest_gate_of`, shared with the written kinds), then the D19 team-local-ID check for `channel_canned` /
  `channel_text` only. Review admission (`ui_review_capture` / `review_note_of`) never calls it. Cost if wrong: none
  observable — the two-phase traces pin both phases.
- R8 — the written review's row 0 is formatted by the device at the review's capture (`on_written_review_captured`)
  into the shared 20-B `review_header` (§2.2's alias), so a DM editor re-captures its label (`relabel`, R4) when the
  review closes back to it. Cost if wrong: none (storage per §2.2).

## The development union (before the final chain) and its fixes

- Run in full, sequentially, default 8 workers, on the checkpoint-4 tree (native floor 3118/202782), logs
  `artifacts/…/dev-union-*.log`. `model`: **292 RED / 5 unusable-or-surviving** — M52 (does not compile), Y02 and Y07
  (the suite crashes: SIGSEGV in `ui10-p3-empty`), W8-M04 and W8-M05 (the suite still passes). The rest, RED / entries:
  chrome 50/50, uisend 39/40 (W8-S12 survived), sliceCbudget 1/1, sliceCsend 1/1, w4aident 9/9, w4bhome 26/26;
  uistatus 19/19, uiteam 20/20, uiinvite 32/32, uiprov 45/45, uijoin 26/26, uipresets 36/39 (U13, W6-P1, W6-P3
  UNUSABLE), config 32/32, consoleline 12/12; uieditor 25/25 — **665 / 674**.
- **M52 (my regression):** the new model-header `static_assert(kEditorCols == kDetailCols)` and `editor_line` sized
  by `kDetailCols` made M52's `kDetailCols = 21` mutant fail to compile — M52's own record says the compile-time tie
  belongs in `src/firmware_ui.cpp` (which no native build compiles) precisely so the native suite reddens on its own.
  Fix: `editor_line[2][kEditorCols + 1]`; the tie moves to the renderer beside its twin
  (`static_assert(kBodyCols == mrui::kEditorCols, …)`); a native `CHECK(kEditorCols == kDetailCols)` in
  `w8-resources`. Scratch re-run: M52 RED (70 assertions).
- **W8-M04 (my entry, EQUIVALENT):** `long_fire` queues the alarm (`queue(SendKind::emergency, …)` sets
  `_emg_req_pending = true`) right AFTER `written_on_fire()` withdraws, so clearing `_emg_req_pending` inside the
  withdrawal cannot be observed. Reshaped to the alarm's observable state (`_emg = Emergency::idle`) and
  `w8-withdraw` now asserts `m.emergency() == Emergency::firing`. Scratch re-run: RED.
- **W8-M05 (survivor):** DONE over a broken binding opening the review was masked by the review capture's own race
  check, which refuses back to the editor with the same note. `w8-binding` now asserts at the DONE press, BEFORE the
  capture: no review requested, phase still `controls`, nothing owed to the capture, `draft_id` not advanced.
  Scratch re-run: RED.
- **Y02 / Y07 — PRE-EXISTING, measured on the exact base tree** (a scratch copy with every fence file at HEAD =
  the preflight hashes, the stock harness, `--target=model --workers=1`): both UNUSABLE at 3059/199638 too.
  `ui10-p3-empty` checks `compose_empty_note(l) != nullptr` and then `strcmp`s the same pointer unguarded; both
  mutants answer `nullptr`, the test segfaults, and since [[B490]]'s fix (2026-10-02) a crash after the summaries
  is UNUSABLE instead of credited RED. Proposed fix (in the fence, meaning unchanged): null-guard the `strcmp`;
  scratch re-run: Y02 RED, Y07 RED. **HELD, not applied** — see the STOP below.
- **U13 / W6-P1 / W6-P3 (`uipresets`) — PRE-EXISTING, same class, OUT of the fence:** the dev union measured 36 RED /
  3 UNUSABLE; the base copy reproduces all three (3059/199638). Crash sites in `test_firmware_ui_presets.cpp`: the
  `ui10-p1-defaults` emergency comparison (`strcmp(… , kPresetDefaults[0].text)` with U13's `nullptr` default) and the
  `w6-v1` boot-line comparison (`strcmp(preset_boot_line(st), …)`, `nullptr` under W6-P1 and on W6-P3's second boot).
  U13 cannot be reshaped (a DISABLED compiled default is spelled only `nullptr`).
- **STOP-2 raised to QA before the final chain** (step 9 requires zero unusable; five entries are unusable at base,
  three of them only fixable outside the fence). Options put: (A) fence the presets test's two null-guards in this
  package and apply all three guards; (B) register and exempt the five, fixed separately; (C) another shape.
- Applied to the real tree after the union ended (no run in flight): the §2.5 cases, M52, W8-M04, W8-M05, W8-S12
  (NOT the held null-guards). Native **3123 / 203168 / 0**; `PIN_CASES`/`PIN_ASSERTS` re-synced with the per-file
  derivation (model 453/8808, send 150/2121, chrome 42/2052, editor 17/1844). `dev2-checks.sh`: census 16 / 674, every
  pattern once; M52, W8-M04, W8-M05, W8-S12 RED at 3123/203168; Y02, Y07 still UNUSABLE (held); firmware-UI
  656 / 1130 / 656, 263 controls accounted, PASS; board-UI 603 identities PASS; ABI stock 290 and supplemental 335,
  9/9 controls RED.

- R9 — M52's compile-time tie relocated to the renderer (see above). Cost if wrong: none — the invariant is still
  compile-time on every board build and runtime-checked natively.
- R10 — W8-M04 reshaped (my own entry, before any freeze). Cost if wrong: the entry attacks the alarm's state rather
  than its request slot; the slot's protection is structural (the order inside `long_fire`).
- R11 — the Y02/Y07 guard is in the fence but HELD with the out-of-fence three: one class, one ruling (STOP-2). Cost
  if wrong: none — nothing is applied until QA rules.
- **W8-S12 (survivor, `uisend`):** the pump's close on `normal_tracking_open()` was invisible to `w8-result: accepted
  FINAL`, because a delivered DM / relayed post closes its own slot inside the tracker. New case `w8-result: NO CONFIRM
  keeps the late-ACK slot while shown; acknowledged back to the list, the pump closes it ONCE` — the one acceptance
  whose tracker is still open at acknowledgement, with compose still open on the person's list. Scratch: W8-S12 RED
  (2 assertions); native 3123 / 203168 / 0.
- **Coverage roll-up attribution** (`probe-attribution.json`, scratch copies of the base and checkpoint-4 trees with
  the stock runner keeping each control's output): the four checks no control reddens now were each reddened at base
  ONLY incidentally — P25e ×2 by L10 (a cascade control: 361 failing checks at base, 501 now), P25h F-10 by K3 (whose
  declared target is P15k2; under K3 the run now diverges earlier, P25h's own precondition fails, so F-10 is never
  reached on the mutated screen), P29's executor check by C5 / C94 / C95 / W4b-N1 / W4b-N3 (rendering controls whose
  cascades used to reach the executor inside P29; none does now — the cause is INFERRED, not traced: P29b's compose
  walks gained the WRITE MESSAGE row, which opens an editor instead of a review). Every
  control keeps its declared contract; no control was retired or weakened.
