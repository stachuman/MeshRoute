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

**Current approved stopping point (2026-08-05): run H5, H6, and H7 through H7-09.** Task 7 and its B64/B113
repair slice have passed QG. Do **not** start H8 yet: that is the Task 8 emergency end-to-end qualification gate.
Do not start H9 until the Task 9 battery reader lands. Retired and conditional checks remain labelled in their own
sections; do not turn an intentionally unavailable later-stage behavior into an earlier-stage failure.

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

Expected on the currently approved Task 7 tree:

```text
phA5 board_ui probe: 38 passed / 0 failed / 38 total
structural: 10 passed / 0 failed / 10 total
wiring:      9 passed / 0 failed / 9 total
```

The wiring total includes W9, which pins B64's `TEAMMATE GONE, repick` rendering and suppressed highlight. The runner
also executes the negative control for each wiring check.

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

Verified 2026-08-05 references for the approved Task 7 + B64/B113 repair tree:

| Environment | RAM | Flash |
|---|---:|---:|
| `heltec_v3` | 214396 B | 1253732 B |
| `heltec_mobile` | 213916 B | 1247280 B |

The Task 7 hardware run should normally use `heltec_mobile`. Record the actual size printed by your build; small
toolchain/link-order flash movement is not by itself a functional failure, while an unexplained RAM change requires
review.

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

### H6-06 — RETIRED 2026-08-05 by UI-7 (the send path is BUILT; see H7). Kept for the audit trail only

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
- [ ] **§B101 (2026-08-05, CHANGED BEHAVIOUR — this row used to say the opposite):** long-press from inside a compose
      sub-view still reaches the alarm, and **the modal is now CLOSED by it**. After acknowledging the alarm you must be
      on the plain screen, **not** on a canned list with a row highlighted. ⇒ press **double** once more: on TEAM it
      must **re-open the list at `back, don't send`** and send nothing. If a message flies here, §B101 has regressed.
- [ ] **§B102 (2026-08-05, THE ONE THAT NEEDS A STOPWATCH AND IS METAL-ONLY):** the acknowledging short press must not
      work until the outcome has been **fully drawn**. Drive an alarm to a terminal outcome while the node is BUSY
      (start a `send` from the console a beat before, so the MAC-idle gate holds the paint), and press the button
      **immediately** — within the first frame. The outcome **must stay on the panel**; only a press after it is fully
      rendered clears it. ⚠ Before the fix this dismissed a distress result the operator never read. There is no console
      line for this: the panel is the entire instrument.

### H6-08 — Battery cadence with an unavailable reader

- [ ] `batt --` throughout. ⛔ **CORRECTED IN PLACE 2026-08-06:** this used to read *"(Task 9 has not landed; the
      reader answers 'unavailable' by design)"*. **Task 9 has landed.** On a healthy cell the expected reading here is
      now a **voltage**, and a `--` means either no measurable cell or the §UI-9 polarity refusal ⇒ guide **H9-05**.
- [ ] Instrument or trace that a sample is **attempted** once per **`kBattPeriodMs`** (`src/firmware_ui.cpp` — ★ read
      the value there; [[B120]] is what a restated one becomes), not on every service pass. The cadence gates on
      *attempted*, not on *succeeded*, precisely so an unavailable reader is not re-read for ever.
- [ ] No sampling starts while the MAC is busy.

### H6-09 — §B103: a distress REPLY must be TEAM-scoped ★★ THIS WAS A LIVE SAFETY DEFECT ON THIS BENCH

⚠ Until 2026-08-05 **any node in radio range posting plaintext on channel 0 — no team membership, no key — rendered as
"someone answered my distress call"**. This check is the proof it cannot any more, and it needs a SECOND node that is
**not** in this node's team.

- [ ] Node A (the panel, `team_id != 0`): long-press to fire an alarm and leave it on a retained outcome.
- [ ] Node B, **no team** (`create` without a team / `team_id == 0`): `send_channel 0 "hello"` — a plain GLOBAL post.
- [ ] Node A's console shows the post arriving (the `CH` unread count in the status bar increments by 1).
- [ ] ★★ **The panel must NOT show `REPLY`.** The retained outcome is unchanged. **If `REPLY` appears with node B's
      name, §B103 has regressed and no reply indication on this build can be trusted.**
- [ ] Now from node C, **a real teammate of A** on the same `team_id`: `send_channel -t 0 "on my way"`.
- [ ] The panel shows `REPLY` with C's name and the first 20 characters of the text. This half matters as much: the fix
      must not have made the reply indication unreachable.
- [ ] ⓘ On a node with `team_id == 0` the REPLY indication is unreachable **by design** — do not file that as a bug.

### H6-10 — §B107/§B108: nothing is lost while a frame is painting

Both are metal-only in the sense that matters: the LOGIC is now natively gated, but whether the **pixels** actually
change is only observable here.

- [ ] **§B107** — put the node under radio load (so frames take many ticks) and let an alarm outcome arrive during a
      repaint. The panel **must** end up showing the new outcome. Before the fix `PICKED UP` / `REPLY` / `FAILED` could
      be swallowed entirely and the panel kept the previous screen until something else invalidated it.
- [ ] **§B107** — while `ARMING` counts down under radio load, the digits must not skip.
- [ ] **§B108** — with unread mail, arrive on the INBOX screen and watch the `DM`/`CH` counts: they must stay up until
      the screen is **actually drawn**, then drop to 0.
- [ ] **§B108, the discriminating one** — while the INBOX frame is painting, have another node send one more message.
      After the frame settles the count must read **1**, not 0. A `0` here means a message was marked read that was
      never on the panel.
- [ ] **§B108** — let the panel blank on the INBOX screen with unread mail, then wake it: the counts must still be
      there. Before the fix they were zeroed into a dark panel.
- [ ] **§B108 round 2 — the same discriminator AT THE CAP.** The first fix survived above `999`: a capped counter
      cannot record an arrival, so the frame's completion marked it read anyway. The counts are now derived from an
      **uncapped arrival serial**, and `999` is only what the bar draws. See bench script **8.14** for the exact
      expected status-bar text and the >999 burst that reaches it.

### H6-11 — §R1/B109: a distress REPLY must LIGHT A DARK PANEL, and a stranger's post must not

⚠ Metal-only in the way that matters: the model half is natively gated, but **"did the screen actually come on"** has
no instrument other than this panel. Needs a real teammate node and a stranger node (the same pair as H6-09).

- [ ] Panel node: fire an alarm and let it reach a retained outcome (`PICKED UP` / `NOT RELAYED`).
- [ ] **Do not touch the button.** Wait out `kEmgHoldMs` **plus** the blank timer until the panel is fully DARK.
- [ ] Teammate node, same `team_id`: `send_channel -t 0 "on my way"`.
- [ ] ★★ **The panel must LIGHT BY ITSELF**, with no button press, showing `REPLY <name>: on my way`. Before this
      ruling it stayed dark and the answer waited for a press — on a rescue device, the one message that must not wait.
