> ## ⛔ OBSOLETE — ADDRESSED AND ARCHIVED 2026-08-01
>
> **Every finding in this review has been applied to the two documents it reviewed.** It is kept only as the audit
> trail for *why* those documents changed; do not action it again.
>
> | § | finding | where it landed |
> |---|---|---|
> | 1 | send attribution (P0, false `PICKED UP`) | spec §2.1 · plan **Task 4** (new send tracker) |
> | 2 | retry deadline / attempt accounting (P0) | spec §4 "Retry timing" · plan Task 3 |
> | 3 | incomplete emergency transitions (P0) | spec §4.2-§4.4 · plan Tasks 2-3 |
> | 4 | DM outcome model | spec §3.4.1 · plan Task 3 |
> | 5 | inbox adapter over `Inbox::pull()` | spec §6.1 · plan Task 7 Step 3 |
> | 6 | blanking bypassed page-chunking | spec §5 (edge-triggered `set_power_save`) · plan Task 5 |
> | 7 | battery ADC on every tick | spec §7 · plan Task 6 Step 2 (30 s cache, MAC-idle) |
> | 8 | board/feature boundary violation | spec §2 (canvas) · plan Tasks 5-6; `mrfw::dispatch` qualified |
> | 9 | snapshot/status omissions, V3 BLE inert | spec §3.3, §6 (BLE dropped on V3; volts not %) |
> | 10 | editorial consistency | applied throughout both documents |
>
> **One correction to this review.** §2 states that `next_ms == 0` leaves the retry "blocked forever" because
> `on_tick` requires `_retry_at_ms != 0`. It does the opposite: `_retry_at_ms` was `_last_try_ms + 0`, a non-zero
> timestamp already in the past, so the guard passed and the retry **spun every tick**, burning all three alarms in
> milliseconds. The defect was real and worse than described, but the fix differs — a backoff floor, not a non-zero
> check. The spec adopts 2 s doubling to 30 s.
>
> **Also superseded by events:** this review assumed the channel-encryption and per-send-location prerequisite was
> still pending. It landed before the revision — `-e`/`-l` are accepted (`console_parse.cpp:250,267`) and honoured
> with the full refusal matrix including `no_fix` (`node.cpp:1402-1526`). Nothing in Phase A waits on protocol work.
>
> Findings were **not** entered in `docs/2026-07-30-open-bug-register.md`: per owner ruling 2026-08-01 that register
> is for implemented code, not for plan/spec defects.

---

# On-device OLED UI design and Phase A plan — review findings

*2026-08-01. Standalone review of the following draft documents; neither source document is modified by this review.*

- `docs/superpowers/specs/2026-07-31-onboard-oled-ui-design.md`
- `docs/superpowers/plans/2026-07-31-onboard-oled-ui-phase-a.md`

## 0. Review outcome

The overall product direction is strong: the emergency path is deliberately simple, delivery wording distinguishes relay evidence from human confirmation, the model is intended to be native-testable, and page-buffered drawing recognizes the radio timing hazard.

The Phase A plan is not ready to execute literally, however. The main blockers are emergency outcome attribution, retry accounting, incomplete emergency transitions, and display operations that bypass the proposed I2C safeguards. Several other requirements already have sufficient firmware support but need a precise OLED-side adapter described in the plan.

This review assumes the channel encryption and per-send location work named as a prerequisite will land before Phase A.

## 1. Emergency outcomes are not attributed to the emergency send

**Severity: P0 — false safety confirmation is possible.**

The proposed `mr_ui_on_push()` sends every `PushKind::channel_sent` and every `PushKind::send_blocked` into the emergency model. It does not check:

- whether `send_blocked.blocked_channel` is true;
- whether the event belongs to the configured UI channel;
- whether a `channel_sent.ctr` matches the emergency transmission;
- whether a canned, console, BLE, scheduled, or emergency command originated the post.

