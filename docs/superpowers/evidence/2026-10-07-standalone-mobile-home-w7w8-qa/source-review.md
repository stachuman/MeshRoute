<!-- Author: Claude (author-performed QA, 2026-10-07); the owner rules and commits. -->
# W7+W8 QA — source review against brief revision 2

Read-only review of the frozen candidate against the authorized brief (`47cb5a46…`) and the checkpoint return
(`8eabbaa9…`). Symbols pin; line numbers are hints at the freeze. Every file below matched the coder's freeze
inventory before and after the review. Verdict per item: **MET** or a numbered finding.

## Editor core — `src/firmware_ui_editor.h` (new, 367 lines)

| Contract (brief §2.1) | Where | Verdict |
| --- | --- | --- |
| Pure: standard headers + `firmware_ui_input.h` only; no model/config include; no `strlen`, no allocation | includes :18–21 | MET |
| 42 bytes, each once, seven groups of six, in order `ABCDEF`…`SPACE . , ? ! -` | `kEditorRepertoire` :31 | MET |
| Group ring 7 + EDIT; character ring 6 + BACK; control ring DEL LEFT RIGHT DONE DISCARD BACK | `kGroupRingItems`, `kCharRingItems`, `EditorControl` :36–39 | MET |
| E1: short wraps; double on a group → E2 first character; EDIT → E3 on DEL | `editor_gesture` E1 arm | MET |
| E2: double inserts at the cursor if `len < cap`, else `FULL` with no write; either way E1 on group 1; BACK → E1 on the same group | E2 arm; `draft_insert` | MET |
| E3: DEL before the cursor, stays, nothing at 0; LEFT/RIGHT within 0..len; DONE refuses empty/all-space with `EMPTY`; DISCARD leaves at once when empty, else E4; BACK → E1 group 1 | E3 arm; `draft_delete`, `editor_all_space` | MET |
| E4: `DISCARD DRAFT?` over the first 19 bytes, `>BACK` / ` DISCARD`, BACK first; BACK → E3 on DEL | E4 arm; `editor_window` discard branch; `editor_ring_rows` | MET |
| Notes clear at the next eligible press, which still acts; `binding_seen` never drawn | `editor_clear_note` (called before acting) | MET |
| Window r = max(0, ⌊cursor/19⌋ − 1); a cursor at a multiple of 19 in column 0 of the next row | `editor_window` | MET |
| Row 0: caller + used/cap right-aligned; a note owns row 0 alone, left-aligned, counter hidden (W7W8R-1) | `editor_header_line` | MET |
| Ring rows: `>` marker cells, `_` for SPACE in rings only, `ADD SPACE`, `BACK TO GROUPS` | `editor_ring_rows` | MET |
| Preload all or nothing; nothing transformed | `draft_preload` | MET |
| Draft 176/4, descriptor 10/1, as static asserts | :67, :111 | MET |

## Storage (brief §2.2, D16 + D18)

`UiModel` gains `_draft` (176), `_name_origin` (1) and `_written` (4) and nothing else; `UiState` gains `EditorView`
(10) and aliases the two editor rows into the existing 60-B union (`editor_line`); `SendReq` appends `draft_id`
(16 → 20); `SendLive`'s new bool sits in padding (12, static assert). The QA ABI runs measure exactly the §2.2 pins on
all three ABIs (UiState 576/568/568, UiModel 1216/1200/1200, SendReq 20, Draft 176/4, EditorView 10/1, NameOrigin 1/1,
WrittenOutcome 4/1, SendLive 12/4, UiSnapshot 1368, UiChrome 20), and the native `w8-resources` case pins
SendTracker 16 and InputFsm 28. **MET.**

## Name flow (brief §2.3)

