# Standalone Home W8 — independent pre-check ledger for paired W7+W8

Date: 2026-10-03. Role: Codex QA; Claude authors; a separate coder implements. Input: standalone Home design **r2.25**. **Pre-check complete; authoring can proceed. Allocation beyond D16 and the pre-DAD Send disposition need settlement before dispatch. This is not a brief PASS or implementation authorization.** The [W7 ledger](2026-10-03-standalone-mobile-home-w7-precheck.md) stands; this ledger adds written-message facts and reconciles the author's r2.25 refinements.

No production, test, tool, design, tracker, MEMORY or simulator edit. Only this evidence set and the new register finding **B497** were written. Nothing staged, committed, reset, cleaned or removed. Full permissions do not change the owner-only allocation/ruling boundary.

## 1. Base, pins and preservation — Q1

MeshRoute HEAD **4c1a000bc71706ac438e64161a9452966a66615f**, parent **10f333298ffad1098daeec37e756ed79ab0fd97f**. Simulator HEAD **6585649ea5a780f0542b2931853a667be56a5b2b**, clean. No staged paths. The bounded F07 inherited from the QA-passed tool-package return remains `27807b320427fb090840e927bf559bfff909f78761d98b29e3aff8b3cb4d177d`.

The [entry inventory](2026-10-03-standalone-mobile-home-w8-precheck/inputs.json) hashes **2,495 MeshRoute paths and 285 simulator paths**, including every nonignored untracked input. [Preflight](2026-10-03-standalone-mobile-home-w8-precheck/preflight.json) reconciles against W7: exactly the four declared r2.25 document changes and W7's added evidence; no unexplained code change. W7's **26-entry** seal verifies. Its report is `e4148d274271addf9e0ff1535b4fce6a9db8155ce54df4b73f929e1d9524be1e`; seal `037b975bd3737808627ff90fb389ed0fea649121b5e0f1d60dd434c564b5af6f`.

Entry preparation pins:

| Path | SHA-256 |
| --- | --- |
| Design r2.25 | `af8e9ff351c0018b4c27662cbc3a1268db5533ecfead1e703a08fc60936b9184` |
| Register at entry | `49a0f71b565dcdca3b628e80471af0ac1968ab1db10803210f4f912812791a09` |
| tracker.md | `4f3e656bca2f0c6ac3573a0a6435670a0e2291d2ebb8838bb649fba849a684e9` |
| MEMORY.md | `711314f83bb302aaa94c7ca36571fd0e8dc04bada6f8f2abe2151d9bc592654f` |

**Request pin discrepancy:** its abbreviated register suffix is `9a09`; the actual full entry hash ends `1a09`. This is reported, not treated as a refreshed production pin. The author must use the full hash above, then the final register hash after this ledger's explicitly permitted finding.

Inherited untracked inputs are the tool-package return and QA re-gate reports/directories and the W7 pre-check report/directory. Inherited tracked changes are the four preparation documents and the bounded F07 harness change. [Final preservation](2026-10-03-standalone-mobile-home-w8-precheck/preservation-final.json) allows exactly B497's register row/next-free update and this new evidence set. The register's final SHA-256 is **`24faa8aa412090e8f57c5916e451739e61ed970168f0505b2dec134f67b71ce8`**; design/tracker/MEMORY retain their entry hashes. Next free finding is **B498**.

The combined brief pins commit plus inventory, both pre-checks/seals, and any explicitly declared author document delta. Commits are not gates (P4). No HEAD-only archive can substitute for this inherited tree.

## 2. Send carrier, scoped gate and composer — Q2

Source authorities: `SendKind` (`src/firmware_ui_model.h:1837`), `SendReq` (:1858), `SendLive` (`firmware_ui_send.h:512`), `send_gate_of` (:518), `ui_compose_send_line` (:562), `ui_perform_send` (:603), and the device's `ui_send_live` (`firmware_ui.cpp:584`). Full users, including tests and textual mutations, are retained in the [reader census](2026-10-03-standalone-mobile-home-w8-precheck/symbol-readers.json) and [summary](2026-10-03-standalone-mobile-home-w8-precheck/reader-summary.json).

Current SendReq is **16 B / align 4 on all three ABIs**: kind, peer ID, stable phrase slot, independent `peer_known`, generation, team ID and peer hash. Both `_req` and `_review` own it. Team/hash fields already landed in W6; r2.25 §7.4 should say **only draft_id is newly added**, giving **20 B on all three ABIs**, not merely an approximate host size. New enum values do not enlarge the u8 SendKind. Keep known-zero hashes distinct from unknown; no numeric hash sentinel.

Recommended gate contract, reusing the current authority:

