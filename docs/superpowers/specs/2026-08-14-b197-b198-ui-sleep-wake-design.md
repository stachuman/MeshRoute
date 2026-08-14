<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# B197 + B198 — reliable button wake and prompt page-buffer rendering · **DESIGN ONLY** · 2026-08-14

**Status:** design for review. No firmware, test, probe, or bench-script implementation is authorized by this file.
The two defects share one sleep-policy boundary and should be one implementation slice. B196 remains a separate,
undiagnosed panic investigation; this design neither attributes it nor proposes a fix for it.

## 1. Verified present state (V1)

| Fact | Current source | Consequence |
|---|---|---|
| UI attention blanks after 15 s | `src/firmware_ui_model.h` — `kBlankMs = 15000` | A dark panel does **not** prove the 30 s boot grace expired. |
| Headless sleep becomes eligible after 30 s | `src/fw_main.cpp` — `MR_BOOT_GRACE_MS = 30000` | Panel blanking and sleep eligibility are independent clocks. |
| ESP32 wake sources are DIO1 plus a ≤1 s timer | `src/fw_main.cpp` — `board_sleep_until()` | GPIO0 and UART are not wake sources. |
| The button is active-low GPIO0 and only polled | `variants/heltec_v3/board_ui.cpp` — `button_pressed()` | A short tap can occur wholly inside sleep; a long hold can be sampled on a timer wake. |
| One service pass pushes one 128-byte page | `variants/heltec_v3/board_ui.cpp` — `next_page()` | Eight pages become roughly eight seconds when sleep paces passes at one second. |
| The logical page-loop authority already exists | `src/firmware_ui_model.h` — `FrameGate::frame_open()` | Do not expose or duplicate board-private `s_painting`. |
| The pinned ESP32-S3 framework supports GPIO light-sleep wake | `driver/gpio.h` and `esp_sleep.h` | `gpio_wakeup_enable(..., GPIO_INTR_LOW_LEVEL)` followed by `esp_sleep_enable_gpio_wakeup()` is available. |

The existing `mac_idle()` predicate in `firmware_ui.cpp` is the same radio/queue predicate used by the sleep gate.
If MAC activity prevents a blank command or page advance, it also prevents that pass from sleeping. This shared
predicate remains authoritative; no second radio-idle definition is introduced (U1).

## 2. Required behavior

1. A sleeping OLED node wakes on one short active-low GPIO0 press.
2. The wake does not turn that short press into a long gesture.
3. While the panel is intentionally lit, the CPU stays awake for the existing bounded 15 s attention window.
4. Sleep is also inhibited while button debounce/gesture classification or a logical page-buffer frame is active.
5. Once the panel is blank, input is idle, and no frame is open, the existing headless sleep policy resumes.
6. DIO1 RxDone wake and the one-second deadline timer remain unchanged and coexist with GPIO wake.
7. Non-OLED profiles preserve the current sleep behavior exactly.

UART input alone remains unable to wake a sleeping node. Supported recovery is button wake, a fresh monitor/DTR
reset, or a console byte received during boot grace. The previously unreliable UART-wake mechanism is not reopened.

## 3. Design

### 3.1 Board-owned GPIO wake setup

Add a narrow `mrui::enable_button_wake()` board-canvas function beside `button_pressed()`:

- `board_init()` continues to configure `MR_UI_BTN_PIN` as `INPUT_PULLUP`;
- after `board_init()`, `mr_ui_init()` calls `enable_button_wake()` once;
- the Heltec V3 implementation calls
  `gpio_wakeup_enable((gpio_num_t)MR_UI_BTN_PIN, GPIO_INTR_LOW_LEVEL)`, then
  `esp_sleep_enable_gpio_wakeup()`;
- both return values are checked. Failure prints one exact boot diagnostic,
  `!! OLED button wake unavailable; sleep disabled`, and the OLED sleep hook remains false for the boot;
- the implementation does not replace, disable, or reorder the existing DIO1 `ext1` setup in
  `board_sleep_until()`.

The active-low level comes from the existing `INPUT_PULLUP`/`button_pressed()` contract. The board TU owns the pin
and ESP-IDF calls; `fw_main.cpp` does not acquire an OLED feature conditional or a button pin number (U3).

### 3.2 Read-only input activity

Add `InputFsm::active() const`. It is true exactly when:

```cpp
_raw || _stable || _pending_tap
```

This covers press debounce, a stable hold, release debounce, and the pending single/double-click decision. It becomes
false after the final release decision. `_armed` and `_fired` need no separate terms: while held, `_raw`/`_stable`
already cover them; after release they are historical state and must not hold the CPU awake.

### 3.3 One feature-neutral sleep hook

`lib/hal/mr_ui.h` gains a fifth unconditional seam, `bool mr_ui_allows_sleep()`:

- non-OLED inline implementation returns `true`;
- OLED implementation returns false until button-wake setup succeeds;
- after setup, it delegates to a pure UI policy using the existing authorities:

```cpp
model.state().blanked && !input.active() && !gate.frame_open()
```

Place that pure predicate with the UI model/`FrameGate`, where native tests can drive the real state machines. Reuse
the existing `FrameGate::frame_open()` accessor; do not add another accessor and do not export `s_painting` from
`board_ui.cpp`.

