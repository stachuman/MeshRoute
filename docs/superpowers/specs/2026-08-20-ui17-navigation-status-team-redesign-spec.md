<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §UI-17 — Heltec navigation, STATUS body and TEAM rows · BOUNDED IMPLEMENTATION SPEC · 2026-08-20

**Status: SPEC FOR REVIEW. No code, no tests, no edits to any other file were made by the dispatch that produced
it.** It converts `docs/superpowers/plans/2026-08-20-status-screen-redesign-note.md` (the owner-approved design
direction) into gateable slices.

**Precedence, and it is the note's own:**
1. the NOTE — owner-approved; its §3 navigation contract, §4 STATUS body, §5 TEAM semantics and the 24x24
   reservation are settled and are not re-opened here;
2. `docs/superpowers/specs/2026-07-31-onboard-oled-ui-design.md` — the as-built design doc with its correction
   history;
3. **the code, which outranks both as evidence** (V1): every fact below is cited at `file:line` and was re-checked
   on 2026-08-20 against the working tree. Line numbers are hints — relocate by symbol (V2).

**Standing constraints for every slice below.**
- ⛔ **WIRE BYTES: ZERO.** No frame, field, flag or `wire_version` is touched by any slice. Nothing here needs one:
  the location data already exists in an authenticated RAM cache (§3.4). ⚠ If an implementer finds a slice that
  appears to need wire, **STOP** — M3 permits the change but C4 requires it to be **its own slice and its own
  commit** with an attribution note, and it is then a different spec.
- ⛔ **`lib/` CARRIES NO CODE CHANGE FROM ANY SLICE.** No slice re-runs the corpus; the s18 md5 is inert by
  construction (the simulator compiles `lib/core`, not `src/`). If a slice ever needs a `lib/core` change it
  becomes a corpus-rerunning slice and must say so in its own brief.
  ⓘ **CORRECTED IN PLACE 2026-08-20 (S8), and the withdrawn absolute is kept visible** — this read *"`lib/` IS
  UNTOUCHED BY EVERY SLICE"*. **S8 repairs exactly ONE DRIFTED COMMENT LINE** in `lib/core/command.h:321` (V1:
  fix the comments you touch — S8 is what makes `Push::enc` load-bearing for the panel, and that line documents
  it wrongly). ⛔ Comment only: no statement, no field, no byte, ⛔ no wire — the corpus stays inert **by
  construction**, and S8 re-runs and reports the s18 md5 anyway (D2). ⛔ No other slice may widen this.
- **Board gate: TWO envs, and this spec deliberately does not name a third.** Per the standing owner ruling the
  dispatch brief picks exactly two; ⛔ a brief may never pre-authorise more. (The OLED envs in the tree are
  `heltec_v3`, `heltec_mobile` and `gateway_heltec` — `platformio.ini:194/423/467`; the last is the `OLED=1 /
  TEAM=0 / N_LAYERS=2` shape that keeps the non-team arms honest.)
- **C1 throughout:** each slice is a feature XOR a refactor. Two refactors are *identified* below and both are
  deliberately deferred to slices of their own (§2.2 note j and §1.9 F-2, collected at F-6).

---

## 1. INVENTORY — every state, screen, transition and timer the redesign touches

### 1.1 The as-built top-level model (verified)

| screen | `list_len` today | `short` today | `double` today | conformant with note §3.1? |
|---|---|---|---|---|
| STATUS | 1 (`firmware_ui_model.h:2700`) | next screen, one press | nothing (`activate` has no `status` arm, `:2019-2067`) | ✅ already |
| TEAM | `s.team_shown` (`:2689`) | **walks rows** (`advance_or_next`, `:1996-2005`) | opens DM compose (`:2020-2029`) | ⛔ MIGRATION |
| INBOX | `s.inbox_shown` (`:2690`) | **walks rows** | opens the detail modal (`:2032-2048`) | ⛔ MIGRATION |
| SEND | 1 (`:2700`) | next screen, one press | opens channel compose (`:2030-2031`) | ✅ already |
| SETTINGS | 1 while closed (`:2695`), else `settings_row_list(s).n` | one press while closed | opens the menu (`:2049-2066`) | ✅ already ([[B232]]) |

⇒ **the migration set is exactly TEAM and INBOX.** STATUS, SEND and SETTINGS already satisfy §3.1. The cycle
itself (`next_screen`, `:2702-2710`) is unchanged: STATUS → TEAM → INBOX → SEND → SETTINGS, skipping the
team-gated slots without moving the remaining rail slots (`ui_chrome`, `firmware_ui_chrome.h:517-525`).

### 1.2 TEAM — passive ↔ interactive migration

- **Today:** arriving on TEAM leaves `_st.cursor` at 0 and `note_team_cursor` (`:2534-2541`) immediately records
  row 0 as the pick, so the panel shows `>` beside a teammate the operator never chose, and `short` walks the
  roster before the screen advances (`advance_or_next`, `:1996-1998`).
- **After:** TEAM lands PASSIVE — a preview list with a **blank marker** and no recorded pick; `short` passes the
  screen in one press; `double` enters the interactive selector; the selector's last row is `BACK`; `BACK`
  returns to the passive form of TEAM (⛔ never to the next screen).
- **Preserved verbatim:** §B64's identity-tracked cursor (`_team_sel_id` / `sync_team_cursor`, `:2516-2531`), its
  loud refusal (`UiState::team_pick_gone`, `:1068`), the suppressed `>` on refusal
  (`draw_team_screen`, `firmware_ui.cpp:1002/1017/1028`) and the empty-roster carve-out (`:2024-2025`).
- **New:** while PASSIVE nothing is picked ⇒ `note_team_cursor` must not run, and `activate` must not queue.

### 1.3 INBOX — the same migration, one plane over

- **Today:** identical shape — `short` walks, `double` opens the detail modal.
- **After:** passive preview (blank marker) → `double` enters → `short` advances → `double` opens the detail
  modal (unchanged) → the modal's own `back` returns to the **interactive** INBOX list it was opened from → the
  list's `BACK` row returns to the passive INBOX.
- **Preserved verbatim:** the `(kind, seq)` identity cursor (`:2556-2580`), the neighbour capture
  (`note_inbox_neighbour`, `:2586-2594`), `inbox_pick_gone` + `MESSAGE GONE` (`firmware_ui.cpp:1083`), the
  [[B231]] newest-at-top publication (`InboxRowBudget::publish`, `firmware_ui_model.h:846-852`) and the [[B233]]
  post-erase repaint latch (`_inbox_rows_stale`, `:1343`).
- ⚠ **The BACK row and the reserved refusal row compete for the same five baselines.** `draw_inbox_screen`
  already spends row 0 on the `INBOX n/N` header and reserves one more row for `MESSAGE GONE`
  (`firmware_ui.cpp:1052/1060/1083`) ⇒ with a `BACK` row the interactive list shows **at most two** message rows
  in the worst case. That is a real cost of the contract and is stated rather than discovered on glass; the
  scrolling window (`list_first`, `:741-745`) already handles it.

### 1.4 SEND, the compose sub-view and the existing detail modal, against the parent-screen `BACK` rule

| view | its exit today | returns to | §3.2-conformant? |
|---|---|---|---|
| compose list (DM or channel) | the derived last row `back, don't send` (`compose_gesture`, `:2675`; text `kDmTexts`/`kChannelTexts`, `:648-649`) | its parent screen — `close_compose` (`:2687`) never touches `_st.screen` | ✅ |
| compose RESULT phase | either press acknowledges (`:2668-2671`) | same parent | ✅ (kept — §9 R-5) |
| inbox detail modal | `>back`, selected on entry (`detail_gesture`, `:2613`) | INBOX (`close_detail`, `:2624-2634`) | ✅ |
| detail terminal `MESSAGE GONE` | either press (`:2600-2603`) | INBOX | ✅ (kept — §9 R-5) |
| SETTINGS menu | the `BACK` row / walking off the last row → the CLOSED entry view (`:2003`, `close_settings_menu`) | SETTINGS itself | ✅ ([[B232]]) |
| PROVISION menu | `BACK` → `close_provisioning` (`:2317`) | the SETTINGS menu it was opened from | ✅ |
| join confirm | `BACK` → `join_select`, ⛔ not the menu (`:221`) | its own parent | ✅ |
| prov `create_result` / `join_result` | either press (`:226`) | the PROVISION menu | ✅ (kept — §9 R-5) |