- [ ] It must light **once**: no flicker, no repeated on/off. The board latches `set_power_save`, so a per-tick write
      would show as visible strobing.
- [ ] Leave it alone again: after `kEmgHoldMs` from the **reply's** arrival it blanks again with `REPLY` retained (one
      press then restores the emergency screen, per §5).
- [ ] ★★ **THE NEGATIVE HALF, and it is the one that must not be skipped.** Repeat with the panel dark and a **stranger**
      node (`team_id == 0`) posting `send_channel 0 "hello"`. The `CH` count moves when you next wake it, but the panel
      **must stay dark**. ⛔ A panel lighting for a passer-by is both the §2.1 false-confirmation class in power form
      and a battery-drain vector; it means the wake was wired to the arrival instead of to the reply.
- [ ] ⓘ **Known and deliberate:** a `BLOCKED` / `PICKED UP` / `NOT RELAYED` / `FAILED` outcome arriving at a dark panel
      does **not** light it — R1 rules on the REPLY only. Do not file that as a bug; it is an open owner question.
- [ ] ⚠ **PROVISIONAL, same caveat as H8-03:** *what wakes the panel* is owner-ruled and must work, but *what counts as
      a reply* is an **inference** — any same-team channel post while an alarm is live qualifies, because the post
      carries no marker saying it is an answer. [[B118]] will replace that with a positive carrier. **The wake half is
      a real pass/fail; the "this post was a reply" half is not, until B118 lands.**

### H6-12 — §R2/B110: a DOUBLE under the emergency overlay must do NOTHING AT ALL

The overlay covers the body, so everything this check is about is invisible by definition — which is exactly why it
needs eyes on the panel and a second node watching the air.

- [ ] On TEAM (with at least one teammate listed), long-press to fire an alarm. The overlay owns the panel.
- [ ] **Double-press twice**, a second or so apart.
- [ ] ★★ **Expected: nothing whatsoever.** The overlay is unchanged, no compose list appears when it is later
      dismissed, and **no message is transmitted** — confirm on a second node that nothing arrived on channel 0.
      ⛔ A message arriving is the hidden mis-send this ruling closes.
- [ ] Repeat with the modal deliberately left open: on TEAM double-press to open the DM list, short-press once onto a
      **real** message, then **long-press to ARM only** (release before it fires — `ARMING` keeps the modal open by
      design). While `RELEASE!` is up, **double-press once**. ⛔ A DM flying here is the same defect via one press.
- [ ] Now confirm the rest of the contract still works: **long** re-fires from a sticky outcome (H6-07), and a **short**
      press still exits once the result has been drawn (H6-07 / §B71). ⓘ A double must never dismiss, even when the
      outcome has been fully presented — that duty was withdrawn by §B71.

## 7. H7 — run after Task 7 lands

★ **UI-7 landed 2026-08-05.** The UI-6 loud-refusal stub is gone: a send now really executes `send` / `send_channel`
through `mrfw::exec_command`. ⚠ **H6-06 is RETIRED by this slice** — it checked for the panel line `no send path: UI-7`,
which no longer exists; the `FAILED` screen now names the real refusal.

⚠ **What the panel can show that no automated gate can:** the composed COMMAND is asserted byte-for-byte natively
(`ui7-line:` cases), and the tracker/model transitions are natively driven. What only this bench can answer is whether
the composed line reached the radio, what the OTHER node received, and what the 128x64 panel actually rendered.

### H7-01 — Canned channel send (the happy path)

- [ ] Short-press to **SEND**, then `double` → the compose list appears, headed `to: team ch 0`.
- [ ] The initial highlight is `>Got your message`; `short` walks to `All good`, then `back, don't send`, then wraps.
- [ ] With `Got your message` highlighted, `double`.
- [ ] **Panel:** the list is REPLACED by the outcome, under the same `to: team ch 0` header. Expect
      `SENDING...` for one frame, then **`SENT, waiting`**, and the bottom line **`press = back`**.
      ★★ **THE `SENT, waiting` STEP IS §B113, AND IT WAS UNREACHABLE UNTIL 2026-08-05.** `ChanState::waiting` was
      assigned nowhere in the tree, so an accepted canned post stayed on `SENDING...` until either the ~36 s verdict or
      — first, on the common path — the 15 s auto-exit. ⇒ **if this step is missing and the panel sits on `SENDING...`
      right up to the settle or the exit, B113 has regressed.** The DM twin (H7-03) always worked; only the channel
      state machine was missing its acceptance arm.
- [ ] **Console (USB, second node):** the receiver logs the channel message with body `Got your message`.
- [ ] **Exactly ONE** channel operation is issued — check the sender's console shows one send, not two.
- [ ] Within ~36 s the outcome settles to **`PICKED UP`** (a relay was overheard) or **`SENT, no relay`**.
      ⚠ §B38: on a fully-1-hop pair `SENT, no relay` is CORRECT at 100 % delivery. Do not treat it as a failure.
- [ ] ⚠ If the modal auto-exits (15 s of no input) before that verdict arrives, the late verdict is ABANDONED by
      design — see H7-08. Press a button occasionally to keep it open if you want to observe the settle.

### H7-02 — Cancel a canned send

- [ ] `double` on SEND → compose list. `short` until `>back, don't send`. `double`.
- [ ] **Panel:** returns to the **SEND** screen (its parent), not to STATUS.
- [ ] **Console:** NO send line at all, on either node. This is the assertion — a quiet console is the result.
- [ ] Re-enter compose: the highlight is back on `>Got your message` and there is **no outcome text**.
- [ ] Leave the sub-view idle for 15 s instead: it exits by itself, again with **nothing sent**.

### H7-03 — DM compose and send

- [ ] `short` to **TEAM**, walk to a known teammate, `double` → header reads `to: <label>` for THAT peer.
- [ ] Highlight `>Are you OK?` and `double`.
- [ ] **Console (sender):** the issued line is `send <id> "Are you OK?" -t -a`. ⚠ **No `-e`** — the parser rejects it
      on an id target; encryption follows the node's own `e2e_dm` setting (spec §3.4).
- [ ] **Panel:** `SENDING...` → **`SENT, waiting`** → **`DELIVERED to <label>`** once the end-to-end ack returns.
      ★ `DELIVERED` appears in exactly one place in this design and this is it. A channel post can never say it.
- [ ] **Second node:** receives the body `Are you OK?`.
- [ ] Repeat with the peer POWERED OFF: the panel must reach **`NO CONFIRM`** (not `DELIVERED`, not a blank screen)
      once the e2e-ack deadline expires. ⚠ This can take a full minute — leave it, or press a button to hold the modal.