| Contract | Where | Verdict |
| --- | --- | --- |
| My device ` CHANGE NAME >BACK`, BACK on entry, short toggles and never leaves (H24), double acts | `home_gesture` my_device arm; `home_activate` resets `EditorView`; `my_device_action_line` | MET |
| Editor: caller name, cap 32, origin `my_device`, preloaded per §2.1 | `open_name_editor` | MET |
| Review rows; `WAS` from that tick's snapshot at review open, then frozen; EDIT selected at entry, after a blank and on `long_arm`; no page token, no LOC | `open_name_review`, `name_was_line`, the blank and `emergency_gesture` resets, `draw_name_flow` | MET |
| SAVE → requested; `take_name_request` once, counted view; `ui_service_name_request` calls `mrfw::rename_node` exactly once; exhaustive mapping | model :3785–3797; `firmware_ui.cpp` `name_result_of`, `ui_service_name_request` | MET |
| Service after the emergency drain, outside the normal busy gate; existing drain calls unchanged | `mr_ui_tick` (call after the drain block) | MET (board-UI W55 pins it) |
| Results and returns per origin; NOT SAVED keeps the draft on group 1, no retry; setup origin re-asks the gate | `name_result_ack`, `editor_return` | MET |
| `long_fire` landings (review/requested → editor; SAVED → released, My device or Home; NOT SAVED → editor; prompt → Home, setup dropped) | `name_flow_on_fire` | MET |
| Prompt `NO NAME SET` / ` SET NAME` / `>SKIP` as a `HomeView` arm on STATUS, after `provision_admit`, only when the own name is empty; SKIP re-asks the gate; DISCARD returns to it with SKIP selected; every exhaustive switch gains the arm | `home_activate` (gate, then `open_name_prompt`), `home_gesture` name_prompt arm, `ui_nav_slot`, `draw_home_screen` | MET |
| Never touches `SendReq`, a send slot, a tracker or the content lock | the name methods | MET |

## Written messages (brief §2.4)

| Contract | Where | Verdict |
| --- | --- | --- |
| WRITE MESSAGE: typed row after the phrases (GRANT at n+1, back last), no marker, no slot, always offered; out of range fails to `back` | `ComposeRow::write`, `compose_row_count/kind/text/loc_marker` | MET |
| Binding at WRITE: kind, live team, DM peer ID; known bit + hash once from the resolver; label at 8 columns from the full raw name | `open_written` (bind phase), `ui_editor_capture` (resolve only on `bind`) | MET |
| Broken binding: note, draft kept, DONE re-shows it and opens nothing, nothing re-binds; catalog changes have no effect | `written_binding_note/tick`, `open_written_review`, `preset_generation_moved`'s written exemption, `compose_gesture` order | MET |
| Review: `TO TEAM <ID8>` / `TO <label≤7> <HASH8>` / `TO T<n> UNVERIFIED`; W6 word wrap; ` SEND >EDIT     n/m`, EDIT selected, never LOC; time-only paging; projection from a borrowed view, never via the catalog path | `ui_review_capture` written branch, `ui_review_header`, `review_written_action_line`, `refresh_review_page` | MET |
| SEND: BUSY while an ordinary request is pending (nothing queues); else lock, queue the bound request whole (U2), `SENDING...` | `written_review_press` | MET |
| Gate per kind; two new typed refusals with zero submission | `send_dest_gate_of`, `send_gate_of`, `send_exec_gate_of`, `ui_perform_send` | MET |
| **Two phases (W7W8R-2):** review admission asks `send_gate_of` only — D19 never applies; execution adds the draft check first and D19 last; no duplicated validation, no invented ID | `ui_review_capture` (both branches call `send_gate_of`), `send_exec_gate_of`; `review_note_of` maps the two execution-only answers to `none` | MET |
| D19's live fact read from `g_node.team_local_id()`, default false | `ui_send_live` | MET (W56 / W8-C5 pin it) |
| Composer: written branch first, `%.*s` from the locked draft view, never `-l`, too-little capacity refuses; worst lines fit 199 | `ui_compose_send_line`; static asserts | MET |
| Request states recorded at the attributing event; `ctr == 0` = accepted-open without touching the emergency counter | `written_executed`, `on_send_unhandled`, `written_outcome_admitted` | MET |
| All 18 `SendFailReason` values classified as tabled | `send_fail_never_aired`, `send_outcome_never_aired` | MET |
| Trackers: one family classification in `match_dm`, `match_aired`, `match_channel_sent`, `match_blocked`, `tick` and the `submit` call | `SendTracker`; `ui_perform_send` | MET — see **B501** for the device-glue reader outside that list |
| `ui_pump_trackers` keyed on `normal_tracking_open` (closes once on acknowledgement/pre-emption) | `ui_pump_trackers`; `written_release` | MET |
| `long_fire` withdraws only a pending **written** request (by kind), unlocks, editor on group 1; emergency untouched; a phrase's owed request unchanged | `withdraw_written_request`, `written_on_fire`, `emergency_gesture` | MET |
| Returns by request state; `take_send_request`'s reset arms gain the written kinds; a D19-refused phrase acknowledges to its list | `written_result_press`, `take_send_request`, compose result branch | MET |

