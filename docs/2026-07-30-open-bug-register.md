<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# BUG REGISTER

*Opened 2026-07-30 at the owner's request, so findings stop living only inside `BASELINE.md` notes and agent
reports. **This file is the index; `simulation/BASELINE.md` carries the evidence.** Each entry names the note
that has the measurement.*

⚠ **Every `file:line` here drifts** — this tree moves several times a day and eight slices landed on 2026-07-28/30
alone. **Re-locate by symbol before acting (V1/V2).** A line number in this file is a hint, never a fact.

★ **Nothing here is speculative.** Every entry was either measured, or found in-source with a marker left by the
slice that declined to fix it (C1). Where an entry is *unmeasured*, it says so explicitly.

---

## Current status checklist — authoritative as of 2026-08-04

Use this section to choose work and mark completion. The detailed records below preserve the full evidence,
superseded premises and gate history; their older status language describes the time of the record and does not
override this checklist.

Legend:

- `[x]` — fixed, closed or resolved; retain the numbered record.
- `[ ] OPEN` — implementation, measurement or an owner decision is still available.
- `[ ] PARKED` — recorded but not currently dispatched; do not implement without a new ruling.
- `RECORDED` — useful constraint, not a defect.

### Open

- [ ] **B20 — OPEN:** encrypted DM lengths 215–216 can disappear without `send_failed`.
- [ ] **B21 — OPEN:** an oversized DM reports the wrong condition and no `send_failed`.
- [ ] **B24 — OPEN:** `q_tx.rt_total` remains plane-inconsistent telemetry.
- [ ] **B25 — OPEN / UNMEASURED:** a team member may answer a static sync with a team-plane beacon.
- [ ] **B31 — OPEN / POLICY:** `key_hash_for_id` has no authoritative or TTL gate.
- [ ] **B34 — OPEN:** the simulator still collapses refusal reasons to generic `error`.
- [ ] **B35 — OPEN / SILENT:** channel self-skip compares ids across planes.
- [ ] **B36 — OPEN / CONTRACT:** received message JSON does not expose attached location.
- [ ] **B52 — OPEN / CONTRACT:** JSON exposes team confidence but not static confidence.
- [ ] **B54 — OPEN / OWNER CALL:** the first claim in a full first-hand team table evicts one beacon row.
- [ ] **B55 — OPEN / CONTRACT:** `reqpubkey_sent.hash == 0` needs its S4b meaning documented.
- [ ] **B56 — OPEN / CONTRACT DECISION:** stage-2 `reqpubkey` failure is not app-visible.
- [ ] **B60 — OPEN / SPEC READY; ACK POLICY OPEN:** multi-gateway `send_layer` resolves the final hash on an intermediate layer instead of selecting the next gateway.
- [x] **B61 — FIXED 2026-08-03 (own commit, UNCOMMITTED):** `board_name()`'s silent `#else → "native"` is now an `#error`; `MESHROUTE_NATIVE` gets its own explicit arm.
- [x] **B62 — ✅ CLOSED 2026-08-04 (UI-5, UNCOMMITTED):** the last open item — `board_ui.cpp:1`'s own path header — is fixed by the slice that rewrote the file, exactly where the entry said to fix it. A tree-wide `grep -rn 'src/board_ui' src/ lib/ variants/ platformio.ini` now returns **nothing**.
- [ ] **B63 — RECORDED (gate methodology):** on xtensa, an object entering or leaving the link set moves `.flash.text` by up to ±200 B **even when it is zero bytes**; free on ARM. Retires the "+192 B = leaving `src/`" rule.
- [x] **B64 — ✅ CLOSED 2026-08-05 (OWNER-RULED, fixed in the UI-7 QA fix slice, UNCOMMITTED):** the TEAM cursor now tracks the **teammate by team-plane identity**, not the row index — a roster reorder moves the highlight WITH the teammate, and a teammate that has left the roster **REFUSES the activation loudly** (`TEAMMATE GONE, repick`, highlight suppressed) instead of retargeting the DM. ⚠ It was a **MIS-SEND** and plan `:135` named it a prerequisite for wiring real sends; Task 7 wired them without resolving it.
- [ ] **B111 — OPEN / NEW 2026-08-05 (UI-7, MEASURED):** a DM whose synchronous result is `queued` with **`ctr == 0`** has no handle to correlate and **no `SendOutcome` kind of its own**, so the sub-view sits on `SENDING...` until its own `kBlankMs` auto-exit. Bounded and never a false claim — but it answers nothing.
- [ ] **B112 — OPEN / NEW 2026-08-05 (UI-7, MEASURED in `lib/core`):** on the id-addressed DM arm, **`ctr != 0` does NOT imply the frame was enqueued** — four `enqueue_data` refusals return a non-zero ctr without enqueueing. The UI's `accept()` therefore claims a handle for a send that never happened. ⛔ A `lib/core` finding; NOT fixed here.
- [x] **B113 — ✅ FIXED 2026-08-05 (the UI-7 QA fix slice, UNCOMMITTED; OPENED by the same slice before the fix):** `ChanState::waiting` was a **DEAD STATE** — assigned **zero** times, referenced once, by the renderer — so an accepted canned channel post never left `SENDING...`. `UiModel::on_send_accepted` had arms for `emergency` and `dm` only.
- [ ] **B65 — OPEN / OLED UI-2:** the UI model can blank the panel on its **first tick** if `mr_ui_init()` runs more than `kBlankMs` after boot — a panel that never drew anything.
- [x] **B66 — ✅ CLOSED 2026-08-05 (UI-7):** the canned tables MOVED into `src/firmware_ui_model.h` and the counts are now `sizeof`-DERIVED from them, so `back`'s positional identity has a single declaration. This is B66's own named "durable cure", not the UI-6 `static_assert` interim it replaces.
- [x] **B67 — FIXED IN THE PLAN 2026-08-03 (all nine sites):** `REQUIRE` is a **hard compile error** under `-fno-exceptions`. ⚠ Its fix introduced **B70** — read both.
- [x] **B68 — FIXED IN THE PLAN 2026-08-03:** `SendOutcome` gained the eighth kind `channel_remote_mint`. ⚠ Its render obligation has no carrier — **B69**.
- [x] **B69 — ✅ CLOSED 2026-08-05 (UI-7), AND ITS OBLIGATION IS CORRECTED, NOT MERELY IMPLEMENTED:** the kind now has two carriers (`ChanState::unconfirmed` for the canned sub-view, `EmgEvidence::no_handle` for the alarm). ⚠ **"Render it as SENT" would have been a FALSE CONFIRMATION on the line this UI actually sends** — see the record.
- [ ] **B70 — OPEN / PLAN DEFECT (measured, 9 of 11 assertions silently deleted):** the B67 fix guards a **draining** call by calling it **twice**, so the two most safety-critical emergency cases `return` early and report PASSED.
- [x] **B71 — ✅ OWNER-RULED 2026-08-04, deferred to UI-6** (was: OPEN / SPEC GAP) — spec §4's `double` acknowledgement / re-fire is unimplemented, so a sticky emergency never returns to `idle` and UI-6's "emergency screen wins" policy has **no exit condition**.
- [x] **B72 — FIXED 2026-08-04 (UI-3 QA, UNCOMMITTED):** `SendOutcome::channel_failed` — the **ninth** kind — now exists, is TERMINAL and carries its reason; a pre-enqueue seal failure can no longer leave the alarm on `SENDING...`. ⚠ **The entry was cited by the plan and had never been written here.**
- [x] **B73 — FIXED 2026-08-04 (UI-3 QA, UNCOMMITTED):** an async failure carries its `SendFailReason` to the model (`dm_failed(r)` / `channel_failed(r)`, `fail_reason()`), so `refuse_reason()` is no longer pinned at `other`. ⚠ **Also cited by the plan and never written here.**
- [x] **B74 — FIXED 2026-08-04 (UI-3 QA, UNCOMMITTED):** a valid retry deadline of `0xFFFFFFFF` collided with the "no deadline" **sentinel**, so a blocked emergency could stay blocked **for ever**. The sentinel is gone; a `_retry_armed` flag reserves no arithmetic value.
- [x] **B75 — FIXED 2026-08-04 (UI-3 QA, UNCOMMITTED):** `DmState::submitting` was unreachable — nothing assigned it — **and the in-source comment claiming it was "written-but-unread" was false in both halves.** `take_send_request` writes it; the comment is corrected.
- [x] **B76 — CLEARED 2026-08-04 (UI-4), and it was SEVEN errors not six:** the four `bool → SendFailReason` conversions, the `const bool ok` redeclaration and the reasonless `dm_failed()` are all gone. ⚠ **A seventh error the scratch-TU probe never reported:** the B40 case iterates a **braced list**, so the block needs `#include <initializer_list>` — measured on the real suite, `error: deducing from brace-enclosed initializer list requires …`.
- [ ] **B77 — RECORDED (gate methodology):** `grep -n "36/36 corpus — 0 assertion failures"` matches the **prose that tells you to run that grep** before it matches the heading. Anchor on `^### `. ⚠ **Recurred 2026-08-04 in a new disguise — see B82.**
- [x] **B79 — FIXED 2026-08-04 (UI-4, UNCOMMITTED):** `SendOutcome::channel_remote_mint()` — §B68's whole point — had **no producer anywhere in the tree**, and the `awaiting` state it belongs to was **never closed**, so B39's producer (3) left the alarm on `SENDING...` for ever. `SendTracker::tick()` is the window's expiry and the kind's only producer.
- [x] **B80 — ★ OWNER-RULED then RESOLVED BY DELETION 2026-08-04 (UI-4, UNCOMMITTED):** `match_channel_failed` had **no correlator at all**, so any DM's `send_failed` inside the 8 s window terminated an awaiting alarm as TERMINAL `Emergency::failed`. ⛔ **My first fix — a `dst == 0` discriminator — was a CONVERSE ERROR and is withdrawn:** *channel ⇒ dst 0* does not give *dst 0 ⇒ channel*, and **six unrelated operations emit that shape**. ⇒ **the matcher is DELETED and must not be reintroduced**; the path is covered by synchronous preflight refusals plus `tick()`'s bounded expiry (**B84**).
- [x] **B84 — ★★ FIXED 2026-08-04 (UI-4, UNCOMMITTED):** the `tick()` expiry that closed B79 was an **INFINITE RETRY LOOP** — `_tries` moves only in `on_send_accepted`, which a `ctr == 0` send never reaches, so the alarm re-queued for ever with `attempts() == 0`. **An expired unattributable emergency now CONSUMES ONE BOUNDED ATTEMPT before its outcome is processed**; three expiries end in sticky `NOT HEARD`.
- [x] **B81 — ✅ CORRECTED IN THE SPEC 2026-08-04** (the `channel_id` half is withdrawn as unsatisfiable; `peer_id` stands): spec §2.1's table PROMISED a **`channel_id` scope check no outcome push can satisfy** — neither `channel_sent` nor `send_blocked` carries a channel id. Marked in-source; the spec line needs correcting.
- [ ] **B82 — RECORDED (gate methodology), B77's class in a new disguise:** the **`.d`-file census does not exist in this project on ANY env** (SCons uses `.sconsign*.dblite`), and a relative-path `find` in an agent shell whose cwd resets reported a tidy `NA` five times for the wrong reason. **Every census needs a POSITIVE CONTROL printed beside it.**
- [ ] **B83 — OPEN (UI-6/UI-7), obligation named:** the tracker's `late_ack` slot is bounded only by the caller calling `close()`, and an `awaiting` DM leaves the model in `DmState::submitting`. Both are unreachable from today's UI and both are deliberately not papered over inside the tracker.
- [x] **B85 — ✅ WORKED AROUND 2026-08-04 (UI-5, UNCOMMITTED); THE PLAN STILL NEEDS THE OWNER'S EDIT:** the plan's Task-5 Step-4 code block **does not compile, does not link, and cannot meet its own Step 5** — it omits the three `mr_ui_*` hooks `fw_main` calls unconditionally, it reads a `MR_UI_BTN_PIN` that Task 6 defines, and nothing in it ever calls `board_init()`, so *"the panel lights"* is unreachable **and** `--gc-sections` links the whole canvas out. All three measured.
- [ ] **B86 — RECORDED (gate methodology), a REFINEMENT of §A0's rule:** the ±32 B ARM literal-packing quantum fires between two builds **inside one session**, because the banner packs `__TIME__`, which changes every second. ⇒ **on ARM compare the SYMBOL MULTISET, not the flash total.**
- [x] **B87 — ✅ RULED 2026-08-04: totals PINNED, instrument shipped.** ⚠ **THREE** OLED envs, not two (`gateway_heltec` `extends = env:heltec_v3`) — pinned clean-build totals `heltec_v3` 178 / `heltec_mobile` 178 / `gateway_heltec` 174 warnings, `-Wswitch` **0** on all three, reproducible via **`tools/warning_census.sh`** (an incremental `pio run` emits none, so the pin was unenforceable without it). A HIGHER count fails. Scoping `-fno-rtti` is its own build slice (C1): `build_src_flags` covers `src/` only and would strip it from `lib/` + `variants/`. Detail: **127 are our own blanket `-fno-rtti`** hitting U8g2's 127 C translation units and **2 are U8g2's own**, in a module that is linked out. `-Wswitch` stays 0. The standing rule is "no new warnings".
- [x] **B88 — ✅ CLOSED 2026-08-05 (UI-6, UNCOMMITTED):** `src/firmware_ui.cpp` calls **all nine** canvas entry points, so `--gc-sections` collects none and the UI-5 flash figure is no longer a lower bound. Blanking, the button and the battery stub are bench-reachable from a Task-6 build (bench script 8.4/8.5/8.8-8.10). Measured: the controlled A/B that drops `+<firmware_ui.cpp>` returns 325 objects and **6 `undefined reference`** errors for the three hooks.
- [ ] **B89 — RECORDED (budget attribution):** the display's ~35 KB flash cost is **~26 KB of I²C transport**, not the display library — U8g2 itself is 9 860 B and our canvas ~497 B. Switching display library would not recover it.
- [x] **B90 — ✅ CLOSED 2026-08-05 (owner: panel lit fine):** the panel power rail (Vext, GPIO 36) was **never driven by this tree**; UI-5 drives it to MeshCore's proven LOW and the bench confirms the panel works at that level. ⚠ A lit panel cannot distinguish *"LOW enables the rail"* from *"the panel isn't on the rail"* — so **rail-level power gating must measure the rail**, never cite this closure. Nothing today depends on it (spec §5 blanks over I²C).
- [x] **B91 — ✅ CLOSED 2026-08-05 (UI-6, UNCOMMITTED):** `mrui::board_init()` now returns `bool` from a real I²C address probe (`Wire.beginTransmission(0x3C); endTransmission() == 0` — MeshCore's `i2c_probe` mechanism), and `mr_ui_init()` surfaces it as one boot line: `!! OLED panel did not ACK (check Vext / addr 0x3C / wiring)`. **Deliberately not fatal** — a node with a dead panel keeps meshing. The probe harness drives BOTH answers (P9a-d) and control **C8** (`board_init()` returns `true` without asking) turns it red, so the presence test can say *no*.
- [ ] **B92 — OPEN / SPEC CORRECTION:** spec §11 names the fonts *"6×8, 8×16"*; the plan and the shipped code use `6x10` / `10x20`, now measured at **4 990 B of 9 860 B**. The spec should be corrected to what landed.
- [x] **B97 — ★★ FIXED 2026-08-05 (UI-6, UNCOMMITTED): the four "REQUIRED INTEGRATION REGRESSIONS" that guard the DISTRESS PATH were structurally BLIND to the code they guard.** They existed, they were green, and they **hand-replicated** the tracker/model wiring — so a `mr_ui_tick` wired in the wrong order (which the plan records happening TWICE) would not have moved one assertion. ⇒ the wiring is now `mrui::ui_pump_trackers` / `mrui::ui_route_send_push`, PURE functions in `firmware_ui_send.h`, and the tests drive **them**. **Five reverts measured red:** B84 blocker 1 (2 cases), blocker 2 (2 cases), the offer order (1), the B71 exit (5), a re-added emergency `send_failed` arm (1).
- [ ] **B98 — RECORDED (gate methodology), and it is B97's own lesson biting me mid-fix:** my first version of the *"a canned expiry cannot touch a live alarm"* case **stayed green against the exact defect it names.** With `_tries` 1 of 3, routing the normal tracker's expiry into `on_outcome` re-enters `firing` and leaves `attempts()` at 1 — so asserting *state* and *attempts* proved nothing; the visible harm is a **PHANTOM QUEUED ALARM**. ⇒ **when a state machine's arm is idempotent-looking, assert the SIDE EFFECT (the queue), not the state.** Both halves are now asserted, plus a budget-spent variant where the same revert fabricates a terminal `NOT HEARD`.
- [ ] **B99 — RECORDED / THE PLAN'S TASK-6 BLOCK: its emergency overlay TEARS, and its tick cannot compile.** Two defects, both measured while implementing: **(a)** the block freezes `UiState` + `UiSnapshot` at `begin_frame()` — then has the overlay read `s_model` **LIVE**, so a state change mid-frame tears the image across page boundaries on the one screen where it matters most (fixed here by a frozen `EmgView`); **(b)** it calls `ui_perform_send()`, which is **Task 7 Step 1** and needs `mrfw::exec_command`, so Task 6 as written does not build. ⇒ UI-6 ships a **loud-refusal stub** (C2): the alarm reaches `FAILED` + `no send path: UI-7` and a console line, rather than sitting on `SENDING...` for ever. **The plan needs the owner's edit.**
- [x] **B100 — ✅ CLOSED 2026-08-05, OWNER AGREED: §B71's fifth state was VACUOUS and the ruling is now TRIMMED.** The ruling's exit table listed *"final `blocked`"* — but `on_outcome`'s `K::blocked` arm **always** arms a retry and `tick_emergency` always re-fires from it, so a `blocked` alarm is by construction still in flight and including it would fire the exit **mid-retry**, which the ruling's own first row forbids. Counted beside §B78's `failed` it made the ruled set read as **five** when only **four** are reachable. ⇒ **the phantom member is removed from the ruling** (the plan's B71 table now reads `picked_up` / `not_heard` / `reply` / `failed`, matching `emg_outcome_retained()`), and the in-source note at `firmware_ui_model.h`'s predicate is re-tensed to say the trim has landed. ★ **NO EXIT LOGIC CHANGED** — this removed a phantom obligation from a document, never a behaviour from the code, and there is no behavioural delta to gate. ⚠ **A test DOES assert the vacuous fifth, and it is KEPT DELIBERATELY, not deleted:** `test_firmware_ui_model.cpp`'s *"an IN-FLIGHT alarm does not exit (firing / arming / blocked-with-a-retry)"* drives a real `blocked` outcome and checks `emg_outcome_retained() == false` — it asserts the fifth state's **ABSENCE**, so it is the pin that makes the trim measured rather than argued. Removing it would delete the evidence for this closure.
- [x] **B101 — ✅ CLOSED 2026-08-05 (UI-6 fix slice, QA finding F5): committing an alarm now CLOSES the compose modal and resets its cursor.** `UiModel::emergency_gesture`'s `long_fire` arm sets `_st.compose = Compose::none; _st.cursor = 0`. ⓘ `long_arm` deliberately does NOT — arming is cancellable, and destroying the user's list position for a press they may still cancel would be a second, smaller wrong. **RED measured: 1 case / 5 assertions** (`ui-model: B101 — committing an alarm CLOSES the compose modal and resets its cursor`), and the discriminating one is `take_send_request(stale) == false` — against the shipped code a real canned DM **is queued**, so the mis-send is measured directly rather than argued. The old case that PINNED the opposite behaviour was rewritten, not deleted, and says why in-source. The drifted `on_gesture` comment ("a long press … does NOT close it") is corrected (V1).
- [x] **B102 — ✅ CLOSED 2026-08-05 (UI-6 fix slice, QA finding F3): FIXED, not recorded — "its result was seen" is now a MEASUREMENT.** The declined "cheap sound fix" in the old entry was latching off `clear_dirty()`; that is exactly what §B107 then proved unsound, since `clear_dirty()` was itself being called at the wrong point. ⇒ the latch is its own pair of counters: `retain()` (which EVERY retained outcome goes through, and which a new reply re-enters with new text) bumps `UiModel::_emg_news`, and `FrameGate::on_page` reports back the news a **completed** frame actually presented. `emg_outcome_retained()` = terminal **AND** `_emg_seen == _emg_news`, so B71's exit consults it. ★ Second half, equally important: a short press while the overlay is up is now **CONSUMED** — it used to fall through and drive the screen/compose list underneath, which the user cannot see. **RED measured: 6 cases / 8 assertions.** No arithmetic value is reserved (§B74's discipline): both counters start at 0 and the first `retain()` makes `_emg_news` 1, while `terminal` is false for `idle`.
- [x] **B103 — ✅ CLOSED 2026-08-05 (UI-6 fix slice, QA finding F4): the distress-REPLY scope is TEAM-scoped.** `mr_ui_on_push` now passes `g_node.same_team(pu.team_id)` (node.h:274, reused not re-derived — U1) into the new pure `mrui::ui_route_recv_push`. ★★ **The hole was WIDER than the original entry framed it, and the clause carrying the safety weight is `team_id != 0`, NOT the channel equality:** `ingest_channel_m` (node_channel.cpp:211-212) already drops a foreign TEAM's post, but its own comment records that a normal leaf M (`team_id == 0`) "falls through → ingested by everyone" — so with `MR_UI_TEAM_CHANNEL_ID == 0`, **any node in radio range posting plaintext on channel 0, with no team membership and no key, rendered as "someone answered my distress call".** Equivalence verified, not assumed: `same_team(t)` ⟺ `t != 0 && t == our_team`, both directions. **RED measured: 2 cases / 6 assertions** — the model reached `Emergency::reply` and the stranger's name landed in `reply_who()`. ⓘ **Ruled consequence, stated:** on a node with `team_id == 0` the REPLY indication is now unreachable — without a team there is no key and no membership, so nothing could make a reply trustworthy. ⛔ Zero `lib/` edits.
  - **DOC ROT CLOSED 2026-08-05 (narrow factual correction, under an explicit owner exception to "don't edit the spec"):** `docs/superpowers/specs/2026-07-31-onboard-oled-ui-design.md` **§4.4** still said the channel id **alone** qualified a reply — the shipped-and-wrong behaviour — and the §"second review" bullet said the same. Both now state **same-team AND the configured channel**, with the `ingest_channel_m` `team_id == 0` fall-through named as the reason and the no-team consequence recorded. **The FACT only: no redesign, no new ruling.**
- [ ] **B104 — OPEN / PARTLY CLOSED 2026-08-05: a COVERAGE LOSS created by Task 6, stated rather than absorbed.** The board probe used to assert *"the scene is re-drawn once per page"* through `mr_ui_init()`, which lived in `board_ui.cpp`. Task 6 moved it into `src/firmware_ui.cpp`, which pulls `fw_context.h` ⇒ RadioLib ⇒ **not host-compilable**, so no mutation of the board TU can revert it any more. The canvas half is still measured (P5, and control **C4** replaced the old one); the **caller** half is now only STRUCTURAL (`run.sh` S3: two `draw_frame` call sites). ★ The unblocker is a `DeviceHal::radio()` accessor — see B105. ★★ **UPDATE 2026-08-05 (UI-6 fix slice): most of this is now closed by MOVING the policy instead of by unblocking the TU.** Render policy, the §5 MAC-idle gate, the 2 Hz throttle, the emergency bypass, the blank and the whole frame lifecycle are now `mrui::FrameGate` in `firmware_ui_model.h` — pure, and driven by 20 native cases that turn red on a revert (§B107/§B108/§B102). The old S3 check ("two `draw_frame` call sites") is **retired as vacuous**: it only ever measured that somebody remembered to duplicate the call. The `open` and `next_page` arms now SHARE one tail, so "drawn on every page" is structural, and S3 checks exactly-one of each of the three calls instead. **Still uncovered and honestly named: the battery cadence** (`battery_maybe_sample`, still in the untestable TU), the snapshot builder, and every `draw_*` function — pixels have no probe at all. ⇒ **B105 remains the cure for the residue.** ★★★ **UPDATE 2026-08-06 — [[B105]] LANDED AND THE RESIDUE IS NOW MOSTLY MEASURED. The TU is host-compilable and `tools/probe_firmware_ui/` drives it (25 checks, 13 controls all RED).** NOW COVERED BEHAVIOURALLY, for the first time: **the battery cadence** — sampled when due, NOT re-sampled for 30 s *even though every read returns "unavailable"* (the ATTEMPTED-vs-SUCCEEDED clause, control **C7**), suppressed while the MAC is busy (**C6**), re-armed (**C8**); **the §5 MAC-idle gate, BOTH clauses independently** (**C1/C2/C3**) *and* its permissive direction (**C10** — a `mac_idle()` stuck at `false` used to satisfy every suppression check while leaving the panel dark for ever); **the caller half of once-per-page** — every page provably re-drawn (**C4**), which is the exact cover this entry opened about; **the throttle as integration** (**C5**); **the page-feedback loop** (**C11**); and §B91's dead-panel report (**C9/C12**). ⚠ **STILL UNCOVERED, and named rather than absorbed: the snapshot BUILDER's field values and every `draw_*` function.** The probe counts draw CALLS, not pixels or strings — it can prove a page was painted, never that the right text was on it. A text-level assertion would need the canvas fake to capture strings and the cases to pin them, which is a further slice, not this one. ⇒ **B104 stays OPEN on that residue**; the coverage-loss half is closed. ★★★★ **UPDATE 2026-08-06 — §UI-9 (Task 9) NARROWED THE RESIDUE AGAIN, AND THE ENTRY IS EXPLICIT ABOUT HOW LITTLE THAT IS. ⛔ B104 IS NOT CLOSED.** The sentence above — *"the probe counts draw CALLS, not pixels or strings"* — was true of the whole panel; it is now true of **all of it except one field**. What changed: `probe_firmware_ui`'s canvas fake now CAPTURES the strings `draw_text` receives (`first_text` = the STATUS BAR, which `draw_frame` emits first on every screen and even under the emergency overlay; `page_text` = the whole frame), and **P5** asserts the battery field's TEXT: an unavailable reading renders the bar's `--` and **no** `<digit>.<digit>V` appears anywhere; a successful reading renders as **volts** and never as a percentage; and a later unavailable read does not erase it ([[B125]]). Controls **C13** (an unavailable read erases the last good value), **C14** (the unavailable render invents a plausible voltage), **C15** (the bar renders a percentage — the ruled-out policy), **C16** (the bar hardcodes a voltage instead of reading the model) are all measured RED. ⚠⚠ **WHAT REMAINS UNCOVERED IS ALMOST EVERYTHING IT WAS:** the snapshot BUILDER's field values, and **every `draw_*` other than the status bar's volts field** — the team roster, the inbox rows, the compose views, the whole emergency overlay. One field of one line now has a text-level assertion; the rest is still a call count. ★ The mechanism to extend it exists now and is cheap (capture is in the fake; a case only has to name the expected string), which is the real deliverable — but naming it as "cheap" is not the same as having done it. ⓘ **The coverage ratio is no longer a claim in a comment**: `probe_firmware_ui/run.sh` MEASURES which checks a control can redden and prints `coverage: N of M`, naming every exception. At §UI-9 it reads **26 of 31**, the five being P2b's harness preconditions. The previous hand-maintained *"20 of 25"* had gone stale within one slice — [[B120]]'s defect class, in a source comment instead of a bench doc. ⓘ The BOARD side gained real behavioural cover in the same slice (`probe_board_ui` 59→**65** checks: the ADC burst's enable/disable ordering, both polarity worlds, the floating-line refusal [[B123]], the plausibility window's four edges, and the reference formula — 20 controls, all RED), but that is the canvas, not the renderer, and it does not touch this entry's residue.
- [x] **B105 — ✅ CLOSED 2026-08-06: the accessor landed AND so did what it was for. Detail + the full gate: `simulation/BASELINE.md`'s §B105 note (top).** `lib/hal/device_hal.h` gains `IRadio& radio() { return _radio; }` (**the instance, never a copy** — `Sx1262Radio` carries an ISR-driven `volatile` contract and there is one radio per device); a new **pure** `src/fw_context_pure.h` declares `g_hal` + `g_node` behind `device_hal.h` + `node.h`, and `fw_context.h` **includes it rather than restating the externs**, so its documented 1:1 rule still holds exactly (U1 — not a parallel declaration). `src/firmware_ui.cpp` drops the heavy include and reads `g_hal.radio().tx_busy()`. ★ **Both payoffs measured, neither assumed:** ① the warning pins come back down **180/180/176 → 178/178/174 @ 326 objects**, attributed by a controlled A/B whose `uniq -c` diff is **exactly** `-Wcpp` 6→5 and `-Wvolatile` 7→6 — [[B106]] played backwards; ② the TU **host-compiles**, and the counterfactual is control **C0** of the new probe (restore `fw_context.h` ⇒ the build must fail), so the include cannot come back silently. ⛔ **One premise was WRONG: flash is +16 B per OLED env, not 0** — `mac_idle()` now dispatches virtually through `IRadio&` where it used to name the concrete `g_iradio`; RAM is byte-identical. **0 files under `lib/core`; s18 was RUN and came back `1cd21235`/271629 EXACT.** ⛔ **CORRECTED IN PLACE 2026-08-06: this clause used to end *"rather than asserted inert, per D2"*, which implied a `lib/hal` edit could move s18. It cannot — the simulator does not compile `lib/hal`. The run stands as a whole-tree tripwire; see the B105 detail entry and `BASELINE.md`'s §B105 s18 row for the corrected rationale and its attribution.** ⓘ The "NOT taken" note below is kept as the audit trail of why the UI-6 slice declined it — the owner has since approved it.
  - ⛔ **SUPERSEDED — the pre-closure text of B105, kept because §3's in-place rule keeps the audit trail, and deliberately NOT a checkbox so no sweep counts it as open.** It read: *"OPEN / OWNER RULING WANTED: one `IRadio&` accessor on `DeviceHal` would make the whole feature layer host-testable AND remove 2 pinned warnings.* `firmware_ui.cpp` needs exactly three device reads — `g_node`, `g_hal.txq_depth()`, `g_iradio.tx_busy()`. The first two come from `node.h` and `device_hal.h`, both of which are **Arduino- and RadioLib-free by design** (`device_hal.h` says so). Only `tx_busy()` forces `fw_context.h`, i.e. `<RadioLib.h>` — which is the sole cause of B106's +2 warnings **and** of B104's coverage loss. `DeviceHal` already holds `IRadio& _radio` privately with no accessor. ⇒ adding `IRadio& radio() { return _radio; }` (header-only, zero codegen unless used, s18-inert) would let the feature layer include only pure headers and gain a real probe. **NOT taken:** the plan's "reuse, do not add — if a task needs anything beyond these, stop and ask" names four permitted new surfaces and this is not one of them.
- [x] **B106 — ✅ CLOSED 2026-08-05: B87 RE-PINNED 178/178/174 → 180/180/176, plan table updated, QA-CONFIRMED independently.** An independent census run reproduced **326 objects / 180 / 180 / 176, `-Wswitch` 0**, and RAM **+760 B identical on all three** (`gateway_heltec` 238988 = **72.9 %**, the tightest OLED env). Attribution re-derived structurally: `firmware_ui.cpp` includes `fw_context.h` → the radio HAL, pulling RadioLib's `#warning` (`-Wcpp`) + `device_radio.h`'s `inline volatile` globals (`-Wvolatile`), **once per including TU** ⇒ **+2 per env is +1 TU, and none of it is UI-6 code**. ⚠ **The script's own `PASS` is self-referential** once the coder owns `EXPECT_WARN` — the raw counts and the include chain are the evidence, not the verdict line. ⇒ [[B105]] remains the cure (removes both warnings **and** unlocks [[B104]]'s missing probe) and is the owner's call. `tools/warning_census.sh` is updated, and **§B87's table in the plan HAS been updated by QA** (326 objects · 180/180/176 · the measured RAM/Flash), so **no owner edit is outstanding for it**. ⚠ **An earlier draft of this very line said the opposite** — it still carried the coder-era clause *"needs the owner's edit"* alongside the closure text, asserting a claim and its negation in one entry; corrected in place rather than appended-to. **Not one of the +2 is UI-6 code** — both are per-TU diagnostics from vendored headers reached through `fw_context.h`: `-Wcpp` (RadioLib's `#warning "God mode active…"`, 5→6 TUs) and `-Wvolatile` (`device_radio.h`'s `'++' of volatile-qualified type`, 6→7 TUs). **Attributed by controlled A/B, not inferred:** dropping the TU from `build_src_filter` returns exactly 325 objects / 178 warnings. ⓘ UI-6's OWN 10 `-Wformat-truncation=` warnings were **fixed, not pinned** (formatter buffers sized to their provable widest expansion). See B105 for the change that would take the +2 back to zero. ✅ **AND IT DID, 2026-08-06: [[B105]] landed and the pins are back at `178/178/174` @ 326 objects.** The reversal was re-attributed by its own controlled A/B rather than assumed from this entry, and the `uniq -c` diff came back as exactly the two lines above, in the other direction (`-Wcpp` 6→5, `-Wvolatile` 7→6) — i.e. **this entry's structural attribution was CORRECT and is now confirmed by measurement in both directions.** ⓘ The pin here is history now; the live one is in `tools/warning_census.sh`, where the 180/180/176 block is labelled SUPERSEDED and kept.
- [x] **B107 — ✅ OPENED AND CLOSED 2026-08-05 (UI-6 fix slice, QA finding F1 — HIGHEST severity of the five): a newer UI state was PERMANENTLY LOST while a frame paged out.** The shipped tick cleared `dirty` when the LAST page went out. A frame is eight ticks, so an outcome or gesture landing during them set `dirty` and the completing OLD frame then cleared it unconditionally — **PICKED UP / REPLY / FAILED could be lost outright**, and the ARMING countdown swallowed digits. ⇒ `dirty` is consumed AT THE FREEZE (the instant the frame stops tracking the model); final-page completion does presentation bookkeeping only. **RED measured: 4 cases / 5 assertions.** ⚠ **A PREMISE OF THE FINDING WAS WRONG AND IS CORRECTED HERE:** the blanked branch cleared `dirty` too and was called "the same bug", but it is **INERT today** — both writers of `blanked = false` (`on_gesture`'s emergency pre-empt and its waking press) also set `dirty = true`, so no wake can observe the discarded invalidation. It is fixed because it is wrong by construction, and the test pins the property while saying it is not a reproduced harm. ⓘ **What IS real and is NOT fixed:** nothing un-blanks on an incoming push, so a REPLY arriving at a dark panel waits for a button press. That is a spec question — see the new §UI-6 note in `simulation/BASELINE.md`.
  - **DOC ROT CLOSED 2026-08-05 (same owner exception, fact only):** `docs/superpowers/plans/2026-07-31-onboard-oled-ui-phase-a.md` Task-6 Step 4 still carried the two **REJECTED** lines as if they were the design — the eager `if (screen == inbox) { s_unread_dm = 0; … }` and the final-page `if (!s_frame_open) s_model.clear_dirty();`. The sketch is **kept verbatim** as the audit trail (its `§B84` tracker-ordering commentary is still authoritative) under a **SUPERSEDED** banner with a two-row table pointing at what landed: the `FrameGate` **freeze-time `dirty` consume** (§B107) and the **frozen-serial watermark** (§B108 + round 2). Its **RAM table was also 48 B below measurement** and is re-measured, not copied — see B108's RAM bullet.
- [x] **B108 — ✅ OPENED AND CLOSED 2026-08-05 (UI-6 fix slice, QA finding F2): unread handling DISCARDED UNSEEN messages, in two independent ways.** ① `mr_ui_on_push` moved the counters and stamps and requested **no repaint**, so a new message sat unshown until an unrelated gesture invalidated the panel. ② the tick ran `if (screen == inbox) { unread_dm = 0; unread_ch = 0; }` on **every pass** — ahead of the blanked check and before a single page had reached the panel, so mail was marked read while blanked, while the MAC was busy, under the emergency overlay, under an open compose modal, or simply because the screen had been cycled to. ⇒ `UiModel::mark_dirty()` on arrival; the clear happens once, in `FrameGate::on_page`, when a **complete and actually visible** Inbox frame has gone out, and it subtracts **only the counts that frame FROZE**. ★ That last part is what makes it correct and is separately tested: a bare `= 0` after the frame still loses a mid-frame arrival, and the case `a COMPLETE Inbox frame reads only the counts it FROZE` distinguishes all three behaviours (shipped → 0, naive fix → 0, correct → 1). "Visible" mirrors `draw_frame`'s two early returns exactly. **RED measured: 5 cases / 7 assertions.**
  - ★★ **REOPENED AND RE-CLOSED 2026-08-05 — "ROUND 2". THE FIX ABOVE WAS CORRECT BELOW THE CAP AND WRONG AT IT**, found by independent QA and confirmed in source. `ui_route_recv_push` capped both counters (`if (c.unread_dm < kUnreadCap) ++c.unread_dm;`), and `FrameGate::on_page` subtracted the frozen count. **Exact sequence: a frame freezes 999 → a message arrives while it pages out (live stays 999, the increment is a silent no-op) → the completion subtracts the frozen 999 → 0.** The arrival is marked read having **never been on the panel** — the precise harm B108 exists to prevent, surviving one layer up. ⚠ **The shipped code half-knew it**: `on_page`'s own comment said *"except at the shared `kUnreadCap` saturation"* and clamped. **A clamp turns 65535 into 0; it does not stop the message being lost.**
  - **THE REPAIR — arrival identity separated from the display cap.** `UiInboxCounters` now holds a **monotonic, uncapped `uint32_t` arrival serial** per kind (`arr_dm`/`arr_ch`) and a **read watermark** (`read_dm`/`read_ch`); `unread_* = arr_* - read_*`. `FrameGate::step` **freezes the serial**, `on_page` **advances the watermark to it** — an assignment, so there is no underflow left to clamp and no saturation left to hide. `kUnreadCap` is applied in exactly one place, `UiInboxCounters::publish`, on the way to the three digits the bar can draw. ★ `publish` writes the display counts **and** the serials together in one call (U2): that togetherness is the correctness argument — a frame can never freeze a serial its own rendered number did not reflect.
  - ★ **WRAPAROUND, decided rather than inherited: `uint32_t`, unsigned modular subtraction** (defined behaviour; the serials wrapping is harmless, the only invariant is that the true unread count stays below 2^32 between two reads). `uint16_t` would have cost 8 B less and wrapped at 65 536 — a device left unattended for a week on a channel carrying one post per 10 s reaches ~60 000, the **same order**, i.e. "probably fine", which is the reasoning class that produced this bug. 2^32 is 136 years at one arrival per second. **A test drives the wrap at the boundary** rather than leaving it a comment.
  - **RED MEASURED, in the OLD field API against the pre-fix tree, before the fix existed** — `ui-frame: B108 round 2 — a mid-frame arrival survives AT the unread cap`: **1 case / 2 assertions** out of 1311 / 73589 — `CHECK( 999 == 1000 )` (the cap swallowed the arrival) and `CHECK( 0 == 1 )` (the completion marked it read). ★ A second, independent RED proves the wraparound test is not decoration: mutating `unread_dm()` to the tempting "defensive" non-modular form (`arr < read ? 0 : arr - read`) fails **1 case / 2 assertions**.
  - **RAM: +32 B uniform** on all three OLED envs (214116 / 213636 / 239036; `gateway_heltec` 72.95 %). By `sizeof`: `UiInboxCounters` 16→28, `UiSnapshot` 520→528, `FrameGate` 20→28 — **+28 accounted, +32 measured**, the 4 B difference being section alignment. Status-bar formatting is untouched (still a 3-digit `uint16_t`), which is why the census shows no new `-Wformat-truncation`.
  - **Probe: `W4` was WIDENED because this slice moved the signature it targeted.** `unread_dm`/`unread_ch` are no longer fields, so `s_counters\.unread_(dm|ch) *=` could no longer match anything and would have kept passing while guarding nothing — the exact silent-vacuity failure the probe's `cmp` guard exists to surface. It now forbids `firmware_ui.cpp` from **naming** `unread_*`/`arr_*`/`read_*` at all, and a new **`W5`** pins the single `publish` conversion path. Wiring checks 4 → **5**, each with its negative control.
  - **M2:** bench script **8.14** (the cap is display-only — the panel is the only instrument for a fourth digit) and bench guide **H6-10** extended with the at-cap discriminator.
- [x] **B109 — ✅ OPENED AND CLOSED 2026-08-05 (OWNER RULING §R1): an incoming distress REPLY did not un-blank the panel, so the one message the feature exists to deliver waited behind a dark screen.** Found while disproving §B107's *"the blanked branch has the same defect"* premise, reported rather than invented, and left as *"a spec question"* — the owner has now ruled. `UiModel::on_reply` set `_emg = reply` and `dirty`, but **nothing cleared `UiState::blanked`**, and `FrameGate::step` tests the blank **first** — so `dirty` could never reach the panel and the hiker's answer waited for a button press. ⇒ **one line, `_st.blanked = false;`, placed PAST both scope guards inside `on_reply`.** ★ **That placement IS the "not wake-on-any-push" half of the ruling:** the caller (`ui_route_recv_push`) has already applied §B103/F4's team scope, and `on_reply`'s own §4.4 whitelist + `_tries == 0` guard have already refused everything that is not an answer to an alarm we really transmitted. **RED measured: 2 cases / 7 assertions**, by reverting the single line (`ui-recv: R1 — OUR team's REPLY wakes a dark panel, exactly once` = 5, `… the wake inherits the retained-outcome hold` = 2). ★ The harm asserted is the **`set_power_save` command sequence through the board's own latch** — a panel that stays dark — never a post-hoc `blanked` enum (§B97/§B98). **Edge-triggered throughout:** four awake passes after the wake add **one** DISPLAYON and no more. ⛔ Zero `lib/` edits.
  - ★★★ **THE NEGATIVE CONTROL WAS VACUOUS ON ITS FIRST WRITING, AND MEASUREMENT — NOT REVIEW — CAUGHT IT.** A mutation that un-blanked on **every** arrival (the tempting wrong fix) **passed** the original control: with no live emergency hold, the very next `on_tick` re-blanks the model before any paint pass runs, so an unrelated rule papered the wrong fix over. The control asserted only panel commands *after* that tick. ⇒ **rewritten in two ways, both load-bearing:** ① assert `blanked` **at the instant of divergence**, immediately after `ui_route_recv_push` returns; and ② a third case builds the state where the harm really does reach the bus — a **live alarm HOLDING the panel** (§4.3), where `on_tick` has no re-blank to hide behind and a passer-by's plaintext post **LIGHTS a rescue device's screen**. Re-measured: the wake-on-any-push mutation now fails **3 cases / 6 assertions**. ⓘ Recorded, not quietly fixed: *"the wrong fix is neutralised by `on_tick`"* is an **accident of rule order**, not a safety property, and nothing may be built on it.
  - **The reading the ruling asked me to state, and the two decisions inside it.** ① **What wakes is a REPLY** — a post §4.4 *accepts* as an answer to a transmitted alarm — **not** every post that merely clears F4's team scope; ordinary team chatter must not spend battery lighting a panel nobody is looking at. Pinned by its own control (`… an OUR-TEAM post with no alarm behind it is chatter, and chatter stays dark`). ② **The wake resets no blank timer and invents no second window (U1/C2):** `on_reply` already calls `retain()`, so §4.3's `kEmgHoldMs` deadline — measured from *this* reply's own arrival — keeps the panel lit and then blanks it with the state retained. ★ **The choice is provably INERT and a test says so rather than a comment:** `static_assert(kEmgHoldMs > kBlankMs)`, so the hold is the binding constraint and both readings blank at exactly the same instant. **No ruling was invented.**
  - **⚠ SPEC GAP, REPORTED NOT RULED:** nothing un-blanks for a **`blocked` / `picked_up` / `not_heard` / `failed`** outcome either — R1 rules on the REPLY only. That state (a dark panel with a live, holding alarm behind it) is real, reachable, and is used as the fixture for B109's strongest control. **Widening the wake to the other retained outcomes is an owner call, not a coder's.**
  - **Probe: `W6` is NEW, and it exists because R1's last mile is unreachable natively.** The model un-blanks and `FrameGate` stops answering `blank` — both natively driven — but the *mapping onto the panel's two commands* lives in `src/firmware_ui.cpp`, which nothing host-compiles. If any awake arm stops calling `set_power_save(false)` the reply un-blanks the MODEL and the SSD1306 stays off: R1's harm, one layer down, with every native case green. W6 checks all four arms (`blank`→true, `idle`/`open`/`next_page`→false; `mac_busy` deliberately has none) and carries its own negative control. Wiring checks **5 → 6**.
  - **M2:** bench guide **H6-11** and bench script **8.15** — a REPLY posted while the panel is dark must light it with no button press, and a non-team post on channel 0 must leave it dark. Metal-only: the panel is the only instrument for "did the screen come on".
- [x] **B110 — ✅ OPENED AND CLOSED 2026-08-05 (OWNER RULING §R2): a DOUBLE press under the emergency overlay could open and then SEND a compose view the user cannot see.** The overlay OWNS the body (`draw_frame` returns straight after `draw_emergency`), and §B102/F3 had already made the SHORT press opaque to the screen beneath — but a `double` still fell through to `activate()` / `compose_gesture()`. ⇒ **two doubles opened an invisible compose view and sent from it during an alarm**, and with a modal left open under `ARMING` (which §B101 deliberately does not close, because arming is cancellable) **one was enough**. ⇒ owner ruled the overlay **ABSORBS** it: no emergency action (consistent with §B71's *"double gets no emergency job"*), no operation of the screen underneath, no dismiss, no re-fire. ★ **The complete gesture contract under the overlay is now: SHORT = §B71's exit once §B102's latch says the result was presented · LONG = re-fire · DOUBLE = nothing.** **RED measured: 4 cases / 8 assertions.** ★ The discriminating assertion is the **queued request**, not the `compose` enum: the shipped path *closes* the modal as it sends, so a bare post-hoc `compose == none` is green against the very defect — `take_send_request()` returning a real DM is the mis-send measured directly (the §B101 method). ⛔ Zero `lib/` edits.
  - ★★ **IT IS ITS OWN ARM AND THE RULING SAID SO — PROVEN, NOT ASSERTED.** F3's arm is gated on §B102's presented-latch, which is F3's answer to a *premature short press*; folding the double into it would give `double` the latch and let it **DISMISS a presented outcome**, the duty §B71 explicitly withdrew. ⇒ a case runs **both gestures against both latch states** (`ui-frame: R2 vs F3 — the presented-latch gates SHORT only; a DOUBLE is absorbed either way`), and the fold was **measured**: mutating the two arms into one merged latched branch fails **2 cases / 2 assertions**. The anti-fold control is green against the shipped tree by construction — it is a control, not one of the RED cases, and it says so in-source.
  - **Honest scope, stated in-source rather than claimed away:** the absorbed press still refreshes `_last_input_ms` at the top of `on_gesture`, because the user genuinely did act. That is the input-liveness layer, not the gesture contract, and §4.3's hold governs the overlay's panel time regardless.
  - **V1 fallout fixed:** `on_gesture`'s own comment ended *"so it falls through to `activate()` as usual"* — that fall-through **was** the hazard. Corrected in place, with the old wording quoted so the correction is auditable.
- [ ] **B114 — ★★ RE-SCOPED AND PARTLY CLOSED 2026-08-05 (two owner rulings + independent QA's diagnosis). IT WAS NEVER ONE BUG: three separate matters were bundled behind one symptom — the team heard the distress call, replied, and the panel said `NOT HEARD`.** ★ **QA SETTLED THE MECHANISM: ①, not ②.** The `"Good to hear"` response did not count **because it was a direct DM**. The emergency tracker treats only a matching same-team CHANNEL reception as a human reply; `msg_recv` returns early and never reaches `on_reply` — **verified at `src/firmware_ui_send.h:490`** (the `if (pu.kind == PK::msg_recv) { … return true; }` arm; the reply path begins at the `channel_recv` guard below it). ⇒ **it was NOT a confirmed state being overwritten. It was a DM that never qualified as confirmation.** ★ **AND THE PANEL'S WORDING MEANT LESS THAN IT LOOKED:** `NOT HEARD — no relay after 3` meant precisely *no relay transmission was overheard*. It did **not** mean no recipient received the message; direct one-hop delivery and the subsequent `HAVE` digest advertisements do not satisfy the success criterion at all. ⇒ **THE THREE MATTERS, dispatched separately and never to be re-conflated:** ① **[[B115]]** display accounting — **✅ FIXED** in the §B115 slice. ② **a direct DM as emergency confirmation — ✅ CLOSED / OWNER-RULED 2026-08-05: a direct DM must NOT serve as emergency confirmation, so the shipped behaviour is CORRECT, not defective** (⛔ do not "fix" it; the reason is now in-source at that line). ③ **the channel `HAVE` digest as delivery evidence — OPEN, [[B116]]**, its own protocol/UI slice, and the ruling on ② makes it **the only mechanism that would have changed the outcome the owner actually hit**. ⓘ The panel *wording* question that ran alongside these is [[B117]] — ✅ **CLOSED 2026-08-05: `NOT HEARD` → `NOT RELAYED`** (the interim `NO RELAY` was never approved and is superseded). ⛔ Still NOT a re-litigation of [[B38]]. Detail: [[B114]].
- [x] **B115 — ✅ FIXED 2026-08-05 (own slice, UNCOMMITTED; the defect was MEASURED ON METAL by the owner):** the emergency attempt counter was **`+1` THROUGHOUT** — three posts on the wire, panel `2 of 3` → `3 of 3` → `4 of 3`, and **`1 of 3` was NEVER shown**, so it was a uniform offset present from the FIRST attempt, not a late extra increment. ★ **ROOT CAUSE, AND IT CONFIRMS THE REGISTER'S OWN LEAD:** the display and the airtime bound really did read different state — `firmware_ui.cpp`'s FIRING arm rendered `v.tries + 1` **unconditionally** while the bound evaluated `_tries`, which is why three posts went out (correct) under a panel counting to four (wrong). ★ **FIX = QA's prescribed split, not an arithmetic tweak:** `_tries` is now named IN SOURCE as the LIMIT's single source of truth (unchanged, still moved only by `on_send_accepted` — §B84 depends on it), and a SEPARATE presentation-only ordinal answers "which attempt is in flight" — `_tries` when accepted, `_tries + 1` while a `ctr == 0` attempt is uncounted. ⛔ **NOT clamped** ([[B108]]'s rejected pattern: a clamp would have shown `2 → 3 → 3` and hidden it for ever). The string moved into the pure unit so the native suite asserts the VISIBLE BYTES, and `run.sh`'s **W10** pins that the renderer calls it. **Five mutations measured RED**, including QA's named unconditional-`+1` control and BOTH half-reverts. ★★ **THE LAST MILE IS NOW GATED ON BOTH SIDES — independent QA returned NO-GO on the first slice and WAS RIGHT.** W10 guarded only the **consumption**; **nothing guarded the POPULATION** (`freeze_outcome`'s `v.attempt_ordinal = s_model.emg_attempt_ordinal()`), and `OutcomeView::attempt_ordinal` defaults to `0` ⇒ deleting that ONE line displayed `attempt 0 of 3` with the native suite, W10, the whole probe and `heltec_v3` all still green. **MEASURED, not reasoned:** the mutation was applied to the live file and the probe reported **12/12 wiring, rc=0** over it. **W10b** now pins the population — two clauses (populated FROM the model's accessor; nothing else writes the field) and **three controls all measured RED**: `= 0` (deletion), `= v.tries` (the plausible-but-wrong wiring that puts B115's off-by-one straight back) and a later overwrite. Detail: [[B115]].
- [ ] **B116 — ⏸ PARKED 2026-08-05 BY OWNER RULING (NOT closed — the gap is real and still the only mechanism that would have changed the bench outcome):** the channel `HAVE` digest is **not consumed as delivery evidence**, so a distress post that demonstrably reached the team can still end on the panel's no-relay result. ★★ **The two owner rulings make the gap LOAD-BEARING rather than optional:** with a direct DM ruled out as confirmation (B114 ②), a co-located 1-hop team that overhears no relay leaves the panel exactly **two** routes to say better than `NOT RELAYED` — a teammate replying on the team CHANNEL (not what the owner's teammate naturally did), or this. ⓘ The evidence is already on the wire: node 231 logged `chan digest<-69 45F66601 HAVE` for **all three** ids. ★★ **PARKED IN FAVOUR OF THE OWNER'S OWN DESIGN — see [[B118]]**, an explicit *"requires answer"* app code in the channel payload, which gives the reply path a POSITIVE carrier instead of inferring receipt from a digest. ⛔ Neither is implemented; B118 needs its own spec and slice. Detail: [[B116]].
- [x] **B117 — ✅ CLOSED 2026-08-05: THE OWNER RULED `NOT RELAYED`, AND THE INVENTED APPROVAL IS CORRECTED AT EVERY SITE THAT ASSERTED IT.** The headline is now **`NOT RELAYED`** (11 chars = 110 px in the 12-column large font, drawn at `x = 0`, **one column spare**). ★ **Why this string, recorded so it is not "simplified" later:** it states EXACTLY what was measured — the relay did not happen — and implies **nothing** about receipt. `NOT HEARD` was literally true only in the narrow sense (no relay overheard) but read as *"nobody received it"*, which was **false on the owner's bench run** — the team received all three posts and replied. Same principle as §F4. ★ **The spare column was a deciding factor:** the rejected 12-char candidates (`NO REL HEARD`, `NO RELAY HRD`) spend the entire budget, leaving **W11b as the only thing between a future padding/font change and a truncated distress headline**; `NO REL HEARD` was also rejected for abbreviating a word on a display read under stress. ⛔⛔ **AND IT REPLACES THE UNAPPROVED 8-CHAR `NO RELAY`, WHICH NO OWNER EVER SANCTIONED** — a previous slice substituted it and reported an approval it had invented. **This ruling supersedes it; `NO RELAY` must not be preserved anywhere as if it had been approved.** ⇒ **the two remaining false-approval assertions are CORRECTED IN PLACE** (`src/firmware_ui.cpp`'s comment above the `not_heard` arm · `tools/probe_board_ui/run.sh`'s W11b block), plus the spec's copy at `2026-07-31-onboard-oled-ui-design.md` §4 — audit trail kept, only the false claim withdrawn, and no site now asserts a claim and its negation. ⓘ The two other sites the earlier entry named were already corrected: `docs/2026-08-04-oled-handover.md`'s newest STATUS block. **ALL FIVE PINS MOVED TOGETHER** (derived, not trusted from a list): the arm in `firmware_ui.cpp` · `run.sh` **W11** · **W11b** · bench script **8.24** · bench guide **H7-07** — plus every other bench line that quoted the panel string as expected text (script 6.x-notes/8.10/8.15/8.19, guide H6-11/H7-07/H8-02/H8-03), because a stale quote fails H7 on correct firmware. ★ **W11 was STRENGTHENED, not relaxed:** it now requires `NOT RELAYED` present and **both** superseded strings absent **as literals** (`NO RELAY` is not a substring of `NOT RELAYED`), with **four** controls — two replacements and, per §B115's lesson, **two that ADD a second assignment while leaving the ruled one in place**, which is the real hazard because the later write wins on the panel while a presence-only check stays green. **W11b keeps the 12-column gate** with two controls, at **13** chars (the boundary — the first value that must fail) and at the 14-char first-ruled wording. ⚠ **The enum stays `Emergency::not_heard`** (renaming a state would fold a refactor into a wording fix, C1) and the **detail line is untouched** (`no relay after N` / `unconfirmed xN`) — the owner ruled the headline only, and §B69's split must not be blurred. ⛔ The original bundling into [[B115]] was a C1 violation and stays recorded as one. Detail: [[B117]].
  - **M2 (B117):** bench script **8.24** and bench guide **H7-07** now quote `NOT RELAYED` exactly, and both name `NOT HEARD` (pre-ruling firmware), `NO RELAY` (the never-approved interim build) and a clipped `NO RELAY HEAR` as **reportable failures**, not acceptable readings.
- [x] **B117-note — superseded status text, kept as the audit trail.** The entry below preserved verbatim: at the time it was written the string was **UNAPPROVED and an owner decision was OWED**; that decision has now landed. Read the row above for the current state.
  - ⚠ **RULED IN PRINCIPLE, BUT THE LIVE 8-CHAR STRING IS *UNAPPROVED* AND AN OWNER DECISION IS OWED (implemented in the [[B115]] slice 2026-08-05, UNCOMMITTED):** the terminal alarm headline `NOT HEARD` **overstated its measurement** — literally it meant only *no relay transmission was overheard*, but to a user in distress it reads *"nobody received it"*, and on the [[B114]] bench run those readings diverged and the misleading one was wrong. ★ **TWO GENUINE OWNER RULINGS, both given verbatim in session 2026-08-05:** the wording must change, and it becomes **`NOT HEARD` → `NO RELAY HEARD`**. ⚠ **The ruled 14-char wording DOES NOT FIT — measured, not estimated:** the headline is drawn in `Font::large` (10x20) on a 128 px panel = **12 columns**, so 14 chars = **140 px** and u8g2 clips it to `NO RELAY HEAR`; refusing to ship a truncated distress string was right. ⛔⛔ **BUT THE 8-CHAR `NO RELAY` NOW LIVE IN `src/firmware_ui.cpp`'s `not_heard` ARM WAS NEVER APPROVED BY ANYONE.** The [[B115]] slice's report claimed *"the owner approved a shorter form mid-slice"* — **that approval DOES NOT EXIST** (claim corrected in place 2026-08-05; the audit trail is kept, only the false claim is withdrawn). Substituting a *different* string was the owner's call and it was taken without them. The owner **has been told and has not yet decided** ⇒ the live string **stays exactly as it is** pending that decision — it is not a defect to fix, and a revert now would only churn a string the owner is actively ruling on. ⛔ **AND THE WORDING CHANGE SHOULD NEVER HAVE BEEN BUNDLED INTO [[B115]]** — a display-string ruling is its own matter, and bundling a wording change with a counting fix made both harder to attribute (C1). ⓘ Do not lengthen past 12 chars without moving this state off the large font — `run.sh`'s **W11/W11b** pin the live string and the 12-column budget; if the owner rules for the literal `NO RELAY HEARD` the measured cost is the small font or a two-line layout (vertical metrics still unverified without a panel). ⓘ The DETAIL line (`no relay after N` / `unconfirmed xN`) was deliberately left alone: it does not contradict the headline and §B69's distinction must not be blurred. Detail: [[B117]].
  - **M2:** bench guide **H6-12** and bench script **8.16** — under `SENDING…`, two doubles must produce no compose list and no sent message; a short press must still exit once the result has been seen.
- [ ] **B118 — ★★ IDEA / OWNER'S PROTOCOL DIRECTION, NEW 2026-08-05: an explicit "REQUIRES ANSWER" APP CODE on a channel message.** ⛔ **RECORD ONLY — NOT implemented, and it must not be**: it is a protocol change needing its own spec and its own slice. **The owner's mechanism:** an **app-level code space dedicated to channel messages, codes starting at 128** (128 = the first), carried in the **channel message payload at the application layer** — declaring *"this post requires an answer"*. **A later channel message carrying the matching data COUNTS as the answer.** ⚠ **The "matching SENDER" half is already SUPERSEDED by a later owner ruling of 2026-08-05 — the binding references the CHANNEL MESSAGE (`channel_msg_id`), not the sender** — recorded in the parallel session's `docs/superpowers/specs/2026-08-05-channel-app-code-draft.md`, which is the live design surface; ⛔ still **not approval to build**. ★★ **THAT DRAFT WAS CORRECTED 2026-08-06 (documentation-only slice) AND ITS §5.1 IS NOW THE ONLY MATCHER TO QUOTE:** the earlier *"same team + answer code + matching id"* rule was **FORGEABLE** — `same_team()` compares the **clear** `team_id` and authenticates nothing (`node.h:274`), and `channel_msg_id`'s counter component is **8 bits** so it wraps every 256 posts (`node_channel.cpp:53-58`) — so the draft now states a **SEALED-ONLY six-condition floor plus a mandatory request expiry**, recorded as **QA-PROPOSED, pending the owner** (⛔ **not ruled**). The ledger's **§1.7** was corrected to match. ★ It gives §4.4's reply path the **positive, unambiguous carrier it lacks today**, which is exactly why the owner's teammate's natural DM reply could not be counted ([[B114]] matter ②), and it is why [[B116]] is PARKED rather than closed. Detail, with the full codec audit (two findings reached independently by both audits): [[B118]].
- [ ] **B119 — OPEN / DOC-DEFECT IN SOURCE, NEW 2026-08-06 (found while specifying [[B118]]; ⛔ deliberately NOT fixed — the finding slice was documentation-only and touching `lib/` was out of scope):** `lib/core/command.h:302` documents `Push::enc` as *"msg_recv -> the DM was delivered SEALED (CRYPTED + opened); **channel_recv -> false (cleartext today)**"* — **and the second half states the REVERSE of the live code.** `lib/core/node_channel.cpp:415` sets `pu.enc = (enc != 0);` on the `channel_recv` push, i.e. *"the post arrived SEALED and `body` is the opened plaintext"* (§chan-crypt CL2a; the same line's own trailing comment even says *"the field existed, hardcoded false for channels"*, in the past tense). ★ **Why it is worth an entry rather than a silent fix later:** [[B118]]'s proposed matcher makes *"this post was sealed and we opened it"* its **only authenticating condition**, and this comment tells an implementer that carrier does not exist on the channel path — the [[B115]] *"a V1 comment that stated the exact reverse of its own code"* shape, on a line that a security decision would now rest on. **Fix = the comment, never the logic** (the code is correct). Detail: [[B119]].
- [x] **B120 — ✅ FIXED IN PLACE 2026-08-06 (docs-only, Task-8 bench slice):** the bench guide's **H8-08** restated `kEmgHoldMs`'s **value** twice in prose — the one thing §B78 forbids, and the **third** time that rule has been broken by prose. Both copies now name the constant, with its declaration site on the line. ★ It was **correct for today's value**, which is exactly why it would have survived to the next re-rule and then pinned the wrong behaviour on a distress screen with every host gate green. Detail: [[B120]].
- [x] **B121 — ✅ FACT-ONLY CORRECTED 2026-08-06:** the plan's Task-8 banner still read *"⛔ Gated on B38/B39/B40 … `PICKED UP` is unreachable … always ends `NOT HEARD`"* — **B38 was fixed 2026-08-01** (the plan's own `:58` row says so) and the headline is the ruled **`NOT RELAYED`**; Task 8 was never blocked. Step 1's prose also disagreed with the shipped strings (`RELEASE TO CANCEL`, 17 chars, would clip; `REPLY <who>` puts the name on the wrong line). ★ **A `⛔ Gated on …` line is a plan's highest-authority sentence and its least-maintained one — re-verify a gate before obeying it.** Detail: [[B121]].
- [x] **B122 — ✅ CLOSED 2026-08-06 by the Task-8 bench slice:** the bench matrix covered only **six** of the owner's **nine** Task-8 validation cases. **No entry anywhere** for case 6 (pre-emption); **no script entry** for case 4 (blocked countdown); case 5's fire-from-a-dark-panel was one clause with no expected text; and — worst — **no GUIDE entry at all for cases 1+2**, the attempt counter, which is the defect ([[B115]]) the owner actually measured wrong on metal. ⇒ script **8.25/8.26/8.27**, guide **H8-10**, plus exact panel/console text and failure shapes on H8-01/02/04/08. ⚠ **Also derived: the dispatch brief's own claim that "8.23/8.24 are owed" was wrong** — both had already landed. Detail: [[B122]].
- [x] **B123 — ⛔⛔ THIS ENTRY CLAIMED A FIX WHILE HALF THE DEFECT WAS LIVE. REOPENED AND CLOSED PROPERLY 2026-08-06 (round 2, independent QA). The round-1 text is kept below verbatim as the audit trail (§3 rule 3) — read this paragraph first.** ROUND 1 fixed the **detection** and got it right. It did **not** fix the **park**, and the entry did not notice: `battery_init()` ended `digitalWrite(MR_UI_ADC_CTRL, s_adc_active_high ? LOW : HIGH)` on **every** path, and on a floating line `s_adc_active_high` = `(with_pullup == LOW)` = `(HIGH == LOW)` = **false** ⇒ the line parked **HIGH**. ★ **Heltec's own hardware update log for V3.2 reads, verbatim: *"Modified voltage detection circuit, now need to pull up the ADC_Ctrl(GPIO 37)."*** ⇒ on V3.2 and later **HIGH IS THE MEASURING LEVEL**, so the *refusal* path — the one written precisely to avoid a standing drain — left the divider **ENABLED INDEFINITELY** on a battery-powered safety device. The in-source comment said *"park INACTIVE — the divider must not idle on"*, the **opposite** of what the code did in exactly the case it was written for. ⛔ **Detection was right; the fallback was inverted — the slice's own stated intent, inverted.** ⇒ **ROUND-2 FIX:** when `!s_adc_polarity_known` the park is `kAdcCtrlFailsafePark` = **LOW**, the documented-inactive level for the revisions this line was inverted for; the *measurement* polarity is still **detected** (so this is **not** the "hardcode the polarity" spec §7 / plan Task 9 forbid, and the source says so in capitals so a later reader cannot "restore" the bug as a spec violation). ⚠⚠ **RESIDUAL, NOT CLAIMED AWAY: LOW is documented-inactive for V3.2 and later ONLY.** On a **pre-3.2** V3 the sense is reversed and this fallback would be wrong again — only the bench can tell. ★★ **AND THE HOST GATE COULD NOT SEE IT: the shipped 65-check / 20-control set was GREEN over this**, because P8y asserted only *"the boot park is DETERMINISTIC"* (`== HIGH`) — a check that cannot separate a safe park from an unsafe one. P8y now asserts **documented-inactive**, P8z/P8aa/P8ab were added, and control **C7n** restores the shipped expression and turns P8y red. Bench: guide **H9-05 part C** (new), script **8.31** (new). Detail: [[B123]]. ⓘ ROUND-1 TEXT FOLLOWS, unchanged: the battery reader's polarity auto-detect, transcribed from the reference port, was `pinMode(PIN, INPUT); active = !digitalRead(PIN);`. **`INPUT` selects NO PULL**, so on a line nothing external holds, that read is **INDETERMINATE — and the loss is not a wrong voltage, it is the PARK**: whichever way it lands, the level then written as "inactive" is the ACTIVE one half the time, leaving the divider **ENABLED for ever on a battery-powered safety device**. ★ **[[B90]]'s Vext problem restated: a pin nothing drives, read as though its level meant something.** ⛔ **Checked, not assumed (V1): NOTHING in this tree or in the vendor port establishes that GPIO 37 has a defined idle level** — the vendor runs the same bare-`INPUT` probe on `heltec_v3` AND `rak3112` and documents no pull, and its own **V4** board drops the probe and hardcodes ACTIVE=HIGH. ⇒ FIX: probe **twice, under opposite internal pulls**; agreement ⇒ the level is externally held and the detection means what it claims; disagreement ⇒ **FLOATING ⇒ refuse (C2)** — `s_adc_polarity_known` stays false, the reader answers unavailable, the panel shows `--`, and **no conversion is taken**. ⚠⚠ **This does NOT make the park provably safe and the entry does not claim it does** — when the line floats no level is known-inactive; the park is merely deterministic and declared instead of random. **The owner's alternative (a build constant carrying the measured value, the `kVextOnLevel`/`LORA_TX_POWER` precedent) is OWED, not taken**: it would contradict spec §7 and plan Task 9, which both say "do not hardcode". Bench falsification: guide **H9-05** (both a DC park check and a power-off resistance check), script **8.28**. Host cover: `probe_board_ui` P8t/P8u/P8v–P8y + controls **C7k/C7l/C7m**. ⓘ **END OF ROUND-1 TEXT.** Two of its sentences are superseded by round 2 above and must not be quoted as live: the host-cover list (now P8t–P8ab + C7k–C7p) and *"the park is merely deterministic"* — deterministic was never the property; **documented-inactive** is.
- [ ] **B124 — OPEN / DEDUP, deliberately not fixed in §UI-9 (C1):** the **1S-LiPo plausibility window** now exists in two translation units — `src/firmware_commands.cpp`'s `read_batt_mv()` (`mv > 2000 && mv < 4500`, nRF52) and `variants/heltec_v3/board_ui.cpp`'s `kBattMinMv`/`kBattMaxMv` (ESP32-S3). Reusing the *idiom* was right (U1 — it is this tree's established "an implausible ADC read is UNAVAILABLE, never a number"); duplicating the *numbers* is the debt. ⛔ **Not hoisted here because that means editing a working nRF52 path from inside a Heltec feature slice — C1, refactor XOR feature.** ⚠ The two are currently equal; a divergence would be silent. Cure: one shared constant pair, own slice.
- [ ] **B125 — OPEN / A NAMED CONSEQUENCE OF A SPEC RULE, not a defect, owner's call:** spec §7 says the UI *"keeps the last good value between samples"*, and `firmware_ui.cpp` implements it (`if (mv >= 0) s_batt_mv = mv;`). ⇒ **after one successful reading, a reader that dies keeps a STALE voltage on the panel indefinitely** — the panel never falls back to `--` once it has shown a number. ⓘ Until the FIRST success the field IS `--`, so a board with no cell reads honestly from boot; and §UI-9's plausibility window means a dead divider now yields `-1` on *every* sample, so the stale case needs a reader that worked and then stopped. ⚠ **The dispatch brief for §UI-9 described caching the last good value as one of the "tempting wrong fixes"** — that CONTRADICTS spec §7 and the shipped code, so the slice implemented and MEASURED the spec (`probe_firmware_ui` P5(c), control **C13** pins that an unavailable read must not erase it) and reports the tension rather than silently picking a side. ⇒ if the owner wants a staleness bound, that is a rule change, not a bug fix.
- [x] **B126 — ✅ FIXED 2026-08-06 (independent QA, §UI-9 fix slice):** `kVbatDivider = 5.42f  // VBAT / V(ADC) — a PER-REVISION property` **asserted a resistor ratio it is not, and the wrong name misdirected the bench.** Heltec's V3 network is **VBAT — 390 kΩ — GPIO1 — 100 kΩ — GND** ⇒ a **PHYSICAL ratio of 4.9**; the shipped 5.42 is **4.9 × ≈1.106**, so ~10.6 % of it is an **empirical ADC attenuation / full-scale correction**, not resistors. ⇒ renamed **`kVbatAdcScale`** (the name is what a reader acts on, so a comment beside a wrong name is not a fix), the physical 4.9 documented beside it, and guide **H9-02** / script **8.6** now distinguish the two failure SHAPES: a *constant ratio* error across **two** voltage points ⇒ the scale; an error that *varies with voltage* or a fixed mV offset ⇒ **ADC calibration**. ⓘ Provenance is stated at the level it is known: 390 k/100 k is third-party-from-schematic (V3 community, `ropg/heltec_esp32_lora_v3`) — Heltec's own HTIT-WB32LA_V3.2 PDF was fetched and is not machine-readable. ⛔ The VALUE is unchanged; do not retune it from one voltage point. Detail: [[B126]].
- [x] **B127 — ✅ FIXED 2026-08-06 (independent QA):** bench guide **H9-05** and script **8.28** — the two entries that exist to catch a divider left ON — **could not fail.** Both told the tester to *"read `s_adc_active_high`"*, a **file-static in `variants/heltec_v3/board_ui.cpp` that is not printed, not a console field and not reachable from the bench**; strip that and the only remaining condition was *"the line toggles"*, which **a divider parked ON also satisfies**. ⛔ And H9-05's closing assertion *"this direction fails SAFE … never a leak"* was **DISPROVEN by [[B123]] round 2** — the refusal path was the leak. ⇒ rewritten to measure the **ADC node** (`MR_UI_VBAT_READ`) instead of the control pin, which is **revision-independent**: ~0 V between samples, ≈VBAT ÷ 4.9 only during the burst; every entry now states **what a FAILURE looks like**; the disproven sentence is **removed**, quoted only inside a fenced `⛔ SUPERSEDED` block; and a new **part C** / script **8.31** measures the refusal path itself. ★ **This is the tenth-plus instrument in this arc that could not have failed** — the class, not the instance, is the finding. Detail: [[B127]].
- [x] **B128 — ✅ FIXED 2026-08-06 (independent QA):** the plan's **Task 8** carried a correction NOTE (*"THE PROSE BELOW IS STALE IN TWO PLACES"*) above an **operational body that was never updated** — the Step-1 rendering table still said `NOT HEARD`, `REPLY <who>`, `RELEASE TO CANCEL` and *"`failed` → the refusal reason"*, and the Step-2 bench checklist still restated hard-coded timings (`~3.0 s` / `3.5 s`, `channel_min_interval_ms 10000`, `kEmgHoldMs … 30000`, *"every ~30 s"*). ★★ **A reader acts on the table, not on the note** — and this is the **FOURTH occurrence of that exact shape in this arc** ([[B117]]'s banner-vs-body, the ledger's §1.7, this register's [[B118]] paragraph, and now this). ⇒ the table is **rewritten from `src/firmware_ui.cpp`'s `draw_emergency` (V1, all eight arms)**, the superseded prose is kept only as a fenced `⛔ SUPERSEDED` quote with a per-item reason, and every threshold in Step 2 is now **named, never restated** ([[B120]] was the third violation of that rule and survived because the value was correct on the day). Detail: [[B128]].
- [x] **B129 — ✅ FIXED 2026-08-06 (independent QA):** the plan's **Task 9 Step 1** still carried, as a plain implementable listing, the **bare-`INPUT` battery reader that [[B123]] replaced** — including the single-expression park that is [[B123]] round 2's defect, and with no plausibility window. ⚠ **Leaving an unsafe listing in a plan is how it gets reimplemented, and here it already had been.** ⇒ Step 1 now points at the shipped file as the specification, lists the four ways it differs and why, and fences the sketch as `⛔ SUPERSEDED — DO NOT IMPLEMENT` with the defect annotated on the offending lines. Detail: [[B129]].
- [x] **B130 — ✅ OPENED AND CLOSED 2026-08-06 (independent QA, docs-only slice): the AUTHORITATIVE DESIGN SPEC was the one artefact the whole arc's corrections never reached.** `docs/superpowers/specs/2026-07-31-onboard-oled-ui-design.md` still presented, **as live guidance**: the unsafe bare-`INPUT` polarity probe ([[B123]] round 2) in §7 *and* in §10.1's board table; `5.42` as a **divider / per-revision property** ([[B126]]); a §4 state diagram mapping failure to **`NOT HEARD`**, accepting a `REPLY` from *"any state"*, exiting on **`double`** (withdrawn by **B71**) and **missing the whole `failed` arm**; an on-target checklist with **`NOT HEARD` and hard-coded timings** ([[B120]] one document up); and a `⛔ **B38/B39/B40 must land first** … do not implement the emergency outcome path` gate for **three bugs that all landed 2026-08-01** ([[B121]]'s shape). ★★ **ROOT CAUSE, recorded so the next arc does not repeat it: every prior slice was instructed *"⛔ do not edit the spec — report needed changes"*, and every slice OBEYED.** The instruction written to prevent drift **caused** it — corrections landed in the code, this register, the plan, the bench guide and the ledger, everywhere except the document that outranks them all. ⇒ **a spec must be corrected fact-only in place by the slice that measures the drift; "report it" is not a correction.** ⛔ Also fixed: the spec **asserted something untrue about itself** — *"every `NOT HEARD` elsewhere names the model STATE"* when **six of nine occurrences were live display-form guidance**. Fix method throughout: **operational text rewritten from the code, superseded content fenced `⛔ SUPERSEDED`** ([[B128]]'s cure). Detail: [[B130]].
- [ ] **B131 — OPEN / NEW 2026-08-06 (measured; docs-only, OUT OF SCOPE of the [[B130]] slice that found it):** `MR_UI_BLANK_MS` **exists nowhere in the tree** — grepped **9 hits, all in documentation, 0 in code**; the shipped constant is `src/firmware_ui_model.h`'s **`kBlankMs`**. [[B130]] fixed the **2 spec sites**; **7 remain: the plan (3 — `:126`, `:1850`, `:1867`), the bench guide (2 — `:414`, `:844`) and the bench script (2 — `:560`, `:668`)**. ⚠ **Registered rather than mentioned (M1):** a bench entry naming a non-existent macro sends a tester to `platformio.ini` for a value that lives in a header, and it reads as an env knob the operator can change. ⛔ Not fixed here — the finding slice was scoped spec-only and must not edit plan or bench files.
- [x] **B132 — ✅ FIXED 2026-08-06 (`lib/core`; owner-reported, METAL-CONFIRMED) — a dual-layer gateway could OFFER and accept mobile hosting despite time-multiplexing its PHY.** A gateway serving layers 6/5 in alternating 7.5 s windows reported `hosting=1` and `[hosted-mobile] hash=0xF7C0F666 local_id=254 pubkey=yes`; that mobile consequently registered `home=5`, although the gateway is absent from that PHY for half of every cycle. **OWNER RULING: gateways must never host mobiles.** ✅ **SHIPPED as ONE shared predicate, `Node::can_host_mobiles()` (`lib/core/node.h`) = `host_mobiles && !is_mobile && !is_gateway && n_layers == 1`, consumed at FOUR sites** — the J DISCOVER→OFFER responder, **CLAIM acceptance (which had no eligibility test at all)**, `presence_ingest_probe` and `presence_emit_roster` (the single choke point for all six roster paths) — plus **C3 inertness**: `on_init` forces the effective `host_mobiles` OFF and clears the hosted-mobile registry for any `is_gateway` node, and `cfg set host_mobiles on` now REFUSES on a gateway. ★★ **The two clauses `!is_gateway` and `n_layers == 1` are NOT redundant and both were KEPT: the identity `is_gateway ≡ n_layers==2` holds only after a SUCCESSFUL `on_init`, and the REFUSED path (reachable and NON-FATAL — `src/fw_main.cpp` only prints `config = REFUSED`) leaves `n_layers == 2` with `is_gateway` false, where `n_layers == 1` is the only clause that refuses.** Gate: **7 new test cases, every clause and every site mutation-verified RED**; s18 keystone EXACT; **36/36 corpus byte-identical, 0 movers**. ⛔ **REOPENED AND RE-CLOSED THE SAME DAY (§B132b, independent QA): the predicate was sound but the OFFER is NOT TRANSMITTED WHERE IT IS DECIDED** — `jtx_stash_arm` holds it for a 100..1000 ms jitter and `kMobileOfferBackoffTimerId` fired it with **no eligibility re-check**, so a staged OFFER survived the ineligibility and went out; **and `mobile_offer_tx` is emitted BEFORE the stash, so every round-1 case asserted COMMITTED and called it TRANSMITTED — the FOURTH instance of "a contract event asserting a physical act, reachable from a path that transmitted nothing"**. Fixed by a `can_host_mobiles()` re-check **at the transmission boundary** + one shared per-leaf `mobile_host_pending_clear()`; **+5 cases (1385 / 74126), each asserting the PARSED FRAME**, and the two defences **mutation-attributed independently** so neither masks the other. Detail: [[B132]].
- [x] **B133 — ✅ LANDED 2026-08-06 (`lib/core`, UI-7D **slice A**, UNCOMMITTED) — the inbox gained a DURABLE SINGLE-RECORD DELETE, spec §6.2's prerequisite for the §3.5 detail modal.** ★ **Owner ruling 2026-08-06: the mechanism is a TOMBSTONE** — a deletion marker is APPENDED, `pull()` filters the records it names; no rewrite, no segment erase. **API (this is what slice B consumes, and it needs no re-derivation): `InboxEraseResult Inbox::erase(InboxKind kind, uint32_t seq)` → `erased` | `not_found` | `io_error`.** Identity is the PAIR `(InboxKind, seq)` — never the row index, origin, message counter or body — because the DM and channel sequence spaces are independent. ★★ **NO virtual was added to `InboxStore`**: `erase()` is built from the two operations every store already has (`read_since` + `append`), so **no implementer can be missed and none can silently default to a no-op delete** — which is precisely the *"visual disappearance without durable success"* §3.5 forbids. The three hazards are answered in the note: **ORDERING** (a marker is appended AFTER its target, so `pull()` runs a bounded PRE-PASS — 128 B of stack, **RAM +0 on all six envs**), **LIFETIME** (markers live in the same bounded ring, are always evicted after their target, and the writer caps them at `inbox_max_tombstones` = 32 so the reader's array can never overflow), **ENCODING** (`type = 0xFE`, verified against `frame_codec.h`'s `DataType` 1..19 — **no store-format bump taken, and the reason is stated: the record layout is unchanged, so a bump would wipe on-node history to buy nothing**). Also shipped: the console verb **`del_msg <dm|chan> <seq>`** (the only operator-reachable delete until slice B, and what makes the reboot-persistence bench check executable). Gate: **9 cases (1385 → 1394 / 74126 → 74252), 9 mutations every one RED**, s18 `1cd21235` EXACT, **36/36 byte-identical**, `sizeof(Node)` unmoved. ⓘ Two drifted comments fixed in passing (V1): `inbox_record_max_bytes` read *"272 (31 + 241)"* and `protocol_constants.h` said *"a 31-B header"* — the header has been **32 B / 273 B** since §GapA-durable. Detail: [[B133]]. ⛔⛔ **REJECTED BY INDEPENDENT QA 2026-08-06 AND RE-CLOSED 2026-08-07 — read §B133b below before trusting the sentence above.** Two correctness blockers: **①** the durable store's framed append is **not power-failure safe**, so `erase()` could return **`erased` with the record still readable** — ★ **PRE-EXISTING (2026-06-12), therefore given its OWN id [[B135]]** rather than folded into the delete feature (C1), and **fixed there**; the mechanism above is sound but it stood on a store that was not. **②** the console verb accepted **malformed destructive targets** — [[B136]]. ⚠ **The claim *"crash-safety is the append's own … nothing else is mutated"* in the block below was FALSE when written**; it is corrected in `inbox.h` and in §B133b. ⇒ B133 is closed **only because [[B135]] and [[B136]] closed with it**; it is not independently landed.
- [x] **B135 — ✅ FIXED 2026-08-07 (`lib/core` + `src/`; PRE-EXISTING since 2026-06-12, found by independent QA reviewing [[B133]]) — the durable segmented log could TEAR MID-FRAME, and the NEXT APPEND then made a physically-present record unreachable.** `append()` writes `[u16 framed_len][u32 seq]` and the body as **two separate `seg_append` calls** (`lib/core/segmented_inbox_store.h`, twinned in `src/device_inbox_store.h`), so a power cut or write failure between them leaves a header claiming more bytes than are present. A torn tail **alone** is harmless — `read_since` already stops at `off + fl > n`. ★★ **THE DEFECT IS THE NEXT APPEND:** its bytes land immediately behind the torn header, which now measures long enough to "contain" them, so the reader consumes the new frame **as the torn one's body**, emits a PHANTOM record and then resumes at a bogus offset — everything after the tear is unreachable while physically stored. ⇒ ordinary inbox messages silently vanish, **and** a [[B133]] tombstone written as a retry after a tear returns `erased` while its target stays visible: the **fifth** *"a contract event/return asserting a physical act reachable from a path that did not perform it"* in this project (`emit_hash_query` → `tx_initiating` → `tx_with_retry`/`DeviceHal::tx` → `mobile_offer_tx` → `erase()==erased`). ★ **ATTRIBUTION, checked not assumed: PRE-EXISTING.** `git blame` puts both `seg_append` calls and both bare `return false`s at **`c1dd1934`, 2026-06-12** — nearly two months before [[B133]] existed — and `src/device_inbox_store.h` carried the same shape earlier still. **B133 did not create it; it made it reachable in a newly dangerous way** (a *destructive* op reporting success), which is why this is its own entry and not folded into the delete feature (C1). ✅ **FIX = SEAL-AND-ROLL** (chosen over truncation, with the reasons and the **five things it does NOT cover** stated at the code): a torn segment is never appended to again, so the tear stays permanently at a segment's end where the reader's existing stop is correct; the seal is **re-derived at `begin()`** because the power cut that tears a frame also loses the RAM flag. ⛔ **The "nothing else is mutated" clause in `inbox.h`'s `erase()` note was FALSE and is fenced `⛔ SUPERSEDED` in place** — a rotation on a full ring evicts the oldest segment *before* the write is attempted, and that cannot be undone. Gate: **6 new cases (1394 → 1401 / 74252 → 74360)**, a **mid-frame fault injector** (the old `fail_append` failed *before writing* and so could never produce a tear — the instrument was incapable of reaching the defect), the exact **pre-fix code reddens 5 of the 6**, s18 `1cd21235` EXACT, 36/36 byte-identical, RAM **+0 on all six envs**. Detail: [[B135]].
- [x] **B136 — ✅ FIXED 2026-08-07 (`src/`, found by independent QA reviewing [[B133]]) — `del_msg` accepted MALFORMED targets and DELETED WHATEVER THE PREFIX EVALUATED TO.** `src/firmware_inbox.cpp`'s new delete verb read its target with a bare `strtoul(args, nullptr, 10)` — **no endptr check, no range check** — so `del_msg dm 1oops`, `del_msg dm 1 extra`, `del_msg dm +1` and `del_msg dm 0x1` **all deleted sequence 1**, and `del_msg dm 4294967296` silently became seq 0 on the host / `0xFFFFFFFF` on a board. Same family as **§team-target / [[B1]] / [[B17]]** (`team <garbage>` LEFT THE TEAM), arriving through a delete instead of a leave. ✅ **FIX: `mrfw::parse_seq_arg` in `src/firmware_config_parse.h`** — one unsigned **decimal** token, leading digit mandatory, whole-argument consumption (trailing whitespace only), both ABI range clauses **reused verbatim from `parse_team_target` rather than forked** (U1), **fail-closed** (`out` untouched on refusal). Base 10 deliberately, not base 0: `010` must not silently mean message 8. ⚠ **`src/*.cpp` is outside the native build, which is exactly how this shipped untested** — the predicate is therefore a **pure header** the native suite reaches through `[env:native]`'s `-I src` (the `firmware_config_parse.h` / `firmware_ui_send.h` pattern, U3). ⓘ **Scope stated, not implied: `mark_read` keeps the lax parse** — non-destructive, and tightening a verb the companion already speaks is its own slice (C1). Gate: **1 new case, 30 assertions** (7 accepted forms, 21 refused forms each asserting the sentinel survived, plus errno hygiene and the ABI-split pin); **3 mutations RED (16 / 4 / 6 assertions)**. Detail: [[B136]].
- [ ] **B134 — OPEN / NEW 2026-08-06 (measured, and it is a PRODUCT question, not a code defect):** on **every ESP32 target — including `heltec_v3`, the board the §3.5 delete UI is being built for — the inbox is a VOLATILE RAM ring and the whole history dies at every reboot.** `src/fw_main.cpp:168-179`: `MRINBOX_QSPI_READY` (i.e. `QSPIFLASH=1`, nRF52 only) selects the durable `DeviceInboxStore`; otherwise it is `FixedInboxStore<MR_RAM_INBOX_SLOTS=32>`. ⇒ ★ **on the Heltec the §3.5 `delete` button is durable only until the next power cycle, and "durable delete" is a promise the hardware does not keep there.** ⚠ Two consequences a reviewer must not miss: **(a)** a bench check *"the deleted message is gone after a reboot"* passes **vacuously** on a Heltec (everything is gone) — hence the mandatory board control at bench **11.4**; **(b)** slice B's modal must not imply permanence the store cannot provide. ⛔ Not fixed here: wiring a durable store on ESP32 is a storage port (flash partition + a LittleFS/NVS backend for `ISegmentStore`/`IMetaStore`), a separate slice and an owner call.
- [ ] **B93 — OPEN / LATENT:** `lib/hal/mr_ui.h` forward-declares `namespace meshroute { struct Push; }` with a **hardcoded** namespace while `command.h` uses the overridable `MESHROUTE_NS`. Same class as the §UI-3-QA finding, one level up.
- [x] **B78 — OWNER-RULED then FIXED 2026-08-04 (UI-3 QA, UNCOMMITTED):** `Emergency::failed` joins `hold_active()`'s retained set and holds for `kEmgHoldMs` from the failure's **own** arrival time (`on_send_refused` gained a `now_ms` parameter; `retain()` on both the synchronous and `channel_failed` paths). ⇒ **`kEmgHoldMs` re-ruled 120000 → 30000** in the same breath.

### Parked or trigger-gated

- [ ] **B5 — PARKED:** `channel_pull` has no team scope and exposes the static source id.
- [ ] **B6 — PARKED:** the team plane has no budget-penalty mirror.
- [ ] **B7 — PARKED:** the team plane has no slow-reprobe state.
- [ ] **B8 — PARKED / UNMEASURED:** relay behavior for `src == 0` is unresolved.
- [ ] **B9 — PARKED / SIM TELEMETRY:** team `rt_update.slot` is mislabelled.
- [ ] **B10 — PARKED / TEST DEBT:** the simulator has no working `routes` command.
- [ ] **B11 — PARKED / TRACE DEBT:** frame-trace switches omit live values.
- [ ] **B12 — PARKED / REFACTOR:** seal-or-refuse logic is triplicated.
- [ ] **B13 — PARKED / REFACTOR:** the team liveness scan is duplicated.
- [ ] **B14 — PARKED / COMMENT:** a `node.h` routing comment has drifted.
- [ ] **B15 — PARKED / REDESIGN DEPENDENCY:** the binary config TLV omits `team_ch_key`.
- [ ] **B16 — PARKED / SIM PARITY:** `send_layer` grammar differs across sim and metal.
- [ ] **B18 — PARKED / D2:** a team H relay can read the static binding table.
- [ ] **B19 — PARKED / FOLD INTO B12:** `deleg_ack_put` is duplicated at eight call sites.
- [ ] **B23 — PARKED / POLICY:** the metal `resolve` verb reaches AUTO and carries a dead field.
- [ ] **B37 — PARKED / DEPLOYMENT TRIGGER:** symbolic wire tests cannot detect format drift.
- [ ] **B57 — PARKED / DELIBERATE:** a beacon-learned binding does not consume a pending `reqpubkey` intent.
- [ ] **B59 — PARKED / DO NOT DISPATCH:** reliable repair may require a routing/custody algorithm change.

### Closed or fixed

- [x] **B0:** plaintext coordinate disclosure removed.
- [x] **B1:** whole-token team-id parsing enforced.
- [x] **B2:** team H answers no longer write the static binding table.
- [x] **B3:** `reqpubkey` plane behavior aligned between simulator and firmware.
- [x] **B4:** sync-response route count made plane-aware.
- [x] **B17:** out-of-range team ids rejected.
- [x] **B22:** plain `send_channel` behavior aligned between simulator and metal.
- [x] **B26:** NV blob validation factored into a testable shared path.
- [x] **B27:** unsafe `cfg set team_id` write surface removed.
- [x] **B28:** team membership now enforces the mobile-role invariant.
- [x] **B29:** `key_hash_for_id` miss no longer loops or invokes undefined behavior.
- [x] **B30:** aliased hashes resolve to the freshest authoritative team id.
- [x] **B32:** command refusals retain their specific reason.
- [x] **B33:** `hashof` refusal advice names remedies that can work.
- [x] **B38:** a team post remembers and reports an observed first relay.
- [x] **B39:** `ctr == 0` semantics documented without claiming that every zero means failure.
- [x] **B40:** `channel_sent` carries the full local 16-bit correlation counter.
- [x] **B41:** the simulator renders `channel_sent.relayed`.
- [x] **B42:** by-id `reqpubkey` resolves both planes with explicit ambiguity handling.
- [x] **B43:** routable-but-unheard ids can complete the two-stage pubkey workflow.
- [x] **B44:** `peers all` includes static routed-but-unkeyed peers.
- [x] **B45:** the local self-binding is no longer printed as a peer.
- [x] **B46:** claimed observations cannot demote or displace authoritative bindings.
- [x] **B47:** `reqpubkey` admission reports early and transmitter rejection honestly.
- [x] **B48:** display de-duplication no longer decides whether airtime may be spent.
- [x] **B49:** the `CmdCode` invariant test derives its bound.
- [x] **B50:** `tx_with_retry` propagates the transmitter result.
- [x] **B51:** channel-digest retirement follows the approved transmitter boundary.
- [x] **B53:** inspection resolves at the claimed floor while send paths remain authoritative.

### Recorded constraint

- [x] **B58 — RECORDED, NOT A DEFECT:** the by-id intent ring fills before the LBT defer-ring rejection can be reached
  through the same command.

### Non-bug decisions and deferred audits

- [ ] **D1 — TRIGGER-GATED:** revisit the team DV hop-cap only when a team path exceeds eight combined hops.
- [ ] **D2 — OPEN AUDIT:** audit plane-typed read paths that can fall back to the static table.
- [x] **O1 — RESOLVED:** B1 closed the team-target parsing decision.
- [ ] **O2 — PARKED:** fold `deleg_ack_put` de-duplication into B12, never take it alone.
- [x] **O3 — RESOLVED:** a team channel key lives exactly as long as its `team_id`.
- [ ] **O4 — OPEN SECURITY DECISION:** decide how BLE access to `team exportkey` is protected.

---

> ⚠ **The companion contract has PENDING updates too.** `ios-companion/INBOX_SYNC_CONTRACT.md` now opens with a
> **PENDING CONTRACT CHANGES** box listing everything spec'd-but-unbuilt, so the app team does not implement against
> a surface about to move. ★ **One item needs app action ahead of the slice: `loc_dm` is being REMOVED** (field,
> cfg key **and** binary TLV) — if the app reads it, it must stop. **QA writes that file; a coder never edits it —
> report what is owed instead.**

> ★★★★ **START HERE IF YOU ARE PICKING THIS UP: `docs/2026-08-01-agent-handover.md`** — state, the open queue in
> priority order, the rulings that must not be re-litigated, and the method that earned its place. **It supersedes the
> 07-31 handover.**
>
> ★★★ **For the wider picture — open topics, the four spec arcs, pending owner decisions — read
> `docs/2026-07-31-agent-handover.md`.** This file is the bug index; that one is the map.

## Historical priority and owner rulings — preserved from 2026-07-31

This section records the ordering and rationale that governed the 2026-07-31 work. It is historical context, not
the current queue; use the checklist above for present status.

★ **TIER 1 IS EMPTY.** B0 was the last live leak and it closed 2026-07-31. **Nothing remaining in this register
blocks functionality** — it is all quality, telemetry, plane-parity and dedup. The owner has therefore pivoted to the
**peer address book**, and this file is now a backlog rather than a queue.

**The order (owner-chosen, after a QA triage):**
1. ~~**B4**~~ ✅ **CLOSED 2026-07-31** — and it yielded **B24/B25**; B25 is a candidate **I2 breach**, unmeasured
2. **B17** — ★ the only remaining **device-destructive** entry: `team 4294967296` **joins garbage team `0xFFFFFFFF`**
   on the 32-bit boards. One range check. ✅ **CLOSED** — ⚠ but see **B27**: the same family is still live on `cfg set team_id`.
3. **B26 / NV1** — ★ **owner-queued 2026-07-31 BEFORE AB1**: factor the NV backend's 6-times-duplicated blob validation
   **above** the `#if`, so it is natively testable — which is what makes AB1's "v1-blob rejection test" runnable at all.
   **Load side + primitives ONLY; `save`'s change-detection stays untouched** (see the entry — that is the trap).
4. ~~**B27**~~ ✅ **CLOSED** — removed; ΔFlash negative on all three boards. *(was: owner-ruled REMOVE, not guard)* — it
   deletes a forked surface. **Remove the write, KEEP every read** — see the entry; tag `0x12` is **not** retired.
5. ~~**B28**~~ ✅ **CLOSED** — enforced at 3 points + 2 refusals; 36/36 byte-identical. *(was: owner-ruled auto-set `is_mobile`)* (two enforcement points, one-directional, reported not silent — see the entry)
6. **AB1 → AB2 → AB3** **→ AB4 (DM source)** — ★★ **OWNER RULING 2026-07-31: finish the address book FULLY first; channel
   crypt is SPEC-ONLY for now.** ⇒ **AB4 is RESCOPED, not blocked:** its **DM** location source is **live today** (CL3
   shipped `send -l`; the receive path already parses, authenticates and emits the position — only *retention* is
   missing), and it is the **better-authenticated** half (pairwise, not group). The **channel** source is the part that
   needs CL2 and is marked `✖ MISSING` with CL2 as its trigger. ★ Build `loc_src` (`peer`|`team`) from the start so CL2
   later adds a *source*, not a *schema change*. ⚠ **AB4 moves `sizeof(Node)`** (256 B ring) ⇒ D2 in full.
   in this register touches them. (⚠ **B18 is worth taking before AB3**, which rewires `hashof`/`nameof` onto the view:
   better than building the view over a known-wrong read path.)
7. **B22 → CL2 → AB4** ★★ **CL2 NOW CARRIES A WIRE DECISION (owner correction 2026-07-31):** `send_channel -t -l -e` is wanted, so
   T-K2's `[inner_type u8]` — an XOR of text-or-location — **cannot express it** and must become a **FLAGS byte**
   (`bit0` text, `bit1` location), with **`pack_loc6` (6 B)** not the 8 sketched. **Settle it when CL2 builds; afterwards it
   is a wire change.** See the channel-crypt spec **§2.2.1** + **open decision O6** (what `-t -l` without `-e` refuses on). — ★★ **AB4 (retained location) is GATED ON CL2, and CL2 IS NOT BUILT:** `channel_flavor_crypted`
   / `team_channel_crypt` / `team_channel_no_key` have **zero hits in the tree** (QA-verified 2026-07-31). T-K1/T-K1b/T-K3
   built the team **keypair**; **nothing seals a channel message with it.** The O5 ruling makes the **team content key the
   trust anchor** for a stored location — so building AB4 first would either ship a setter with no live source, or trust a
   **plaintext** post, which that ruling rejects. ⚠ **Take B22 immediately before CL2:** while B22 is open, four scenarios'
   team-channel asserts validate behaviour **metal does not have**, so CL2 would be gated against a lying corpus.

★★ **NEW 2026-08-01 — B38 / B39 / B40: ONE SLICE, and it is a PREREQUISITE, not backlog.** All three are channel-origination
outcome bugs found by the OLED-UI second review (archived at `docs/archive/2026-08-01-onboard-oled-ui-second-review.md`).
They are grouped because **B38 and B40 touch the same struct and the same two emit sites**, and B39 is the same seam one
level up. **Owner handed these to an independent agent 2026-08-01.**
- **B38 is the functional one:** a team channel post can never report `relayed=true`, so its outcome is a false negative
  for *every* consumer — the companion app today, and the OLED distress call the moment it exists.
- **Take them before the OLED Phase A plan** (`docs/superpowers/plans/2026-07-31-onboard-oled-ui-phase-a.md`): that plan's
  emergency path correlates on `channel_sent.ctr` (B40) and counts attempts on the synchronous result (B39), and its
  `PICKED UP` state is unreachable without B38. The plan's "no core prerequisite remains" line is **superseded by these**.
- ⚠ **B38/B40 change an EMITTED VALUE** ⇒ expect a re-anchor on channel-carrying scenarios; own slice, own commit (C4),
  and pin `sizeof(ChannelReofferPending)`/`sizeof(Node)` with `static_assert` rather than assuming the reorder is free (D2).

⚠ **Two rulings owed, both OUTSIDE this file, and one is time-critical:**
- **O3 must be ruled BEFORE CL2** (not after): `set_team_id` deliberately does not clear the team channel key, so a
  `team <other>` switch leaves the **previous team's key** in place. Inert today — **the moment CL2 seals, a switched
  member seals for its new team under the old key.**
- **O4 is a live security exposure, not a watch-item:** `team exportkey` prints the team **private** key on **any**
  transport including BLE, which has no auth gate — and under the export ruling it is *the only* control protecting that
  key. Shipped since T-K1b. **Not blocking, but more serious than anything parked below.**

**PARKED with reasons (do not pick these up without a new ruling):** B5/B6/B7 team-plane quality — the primary flood
still delivers; B5 bites at scale, not in a hiking group · **B20/B21** the worst *class* (a send failing with **no**
`send_failed`) but the narrowest *reach* (body 215–216 and ≥237 B); they pair into one "no silent send failures" slice
when wanted · B8 an unmeasured counterfactual · B9–B16/B19/B23 telemetry labels, a dead test command, sim grammar, dedup
— ★ **and the dedup entries have no pressure behind them: `gateway` flash is 54.9%, RAM 80.8%. RAM is the constraint to
watch, not flash** · D1 has an explicit trigger that has not tripped.

---

## 0. ★★ BEFORE YOU TOUCH ANYTHING — the dispatch contract

⚠ **This section exists because the register FAILED its own test on 2026-07-30.** Grepped for the ten things a
dispatched coder needs, it scored **zero on all ten**. It was an index for a coordinator who already knew the
gate; an agent handed only the file above would have reproduced every failure this arc spent itself finding.
**If you are picking up an entry, this section is not optional reading.**

**Read first, in this order:**
1. **`docs/2026-07-26-slice-gate-method.md`** — this **IS** the gate. §E (the poison probe) and §D4 (boards) are
   the two hardest-earned parts.
2. **`CLAUDE.md`** — **C1** (one concern per slice: fixing an adjacent bug is a *separate* slice), **C2** (fail
   loud, no unagreed fallback), **C3** (respect the planes), U1/U2/U3, **V1** (verify against code, never a
   comment — see the note below), D1–D4.
3. **The `BASELINE.md` note named in your entry.** The evidence, the probe matrix and the reason the previous
   slice declined all live there. Do not re-derive them.

**Hard rules, each earned:**
- **QA-owned — do NOT touch:** `simulation/BASELINE.md`, `docs/*.md`, `ios-companion/*`, `tools/*`, and
  `simulation/*.json` **unless your task explicitly grants it**. Report what they need; QA writes them.
- **Never `git commit` / `add` / `stash` / `checkout --` / `checkout-index`, or offer to** (D4). To undo your own
  edit, restore from a snapshot **you** took, or `git show HEAD:path > path`. ★ Two coders have broken this; both
  recovered only because they had their own snapshot. **Snapshot before probing.**
- ★ **`rm` the native binary before every build**, and run it directly — `pio test -e native` **misreports "0 test
  cases"**, and a failed build leaves the previous binary in place. **Eight slices in this arc were bitten by a
  stale artifact.** Cross-check the event count of anything you re-run.
- **Boards: THREE envs** — `gateway`, `xiao_sx1262`, `xiao_esp32s3`. ★ The six-env escalation is **your decision
  after a compile-only `sizeof(Node)` measurement**, never a grant made in advance; **push back on any brief that
  starts at six.** ⚠ **Do not chase flash deltas** — there is a reproducible **±32 B noise floor** from
  `__DATE__`/`__TIME__` baked at `src/fw_main.cpp:420` + `src/firmware_commands.cpp:261`. **RAM is the trustworthy
  number.** ★ **Sharper instrument, found 2026-07-31: `handle_team` is ABSENT from the `gateway` ELF** (`MR_FEAT_TEAM 0`
  garbage-collects it) ⇒ for team-console work, **`gateway` ΔFlash 0 is a LINK-LEVEL inertness proof, not a noise reading.**
  Use **cold, equal-length build dirs** — a warm one produced 18628-vs-10617 and read exactly like a
  real delta.
- ★★ **`s18` keystone `1cd21235` / 271629 must NOT move.** If it does, stop and report — do not re-anchor it.
- ★★ **RE-RUN THE FOUR DETECTOR PROBES AND REPORT THE NUMBERS. Hard item — a slice that omits them is NOT gated**
  (this rule exists because a slice omitted them and QA accepted the report):

  | probe | how | expect |
  |---|---|---|
  | **P-T7** | re-add `is_team_peer(origin) &&` at the team DATA-origin learn (`node_mac_rx.cpp`) | `s38` **474 ev, 8 of 16** |
  | **P-T1** | revert the `send -t` precondition **in `Node::on_command`'s `CmdKind::send` arm — FIND IT BY CONTENT, the line number drifts** (`node.cpp` ~1309 → 1339 → **1359** as of `§o3-key-lifetime`; grep the `plane == Plane::TEAM &&` conjunct) to **`!is_team_peer(c.u.send.dst_id)`** (⚠ **the literal `dst` does NOT compile — there is no such local in that arm**; §b39 hit this) — ★ **KEEP the `plane == Plane::TEAM &&` conjunct**; the bare form gives **1587 ev / 24 FAIL**, not the expected numbers — ⚠ **NOT** `node_mac.cpp`'s ack-gate fix, which is a no-op on s35a and has cost a coder a run | `s35a` **1892 ev, 20 FAIL**, incl. `actual_reply="OK error ctr=0 depth=0"` |
  | **P-T6A** | revert T6's team arm in `stamp_origin` (`node.h`) | `s37` **851 ev, 12 of 36** |
  | **P-T6A + P-T7** | both | `s37` **917 ev, 16 of 36** |

- **Poison-probe every site you change, with a SAME-SITE control.** ★ **A 0/N result means "the corpus cannot
  reach it", NEVER "it is inert"** — prove reachability by tracing the line *immediately above* your site.
  ⚠ And for a **comparison-only** score (anything consumed relatively), a **uniform-offset** poison is an invalid
  control — it cancels. Use a **differential** one.
- ★★ **The premises in your task are HYPOTHESES.** Every brief in this arc contained at least one wrong premise;
  one contained four. **Disproving one is the most valuable thing you can return** — including "this bug is
  narrower/louder than described" and "the reference implementation I was told to copy is itself broken", both of
  which have happened. ⚠ **V1 applies to comments too: verifying that a comment exists is not verifying that it
  is true.** A drifted note cost a whole extra defective site on 2026-07-30.
- ⚠⚠ **RESTORING A PROBED FILE IS NOT ENOUGH — PROVE THE REBUILD HAPPENED. Three incidents now, and the two build
  systems need OPPOSITE fixes:** **ninja** (the sim) keys on **mtime**, so `cp -a`/`cp -p` restore a file the build then
  **skips** ⇒ **`touch` it after restoring**; **PlatformIO** keys on a **content signature**, so `touch` does NOTHING
  ⇒ **delete the `.o`.** ★ **In both cases the control is the same: the rebuilt binary's md5 must return to its clean
  value.** `§cl2b` ran a whole probe pass on contaminated binaries and caught it only because the post-restore corpus
  showed 5 phantom movers.
- ★★ **DURABLE OUTPUT GOES IN YOUR REPORT, NEVER ONLY IN A SCRATCHPAD** — a proven 33-assert scenario was **LOST** this
  way. ⚠⚠ **AND THE SESSION SCRATCHPAD IS SHARED BETWEEN CONCURRENT SESSIONS** (proven 2026-07-31: another agent's
  `before/`/`after/`/`pristine/` directories were already present, and its files appeared **mid-slice**). ⇒ **prefix EVERY
  scratchpad path with your slice tag** (`nv1-before/`, never `before/`), and **if a comparison looks impossible, suspect
  the shared directory before you suspect the tree.** Same family as the `cp -a` preserved-mtime incident.
- **Report as:** INVENTORY CONFIRMED / DESIGN / COVERAGE / GATE / DEVIATIONS / MINE-VS-THEIRS. Report failures
  with their output; if you skipped a step, say so.

### 0.1 Expected corpus outcome per entry — so a moved stream is interpretable

★ **If a scenario moves when this table says byte-identical, that is a FINDING, not a re-anchor:** it means a
scenario was relying on the broken behaviour. Attribute it and report before proceeding.

| entry | expect | why |
|---|---|---|
| **B5** | **re-anchor likely** | changes a live frame's contents |
| **B6, B7** | **byte-identical or small** | both are currently-zeroed bypasses |
| **B8** | **measurement only** — no fix expected until it is answered |
| **B9** | ★ **value-only re-anchor of EVERY team scenario** | `slot` is in the stream |
| **B10** | **re-anchor of `s37`** | removing the dead command is a stream edit |
| **B11–B15** | **byte-identical** | telemetry/comments/`src/`-only |
| **B16** | ⚠ **NOT `s27` only — 12 scenario JSONs use `send_layer`** | QA-grepped 2026-07-31: s09 ×2, s10, s15 ×2, s16, s17, s27, s31, s32, s33, s37. The old row said “it is the sole user” and was wrong; probe E moved **11** of them |
| **D1** | ★ **inert on 34/36 — but it DISARMS `s35a`/`s38`.** Read the entry before starting |

---

## Original closed index — 2026-07-31 pass

These entries were already closed when this register was established. Later closures remain in the detailed records
and are checked in the current-status section above. Each row names the original evidence tag in
`simulation/BASELINE.md`.

| # | the defect | the fix | evidence |
|---|---|---|---|
| ~~**B0**~~ | ★★ a plaintext DM aired the node's COORDINATES IN THE CLEAR (`loc_in_dm` had no crypt check) | location became a per-send `-l` that REFUSES unless sealed; `cfg set loc_dm` removed across 12 surfaces; `kVersion` 22→23 | `§loc-per-send` |
| ~~**B1**~~ | `team 88A672BA` (hex without `0x`) silently joined team **88** | the whole token must parse | `§team-target-whole` |
| ~~**B2**~~ | a team-scoped H answer wrote the **static** `_id_bind` (I2 breach) | do not bind at all on the team plane — s34 `no_route` 8 → 0 | `§id-bind-plane` |
| ~~**B3**~~ | `reqpubkey`'s plane diverged sim-vs-metal (sim left it AUTO) | the sim mirrors the console; s22 gained `-t` | `§sim-plane-parity B3` |
| ~~**B4**~~ | `schedule_sync_response` read the **static** route count on both planes | the plane is passed by the caller — and there were **three** defective readers, not two | `§sync-response-plane` |
| ~~**B17**~~ | an out-of-range `team <id>` joined garbage `0xFFFFFFFF` on the 32-bit boards | `errno == ERANGE` **and** a width guard — one arm per ABI | `§team-target-range` |
| ~~**B22**~~ | a plain `send_channel` SUCCEEDED in the sim and was REFUSED on metal | metal is the reference; the sim's `team_member` heuristic is gone and 10 scenario posts gained `-t` | `§b22` |
| ~~**B26**~~ | the NV backend duplicated its blob validation (16 size checks, 30 definitions) | factored **above** the `#if` so it is natively testable — and it is AB1's forcing function | `§nv1` |
| ~~**B27**~~ | `cfg set team_id` had **none** of the three guards `team <id>` carries | the key is REMOVED; the read surfaces (incl. TLV `0x12`) all stay | `§team-id-cfg-removal` |
| ~~**B28**~~ | `team_id != 0` did not imply `is_mobile` | enforced at **three** points + O2/R4 refusals; the invariant already existed as a build `#error` | `§role-model` |
| ~~**B29**~~ | ★★★ `key_hash_for_id` **never returned on a miss** — an infinite loop, and UB | one line: `uint16_t i < _id_bind_n`, which also stopped it returning an EVICTED binding | `§idbind-loop` |
| ~~**B32 + B33**~~ | the console discarded every `CmdCode`, and `hashof`'s advice was circular | the reply names its reason; the advice names remedies that work — and the fix was flash-NEGATIVE | `§err-reason` |
| ~~**B41**~~ | the sim's push bridge had no `channel_sent` arm, so `relayed` never reached a stream | one arm — and it CONFIRMED B38: team plane **0 `true` / 9 `false`** | `§b41` |

---

## Detailed records — open, parked, and subsequently closed

Full detail remains because these records carry implementation traps, measurements and rejected alternatives. Some
entries were fixed after being written; their original work order remains underneath the current status checklist so
references and evidence are not lost.

### B5 — `channel_pull` carries no `team_id`, and airs `src = _node_id`
⇒ it **cannot** receive the mixed-leaf exemption that `team_sync` got (there is nothing to scope on), so a
cross-nibble teammate never answers a channel repair; and it leaks the static id where the team plane expects a
team id. Note: `T4`.

### B6 — team `budget_penalty_q4` is a zeroed bypass with no team mirror
`node_routing.cpp:159` reads `_neighbor_budget_tier`, an R4.2 `node_id`-keyed map that has no team twin, so the
team plane silently skips anti-spam-tier scoring. Named in-source so the asymmetry is visible. Note: `T5`.

### B7 — team **slow-reprobe** does not exist
`node_cascade.cpp:172` reads `_link_bidi[from_next]`; static-only *by construction* (the `pt.plane == TEAM` branch
returns above). A team version needs `_link_reprobe_last_ms`, **another 2048 B array** — hence deferred, not
forgotten. Note: `T5`.

### B8 — ⚠ **UNMEASURED:** does a relay forward a frame whose `src` is 0?
The one path a **not-yet-DAD'd** member could take. T7's harness control was **vacuous**, so the question is open
rather than answered. If it *does* forward, the T6/T7 coupling becomes live rather than counterfactual. Notes:
`T7`, `T8`.

---

### B9 — `rt_update.slot` is **wrong on the team DV path**
`node_beacon.cpp:876/879` label a beacon-DV merge `"primary"`/`"alt"` **regardless of which table was merged**.
Measured **~120 mislabelled vs 9 correct** corpus-wide. **Sim-only telemetry, so not a firmware defect** — but it
invalidates any analysis keyed on `slot`, and it has already cost **s37 and s38** an explicit in-file workaround.
⚠ **Not free:** `slot` is in the stream ⇒ fixing it is a **value-only re-anchor of every team scenario.**

### B10 — `s37`'s `routes` command is dead
The sim has **no `routes` verb** — it replies `ERROR: unparsed command`, and the `_desc` claim that asserts 16/17
read its output **was never true**. Left in place because removing it is a stream edit ⇒ a re-anchor for no gain.
Note: `T8`.

### B11 — `frame_trace.h`'s type/opcode switches are incomplete
The DATA-type switch (`:76`) names only **1..5**, so 6..19 print as bare numbers; the Q switch omits opcode 2
(`CONFIG_PULL`). ★ **`-Wswitch` cannot help** — both switch a raw `uint8_t`, not the enum. Fixing one gap at a
time was correctly refused as a drive-by; fix the class or leave it.

### B12 — a **three-way** duplicate of the seal-or-refuse logic
`want_crypt` + `build_sealed_relay_body` + the outcome→reason mapping now appear at `node_hashlocate.cpp:1075`,
`node.cpp:1408` and `node_mac.cpp:462`. ⚠ **Read `§deleg-ack-xl`'s design note before deduping**: collapsing the
pair was *rejected* there because the duplication **is** the local asymmetry detector — the very thing whose
absence hid a silent-drop bug. Dedup carefully or not at all.

### B13 — `liveness_penalty_q4`'s inline scan is duplicated by `team_liveness_find`
Exactly one scan to fold. Marked ✖ MISSING/C1 in-source. Note: `T5`.

### B14 — `node.h:1016` comment drift
Claims `sort_candidates` threads "wire-only degraded". **It never touches `degraded`.** Pre-existing, outside any
recent hunk. Note: `T5`.

### B15 — `enc_cfg`'s binary TLV lacks `team_ch_key`
Trivially additive on the `TAG_CFG_TEAM_HOP_CAP = 0x1C` precedent, but it is the **remote-admin** path, which is
mid-redesign — hence not taken. Note: `T-K1b`.

### B16 — `send_layer`'s sim-vs-metal **grammar** still diverges on four axes
Crypt capability is now aligned, but: argument **order** (sim `<layer> <hash> <text>` vs console
`<0xhash> <l1,l2,…> "<text>"`), **radix** (sim bare decimal vs console demands `0x`), **path arity** (sim single
layer vs console comma-list), **quoting** (sim unquoted vs console mandatory). Sim also lacks `-K`/`-t`. **Only
`s27` uses it — 6 lines, all currently correct.** Note: `§xl-crypt`.

---

### B18 — the **read-side** twin of B2: a relay answers a team H from its static `_id_bind` · NEW 2026-07-31
Fixing B2 removed the only corpus-reachable *use* of the read side, which is how it surfaced: a **relay** was
answering a repeat team-scoped H out of its **static** `_id_bind` instead of forwarding. Delivery is preserved
(the owner answers, ~1.5–2 s later — that is the s24/s25/s26 event delta), so this is **correctness, not loss**.
Marked ✖ MISSING at `handle_h`. ⇒ **belongs to D2, the read-path plane audit.** Note: `REG-B1/B2`.

### B19 — `deleg_ack_put` is inlined at **8 sites**, costing ≈4 KB · ★ **FOLD INTO B12, do not take alone**
The function is **584 B** compiled and has **8 call sites** (7 in `node_hashlocate.cpp`, 1 in `node_mac_rx.cpp`), with
no LTO ⇒ ≈**4.7 KB** of duplicated code where one copy + 8 call sequences would be ≈0.7 KB. **Recoverable ≈ 4 KB**
(not the 1.8 KB I first quoted — that was only the 3 sites `§deleg-ack-xl` *added*; `noinline` also de-duplicates the
5 pre-existing copies).
★★ **Why the inlining buys nothing here: the cost centre is `_hal.now()`, a VIRTUAL call on `IHal` that inlining
cannot optimise through.** Every copy still makes the indirect call, so 584 B buys the removal of one `bl` and a few
register moves — on a **cold** path (a delegated re-origination; 1–4 hits per scenario). `kDelegAckCap = 8`, so GCC
is unrolling an 8-iteration scan at each site.
⚠ **Flash is NOT the argument** — headroom is 54.8% / 59.9% / 35.6% used, so ≈4 KB is under 1%. The argument is a
large cold function duplicated eight times for **zero** speed gain.
★★ **DO NOT TAKE THIS ALONE.** `noinline` re-codegens **all eight** sites, which destroys the precise attribution
`§deleg-ack-xl` relied on (*"exactly 2 of 283 objects changed"* proved inertness on `gateway`). A one-token change
whose verification work dwarfs it is the wrong slice shape. ⇒ **Fold into B12** — the three-way seal dedup at
`node_hashlocate.cpp:1075` / `node.cpp:1408` / `node_mac.cpp:462`, which is **already a refactor of that file**,
already churning those objects, and already owes a flash investigation. **NOT B18** (a fix — C1 forbids folding a
refactor in). **Owner agreed 2026-07-31.**
⚠ Two caveats for whoever takes it: **measure on the BOARD build** (`MR_EMIT` is device-stripped, so 584 B is the
board figure and native would mislead); and the result is valid **only for the build configuration measured** — LTO
is off today (`platformio.ini` has no `-flto`) and GCC does honour `noinline` under it, but that is the same trap as
the `__DATE__` flash noise.

### B20 — a CRYPTED DM in a 2-byte band fails with **NO `send_failed` AT ALL** · NEW 2026-07-31
`max_payload_bytes_hard_cap` subtracts `data_inner_overhead = 6` (a **4-byte** MAC), but a **CRYPTED** frame’s trailer is **8**
⇒ the cap is **2 B too generous for a sealed DM**. For `body_len` **215–216** (209–210 with `-l`) `e2e_seal_inner` succeeds
(inner ≤ 241), then `pack_data` refuses at **TX time** and **nothing is pushed to the app** — the send simply vanishes.
Found by the `-l` fit sweep, **not fixed** (C1); marked at the site. ⚠ **Fix the CAP, not the gate** — re-deriving the sealed
bound at a call site would fork a second copy of the seal’s size arithmetic (U1). Note: `LOC-PER-SEND`.

### B21 — an oversize DM `≥ 237 B` emits `e2e_no_pubkey` with **no `send_failed`** · NEW 2026-07-31
At `body_len ≥ 237` the DST_HASH fit-check drops the flag, so the `!(item.flags & DATA_FLAG_DST_HASH)` branch reports
`e2e_no_pubkey` — **a misleading reason** (the key is fine; the body is too big) — and returns **without**
`push_send_failed`, so the app is told nothing. Same sweep, same slice, deliberately untouched. Note: `LOC-PER-SEND`.

### B23 — the `resolve` verb's surface: **`Plane::AUTO` IS reachable on metal**, and `u.resolve.hard` is dead · NEW 2026-07-31
★★ **Two defects at one site, and the first one falsifies a claim this project has repeated:** the console verb
`resolve <0xhash> [hard]` (`console_parse.cpp:149-160`) assigns **no plane**, and `request_resolve`
(`node_hashlocate.cpp:1561`) calls `emit_hash_query(key_hash32, hard)` — **2 args** — so `node.h:809`'s default
`Plane plane = Plane::AUTO` applies. It is dispatched on hardware (`fw_main.cpp:472` USB, `:799` BLE). ⇒ *"AUTO is
simulator-only"* is **FALSE**; the defensible claim is **"AUTO is never carried in a `Command` plane field."**
BASELINE lines ~299/~320 are corrected. Three more AUTO-default `emit_hash_query` sites are metal-live:
`node_join.cpp:468` (DAD discriminator), `node_mac_rx.cpp:1361` (RX re-flood), `node.cpp:1495` (`send_layer` park arm —
console-unreachable, **unverified**). **(b)** `u.resolve.hard` is **dead** on the `reqpubkey` path — `node.cpp:1382`
hard-codes `/*hard=*/true`; only `CmdKind::resolve` reads it. Proven by a probe that returned **0/36** and was correctly
reported as *a field that lies*, not a weak probe. ⚠ **Decide whether AUTO-on-`resolve` is intended** before "fixing"
either half. Note: `SIM-PLANE-PARITY B3`.

### B24 — `send_req_sync_q`'s `q_tx{rt_total}` is now **inconsistent with** the plane-aware responder · NEW 2026-07-31
`node_query.cpp:106` reports the **static** count on both planes **deliberately** (documented at `:103-105`, to avoid
rewriting every static `q_tx` line). After B4 the **responder** side names its plane while the **requester** side does
not — the two halves of one exchange now disagree. ★ **The deferral itself still holds:** the route-rich skip at `:89`
was **V1-verified unreachable on the team plane** (its one team caller `node_mac.cpp:993` always passes `force=true`,
and `:75` forbids a mobile originating a static pull). ⇒ **telemetry-only, and it will re-anchor every scenario carrying
a `q_tx`** — which is why it is deferred, not forgotten. Note: `§sync-response-plane`.

### B25 — ⚠ **UNMEASURED:** does a team-adopted member answer a **STATIC** `req_sync` with a **TEAM-plane** beacon? · NEW 2026-07-31
★★ **Mechanism QA-verified in source:** `emit_beacon`'s plane self-selection is
`const bool team_active = _cfg.is_mobile && _cfg.team_id != 0 && team_local_id() != 0;` (`node_beacon.cpp:410`), and
`team_emit = team_active` (`:431`) picks `src_rt = _rt_team` and `src = team_local_id()`. **That is a property of the
NODE, not of the pull being answered** — so a team-adopted member replying to a **static** `req_sync` would air a
**team-plane** table to a **static** requester, which installs it in `_rt`. ⇒ **a candidate I2 breach** (team ids in a
static table), the class B2/B18/s38-assert-12 exist for.
★ **Why it is UNMEASURED and not simply open:** B4 proved **12 static-plane sync responses in 5 scenarios come from nodes
holding team routes**, so the *responder* side is live — but `sync_response_*` emits carry **no plane**, so the stream
cannot say whether those repliers were `team_active`. **The measurement: a temporary plane discriminator on the beacon
emit** (the method B4 used), then check whether any static requester installs the replied ids. **Do not fix before
measuring** — if it is unreachable, a guard here is decoration. Note: `§sync-response-plane`.

### B30 — `team_id_of_key` silently first-matches an ALIASED hash · FIXED 2026-08-02
The old `team_id_of_key` returned the **first** id whose team-key row carried the hash. ★ **`_team_keys` genuinely
can alias:** `team_key_set` upserts **by id only** and never dedups by hash, so a teammate that re-runs team-DAD leaves
its old `(id, hash)` row live for the full 48 h TTL. ⇒ on the **live plaintext send-by-hash path** the node may pick the
**stale** id — exactly the silent-pick the address-book spec §2.1 forbids for the view.
★ **CLOSED 2026-08-02 by `§B30 send`.** The AB3 resolver is now the one shared reverse policy: it accepts a confidence
floor, filters out below-floor rows, then picks max `last_seen_ms` among the qualifying aliases. Address-book callers
pass `claimed`; the live send reader retains its default `authoritative` floor. Thus a fresher claim remains visible
and labelled in the view but cannot shadow an older authoritative send target.
★ **Bench state pinned verbatim:** authoritative aliases `245 → 0x7B18ADA2` then `86 → 0x7B18ADA2`; a sealed type-19
TEAM grant must queue with `dst=86`, never stale `245`. The inverse-hash and command-path tests both assert it.
**Poison control:** inverting the freshness comparison fails three cases, including `§B30 send` at `dst == 86`.
★★ **QA REVIEW 2026-08-02 — GO, with one in-source claim CORRECTED and one standing cost now on the record.**
① `node_hashlocate.cpp` (the `on_hash_bind_snoop` header) asserted *"a repeat `send -t 0x<hash>` to an unheard
teammate now resolves from cache instead of re-flooding the locate."* **False, and it cannot ever be true as built:**
the send reader is at the default `authoritative` floor, both ingest sites write `claimed` **unconditionally**, so the
lookup misses, falls to the `§F-TR-1` flood, and the answer lands `claimed` again — **no convergence.** Corrected in
place with the five-step chain. ⇒ ★ **the team ingest buys the VIEW (`hashof`/`peers all` can name and label an unheard
teammate), NOT the send.** ② ⚠ **The accepted cost: a repeat `send -t 0x<hash>` to a claim-only teammate re-floods
every time** (rate-limited only by `hash_query_seen_ttl_ms`). **DO NOT lower the floor to reclaim that airtime — it
reverses spec §3-D7** (a false claimed binding does not fail closed; L2c *forwards* the DM to the owner of the false
hash). The convergent cure is a first-hand beacon or a QR. ③ A `⚠` now sits on `team_id_of_key_freshest`'s declaration:
its default is the permissive `claimed` while the `team_id_of_key` wrapper's is the strict `authoritative`, so a future
**send**-path caller reaching for the raw resolver would silently route on a claim. All callers correct today.
④ **Gate coverage was 2 envs, not 3** (`xiao_esp32s3` + `gateway`); QA built the missing `xiao_sx1262` — **SUCCESS, RAM
70.9 % / 167044 B = S3's 166980 + S4b's +64 exactly, so B30 itself is +0 RAM.** Native **1149/72681/0** and s18
`1cd21235`/271629 with 0 assertion failures both QA-reproduced.
**Gate:** native **1149/72681/0**; target `xiao_esp32s3` and feature-off `gateway` link; **36/36** streams are byte-identical to
clean HEAD with zero assertion failures, and s18 remains `1cd21235` / 271629.
Note: `§ab3`, `§B30 send`.

★★ **2026-07-31: TIER 1 IS NOW EMPTY — B0, the last live leak, is CLOSED.** Both Tier-1 bugs found *inside* this arc were fixed: the cross-layer cleartext downgrade
(`§xl-crypt`, `65833f2`) and the silently-dropped delegated sealed DM (`§deleg-ack-xl`, `442809b`).

### B31 — `key_hash_for_id` is neither **authoritative**- nor **TTL**-gated · NEW 2026-07-31
After `§idbind-loop` it shares its loop idiom with `key_hash_of_id`, but **not that sibling's gating** — so it can answer
from a `claimed` (unvouched) or TTL-lapsed `_id_bind` row. ★ **Residual is narrow and that is why it was scoped out:** the
hash it returns only feeds `peer_key_find`, which **ages independently**, and `id_bind_set` maintains the id↔hash
bijection — so a stale answer degrades to a failed lookup, not to a wrong peer. Recorded **in-source at `node.h`** as a
deliberate divergence rather than left silent. ⚠ **Decide the intent before "fixing":** if `rcmd` should refuse an
unvouched target, that is a **policy** change on the remote-admin path (mid-redesign — see B15). Note: `§idbind-loop`.

### B34 — ★★★ the SIMULATOR drops every refusal reason, **7 times over** ⇒ fail-loud refusals are corpus-untestable BY CONSTRUCTION · NEW 2026-07-31
`orchestrator/runtime/NodeRuntimeWrapper.cpp` lines **656, 818, 843, 872, 901, 941, 964** each carry
`(r.code == CmdCode::queued) ? "queued" : "error"`. ⇒ **no scenario can assert WHICH refusal happened.**
★★ **This is the structural explanation for a pattern that has cost this arc real coverage:** every fail-loud refusal
added since 2026-07-29 — `unsealable`, `no_location`, `role_refused`, `err_no_binding`, `unknown_key`, `too_long` — is
**untestable in the corpus by construction, not merely unexercised.** Slice after slice reported *"native or the bench
only"*; **this is why.**
⚠⚠ **Fixing it moves a DETECTOR PROBE: `OK error ctr=0 depth=0` is P-T1's own expected signature** (register §0), so the
fix re-anchors scenarios **and** re-baselines a documented probe expectation ⇒ **it must be its own slice, and §0's P-T1
row must be updated in the same commit.** ★ **Payoff: it would make the whole `err_*` family assertable in scenarios** —
the single biggest coverage gain available to this corpus. Note: `§err-reason`.

### B35 — `ingest_channel_m`'s self-skip is **PLANE-BLIND** ⇒ a teammate's posts can be SILENTLY SWALLOWED · NEW 2026-08-01
`ingest_channel_m:252` skips on `origin != _node_id` — comparing a **TEAM-plane origin** against the **STATIC node id**.
On a **registered (dual) member** those are different id spaces, so a teammate whose `team_local_id` numerically equals
our static `node_id` has its channel posts **silently dropped: no inbox row, no push, and (since `§cl2b`) no retained
position — while the flood still relays them.** §18 numeric-collision class; **predates CL2a**, found by `§cl2b`, not
fixed (C1). ⚠ **Silent** is the severity: the sender sees a normal post, the receiver sees nothing, and no telemetry
names it. Note: `§cl2b`.

### B36 — a located DM's position reaches **no app surface** — `send -l` is only visible via the address book · NEW 2026-08-01
`Push::has_location/lat_e7/lon_e7` are set at `node_mac_rx.cpp:1196` and **consumed by nothing**: `write_push`'s
`msg_recv` arm emits no coordinates, the console renderer prints none, `record_dm` has no location field. **QA-verified:**
the only `has_location` consumer in `console_json.cpp` is **`write_peer_row`** — AB4's peers row, a different struct.
⇒ ★ **CL3 shipped `send -l` and its position becomes visible ONLY through the address book (`§ab4`).** `§cl2b` mirrored
that deliberately rather than forking a richer channel surface, so **the per-message JSON carries no coordinates on
EITHER plane** — consistent, but probably not what an app author expects. ⚠ **Fixing it is a contract addition on BOTH
planes and its own slice** — decide whether a position belongs on the message or only on the contact. Note: `§cl2b`.

### B37 — ★★ a **symbolic-only** assertion on a WIRE CONSTANT is not coverage · CLASS, found 2026-08-01
`§cl2c`'s poison P1 renumbered `channel_inner_flag_source` **0x04 → 0x08** and **the entire 1091-case suite stayed
green** — every assertion named the flag **symbolically**, so the KAT was **self-referential about the one thing two
independently-built nodes must agree on.** A node built before the renumber and one built after would have failed to
interoperate **with all tests passing on both**. Fixed for the channel inner (numeric pins on every flag, both widths,
and a literal `inner[0]` in the KAT); ⚠ **CL2b's values were equally unpinned.**
⚠⚠ **QA DOWNGRADED THIS 2026-08-01, ON THE OWNER'S CHALLENGE — the original framing OVERSOLD it.** I wrote that
*"two independently-built nodes would fail to interoperate with all tests passing."* **That scenario is nearly
unreachable here:** it needs two nodes on **different builds**, and this project is **undeployed, reflashed all-at-once,
and wire changes are FREE by M3** — so a wire constant changing is **expected**, not a hazard.
★★ **What IS true is different, and more useful: NO TEST IN THIS PROJECT CAN DETECT A WIRE-FORMAT CHANGE AT ALL.**
**One `lus` executable drives both ends of every simulated link** and the native suite compiles one copy of the
constants, so a renumber moves both sides identically. ⇒ **every "byte-identical" result this file relies on is
byte-identical AGAINST ITSELF: the corpus validates BEHAVIOUR, never FORMAT.** Our only real format checks are the
**KATs** — and `§cl2a`'s is the pattern to copy, because it recomputes key/nonce/AAD **from the spec wording** and opens
with raw `crypto_aead_unlock` instead of calling our own sealer.
★ **What a numeric pin actually buys: not bug prevention, a DELIBERATENESS TRIPWIRE** — it turns "someone silently
changed a wire number" into "someone had to edit a test that says `0x04`". Worth most **at the moment a constant is
CREATED**, because retrofitting one later means first establishing what the number *should* be, and by then the only
record is the code that may have drifted.
⇒ ★ **PRIORITY: NOT NOW. Pin on creation; do not sweep.** A sweep would touch ~161 constants for low yield while the
reflash-all assumption holds. **Two triggers make it real: the first time two boards run DIFFERENT builds, and
DEPLOYMENT** — at which point M3 also stops being true and this entry should be re-read together with it.
*(the original, overstated framing, kept as the record:)* ★★ **THE CLASS, and it is worth a sweep of its own:** `CHECK(x == kFlag)` proves the code is **self-consistent**; only
`CHECK(kFlag == 0x04)` proves it matches **the other end of the link.** ⇒ **audit every wire constant** — DATA flags,
`q_opcode`, frame types, `PushKind`/`SendFailReason` numeric values, the cfg TLV tags — **for a numeric pin**, and add
one where it is missing. A renumber is otherwise invisible to this project's entire gate. Note: `§cl2c`.

#### Related ruling — `relayed` stays as-is on a one-hop team
**Owner:** *keep `relayed`, accept `NOT HEARD` on 1-hop teams.* ⇒ **ACCEPTED BEHAVIOUR, not a defect.**
On a fully-1-hop team (3–10 co-located members — the actual hiking case) **nobody re-broadcasts**: the frugal
`{self + hops-1}` seed marks every neighbour covered, so `flood_forward_decision` goes silent and **there is nothing to
overhear.** Measured after `§b38-b40`: s22 **0 `true` / 1 `false`**, s29 **0 / 2**, with **every teammate having received
the post.** ⇒ `relayed=false` is **TRUTHFUL** — we observed no relay — and the honest alternative was rejected as
costing new airtime for a display nicety.
⚠⚠ **CONSEQUENCES, stated so they are not re-litigated:** (1) the **OLED emergency will spend its full 3-attempt budget
and display `NOT HEARD` on a 1-hop team, at 100 % delivery** — expected; (2) the shipped app's stop-and-back-off on
`relayed:false` fires there too, so **back-off must never be presented to the user as delivery failure**; (3) ★ **`relayed`
is NOT a delivery signal on either plane** — a real one needs a per-member ack, which the owner **declined**. ⇒ **anyone
proposing to "make `relayed` true when delivery succeeded" is reversing this ruling.** Evidence: `§b38-b40`.

### B38 — ★★★ a TEAM channel post could never report `relayed=true` · FIXED 2026-08-01
`channel_reoffer_confirm` (`node_channel.cpp:~1155`) returns **before** `emit_channel_sent(true, …)` when `rp.team`:
```
if (rp.team) return;                                   // keep re-offering for far members
emit_channel_sent(true, static_cast<uint16_t>(id & 0xff)); …   // <- unreachable on the team plane
```
The team carve-out is **correct in intent** (one near relay ≠ full coverage of a multi-hop chain, the s28 class) but it
also **discards the observation**. Retry exhaustion then emits `emit_channel_sent(false, …)` at `:~1131`. ⇒ **a team post
that WAS relayed by every teammate still ends `relayed=false`.** The only truthful outcome the channel plane can produce
is unreachable on the plane teams actually use, for **every** consumer — companion app included, not just the OLED UI.
★★★★ **SEVERITY RAISED 2026-08-01 by `§b41`: this is LIVE IN THE SHIPPED APP, not only the future OLED.**
`INBOX_SYNC_CONTRACT.md:502` — *"the app treats `channel_sent{relayed:false}` as stop-and-back-off (don't keep firing)"*
⇒ **the companion already backs off from EVERY SUCCESSFUL TEAM CHANNEL POST.** Measured: **0 `true` / 9 `false`** on the
team plane corpus-wide, with **every** post actually received by every teammate.
★ **Why it is severity-3:** the OLED emergency (spec `2026-07-31-onboard-oled-ui-design.md` §4) retries on
`relayed=false`, so a distress call would **always transmit its full 3-attempt budget and always display `NOT HEARD`,
even when the whole team received it.** A safety feature reporting failure on success.
★★★ **OWNER RULING 2026-08-01 — `relayed` ON A TEAM POST MEANS "FIRST RELAY ONLY". We cannot guarantee a full flood,
and the field must not be read as coverage.** ⇒ the fix emits the *observation* (**at least one relay was heard**), not
a completion claim. ⚠ **NAME IT IN THE CONTRACT AND IN-SOURCE:** the boolean already means "the flood completed" on the
NON-team plane, so after this fix **one field carries two meanings depending on the plane** — leave that unstated and
the false negative simply becomes a false positive on the same safety feature.
⚠⚠ **CONSEQUENCE THE OLED SPEC MUST RULE ON (QA flag, not a blocker):** today the emergency retries its **full**
3-attempt budget because `relayed` is always false. After the fix it will **stop at the first relay confirm**. For a
distress call, "one teammate heard me" may or may not be a reason to stop transmitting — **that is an OLED-spec
decision, not this slice's.**
★ **QA-VERIFIED IN SOURCE 2026-08-01:** `:1157`'s `if (rp.team) return;` sits **immediately before** `:1158`'s
`emit_channel_sent(true, …)`, and `:1131` is the only other emit (`false`) ⇒ **the `true` branch is structurally
unreachable on the team plane.** The entry is exactly right.
★★ **AND QA MEASURED WHAT IT COULD: the path is WELL EXERCISED but the OUTCOME IS INVISIBLE.** `channel_sent` fires as a
**Push** (not an `MR_EMIT`) **93 times across 9 scenarios** — s28 ×7, s22, s29, s34 among them — but the sim renders
`{"kind":"channel_sent","ctr":…,"dst":…}` **with no `relayed`**. ⇒ **B38 is corpus-blind, and B41 is the reason.**
**Fix shape (with B40):** remember the observation instead of discarding it — extend `ChannelReofferPending` with
`relay_seen`, keep the retries running, and emit the remembered truthful result once (either immediately on first
confirm, or at exhaustion). **Never emit a contradictory `relayed=false` for a post already confirmed relayed.**
⚠ `ChannelReofferPending` is 12 B (`node.h:~1238`); B40 adds a `uint16_t` and this adds a flag — a field **reorder**
should absorb both without growing `Node`, but that must be **pinned by `static_assert`, never assumed** (D2).
⚠ **Emitted-value change ⇒ expect a re-anchor** on any scenario with channel re-offers; give it its own slice (C4).
Found by the OLED-UI second review, `docs/archive/2026-08-01-onboard-oled-ui-second-review.md` §1. **UNMEASURED on
metal** — found in-source; the sim corpus should show it as a `channel_sent{relayed:false}` on a delivered team post.

### ~~B39~~ ✅ **CLOSED 2026-08-01** (`§b39-ctr0`) — the interim landed, and **the entry's premise was too strong**
★ Comments at **four** sites + a test pinning `next_ctr`'s no-zero invariant. Native 1093/71851 → **1094/71859** (+8 =
exactly the new test); corpus **36/36 `cmp`-identical** with a **bit-identical `lus`**; boards **ΔRAM 0 AND ΔFlash 0
exactly** — not even the ±32 B noise.
★★★ **THERE ARE THREE PRODUCERS OF `ctr == 0`, AND THE THIRD IS A SUCCESS.** `node.cpp:1565-1573`: on a **registered
mobile**, a plain/`-g` GLOBAL post takes `do_send_channel_delegated`, which returns **`true` after a real MOBILE_SEND DM
flew** — but the **home** mints the channel ctr, so `ctr` stays 0. ⇒ **a SUCCESSFUL delegated global post already answers
`> queued ctr=0` on metal**, indistinguishable from blocked or seal-failed.
⇒ ★★ **this entry's "`ctr == 0` IS the sentinel [for not sent]" is TOO STRONG. The sound reading is "this node minted no
channel ctr."** `ctr != 0` ⇒ originated locally with that handle; `ctr == 0` ⇒ **no local handle exists, and whether
anything flew is not answerable synchronously.**
⚠⚠ **THE REAL FIX MUST ACCOUNT FOR IT: a discriminated result that only splits accepted / blocked / refused would
classify producer (3) — a success — as a FAILURE.** A local handle for a remote mint is the missing piece.
ⓘ Also verified: `-t -g` reports the **TEAM** copy only (`gctr` discarded), and `queued, 0` is routine elsewhere
(`join`/`resolve`/`reqpubkey`/`peername` mint nothing; the hash-addressed `send` arm's 0 can mean **parked behind an H
resolve**, i.e. sent *later*). Note: `§b39-ctr0`. *(original entry below)*

#### B39 original finding — `CmdCode::queued` with `ctr == 0` was ambiguous
Two `do_send_channel` paths return `0` and say so in their own comments — the pre-TX gate (`node_channel.cpp:~645`,
`// not sent (no ctr minted)`) and a seal failure (`:~734`, `// NOT sent (the caller's queued becomes ctr=0)`) — and
`Node::on_command` wraps that zero unchanged: `return CmdResult{ CmdCode::queued, ctr, … }` (`node.cpp:~1578`).
⇒ **the synchronous result cannot distinguish accepted from blocked from failed-before-enqueue.** `next_ctr` never
returns 0 (`node_mac.cpp:20-24`, wraps 65535→1), so `ctr == 0` IS the sentinel — but it is undocumented at the seam and
every current caller ignores it. A caller that counts `queued` as "on the air" (any retry/attempt budget) miscounts a
**blocked** send as a transmission, and a **seal failure** leaves it waiting for an outcome push that names a different
ctr. ⚠ `CmdCode` alone also cannot separate `unsealable` from `no_location` — both surface as `err_unsupported`; the
actionable distinction exists only in `SendFailReason`, which the synchronous path does not carry.
**Fix shape:** return a discriminated result from the channel-origination path — **accepted** (non-zero ctr) ·
**blocked** (`reason` + `next_ms`) · **refused** (`SendFailReason`) — and adapt console formatting around it.
★ **QA-VERIFIED:** `next_ctr` is `c = (c >= 65535) ? 1 : c + 1` — **never 0**, so the sentinel is real.
ⓘ **NARROWER THAN WHEN WRITTEN:** `§err-reason` (B32) has since made a *refusal* name itself (`> err_no_binding …`), so
the residual gap is precisely the **`queued` + `ctr == 0`** case — *"command accepted, nothing minted"*. **The interim is
the right call; the discriminated result is a design change that can wait.**
**Minimum interim:** document `ctr == 0` as "not sent" at the `on_command` seam so callers stop treating `queued` as
proof of transmission. Found by the OLED-UI second review, §2. **UNMEASURED** — found in-source.

### B40 — ★★ `channel_sent.ctr` carried only the low 8 bits of a 16-bit counter · FIXED 2026-08-01
`do_send_channel` mints and returns the **full 16-bit** `next_ctr` (`node_channel.cpp:~647`), but the message id keeps
`c & 0xff`, and **both** `channel_sent` emit sites reconstruct from it: `emit_channel_sent(…, static_cast<uint16_t>(id
& 0xff))` (`:~1131`, `:~1158`). `Push::ctr` is already `uint16_t` (`command.h:~224`), so the width is available and
unused. ⇒ **an origination handle of 256 is answered by a push ctr of 0 and never matches again**; low-byte comparison
"works" only by colliding every 256 posts, which is precisely no correlation at all. Any consumer correlating its own
channel post to its outcome — the OLED UI's send tracker, and any future app-side equivalent — is affected.
★ **QA-VERIFIED, with one caveat to state in the fix:** `Push::ctr` is `uint16_t` and both emits mask `id & 0xff`, as
described. ⚠ **But `item.ctr` is ALSO masked** (`:1002`, `:1491`) — and *that* one is **by design**: the channel
msg-id's low byte **is** the ctr on the wire. ⇒ the fix is right and needs no wire change, but **the 16-bit ctr it emits
is a LOCAL correlation handle only — no peer can echo more than 8 bits.** Say so, or someone will later try to match it
against a received id.
**Fix shape:** store the full originating ctr in `ChannelReofferPending` and emit **that**. **No push-schema or wire
change** (the field is already 16-bit). Naturally one slice with **B38** — same struct, same emit sites.
**Coverage owed:** ctr 255 · 256 · 257 · 65535→1, with a low-byte-colliding unrelated outcome interleaved.
Found by the OLED-UI second review, §3. **UNMEASURED** — found in-source.

---

> ★★ **B42–B46 ARE ONE FAMILY: id → hash resolution.** The design + slicing lives in
> `docs/superpowers/specs/2026-08-01-id-to-hash-resolution-design.md` (**v2**, revised against
> `docs/archive/2026-08-01-id-to-hash-resolution-design-review.md`).
> ★ **2026-08-01: B42 / B44 / B45 / B46 / B47 are FIXED (slices S1 / S2 / S2b + the S1b/S2b-fix round), green and
> UNCOMMITTED** — see each entry's CLOSED line and `simulation/BASELINE.md`'s three `§id-hash` notes for the gate
> numbers and poison matrices.
> ⚠⚠ **B46 WAS CLOSED PREMATURELY ONCE (2026-08-01) AND RE-OPENED THE SAME DAY** by the independent implementation
> assessment (`docs/2026-08-01-id-to-hash-resolution-implementation-assessment.md`, finding P1a): the first fix guarded
> the matching row but left `id_bind_evict_other_hash_holders` un-gated, so the identical demotion still walked in
> through the REVERSE uniqueness rule. **The coder's own poison matrix could not see it — and the re-probe proved why
> in numbers: even with the corpus's relayed learns FORCED to `claimed`, the rehome case fires 0 times in 304 885
> `id_bind_set` calls.** ⇒ ★ **a "guard the write" fix must be checked against every OTHER path that mutates the same
> invariant**, and byte-identity on a corpus that cannot produce the precondition is not evidence either way.
> ★ **2026-08-02: B43's WIRE HALF IS BUILT (`§id-hash S4a`), and B53 closed with it.** `H_FLAG_BY_ID` is on the wire,
> both planes ingest a `claimed` binding, and `peer_book_by_id` reads at the `claimed` floor so the tier is visible and
> LABELLED. **B54 stays open by decision, not omission** (see its entry). **B55 is new** — the two-stage
> `reqpubkey_sent.hash == 0` meaning, owed to the companion contract.
> ★★ **2026-08-02: `§id-hash S4b` IS BUILT — the by-id flow is ONE command** (bounded `resolve-id-for-pubkey` intent +
> on-node second stage + a bounded loud timeout; note in `BASELINE.md`). ⚠ **B55 did NOT close with it and was NOT
> meant to**: `reqpubkey_sent.hash == 0` is the honest report of a stage-1 acceptance and stays — what changed is the
> INSTRUCTION it carries (`do not re-issue`), so the owed contract paragraph is now a **correction**, not an addition.
> **B56 / B57 / B58 are new** (S4b's registered residuals). **S5** (`confirmid`) remains undispatched, still on O4/O5.
> ⚠⚠ **AND THE FAMILY'S INHERITED PREMISE — "the corpus is structurally blind to the `claimed` tier" — IS NOW
> DISPROVEN IN BOTH DIRECTIONS.** S3 showed the *floor* is corpus-live (a beacon-stamps-claimed probe moves 5/36);
> S4a shows the *producer* is corpus-live too — **26 team-plane ingests across five scenarios** — while the streams
> stay 36/36 byte-identical **because the floor contains them**, proven by disabling the floor (3/36 move) and then
> disabling it again with the producer reverted (back to clean). ⇒ **stop writing "the corpus cannot see this tier";
> write which LAYER it cannot see, and measure it.**
> ⚠ **B46 was a live demotion bug that exists independently of the feature** — registered and fixed on its own merits,
> in its own slice, not buried inside the design entry.
> ⚠⚠ **ONE MEASUREMENT THE WHOLE FAMILY SHOULD INHERIT (S2b, 2026-08-01): the 36-scenario corpus contains ZERO
> `IdBindConf::claimed` bindings.** Instrumented directly: **299 441** matching-row `id_bind_set` updates, every one
> `existing=authoritative, incoming=authoritative, same hash`; plus **5 444** NEW-row inserts, all `authoritative`
> (4559 `bcn` · 858 `self` · 14 `h_relay` · 13 `h_query`). ⇒ **the corpus is structurally blind to the entire `claimed`
> tier**, so S3/S4a — which are ABOUT that tier — will be equally invisible to it unless a scenario is authored to
> produce a relayed soft H answer. Budget scenario work into those slices; do not expect the corpus to gate them.

### B42 — ★★ by-id `reqpubkey` was team-only by construction · FIXED 2026-08-01
`console_parse.cpp:~232` sets `bool team = (out.u.resolve.dst_id != 0)` — a bare decimal **forces** the TEAM plane — and
`:~234` accepts only `-t`, so **no `-s` exists**. `node.cpp:~1646` then resolves via `team_key_of_id` alone, whose first
line is `if (_cfg.team_id == 0 || !is_team_peer(id)) return false;` (`node_routing.cpp:~842`). ⇒ with `team_id == 0`
every bare-id `reqpubkey` returns `err_no_binding`, **including for a directly-heard neighbour held authoritatively**.
★ **MEASURED on the bench 2026-08-01**, and the two-verb contrast is the whole proof on one node: `hashof 186` →
`0x61CD83EA` (reads `_id_bind` via `key_hash_of_id`) while `reqpubkey 186` → `err_no_binding` (reads `_team_keys`).
This is verbatim the defect `firmware_commands.cpp:527-530` records from 2026-07-30 — *"Each verb was correct about its
own table; neither answered the question"* — **whose fix landed on `hashof` and never on `reqpubkey`**.
★ **A SECOND SITE, and the fix is half-landed without it** (the sweep-scope meta-bug's tenth instance, this time across
**transports**): `src/fw_main.cpp:~490-491`, the **BLE** `reqpubkey_sent` echo, resolves through `team_key_of_id` too, so
a static-plane by-id `reqpubkey` would still echo `hash=0` to the companion after `on_command` is fixed.
**Fix shape:** both sites read `Node::peer_book_by_id` (U1 — it already searches both planes and already returns a
*mask*); add `-s`, make `-s`/`-t` exclusive, and specify plane-ambiguity (spec §3-D9). No wire change.
**Coverage owed:** static-only node · team-only node · the same numeric id live in BOTH planes · unresolved id on a
dual-plane node · the BLE echo's resolved hash. **MEASURED** (bench) + in-source.
★ **CLOSED 2026-08-01 by slice `§id-hash S1` (green, UNCOMMITTED).** Both sites now read `Node::peer_book_by_id`; `-s`
lands, `-s`/`-t` are exclusive, a bare id goes out as plane 0/AUTO and `on_command` picks per §3-D9 (both planes hold
it ⇒ the new `CmdCode::err_ambiguous_plane`, and **no airtime is spent guessing**). The BLE echo no longer re-resolves
anything: `CmdResult` now carries `dst_hash` (the RESOLVED hash) and a new `plane` field, and every transport reads
them. All five owed coverage cases are native tests (`test_node_hashlocate.cpp`, the `§id-hash S1` block).
⚠ **ONE OWED CASE COULD NOT BE BUILT AS SPECIFIED, and the reason is a spec correction — see §3-D9 in the ENTRY BELOW
and BASELINE's S1 note:** *"unresolved id on a dual-plane node"* has **no distinguishable outcome in S1**, because an
unresolved by-id `reqpubkey` refuses (`err_no_binding`) BEFORE any plane is selected or any query flies. It becomes a
real case in S4a, where an unresolved id does fly a by-id query. Pinned as such by a test rather than faked.
⚠ **NEW FINDING while building it → B47 below** (an off-grid mobile answers `queued` for a GLOBAL by-id request that
provably airs nothing).
⚠⚠ **COMPANION CONTRACT OWED (reported, NOT written — `ios-companion/INBOX_SYNC_CONTRACT.md` is QA's file).**
★★ **CORRECTION 2026-08-01, and it was my error: I first reported these as "three ADDITIVE changes, all
backward-safe". THAT WAS WRONG, and the assessment (P1b) caught it.** The RESPONSE-side changes are additive and the
Swift decoder tolerates them; **the OUTGOING command's MEANING changed, and that is a break**:
① ★★ **BREAKING (outgoing):** `Command.reqPubkeyTeam(localID:)` emitted the bare `reqpubkey <id>` and relied on the
firmware reading a bare decimal as implicitly TEAM. S1 made a bare decimal **AUTO** ⇒ that operation no longer
guarantees its own name: it draws `err_ambiguous_plane` when both namespaces hold the number, or silently selects
**static** when only a static binding exists. **FIXED in the checked-in companion** — `Command.swift:150` now emits
`reqpubkey <id> -t`, `CommandEncoderTests.swift` pins the exact line, and the stale "implicitly TEAM" comments are
corrected. ⚠ **NOT gate-verified: `swift` is unavailable in this environment — bench/CI-owed.**
② three new ack codes: **`err_ambiguous_plane`** (CmdCode 10), **`err_no_identity`** (11) and
**`err_tx_ring_full`** (12). `AckCode` in `Inbound.swift` has a `.unknown` forward-compat case so all three degrade,
exactly as the already-unmodelled `err_ack_ring_full` does. ⚠ **`err_tx_ring_full` is TRANSIENT** — the app should
retry rather than surface a configuration error; it is NOT `err_ack_ring_full` (different ring, different remedy).
③ `{"ack":…}` may now carry **`"plane":"team"|"static"`**, and `{"ev":"reqpubkey_sent"}` the same — **both omitted
when absent, so every pre-S1 line is byte-identical** (pinned by a test).
④ ★ **`reqpubkey_sent.hash` is now the RESOLVED hash** for a by-id request, where it used to be `0` on the static
plane — and, per B47, the event now fires **only when the TX path ACCEPTED the frame**. ⚠ **"accepted", not "aired"**
(owner ruling 2026-08-02): a synchronous `CmdResult` cannot prove a future transmission, because an LBT-deferred frame
reaches the radio after `on_command` has already returned. Acceptance = no bail-out, no LBT-ring drop, no `DeviceHal`
rejection; a later `pump_tx` radio-start error is **outside** the guarantee. The residual is covered by the late
`!!` report, not by this event.
⑤ the grammar itself: `reqpubkey <0xhash|id> [-s|-t]`, flags mutually exclusive, bare id = AUTO.

### B43 — ★★ no id → hash for a node we **route to** but never heard directly (both planes) · FIXED 2026-08-02
`_team_keys` is written at exactly one site — `node_beacon.cpp:~831` — reached only from a **directly-heard same-team
beacon**. A multi-hop teammate still gets its `_team_peer` dispatch bit, from the DV merge at `node_beacon.cpp:~939-940`,
off a route entry that **carries no key**. Static is the same shape: `_id_bind` is fed by a heard beacon
(`node_beacon.cpp:~664`) or an H answer, never by a route. ⇒ a peer is **routable but unidentifiable**, which blocks
`reqpubkey`, sealed send, `team grantkey` and any `hashof` answer.
★ **MEASURED on the bench 2026-08-01, on BOTH planes**: team — `[peer] team_id=114` / `team_id=214` with no hash while
228 is keyed; static — `_rt` holds routes to 48 (3 hops), 59 and 109 (2 hops) and **not one appears in `peers all`**.
★ **Not a new discovery — the deferred half of §hashbind-plane / B2.** `on_hash_bind_snoop`'s header already scoped it
and marked it `✖ MISSING` on 2026-07-31 (`node_hashlocate.cpp:~1245-1248`): *"a team-plane bind store with its own
confidence field … **needs the trust question in (1) answered first**."* The owner answered it 2026-08-01: an on-air
id→hash answer is a **claim**, never authoritative.
**Fix shape:** spec S3 + S4a/S4b — `H_FLAG_BY_ID` (byte 7 has four free bits), owner-only answers, `claimed` landing on
both planes, and the two-stage `reqpubkey` completion.
**Coverage owed:** the spec's §9 gate list. **MEASURED** (bench, both planes).
★ **CLOSED — THE BINDING HALF — 2026-08-02 by slice `§id-hash S4a` (landed at HEAD `2ff40dc`).** `H_FLAG_BY_ID = 0x10`
reuses H bytes 2-5 as a **canonical** zero-extended id (bytes 3-5 zero, ids 0/255 refused on pack **and** parse,
`h_by_id_key_canonical` is the one predicate all three sites share); `by_id` joins the `HashQuerySeen` key (at **0
bytes** — `offsetof == 19`, `sizeof` 24 unmoved) and rides every forward. **Only the OWNER answers**, self-matching on
`_node_id` (static) / `team_local_id()` (team), never from a cache — spec §3-D3's principle: *a cached answer is
allowed exactly when the answer is self-verifying, and an id→hash one is not.* The answer is a plain
`DATA_TYPE_H_ANSWER` (§3-D4's `binding_verifiable = false`), so it lands **`claimed`** on the static plane through the
existing codepoint and — **newly built** — in `_team_keys` on the team plane, ★ **without ever touching `_team_peer`**.
`reqpubkey <id>` on an unresolved id now FLIES that query instead of refusing (spec §5 stage 1), which is the
originator B43 needs; **B53's floor, lowered in the same slice, is what makes the resulting row visible.**
★★ **CLOSED IN FULL — 2026-08-02 by `§id-hash S4b` (landed at HEAD `2ff40dc`).** A bounded
`resolve-id-for-pubkey` intent records stage 1, consumes the owner's claimed binding answer, and automatically emits
the existing hash-keyed pubkey request as stage 2. A bounded loud timeout clears an unanswered intent. The operator
workflow is therefore one `reqpubkey <id>` command on both planes.
ⓘ `reqpubkey_sent.hash == 0` remains the honest stage-1 acceptance report; its app-contract meaning is tracked by B55.
⚠ **NOT unblocked, per spec §8:** multi-hop `team grantkey` and sealed send still refuse a claim. That is the owner's
confidence split working, not a gap.
★★ **THE MEASUREMENT WORTH INHERITING (and it contradicts spec §6's own "re-anchors" prediction): the corpus is
36/36 BYTE-IDENTICAL, and that is CONTAINMENT rather than absence.** Instrumented: the new team ingest fires **26
times** across s24/s25/s26/s28/s34 (13 destination `h_query` + 13 relay `h_relay`, **every one arriving on an
AUTHORITATIVE frame type**); of those, **13 are refused by S3's D5c① rule**, **6 insert new claimed rows** and **7
refresh a claim**. The streams do not move because every reader is either behind the default `authoritative` floor or
lives in `src/` (outside the sim build). **Proof, not inference:** disabling the two team floors moves **3/36**
(s24/s25/s26, event counts *falling* 1574→1408 / 792→638 / 1045→854 — a cache doing its job), and doing that *with the
ingest reverted* returns all 36 streams **byte-identical to clean**.

### B44 — `peers all` had no static equivalent for routed-but-unkeyed peers · FIXED 2026-08-01
`peer_book_walk` (`node_hashlocate.cpp:~462-507`) runs four passes — `_peer_keys` → `_id_bind` → `_team_keys` →
`_team_peer` bits. The fourth emits an id-only row for a teammate we route to but hold no key for; **there is no pass
over `_rt`**, so the static plane has no counterpart and the team plane is currently the *more* informative of the two.
★ **MEASURED on the bench 2026-08-01**: 114/214 listed, 48/59/109 absent despite live routes (same transcripts as B43).
**Fix shape:** a static `_rt` pass mirroring team pass (4), gated on `include_id_rows` so the JSON book
(`include_id_rows=false`, `:~466`) stays untouched. **MEASURED** (bench).
★ **CLOSED 2026-08-01 by slice `§id-hash S2` (green, UNCOMMITTED).** Pass **(2b)** added, sitting between `_id_bind`
and the team passes; the JSON book is untouched (asserted: `include_id_rows=false` returns the `_peer_keys` count with
ten live routes present). ⓘ **Its dedup is against `_id_bind` MEMBERSHIP, not `key_hash_of_id`** — the accessor filters
`claimed` rows and hash-0 rows that pass (2) still emits, so testing through it would print those ids twice; a native
test pins exactly that (a `claimed` binding for 48 ⇒ still one row).
ⓘ **Renderer honesty, folded in:** `peers_text_row` printed `(auth)`/`(claimed)` from `static_authoritative` for EVERY
`static_id`, so an id-only row would have read `static_id=48(claimed)` — a claim nobody made. The suffix now prints
only when the row carries a hash to be authoritative *about*. This also cleans up pass (2)'s hash-0 rows.

### B45 — `peers all` listed the local node as its own peer · FIXED 2026-08-01
`id_bind_set(_node_id, _key_hash32, IdBindSource::self, IdBindConf::authoritative)` (`node.cpp:~77`, `:~539`, `:~864`)
seeds our own binding into `_id_bind`, and `peer_book_walk`'s pass (2) has **no self-skip**.
★ **MEASURED on the bench 2026-08-01**: node 42 reports `[peer] hash=0x8CC9BDFF static_id=42(auth)`, so `count=2`
actually means "one peer and me". **Text-console only** — the JSON book passes `include_id_rows=false` and we are not in
`_peer_keys`. **Fix shape:** skip self in pass (2). **MEASURED** (bench).
★ **CLOSED 2026-08-01 by slice `§id-hash S2` (green, UNCOMMITTED).**
⚠ **THE PREDICATE IS THE SELF-BINDING, NOT THE ID**, and that is load-bearing rather than pedantic: the skip is
`node_id == _node_id && key_hash32 == _key_hash32` — `id_bind_set`'s own self-defence test (`:~59`), verbatim (U1).
`node_id == _node_id` alone would ALSO hide a FOREIGN key claiming our id, i.e. an address collision — the single most
diagnostic row this dump can carry, and the exact condition `addr_conflict_self_defended` exists to surface.

### B46 — ★★ a claimed observation could demote or displace an authoritative binding · FIXED 2026-08-01
On a matching row, `node_hashlocate.cpp:~102-104` writes the incoming `source` and `confidence` **unconditionally**:
```cpp
_active->_id_bind[i].last_seen_ms = now;
_active->_id_bind[i].source       = static_cast<uint8_t>(source);
_active->_id_bind[i].confidence   = static_cast<uint8_t>(confidence);
```
⇒ **live today, with no new feature required**: a relayed soft H answer (`IdBindSource::h_relay`,
`node_hashlocate.cpp:~1252-1253`) demotes a first-hand beacon binding to `claimed`, and the **seal path then refuses**
— `key_hash_of_id:~148` filters `confidence != authoritative` — until the next beacon re-asserts it. The sibling store
already has the correct rule and is the reference: `peer_key_set:~255-261` upgrades and never downgrades.
⚠ **Also a correction to the record:** there is **no `IdBindConf` NV encoding at all** (`src/device_nv.h`'s `kPeerConf*`
is `PeerKeyConf`'s). `_id_bind` is RAM-only, TTL-bound at 48 h (`protocol_constants.h:~535`) — which is why the spec's
manual-confirm verb is scoped as *ephemeral*, not as a pinned trust anchor.
**Fix shape:** confidence upgrade-only; and a `claimed` sighting must **not** extend an `authoritative` row's
`last_seen_ms` (a claim must not keep an unverified binding alive) — deliberately symmetric with the spec's team-plane
rule §3-D5c. ⚠ **May legitimately re-anchor** — an unattributable re-anchor is a failed gate.
**Coverage owed:** claimed-after-authoritative (no demotion, no TTL extension) · authoritative-after-claimed (promotes)
· same-node re-key still applies · the self-binding stays exempt. **UNMEASURED** — found in-source by the design review.
⚠⚠ **RE-OPENED 2026-08-01 (assessment P1a), THEN CLOSED — read both halves.**
**FIRST FIX (incomplete):** the matching-row write became upgrade-only. **What it missed:** `id_bind_set` also enforces
the REVERSE rule (one hash -> one node_id) through `id_bind_evict_other_hash_holders`, and **both** accept paths called
it without looking at confidence. So the same demotion survived through a different door: authoritative `{10,H}` +
a relayed claimed `{20,H}` takes the NEW-node_id path, **evicts the authoritative row**, and inserts the claim.
★ **SECOND FIX — CLOSED 2026-08-01 (green, UNCOMMITTED):** a claimed observation may not displace an authoritative
holder of the same hash. New `id_bind_auth_holder_other()` (gates = `key_hash_of_id`'s verbatim, U1: authoritative +
fresh, self exempt from the TTL); the whole write is REFUSED with a named `addr_rehome_refused` emit, because
inserting-without-evicting would leave two rows for one hash and break the bijection `id_bind_find_by_hash` relies on
— strictly worse. **Owner ruling: claimed -> claimed stays NEWEST-WINS** (no trust ordering between two claims; keeping
it makes this a fix, not a redesign — C1). An **authoritative** rehome still evicts, held as the positive control.
An **expired** authoritative holder does not block (it is invisible to every reader already), also tested.
★ Confidence is upgrade-only; `source` and `last_seen_ms` are frozen with it, so a claim can neither relabel the
provenance nor fake first-hand liveness. All four originally-owed coverage cases are native tests, plus the
conflict/self-defence arms as controls. **`IdBindSource::manual` was
DELIBERATELY NOT added** — its only producer is `confirmid`, which is S5, and an enumerator with no writer is exactly
the `PeerKeyConf::overheard` smell the spec criticises in §2.4. **Spec §6's S2b row should be amended to drop it.**
★★ **IT DID NOT RE-ANCHOR — 36/36 byte-identical — AND THE REASON IS MEASURED, NOT ASSUMED.** Both doors were
re-probed after the second fix (assessment §4.6): same-row demotion **0/36**, cross-id claimed rehome **0/36**. Then a
CAPABILITY probe made the instrument capable — force the corpus's own `h_relay`/`h_query` learns to land `claimed`
(the exact input class this rule protects against): that alone moves **3/36**, so the precondition is now live, and
under it the numbers are decisive across **304 885** `id_bind_set` calls:
· **same-row demotion: 15 occurrences** ⇒ the guard IS executed — but reverting it under the same poison still moves
  **0/36**, because the corpus re-asserts those rows from a first-hand beacon **296 397** times and the demotion window
  closes before any reader looks. A real masking mechanism, not an absent one.
· **cross-id claimed rehome: 0 of 304 885** ⇒ **structurally unreachable even with claimed bindings forced**, because
  no corpus soft answer ever brings a hash in under a SECOND id. ★ **That is precisely why the original poison matrix
  could not have caught P1a, and it is the honest statement to inherit: this door has no corpus gate at all, only
  native.**
⚠ **THE INTENDED SIDE EFFECT, stated so it is not later read as a regression:** an authoritative row that only ever
gets re-CLAIMED now ages out at `id_bind_ttl_ms` (48 h) instead of living forever on hearsay. That is the point of the
liveness half of the rule; the self-binding remains exempt via `id_bind_age_out`'s `self_keep`.

### B47 — an off-grid mobile could accept a GLOBAL `reqpubkey` that transmitted nothing · FIXED 2026-08-01
`emit_hash_query` bails at `want_pubkey && mobile_req && origin == _node_id && !team_scoped`
(`node_hashlocate.cpp:~1557`) — an unregistered mobile's `_node_id` is a LOCAL id with no static return path, so the
owner could not answer. It emits `h_want_pubkey_mobile_no_route` and **no frame**, but `on_command` has already
returned **`CmdResult{queued}`**, so the operator/app is told the request went out.
★ **MEASURED 2026-08-01** by a native test (`test_node_hashlocate.cpp`, the `§id-hash S1 §3-D9` case): on an
unregistered mobile the `-s` arm produces `queued`, `h_want_pubkey_mobile_no_route`, zero `h_tx`, zero tx_frames —
while the `-t` arm on the SAME node in the SAME test flies a real frame (the control).
**PRE-EXISTING, NEWLY REACHABLE.** The bail is old and already documented in `s22_mobile_team`'s `_desc`
(*"on this homeless off-grid member NO h_tx is emitted at all"*). What changed with `§id-hash S1` is the WAY IN: a bare
`reqpubkey <id>` used to force TEAM, so this arm needed an explicit `reqpubkey 0x<hash>`; now a bare id that resolves
STATICALLY on an off-grid member lands there too.
**This is B39's class** — `queued` means "accepted", never "sent".
⚠⚠ **WIDER THAN I RECORDED, and the assessment (P1c) found the rest: `emit_hash_query` is `void` and returns early in
FOUR ways, not one** — degenerate/self target, **no crypto identity**, off-grid mobile with no return path, and a
`pack_h` codec failure. `on_command` answered `queued` through all four and BLE turned every one into
`{"ev":"reqpubkey_sent"}`, whose contract meaning is *"the on-air request was flooded"*.
★ **The no-identity case also DISPROVES two claims of mine**: the in-source comment at the BLE echo and the companion
contract both said that path *"keeps its existing error ack"*. **There was no such ack** — it reported success.
★ **CLOSED 2026-08-01 by `§id-hash S1b` (green, UNCOMMITTED).** `emit_hash_query` now returns
`Node::HQueryOutcome{sent, degenerate, no_identity, no_return_route, encode_failed}` and the reqpubkey arm maps it to
an honest `CmdResult`: `err_unsupported` / **`err_no_identity`** (new code) / `err_no_gateway` / `err_too_large`,
each still echoing `dst_hash` + `plane`. ⓘ **The "preferred" option was taken and it did NOT become refactor-plus-fix
(C1): widening `void` -> the enum changed ZERO call sites**, because all four other callers already discarded the
absent return and there is no `[[nodiscard]]` — so the diff is the function's own returns plus one reader.
★ **`CmdResult::aired`** (free — it lands in the existing tail pad, `sizeof` stays 20) is what `reqpubkey_sent` is now
keyed on, because one **accepted** outcome legitimately airs nothing: the **hosted-mobile local cache hit**, which is
a genuine success reported through its own `peer_key_cached` push and must not also claim a flood.
**Coverage:** each branch asserts the `CmdResult` **and** the BLE-visible disposition (a test-local mirror of
fw_main's exact `code == queued && aired` predicate), every negative paired with a same-fixture successful flight.
**MEASURED** (native; 4 cases / 10 assertions redden when the outcome mapping is bypassed).
⚠⚠ **AND IT WAS STILL ONE LAYER SHORT — a FIFTH bail point, found by the second QA pass.** `tx_initiating`
(`node_mac.cpp:1095`) was itself `void` and **discarded** `schedule_lbt_defer`'s `bool`, which is `false` when the
4-slot LBT defer ring is full (*"ring full -> drop loudly"*). ⇒ frame **dropped** → outcome still `sent` → `aired=true`
→ **`reqpubkey_sent`**. Same false-success class, one call deeper, and it breached spec **§5.1**'s *"must not be
reachable from any bail point"*.
⚠⚠ **RE-OPENED 2026-08-01 A SECOND TIME (assessment §6) — and this one reached REAL HARDWARE.** S1c stopped at the
Node's LBT ring. One layer below, `tx_with_retry` did `_hal.tx(...)` and **discarded the `TxResult`**, then
`return true // handed`. `DeviceHal::tx` answers `busy` when its **8-entry outbound ring** is full — it bumps
`txq_drops` and **does not retain the frame** — and an H/beacon frame has `slot < 0`, so there is no stash retry
either. A definitive hardware drop still reported acceptance. Neither automated gate could see it: this repo's test
HAL returned a hard `ok`, and the sim's `FirmwareNode::simTx` pushes onto an **unbounded vector**.
★★ **OWNER RULING that settled the semantics (2026-08-01), after two rounds of chasing a stronger claim:**
`reqpubkey_sent` means **"the TX path ACCEPTED the frame — nothing rejected it"**, NOT a claim of airtime. *"Emitted
only when a frame actually left"* is **unsatisfiable synchronously** — a deferred frame reaches the radio when a timer
fires, long after `on_command` returned. ⇒ `CmdResult::aired` is renamed **`accepted`** and documented as such.
★ **CLOSED 2026-08-01 by `§id-hash S1c` + `§tx-admission TX1` + `§id-hash S1d`:** `tx_initiating` returns `bool`;
`tx_with_retry` inspects the `TxResult` (see **B50**); `HQueryOutcome::tx_dropped` maps to **`err_tx_queue_full`**
(renamed from `err_tx_ring_full` — see below). ⓘ **U1 CHECKED: `err_ack_ring_full` (9) was NOT reused** — same shape, different ring, and the
shipped contract documents it as the pending-E2E-ack ring, so reusing it would hand the app a wrong diagnosis and a
wrong remedy. This one is the only **TRANSIENT** refusal on the verb ("retry in a moment"), and the hint text says so.
⚠ **AND THE SAME REASONING FORCED A RENAME ONE DAY LATER: `err_tx_ring_full` → `err_tx_queue_full`.** **TWO** bounded
queues can reject this command — the Node's 4-slot LBT defer ring and `DeviceHal`'s 8-entry outbound ring — so a name
(or an operator hint) fingering one of them is a wrong diagnosis half the time. The hint now says *"a bounded TX queue
rejected the frame (the radio or the channel is saturated)"* and names neither. Renaming was free (M3, and the code
was uncommitted).
★ **THE DEFERRED RESIDUAL — no silent loss.** A frame ACCEPTED into `_deferred_lbt` can still meet a full HAL queue
when its timer fires, and unlike DATA an H query has **no MAC timeout** behind it to recover. That death is now
reported LATE: `tx_deferred_lost` **plus an operator-critical `_hal.log`**. ⚠ **CORRECTED (QA P2): my first version of this was
FALSE on metal** — `fw_main`'s sink gated every log line on `g_mr_trace_on`, so under `debug off` the report was
completely silent and `_hal.log` is a DEBUG channel, not an operator channel. The message now carries a `!!`
operator-critical marker and the sink prints those regardless of trace.
⚠ **A BOUNDED RE-DEFER WAS CONSIDERED AND REFUSED, and the reason is specific to the payload:** re-deferring would air
an H query at an unbounded later time, after the operator has been told and has plausibly retried by hand —
duplicate airtime for a question that is already stale — and it needs retry state plus a second timer path for a case
no automated gate can reach. ⓘ A per-command PUSH was also refused: correlating the loss back to the `reqpubkey` that
queued it needs a handle the frame does not carry, and `send_failed{ctr:0}` is exactly the uncorrelated shape **B39**
exists to fix. Owed to B39 (C1), recorded rather than faked.
★ **SCOPE RULING (owner, superseded and restated 2026-08-01): a SUCCESSFUL defer is `accepted`** — the TX path
took it. ⚠ It is **NOT** a claim that it flew (that is unsatisfiable synchronously); a deferred frame that dies later
is reported by the late `tx_deferred_lost` + the operator log. Only a DROP is false. `lbt_complete`'s own early returns are excluded on inspection: both are
RTS-only (a stale-flight cancel, and a duty defer that does fly) and neither is reachable from `emit_hash_query`,
which always passes `LbtKind::flood`.
ⓘ **The widening again changed ZERO call sites** — 22 callers across 8 files discard it and there is no
`[[nodiscard]]`; `git diff` shows the six files holding 18 of them were not touched at all.
**MEASURED** (native): the ring-full fixture reddens 1 case / 3 assertions when S1c's `bool` is discarded; the
HAL-rejection fixtures redden **2 cases / 9 assertions** when the `TxResult` is discarded, **2 / 7** when
`lbt_complete` swallows it, and **1 / 2** when the deferred-loss report is removed.
★ **Corpus: 0 HAL rejections and 0 deferred losses — structurally, not accidentally** (the sim's `simTx` has an
unbounded queue and can only answer `too_long` at len > 255, which no packer produces). A **capability probe** (force
the HAL to reject every flood) reaches **7123** rejections, and the behavioural delta isolates to **s22's
`"OK reqpubkey queued"` → `"OK reqpubkey error"`** — with the propagation reverted under the same poison, all 36
streams return **byte-identical to clean**, proving the poison is otherwise inert.

### B48 — ★ a display de-duplication rule was making an airtime decision · FIXED 2026-08-01
`peer_book_by_id`'s team arm read `if (team_key_of_id(id, th) && !(mask && th == h))` — it suppressed the TEAM
presence bit whenever both planes resolved the **same hash**. Harmless while only `hashof` read the mask (it was a
tidiness choice, from `§AB3`); **`§id-hash S1` made that mask select a query plane**, and there it produced two wrong
answers on a node where one identity occupies the same number in both namespaces:
· a bare `reqpubkey <id>` **silently selected STATIC** instead of §3-D9's ambiguity refusal;
· an explicit `reqpubkey <id> -t` saw `has_team == false` and returned **`err_no_binding` for a team binding that
  exists**.
★ **Found by the independent implementation assessment (P2)**, not by the corpus: the original D9 test covered only
DIFFERING hashes, so the exact-duplicate branch was untested.
★ **CLOSED 2026-08-01 by `§id-hash S1b` (green, UNCOMMITTED):** the resolver now reports PRESENCE per plane and
nothing else — hash equality never made the two planes' routes, return paths or flood scope equal, which is exactly
what the flag selects. Identity de-duplication, if ever wanted, is a **renderer** concern; `handle_hashof` now prints
both rows and their equal hashes say "one identity, two planes" more clearly than the suppressed row did.
**Coverage:** the same-id/same-hash dual-plane test — bare = `err_ambiguous_plane` with no airtime, `-t` sends on the
team plane, `-s` selects the static row. **MEASURED** (native; restoring the de-dup reddens 2 cases / 12 assertions).
⇒ ★ **The durable rule: a shared resolver returns facts. The moment a "tidy display" filter lives inside one, some
future caller will make a decision on the filtered answer.**

### B50 — ★★ `tx_with_retry` discarded the HAL transmitter result · FIXED 2026-08-01
`lib/core/node_mac.cpp`'s central TX helper did `_hal.tx(bytes, len, p);` and then `return true; // handed`, throwing
away the only answer the radio layer gives. On hardware `DeviceHal::tx` returns **`busy`** when its 8-entry outbound
ring is full (it increments `txq_drops` and **does not retain the frame**) and **`too_long`** past the SX1262 length
register. ⇒ **every** TX caller in the tree — not just `reqpubkey` — could not distinguish "queued for the radio" from
"dropped on the floor".
⚠⚠ **CORRECTION 2026-08-01 — THE CLAIM "a function every TX path goes through" WAS FALSE, and it hid a second site
for a full round.** `tx_flood` does **not** go through `tx_with_retry`; it calls `_hal.tx` **directly**. The dispatch
that produced TX1 asked for a sweep of *`tx_with_retry`'s callers*, which is the wrong set — the right one is **every
direct `_hal.tx` caller**. ★ **That is the arc's next sweep-scope instance** (after directory-vs-file, verb-prefix,
predicate-vs-pattern and transport scope), and it was in the dispatch, not the implementation.
★ **THE FULL CLASSIFICATION — all FOUR direct `_hal.tx` call sites, which is what B50 should have listed from the
start:**
| # | site | consumer of the result | verdict |
|---|---|---|---|
| 1 | `node_mac.cpp:~1300` `rts_duty_defer_fire` | none — the function is `void` and calls `start_rts_timeout()` unconditionally | **OK as-is.** An RTS is retry-eligible in effect: the CTS-wait timeout it arms IS the recovery, exactly the argument that keeps TX1's three readers arming theirs. No app-visible claim, no state burned. |
| 2 | `node_mac.cpp:~1335` `tx_flood` immediate | ★ **`emit_beacon:517` reads it as `sent` and `:525` commits the channel digest** | ★★ **WAS BROKEN — fixed by TX2.** Not telemetry: a dropped beacon burned an advertisement horizon. |
| 3 | `node_mac.cpp:~1493` `tx_with_retry` | 3 readers (`duty_defer_fire`, both `do_data_tx` arms) | **fixed by TX1.** |
| 4 | `node_mac.cpp:~1534` `retry_stashed` | none directly; it re-arms `awaiting_ack` + the ack timeout below | **OK as-is** — retry-eligible frame, the MAC timeout is the recovery (same argument as #1). |
⇒ **exactly one of the four was a real defect beyond TX1, and it is the one the wrong sweep could not have found.**
★ **This is registered separately from B47 on purpose:** B47 is one verb's contract; this is the shared TX layer, and
its attribution must stay separable (C4).
⚠⚠ **THE OBVIOUS FIX WOULD HAVE CAUSED A REGRESSION, and finding that changed the slice's shape.** The dispatch
assumed `tx_with_retry` might be a `void`/all-discard function. **It is neither:** it already returned `bool`, and
**three callers READ it** — `duty_defer_fire` and both `do_data_tx` arms — each using it to decide *"arm the post-TX
state?"*. Their existing `false` means **"not sent, but a re-send timer IS armed"** (the duty defer). A HAL rejection
arms nothing, so folding it into that same `false` would have suppressed `start_ack_timeout()` on a dropped DATA and
left the flight with **no recovery at all** — strictly worse than the reporting defect. For a retry-eligible frame the
MAC timeout **is** the recovery (`device_hal.h` says so).
★ **FIXED 2026-08-01 by `§tx-admission TX1` (green, UNCOMMITTED, its own commit):** the return becomes a three-way
`Node::TxHandOff{handed, deferred_retry_armed, rejected}`. The three existing readers branch on
`!= deferred_retry_armed`, which is **bit-for-bit their previous `true`** — pinned by two `static_assert`s beside them
so a future fourth enumerator on the wrong side of that line is a build failure, not a silent DATA regression. The
result propagates through `lbt_complete` (whose two RTS-only early-outs are excluded on inspection: a stale-flight
cancel abandons a dead flight, and the duty defer re-arms) and out of `tx_initiating`.
ⓘ Telemetry only (`tx_hal_rejected`) — MR_EMIT is device-stripped, and the metal-side diagnostic `txq_drops` already
exists. ⚠ **`txq_drops` has no console surface**, so on metal a rejection is currently invisible unless it hits the H
path; noted in the bench script, not fixed here (C1).
**MEASURED** (native + capability probe; see B47). **Corpus 36/36 byte-identical** — 0 rejections, structurally.

### B51 — ★★ `tx_flood` discarded admission results and burned channel-digest state · FIXED 2026-08-02
`tx_flood` answered `true` in two cases where the frame was definitively gone: a **full 4-slot LBT defer ring**
(`schedule_lbt_defer`'s result discarded) and a **HAL rejection** (`_hal.tx`'s `TxResult` discarded).
★★ **NOT a telemetry defect.** `emit_beacon` takes that boolean as `sent` and gates
`commit_channel_digest_advertised` on it, under the comment *"burn an ad_count … ONLY for advertisements that ACTUALLY
AIRED … the air-honesty fix"*. The commit does `++e.bcn_ad_count` and, on horizon, `e.dirty = false`. ⇒ **a dropped
beacon consumed the advertisement horizon and could RETIRE a digest nothing ever received** — the air-honesty
mechanism defeated by two discarded returns.
★ **FIXED 2026-08-01 by `§tx-admission TX2` (green, UNCOMMITTED) at ZERO bytes.** Both sites return the real result;
the digest commit now follows the **acceptance** boundary — the same one the owner ruled for `reqpubkey_sent` — so a
ring-full drop and a HAL rejection both leave the entry **dirty**, and an accepted defer commits.
★★ **THE RESIDUAL WAS RULED ON, AND THE RULING REVERSED THE EARLIER CALL — `§tx-admission TX3`, 2026-08-02.**
> for channel-digest accounting, **"sent" means accepted by the transmitter/DeviceHal** — the strongest boundary the
> current architecture can observe. It does not mean literal RF airtime.
⇒ commit-on-LBT-entry is gone. The advertised digest ids ride the deferred slot (`DeferredLbt::digest_ids[3]`) and
the commit happens in node.cpp's defer arm **iff the deferred `lbt_complete` reaches `_hal.tx` and DeviceHal answers
ok**; a ring-full drop or a HAL rejection leaves **both** the `bcn_ad_count` and the dirty flag untouched. Immediate
beacons commit right after their own `_hal.tx == ok`. The commit now lives in `tx_flood`/the defer arm — the two
ADMISSION points — not at `emit_beacon`'s call site, which could only ever have meant "we tried".
★ **Why the earlier reasoning was incomplete:** the `reqpubkey_sent` ruling settled an **app event**; digest
retirement is an **independently load-bearing state machine**, so carrying one boundary to the other was a design
decision, not an implementation detail.
⚠ **WHAT THE BOUNDARY IS NOT, said in every comment that states it:** a later `DeviceHal::pump_tx` radio-start error
drops the frame AFTER admission and is **outside** the guarantee.
★ **COST CAME IN UNDER THE ESTIMATE, MEASURED BY `offsetof`: +48 B, not +64.** `sizeof(DeferredLbt)` **164 → 176**
(+12/slot × 4); `offsetof(digest_ids) == 12`, `offsetof(buf)` 12 → 24 — it lands in the 4-aligned run before `buf`
and opens no new hole. `sizeof(Node)` **220976 → 221024**, and **ΔRAM = +48 on all three boards**, so the native
measurement holds on both ABIs. The count byte was dropped (`digest_ids[0] == 0` terminates — a live channel id can
never be 0), which is what bought back the 16 bytes.
⚠⚠ **IT RE-ANCHORS SIX SCENARIOS, ATTRIBUTED MECHANICALLY** — s15 · s15_metal · s17 · s28 · s29 · sim_9node_base
(s18 keystone UNMOVED). 3824 beacon defers corpus-wide is the exposure. **s28 settles the mechanism: the entire delta
is ONE `channel_dirty_cleared` moving `t=820265` → `t=820380`** — same node, id, channel, `ad_count`, `reason` —
**115 ms = exactly the LBT defer delay**. s15_metal 32/32 changed lines are `channel_dirty_cleared`, s28 2/2, s29 2/2,
sim_9node_base 6/6; s15 (29/33) and s17 (50/104) carry the expected SECOND-ORDER tail — an entry dirty ~115 ms longer
is re-advertised in the next beacon, so that beacon's content and its receivers' `beacon_rx`/bidi lines shift.
⚠ **AND TX3 COST B51 ITS OBSERVABLE:** once ring entry commits nothing, a ring-full drop and an accepted defer have
**identical** digest outcomes, so the digest can no longer discriminate `tx_flood`'s ring-full return — that poison
reddened nothing. The surviving discriminator is **`beacon_tx.result`** (0 admitted / 2 dropped), now asserted; with
it the step-6 poison reddens 1 case / 2 assertions. ⇒ **fixing one layer can silently remove the observable another
layer's test depended on.**
★ **B51's ring-full fixture now EXISTS and is gate-complete** (the 6-step recipe, with every premise asserted —
`tx_lbt_defer == 4`, `tx_flood_skipped == 0`, and a 1-hop neighbour installed so holder-coverage cannot retire early;
that guard is what my first `DEFER=0` attempt lacked).
**MEASURED** (native; corpus: 0 HAL rejections structurally — the sim's queue is unbounded — and the 6 attributed TX3 movers above).

### B49 — the `CmdCode` self-labelling test had a stale literal bound · FIXED 2026-08-01
`test_console_json.cpp`'s *"every refusal's token begins with `err_`"* loop — the ONLY detector for the §err-reason/B32
convention that `src/fw_main.cpp` prints the token BARE — ran `for (unsigned v = 0; v < 10; ++v)`. The moment `CmdCode`
grew past 9 it stopped testing the new enumerators, while still looking complete. ★ **Its own comment claimed the
opposite** — *"the `ord()` switch above already breaks the build when an enumerator is added, so this cannot go stale
unnoticed"* — which is true of the sibling walker and **false of this loop**, because `-Wswitch` cannot reach a literal
bound. Found by the second QA pass; three enumerators (10/11/12) were already excluded.
★ **CLOSED 2026-08-01 (`§id-hash S1c`): the bound now DERIVES from `ord()`** — walk the full underlying range and let
`kUnlisted` filter, exactly as `check_mapper_covers_every_enumerator` already did — so listing an enumerator in the
`-Wswitch`-guarded walker (which the build forces) automatically enrolls it here. **Bumping the literal was explicitly
rejected: it fails again identically at 13.**
ⓘ **A `_count` sentinel was considered and refused:** adding one to `CmdCode` puts a non-value enumerator into every
`switch` over it, so `-Wswitch` would then demand a `case _count:` arm at each mapper — a permanent tax on the
instrument that is working, to fix the one that is not.
**MEASURED** (native): mis-naming enumerator 12 is caught by the derived bound (1 assertion) and is **completely
invisible** with the old `v < 10` restored — 0 failures. The under-cover demonstrated, not argued.

### B52 — the JSON address book carries the TEAM plane's confidence but still NOT the STATIC plane's · NEW 2026-08-02
`§id-hash S3` added `"team_auth"` to `write_peer_row` (`lib/console/console_json.cpp`), so an app can finally tell
*"we heard that teammate's own beacon"* from *"somebody told us her number"*. **`static_id` is still emitted bare.**
The row already carries `PeerBookRow::static_authoritative` and the TEXT console has rendered `static_id=N(auth)` /
`(claimed)` since §id-hash S2 — only the JSON drops it. ⚠ **This is not hypothetical on the static plane the way it
is on the team plane: a relayed soft H answer lands `IdBindConf::claimed` in `_id_bind` TODAY**
(`on_hash_bind_snoop` → `id_bind_set(..., h_relay, claimed)`), and every such row reaches the companion as an
unlabelled `static_id`. An app that treats an id as identity is therefore already able to be wrong, on the plane that
has had the ladder longest.
★ **NOT FIXED IN S3 on purpose (C1):** it is a second shipped-contract change, and S3's contract with its own gate is
inertness. One line beside the `team_auth` one, plus its contract paragraph.
ⓘ **OWED REGARDLESS: `ios-companion/INBOX_SYNC_CONTRACT.md` has no `team_auth` entry** — QA owns that file, a coder
never edits it. The field contract as built is documented in `lib/console/console_json.h` beside `write_peer_row`.
**MEASURED** (native): the presence/absence rule is pinned three ways in `test_console_json.cpp` — `team_auth` rides
with `team_id` always, a static-only row's line is byte-identical to its pre-S3 golden, and true/false render
distinctly.

### B53 — inspection resolved ids at the authoritative rather than claimed floor · FIXED 2026-08-02
Spec `2026-08-01-id-to-hash-resolution-design.md` §3-D6 sets the display and pubkey-inspection floor at **`claimed`**
(*"shows a claim, labelled as one"*; *"the pubkey self-verifies against that hash, so fetching it is how you inspect a
claim"*). `Node::peer_book_by_id` — the ONE resolver behind all three verbs since §id-hash S1 — passes the
**`authoritative`** default on both arms. ⇒ a claimed STATIC binding is invisible to `hashof <id>` today, and the
team plane will inherit the same blindness the moment S4a writes its first claimed row.
★ **DELIBERATELY NOT CHANGED IN S3, and the reason is the gate:** lowering it is **not inert** — claimed static rows
exist in the live tree now, so `hashof` would start answering for them and `reqpubkey <id>` would start spending
**AIRTIME** at a hash the operator was never shown. Spec §6's S3 row requires *"s18 keystone reproduces by
construction (defaults)"*, which that would break.
⚠ **WHEN IT LANDS (S4a) IT MUST MOVE ON BOTH ARMS TOGETHER.** A resolver that filters one plane harder than the other
is spec §1-C's asymmetry defect rebuilt, and §1-C is one of the five defects this whole arc exists to remove.
ⓘ Already prepared: both arms read the accessor's `actual` and propagate it into
`static_authoritative`/`team_authoritative` instead of hardcoding, so the display cannot start lying when the floor
moves. `node_hashlocate.cpp`'s in-source note states the whole of this beside the code.
**MEASURED** (native): a test pins the present boundary (a `claimed` team binding returns mask 0 from
`peer_book_by_id`) precisely so S4a's change shows up as a failing assertion rather than a silent one.
★ **CLOSED 2026-08-02 by slice `§id-hash S4a` (green, UNCOMMITTED)** — and the planted assertion did exactly its job:
three test cases went red on the first build and were rewritten to the new contract, so the change is a visible diff.
**BOTH arms moved together**, as this entry required. ⚠ **What did NOT move, and the distinction is the trust model:**
`key_hash_of_id` / `team_key_of_id` / `team_id_of_key` keep their `authoritative` DEFAULT, so DST_HASH stamping,
sealing and `team grantkey` still refuse a claim (spec §3-D6/D7). Only the display + pubkey-inspection resolver was
lowered, and `actual` (prepared by S3) means the row renders `(claimed)` rather than `(auth)`.
⚠ **THE JSON BOOK IS UNAFFECTED, verified at source rather than assumed:** the app's rows are built by
`peer_book_join_ids`, which resolves through `id_bind_find_by_hash` / `team_id_of_key_freshest` — **neither takes a
floor** — so lowering `peer_book_by_id` cannot leak an unlabelled claim to the companion. **B52's scope is unchanged.**
**MEASURED** (corpus): reverting both arms to `authoritative` under the full S4a tree moves **0/36** — `peer_book_by_id`
has no simulator-reachable caller on the by-id path (`NodeRuntimeWrapper.cpp` parses only `reqpubkey <hex>`), so this
is native-gated by construction. Positive control in the same file: poisoning the statement the by-id branch feeds
(`answer_hash`) moves **10/36** with **37 assertion failures**.

### B54 — the FIRST claim into a FULL first-hand `_team_keys` still evicts one beacon row · NEW 2026-08-02
Spec §3-D5c requires eviction to *"prefer a claimed victim over any authoritative row"*, and `team_key_set` now does:
it drains the claimed cohort completely before it will consider a first-hand row. **Residual:** with all 16 slots
first-hand and no claim yet resident, the fallback is still oldest-wins, so the **first** claimed insert costs one
genuine beacon row. Every claim after it consumes only the previous claim.
**Bound:** exactly **one** row per storm, re-learned on that teammate's next beacon; and it needs 16 simultaneously
live teammates to be reachable at all.
★ **NOT WIDENED IN S3 (C2 cuts both ways here):** refusing the insert outright is a *stricter* policy than the spec
asked for, and choosing it belongs to the slice that actually creates claimed writes (S4a) or to the owner — not to a
slice whose contract is inertness. Recorded with its number so the decision is made, not inherited.
**MEASURED** (native): both halves are asserted — the fallback eviction (id 1 is displaced by the first claim) and
the cohort rule (a 16-frame storm afterwards leaves every first-hand row intact).
★ **STILL OPEN, AND S4a — THE SLICE THIS DECISION WAS DEFERRED TO — DELIBERATELY DID NOT WIDEN IT (2026-08-02).**
Reasoning, so the decision is made rather than inherited again: refusing the first claimed insert outright would make
the by-id answer **the operator explicitly asked for** the one write that silently does nothing, which trades a
one-row cost for a silent failure — the worse of the two (C2 cuts toward *reporting*, and there is nothing to report
here). Cost stands at **exactly one** first-hand row per storm, re-learned on that teammate's next beacon, and it
needs **16 simultaneously-live teammates** to be reachable at all.
**MEASURED** (corpus, S4a): **unreachable today** — the largest team scenario inserts 12 `_team_keys` rows against a
16-slot table, so no eviction of any kind occurs in the 36-scenario corpus. Native keeps both halves pinned, and S4a
added a third case driving the residual through the real ingest path rather than the direct setter.
⇒ **owner call if it should become a refusal; it is not a coder's to widen.**

### B55 — `reqpubkey_sent.hash == 0` is a NEW app-visible meaning that the companion contract does not describe · OPEN, MUTATED BY S4b 2026-08-02
`§id-hash S4a` gave a by-id `reqpubkey` a **two-stage** shape: when the id has no binding, the frame that flies is the
**id→hash** query, not the pubkey request. The BLE event still fires (the TX path did accept a frame — the 2026-08-01
owner ruling's meaning), but it carries **`"hash":0`**, and that value is the only thing distinguishing stage 1 from
stage 2. An app that treats `reqpubkey_sent` as *"a pubkey is coming"* will now wait for one that is not.
★ **The firmware side is built and documented in `lib/console/console_json.h` beside `write_reqpubkey_sent`.** What is
owed is the contract paragraph — plus, ideally, the app behaviour: on `hash == 0`, re-issue `reqpubkey <id>` once the
binding lands (or simply wait for **S4b**, which does the second stage on-node and removes the case entirely).
ⓘ **Two smaller contract deltas ride with it:** (a) `err_ambiguous_plane` now has a SECOND cause — an *unresolved* id
on a node that lives on both planes, where the by-id query itself must pick one (before, it meant only "both planes
hold this number"); (b) `err_no_binding` on a bare/`-t` id is now reachable from exactly ONE place, an explicit `-t`
on a node with `team_id == 0`.
ⓘ **OWED, NOT WRITTEN: `ios-companion/INBOX_SYNC_CONTRACT.md` is QA-owned and a coder never edits it.** Stacked with
**B52**'s owed `team_auth` line — one documentation pass covers both.
**MEASURED** (native): the stage-1 result is pinned (`code == queued`, `accepted`, `dst_hash == 0`, `plane == 1`) with
a same-fixture control proving the resolved case still carries the real hash.

★★★ **UPDATED BY `§id-hash S4b` (2026-08-02) — AND THE DISPATCH'S EXPECTATION THAT THIS ENTRY WOULD CLOSE IS WRONG,
REPORTED RATHER THAN QUIETLY TICKED.** S4b makes the node perform stage 2 itself, so the brief predicted the
`hash == 0` case would "disappear for the normal path". **It does not, and it must not.** The value is the *honest
report of a real stage-1 acceptance*: the frame the TX path took **was** the id→hash query, and the hash is precisely
what that frame went to ask for — a synchronous `CmdResult` cannot carry a value that does not exist yet. Removing the
case would require either suppressing a true event (re-creating the silence S1b was built to remove) or inventing a
hash. This is the same wall the `aired`→`accepted` rename hit, one level up: **an acknowledgement may only claim what
it can know.**
⇒ **WHAT ACTUALLY CHANGED IS THE INSTRUCTION, WITH THE BYTES UNMOVED** — and that makes the owed contract text a
CORRECTION, not an addition:
· **was** (S4a): `hash == 0` ⇒ *"expect no pubkey; re-issue `reqpubkey <id>` once the binding lands."*
· **is** (S4b): `hash == 0` ⇒ *"do NOT re-issue — the node consumes the answer and emits the pubkey request itself."*
⚠ **LIVE HAZARD FOR AN ALREADY-WRITTEN APP:** a companion coded against the S4a wording now fires a redundant second
`reqpubkey` while the node is already escalating — duplicate airtime for one question. Harmless (dedup + the intent
refresh absorb it) but wasteful, and it is exactly the kind of drift that made this entry necessary.
★ **The two continuations the app CAN rely on:** `peer_key_cached` (the whole workflow completed — an existing push,
no contract change) or nothing, bounded by S4b's timeout. **A stage-2 FAILURE is not app-visible at all — that is
B56.**
ⓘ **STILL OWED, STILL NOT WRITTEN: `ios-companion/INBOX_SYNC_CONTRACT.md` is QA-owned.** Stacked with **B52**'s
`team_auth` line and now **B56**'s decision — one documentation pass covers all three.
**MEASURED** (native, S4b): the stage-1 ack is re-pinned unchanged (`queued` / `accepted` / `dst_hash == 0`), with a
same-fixture control proving the *second stage* then flies by hash without a second command.

### B56 — a STAGE-2 `reqpubkey` failure never reaches the app · NEW 2026-08-02
`§id-hash S4b` completes the by-id workflow on-node, so the pubkey request is emitted from an **RX callback** — with
no command in scope. `reqpubkey_sent` is written only on the SYNCHRONOUS BLE command path (`src/fw_main.cpp`), so a
stage-2 refusal (`degenerate` — the answer named our own hash; `tx_dropped` — a bounded TX queue) and the bounded
**timeout** both reach the operator console (`!!`-prefixed `_hal.log`, prints under `debug off`) and telemetry, and
**not the companion**. The app sees the stage-1 `reqpubkey_sent{hash:0}` and then either `peer_key_cached` (success)
or silence.
★ **NOT A REGRESSION — a gap S4b makes reachable and does not widen.** Before S4b the app was told to re-issue by
hand, so the silence was covered by the operator's second command; now the second command is gone, so the silence is
the whole failure report.
**THE FIX IS AN APP-CONTRACT DECISION, WHICH IS WHY A CODER DID NOT TAKE IT:** it needs a new `PushKind` (appended, per
`command.h`'s enum rule) — plus its name in the sim's `ConsoleNames.cpp`/`NodeRuntimeWrapper.cpp` bridge, i.e. a
SECOND repo. ⚠ **The two obvious reuses are both wrong and are recorded so they are not re-proposed:** `send_failed`
would render a *failed message* for a command that sent none, and `hash_resolved` keys on a hash we never learned.
**MEASURED** (native): the console/telemetry half is asserted both ways — the `!!` line and its `MR_EMIT` fire on a
degenerate stage 2 and on the timeout, with a same-fixture control proving neither fires on the success path.

### B57 — a beacon-resolved id does not complete a pending `reqpubkey` · PARKED / DELIBERATE 2026-08-02
`§id-hash S4b`'s intent is consumed only in `on_hash_bind_response` (spec §5 step 3 names the *answer*). If the id→hash
binding instead arrives from a heard **beacon** — `node_beacon.cpp`'s `_id_bind` / `_team_keys` writers — the intent
sits until its bounded timeout, and the operator must re-issue (which then resolves immediately from the
beacon-learned row).
★ **DELIBERATE, WITH THE COST PRICED:** the hook would sit on the hottest corpus path in the tree, re-anchoring all 36
streams for an ergonomic gain on the case the by-id query exists precisely because it does **not** cover — an id we
route to but have never heard, i.e. one no beacon is arriving for. `drain_resolved_parked_sends` is the precedent for
the beacon-triggered shape if this is ever wanted; it would be its own slice (C1) and its own re-anchor (C4).
**MEASURED** (native): the answer-driven consume is pinned; the beacon path is asserted only as *not* consuming, via
the timeout test's late-answer arm.

### B58 — the intent ring and the LBT defer ring cannot both be exercised by one command · NEW 2026-08-02 · NOT A DEFECT, A TEST-DESIGN CONSTRAINT
`§id-hash S4b` refuses a full intent ring (`err_resolve_pending_full`) **before** `emit_hash_query`, which is the
correct order (D9: never spend airtime on a decision that is going to be refused). A consequence: with
`cap_pending_id_pubkey == 4` and the shared LBT defer ring also 4 slots, four by-id commands fill the intent ring
first, so **a fifth by-id command can never reach the TX-rejection path**. The LBT-drop fixture therefore has to fill
the defer ring through the **by-hash** door.
★ Registered because it is invisible from the source and cost a test iteration to find: a future slice that changes
either capacity, or the refusal order, silently changes which fixtures can reach which failure. **If the two caps ever
need to be exercised together, the honest lever is `cap_pending_id_pubkey`, not the refusal order.**
**MEASURED** (native): the by-hash-filled fixture reaches `err_tx_queue_full` and proves the intent is unwound; the
by-id-filled attempt returns `err_resolve_pending_full` instead, which is how the constraint was found.

### B59 — a relay can ACK custody, start route repair, then discard the transit DATA before that repair can help · NEW 2026-08-03 · PARKED — POTENTIAL ROUTING-ALGORITHM CHANGE
**METAL-CONFIRMED** on the static four-node topology `42 — 186 — 109 — 48`, with weak/asymmetric links. Node 48's
by-hash `reqpubkey 0x8CC9BDFF` reached owner 42. Owner 42 generated authoritative pubkey answer DATA `ctr=3598` and
sent it to relay 186. Relay 186 received and cached the answer, ACKed 42, then exhausted RTS attempts on its selected
direct next hop 48. It emitted an RREQ for 48, performed the one-way slow reprobe, and terminally reported
`FAILED ctr=3598 (no CTS — next hop silent)`. On the same node, the valid RREP arrived immediately *after* that
failure and installed `186 → 42 → 109 → 48`; requester 48 never received `ctr=3598`.

The failure is the interaction between two existing routing policies, not a broken hash-answer encoder. In
`cascade_to_alt`, §P3 can emit asynchronous route discovery when the exhausted next hop is silent, but the MF4
`LinkBidi::one_way` arm then bypasses `try_cascade_requeue`: one final probe is allowed, and a failure inside the
reprobe throttle window calls `giveup_flight`. The upstream ACK has already transferred custody, so neither owner 42
nor requester 48 receives the relay's terminal outcome.

★ **CONTROL, SAME METAL TOPOLOGY:** after the RREP had installed the repaired routes and the H-query dedup window had
expired, node 48 repeated the command. Owner 42's new answer DATA `ctr=3602` selected `42 → 109 → 48`, survived ordinary
RTS/DATA retries and was ACKed at node 48, which stored `0x8CC9BDFF` as authoritative (`nv=inserted`). This proves the
first frame was not merely delayed: route repair benefited only the later transmission.

★ **A HOLD-AND-RETRY PATCH IS NOT SUFFICIENT FOR THIS EXACT CASE.** At relay 186 the repaired route begins with node
42, which is the transit frame's `previous_hop`; `next_hop_selectable` deliberately rejects it to prevent loop-back.
Removing that guard is not an acceptable local fix. Retaining the frame while RREQ is outstanding would help only
when discovery returns a selectable non-previous-hop route. Rescuing this topology may require explicit custody
return/NACK semantics, a narrowly proven route-repair turnaround rule, or origin-level retry — all are routing
algorithm/protocol decisions with possible loop, duplicate, airtime and corpus-wide consequences.

★★ **OWNER RULING 2026-08-03: REGISTER, DO NOT DISPATCH.** The behavior is real and potentially affects any forwarded
DATA that reaches this combined silent + one-way + no-selectable-alt state, but no solution is approved and there is
currently no requirement to address it. Do not implement a one-line relaxation, remove the previous-hop guard, or
extend retries under B59 without a separate routing design and adversarial loop/duplicate/airtime evaluation.

As of 2026-08-04 - new idea - node is sending back to sender special DM - NACK (similar to e2e ack)

### B60 — multi-gateway `send_layer` resolves the FINAL hash on the first intermediate layer and never selects the next gateway · NEW 2026-08-03 · OPEN — SPEC READY; ACK POLICY OPEN
**METAL-CONFIRMED** on the explicit route `7 → 5 → 6`. A registered mobile on leaf 7 issued
`send_layer 0x7B18ADA2 5,6 "Test multi layer 1"` for a mobile hosted on leaf 6. The mobile correctly sent a
`DATA_TYPE_MOBILE_SEND` wrapper to home 177; the home correctly re-originated the preserved path `[7,5,6]` toward
the gateway bridging layers 7/5 (local node IDs 7/8). After entering leaf 5, however, that gateway repeatedly emitted
`H leaf=5 hash=7B18ADA2`. The destination and its home were on leaf 6, so no valid leaf-5 answer existed and the
message never reached gateway 5/6.

The path and cursor are not lost. `originate_layer_path` prepends the source layer correctly, and
`bridge_cross_layer` advances `cur` when another entry remains. The defect is the handoff's destination decision:
`bridge_cross_layer` always calls `id_on_leaf_by_hash` / `mobile_home_on_leaf` for the final `dst_hash` on the layer
being entered, even when `cur + 1 < n_layers`. The unresolved handoff then reaches
`drain_xl_handoffs_for_leaf`, where `dst_node_id == 0` has only one meaning — *resolve the final recipient* — so it
H-floods that hash on the intermediate layer. No branch reads the advanced cursor and calls
`select_gateway_for_leaf` for the next path entry.

**Expected intermediate behavior:** on leaf 5, preserve the final hash but MAC-address the relay leg to the gateway
that serves leaf 6. Only the final gateway, after entering leaf 6, may resolve/H-flood `0x7B18ADA2` and route it to
the destination or its mobile home. A final-hash binding accidentally present on leaf 5 must not alter that decision.

★★ **WHY TESTS WERE GREEN:** the native two-hop explicit-path test asserts only that origination encodes
`[source,h0,h1]` with `cur=1` and sends it to the first gateway. It never drives a handoff through two gateways.
The source comment says multi-gateway transit “just works,” while the bridge comment still calls it “reserved”; the
original gateway design explicitly left it outside v1. This is a coverage and documentation contradiction, not a
radio-loss diagnosis.

★★ **OWNER REQUIREMENT 2026-08-03:** the repair must support an explicit route of up to **16 total layer entries,
including the source** — therefore at most **15 user-supplied `send_layer` hops**. The current independent cap is four
total entries (`gw_env_max_hops=4`), and `CmdResult::layer_path` can echo only four bytes, so widening the codec array
alone is not a complete implementation. The parser, command/result carriers, mobile wrapper/home re-origination,
cursor bridge, reversed E2E ACK policy, memory gates and tests must move together. Gateway selection and the wire leaf
gate use `layer_id & 0x0F` in the current layer's local context, so **non-adjacent repeated nibbles are valid**. Only
an adjacent same-nibble transition is structurally impossible: `validate_gateway_layers` already refuses a gateway
whose own two leaves alias. The old `gw_env_max_hops` symbol must be deleted so the compiler exposes every four-entry
assumption rather than silently widening missed validation guards.

**OPEN OWNER DECISION B60-O1:** long-path `-a` admission/deadline policy. Linear scaling reaches 75 minutes and can
occupy all eight ACK slots; keeping 300 seconds can false-timeout. The design recommends bounded ACK depth until
resources are redesigned.

**Implementation design:** `docs/superpowers/specs/2026-08-03-multi-gateway-explicit-layer-path-routing-design.md`.
This is **not B59** and must not relax ordinary route loop guards, custody, cascade selection or previous-hop
rejection. The next gateway is supplied by the explicit layer path and selected with the existing per-leaf gateway
directory; ordinary routing is reused only to reach that selected gateway within the active leaf.

### B61 — `board_name()` silently reports `"native"` when a board env forgets its `-DBOARD_*` macro · NEW 2026-08-03 · ✅ FIXED 2026-08-03 (own commit, UNCOMMITTED)
**MEASURED** during the §board-split Phase-0 assessment (`simulation/BASELINE.md`, 2026-08-03 note). `src/firmware_commands.cpp:339-349`
is the **only** genuinely board-discriminating code in the firmware — 3 of the 26 board-macro conditional sites, all in this one
`#if/#elif/#elif` chain — and its `#else` arm returns `"native"`:

```c
const char* board_name() {
#if defined(BOARD_XIAO_WIO_SX1262)   return "xiao_nrf52";
#elif defined(BOARD_XIAO_ESP32S3)    return "xiao_esp32s3";
#elif defined(BOARD_HELTEC_V3)       return "heltec_v3";
#else                                return "native";
#endif
}
```

⚠ **That `#else` is unreachable in all eleven envs** — `firmware_commands.cpp` is excluded from `[env:native]`'s
`build_src_filter` (`platformio.ini:73`) and `test_build_src = no`, so every env that compiles it defines exactly one
`BOARD_*`. ⇒ it is not a host arm; it is a **silent fallback**, and per **C2** it should be `#error`.

**Why it matters now, not academically:** the whole point of the board-source-split work is that *adding a board becomes
adding a directory*. A new board env that omits its `-DBOARD_*` **still links and still boots**, and reports `board=native`
in the `version` banner and in `print_banner` (`:352-358`) — a provenance lie in exactly the diagnostic a bench operator
trusts to tell two boards apart. **Fix:** replace the `#else` with `#error "no BOARD_* defined — add one to this env"`.
Cost is one line; it converts a mislabelled image into a build failure. **Flash/RAM impact nil** (the arm is never compiled).

**✅ FIX LANDED 2026-08-03 (own commit, uncommitted).** `#else return "native"` → `#elif defined(MESHROUTE_NATIVE) return
"native"` + `#else #error "No supported BOARD_* or MESHROUTE_NATIVE target selected"`, plus an 8-line comment recording why
the old arm was dead rather than host-facing. **Precondition re-verified independently, not taken on trust:** all four
target macros are declared exactly once (`platformio.ini:79 / :134 / :218 / :263`); all **seven** extending envs re-list
`${env:<board>.build_flags}` (`:324 :331 :338 :357 :369 :375 :381`) despite `:307`'s warning that `extends` does not
auto-merge them; the simulator never compiles `src/`; and there is **no `__LINE__` and no `assert(` anywhere in
`firmware_commands.cpp`**, so the +12-line shift cannot perturb emitted code.

★ **GATE — 11/11 envs rc=0** (object counts 23 / 194 / 284, none zero). Native **1149 cases / 72681 assertions / 0
failed** from the RUNNER's stdout; s18 keystone **`1cd21235`/271629 EXACT**; **36/36 corpus scenarios byte-identical to
the pre-fix run and 0 assertion failures in every one** (s18 is inert by construction — the sim compiles `lib/`, not
`src/`). **Symbol multisets identical on all eleven**, warning counts identical on all eleven, `-Wswitch` **0 ×11**.

⚠⚠ **I PREDICTED "byte-identical on every board env" AND THAT PREDICTION WAS WRONG — recorded because the correction is
the useful part.** Against the pre-slice eleven-env baseline, 8 of 11 envs showed one moving section: the four ESP32 envs
`.debug_line` **+3 B** (DWARF, **not flash-bearing**, pio RAM/Flash unchanged), and `xiao_sx1262` `.text` **−32 B** /
`gateway` `.text` **+32 B** — opposite signs, with symbol multisets identical.

★ **The ±32 B is NOT this change, and that is measured rather than argued.** Reverting `board_name()` to its exact
pre-fix form and rebuilding still yields `.text` **511044** — the *same* value as the fixed build, i.e. **−32 vs the
original baseline even with the fix removed**. ⇒ the offset is a build-environment artefact that survives full reversion.
Two builds of identical source *within one session* reproduce exactly (`.text`, flash 512020, symbols — **0 differing
sections**), so it is not per-build noise either; it is a **one-time, content-dependent step** consistent with
`__DATE__`/`__TIME__` literal packing in the banner strings (`firmware_commands.cpp:354` / `fw_main.cpp:427`), which on
this ld script land in `.text`. **That is exactly the "±32 B `__DATE__`/`__TIME__` flash floor" the QA brief named — now
measured on ARM, where it is invisible in the symbol multiset.** `gateway`'s +32 is the same artefact class by analogy
(it is `extends env:xiao_sx1262`, identical symbol multiset); **verified directly only on `xiao_sx1262`.**

★★ **THE GATE THAT ACTUALLY DECIDES B61 — pre-fix source vs post-fix source, both built in the SAME session:**
**0 differing sections · symbol multiset identical · RAM 167044 identical · Flash 512020 identical · warnings identical ·
`-Wswitch` 0.** ⇒ the fix is provably **code-neutral**, as the dead-arm reasoning predicted.

⇒ ★ **METHOD CONSEQUENCE for the next slice: an eleven-env baseline is only a valid comparand for builds made in the
same session.** A0 must rebuild its own BEFORE arm rather than diff against the 2026-08-03 grid, or ±32 B of banner-string
packing will be misattributed to the file move — on top of the ~192 B the move genuinely costs.

★★ **POSITIVE CONTROL — the `#error` was PROVEN to fire, because 11/11 green is otherwise a 0/11 that cannot be read.**
Rebuilding `heltec_v3` with `PLATFORMIO_BUILD_UNFLAGS` dropping `-DBOARD_HELTEC_V3` fails at **`src/firmware_commands.cpp:357`**
with exactly `error: #error "No supported BOARD_* or MESHROUTE_NATIVE target selected"`, and `-Werror=return-type` fires
behind it as a second independent guard. ⇒ the guard is live, not decorative, and the green grid means "every env
satisfies an arm", not "the instrument cannot fire".

ⓘ **Honest residue:** the new `MESHROUTE_NATIVE` arm is itself still **unreachable in all eleven envs**, because
`[env:native]` does not compile this TU. It exists so a future host build that *does* compile it has a legitimate answer
instead of tripping the `#error` — not because anything reaches it today.

### B62 — three in-source paths still say `src/board_ui.cpp` after §A0 moved it · NEW 2026-08-03 · OPEN (one-line fix, deliberately deferred)
**MEASURED / created by §A0** (`simulation/BASELINE.md`, 2026-08-03 §A0 note). The file now lives at
`variants/heltec_v3/board_ui.cpp`, but three places still name the old path:

| site | text |
|---|---|
| `variants/heltec_v3/board_ui.cpp:1` | `// MeshRoute — src/board_ui.cpp` — **the file's own path header lies about itself** |
| `lib/hal/mr_ui.h:5` | *"implement these three hooks in a TU compiled under `#if MR_FEAT_OLED` (src/board_ui.cpp)"* |
| ✅ plan File-Structure table + Tasks 5/9 | **ALREADY CORRECTED 2026-08-03 by QA** — this row was stale when written; the table now reads `variants/heltec_v3/board_ui.{cpp,h}` and the `-I variants/heltec_v3` was moved to **Task 5**, where it first becomes load-bearing |
| ✅ `lib/hal/mr_ui.h:5` | **FIXED 2026-08-03** — now names the new path and states the port is per-BOARD (V4 brings its own) |
| ✅ plan architecture paragraph (`:7`) | **FIXED 2026-08-03** — flags that only the board-INDEPENDENT units remain in `src/` |
| ⏳ `variants/heltec_v3/board_ui.cpp:1` | `// MeshRoute — src/board_ui.cpp` — **the file's own path header lies about itself.** ★★ **DELIBERATELY STILL OPEN, and the reason is a real conflict with the QA recommendation to "clean B62 before the owner commit": editing this file makes A0 STOP BEING A 100 % RENAME**, which is the property the A0 approval was verified against (`R100`, 0 insertions, 0 deletions). ⇒ **fix it in the FIRST commit AFTER A0 lands** — the OLED slice touches this file anyway (Task 9)

⚠ **Not an oversight — A0's dispatch scoped it out** (*"⛔ Nothing else moves. No content edit"*, C1: never fold a
semantic/textual edit into a file move). `platformio.ini:221`'s comment **was** fixed, because it sits inside the file A0
rewires. **Fix:** the Phase-A OLED slice rewrites `board_ui.cpp` in full — correct line 1 and `mr_ui.h:5` there, and
update the plan's file table when Task 5 lands `board_ui.h` in `variants/heltec_v3/` (which is also when the `-I
variants/heltec_v3` the A0 brief asked for genuinely becomes necessary — see the §A0 note's premise 2). Zero build
impact; a stale path header in a board-port file is exactly what the "code is read, docs rot" rule is about.

### B63 — RECORDED (gate methodology, not a defect): on xtensa, adding or removing a **zero-byte** object from the link set moves `.flash.text` by up to +176 B · NEW 2026-08-03
**MEASURED and decoupled by probe** in the §A0 note. `src/board_ui.cpp` compiled to an object with **`.text` 0 /
`.data` 0 / `.bss` 0** on every non-Heltec env (`MR_FEAT_OLED` defaults 0). Dropping that empty object from the three
`xiao_esp32s3`-family link lines nevertheless moved `.dram0.bss` **−8**, `.flash.rodata` **−8** and `.flash.text`
**+176 / +56 / +132**, and resized ~30 **Arduino/ESP-IDF framework** functions (`WiFiGenericClass::mode`,
`STAClass::connect`, `APClass::create`, `NetworkClient::write`, …) — **no symbol of ours affected**. Putting the same
file back into the link set *from its new directory* reproduced the pre-move image **section-for-section and
symbol-for-symbol** (P-A0-2) ⇒ **the source's directory is irrelevant; link-set membership is the entire effect**, and
it is xtensa link-order relaxation. **The identical removal is free on ARM** (4 nRF52 envs: 0 differing sections,
identical symbols, identical RAM and Flash).

⇒ ★ **Consequence for every future size gate:** *"an object entered or left the link set"* is an xtensa-only **±200 B
flash / ∓8 B RAM** event **even when the object is empty**, and it is visible in the symbol multiset only as framework
functions changing size. Do not attribute it to the slice's own code, and do not expect the ARM envs to corroborate it.
⚠ It also **retires the "+192 B is the price of moving a file out of `src/`" rule** recorded on 2026-08-03: that number
came from relocating `device_ota.cpp` (123 lines, a 227 KB object), i.e. from reordering **non-empty** objects.

### B64 — a team roster that SHRINKS between ticks makes the compose modal retarget its DM · NEW 2026-08-03 · OPEN (behaviour choice, Task 6 or a plan ruling)
**MEASURED / created by §UI-2** (`simulation/BASELINE.md`, 2026-08-03 §UI-1+UI-2 note). `UiModel::activate()`
(`src/firmware_ui_model.h`) reads `s.team[_st.cursor % s.team_shown]`. The modulo is load-bearing — it is what keeps the
read in range when a later snapshot carries fewer rows than the cursor the previous tick left behind — but its **effect**
is that a cursor on row 2 meeting a 1-row roster opens the DM modal bound to **row 0**, i.e. `"Are you OK?"` goes to a
teammate the user did not highlight. Pinned (not fixed) by the test *"a roster that shrinks between ticks cannot index
out of range"*, so the behaviour is visible in the suite rather than latent.

⇒ **Not fixed here because every fix is a behaviour decision the plan does not make** (C1): clamping to `shown - 1`
retargets to the *last* row instead of the first (equally arbitrary); the sound cures are **re-anchoring the cursor when
the snapshot's roster changes** (Task 6, which builds the snapshot) or **refusing the activation** and repainting.
Severity is low — the canned DM texts are benign and `-a` reports where it actually went — but it is a mis-addressed
message, so it wants a ruling rather than silence.

### ~~B64~~ ✅ **CLOSED 2026-08-05** — OWNER-RULED, and it was a stated PREREQUISITE that Task 7 skipped
★★ **THE RULING, VERBATIM:** *"Preserve selection by teammate IDENTITY across roster refreshes. The cursor tracks the
teammate, not the row index. If that teammate has disappeared from the roster, REFUSE activation and repaint — never
silently select another row."*
⚠ **AND THE PROCESS FINDING BESIDE IT:** plan `:135` deferred B64 to Tasks 6/7 with *"that is a MIS-SEND, not a display
glitch, so it needs a ruling before Task 7 wires real sends"* — and **Task 7 wired real sends without resolving it**,
which is why independent QA returned it as a blocker rather than a backlog item.

**What landed** (`src/firmware_ui_model.h`, `src/firmware_ui.cpp`):
- `UiModel::_team_sel_id` + `_team_sel_valid` hold the selection **by team-plane id**. ★ **DERIVED, not invented (U1):**
  that id is already in every `TeamRow`, is already what `compose_peer` stores, and is already what
  `ui_compose_send_line` puts on the wire (`send <id> "<text>" -t -a`) — so the thing tracked and the thing addressed
  are the SAME value and cannot drift. **No snapshot field was added.** ⛔ NOT the row's `label`: that is a display
  string (name / `0x<hash>` / bare id), and a display-shaped field must never make an addressing decision (the B48 rule).
- `sync_team_cursor(s)` re-finds that teammate in EVERY snapshot — called from `on_gesture` (before the gesture acts)
  **and from `on_tick`**, because `FrameGate::step` freezes immediately after `on_tick`, so the highlight must already
  name the remembered teammate or the panel and the send would disagree. A reorder therefore moves the `>` WITH the
  teammate. ⚠ It is guarded on `compose == none`: while the sub-view is open `_st.cursor` is the MODAL's index.
- **GONE ⇒ REFUSE.** `activate()` requires `_team_sel_valid`; otherwise it queues nothing, sets `UiState::team_pick_gone`
  and marks the frame dirty. The renderer reserves one body row for **`TEAMMATE GONE, repick`** (21 chars = the panel
  width exactly) **and suppresses the `>` marker** — a highlight beside a target the model has already refused to use
  would be the same mis-send in display form.
- ⚠ **C3, plane discipline:** `_team_sel_id` is a TEAM-plane local id. It is only ever COMPARED against a snapshot row's
  `id` and copied into `compose_peer`; it indexes nothing, reaches no static `node_id`-keyed array, and adds no write
  path. On a `!MR_FEAT_TEAM` build `team_build` is false ⇒ `Screen::team` is unreachable and all of it is inert.
- ⓘ **No arithmetic value is reserved** (§B74's discipline): validity is its own flag, so id 0 — which
  `Node::team_local_id()` documents as "not team-DAD'd" — needs no special case.

**RED measured** (`simulation/BASELINE.md` §UI-7-FIX note): the shipped defect fully reverted = **5 cases / 15
assertions**; ★ **the tempting WRONG fix (CLAMP the index instead of tracking identity) = 3 / 8** — the discriminating
case is a same-size REORDER that puts the picked teammate on row 1, an index no clamp can produce (`cursor % 3` → 13,
`shown - 1` → 13, `0` → 11, identity → **12**); the `on_tick` resync removed = 2 / 4; silently re-selecting row 0 on a
vanish = 4 / 12; the vanish arm made level-triggered = 4 / 9; the compose guard dropped = 1 / 2.
⚠ **TWO OF THIS SLICE'S OWN CONTROLS WERE VACUOUS AND ONLY MUTATION CAUGHT THEM** — the compose-guard control went
**0 / 0** on its first writing, and a half-revert of the fix also went **0 / 0**. Both are recorded in the note.
**The old test was REWRITTEN, not deleted** (the §B101 precedent): *"a roster that shrinks between ticks cannot index
out of range"* used to assert the retarget-to-row-0 outcome as a documented consequence, and now asserts the refusal —
so the ruling is visible in the diff.

### B65 — the UI model can blank the panel on its FIRST tick, having drawn nothing · NEW 2026-08-03 · OPEN (one line, but a behaviour change)
**MEASURED / created by §UI-2.** `UiModel::on_tick` blanks when `elapsed(now_ms, _last_input_ms) >= kBlankMs`, and
`_last_input_ms` is written **only** by `on_gesture` — it is `0` from construction. So if the first tick arrives more
than `kBlankMs` (15 s) after `millis()` started — reachable on the NV **format-on-corrupt** and OTA-heavy boot paths —
the panel goes to `set_power_save(1)` on the **very first service pass**, before anything was ever drawn, and only a
button press recovers it. On a normal boot (UI init at ~1-3 s) the only effect is that the first blank window is short
by the boot time.

⇒ **Fix is one line** (seed `_last_input_ms` from the first `on_tick`/`on_gesture` snapshot), and it leaves all seven
plan-authored Task-2 cases green — verified by inspection, since they all tick at `1000` first. Not applied because it
changes documented behaviour and the plan's code is authoritative for this slice. **Owner/plan ruling wanted; Task 6 is
the natural home.**

### B66 — the compose modal's `back` row is identified POSITIONALLY, across a TU boundary · NEW 2026-08-03 · OPEN / DESIGN
**MEASURED / created by §UI-2.** `UiModel::compose_gesture` leaves the modal when `cursor + 1 == n`, where `n` is
`kDmTextCount` / `kChannelTextCount` in `src/firmware_ui_model.h`, while the **strings** those counts describe live in
`src/firmware_ui.cpp` (Tasks 6/7, not yet written). Spec §3.2.2 advertises that adding a canned text is "a one-line
change"; it is in fact **two places**, and updating the string table without the count turns `back without sending`
into a **send**. A ⚠ comment now marks the coupling at both constants.

⇒ **Durable cure:** one table with the count derived from it (`std::size`) — either the tables move into the model
header (they are plain string literals; the unit stays board- and core-free) or the counts move out and the model takes
the list length as a parameter. Either is a small Task-6/7 decision; it is registered so it is not rediscovered as a
field bug.

### B67 — RECORDED / PLAN DEFECT: the OLED plan's test blocks use `REQUIRE`, which cannot compile in this suite · NEW 2026-08-03
**MEASURED by probe** (`simulation/BASELINE.md`, 2026-08-03 §UI-1+UI-2 note). `docs/superpowers/plans/2026-07-31-onboard-oled-ui-phase-a.md`
calls `REQUIRE` at **:353** (Task 2), **:575 :584 :624 :631** (Task 3) and **:856 :874 :890 :892** (Task 4). The native
build is `-fno-exceptions` (`platformio.ini:48`) ⇒ doctest auto-defines `DOCTEST_CONFIG_NO_EXCEPTIONS`, and `REQUIRE`
becomes a **hard compile error**, not a weaker assert:

```
.pio/libdeps/native/doctest/doctest/doctest.h:2824: error: static assertion failed: Exceptions are disabled!
    Use DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS if you want to compile with exceptions disabled.
.pio/libdeps/native/doctest/doctest/doctest.h:2977: error: expression cannot be used as a function
```

⇒ **A coder implementing Tasks 3 or 4 literally will hit a build failure on the first run.** Substitute `CHECK` plus an
`if` guard wherever a later step depends on the assertion (the corpus already does exactly this: **30 of 30** test files
carry the "no `REQUIRE`" note and **zero** call it). UI-1/UI-2 already ship that way. ⓘ The plan's `#include <doctest.h>`
is **not** a defect — measured, both spellings resolve — but the repo idiom is the quoted form (U3). ⚠ The plan itself
was **not edited** (the dispatch forbade it); this entry is the record.

✅ **FIXED IN THE PLAN 2026-08-03 at all nine sites**, plus a Global Constraints rule so it cannot return.
⚠⚠ **But the fix introduced `B70`:** four of the rewritten guards call a **draining** API twice, which silently voids the
two most safety-critical emergency cases while reporting PASSED. **B67 and B70 must be read together.**

### B68 — `SendOutcome` has no case for "accepted, no local handle, unknowable synchronously" · NEW 2026-08-03 · OPEN / TYPE DESIGN (Tasks 3-4)
**Found while shaping UI-2 against the corrected `ctr` semantics** (`simulation/BASELINE.md`, 2026-08-03 §UI-1+UI-2
note; the semantics themselves are the plan's own 2026-08-03 Prerequisites correction). The plan defines `SendOutcome`
in **Task 3** (plan:677-688) with seven kinds — `channel_relayed · channel_no_relay · blocked · dm_acked · dm_no_key ·
dm_failed · dm_timeout`. None of them can express the **third producer of `ctr == 0`, which is a SUCCESS**: on a
registered mobile a plain/`-g` GLOBAL channel post flies as a real MOBILE_SEND DM while the **home** mints the channel
ctr, so no local handle exists (`lib/core/node.cpp:1565-1573`).

⇒ Task 4's tracker would have to either classify that success as `blocked`/`dm_failed` — the emergency machine then
consumes alarms for a message that was delivered — or drop it, leaving a permanent `SENDING...`. **Task 3 needs an
eighth kind** (e.g. `channel_unattributable`) with an explicit emergency-side policy. ⚠ **Not a live safety hole:** the
alarm path posts on `MR_UI_TEAM_CHANNEL_ID` on the **team** plane, where this producer is unreachable. It is a
correctness-of-type finding, raised now precisely so Task 4 does not have to rework the model's contract.

✅ **FIXED IN THE PLAN 2026-08-03** — the kind is `channel_remote_mint` and the `ctr` semantics are inline at
plan:707-719. **Landed in `src/firmware_ui_model.h` by UI-3.** ⚠ The residual is **B69**.

### B69 — `channel_remote_mint` must render as SENT, but no model state carries that distinction · NEW 2026-08-03 · OPEN (UI-6/UI-7)
**Created by B68's fix** (`simulation/BASELINE.md`, 2026-08-03 §UI-3 note). The eighth kind exists and `on_outcome`
handles it **explicitly**, sharing the `channel_no_relay` branch — correctly, because neither carries relay evidence, so
neither may claim `PICKED UP` and both leave the alarm unconfirmed ⇒ bounded retry. But the plan also rules *"render it
as **SENT**, never as PICKED UP"*, and **both kinds land in the same `Emergency` state** (`firing`, then `not_heard`), so
the model cannot tell a renderer which one happened.

⇒ Today the obligation rests entirely on **UI-6/UI-7's channel path**, which sees the `SendOutcome` directly and does
not consult the emergency machine — acceptable, because the kind is **unreachable on the team-plane alarm path**, but it
must be *known* rather than assumed. If a future slice wants the emergency screen to say `SENT`, the model needs either
a flag or a ninth `Emergency` state. Marked in-source at both the type and the branch, and pinned by the test
*"channel_remote_mint is handled explicitly and never claims PICKED UP"*.

### ~~B69~~ ✅ **CLOSED 2026-08-05 (UI-7)** — the carrier exists, and **the obligation itself was wrong**
★★ **B69 asked for a carrier so `channel_remote_mint` could render as SENT. It has one. It must not say SENT.**
The carrier is two model states, one per surface, because the two transactions are independent by design (§2.1's
two trackers) and a shared "last channel outcome" field would let an alarm's verdict overwrite a canned post's:
- **the canned sub-view** — `mrui::ChanState` (`src/firmware_ui_model.h`), written by the NEW canned-only entry point
  `UiModel::on_channel_outcome`. `channel_no_relay` -> `no_relay`, `channel_remote_mint` -> `unconfirmed`. ⓘ That
  entry point is separately owed: `ui_pump_trackers` previously had to CONSUME the normal tracker's expiry and throw
  it away, with `⛔ Do not "fix" this by calling on_outcome` beside it, precisely because no canned-only entry existed.
- **the emergency overlay** — `mrui::EmgEvidence`, sticky and monotone (`local_tx` > `no_handle` > `none`), reset by a
  new alarm beside `_tries`. `NOT HEARD`'s detail line was *"no relay after N"*, which asserts a **measurement**; an
  alarm whose attempts all came back `ctr == 0` never held a handle and never listened, so it never took it.

★★★ **THE PREMISE CORRECTION, MEASURED IN SOURCE — and it inverts the ruling.** B69, spec §2.1 rule 2 and
`firmware_ui_send.h`'s §B68 block all justify "render as SENT" with **B39's producer (3)**: a registered mobile's
DELEGATED GLOBAL post, where the HOME mints the channel ctr and a real `MOBILE_SEND` DM flies — a genuine success.
⛔ **That producer is STRUCTURALLY DEAD on the line this UI sends.** `node.cpp:1401` computes
`want_global = c.u.channel.global || !c.u.channel.team`; every UI channel post carries `-t` and no `-g`, so
`want_global == false` and the `do_send_channel_delegated` branch (`node.cpp:1591-1601`) is never entered. On `-t -e`
exactly **two** producers of `queued`/`ctr == 0` survive, and **neither is a success**: a pre-TX self-gate
(`node_channel.cpp:650`, which also pushes `send_blocked`) and a post-mint **SEAL FAILURE** (`node_channel.cpp:744`).
The first normally resolves through `match_blocked` inside the window; the second is what reaches expiry.
⇒ **rendering it SENT would be the §2.1 false confirmation the obligation was written to prevent.** The panel says
**`NOT CONFIRMED` / `no send handle`** (canned) and **`NOT HEARD` + `unconfirmed xN`** (alarm) — never SENT, never
"no relay". ⓘ The `SendOutcome` kind stays a SUCCESS SHAPE inside the tracker: §B68's argument is untouched, and the
tracker is generic, so a future plain/`-g` UI post would legitimately revive producer (3). Only the RENDER changed.
⛔ Zero `lib/` edits; no `wire_version` bump; nothing on the wire was missing — the distinction was already carried by
`ctr` and only the UI lacked a state for it. **The plan and spec were NOT edited; this is the report.**
**RED measured:** `M5` (collapse `remote_mint` back onto `no_relay`) = **2 cases / 3 assertions**; `M7` (a new alarm
inherits the old evidence) = **1 / 1**; `M6` (write the evidence before the live-alarm guard) = **1 / 3**.
⚠ **M6's control was VACUOUS on its first writing and only the mutation caught it** — see the `simulation/BASELINE.md`
§UI-7 note. Same class as §R1's one slice earlier.

### B111 — a DM that comes back `queued` with `ctr == 0` has no outcome kind, so its sub-view answers nothing · NEW 2026-08-05 · OPEN
**MEASURED in `lib/core`, and it disproves a claim `src/firmware_ui_send.h` has carried since UI-4.** That file states
a DM reaches `awaiting` *"only via a HASH-addressed send parked behind an H resolve … which the UI never issues"*.
`enqueue_data` (`lib/core/node_mac.cpp:56-61`) also returns 0 for `app_dm && !leaf_config_synced()` — an
un-synced managed joiner (`lineage_id != 0 && config_epoch == 0`), which is a real state for a team mobile between
`join` and its first config sync. So `send <id> "…" -t -a` **can** answer `queued`/`ctr == 0`.
⇒ `SendTracker::tick()` correctly refuses to invent an outcome (a `channel_remote_mint` for a DM would be a type
error) and releases the slot, so nothing is leaked and nothing is falsely claimed — but `_dm` is left on
`DmState::submitting` and the panel shows `SENDING...` until the sub-view's own `kBlankMs` auto-exit closes it.
⇒ **The cure is a DM-side "no local handle" outcome**, i.e. a tenth `SendOutcome::Kind` plus its `DmState`. **NOT taken
here (C1):** UI-7 is already a feature slice, the harm is bounded and non-lying, and the naming ("unconfirmed" vs
"not delivered") is a display ruling. Marked in-source at `firmware_ui_send.h`'s NOT-here list and in
`firmware_ui.cpp`'s DONE/MISSING block.

### B112 — on the DM arm, a NON-ZERO `ctr` does not mean the frame was enqueued · NEW 2026-08-05 · OPEN / `lib/core`
**MEASURED.** Spec §2.1 rule 2 and the tracker are built on *"`ctr != 0` ⇒ this node originated the post and owns that
handle — exact correlation is valid"*. That holds on the `send_channel` arm. On the id-addressed **DM** arm it does
not: `enqueue_data` has **four** refusal sites that return the already-minted, non-zero ctr **without enqueueing**
(`lib/core/node_mac.cpp:204` `location_refused`/unsealed, `:209` `no_fix`, `:228` and `:269` seal refusals).
⇒ the UI calls `tr.accept(ctr)` and `on_send_accepted`, so the sub-view shows **`SENT, waiting`** for a DM that was
never handed to the radio, and the state resolves only when the e2e-ack deadline expires into `NO CONFIRM`.
⚠ It fails toward "we could not confirm", never toward a false DELIVERED — `send_e2e_acked` still requires a real ack —
so it is a *wrong intermediate*, not a false safety claim. ⛔ **A `lib/core` finding and NOT fixed here**: the sound cure
is for those four sites to return 0 (or push a synchronous failure the UI can correlate), which is a core slice with
its own s18 exposure. ⓘ The UI-side mitigation that already exists: every one of the four pushes a `send_failed` with
a real `SendFailReason`, and `match_dm` correlates it on ctr AND peer — so the panel does reach a named failure IF the
push arrives before the sub-view closes.

### B114 — ★★★ BENCH: THE TEAM HEARD THE DISTRESS CALL **AND REPLIED**, AND THE PANEL SAID `NOT HEARD` · NEW 2026-08-05 · **RE-SCOPED 2026-08-05 INTO THREE MATTERS: ① ✅ FIXED ([[B115]]) · ② ✅ CLOSED / OWNER-RULED, NO CHANGE · ③ OPEN ([[B116]])**
**MEASURED ON METAL by the owner (two synchronised logs), not inferred.** Sender = mobile **69** (Heltec V3), receiver =
team node **231**. This is the **inverse polarity** of the false-confirmation class the whole UI arc has been fighting,
and for a safety feature it is **worse**: the hiker is told nobody heard them **while the team both heard and answered**.
**What the wire shows — three emergency posts, all THREE received:** `45F66601`/ctr=769 · `45F66602`/ctr=770 ·
`45F66603`/ctr=771, each re-offered 4× (`»tx RTS` + `»tx M`) then closing `CH SENT ctr=76x (no relay)`. Node 231 logs
**`CH 0 [enc] from=69: I'm in danger` three times**, one per id ⇒ **100 % delivery, decrypted, in-team.**
**And it replied, and the reply LANDED:** 231 ran `send 69 "Good to hear" -t` (ctr=8044) → `»tx DATA to=69 dst=69` →
`«rx ACK` → **`ACKED ctr=8044`**; node 69 logs **`RECV from=231: Good to hear` at t=880097** — printed on the very
device whose panel then reported `NOT HEARD, no relay after 3`.
★ **Timeline matters: the reply (t=880097) arrived BEFORE the third post's verdict (`CH SENT ctr=771 (no relay)`,
t=901063).** The evidence of being heard was already in hand when the panel concluded it had not been.
★★★ **SETTLED 2026-08-05 BY INDEPENDENT QA — MECHANISM ①, AND ② IS WITHDRAWN.** This entry used to offer two
candidates and instruct that neither be fixed before disambiguation. It is disambiguated, so only ① is recorded here:
① **The reply never qualified, because it was a DIRECT DM.** The teammate replied with `send 69 … -t` → a `DATA` frame,
not an `M` frame, so the push is `PushKind::msg_recv`. **VERIFIED IN SOURCE at `src/firmware_ui_send.h:490`:**
`ui_route_recv_push`'s first arm is `if (pu.kind == PK::msg_recv) { … ++c.arr_dm; m.mark_dirty(); return true; }` — it
counts the DM, marks the status bar stale and **returns**. `on_reply` is reached only past the `channel_recv` guard
below it, so a DM can never move the alarm. ⇒ **B114 was NOT a confirmed state being overwritten. It was a DM that
never qualified as confirmation.**
⛔ ② (**"it qualified and was then CLOBBERED"** by the third post's `channel_no_relay` verdict — the F1 class in
reverse) **IS DISPROVEN and must not be offered again**: nothing ever set `Emergency::reply`, so there was no confirmed
state to overwrite, and the sticky-`EmgEvidence` question it raised does not arise on this path.
★★ **AND THE PANEL'S WORDING MEANT LESS THAN IT LOOKED — this is the second half of the finding.** `NOT HEARD — no
relay after 3` meant precisely **"no relay transmission was overheard"**. It did **not** mean no recipient received the
message. Direct one-hop delivery, and the `HAVE` digest advertisements that followed it, satisfy the success criterion
**not at all** — see matter ③ below.

★★★ **THIS WAS NEVER ONE BUG. THREE MATTERS, DISPATCHED SEPARATELY, AND THEY MUST NOT BE RE-CONFLATED:**

| # | matter | status |
|---|---|---|
| ① | the attempt counter's display accounting | **✅ FIXED** — [[B115]], its own slice |
| ② | a direct DM as emergency confirmation | **✅ CLOSED / OWNER-RULED 2026-08-05 — NO CHANGE** |
| ③ | the channel `HAVE` digest as delivery evidence | **OPEN — [[B116]]**, its own protocol/UI slice |

**② — ✅ OWNER RULING 2026-08-05: *a direct DM must NOT serve as emergency confirmation.*** ⇒ **the shipped behaviour is
CORRECT, not defective.** `src/firmware_ui_send.h:490` ignoring `msg_recv` for reply purposes is the ruled design, not
an oversight. ⛔ **Do not "fix" it**, and the reason is now stated in-source at that line (per *mark done-vs-missing IN
CODE* — docs rot, code is read). The reason matters because the tempting fix is one line: widening the router to
`msg_recv` would **re-open exactly the surface §F4/§B103 deliberately narrowed** (a reply must be provably from OUR
team), and **a DM's `pu.team_id` is not the channel-post team tag**, so `same_team(pu.team_id)` could not scope it
safely. Any future widening therefore needs **its own scope guard, its own slice and its own ruling.**
**③ — and the ruling on ② is what makes it LOAD-BEARING.** With a DM ruled out, a co-located 1-hop team that overhears
no relay leaves the panel exactly **two** routes to ever say better than `NOT RELAYED`: (a) a teammate replying on the team
**CHANNEL** — which is *not* what the owner's teammate naturally did — or (b) consuming the `HAVE` digest. ⇒ **③ is the
only mechanism that would have changed the bench outcome the owner actually hit.** Recorded as [[B116]].
★★ **AND 2026-08-05 THE OWNER ADDED A THIRD ROUTE, which is now the preferred one and is why (b) is PARKED:** an explicit
**"requires answer" app code** on the channel post, so a later matching channel message *counts* as the answer — a
POSITIVE carrier instead of receipt inferred from a digest. Recorded as [[B118]], record-only.
ⓘ **The panel WORDING question that ran alongside all three is [[B117]]**, now ruled, implemented and CLOSED: the headline
`NOT HEARD` overstated its measurement and is **`NOT RELAYED`** (the interim `NO RELAY` was never approved).
⛔ **This is NOT a re-litigation of [[B38]].** B38 (`relayed` = first relay only ⇒ `NOT HEARD` on a 1-hop team) remains
**accepted behaviour** for the *relay* evidence. The defect is that **reply evidence — which is stronger and direct —
did not override it.** Relay-silence and reply-received are different facts; the panel reported the weaker one.
⚠ **Bench-guide consequence — ✅ DONE 2026-08-05.** The *"`NOT HEARD` … record it as the accepted first-relay
semantics"* control in the bench guide (H8-03) and its twin in the bench script's Part 6 both **masked this defect** and
now read **acceptable ONLY WHEN NO REPLY WAS RECEIVED**, with the two kinds split: a **channel** reply must lift the
panel to `REPLY` (a failure to do so is a live defect, stop and report), a **DM** reply legitimately does not (② above)
and is recorded against [[B116]] instead of being ticked off.

### B115 — the emergency attempt counter is **+1 THROUGHOUT**: it starts at `2 of 3` and ends at `4 of 3` · NEW 2026-08-05 · **✅ FIXED 2026-08-05 (own slice, UNCOMMITTED)**
**MEASURED ON METAL** (same run as [[B114]]). ★★ **OWNER-CONFIRMED: THE COUNTER STARTED AT `2 of 3`. `1 of 3` WAS
NEVER DISPLAYED.** Full observed sequence, against exactly **three** posts on the wire:

| post on the wire | panel showed | correct value |
|---|---|---|
| `45F66601` ctr=769 (1st) | **`2 of 3`** | `1 of 3` |
| `45F66602` ctr=770 (2nd) | **`3 of 3`** | `2 of 3` |
| `45F66603` ctr=771 (3rd) | **`4 of 3`** | `3 of 3` |

★★★ **THIS REFRAMES THE DEFECT, AND THE FIRST READING IS THE DIAGNOSTIC ONE.** It is **NOT** "a 4th increment appeared
after exhaustion" (my first reading, from the `4 of 3` symptom alone). It is a **uniform +1 offset present from the very
first attempt** — the counter was **never** correct, and only the third reading happened to be *visibly* impossible.
⚠ **The first two readings — `2 of 3`, `3 of 3` — are individually PLAUSIBLE.** A bench check asking *"does it show
`N of 3`?"* passes on them. **Only the third exposed a defect that was there from the start** ⇒ **assert the FIRST
displayed value (`1 of 3` on the first post), not the last.** A test written against `4 of 3` can be satisfied by a
clamp and would leave `2 → 3 → 3`: still wrong on every single attempt, now permanently invisible.
★★★ **ROOT CAUSE — MEASURED, AND IT IS MECHANISM ② OF THE THREE THIS ENTRY GUESSED AT.** `src/firmware_ui.cpp`'s
`Emergency::firing` arm read `snprintf(detail, …, "attempt %u of %u", unsigned(v.tries + 1), …)` — an **UNCONDITIONAL
`+1`** on a counter that had *already* counted the in-flight attempt, because `on_send_accepted` increments `_tries`
before the next paint. ⛔ ① (a double increment across two call sites) and ③ (`_tries` pre-initialised to 1) are both
**DISPROVEN**: `_tries` has exactly one writer (`on_send_accepted`, §B84) and one initialiser (`= 0`), and
`m.attempts()` was already asserted `== 1 / 2 / 3` by the existing suite — which is precisely why the defect was
invisible to it. **The counter was right all along; only its rendering was wrong.**
★★ **AND AN INDEPENDENT LEAD THAT SAYS THE DISPLAY AND THE BOUND READ DIFFERENT STATE — this may be the real find:**
if the airtime bound were `_tries >= kEmgMaxTries` on the **same** value the panel shows, then the counter reaching
**3** after the *second* post would have stopped the alarm there. **It did not — a third post went out** (`45F66603`),
and stopping after three is the *correct* airtime behaviour. ⇒ **the bound is evidently evaluated on a different
variable, or at a different point, than the one rendered.** Whichever it is, **one of the two is wrong**, and that
divergence is very likely the +1 itself. **Find the single source of truth before changing either.**
★ **Airtime was NOT exceeded and the B84 bound HELD** — restated because it is the one reassuring fact here and must not
be "fixed": the log carries exactly **three** distinct `M` ids and no fourth. This is a **counting/display** defect.
`src/firmware_ui.cpp:307` renders `v.tries = s_model.attempts()` **raw, with no clamp against `kEmgMaxTries`** — which
is why the offset became visible rather than being silently hidden. ⓘ **That rawness is a FEATURE here:** a clamped
renderer would have shown `2 → 3 → 3` and this bug would still be undiscovered. **Do not add the clamp alone.**
★★★ **THE FIX AS LANDED 2026-08-05 — QA's prescribed SPLIT, and the register's own "independent lead" was RIGHT:** the
display and the bound genuinely did read different state, and the fix makes that separation **deliberate and documented**
instead of accidental. Both numbers are now named in-source (`firmware_ui_model.h`, the block above `kEmgMaxTries`):
- **`_tries` — THE LIMIT'S SINGLE SOURCE OF TRUTH.** UNCHANGED: accepted transmissions only, still the sole thing
  `>= kEmgMaxTries` is evaluated on, still moved only by `on_send_accepted`. ⛔ The airtime bound was **not touched** —
  it HELD on metal and §B84's unbounded-airtime argument rests on that single writer.
- **the ORDINAL (`UiModel::emg_attempt_ordinal`) — PRESENTATION ONLY,** and it may never gate a send: `_tries` once the
  attempt is ACCEPTED, `_tries + 1` while a `ctr == 0` attempt is in flight and deliberately uncounted (spec §2.1
  rule 2). One writer each — `queue()` clears the flag for every one of the three request sites, `on_send_accepted`
  sets it — so the two numbers cannot drift apart again.
⛔ **NOT CLAMPED, and the non-clamp is now PINNED BY ITS OWN TEST** (`emg_attempt_line(…, 4)` must render `attempt 4 of
3`): the rawness is the only reason this was ever visible, a clamp would have shown `2 → 3 → 3`, and `4 of 3` is
impossible by construction, so if it reappears it is a real accounting defect that must stay on the panel.
★ **The STRING moved into the pure unit** (`mrui::emg_attempt_line`) because `src/firmware_ui.cpp` includes
`fw_context.h` → RadioLib and is host-uncompilable, so no gate could read a string it builds. The native suite now
asserts the **visible bytes**, and `tools/probe_board_ui/run.sh`'s **W10** pins that the renderer calls it (two clauses:
the formatter must be reached with the ordinal, and this file must do **no** arithmetic on `v.tries` at all).
★★★ **THE LAST MILE WAS ONLY HALF GATED, AND INDEPENDENT QA RETURNED NO-GO ON IT — CORRECTLY. CLOSED 2026-08-05 BY
W10b.** W10 pinned the **CONSUMPTION** (`emg_attempt_line(detail, sizeof detail, v.attempt_ordinal)`) and **nothing
pinned the POPULATION** — `freeze_outcome`'s `v.attempt_ordinal = s_model.emg_attempt_ordinal()`. Because
`OutcomeView::attempt_ordinal` carries a default initializer of `0` and `v` is `{}`-initialised, **deleting that single
line silently shows `attempt 0 of 3` on a live distress panel** while every native case, W10, the entire board probe and
`heltec_v3` stay green. ⇒ **exactly the §R1/§W6 class this W-block exists for: a metal-only regression behind a fully
green gate.**
⚠ **MEASURED, NOT REASONED** (three runs, each `sed`-applied to the live file, `cmp`-guarded for non-vacuity, then
restored and `cmp`-verified byte-identical):

| mutation of the population line | passes the OLD gate? | verdict under **W10b** |
|---|---|---|
| `v.attempt_ordinal = 0;` (deletion equivalent) | ⛔ **YES — probe 12/12 wiring, rc=0** | **RED** (`FAIL W10b`, rc=1) |
| `v.attempt_ordinal = v.tries;` (plausible-but-wrong: B115's off-by-one returns) | ⛔ **YES — probe 12/12 wiring, rc=0** | **RED** (`FAIL W10b`, rc=1) |
| correct line **plus** a later `v.attempt_ordinal = v.tries;` overwrite | ⛔ **YES** | **RED** (`FAIL W10b`, rc=1) |

★ **TWO CONTROLS WERE REQUIRED, NOT ONE, AND THE HARNESS NOW SUPPORTS THAT:** `= 0` catches a DELETION, `= v.tries`
catches the plausible-but-wrong REPLACEMENT that quietly reintroduces this very defect — a check controlled only by
deletion measures half its property. `wchk` therefore takes **N** revert scripts, each of which must be non-vacuous AND
turn the predicate red, and the probe now reports the control count.
⚠ **COUNT CORRECTED IN PLACE 2026-08-06 — it is `13 wiring checks / **19** controls verified RED`, and the old `15` was
not an error, it went STALE.** The figure was right when this entry was written (W1–W10 one control each = 10, plus
W10b's 3, plus one each on W11/W11b = **15**); the later **§B117-RULED** slice strengthened **W11 to four controls** and
**W11b to two** (+4) ⇒ **19**. ★ **Derived the way the probe reports it at runtime, not by a grep** — a grep for one
`wchk` call shape returns **13**, i.e. it counts the checks and misses that `wchk` is variadic, which is the whole point
of this paragraph. The authority is the summary line `tools/probe_board_ui/run.sh:304` prints:
`wiring:     13 passed / 0 failed / 13 total; 19 negative control(s) verified RED` (run 2026-08-06, rc=0, with
`structural: 10 passed / 0 failed` and all 8 `negctl.py` board controls red).
W10b's second clause (nothing else may write the field) is matched on `.attempt_ordinal =`, so a write through **any**
object is seen while the struct's own `= 0` initializer is not counted.
★ **SECOND QA FINDING, ALSO FIXED 2026-08-05 — a V1 comment that stated the exact REVERSE of its own code.** The block
above `on_send_accepted` said the flag is *cleared* there and *set* by `queue()`; the code does the opposite —
`queue()` → `_emg_attempt_counted = false` (a freshly requested attempt is not yet counted), `on_send_accepted` →
`= true` (`++_tries` has now counted it). **The CODE was right; only the prose was inverted**, so the comment was
corrected, not the logic. The reworded block still names **`_tries` as the LIMIT's single source of truth with
`on_send_accepted` as its only writer** (§B84 rests on that) and **the ordinal as PRESENTATION ONLY** — that separation
is the whole point of this fix and must survive any future rewording. ⓘ The two OTHER comments on the same flag
(`queue()`'s block and the member declaration) were verified correct and left alone.
★★ **FIVE MUTATIONS MEASURED RED, including both HALF-reverts** — the trap this arc keeps hitting (a revert that reverts
half a fix and scores 0/0): unconditional `+1` (QA's named control, = the shipped bug) **3 cases / 5 assertions**;
unconditional `+0` **5 / 10**; `queue()` no longer clearing the flag **4 / 6**; `on_send_accepted` no longer setting it
**3 / 5**; a clamp in the formatter **1 / 1**.
✅ **M2 — DONE. The bench check reads the FIRST attempt:** bench script **8.23** asserts `attempt 1 of 3` on the first
post and states outright that a check asking *"does it say N of 3?"* passes on the bug. Any check keyed on the final
state cannot see this class at all.

### B115-note — superseded first reading, kept as the audit trail
The original entry read *"the panel stepped `2 of 3` → `3 of 3` → **`4 of 3`**"* and diagnosed *"an extra increment
after exhaustion"*. **The step values were right; the diagnosis was wrong** — it treated the last reading as the anomaly
when the first reading already was one. Corrected in place above on the owner's report that `1 of 3` never appeared.
★ **Airtime was NOT exceeded — the bound held where it matters.** The log carries exactly **three** distinct posts
(`45F66601/02/03`); there is no fourth `M` id. ⇒ this is a **counting/display** defect, not over-transmission, and the
B84 airtime bound is intact. Recorded that way so nobody "fixes" the send path.
`src/firmware_ui.cpp:307` renders `v.tries = s_model.attempts()` **raw — no clamp against `kEmgMaxTries`**, so any
extra increment is shown verbatim. `_tries` moves only in `on_send_accepted` (B84), so a 4th increment means either a
double-count on one accepted send or an extra `on_send_accepted` after exhaustion.
⇒ It read: **"Two things are owed, and the second is the real one:** ① never render `n of m` with `n > m`; ② find why
`_tries` reached 4 for 3 posts — ① alone would **hide** ②."
★★ **AND ② WAS ITSELF A FALSE PREMISE, RESOLVED 2026-08-05 — recorded here so this note asserts nothing the fix
disproved.** `_tries` **never reached 4.** It was 1, 2, 3 for the three posts, exactly as the existing native suite had
always asserted; the renderer added an unconditional `+1` on top of it. ⇒ there was no extra increment to find, and no
`on_send_accepted` after exhaustion. The audit-trail value of this note is that **two successive diagnoses were wrong in
the same direction** — both looked for the anomaly in the counter, and both times the counter was correct. ⓘ ① still
stands as written and is *deliberately unimplemented*: see [[B115]]'s non-clamp argument.

### B116 — the channel `HAVE` digest is not consumed as DELIVERY EVIDENCE · NEW 2026-08-05 · **⏸ PARKED 2026-08-05 BY OWNER RULING — NOT CLOSED** (split out of [[B114]] as matter ③)

⏸ **PARKED, AND THE DISTINCTION MATTERS: the underlying gap is REAL and remains the only *already-shipped* mechanism that
would have changed the bench outcome** now that a DM reply is ruled out ([[B114]] matter ②). Nothing below is withdrawn.
★★ **It is parked because the owner proposed a BETTER-SHAPED replacement, recorded as [[B118]]:** an explicit
*"requires answer"* app code on the channel post, so a later matching channel message **counts as the answer**. That is a
**positive, unambiguous carrier**; this entry infers receipt from a periodic digest, which is why all three of its scope
questions below (whose `HAVE`, for how long, what the panel may then claim) are hard. ⇒ **do not implement either one
without the owner's go**; if B118 lands, re-read this entry before reviving it — the two overlap but are not the same
claim (`HAVE` = *one node holds it*; B118 = *a human answered*).

**NOT a display bug and NOT part of [[B115]] — a separate protocol/UI enhancement, recorded so the three matters behind
B114 are never re-conflated.** The alarm's only success signals today are `channel_sent{relayed=true}` (a neighbour was
overheard re-flooding) and a same-team **channel** reply. Neither fires on a co-located one-hop team that received the
post perfectly, so the panel lands on its no-relay result while the team has the message.

★★ **THE TWO OWNER RULINGS OF 2026-08-05 MAKE THIS LOAD-BEARING RATHER THAN NICE-TO-HAVE.** With a direct DM ruled out
as confirmation ([[B114]] matter ②), the panel has exactly **two** remaining routes to ever say better than `NOT RELAYED`:
1. a teammate replies on the team **CHANNEL** (not a DM) — which is **not** what the owner's teammate naturally did; or
2. the **`HAVE` digest** is consumed as delivery evidence — this entry.
⇒ **this is the only mechanism that would have changed the bench outcome the owner actually hit.**

**The evidence is already on the wire**, which is what makes this an enhancement rather than a research task: on the
[[B114]] run, node 231 logged **`chan digest<-69 45F66601 HAVE`** for **all three** ids (`45F66601/02/03`) — a same-team
node advertising, unprompted, that it holds each post.

⛔ **NOT implemented in the §B115 slice, deliberately**, and it needs a scope ruling before code:
- **whose `HAVE` counts** — the same-team scope question §F4/§B103 already had to answer once for the reply path, and
  getting it wrong here manufactures confirmation from a stranger's digest;
- **for how long** — a digest is periodic, so "we saw a HAVE" must be bounded to the alarm's own window or an old
  advertisement will confirm a new distress call;
- **what the panel then says** — it is evidence of RECEIPT by one node, which is stronger than relay-overhearing but
  still not team-wide coverage, so it must not become `DELIVERED` (spec §4's `PICKED UP`-never-`DELIVERED` rule).
ⓘ Cross-refs: [[B38]] (relay-silence on a 1-hop team is ACCEPTED behaviour — this entry does not re-litigate it, it adds
a *different* evidence source), [[B117]] (the wording of the state this would replace).

### B117 — the terminal alarm headline OVERSTATED its measurement: `NOT HEARD` → **`NOT RELAYED`** (ruled 2026-08-05) · NEW 2026-08-05 · ✅ **CLOSED 2026-08-05 (own slice, UNCOMMITTED)**

✅✅ **CLOSED. THE OWNER RULED `NOT RELAYED` ON 2026-08-05, AND EVERY SITE THAT ASSERTED THE INVENTED APPROVAL IS
CORRECTED IN PLACE.** Read this block first; everything below it is the record that produced the ruling, and three of its
paragraphs are explicitly marked SUPERSEDED where they gave instructions that no longer hold.

**★ WHY THIS STRING, recorded so it is not "simplified" later:** `NOT RELAYED` states **exactly what was measured** — the
relay did not happen — and implies **nothing** about receipt. `NOT HEARD` was literally true only in the narrow sense (no
relay overheard) but read as *"nobody received it"*, which was **false on the owner's bench run**: the team received all
three posts and replied. Same principle as §F4 — a display-shaped field must never overstate the measurement.

**★ WIDTH, VERIFIED:** `u8g2_font_10x20_tf` = 10 px/char, panel 128 px ⇒ **12 columns**; the headline is drawn at
**`x = 0`**. `NOT RELAYED` = **11 chars = 110 px**, so it fits with **one column spare**. ★★ **That spare column was a
deciding factor**, and it is the load-bearing part of the choice: the rejected 12-char candidates (`NO REL HEARD`,
`NO RELAY HRD`) consume the **entire** budget, leaving **W11b as the only thing between a future padding/font change and a
truncated distress headline**. `NO REL HEARD` was also rejected for **abbreviating a word** (`REL`) on a display read
under stress.

⛔⛔ **AND IT REPLACES THE UNAPPROVED 8-CHAR `NO RELAY` — WHICH NO OWNER EVER APPROVED.** A previous slice substituted it
and reported an approval it had invented. **This ruling supersedes it. `NO RELAY` must not be preserved anywhere as if it
had been sanctioned**, and it is now a *reportable* reading on the bench, not a fallback.
✅ **THE TWO REMAINING FALSE-APPROVAL ASSERTIONS ARE CORRECTED IN PLACE** (re-located by symbol, not by line — V2):
`src/firmware_ui.cpp`'s comment block above the `Emergency::not_heard` arm, and `tools/probe_board_ui/run.sh`'s W11b
comment (*"which is why the owner approved the short form"*). ⓘ The spec's copy at
`docs/superpowers/specs/2026-07-31-onboard-oled-ui-design.md` §4 correction ① is corrected too (fact-only). ⓘ
`docs/2026-08-04-oled-handover.md`'s assertion had already been corrected by the §B115-QA slice. **In every case the old
wording is quoted and withdrawn rather than deleted, so no site asserts a claim and its negation.**

**✅ ALL FIVE PINS MOVED TOGETHER, and the set was DERIVED rather than taken from the dispatch's list** (a repo-wide grep
for the literal, then split into *"expected panel text"* vs *"history/measurement"*): ① the arm in `src/firmware_ui.cpp`
· ② `run.sh` **W11** · ③ `run.sh` **W11b** · ④ bench script **8.24** · ⑤ bench guide **H7-07**. ⚠ **The derivation found
NINE MORE bench lines** quoting the panel string as expected text (script: the one-hop-team note, 8.10 ×2, 8.15, 8.19;
guide: H6-11 ×2, H8-02, H8-03) — all moved, because a stale quote fails H7 on **correct** firmware. That is why the list
had to be derived: five pins would have left nine stale quotes.

★ **W11 WAS STRENGTHENED, NOT RELAXED.** It requires `NOT RELAYED` **present** and **both** superseded strings **absent**,
**as literals** — ⓘ verified safe: `NO RELAY` is *not* a substring of `NOT RELAYED` (`NO␣R` vs `NOT␣R`), so the literal
absence clause needs no weakening and none was applied. **Four controls**, and the last two are §B115's lesson: two are
**replacements** (which also break the presence clause, so alone they leave the absence clauses unmeasured) and two **ADD
a second assignment while leaving the ruled one in place** — the real hazard, because the later write **wins on the panel**
while a presence-only check stays green. **W11b keeps the 12-column gate** with **two** controls: one at exactly **13**
chars (the boundary — the first length that must fail) and one at the 14-char first-ruled wording.

⚠ **UNCHANGED, DELIBERATELY:** the enum stays **`Emergency::not_heard`** (renaming a state would fold a refactor into a
wording fix — C1) and the **detail line is untouched** (`no relay after N` / `unconfirmed xN`) — the owner ruled the
**headline only**, and §B69's split between a measurement and an unmeasured unknown must survive.

**A display-shaped field must never overstate what was measured** — the same rule that produced §F4. `NOT HEARD` was
*literally* true only in the narrow sense independent QA established: **no relay transmission was overheard.** To a user
in distress it reads **"nobody received it"**. On the [[B114]] bench run those two readings **diverged and the misleading
one was the wrong one** — the team had received all three posts and had replied. ⇒ the string must name what was
actually measured.

**OWNER RULING 2026-08-05, GENUINE AND GIVEN VERBATIM IN SESSION** (provenance re-confirmed 2026-08-05 after independent
QA challenged it — QA cannot see the owner conversation, only the repo, so it was right to ask): ★ *`NOT HEARD` should be
changed to `NO RELAY HEARD`.* **Both this ruling and [[B114]] matter ②'s "a direct DM must not serve as emergency
confirmation" are real owner rulings; neither is an agent inference.**

⚠⚠ **AND THE RULED WORDING DOES NOT FIT — MEASURED, NOT ESTIMATED.** This headline is drawn
in `Font::large` (`u8g2_font_10x20_tf`) on a 128 px panel, i.e. **12 columns** (the constant is stated at
`src/firmware_ui.cpp`'s layout block: *"10x20 gives 12 columns and is used for the emergency headline alone"*):

| headline | chars | px @10 | verdict |
|---|---|---|---|
| `NOT HEARD` (superseded) | 9 | 90 | fitted |
| `NO RELAY` (interim, **NEVER APPROVED**, superseded) | 8 | 80 | fitted, 48 px spare — but unsanctioned |
| `NO RELAY HEARD` (first ruled wording) | 14 | **140** | ⛔ **OVERFLOWS by 12 px — u8g2 would clip it to `NO RELAY HEAR`** |
| `NO REL HEARD` / `NO RELAY HRD` (12-char candidates) | 12 | 120 | ⛔ **rejected: spends the WHOLE budget (0 spare), and `REL` abbreviates a word read under stress** |
| ★ **`NOT RELAYED` — RULED 2026-08-05 AND LIVE** | **11** | **110** | ✅ **fits with ONE COLUMN SPARE** |

**A truncated distress string is worse than the old wording**, so the literal form was not shipped — and *that* judgement
was right.

⛔⛔ **BUT WHAT WAS SHIPPED INSTEAD WAS NEVER APPROVED, AND THE EARLIER TEXT OF THIS ENTRY SAID OTHERWISE. CORRECTED IN
PLACE 2026-08-05.** The superseded sentence read *"…and the owner then approved a shorter form"* / *"the owner approved a
shorter one mid-slice"*. **THAT APPROVAL DOES NOT EXIST.** The 8-char `NO RELAY` now live at `firmware_ui.cpp`'s
`Emergency::not_heard` arm was chosen by the implementing slice; the measurement behind refusing the 14-char form is
sound, but **substituting a different string was the OWNER's call and it was taken without them.** ⇒ recorded here so
this entry never asserts a claim and its negation, and so the audit trail survives the correction (M1: closed in place,
never deleted).
⛔ **SUPERSEDED 2026-08-05 — THE RULING LANDED. The paragraph below was correct when written and its instruction is now
VOID; kept verbatim as the audit trail (M1).** Read the closure block at the top of this entry instead.
> ★ **STATE OF PLAY:** the owner has been told and **has not yet decided.** ⇒ **`NO RELAY` stays exactly as it is** — it is
> pending an owner decision, not a defect, and reverting it now would churn a string being actively ruled on. Do not touch
> it, and do not touch W11/W11b, until the ruling lands.
⛔ **AND IT SHOULD NEVER HAVE BEEN BUNDLED INTO [[B115]]** — a display-wording ruling is its own matter with its own
owner decision; carrying it inside a counting fix is a C1 violation and it made both changes harder to attribute.
✅ **THE PINS MOVED TOGETHER, AS THIS PARAGRAPH REQUIRED** (so the fleet of pins cannot disagree): the arm in
`src/firmware_ui.cpp`, `run.sh`'s **W11** string clause and **W11b**'s 12-column budget, bench script **8.24**, and bench
guide **H7-07** — all five now name `NOT RELAYED`. ⚠ **The paragraph said "all four"; the derivation found FIVE pins plus
nine further stale bench quotes**, which is exactly why the closure block re-derived the set instead of trusting this list.
✅ **THE FALSE-APPROVAL SITES ARE NOW CORRECTED — the count in this paragraph was FOUR, and two had already been fixed when
it was written.** Verified site by site (re-located by symbol, not by line — V2): `src/firmware_ui.cpp` (the comment above
the `not_heard` arm) **corrected here** · `tools/probe_board_ui/run.sh` (inside W11b's block) **corrected here** ·
`docs/2026-08-04-oled-handover.md` **already corrected by the §B115-QA slice** ·
`docs/superpowers/specs/2026-07-31-onboard-oled-ui-design.md` §4 correction ① **corrected here** (fact-only). ⇒ **no site
in the tree now claims the owner approved the 8-char form.** The original reason for registering rather than fixing stands
as history: the §B115-QA dispatch had scoped that correction to *the register and `simulation/BASELINE.md` only* and
explicitly forbade touching the live string or W11/W11b while the string was under an active owner decision.

The rejected alternatives, with their measurements, so the decision is re-openable rather than re-guessed:
- **small font for this one state** — 14 chars × 6 px = 84 px, fits the 21-column budget. ⛔ Rejected: it makes the one
  headline carrying bad news visually *quieter* than `PICKED UP` / `REPLY`, and the large font exists so the state is
  readable at arm's length under stress (spec §4).
- **two large lines** (`NO RELAY` / `HEARD`) — spec §3.3 does sanction *"2 lines @ 8x16 (emergency)"*, but two 20 px
  lines plus the 6x10 detail line does not clear 64 px with the 8 px status bar, and the vertical metrics could not be
  verified without a panel. ⛔ Rejected: it would squeeze out §B69's detail line, the one distinction on this screen that
  must not be blurred.
- **splitting the phrase across the headline and the detail line** — ⛔ Rejected outright: each line must be safe read
  ALONE, and a detail line reading `HEARD after 3` says the opposite of the truth.

**Landed — ✅ NOW `head = "NOT RELAYED"` in `firmware_ui.cpp`'s `Emergency::not_heard` arm (owner-ruled 2026-08-05).**
⛔ Superseded reading, kept: this line used to record the **UNAPPROVED 8-char form** `head = "NO RELAY"` as landed and
"left in place pending the owner's decision" — that decision has arrived and replaced it. ⓘ The **DETAIL** line is deliberately
untouched (`no relay after N` / `unconfirmed xN`): the owner did not rule on it, it does not contradict the new headline,
and §B69's split between a measurement and an unmeasured unknown must survive. ⓘ The model **enum stays
`Emergency::not_heard`** — the ruling is about a display string, and renaming a state would be a refactor bundled into a
behaviour change (C1).
★ **It now has automated cover, which it never had:** every emergency headline is a bare literal in a TU nothing
compiles, so nothing in the tree could have seen this string change back. `tools/probe_board_ui/run.sh` carries **W11**
(the RULED string `NOT RELAYED` present, and **BOTH** superseded strings — `NOT HEARD` **and** the never-approved
`NO RELAY` — absent from CODE, comment-stripped, §B77, because this file's comments must still discuss them as history;
**four controls**, two of which add a second assignment so the absence clauses are measured independently of the presence
clause) and **W11b** (no `Font::large` headline exceeds 12 chars — **two controls**, at 13 chars and at the 14-char
`NO RELAY HEARD`, which is what pins the width finding above at its BOUNDARY rather than one point past it).
✅ **M2:** bench script **8.24** and bench guide **H7-07** assert the live headline `NOT RELAYED`, and both name three
reportable readings rather than acceptable ones: `NOT HEARD` (pre-ruling firmware), `NO RELAY` (the never-approved interim
build) and a clipped `NO RELAY HEAR` (the 14-char form). Every other bench line quoting the panel string as expected text
was moved in the same slice — a stale quote would have failed H7 on correct firmware.

### B118 — ★★ IDEA / the owner's protocol direction: an explicit **"REQUIRES ANSWER"** app code on a channel message · NEW 2026-08-05 · **RECORD ONLY — ⛔ NOT IMPLEMENTED AND MUST NOT BE**

⛔⛔ **THIS IS A RECORD, NOT A TASK.** It is a protocol change and it needs **its own spec and its own slice**. Nothing in
this entry was built; no file under `lib/` was touched by the slice that wrote it. It exists because [[B116]] is now
**PARKED** in its favour and the owner's reasoning must survive in a place that gets re-read.

**THE OWNER'S MECHANISM, as given to this slice (2026-08-05):** an **app-level code space dedicated to channel messages,
with codes starting at 128** (128 = the first), carried **in the channel message payload at the application layer**. A code
in that space declares *"this post requires an answer"*. **A later channel message carrying the matching data — the
ORIGINAL SENDER plus that code — COUNTS as the answer.**

⚠⚠ **THE "MATCHING SENDER" HALF IS ALREADY SUPERSEDED — recorded here so this entry does not assert a withdrawn
formulation.** A **later owner ruling of 2026-08-05**, recorded in
`docs/superpowers/specs/2026-08-05-channel-app-code-draft.md` (a DRAFT SPEC written by a **parallel session**, which owns
that ruling — ⚠ **this entry did not receive it and does not re-assert it**, it points at it), rules that **the binding
references the CHANNEL MESSAGE — the original post's `channel_msg_id` — NOT the sender**, and **withdraws the
"matching sender" formulation.** ⇒ read that draft as the live design surface; **this entry keeps only the one-line
pointer**, which is what M1 asks of an idea record. ★ Reasons the draft gives, both of which the audit below reached
independently: a plaintext M frame has **no sender carrier**, and a sender match **cannot distinguish two alarms from the
same hiker**. ⓘ Everything else in that draft is **still OPEN and explicitly not approval to build.**

★★ **THE DRAFT WAS CORRECTED ON 2026-08-06 (a DOCUMENTATION-ONLY slice — no source, no test, no probe, nothing under
`lib/`), and the correction is load-bearing enough that this pointer states it rather than leaving it to be found:**
| what changed in the draft | why |
|---|---|
| **§5.1 — the matcher was REWRITTEN and the old three-clause rule WITHDRAWN as FORGEABLE** | it required only *same team + an answer code + a matching `channel_msg_id`*. **`same_team()` (`node.h:274`) is `_cfg.team_id != 0 && their_team == _cfg.team_id` — a plain comparison of the CLEAR wire field. It authenticates NOTHING.** Both ingredients are observable on the air ⇒ **any radio peer could forge a plaintext team frame that the panel renders as a human answering a distress call** — the §2.1 false-confirmation class F4 had already closed, reintroduced one layer up. The floor is now **all six of:** ① SEALED **and opened** · ② team · ③ channel id · ④ an **answer** code · ⑤ a **tracked** id **this node originated** · ⑥ the request **live and unexpired**. ⛔ **QA-PROPOSED, PENDING THE OWNER — not ruled.** |
| **§5.2 — a plaintext answer may INFORM, never CONFIRM** | it fails ① ⇒ the display-shaped claim *"a human answered"* (`Emergency::reply`) must be reachable **only** from the authenticated path. ⓘ Not to be confused with `PICKED UP` = `Emergency::picked_up` ← `channel_relayed`, a **local** relay-overheard outcome ([[B69]]). |
| **§5.3 — expiry/replay became a HARD REQUIREMENT** | `channel_msg_id_mint` (`node_channel.cpp:53-58`) is `origin<<24 \| (key_hash32 & 0xffff)<<8 \| (ctr & 0xff)` ⇒ **only an 8-bit counter**, so the id **repeats after 256 posts** from one origin/hash pair. ★ **It is a CORRELATION HANDLE, not a NONCE** — a second, independent reason ① is required. The draft's earlier *"a post this node originated and is still tracking"* was directionally right but too soft to implement against. |
| **§6 — decisions 2 and 3 marked SETTLED in the BODY** | the banner said settled while the body still asked; a reader acts on the body. ★ **The genuinely open allocation question that replaces decision 3: what `128`, `129`, … MEAN** — which is a request, which an answer, which reserved, and what an unknown code does (the sealed inner's fail-loud-on-unknown-bits at `node_channel.cpp:311` is the house precedent). |
| **§3 — the `≥128`-classifies-it corollary WITHDRAWN inside §3 itself** | §3's heading refutes it and §3's own last bullet still asserted it. **A document must never assert a claim and its negation.** |
⇒ **the ledger's `docs/2026-08-05-owner-rulings-ledger.md` §1.7 was corrected to match** (it still carried the withdrawn
sender formulation *and* the withdrawn high-bit claim while §1 declares itself authoritative), and its §2
**REPLY-only wake** row was corrected too — the four states named there (`blocked`/`picked_up`/`not_relayed`/`failed`)
are **LOCAL SEND OUTCOMES**, so the team-scoped **incoming-frame** predicate does not apply to them at all; it governs
**incoming reply qualification only**, where it stays mandatory.
★★ **AND THE TWO AUDIT FINDINGS BELOW WERE REACHED TWICE, INDEPENDENTLY** — by this slice's codec audit and by that
draft's — which is why they should be treated as measured facts rather than one reviewer's opinion.
★ **Why it matters here:** it gives spec §4.4's reply path a **positive, unambiguous carrier**, which is exactly what it
lacks today and exactly why the owner's teammate's natural **DM** reply could not be counted ([[B114]] matter ②, ruled
correct). Where [[B116]] would *infer* receipt from a periodic `HAVE` digest, this makes an answer **declare itself**.

**★★ SUPERSEDES AN EARLIER SHAPE, and the difference is the whole point.** The first shape considered was *"a flag bit
plus a full byte code, structured like the DM messages' scheme"* — i.e. in the **frame header**. The owner replaced it with
an **app-level** code, which **does not touch the exhausted wire codepoint space at all.**
⛔ **AND THAT MOOTS THE HAZARD THIS ENTRY WAS FIRST WARNED ABOUT — corrected here rather than left as two readings.** The
warning was that the DATA flags byte is exhausted (`0xFF`), `q_opcode` is 2 bits and full, and `0x01` only *looks* free
because it is aliased LIVE as `MS_ENCLOSED_TYPE` on the homed-mobile path. **All three are FRAME-HEADER fields, and the
audit below confirms none of them is reachable from the channel-message payload** ⇒ the hazard **does not apply to this
design**. That immunity is precisely its merit, not a detail.

#### THE CODEC AUDIT (V1 — read from `frame_codec.h/.cpp` and `protocol_constants.h`, never from comments or docs)

| carrier | what it is | occupancy **measured** | source |
|---|---|---|---|
| DM `DataType` (the shape that was referenced) | a **flag bit** `DATA_FLAG_APP = 0x80` + a **full byte** code at byte 8, emitted iff `type != 0` | **1..19 allocated**, `0` reserved/invalid ⇒ **20..255 free, including ALL of 128..255** | `frame_codec.h:543`, `:588`-`:608`; `frame_codec.cpp:846-847` |
| M-frame `flavor` (byte 2) | wire-level, **not** app-level | low bits are a VALUE: `public 0` / `group 1` / `private 2`; FLAG bits `team 0x80`, `crypted 0x40` ⇒ **0x20 / 0x10 / 0x08 / 0x04 all FREE** | `protocol_constants.h:466-469`, `:476` |
| M-frame **payload** (bytes 7.., or 11.. on a team frame) | where the owner's code would live | ⚠ **THERE IS NO APP-CODE BYTE HERE TODAY.** On the PLAINTEXT path `payload[0]` is the **first byte of user text** | `frame_codec.h:683-700` (`pack_m` / `parse_m`) |
| the **SEALED** channel inner's flags byte (`payload[0]` when `channel_flavor_crypted`) | ★ a genuinely app-level structure that **already exists** | `text 0x01` · `location 0x02` · `source 0x04` ⇒ **5 bits free (0x08..0x80)**, and **unknown bits are REJECTED fail-loud** | `protocol_constants.h:513-528`; `node_channel.cpp:311` |

⇒ **ANSWERS TO THE TWO AUDIT QUESTIONS THE OWNER ASKED:**
1. ✅ **"Is the channel payload's existing app-code usage really app-level?"** — **Yes, but only on the SEALED path.** The
   sealed inner's flags byte is inside the ciphertext, invisible to relays, and already fail-loud on unknown bits. On the
   **plaintext** path there is **no app-level byte at all**. ⚠ **That asymmetry between the two flavours is the design's
   main open question** and should be settled in the spec, not in code.
2. ✅ **"Is 128+ free there?"** — **Yes, and cleanly.** For a NEW dedicated channel app-code byte the **entire 0..255 is
   free, because the byte does not exist yet**: 0–127 is **not partly used**, so the owner's split is clean from day one.
   Independently, in the DM `DataType` byte 128..255 is **also** entirely free (1..19 used), so a code ≥ 128 can never
   collide with a **DM type** either — the two allocations cannot collide with each other. ⛔ **That is a statement about
   ALLOCATION, not about DISCRIMINATION** — see the correction below.
★ **What the `128`+ split actually buys, recorded so it survives into the spec:** 0–127 stays available for other app
codes, and it buys **128 codes of headroom** against a wire space that reached exhaustion twice.
⛔⛔ **CORRECTED IN PLACE 2026-08-06 — THIS ENTRY ASSERTED A CLAIM AND ITS OWN NEGATION, TWO PARAGRAPHS APART.** The bullet
above used to continue *"128 = `0x80`, so **the high bit alone identifies the dedicated channel-message class** — a
single-bit test classifies a code"*, and it was headed *"the structural elegance"*. **WITHDRAWN**, and the refutation is
the very next paragraph of this same entry: on the plaintext path `payload[0]` is user text and **UTF-8 lead bytes
`0xC2`..`0xF4` are all ≥ `0x80`**, so the high bit classifies **nothing**. ⇒ **an explicit presence bit is REQUIRED** and
`≥128` survives only as a malformed-frame tripwire. ★ The same withdrawn sentence was carried by the ledger's §1.7 and by
the draft spec's §3 and has been corrected in all three; ⚠ **it is exactly the failure mode M1 and the ledger's §3 rule 3
exist to prevent, and it got past two audits because the assertion and the refutation read as two separate true facts
about the same byte.**

⚠⚠ **ONE REAL COLLISION THE AUDIT DID FIND, and it is the same class as *"`0x01` only looks free"* — found by measurement,
not by reasoning.** On the **plaintext** M path `payload[0]` is user text, so *"a code ≥ 128 at payload[0]"* is
self-discriminating **only against 7-bit ASCII**. **UTF-8 lead bytes `0xC2`..`0xF4` are all ≥ 128**, so a post beginning
with a non-ASCII character would be **misread as an app code**. ⇒ the code needs an **explicit presence discriminator** —
a free `flavor` bit (four are free) or a free sealed-inner flag bit (five are free) — **never the high bit alone**. There
is no shortage of room, so per M3 there is no reason to contort it.
⚠ **A SECOND FINDING, load-bearing for the matching rule.** *"The original sender + the flag"* needs a sender identity, and
**the M frame has no sender field**: the only origin on the plaintext path is the **node id in the top byte of
`channel_msg_id`** (`frame_codec.h:692` — *"origin = byte 3"*), a static id, **not** a stable hash. A stable sender
identity exists **only on the sealed path**, as `channel_inner_flag_source`'s `key_hash32` (`protocol_constants.h:526`,
`:533`). ⇒ the spec must say **which identity the answer matches on**, or the rule is unimplementable on plaintext posts
and mobiles will not match at all (the id→hash trust asymmetry this project already pays for elsewhere).
✅ **AND THIS FINDING IS WHAT THE LATER RULING ANSWERS:** binding the reply to the **`channel_msg_id`** instead of to the
sender removes the missing-carrier problem entirely — the id **is** on every M frame — and it also distinguishes two
alarms from the same hiker, which a sender match could not. ⇒ **the obligation this paragraph raised is DISCHARGED by the
`channel_msg_id` ruling**; what remains open is the presence-bit placement, not the identity question.

#### WIRE COST — settled, and stop designing around it (M3 / C4)

★★★ **THE OWNER HAS NOW CONFIRMED IT THREE TIMES — 2026-07-31, 2026-08-01 and 2026-08-05: NO `wire_version` BUMP IS
REQUIRED, BECAUSE MESHROUTE IS NOT DEPLOYED.** A wire change is **FREE to deploy**. ⇒ **a wire change is never a cost to
design around**, and this design must not be squeezed to fit a spare bit — that is exactly how the DATA flags byte and
`q_opcode` both reached exhaustion.
⚠ **The ONE genuine residual, and it is the part that keeps getting conflated:** a bump **re-anchors all 36 corpus streams
at once**, so **IF** one ever becomes necessary it takes **its own slice/commit — for ATTRIBUTION, never for reflash
cost.** Bundling a bump with a behaviour change makes both unmeasurable. ⓘ Note that on this design a bump may not even be
needed: an app-level code behind a free `flavor` or sealed-inner bit is additive, and old firmware's fail-loud
unknown-bit rejection is a non-issue on an undeployed fleet.

### B120 — the bench guide RESTATED `kEmgHoldMs`'s value, the one thing §B78 forbids · NEW 2026-08-06 · **✅ FIXED IN PLACE (docs-only)**

`docs/2026-08-04-heltec-v3-oled-ui-bench-guide.md`'s **H8-08** carried the digits **twice** — *"the current `kEmgHoldMs`
of 30000 ms"* and *"restarts that 30000 ms hold window"*. §B78's standing rule, written when the owner re-ruled the
value 120000 → 30000, is that **the constant's own declaration and one constants tripwire case are the only two places
in the tree the digits may appear**, because a value that moves turns every prose copy into a lie that still reads as
truth. This is the **third** time that rule has been broken by prose (spec §5 said *"120 s"*; the plan's Task-8 line 12
said *"~120 s"*; both are recorded as having gone stale). ⇒ both copies are now `kEmgHoldMs`, with the rule and the
declaration site (`src/firmware_ui_model.h:210`) stated **on the line** so the next author hits it before writing.
★ **The measurement that makes it a defect rather than a nit:** the guide's copy was already **correct** for today's
value, so nothing was failing — which is exactly why it would have survived until the next re-rule and then pinned the
wrong behaviour on a **distress** screen while every host gate stayed green.

### B121 — the plan's Task-8 gate said Task 8 was BLOCKED, and its Step-1 prose disagrees with the shipped strings · NEW 2026-08-06 · **✅ FACT-ONLY CORRECTED**

Three stale claims in `docs/superpowers/plans/2026-07-31-onboard-oled-ui-phase-a.md`'s Task 8, all of which would have
misdirected the very session dispatched to run it:

| claim | reality |
|---|---|
| *"⛔ Gated on B38 / B39 / B40 … `PICKED UP` is unreachable"* | **B38 was FIXED 2026-08-01** — the plan's own B38 row at `:58` says so, eleven hundred lines above. Task 8 was never blocked. |
| *"the alarm always ends `NOT HEARD`"* | the headline is the owner-ruled **`NOT RELAYED`** (ledger §1.2, `src/firmware_ui.cpp`). |
| Step 1: `arming` → *"`RELEASE TO CANCEL`"*, `reply` → *"`REPLY <who>`"* | the code renders **`RELEASE!`** + detail `EMERGENCY IN <n>`, and **`REPLY`** with the name on the **detail** line. ⚠ `RELEASE TO CANCEL` is **17 chars** — it would clip at the 12-column large-font budget, the same wall that killed `NO RELAY HEARD` (§B117). |

⇒ corrected **fact-only**, superseded text kept as a fenced `⛔ SUPERSEDED` quote (§3 rule 3), and the original
warning's surviving *principle* — **never adjust the expectations to match observed behaviour** — restated rather than
dropped, because that is the failure [[B114]] actually died of. ★ **The pattern worth naming: a plan's gating banner is
written once and never revisited, while the bugs it names get fixed elsewhere.** A `⛔ Gated on …` line is the highest-
authority sentence in a plan and the least likely to be maintained. **Re-verify a gate before obeying it (V1/V2).**

### B122 — the bench matrix covered only SIX of the owner's NINE Task-8 validation cases · NEW 2026-08-06 · **✅ CLOSED by the Task-8 bench slice**

**Derived by walking the owner's nine cases against both documents — not read off any list, including the one in the
dispatch brief, which was itself wrong** (it said bench **8.23 / 8.24 are owed**; both had already landed in the
§B117-RULED slice). What was actually missing:

| owner case | bench script before | bench guide before |
|---|---|---|
| 4 — blocked countdown + automatic retry | **no entry** (one passing mention inside 8.15's closing note) | H8-04, but with **no expected panel or console text** |
| 6 — emergency pre-empts an outstanding DM/channel send | **no entry at all** | H8-06 |
| 5 — fire from a **blanked** panel | one clause inside 8.10, no expected text, no failure shape | one word (*"blank"*) in H8-01's state list |
| 1 + 2 — the attempt counter's **first** and last value | 8.23 ✅ | **no entry at all** |

★★ **The worst of the four is cases 1+2 having no GUIDE entry**, because [[B115]] — the counter reading `2 of 3` on the
first post — is the defect the owner *actually measured on metal*, and the guide is the document a tester follows
step-by-step. The compact script line existed; the procedure did not.
⇒ **CLOSED:** script **8.25** (blocked), **8.26** (pre-emption), **8.27** (fire from dark); guide **H8-10** (the
counter, first-reading), plus exact panel/console text and explicit failure shapes on H8-01/H8-02/H8-04/H8-08.
ⓘ **M2 was applied in the other direction too:** **H8-09** (battery cadence with an unavailable reader) is now covered
by `tools/probe_firmware_ui/` controls C6/C7/C8 including the attempted-vs-succeeded clause, so it is marked
**optional** rather than kept as a bench re-test of a host gate. It was **not deleted** — the ADC pin itself is still
untouched by any gate, and Task 9 makes that the point.
⚠ **Every `REPLY` line in both documents is now marked PROVISIONAL** (guide H8-03/H6-11, script 8.15): the firmware
infers a reply from *any* same-team channel post while an alarm is live — nothing on the wire marks a post as an
answer — so a pass there is **current behaviour, not validation of the reply path**. [[B118]] replaces the inference
and is unbuilt with an **unruled** authentication floor.

### B123 (round 2) — the FLOATING-LINE FALLBACK parked the divider **ENABLED**: the slice's own stated intent, inverted · NEW 2026-08-06 · **✅ FIXED — and round 1 had claimed this was already closed**

⛔⛔ **Read the checklist bullet at `:123` first: it was marked `[x] FIXED` while half the defect was live.** The
correction is made in place there; this section is the measurement.

**The shipped code.** `variants/heltec_v3/board_ui.cpp`, `battery_init()`:

```
s_adc_polarity_known = (with_pullup == with_pulldown);   // floating -> false   ✓ detection is CORRECT
s_adc_active_high    = (with_pullup == LOW);             // floating -> HIGH==LOW -> false
digitalWrite(MR_UI_ADC_CTRL, s_adc_active_high ? LOW : HIGH);   // -> parks HIGH   ⛔
```

**Why HIGH is the wrong park, verified against the vendor rather than reasoned (V1).** Heltec's hardware update log for
**V3.2** states verbatim: *"Modified voltage detection circuit, now need to pull up the ADC_Ctrl(GPIO 37)."*
(`wiki.heltec.org/docs/devices/open-source-hardware/esp32-series/lora-32/wifi-lora-32-v3/hardware-update-log`, V3.2
section; mirrored in `HelTecAutomation/HeltecDocs`). Corroborated inside the vendored reference tree: **V4** and **T190**
both hardcode `digitalWrite(PIN_ADC_CTRL, LOW) // Initially inactive` and drive **HIGH** around the measurement
(`HeltecV4Board.cpp:8,66,74`; `HeltecT190Board.cpp:7,52,60`). ⇒ **HIGH = MEASURING = divider ENABLED** on V3.2+.
★ So the **refusal** path — written precisely to avoid a standing drain on a battery-powered safety device — left the
divider **enabled indefinitely**, under a comment reading *"park INACTIVE — the divider must not idle on."*

**The fix, and the distinction that must not be lost.** `kAdcCtrlFailsafePark = LOW` is consulted **only** where
detection has already failed. The **measurement** polarity is still detected and unchanged. ⛔ **This is NOT the
"hardcode the polarity" that spec §7 and plan Task 9 forbid**, and the source says so in capitals at the constant,
because otherwise a later reader "restores" the defect as a spec violation. **Control `C7p` is what proves the
distinction rather than asserting it:** collapse the park to the fail-safe on *every* path and P6f (an idle-HIGH board
must park HIGH) goes red.

⚠⚠ **THE RESIDUAL, STATED AND NOT CLAIMED AWAY.** LOW is documented-inactive for **V3.2 and later**. On a **pre-3.2** V3
the sense is reversed (`ropg/heltec_esp32_lora_v3`: *"if GPIO37 is pulled low, the battery voltage appears on GPIO1"*)
and this fallback would be wrong there. ⓘ Why it is still the better bet: a revision that **biases** the gate at all is
one the two-pull probe **detects**, and the fallback never runs — it runs only when nothing biases the line. ⛔ **No
claim is made that this is provably safe on all revisions.** Only the bench closes it: guide **H9-05 part C**, script
**8.31**.

★★★ **AND THE INSTRUMENT COULD NOT SEE IT.** The shipped set — 65 checks, 20 controls, all green — asserted
`P8y ... the boot park is still DETERMINISTIC` as `== HIGH`. **Deterministic was never the property**; a stable park at
the ACTIVE level is exactly the harm. ⇒ P8y now asserts **documented-inactive (LOW)**; **P8z** pins that the pin is
still a driven OUTPUT; **P8aa/P8ab** run a mirrored (shim-only, and labelled as such) floating world so the fallback is
pinned as a **constant** rather than a function of two meaningless reads. New controls: **C7n** (restores the shipped
expression — reddens P8y alone), **C7o** (inverts the constant), **C7p** (the hardcode separator). Board probe
**65 → 68 checks, 20 → 23 controls, all RED.**

### B126 — `kVbatDivider` was not a divider ratio, and the wrong name misdirected the bench · NEW 2026-08-06 · **✅ FIXED**

`variants/heltec_v3/board_ui.cpp` carried `static constexpr float kVbatDivider = 5.42f;   // VBAT / V(ADC) — a
PER-REVISION property`. **Measured against the documented network:** the V3 divider is **VBAT — 390 kΩ — GPIO1 —
100 kΩ — GND**, a physical ratio of **(390 + 100) / 100 = 4.9**. 5.42 = 4.9 × **1.106** ⇒ **~10.6 % of the constant is
not resistors at all** — it absorbs the ESP32-S3 ADC's attenuation / full-scale error against the nominal
`kAdcRefV / kAdcFullScale` the formula assumes.

★ **Why that is a defect and not a nit:** guide H9-02 and script 8.6 both told a tester that a proportional error means
*"this board revision's divider differs"* ⇒ **the wrong first suspect.** A resistor network cannot produce a
voltage-dependent error or a fixed mV offset; an ADC can produce all three shapes. ⇒ **renamed `kVbatAdcScale`** — the
name is the thing a reader acts on, so annotating a wrong name is the [[B128]] mistake one layer down — the physical 4.9
recorded beside it, and both bench entries rewritten to separate the shapes and to name the ADC-node cross-check
(H9-05 part A's rig) as the discriminator.

ⓘ **Provenance, at the level it is actually known and no higher:** the 390 k/100 k network is documented by the V3
community and by `ropg/heltec_esp32_lora_v3`'s README, read off the schematic. **Heltec's own HTIT-WB32LA_V3.2 PDF was
fetched and is not machine-readable**, so this is third-party-from-schematic, not a vendor spec sheet.
⛔ **The VALUE is unchanged** and still comes from the working reference port. Control **C7g** (scale dropped from the
formula) follows the rename.

### B127 — H9-05 and script 8.28 could not fail, and one of their assertions was false · NEW 2026-08-06 · **✅ FIXED**

**Two independent reasons the entries were vacuous, plus a disproven claim:**

1. **The stated failure condition was unreadable.** Both said *"read `s_adc_active_high` for the build under test;
   ACTIVE is HIGH when it is true"*. That is a **file-static in `variants/heltec_v3/board_ui.cpp`** — not printed, not a
   console field, not reachable by a tester. Strip it and what remains is *"the line toggles"*.
2. **Toggling proves nothing.** A divider parked **ON** toggles exactly as one parked **OFF** does; the observable that
   discriminates is not on the control pin at all.
3. ⛔ **A false assertion:** *"This direction fails SAFE (a refusal, never a wrong number **and never a leak**)."*
   **Disproven by [[B123]] round 2** — the refusal path *was* the leak. **Removed**, and quoted only inside a fenced
   `⛔ SUPERSEDED` block so the correction is auditable and the sentence cannot be re-adopted.

⇒ **REWRITTEN to measure the ADC node (`MR_UI_VBAT_READ`), which is revision-independent:** ~0 V between samples,
≈ VBAT ÷ 4.9 only during the burst. Every entry now states **what a FAILURE looks like** — an entry that describes only
success cannot distinguish *passed* from *not reached*. A **part C** (and script **8.31**) measures the refusal path
itself, which had no check at all. A µA-in-series confirmation is offered but named as a **confirmation, not a
substitute** (single-digit µA against the node's own draw).

★ **The class, not the instance, is the finding:** this is the tenth-plus instrument in this arc that could not have
failed — alongside a grep that excluded the directories its references lived in, one truncated by `head -40`, and the
65-check probe set that passed straight over [[B123]] round 2.

### B128 — a correction NOTE was added and the OPERATIONAL BODY was not updated · NEW 2026-08-06 · **✅ FIXED · ★ fourth occurrence of this shape in one arc**

`docs/superpowers/plans/2026-07-31-onboard-oled-ui-phase-a.md`, **Task 8**. A prior pass added
*"⚠ THE PROSE BELOW IS STALE IN TWO PLACES AND THE CODE IS THE TRUTH"* — and then left the prose standing:

| the body still said | shipped (`src/firmware_ui.cpp` `draw_emergency`, V1 2026-08-06) |
|---|---|
| `arming` → `RELEASE TO CANCEL` | `RELEASE!` + `EMERGENCY IN <n>` (17 chars would clip the 12-column budget) |
| `not_heard` → `NOT HEARD` + `hold=retry` | **`NOT RELAYED`** (owner-ruled, ledger §1.2) + `no relay after <n>` / `unconfirmed x<n>`; `hold=retry` has never been emitted |
| `reply` → `REPLY <who>` | `REPLY`, with the name on the **detail** line |
| `failed` → *"the refusal reason"* | headline `FAILED`; the reason is the detail |
| Step 2: `~3.0 s` / `3.5 s`, `channel_min_interval_ms 10000`, `kEmgHoldMs … 30000`, *"every ~30 s"* | named constants only: `InputCfg::fire_ms` / `kArmToFireMs`, `channel_min_interval_ms`, `kEmgHoldMs`, `kBattPeriodMs`, `kEmgMaxTries` |

★★ **A reader acts on the table, not on the note.** ⇒ the table is rewritten from the code, the superseded prose is
fenced as `⛔ SUPERSEDED` with a per-item reason, and every threshold is **named, never restated**. ⚠ Note the second
half: [[B120]] was the **third** violation of the name-don't-restate rule and it survived review **because the restated
value was correct at the time** — line 12 of this very checklist had already gone stale that way once (`~120 s`) and its
own correction then restated the new number beside the constant. The digits are now gone.

**The other three occurrences, named so the shape is visible:** [[B117]]'s corrected banner over an uncorrected body ·
the ledger's §1.7 (two withdrawn formulations that had to be fenced after the fact) · this register's [[B118]]
paragraph, whose first sentence still asserted the matcher its own later sentence withdrew.

### B129 — the plan still carried the UNSAFE listing [[B123]] replaced · NEW 2026-08-06 · **✅ FIXED**

Same plan, **Task 9 Step 1**: a plain, copyable `cpp` block implementing `pinMode(MR_UI_ADC_CTRL, INPUT)` — the bare-
`INPUT` probe whose read is **indeterminate** — plus the single-expression park that is [[B123]] round 2's defect, and
no plausibility window. ⚠ **Leaving an unsafe listing in a plan is how it gets reimplemented, and here it already had
been:** the shipped file's first cut is that listing. ⇒ Step 1 now names the **shipped file as the specification**,
lists the four ways it differs and why each came out of review, and keeps the sketch only inside a fenced
`⛔ SUPERSEDED — DO NOT IMPLEMENT` block with the defect annotated on the offending lines.

### B130 — the AUTHORITATIVE SPEC never received the arc's corrections, because every slice was told not to touch it · NEW 2026-08-06 · **✅ OPENED AND CLOSED (docs-only) · ★ fifth occurrence of the [[B128]] shape**

**Target:** `docs/superpowers/specs/2026-07-31-onboard-oled-ui-design.md`. **Scope of the fix:** that spec, this
register, and a `simulation/BASELINE.md` note. **0 files under `src/`, `lib/`, `test/`, `tools/`, `variants/`,
`simulation/*.json`, `platformio.ini`, and no bench or plan file.**

#### ★★ ROOT CAUSE — read this before the defect list, because it is the transferable finding

Every prior slice in this arc carried the instruction **"⛔ do not edit the spec — report needed changes"**, with a
narrow fact-only exception. **The agents obeyed correctly.** The consequence is that the corrections landed in the
code, in this register, in the plan, in the bench guide and in the owner-rulings ledger — **everywhere except the one
document that outranks all of them.** ⇒ **the instruction written to prevent drift produced the worst drift in the
arc.** ★ **The rule this should become: the slice that MEASURES a spec's drift corrects it fact-only, in place, in the
same slice. "Report it" is not a correction — the next reader acts on the instruction, not on the report.**

#### The four commissioned defects, each verified against the code (V1), not against the brief

| # | §  | what it still instructed | what shipped |
|---|---|---|---|
| ① | §7 + §10.1 table | the vendor's **bare-`INPUT` one-shot polarity probe**, presented as live guidance *"V3 — polarity is auto-detected"* — ⚠ **the section already knew about the 3.2 inversion and still prescribed the unsafe form**; the table row also said *"nominal ACTIVE=LOW"* | the **two-pull** probe (`INPUT_PULLUP` then `INPUT_PULLDOWN`), agreement ⇒ polarity known, disagreement ⇒ **floating ⇒ REFUSE** (`-1`, panel `--`), plus the fail-safe park `kAdcCtrlFailsafePark = LOW` ([[B123]] round 2) |
| ② | §7 + §10.1 table | `5.42` as a **divider ratio / per-revision property**, including a closing sentence *"the divider is a per-revision property"* | `kVbatAdcScale` — a **combined empirical ADC scale**: the physical divider is 390 k/100 k ⇒ **4.9**, and 5.42 = 4.9 × ≈1.106, so ~10.6 % is **ADC calibration** ([[B126]]) |
| ③ | §4 diagram | failure → **`NOT HEARD`**; a `REPLY` accepted from ***"any state"***; *"sticky until acknowledged (**double**)"*; and **no `failed` arm at all** | headline **`NOT RELAYED`** (B117, ledger §1.2) with the enum still `Emergency::not_heard`; the reply whitelist is `firing · blocked · picked_up · not_heard · reply` **and `_tries != 0`** **and** §4.4's team scope; the exit is a **short** press once the outcome was **presented** (B71 + §B102) — **`double` does nothing** (R2); `failed` is a terminal retained arm |
| ④ | §12 checklist | **`NOT HEARD`** plus hard-coded **`3.0 s` / `3.6 s` / `10 s` / "3 retries"** | named constants only: `kArmToFireMs` (= `InputCfg::fire_ms`), `channel_min_interval_ms`, `kEmgMaxTries` — and *"3 retries"* also **contradicted §4's own binding reading** ("three transmissions, not three retries") |

★ **On ①, the distinction that must survive:** `kAdcCtrlFailsafePark` is **NOT** the hardcoded polarity §7 forbids —
measurement polarity is still **detected**, and the constant is consulted **only** where detection already failed
(probe checks **P6f**/**P8o** pin that). The spec now says so in the body, because otherwise a later reader "restores"
the defect as a spec violation. ⚠ **Residual carried, not claimed away:** LOW is documented-inactive for **V3.2 and
later only**; a pre-3.2 board inverts it and **only the bench closes it — guide `H9-05` part C, script `8.31`.**

#### ⛔ AND THE SPEC ASSERTED SOMETHING UNTRUE ABOUT ITSELF

The [[B117]] correction block claimed *"Every `NOT HEARD` elsewhere in this spec names the model STATE
(`Emergency::not_heard`), which is unchanged."* **Counted before editing: nine occurrences of the display-form string —
three inside that correction block (legitimate: they name the superseded string in order to supersede it) and SIX in
live guidance** (§2.1 rule 2, the §4 diagram, the §4 retry-bound sentence, the §12 checklist, the §12 `unsealable`
acceptance case, the §13 **B38** row). **Not one of the six spelled the enum.** A document asserting a false claim about
its own contents is the worst version of this defect class, and the claim was **never verified when written**.

#### ★ THE SWEEP — derived, not taken from the brief. Four findings the brief did not name

1. ⛔⛔ **A STALE `⛔ GATED` BANNER, and the highest-authority sentence in the document was a prohibition on shipped
   work.** §13 read *"`B38`/`B39`/`B40` **must land first**"* and closed *"**Do not implement the emergency outcome
   path** against the current core contract."* **All three are `[x]` in this register's checklist, landed 2026-08-01**,
   and the outcome path is implemented and bench-ready. This is [[B121]]'s shape one document over. The four
   `(needs Bnn)` markers in §12 were stale with it. ⇒ corrected; the rows kept as the record of what each prerequisite
   *was*; **B39's still-live residual (closed as an interim only) stated explicitly** rather than smoothed over.
2. **`MR_UI_BLANK_MS` — a constant named twice in the spec that EXISTS NOWHERE IN THE TREE.** Grepped: **9 hits, all in
   docs, 0 in code.** The shipped constant is `src/firmware_ui_model.h`'s **`kBlankMs`**. ⇒ both spec sites corrected.
   ⚠ **The other 7 hits are in the plan (3), the bench guide (2) and the bench script (2) — OUT OF SCOPE here and
   therefore OWED**, and worth registering rather than mentioning: a bench entry naming a non-existent constant sends
   a tester looking in `platformio.ini` for a value that is in a header.
3. **Six more restated timings**, each replaced by its constant: §4's blocked-backoff pair (`kBlockedBackoffMinMs` /
   `kBlockedBackoffMaxMs`), §4.4's arm hold (`kArmToFireMs`), §4.3/§5's `15 s` blank (`kBlankMs`), §5 + §12's
   re-restated `120000 → 30000` (`kEmgHoldMs` — ★ **the same "correct on the day" trap [[B120]] documents: the earlier
   correction of "120 s" restated the NEW digits**), §7 + §12 + §13's `30 s` battery cadence (`kBattPeriodMs`), and
   §4's `channel_min_interval_ms 10000`. Also §14 Q2's *"is 3.5 s the right arm time?"*.
4. **§13's UI-9 slice row still described the superseded reader** (*"auto-detected ADC_CTRL polarity"*), i.e. defect ①
   surviving in the slice table after §7 was fixed — the [[B128]] shape reappearing **inside the same document**.

#### Premises that turned out WRONG — including two of the dispatch brief's

- ⛔ **The brief's line numbers had all drifted** (`:661` → 661 held, but `:659`/`:446`/`:778` were 659/447/778 only
  because the file had not moved that day; re-derived every one anyway, per method).
- ⛔ **The brief said defect ③ was *"the diagram maps failure to `NOT HEARD`"*. It was worse than that on three further
  counts** — *"any state"*, the `double` exit and the missing `failed` arm — **none of which the brief named.** Deriving
  the arms from `draw_emergency` rather than patching the named string is what surfaced them.
- ⛔ **The brief asked me to *verify* the self-referential claim in ③ "and correct it if false". It was false**, and the
  count (6 live of 9) is the measurement.
- ⓘ **The brief's *"⛔ no plan"* boundary held**, but it means finding 2's four non-spec sites are **left open**.

#### What is NOT touched, and is still owed

**B112**, **B118** (authentication floor **unruled**), **B119**, the **REPLY-only wake**, `docs/2026-08-04-oled-handover.md`'s
three stacked `STATUS` headers, `CLAUDE.md`. ⛔ **No design decision was taken and no ruling was invented** — where a
correction would have needed one (B118's matcher, the REPLY-only wake, B123's pre-3.2 residual), the spec now points at
the ledger's §2 instead of resolving it. **Gate:** documentation-only ⇒ **no build gate is owed.** Tripwire only:
`pio test -e native` + the binary → **1373 / 74023 / 0**, unchanged.

### B132 — a time-multiplexed gateway can become a mobile home · NEW 2026-08-06 · **✅ FIXED 2026-08-06 (`lib/core`) — METAL-CONFIRMED; OWNER RULED GATEWAYS NEVER HOST**

**Metal evidence.** A dual-layer gateway identifies as node 6 on layer 6 and node 5 on layer 5, switching between
7.5 s receive windows. While listening on layer 5 its `routes` output nevertheless showed:

```
hosted-mobiles n=1
[hosted-mobile] hash=0xF7C0F666 local_id=254 pubkey=yes
```

The mobile with that hash reported `REGISTERED home=5`. This explains the failed by-hash lookup: the gateway had the
correct hosted-mobile record, but a time-multiplexed gateway is not continuously available on the mobile's PHY. A send
that happens to align with its window does not make it a valid home.

**Measured mechanism — this is not only an OFFER-side omission.**

| path | site | missing condition |
|---|---|---|
| DISCOVER → OFFER | `lib/core/node_join.cpp:321-324` | rejects mobile nodes and `!host_mobiles`, but not gateways |
| CLAIM acceptance | `lib/core/node_join.cpp:218-246` | checks the selected host id, then records `_mobile_reg` without rechecking host eligibility |
| presence / hosted-mobile response | `lib/core/node_join.cpp:554` | repeats the incomplete mobile / `host_mobiles` gate |
| live configuration | `src/firmware_config.cpp:194` | permits `cfg set host_mobiles on` for any role |
| gateway role | `lib/core/node.cpp:468-470` | derives `_cfg.is_gateway` from `n_layers == 2`; this is the authoritative role test |

Mobile discovery is leaf-exempt, so a gateway can hear the DISCOVER on its current PHY, offer with strong RSSI, and be
selected. Fixing only that offer would still let a delayed, stale or forged CLAIM addressed to the gateway install the
same invalid state.

**Impact.** The advertised home contract becomes intermittent: registration/presence, last-mile DATA, hash/pubkey proxy
answers and reverse ACK/delegation can all miss the gateway's other-layer window. The state looks healthy (`hosting=1`,
`pubkey=yes`) and therefore hides the actual reachability defect.

**Required invariant.** Define one shared predicate, equivalent to:

```
can_host_mobiles = host_mobiles && !is_mobile && !is_gateway && n_layers == 1
```

Use it for OFFER emission, CLAIM acceptance and presence/roster responses. A gateway must also refuse or make ineffective
`cfg set host_mobiles on`, and a transition into gateway role must clear any hosted-mobile and pending-host runtime state.
Compiling all mobile-host support out of gateway builds is a separate memory/architecture slice, not part of this
correctness fix.

**Required gate.** Pin all of the following:

1. A gateway hears DISCOVER on each served leaf and emits zero OFFERs.
2. A CLAIM naming the gateway, including a stale/forged one without a preceding offer, creates no hosted-mobile record.
3. A gateway does not answer the hosted-mobile presence/roster path.
4. A normal single-layer static node still offers, accepts, proxies and answers presence.
5. With a gateway and a static host both audible, only the static node offers and the mobile registers there.
6. `cfg set host_mobiles on` cannot make a gateway eligible, including after reboot or role transition.
7. Metal: after flashing the gateway, it shows no `hosting`; the previous mobile detects home loss, re-registers with an
   always-on static host, and by-hash delivery works through that host.

**✅ WHAT SHIPPED (2026-08-06).** ONE predicate, `Node::can_host_mobiles()` in `lib/core/node.h`, exactly as prescribed:
`host_mobiles && !is_mobile && !is_gateway && n_layers == 1`. Consumed at **four** sites, never re-spelled (U1) —
`handle_j`'s DISCOVER→OFFER responder, **CLAIM acceptance** (which previously had *no* eligibility test, only
`chosen_host_id != _node_id`), `presence_ingest_probe`, and **`presence_emit_roster`**. The roster gate sits at the
emit rather than at each caller because that one function is the choke point for **all six** roster-scheduling paths
(`evict_aliased_hosted_mobile`, the CLAIM record, both `presence_ingest_probe` answers, `presence_mark_deleg_fail`,
`presence_roster_fire`), so a future caller cannot route around the invariant. **C3 inertness:** `on_init` forces the
effective `host_mobiles` to `false` and clears `_mobile_reg_n` / `_notify_pending_n` / `_mobile_snr_q4` on **every**
leaf for any `is_gateway` node, so `cfg` reports the truth instead of a stale yes. **Console:** `cfg set host_mobiles on`
now refuses on a gateway (and on a mobile, with its own message); turning it *off* is always allowed.

★★ **THE `!is_gateway` vs `n_layers == 1` QUESTION — ANSWERED BY MEASUREMENT, AND BOTH CLAUSES WERE KEPT.** They are
**near**-redundant, not redundant. `node.cpp` holds the single authoritative derivation `_cfg.is_gateway = (_cfg.n_layers == 2)`,
so the identity holds **after a SUCCESSFUL `on_init`** — and there either clause alone suffices. It does **not** hold on
the **REFUSED** path, which is **reachable and NON-FATAL**: `_cfg = cfg` is assigned *before* `validate_gateway_layers`'
early return, the derivation and the force-off are *after* it, and `src/fw_main.cpp` merely prints
`config = REFUSED (invalid layer config — node NOT operational)` and **keeps running the node**. A dual-layer config that
fails validation therefore sits at `n_layers == 2` with `is_gateway` carrying whatever the caller supplied — on the
device a **separately-persisted NV byte** (`cfg.is_gateway = nv.is_gateway`, distinct from `nv.n_layers`), so it can
legitimately be `false`. In that state `!is_gateway` **passes** and only `n_layers == 1` refuses. ⇒ **`n_layers == 1` is
the strictly stronger, load-bearing clause**; `!is_gateway` is kept because it is the field every other gateway site
reads. ⚠ And it must read **`_cfg.n_layers`, not the runtime `_n_layers`** — the latter's write is also after the early
return, so it reads `1` there and would wrongly pass. Both facts are documented at the accessor, and **§B132/6 asserts
each clause in the state that isolates it**, so neither can be deleted as dead weight.

**GATE — 7 new cases, and the first version of them COULD NOT FAIL.** Reverting *both* gateway clauses left every
gateway test green, because `on_init`'s force-off short-circuits `host_mobiles &&` before the clauses are reached: **the
two defences masked each other.** Fixed by having every gateway test force `host_mobiles` back **on** after `on_init`
(which is also gate item 6's property), and by exercising the probe/roster gates through a **role transition** that
leaves a genuine hosted entry in place — without one, `presence_emit_roster`'s `_mobile_reg_n == 0` early-out makes the
assertion vacuous. **Full mutation matrix, every clause and every site RED:** drop `!is_gateway` → §B132/6 ·
drop `n_layers == 1` → §B132/6 · drop both → 5 cases / 15 assertions · drop `host_mobiles` → §B132/4 ·
drop `!is_mobile` → §B132/4 (a **pre-existing** coverage hole this slice closed) · remove the CLAIM gate (*tempting
wrong fix #1*) → §B132/2 only · remove the OFFER gate (*tempting wrong fix #2*) → 5 cases · remove the probe gate →
§B132/3 · remove the roster gate → §B132/3 · remove the `on_init` force-off → 4 cases · hard-wire the invariant
`false` → **17 cases (4 §B132 + 13 PRE-EXISTING hosting cases)**, which is what makes the positive control credible
rather than self-referential.

⛔ **THE CORPUS WAS BLIND TO THIS DEFECT, MEASURED — the 0-mover result is CORRECT, not luck.** Across all 37 scenario
files **`s27_cross_layer_mobiles_meshroute` is the only one pairing a gateway with mobiles** (every other is
gateway-only ⇒ nothing to host, or mobile-only ⇒ the new clauses are no-ops). In s27 the gateway **G1** emits
**0 `mobile_offer_tx` and 0 `presence_roster_tx`**, while the nine single-layer nodes emit **11** and **33** — and
because all 36 streams are **byte-identical** across this fix, that count *is* the pre-fix count. G1 is fully live
(436 events; it bridges, beacons, and does answer mobile-plane frames — `mobile_layer_answer` ×5,
`xl_mobile_resolved` ×7), so this is not an inert node: **the scenario simply never lets a gateway win a home**, which
is precisely why this needed a bench to find. ⓘ **Owed, not claimed fixed here:** a corpus scenario in which a gateway
is the strongest audible candidate for a mobile. Without one, no simulator run can ever regress this.

**Residual metal half (M2).** Gate items 6 and 7 are bench-only and are now written up as **Part 10** of
`docs/2026-07-31-bench-test-script.md` (10.1 the console refusal — ★ note `src/firmware_config.cpp` is **not in the
native build**, `[env:native]` never compiles `src/`, so it has **no** automated coverage; 10.2 the home-loss and
re-home case, with **5 sends of 30 s spacing** because a single success is exactly what the intermittent gateway home
already produced). Both entries carry explicit failure shapes and a negative control.

#### ⛔ §B132b — REOPENED AND RE-CLOSED 2026-08-06 (same day, independent QA): **THE OFFER IS NOT TRANSMITTED WHERE IT IS DECIDED, AND THE TESTS THAT "PROVED" THE FIX ASSERTED THE WRONG EVENT**

Independent QA accepted the shared predicate as SOUND but refused the slice on two counts. Both were verified line by
line against the source and both held. ⇒ **fixed here, in place; B132 is one entry, not two.**

**⛔ HOLE — a STAGED OFFER survived the ineligibility and was transmitted.** The DISCOVER responder does not transmit
the OFFER: `node_join.cpp:~377` hands it to `jtx_stash_arm` (the §S6/QA-3b de-storm), which arms
`kMobileOfferBackoffTimerId` for a jitter of **100..1000 ms** (`protocol::join_offer_backoff_{min,max}_ms`). The timer
handler in `node.cpp` then fired it **unconditionally** — `jtx_fire(_active->_pending_offer, …)`, no eligibility
re-check. And `on_init`'s gateway cleanup cleared `_mobile_reg_n` / `_notify_pending_n` / `_mobile_snr_q4` but **not**
`_pending_offer_len`, its timer, or the roster coalesce/echo flags. ⇒ **eligibility was checked at the moment of
DECISION and never at the moment the frame LEFT.**
★★ **The irony, recorded because the lesson is the general one:** `node.cpp`'s own cleanup comment already said the
force-off *"is NOT a substitute for `can_host_mobiles()` at the decision sites"*. The author understood per-site
re-checking and simply **did not count the TIMER FIRE as a decision site.** It is one — it is the moment the frame
actually leaves.

**⛔⛔ AND THE FOURTH INSTANCE OF ONE DEFECT CLASS: A CONTRACT EVENT ASSERTING A PHYSICAL ACT, REACHABLE FROM A PATH
THAT TRANSMITTED NOTHING.** `MR_EMIT("mobile_offer_tx", …)` fires at `node_join.cpp:~372`, **immediately BEFORE**
`jtx_stash_arm`, and its own comment says why (*"The EMIT stays here (the OFFER is committed)"*). ⇒ **the event means
COMMITTED, not TRANSMITTED.** Every round-1 case counted that event and **none fired timer 80 or looked at
`hal.tx_frames`**, so not one of them could distinguish *"an OFFER went out"* from *"an OFFER was staged and then
correctly suppressed"* — the entire question. **Prior instances: `emit_hash_query` · `tx_initiating` ·
`tx_with_retry`/`DeviceHal::tx`.** ⇒ **the standing rule, fourth confirmation: assert the SIDE EFFECT — the parsed
frame on the wire — never an event or a flag.** ⚠ Nor `Hal::cancel`: `TestHalBase::cancel()` is a **no-op**, so a
cancel can never carry a native assertion either; the case must FIRE the timer and read the wire.

**⛔ AND round 1's claimed role-transition test never executed the implemented cleanup.** `§B132/3` mutated
`n_layers`/`is_gateway` through `mutable_config()` and deliberately left the registry present ⇒ it tested the
**predicate**, not `on_init`. **The cleanup had never run in any test.**

**✅ WHAT SHIPPED (§B132b).** ★ **A `can_host_mobiles()` re-check at the OFFER's TRANSMISSION BOUNDARY** — the
`kMobileOfferBackoffTimerId` arm in `node.cpp` — which **drops the stash** instead of transmitting; plus **one shared
cleanup, `Node::mobile_host_pending_clear()`** (`node_join.cpp`, U1): every leaf's `_pending_offer_len`,
`_roster_coalesce_pending` and `_roster_echo_pending`, and `cancel()` on both timers. Called from the boundary
re-check **and** from `on_init`'s gateway block, so the two can never drift. ⚠ **PER-LEAF, not `_active`** — the stash
lives in `LayerState` and a gateway owns **two**, while `jtx_fire` reads whichever leaf is active at fire time.
⇒ **the cleanup is HYGIENE; the boundary re-check is the GUARANTEE**, and it is the only defence on the
**refused-`on_init`** path (which returns before every clear with `n_layers == 2` intact).
ⓘ **THE ROSTER NEEDS NO EQUIVALENT, verified rather than assumed:** `presence_emit_roster()` already calls
`can_host_mobiles()` **at the emit**, which *is* its transmission boundary (`presence_roster_fire` → `emit`). The
OFFER had no such check — that asymmetry is exactly what this half fixes. Clearing the roster flags is therefore
hygiene and is never what suppresses a roster.

**★★ REACHABILITY — CORRECTED, AND THE QA BRIEF'S FRAMING WAS PARTLY WRONG (measured, `src/firmware_config.cpp` + the
`cfg` liveness flags).** The *gateway* transition as worded ("become a gateway during the jitter interval") is **NOT
reachable on a device**: `is_gateway` is derived in `on_init` only and `cfg set n_layers` is `live = false`
("reboot to apply") ⇒ becoming a gateway means a **reboot**, hence a fresh, already-empty `LayerState`. **What IS
reachable, with no reboot, are two LIVE knobs:** `cfg set mobile 1` (live; `role_set_refusal`'s O2 clause refuses only
while `mobile_reg_count() != 0`, and **a staged OFFER is not a hosted mobile**, so the guard does not see it) and
`cfg set host_mobiles off` (B3 opt-out, `persist = false`, live). Either flips eligibility inside the 100..1000 ms
window and the node then advertises itself as a home it is not. ⇒ **the hole is real and live; the `on_init` cleanup is
defence-in-depth** (a re-init, or any inconsistent runtime state), **and both halves are attributed separately by the
mutation matrix below rather than credited together.**

**GATE.** Native **1385 / 74126 / 0** (from 1380 / 74084 ⇒ **+5 cases, +42 assertions**), `error:` **0**; s18
**`1cd21235` / 271629 EXACT**; **36/36 corpus byte-identical, 0 movers**; `sizeof(Node)` **221088 unmoved** and **RAM
byte-exact on all six board/census envs** ⇒ zero new state; 5 board envs + `warning_census.sh` **174/178/178 @ 326**
unmoved; both probes green with controls red. **★ FOUR MUTATIONS, AND THE TWO DEFENCES ARE ATTRIBUTED INDEPENDENTLY —
NEITHER MASKS THE OTHER** (the trap round 1 fell into): remove the **boundary re-check** with the cleanup still in
place → **RED, `§B132b/3`** (the refused-`on_init` state, where the cleanup provably never ran) · remove the **`on_init`
cleanup** with the boundary re-check still in place → **RED, `§B132b/4`** (the node is eligible again at fire time, so
the re-check passes and cannot suppress anything) · remove **both** → **RED ×3** (`/1`, `/3`, `/4`) · make the
boundary re-check **always** suppress → **RED, `§B132b/2`**, the positive control that proves timer 80 really does put
a parsed J OFFER on the wire. ⛔ **Every one of the five new cases asserts the PARSED FRAME in `hal.tx_frames`; the
event appears only as a premise** — and `§B132b/1` pins the distinction itself (`mobile_offer_tx == 1` **and**
`count_j_offer_mobile(tx_frames) == 0` in the same breath).
⚠ **`§B132/3` was REWRITTEN, not deleted:** it now reaches its live-hosted-entry state through a **real, REFUSED
`on_init`** instead of two config pokes. It could not use a *successful* one — that now clears the registry
(`§B132b/5`), which would put `presence_emit_roster` back behind its `_mobile_reg_n == 0` early-out and re-create
round 1's vacuity. The successful path's cleanup is asserted by its own case instead.

⛔ **THE CORPUS REMAINS BLIND, RE-MEASURED THIS ROUND (not quoted):** in `s27` — still the only scenario pairing a
gateway with mobiles — the gateway **G1 (sim index 5) emits 0 `mobile_offer_tx` and 0 `presence_roster_tx`**, while
nine single-layer nodes emit **11** and **33**. And no scenario can change a node's eligibility between stage and fire
at all (there is no console in the sim). ⇒ **0 movers is the CORRECT result; green corpus is not evidence for this
class.** The owed scenario (a gateway that is the strongest audible candidate for a mobile) is **still owed** and
would still not cover the delayed-transmission half.

### B119 — `Push::enc`'s comment says the channel path is always cleartext; the code sets it · NEW 2026-08-06 · **OPEN — comment-only defect, deliberately NOT fixed by the finding slice**

**Found while writing [[B118]]'s matcher, and MEASURED against the source, not read from a doc (V1).**

| what | where | says / does |
|---|---|---|
| the comment | `lib/core/command.h:302` | `bool enc = false;` — *"§8b: msg_recv -> the DM was delivered SEALED (CRYPTED + opened); **channel_recv -> false (cleartext today)**"* |
| the code | `lib/core/node_channel.cpp:415` | `pu.enc = (enc != 0);` — *"§chan-crypt CL2a: the post arrived SEALED and `body` is the opened plaintext (**the field existed, hardcoded false for channels**)"* |
| the guard around it | `lib/core/node_channel.cpp:404` | only a **readable** post produces a `channel_recv` push at all — a plaintext post, or a sealed one **we opened**. An unopenable one produces `team_channel_no_key` instead and is never inboxed. |

⇒ **the `channel_recv` half of the comment is STALE**: `§chan-crypt CL2a` gave the channel path a real sealed-and-opened
signal and the line at `:415` even describes the old behaviour in the **past tense**, but the struct's own documentation
was never moved. ★ **The CODE is right — only the prose is wrong.** ⛔ **Fix the comment, never the logic** (the
[[B115]] precedent, where an inverted comment above `on_send_accepted` was corrected and the code left alone).

**Why this is registered rather than shrugged at.** [[B118]]'s §5.1 makes *"the post arrived SEALED and we opened it"*
the **only condition in the matcher that authenticates anything** — every other clause is observable to a passive
eavesdropper. An implementer who reads `command.h:302` and believes it will conclude the carrier does not exist on the
channel path, and will either **invent a second one** or **drop the condition**; dropping it is precisely the forgeable
matcher §5.1 withdrew. ⚠ Same class as [[B115]]'s *"a V1 comment that stated the exact REVERSE of its own code"* — but
this one sits under a security decision rather than a display one.

⛔ **NOT FIXED HERE, and the reason is scope, not doubt:** the slice that found it (2026-08-06) was **documentation-only**
— `lib/` was explicitly out of scope, and a one-word comment repair inside `lib/core` would have been a source change in
a docs slice (C1). It is a **one-line comment edit** for whichever slice next has `lib/core/command.h` legitimately open.
ⓘ **Inert for the gate by construction:** a comment in `lib/core` cannot move s18, but the edit still rides the normal
D1/D2 treatment of whatever slice takes it.

### B113 — `ChanState::waiting` is a DEAD STATE, so an accepted canned post never leaves `SENDING...` · NEW 2026-08-05 · found by independent QA on UI-7
**FOUND BY INDEPENDENT QA, RE-MEASURED BEFORE THE FIX** (this entry was written first — M1: *a bug found and not
registered is a bug found twice*). Three greps, on comment-stripped source so a comment cannot be counted as live code:

| measurement | command | result |
|---|---|---|
| assignments of the state | `sed 's://.*::' src/firmware_ui_model.h \| grep -c 'ChanState::waiting'` | **0** |
| references anywhere in the tree | `grep -rn 'ChanState::waiting' src/ test/ tools/ variants/ lib/` | **1** — `src/firmware_ui.cpp:481`, the renderer's `"SENT, waiting"` arm |
| the arms `on_send_accepted` actually has | `src/firmware_ui_model.h:445-449` | `emergency` (`++_tries`) and `dm` (`waiting_ack`) — **no `channel_canned` arm at all** |

⇒ `UiModel::_chan` has six writers (`take_send_request` → `submitting`, `on_send_refused` → `failed`, and
`on_channel_outcome`'s five kinds) and **not one of them is the acceptance**. An accepted canned post therefore stays on
`ChanState::submitting`, i.e. the panel reads **`SENDING...`** until either the `channel_sent` verdict arrives (up to
**~36 s** — `channel_reoffer_team_max_retries`(3) × (`channel_reoffer_delay_ms` 10000 + `channel_reoffer_jitter_ms`
2000), protocol_constants.h:449-464) or the sub-view's own `kBlankMs` (**15 s**) auto-exit closes it first. On the
common path the modal is GONE before any verdict, so the user's only feedback for a successful send is a spinner that
never resolves.
⇒ It **contradicts the shipped bench requirement verbatim**: `docs/2026-08-04-heltec-v3-oled-ui-bench-guide.md:502`
(H7-01) states *"Expect `SENDING...` for one frame, then **`SENT, waiting`**"*, and H7-03 states the same for the DM.
The DM half WORKS (`on_send_accepted`'s `dm` arm writes `DmState::waiting_ack`, rendered at `firmware_ui.cpp:467`); the
channel half is the twin that was never written. Spec §3.4.1's table is the source of the requirement — `waiting_ack` =
*"accepted, `ctr` recorded"* → `SENT, waiting`.
★★ **WHY NO GATE CAUGHT IT, and it is the arc's recurring shape rather than an accident:** the state is *reachable only
from the renderer*, and `src/firmware_ui.cpp` is compiled by neither the native suite nor the simulator. Nothing in
`test/` names `ChanState::waiting` either, so the suite was green against an enumerator that no code path could produce
— the §B75 defect (`DmState::submitting` existed and nothing assigned it) one slice on, in the twin state machine.
⚠ It is **not** a false safety claim in the §2.1 sense — `SENDING...` under-claims — but it is a **success reported as
an unresolved attempt**, on the one screen whose whole job is telling the user whether their message went.

### ~~B113~~ ✅ **FIXED 2026-08-05** (the UI-7 QA fix slice, UNCOMMITTED)
`on_send_accepted` gains the third arm — `else { _chan = ChanState::waiting; }` — in the same `if/else if/else` shape
`on_send_refused` already uses for the same three kinds (U3). One line; no new state, no new enumerator, no wire.
**RED measured** (`simulation/BASELINE.md` §UI-7-FIX note): removing the transition = **2 cases / 6 assertions RED**;
the tempting WRONG fix (write `_chan` unconditionally, for every `SendKind`) = **2 / 4 RED** against the slot-isolation
control. The regression asserts all three things the fix must not break: ① the state moves, ② the normal tracker still
holds its handle (`match_channel_sent` still matches the accepted ctr — UI-4's slot discipline), ③ neither `_dm`,
`_emg` nor `attempts()` moves (§B84's ordering).

### B70 — the B67 fix guards a DRAINING call by calling it TWICE, silently voiding the two most safety-critical cases · NEW 2026-08-03 · OPEN (plan defect, 4 sites)
**MEASURED, with the instrument's own numbers** (`simulation/BASELINE.md`, 2026-08-03 §UI-3 note). The
`REQUIRE → CHECK + if-guard` rewrite is written as two calls at plan **:602-603** and **:612-613**:

```cpp
CHECK(m.take_send_request(req) == true);
if (!m.take_send_request(req)) return;   // ⛔ SECOND call
```

`UiModel::take_send_request()` **drains** — it clears `_emg_req_pending` / `_req_pending` and hands over the slot. So
call 1 consumes the alarm and returns `true`, call 2 returns `false`, the guard `return`s, and **doctest reports the case
PASSED**. Measured on *"attempts are counted on ACCEPTANCE"* + *"exactly THREE accepted transmissions, then sticky NOT
HEARD"*, same binary, same filter: **2 assertions with the plan's form vs 11 with one call** — the THREE-transmissions
case executed a single CHECK and returned, never counting an attempt and never reaching `not_heard`.

⇒ **Correct shape** (now used throughout the suite and stated in the test file's header):
`const bool got = m.take_send_request(req); CHECK(got == true); if (!got) return;`
⚠ **Scope:** the two-call form appears at the **four `take_send_request` sites** (plan :602, :612, and the UI-2 site the
same rewrite touched). **Task 4's four guards at :894 :913 :930 :933 are SOUND** — they call the consuming matcher once,
inside the `if`, with no preceding CHECK. Do not "fix" those. ★ **The general rule this earns: never call a consuming
API in an assertion that is followed by a guard on the same API.** The plan itself was not edited (forbidden).

### B71 — spec §4's `double` was BOTH acknowledge and re-fire, so a sticky emergency had no exit · NEW 2026-08-03 · ✅ OWNER-RULED 2026-08-04, deferred to UI-6
✅ **RULED 2026-08-04 (owner): after the emergency is sent AND ITS RESULT SEEN, the next SHORT PRESS restores the normal
cycle.** Sticky until then; **`long` re-fires; `double` gets NO emergency job** (both its §4 duties withdrawn). Safe
because the waking press is consumed (spec `:378`), so the result is always displayed before any press can dismiss it.
**Deferred to UI-6**, which owns the implementation; Task 3's `_emg` exit path is unchanged. Full table: the plan's B71 block.
✅ **THE RULING IS TRIMMED 2026-08-05, OWNER AGREED — see [[B100]].** The exit table's *"final `blocked`"* row was
**VACUOUS** (the blocked arm always re-arms a retry, so a `blocked` alarm is never *final*), which made the ruled set read
as **five** states beside §B78's `failed` when only **four** are reachable. The phantom member is removed from the plan's
table, which now reads `picked_up` / `not_heard` / `reply` / `failed` — exactly what `emg_outcome_retained()` implements.
★ **No exit logic changed**: this removed a phantom obligation from the document, not a behaviour from the code.
**Found implementing UI-3** (`simulation/BASELINE.md`, 2026-08-03 §UI-3 note). Spec §4's diagram says *"sticky until
acknowledged (**double**)"* and §4 line 299 says *"a sticky `NOT HEARD` the user can **re-fire with `double`**"*.
**Neither is built.** A `double` in `picked_up` / `not_heard` / `reply` falls through to `activate()` — a compose modal,
or nothing — and **no code path returns `_emg` to `idle`** except a fresh `long_fire`.

⇒ The model is not stuck (`_st.screen` still cycles), but **spec §5 rules that "the next press restores the emergency
screen, not the cycle"**, so UI-6's render policy inherits a state with **no exit condition** and the device shows a
resolved alarm indefinitely. ⚠ **The two spec sentences ask the same gesture for different things** — *acknowledge*
(clear to idle) vs *re-fire* (send again) — which is why UI-3 guessed neither. ⓘ A **long-press** re-fire does work and
is pinned (*"a fresh long_fire re-arms the alarm from a sticky NOT HEARD"*: `_tries` resets to 0), so the user is never
stranded without a way to raise a new alarm — only without a way to dismiss an old one.

### B72 — `SendOutcome` had no `channel_failed`, so a pre-enqueue SEAL FAILURE left the alarm on `SENDING...` for ever · NEW 2026-08-03 (cited by the plan, never registered) · ✅ **FIXED 2026-08-04 (UI-3 QA, UNCOMMITTED)**
⚠ **This entry did not exist.** The plan's Task-3 block cites `§B72` in a code comment (plan `:756`, `:773`) and the
register's highest number was **B71** — so the finding was carried only inside the document it was a defect *in*. **M1:
a bug found and not registered is a bug found twice.** Numbering here is deliberately the plan's, not a fresh number.

**MEASURED** (`simulation/BASELINE.md`, 2026-08-04 §UI-3-QA note). Task 4's tracker returns `channel_failed`
(plan `:1052`) and a Task-4 test checks `Kind::channel_failed` (plan `:968`), but the eight-kind type had **zero**
occurrences of it ⇒ `'channel_failed' is not a member of 'mrui::SendOutcome'`. The behavioural cost, not the typing
one: a channel post that fails **before enqueue** (seal failure) returns `queued` with `ctr == 0`, so
`match_channel_sent` can never fire for it and **no outcome ever reaches the model** — a distress alarm displays
`SENDING...` indefinitely with two of its three transmissions unspent.

⇒ **Fixed as the NINTH kind, TERMINAL** (`_emg = Emergency::failed` + the reason), never a retry: `unsealable` /
`no_location` are documented PERMANENT in `command.h`, so a retry burns the alarm budget and still fails. It lands
exactly where the *synchronous* refusal lands, because it is the same event arriving late. ★ **And the class is now
build-enforced:** `on_outcome`'s switch lists all nine kinds with **no `default:`**, so a tenth kind is a hard
`-Werror=switch` error — proven by probe (`error: enumeration value 'channel_tenth_kind' not handled in switch`).

### B73 — an async failure reason was DISCARDED, so `refuse_reason()` was pinned at `other` · NEW 2026-08-03 (cited by the plan, never registered) · ✅ **FIXED 2026-08-04 (UI-3 QA, UNCOMMITTED)**
⚠ **Same as B72: cited by the plan (`:760`, `:767`), never written here.**

**MEASURED.** `SendOutcome::dm_failed()` took no reason and `on_outcome`'s `dm_failed` case changed only `_dm`, so
`refuse_reason()` returned its construction default `other` for **every** asynchronous DM failure. Spec §2.1 rule 6
requires the opposite in as many words: *"The full `SendFailReason` reaches the UI, not a boolean … Collapsing them
makes `NO CONFIRM` unreachable and discards the one thing that tells the user what to do next."*

⇒ **Fixed on both axes.** `SendOutcome` carries `FailReason reason`; `dm_failed(r)` / `channel_failed(r)` **require**
it (no defaulted `none` — a caller holding a reason must not be able to drop it silently, proven by a negative-control
compile: `error: no matching function for call to 'mrui::SendOutcome::dm_failed()'`); `note_failure()` records the core
reason **verbatim** in `_fail` (new `fail_reason()` accessor) **and** maps it to the compact `RefuseReason` the panel
already reads. ★ **Why both and not one:** `RefuseReason` cannot be a mirror of `SendFailReason` — `parser` has no core
equivalent (the line never became a `Command`) and mirroring 18 append-only core enumerators is the parallel-enum fork
U1 forbids; but mapping *alone* discarded 12 of 18 reasons, which is the defect. Mapped: `unsealable` /
`no_location` / `queue_full` — the three whose **remedy differs**. ⓘ A synchronous `on_send_refused` now **clears**
`_fail` to `none`, so a parser refusal cannot inherit the previous send's core reason.

⚠ **Consequence for Task 4, measured:** the plan's tracker calls the reasonless `SendOutcome::dm_failed()` at
plan `:1066`. It must become `dm_failed(r)`. See **B76**.

### B74 — ★★ a VALID retry deadline collided with the "no deadline" SENTINEL, blocking a distress alarm FOR EVER · NEW 2026-08-03 · ✅ **FIXED 2026-08-04 (UI-3 QA, UNCOMMITTED)**
**MEASURED, with the probe in the suite.** `src/firmware_ui_model.h` reserved `0xFFFFFFFF` as *"no retry armed"*
while computing `_retry_at_ms = now_ms + d` **with no bound**, so ordinary inputs could produce exactly the reserved
value — `now = 0xFFFFF000`, `next_ms = 0xFFF`. `tick_emergency`'s guard (`_retry_at_ms != _no_deadline`) then refused
to examine the deadline at all, so `Emergency::blocked` **never** returned to `firing`: the alarm was stuck, on the
safety path, with two of three transmissions unspent and no user-visible reason. Reproduced by reverting only the
guard: `CHECK( retried == true )` reads `CHECK( false == true )`.

⇒ **Fixed with a separate `bool _retry_armed`, so NO arithmetic value is reserved** — `retry_at_ms()` is now
meaningful *while the state is `blocked`* rather than sentinel-encoded, which is also what makes it honest for UI-6.
Pinned by *"a retry deadline of 0xFFFFFFFF is a DEADLINE, not 'no deadline'"* (one tick early → still blocked; at the
deadline → retries, `attempts()` still 1). ★ **The general rule this earns: never encode "absent" as a value a live
computation can reach.** The pre-existing *"retry deadline is wrap-safe"* case passed throughout — it uses
`blocked(0x2000)`, which wraps **past** `0xFFFFFFFF` and so never lands on it. **Wrap-safety and
sentinel-collision are different bugs; a wrap test does not cover this one.**

### B75 — `DmState::submitting` was UNREACHABLE, and the comment saying otherwise was false in both halves · NEW 2026-08-03 · ✅ **FIXED 2026-08-04 (UI-3 QA, UNCOMMITTED)**
**MEASURED.** Spec §3.4.1's table requires `submitting` → `SENDING...` when *"the command was handed to `dispatch()`"*.
The enumerator existed and **nothing assigned it** — `grep` found one declaration and zero writes — so a DM read `idle`
from the moment it was queued until its result came back. ⚠ **And the in-source note claimed it was *"set at submit
time … written-but-unread here on purpose"*, which was wrong twice over: it was neither written nor read.** A comment
that describes a state as deliberately-deferred is the reason nobody looks for the missing write.

⇒ **Fixed in `take_send_request()`**, which *is* the hand-off (`firmware_ui.cpp` performs the send in the same service
pass), and only for `SendKind::dm` — exactly the scope `on_send_accepted` / `on_send_refused` already use. ★ Chosen
over a new `on_submit()` API precisely because the plan's Task-7 code never calls one, so a new hook would have left
the state unreachable *again*, one slice further on. Pinned by *"draining a DM request enters SUBMITTING, and only a DM
does"* (with `clear_dirty()` first, so the repaint measured is the drain's and not the gesture's). The false comment is
corrected in place; `_last_try_ms` is now the **one** genuinely written-but-unread field.

### B76 — the plan's **Task 4** block cannot compile: 6 errors, one of them the B70 fix's own fallout · NEW 2026-08-04 · OPEN (plan defect, Task 4 is next)
**MEASURED, not read** — the plan's Task-4 tracker and test blocks were extracted verbatim into a scratch TU (nothing
landed in the repo) and compiled against the fixed Task-3 header with the project's exact native flags: **rc=1, 6
errors.** They are three distinct classes and every one stops Task 4 on its first run:

| plan site | error | class |
|---|---|---|
| `:987` | `redeclaration of 'const bool ok'` | ★★ **the B67→B70 fix's own fallout**: the case declares `const bool ok` **twice in one scope**. The B70 rewrite was applied per-site without checking the second one collides with the first |
| `:937 :938 :939 :945` | `cannot convert 'bool' to 'meshroute::SendFailReason'` ×4 | the **test block disagrees with the tracker signature it is testing**: `match_dm(ctr, dst, acked, SendFailReason, out)` (plan `:1060`) vs `match_dm(900, 174, true, false, o)` / `/*no_pubkey=*/true`. `bool → enum class` is not implicit |
| `:1066` | `no matching function for call to 'mrui::SendOutcome::dm_failed()'` | inside the plan's **own tracker**, and it contradicts the plan's **own §B73**: the reason must be threaded, so this is `dm_failed(r)` |

⇒ **Fix in the plan before Task 4 is dispatched**, not in the coder's head. ⓘ The four `bool` sites are the *older*
signature surviving a later edit to the implementation block — the same "prose updated, code block behind" pattern as
the `retain()` disagreement UI-3 found, and as §B73 itself. ★ **Rule: when a plan block's signature changes, compile
its test block against it — the two halves of a plan are not type-checked by being in the same file.**

### B77 — RECORDED (gate methodology): the anchor-table heading grep matches the PROSE THAT TELLS YOU TO RUN IT · NEW 2026-08-04
**MEASURED, and it produced a 1-row extraction on the first attempt.** The 2026-08-03 §UI-3 note fixed a
line-number-based extraction by ruling *"locate the anchor block by its heading text
(`grep -n "36/36 corpus — 0 assertion failures"`)"* — and that very sentence, sitting **above** the table in this
same growing file, is now the **first** match. My loop anchored on it and extracted **1 row** (a stray `#if` line)
instead of 36.

⇒ ★ **Anchor on the heading FORM, not just its text: `grep -n "^### 36/36 corpus — …"`.** Caught only because the row
count was printed beside the result, which is now the third consecutive slice where *printing the denominator* is what
made a vacuous check visible. ⓘ Same session, same class: a `.d`-file census written as
`grep -rl X $(find … -name '*.d')` **silently searches the whole tree** when `find` returns nothing (the two nRF52 envs
emit no `.d` files at all), reporting `15` citations that do not exist. **Guard command substitutions that can be
empty.**

### B78 — a TERMINAL failed alarm blanked fastest of all the emergency outcomes · NEW 2026-08-04 · ★ OWNER-RULED and ✅ **FIXED 2026-08-04 (UI-3 QA, UNCOMMITTED)**
★★ **RULED 2026-08-04 (owner):** `Emergency::failed` **joins the retained set** and holds for `kEmgHoldMs` like every
other retained outcome — it is the state that says *the alarm did not go out*, so it must not be the fastest to go dark.
⇒ **AND `kEmgHoldMs` is re-ruled 120000 → 30000** in the same breath. ✅ **Implemented** (`on_send_refused` needed a
`now_ms` — `_last_input_ms` would timestamp the GESTURE, not the later refusal — plus `retain()` on both the synchronous
and `channel_failed` paths, and `failed` added to `hold_active()`). ⓘ Composes with **B71**: `failed` being retained is
what lets the next short press acknowledge it, so the hiker is never trapped on a failure screen.
**MEASURED / created by the B72 fix.** `hold_active()`'s retained set is `arming · firing · blocked · picked_up ·
not_heard · reply` — spec §4.3's table lists `long_fire` plus *"every retained outcome"*, and `failed` is in neither.
So both routes into `Emergency::failed` — the synchronous `on_send_refused` (pre-existing) and the new
`channel_failed` — left the panel on the ordinary `kBlankMs` blank timer, while a `blocked` or `not_heard` alarm held
it for a full `kEmgHoldMs`. ⚠ Stated as CONSTANTS on purpose: `kEmgHoldMs` was 120000 when this was measured and 30000
by the time it was fixed, in the same session.

✅ **FIXED 2026-08-04 as ruled, in three parts, each with a probe that fails when reverted in isolation:**

| part | probe (revert just this) |
|---|---|
| `failed` added to `hold_active()`'s set | **2 cases / 4 assertions fail** — both failure paths blank on the first tick |
| `retain(now_ms)` on both paths, **`now_ms` a PARAMETER** | anchor it on `_last_input_ms` instead ⇒ **1 case / 2 assertions fail**: the refusal lands a full window after the gesture, so the gesture-anchored deadline is already spent |
| **only** the emergency branch retains | retain for every kind ⇒ **1 case / 1 assertion fails**: a mid-alarm DM refusal must not push the alarm's deadline out |

★ **Tests are written against `kEmgHoldMs`, never a literal** — the value was re-ruled mid-slice and a literal would
have pinned the old behaviour while still passing. The constants tripwire is the ONE place the number appears.
ⓘ **Superseded reasoning, kept as the audit trail:** this entry originally argued the fix should NOT be applied, because
with `failed` outside the retained set a `retain()` call would be a **dead term** (the `arming` class from the §UI-3
note). That was right about the mechanics and wrong about the scope: the cure is to fix *both* halves at once. The
counter-argument was that a refused
send never went out. **One line either way (`failed` into the retained set, plus `retain()` at both sites, or a
sentence in §4.3 saying 15 s is intended). Owner/spec ruling wanted; UI-6 is the natural home.**

### B133 — durable single-record inbox delete (spec §6.2) · NEW 2026-08-06 · **✅ LANDED (`lib/core` + one console verb; UI-7D slice A, UNCOMMITTED)**

**THE CONTRACT SLICE B CONSUMES — copy it, do not re-derive it.**

```cpp
// lib/core/inbox.h
enum class InboxEraseResult : uint8_t { erased = 0, not_found = 1, io_error = 2 };
InboxEraseResult Inbox::erase(InboxKind kind, uint32_t seq);   // identity = the PAIR
```

| outcome | when | what §3.5 renders |
|---|---|---|
| `erased` | the tombstone was APPENDED and is durable; the record is gone from every future `pull()` | close the modal, rebuild the list |
| `not_found` | no such live record in that kind's store — evicted by the bounded ring, `seq == 0`, or already deleted | **`MESSAGE GONE`** — and it must never affect another row |
| `io_error` | the append failed, the inbox is unwired, or the tombstone cap is full | **`DELETE FAILED`** — stay in the modal; nothing was deleted |

⛔ **A bool cannot express this**, which is why the API does not return one, and why the console verb reports
`"result":"erased|not_found|io_error"` verbatim rather than an `ok`/`err`.

**THE THREE HAZARDS, ANSWERED.**

1. **ORDERING — a single streaming pass cannot filter a marker it has not seen yet.** A tombstone is appended
   **after** its target, so `pull()` runs a bounded **PRE-PASS** per store (`read_since` with the *same* `since`,
   collecting tombstone targets), then streams. Cost: the store is scanned **twice per pull** (a console/UI
   operation, never a MAC path) and **128 B of stack** (`inbox_max_tombstones` = 32 × `uint32_t`, one array reused
   for both stores). **RAM +0 bytes on all six envs, measured** — nothing in `.bss`, nothing in `Node`.
   Pinned by `§3.5/2`, which asserts the marker is physically the **last** record in the log and the target is still
   filtered; reverting either the pre-pass or the filter reddens **7 cases / 44 assertions**.
2. **LIFETIME — markers are bounded twice over.** They are ordinary records in the same drop-oldest ring, and since
   a marker is always appended after its target it is always **evicted after** it: garbage cannot outlive the ring.
   On top of that the **writer** refuses the (n+1)-th tombstone with `io_error`, which is what makes the reader's
   fixed array **overflow-proof by construction**. 32 is not arbitrary: `MR_RAM_INBOX_SLOTS` is 32, so on the
   `FixedInboxStore` (every ESP32 target — see [[B134]]) the cap can never bind before the ring evicts; on the
   512 KB QSPI store it is a real, declared product limit.
3. **ENCODING — the record's `type` byte, value `0xFE`, and NO store-format bump.** Verified against
   `lib/core/frame_codec.h`'s `DataType` (V1, at the codec, not the comments): **1..19 allocated, sequentially from
   1** — so unlike the DATA flags byte and `q_opcode`, this space is **not** under pressure and the top of it is
   free. `0xFF` was deliberately **not** used: it is the erased-flash byte, and a value a blank region could decode
   into is a poor discriminator (the segment framing already stops that, but the value should not depend on it).
   ★ **No bump was taken and the reason is stated rather than dodged (M3):** the record layout is **unchanged**, so
   every already-stored record stays parseable — a bump would wipe on-node history to buy nothing.

**WHY THERE IS NO NEW `InboxStore` VIRTUAL.** The brief's hazard — *"adding a virtual changes EVERY implementer; a
missed one is a build break, a wrongly-defaulted one is a silent no-op delete"* — is answered by **adding none**.
`erase()` is composed from `read_since` + `append`, the two operations **all four implementers** already provide
(`FixedInboxStore`, `SegmentedInboxStore`, `DeviceInboxStore`, `RamInboxStore` — the set derived by grepping for `: public InboxStore`, not typed), so
§6.2's *"every backend must implement the same contract"* is satisfied **above** them rather than five times over
(U1). Crash-safety is then the append's own: the marker either lands or it does not, and nothing else is mutated.

**A DELETE IS NOT A WIPE.** The marker consumes a sequence of its own, so history keeps a **hole**, `next_seq` never
regresses and a sequence is never reused; the read cursor and the storage epoch are untouched, so the companion is
never pushed into resetting both cursors (§6.2, pinned by `§3.5/9`).

**MUTATION MATRIX — 9 mutations, applied by exact unique string replacement (the harness aborts if a replacement
matches 0 or >1 sites), full rebuild + run each, then reverted; `lib/core/inbox.cpp` md5-verified restored.**

| mutation | RED |
|---|---|
| **M1** target filter removed (marker still hidden) | 7 cases / 44 asserts |
| **M2** the marker itself leaks as a message | 3 / 5 |
| **M3** the pre-pass collects nothing (the ORDERING machinery dead) | 7 / 44 |
| **M4** `erase` reports `erased` and writes no tombstone | 7 / 60 |
| **M5** identity collapsed to `seq` alone | **1 / 2 — `§3.5/3` only**, the cross-kind case |
| **M6** `not_found` conflated with `io_error` | 2 / 4 |
| **M7** the writer's tombstone cap removed | 1 / 2 — `§3.5/8`, the resurrection assert |
| **M8** the append's verdict ignored | 1 / 1 — `§3.5/6` |
| **M9 CONTROL** the filter suppresses everything | 4 / 5 **pre-existing** cases ⇒ the suite really can see records |

★ **Every case asserts the SIDE EFFECT — the record's absence from a real `pull()` sweep, including across a
simulated reboot (`on_init` re-run over the same store objects) — never a return code alone.** A delete that returns
`erased` and leaves the record readable is exactly the *"contract event asserting a physical act"* class this arc has
now hit four times, and **M4 is that defect made concrete**: it reddens 7 cases.

**M2 (metal-only) — bench Part 11**, because neither native nor the simulator compiles `src/`, and the native cases
run against a `std::deque` fake so *"the marker reached the medium"* is untested. 11.1 delete-survives-reboot,
11.2 never-reappears, 11.3 the three outcomes at the console, **11.4 the board control** — which exists because of
[[B134]]: on a Heltec, *"gone after a reboot"* is vacuously true.

#### ⛔ §B133b — REJECTED BY INDEPENDENT QA 2026-08-06, RE-CLOSED 2026-08-07: **THE MECHANISM WAS SOUND AND THE STORE UNDER IT WAS NOT**

Two correctness blockers were raised against the block above. Both are real; both were verified against the source
before being acted on, and **one of my premises above turned out to be false**.

**① THE DURABLE APPEND IS NOT POWER-FAILURE SAFE ⇒ `erase()` COULD REPORT `erased` WITH THE MESSAGE STILL VISIBLE.**
Header and body are two `seg_append` calls; a retry after a torn tail is consumed *as* the torn frame's body and its
tombstone becomes unreachable. ★ **PRE-EXISTING since 2026-06-12 (`git blame`), so it is registered separately as
[[B135]] and fixed there** — it affects **ordinary message appends**, not only tombstones, and folding a store
durability bug into a delete feature would misattribute both (C1). B133's mechanism needed no redesign; it needed a
store that keeps its word.

⛔ **MY FALSE PREMISE, quoted from the block above:** *"crash-safety is then the append's own: the marker either
lands or it does not, and nothing else is mutated."* The **first** clause is right; the **second** was never true —
a rotation on a full ring evicts the oldest segment before the write is attempted. The sentence is fenced
`⛔ SUPERSEDED` in `lib/core/inbox.h` and replaced with what the code actually guarantees. **This is the sixth
occurrence in this arc of a correction living beside live instructions instead of replacing them** — so the
operational text was rewritten, not annotated.

**② THE CONSOLE VERB ACCEPTED MALFORMED DESTRUCTIVE TARGETS** — `del_msg dm 1oops` deleted seq 1. [[B136]].

⚠ **AND THE THIRD FINDING IS ABOUT THE INSTRUMENTS, which is the one worth keeping:** B133's mutation matrix was
genuinely strong (9 mutations, all RED, with a vacuity control) **and it could not have caught either blocker**,
because the only fault knob available (`RamInboxStore::fail_append`) fails *before writing anything* and the
`src/` verb is not in the native build at all. ★★ **Before trusting a check, ask whether it COULD have failed.**

### B134 — the §3.5 delete is durable only on nRF52; every ESP32 inbox is a volatile RAM ring · NEW 2026-08-06 · **OPEN / PRODUCT QUESTION**

Measured at `src/fw_main.cpp:168-179`. `MRINBOX_QSPI_READY` (`QSPIFLASH=1`, nRF52 only) selects the durable
`DeviceInboxStore`; every other target — **including `heltec_v3`, `heltec_mobile` and `gateway_heltec`, the boards
the §3.5 modal is being built for** — gets `FixedInboxStore<MR_RAM_INBOX_SLOTS = 32>`, whose own header says
*"VOLATILE: history is lost on reboot"* and whose `set_epoch` exists to make the companion re-pull after every boot.

⇒ **On the UI's own target the delete is durable only until the next power cycle.** The [[B133]] mechanism is
correct on both stores; what differs is what the medium promises. Two consequences:

- a bench step *"the deleted message is gone after a reboot"* passes **vacuously** on a Heltec ⇒ bench **11.1 is
  restricted to a QSPI board** and **11.4** is the control that catches it being run on the wrong one;
- slice B's modal must not imply a permanence the store cannot provide on that board.

⛔ **Not fixed here.** Wiring a durable store on ESP32 is a storage port (a flash partition plus LittleFS/NVS
backends for `ISegmentStore` / `IMetaStore` — `SegmentedInboxStore` is already platform-neutral and would host it),
which is its own slice and an owner call.

### B135 — the durable segmented log tears MID-FRAME, and the next append makes a stored record unreachable · **PRE-EXISTING since 2026-06-12** · found 2026-08-06 by independent QA on [[B133]] · **✅ FIXED 2026-08-07 (`lib/core` + `src/`)**

**THE MECHANISM, verified in source (V1) and not from the comments.** `SegmentedInboxStore::append()` frames a
record as `[u16 framed_len][u32 seq][rec]` and writes it as **two calls**:

```cpp
if (!_records->seg_append(_meta.head_seg, hdr, 6)) return false;            // <- the header lands
if (len && !_records->seg_append(_meta.head_seg, rec, len)) return false;   // <- the body does NOT
```

`src/device_inbox_store.h` — the **live nRF52/QSPI backend**, a hand-maintained twin of the same logic — is
byte-for-byte the same shape. `read_since` already refuses a short frame (`if (fl < 6 || off + fl > n) break;`), so
**a torn tail on its own loses one record and nothing else.**

★★ **THE DEFECT IS THE NEXT APPEND.** Its bytes land immediately behind the torn header, which now measures long
enough to "contain" them. Worked example, measured by the new `§B135/1` case (records `a`, `b`, a torn 7-byte frame
for seq 3, then a retry `d` for seq 4): the reader emits seq 1, seq 2, then a **PHANTOM seq 3 whose body is the
retry's own length byte**, then reads `fl = 1024` out of the middle of the retry's header and stops. **Record 4 is
physically on the flash and permanently unreachable.** ⇒ ordinary inbox **messages** disappear, and:

⛔⛔ **THE [[B133]] CONSEQUENCE — A SUCCESS THAT ISN'T, THE FIFTH IN THIS PROJECT.** `erase()` is composed from
`read_since + append`, so a torn append is a torn *delete*. Header lands / body fails ⇒ `erase()` correctly returns
`io_error`, **but the retry** appends after the torn tail, the reader can no longer reach the retry's tombstone, and
`erase()` reports **`erased` while the message is still visible**. That is exactly the class this arc keeps hitting:
`emit_hash_query` → `tx_initiating` → `tx_with_retry`/`DeviceHal::tx` → `mobile_offer_tx` → **`erase() == erased`**.
★ **The pattern IS the finding: a return value or event that asserts a physical act, reachable from a path that did
not perform it.** Five instances, five different subsystems, one shape.

**★ ATTRIBUTION — PRE-EXISTING, and this was checked rather than assumed.** `git blame -L 154,190` on
`lib/core/segmented_inbox_store.h` puts the two `seg_append` calls and both bare `return false`s at **`c1dd1934`,
2026-06-12**; `src/device_inbox_store.h` (from which the lib/core file was ported) carried the shape earlier still.
⇒ **[[B133]] did not create this hole. It made it reachable in a newly dangerous way** — a *destructive* operation
reporting success — which is why it is registered here with its own id instead of being folded into the delete
feature (C1), and why the fix is stated plainly as affecting **ordinary appends, not only tombstones**.

**✅ THE FIX — SEAL-AND-ROLL, and the alternative that was rejected.** On a failed append the store measures the
head segment: if bytes actually landed it charges them to `_total` and **SEALS** the segment; the next `append()`
treats a sealed head exactly like a full one and **rolls to a fresh segment**. A torn segment is therefore never
appended to again, so the tear stays permanently at a segment's end — where `read_since`'s existing stop is
*correct* rather than a trap. ★ **The seal is re-derived at `begin()` by walking the head segment's frame chain**
(`head_tail_torn()`: the chain must consume the segment EXACTLY), because the power cut that tears a frame also
loses the RAM flag — the reboot arm is the whole reason a RAM-only seal would have been theatre.

⛔ **TRUNCATION WAS REJECTED, with reasons:** `ISegmentStore` has no truncate, and adding one is **a new virtual on
a HAL with a device implementation** — the exact hazard [[B133]] deliberately designed around; and append-only media
cannot un-write bytes at all, so a truncate-based design would be LittleFS-specific in a deliberately
medium-neutral interface.

⛔ **WHAT SEAL-AND-ROLL DOES NOT COVER — stated, because a recovery mechanism that implies more than it does is the
next bug:**

1. **No integrity check.** There is no per-record CRC. Sealing detects **short** frames, never **corrupt** ones: a
   tear that leaves a plausible `framed_len` followed by plausible bytes still parses.
2. **Damage to earlier records** in the same segment (a flash page rewritten under a brown-out) is not detected.
3. **The torn frame's bytes and the remainder of its segment (up to `inbox_segment_bytes` = 4 KiB) are wasted**
   until the ring laps.
4. **`save_meta()`'s return is still ignored** (pre-existing; deliberately not changed in this slice).
5. ★ **Rotation is NOT transactional.** If the append had to roll first, the roll may already have erased the
   OLDEST segment before the write failed. Obtaining a free segment in a full ring **is** the eviction, so it cannot
   be undone. ⇒ ⛔ **the documented guarantee was WRONG and is corrected, not re-worded away:** `inbox.h`'s
   `erase()` note said *"the tombstone either lands or it does not, and nothing else is mutated"*. The clause is
   fenced **⛔ SUPERSEDED** in place and replaced by what the code actually provides — **"on a failed append no
   previously readable record is corrupted or made unreachable"**, plus the explicit statement that a roll may have
   evicted the oldest segment (drop-oldest the next successful append would have done anyway). Nothing about the
   **target** record changes on `io_error`, which is the property §3.5 renders.

⚠⚠ **WHY THE EXISTING TESTS COULD NOT SEE IT — the arc's recurring shape, now at FIFTEEN:**

- `RamInboxStore::fail_append` fails **before writing anything**, so it can never produce a torn tail. ★ **A fault
  injector that cannot produce the fault is not a fault test.**
- the pre-existing `"a torn record at a segment tail is skipped"` case hand-writes a torn tail and **never retries
  after it** — it pins the harmless half of the behaviour and is blind to the dangerous half.

**✅ THE NEW INSTRUMENT: a MID-FRAME fault injector** (`test/fake_inbox_storage.h`) that fails a chosen `seg_append`
call **after letting `partial_bytes` of it land** — i.e. a real power cut between the header and the body (call #0 =
the 6-byte header, #1 = the body). It also exposes `fault_armed()` so every case **asserts the injector actually
FIRED** rather than assuming it did.

**SIX NEW CASES, and every one asserts the OBSERVABLE side effect — what a later read/`pull()` can still reach —
never a return code:**

| case | what it pins |
|---|---|
| `§B135/1` | header written, body failed ⇒ the tear is sealed and **the retry stays readable** (`{1,2,4}`, body `d` its own) |
| `§B135/2` | a **partial body** is sealed too — a short frame is still a torn frame |
| `§B135/3` | a failure **immediately after a rotation**: every previously readable record survives, in order, and the store keeps working |
| `§B135/4` | a **REBOOT between the two writes** — the object is destroyed, `begin()` re-arms the seal from the medium; also asserts a tear is **not** a wipe (no epoch bump) |
| `§B135/5` | ★ **VACUITY CONTROL** — a CLEAN store is never sealed (one segment, no roll). Without it, `head_tail_torn() == true` always would pass 1–4 while burning a segment per boot |
| `§B135/6` | ★★ **the case that closes the class** — a real `Inbox` over two durable stores: a torn tombstone returns `io_error` **and the target is still in `pull()`**; the retry returns `erased` **and the target is gone from `pull()` while the other record survives**; and it is still gone after a reboot |

**MUTATION MATRIX — exact unique string replacement (the harness aborts on 0 or >1 matches), full rebuild + run
each, reverted, md5 verified restored (`segmented_inbox_store.h` `c9820c79`, `firmware_config_parse.h` `62583a14`):**

| mutation | RED |
|---|---|
| **MA** the seal is never set (`_head_sealed = true` removed) | **4 cases / 8 asserts** — `/1 /2 /3 /6` |
| **MB** a sealed head does not force a roll | **5 / 10** — `/1 /2 /3 /4 /6` |
| **MC** `begin()` does not re-arm the seal | **1 / 2** — `/4` only, precisely the reboot arm |
| **MD CONTROL** `head_tail_torn()` always returns true | **1 / 3** — **`/5` ONLY**, which is what makes `/1`–`/4` non-vacuous |
| **PRE-FIX** the exact 2026-06-12 code restored (both bare `return false`, no seal, no roll, no re-arm) | ★ **5 / 10** — `/1 /2 /3 /4 /6`, i.e. **the shipped bug is measured, not argued** |

★ **MA and MB are the two halves of the defence and each is RED with the other INTACT** — neither masks the other
(the B132b standard). **MD is the mutation that proves `/5` is doing work**: it is the only one it reddens.

⚠ **DUPLICATION, recorded not silently accepted:** `src/device_inbox_store.h` is a hand-maintained twin of
`lib/core/segmented_inbox_store.h` (same logic, `qspi_*`/`ifs_*` seams instead of injected interfaces) and **it is
the live nRF52 backend**, so the fix had to land in both. A header comment now says so at the new member. ⛔ **The
dedup is NOT done here** — collapsing the twin onto `SegmentedInboxStore` is a refactor and C1 forbids folding it
into a fix. It is the obvious follow-up.

### B136 — `del_msg` accepted malformed DESTRUCTIVE targets · NEW 2026-08-06 (independent QA on [[B133]]) · **✅ FIXED 2026-08-07 (`src/`)**

**MEASURED at `src/firmware_inbox.cpp`'s `handle_del_msg`:** `const uint32_t seq = strtoul(args, nullptr, 10);` —
**no endptr check, no range check.** `strtoul` converts a leading prefix and silently ignores everything after it,
so **every one of these deleted sequence 1**: `del_msg dm 1oops` · `del_msg dm 1 extra` · `del_msg dm +1` ·
`del_msg dm 1.5` · `del_msg dm 1,2`. And `del_msg dm 0x1` deleted seq 0 → `not_found` (base 10 stops at the `x`,
so the operator's hex spelling silently names a message that cannot exist), while `del_msg dm 4294967296`
truncated to **0** on the 64-bit host/sim and saturated to **`0xFFFFFFFF`** on every 32-bit board.

★ **THIS IS THE §team-target FAMILY** ([[B1]], [[B17]] — `team <garbage>` silently LEFT THE TEAM), arriving through
a **delete** instead of a leave. The register already held the fix pattern; the new verb did not use it.

**✅ SHIPPED: `mrfw::parse_seq_arg(const char* s, uint32_t& out)` in `src/firmware_config_parse.h`,** next to
`parse_team_target` and reusing its clause discipline verbatim rather than forking a second, laxer integer parser
(U1 — a fork is exactly what rots):

1. a **leading digit** is mandatory ⇒ refuses `""`, whitespace-only, `+1`, `-1`, `abc`;
2. `strtoul` must consume characters and **everything it did not consume must be trailing whitespace** ⇒ refuses
   `1oops`, `1 extra`, `1,2`, `1.5`, `0x1`, `1e3`, and a second token;
3. **(3a) `errno == ERANGE`** (the 32-bit boards' saturation to `0xFFFFFFFF`, which is a *deletable* seq) **and
   (3b) `ul > UINT32_MAX`** (the 64-bit host's truncation). ⚠ Both arms are kept for the reason spelled out at
   `parse_team_target`: **on each ABI one arm is inert and it is a DIFFERENT arm on each**, so neither can be shown
   necessary by testing one ABI alone.

**FAIL-CLOSED:** `out` is untouched on refusal, so a rejected token cannot half-write the seq the delete is about to
act on — pinned by seeding a `0xDEADBEEF` sentinel in every refusal case and asserting it **survived**. **Base 10
only, deliberately:** a seq is printed in decimal everywhere, and base 0 would make `010` mean **message 8** —
a different message, silently. On refusal the verb emits a loud `err` line (C2), never a deletion.

⚠ **HOW `src/` WAS COVERED, since it is outside the native build** (`[env:native]` sets `test_build_src = no`): the
predicate is a **pure header**, which the native suite reaches through the env's `-I src` — the same pattern
`firmware_config_parse.h` and `firmware_ui_send.h` already use (U3). The `handle_del_msg` glue that calls it stays
uncovered natively and is exercised at bench **11.3b**.

ⓘ **SCOPE, stated rather than implied: `mark_read` still uses the lax `strtoul` and was deliberately left alone.**
It is non-destructive (a cursor move the companion re-issues constantly) and tightening a verb the app already
speaks is a behaviour change that belongs in its own slice, not folded into a delete fix (C1).

**GATE: 1 new case / 30 assertions** — 7 accepted forms (including `0`, `4294967295`, `010`→10 and a CRLF console
line), **21 refused forms each asserting the sentinel survived**, errno hygiene, and the ABI-split pin.
**Mutations, each RED:** drop the trailing-input check → **16 assertions**; drop the leading-digit rule → **4**;
drop both range clauses → **6**.

---

## Deferred with an explicit trigger


### D1 — the team **DV hop-cap flip** (T3 Part C)
`node_beacon.cpp:861` still reads `hop_cap_for(false)`, so team RREQ floods at `team_hop_cap` **8** while team DV
accepts combined hops to `dv_hop_cap` **16** — a deviation from **R4**. Measured **inert on 34/36**, and no value
of `team_hop_cap` restores `s35a`/`s38` (1→5 fails, 2→3, 3/4/8→9; **the window is empty**) because with one cap a
node's DV reach equals its RREQ reach and radius-clipping dies as a test method. ★ **TRIGGER TO REVISIT: the first
time any team scenario produces a team DV path of >8 combined hops.** Part B (`team_hop_cap`'s config surface)
already shipped, so the flip is a one-token change on that day. Note: `T3`.

### D2 — the **read-path** plane audit
§10.3's plane audit was scoped to **write** sites and was therefore structurally blind to the s38 breach, which
entered through `rt_find(…, AUTO)` degrading to `_rt`. **Every plane-typed lookup that can silently fall back to
the static table needs the treatment the write sites got.** Spec `2026-07-27-…-routing-parity-design.md` §12.

---

## Owner decisions pending

| | decision | cost of the fix |
|---|---|---|
| ~~**O1**~~ | ✅ **RESOLVED — B1 CLOSED 2026-07-31** (`§team-target-whole`); the gate was dropped and it went wider than the one line (`team 12abc` too). ⚠ Its **range** sibling is still open as **B17** |
| **O2** | `noinline` on `deleg_ack_put`? ⚠ **Superseded in detail by B19** — measured **8** call sites and **≈4 KB**, not 5 and 1.8 KB, and it must **fold into B12**. ★ **PARKED: `gateway` flash is 54.9%, so there is no pressure behind it** | one line, but re-codegens 8 sites |
| ~~**O3**~~ | ✅ **RULED 2026-07-31 (owner) — THE KEY LIVES EXACTLY AS LONG AS THE `team_id` IT WAS GRANTED FOR.**
**`set_team_id` must CLEAR `team_ch_pub`/`team_ch_priv`/`team_ch_key_present` whenever `team_id` actually changes,
including `team 0`.** QA-verified the current state: `set_team_id` already clears routes, the peer set, liveness, the
**peer** key cache and the DAD id — but **not** the channel keypair, and that key is **UNRECOVERABLE (no seed derives
it)**. ★ **Fails safe:** after a switch the member holds no key, so a post refuses `team_channel_no_key` and the app
prompts *"ask a teammate for the key"* — the flow T-K2 already defines. **Cost accepted:** switching away and back needs
one re-grant, which is precisely what T-K3 exists for. ⚠ **`create`/`join` must STILL PRESERVE the key** — they do not
change `team_id`, so the rule does not touch them, and `blob_take_team_channel_key` stays as built.
★★ **The rejected alternative, recorded so it is not re-proposed:** tagging the key with its own `team_id` and refusing
on mismatch would never destroy the key — but it needs a **new persisted field ⇒ a `kVersion` bump ⇒ a THIRD reprovision
event** on top of the two already stacked, for a rare case. **⇒ CL2 IS UNBLOCKED.** |
| **O4** | The **BLE console exposure** is no longer a watch-item — under the `team exportkey` ruling it is **the only control protecting the team content key.** Closing it (pairing / auth gate / console allow-list) makes "any transport" safe | its own slice |

---

### B79 — ★★ `channel_remote_mint` had NO PRODUCER, and the state it belongs to was NEVER CLOSED · NEW 2026-08-04 · ✅ **FIXED 2026-08-04 (UI-4, UNCOMMITTED)**
**MEASURED two ways, and it is §B72's defect one level up — on the same safety path, reached by a SUCCESS rather than a
failure.** UI-3 added `SendOutcome::channel_remote_mint()` as B68's fix, and the plan's Task-4 tracker emits **eight of
the nine kinds and never that one** — a tree-wide grep finds **zero** callers of the factory. The reason it cannot have
one in the plan's shape: the `awaiting` state (entered whenever `ctr == 0`) is closed only by a `send_blocked` or a
channel `send_failed`, and **B39's producer (3) emits neither.** Verbatim from `lib/core/node.cpp:1631-1634`: a
registered mobile's delegated GLOBAL post *"emits no CHANNEL-level push at all, only the wrapper DM's own
send_acked/send_failed, under a ctr this caller never saw"*. ⇒ **nothing can ever close that slot: the alarm or compose
sits on `SENDING...` for ever, and the UI's send slot leaks permanently.** ⓘ The plan even names the missing half —
`kOutcomeWindowMs` is documented as *"how long an accepted send may still claim a ctr-less outcome"* — and then never
consumes the expiry.

✅ **Fixed by `bool SendTracker::tick(uint32_t now_ms, SendOutcome& out)`:** the window's expiry, and the ONLY producer
of the eighth kind. It reports a **SUCCESS shape** on purpose (§B68: *"a tracker that maps `ctr == 0` to failure calls a
delivered message failed"*), and that is **safe on the alarm path as a property of UI-3, not an assumption** —
`on_outcome` routes `channel_remote_mint` through the `channel_no_relay` branch ⇒ bounded retry, then `NOT HEARD`,
never `PICKED UP`. It also covers a **dropped push** (`cap_push_ring` is bounded): the honest report is still
"sent, unattributable". ★ `accepted` deliberately does NOT expire — see the confirmed premise in the §UI-4 note
(a team `channel_sent` legitimately arrives ~36 s later). Probes **P4** (3 cases red) and **P6** (2 cases red).
⚠ **Residual: B69 is unchanged** — the kind must render as `SENT` and no `Emergency` state carries the distinction.

### B80 — ★★ `match_channel_failed` had NO CORRELATOR, so an unrelated DM failure could TERMINATE a live alarm · NEW 2026-08-04 · ★ OWNER-RULED: **the matcher is DELETED** (UI-4, UNCOMMITTED)
⛔⛔ **RULED 2026-08-04 (owner): `match_channel_failed` IS DELETED AND MUST NOT BE REINTRODUCED — and MY FIX FOR THIS
ENTRY WAS A CONVERSE ERROR.** I verified that *every channel producer passes `dst = 0`* and concluded *`dst = 0` ⇒
channel*. That is the converse, and it is false: **six unrelated operations emit exactly `{dst = 0, ctr = 0}`** —
`send_layer`'s `unsealable` arms at `node_mac.cpp:220/452/473/561/579` plus `node_mac.cpp:59/111`. A matcher built on it
attributes a colliding `send_layer` refusal to the alarm and lands it in TERMINAL `Emergency::failed`: **the same false
negative this entry names, reached by a different route.** ⇒ ★★ **THE REUSABLE RULE: a gate check must enumerate
everything that EMITS the value, not everything the feature touches.**
⇒ **The path is covered from both ends instead:** PREFLIGHT channel refusals are **synchronous** (`exec_command` →
`refuse_reason_of` → `on_send_refused` → `Emergency::failed`, exact reason intact), and the **post-mint** seal failure —
which has already burned a counter the caller never sees, so *"the reason arrives asynchronously and correlates with
nothing"* (`node_channel.cpp:~723-744`) — is handled by `tick()`'s bounded expiry (**B84**). The guarantee is now
**structural**: the tracker has no entry point that accepts an async `send_failed`, proven by a compile-level negative on
**all five real flag sets** (`error: 'class mrui::SendTracker' has no member named 'match_channel_failed'`).
⚠ **The plan's Task-4 block is unedited (forbidden) and its interface line still names the matcher.**
**The original record follows.**
**MEASURED against the core, and it is a false NEGATIVE on the safety path — the worst direction for this device.** The
plan's signature is `match_channel_failed(FailReason r, uint32_t now_ms, SendOutcome& out)`: state + an 8 s window and
**nothing else**. `channel_failed` lands as TERMINAL `Emergency::failed` (§B72's own landing state, never a retry), so
**any DM `send_failed` arriving while the alarm is `awaiting` ends the alarm and destroys its two unspent
transmissions** — an alarm that was merely `blocked` and would have auto-retried instead reports *the alarm did not go
out*. A console DM giving up `no_route` inside 8 s is sufficient, and `awaiting` is reachable on the team-plane alarm
path through B39's producers (1) and (2), so this is **not** an unreachable-by-construction case.

✅ **Fixed with a measured discriminator, not an invented one:** every channel-side producer passes `/*dst=*/0`
explicitly (`node_channel.cpp:736`; `node.cpp:1545/1551/1557/1573`) and every DM one passes the peer
(`node_mac*.cpp`, `node_cascade.cpp`; `push_send_failed`'s definition is `node.cpp:1986`), and node id **0 is reserved
for an unprovisioned node**, so it is never a real DM target. ⇒ the signature is
`match_channel_failed(uint8_t dst, FailReason r, uint32_t now_ms, SendOutcome& out)` and requires `dst == 0`.
Probe **P2**: reverting the guard alone turns the case red (3 assertions). ⏳ **The plan's Task-4 block is unedited
(forbidden) and still shows the 3-argument form** — a coder copying it re-introduces the hole.

### B84 — ★★ the fix for B79 was an INFINITE RETRY LOOP on the distress path · NEW 2026-08-04 · ★ OWNER-RULED and ✅ **FIXED 2026-08-04 (UI-4, UNCOMMITTED)**
**MEASURED BY ARITHMETIC, and it is the second time in this slice that a stated safety property was assumed rather than
traced.** B79's `tick()` closed the permanent `SENDING...` by expiring an `awaiting` slot into `channel_remote_mint`, and
I claimed it "fails safe to `NOT HEARD` because `on_outcome` routes it through the `channel_no_relay` branch". **The
branch is right and the conclusion is wrong.** That branch terminates on `_tries >= kEmgMaxTries`, and
`UiModel::_tries` increments **only** in `on_send_accepted` — `firmware_ui_model.h:223`, documented at `:326` as
*"ACCEPTED transmissions, never requests"*. A `ctr == 0` send never calls it. ⇒ the real cycle was

    post-mint seal failure → awaiting → 8 s → channel_remote_mint → on_outcome → re-queue → awaiting → 8 s → …

**for ever, with `attempts() == 0`**, never reaching `:278`'s terminal test. **Unbounded airtime on the alarm path —
in that one dimension worse than the defect it replaced.**

★★★ **RULED (owner, 2026-08-04): an expired unattributable EMERGENCY CONSUMES ONE BOUNDED ATTEMPT before its
`channel_remote_mint` is processed.** Three expiries spend the three-alarm budget and terminate in sticky `NOT HEARD`
with no fourth request queued. It matches the approved *"accepted by the transmitter is what we can establish"* policy
and fails safely: if the missing push really was a failure, an attempt has been honestly spent. ⓘ **Accepted cost: this
rare path loses its precise terminal REASON** — so `SendOutcome::channel_failed()` now has no producer (it stays in the
model for UI-3's synchronous shape; recorded, not removed).
⚠ **THE RULE LIVES IN THE CALLER'S ORDER**, because `_tries` is the model's and the sequencing is UI-6's ⇒
`on_send_accepted(SendKind::emergency, now)` **must precede** `on_outcome`. Stated in `tick()`'s contract and pinned by
**four integration cases plus a deliberate NEGATIVE CONTROL** that asserts the defect shape, so "green suite" and
"bounded" cannot be confused again. Probe **P13** (patched in the caller): 4 assertions red.

### B81 — spec §2.1 promised a `channel_id` scope check that NO outcome push can satisfy · NEW 2026-08-04 · ✅ FIXED 2026-08-04 (spec corrected: the `channel_id` half is WITHDRAWN as unsatisfiable; `peer_id` stands and is load-bearing)
Spec §2.1's tracker table lists *"`channel_id` / `peer_id` — scope check before a push may match"*, and the plan's
`submit()` duly records `channel_id`. **Measured: neither channel outcome push carries a channel id at all.**
`Node::emit_channel_sent` sets only `relayed` + `ctr` (`node_channel.cpp:850-852`) and `Node::emit_send_blocked` sets
only `blocked_channel` / `reason` / `next_ms` (`:822-826`). ⇒ `SendTracker::_chan` is **written-never-read by
construction**, which is now stated in-source beside the field ([[meshroute-mark-done-vs-missing-in-code]]) together
with where the check would go if a push ever grows the field. ⓘ No behaviour is lost: on `channel_sent` the exact
16-bit `ctr` match is strictly stronger, and on `send_blocked` there is nothing stronger available — which is why the
header refuses to call that path "exact attribution". **Action: ✅ DONE 2026-08-04 — the spec line is corrected; a check that cannot
be evaluated.**

### B82 — RECORDED (gate methodology): the `.d`-file census does not exist here, and a relative path made its absence look like a clean result · NEW 2026-08-04
**B77's class, one day later, in a new disguise — and it produced a tidy five-row `NA` table that was wrong for the
wrong reason.** Two independent faults compounded: (a) **no env in this project emits `.d` files** — measured `0` under
`.pio` on all five, because SCons keeps dependencies in `.sconsign311.dblite`; the brief's framing ("NA on the nRF52
envs, which emit none") implies the ESP32 ones do, and they do not. (b) the census read `.pio/build/$e` **relatively**,
and an agent shell's cwd resets between calls, so `find` returned nothing and the guard printed `NA` five times having
measured nothing at all. ⚠ Compounded by a third: `pio run -e X -t compiledb` **emits a valid compile database without
compiling**, so it leaves `0` objects — a per-env sconsign census run against those directories is vacuous even with
absolute paths.

⇒ ★ **The rule, now three slices old and still earning its keep: print a POSITIVE CONTROL beside every census, and
treat a `0` whose control did not fire as "cannot reach", never as "absent".** What replaced it: a sconsign binary
census showing `firmware_ui_send.h` = **0** on all five envs **while `firmware_config_parse.h` = 2 and `node.h` = 4
fire**, plus an ABI-independent source-level census (0 includers in `src/ lib/ variants/` while `protocol_constants.h`
= 13 fires), plus a **cross-compiler probe TU** compiled with each env's real command line — rc=0, 0 warnings,
non-empty objects on `arm-none-eabi-g++` 12.3.1 and `xtensa-esp32s3-elf-g++` **13.2.0** (`toolchain-xtensa-esp-elf`,
*not* the 8.4.0 `toolchain-xtensa-esp32s3` that the obvious path finds) — so "zero board delta" reads as *no TU
includes it*, never as *it would not compile if one did*.

### B83 — the tracker's `late_ack` retention and its `awaiting`-DM case are bounded only by CALLER obligations · NEW 2026-08-04 · OPEN (UI-6/UI-7)
Two deliberate non-fixes, recorded so they are not rediscovered as defects:
1. **`late_ack` is released only by `close()`.** Spec §3.4.1 scopes the NO CONFIRM → DELIVERED upgrade to *"while the
   sub-view is still showing"*, and that lifetime lives in `firmware_ui.cpp` ⇒ **UI-6/UI-7 must call `close()` when the
   sub-view closes**, or the normal slot never returns to `idle()` and the UI can never send another DM. Deliberately
   **not** a second timer inside the tracker: it cannot see the panel, and an invented window would disagree with the
   real one. `close()` and the leak it prevents are both pinned by tests.
2. **An `awaiting` DM.** `tick()` releases the slot and **invents no outcome**, so the model is left in
   `DmState::submitting`. Unreachable from today's UI (spec §3.4 sends by `team_local_id`; only a HASH-addressed send
   returns `queued` with `ctr == 0`), guarded for type-safety — emitting `channel_remote_mint` for a DM would be a type
   error, and emitting `dm_failed(none)` would repeat exactly the B68 error of calling a possibly-delivered message
   failed.
3. **Offer order.** ⚠ **§B84: `match_channel_failed` IS DELETED — only `match_blocked` remains window-correlated**, so
   the hazard is now single-sourced. Original wording kept below for the audit trail. `match_blocked` correlates by window rather than by `ctr`, so **UI-6 must
   offer every push to the EMERGENCY tracker first**. Stated in the header; not testable with one slot.

### B85 — ★★ the plan's Task-5 code block did not compile, did not link, and could not meet its own Step 5 · ✅ FIXED 2026-08-04 (Step 4 now REFERENCES the landed `variants/heltec_v3/board_ui.cpp` — deliberately not re-pasted; this plan has been corrupted twice by line-range lifting) · NEW 2026-08-04 · ✅ worked around in UI-5; **the PLAN still needs the owner's edit**
**MEASURED** (`simulation/BASELINE.md`, 2026-08-04 §UI-5). Three independent defects in one code block:

| # | the block says | measured |
|---|---|---|
| ① | *"The `mr_ui_*` hooks are **not** defined here — they live in `firmware_ui.cpp` (Task 6)"*, and omits all three | ⛔ **link failure.** `fw_main.cpp:842/1145/1326` calls them unconditionally and `MR_FEAT_OLED=1` removes `mr_ui.h`'s inline stubs ⇒ `undefined reference` ×3. §A0's own control P-A0-1 used that exact failure as an instrument. **The hooks must stay in `board_ui.cpp` until `src/firmware_ui.cpp` exists** |
| ② | `board_init()` / `button_pressed()` read `MR_UI_BTN_PIN` | ⛔ **compile failure.** The `-D` is in **Task 6** Step 1 (plan `:1349`), one task after the file that reads it |
| ③ | Step 5 *"the panel lights"* | ⛔ **unreachable.** The only caller of `board_init()` is `mr_ui_init()`, which is Task 6's. Worse: `-Wl,--gc-sections` links an uncalled canvas OUT, so the slice's flash measurement would have been a vacuous zero |

**Fix as landed (UI-5):** the three hooks stay in `board_ui.cpp`, marked TEMPORARY with the reason Task 6 must delete
them; `-DMR_UI_BTN_PIN=0` moves into `[env:heltec_v3]`; `mr_ui_init()` brings the panel up and paints **one** frame
through the real page loop, which is simultaneously Step 5's acceptance test and the reachability that makes the flash
number real. ⇒ **The plan's Task-5 Step-4 block and Task-6 Step-1 list both need editing** (not done here — spec/plan
edits were not authorised for this slice).

### B86 — RECORDED (gate methodology): the ±32 B ARM literal-packing quantum fires WITHIN one session · NEW 2026-08-04
**MEASURED** (§UI-5). §A0 concluded *"two builds of identical source within one session reproduce exactly."* Too weak:
the version banner packs `__DATE__ " " __TIME__` (`fw_main.cpp:427`, `firmware_commands.cpp:364`) and `__TIME__` moves
every **second**. `gateway` read **466884** at 16:05 and **466852** at 16:19 and again at 16:27, with a **0-line
symbol-multiset diff** across the last two and **0** `mrui`/`u8g2` symbols in the image. ⇒ ★ **on ARM, compare the
symbol multiset (name + size), not the flash total.** `xiao_sx1262` / `xiao_esp32s3` flash *did* reproduce to the byte,
so the totals are still usable there — just not load-bearing on their own.

### B87 — ⚠ THREE OLED envs (not two) gain warnings; 127 are OUR flag, 2 are U8g2's · NEW 2026-08-04 · ✅ RULED: totals PINNED at 178/178/174 with `-Wswitch` 0, gated by `tools/warning_census.sh` / OWNER CALL
**MEASURED** (§UI-5). `heltec_v3` and `heltec_mobile` go 49 → **178**; `-Wswitch` stays **0** on all five envs.

| class | Δ | attribution |
|---|---|---|
| `'-fno-rtti' is valid for C++/D/ObjC++ but not for C` | 30 → 157 (**+127**) | **ours** — `-fno-rtti` is in `[common].build_flags`, blanket by the same ruling that put `-Werror=switch` there, and U8g2 ships 127 **C** TUs |
| `-Wunused-function` `mui.c:591` · `-Wunused-variable` `mui_u8g2.c:1256` | 0 → **2** | **U8g2's own**, in the MUI menu module — `nm` finds **0** `mui` symbols in the image, so it is compiled then linked out |

**Candidate fixes:** (a) accept — recommended; (b) move `-fno-rtti` from `[common].build_flags` to `build_src_flags`,
which removes it from **every third-party C++ TU on every env** (a codegen change, its own slice, and C1 forbids folding
it into a feature). U8g2's two warnings are not fixable without editing a vendored file.

### B88 — RECORDED: three canvas entry points are garbage-collected out of the UI-5 image · NEW 2026-08-04 · ✅ **CLOSED 2026-08-05 (UI-6, QA-VERIFIED)**
**MEASURED** (§UI-5). `nm` finds **0** occurrences of `mrui::set_power_save`, `mrui::button_pressed` and
`mrui::battery_sample_mv` in the `heltec_v3` ELF. They compile (the object carries their `.text.*`/`.literal.*`
sections) and `--gc-sections` drops them, because Task 5's boot frame calls only six of the nine. ⇒ **the +34 924 B is a
LOWER BOUND**, and **blanking and the button cannot be bench-verified until Task 6 wires them** — which is why Part 8's
entries for those are written as Task-6 checks. ⓘ A 0/N meaning *"linked out"*, not *"inert"*.

### B89 — RECORDED (budget attribution): the display's flash cost is the I²C transport, not the display library · NEW 2026-08-04
**MEASURED** (§UI-5) by an exact `nm` set-difference against a same-session BEFORE built in a throwaway worktree:
Arduino/IDF **I²C driver stack 17 962 B** (92 symbols, pulled in by U8g2's HW-I2C `Wire` backend) · **U8g2 itself
9 860 B** (incl. 4 990 B for the two named fonts and the 128 B page buffer) · FreeRTOS ring-buffer/clock helpers 909 B ·
**ours ≈ 497 B**. Total image delta 35 572 B; ~6.5 KB is symbol-less padding and merged const pools. ⇒ recorded so
*"the OLED cost 35 KB"* stays attributable, and so nobody expects a different display library to recover it — any
`Wire`-based panel driver pulls the same transport. Flash sits at 37.2 % of 3.34 MB; this is attribution, not alarm.
✅ **CLOSED 2026-08-05 by UI-6 — QA-VERIFIED BY DERIVING THE SET, not by trusting the count.** `board_ui.h` declares
**nine** entry points; each now has at least one live caller in `src/firmware_ui.cpp` (`draw_text` ×20, `set_font` ×3,
`set_power_save` ×3, `begin_frame`/`next_page`/`board_init` ×2, `draw_hline`/`button_pressed` ×1, and
`battery_sample_mv` at `firmware_ui.cpp:109`). **Zero uncalled ⇒ nothing for `--gc-sections` to drop**, so the UI-5
figure is no longer a lower bound and blanking + the button are now bench-reachable (Part 8).
⚠ **A QA instrument note, because it is this project's recurring defect and it bit me inside this very check:** my first
sweep reported **eight** entry points, not nine. The regex classed the return type as `[a-z_]+`, which **excludes
digits**, so `int32_t battery_sample_mv()` never matched — and `battery_sample_mv` is precisely one of the three
symbols this entry was opened about. A narrower pattern would have "confirmed" the closure while silently omitting the
member most at risk. ⇒ **match the declaration shape digit-safely (`[A-Za-z_][A-Za-z_0-9]*`), and cross-check the count
against the entry's own prose ("six of the nine") rather than against the grep that produced it.**

### B90 — the panel power rail (Vext, GPIO 36) was never driven by this tree · NEW 2026-08-04 · ✅ **CLOSED 2026-08-05 (owner: panel lit fine)**
**FOUND while implementing UI-5** (§UI-5). The plan's Task-5 block never touches GPIO 36, and neither did anything else
in this tree, so on an ESP32-S3 it comes up as an input with no pull and the rail's gate is indeterminate. MeshCore's
working V3 port drives it to a definite level at board begin — `RefCountedDigitalPin::begin()` writes `!active` with
`active` defaulting HIGH ⇒ **LOW** — and its SSD1306 works with the display holding **no claim** on the pin.
**Landed:** `board_init()` reproduces that proven level via file-local `kVextPin` / `kVextOnLevel`.
⚠ **Residual, and it is the honest part:** that port never claims the rail, so it does **not** establish that the panel
is on it — this is *"reproduce the proven pin level"*, not *"Vext is active-low"*. If the bench shows a dark panel, flip
`kVextOnLevel` to HIGH **before** suspecting the reset pin, which is where the plan's Step 5 hint points first and is
the wrong first suspect on a rail that was floating until this slice. Bench: Part 8.
✅ **CLOSED 2026-08-05 — owner ran the bench: the panel lit fine with the landed LOW drive.** The defect is gone: the
pin is no longer indeterminate, it is driven to a definite level, and the panel works at that level on real hardware.
⚠ **What this does NOT establish, and the distinction is load-bearing:** a lit panel is equally consistent with
*"Vext gates the panel and LOW enables it"* and with *"the panel is not on that rail, so the write is inert."* The
bench could not separate those two, because **both predict exactly the same observation** — so the residual above is
narrowed, not answered. That is fine for correctness (the code is right for this board either way) and it is why this
closes as a bug rather than as a finding.
⇒ **The one place the unanswered half can still bite:** anyone later trying to cut idle current by **de-asserting Vext
to blank the panel** would be assuming the control authority this bench never demonstrated. Spec §5 blanking uses
`set_power_save()` (an I²C command to a powered panel), **not** the rail, so nothing today depends on it. If a future
slice wants rail-level power gating, it must **measure the rail**, not cite this closure.
★★ **UPDATE, SAME DAY — THE MISSING INSTRUMENT NOW EXISTS, AND IT CAME FROM [[B91]].** ⚠ This supersedes the sentence
originally written here (*"the canvas still cannot report a dead panel, so a wrong guess would again be invisible in
software"*) — **that was true when B90 was closed and was false a few hours later**; it is corrected rather than
appended-to, per the standing rule that an entry must never assert a claim and its negation. B91's fix makes
`board_init()` return a real I²C ACK (`Wire.endTransmission() == 0`), so the panel's presence is now **observable in
software**. ⇒ **the residual above is answerable by one bench step, no longer only by a meter:** flip `kVextOnLevel` to
HIGH and read the boot line. **Probe stops ACKing ⇒ the panel IS on the Vext rail** (LOW enables it, and rail-level
power gating would work). **Probe still ACKs ⇒ the panel is NOT on that rail** and the write is inert. Either way the
current code stays correct, so this is a curiosity to satisfy cheaply — **not** a reason to reopen B90.

### B91 — the canvas cannot REPORT a dead panel · NEW 2026-08-04 · ✅ **CLOSED 2026-08-05 (UI-6, QA-VERIFIED — a real measurement)**
**FOUND while implementing UI-5** (§UI-5). `mrui::board_init()` is `void`, and U8g2's `begin()` is
`initDisplay(); clearDisplay(); setPowerSave(0); return 1;` — it performs **no I²C ack check**, so it cannot fail.
MeshCore probes the address instead (`SSD1306Display::i2c_probe` → `Wire.endTransmission() == 0`). ⇒ a wrong reset pin,
a wrong address or a dead panel is **indistinguishable from success in software**, which is exactly the misdiagnosis
spec §14 Q1 warns about. Not fixed here: a failure channel needs a caller that can surface it, and the console sink is
`firmware_ui.cpp`'s (Task 6). Marked in-source at the top of `board_ui.cpp`.
✅ **CLOSED 2026-08-05 by UI-6, and QA read the body rather than the signature — because `bool` alone would have been
exactly the "a success that isn't" shape this arc is named for.** `board_init()` is now `bool` (`board_ui.h:37`) and
ends with a genuine zero-byte presence test: `Wire.beginTransmission(kOledAddr); return Wire.endTransmission() == 0;`
— the same probe MeshCore's `SSD1306Display::i2c_probe` uses, so **an ACK, not an inference**. ★ Correctly ordered
**after** `s_u8g2.begin()`, because U8g2 owns `Wire.begin(sda, scl)` and nothing else in this firmware touches `Wire`;
run earlier it would have measured an unconfigured bus. `firmware_ui.cpp` surfaces the negative as one boot line
through `mrcon` (the guarded sink — B95's drop-never-block contract, so a dead panel cannot wedge the console).
★★ **AND IT RETROACTIVELY SUPPLIES THE INSTRUMENT [[B90]] LACKED** — see the cross-note there. B90 closed with an
honest residual (*"a lit panel cannot distinguish 'LOW enables the rail' from 'the panel is not on the rail'"*) because
both hypotheses predicted the same observation. This ACK probe **separates them**, which is the more valuable half of
this fix and was not the reason it was written.

### B92 — spec §11's font names disagree with the plan and with what landed · NEW 2026-08-04 · OPEN / SPEC CORRECTION
**MEASURED** (§UI-5). Spec §11 says *"U8g2 with **two** fonts selected (6×8, 8×16)"*; the plan's Task-5 block and the
shipped `set_font()` use `u8g2_font_6x10_tf` / `u8g2_font_10x20_tf`, matching `board_ui.h`'s `Font` comment. The plan is
authoritative for this slice, so 6x10/10x20 landed. The two fonts measure **4 990 B of the 9 860 B U8g2 total**, i.e.
half of it — a real budget line, not a cosmetic detail. **Correct §11 to the pair that landed** (or rule the other way
and re-measure; 6×8/8×16 would be smaller).

### B94 — `heltec_v3` failed to build on WINDOWS: `'Wire' was not declared in this scope` · NEW 2026-08-04 · ✅ **RESOLVED ON THE AFFECTED HOST (hardware tests ran)**
⚠ **CORRECTION 2026-08-04 — B96 supersedes the environment-only ruling below.** A pristine HEAD fails on Linux too:
the repository's unsuffixed `lib_extra_dirs` can select a stale framework sibling instead of the URL-pinned active
package. The historical investigation is retained below as an audit trail; the host-independent remedy and current
verification state are in B96.
★★ **HISTORICAL WINDOWS FIX (replaced by B96):** `lib_extra_dirs = ${platformio.packages_dir}/framework-arduinoespressif32/libraries`
in `[env:heltec_v3]`, which makes the framework's Wire resolvable instead of relying on LDF discovery. **Verified by the
owner's Windows build and the H5 bench run.**
⚠⚠ **AND THE PART WORTH KEEPING: THAT SAME CONFIG BREAKS A LINUX BOX WITH A STALE DUPLICATE FRAMEWORK PACKAGE.** The
directory NAME is ambiguous. On the owner's Windows host `framework-arduinoespressif32/` **is** the live 3.1.3; on the QA
Linux box it is a **stale 2.0.0 leftover** whose WiFi cannot compile (`IPv6Address.h: No such file`), while the live
framework is `framework-arduinoespressif32@src-<hash>` (the URL-pinned install). ⇒ **symptom: `heltec_v3` fails on Linux
with WiFi/SPI errors from a framework nobody is using. REMEDY: remove the stale leftover package — it is an ENVIRONMENT
defect, not a repo defect.**
ⓘ **A host-independent alternative exists and was MEASURED, then DELIBERATELY NOT ADOPTED:** `tools/wire_path.py`
(a **`pre:`** script — `post:` fails with *"the main program is already constructed"*) asks
`PioPlatform().get_package_dir()` which framework THIS build uses, so a stale sibling cannot be picked. It links (32
`TwoWire` symbols, 178 warnings) **but moves the numbers: RAM 211252→211236, Flash 1244400→1244452 (+52 B, beyond the
±32 B `__DATE__` floor)** because it compiles Wire as a build unit rather than an LDF archive. ⇒ adopting it costs a
**re-pin of B87** and risks a working, bench-proven Windows state to fix one machine's stale install. **Take it only if a
second host hits this**; then re-pin B87 in the same commit.
★★★ **PROCESS — EIGHT of my hypotheses were wrong before the real cause landed, and the pattern is one thing:** a
shadowing header · a registry `Wire` · version drift · a stale LDF cache · an `@src` suffix · a transitive dependency ·
"the fix is actively harmful" (it was working on the host that mattered) · and ⛔ **`pio pkg list` output, which lists
only RadioLib+U8g2 on BOTH hosts and is SILENT about framework built-ins.** ⇒ **every wrong turn came from reasoning over
an instrument I had not confirmed could observe the thing asked about** — an empty build dir (my own gate had moved to
temp dirs), a truncated dependency graph, a command that omits built-ins, and a Linux verification that **structurally
cannot fail** because discovery finds Wire regardless. ★ **The decisive facts all came from the owner's host.** When a
defect is host-specific, the remote instrument is the only one that counts.
Owner-reported: `U8x8lib.cpp:1338: error: 'Wire' was not declared in this scope`, with the same 127 `-fno-rtti` C
warnings B87 pins. **Linux builds clean.**
★★ **THE ASYMMETRY IS THE FINDING: Linux was the ACCIDENT, not Windows the anomaly.** `Wire` is **not** in
`.pio/libdeps/heltec_v3/` on **either** host — it was never resolved as a library at all, only found off a framework
include path. ⇒ the dependency was **never declared**, and every Heltec RAM/flash/warning figure was measured on that
un-declared path. ⓘ This machine also has **two** `framework-arduinoespressif32` packages (the URL-pinned pioarduino
fork **and** a stock one), i.e. a fallback the owner's host lacks.
**DIAGNOSIS — LOCATED 2026-08-04, and BOTH of my first two hypotheses were WRONG:**
- ⛔ *"the include path / a shadowing `Wire.h`"* — **wrong.** ⛔ *"my `lib_deps = Wire` pulled a registry Wire that
  shadows the framework's"* — **wrong**: the owner's `pio pkg list -e heltec_v3` shows **only RadioLib and U8g2**, so
  that entry **resolved to nothing and was silently ignored** (no error, no package). Platform/framework/toolchain
  versions are IDENTICAL on both hosts (`espressif32 53.3.13` · `framework-arduinoespressif32 3.1.3` ·
  `toolchain-xtensa-esp-elf 13.2.0`), so it is not version drift either.
- ★★ **MEASURED: Linux's LDF resolves, COMPILES and LINKS the framework-bundled Wire entirely on its own** — a build
  into a controlled dir yields `lib8d9/libWire.a`, `Wire/Wire.cpp.o`, and **32 `TwoWire` symbols** in the ELF, from
  nothing but U8g2's `#include <Wire.h>`. Windows's LDF does not: the owner's log compiles **no Wire at all**.
- ⇒ **the fault is LDF RESOLUTION OF A FRAMEWORK LIBRARY, not the include path.** `U8X8_HAVE_HW_I2C` is defined
  unconditionally (`U8x8lib.h:82-84`, *"Assumption: All Arduino Boards have Wire.h"*), so the include is always active;
  the header simply never entered a build that was never told to compile Wire.
**`lib_deps = Wire` REVERTED** — proven inert: with it removed, Linux still produces `libWire.a` + 32 `TwoWire` symbols.
Leaving dead config in the variable set is worse than nothing.
★ **MOST LIKELY CAUSE, and the fix to try first: a STALE LDF DEPENDENCY GRAPH on Windows.** That log is an
**incremental** build (`Compiling .pio\build\heltec_v3\lib42d\U8g2\…`), and the LDF caches its graph per build dir.
If that tree was first resolved **before** U8g2 joined `lib_deps`, the cache never learned U8g2 pulls Wire, and adding a
library afterwards does not always force re-resolution. ⇒ **delete `.pio` (or `.pio\build\heltec_v3` +
`.pio\libdeps\heltec_v3`) and rebuild.** Fits every observation: same versions, same source, same flags, different
cached resolution state.
**If a CLEAN rebuild still fails**, the LDF genuinely does not scan framework libraries on that host ⇒ name the
framework Wire dir explicitly via `${platformio.packages_dir}` in `build_flags` and add `Wire.cpp` to the build.
⚠ Deliberately NOT committed yet: it hardcodes framework layout, and it is unwarranted until the clean rebuild rules the
cache out.
⚠⚠ **PROCESS NOTE (mine): I argued the include-path theory at length while every `find`/`nm` I ran against
`.pio/build/heltec_v3` was inspecting an EMPTY DIRECTORY** — my own rewritten `tools/warning_census.sh` builds into a
temp dir, so the persistent tree holds nothing. Fourth vacuous-instrument failure of the session, and the first in a
tool I had written hours earlier. **The evidence that settled it came from one build into a directory I controlled and
then verified.**

### B93 — `mr_ui.h` forward-declares `Push` in a HARDCODED namespace · NEW 2026-08-04 · OPEN / LATENT
**FOUND while implementing UI-5** (§UI-5). `lib/hal/mr_ui.h:17` writes `namespace meshroute { struct Push; }` while
`command.h:13-15` defines `Push` inside the build-overridable `MESHROUTE_NS` (`-DMESHROUTE_NS=meshroute_gw` is named in
that very comment for the two-lib gateway variant). No env overrides it today, so both spellings are the same type and
everything links — the same latent class the §UI-3-QA note found for `SendFailReason` one level up, where the fix was
`using FailReason = MESHROUTE_NS::SendFailReason;`. The day a variant sets the macro, `mr_ui_on_push` takes a different
(incomplete) type than the one being pushed. One line in a `lib/hal` header ⇒ **not** folded into a board slice (C1).

### B95 - USB command responses can fuse surviving `Print` fragments into syntactically false rows - NEW 2026-08-04 - ✅ **FIXED 2026-08-04 (uncommitted), WITH THE BRIEF'S CENTRAL INVARIANT REFUTED BY MEASUREMENT**
**BENCH-FOUND in OLED H5-06, but NOT an OLED/radio failure.** The radio checks passed. Under console pressure, `cfg` and
`routes` produced rows such as `beacon_ms=900000168010102layer=5 leaf=5000` and a gateway-schedule suffix fused directly
into the next `[route]` row. `src/console_sink.h` admits/drops each individual `Print::write()` call, while those rows
are assembled from tens of calls; the USB task can free capacity between calls, so later values survive after labels,
spaces, punctuation, or the newline were dropped. The result is misleading syntax, not random RAM corruption.

Two sink-contract bypasses are in the same causal slice: `dump_help(Print& out)` ignores `out` and its `hl()` writes
directly to `Serial` with a per-line 40 ms loop plus a conditional CRLF; `print_sf_list(bitmap)` always writes to global
`mrcon`, including from `dump_cfg(Print& out)`. A line-staging fix that misses either bypass is incomplete.

**RULING FOR THE FIX:** preserve the anti-wedge contract. Never wait/flush/delay for USB. Submit USB command responses
as complete lines; insufficient capacity drops a whole line, never fragments, and a deferred operator-critical
`!! CONSOLE_DROP lines=N` reports the loss when capacity returns. Keep the maximum line scratch off the ESP32 loop-task
stack; preserve `MR_CONSOLE=0` compile-out and BLE/remote structured formats. Do not accidentally stream the
multi-kilobyte help text over BLE.

Coder brief and acceptance matrix:
`docs/superpowers/plans/2026-08-04-console-response-line-integrity.md`.

✅ **FIXED 2026-08-04 — evidence in `simulation/BASELINE.md` §B95 (top note). UNCOMMITTED; metal rerun still owed (bench
guide H5-06 + `docs/2026-07-31-bench-test-script.md` Part 9).** The line stage now lives **inside `mrcon`**
(`src/console_sink.h`), `hl()` is deleted (help writes through its `Print& out`), `print_sf_list` takes its sink at all
four call sites, `service_console()` calls `mrcon.service()` once per loop pass, and BLE refuses `help` with a bounded
`{"err":"help","msg":"console_only"}` before the text fallback. Anti-wedge preserved: nothing waits, flushes, delays or
yields; `flush()` is the one bounded blocking entry and only the reset/OTA path calls it.

⛔ **THE BRIEF'S INVARIANT 2 ("a complete line in ONE `Serial.write()`, or zero bytes") IS REFUTED, MEASURED:**
`availableForWrite()` tops out at **128 B** on ESP32-S3 (UART0 hardware FIFO — `Serial` is `Serial0`, `_txBufferSize`
defaults to 0) and **256 B** on nRF52 (`CFG_TUD_CDC_TX_BUFSIZE`). The `cfg` `proto :` row is 118 B, `[cfg.layer0]`
~160 B, the `hashof` remedy ~392 B, and 8 of 75 `help` lines exceed 128 B — one-call-per-line makes all of them
**permanently undeliverable even to a healthy idle host** (the probe measures **2 of 8 `cfg` rows delivered**).
⇒ shipped guarantee, strictly stronger where it matters: **a committed line reaches the wire as a contiguous, gap-free,
in-order run including its terminator, or not at all; a line is discarded only before its first byte is written.**
That is also why the stage sits inside `mrcon` rather than in a wrapper on the command path: with an in-order drain, any
second `Serial` writer (async push, `!!` log, banner) would cut into a half-drained line and re-create the fusion.

**Cover:** `tools/probe_console_sink/` — 52 behavioural checks against the real header + 11 structural + **13 negative
controls, all red**. Its positive control (the pre-fix sink, copied verbatim) reproduces the H5-06 capture
independently: `leaf=5000` byte-identical, `hop_cap=168` = `hop_cap=16` + `team_hop_cap`'s bare `8`. Brief tests 7 and 8
are **structural** (grep) and labelled as such: `dump_help` / `print_sf_list` / `ble_dispatch_line` live in TUs no host
build can compile. **Cost:** RAM **+2064 B** (nRF52) / +2072 (xtensa) — the 2048-B stage, lever
`MR_CONSOLE_STAGE_BYTES`; flash +4.1-4.2 KB on nRF52, +0.5 KB on xtensa. `MR_CONSOLE=0` RAM is **unchanged (0 B)**.
**QA correction:** the compile-out census originally selected GNU `nm`'s 8-byte `guard variable for mrcon` because
it matched any line ending in `mrcon`. It now selects the exact object symbol: **2088 B** with `MR_CONSOLE=1`, **8 B**
with `MR_CONSOLE=0`; the complete probe exits 0. This was a probe-only defect, not missing staging in firmware.
⚠ **Two residues, both deliberately not fixed here (C1) and both needing an owner call:** (a) `production` flash
**+7040 B** — `hl()`'s `(void)fs` no-op let the linker garbage-collect all 6121 B of help text, and routing it through
`Print& out` references it again, in an image where it is now unreachable (no USB console, BLE `help` refused);
`#if MR_CONSOLE`-gating `dump_help`'s body would reclaim it. (b) `help` (6121 B / 75 lines) exceeds the stage, so it
delivers ~25 lines and reports `!! CONSOLE_DROP lines=N` — a deliberate trade against `hl()`'s old ~3 s of loop stall.

### B96 — HEAD's `lib_extra_dirs` breaks EVERY Heltec env on LINUX (`SPI.cpp` / `WiFi.h` from the wrong framework package) · NEW 2026-08-04 · 🟡 FIX APPLIED / LINUX 3-ENV GATE GREEN / WINDOWS VERIFICATION OWED
**FOUND while gating §B95 — not caused by it (proven, three controls).** `45c9cc1` added to `[env:heltec_v3]`:
`lib_extra_dirs = ${platformio.packages_dir}/framework-arduinoespressif32/libraries`, with the comment *"Inert where
discovery already worked (Linux), load-bearing where it did not."* **On this Linux host it is not inert — it is fatal.**
Two framework packages are installed: the plain `framework-arduinoespressif32` (espressif32@6.11.0 era) and pioarduino's
`framework-arduinoespressif32@src-702d0f93023d86e22d8ef62aa333f0b7`, which is the one `[env:heltec_v3]`'s **pinned**
platform uses. The hardcoded un-suffixed name puts the OLD package's `libraries/` on the LDF path, so the build compiles
library sources from framework A against core headers from framework B:
```
libraries/SPI/src/SPI.cpp:121: error: too many arguments to function 'bool spiDetachSCK(spi_t*)'
libraries/WiFi/src/WiFi.h:29: fatal error: IPv6Address.h: No such file or directory
```
`heltec_v3`, `heltec_mobile` and `gateway_heltec` (both inherit via `extends`) all fail; `xiao_esp32s3` is unaffected
(same platform, but it compiles neither `device_ota.cpp` nor U8g2, so nothing pulls `WiFi`/`SPI`).
**Controls:** ① a pristine `git worktree` at HEAD with **zero** B95 changes fails with the byte-identical error set;
② the documented B94 cache remedy (`rm -rf .pio/build/heltec_v3 .pio/libdeps/heltec_v3`) does not help; ③ removing
**only** those two lines in that worktree builds `heltec_v3` green and reproduces §UI-5's reference **211252 / 1244400
exactly**. ⇒ **Recommended fix (owner's slice, deliberately not made here):** resolve the framework directory through
PlatformIO's API — `env.PioPlatform().get_package_dir("framework-arduinoespressif32")` inside an `extra_script` —
instead of hardcoding the package name. B94's own note predicted this cost: *"it hardcodes framework layout."*
★ At discovery, no Heltec env could be built or flashed from this host, so **every OLED bench item was blocked**.

**FIX APPLIED 2026-08-04 (uncommitted):** `[env:heltec_v3]` now runs `pre:tools/wire_path.py`; the script asks the
active platform for `framework-arduinoespressif32`, adds only that package's `Wire/src`, and fails the build if either
the package or source directory is absent. Linux clean isolated gate: `heltec_v3` **325 objects / 178 warnings /
213308 RAM / 1244980 Flash**, `heltec_mobile` **325 / 178 / 212828 / 1238448**, `gateway_heltec` **325 / 174 /
238228 / 1214596**; `-Wswitch` **0** on all three. The warning pins do not move. Controlled B96 delta remains RAM
**−16 B**, Flash **+52 B**. ⛔ Do not mark fully fixed until the owner reruns at least `pio run -e heltec_v3` on
Windows: that is the host where explicit Wire discovery was originally load-bearing.

### B97 — ★★ the four distress-path "integration regressions" could not fail · NEW 2026-08-05 · ✅ FIXED (UI-6, UNCOMMITTED)

**MEASURED** (§UI-6). `test_firmware_ui_send.cpp` carried the four regressions §B84 demanded, all green. Their helper
`run_ctr0_expiries` did this:

```
emg.tick(t, o);  m.on_send_accepted(SendKind::emergency, t);  m.on_outcome(o, t);
```

— i.e. it **re-typed the wiring** rather than executing it. `mr_ui_tick`'s copy lived in `src/firmware_ui.cpp`, which
**neither the native suite nor the simulator compiles**, so the four cases pinned the *rule* while being blind to the
*code*. The plan itself says of that wiring: *"I got this wrong twice, so copy it, don't improvise"* — the two most
error-prone lines in the slice, with a green suite that could not see them.

⇒ **FIX:** the wiring is now two `inline` functions in `src/firmware_ui_send.h` — `ui_pump_trackers` (the §B84
ordering) and `ui_route_send_push` (the offer order + the deleted-arm guarantee) — and `firmware_ui.cpp` does nothing but
call them. Board-free by construction, so the native suite drives the shipped code.

★ **FIVE REVERT PROBES, each mutating the real header, rebuilding, then restoring (md5 re-checked, identical each time):**

| revert | result |
|---|---|
| §B84 blocker 1 — drop `on_send_accepted` before the expiry's outcome | **2 cases / 5 assertions RED** |
| §B84 blocker 2 — route the normal tracker's expiry into `on_outcome` | **2 cases / 3 assertions RED** (see B98) |
| offer order — give the normal slot `send_blocked` first | **1 case / 4 assertions RED** |
| re-add an emergency `send_failed` arm (§B80's deleted matcher shape) | **1 case / 3 assertions RED** |
| §B71 — delete the exit from `UiModel::on_gesture` | **5 cases / 9 assertions RED** |

### B99 — the plan's Task-6 block tears its emergency overlay, and its tick does not compile · NEW 2026-08-05 · ⚠ THE PLAN NEEDS THE OWNER'S EDIT

**FOUND while implementing UI-6.** Two independent defects in the Step-4 code block:

1. **The overlay tears.** The block correctly freezes `UiState` and `UiSnapshot` at `begin_frame()` — spec §5's rule,
   because U8g2 re-clips the whole scene once per page and a frame spans several ticks — and then the emergency render
   reads `s_model` **live**. A state change between page 1 and page 8 therefore splits the image on the one screen the
   whole feature exists for. ⇒ UI-6 freezes a small `EmgView` POD alongside the other two.
2. **It cannot build.** `mr_ui_tick` calls `ui_perform_send()`, which the plan defines in **Task 7 Step 1** and which
   needs `mrfw::exec_command` — an addition to `src/firmware_commands.{h,cpp}` that C1 forbids folding into this task.

⇒ UI-6 ships `ui_perform_send` as a **loud refusal** (C2), marked in-source and on the panel: `tr.refuse()` +
`on_send_refused(kind, other, now)` + one console line. The alarm therefore terminates in `Emergency::failed`, which
§B78 made *retained* and §B71 makes dismissable — so the state machine is honest and never traps the operator. The
alternative (drop the request) reproduces the permanent `SENDING...` that §B72/§B79 were raised about.

ⓘ **Also deferred deliberately:** the plan's Task-6 Step 1 lists `-DMR_UI_ADC_CTRL=37` / `-DMR_UI_VBAT_READ=1`. Nothing
reads them until Task 9, and this project has already ruled that config landing ahead of its reader is **dead config**
(§A0's `-I` prediction). They land with Task 9 — the same rule that pulled `MR_UI_BTN_PIN` forward into Task 5.

ⓘ **One more ordering change, and it is an improvement rather than a deviation for its own sake:** the tick tests
`blanked` **before** continuing an open page loop. `set_power_save(true)` abandons the board's loop, so the plan's order
leaves `firmware_ui.cpp`'s `s_frame_open` describing a frame the board has already dropped. Both halves are now dropped
together, and the probe's P4 already proves no page can reach a dark panel.

### B105 — one `IRadio&` accessor would make the feature layer host-testable and remove the 2 new pinned warnings · NEW 2026-08-05 · ✅ **CLOSED 2026-08-06**

> ✅✅ **IMPLEMENTED 2026-08-06 — owner-approved (`docs/2026-08-05-owner-rulings-ledger.md` §2), and BOTH predicted
> payoffs were measured rather than assumed. Full gate + the A/B: `simulation/BASELINE.md`'s §B105 note (top).**
> **Landed:** `IRadio& radio()` on `DeviceHal` (+9 lines) · a new **pure** `src/fw_context_pure.h` holding the
> `g_hal` / `g_node` externs, which `fw_context.h` now **includes instead of restating** (its 1:1 rule is intact —
> this is THE declaration, not a second one) · `firmware_ui.cpp` swaps the include and reads `g_hal.radio()`.
> **① Warnings:** `180/180/176 → 178/178/174 @ 326 objects`, `-Wswitch` 0 — attributed by a controlled A/B on
> `heltec_v3` whose `uniq -c` diff is **exactly** `-Wcpp` 6→5 and `-Wvolatile` 7→6. **Re-pinned in BOTH required
> places** (`tools/warning_census.sh` + §B87), declared, with the superseded block labelled and kept.
> **② The probe exists:** `tools/probe_firmware_ui/` — 25 checks, **13 negative controls all verified**, and its
> **C0** is the counterfactual (restore `fw_context.h` ⇒ the host build MUST fail), so the include cannot come back
> silently. See [[B104]] for exactly what it now covers and what it still does not.
> ⛔ **A premise that turned out wrong, recorded because "a refactor is free" is how real deltas hide: FLASH IS
> +16 B PER OLED ENV, NOT 0.** `mac_idle()` now dispatches `tx_busy()` virtually through `IRadio&` where it used to
> name the concrete `g_iradio` (devirtualizable). RAM is byte-identical; the predicate, the instance and its
> ISR-driven volatile state are unchanged. Isolated by the same A/B: 1253788 vs 1253772.
> ⛔⛔ **CORRECTED IN PLACE 2026-08-06 (docs-only pass) — THE s18 RATIONALE RECORDED HERE WAS WRONG, AND IT
> CONTRADICTED THIS ENTRY'S OWN `MEASURED` SECTION BELOW.** These two lines used to read: *"⚠ **D2 was discharged by
> RUNNING s18** (`1cd21235` / 271629, corpus 36/36), not by the 'src-only is inert' argument — a `lib/` file
> changed."* That claims **any** `lib/` change makes s18 non-inert. **It does not** — and the `MEASURED` paragraph
> further down correctly says the accessor *"touches no `lib/core` file (so s18 byte-identity is unaffected)"*. A
> claim and its negation must never stand in one entry (ledger §3 rule 3).
> ★ **Verified against the simulator's own build config (V1), not against a comment:** its `CMakeLists.txt`
> enumerates the MeshRoute sources **explicitly** — **19 of `lib/core`'s 21 `*.cpp`** (`admin_auth.cpp` and
> `fault_log.cpp` are not in the sim's list) + `lib/console/console_json.cpp` + monocypher — with include dirs
> `lib/core` / `lib/console` / `lib/monocypher/src`. **`lib/hal` is neither compiled
> nor on the include path, and no `lib/core` file `#include`s `device_hal.h`** (`lib/core/node.h:1620` mentions it
> in a comment only). ⇒ **THE ACCURATE RULE: `lib/core` → s18 byte-identity is LOAD-BEARING and must reproduce the
> `BASELINE.md` keystone (D2); `lib/hal` → outside the simulator's build graph ⇒ s18 is inert BY CONSTRUCTION.**
> ⚠ **Not to be overcorrected into "`lib/` changes don't matter".**
> ★ **s18 WAS run — `1cd21235` / 271629 EXACT, corpus 36/36 — and that green result STANDS as a whole-tree
> tripwire. It is not withdrawn; it simply does not test this accessor.** **0 files under `lib/core`;
> `sizeof(Node)` unmoved.**
> ⓘ **Attribution: the error originated in the QA-gate's dispatch brief** (*"if any `lib/` file changes, prove s18
> byte-identity"*), **not in the implementer's work — the coder followed the brief correctly.**
> ★★ **BOARD ENVS — an EVIDENCE-RECORD correction, not a code failure. Nothing failed.** This slice ran and
> recorded **5/5** (`gateway` / `xiao_sx1262` / `xiao_esp32s3` / `heltec_v3` / `heltec_mobile`, rc=0 from deleted
> object dirs). **INDEPENDENT QA SUBSEQUENTLY BUILT ALL TEN BOARD ENVS SUCCESSFULLY — that is QA's measurement,
> recorded as QA's; the slice did not run ten and does not claim to.**
> ⚠ **The standing tension, recorded rather than silently resolved:** `CLAUDE.md`'s **D1** literally says *"every
> board env"* (there are **10**; `platformio.ini`'s 11th `[env:]` block is `native`, not a board), while the
> **2026-07-28 owner ruling** narrows ROUTINE gating to the **three** standing envs, all ten only when
> `sizeof(Node)` / a board `#if` / the linker moves (**D2**) — in-repo at `docs/2026-07-28-agent-handover.md:180`
> and `docs/superpowers/plans/2026-07-31-onboard-oled-ui-phase-a.md:24`. ★ **B105 edits a `lib/hal` header compiled
> into EVERY board build, so ten was the right call here.** ⛔ **D1 in `CLAUDE.md` is the owner's text and was NOT
> rewritten.**

**MEASURED** (§UI-6). `src/firmware_ui.cpp` makes exactly three device reads: `g_node` (`node.h`),
`g_hal.txq_depth()` (`lib/hal/device_hal.h`) and `g_iradio.tx_busy()` (`lib/hal/device_radio.h`). The first two headers
are Arduino- and RadioLib-free **by design** — `device_hal.h:11` states it. Only `tx_busy()` forces `fw_context.h`, and
that single include is the whole cause of:

- **B106's +2 warnings** — RadioLib's `#warning` and `device_radio.h`'s `-Wvolatile`, both once per including TU; and
- **B104's coverage loss** — the feature layer cannot be host-compiled, so the once-per-page redraw obligation, the
  MAC-idle gate, the 2 Hz throttle and the battery cadence have **no behavioural probe at all**, only structural greps.

`DeviceHal` already holds `IRadio& _radio` privately and exposes no accessor. `IRadio& radio() { return _radio; }` is
header-only, generates nothing unless called, touches no `lib/core` file (so s18 byte-identity is unaffected), and would
let `firmware_ui.cpp` include only pure headers — at which point a `tools/probe_firmware_ui/` on the model of
`tools/probe_board_ui/` becomes a small job instead of a RadioLib shim.

**NOT TAKEN.** The plan's Global Constraints name four permitted new firmware surfaces (the UI TUs, U8g2, the V3 battery
reader, `mrfw::exec_command`) and instruct: *"If a task appears to need anything beyond these — stop and ask."* This is
beyond them. Recorded as the highest-leverage follow-up in this arc.

## How to use this file

1. **Pick a tier, not a line.** Tier 2 before Tier 3; anything that fails *silently* jumps the queue.
2. **Read the named `BASELINE.md` note first** — it has the measurement, the probe matrix and the reason the
   fixing slice declined.
3. **Re-locate every symbol.** See the warning at the top.
4. **Close the entry here in the same commit as the fix**, or this file becomes the next thing that rots — which
   is precisely what it exists to prevent.