While the emergency model is in `firing` or `blocked`, an unrelated channel outcome can therefore produce `PICKED UP` or `NOT HEARD`. Even a blocked DM can incorrectly put the emergency UI into `BLOCKED`.

The synchronous command response is captured in a `BufferSink` and discarded. Parser refusal or another immediate failure can consequently leave the display on `SENDING...`. The push handler also ignores `send_failed`, including `unsealable`, `no_location`, and other actionable refusal reasons.

### Proposed solution

Add a firmware-layer `UiSendTracker` that records at least:

- send kind (`emergency`, canned channel, or DM);
- accepted/refused state;
- channel ID or peer ID;
- the accepted command's `ctr` when one exists;
- the time at which the attempt was accepted;
- the expected terminal push kinds.

`ui_perform_send()` must return a typed result to the model instead of discarding the synchronous response. Prefer factoring a small typed firmware helper out of the existing dispatch path so both console dispatch and the UI reuse the same parser and `Node::on_command()` behavior. If the existing textual sink remains the only integration point, parsing its output must be isolated and tested rather than spread through UI code.

Only a matching `channel_sent.ctr` may complete an accepted emergency attempt. A `send_blocked` event must at minimum be channel-scoped and accepted only during the immediate outcome window for the pending UI request; its lack of a `ctr` makes explicit serialization important. `send_failed` and synchronous parser refusal require visible terminal or actionable states rather than an indefinite `SENDING...` state.

Add native tests with interleaved unrelated channel and DM outcomes to prove that they cannot alter the emergency state.

## 2. Retry deadlines and attempt accounting are incorrect

**Severity: P0 — automatic retry can deadlock or transmit fewer than three alarms.**

`Push::next_ms` is a relative delay: milliseconds until origination is allowed. The plan calculates the retry deadline as `_last_try_ms + next_ms`, but `_last_try_ms` is copied from `_last_input_ms`. That timestamp remains the user's original gesture time during later automatic retries and is not the time at which the block outcome arrived.

A capacity/duty block may report `next_ms == 0`. The proposed `on_tick()` explicitly requires `_retry_at_ms != 0`, so this form stays blocked forever.

Finally, `_tries` increments when the model emits a request, before the command is known to have been accepted. A pre-TX refusal can therefore consume one of the three permitted radio attempts even though nothing was transmitted.

### Proposed solution

- Pass `now_ms` into send-outcome handling and compute `retry_at_ms = now_ms + next_ms` with wrap-safe unsigned comparisons.
- Count an attempt only after the command is accepted for transmission and has an identifying handle/`ctr`.
- Do not count parser failures or `send_blocked` events as transmitted attempts.
- Define an explicit bounded policy for `next_ms == 0`. A reasonable Phase A policy is a UI-only exponential recheck delay, capped at a modest interval, while retaining `BLOCKED` and without consuming a radio attempt. If an existing limits accessor can provide an exact recovery time, prefer it.
- Define whether “3 attempts” means three total transmissions or three retries after the first transmission. The design text currently uses both readings; the native tests must pin one meaning.

Add tests for an initial block, a block after an automatic retry, `next_ms == 0`, time wraparound, and a blocked request followed by exactly three accepted transmissions.

## 3. The emergency machine cannot implement all specified transitions

**Severity: P0 — the primary product path is incomplete.**

The Task 2 model checks `compose != none` before dispatching gestures to the emergency switch. `compose_gesture()` handles only short and double presses, so `long_arm`, `long_fire`, and `long_cancel` are swallowed inside both compose views. This contradicts the requirement that emergency activation work from every screen and sub-view.

Other missing transitions/data are:

- `cancelled` has no one-second deadline returning it automatically to `idle`;
- the arming countdown has no changing `hold_ms`/deadline in `UiState` or `UiSnapshot`;
- no periodic countdown boundary marks the model dirty;
- emergency drawing still goes through the general 500 ms paint throttle;
- no `REPLY: <name> <text>` state or payload exists;
- no transition stores an inbound teammate reply as the true human confirmation;
- Task 8 expects rendering code to access information that its declared interfaces do not carry.

