# Heltec V3 OLED UI — Compile and Staged Bench Guide

Date: 2026-08-04  
Scope: Phase A onboard OLED UI on Heltec WiFi LoRa 32 V3  
Related plan: `docs/superpowers/plans/2026-07-31-onboard-oled-ui-phase-a.md`

This is the focused hardware companion to `docs/2026-07-31-bench-test-script.md` Part 8. It is deliberately split by implementation availability so hardware work can start after Task 5 without pretending later UI paths already exist.

## 1. When to run each group

| Group | Available after | Main purpose |
|---|---:|---|
| H5 | Task 5 — board canvas | Prove the OLED electrical seam, transport, static frame, and absence of an immediate radio regression |
| H6 | Task 6 — feature layer | Prove rendering, button navigation, blank/wake behavior, and display/radio coexistence |
| H7 | Task 7 — sends and compose | Prove canned channel sends, DM compose, cancellation, send outcomes, and inbox presentation |
| H8 | Task 8 — emergency integration | Prove long-press safety behavior, retries, pickup/reply feedback, isolation, and preemption |
| H9 | Task 9 — battery reader | Prove the Heltec V3 battery measurement against a meter and under radio load |

Start H5 now. Do not use a missing later-stage behavior as an H5 failure.

## 2. Equipment and topology

### Minimum for H5

- One Heltec WiFi LoRa 32 V3.
- A known-good USB data cable.
- A computer with this repository and PlatformIO installed.
- A phone or camera for recording the display.

### Add for H6

- Preferably a second compatible MeshRoute node for continuous beacon/DM traffic.
- Optional logic analyser or oscilloscope for OLED SDA/SCL checks:
  - SDA: GPIO 17
  - SCL: GPIO 18

### Add for H7 and H8

- At least two nodes on the same test network.
- For emergency pickup/reply tests, a second node that can receive and answer the emergency traffic.
- A deliberately unreachable or powered-off recipient for timeout/retry cases.

### Add for H9

- A charged LiPo suitable for the board.
- A reasonably accurate multimeter.
- Safe access to battery voltage and ground test points.

## 3. Important GPIO0 precaution

The Heltec V3 UI button is GPIO 0, active LOW with the internal pull-up. GPIO 0 is also an ESP32-S3 boot strap.

- Do not hold the button while powering the board, resetting it, or starting an ordinary flash.
- If the board unexpectedly enters the ROM download loader, release the button and reset it.
- Deliberately holding the button during reset is useful only as a separate recovery-path test.

## 4. Compile, flash, and monitor

Run all commands from the repository root:

```text
cd /home/staszek/MeshRoute
```

### 4.1 Choose the environment

Use `heltec_v3` for the isolated Task 5 board-canvas bring-up:

```text
pio run -e heltec_v3
```

Use `heltec_mobile` for the integrated Phase A mobile UI from Task 6 onward:

```text
pio run -e heltec_mobile
```

Record the chosen environment with every result. Do not compare observations made with different environments as though they were the same binary.

### 4.2 Run the host-side board probe

Before flashing Task 5 or later:

```text
./tools/probe_board_ui/run.sh
```

Expected Task 5 baseline:

```text
phA5 board_ui probe: 34 passed / 0 failed / 34 total
```

The runner also executes its compile-negative controls. Treat any non-zero exit as a failed preflight.

### 4.3 Make a clean board build

For Task 5:

```text
pio run -e heltec_v3 -t clean
pio run -e heltec_v3
```

For Task 6 onward:

```text
pio run -e heltec_mobile -t clean
pio run -e heltec_mobile
```

A successful build ends with `SUCCESS`. Record RAM and flash from the PlatformIO summary. Size movement is not automatically a failure, but unexplained movement from the reviewed baseline must be investigated before treating the hardware result as authoritative.

Verified 2026-08-04 build references for the current Task 5 tree:

| Environment | RAM | Flash |
|---|---:|---:|
| `heltec_v3` | 211252 B | 1244400 B |
| `heltec_mobile` | 210772 B | 1237796 B |