- [ ] Repeat on a node with `e2e_dm = 1` and NO stored pubkey for that peer: expect **`NO KEY`**.
      ★ It is a genuine dead end on-device (the node may never auto-issue `reqpubkey`), which is why it is named.

### H7-04 — Cancel DM compose

- [ ] Enter DM compose for a known peer, `short` to `>back, don't send`, `double`.
- [ ] **Panel:** returns to **TEAM** with the same teammate highlighted.
- [ ] **Console:** no `send` line. Nothing was transmitted.
- [ ] Re-enter compose for the same peer: highlight on `>Are you OK?`, no stale outcome text.

### H7-05 — Inbox presentation

- [ ] Receive at least one DM and one channel message on the device under test.
- [ ] `short` to **INBOX**.
- [ ] **Panel header:** `INBOX <shown>/<total>` — e.g. `INBOX 3/3`. ⚠ If more messages are stored than fit, `total`
      must exceed `shown`. **The cap must never be presented as the mailbox size.**
- [ ] Rows are tagged **`DM`** or **`CH<n>`**, followed by the clamped body and an age.
- [ ] **All DM rows come first, then all channel rows.** ⚠ This is BLOCK order, not chronological — the two sequence
      spaces are independent. Do not report it as a sorting bug.
- [ ] `short` walks the rows and leaves for SEND at the end, like TEAM.
- [ ] With NO durable inbox store installed the screen reads `no stored rows` **and still shows the live DM/CH
      counters**. ⚠ It must NOT say "no messages" — the node cannot know that.
- [ ] Send yourself a long message (>20 chars): the row is CLIPPED, and the rows above and below are undamaged.

### H7-06 — Send tracker closure