### Proposed solution

Handle `long_*` gestures before blank-wake consumption and before compose dispatch. Emergency gestures should pre-empt normal navigation everywhere.

Represent all time-based transitions explicitly in the model:

- `arming_fire_at_ms` or a derived `arming_seconds_left`;
- `cancelled_until_ms`;
- `emergency_display_until_ms`;
- `retry_at_ms`.

Mark dirty only when the visible countdown value changes, while still allowing emergency states to bypass the ordinary 2 Hz throttle under the MAC-idle gate.

Add a bounded reply payload to the UI feature layer—sender display name/ID plus a display-clamped body. When an appropriate inbound team-channel post arrives while an emergency is active or sticky, transition to a sticky `reply` state. Define whether any team-channel message qualifies or whether it must be on `MR_UI_TEAM_CHANNEL_ID`; the latter is safer and less surprising.

Native integration tests must drive the input FSM and model together, including a long press while a compose view is open and while the panel is blanked.

## 4. DM delivery and `NO KEY` feedback have no model path

**Severity: P1 — specified behavior and hardware acceptance steps cannot pass.**

The design requires a canned DM to surface the destination's end-to-end acknowledgement and to show `NO KEY` when an encryption-configured node lacks the peer key. The proposed model has no DM send-status state, and `mr_ui_on_push()` ignores both `send_e2e_acked` and `send_failed`.

The Task 7 hardware checklist nevertheless expects both outcomes on the panel.

### Proposed solution

Give non-emergency sends a small, independent outcome model:

- `submitting`;
- `waiting_for_e2e_ack` for `-a` DMs;
- `delivered` on a matching `send_e2e_acked`;
- `no_key` on matching `send_failed{no_pubkey}`;
- `not_confirmed` on `e2e_ack_timeout`;
- `failed` plus a compact reason for other failures;
- a short timeout or explicit acknowledgement returning to the parent screen.

Track the accepted DM `ctr` and peer so unrelated ACK/failure pushes cannot complete the UI transaction. Add native tests for unrelated ACKs, late ACK after timeout, `no_pubkey`, and successful delivery.

## 5. The existing inbox is sufficient; the OLED adapter is underspecified

**Severity: P1 documentation/plan gap — no new inbox subsystem is required.**

The firmware already exposes the needed unified abstraction:

- `g_node.inbox().pull(dm_since, chan_since, callback)` visits both stores;
- every `InboxEntry` carries `InboxKind::dm` or `InboxKind::channel`;
- DM and channel entries carry their corresponding sender/channel metadata and body;
- the console `pull_inbox` command is an NDJSON transport over the same `Inbox::pull()` API.

One nuance must be documented: `Inbox::pull()` returns the DM block oldest-first, followed by the channel block oldest-first. The DM and channel sequence spaces are independent; it does not return one chronologically interleaved stream.

The current Phase A plan stops at counters and renders `use the app to read`, despite also saying Task 6 will wire the inbox cursor. This is an implementation-plan omission, not a missing firmware capability.

### Proposed solution

Specify an OLED-side bounded view over `Inbox::pull()`:

- call `g_node.inbox().pull()` directly; do not dispatch textual `pull_inbox` into the 512-byte `BufferSink`;
- retain only the bounded number of rows the OLED can browse;
- visibly prefix every row with `DM` or `CH` (and the channel number where useful);
- clamp sender/body text safely to the display width;
- define whether “merged” means one screen with two labeled blocks or a chronological sort by `rx_time_ms`;
- if chronological sorting is chosen, document reboot/uptime semantics before extending this behavior to persistent inbox targets;
- define the short-press cursor behavior and what happens when there are more stored rows than the UI bound;
- keep the existing UI-local unread counters unless the owner explicitly wants viewing the OLED to advance the durable `mark_read` cursors.