These are comparison references, not permanent protocol constants. Later tasks are expected to move them.

### 4.4 Find the serial port

```text
pio device list
```

Typical Linux ports are `/dev/ttyACM0` or `/dev/ttyUSB0`. The actual name is authoritative. The device can disconnect and return under a different name after flashing; rerun the command if necessary.

### 4.5 Upload

If only one compatible serial device is attached:

```text
pio run -e heltec_v3 -t upload
```

or, from Task 6 onward:

```text
pio run -e heltec_mobile -t upload
```

With more than one attached device, select the port explicitly:

```text
pio run -e heltec_v3 -t upload --upload-port /dev/ttyACM0
```

Replace the environment and port as required.

### 4.6 Open the console

```text
pio device monitor --port /dev/ttyACM0 --baud 115200
```

After connecting, capture:

```text
version
whoami
cfg
```

Keep the console log from boot onward. A `-dirty` version is acceptable for an uncommitted QA build, but the exact build environment, source state, and test date must be recorded.

## 5. H5 — tests available now

### H5-01 — Preflight and boot identity

- [x] `./tools/probe_board_ui/run.sh` passes.
- [x] `pio run -e heltec_v3` succeeds.
- [x] Upload succeeds without holding GPIO 0.
- [x] The console reaches the normal MeshRoute prompt.
- [x] `version`, `whoami`, and `cfg` are captured.
- [x] No reset loop, watchdog reset, brownout, or bootloader loop occurs.

Pass: normal firmware boot and stable console operation with the OLED-enabled binary.

### H5-02 — Static Task 5 frame

⛔ **UI-6 DELETED THIS FRAME (2026-08-05). H5-02 and H5-03 are UI-5-ONLY and cannot pass on a Task-6 build** — the
splash existed to make the canvas reachable under `--gc-sections` (§B88), and the feature layer replaced it with the real
page-chunked render. On a Task-6 build the first thing on the panel is the live STATUS screen: go to **H6-01**.
The boxes below are already ticked from the UI-5 run; leave them as the record.

Observe the panel immediately after boot.

- [x] The panel powers on.
- [x] The first line reads `MeshRoute` in the large font.
- [x] A horizontal rule is visible below the title.
- [x] The second text line reads `OLED UI-5 ok`.
- [x] Text is upright, readable, and not clipped.
- [x] No obvious random pixels, page-wrap corruption, or persistent partial frame is visible.

Reference font intent:

- large: approximately 10×20
- small: approximately 6×10

Pass: the complete static frame is legible and stable.

### H5-03 — Static-frame stability

Leave the board running for at least five minutes while retaining the serial log.

- [x] The image remains visible and unchanged.
- [x] No periodic flicker or repeated clear/redraw is visible.
- [x] The board remains responsive to console commands.
- [x] No spontaneous reset appears in the log.

Optional instrument check: after the initial paint, SDA/SCL should not show a continuous redraw stream during this Task 5 static state.

### H5-04 — Vext polarity diagnosis, only if the panel is dark

The reviewed Task 5 implementation drives GPIO 36 LOW, matching the known Heltec/MeshCore behavior.

1. Confirm the firmware is otherwise running through the serial console.
2. Record that GPIO 36 LOW produced a dark panel.
3. Change only the Vext level to HIGH, rebuild, and repeat H5-02.
4. Record which level powers the panel.

- [ ] LOW works; no source change is required.
- [ ] Or HIGH works, and the exact observation is reported as a board/polarity correction.

Do not combine the polarity experiment with reset-pin changes; otherwise the causal result is lost.

### H5-05 — Reset-pin diagnosis, only if H5-04 does not recover the panel

The current seam uses OLED reset GPIO 21.

1. Restore the proven Vext setting from H5-04.
2. Test reset GPIO 21.
3. If still dark, change only the display reset choice to `U8X8_PIN_NONE`.
4. Rebuild and repeat H5-02.

- [ ] GPIO 21 works.
- [ ] Or `U8X8_PIN_NONE` works, and the result is reported as a board reset-seam correction.

If neither choice works, stop. Capture the serial boot log, clear photos, supply voltage, and any I²C activity before attempting broader changes.

