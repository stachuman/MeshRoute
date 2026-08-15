<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# B197 + B198 — reliable button wake and prompt page-buffer rendering · **DESIGN ONLY** · 2026-08-14

**Status: ✅ APPROVED BY THE OWNER 2026-08-14 WITH FOUR REQUIRED EDITS — ALL FOUR ARE APPLIED BELOW — AND THEREFORE
READY FOR IMPLEMENTATION.** ⛔ **CORRECTED IN PLACE (§3 rule 3): this line previously read *"design for review. No
firmware, test, probe, or bench-script implementation is authorized by this file."* That was true when written and is
now FALSE.** The four edits are: **(1)** the inherited radio-idle duplication, §1 — ⛔ do not unify it; **(2)** the
GPIO0 strap facts, §3.1.1; **(3)** DIO1/GPIO coexistence marked as the principal unproven hardware assumption with a
fail-safe, §3.1.2; **(4)** the structural simulator-inertness proof, §7. ⚠ **No QG approval of an IMPLEMENTATION is
claimed — none exists yet.**
The two defects share one sleep-policy boundary and should be one implementation slice. B196 remains a separate,
undiagnosed panic investigation; this design neither attributes it nor proposes a fix for it.

⛔⛔ **STATUS CORRECTION 2026-08-15 — [[B200]]. THE IMPLEMENTATION OF §3.1 AS WRITTEN PANICKED THE NODE ON DEMAND**
(`Interrupt wdt timeout on CPU1`, reproduced by a long press), because §3.1 instructed a **level-triggered GPIO wake
armed ONCE AT BOOT and never disarmed**. **§3.1 and §3.4 are WITHDRAWN AND REWRITTEN IN PLACE below**; the fix
(arm at the sleep, disarm on wake, re-sample the pin, roll back partials, fail closed) is the §B200 slice. Every
other section of this design still stands. ⚠ Bench **23.1(b)'s pass does NOT transfer** — it was measured with the
wake armed permanently and must be re-run.

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

⛔⛔ **CORRECTED 2026-08-14 (QG). This paragraph previously read: *"The existing `mac_idle()` predicate in
`firmware_ui.cpp` is the same radio/queue predicate used by the sleep gate … This shared predicate remains
authoritative; no second radio-idle definition is introduced (U1)."* That implied ONE shared authority. THERE IS
NOT ONE.** `mac_idle()` (`src/firmware_ui.cpp:206`) and the sleep gate's inline terms (`src/fw_main.cpp:1426`) are
**two separate expressions of the same rule** — `!g_hal.radio().tx_busy() && g_hal.txq_depth() == 0` against
`!g_iradio.tx_busy() && g_hal.txq_depth() == 0`. They are **equivalent today** (§B105: `g_hal.radio()` IS
`g_iradio`, bound by reference at `fw_main.cpp:166`) but they are not one implementation.

⇒ **THE SLICE INHERITS THIS PRE-EXISTING DUPLICATION AND ADDS ONLY `mr_ui_allows_sleep()` TO THE SLEEP GATE.**
⛔ **DO NOT refactor or unify the two radio-idle expressions in this slice** — that is a separate change with its own
risk, and folding it in would violate C1.

★ **One comment IS corrected while the area is touched (V1), with logic UNCHANGED:** the block above `mac_idle()`
(`src/firmware_ui.cpp:199-200`) claims *"The SAME predicate fw_main.cpp:1406 uses to decide it may sleep (U1 — do
not invent a second one)"*. **Both halves are wrong: it is an equivalent expression rather than the same predicate,
and the line reference is STALE — the gate is at `:1426`, not `:1406`.** Correct the wording and the reference; change
no code.

## 2. Required behavior

1. A sleeping OLED node wakes on one short active-low GPIO0 press.
2. The wake does not turn that short press into a long gesture.
3. While the panel is intentionally lit, the CPU stays awake for the existing bounded 15 s attention window.
4. Sleep is also inhibited while button debounce/gesture classification or a logical page-buffer frame is active.
5. Once the panel is blank, input is idle, and no frame is open, the existing headless sleep policy resumes.
6. DIO1 RxDone wake and the one-second deadline timer remain unchanged. ⚠ **Their coexistence with GPIO wake is the
   design's PRINCIPAL UNPROVEN HARDWARE ASSUMPTION (§3.1.2), not established behaviour** — it is proved by the first
   metal gate, independently per wake source, or the design does not proceed as written.
7. Non-OLED profiles preserve the current sleep behavior exactly.

UART input alone remains unable to wake a sleeping node. Supported recovery is button wake, a fresh monitor/DTR
reset, or a console byte received during boot grace. The previously unreliable UART-wake mechanism is not reopened.

## 3. Design

### 3.1 Board-owned GPIO wake setup

⛔⛔ **WITHDRAWN AND REWRITTEN IN PLACE 2026-08-15 (§3 rule 3 — a withdrawal stays visible). THIS SECTION AS WRITTEN
CAUSED [[B200]]: THE NODE PANICS ON DEMAND WHEN THE BUTTON IS HELD.** The withdrawn text read:

> - `board_init()` continues to configure `MR_UI_BTN_PIN` as `INPUT_PULLUP`;
> - **after `board_init()`, `mr_ui_init()` calls `enable_button_wake()` once;**
> - the Heltec V3 implementation calls `gpio_wakeup_enable((gpio_num_t)MR_UI_BTN_PIN, GPIO_INTR_LOW_LEVEL)`, then
>   `esp_sleep_enable_gpio_wakeup()`;
> - both return values are checked. Failure prints one exact boot diagnostic, … and the OLED sleep hook remains
>   false for the boot;
> - the implementation does not replace, disable, or reorder the existing DIO1 `ext1` setup.

**WHY IT WAS WRONG, and it is one fact:** `GPIO_INTR_LOW_LEVEL` is **level-triggered** — light sleep admits no other
kind — and **a level interrupt cannot be cleared while the level persists.** Armed once at boot and never disarmed,
a **held** button re-asserts GPIO0's interrupt status continuously; RadioLib's `setPacketReceivedAction(mr_on_dio1)`
(`lib/hal/device_radio.h:75`) has installed the shared GPIO ISR, so that is an **interrupt storm on a running core**
⇒ `Guru Meditation Error: Core 1 panic'ed (Interrupt wdt timeout on CPU1)`, `Core 1 was running in ISR context`.
Reproduced on metal 2026-08-15 by one long press. ⓘ The failure was **structural, not incidental**: nothing in the
design gave the arm a lifetime, so no reviewer had a place to ask how it ended.

**WHAT REPLACES IT (implemented 2026-08-15; ⛔ no owner or QA approval of the implementation is claimed):** the
board canvas exposes a **PAIR** scoped to a single sleep, and `mr_ui_init()` arms **nothing**.

- `board_init()` still configures `MR_UI_BTN_PIN` as `INPUT_PULLUP`, and still arms no wake source;
- `mrui::arm_button_wake()` returns `armed` / `button_down` / `failed`. It **re-samples the pin first** and refuses
  (`button_down`) while the button is held — arming a LOW-level source on an already-LOW pin *is* the storm, and the
  debounced `InputFsm` is not an acceptable substitute because the press can land between the tick and the sleep;
- on `armed` it has called `gpio_wakeup_enable(..., GPIO_INTR_LOW_LEVEL)` then `esp_sleep_enable_gpio_wakeup()`, both
  return values checked, and **a partial arm is rolled back** (`gpio_wakeup_disable`) before reporting `failed`;
- `mrui::disarm_button_wake()` calls `gpio_wakeup_disable(MR_UI_BTN_PIN)` **and**
  `esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO)`, attempts both even if the first fails, and ANDs the two;
- `src/fw_main.cpp`'s `board_sleep_until()` arms **immediately before** `esp_light_sleep_start()` and disarms as the
  **FIRST statement after it returns**, ⛔ **inside** the existing `if (rtc_gpio_is_valid_gpio(LORA_PIN_DIO1))` guard —
  outside it, a board whose DIO1 is not RTC-capable would arm and then neither sleep nor disarm, i.e. B200 again.
  ★★ **"First" is load-bearing, not stylistic (round-2 correction): on a GPIO wake the button is BY DEFINITION still
  held low — that is what woke the node — so anything placed between the halt returning and `gpio_wakeup_disable()`
  runs with the storm condition live on a running CPU. Diagnostics come after the teardown, never before it;**
- the halt's **return code is checked**: `ESP_ERR_SLEEP_REJECT` / `ESP_ERR_SLEEP_TOO_SHORT_SLEEP_DURATION` mean the
  CPU never slept, so they are counted (`wksleepfail=`) and reported as "did not sleep" rather than being counted in
  `slept=` with a stale wake cause;
- anything but `armed` ⇒ **nothing was armed and the node does not sleep this pass**;
- an arm **or disarm** HARDWARE failure latches sleep off for the whole boot and says so once —
  `!! OLED button wake unavailable; sleep disabled` / `!! OLED button wake stuck armed; sleep disabled`;
- ⛔ neither call replaces, disables or reorders the DIO1 `ext1` setup.

★ **The fail-closed property is now stronger than a latch could make it: "sleeping with the button unarmed" is
unreachable by construction**, because the arm happens inside the sleep path and refuses the sleep on failure.

The active-low level comes from the existing `INPUT_PULLUP`/`button_pressed()` contract. The board TU owns the pin
and ESP-IDF calls; `fw_main.cpp` does not acquire an OLED feature conditional or a button pin number (U3) — the pair
reaches it through two new unconditional `lib/hal/mr_ui.h` seams with non-OLED no-op stubs.

#### 3.1.1 ⚠ GPIO0 IS THE ESP32-S3 BOOT STRAP PIN — recorded explicitly so the question is answered before it is asked