For Phase A, a single screen with labeled DM and channel rows is enough to satisfy “merged inbox”; strict chronological interleaving is optional but must not be implied without a rule.

## 6. Blanking bypasses the page-chunking safety rule

**Severity: P1 — repeated full-display I2C traffic can disturb the radio hot path.**

The proposed `blank()` calls U8g2 `clearDisplay()`. U8g2 documents that this clears both its internal buffer and the connected display, so it performs display I/O rather than merely changing local state. The proposed tick calls `blank()` on every service pass while the model remains blanked.

This reintroduces the full-display transfer the page-buffer design is intended to avoid, potentially on every loop. It also spends power while supposedly blanked.

Reference: <https://github.com/olikraus/u8g2/wiki/u8g2reference#cleardisplay>

### Proposed solution

- Make blanking edge-triggered, not level-triggered: perform hardware work once when entering or leaving the blanked state.
- Prefer `setPowerSave(1)` to turn the SSD1306 panel off without clearing display RAM; use `setPowerSave(0)` on wake and repaint if required.
- Keep a board-side `panel_asleep` latch so repeated ticks are no-ops.
- If a clear is still desired, send it through the same page-chunked state machine and MAC-idle gate as every other frame.
- Ensure one tick cannot send the final paint page and then immediately initiate a second display operation.

Add an instrumented board test or trace counter proving that a blanked panel causes no repeated I2C transactions.

## 7. Battery ADC work runs on every service pass

**Severity: P1 — avoidable work is placed before the MAC-idle gate.**

`build_snapshot()` is called every `mr_ui_tick()` and calls `battery_mv()` unconditionally. The proposed V3 reader toggles the divider and performs eight `analogRead()` calls each time. This happens even when the model is clean, the display is blank, or the MAC is busy.

### Proposed solution

Cache the battery reading in the feature layer:

- sample at boot when safe, then at a slow fixed cadence such as 15–60 seconds;
- start a sample only under the MAC-idle predicate;
- preserve the last good millivolt value between samples;
- render unavailable until the first successful sample;
- keep the board function responsible only for one board-specific sample operation.

The Phase B V4 settling delay should remain a non-blocking state machine as already required by the design.

## 8. The implementation plan violates its own board/feature boundary

**Severity: P1 — architecture drift plus literal compile problems.**

The design states that `board_ui.cpp` never knows what a screen is. The Phase A plan then makes `board_ui.h` include `firmware_ui_model.h`, accepts `UiState`/`UiSnapshot`, and places all screen, compose, and text rendering policy in `board_ui.cpp`.

Task 8 subsequently instructs the implementer to modify `firmware_ui.cpp` for `draw_current_screen`, even though Task 4 placed that function in `board_ui.cpp`.

The send snippets also call `dispatch()` unqualified, but it is declared as `mrfw::dispatch`. The body helpers shown after `draw_current_screen()` also need declarations before their first use if the snippets are followed in the displayed order.

### Proposed solution

Preserve the design boundary:

- `firmware_ui.cpp` owns screen selection, string formatting, and render policy;
- `board_ui.h` exposes a small display-independent canvas/page interface such as `begin_frame`, `draw_text`, `draw_hline`, `set_font`, `next_page`, `set_power_save`, button sampling, and battery sampling;
- `board_ui.cpp` owns U8g2, I2C, GPIO, ADC, and panel state only;
- `board_ui.h` does not include the UI model;
- Task 8 modifies `firmware_ui.cpp`, matching the architecture and its own file list.

Alternatively, if the simpler board-owned renderer is preferred, amend the architecture explicitly and stop claiming the hard boundary. The current documents should not promise one architecture and implement the other.

Qualify the command call as `mrfw::dispatch` and include a compile-complete ordering or forward declarations in the plan snippets.

## 9. The snapshot and renderer omit specified status data

**Severity: P2 — feature completeness and misleading indicators.**