### H5-06 — Basic radio sanity with OLED initialised

Use a second node if available. The purpose is not yet UI interaction; it is to catch a gross transport or timing regression caused by enabling the panel.

✅ **RADIO CRITERION: PASSED** (2026-08-04) — beacons, the reachable-peer transfer and the no-new-reset checks below are
all ticked, and enabling the panel caused no transport or timing regression. **The console glitches recorded here were
NOT an OLED or radio failure**: they were the USB console admitting individual `Print::write()` fragments, which is now
**§B95** in `docs/2026-07-30-open-bug-register.md`.

⇒ **§B95 IS FIXED (uncommitted, 2026-08-04)** — line-staged sink inside `mrcon`, the direct-`Serial` `hl()` help bypass
deleted, `print_sf_list` given its sink, and losses reported as `!! CONSOLE_DROP lines=<N>`. Evidence:
`simulation/BASELINE.md` §B95 + `tools/probe_console_sink/`.
- [ ] **POST-FIX RERUN OWED HERE**: repeat the `cfg` / `routes` / `help` captures below and run
  **`docs/2026-07-31-bench-test-script.md` Part 9** (9.1–9.9), which carries the exact expected console lines. Pass =
  every received row structurally complete, no row fused into another, `help` never leaving output on the same physical
  line, and any omission arriving as whole lines plus the `!! CONSOLE_DROP` report.
- ⛔ **BLOCKED BY §B96**: `heltec_v3` / `heltec_mobile` / `gateway_heltec` do not build on the Linux host at HEAD
  (`lib_extra_dirs` points the LDF at the wrong framework package). The rerun needs that fixed, or a Windows host.

- [x] The Heltec continues to receive normal beacons.
- [ ] A normal command or DM can be queued. -  notes - help messages - no correct lineend (minor), minor glitches in output:
cfg
    node_id=106
      radio : freq=869.0000 routing_sf=7 sf_list=6,7 bw=125.00 kHz cr=5 tx_power=22
      proto : duty=1.00% beacon_ms=900000168010102layer=5 leaf=5000
routes
    1Laye0000[route] dest=5 next=5 hops=1 score=208 pen=0 gw=1 leaf=5 age_ms=39214 cand=3

routes
    [route]   gw_sched period=15000ms heard_ms=39214608526@]5@0125015-20[route] dest=5 next=5 hops=1 score=208 pen=0 gw=1 leaf=5 age_ms=54969 cand=3
routes
    [route]   gw_sched period=15000ms heard_ms=54969533126@]5@837]013013[route] dest=5 next=5 hops=1 score=208 pen=0 gw=1 leaf=5 age_ms=59377 cand=3


- [x] With a reachable peer, the transfer reaches the same ACK/receive outcome as the same topology normally produces.
- [x] No new reboot, watchdog, or persistent receive-decode failure begins after OLED initialisation.

Pass: ordinary radio and console operation remain viable with the static display active.

### H5-07 — Record the boot-strap behavior

This is a board fact, not a Phase A UI pass condition.

- [x] Normal reset with the button released boots MeshRoute.
- [x] If deliberately tested, reset with the button held enters the expected loader/recovery behavior.
- [x] Releasing the button and resetting restores normal boot.

Do not repeatedly use the loader case as part of ordinary UI testing.

## 6. H6 — run after Task 6 lands

Build and flash `heltec_mobile`, then repeat the boot-identity and radio-sanity portions of H5-01/H5-06 with `heltec_mobile` substituted for `heltec_v3` before the following tests.

### H6-00 — Panel-ACK report (§B91) — do this first, it is the new diagnostic

`board_init()` now probes the panel's I²C address and `mr_ui_init()` reports the answer. That one line splits the two
failure modes H5-04/H5-05 had to guess between.

- [ ] On a healthy panel the console prints **nothing** about the OLED at boot.
- [ ] ★ Positive control (do it once, so the absence is evidence): disconnect SDA (17) or SCL (18), reboot, and confirm
      the console prints exactly `!! OLED panel did not ACK (check Vext / addr 0x3C / wiring)`.