| Kind | Draft question | Catalog question | Destination question |
| --- | --- | --- | --- |
| emergency | none | exempt | exempt, as ruled |
| dm / channel_canned | none | stable slot, equality of generation, enabled, correct phrase kind | live team; known-hash equality/resolution for DM |
| dm_text / channel_text | a counted draft with the requested ID is still content-locked and in bounds | **none**; never a synthetic preset slot/generation | live team; known-hash equality/resolution for DM |

The device adapter currently resolves a peer only for exact `SendKind::dm`; it must resolve **both DM kinds**, once, through `Node::team_key_of_id`. The composer must branch on written kinds **before** any catalog indexing. Pass a borrowed counted draft view, not a pointer into the frozen display rows, an owned second buffer or a strlen-based read. The draft has no required NUL at 163 bytes. A failed lock/ID/length check must be a typed zero-submission refusal; the author must freeze its panel wording. Do not silently use the catalog, empty text or an emergency slot.

Written forms remain `send <id> "<draft>" -t -a` and `send_channel <ch> "<draft>" -t -e`. The 42-byte repertoire excludes both quote and backslash, so no new escaping grammar is needed. A full 163-byte body with a u8 ID/channel of 255 occupies **180/188 line bytes, 181/189 including NUL** (DM/channel). Even the existing conservative ten-digit `%u` bound gives **188/196 including NUL**, within the unchanged **199-byte static buffer**. These are [arithmetic measurements](2026-10-03-standalone-mobile-home-w8-precheck/reachability.json), not a proof of a not-yet-written composer. Require real-composer tests for empty, all-space, 17/18, 163/164 and too-small output capacity. No written path adds `-l`, irrespective of fix availability; phrases and alarms keep their existing location rules.

The shipped path is `firmware_ui.cpp::ui_exec` (:552) → `mrfw::exec_command` (`firmware_commands.cpp:1904`) → shared command-line validation (:1907) → **real console::parse_command** (:1910) → Node::on_command (:1913). It bypasses USB/BLE intake/staging, while sharing their parser/core. The repaired transcript and existing inbox send rows witness those transport/parser forms, **not the new draft/composer/lifetime wiring**. Add native real-parser checks of composed written lines in the send tests, plus exact executor captures in the real firmware-UI probe. Do not claim a parser stand-in proves production parsing.

## 3. Request states and core failure classification — Q3

The four request states are orthogonal to editor phase and displayed DmState/ChanState:

| Written state | Current path / required mapping |
| --- | --- |
| queued | SEND queues a whole bound request and locks the draft; `_req_pending` is still true. A press consumes nothing from the draft or request. |
| known refused | scoped-gate refusal before executor; empty/truncated composition or parser refusal; synchronous executor refusal; or an attributable async reason proved never to have aired below. Unlock; keep draft until explicit discard/review. |
| accepted, open | executor returned queued, including ctr zero; nonzero accepted handle is QUEUED, and correlated send_aired yields SENT, waiting **without ending tracking**. |
| accepted, final | attributable terminal outcome without a never-air guarantee. Release on acknowledgement/pre-emption; do not fabricate a refusal. NO CONFIRM may still upgrade to DELIVERED while the result remains open. |

Current `take_send_request` (:3558) drains emergency first, then copies `_req` whole and clears ordinary pending; its reset arms recognize only dm/channel_canned. `ui_perform_send` gates before touching the tracker/executor, submits, executes synchronously, then refuses or accepts. Its ctr-zero branch calls `awaiting_outcome` **without** `on_send_accepted`. A written request must record execution/accepted-open even on that branch, without changing the emergency attempt counter or its expiry ordering. Preserve existing DM-zero behavior (registered B111), channel bounded expiry and the eight-second correlation window; no invented timeout failure.

`SendTracker::match_dm` (:142), `match_aired` (:178), `match_channel_sent`, `match_blocked`, `tick` and `submit` all need family classification when new kinds arrive. An exact `kind == dm` left behind can turn dm_text into a channel, ignore its ACK or route expiry incorrectly. Prefer one shared pure DM/ordinary/written classification. Preserve exact full ctr+peer DM attribution, channel ctr attribution, emergency-first routing, late-ACK upgrade and the ban on a ctr-less channel `send_failed` matcher (B80/B84).

All **18 SendFailReason values** were traced to core executable emitters, not enum comments. The [classification ledger](2026-10-03-standalone-mobile-home-w8-precheck/failure-classification.json) records **66 source occurrences**, exact sites and contexts. Recommendation:

| Reason | Retention classification | Source basis / limits |
| --- | --- | --- |
| no_pubkey, no_identity, too_large, bad_rng | known not aired **when attributable** | Origination/seal admission in node_mac.cpp:353–396, :777–780; channel seal admission :736–738 has no attributable async handle. |
| joining | known not aired when attributable | node_mac.cpp:120, before new origination admission, typically ctr zero. |
| mobile_no_home | known not aired when attributable | node_mac.cpp:193 / node_hashlocate.cpp:1708, wrapper/admission refusal. |
| unsealable, no_location | known not aired when attributable | Admission before enqueue, e.g. node_mac.cpp:303–322 and node.cpp:1836–1885. Written no-location text does not normally request either location failure. |
| cap, min_interval | known not aired for the existing attributable **send_blocked** path | Channel budget blocks before mint/enqueue at node_channel.cpp:630–644; DM min_interval at node_mac.cpp:1009. Preserve the existing weaker channel-ness/time-window match; these are not a reason to invent a send_failed correlator. |
| no_ack, e2e_ack_timeout | may have aired | ACK absence does not prove non-reception; late E2E ACK is explicitly supported. |
| no_cts | may have aired | ack_timeout_fire resets the same flight for another RTS (node_cascade.cpp:885); later CTS giveup can follow an earlier DATA attempt. |
| gateway_unreachable | may have aired | gateway_doorstep_hold is also called from ack_timeout_fire (:876), then can give up (:918). |
| no_route, queue_full | may have aired / conservative | Deferred/requeued carriers retain identity and potentially prior attempts; the reason alone does not establish first admission. node_cascade.cpp:749, :756, :791, :941. |
| reprovisioned | may have aired | Purge includes the owned PendingTx, with no prior-air restriction (node_channel.cpp:1501–1511). |
| none, any unclassified/new value or producer | may have aired / conservative | No truthful never-air guarantee. Do not turn ignorance into known refusal. |

**Attribution comes before this table.** An unmatched push cannot change the draft's ownership. An actual matched failure without a proven classification counts accepted-final. An absence of any outcome stays accepted-open; it is not an unclassified failure. Channel asynchronous seal failures retain today's bounded unconfirmed result, not an invented precise reason.

**New finding B497:** the reprovisioned enum comment promises “before it aired”, contrary to its in-flight producer. Registered separately, static trace only, no production defect or new on-air reproduction claimed. W7+W8's conservative classification works without editing lib/core. B497 is not folded in.

At `long_fire`, withdraw **only a pending written ordinary request**, unlock its draft and return to the editor after the overlay. The emergency request must still drain first and remain untouched. Saved-phrase ordinary requests keep their existing owed-slot behavior. Never use generic `_req_pending=false` without a kind check, never re-submit the withdrawn text after the alarm, and never infer retained ownership later from `_fail/_refuse`, which emergency outcomes can overwrite. Record the written state's retain/release fact at the attributable event.

## 4. Draft ownership, review storage and returns — Q4/Q6/Q9

D16 already allocates one counted draft[163], ID and lock. Use the independent `_review` carrier for destination binding from WRITE MESSAGE, `_req` for pending execution, and queue the binding whole (U2). Resolve the DM known bit/hash at that entry, not after minutes of editing at DONE: W6's phrase capture currently assigns them at review capture (`send.h:683–684`), and copying that assignment into the written path would rebind the chosen person. Compare subsequent live answers against the entry binding. Preserve the draft-ID sequence when clearing text; do not reset every new draft to an indistinguishable ID. Specify wrap behavior without a numeric sentinel or a second resident counter. No third carrier, second draft or resident text copy is needed. Do not let opening a review overwrite an older owed request. The final brief must prove that a new SEND cannot overwrite occupied ordinary work; either show the production scheduling exclusion or refuse that collision explicitly without adding a queue.

Lock and view ownership are different:

1. Opening a review freezes a new draft ID and disables editing by phase; SEND content-locks it and enters queued/result ownership.
2. Closing the editor display cannot release the draft. The ordinary request drains its **locked model-owned bytes**, even when no editor body is visible.
3. After executor return the content lock can clear, but result ownership prevents another editor/draft from replacing it.
4. Withdrawal/refusal keeps the same bytes and cursor, unlocked, for a fresh review. Acknowledging accepted-open releases them and ends tracking: the declared late-outcome residual remains. Acknowledging accepted-final returns to the originating phrase/person list and releases them.
5. Names never use the content lock, ordinary send carrier or tracker. Their single bounded service request follows W7; emergency and ordinary radio traffic cannot prevent its drain.

The shipped `ui_pump_trackers` (:260–305) closes normal tracking when `!m.compose_open()`. A written result must count as the transaction still open; merely leaving the editor body must not close the normal tracker. Result acknowledgement/pre-emption then closes it exactly once. Preserve NO CONFIRM→DELIVERED while open, ignore a late outcome after acknowledgement, and prevent an ordinary outcome from moving Emergency.

Reuse W6's `review_wrap_line`, `review_page_count`, `review_page_rows` (`model.h:1580–1596`), three rows in the existing union, 20-byte review_header and kDetailPageMs. **Do not route manual text through `ui_review_capture`'s phrase catalog lookup/body copy** (`send.h:679–690`). Introduce a source-kind branch/common projection that borrows the counted draft, projects at most three lines into the frozen union, and leaves Inbox/phrase `_detail_body` ownership unchanged. No full written draft in UiState, UiSnapshot or a stack local.