`fw_main.cpp` adds the hook to the existing sleep condition after `mr_ui_tick()` has serviced the current pass:

```cpp
if (may_sleep && mr_ui_allows_sleep() &&
    !g_iradio.tx_busy() && g_hal.txq_depth() == 0 &&
    !serial_has_input() && !mrble::connected()) {
    // existing deadline calculation, slept counter, and board_sleep_until()
}
```

The hook constrains `g_force_sleep` too: an explicit `sleep` request on an OLED build waits until the bounded UI
attention/frame/input work is complete. Non-OLED behavior is unchanged because its hook is always true.

### 3.4 Wake-to-gesture sequence

1. The blank, idle node enters light sleep through the existing gate.
2. GPIO0 going low wakes it immediately; DIO1 and timer wake remain armed.
3. The next `mr_ui_tick()` samples the low level. `InputFsm::active()` becomes true before debounce completes, so the
   node cannot sleep again between the edge and classification.
4. Release debounce and `_pending_tap` keep it awake through the double-click window.
5. A single tap is emitted as `short_press`, the existing model consumes that first waking press to unblank, and the
   existing 15 s attention window begins.
6. Page-buffer service passes run back-to-back while the panel is lit. Each pass still pushes at most one page and
   returns to radio service before the next page.
7. After blanking, with no active gesture and no open logical frame, `slept=` resumes increasing.

### 3.5 What does not change

- Keep U8g2 `_1_` page-buffer mode and one `next_page()` call per service pass.
- Do not use a full-frame buffer and do not push multiple pages in one call.
- Do not change the 15 s blank timeout, 30 s boot grace, one-second sleep cap, or emergency timing.
- Do not change app pushes, OLED strings/state semantics, retry/recovery, wire/NV formats, core protocol behavior,
  simulator outcomes, `sizeof(Node)`, or `sizeof(TxOutcome)`.

## 4. Implementation map

| File | Change |
|---|---|
| `variants/heltec_v3/board_ui.{h,cpp}` | Add checked active-low GPIO wake setup; keep `s_painting` private. |
| `src/firmware_ui_input.h` | Add read-only `InputFsm::active()`. |
| `src/firmware_ui_model.h` | Add the pure three-authority sleep predicate; reuse `FrameGate::frame_open()`. |
| `lib/hal/mr_ui.h` | Add the fifth hook and non-OLED `true` stub. |
| `src/firmware_ui.cpp` | Initialize wake, fail closed, and implement the OLED hook. |
| `src/fw_main.cpp` | Add the hook to the existing sleep gate only. |
| native/UI/board probes | Prove state semantics, wake setup, and runtime wiring with controls. |
| maintained bench script | Add only the metal-only coexistence/timing checks listed below (M2). |

## 5. Automated proof and controls

### Native

- `InputFsm::active()` is false at idle; true during press debounce, stable hold, release debounce, and a pending
  single/double decision; false after short, double, long-cancel, and long-fire release completion.
- The pure sleep policy is true only for `blanked && !active && !frame_open`; exercise every false term separately.
- Existing `FrameGate::frame_open()` lifecycle cases remain green.
- Controls must turn RED when `active()` ignores `_raw` or `_pending_tap`, or when any sleep-policy term is restored to
  a permissive constant.

### Durable host probes

- Board probe observes both ESP-IDF wake calls, GPIO0, low-level polarity, call order, and both failure returns.
- Feature probe observes fail-closed wake initialization plus lit, input-active, frame-open, and blank-idle sleep
  decisions through the real `mr_ui_allows_sleep()`.
- A source-wiring check pins `mr_ui_tick()` before the sleep gate, `mr_ui_allows_sleep()` in that gate, the exhaustive
  existing sleep body, and the non-OLED `true` stub.
- Controls must fail for deletion of the hook, inverted/permissive logic, wrong wake level, either wake call removed,
  wake failure allowed to sleep, use of `s_painting`, and replacement/removal of DIO1 wake.

## 6. Metal acceptance

Keep configuration and RF topology fixed while comparing before/after behavior:

1. After `slept=` is increasing, one short tap wakes the node and is not classified as a long press.
2. Emergency fire remains approximately 3.5 s from the debounced press.
3. With the MAC idle, a complete emergency frame reaches the panel in under 250 ms.
4. During repaint, a second node still completes radio RX and an RTS/CTS/DATA exchange; page pushes remain separated
   by service passes.
5. After the panel's 15 s blank, and after the 30 s boot grace, `slept=` resumes increasing.
6. Button wake followed by a console byte latches the existing host-awake behavior.
7. DIO1 still wakes the sleeping node for LoRa receive. A USB serial byte by itself is explicitly not a wake test.

## 7. Implementation gate

When this design is approved and implemented, run the native wrapper and direct binary, both UI probes with their
negative controls, `tools/warning_census.sh`, the three OLED builds, and the three essential non-OLED builds. Re-run
the exact `s18` keystone once as an inertness check; no 36-scenario rerun or anchor-table edit is justified because
the slice does not change simulator-compiled protocol code. Finish with `git diff --check` and report every skipped
metal item honestly. Keep all work uncommitted (D4).