- [ ] With the panel disconnected the node still beacons and still answers the console — the report is not fatal.
- [ ] Reconnect; the line disappears again.

Reading the result: **line present** ⇒ nothing is answering (rail / address / wiring — work H5-04 then H5-05).
**Line absent but the panel is dark** ⇒ the panel ACKs, so the fault is downstream (reset pin, contrast, or render).

### H6-01 — Initial status screen

Exact expected content on a fresh node with no mail, no teammates heard, and Task 9 not landed:

```
DM0 CH0 T0/0 --          <- status bar, 6x10, with a full-width rule under it
STATUS
me T<team_local_id>  team <8 hex digits>
DM 0, newest --
CH 0, newest --
batt --
```

- [ ] The live status screen replaces the Task 5 static proof frame.
- [ ] `me T…` matches `team` / `whoami`; the 8-hex `team …` matches `cfg`'s `team_id`.
- [ ] Text remains within the 128×64 canvas.
- [ ] Battery reads `--`, **never a number** — the V3 reader is Task 9 (`console_json.h:126` rule).
- [ ] Ages read `--` until the first push arrives, then switch to `12s` / `5m` / `1h05` form.
- [ ] Missing data uses the specified unknown/empty representation rather than stale values.

### H6-02 — Short-press screen navigation

- [ ] On a team build, the screen order is STATUS → TEAM → INBOX → SEND → STATUS.
- [ ] On STATUS and SEND, a short press advances exactly one screen.
- [ ] On TEAM, each short press advances exactly one peer row; only a press at the end leaves TEAM.
- [ ] On INBOX, navigation follows the implemented row/end rule without skipping an item.
- [ ] Releasing the button does not create a second navigation event.
- [ ] A double press on STATUS or INBOX performs no action.
- [ ] Repeated deliberate presses traverse the complete cycle in order.
- [ ] Button bounce does not skip screens.

### H6-03 — Blank and wake

- [ ] The display blanks after the configured inactivity interval.
- [ ] The transition to blank occurs once; it is not continuously repainted.
- [ ] The first short press wakes the display without also advancing the screen.
- [ ] A later short press advances normally.
- [ ] Radio reception continues while the display is blank.

Optional instrument criterion: after blanking, SDA/SCL show the one transition burst and then remain quiet until a real UI change.

### H6-04 — Rendering stability under radio load

Generate sustained ordinary beacon and DM traffic with a second node.

- [ ] Cycle all UI screens while traffic is active.
- [ ] No torn or half-old frame remains on screen.
- [ ] No reboot or watchdog occurs.
- [ ] CTS/ACK behavior does not visibly degrade relative to the same link with the UI left idle.
- [ ] Rendering resumes correctly after a busy radio interval.

Record comparable idle and active-radio logs. A single weak-link retry is not proof of a UI regression; a repeatable increase aligned with display painting is.

### H6-05 — Dynamic model updates

- [ ] Unread/inbox indicators change after incoming traffic.
- [ ] Network or peer state changes appear on the next permitted refresh.
- [ ] Unchanged state does not cause visible continuous redraw.
- [ ] Values agree with the console representation of the same state.

### H6-06 — The send path is deliberately NOT BUILT, and must say so

★ This exists so `not built` is never mistaken for `the radio failed`. `ui_perform_send` is a loud-refusal stub;
`mrfw::exec_command` is Task 7's, and C1 forbids folding it into this slice.

- [ ] Long-press: the panel shows large `RELEASE!` with `EMERGENCY IN 3` … `2` … `1` counting down while held.
- [ ] The countdown digit changes visibly, and the panel does **not** repaint between digit changes.
- [ ] Release past 3.5 s: `SENDING...` appears, then large `FAILED` with small `no send path: UI-7`.
- [ ] The console prints exactly `!! UI send path not built (plan Task 7 / slice UI-7)`.
- [ ] ⛔ The panel never remains on `SENDING...` — a stuck SENDING is the §B72/§B79 defect class.
- [ ] ⛔ Nothing is transmitted (confirm on the second node), and the panel claims nothing but a failure.
- [ ] Release at ~3.0 s instead: `CANCELLED` shows briefly and auto-returns.