Written review preserves every byte, spaces included; concatenated lines equal the exact payload; no injected newline. Headers: `TO TEAM 12A1B2C3`; known DM `TO <label ≤7> <HASH8>` (at most 19 cells); unknown `TO T255 UNVERIFIED`. The shared header's current exact-dm comparison must include dm_text. Read full raw names before formatting; retain the known bit for a synthetic known-zero hash; do not sanitize a generated 0xBB marker again.

Action row **` SEND >EDIT     1/2` is exactly 19 cells**; page token columns 16–18, no LOC. Keep `EDIT` selected on entry, blank and long_arm. Short toggles, double selects; time alone advances pages and never queues. Blanking suspends cadence and wake restarts on the same page. Saved phrases keep BACK, their LOC request, capture and list return. Names keep SAVE NAME?, fixed two name rows, WAS, no page token and no LOC.

Context changes while editing/reviewing keep bytes/cursor and refuse the old binding. A written draft is never silently re-bound to a new team or reused peer ID. Recommend DISCARD→reopen WRITE MESSAGE as the explicit way to choose a new destination. The author must state this repair/exit path; no automatic target change on DONE or acknowledgement. A preset-generation change has **no effect** on the written editor/review/request, while still invalidating a phrase selection/review as before. After execution, team/recipient changes do not re-gate that past send.

## 5. Entry rows, positional readers and B444 — Q5

Current row authority is `ComposeRow` and `compose_row_count/kind/slot/text/loc_marker/line` (`model.h:1754–1810`), used by `compose_gesture` (:6035–6095), `draw_compose` (`ui.cpp:2397–2445`) and `send_list_row_override` (:1822). The old `cursor+1==n` is historical B66 prose, not a safe rule to restore.

Required derived orders:

| Context | Rows |
| --- | --- |
| Top-level Send | enabled team phrases → WRITE MESSAGE → MENU |
| Person compose | enabled DM phrases → WRITE MESSAGE → GRANT KEY if offered → back, don't send |

Add a typed **write** action, not a text row. WRITE, GRANT and exit carry no location marker and never yield a sendable preset slot. An empty catalog still gives WRITE+exit (plus offered grant for DM). Recalculate row count/window/cursor bounds, GRANT index and every positional test/control through the shared row authority. K7 grant gating, target hash, confirmation default, effect and returns remain unchanged; only its position moves one. Preserve out-of-range fail-closed behavior.

Tests pinning positions include `ui7-B66` (:2241), `ui10-p3-r1` (:10521), TEAM/Send model fixtures and probe K7/invite/compose rows. Probe R1 (:2016) attacks the action-aware count and R2 (:2023) the action row text; re-anchor their same properties, never retire them as “obsolete”. The full [reader census](2026-10-03-standalone-mobile-home-w8-precheck/symbol-readers.json) includes raw SendReq aggregate construction in tests/mutants: appending a draft ID must not accidentally reinterpret existing initializer values.

**B444 exposure reproduced through the real model:** with team_build=true, a team/key present and my_team_id=0, Home correctly omits SEND TO TEAM. Walking Home's MENU, then three short presses reaches SEND; double enters the channel phrase list. [Five-check witness](2026-10-03-standalone-mobile-home-w8-precheck/reachability.json), compiled against unchanged source, exits 0. `next_screen` (:6225) skips only by team_build; `open_send_list` (:4411) has no key/ID readiness gate. Core `on_command::send_channel` accepts a mobile team with team_id alone (`node.cpp:1778`); `do_send_channel` falls back to static origin if team-local ID is zero (`node_channel.cpp:621–623`). The witness proves UI reachability, not a new RF reproduction; B444 already owns the core defect.

**OWNER RULING REQUESTED before combined dispatch:** Home hiding the item does not close the menu-mode path. Recommended src-only disposition: an explicit execution refusal for **both ordinary channel kinds** until the team-local ID exists, with named panel wording and tests; keep the rail/list navigable and the emergency kind exempt. This is a named safety change to existing phrase execution, so it cannot be smuggled under “phrases unchanged”. Alternatively complete separate W1b first. Hiding rail SEND would revise D1's navigation; changing lib/core inside W7+W8 would change its fence/P6 gate class. A UI refusal does **not** close B444 for non-UI senders. Price any new retained storage; a transient live answer is not permission for a resident field.

**DM editor rail:** recommend SEND for editor, message review and result, even when opened from TEAM→person. Current `ui_nav_slot` already maps DM compose on TEAM to SEND (`chrome.h:341–343`). Preserve the underlying screen/person binding for return, rather than changing Screen just to draw the rail. Name editor/review/result and the Home name prompt box STATUS. Emergency still suppresses all rails first. This implements the reviewed R-4/§5.4 rule; author should explicitly include DM, not request a new policy ruling for the existing meaning.