`platformio.ini:231-234` documents the hazard: GPIO0 held across a **reset** enters serial-download mode, so a user
holding the emergency long-press through a brownout gets *"a bricked-looking device"* (spec §10.1) — *"a hardware
behaviour to document, not one the UI can fix"*. **The facts, stated:**

- **GPIO0 is sampled as a boot strap ONLY during reset.**
- ⇒ **arming it as a RUNTIME light-sleep wake source does NOT worsen that behaviour** — nothing this design adds is
  sampled at reset.
- **Holding the button through a reset can still enter download mode.** That is unchanged and out of scope.
- ★ **BENCH INSTRUCTION (M2): if the device appears bricked, RELEASE GPIO0 AND RESET AGAIN.**

#### 3.1.2 ⛔⛔ THE PRINCIPAL UNPROVEN HARDWARE ASSUMPTION — DIO1 / GPIO wake coexistence

★★★ **This is the one thing in this design that could sink it, and it is recorded as an ASSUMPTION, not as
established behaviour.** The existing DIO1 wake uses the **RTC-domain** `esp_sleep_enable_ext1_wakeup`
(`src/fw_main.cpp:986`); the button would use the **digital-domain** `gpio_wakeup_enable` + `esp_sleep_enable_gpio_wakeup()`.
**Whether the two coexist in LIGHT sleep on the ESP32-S3 is UNVERIFIED on this hardware.**

- **The FIRST metal gate must prove INDEPENDENTLY that a sleeping node wakes from (a) active-low GPIO0 and
  (b) LoRa DIO1 / RxDone.** Neither result may be inferred from the other.
- ⛔⛔ **FAIL-SAFE, and it is not optional: if EITHER wake-source setup returns an error, the node must REMAIN AWAKE.
  It must NEVER enter sleep with the user button silently unarmed** — that is the present defect made permanent and
  invisible.
- ⛔ **Do NOT replace the existing DIO1 `ext1` mechanism unless coexistence FAILS.**
- ⓘ **If it fails:** using light-sleep GPIO wake for **both** pins, with **separate HIGH/LOW levels**, is a possible
  fallback — but it touches the radio wake path and therefore **requires its own radio regression gate**. It is not
  authorised by this design.

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

⛔ **STEPS 1-3 WERE REWRITTEN IN PLACE 2026-08-15 (§B200): the withdrawn version ASSUMED A PERMANENT ARM.** It read:
*"1. The blank, idle node enters light sleep through the existing gate. 2. GPIO0 going low wakes it immediately;
DIO1 and timer wake remain armed. 3. The next `mr_ui_tick()` samples the low level…"* — a sequence in which the
button is simply always a wake source, which is exactly the assumption that panicked the node. **Steps 4-7 are
unchanged and still stand.**

1. The blank, idle node reaches the existing gate. `board_sleep_until()` re-samples the button: **if it is held, the
   node does not arm and does not sleep this pass** (there is nothing to wake for, and arming would storm).
2. Otherwise the GPIO wake is armed **for this sleep only**, the CPU halts, and GPIO0 going low wakes it. DIO1 and
   the timer are armed by the same call, as before. **The FIRST thing that happens when the halt returns is the
   disarm** — a running core never carries the level, and on a GPIO wake the button is still held, so nothing may be
   done ahead of it. Only then is the halt's own verdict checked (a refused sleep is not a sleep) and
   `esp_sleep_get_wakeup_cause()` tallied into `wk_gpio/wk_ext1/wk_tmr`.
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

When this design is approved and implemented, run the native wrapper **and the direct binary** (⚠ the wrapper prints
a false *"0 test cases"*), both UI probes with their negative controls, `tools/warning_census.sh`, the three OLED
builds, and the three essential non-OLED builds. Finish with `git diff --check`, report every skipped metal item
honestly, and keep all work **uncommitted (D4)**.

★★ **SIMULATOR INERTNESS — USE THE ESTABLISHED STRUCTURAL PROOF, NOT A SINGLE SCENARIO RUN.** ⛔ The previous wording
(*"re-run the exact `s18` keystone once as an inertness check"*) is **withdrawn as insufficient**: a scenario run
behind a **stale binary** reproduces the old streams and looks exactly like *"nothing moved"* — that has already
produced one false conclusion in this arc. The required sequence is:

1. **record the PRE-slice `lus` md5**;
2. **invoke the canonical simulator rebuild**;
3. **confirm ZERO relevant build actions** — the changed files (`src/`, `variants/`, `lib/hal/mr_ui.h`) are **outside
   the simulator's sources**, which is *why* it is inert, and the build-action count is what demonstrates it;
4. **require the POST-slice `lus` md5 to be IDENTICAL**;
5. **retain `s18` as a smoke/keystone check — ⛔ never as the sole proof**, and read its expected value from
   `simulation/BASELINE.md` rather than hardcoding it (D1).

⛔ No 36-scenario rerun and ⛔ no anchor-table edit are justified or authorised.