### H6-07 — §B71: the emergency screen's exit

- [ ] From the `FAILED` screen, one short press clears the alarm and the normal cycle resumes.
- [ ] While `SENDING...` is showing, a short press does **not** clear the overlay (an unseen outcome is sticky).
- [ ] Let the panel blank on the `FAILED` screen (past `MR_UI_BLANK_MS`, then past `kEmgHoldMs`): the **first** short
      press only wakes it and the outcome is **still displayed**; the **second** clears it.
- [ ] A long press from the sticky screen re-fires, from any screen and from a blanked panel.
- [ ] A **double** press on the sticky screen does nothing to the alarm (both of spec §4's `double` duties are withdrawn).
- [ ] Long-press from inside a compose sub-view still reaches the alarm, and the acknowledging short press acts on the
      alarm rather than moving the compose cursor.

### H6-08 — Battery cadence with an unavailable reader

- [ ] `batt --` throughout (Task 9 has not landed; the reader answers "unavailable" by design).
- [ ] Instrument or trace that a sample is **attempted** about every 30 s, not on every service pass — the cadence gates
      on *attempted*, not on *succeeded*, precisely so an unavailable reader is not re-read for ever.
- [ ] No sampling starts while the MAC is busy.

## 7. H7 — run after Task 7 lands

### H7-01 — Canned channel send

- [ ] Use short presses to reach SEND.
- [ ] Double-press on SEND to enter the channel compose sub-view.
- [ ] Confirm the initial highlight is `Got your message`.
- [ ] Use short press to move among `Got your message`, `All good`, and `back without sending`.
- [ ] Double-press a message to send it.
- [ ] Exactly one channel operation is queued.
- [ ] The UI reaches the correct queued/sending outcome.
- [ ] A receiving node observes the intended body and channel.

Repeat with an unavailable or blocked send path and verify the UI reaches the specified bounded failure outcome rather than remaining on `SENDING...` indefinitely.

### H7-02 — Cancel a canned send

- [ ] Double-press on SEND to enter the channel compose sub-view.
- [ ] Short-press until `back without sending` is highlighted.
- [ ] Double-press that row.
- [ ] No radio operation is queued.
- [ ] The UI returns to SEND.
- [ ] Re-entering compose starts on the first message.
- [ ] Leaving the sub-view idle for `MR_UI_BLANK_MS` also exits without sending.

### H7-03 — DM compose and send

- [ ] Use short presses to highlight a known peer on TEAM.
- [ ] Double-press to enter the DM compose sub-view for that peer.
- [ ] Confirm the initial highlight is `Are you OK?`.
- [ ] Use short press to choose `Are you OK?` or `I'm OK`, then double-press to send.
- [ ] Exactly one DM is queued for the selected peer.
- [ ] A reachable peer receives the intended body.
- [ ] ACK/no-confirm/failure presentation matches the actual console outcome.

Repeat for an unknown/unresolved peer and verify the UI refuses safely with the designed reason; it must not silently send to a different peer or plane.

### H7-04 — Cancel DM compose

- [ ] Enter DM compose for a known peer.
- [ ] Short-press until `back without sending` is highlighted.
- [ ] Double-press that row.
- [ ] No DM is queued.
- [ ] The UI returns to TEAM without changing the selected recipient unexpectedly.
- [ ] No stale partial message is sent on the next compose attempt.

### H7-05 — Inbox presentation

- [ ] Receive at least one DM and one channel message.
- [ ] Both appear in the merged inbox presentation with an unambiguous DM/channel marker.
- [ ] Ordering agrees with the firmware inbox sequence.
- [ ] Opening/reading an item updates unread state as designed.
- [ ] Long bodies are clipped/wrapped predictably without corrupting adjacent UI state.

### H7-06 — Send tracker closure

- [ ] A normal acknowledged DM closes its tracker slot.
- [ ] A channel send without a relay confirmation reaches its bounded `NOT HEARD`/equivalent result.
- [ ] A late ACK can improve `NO CONFIRM` only where the design permits.
- [ ] An unrelated asynchronous failure does not terminate the active send.
- [ ] After each terminal result, another send can start; no slot remains leaked.

## 8. H8 — run after Task 8 lands

Use two same-team nodes plus one deliberately unreachable case. A third node or a topology that actually relays the channel post is needed to prove `PICKED UP`: `relayed` means first relay, not coverage, and it can legitimately remain false on a fully one-hop team even when every nearby node receives the emergency. Retain serial logs from all active nodes.

### H8-01 — Long-press threshold and cancellation

Test from each allowed UI state: status, inbox, team, send, both compose sub-views, and blank.

- [ ] A hold shorter than the fire threshold shows progress/cancel behavior but sends nothing.
- [ ] Releasing near 3.0 seconds cancels, displays `CANCELLED`, and returns after approximately one second.
- [ ] A deliberate hold past 3.5 seconds fires exactly one emergency sequence and reaches `SENDING...`.
- [ ] Releasing after fire does not generate a second emergency.
- [ ] Waking from blank does not accidentally shorten the safety hold.

Use timestamps from the serial log; visual impression alone is not sufficient for threshold acceptance.

### H8-02 — Recipient unavailable

Power off or isolate the intended receiving side.

- [ ] The UI shows the initial emergency transmission.
- [ ] The bounded retry schedule is followed.
- [ ] Exactly three accepted transmissions are consumed.
- [ ] The final state is `NOT HEARD`.
- [ ] No infinite retry or permanent `SENDING...` state occurs.
- [ ] No fourth emergency request is queued.
- [ ] A later new emergency can be started.

### H8-03 — Pickup and reply

With the receiving side available and the post actually relayed:

- [ ] The receiver sees the emergency.
- [ ] The relay event moves the sender to `PICKED UP`; the label is not `DELIVERED` because this proves a first relay, not team-wide coverage.
- [ ] A valid reply on channel 0 moves the sender to `REPLY` and displays the intended sender/text.
- [ ] Duplicate radio frames do not generate duplicate user-visible emergencies.

Control: repeat on a fully one-hop team. Receipt with `relayed=false` may end as `NOT HEARD`; record it as the accepted first-relay semantics, not as lost delivery.

### H8-04 — Temporary block/backpressure

Fire twice inside the configured 10-second channel minimum interval.

- [ ] The UI displays `BLOCKED` with a live countdown.
- [ ] The countdown follows the reported retry deadline.
- [ ] It fires automatically when the bounded delay expires.
- [ ] Attempts are neither lost silently nor multiplied.
- [ ] Permanent refusal ends safely and leaves the UI reusable.

### H8-05 — Outcome isolation

While an emergency is active, generate unrelated operations from the console or another UI path.

- [ ] While the emergency is `firing`, a canned channel post issued from the same node's console does not move the emergency state.
- [ ] An unrelated DM ACK/failure does not close or downgrade the emergency.
- [ ] A colliding `dst=0` asynchronous failure shape does not terminate the emergency.
- [ ] Only the intended correlated emergency outcome changes emergency state.

### H8-06 — Emergency priority

Start an acknowledged (`-a`) DM and initiate a valid emergency hold while that DM is still awaiting its end-to-end ACK.

- [ ] Emergency presentation preempts the ordinary view as designed.
- [ ] The emergency reaches `Node::on_command` in the same service pass, without waiting for the DM ACK or deadline.
- [ ] The DM is never duplicated.
- [ ] After the emergency terminates, the UI returns to a defined state.

Repeat with an outstanding canned channel post:

- [ ] The canned post's UI tracking is abandoned so the emergency can use the channel slot immediately.
- [ ] A late outcome from the abandoned canned post does not move the emergency.

### H8-07 — Conditional emergency location

Run both halves; neither is optional.

1. Configure non-zero `lat` and `lon`, then fire an emergency.
2. Confirm the receiver's emergency record carries that position.
3. Set both coordinates to zero, then fire another emergency.

- [ ] Non-zero configured coordinates are attached.
- [ ] With zero coordinates, the emergency still transmits without a `no_location` refusal.
- [ ] The positionless receiver record does not fabricate coordinates.

### H8-08 — Emergency display timing and frame integrity

- [ ] The complete emergency scene renders, not only the top eighth of the panel.
- [ ] An instrumented frame consists of eight page transfers.
- [ ] A blanked emergency state produces no repeated I²C traffic.
- [ ] On `PICKED UP`, the panel remains on for the current `kEmgHoldMs` of 30000 ms rather than the ordinary blanking interval.
- [ ] A reply arriving late restarts that 30000 ms hold window.
- [ ] After the retained emergency blanks, the first press wakes and restores the emergency result; it does not dismiss it.
- [ ] Once awake with the result seen, the next short press returns to the normal cycle.

### H8-09 — Unavailable battery-reader cadence

Before Task 9 replaces the unavailable reader, instrument the sample attempts if needed.

- [ ] A reader returning `<0` is attempted approximately every 30 seconds.
- [ ] Failure does not cause an ADC attempt every service pass.
- [ ] Attempts are deferred while the MAC is busy.

## 9. H9 — run after Task 9 lands

### H9-01 — Pre-reader behavior

Before a valid sample is available:

- [ ] The UI shows `--` or the specified unknown battery representation.
- [ ] It does not display a fabricated zero or stale compile-time value.

### H9-02 — Meter comparison

1. Power from a battery in a stable, safe state.
2. Measure battery voltage with the multimeter.
3. Wait for a firmware battery sample.
4. Record both readings and their timestamps.

- [ ] Displayed/console voltage is within approximately 50 mV of the meter under stable conditions.
- [ ] Repeated samples are plausible and do not jump wildly at idle.
- [ ] The conversion remains plausible near both a fuller and a lower safe battery voltage, if available.

If the result differs by a near-constant ratio, suspect divider/attenuation assumptions. Do not tune a compensation factor from one voltage point.

### H9-03 — Sampling cadence and MAC safety

- [ ] Sampling occurs at the designed slow cadence, approximately 30 seconds unless the final design says otherwise.
- [ ] It does not continuously poll the ADC.
- [ ] A sample is deferred while the MAC/radio is busy where required.
- [ ] Sustained radio traffic does not cause a reboot or obvious receive collapse.
- [ ] Once the radio becomes idle, a valid sample eventually appears.

### H9-04 — USB and battery transitions

- [ ] Battery-only boot reports a plausible battery voltage.
- [ ] Attaching USB does not cause an impossible voltage reading or reset loop.
- [ ] Removing USB with a charged battery does not corrupt the UI state.
- [ ] Any board-specific charging behavior is recorded separately from the firmware reading.

## 10. Evidence template

Copy one row per test into the test report:

| Test ID | Date/time | Environment | Firmware/version | Result | Evidence | Notes |
|---|---|---|---|---|---|---|
| H5-02 |  | `heltec_v3` |  | PASS/FAIL | photo + boot log | Vext LOW; reset GPIO 21 |

For any failure, preserve:

- the full serial log from boot through failure;
- `version`, `whoami`, and `cfg`;
- the exact PlatformIO environment;
- a display photo or short video;
- topology, frequency, SF, bandwidth, and peer firmware versions for radio cases;
- any temporary Vext/reset source change as a small diff;
- meter or logic-analyser readings where applicable.

## 11. Stop and report conditions

Stop expanding the experiment and report the evidence if any of these occurs:

- repeated reboot, watchdog, brownout, or stack failure;
- the panel remains dark after the isolated Vext and reset-pin checks;
- the OLED bus remains continuously active while the UI is blank or unchanged;
- radio CTS/ACK failures rise repeatably only while the display is painting;
- a short or waking press transmits an emergency;
- an unrelated send outcome marks an emergency as picked up or completed;
- retry state never terminates or permanently consumes a send slot;
- the battery reading is unsafe or physically implausible.

Do not compensate for these symptoms by widening timeouts, weakening correlation, or adding board-specific constants until the failing layer has been identified.