## 6. Combined measured allocation — Q7/Q12

[Measurement script/results](2026-10-03-standalone-mobile-home-w8-precheck/layout-measure.json) use **struct-only scratch copies**, the stock ABI helper's real env flags, real compilers and matching nm for all three ABIs. They are neither an implementation nor linked-RAM/flash evidence. UiState remains align 8; SendReq and Draft align 4. No feature body or new send kind implementation is added by this measurement.

| Shape | UiState N/M/G | UiModel N/M/G | SendReq N/M/G | UiModel + separate frozen UiState delta N/M/G |
| --- | --- | --- | --- | --- |
| Current base | 568 / 560 / 560 | 1016 / 1000 / 1000 | 16 / 16 / 16 | 0 / 0 / 0 |
| D16 exactly | 576 / 568 / 568 | 1208 / 1184 / 1184 | 16 / 16 / 16 | +200 / +192 / +192 |
| D16 + u32 draft_id in both carriers | 576 / 568 / 568 | 1216 / 1192 / 1192 | 20 / 20 / 20 | **+208 / +200 / +200** |
| Above + model-private u8 written state + three u8 reason/refusal/code fields | 576 / 568 / 568 | 1216 / 1200 / 1200 | 20 / 20 / 20 | **+208 / +208 / +208** |

N/M/G = native / heltec_mobile / gateway ABI. The current **UiSnapshot 1368, UiChrome 20, SendLive 12 and SendTracker 16** remain unchanged in these shapes. The D16 draft is 176 B, editor descriptor 10 B, NameOrigin 1 B. Two visible editor rows alias the existing 60-B page union; WAS uses review_header. Count embedded UiState once inside UiModel, then the frame's separate UiState once. Stock headless gateway has no OLED resident instance; its measured type is not an assertion of linked gateway RAM growth.

**Owner allocation requested:** minimum **+8 B beyond D16 on every ABI**, total **+200 B board / +208 B host objects**. Recommendation: preserve D16's ten-byte descriptor and encode caller/request phase and the retain/release result there, with explicit typed meanings, instead of adding a separate outcome record. This is a proposed integration contract; the coder must prove its actual complete fields match the granted measurement, not infer that padding authorizes another field. The measured explicit four-byte outcome shape costs **+16 B beyond D16 on boards, +8 host**, total +208 all ABIs; native hides the additional board quantum. If the author chooses that shape, request that figure instead.

No new text array, tracker, send line or binding carrier is needed. A label for the editor can reuse the mutually exclusive review_header/page storage; never derive an 8-column header from an already clipped six-column TEAM label. W7's name-request seam remains descriptor-driven. Any additional owned state found in implementation returns as STOP-1 for measurement. Re-pin the actual types and measure linked RAM/alignment and flash on the ruled pair; object pricing is not a blanket linked-section allowance.

## 7. W7+r2.25 consistency and paired contract — Q8

The author's r2.25 refinements otherwise combine correctly with W8:

- Prompt is a Home sub-view, STATUS rail, separate NameOrigin; admit before prompt, re-admit on SKIP and saved-name continuation. No half-open provisioning behind the prompt.
- WAS is captured **when name review opens**, from that tick's own-name snapshot, then frozen; it is not the earlier W7 recommendation to capture at editor opening. A concurrent console rename cannot alter draft/review bytes; W0 still builds the eventual durable record from current live identity.
- All five RenameResults are fail-loud and explicit; name service runs outside the normal tracker busy gate, once. Unchanged means NAME SAVED with zero writes.
- Window uses two adjacent 19-cell grid rows, including the next empty row at a multiple-of-19 cursor; descriptor and visible bytes are frozen together. E4 BACK returns E3/DEL. EDIT/not-saved returns group 1 with cursor kept.
- Written callers have cap 163 and SEND rail; name cap 32 and STATUS rail. Name flow never locks a send draft. Saved phrases never open the editor or acquire a draft.

**Author clarifications before brief freeze:**

1. §5.4 says returns “after an alarm” land group 1, but §5.5 preserves an active E1–E4 editor and ring position. Qualify group-1 return for review/result cancellation back to the editor; an alarm/cancel over an already active editor must have one explicit ring rule. Recommendation: keep active editor ring/position, matching the event table; reset to group 1 only when review/result returns to editor. This is a text/transition consistency question, not authority to change the safety priority.
2. §5.3's note-clearing press must mean the next **eligible lit, non-overlay short/double**. First wake is consumed, long gestures retain emergency priority, and overlay presses cannot perform hidden edits or sends.
3. Freeze the safe exit from a broken team/recipient binding, the typed stale-draft refusal wording, the ordinary-slot collision behavior, and the B444 disposition. No implicit rebinding or discarded draft.
4. Correct the already-landed SendReq fields in §7.4 and count both carriers in §11.1; D16 is not the new draft_id carrier grant.