- [ ] After a DELIVERED DM, immediately compose and send another: it goes out. No slot is stuck.
- [ ] After a `NO CONFIRM` DM (H7-03's powered-off case), close the sub-view and send **any** other message.
      ★★ **This is the one that would fail silently:** an unconfirmed DM used to hold the single normal slot for ever,
      and every later send — canned post included — would simply never be issued. If the second message does not go
      out, STOP and report; that is the leak UI-7 closed.
- [ ] Fire the emergency (long press) while a canned post is outstanding: the alarm goes out immediately and does not
      wait for the canned post's verdict.

### H7-07 — §B69: an unconfirmed send must never read as SENT ★★ NEW, AND IT IS THE SLICE'S SAFETY POINT

★ **Why it needs the bench:** the two `ctr == 0` producers on this path are a pre-TX self-gate and a channel SEAL
FAILURE. Neither is reproducible from the console alone, and the panel is the only instrument for what the user is
told. **Hard to provoke deliberately — record it opportunistically if it appears.**

- [ ] Provoke a seal failure if you can (a node whose team channel key was removed after `create`/`join`), or simply
      watch for the state during H8's alarm work.
- [ ] **Panel (canned sub-view):** **`NOT CONFIRMED`** on the first line and **`no send handle`** on the second.
- [ ] ⛔ It must NEVER read `SENT`, and it must NEVER read `SENT, no relay`. Both would be claims the node cannot make:
      with no local handle it never listened, and on this `-t` line a zero ctr is a block or a seal failure, not a
      delegated success. **If you see either wording, STOP and report — that is the §2.1 false confirmation.**
- [ ] **Panel (emergency overlay), same condition:** headline **`NOT RELAYED`**, detail **`unconfirmed x3`** —
      NOT `no relay after 3`. The headline is deliberately the same as the measured case: the user's action is
      identical (do not assume help is coming), only the claim about what was measured differs.
      ⚠ **The headline is `NOT RELAYED`** — owner-ruled 2026-08-05 (register B117), 11 chars with one column spare in
      the 12-column large font. It says exactly what was measured (the relay did not happen) and nothing about receipt.
      ⛔ **Three wrong readings, and none of them is acceptable:** `NOT HEARD` is pre-ruling firmware · `NO RELAY` is the
      intermediate build carrying an 8-char string **no owner ever approved** (substituted by an implementing slice,
      superseded — not a fallback) · a CLIPPED `NO RELAY HEAR` is the first ruled 14-char wording, which is 140 px on a
      128 px panel. Report whichever you see rather than accepting it.

### H7-08 — the sub-view's lifetime bounds the outcome, and that is DELIBERATE

- [ ] Send a canned channel post and let the modal auto-exit (15 s, no presses).
- [ ] The late `channel_sent` verdict (up to ~36 s) is **not** shown afterwards, and the panel does **not** pop back.
      ★ That is the ruled trade-off: the sub-view is the only renderer of a normal outcome, so retaining the slot past
      it can only leak. **Report it only if the panel shows something rather than nothing.**
- [ ] Immediately send again: it works (this is H7-06's assertion from the other side).

### H7-09 — §B64: a teammate that LEAVES the roster must never inherit your DM ★★ NEW, OWNER-RULED 2026-08-05

★ **Why it needs the bench:** the model half is natively gated, but the *panel* half is not — and the panel half IS the
safety property. `src/firmware_ui.cpp` is compiled by neither the native suite nor the simulator, so only this bench can
confirm that the highlight really disappears and the reason really appears.
★ **The ruling:** the cursor tracks the TEAMMATE, not the row. A teammate who has gone ⇒ **refuse and repaint**, never
retarget. Before this, a cursor on row 2 meeting a shorter roster sent `"Are you OK?"` to **row 0** — a mis-send.

- [ ] **Setup:** three same-team nodes in range so TEAM lists at least two teammates (`T2/2` or better in the bar).
      Short-press to **TEAM** and walk the `>` marker onto the **LAST** teammate in the list.
- [ ] **Now power that teammate down** and wait for it to drop out of the roster (the bar's `T<shown>/<total>` falls;
      it follows `rt_team_count()`, so allow the routing aging interval).
- [ ] **Panel, the moment the row goes:** the list redraws with **NO `>` marker anywhere**, and the LAST body line reads
      exactly **`TEAMMATE GONE, repick`**.
- [ ] `double` now. **Panel:** unchanged — **no compose sub-view opens at all**. **Console (both nodes): NOTHING is
      sent.** ⛔ A `send <id> "Are you OK?" -t -a` appearing here is the B64 mis-send, live. **STOP and report.**
- [ ] `short` once to walk on. The `>` marker reappears on the next teammate and **`TEAMMATE GONE, repick` disappears**.
- [ ] `double`, then `double` again: the DM goes out, and the console line names **the teammate now under the marker** —
      confirm the id matches that row, not the one that vanished.
- [ ] **The REORDER half** (the case a clamp would get wrong): with three teammates listed, put the `>` on the middle
      one and note its label. Leave it for a while so the roster re-sorts as scores/ages move (or power a *different*
      teammate down and back up). **The `>` must stay on the teammate you chose, even if it changes row**, and the DM
      must go to that teammate's id. ⚠ If the marker stays on a fixed ROW while the names shift under it, identity
      tracking has regressed to index tracking.
- [ ] Cycle off TEAM and back while the message is up: it must **not** still be showing (it is retired on leaving).

## 8. H8 — run after Task 8 lands

Use two same-team nodes plus one deliberately unreachable case. A third node or a topology that actually relays the channel post is needed to prove `PICKED UP`: `relayed` means first relay, not coverage, and it can legitimately remain false on a fully one-hop team even when every nearby node receives the emergency. Retain serial logs from all active nodes.

★★ **TASK 8 NEEDED NO FIRMWARE — this group IS Task 8.** The plan's Task-8 Step 1 (render the eight emergency states)
landed with Tasks 1–7 and the §B115/§B117 slices; the shipped strings were verified against `src/firmware_ui.cpp`'s
`draw_emergency` on 2026-08-06, not against the plan. ⇒ **run this group and Task 8 is complete.**

★ **THE OWNER'S NINE VALIDATION CASES → THIS GUIDE.** Run these ten entries and all nine are covered:

| # | owner's case | entry |
|---|---|---|
| 1 | the **first** visible counter is `attempt 1 of 3` | **H8-10** (script 8.23) |
| 2 | the sequence ends at `attempt 3 of 3` | **H8-10** (script 8.23) |
| 3 | the terminal headline is `NOT RELAYED` | **H8-02** (script 8.24) |
| 4 | blocked countdown and automatic retry | **H8-04** (script 8.25) |
| 5 | blanked-panel emergency, and outcome wake | **H8-01** step 6 + **H6-11** (script 8.27 + 8.15) |
| 6 | emergency pre-empts an outstanding DM/channel send | **H8-06** (script 8.26) |
| 7 | location included only when available | **H8-07** (script 8.18) |
| 8 | the retained outcome holds for `kEmgHoldMs` | **H8-08** (script 8.10) |
| 9 | no repeated I²C while blanked | **H8-08** last group + script **8.4** |

⚠⚠ **CASE 5 IS HALF RULED AND HALF OPEN, AND THE TESTER MUST NOT CONFLATE THEM.** An incoming **REPLY** waking a dark
panel is owner-ruled and **must work** (H6-11). The four **LOCAL** outcomes — `BLOCKED` / `PICKED UP` / `NOT RELAYED` /
`FAILED` — arriving at a dark panel **do not wake it today, by design as shipped**, and widening that is an *open owner
item*. ⛔ **Do not file the second half as a defect from this run**, and do not tick H6-11 on the strength of a local
outcome lighting the panel.

⚠ **AND EVERY `REPLY` READING IN THIS GROUP RESTS ON AN INFERENCE — see the PROVISIONAL note in H8-03.**

### H8-01 — Long-press threshold and cancellation

Test from each allowed UI state: status, inbox, team, send, both compose sub-views, and blank.

- [ ] A hold shorter than the fire threshold shows progress/cancel behavior but sends nothing.
- [ ] Releasing near 3.0 seconds cancels, displays `CANCELLED`, and returns after approximately one second.
- [ ] A deliberate hold past 3.5 seconds fires exactly one emergency sequence and reaches `SENDING...`.
- [ ] Releasing after fire does not generate a second emergency.
- [ ] Waking from blank does not accidentally shorten the safety hold.

Use timestamps from the serial log; visual impression alone is not sufficient for threshold acceptance.

**Exact panel text, verified against `draw_emergency` (`src/firmware_ui.cpp`) 2026-08-06:**

| state | headline (`Font::large`) | detail (`Font::small`) |
|---|---|---|
| arming | `RELEASE!` | `EMERGENCY IN <n>` — `<n>` counts down |
| firing | `SENDING...` | `attempt <k> of 3` |
| blocked | `BLOCKED` | `retry in <n>s` |
| picked up | `PICKED UP` | `a relay heard it` |
| terminal, no relay | `NOT RELAYED` | `no relay after <k>` — or `unconfirmed x<k>` on the handle-less path |
| reply | `REPLY` | `<who>: <text>` |
| cancelled | `CANCELLED` | *(none)* |
| refused | `FAILED` | `BAD CMD` · `NO CRYPTO` · `NO FIX` · `QUEUE FULL` · `REFUSED`, plus the core's `CmdCode` name |

⚠ **The plan's Task-8 Step 1 prose is STALE where it disagrees with this table** — it says `RELEASE TO CANCEL` (17
chars; it would clip at the 12-column budget) and `REPLY <who>` (the name is on the *detail* line, not the headline).
**The table above is derived from the code; the plan is not.** Do not report a mismatch against the plan as a defect.

- [ ] **★ Case 5, first half — FIRE FROM A FULLY DARK PANEL.** Leave the node untouched until the panel is dark with
      no alarm outstanding, then long-press **in the dark** with no preparatory wake press. The panel must light, arm
      (`RELEASE!`) and fire (`SENDING...`) on that **one** gesture, the second node must receive the body, and the hold
      duration must match a lit-panel hold on the log timestamps.
      ⛔ **FAILURE:** a first long press that only wakes and needs a **second** press to fire — spec §5 exempts a long
      press from the consumed-waking-press rule precisely so a dark panel cannot swallow an alarm. Also failing: the
      console shows the send while the panel stays dark, or the hold is visibly shorter than from a lit panel.

### H8-02 — Recipient unavailable

Power off or isolate the intended receiving side.

- [ ] The UI shows the initial emergency transmission.
- [ ] The bounded retry schedule is followed.
- [ ] Exactly three accepted transmissions are consumed.
- [ ] The final state is `NOT RELAYED` (the panel string; the model state is still `Emergency::not_heard`).
- [ ] The detail line beneath it reads `no relay after 3` — or `unconfirmed x3` if every attempt came back with
      `ctr == 0` (the handle-less path, H7-07). The two are **different claims** and must not be conflated.
- [ ] No infinite retry or permanent `SENDING...` state occurs.
- [ ] No fourth emergency request is queued.
- [ ] A later new emergency can be started.

⛔ **FAILURE READINGS FOR THE HEADLINE — each names a different cause, so record which one you saw** (full rationale in
bench script 8.24): `NOT HEARD` ⇒ pre-ruling firmware was flashed · `NO RELAY` ⇒ the intermediate build was flashed,
and that 8-char string was **never approved by anyone** — it is superseded, not an acceptable fallback · a clipped
`NO RELAY HEAR` ⇒ the first ruled 14-char wording was used and does not fit the 12-column large font · `NOT RELAYE`
(ten characters shown) ⇒ the panel is clipping at a **narrower** budget than 12 columns, which the host probe's W11b
cannot see because W11b only counts characters.

### H8-03 — Pickup and reply

With the receiving side available and the post actually relayed:

- [ ] The receiver sees the emergency.
- [ ] The relay event moves the sender to `PICKED UP`; the label is not `DELIVERED` because this proves a first relay, not team-wide coverage.
- [ ] A valid reply on channel 0 moves the sender to `REPLY` and displays the intended sender/text — headline `REPLY`,
      detail `<who>: <text>` (the name is on the **detail** line, not the headline).
- [ ] Duplicate radio frames do not generate duplicate user-visible emergencies.

⚠⚠ **THIS `REPLY` CHECK IS PROVISIONAL, AND ITS PASS IS NOT VALIDATION OF THE REPLY PATH.** What the firmware does
today is an **INFERENCE, not a confirmation**: `ui_route_recv_push` (`src/firmware_ui_send.h`) lifts the alarm to
`REPLY` for **any** `channel_recv` on our team's channel while an alarm is live — the post carries **no marker saying
it is an answer**, so *"on my way"*, *"anyone got a spare battery"* and a stray automated post are indistinguishable to
the panel. ⇒ **an ordinary channel post becoming `REPLY` is the current behaviour, not proof the mechanism works.**
[[B118]] — the owner's app-code design, which gives the answer a **positive carrier** bound to the original post's
`channel_msg_id` — is what will replace the inference. It is **not built** and its authentication floor is **unruled**.
⛔ **Do not harden this line into a pass/fail acceptance criterion, and do not report its pass as "the reply path is
validated."** Record what you posted verbatim, so a later run against B118 firmware can be compared against it.

Control: repeat on a fully one-hop team. Receipt with `relayed=false` may end as `NOT RELAYED`, and that is **acceptable
ONLY WHEN NO REPLY WAS RECEIVED** — the earlier wording ("record it as the accepted first-relay semantics") MASKED
[[B114]], a live bench run in which the team received all three posts *and answered* while the panel reported no relay.
⇒ if ANY reply arrived, do **not** tick this line. Record which kind it was:
- a reply on the **team CHANNEL** must lift the panel to `REPLY`. It not doing so is a LIVE DEFECT — stop and report.
- a reply by **DM** legitimately does **not** lift it (owner-ruled 2026-08-05, B114 matter ②: a direct DM is not
  emergency confirmation). Record it against B116 (`HAVE`-digest evidence), which is the open work, not as a pass here.

### H8-04 — Temporary block/backpressure

Read `ch_min_ms` with `cfg get` first (default **10000**) — every deadline below is judged against that value, not
against the number in this sentence. Fire twice inside it.

- [ ] The UI displays headline **`BLOCKED`** with detail **`retry in <n>s`**, and `<n>` **decrements** — at least one
      visible change per second of wall time. ⛔ A frozen digit means the countdown is drawn from a stored duration
      instead of the live deadline.
- [ ] The console prints exactly `BLOCKED channel reason=min_interval — retry in <N> ms`, and the panel's countdown
      agrees with that `<N>`.
- [ ] ⓘ The synchronous answer to the blocked post is still `ack:queued` with **`ctr=0`** and nothing airs. That is the
      documented channel self-gate (`src/fw_main.cpp:1131`), **not** a second failure to report.
- [ ] It fires automatically when the bounded delay expires — the panel returns to **`SENDING...`** **with the button
      untouched**, and the second node receives the body.
- [ ] Attempts are neither lost silently nor multiplied. **Derive, do not assume:** the highest `attempt <k> of 3` the
      panel ever showed must equal the number of distinct `»tx M` ids in this node's console for this alarm. A block
      that aired nothing must not have spent one.
- [ ] Permanent refusal ends safely and leaves the UI reusable.

⛔ **FAILURE SHAPES, each a different cause:** stuck on `BLOCKED` past the deadline ⇒ the retry was never armed ·
`retry in 0s` sitting indefinitely ⇒ the deadline passed but nothing re-queued · resuming only after you press the
button ⇒ the retry is input-driven, which defeats the point on a device already put down · jumping straight to
`NOT RELAYED` / `FAILED` with no second `SENDING...` ⇒ the block was consumed as an attempt outcome instead of a
deferral.

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
- [ ] **★ Case 9 — a blanked emergency state produces no repeated I²C traffic.** Method, because "it looked idle" is
      not a measurement: put a scope or logic analyser on **SDA 17 / SCL 18**, let the panel blank, and watch for at
      least a minute. **Pass = one short burst at the blank transition, then silence.** ⛔ **FAILURE = a repeating
      burst** at the tick rate — that is `set_power_save` being written every pass instead of on the edge (spec §5),
      and on a rescue device it is a battery-drain vector, not a cosmetic issue. ⓘ The **decision** to blank edge-wise
      is already gated on the host (§UI-5 control C1, and the feature probe's MAC-idle/page checks); **only the bus
      itself is metal-only**, which is why this line is here and not a re-test of the corpus. Same check as bench
      script 8.4 — run it once, tick both.
- [ ] On `PICKED UP`, the panel remains on for **`kEmgHoldMs`** rather than the ordinary `MR_UI_BLANK_MS` blanking interval.
- [ ] A reply arriving late restarts that `kEmgHoldMs` hold window, measured from the **reply's own** arrival.
      ⚠ **READ THE CONSTANT, DO NOT RESTATE IT.** `kEmgHoldMs` is declared once, at `src/firmware_ui_model.h:210`, and
      that line plus one constants tripwire case are the **only** two places in the tree the digits may appear (§B78).
      The owner re-ruled this value once already and every prose copy of it went stale the same day — the two copies
      that used to sit on these very lines were among them. Look the value up before the run; do not write it back in.
- [ ] After the retained emergency blanks, the first press wakes and restores the emergency result; it does not dismiss it.
- [ ] Once awake with the result seen, the next short press returns to the normal cycle.

### H8-09 — Unavailable battery-reader cadence — ⛔ SUPERSEDED BY §9 (Task 9 has landed)

⛔ **CORRECTED IN PLACE 2026-08-06.** This entry opened *"Before Task 9 replaces the unavailable reader…"*. **Task 9
has landed** (`variants/heltec_v3/board_ui.cpp`'s `battery_sample_mv`), so that framing is no longer true and the
cadence questions now have a REAL reader behind them. The cadence itself is unchanged and the host gate still covers it.

- [ ] A reader returning `<0` is attempted once per **`kBattPeriodMs`** (`src/firmware_ui.cpp`) — ★ read the value from
      the constant; **do not** copy it into this document ([[B120]]: a restated value that is correct today is exactly
      what rots).
- [ ] Failure does not cause an ADC attempt every service pass.
- [ ] Attempts are deferred while the MAC is busy.

ⓘ **All three are covered by a HOST gate** — `tools/probe_firmware_ui/` measures the cadence including the
*attempted-vs-succeeded* clause, the MAC-idle suppression **and** its permissive direction (controls C6/C7/C8), and
since §UI-9 also what the cadence PUTS ON THE PANEL (P5, controls C13–C16). Per M2 the bench holds only what no
automated gate can reach ⇒ **this entry stays OPTIONAL.**
⇒ ★ **The part no host gate can reach is now §9 below**, and it is no longer hypothetical: the ADC pin, the divider
ratio, and — the one added by Task 9 — **whether GPIO 37's idle level exists at all**. Run **H9-01 … H9-05**.

### H8-10 — ★★ The alarm's attempt counter: READ THE FIRST NUMBER (owner cases 1 and 2)

★★★ **THIS IS THE ENTRY THAT CATCHES THE BUG THE LAST BENCH RUN MISSED.** The shipped panel stepped `attempt 2 of 3`
→ `3 of 3` → `4 of 3` against exactly three posts on the wire, and the owner confirmed **`1 of 3` was never shown**
(§B115). ⚠ **`2 of 3` and `3 of 3` are each individually plausible**, so a check that asks *"does it say N of 3?"*
**passes on the bug**. Only the **first** reading discriminates — do not start watching at the second attempt.

★ **Host coverage, so this stays residue:** the ordinal is computed in the model and the string is built by one pure
formatter, both natively asserted down to the visible bytes. **What no host gate reaches:** whether those bytes are
what the *panel* actually paints — `probe_firmware_ui` counts draw **calls**, never their text (register B104).

1. Isolate or power off the receiving side so the alarm runs its full budget (the H8-02 rig).
2. Long-press to fire, and watch the overlay **from the very first frame it appears**. Film it if the panel is small —
      a missed first frame is an unrun test, not a pass.
3. **Expected, in order, one per accepted transmission, headline `SENDING...` throughout:**
   **`attempt 1 of 3`** → `attempt 2 of 3` → `attempt 3 of 3`, then the terminal screen of H8-02.
4. Cross-check on the same node's console: the number of **distinct `»tx M` ids** for this alarm must equal the
   **highest ordinal** the panel showed.
5. ⛔ **FAILURE SHAPES:** `attempt 2 of 3` on the **first** post ⇒ the §B115 uniform `+1` is back · anything above
   `3 of 3` (`4 of 3`) ⇒ the same defect seen from the other end · three ids on the console but four ordinals on the
   panel ⇒ the counter is counting requests instead of accepted transmissions · a counter that **stalls** on one value
   across three console `»tx M` ids ⇒ the ordinal stopped tracking `_tries`.
6. ⓘ The counter is deliberately **not clamped** — that rawness is the only reason the original defect was ever
   visible. If you see `4 of 3`, **report it**; do not write it off as cosmetic.
7. ⓘ Re-fire from the sticky terminal screen (a fresh long press) and confirm it opens at **`attempt 1 of 3`** again —
   a new alarm resets the budget.

## 9. H9 — run after Task 9 lands · ★★ TASK 9 HAS LANDED, SO THIS SECTION IS NOW THE ACCEPTANCE RESIDUE

★★ **WHY THIS SECTION IS THE WHOLE POINT OF TASK 9's BENCH DEBT.** The host gates measure the cadence, the MAC-idle
suppression, the enable→sample→disable ordering, both polarity worlds, the plausibility window and the `--` rendering
(`tools/probe_board_ui/` P6e–P6g + P8a–P8ab, controls C7a–C7p; `tools/probe_firmware_ui/` P4/P5, controls C6–C8/C13–C16).
**Four things they structurally cannot reach, because the shim invents the raw counts and the pin levels:**

| # | only metal can answer | entry |
|---|---|---|
| 1 | is the combined ADC scale (`kVbatAdcScale`) right for *this* board revision, and how much of any error is ADC calibration rather than the resistors? | **H9-02** |
| 2 | does the control line have a DEFINED IDLE LEVEL at all, and is the divider actually parked OFF while a reading is being taken? | **H9-05 A/B** |
| 3 | when the firmware **REFUSES** (permanent `--`), is the divider off? — the **fail-safe park**, whose level is documented-inactive for **V3.2+ only** | **H9-05 C** ★NEW |
| 4 | does the sampling burst disturb the radio in the real timing? | **H9-03** |

⚠⚠ **NAME THE CONSTANTS, NEVER RESTATE THEIR VALUES.** Every number below is read from source at run time:
`kBattPeriodMs` (`src/firmware_ui.cpp`) · `kVbatAdcScale` / `kAdcRefV` / `kAdcFullScale` / `kAdcBits` / `kAdcSamples` /
`kBattMinMv` / `kBattMaxMv` / `kAdcCtrlFailsafePark` (`variants/heltec_v3/board_ui.cpp`). [[B120]] was the **third**
violation of this rule and it survived review because the restated value was correct *on the day*.
ⓘ **`kVbatDivider` was RENAMED to `kVbatAdcScale` 2026-08-06** ([[B126]]) — it was never a resistor ratio; see H9-02.

### H9-01 — Nothing measured yet must read as `--`, never as a number

**Do:** flash a Task-9 build and watch the status bar before the first successful sample completes.

**Expected — exact text.** The status bar's last field is `--`. On the STATUS screen the body line reads exactly
`batt --`. **Nothing anywhere on the panel matches the shape `<digit>.<digit>V`.**

**What a FAILURE looks like:**
- `0.0V` in the bar, or `batt 0mV` in the body → the plausibility window (`kBattMinMv`/`kBattMaxMv`) is not being
  applied, or a raw-0 read is being rendered. **This is the defect class the window exists for** — a display-shaped
  field asserting a measurement it does not have.
- Any plausible-looking voltage appearing *instantly* at boot before a sample is due → a fabricated default.
- The bar shows a **percentage** → ruled out (plan Task 9 Step 3, spec §3.3): volts or `--`, never a percentage.

### H9-02 — Meter comparison ★ THE CHECK THAT VALIDATES `kVbatAdcScale`, AND THE ONLY ONE THAT CAN

⛔ **CORRECTED 2026-08-06 ([[B126]]) — WHAT THIS ENTRY IS COMPARING AGAINST.** The constant used to be called
`kVbatDivider` and was documented as *"VBAT / V(ADC) — a PER-REVISION property"*, which sent the tester after **resistor
tolerance**. It is not a resistor ratio: the V3 network is **VBAT — 390 kΩ — GPIO1 — 100 kΩ — GND**, a physical ratio of
**4.9**, and the shipped constant is **4.9 × ≈1.106** — the extra ~10.6 % is an **empirical ADC attenuation /
full-scale correction** against the nominal `kAdcRefV / kAdcFullScale` the formula assumes.
⇒ ★ **A meter discrepancy is an ADC-CALIBRATION suspect at least as much as a divider-tolerance one**, and the two are
told apart by shape, not by size — see the failure list below. ⓘ Provenance of the 390 k/100 k figure is
third-party-from-schematic (the V3 community and `ropg/heltec_esp32_lora_v3`), **not** a Heltec spec sheet; the
HTIT-WB32LA_V3.2 PDF was fetched and is not machine-readable.

1. Power the node from a **battery** in a stable, safe state, **USB detached** (USB changes what the divider sees —
   see H9-04).
2. Measure the cell terminal voltage with the multimeter. Record it.
3. Wait for one firmware battery sample (at most one **`kBattPeriodMs`** period plus an idle window).
4. Record the panel reading and the meter reading with timestamps.

**Expected — the agreement window, stated exactly.** The panel renders **one decimal** (`fmt_volts` is `%u.%uV`,
truncating), so the comparison must be made against that resolution and not against a fiction of precision:

- **PASS:** `panel_volts <= meter_volts` **and** `meter_volts - panel_volts < 0.150 V`.
  ⓘ Derivation, so the number is not taken on faith: the display truncates to 100 mV (up to **−99 mV** by
  construction), and Task 9's own budget for the analogue chain — divider tolerance, ADC INL/offset, and the mean of
  **`kAdcSamples`** — is **±50 mV**, the figure plan Task 9 Step 2 and script 8.6 already state. 99 + 50 ⇒ **150 mV**,
  one-sided low. ⚠ The panel reading **above** the meter is NOT within tolerance in either direction: truncation cannot
  produce it, so it means the ratio is wrong.
- **PASS:** repeated samples at idle stay within **one display step** (0.1 V) of each other.
- **PASS (if a second safe voltage is reachable):** the agreement holds at a fuller **and** a lower cell voltage.

**What a FAILURE looks like, and what each shape means:**
- A **near-constant RATIO** error (e.g. every reading ≈ 0.82× or ≈ 1.22× the meter), holding at **both** voltage points
  → the combined `kVbatAdcScale` is wrong for this board. ⛔ **Record the measured ratio at BOTH points; do NOT tune the
  constant from one voltage point.** ⚠ **Do NOT report this as "the resistors are out of tolerance"** — a proportional
  error is equally consistent with ADC full-scale/attenuation error, and 10 % of the shipped constant already *is* that
  correction. The discriminator is the next bullet.
- An error that **grows or shrinks with voltage** (a ratio that is not constant across the two points), or a fixed
  **offset** in mV rather than a ratio → **ADC calibration / INL / offset**, not the divider. A resistor network cannot
  produce either shape. ⓘ To separate them further you need the ADC node itself: measure `MR_UI_VBAT_READ` to ground
  during a burst and compare it with meter\_VBAT ÷ 4.9. **Node matches the physical divider but the panel does not ⇒ the
  fault is downstream, in the ADC or the constant, not in the resistors.** That measurement is H9-05 part A's rig.
- The panel shows `--` while the meter reads a healthy cell → **go to H9-05 first.** The most likely cause is the
  polarity/floating refusal, not the scale.
- Readings jump by more than one display step at idle → the sampling burst is picking up switching noise; note whether
  they correlate with radio activity and cross-check H9-03.

### H9-03 — Sampling cadence and MAC safety, on the real timing

**Do:** with a serial log running, observe for at least three cadence periods; then repeat under a sustained DM load.

**Expected:**
- [ ] A sample is taken about once per **`kBattPeriodMs`** — not per service pass.
- [ ] Under sustained TX the sample is **deferred**, and one appears promptly once the radio goes idle.
- [ ] No CTS-timeout regression versus the same radio load with the reader disabled.

**What a FAILURE looks like:** a CTS-timeout or delivery regression that appears **only** with a battery-capable build
⇒ the burst is landing inside an exchange. ★ The relevant number is `cts_to_data_gap_ms` (config), not a figure quoted
here. ⓘ Task 9 deliberately imports **no settle delay** into the burst (spec §7 forbids V4's `delay(10)`); a regression
here would mean the burst itself is too long, which is a design question, not a tuning one.

### H9-04 — USB and battery transitions

- [ ] Battery-only boot reports a plausible voltage (and H9-02's window still holds).
- [ ] Attaching USB does not produce an impossible reading or a reset loop. ⓘ A reading that moves on USB is expected —
      the charger drives the cell node — and is **not** a firmware defect; record it, do not "fix" it.
- [ ] Removing USB with a charged battery does not corrupt the UI state.
- [ ] Any board-specific charging behaviour is recorded **separately** from the firmware reading.

### H9-05 — ★★ THE ADC CONTROL LINE: IS ITS IDLE LEVEL REAL, AND IS THE DIVIDER PARKED OFF? (★NEW, Task 9)

★★ **WHY THIS ENTRY EXISTS, and it is the one bench check Task 9 genuinely added.** `MR_UI_ADC_CTRL` (GPIO 37) is a
CONTROL line, not the ADC input. The firmware resolves its polarity by **probing** the idle level, because Heltec
inverted it past rev 3.2 while keeping the "V3" name. ⛔ **Nothing in this tree or in the vendor port establishes that
the line HAS a defined idle level** — the reference port reads it under a bare `INPUT` (no pull) on two different
boards and documents no pull, and its own V4 board drops the probe and hardcodes the level. If the line floats, a
naive probe is a **coin flip**, and the losing side does not show a wrong voltage — **it parks the divider ENABLED, a
continuous drain on a battery-powered safety device.** ★ This is [[B90]]'s Vext problem restated, and B90 closed
cleanly only because *"reproduce the proven level"* was kept distinct from *"we know the rail."*

⇒ Task 9 probes the line **twice, under opposite internal pulls**, and treats a disagreement as *floating ⇒ refuse*
(`s_adc_polarity_known` stays false, the reader answers unavailable, the panel shows `--`). **This entry is what
falsifies that**, and it is cheap.

⛔⛔ **REWRITTEN 2026-08-06 — THE PREVIOUS VERSION OF THIS ENTRY COULD NOT FAIL, and its central assertion was false.**
Kept here as the audit trail, not as instructions:
> ⛔ **SUPERSEDED (A):** *"FAILURE — the important one: the line sits at the level the firmware treats as ACTIVE. Read
> `s_adc_active_high` for the build under test."* — **`s_adc_active_high` is a file-static in
> `variants/heltec_v3/board_ui.cpp`. It is not printed, not a console field and not reachable by a tester**, so the
> only stated failure condition was unreadable. What remained was *"the line toggles"*, which **does not distinguish a
> divider parked OFF from one parked ON** — both toggle.
> ⛔ **SUPERSEDED (B):** *"This direction fails SAFE (a refusal, never a wrong number **and never a leak**)."* —
> **DISPROVEN.** The refusal path parked GPIO 37 at the **V3.2 MEASURING** level, so the "safe" direction was itself
> the leak ([[B123]] round 2). The fail-safe park now targets the documented-inactive level, and **part C below is what
> falsifies THAT.**

★★ **THE MEASUREMENT THIS ENTRY NOW TURNS ON IS THE ADC NODE, NOT THE CONTROL PIN.** Whether GPIO 37 is HIGH or LOW
tells you nothing until you know the revision's convention; whether the **divider is conducting** is directly
observable and revision-independent. ⓘ Read both pin numbers out of `[env:heltec_v3]` in `platformio.ini`
(`MR_UI_ADC_CTRL`, `MR_UI_VBAT_READ`) rather than assuming them.

**Before any of A/B/C: record the board revision** from the silkscreen, and the convention it implies —
**pre-3.2: LOW measures · V3.2 and later: HIGH measures** (Heltec hardware update log, V3.2: *"Modified voltage
detection circuit, now need to pull up the ADC_Ctrl(GPIO 37)"*). Everything below is read against that line.

**A — is the divider conducting between samples? (the direct test; do this one first).**
1. Battery-powered, USB detached, node booted and idle, panel showing a **battery reading** (a number, not `--` — if it
   is `--`, this build refused and you are in part **C**, not A).
2. Meter in **DC volts** on the **ADC input** (`MR_UI_VBAT_READ`) to ground, watched **between** samples — i.e. most of
   the time; a burst is one `kBattPeriodMs` apart.
3. Meter in **DC volts** on `MR_UI_ADC_CTRL` to ground, same window. Record **both** levels and which is which.

- [ ] **PASS:** between samples the ADC node sits at **~0 V** (the divider's low leg with no source above it), and rises
      to roughly **VBAT ÷ 4.9** — a fraction of a volt, neither rail — only during the once-per-cadence burst. The
      control pin rests at the **non-measuring** level for the recorded revision and flips for the burst.
- ⛔ **FAILURE — THE ONE THIS ENTRY EXISTS FOR:** the ADC node sits at **VBAT ÷ 4.9 continuously**, between samples as
  well as during them ⇒ **the divider is permanently enabled** and the cell is draining through it. Cross-check: the
  control pin will be resting at the **measuring** level for the recorded revision. ⛔ **Stop and report** — a
  battery-life defect, invisible on the panel.
- **FAILURE:** the ADC node never rises ⇒ no sample is happening, or the divider is never enabled (cross-check H9-03
  and H9-01: a panel reading with a dead node would mean the number is not coming from this net at all).
- **FAILURE:** the control pin never flips ⇒ no sample is happening (cross-check H9-03).
- **FAILURE:** the control pin rests at the measuring level **and** the ADC node still reads 0 V ⇒ the two do not agree;
  the recorded revision convention is wrong for this board, or the control drives something else. **Report both
  readings** — do not pick whichever supports the firmware.
- ⓘ **If a µA-capable meter in series with the cell is available**, the same fault reads as a **standing** current of
  roughly VBAT ÷ 490 kΩ (single-digit µA) that does **not** disappear between samples. That is a confirmation, not a
  substitute: it is small against the node's own draw, and the ADC-node reading is the discriminating one.

**B — is the idle level externally defined? (the falsification of the probe's premise).**
1. Power the node down completely (battery out, USB out).
2. Meter in **resistance** mode: measure `MR_UI_ADC_CTRL` to **3V3** and to **GND**.

- [ ] **PASS:** one of the two reads a finite resistance (a pull resistor is present) and the other reads open. **Record
      which side and the value** — that is the first direct evidence anyone in this project will have had for this pin,
      and it retires the assumption. ⓘ Cross-check it against A: a pull to **3V3** should pair with a firmware that
      resolved ACTIVE = LOW; a pull to **GND**, with ACTIVE = HIGH.
- **FAILURE / INCONCLUSIVE:** both sides read open ⇒ **the line genuinely floats and the probe's premise is false.**
  Expected firmware behaviour is then the refusal (panel `--` for ever, **no** conversion taken) **and** the fail-safe
  park of part C. ⛔ **Report it — do not work around it.** The owner's alternative (a build constant carrying the
  measured value, the `kVextOnLevel` / `LORA_TX_POWER` precedent) is recorded as owed in `simulation/BASELINE.md`'s
  §UI-9 note and is **not** implemented, because spec §7 and plan Task 9 both say "do not hardcode".
- **FAILURE:** *both* sides read a finite resistance ⇒ the meter is reading through the powered-down SoC, not a pull.
  **The board is not fully de-energised** — remove the cell and re-check before recording anything from B.

**C — ★★ THE FAIL-SAFE PARK: when the firmware REFUSES, is the divider actually off?** ([[B123]] round 2 — this is the
part that did not exist, and the defect it catches shipped.)

★ **Run this whenever the panel shows a permanent `--`** — that is the refusal, i.e. the two-pull probe found the line
floating and `kAdcCtrlFailsafePark` (`variants/heltec_v3/board_ui.cpp`) chose the park. **This is exactly the path with
no detected polarity to fall back on, so it is the one that must be measured rather than reasoned about.**

1. Battery-powered, USB detached, panel showing `--` and never a number.
2. Meter in **DC volts** on the **ADC input** (`MR_UI_VBAT_READ`) to ground. Watch for at least two `kBattPeriodMs`.
3. Meter in **DC volts** on `MR_UI_ADC_CTRL` to ground.

- [ ] **PASS:** the ADC node reads **~0 V and never moves** (no burst at all — the refusal takes no conversion), and the
      control pin sits at the **non-measuring** level for the recorded revision.
- ⛔ **FAILURE — THE SHIPPED DEFECT:** the ADC node sits at **VBAT ÷ 4.9** continuously ⇒ the refusal path parked the
  divider **ENABLED**. Record the control pin's level and the board revision. ⛔ **Stop and report:** the fail-safe
  constant is wrong for this revision, and the file says so may happen — it targets **V3.2 and later** and is **not**
  claimed safe on a pre-3.2 board.
- **FAILURE:** the ADC node shows periodic bursts while the panel says `--` ⇒ the refusal is not being honoured; the
  reader is sampling anyway (host cover for this is `probe_board_ui` P8w, so a red here means the board build and the
  probed source have diverged).
- ⚠ **NOT a failure of C:** a permanent `--` on a board whose cell the meter says is healthy. That is the **weak-pull
  false negative** of part B — an external pull weaker than the ESP32-S3's internal ~45 kΩ reads as "floating". It costs
  a reading, not a leak **provided C passes**; ⛔ it is **not** self-evidently leak-free, which is precisely what the
  superseded text above got wrong. **Run C, do not assume it.**

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