⇒ **no existing exit walks into another top-level screen.** The one thing the audit did find is a *display*
mismatch, not a navigation one: a DM compose opened from TEAM boxes the **SEND** rail slot by ruled design
(`ui_nav_slot`, `firmware_ui_chrome.h:270…`; pinned by the probe's *"P14d a compose modal opened from TEAM
selects SEND, not TEAM"*), while its `back` returns to TEAM.

★★★ **OWNER-RULED 2026-08-20 (§9 R-4 and R-5), and both rulings land HERE.** **R-4: keep §5.2 — the rail
names the BODY**, so a DM compose opened from TEAM keeps the SEND slot boxed, and **`BACK` independently returns
to TEAM**; the two are separate facts, both correct, and ⛔ the probe's P14d expectation is unchanged and must
not be re-pinned. **R-5: keep the shipped terminal acknowledgement** — either press acknowledges a result and
returns to its parent; ⛔ **no slice adds a selectable `BACK` row to a result screen** (it would cost a press on
every outcome, an emergency outcome included).

### 1.5 §3.3 AUDIT — every timer and timeout, what it does TODAY, and what §3.3 requires

⛔ **The note forbids assuming the current screens comply. Two of them do not.**

| # | timer | site | what it does today | §3.3 verdict |
|---|---|---|---|---|
| T1 | `kBlankMs` = 15 s panel blank | `firmware_ui_model.h:1348-1357` | sets `blanked`, marks dirty, calls `ConfigService::on_blank()` (a draft-preserving no-op by construction) | ✅ a power action; preserves everything |
| T2 | the consumed wake press | `:1204` | the first press after blanking is swallowed and only un-blanks | ✅ §3.3's "the consumed wake press restores the same interaction" |
| T3 | **compose auto-exit at `kBlankMs`** | `:1287-1289` (`close_compose()`) | **DISCARDS the open compose modal and its cursor** at the same deadline as the blank ⇒ the interaction is already gone when the operator wakes the panel | ⛔ **VIOLATES §3.3** |
| T4 | **detail-modal auto-exit at `kBlankMs`** | `:1293` (`close_detail()`) | **DISCARDS the open record's detail view and its selected action** | ⛔ **VIOLATES §3.3** |
| T5 | detail page cycle, `kDetailPageMs` = 2 s | `:1302-1308` | advances/cycles the page, marks dirty, ⛔ deliberately does NOT touch `_last_input_ms` | ✅ a display cadence; discards nothing |
| T6 | emergency `kEmgHoldMs` / `kCancelledMs` / `kArmToFireMs` / blocked backoff | `:865-870`, `tick_emergency` `:2780-2802` | the alarm's own machine | ✅ safety overlay — note §2's stated exception |
| T7 | `STILL JOINING` at `kJoinStillMs` | `:1318-1321` | an edge-triggered WORD change; ⛔ no state moves, nothing is cancelled | ✅ |
| T8 | `FrameGate` 2 Hz throttle + MAC-idle gate | `:2848-2896` | paint policy only | ✅ |
| T9 | battery sample, 30 s | `firmware_ui.cpp:339-347` | no UI state | ✅ |
| T10 | transient notes (`settings_note`, `team_pick_gone`, `inbox_pick_gone`) | `:1165`, `:1068`, `:1075` | cleared by the next **navigation press**, not by a timer | ✅ |

**T3 and T4 are not accidents.** Both are documented and reasoned: design §3.2.1 (*"Auto-exit after `kBlankMs` of
no input … A modal that can outlive the user's attention is a modal that eventually sends the wrong thing"*) and
design §3.5 / `firmware_ui_model.h:1290-1292` (*"ordinary modal timeout returns to INBOX without deleting"*).
⇒ §3.3 **reverses two deliberate safety behaviours**, with a genuine trade in both directions.

★★★ **OWNER-RULED 2026-08-20 (reported form): §3.3 WINS, FOR BOTH.** The compose sub-view **and** the inbox
detail modal are **PRESERVED across blank/wake**; the wake press stays consumed (`:1204`); the **emergency
transitions remain the safety exception** (`long_arm` still closes the detail modal, `long_fire` still closes
compose — `:2720-2731`, `:2775`). ⇒ **T3 and T4 are DELETED by S2, which is now a RESOLVED slice, not an
owner-gated one.** ⛔ No other slice may touch them.
⛔ **WITHDRAWN WORDING, KEPT VISIBLE:** this block previously ended *"see **OQ-1**. Slice S2 exists to pay that
reversal and is GATED on the ruling"*, and §9 carried OQ-1 as an open question. It is ruled; see §9 R-1.
⚠ The design doc's §3.2.1 and §3.5 now carry superseded sentences ⇒ a correction-in-place is owed there, drafted
by S2 (⛔ the design doc is QG-owned and is not edited by the slice).

**The NEW interactive list state (S1) is specified §3.3-conformant from the start**: it survives blanking, the
wake press is consumed as it is today, and it is retired only by `BACK`, by leaving the screen, or by the
emergency overlay (see S1 pin 6). ⛔ It gets no timeout of its own.

### 1.6 What today's screens render, so nothing is lost silently

- **STATUS body** (`draw_status_screen`, `firmware_ui.cpp:971-988`), five rows: `team %08lx` · `me T%u` ·
  `DM %u, newest %s` · `CH %u, newest %s` · then `RESTART NEEDED` **or** `batt %ldmV` / `batt --`.
  Their disposition under the redesign is inventoried line by line in §2.3.
- **TEAM row** (`:1016-1018`): `%c%-9.9s %4.4s %uh` — marker, 9-column label, 4-column age, hop count.
  The note's §5.2 format has no hops column; see §3.2 and **F-1**.
- **INBOX rows** (`:1078-1079`) and the detail modal (`:1093-1113`) are unchanged by this spec except for the
  marker suppression and the new `BACK` row.

### 1.7 As-built facts the note relies on — all re-verified 2026-08-20

- panel 128x64; strip `y=0..8`, rule at `y=9` (`kBarRuleY`, `firmware_ui.cpp:669`); rail `x=0..9`, `y=10..59`
  (`kRailX/kRailW/kRailY0`, `:881-891`, with the `== 59` static_assert at `:889`);
- body origin `x=12`, 116 px, **19 columns**, baselines 19/29/39/49/59 (`kBodyX`/`kBodyCols`/`kBodyPx` + the two
  static_asserts, `:703-709`; `kBodyY0`/`kBodyDy`/`kBodyRows`, `:670-672`);
- the strip carries mail / home age / team count / key / battery (`kStrip`, `:782-788`);
- `TeamRow` carries `id`, `last_heard_s`, `score_q4`, `hops`, `label` (`firmware_ui_model.h:663-666`), filled at
  `firmware_ui.cpp:574-588` from the **primary** route candidate;
- the label resolver is `team_key_of_id → peer_name_find → 0x<hash> → bare id`
  (`label_for_team_id`/`label_from_hash`, `firmware_ui.cpp:350-359`);
- **the authenticated peer-location cache exists**: `Node::peer_loc_set` / `peer_loc_find`
  (`lib/core/node_hashlocate.cpp:407` / `:432`, declared `lib/core/node.h:1104-1110`), fed from a sealed DM
  (`lib/core/node_mac_rx.cpp:1789`, `PeerLocSrc::peer`) and from an attributed encrypted team post
  (`lib/core/node_channel.cpp:373`, `PeerLocSrc::team`). **Both call sites do their own authentication test
  before calling** (`node.h:1091-1103`) — the cache holds no unauthenticated position. Neither function is
  feature-gated (both sit above the first `#if MR_FEAT_TEAM` in that file, `:477`).
- ⚠ `peer_loc_find` reports **`age_s = 0xFFFFFFFF` on a backwards clock** (`node_hashlocate.cpp:441`) —
  deliberately "maximally stale", which the freshness rule in §3.4 inherits for free.

### 1.8 The gesture matrix this spec is measured against

| state | `short` | `double` | `long_arm` / `long_fire` / `long_cancel` |
|---|---|---|---|
| any passive top-level screen | next available screen | enter, if the screen has an interaction (STATUS: no-op) | emergency, pre-empts everything (`:1201-1203`) |
| TEAM interactive | next row, ending on `BACK` (⛔ never off-screen) | activate the row: member → DM compose · `BACK` → passive TEAM | as above; the list is NOT closed (S1 pin 6) |
| INBOX interactive | next row, ending on `BACK` | member → detail modal · `BACK` → passive INBOX | as above; **the interactive list is PRESERVED** — `long_arm` closes the **detail modal, and only when that modal is open** (`:2720-2731`) |
| blanked | consumed, un-blanks only (`:1204`) | consumed, un-blanks only | emergency still fires from dark (`:1201`) |
| emergency overlay up | absorbed; dismisses only a PRESENTED retained outcome (`:1227-1230`) | **absorbed entirely** (§R2, `:1246`) | re-arm / cancel |

### 1.9 As-built facts that CONTRADICT or COMPLETE the note — reported, not silently reconciled

- **F-1 · the TEAM row loses HOPS, and the note does not say so.** Today's row ends `%uh`
  (`firmware_ui.cpp:1016-1018`) and design §3.3 requires *"last-heard age, signal quality and hops"*. The note's
  §5.2 format has no column for either. ⇒ the design doc needs a correction-in-place (drafted by S4, ⛔ not
  edited by it).
- **F-2 · "signal quality" has never been on the panel.** `TeamRow::score_q4` is written
  (`firmware_ui.cpp:580`) and read by nothing in `src/`, `test/` or `tools/` — a written-but-unread field of the
  same class the model header already flags for `_last_try_ms` (`firmware_ui_model.h:95-97`). The note
  describes the snapshot as carrying "team-local id, label, route age and hops" and omits it.
- **F-3 · §3.3 reverses two documented, reasoned behaviours** (T3/T4 in §1.5) rather than merely auditing them.
  The note asks for the audit but does not state that the answer is a reversal ⇒ **RULED, §9 R-1: §3.3 wins, S2
  deletes both timeouts.**
- **F-4 · the note's `CFG UNSAVED` both re-spells a shipped string and reverses a ruled removal.** The lexeme is
  `CFG* UNSAVED` / `CFG! RELOAD`, owned by `cfg_marker_text` (`firmware_ui_model.h:600-603`), and §CHROME-4 /
  design §6 removed that text from STATUS on purpose ⇒ **RULED, §9 R-3: no configuration text returns to
  STATUS.**
- **F-5 · `TEAM 3D9348A5` fits only WITHOUT the shipped `0x` prefix.** `ui_fmt_team_id_full` is `0x%08lX`
  (`firmware_ui_chrome.h:233-237`) ⇒ `TEAM 0x3D9348A5` is 15 columns against the 14 the note's own geometry
  leaves at `x=40`. The note's claim that the full eight-hex id "fits beside the logo" is true; reusing the
  existing helper to render it is not ⇒ §2.2 note j.
- **F-6 · two refactors are identified and both are deferred** (C1): unifying the team-id spellings (F-5) and
  deleting `score_q4` (F-2). Each is its own slice; ⛔ neither rides a feature slice here.
- **F-7 · design §10.3 still places the TEAM distance column in *Phase B on V4*, behind a UART NMEA driver, and
  calls peer-location propagation *"the open dependency … not settled"*.** The note's §2 correction resolves the
  propagation half (the AB4 cache exists and is authenticated) but also moves the distance column onto V3 in
  slice 4 without saying that it reverses §10.3 and design §3.3's *"Phase B adds a distance column on V4"*. ⇒ a
  design-doc correction-in-place is owed (drafted by S5, ⛔ not edited by it). ⓘ This is a doc-vs-note
  contradiction; the CODE agrees with the note (the cache is live and unconditional, §1.7).
- **F-8 · a lit TEAM screen's ages already go stale today.** The only repaint invalidation in the tree compares
  the **chrome** projection (`ui_chrome_invalidate`, `firmware_ui_chrome.h:449`), which carries no per-row body
  token, and `FrameGate::step` answers `idle` on a clean model. ⇒ the note's *"bounded clock-driven repaint"*
  gate is not a new requirement on new code — it closes a pre-existing gap (S4).
- **F-9 · RESOLVED BY SCOPE, NOT BY REVERSAL — the §8.15 stranger rule SURVIVES BY CONSTRUCTION (owner-ruled
  2026-08-20).** §R1/[[B109]] (owner-ruled 2026-08-05) is *"what wakes the panel is a REPLY, not team chatter"*,
  implemented as the placement of `_st.blanked = false` **past both guards** inside `on_reply`
  (`firmware_ui_model.h:1796-1812`) and stated on the bench at §8.15 as *"a REPLY lights a DARK panel; **a
  stranger's post does not**"*. S8's scope was ruled so that both rules hold at once: the wake fires for **a DM
  delivered to us** (`msg_recv`, sealed or not — it is *addressed to us*) and for **a channel post that arrived
  SEALED and was opened with OUR team channel key** (`channel_recv` **gated on `pu.enc == true`**).
  ⇒ **a stranger's cleartext post still does not light the panel**, so ⛔ **nothing is withdrawn and no bench
  §8.15 correction is owed.** ⛔ **WITHDRAWN WORDING, KEPT VISIBLE:** this finding read *"S8 REVERSES HALF OF A
  STANDING OWNER RULING … the earlier ruling's second half is WITHDRAWN for `msg_recv`/`channel_recv` … A bench
  §8.15 correction-in-place is owed, drafted by S8"*. Both the reversal and that drafted bench edit are
  **DROPPED**.
  ★ **MEASURED, NOT ARGUED — the three delivery cases** (`lib/core/node_channel.cpp:405-419`): an
  **undecryptable / foreign** post is not `readable`, so it is **not inboxed and NO `channel_recv` is emitted at
  all** — no push, therefore no wake, by construction and not by our gate; a **sealed post we opened** delivers
  with `pu.enc = (enc != 0)` **true** (`:415`) ⇒ wakes; a **cleartext post on a matching channel id** delivers
  with `enc` **false** ⇒ ⛔ must **not** wake. The DM side is `pu.enc = crypted_ok`
  (`lib/core/node_mac_rx.cpp:1758`) and is **not** gated — a DM is addressed to us either way.
  ⚠ **A DRIFTED COMMENT S8 MUST FIX (V1):** `lib/core/command.h:321` still documents the field as
  *"channel_recv -> false (cleartext today)"*, which `node_channel.cpp:415` falsified. ⛔ It is a **comment
  only** — no behaviour, no wire — and S8 is the slice that makes the field load-bearing for the UI, so S8
  repairs it in place. ⚠ It is the **one line under `lib/` any slice in this spec touches**; it changes no
  emitted byte, so the corpus stays inert — but the slice must say so and re-run the s18 md5 anyway.
- **F-10 · lighting the panel also SUPPRESSES LIGHT SLEEP, which is a bigger cost than "the panel is lit".**
  `ui_allows_sleep` is `m.state().blanked && !in.active() && !g.frame_open()` (`firmware_ui_model.h:2948`) ⇒
  while the panel is awake the node does **not** light-sleep at all. A quiet node is unaffected (S8's sleep pin),
  but a chatty channel costs a full attention window of **no sleep** per message. ⇒ **RULED (§9 R-6): ACCEPTED
  FOR V1, no rate limiter** — with F-9's `enc` scope the traffic is the operator's own team's sealed messages
  and DMs addressed to them. Measured at §7.8 step 4 so a future ruling has data.

---

## 2. THE STATUS BODY

### 2.1 Geometry (note §4.1/§4.2 — normative, restated with its probe consequences)

- reserved mark slot: `x = 12..35`, `y = 12..35`; placeholder `mrui::draw_rect(12, 12, 24, 24)` — the primitive
  exists and forwards to `u8g2.drawFrame` (`variants/heltec_v3/board_ui.h:65`, `board_ui.cpp:318`);
- final asset: native **24x24** monochrome XBM = 72 bytes (`stride_of(24) == 3`, `firmware_ui_icons.h:56-60`);
  minimum accepted 16x16 = 32 bytes, centred at `x=16..31`, `y=16..31`; ⛔ **no runtime scaling**;
- the asset uses the repository's ONE byte-order contract — row-major, **LSB-first**, 1 bpp, rows padded to whole
  bytes (`firmware_ui_icons.h:12-25`) — and is drawn through `mrui::draw_bitmap` (`board_ui.cpp:315`, a direct
  `drawXBM`, which handles a 3-byte stride exactly as the 11-px battery outline already does);
- **rows 0-2 draw text at `x = 40`** ⇒ 88 px ⇒ **14 columns**; **rows 3-4 draw at `x = 12`** ⇒ 19 columns;
- the mark is drawn **only** in the STATUS body; the strip and rail are untouched.

⛔⛔ **THIS BREAKS TWO SHIPPED PROBE ASSERTIONS AND BOTH MUST BE RE-POINTED, NOT WEAKENED:**
1. `P14f every ordinary body draw starts at x=12 (the one kBodyX)` — implemented as
   `body_text_min_x() == kBodyXExpected` over a five-screen walk (`tools/probe_firmware_ui/probe_main.cpp:2077`,
   `kBodyXExpected` at `:817`). ⇒ it must become a **per-screen expected origin SET** — STATUS `{12, 40}`,
   every other screen `{12}` — **plus** two positive terms so it can still fail: STATUS drew at least one row at
   `x = 40` **and** at least one at `x = 12`, and no `x = 40` row exceeds **14 columns** (88 px).
   ⛔ **Never relax it to `min_x >= 12`**: that is the instrument-that-cannot-fail shape this project has
   registered twenty-one times (see the `P14f` block's own comment at `:2071-2074`).
2. `P14a the frame draws exactly 5 + 5 glyphs and 1 frame` — `bitmaps_on_page(0) == 11`
   (`probe_main.cpp:1968`), and `bitmaps_on_page` counts **every non-text record, `draw_rect` included**
   (`:226-229`, `:446`). The STATUS placeholder/asset adds one ⇒ the expectation becomes screen-dependent
   (STATUS 12, others 11), with the extra record pinned at its own `x=12, y=12, w=24, h=24`.
3. The probe's row reader `body_row(row)` reads text at `x = 12` (`:823`) ⇒ a sibling `status_row(row)` at
   `x = 40` is needed for rows 0-2. (`text_at` is exact-coordinate, so a wrong x reads `nullptr` — the failure
   direction is loud.)

### 2.2 Contents — one row at a time, with its data source and its substitutions

Every string below is composed in a **pure** unit and never in `src/firmware_ui.cpp`, per the §B115 rule that
this file cluster states nine times: *a string built in `firmware_ui.cpp` is a string no automated gate can read*
(`firmware_ui_model.h:102-104`). ⇒ **new pure unit `src/firmware_ui_status.h`**, with its own mutation battery
target (§5). The renderer calls it and draws.

| row | x | ≤cols | normal | substitution / priority | source |
|---|---|---|---|---|---|
| 0 | 40 | 14 | `TEAM 3D9348A5` (13) | `team_id == 0` ⇒ `NO TEAM` (7) | `UiSnapshot::team_id` ← `g_node.config().team_id` (`firmware_ui.cpp:591`) |
| 1 | 40 | 14 | `ME T220` (≤7) | `team_id == 0` ⇒ **empty**; `team_id != 0 && my_team_id == 0` ⇒ `ME NO ID` (8) | `UiSnapshot::my_team_id` ← `g_node.team_local_id()` (`:590`) |
| 2 | 40 | 14 | `4 KNOWN` (≤8 at `9+`) | `!team_build` ⇒ **empty**; `team_id != 0 && !team_key_present` ⇒ `NO TEAM KEY` (11) | `team_total` ← `rt_team_count()` (`:570-571`); `team_key_present` ← `team_channel_key_present()` (`:612`); `team_build` (`:553`) |
| 3 | 12 | 19 | `3 NEW / HOME 42s` (≤18) | `!mobile_build` ⇒ the `NEW` half alone; `mobile_build && !home_confirmed_ever` ⇒ `HOME --` | `unread_dm`+`unread_ch` (`:548`), `home_confirm_age_ms`/`home_confirmed_ever` (`:602/609`), `mobile_build` (`:598`) |
| 4 | 12 | 19 | `52.123,21.456` (≤16) | `reboot_required` ⇒ `RESTART NEEDED` (14) **owns the row**; else no fix ⇒ `NO LOCATION` (11) | `SettingsView::reboot` (`freeze_settings`, `:657`); `NodeConfig::lat_e7/lon_e7` |

**The decisions inside that table, stated as decisions:**

a. **Row 1 is blank, not a second `NO TEAM`.** The note says the ME row shows `NO TEAM` with no team; rendering
   it on both rows would spend two of five body rows on one fact. ⇒ row 0 owns the token, row 1 says nothing.
   ⚠ REPORTED, NOT INVENTED.
b. **`ME NO ID` is a case the note does not cover and the code makes reachable**: `Node::team_local_id()`
   documents `0` as *"not team-DAD'd"* (`node.h`, and `firmware_ui_model.h:2513-2515` relies on exactly that), so
   an in-team node before DAD would otherwise render `ME T0` — a plausible id, which §4's own rule forbids.
   New lexeme, listed in §8 for ruling.
c. **`NO TEAM KEY` outranks the count on row 2** because it is the actionable half: without the team **content**
   key the routes are real but the posts are unreadable, and `team_key_present` is exactly that fact and no other
   (`node.h`'s own note; `UiSnapshot::team_key_present`, `firmware_ui_model.h:749-758`).
d. ★★★ **THE WORD IS `KNOWN`, NOT `HEARD` — QA/OWNER-RULED 2026-08-20, AND IT IS A HONESTY FIX RATHER THAN A
   PREFERENCE.** ⛔ **WITHDRAWN WORDING, KEPT VISIBLE:** the note's §4.2 and this spec's first draft both said
   `4 HEARD`. The count is `rt_team_count()` (`firmware_ui.cpp:570`) — **route evidence**, exactly the quantity
   §3.3 of this spec forbids describing with "seen"/"heard" language, because a multihop route says somebody
   else heard that teammate. ⇒ `%s KNOWN` → `4 KNOWN` / `9+ KNOWN`. ⛔ It must not say `MEMBERS` either (note
   §4.2: the route table is not an authoritative membership roster), and ⛔ not `HEARD`.
   The token reuses **`ui_fmt_team`** (`firmware_ui_chrome.h:140`) — the strip's own already-clamped 0..9/`9+`
   value — so the two surfaces cannot disagree (U1).
   ⓘ **`NO TEAM KEY` outranking the count is ACCEPTED AS-IS** (QA, 2026-08-20); note c stands unchanged.
e. **The unread saturation token is `ui_fmt_mail`'s** (`firmware_ui_chrome.h:103`, `kMailMax = 99` ⇒ `99+`), not
   `kUnreadCap`'s 999 (`firmware_ui_model.h:777`). Reason: the STATUS row states the same COMBINED count the
   strip's envelope draws, and one fact must have one token (U1). Widest expansion `99+ NEW / HOME 59m` = **18**
   of 19 — proven at the format, per §7.1 rule 5.
f. **`HOME --`, not `HOME UNKNOWN`.** `ui_fmt_home_age` (`firmware_ui_chrome.h:118`) is design §4.2's ruled
   table, is bounded to 3 columns by construction and already renders `--` for "never confirmed";
   `99+ NEW / HOME UNKNOWN` is 22 columns and would clip. The note's `HOME UNKNOWN` lexeme is therefore listed
   **WITHDRAWN** in §8, kept visible.
   ⛔ And on a build with **no mobile plane** the HOME half is **omitted entirely** rather than rendered `--`:
   design §4.2's distinction between "not applicable" and "never confirmed" is already law for the strip icon
   (`ui_chrome`, `firmware_ui_chrome.h:481-489`), and this row must not contradict it. `gateway_heltec` is a real
   `MOBILE=0` build.
g. **Row 4 priority is `RESTART NEEDED` > coordinates > `NO LOCATION`, deterministically and never both.** This
   PRESERVES the shipped behaviour (`firmware_ui.cpp:983-984`, design §3.6.5 — a saved-but-reboot-required state
   stays visible until the reboot) and pays the note's "define deterministic priority" requirement. The
   coordinates remain readable on the console (`cfg`) while the condition stands.
h. **`have a fix` is `ui_have_fix`'s predicate, reused not re-derived** (`firmware_ui.cpp:430-433`): the core
   itself refuses a located send when both coordinates are zero, so any other definition would disagree with the
   thing that actually rejects us. ⇒ `(0,0)` renders `NO LOCATION`, never a plausible fix (note §4.2).
i. **The coordinate token TRUNCATES toward zero to three decimals** (`lat_e7 / 10000`), it does not round: the
   panel must never render a position more precise or more advanced than the stored one. Widest expansion
   `-89.123,-179.123` = **16** of 19. Signs handled explicitly; a native case pins each of the four sign
   quadrants and the `-0.000` boundary.
j. ⚠ **`TEAM %08lX` is a THIRD spelling of the team id and it is declared, not accidental.** The existing pure
   token is `ui_fmt_team_id_full` = `0x%08lX` (`firmware_ui_chrome.h:233-237`) — **15 columns with the `TEAM `
   prefix, i.e. one past the 14 available at `x=40`**, so it cannot be reused here as-is; the shipped STATUS row
   already omits the prefix (`team %08lx`, `firmware_ui.cpp:973`) and this spec only uppercases it (the
   fingerprint's own uppercase rule, `firmware_ui_chrome.h:217-218`). ⇒ the honest cure is to hoist the eight
   digits into one `ui_fmt_team_id_hex8` that `ui_fmt_team_id_full` then composes — **that is a refactor of a
   shipped, probe-pinned function and C1 forbids it riding a feature slice.** ⇒ **S3 adds the STATUS token with
   an in-source note naming the relation and the future unification slice** (the [[B224]] declared-duplication
   idiom: declare it, reason about it, edit the two together, and close it in a refactor slice of its own).

### 2.3 What today's STATUS body loses — inventoried, so nothing goes silently

| today | after | where the fact survives |
|---|---|---|
| `DM %u, newest %s` / `CH %u, newest %s` (two rows, per-kind unread + per-kind newest age) | one combined `3 NEW` | the per-kind split survives on the INBOX screen's empty state (`firmware_ui.cpp:1043-1048`) and per row on the populated list (`:1078`); the combined count is also the strip's envelope. ⚠ **The per-kind *newest age* is not shown anywhere else when the list is non-empty** — stated, not glossed. |
| `batt %ldmV` (exact millivolts) | dropped — the note's five rows leave no room | the strip's `4.1V` decivolt token (`ui_fmt_batt`, `firmware_ui_chrome.h:170`) and the console. ⚠ The exact mV reading leaves the panel. |
| `batt --` | dropped | the strip renders `--` for the same state, by the same rule. |
| `RESTART NEEDED` | **kept**, row 4, priority (g) | unchanged (`kCfgRestartText`, `firmware_ui_model.h:604`). |
| `team %08lx` / `me T%u` | kept, uppercased, rows 0/1 | — |

⇒ **two facts genuinely leave the panel** (exact battery mV; the per-kind newest-message age while the inbox is
non-empty). Both remain on the console. Recorded here because the note's §4 defines the whole body and therefore
implies the drop without naming it.

★★★ **OWNER-ACCEPTED 2026-08-20 — THREE REMOVALS, APPROVED:** the TEAM row's **hops** (§3.2 / F-1), the STATUS
body's **exact battery millivolts**, and the **per-kind newest-message age while the inbox is populated**. The
approved row contents take priority; **the battery stays in the strip** as its volts token; each fact remains
available elsewhere (console `status` / `cfg`, the strip, the INBOX rows). ⇒ ⛔ **no slice may re-add a row to
restore one of them**, and ⛔ none of the three may be described as an oversight.
⚠ **THE DESIGN DOC MUST RECORD THEM.** §3.3 currently promises the TEAM row shows *"last-heard age, signal
quality and hops"* and STATUS's own paragraph offers *"battery detail"*. The correction text is **DRAFTED in the
slice report** (S3 for the STATUS pair, S4 for hops) — ⛔ the design doc is QG-owned and is not edited by any
slice here.

---

## 3. TEAM ROWS

### 3.1 Identity — reuse, do not fork

The label is **already** resolved by the ruled order and clamped at the snapshot
(`label_for_team_id`, `firmware_ui.cpp:353-359`, into `TeamRow::label[kLabelCap + 1]`,
`firmware_ui_model.h:665`). ⛔ No slice re-implements it, and ⛔ no slice selects or sends by the displayed text
(`_team_sel_id` remains the only send identity, `:2029`). Shortening is **presentation only** (§5.1).

⚠ The new format narrows the visible label from **9** columns to **6** (`%-9.9s` → `%-6.6s`). That is a real
legibility change on a screen whose whole purpose is naming people; it is the note's own ruled format and is
listed in §8 so the owner sees the width alongside the tokens it pays for.

### 3.2 The row, its width proof, and what it drops

Note §5.2's format, adopted verbatim: `%c%-6.6s %3s %4s %2s`
⇒ `1 + 6 + 1 + 3 + 1 + 4 + 1 + 2 = 19` of 19. Passive rows use a **blank** marker; the interactive selection uses
`>`; the `BACK` row renders as the existing action-row idiom `%c%s` (`BACK` = 1 + 4 of 19).

⛔ **HOPS LEAVE THE ROW.** Today's row ends `%uh` (`firmware_ui.cpp:1016-1018`) and design §3.3 promises *"plus
last-heard age, signal quality and hops"*. The new columns are distance and bearing; there is no room for hops at
19 columns and the note does not mention the loss. ⇒ **F-1** (§1.9), and the design doc will need a
correction-in-place (drafted by the implementing slice, ⛔ never edited by it — the design doc is QG-owned).

⚠ **`TeamRow::score_q4` is written and read by NOTHING.** It is filled at `firmware_ui.cpp:580` and no renderer,
model or test reads it (grepped across `src/`, `test/`, `tools/`). The "signal quality" design §3.3 promises has
therefore never been on the panel. This spec does **not** delete it (that is a refactor — C1) and does not render
it; the slice that touches `TeamRow` states the fact in-source per the
[[meshroute-mark-done-vs-missing-in-code]] rule and names the deletion as its own future slice.

### 3.3 Age — the honest word

`TeamRow::last_heard_s` is computed from the **primary route candidate's** `last_seen_ms`
(`firmware_ui.cpp:578-586`) ⇒ on a multihop path it is **route-evidence age**, not proof this device heard that
teammate. ⇒ every string, comment and doc line uses **route age / known age**; ⛔ never "seen", "online",
"connected" or "in contact" (design §4.2's ban on "connected" is a rule about surfaces, and this is a surface).
The token is `fmt_age` → `ui_fmt_home_age` (`firmware_ui.cpp:381-385`), bounded to 3 columns by construction
(`kAgeTokenCap`) — which is exactly the `%3s` the format reserves, and `UINT32_MAX` still renders `--`.

**Ordering is NOT changed by any slice in this spec** — **owner-ruled 2026-08-20 to KEEP the current order**
(§9 R-2; note §5.3). ⛔ The navigation slice in particular
must not re-sort as a side effect: the rows keep `rt_team_at(i)` order (`firmware_ui.cpp:574-575`).

### 3.4 Distance and bearing — over the cache that already exists

**Show distance and bearing only when ALL FOUR hold** (note §5.4):
1. our own position is configured — `ui_have_fix()`'s predicate (`firmware_ui.cpp:430-433`);
2. the team-local id resolves to a **non-zero** peer hash — `Node::team_key_of_id(id, hash)` (`node.h:219`), the
   same first step `label_for_team_id` already takes ⇒ one resolution per row, reused (U1);
3. `Node::peer_loc_find(hash, lat, lon, age_s, src)` returns true (`node_hashlocate.cpp:432`);
4. `age_s <= kPeerLocMaxAgeS` where **`kPeerLocMaxAgeS = 600`** — the owner-approved ten minutes, ONE named
   constant with boundary cases at 599 / 600 / 601 **and** at `0xFFFFFFFF` (the backwards-clock value
   `peer_loc_find` returns by design, `node_hashlocate.cpp:441`).

Otherwise **both columns are blank** — ⛔ never `0m`, never a retained old coordinate wearing current-looking
units, never an estimate.

⛔⛔ **RENDERING TEAM CREATES NO TRAFFIC OF ANY KIND.** `peer_loc_find` is `const` and touches no timer, no
queue and no radio; nothing in any slice calls a location request, a `reqpubkey`, a DM or a broadcast. A probe
phase asserts zero TX-queue depth change and zero radio starts across a full TEAM walk (the counters
`probe_firmware_ui` already keeps for P2b). Any continuously-refreshed teammate position is a **separate future
specification** covering airtime, privacy, authentication, freshness and user control (note §6).

**The maths lives in a new pure unit `src/firmware_ui_geo.h`** with its own battery target, and four rules are
load-bearing rather than stylistic:
- ⛔ **the longitude difference must NOT be computed in `int32_t`**: `lon_e7` spans ±1 800 000 000, so a
  difference reaches 3.6e9 and **overflows** — compute in `int64_t` (or normalise first). The latitude
  difference (±1.8e9) fits, and is still computed the same way so one rule covers both;
- **antimeridian normalisation**: fold `dlon_e7` into ±1 800 000 000 before any conversion; a native case pins a
  pair straddling ±180° (without it, two neighbours read as half a planet apart);
- differences are taken as **integers first, then converted to float** — converting the absolute e7 values first
  loses the difference in `float`'s 24-bit mantissa (catastrophic cancellation for nearby peers, which is the
  only case that matters);
- equirectangular approximation: `dy_m = dlat_e7 * 1.1131949e-2`, `dx_m = dlon_e7 * 1.1131949e-2 *
  cosf(mid_lat)`, `d = sqrtf(dx² + dy²)`. It is accurate far beyond a four-column token at hiking ranges; the
  error at continental distances is irrelevant to a `12k`-shaped display.
- **the bearing needs no `atan2`**: the eight-way octant is decided by comparing `|dx|` and `|dy|` against
  `tan(22.5°) ≈ 0.41421356` — multiplies and comparisons only. Geographic bearing from OUR coordinate to the
  peer's last reported one; ⛔ not movement, ⛔ not relative to how the device is held, ⛔ cardinal TEXT rather
  than an arrow (the panel has no compass and must not imply one).
- ★★★ **COINCIDENT POINTS — RULED 2026-08-20: distance `0m`, direction BLANK.** ⛔ **WITHDRAWN WORDING, KEPT
  VISIBLE:** the first draft's S5 pin asked for *"`0m` with a defined bearing token"*. A zero-length vector has
  **no bearing**, so any token there would be **fabricated** — the exact class this screen exists to avoid, and
  the same rule as a cache miss blanking rather than reading `0m`. ⇒ the helper returns a valid distance and
  **`has_bearing = false`**; the direction column is blank. It is a state of its own, ⛔ never octant 0 (`N`).
  ⓘ It is reachable in practice: two nodes at one campsite, three decimals apart, and both e7 deltas zero.
- ⚠ `cosf`/`sqrtf` pull float libm; `<cmath>` is already used in `src/` (`firmware_join_profiles.h:48`) and in
  `lib/core`. **The per-env flash delta is MEASURED in the slice, not assumed** (§6). If it proves unacceptable
  an integer cosine table is the fallback — a measured decision, taken then, not now.

**Snapshot projection.** ★★★ **CORRECTED IN PLACE 2026-08-21 — THE OWN-LOCATION HALF WAS PULLED FORWARD INTO
S3 AND IS DONE (QG-authorised).** ⛔ **WITHDRAWN WORDING, KEPT VISIBLE:** this paragraph was headed *"Snapshot
projection (slice S5)"* and ended *"…and `peer_loc_valid` (`bool`), **plus `UiSnapshot` gains `own_lat_e7` /
`own_lon_e7` / `own_fix`**"*, i.e. it assigned all six fields to S5.
★ **WHY THEY MOVED, RECORDED AS THE QG BLOCKER IT WAS:** S3's row 4 renderer read `g_node.config()` **live, once
per OLED page**, so the coordinate line could **TEAR MID-FRAME** across the eight page transfers a frame spans —
the §5 freeze contract, broken by the very row this spec added. ⇒ the remedy is to **freeze the location into
the snapshot**, and a remedy cannot wait for a later slice. **`own_lat_e7` / `own_lon_e7` / `own_fix` therefore
landed in S3 and are DONE.**
⇒ **What is still S5's, and it is the whole of the geo work:** `TeamRow` gains `peer_lat_e7`, `peer_lon_e7`
(`int32_t`), `peer_loc_age_s` (`uint32_t`, **verbatim from the accessor's out-param** — the
`home_confirm_age_ms` precedent at `firmware_ui_model.h:759-768`: no cast, no clamp, no re-derivation at the
publish site) and `peer_loc_valid` (`bool`); the projection over `peer_loc_find`; and the freshness/distance/
bearing maths. The **decisions** — freshness, formatting, blanking — all live in the pure units where the native
suite drives them; `build_snapshot` only copies (§B115).

---

## 4. SLICING — C1-safe, independently gateable

Each slice: **scope · files · pins · mutation classes · probe/battery impact · pure-vs-renderer-vs-device.**
Every slice ends green and uncommitted; ⛔ the user commits (D4).

### S1 — Navigation consistency: TEAM and INBOX passive ↔ interactive
*(the note's slice 1; the proven [[B232]] pattern, applied twice)*

- **Scope.** ONE new pure state `enum class ListView : uint8_t { passive = 0, interactive }` and ONE `UiState`
  field, because only the current screen can be entered and leaving must reset it. ⛔ It is deliberately **not**
  folded into `Settings` (that enum is SETTINGS's own richer four-arm state and folding is a refactor of shipped
  code — C1); ⛔ and it is not a second authority: a `default`-less helper maps a `Screen` to *"is this screen
  entered"*, reading `Settings` for SETTINGS and `ListView` for TEAM/INBOX, so one predicate answers it.
  - `list_len` becomes: passive TEAM/INBOX ⇒ **1**; interactive ⇒ `shown + 1` (the `BACK` row).
  - the `BACK` row is resolved by a `default`-less pure `list_row_kind(cursor, shown) -> {member, back}` — ⛔
    never a bare `cursor == shown` at a call site (§B66: position is not an identity), and shared by both
    screens (U1).
  - enter/close primitives mirror `open_settings_menu` / `close_settings_menu` (`firmware_ui_model.h:2115-2120`)
    and re-establish the arm and cursor 0. ★★★ **THE TWO PRIMITIVES DIFFER ON THE PICK, AND THE ENTER SIDE IS A
    CORRECTION** (QG-accepted deviation 1, 2026-08-21): **ENTER ESTABLISHES THE PICK AS ROW 0 BY IDENTITY**
    (the `note_*_cursor` write side, on the row the cursor now rests on); **CLOSE invalidates it** (a passive
    screen holds no pick, §1.2/§1.3). ⛔ **WITHDRAWN WORDING, KEPT VISIBLE:** this bullet read *"and
    re-establish the same three facts: the arm, cursor 0, and the pick **invalidated**"* — i.e. it specified the
    enter side as an invalidate too.
    ★ **WHY THE DEVIATION IS SAFER, RECORDED AS QG ACCEPTED IT:** entering selects row 0 **by identity**, which
    is what lets the very next `double` act safely; a literal invalidate would make **the first `double` after
    entering REFUSE** (there is no pick to activate), and the repair — re-noting the pick at activation time —
    is precisely what **§B64 forbids** (`firmware_ui_model.h:2516-2531`: an announced-lost pick is DROPPED and
    must never be silently re-noted). ⇒ establish it once, at the entry, on a row the operator is looking at.
  - the leave reset is a **pure** `list_view_reset_on_leave(...)` forwarded to from both leave paths — the
    [[B223]] extraction rule, for the fifth time in this arc: a guard written only where it is currently
    reachable is a guard no mutation can redden.
  - `note_team_cursor` / `note_inbox_cursor` run **only while interactive**; a passive screen records no pick,
    so `activate` on a passive screen cannot queue anything.
  - ★★★ **IN `activate`, THE REFUSAL OUTRANKS THE `BACK` ROW** (QG-accepted deviation 2, 2026-08-21). A
    vanished selection must be refused **before** the same cursor index may be read as `BACK`: a roster that
    SHRANK can leave the lost pick's index sitting exactly on the `BACK` index, and a `BACK`-first order would
    turn *"the teammate you chose is gone"* into a silent, successful-looking exit. ⇒ order the arms
    **refusal → row kind → member/BACK**, on **both** screens. ⓘ This is `activate`'s existing §B64/§UI-7D
    refusal (`:2020-2028`, `:2041-2045`) keeping its priority as the list grows a row, not a new refusal.
  - ★★★ **COMING TO REST ON `BACK` RETIRES THE REFUSAL** (QG-accepted deviation 3, 2026-08-21): when a
    navigation press leaves the cursor on the `BACK` row, `team_pick_gone` / `inbox_pick_gone` is cleared, on
    both screens. ⛔ Without it the previous bullet builds a **dead end**: with the refusal standing, every
    `double` refuses — **including the one on `BACK`** — and the operator cannot leave the list at all.
    ⓘ **The two rules do not contradict each other, and the seam is WHICH PRESS:** the retirement happens on the
    `short` that comes to rest on `BACK` (the write side, beside `note_*_cursor`); the priority applies inside
    `activate` while a refusal is **still standing** — i.e. when the pick vanished under a cursor that merely
    happens to coincide with the `BACK` index, without the operator having walked there.
- **Files.** `src/firmware_ui_model.h` (pure). `src/firmware_ui.cpp` (renderer: marker `>` only while
  interactive; the `BACK` row drawn as the last list row; the reserved refusal rows keep their places).
- **Pins.** (1) TEAM lands passive: **no `>` anywhere** and `short` reaches the next screen in ONE press.
  (2) `double` enters; `short` walks; the last row is `BACK`; a further `short` from `BACK` returns to the FIRST
  row — ⛔ it never leaves the screen and never wraps into an action. (3) `double` on `BACK` returns to the
  passive form of the SAME screen; one further `short` then passes it. (4) §B64/§UI-7D identity refusals survive
  unchanged from the interactive list (a teammate/record that vanished still REFUSES and still suppresses the
  marker). (5) Both screens: an empty roster/list still offers `BACK` and still leaves. (6) **Blank/wake
  retention:** blanking with the list interactive, then one press (consumed) ⇒ still interactive, same identity
  selected. (7) `long_arm` from an interactive list arms the emergency and **does not close the list** (nothing
  can send from it and arming is cancellable — §B101's own argument); `long_arm` still closes the detail modal.
  (8) INBOX detail `back` returns to the **interactive** list, not to the passive form.
  ★ The three QG-accepted deviations, pinned as behaviour on **both** screens: (9) **the first `double` after
  entering ACTIVATES** — it opens the DM compose / the detail modal for row 0 and ⛔ never refuses (deviation 1);
  (10) **a vanished pick whose index coincides with the `BACK` row REFUSES** — the panel says so, ⛔ nothing is
  activated and the list is ⛔ not exited (deviation 2); (11) **one `short` onto `BACK` clears the refusal, and
  the `double` there then LEAVES to the passive form** — ⛔ the list can never become a dead end (deviation 3).
- **Mutations (`--target=model`).** Each RED at match count 1: land-interactive-on-arrival; `list_len` returning
  `shown` while passive; the `BACK` row mapped positionally to a member; `BACK` leaving the screen instead of
  closing the view; the leave reset dropped (mutate the pure helper's own assignment, [[B223]]); the marker
  suppression inverted; `note_*_cursor` running while passive.
  ★★ **PLUS THE THREE ACCEPTED DEVIATIONS, AND EACH MUST REDDEN FOR *BOTH* SCREENS** (QG-required, 2026-08-21):
  **(a)** the entry's **row-0 identity establishment** dropped (⇒ the first `double` after entering refuses);
  **(b)** the **refusal-over-`BACK` priority** inverted (⇒ a vanished pick on the `BACK` index exits silently);
  **(c)** the **refusal retirement on `BACK`** dropped (⇒ the dead-end list).
  ⓘ **EITHER REMEDY SHAPE SATISFIES THIS SPEC, and the choice is the implementer's:** a **direct mutation per
  screen** (six entries), **or** — where the decision is hoisted into **ONE shared pure helper** that both
  screens forward to — **a single mutation of that helper**, which then reddens both branches at once. ⛔ What
  is *not* acceptable is a mutation that reddens TEAM while INBOX's duplicated branch goes unmeasured: that is
  the half-covered-duplicate shape this project keeps paying for (U1). QG offered the hoist explicitly, and it
  is preferred wherever it removes the duplication rather than adding a layer over it.
  ★ **SYMMETRIC COVERAGE OF THE DUPLICATED INBOX BRANCHES IS OWED IN THE SAME WAY**, and the two the review
  named are: the **passive `list_len`** arm and the **`BACK` activation** arm. ⇒ every TEAM entry above has an
  INBOX twin, by a second mutation or by a once-mutated shared helper.
- **Probe (`tools/probe_firmware_ui/run.sh`, both arms).** ⚠ **Every phase that reaches an INBOX row changes its
  press prefix** (P6a-P6g all assume `short` walks the list); `walk_to_slot` (`probe_main.cpp:803`) still works
  because it walks by the boxed rail slot, but the row walks inside INBOX now need a `double` first. Add: a
  passive-screen negative control (a `short` on TEAM/INBOX moves the RAIL, never a row) and the contained-`BACK`
  control. Controls that remove the new guards must redden.
- **Battery/probe hygiene.** [[B217]]: read the CURRENT `BASE_CASES`/`BASE_ASSERTS` pin from
  `tools/probe_ui_model_mutations.py` before and after, re-pin with the derivation in place when the native
  counts move, and **confirm each battery actually RAN** (an aborted battery prints no RED lines, which reads
  exactly like a battery with nothing to find). ⚠ Never run a probe and a battery concurrently.
- **Split.** Pure model: the state, the row-kind resolver, both cursors' gating, the reset. Renderer: markers +
  the `BACK` row. Device: nothing.

### S2 — §3.3 retention conformance for the EXISTING compose and detail modals · **RESOLVED (§9 R-1)**

⛔ **WITHDRAWN HEADING, KEPT VISIBLE:** this slice read *"· ⛔ OWNER-GATED (OQ-1)"* and carried a *"Only if the
owner rules for §3.3"* clause. The owner ruled on 2026-08-20 (§9 R-1): **§3.3 wins for both modals** ⇒ the slice
is dispatchable as written.

- **Scope.** Exactly two lines' worth of behaviour: **delete** the compose auto-exit
  (`firmware_ui_model.h:1287-1289`) and the detail auto-exit (`:1293`). Nothing else. It reverses design §3.2.1
  and §3.5, so it lands alone, with the design-doc correction text **drafted in the slice report** (⛔ the design
  doc is QG-owned and must not be edited by the slice).
- **Pins.** (1) The compose sub-view survives `kBlankMs` with the panel dark; the wake press is consumed and the
  SAME list, cursor and phase are on the panel; ⛔ nothing was sent. (2) The detail modal survives the same way
  with the SAME record, page and selected action; ⛔ nothing was deleted; ⛔ the selected action is still `back`
  if that is where it was left. (3) **The emergency exception is intact:** `long_fire` still closes compose and
  `long_arm` still closes the detail modal (`:2720-2731`, `:2775`) — a hidden Delete may not survive under an
  alarm overlay. (4) The detail page cadence still does not postpone the blank (`:1297-1299`), so a long body
  still cycles for exactly one attention window and then the panel blanks **with the modal retained**. (5) The
  blank itself still fires on time — ⛔ deleting the two timeouts must not extend `kBlankMs`.
- **Mutations (`--target=model`).** Re-instating either auto-exit (RED at match count 1, one per modal); the
  emergency close removed (a Delete surviving under the overlay); the blank deadline made conditional on a modal
  being open.
- **Battery `model`; probe P6 (add a blank-and-wake step over an open modal, then confirm the store is
  untouched).** Pure model only. Renderer: nothing. Device: nothing.

### S3 — STATUS body: the 24x24 slot, the narrowed geometry and the five facts

- **Scope.** The placeholder mark, the two-column geometry, and the row table of §2.2 — as pure formatters plus
  a renderer that only places them. ⛔ No final artwork (S6). ⛔ No new configuration authority: the SETTINGS
  rail badge stays the persistent unsaved/conflict indicator (design §6).
- **Files.** NEW pure `src/firmware_ui_status.h` (every row's string, every substitution, the row-4 priority);
  `src/firmware_ui.cpp` (`draw_status_screen` becomes placement + one `draw_rect`); ⛔ `firmware_ui_chrome.h` is
  read (its four formatters are reused) and **not modified**.
- **Pins.** (1) Every row's exact bytes for the normal case and for EVERY substitution, asserted natively.
  (2) The width proof per format: rows 0-2 ≤ 14 columns, rows 3-4 ≤ 19. (3) Row-4 priority: with
  `reboot_required` **and** a valid fix, the row reads `RESTART NEEDED` and the coordinates are absent; with
  neither, `NO LOCATION`. (4) `(0,0)` renders `NO LOCATION`, never `0.000,0.000`. (5) The mark is drawn at
  exactly `12,12,24,24` and no body text intrudes into `x < 40` on rows 0-2. (6) `gateway_heltec`'s shape
  (`team_build` false, `mobile_build` false) renders no team row, no HOME half and claims nothing.
- **Mutations (new `--target=uistatus`).** Row-4 priority inverted; the fix predicate widened to `!= 0` on one
  coordinate only; `NO TEAM` replaced by a zero id; the `HOME` half rendered on a non-mobile build; `ui_fmt_mail`
  swapped for the raw sum; the coordinate divisor rounded instead of truncated.
- **Probe.** **P14f and P14a re-pointed exactly as §2.1 requires** (⛔ not weakened), plus a `status_row()`
  reader at `x = 40`; a control that draws a row at `x = 12` on rows 0-2 must redden, and a control that removes
  the mark must redden.
- **Split.** Pure: all strings + the priority. Renderer: `draw_rect` + placement. Device: nothing.

### S4 — TEAM row format: label, route age, and the two reserved columns

- **Scope.** The `%c%-6.6s %3s %4s %2s` row, composed in ONE pure formatter; the distance/bearing columns exist
  and are **always blank** in this slice (nothing computes them yet — stated in-source, the
  [[meshroute-mark-done-vs-missing-in-code]] rule). Plus the bounded clock-driven repaint (below).
- **Files.** NEW pure `src/firmware_ui_team.h`; `src/firmware_ui.cpp` (`draw_team_screen` calls it).
- **The repaint, and it is a real gap this slice must close.** Today a lit TEAM screen's age column goes stale:
  `FrameGate::step` answers `idle` on a clean model, and the only invalidation in the tree compares the
  **chrome** projection (`ui_chrome_invalidate`, `firmware_ui_chrome.h:449-453`, called at
  `firmware_ui.cpp:1684`), which carries the strip and rail and **not** the body's per-row tokens. ⇒ S4 adds a
  pure `ui_team_invalidate(UiModel&, live_snapshot, frozen_snapshot)` in the same shape — it **RAISES or does
  nothing, and ⛔ NEVER CLEARS** (§8.3.1's withdrawn instruction, `firmware_ui_chrome.h:428-437`). It costs
  **zero new RAM**: the frozen snapshot already exists (`s_frame_snap`, `firmware_ui.cpp:1704`). ⛔ It compares
  the **bucketed values** that map 1:1 to the drawn tokens (the age bucket, later the distance token and the
  octant) — ⛔ never re-formatted strings per tick, and never the raw ages, which would repaint every second for
  a panel that did not change (§8.2's own argument). It cannot wake a dark panel: `FrameGate::step` tests
  `blanked` first and never examines `dirty` — pinned by a case, not asserted in prose.
- **Pins.** Row bytes for: a long name (clamped to 6), a short name, a `0x<hash>` label, a bare `id n` label,
  `--` age, the passive blank marker vs the interactive `>`, and the `BACK` row. Width proof ≤ 19 at every
  expansion. The `TEAMMATE GONE, pick` refusal row still fits and still suppresses the marker. Ordering
  unchanged (⛔ a mutation that sorts the rows must redden). A lit TEAM screen repaints when an age token turns
  and **only** then; a dark panel does not wake.
- **Mutations (new `--target=uiteam`, plus `model` for the invalidation if it lands beside the chrome one).**
  Precision dropped from `%-6.6s` (letting a long label push the columns off); the invalidation made to clear;
  the invalidation comparing raw ages instead of buckets.
- **Probe.** P14f's widest-line INFO line re-read; a TEAM walk with a maximal label; a lit-panel repaint control.

### S5 — Location projection: the existing cache, three pure helpers, two rendered columns

- **Scope.** Publish the cache through the frozen snapshot; add `src/firmware_ui_geo.h`; render DIST/DIR.
- **Files.** NEW pure `src/firmware_ui_geo.h`; `src/firmware_ui_model.h` (the **`TeamRow` peer-location fields**
  — ⓘ the `UiSnapshot` OWN-location fields are **already done**, pulled forward into S3, §3.4);
  `src/firmware_ui_team.h` (the two columns); `src/firmware_ui.cpp` (`build_snapshot` copies — nothing else).
  ⛔ **`lib/` untouched**: `peer_loc_find`, `team_key_of_id` and `NodeConfig` are read exactly as they are.
- **Pins (the note §7 location matrix, in full).** own fix missing ⇒ blank · peer hash unresolvable ⇒ blank ·
  cache miss ⇒ blank (⛔ never `0m`) · **age 599 / 600 shows, 601 blanks** · `age_s == 0xFFFFFFFF` blanks ·
  negative coordinates in all four quadrants · **identical points ⇒ `0m` and a BLANK direction** (⛔ never `N`,
  ⛔ never a fabricated octant) · a long distance ⇒ the saturation token · **all eight bearings** driven
  directly · an antimeridian pair ·
  the `int64_t` longitude difference (a control using `int32_t` must redden) · **zero radio traffic across a
  full TEAM walk** (queue depth and radio starts counted, not argued).
- **Mutations (new `--target=uigeo`).** `<=` → `<` at the freshness bound; the bound itself changed; the
  antimeridian fold removed; the difference taken in `int32_t`; the octant thresholds swapped; `cosf` dropped
  (longitude unscaled); **`has_bearing` forced true for a zero-length vector** (⇒ a fabricated `N` at a
  coincident point).
- **Probe.** A TEAM arm with a seeded cache entry through the REAL renderer (a control that hard-wires "fresh"
  must redden — the [[B226]] shape: a token the pure suite proves and the production renderer never shows is
  not proven).
- **Split.** Pure: freshness, geometry, tokens. Renderer: two `%s` fields. Device: one `peer_loc_find` call per
  shown row, per tick, at `build_snapshot` — bounded by `kMaxTeamRows` (8) and a linear scan of ≤16 slots
  (`cap_peer_loc`, `node.h:2811`); measured in §6, ⛔ never a flash or radio access.

### S6 — Artwork: the real mark replaces the placeholder

- **Scope.** ONE asset in `src/firmware_ui_icons.h` (24x24 = 72 B, or the accepted 16x16 = 32 B centred inside
  the reserved slot), `draw_bitmap` instead of `draw_rect`. **⛔ No geometry moves and no text moves** — that is
  the whole point of reserving the slot.
- **Pins.** The asset decodes under the ONE byte-order expression (`firmware_ui_icons.h:19`) — pinned by the
  existing ASCII-art decode test's shape, on a glyph **asymmetric on both axes** (a symmetric mark would pass a
  mirror, a flip and a bit-reversal alike — the instrument-that-cannot-fail shape the icons header already
  names); `byte_count_of(24,24) == 72`; the draw lands at `12,12,24,24`; every STATUS text row is byte-identical
  to S3's.
- **Mutations (`--target=icons`).** Stride hand-written as 1; the asset drawn at the text origin; a 16x16 asset
  drawn at the slot origin instead of centred.
- **Cost.** `.rodata` only — **zero SRAM** (`firmware_ui_icons.h:26-30`); flash delta measured per env.

### S8 — Wake-on-receive: a received DM or channel message lights the panel *(owner-ruled 2026-08-20)*

- **The ruling, in its final scoped form (owner, 2026-08-20).** The OLED **wakes when a message is RECEIVED**,
  and "a message" is exactly two things:
  **(a)** a **DM delivered to us** — `PushKind::msg_recv` (`lib/core/command.h:193`), **sealed or not**, because
  it is *addressed to us*; and
  **(b)** a **channel post that arrived SEALED and was opened with OUR team channel key** —
  `PushKind::channel_recv` (`:194`) **AND `pu.enc == true`**.
  ⛔ **A CLEARTEXT channel post does NOT wake the panel**, and ⛔ no other push kind wakes anything — not a
  beacon, not a route event, not a send outcome, not a join push.
- ★★★ **THE `enc` GATE IS THE WHOLE SAFETY ARGUMENT, AND IT IS WHY NOTHING IS REVERSED** (F-9). With it, §R1's
  *"a stranger's post does not light a dark panel"* (bench §8.15) **still holds**: an undecryptable/foreign post
  emits **no push at all** (`node_channel.cpp:405-419` — unreadable ⇒ not inboxed, no `channel_recv`), and a
  cleartext post on a matching channel id — the one case that *does* deliver from outside our key — is refused
  by the gate. ⇒ the exposure is **our own team's sealed traffic plus DMs addressed to us**, nothing wider.
  `on_reply`'s own path is untouched and keeps working exactly as it does.
  ⛔ **WITHDRAWN WORDING, KEPT VISIBLE:** this bullet read *"IT REVERSES HALF OF §R1/[[B109]] AND SAYS SO …
  That half is WITHDRAWN for these two kinds only"*. It reverses nothing.
- ⚠ **ONE `lib/` COMMENT REPAIR RIDES THIS SLICE (V1):** `lib/core/command.h:321` documents `enc` as
  *"channel_recv -> false (cleartext today)"*, which `node_channel.cpp:415` falsified — and S8 is the slice that
  makes the field load-bearing for the panel. Comment only; ⛔ no behaviour, ⛔ no wire, corpus inert by
  construction — and the s18 md5 is re-run and reported regardless (D2).

- **Where it goes, and why there.** `mrui::ui_route_recv_push` (`src/firmware_ui_send.h:508-540`) is already the
  ONE pure router for both kinds, already reached from the single device entry point
  (`mr_ui_on_push`, `src/firmware_ui.cpp:1832-1846`), already calls `m.mark_dirty()` on both arms, and is
  compiled by the native suite. ⇒ **one call in each of the two arms**, and the kind gate is the function's own
  existing structure (`pu.kind != PK::channel_recv ⇒ return false`) rather than a new predicate (U1).
  ⛔ Not in `mr_ui_on_push`: that TU is compiled by neither the native suite nor the simulator (§B115).

- **The mechanism, chosen after inventorying `_last_input_ms` — ⛔ the wake must NOT write it.**
  `_last_input_ms` is written only by a real gesture (`:1199`) and the first-tick seed (`:1285`), and it drives:
  the compose auto-exit (`:1287` — deleted by S2), the detail auto-exit (`:1293` — deleted by S2) and the panel
  blank (`:1349`). Writing it from a push would postpone whatever else it ever comes to drive, which is exactly
  what the ruling forbids. ⇒ **a SEPARATE deadline**, `_msg_wake_until_ms`, tested by a `wake_active(now)`
  predicate beside the existing `hold_active(now)` (`:2803-2809`) in the blank condition:
  `if (!blanked && !hold_active && !wake_active && elapsed(now, _last_input_ms) >= kBlankMs) → blank`.
  It is `hold_active`'s shape verbatim (U3), it is wrap-safe the same way, and it leaves every real-press
  behaviour untouched. **The window is `kBlankMs`, measured from the message's OWN arrival** — the ruling's
  *"the standard blank timeout re-applies after the wake"*. ⛔ No second constant.
  ★★★ **ONE EXCEPTION, AND IT IS §B65's SEED WINNING — CORRECTED IN PLACE 2026-08-22 (implemented and tested).**
  ⛔ **WITHDRAWN WORDING, KEPT VISIBLE:** the sentence above was written as an absolute, and pin (9) below read
  *"The panel re-blanks `kBlankMs` after the message, not after the previous press"* **with no exception**.
  ⇒ **A MESSAGE THAT ARRIVES BEFORE THE FIRST TICK MEASURES ITS WINDOW FROM THE FIRST TICK**, not from its own
  arrival. **Why it must:** production **drains pushes immediately before `mr_ui_tick`**, so a wake stamp taken
  ahead of the first tick would land where §B65's seed belongs and **consume it** — and §B65 exists because a
  `mr_ui_init()` running more than `kBlankMs` after boot otherwise **blanks the panel on its very first tick**,
  leaving a safety device dark the first time it is looked at (`firmware_ui_model.h:1280-1285`). ⇒ **the seed
  outranks the message stamp**, the operator still gets a full window, and the case is driven directly
  (`test/test_firmware_ui_model.cpp:5972`). ⛔ It is not a tolerance or a rounding artefact — it is a stated
  priority between two deadlines, and a slice that "simplifies" it re-opens B65.
  ⓘ **Why not `on_reply`'s trick of relying on an existing deadline:** `on_reply` needs none because
  `kEmgHoldMs > kBlankMs` keeps the panel lit through `hold_active` (`:1808-1811`). A plain message has no hold,
  so clearing `blanked` alone would blank again on the very next tick — a one-frame flash. Stated because that
  is the tempting one-line version of this slice.
  ⓘ **S8 does not depend on S2 and S2 does not depend on S8:** because the wake never touches `_last_input_ms`,
  the two modal timeouts behave identically whether or not S2 has landed. Either order is safe.

- **⛔ A PUSH NEVER NAVIGATES.** The wake lights **the CURRENT screen**, whatever it is. ⛔ It does not switch to
  INBOX, does not open a modal, does not move a cursor, does not change a selection, does not clear
  `team_pick_gone`/`inbox_pick_gone`, and does not retire a transient note. This is the standing rule (the
  [[B233]] class: the operator's place on the panel is theirs), and it is what makes the wake safe to combine
  with §3.3's retention: **blank → message arrives → the panel lights showing exactly what was there.**
- **⛔ EMERGENCY OVERLAYS OWN THE PANEL.** The wake writes **no** emergency field — not `_emg`, not `_tries`, not
  `_emg_hold_until_ms`, not `_emg_news`, and it does not mark an outcome presented. If an alarm state is up, the
  panel lights showing the alarm (which is the safety-first answer), and the wake press remains consumed
  (`:1204`) so it cannot dismiss a retained outcome (§B71) by accident.

- **Power — stated honestly, and ACCEPTED FOR V1 (owner-ruled 2026-08-20, §9 R-6).** A node receiving nothing
  sleeps exactly as today. A node receiving messages pays **one attention window (`kBlankMs`) of lit panel per
  message**, and — because `ui_allows_sleep` requires `blanked` (`:2948`) — **that window is also a window of no
  light sleep** (F-10). ⇒ **ACCEPT FOR V1, ⛔ NO RATE LIMITER**: with the `enc` scope the exposure is only the
  operator's own team's sealed traffic and DMs addressed to them, which is low-rate by use case. ⛔ A limiter is
  a possible FUTURE ruling and is ⛔ not this spec's; the cost stays stated here and is **measured** at §7.8
  step 4 so a later ruling has data rather than argument.

- **Files.** `src/firmware_ui_model.h` (the deadline, the predicate, the one entry point). `src/firmware_ui_send.h`
  (two call sites + the `enc` gate). ⛔ `src/firmware_ui.cpp` is **not** touched — `mr_ui_on_push` already routes
  both kinds. ⚠ `lib/core/command.h` — **one drifted comment line only** (F-9), no code.
- **Pins.** (1) `msg_recv` on a blanked panel ⇒ `blanked` false, `dirty` true — **and it wakes with `enc` FALSE
  as well as true**, driven as two cases, because a DM is addressed to us either way (⛔ a gate copied onto this
  arm would silence an unsealed DM). (2) **A SEALED `channel_recv` (`pu.enc == true`) wakes.** (3) ★★★ **A
  CLEARTEXT `channel_recv` (`pu.enc == false`) does NOT wake** — same kind, same channel id, same everything
  else: the two cases differ in `enc` alone, which is what makes the gate measurable rather than merely present.
  (4) **Every other `PushKind` wakes nothing** (drive the full enum, not a sample). (5) ⛔ **The counters and
  `mark_dirty()` are UNCHANGED on every arm** — a cleartext post still counts as unread and still repaints a lit
  panel; ⛔ the gate governs the WAKE and nothing else. (6) ⛔ **No navigation:** across a wake, `screen`,
  `cursor`, the interactive view state, both selections and both `*_pick_gone` flags are byte-identical to
  before. (7) ⛔ **No emergency field moves**, in every emergency state including a retained outcome.
  (8) `_last_input_ms` is **unchanged** by the wake — ★ **and this is an IMPLEMENTATION INVARIANT, stated and
  reviewed, ⛔ NOT an independently observable behaviour.** ⛔ **WITHDRAWN WORDING, KEPT VISIBLE:** this pin read
  *"(assert the field's effect: an open modal's own deadline, where one still exists, is not postponed)"* — and
  **there is no such deadline to assert against: this spec's own §9 R-1 DELETED both of them** (S2 removed the
  compose and detail auto-exits), which leaves **`blank_due` as the field's only reader**. ⇒ a wake window of
  `kBlankMs`-from-now is **arithmetically identical** to stamping the field, so no test can tell the two apart.
  ⚠ **THE LESSON, RECORDED BECAUSE THE FALSIFIER WAS ONE RULING EARLIER IN THIS SAME DOCUMENT:** a spec that
  rules a DELETION must sweep the assertions that stood on the deleted thing — an obligation phrased against a
  timer that no longer exists is an instrument that cannot fail, and this one survived three revisions. (9) The panel re-blanks **`kBlankMs` after the message**, not after
  the previous press — **EXCEPT a message that arrives BEFORE THE FIRST TICK, whose window runs from the FIRST
  TICK** (§B65's seed outranks the message stamp; see the mechanism bullet above). **Both arms are pinned**, and
  ⛔ the pre-first-tick arm is the one a "simplification" would silently drop. (10) A wake **while already awake** changes nothing but the deadline (⛔ no repeat
  `set_power_save` storm — the board latches it, and the frame gate is the only thing that talks to the panel).
  (11) **Quiet node: sleep unaffected** — with no pushes, `ui_allows_sleep` answers exactly as today.
- **Mutations (`--target=model` for the deadline/predicate; NEW `--target=uisend` for the placement + the gate).**
  ★ **the `enc` gate DROPPED** (⇒ a cleartext post wakes) — RED at match count 1, and it is the slice's headline
  control; **the gate INVERTED** (⇒ only cleartext wakes); **the gate COPIED onto the `msg_recv` arm** (⇒ an
  unsealed DM stops waking — the half-applied shape, which a `enc == true` fixture alone would survive); the
  wake moved ahead of the kind gate (⇒ every push wakes); the wake dropped from the `channel_recv` arm only;
  `wake_active` inverted; ★ **the wake writing `_last_input_ms` TOGETHER WITH `_seeded`** — ⛔ **WITHDRAWN,
  KEPT VISIBLE:** this entry read *"the wake writing `_last_input_ms`"* **alone**, which post-R-1 is
  **behaviourally INERT** (see pin (8)) and would be a mutation that cannot redden. The live entry is the
  **plausible copied-from-`on_gesture` defect** — the wake stamping the seed pair exactly as a real press does
  (`tools/probe_ui_model_mutations.py:1558`, S09) — which **re-creates §B65** and **is** observable; the wake
  writing `_st.screen`; the wake clearing `_emg`. ⓘ `src/firmware_ui_send.h` has **no battery target today**
  (`tools/probe_ui_model_mutations.py:55-81`) — a battery is per-source-file, so without its own target the two
  call sites **and the `enc` gate** would have no controlled mutation at all, which is the [[B217]] shape this
  project registers.
- **Probe.** `probe_firmware_ui`: a blanked-panel phase that injects a `msg_recv` and a **sealed**
  `channel_recv` and asserts the REAL renderer lit the panel **on the screen that was current** (the [[B226]]
  discipline: a control that hard-wires the wake must redden), plus **two** negative arms — a **cleartext**
  `channel_recv` and several other push kinds — each asserting `last_power_save` stays 1 and **zero** bus
  operations (the P13f shape, which already measures exactly this for a blanked chrome change). ⚠ The cleartext
  arm is the one that would be forgotten, and it is the arm that proves the gate.
- **Split.** Pure model: the deadline, the predicate, the "no navigation / no emergency write" invariants. Pure
  send-router: the two call sites and the kind gate. Renderer: nothing. Device: nothing.

### S7 (DEFERRED) — TEAM ordering · ⛔ **DEFERRED BY RULING (§9 R-2)**
Not in this spec's scope: the owner ruled on 2026-08-20 to **keep the current ordering**. Recorded so that no
slice above quietly does it: **S1-S6 and S8 must not re-order the roster.**

---

## 5. TEST / MUTATION / PROBE PLAN, per slice

| slice | native suites | battery targets | probe phases |
|---|---|---|---|
| S1 | `test/test_firmware_ui_model.cpp` | `model` | P6a-P6g (press prefixes), P14b/P14c/P14d (rail unchanged), P15/P16 (walk prefixes) |
| S2 | `test/test_firmware_ui_model.cpp` | `model` | P6 (blank/wake over an open modal) |
| S3 | new `test/test_firmware_ui_status.cpp` | **new `uistatus`** | **P14f + P14a re-pointed**, P13 unaffected (strip untouched), P7b (STATUS carries no withdrawn marker) |
| S4 | new `test/test_firmware_ui_team.cpp` | **new `uiteam`** (+ `model` if the invalidation lands beside the chrome one) | P14f width INFO, a lit-panel repaint control |
| S5 | new `test/test_firmware_ui_geo.cpp` (+ team-row cases) | **new `uigeo`**, `uiteam` | a seeded-cache TEAM arm through the real renderer; zero-traffic counters |
| S6 | the icons decode test | `icons` | P14a bitmap census |
| S8 | `test/test_firmware_ui_model.cpp` + `test/test_firmware_ui_send.cpp` | `model`, **new `uisend`** | a blanked-panel wake phase (DM sealed+unsealed, sealed channel post) + **two** negative arms — a **cleartext** channel post and the other push kinds — both in the P13f zero-bus shape |

**Standing rules for every slice.**
- **[[B217]] re-pin duty.** The mutation runner aborts with `sys.exit(2)` and **applies zero mutations** when the
  clean baseline does not match its pinned `BASE_CASES` / `BASE_ASSERTS`. ⇒ **read the pin from
  `tools/probe_ui_model_mutations.py` at dispatch time; ⛔ never hardcode or carry a number from a document**
  (this spec deliberately states none). When a slice moves the native counts, re-pin with the derivation written
  in place, and **prove each battery RAN** (report RED counts and the count of unusable entries).
- ⚠ **Never run a probe and a battery concurrently** — the mutation runner serialises on `.pio/build/native`
  (`tools/probe_ui_model_mutations.py:265-268`) and a concurrent build corrupts a measurement rather than
  failing it.
- New pure files must be added to the three base `build_src_filter`s only if they are `.cpp` — these are all
  headers, so the board builds inherit them; the native suite needs each new `test_*.cpp`.
- Every new battery target is registered in `TARGET_SRC` (`tools/probe_ui_model_mutations.py:55-81`) — a battery
  is **per-source-file**, which is why S3/S4/S5 get their own rather than sharing `model`'s entries.
- ⛔ **Label length:** probe check labels must stay at or under 64 characters or the roll-up under-attributes
  their controls ([[B229]]).
- **Gate before "ready" (D1/D3):** `pio test -e native` **then run** `./.pio/build/native/program` (the wrapper
  misreports "0 test cases"); the s18 md5 **read from `simulation/BASELINE.md`** (inert here by construction —
  `src/`-only); the **two** board envs, sequentially; `git diff -- lib/` empty. Report failures with output; say
  what was skipped.

---

## 6. RESOURCE COSTS — measured, not assumed

**Baseline measured 2026-08-20 on this tree** (host reveal:
`g++ -std=c++20 -DMESHROUTE_NATIVE=1 -DMR_N_LAYERS=2 -Isrc -Ilib/core -Ilib/hal` over a TU that prints the
`sizeof`s):

| type | host bytes |
|---|---|
| `mrui::UiSnapshot` | **608** |
| `mrui::UiState` | **200** |
| `mrui::UiModel` | **600** |
| `mrui::TeamRow` | **28** |
| `mrui::InboxRow` | **40** |

⛔⛔ **CORRECTED IN PLACE 2026-08-22 BY S5's ELF INSPECTION — THE TWO STRUCTS DO NOT BEHAVE THE SAME, AND THE
WITHDRAWN READING IS KEPT VISIBLE.** This paragraph read: *"**`UiSnapshot` and `UiState` are each instantiated
TWICE on the OLED envs** — the model's own copy plus the frame's frozen copy (`s_frame_snap` / `s_frame_state`,
`firmware_ui.cpp:1703-1704`) — and the per-tick snapshot is additionally a **stack** local
(`firmware_ui.cpp:1639`). ⇒ a growth of n bytes costs ~2n of static RAM **and** n of loop-task stack."*
★ **WHAT THE ELF SAYS INSTEAD:**
- **`UiSnapshot` is static exactly ONCE** — `s_frame_snap`. **`s_model` embeds no snapshot** (measured: its size
  did not move when the snapshot grew), because `UiModel` takes one as a *parameter*, never as a member. ⇒ a
  growth of *n* costs **n of static RAM and n of TRANSIENT loop-task stack** (the `build_snapshot` local), ⛔ not
  2*n* static.
- **`UiState` genuinely is static twice** — the model's `_st` **and** `s_frame_state` — so for THAT struct the
  ~2*n* reading stands.
⚠ **THE LESSON, RECORDED BECAUSE IT COST TWO WRONG ROWS BELOW:** "frozen copy" and "model's own copy" are not
symmetric, and the doubling must be **read off the image**, never inferred from the freeze pattern. ⛔ Every
figure in this table is a per-board `RAM_used` diff plus an ELF read — D2's standing warning still applies on
top: native's 8-byte alignment structurally hides a 4-byte-align board padding shift.

| slice | ⚠ **an unlanded slice's figure is an ESTIMATE and says so; a landed slice's is MEASURED and names its method** (S3, S5) |
|---|---|
| S1 | `UiState` +1 byte (`ListView`); expected to land in existing tail padding ⇒ **0**. Measure. |
| S2 | 0 |
| S3 | ⛔ **WITHDRAWN, KEPT VISIBLE:** *"0 RAM (all strings are `.rodata` / stack buffers)"* — **FALSE as written**, because the §3.4 pull-forward landed the own-location fields here. **MEASURED: `UiSnapshot` 608 → 616 (+8)** — the two `int32_t` coordinates; **the `bool` cost ZERO**, landing in existing padding (**offsetof-proven**, not argued). ⇒ **~+8 B static and ~+8 B TRANSIENT loop-task stack.** ⛔ **WITHDRAWN, KEPT VISIBLE:** *"~+16 B static across the struct's two instances and +8 B loop-task stack"* — that used the two-static-instances reading S5's ELF inspection has since falsified (see the paragraph above this table). ⓘ **DERIVED, not separately measured:** it is this row's own measured +8 combined with S5's ELF fact that only `s_frame_snap` is static; a slice re-measuring it should report the observed figure. Strings remain `.rodata`/stack; flash: a handful of formatters. |
| S4 | 0 RAM (the invalidation reuses the existing frozen snapshot). |
| S5 | ★ **MEASURED, and it corrects TWO earlier claims of this table.** **`TeamRow` 28 → 40.** ⛔ **WITHDRAWN, KEPT VISIBLE:** *"`TeamRow` 28 → ~44 (`+4 +4 +4 +1` → padded to +16) ⇒ `UiSnapshot` 616 → ~744 (+128 …)"*, and before that *"608 → ~736 … plus `own_lat/own_lon/own_fix` which should land in tail padding"*. **The `bool` cost ZERO** — `offsetof(TeamRow, peer_loc_valid)` is **26**, inside the pad that already followed `label[15]`; the three 4-byte fields take **28/32/36** ⇒ **+12, not +16**. ⇒ **`UiSnapshot` 616 → 712 (+96, not +128).** <br>★★★ **RAM, BY ELF INSPECTION AND A CONTROLLED TWO-ENV A/B — AND THE "TWO OLED INSTANCES / ~+192 B STATIC" READING IS WITHDRAWN:** the image holds **ONE static `UiSnapshot`** (`s_frame_snap`, 0x268 → 0x2c8); **`s_model` embeds none** (0x248, unchanged) — the "second instance" is a **transient stack value**, the per-tick `build_snapshot` local. ⇒ **~+96 B static and ~+96 B TRANSIENT loop-task stack.** Confirmed on metal-shaped builds: `heltec_v3` RAM **216 684 → 216 780**, `gateway_heltec` **241 636 → 241 732** — **+96 on both.** <br>**Flash: +5 320 B** (`heltec_v3`) / **+5 316 B** (`gateway_heltec`), of which **3 561 B is libm — and it is the cosine's ARGUMENT REDUCTION, not the cosine**: `__kernel_rem_pio2f` 1 279 + `two_over_pi` 792 + `__ieee754_rem_pio2f` 552, against `cosf` itself **121** and `sqrtf` **60** — plus **805 B** of this slice's own code and **~950 B** of growth inside existing symbols. ⇒ §3.4's **integer-cosine-table fallback is QUANTIFIED at ~3.5 KB and NOT TAKEN**: flash sits at **39.0 % / 37.7 %** of 3 342 336 B. **`UiState` 200 and `UiModel` 600 unchanged**; warning sets identical pre↔post, `-Wswitch` zero. |
| S6 | flash +72 B `.rodata` (or +32 for a 16x16); **RAM 0** by construction. |
| S8 | ★ **MEASURED (A/B).** **`UiModel` 600 → 608 on BOTH ESP32 ELFs.** ⛔ **WITHDRAWN, KEPT VISIBLE:** *"`UiModel` +4 bytes (`_msg_wake_until_ms`), expected to land beside the existing `uint32_t` deadlines … Measure."* — the **`uint32_t` deadline did land in existing padding and cost ZERO**; what costs is the **`bool`**, which takes the struct's **8-byte tail step**. `UiModel` is instantiated **once** (`s_model`), so ⛔ it does not double. <br>**Controlled per-env totals after S8, read off the image:** `heltec_v3` RAM **216 796** / flash **1 303 560** (post-S5: 216 780 / 1 303 444 ⇒ **+16 RAM / +116 flash**); `gateway_heltec` RAM **241 732** / flash **1 259 668** (post-S5: 241 732 / 1 259 596 ⇒ **+0 RAM / +72 flash**). ⚠ **The per-env RAM deltas differ from the naive +8 — +16 on one env, +0 on the other — and that is REPORTED AS READ OFF THE IMAGE, ⛔ not reconciled by argument:** section rounding and alignment decide where an 8-byte struct step actually lands, and D2's standing warning is exactly that native alignment cannot predict it. **`UiSnapshot` 712 / `UiState` 200 / `TeamRow` 40 unchanged.** |

**Per-tick cost.**
- S5 adds, per tick while the snapshot is built: ≤8 `team_key_of_id` lookups (already paid — `label_for_team_id`
  performs the same lookup at `firmware_ui.cpp:357`, so the slice should take **one** resolution per row and
  hand it to both, U1) and ≤8 `peer_loc_find` scans of ≤16 slots. No flash, no radio, no allocation.
- S4's invalidation is ≤8 bucketed comparisons per tick **while the TEAM screen is current**; ⛔ never a
  per-tick re-format. It follows the §CHROME-3 precedent exactly: it may only RAISE `dirty`, and while the panel
  is dark it changes nothing (`FrameGate::step` tests `blanked` first, `firmware_ui_model.h`'s gate block).
- S8 costs **one comparison per tick** (`wake_active`, beside the existing `hold_active`) and one `uint32_t`
  write per received message. ⛔ No timer, no allocation, no extra bus call — the frame gate remains the only
  thing that talks to the panel.
- ⚠ **S8's real cost is POWER, not cycles, and it is not a per-tick figure:** each received message buys one
  `kBlankMs` window of lit panel **and** of suppressed light sleep (`ui_allows_sleep`, `firmware_ui_model.h:2948`
  — F-10). **Accepted for V1 by ruling (§9 R-6), with no rate limiter**; measured on the bench (§7.8 step 4).
- ⛔ **No new timer id.** `TimerWheel::kCap` is untouched — nothing here is protocol time.

---

## 7. METAL — one full-UI walkthrough

⛔ **THE BENCH SCRIPT (`docs/2026-07-31-bench-test-script.md`) REMAINS THE AUTHORITY (M2).** This section is
assembled **from it plus the new checks**, in the owner's ruled inline format: every command and expected line is
written out here, with **no cross-references to follow**. When a slice adds a metal-only behaviour, its report
**drafts** the corresponding bench edit; ⛔ the slice does not edit the bench script (supervisor-landed after
PASS).

**Absorbed by owner ruling 2026-08-20, and therefore run as part of this walkthrough:** the [[B231]]/[[B233]]
inbox recheck (§7.5), bench 27.17 / [[B230]] (§7.6), and the Part 20 + 25.4 [[B232]] press-sequence re-run
(§7.7).

### 7.0 Equipment, build, evidence
- H1 = a Heltec V3 mobile (`heltec_mobile`), H2 = any peer in range, both on the same team and PHY.
- ⚠ **EVERY `send` IN THIS WALKTHROUGH IS PLANE-EXPLICIT, DELIBERATELY.** The verb is
  `send <id|0xhash> "<text>" [-a] [-e] [-t] [-l]` and **a bare `send <id>` uses the GLOBAL/home plane and FAILS
  when there is no home** (`src/firmware_commands.cpp:854`) — on a team-only bench that is a step that cannot
  pass. ⇒ a team-plane message always carries `-t`, and a sealed one always uses the **`0x<hash>` form**
  (`-e` is *encrypt(hash only)*; `-l` is refused unless the message is sealed). ⛔ Do not "simplify" a command
  here by dropping a flag.
- **Record the two hashes once, at the start, and reuse them:** on H1 run `whoami` (H1's own hash) and
  `hashof <H2-id> -t`; on H2 run `whoami` and `hashof <H1-id> -t`. Both print `0x…`. Write them down as
  `<H1-hash>` / `<H2-hash>`.
- Flash the committed revision under test on H1 and confirm it: `version` ⇒ a real revision, ⛔ **not `nogit`**.
- ⚠ Starting from a factory-reset node, do `cfg set sf_list 6,7` **first** — otherwise `team new` refuses
  (that refusal is §7.6's subject).
- Record PASS / FAIL per checkbox. A failure carries the console lines **and a panel photo**. Keep the ELF for
  every image you flash.

### 7.1 The navigation contract (S1)
1. ☐ From STATUS press `short` **five times**, one press per screen. Expected: the rail's boxed slot advances
   STATUS → TEAM → INBOX → SEND → SETTINGS → STATUS, **one press per screen with no exceptions**.
   ⛔ FAIL if any screen costs more than one `short` to pass.
2. ☐ On TEAM (with ≥2 teammates): the rows are listed with **no `>` marker anywhere**. Press `short` ⇒ the rail
   moves to INBOX. ⛔ FAIL if a row highlight moved instead.
3. ☐ Return to TEAM, press `double` ⇒ a `>` appears on the first row and a `BACK` row is visible at the end of
   the list. Press `short` repeatedly to the `BACK` row, then `short` once more ⇒ **the highlight returns to the
   first row**. ⛔ FAIL if the panel left TEAM.
4. ☐ On `BACK`, press `double` ⇒ the `>` disappears (passive TEAM), the rail is still on TEAM. One `short` ⇒
   INBOX. ⛔ FAIL if the `double` jumped to another screen.
5. ☐ Repeat 2-4 on INBOX. Then `double` on a message row ⇒ its detail modal opens with `>back` selected. Press
   `double` ⇒ back at the **interactive** INBOX list (the `>` is still there), not at the passive one.
6. ☐ **Blank/wake retention:** with the INBOX list interactive and a row highlighted, leave the board ~20 s
   until the panel goes dark. Press **once** ⇒ the panel lights and **the same row is still highlighted**
   (the press is consumed, nothing activates). ⛔ FAIL if the list returned to passive or the highlight moved.
7. ☐ **Emergency still pre-empts:** from the interactive TEAM list, `long`-press ⇒ the alarm arms
   (`ARMING`/countdown), the rail disappears, the body is the alarm's. Release before the fire deadline to
   cancel ⇒ the panel shows the **`CANCELLED` overlay**. ⚠ **WAIT for it to clear** — that overlay owns the body
   for a deliberate ~1 s (`kCancelledMs`, `firmware_ui_model.h:866`; `tick_emergency` returns the machine to
   `idle` at `:2781`) — **and only then** verify the TEAM list is still interactive with the same selection.
   ⛔ Reading the panel during the overlay measures the overlay, not the retention.
8. ☐ **Negative control:** on the passive TEAM screen press `double` twice quickly on a screen with **no**
   teammates ⇒ the list opens and offers `BACK` only; `double` on `BACK` returns to passive. ⛔ Nothing is sent.
9. ☐ **S2 — the compose sub-view survives blank/wake.** From the interactive TEAM list, `double` on a teammate
   ⇒ the compose list opens on the first message. Leave the board ~20 s until the panel is dark, then press
   **once**. Expected: the panel lights showing **the same compose list, the same highlighted message**.
   ⛔ FAIL if it returned to TEAM (that is the deleted `kBlankMs` auto-exit). ⛔ Nothing may have been sent —
   check H2's console. Leave via `back, don't send`.
10. ☐ **S2 — the detail modal survives blank/wake, with its action intact.** From the interactive INBOX list,
    `double` a record ⇒ the modal opens with `>back`. Press `short` once so `>delete` is selected. Wait for the
    panel to blank (~20 s), press once to wake. Expected: **the same record, the same page and `>delete` still
    selected**. ⛔ FAIL if the modal closed. ⛔ **Nothing may have been deleted** — confirm the record count is
    unchanged (`pull_inbox` over USB, or simply read the list after pressing `short` back onto `>back` and
    leaving with `double`).
11. ☐ **S2 — the emergency exception still holds.** With the detail modal open on `>delete`, `long`-press ⇒ the
    alarm arms and **the modal is closed** underneath it (a hidden Delete may not survive under an overlay).
    Cancel, wait ~1 s for the `CANCELLED` overlay to clear, and confirm the panel is back on the INBOX list with
    no modal open. ⛔ FAIL if the modal is still up.

### 7.2 The STATUS body (S3, and S6 for the mark)
1. ☐ Cycle to STATUS. Expected body, top to bottom:
   `TEAM <8 uppercase hex>` · `ME T<n>` · `<n> KNOWN` (rows 0-2, starting to the right of the mark) ·
   `<n> NEW / HOME <age>` · `<lat>,<lon>` (rows 3-4, full width).
   ⛔ The third row must read **`KNOWN`** — ⛔ **not `HEARD`** (it is route evidence, not direct RF contact) and
   ⛔ not `MEMBERS`.
   ⛔ FAIL if any of rows 0-2 starts at the left margin (it must clear the reserved mark) or if any row runs off
   the right edge.
2. ☐ Compare against the console: `status` and `cfg` ⇒ the team id, the team-local id and the coordinates on the
   panel must equal the console's, and the panel's team id is the **same eight hex digits** (case aside).
3. ☐ The mark: a `24x24` outline (S3) or the final artwork (S6) at the top-left of the body. ⛔ FAIL if it
   overlaps the rail, the strip or any text.
4. ☐ **`NO LOCATION`:** `cfg set lat 0` + `cfg set lon 0` ⇒ within one repaint the last row reads
   `NO LOCATION`. ⛔ It must never read `0.000,0.000`. Restore the coordinates afterwards.
5. ☐ **`NO TEAM`:** on a node with no team (`cfg` shows `team_id=0x00000000`) ⇒ row 0 reads `NO TEAM` and row 1
   is blank. ⛔ FAIL on `TEAM 00000000` or `ME T0`.
6. ☐ **`RESTART NEEDED` priority:** provoke a reboot-class saved state (see §7.7 step 8; ⚠ only reachable on a
   build with `-DMR_UI_BLE_ROW=1` — otherwise record **not-run with that reason**, ⛔ never FAIL). Expected: the
   last row reads `RESTART NEEDED` and the coordinates are **absent** for as long as it stands.
7. ☐ Read the whole body at arm's length. ⛔ FAIL if any row is clipped mid-word.

### 7.3 TEAM rows (S4) and distance/bearing (S5)
1. ☐ With ≥2 teammates, enter the TEAM list. Each row: `<name up to 6> <age> <dist> <dir>`. ⛔ FAIL if text runs
   under the rail or off the right edge, or if a long name pushes the age off the row.
2. ☐ A teammate with a long stored name renders **exactly six characters** of it. ⛔ FAIL on a clipped seventh.
   ⚠ The rename verb takes a **hash**, never an id: on H1 run `peername 0x<H2-hash> "Wolfgangetta"`
   (`src/firmware_commands.cpp:874`). ⛔ `peername <id> …` is not a valid command and will simply refuse.
3. ☐ A teammate with no cached name renders `0x<hash>` shortened, or the bare id. Cross-check with
   `hashof <H2-id> -t` and `peers` over USB.
4. ☐ **Distance appears only with evidence.** Set coordinates on both nodes (`cfg set lat …` / `cfg set lon …`).
   On H2 first confirm it actually holds H1's public key — `peers` shows H1's row with a key and its `0x<hash>`;
   if it does not, run `reqpubkey <H1-id> -t`, wait, and check `peers` again. Then, from H2:
   `send 0x<H1-hash> "hi" -t -a -e -l`
   ⇒ within one repaint H1's TEAM row for H2 shows a plausible distance and an eight-way bearing.
   ⚠ **All four flags are load-bearing:** `-e` seals (and requires the `0x<hash>` form), `-l` attaches the
   position and is **refused unless the message is sealed**, `-t` keeps it on the team plane, `-a` asks for the
   ack that makes the delivery visible. ⛔ `send <H1-id> "hi" -a -l` is **not** a valid sealed send and must not
   be used here — it is the global/home plane and the core refuses the location attachment.
   ⛔ FAIL if a distance appears for a teammate that never sent an authenticated position.
5. ☐ **Staleness — and the peer must stay ALIVE while it goes stale.** ⛔ **Do not simply leave the teammate
   silent:** the route itself would expire and the whole ROW would disappear, which measures route lifetime, not
   the freshness bound. ⇒ keep H2 powered, in range and beaconing normally, and send **no further
   location-bearing traffic from it** for **more than ten minutes**. Expected: **the row REMAINS** with its name
   and its (still-small) route age, and **the distance and the direction both go blank**.
   ⛔ FAIL if a distance survives past the bound, and ⛔ FAIL if the row vanishes (that is a different defect —
   record it and stop, do not re-run until the route is healthy).
   ⓘ To restore the columns, repeat step 4's sealed located send; they must come back within one repaint.
6. ☐ **No ADDITIONAL traffic — measured against a baseline, because scheduled beacons never stop.**
   ⛔ A bare "watch for no beacon" cannot discriminate: a healthy node beacons on its own schedule whatever the
   panel shows. ⇒ (i) with the panel on **STATUS**, capture the console for **five minutes** and count the
   outbound lines by kind; (ii) repeat for the **same five minutes** sitting on TEAM, entering and leaving the
   interactive list throughout. Expected: **no ADDITIONAL query, DATA or location-bearing transmission** in the
   TEAM window — beacon counts may differ only as their own schedule explains, and ⛔ **zero** `reqpubkey`,
   zero DM, zero channel post and zero location request may appear that the STATUS window did not also show.
   ⓘ **The automated proof is the stronger one and it exists:** the S5 probe counts TX-queue depth and radio
   starts across a full TEAM walk and asserts **zero**. This bench step is the sanity check on real hardware,
   not the primary evidence.
7. ☐ Sanity: clear H1's own fix (`cfg set lat 0`, `cfg set lon 0`) ⇒ every distance and direction goes blank at
   once. Restore.
8. ☐ **Coincident points, if two nodes can be put side by side:** set both nodes to the SAME `lat`/`lon` and
   repeat step 4 ⇒ the row shows **`0m` and a BLANK direction**. ⛔ FAIL on `N` (or any other cardinal) — a
   zero-length vector has no bearing.

### 7.4 The rest of the panel is unchanged
1. ☐ The top strip still carries mail, home age, team count, key and battery, and nothing overlaps
   (read it at arm's length).
2. ☐ Idle sleep still works: leave the node alone and confirm from `status` that sleeps are still accumulating.
   ⛔ FAIL if the UI now prevents light sleep.

### 7.5 ABSORBED — [[B231]]/[[B233]] inbox recheck on glass
1. ☐ Seed H1's inbox from H2, **plane-explicitly** — two DMs on the team plane:
   `send <H1-team-local-id> "dm one" -t` then `send <H1-team-local-id> "dm two" -t`
   (⛔ **not** a bare `send <id>`: that is the global/home plane and fails outright on a team-only bench —
   `src/firmware_commands.cpp:854`. The `0x<H1-hash>` form is equally acceptable and is required if you also
   want them sealed) — and two channel posts:
   `send_channel 0 "ch one" -t` then `send_channel 0 "ch two" -t`.
2. ☐ H1 panel → INBOX. Expected order top to bottom: **`dm two` · `dm one` · `ch two` · `ch one`** — the DM
   block first, then the channel block, **newest at the TOP within each block**.
3. ☐ Enter the list (`double`) and move the highlight onto `dm one`. From H2:
   `send <H1-team-local-id> "dm three" -t`.
   Expected: `dm three` appears at the **top** of the DM block and **the highlight is still on `dm one`**
   (pushed one row down). ⛔ FAIL if the highlight re-targeted onto the newcomer.
4. ☐ **Delete-middle:** open `dm one`, `short` to `>delete`, `double`. Back at the list: the row is **GONE with
   no further press** — time only, within ~1 s — and the highlight sits beside where it was.
   ⛔ FAIL if the deleted row is still drawn until you press something.
5. ☐ Open the row now adjacent and confirm it opens the record **its label names** (no off-by-one).
6. ☐ **Delete-last:** delete the bottom row of the channel block. Same expectation: gone with no press,
   highlight on its predecessor.
7. ☐ Envelope sanity: the strip's unread count matches what remains; one fully drawn INBOX list returns it to 0.

### 7.6 ABSORBED — bench 27.17 / [[B230]]: the incomplete-PHY refusal
1. ☐ On a node whose persisted `sf_list` is EMPTY (`factory_reset`, then `cfg` shows no DATA SF), type
   `team new freq=869 sf=7 bw=125` ⇒ **must answer EXACTLY:**
   `> team err: incomplete PHY — the MISSING part is this node's `sf_list` (the DATA SF set): it is EMPTY, which blocks DATA entirely.`
   `>   ★ `sf_list` is NOT a `team` key — the team PHY tail carries freq/sf/bw only and PRESERVES this node's own DATA SF set.`
   `>   set it FIRST: `cfg set sf_list 6,7`, then retry your original `team` command.`
   `>   NOTHING changed — team_id, the team channel key, the PHY and NV are all as they were.`
   ⛔ It must NOT say `need freq, routing_sf(5..12), sf_list(DATA SF), bw` and must NOT suggest
   `set them inline: `team new freq=869.0 sf=7 bw=125``.
2. ☐ **Form neutrality:** repeat as a JOIN — `team 0x12A1B2C3 freq=869 sf=7 bw=125` on the same empty-`sf_list`
   node ⇒ **the same four lines, verbatim**. ⛔ The remedy must not name `team new`.
3. ☐ **Follow the remedy:** `cfg set sf_list 6,7`, then re-run step 1's command ⇒ the team is created/joined and
   `> team PHY:` ends `sf_list=6,7` (⛔ not `7`).
4. ☐ **Control, the generic arm:** with a valid `sf_list` in place, provoke a genuinely incomplete tail ⇒ must
   still answer `> team err: incomplete PHY — need freq, routing_sf(5..12), bw. …` followed by
   `>   set them inline on your `team` command: `freq=869.0 sf=7 bw=125` …`.
5. ☐ `cfg` after steps 1 and 2 ⇒ team_id, key, PHY and NV all unchanged (each refusal spends no write and no
   airtime).

### 7.7 ABSORBED — [[B232]] press sequences: Part 20 + Part 25.4 re-run
⛔ **SETTINGS lands CLOSED**: the body shows ONE row, `>ENTER SETTINGS`; every "walk to `<row>`" begins with a
`double` on that row. A `short` on the closed view **passes the screen** — that is the ruling, not a fault.
1. ☐ Cycle past SEND: the fifth screen is SETTINGS — confirmed by the rail's **bottom** slot carrying the
   selection frame. Expected body: a single row `>ENTER SETTINGS`. ⛔ No menu rows before you `double`.
   ⛔ FAIL if cycling past SETTINGS costs more than ONE short press.
   ⛔ There must be **no `BLE` row** once open — the UI-12 transport is compiled on no ESP32 env.
2. ☐ `double` ⇒ the menu opens on its first row. Rows in order, three at a time as you `short`: `DM crypt`,
   `key attach`, `auto reg`, `PROVISION`, `SAVE`, `DISCARD`, `BACK`. A `short` past `BACK` returns to
   `>ENTER SETTINGS` — ⛔ it does not leave the screen.
3. ☐ Compare the first three values with `cfg` over USB: **the panel's values must equal the persisted ones**.
4. ☐ Highlight `DM crypt`, `double` ⇒ the value is **bracketed** (`>DM crypt   [off]`); `short` ⇒ `[on]`;
   ⛔ the cursor must not move to the next row while bracketed; `double` accepts and the bracket goes.
5. ☐ ⛔ Nothing is saved: `cfg` over USB still reports the OLD value, and the panel shows the marker row
   `CFG* UNSAVED`. **Walk out to `>ENTER SETTINGS` and confirm `CFG* UNSAVED` is STILL on the panel there.**
6. ☐ Cycle to STATUS: the unsaved state is visible as the SETTINGS rail slot's **badge**. ⛔ The STATUS body must
   not carry the marker text. ⛔ The strip must not be shortened or overdrawn to make room.
7. ☐ Leave the board ~20 s so the panel blanks, then press once. The marker is still there and the value is
   still edited. ⛔ A draft may never be discarded because attention timed out.
8. ☐ Walk to `BACK`, `double` ⇒ the panel **stays on SETTINGS** showing `>ENTER SETTINGS`, marker still up; one
   further `short` leaves for STATUS. Re-enter and `double`: the edited value is **still edited**, and the menu
   re-opens on its **first** row.
9. ☐ Walk to `SAVE`, `double` ⇒ panel says `SAVED`, the marker row is gone and the rail badge is the plain gear.
   `cfg` over USB ⇒ the edited field holds the new value **and every non-covered field is unchanged** (same node
   id, team id, channel counter, radio floor).
10. ☐ Power-cycle. `cfg` ⇒ the saved value survives and so does everything in step 9.
11. ☐ **Badge table, read from `>ENTER SETTINGS`** (⛔ do not open the menu to read it):

    | do this | badge on the SETTINGS rail icon | SETTINGS must ALSO print |
    |---|---|---|
    | fresh boot, never opened SETTINGS | plain gear | — |
    | edit a value, do not save | gear **+ dot** | `CFG* UNSAVED` |
    | with that draft open, `cfg set e2e_dm 1` over serial | gear **+ exclamation** | `CFG! RELOAD` |
    | save a reboot-class field (`ble_mode`) | gear **+ restart marker** | `RESTART NEEDED` |
    | unsaved **and** restart-required | the **unsaved** dot | both texts, own rows |
    | conflict **and** unsaved | the **exclamation** | `CFG! RELOAD` |

    ⚠ The restart rows need `-DMR_UI_BLE_ROW=1`; on a stock image record them **not-run with that reason**,
    ⛔ never as a FAIL. ⛔ Fail on an icon-only configuration error.
12. ☐ **PROVISION unchanged:** `double` on `PROVISION` ⇒ the child menu opens on `>CREATE TEAM`, also offering
    `JOIN NETWORK` and `BACK`; `double` on `BACK` returns to the SETTINGS menu. ⛔ Not to another screen.

### 7.8 Wake-on-receive (S8)
1. ☐ **A DM lights a dark panel, on the screen that was current.** Put H1 on **TEAM** (passive), leave it ~20 s
   until the panel is dark. From H2: `send <H1-team-local-id> "wake one" -t`.
   Expected: within one repaint the panel **lights by itself**, showing **TEAM** — the strip's envelope count
   has gone up. ⛔ **FAIL if the panel switched to INBOX or opened anything**: a push never navigates.
2. ☐ **A SEALED team channel post does the same.** With the team **content key present on both nodes** (the
   strip's key icon is the normal key, not the crossed one), blank the panel again; from H2:
   `send_channel 0 "wake two" -t`.
   Expected: the panel lights, still on TEAM, envelope count up by one.
2b. ☐ ★★★ **THE DISCRIMINATOR — a CLEARTEXT channel post must NOT light the panel.** Produce a post on the same
   channel id that arrives **unsealed** (the simplest rig: a node with **no team content key**, or an
   otherwise-identical post sent on a plain/leaf channel with `team_id = 0`). Blank H1's panel, send it, and
   wait a full attention window.
   Expected: **the panel stays DARK** — while the strip's unread count still moves once you wake it by hand, and
   the message is still in the INBOX.
   ⛔ **FAIL if the panel lights**: that is the `enc` gate missing, and with it the §8.15 rule *"a stranger's
   post does not light a dark panel"*, which this ruling deliberately preserves.
   ⓘ An **undecryptable/foreign** post needs no step: it is never inboxed and emits no push at all
   (`lib/core/node_channel.cpp:405-419`) — there is nothing that could wake.
2c. ☐ **A DM wakes whether or not it was sealed.** Blank the panel; send an **unsealed** team DM
   (`send <H1-team-local-id> "wake three" -t`) ⇒ the panel lights. Then blank and send a **sealed** one
   (`send 0x<H1-hash> "wake four" -t -a -e`) ⇒ it lights again. ⛔ FAIL if only one of the two wakes: a DM is
   addressed to us either way, and ⛔ the channel gate must not have been copied onto this arm.
3. ☐ **The selection is preserved.** Enter the interactive INBOX list, highlight a row, let the panel blank.
   From H2 send another team DM. Expected: the panel lights showing **the interactive list with the same row
   still highlighted** (the new message appears at the top of its block and pushes it down, per §7.5 step 3).
   ⛔ FAIL if the highlight moved or the list returned to passive.
4. ☐ **The blank timeout re-applies, and the sleep cost is recorded rather than assumed.** After the wake, touch
   nothing: the panel must blank again **~15 s after the MESSAGE arrived** (not 15 s after your last press).
   ⇒ note `slept=` from `status` before the wake and again two minutes after the panel has re-blanked: sleeping
   must **resume** once dark. ⓘ Record how many messages arrived in that window — this is the F-10 / §9 R-6
   measurement kept for a possible future limiter ruling; it is **data for the owner, ⛔ not a pass/fail**.
5. ☐ **⛔ NOTHING ELSE WAKES IT.** With the panel dark and H1 idle, let H2 simply beacon (send nothing) for two
   minutes. Expected: **the panel stays dark the whole time.** ⛔ FAIL on any wake — that is wake-on-any-push,
   the exact thing the ruling narrows to a DM and a sealed team post.
6. ☐ **The quiet-node sleep guard still passes (bench 24.3 / 25.6).** On a node with **no** traffic reaching it,
   confirm from `status` that `slept=` climbs steadily as it does today. ⛔ FAIL if idle sleep regressed — a
   node receiving nothing must sleep **exactly** as it did before this slice.
7. ☐ **An emergency is not disturbed.** Fire an alarm and let its outcome be presented, then let the panel
   blank with the outcome retained. From H2 send a team DM ⇒ the panel lights **showing the same retained
   alarm outcome**, unchanged; ⛔ the attempt count, the headline and the outcome must all be identical, and
   ⛔ the alarm must not re-fire. One `short` (the consumed wake press) does **not** dismiss it; the next one
   does, exactly as today.

### 7.9 Stop rules
- ⛔ Stop and report on: a send the operator did not choose; a distance shown without an authenticated fresh
  position; a draft or a selection lost to a timeout that this spec says must preserve it; any panel claim the
  console contradicts.
- Record the exact image (`version`), the console transcript and a photo per failure.

---

## 8. STRING INVENTORY — every lexeme, for owner ruling

House style: each string is declared **once**, in a pure unit, and **pinned by a native case**, so an owner
ruling changes it in exactly one place (the slice-5/6 precedent, `firmware_ui_model.h:548-551` and `:562-565`).
Widths are against the row's own budget (14 columns at `x=40`, 19 at `x=12`).

| # | lexeme / format | where | cols (widest) | status |
|---|---|---|---|---|
| S-1 | `TEAM %08lX` → `TEAM 3D9348A5` | STATUS row 0 | 13 / 14 | **NEW** (uppercases the shipped `team %08lx`; see §2.2 note j) |
| S-2 | `NO TEAM` | STATUS row 0 | 7 / 14 | **NEW** (the note's own word) |
| S-3 | `ME T%u` → `ME T220` | STATUS row 1 | 7 / 14 | **NEW** (uppercases the shipped `me T%u`) |
| S-4 | `ME NO ID` | STATUS row 1, in-team before team-DAD | 8 / 14 | **NEW** — a case the note does not cover (§2.2 note b) |
| S-5 | `%s KNOWN` → `4 KNOWN` / `9+ KNOWN` | STATUS row 2 | 8 / 14 | **NEW**, and **CORRECTED 2026-08-20 (QA/owner)**: ⛔ **WITHDRAWN `%s HEARD` / `4 HEARD`**, kept visible — the count is route evidence, which §3.3 forbids calling "heard". ⛔ never `MEMBERS` either |
| S-6 | `NO TEAM KEY` | STATUS row 2 | 11 / 14 | **NEW** (the note's own word) |
| S-7 | `%s NEW` → `3 NEW` / `99+ NEW` | STATUS row 3 | 7 | **NEW**; saturation is `ui_fmt_mail`'s `99+` |
| S-8 | ` / HOME %s` → `/ HOME 42s`, `/ HOME --` | STATUS row 3 | 18 combined | **NEW** (the age token is `ui_fmt_home_age`'s, reused) |
| S-9 | `NO LOCATION` | STATUS row 4 | 11 | **NEW** (the note's own word) |
| S-10 | `%s%ld.%03lu,%s%ld.%03lu` → `52.123,21.456` | STATUS row 4 | 16 | **NEW format**; truncates toward zero |
| S-11 | `%c%-6.6s %3s %4s %2s` | TEAM row | 19 | **NEW format** (note §5.2, verbatim) |
| S-12 | `BACK` | TEAM + INBOX interactive last row | 5 | **REUSED** — the shipped spelling (`provision_row_label` / `settings_row_label`, `firmware_ui_model.h:450/360`); ⛔ no second spelling |
| S-13 | `850m` · `1.2k` · `12k` · `999k` · **`far`** | TEAM distance column | 4 | **NEW**; the note left rounding + saturation to this spec (its §5.4 / §8.2) — proposal: exact metres below 1 000; one decimal below 10 km; whole km to 999; then `far` |
| S-14 | `N` `NE` `E` `SE` `S` `SW` `W` `NW` | TEAM direction column | 2 | **NEW**; ⛔ cardinal text, never an arrow |
| S-15 | `HOME UNKNOWN` | — | 12 | ⛔ **WITHDRAWN**, kept visible: it does not fit beside `%s NEW` and `ui_fmt_home_age`'s `--` already owns the state (§2.2 note f) |
| S-16 | `CFG UNSAVED` | — | 11 | ⛔ **DROPPED BY RULING (§9 R-3)**, kept visible: no configuration text returns to STATUS. The shipped lexeme stays `CFG* UNSAVED` / `CFG! RELOAD` on SETTINGS (`cfg_marker_text`, `firmware_ui_model.h:600-603`) |
| S-17 | `RESTART NEEDED` | STATUS row 4 | 14 | **REUSED** — `kCfgRestartText` (`firmware_ui_model.h:604`), already on this row today |

**Count: 17 entries — 13 NEW (S-1…S-11, S-13, S-14), 2 REUSED (S-12, S-17), 1 WITHDRAWN (S-15), 1 DROPPED BY
RULING (S-16).** ⓘ The count is unchanged by the 2026-08-20 revision: `KNOWN` **replaces** `HEARD` inside S-5
(its withdrawn form kept visible), and **S8 adds no string at all**. ⚠ REPORTED, NOT INVENTED: where the note rules a SEMANTIC and no lexeme, the wording above is this file
cluster's house style applied to it, one line each, pinned by a native case.

---

## 9. RULINGS — ★ ALL CLOSED. ZERO OPEN QUESTIONS.

⛔ **THE SECTION HEADING IS CORRECTED IN PLACE, TWICE, AND BOTH EARLIER FORMS ARE KEPT VISIBLE**: it read
*"OPEN QUESTIONS — only the genuinely unresolved"* (carrying OQ-1…OQ-5), then *"RULINGS (closed) and the one
question that remains"* (carrying OQ-6). **OQ-1…OQ-6 were ruled by the owner on 2026-08-20, PLUS the R-7 scope ruling** (the S8 wake scope, which was
never numbered as an OQ — QG-corrected wording 2026-08-20) ⇒ **nothing in this spec is waiting on a decision.** Each ruling is recorded below in reported form **and folded into the normative
body at the place named**; the questions' own text is kept visible so each ruling can be read against what was
asked.

**R-1 (was OQ-1) · §3.3 versus the two shipped attention timeouts — ★ §3.3 WINS, FOR BOTH.**
*Asked:* the note forbids an attention timeout discarding a draft, a detail selection or a compose choice, while
`kBlankMs` closes the compose sub-view (`firmware_ui_model.h:1287-1289`) and the detail modal (`:1293`), both
deliberately (design §3.2.1, §3.5) — and preserving compose across a blank leaves a pocketed device one
consumed press plus one `double` from a send.
*Ruled:* **preserve compose AND detail across blank/wake**; the wake press stays **consumed**; the **emergency
transitions remain the safety exception**. ⇒ **landed at §1.5 (the T3/T4 verdict block) and in slice S2, which is
now RESOLVED rather than owner-gated**; metal at §7.1 steps 9-11.

**R-2 (was OQ-2) · TEAM ordering — ★ KEEP THE CURRENT ORDER.**
*Asked:* existing `rt_team_at` order, worst/stalest first, or another measured rule.
*Ruled:* **keep it as it is; S7 stays deferred.** ⇒ **landed at §3.3 and at slice S7**; ⛔ no slice re-sorts.

**R-3 (was OQ-3) · Does STATUS regain the configuration text? — ★ NO.**
*Asked:* the note lists `CFG UNSAVED` among the substitutions, while §CHROME-4/design §6 deliberately removed
`CFG* UNSAVED` / `CFG! RELOAD` from STATUS (the probe pins the absence — *"P7b …and the STATUS body no longer
carries the withdrawn marker TEXT"*).
*Ruled:* **no configuration text returns to STATUS.** The **badge** carries the state on every screen and
**SETTINGS says the words**; **only `RESTART NEEDED`** appears on STATUS, on row 4, as it does today. ⇒ **landed
at §2.2 (row 4 and note g), at §1.9 F-4 and at string-inventory S-16, which stays DEFERRED-then-DROPPED**.

**R-4 (was OQ-4) · The rail's modal mapping versus the parent-screen `BACK` rule — ★ KEEP §5.2.**
*Asked:* a DM compose opened from TEAM boxes the **SEND** rail slot while its `back` returns to **TEAM**.
*Ruled:* **keep the §5.2 mapping — the rail names the BODY** (SEND is selected during a DM compose) — and
**`BACK` independently returns to TEAM**. The two are separate facts and both are correct. ⇒ **landed at §1.4
(the audit table's closing paragraph)**; the probe's P14d expectation is unchanged and ⛔ must not be re-pinned.

**R-5 (was OQ-5) · Terminal results: acknowledgement or a selectable `BACK`? — ★ KEEP THE ACKNOWLEDGEMENT.**
*Asked:* every terminal screen is acknowledged by either press and returns to its parent
(`:2668-2671`, `:2600-2603`, `:226`); should it instead carry a selectable `BACK` row?
*Ruled:* **keep the shipped terminal acknowledgement; ⛔ no extra `BACK` row.** ⇒ **landed at §1.4**; ⛔ no slice
adds a row to a result screen.

**R-6 (was OQ-6) · Does a chatty channel need a wake rate limit? — ★ ACCEPT FOR V1, ⛔ NO RATE LIMITER.**
*Asked:* S8 lights the panel for one attention window per received message, and the cost is larger than the
ruling named — `ui_allows_sleep` requires `blanked` (`firmware_ui_model.h:2948`), so **each wake also suppresses
light sleep for that window** (F-10); messages arriving more often than `kBlankMs` would keep a hiking node
awake indefinitely. Accept as-is, or rate-limit (minimum interval / per-window cap / "already awake ⇒ do not
extend")?
*Ruled:* **accepted for v1, with no limiter.** With **R-7's `enc` scope** the exposure is only **the operator's
own team's sealed traffic and DMs addressed to them** — low-rate by use case. ⛔ A limiter is a **possible
future ruling, not this spec's**. ⇒ **landed in slice S8's Power bullet, at §6's per-tick block and at §1.9
F-10**; the figure is still **measured** at §7.8 step 4 so a later ruling has data rather than argument.

**R-7 (F-9, ruled the same day) · What exactly counts as "a message received"? — ★ SCOPED, NOT REVERSED.**
*Asked:* S8 as first specified would have withdrawn half of §R1/[[B109]] (*"a stranger's post does not light a
dark panel"*, bench §8.15).
*Ruled:* the wake fires for **(a)** a **DM delivered to us** (`msg_recv`, **sealed or not** — it is addressed to
us) and **(b)** a channel post that arrived **SEALED and was opened with our team channel key**
(`channel_recv` **gated on `pu.enc == true`**). A **cleartext** post on a matching channel id delivers with
`enc == false` and ⛔ **must not wake**; an undecryptable/foreign post emits **no push at all**
(`lib/core/node_channel.cpp:405-419`). ⇒ **§8.15 survives by construction, nothing is withdrawn, and the drafted
bench §8.15 correction is DROPPED.** ⇒ **landed at §1.9 F-9, in slice S8 (the ruling bullet, the `enc` pins (1)-(3)
and the three gate mutations) and at metal §7.8 steps 2 / 2b / 2c**.

---

### ⓘ Open questions: **NONE.**
⛔ Nothing in this spec is blocked on an owner decision. A slice that believes it has found one must **STOP and
report** rather than choose a default — that is the standing rule this section was created to serve, and it does
not lapse because the list is empty.

---

## 10. WHAT THIS SPEC DELIBERATELY DOES NOT DO

- ⛔ no wire change of any kind, and no `wire_version` bump (§0);
- ⛔ no `lib/` CODE or BEHAVIOUR change — `peer_loc_find`, `team_key_of_id`, `rt_team_at` and `NodeConfig` are
  read as they are; ⓘ S8 carries the ONE explicitly bounded comment correction (`command.h:321`, §0's sole
  exception — QG-aligned wording 2026-08-20);
- ⛔ no continuously-refreshed teammate position, no periodic location broadcast, no location request — a
  separate future specification (note §6);
- ⛔ no TEAM re-ordering (§9 R-2), no preset catalog (design §3.2.2), no §3.6.4 nearby-team scan;
- ⛔ no unification of the two team-id spellings and no deletion of `TeamRow::score_q4` — both are refactors and
  both are named here so a later slice can claim them (C1);
- ⛔ **no wake on anything but a DM addressed to us and a SEALED team channel post** — ⛔ never a cleartext
  post, never any other push kind (S8, §9 R-7) — and ⛔ **no wake rate limiter**: accepted for v1 without one
  (§9 R-6), with the cost measured at §7.8 step 4 for any future ruling;
- ⛔ no navigation from any push, ever — a wake lights the current screen and nothing else (S8).