Recommend **one feature brief and one final freeze/gate**, as D17 requires. Internally: first settle/capture baseline and pure editor/API/layout tests; then name integration and origins; then WRITE rows/written request/state integration; finally real-renderer/executor controls and the complete chain. These are implementation checkpoints, not independently released half-products. No refactor/file move of existing pager/input/service code mixed in (C1); no wire/NV/core changes. B480 adds a direct action-deletion model mutant; B481 is an honest case-title/comment correction, preserving the neighboring genuine ordinary-before-alarm test.

## 8. Interruption and proof matrix — Q9

Join W7's mapped gesture/tick/overlay/FrameGate authorities. Required distinguishing cases:

| Surface | Required proof |
| --- | --- |
| All callers | All E1–E4/ring boundaries; blank/wake consumes first press; receive counters/wake only; long_arm/cancel/fire; review safe selection; no hidden action under overlay; frozen pages while live input/name changes. |
| Written entry | Both entry routes, no phrases, grant offered/not offered, maximum list, first/last/out-of-range rows; real team/peer binding; known-zero marked synthetic. |
| Review and context | 17/18/163/164 bytes, long words, exact concatenation and ≤6 pages; no-send timer; EDIT default/reset; no LOC; team change/leave, hash remap/disappearance, duplicate labels, generation inert for text. |
| Queued | Press ignored; draft still locked after editor display closes; execution reads identical bytes exactly once; stale-ID/unlocked refusal is zero core submissions; long_fire withdraws only written ordinary work. |
| Refused | Each synchronous gate/parser/executor arm and attributable never-air reason; acknowledgement/alarm retain identical text/cursor; no automatic resend. |
| Accepted-open | ctr-zero, queued handle and aired states; acknowledgement/alarm closes tracking and releases draft, later outcome ignored; no false “failed/not sent” claim. |
| Accepted-final | Every terminal classification; late ACK upgrade while open; release/return; may-air failures never produce a never-air claim. |
| Slot isolation | Emergency before ordinary; bounded retry/expiry ordering; saved phrase stays owed; written withdrawal never removes an alarm; unrelated pushes and normal outcomes cannot alter Emergency. |
| Name/prompt | W7's exact origin/result table, save once, all five answers, console race, setup gate re-asked, gateway rename without team prompt; no SendKind or lock use. |

Native tests belong in the new editor test plus existing model/send/chrome tests; parser checks can live in the existing send test using the real public parser. Do not add an unnecessary new production TU. Tests must distinguish the rule, not mirror an implementation. B480 attacks **omission of `if (preset_generation_moved(s)) close_compose();`**, not just its predicate; preserve saved-phrase closure and prove the text exception. B481's case at model test :1165 only proves overlay absorption; accurately retitle/comment it. The neighboring :1149 case genuinely proves two-slot priority.

The real firmware-UI probe compiles firmware_ui.cpp, records the executor line and scripts the W7 rename service. Extend its recorded fake for all RenameResults/call counts, then prove new written commands/body/count/no-l, zero submissions on cancellation/refusal, renderer rails/19-cell rows/cursor underline, and service/drain reachability. The current draw_hline fake only counts calls; W7 already requires x/y/width recording to prove the cursor geometry. Raw InputFsm samples must reach the editor. Real W0 identity/router arms remain the witness for actual rename persistence; a scripted rename answer is not that proof.

The board-UI runner's existing W7 pins the ui_have_fix/executor seam; W8 bans composer text in the device TU. Preserve these properties. Its drain/wake/FrameGate wiring checks still apply. Add named wiring identities/negative controls for any new name-request drain and draft-source wiring not reached by old predicates, and update expected.tsv plus accounting census for **exactly those additions**, never just a lower total. Core state/lifetime decisions need native/real-UI controls; a textual board check alone is insufficient.

## 9. Reader/fence and mutation ledger — Q9, D5/D6/P7

Recommended product fence: new `src/firmware_ui_editor.h`, `src/firmware_ui_model.h`, `src/firmware_ui_send.h`, `src/firmware_ui_chrome.h`, `src/firmware_ui.cpp`. Recommended tests: new `test/test_firmware_ui_editor.cpp`, existing model/send/chrome test files; add existing input/status/team test files **only if their actual reader expectations move**, explicitly fenced. No config service, device_nv, fw_main, command TU, console grammar, lib/core or simulator edit.

Tool fence: firmware-UI probe_main.cpp/run.sh; board-UI run.sh/expected.tsv/accounting census if new declarations require it; mutation harness (new uieditor, new written mutations, B480, exact re-anchors and native pin); stock ABI changed pins and its supplemental new-type measurement. Audit any dependent tools tests before adding them to the fence. No change to shared fake numeric formatting, transcript builder or accounting outcome contracts.