The design requires teammate names/fallback hashes, team ID, registration state, BLE mode, battery millivolts, and a persistent status bar. The proposed snapshot/render path provides only a teammate local ID, route metrics, BLE-connected boolean, and battery millivolts. It does not resolve names, carry the configured team ID, or expose registration/BLE-mode detail.

On the Heltec V3 target, `mrble::connected()` currently resolves to the inert ESP32 implementation and always returns false. A displayed `ble` state would therefore look like a disconnected-but-supported service even though this target currently has no active BLE transport.

The status-bar example promises a percentage, while the plan renders only `OK` or `--`; no voltage-to-percentage policy is specified. The plan also caps teammate rows at 8, whereas the design's budget names 16.

### Proposed solution

- Extend `TeamRow` with a bounded display label resolved through `team_key_of_id()` and `peer_name_find()`, falling back to hash then local ID as specified.
- Add configured team ID and the intended registration fields to `UiSnapshot`.
- Render BLE as `N/A` on targets where the transport is compiled inert, or remove it from the V3 status bar until ESP32 BLE exists.
- Prefer battery millivolts or a simple voltage bar for Phase A. Do not display a percentage until a chemistry/discharge-curve policy is approved.
- Resolve the row cap to one value. If only 8 are displayed, show truncation or pagination and do not report 8 as the total team size.

## 10. Editorial consistency fixes

**Severity: P2 — not implementation blockers, but likely to mislead later slices.**

Before marking either document approved, reconcile these remnants:

- Phase B §10.3 still says emergency location should be revisited, although §4.1 records the resolved owner decision to include it when available.
- Feature gating refers to slots 2, 4, and 5, but the cycle has only four slots.
- The final open question says each canned-message “slot” lengthens the cycle, although the revised design moved messages into a compose sub-view.
- The design budgets 16 team rows while the plan implements 8.
- The plan's file-structure table says `board_ui.cpp` implements the `mr_ui_*` hooks, but Task 6 moves those hooks into `firmware_ui.cpp`.
- Task 2 reports “all five cases” after listing more than five tests.
- Current source line references in the design have already drifted as prerequisite work changed `fw_main.cpp`; prefer symbol references, optionally with advisory line numbers.

## 11. Recommended plan revision order

Revise the documents in this order before implementation:

1. Define send attribution and the typed UI send-result contract.
2. Rewrite the emergency retry/accounting model and native matrix.
3. Complete emergency gestures, countdown, cancellation, reply, and failure states.
4. Add the DM outcome model and correlation tests.
5. Clarify the existing-inbox OLED adapter and bounded browse behavior.
6. Correct the display boundary, blanking transition, and emergency painting rules.
7. Cache battery sampling outside the service hot path.
8. Complete the snapshot/status contract and resolve V3 BLE presentation.
9. Apply the editorial consistency fixes and only then execute Task 1 onward.

## 12. Minimum additional acceptance cases

The revised Phase A plan should include these cases in addition to its existing gates:

- a canned or console channel post completes while an emergency is firing and cannot alter the emergency state;
- a blocked DM cannot put the emergency screen into `BLOCKED`;
- parser refusal and `send_failed{unsealable}` leave `SENDING...` and show an actionable failure;
- `next_ms == 0` does not deadlock automatic retry;
- three accepted transmissions occur even if one or more preceding requests were blocked;
- long emergency gestures work from both compose views and from a blanked panel;
- the arming countdown visibly changes and cancellation returns to the parent screen automatically;
- a matching teammate reply becomes sticky human confirmation; unrelated channel traffic does not;
- a DM ACK or failure is matched by `ctr` and peer; unrelated ACKs are ignored;
- the inbox displays bounded, readable rows labeled `DM` or `CH` using the existing `Inbox::pull()` API;
- a blanked panel produces no repeated I2C traffic;
- battery ADC sampling occurs only at its slow cadence and never begins while the MAC is busy;
- `heltec_v3` and `heltec_mobile` compile with the final TU ownership and namespaced dispatch call.