## Interruption, rails, rendering, B480/B481 (brief §2.5–§2.7)

Editor joins the existing priority order (long gestures, wake, overlay, dispatch); a blank resets the name review to
EDIT and the written review to EDIT; frames read only the frozen descriptor and union (`draw_editor`,
`draw_name_flow`, `draw_review`, `draw_compose`); cursor `draw_hline(kBodyX + 6·col, body_y(1+row) + 1, 6)` with
`kBodyX = 12`; rails: the name flow and prompt box STATUS, message flows box SEND. B480: one model entry deletes the
exact statement `if (preset_generation_moved(s)) close_compose();` (one match in source). B481: the legacy case is
retitled and recommented, assertions unchanged; its neighbour is unchanged. **MET.**

## Findings

**B501 (registered; OPEN / LOW / LATENT, nonblocking).** `mr_ui_tick`'s alarm drain still reads
`s_tracker_normal.kind() != mrui::SendKind::dm` (`firmware_ui.cpp`, the `emergency_pending()` branch). Since W8 that
predicate is true for `dm_text`, so a written DM transaction would be abandoned like a channel post instead of kept
like a phrase DM. The brief review had named this consumer for the reader audit; the coder's audit reported per-file
counts and did not classify it. **Measured reachability:** a `dm_text` transaction can sit in the normal tracker when
the emergency becomes pending only on the `long_fire` tick that also releases the written result
(`written_on_fire` → `written_release` + `close_compose`), so the next tick's `ui_pump_trackers` closes the tracker in
any case (`normal_tracking_open()` is false once released); a queued written request is withdrawn before it can be
drained, and no ordinary written request can start or drain while the alarm is live. The only difference is whether a
DM outcome arriving between that drain and the next pump is matched to an already-released record, which no frame
shows. No current behaviour differs; the deviation is latent. Close by spelling the predicate through the family
helper (`!mrui::send_kind_dm(s_tracker_normal.kind())`) at the next authorized touch of `firmware_ui.cpp`, with a
firmware-UI or board-UI control, and by correcting the two drifted tracker comments that still spell the plane split
as `_k == dm` / `_k != dm` (`SendTracker::match_aired`'s header and `ui_route_send_push`'s note).

**Disposition, no finding:** `src/firmware_ui_presets.h:88` (read-only in this package) says `mrui::SendReq` carries
`{slot, generation}`. That remains true of the phrase identity it describes; the appended `team_id`/`peer_hash` (W6)
and `draft_id` (W8) do not falsify it. No register entry.

**Carried, unchanged:** the brief's rationale sentence "no note fits beside `163/163`" is too broad (FULL/EMPTY/BUSY
fit); the rule itself is correct and implemented. Correct at a future authorized documentation refresh (QA re-review
receipt, 2026-10-04). Not edited here.