[Mutation census](2026-10-03-standalone-mobile-home-w8-precheck/mutation-census.json) is **AST-only**; the executable harness was never imported or run. Full entries carry their patterns/replacements/source positions and match census for disposition. [Recommended union](2026-10-03-standalone-mobile-home-w8-precheck/recommended-union.json):

| Selector | Existing batteries / entries |
| --- | --- |
| (a), product files planned to change | model 264; chrome 48; uisend 26; sliceCbudget 1; sliceCsend 1; w4aident 9; w4bhome 26 — **375** |
| (b), caller/dependency acceptance surface | uistatus 19; uiteam 20; uiinvite 32; uiprov 45; uijoin 26; uipresets 39; config 32; consoleline 12 — **225** |
| Union before additions | **15 batteries / 600 entries**, plus new uieditor/written entries and B480's +1 |

The W7 union was 587; promoting uisend from dependency to changed-source adds no duplicate, sliceCsend adds 1, and consoleline adds 12 as the existing console-line validity/limit dependency (not as a proof of the new composer or parser). No existing battery targets firmware_ui.cpp. No existing input-header battery was found in TARGET_SRC. uieditor's new target and configured minimum must be named by the author. Do not use 600 as a frozen final floor once entries are added; derive counts from the candidate AST and direct native binary.

Candidate dependencies devicenv, teamkeyring and provservice were audited but are excluded from this recommendation: their implementations/record shapes are unchanged; W0's real service and W6's catalog remain read-only, and config/uiprov/uijoin plus new integration tests cover the new callers. The author can add them with a reason; do not silently pretend selector (a) and (b) are the same.

Anchors needing disposition include model W6-M01/M04/M06–M09/M17/M18, S03, Y06 and B480's new action deletion; chrome X17–X21/X24/X48; send U14/U15 and W6-S01–S11; compose/grant row tests and probe R1/R2/C139/C141, W3 row/detail controls, W6 review checks. The complete source statements, not this abbreviated list, are in the census and [send probe candidates](2026-10-03-standalone-mobile-home-w8-precheck/send-probe-controls.json). Preserve each mutant's attacked property and match count; new kinds must not make a phrase control vacuous. Every new mutation must be memory-safe, compile, and fail an intended assertion; crashes/build failures/unknown exits are UNUSABLE under the repaired harness, not RED. B492's bounded F07 and the eight sibling tuples stay untouched. New caller/tests can legitimately move their assertion counts; derive and explain those counts rather than inheriting 13 or the earlier eight exact counts as this new feature's gate.

Expectation-change ledger for the author: new WRITE rows/count/windows and K7 shift; new editor/review/result/prompt renders and rails; My device CHANGE NAME row; unnamed Home JOIN/CREATE prompt; new caller-sensitive result returns; B480/B481. Phrase bytes, location flags, word wrap/cadence, BACK default, K7 semantics, Inbox read-watermark/navigation, emergency geometry/budget/priority and the console outputs remain acceptance properties. Classify every changed assertion/control; “same file changed” is not a justification for altering a baseline.

## 10. Fresh baselines and gate recommendation — Q9

Every result below was run independently on this exact entry tree. Commands, exit statuses and durations are in [runs.json](2026-10-03-standalone-mobile-home-w8-precheck/runs.json); [baseline summary](2026-10-03-standalone-mobile-home-w8-precheck/baseline-summary.json); raw local logs are hashed in [raw-files.json](2026-10-03-standalone-mobile-home-w8-precheck/raw-files.json).

| Instrument | Fresh result |
| --- | --- |
| pio native, then direct native binary | **3059 cases / 199638 assertions / 0 failed / 0 skipped** |
| Stock ABI | **290 checks; 9/9 controls RED; 0 unusable** |
| Firmware-UI default, all arms/controls | **591 / 1059 / 591** checks, **240 controls**, independently reconciled one declared label/contract to one observed verdict |
| Board-UI default | **592/592 identities exactly once**, exit 0: canvas 124/110; trait 14; missing-trait 12; structural 23; wiring 60 plus 186 controls; negctl 60/3 |
| Fresh simulator configure/build and full anchored corpus | **36/36**, all 14 per-stream fields identical to W7's manifest |
| s18 | **269517 events; MD5 `32afbf11e43b4bf9d0bd470ad502ba0a`**, agreeing with current simulation/BASELINE.md |
| Fresh lus | SHA-256 **`e304147d99ae166fb815e6a2ef06c5ff905579b68add3b0ba142d7031e26caa2`**, identical to W7 |
| Command inventory | **197 rows**, stock --check passes |

[Corpus manifest](2026-10-03-standalone-mobile-home-w8-precheck/corpus-manifest.json) supplies the whole before-state; do not inherit a single keystone as proof of every stream. Predicted corpus remains byte-identical because this fence is src-only, with no simulator/wire/lib change. Board-UI's former W49/W51/W54 failures are closed; its **default accounting run** is the gate, not --no-neg.

Combined coder gate: source/inventory/pins first; native build + direct binary; fresh simulator/full anchored corpus and per-stream comparison; stock and supplemental ABI with all controls; real firmware-UI and board-UI default probes/accounting; unchanged W0 identity/router and repaired transcript as useful dependency safety nets (the author must explicitly include them or justify exclusion); inventory/authority/literal/ownership readers affected by the fence; full tools discovery; six-env warning census; repeatable ruled board measurement pair; derived mutation union, then one freeze and preservation proof. Sequence mutation/build/measurement work exclusively; no coder edits during QA gates.

Independent gate per P6: native + full corpus + ruled boards/RAM/flash + all **changed-source** batteries (including new uieditor) + affected probes/ABI/accounting/reader checks. The coder runs the full chosen dependency union and census; QA may add dependency batteries with a stated reason. Re-run any tool regression suites and full discovery if runners/tests change. No inherited coder figures. The final receipt requires the exact `PIN re-synced? YES — <derivation>` line, final candidate/brief/preparation/simulator hashes and separate explanations for alignment/flash movement.

Not run by this pre-check: mutation batteries, full tools discovery, warning census, inbox/identity/transcript dependency runs, board size pairs, stack builds or metal. These are not claimed passed here. Board baselines/final pairs belong to the coder under `.pio-measure/`; no board allocation is inferred from an ordinary build or a type measurement.

## 11. Metal-plan residue — Q11 / M2

Do not edit the metal plan at pre-check. The combined brief/QA landing should add **EDIT-01** and scoped additions to existing UI-15/UI-16/UI-17/POWER-01; current rows remain OWED. Host controls prove bytes/transitions, not the real button, cursor underline, glyph legibility, radio delivery or sleep behavior.

- **EDIT-01:** on H1 and V, an uncoached user opens My device→CHANGE NAME, enters `STAN`, exercises DEL and a correction, sees `NAME           4/32`, full `SAVE NAME?`, safe ` SAVE >EDIT`, WAS, then explicit SAVE→`NAME SAVED`. Verify the live name by `whoami`; a same-name save's zero-write proof remains automated. Then WRITE MESSAGE and type **`RETURN TO BASE NOW`** (18 bytes), correcting one byte: the grid shows all bytes and the next-cell cursor; header `TO TEAM      18/163` or the bounded DM label. Review shows `RETURN TO BASE NOW`, safe ` SEND >EDIT     1/1`, **no LOC**. Record gesture/time/accidental-double/accidental-arm counts. The owner can send only on the controlled bench.
- **UI-15:** send that written team body, then a written DM. Compare receiver body bytes, not command echo; neither contains location even with a valid fix. Observe `SENDING...`/`QUEUED`/`SENT, waiting`, then channel `PICKED UP` or `NO RELAY HEARD`; DM `DELIVERED to` only on matching ACK. A genuine refused send returns the exact draft for fresh review with no automatic resend. A missing ACK is **not** that refusal case. Add the ruled pre-DAD UI refusal line when settled; don't invent it in this ledger.
- **UI-16/UI-17:** while editing, reviewing and awaiting outcome, deliberate emergency fires under the existing real-air/budget controls. No hidden ordinary send/ack under overlay; name/draft return agrees with the caller/state table. Queued-written withdrawal is primarily an automated scheduling proof; a physical test may claim it only if that precise state was actually observed, not inferred from a long button hold. Saved phrase's owed behavior stays distinct.
- **POWER-01:** long typing across the two-row window, blank/wake and received-message wake; first press wakes only, cursor/text survive, no panel re-home/hidden ring action, physical underline stays within the two visible rows. After idle blank, actual sleep resumes with the existing wake-arm diagnostics. Follow the headless-boot/no-console-input method so the host-present latch does not invalidate sleep observations.

## 12. Author/owner handoff

Ready facts: one editor, one ruled draft and one shared page union are sufficient; no new console verb, NV, wire or simulator input is needed. Minimum SendReq growth is measured for both carrier instances. Current baseline instruments are green and corpus predicted inert.

Before author freezes the combined brief: obtain the **additional +8 B minimum carrier allocation** (or choose/price the explicit outcome shape); settle **B444's menu-mode exposure** without disguising a phrase-policy change; resolve §5.4/§5.5's alarm landing wording, note priority, binding-repair exit, stale-draft wording and ordinary-slot collision proof. DM SEND rail follows the already reviewed body rule. D16/D17 and B480/B481 are not reopened. New B497 is separate; no other finding was registered.

Evidence seal: [SHA256SUMS](2026-10-03-standalone-mobile-home-w8-precheck/SHA256SUMS). Replay evidence scripts only from an unsealed scratch copy; they create measurement outputs, not implementations. Existing author/coder evidence stays untouched.
