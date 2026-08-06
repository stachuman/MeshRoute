// MeshRoute — variants/heltec_v3/board_ui.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Heltec WiFi LoRa 32 V3 board-UI port: the ONLY translation unit in the tree that knows U8g2, I2C, the user-button
// GPIO, the battery ADC and the panel power rail exist. Compiled ONLY when MR_FEAT_OLED=1 — today the heltec_v3 env
// and the two that extend it (gateway_heltec / heltec_mobile). On every other profile this whole TU is empty and
// fw_main uses mr_ui.h's inline no-ops, so the call sites stay unconditional.
// Plan Task 5 = spec slice UI-5 (`docs/superpowers/plans/2026-07-31-onboard-oled-ui-phase-a.md`).
//
// ★ WHAT IS DONE AND WHAT IS NOT — stated here because docs rot and code is read:
//   DONE      the display-independent canvas of board_ui.h — panel bring-up, page-chunked paint, edge-triggered
//             blanking, the user button. Nothing above this file may see U8g2, and nothing here may see a "screen".
//   DONE      battery_sample_mv() is REAL as of plan Task 9 / slice UI-9 (spec §7): auto-detected ADC_CTRL polarity,
//             divider enabled only around the burst, mean of 8 samples, NO settling delay. It still answers `-1` when
//             the reading is not a battery (see the plausibility window), because `--` is this project's rule for an
//             unavailable reading (console_json.h:137), never a plausible wrong number.
//   NOT DONE  nothing in this TU can prove the ADC SCALE or the detected POLARITY are right for the board in the
//             operator's hand — both are reproduced from a working port, and only a multimeter closes that
//             (docs/2026-07-31-bench-test-script.md Part 9; guide H8-09/H9-01).
//   NOT DONE  the FAIL-SAFE PARK for a floating control line (kAdcCtrlFailsafePark) is documented-inactive for
//             **V3.2 and later only**. It is NOT proven safe on a pre-3.2 board, and this file does not claim it is;
//             guide H9-05 / script 8.28 are the falsification. See [[B123]] round 2 at the constant.
//   DONE      §B91 (Task 6): board_init() now REPORTS whether the panel ACKed — an I2C address probe, the same
//             mechanism MeshCore uses (SSD1306Display::i2c_probe), because U8g2's begin() always returns 1. The canvas
//             still owns no report CHANNEL: src/firmware_ui.cpp turns the bool into a console line.
//   GONE      the three TEMPORARY mr_ui_* hooks that used to sit at the bottom of this file. ★ Task 6 took ownership:
//             they are DEFINED IN src/firmware_ui.cpp NOW, and defining them in both places is a duplicate-symbol
//             link failure. A board file must never see a `Push` or decide when to paint (spec §2.1, §5; rule U3).
#include "mr_features.h"

#if MR_FEAT_OLED

#include <Arduino.h>       // pinMode / digitalRead / digitalWrite + the LOW/HIGH/INPUT_PULLUP levels. U8g2 pulls this
                           // in transitively (U8x8lib.h:43), but this TU uses it directly, so it says so.
#include <Wire.h>          // §B91: the panel-ACK probe. U8g2 already links Wire on this env (U8x8lib.cpp:52 includes
                           // it unconditionally) and OWNS Wire.begin(), so this adds no dependency — only a use.
#include <U8g2lib.h>
#include "board_ui.h"

#ifndef MR_UI_BTN_PIN
#  error "MR_UI_BTN_PIN is not defined — the board env must supply the user-button GPIO (platformio.ini, [env:heltec_v3])"
#endif
// §UI-9 (plan Task 9): the battery pins arrive WITH the reader, by the standing "no config before its reader" rule.
// C2 — fail loud rather than defaulting: a wrong ADC pin silently reads a different net, and a defaulted control pin
// would leave a divider enabled for ever (a standing current draw on a safety device).
#ifndef MR_UI_ADC_CTRL
#  error "MR_UI_ADC_CTRL is not defined — the board env must supply the battery-divider CONTROL GPIO (platformio.ini, [env:heltec_v3])"
#endif
#ifndef MR_UI_VBAT_READ
#  error "MR_UI_VBAT_READ is not defined — the board env must supply the battery ADC input GPIO (platformio.ini, [env:heltec_v3])"
#endif

// ---- board table -------------------------------------------------------------------------------------------------
// Recovered from MeshCore's WORKING Heltec V3 port, not from datasheet reading (the provenance rule spec §10 states):
//   panel I2C   SDA 17 / SCL 18  — ~/MeshCore/variants/heltec_v3/platformio.ini PIN_BOARD_SDA / PIN_BOARD_SCL
//   panel reset 21              — ~/MeshCore/src/helpers/ui/SSD1306Display.h's PIN_OLED_RESET default. ⓘ This
//                                 RESOLVES spec §14 Q1's "MeshCore's V3 variant defines no PIN_OLED_RESET": the
//                                 VARIANT does not, but the display driver it selects does, and 21 is also what our
//                                 own pre-A0 seam note said. Still worth one bench confirmation.
//   panel addr  0x3C            — U8g2's ssd1306_128x64_noname descriptor already targets it
// Identical on the V4 (spec §10.1), and the V4 gets its own variants/heltec_v4/board_ui.cpp regardless (spec §0), so
// these stay file-local constants: nothing outside this TU reads them and there is nothing to override.
static constexpr uint8_t kOledRst  = 21;
static constexpr uint8_t kOledScl  = 18;
static constexpr uint8_t kOledSda  = 17;
static constexpr uint8_t kOledAddr = 0x3C;   // §B91: the 7-bit address the ACK probe asks for (U8g2's own target)

// ★ THE PANEL POWER RAIL — added against the plan's Task-5 code block, which does not touch it, and this is a
//   dark-panel risk rather than a nicety. Vext (GPIO 36) is a switched peripheral rail on this board and NOTHING in
//   this tree has ever driven it, so its gate sits at whatever reset leaves (ESP32-S3 GPIO36 comes up as an input
//   with no pull). MeshCore's working V3 port drives it to a DEFINITE level at board begin:
//   RefCountedDigitalPin::begin() does pinMode(36, OUTPUT) + digitalWrite(36, !active) with `active` defaulting HIGH
//   ⇒ **LOW** — and its SSD1306 comes up with the display holding no claim on that pin.
//   ⇒ LOW is the level under which that panel is KNOWN to work; leaving the pin floating is not.
// ⚠ What that port does NOT establish is whether the panel is on this rail at all (it never claims it), so this is
//   "reproduce the proven pin level", not "Vext is active-low". If the bench shows a dark panel, flip kVextOnLevel to
//   HIGH *before* suspecting the reset pin or the driver — the plan's Step 5 hint sends you to the reset pin first,
//   and on a rail that was floating until this slice that is the wrong first suspect.
//   Bench check + both remedies: docs/2026-07-31-bench-test-script.md, Part 8.
static constexpr uint8_t kVextPin     = 36;
static constexpr uint8_t kVextOnLevel = LOW;

// ---- panel -------------------------------------------------------------------------------------------------------
// U8G2_SSD1306_128X64_NONAME_1_HW_I2C(rotation, reset, clock, data) — signature verified against the pinned U8g2
// 2.35.30 (src/U8g2lib.h:1587), so `clock` is SCL and `data` is SDA, in that order.
// ★ `_1_` is the PAGE-BUFFER mode: one 128 B buffer pushed as 8 pages, NOT the 1024 B full-frame `_F_` mode. That is
//   the timing constraint of spec §5, not a preference — see board_ui.h. U8g2 pinMode()s every configured pin itself
//   (U8X8_MSG_GPIO_AND_DELAY_INIT) and drives the reset sequence, and it sets the bus to 400 kHz from the SSD1306
//   descriptor's i2c_bus_clock_100kHz = 4 — the very number spec §5's 25 ms full-frame figure is computed from.
//   Its HW-I2C init calls Wire.begin(sda, scl) with the pins above (U8x8lib.cpp:1348); nothing else in this firmware
//   uses Wire, so the panel owns the bus.
static U8G2_SSD1306_128X64_NONAME_1_HW_I2C s_u8g2(U8G2_R0, kOledRst, kOledScl, kOledSda);

static bool s_painting = false;   // a page loop is open — next_page() still has pages to push
static bool s_asleep   = false;   // ★ the blanking LATCH; see set_power_save()

// ---- battery divider (spec §7; plan Task 9 = slice UI-9) -----------------------------------------------------------
// Provenance rule (spec §10, and the same one the Vext block above obeys): every value here is recovered from
// MeshCore's WORKING Heltec V3 port — ~/MeshCore/variants/heltec_v3/HeltecV3Board.h, `begin()` :31-36 and
// `getBattMilliVolts()` :79-92 — not from datasheet reading. V3 and V4 share the pins and the formula (spec §10.1);
// only the polarity handling and the settle differ, and V4 gets its own variants/heltec_v4/board_ui.cpp anyway.
static constexpr uint8_t  kAdcBits      = 10;      // analogReadResolution(10) — the resolution the divisor assumes
static constexpr float    kAdcFullScale = 1024.0f; // 2^kAdcBits; MeshCore's formula divides by exactly this
static constexpr float    kAdcRefV      = 3.3f;    // full-scale volts at the ADC pin
// ★★ THIS IS AN EMPIRICAL COMBINED ADC SCALE, NOT A RESISTOR RATIO — and the old name (`kVbatDivider`, "VBAT / V(ADC)
//    — a PER-REVISION property") ASSERTED THAT IT WAS ONE. Renamed rather than annotated, because the name is what a
//    bench reader acts on ([[B126]], independent QA 2026-08-06). MEASURED AGAINST THE DOCUMENTED NETWORK:
//      · Heltec's V3 battery divider is **VBAT — 390 kΩ — GPIO1 — 100 kΩ — GND** ⇒ a PHYSICAL ratio of
//        (390 + 100) / 100 = **4.9**.
//      · The reference port's 5.42 is 4.9 × **1.106** ⇒ roughly **10.6 % of this number is not the resistors at all**.
//        It absorbs the ESP32-S3 ADC's attenuation / full-scale error against the nominal `kAdcRefV / kAdcFullScale`
//        this formula assumes.
//    ⇒ ★ A METER DISAGREEMENT IS AN **ADC-CALIBRATION** SUSPECT AT LEAST AS MUCH AS A RESISTOR-TOLERANCE ONE. As
//      `kVbatDivider` the bench entries pointed the tester at the resistors alone, which is the wrong first suspect;
//      guide H9-02 and script 8.6 now say both.
// ⓘ PROVENANCE AT THE LEVEL IT IS ACTUALLY KNOWN, not rounded up: the 390 k / 100 k network is documented by the V3
//   community and by `ropg/heltec_esp32_lora_v3`'s README, read off the schematic — Heltec's own HTIT-WB32LA_V3.2 PDF
//   was fetched and is not machine-readable, so this is third-party-from-schematic, NOT a vendor spec sheet.
// ⛔ The VALUE is unchanged and still comes from the WORKING reference port. Do not retune it from one voltage point.
static constexpr float    kVbatAdcScale = 5.42f;   // mv = kVbatAdcScale * (kAdcRefV / kAdcFullScale) * raw * 1000
static constexpr uint8_t  kAdcSamples   = 8;       // mean of 8, as the reference port does
// ★ THE PLAUSIBILITY WINDOW IS NOT A NEW POLICY — it is this tree's EXISTING answer for an ADC battery reader (U1):
//   src/firmware_commands.cpp's read_batt_mv() ends `return (mv > 2000 && mv < 4500) ? mv : -1;` with the comment
//   "1S-LiPo plausible range; else omit". Both boards carry a 1S cell, so the window is the same one.
//   ⇒ this reader can genuinely answer "unavailable", which is what keeps board_ui.h's `<0` contract and the panel's
//     `--` alive on hardware. Without it a disconnected divider reads raw 0 and the panel would claim `0.0V` — a
//     display-shaped field overstating the measurement, the exact class of F4 / §B117 / the `NOT RELAYED` ruling.
//   ⚠ DUPLICATION, STATED RATHER THAN HIDDEN: the same two numbers now exist in two TUs. Hoisting them into a shared
//     header means editing a working nRF52 path from inside a Heltec feature slice (C1: refactor XOR feature), so it
//     is registered as a follow-up instead of done here.
static constexpr int32_t  kBattMinMv    = 2000;
static constexpr int32_t  kBattMaxMv    = 4500;

// ★★ POLARITY IS PROBED, NEVER HARDCODED — AND THE PROBE IS CHECKED, BECAUSE THE OBVIOUS FORM OF IT IS A COIN FLIP.
//    `MR_UI_ADC_CTRL` (GPIO 37) is a CONTROL line, not the ADC input: it gates the VBAT divider, so it must be driven
//    ACTIVE only around the burst and parked INACTIVE the rest of the time or the divider leaks CONTINUOUSLY.
//    Boards past rev 3.2 INVERTED that line while keeping the "V3" name, which is why the reference port auto-detects
//    instead of defining a level (`begin()` :31-33) and why spec §7 / plan Task 9 forbid hardcoding LOW here.
//
// ⚠⚠ THE DEFECT IN THE REFERENCE PORT'S PROBE, AND WHY THIS ONE DIFFERS. MeshCore does:
//        pinMode(PIN_ADC_CTRL, INPUT); adc_active_state = !digitalRead(PIN_ADC_CTRL);
//    `INPUT` selects NO PULL. If nothing external defines GPIO 37's level, that read is INDETERMINATE — and the
//    failure is not a wrong voltage, it is the PARK: whichever way an indeterminate read lands, the level then written
//    as "inactive" is the ACTIVE one half the time, leaving the divider ENABLED for ever on a battery-powered safety
//    device. ★ This is [[B90]]'s Vext problem restated: a pin nothing drives, read as if its level meant something.
//    ⛔ WHAT THE TREE CAN AND CANNOT ESTABLISH, checked rather than assumed (V1): the vendor sources define
//      `PIN_ADC_CTRL_ACTIVE/INACTIVE` (HeltecV3Board.h:14-15) and run the bare-`INPUT` probe on heltec_v3 AND
//      rak3112 — and NOWHERE do they document a pull-up, a pull-down or an idle level. Their own **V4** board drops
//      the probe entirely and hardcodes ACTIVE=HIGH (HeltecV4Board.cpp:7-8). ⇒ **"the line has a defined idle level"
//      is NOT established by anything in this tree or in the vendor port.** Reproducing the probe verbatim would be
//      claiming knowledge we do not have — the exact "reproduce the proven level" vs "we know the rail" line B90 drew.
//
// ★ WHAT THIS PORT DOES INSTEAD (and it is the honest minimum, not a cleverness): probe the line TWICE, once against
//   an internal pull-up and once against an internal pull-down.
//     · both reads AGREE  -> something EXTERNAL is holding the line, the idle level is real, and the detection means
//                            what the reference port intends. Polarity = the inverse of that level.
//     · they DISAGREE     -> the line is FLOATING. The detection is meaningless, so it is REFUSED rather than
//                            guessed (C2): `s_adc_polarity_known` stays false and battery_sample_mv() answers
//                            UNAVAILABLE for the life of the boot, so the panel shows `--` and never a number
//                            derived from a coin flip.
// ⚠⚠ WHAT THIS STILL DOES NOT DO, stated plainly because it matters: it CANNOT make the park provably safe. When the
//    line floats there is no level that is known-inactive on EVERY revision, so the pin is still driven somewhere —
//    deterministically and declared, instead of randomly. Detected-and-loud is an improvement over silent-and-random;
//    it is not proof. ⇒ **THE OWNER MAY PREFER THE OTHER ANSWER** — replacing detection with a build constant
//    carrying the measured value (the `kVextOnLevel` / `LORA_TX_POWER` precedent). That would CONTRADICT spec §7 and
//    plan Task 9, both of which say "do not hardcode", so it is reported as owed rather than taken here. Neither
//    option is implemented on the owner's behalf.
//
// ★★★ THE FAIL-SAFE PARK — AND IT IS THE HALF THAT SHIPPED **INVERTED** ([[B123]] round 2, independent QA 2026-08-06).
//    The first cut of this file wrote ONE expression on EVERY path: `digitalWrite(CTRL, s_adc_active_high ? LOW : HIGH)`.
//    On a floating line `s_adc_active_high` is `(with_pullup == LOW)` = `(HIGH == LOW)` = **false**, so that expression
//    parked GPIO 37 **HIGH** — and Heltec's own hardware update log for **V3.2** reads, verbatim:
//        "Modified voltage detection circuit, now need to pull up the ADC_Ctrl(GPIO 37)."
//        — wiki.heltec.org/docs/devices/open-source-hardware/esp32-series/lora-32/wifi-lora-32-v3/hardware-update-log
//          (V3.2 section; mirrored in HelTecAutomation/HeltecDocs)
//    ⇒ on V3.2 and later, **HIGH IS THE MEASURING (ACTIVE) LEVEL** ⇒ the REFUSAL path — the one written precisely to
//    avoid a standing drain — left the divider **ENABLED INDEFINITELY** on a battery-powered safety device. The
//    comment above it said *"park INACTIVE — the divider must not idle on"*, which was the OPPOSITE of what it did in
//    exactly the case it was written for. ⛔ Detection was right; the fallback was wrong.
// ⇒ when the polarity is UNKNOWN the park is `kAdcCtrlFailsafePark` = **LOW**, the level Heltec DOCUMENTS as
//   non-measuring on the revisions this line was inverted for.
//
// ⛔⛔ THIS IS **NOT** THE "HARDCODE THE POLARITY" THAT SPEC §7 AND PLAN TASK 9 FORBID, and the distinction is why this
//    is a named constant instead of a literal in the `digitalWrite`. The **MEASUREMENT** polarity is still DETECTED and
//    nothing here changes it: when the two-pull probe SUCCEEDS, both the enable level and the park still come from
//    `s_adc_active_high`, and probe checks P6f (an idle-HIGH board parks HIGH) and P8o (an idle-LOW board parks LOW)
//    turn RED the moment that stops being true. `kAdcCtrlFailsafePark` is consulted ONLY where detection has ALREADY
//    FAILED and there is no detected polarity to consult. ★ A later reader must not "restore" the single expression as
//    a spec violation — that is the defect, not the rule.
// ⚠⚠ THE RESIDUAL, AND IT IS **NOT** CLAIMED AWAY: LOW is documented-inactive for **V3.2 and later** (and it matches
//    the vendor's own V4 and T190 boards, which drop the probe and hardcode LOW-inactive / HIGH-to-measure —
//    `HeltecV4Board.cpp:8,66,74`, `HeltecT190Board.cpp:7,52,60`). On a **pre-3.2** V3 the sense is the other way round
//    (`ropg/heltec_esp32_lora_v3`: *"if GPIO37 is pulled low, the battery voltage appears on GPIO1"*), so THERE this
//    fallback would be wrong again. ⓘ Why it is still the better bet rather than the coin flip re-flipped: a revision
//    that BIASES the gate at all is a revision the two-pull probe DETECTS, and then the fallback never runs — the
//    fallback runs only when NOTHING biases the line. ⛔ **This is not "provably safe on all revisions", and no such
//    claim is made.** Only the bench closes it: guide **H9-05 part C**, script **8.31**.
static constexpr uint8_t kAdcCtrlFailsafePark = LOW;
// ⓘ FALSE-NEGATIVE DIRECTION: an external pull WEAKER than the ESP32-S3's internal (~45 kΩ) would read as "floating"
//   and the panel would show `--` on a board that actually works. That is a refusal, not a wrong number, and the bench
//   entries distinguish it (guide H9-01 / H9-05 part B).
// ⛔ CORRECTED IN PLACE 2026-08-06: this note used to end *"...not a wrong number AND NOT A LEAK"*. **That was FALSE
//   and it is the sentence [[B123]] round 2 disproved** — on this very path the park was the MEASURING level, so the
//   "safe" direction WAS the leak. It is leak-free only BECAUSE of `kAdcCtrlFailsafePark` above, and only on the
//   revisions that constant is documented for. ⇒ the claim now depends on a measurement, and script **8.31** /
//   guide **H9-05 part C** are that measurement. Do not restore the unconditional form.
// ⓘ SETTLE: the two probe reads take a µs-scale settle because we deliberately CHANGE the pull between them; that is
//   different from the sampling burst, which takes NONE. The reference V3 port samples immediately after driving the
//   line and only its V4 port inserts `delay(10)`, which spec §7 forbids importing — 10 ms of blocking wait against a
//   `cts_to_data_gap_ms` of 5 is the same hazard class as a full-frame repaint.
static constexpr uint32_t kPullSettleUs = 50;   // pull change -> stable read; µs-scale, boot-only, never in the burst
static bool s_adc_active_high    = false;
static bool s_adc_polarity_known = false;       // false => the probe found the line FLOATING => refuse to read

static void battery_init() {
    pinMode(MR_UI_ADC_CTRL, INPUT_PULLUP);
    delayMicroseconds(kPullSettleUs);
    const int with_pullup = digitalRead(MR_UI_ADC_CTRL);
    pinMode(MR_UI_ADC_CTRL, INPUT_PULLDOWN);
    delayMicroseconds(kPullSettleUs);
    const int with_pulldown = digitalRead(MR_UI_ADC_CTRL);

    s_adc_polarity_known = (with_pullup == with_pulldown);           // disagreement == nothing external holds the line
    s_adc_active_high    = (with_pullup == LOW);                     // idle level probed, then inverted
    pinMode(MR_UI_ADC_CTRL, OUTPUT);
    // ★ PARK INACTIVE — FROM TWO SOURCES, NEVER ONE. The DETECTED inactive level when the probe succeeded; the
    //   DOCUMENTED-inactive fail-safe when it did not. A single `s_adc_active_high ? LOW : HIGH` here parks the V3.2
    //   ACTIVE level on exactly the refusal path that exists to prevent a standing drain ([[B123]] round 2, above).
    digitalWrite(MR_UI_ADC_CTRL, s_adc_polarity_known ? (s_adc_active_high ? LOW : HIGH)
                                                      : kAdcCtrlFailsafePark);
}

namespace mrui {

bool board_init() {
    // Panel power first: the rail must be settled before any I2C traffic reaches the panel.
    pinMode(kVextPin, OUTPUT);
    digitalWrite(kVextPin, kVextOnLevel);
    pinMode(MR_UI_BTN_PIN, INPUT_PULLUP);   // active LOW — see button_pressed()
    battery_init();
    // begin() = initDisplay + ONE full clearDisplay + setPowerSave(0). That clear is a 1024 B (~25 ms) blocking
    // transfer, and it is the one place spec §5 allows one: this runs at the end of setup() (fw_main.cpp's
    // mr_ui_init() call site) with the radio idle and no frame in flight.
    s_u8g2.begin();
    // begin() left the panel AWAKE, so the latch must agree with the hardware. Setting it explicitly is not
    // redundant: a re-init after a blank would otherwise leave s_asleep=true against a lit panel, and the next
    // set_power_save(true) would be a latched no-op — a panel that can never blank again.
    s_asleep   = false;
    s_painting = false;
    set_font(Font::small);
    // ★ §B91 — THE PANEL-PRESENCE ANSWER, and it is a MEASUREMENT, not an inference. U8g2's begin() returns 1
    //   unconditionally (it never reads the bus), so before this the firmware could not tell a working panel from a
    //   floating rail, a wrong address or a broken trace — and the UI-5 bench question "is the panel on Vext?" had no
    //   instrument. A zero-byte transmission is the standard presence test and is exactly what MeshCore's
    //   SSD1306Display::i2c_probe does. Run AFTER begin(), because U8g2 owns Wire.begin(sda, scl) and nothing else in
    //   this firmware touches Wire, so the bus is configured only from here on.
    //   endTransmission() == 0 means the device ACKed its address. Any other code = no answer.
    Wire.beginTransmission(kOledAddr);
    return Wire.endTransmission() == 0;
}

void begin_frame() {
    s_u8g2.firstPage();     // clears the page buffer and rewinds to tile row 0 — composes only, touches NO bus
    s_painting = true;
}

bool next_page() {
    if (!s_painting) return false;          // no frame open, or one abandoned by a blank -> touch NO bus
    if (s_u8g2.nextPage()) return true;     // pushed one 128 B page (~3 ms); more remain
    s_painting = false;                     // that was the last page — the frame is now on the panel
    return false;
}

void set_font(Font f) {
    // Two fonts only, per spec §11's "do not link the full font set". U8g2 puts every font in its own
    // .text.<fontname> section (U8G2_FONT_SECTION), so --gc-sections drops the ~700 we never name.
    s_u8g2.setFont(f == Font::large ? u8g2_font_10x20_tf : u8g2_font_6x10_tf);
}

void draw_text(int x, int y, const char* s) { s_u8g2.drawStr(x, y, s); }
void draw_hline(int x, int y, int w)        { s_u8g2.drawHLine(x, y, w); }

// ★ EDGE-TRIGGERED, and the latch is the entire point. An earlier design draft called clearDisplay() on every tick
//   while blanked — a full 1024 B I2C transfer per service pass, i.e. exactly the traffic the page-chunking rule
//   exists to prevent, plus the power it wastes (spec §5's power note). setPowerSave sends ONE SSD1306
//   DISPLAYOFF/DISPLAYON command and KEEPS display RAM, so waking needs no redraw.
//   A blanked panel must produce NO repeated bus traffic — spec §12 lists that as an acceptance case, and the phA5
//   host probe measures it (see simulation/BASELINE.md §UI-5).
void set_power_save(bool on) {
    if (on == s_asleep) return;   // repeat calls are GENUINE no-ops, not merely cheap ones
    s_u8g2.setPowerSave(on ? 1 : 0);
    s_asleep = on;
    // Abandon any open page loop: a later tick must never push a page into a dark panel, and the frame's inputs are
    // stale by the time it wakes anyway.
    if (on) s_painting = false;
}

bool button_pressed() { return digitalRead(MR_UI_BTN_PIN) == LOW; }   // INPUT_PULLUP -> pressed reads LOW

// ★ ONE SAMPLE. The CALLER decides when (spec §7: boot, then every ~30 s, and only under the §5 MAC-idle predicate —
//   src/firmware_ui.cpp's battery_maybe_sample). This function owns no cadence and no policy; it must not acquire one.
// ★ ENABLE -> SAMPLE -> DISABLE, with NO per-tick residue — the same edge/latch discipline set_power_save() follows.
//   The divider is live for the duration of eight analogRead()s and is parked inactive again on every exit path.
// ⚠ <0 means UNAVAILABLE and the panel renders `--`. Never substitute a plausible default voltage.
int32_t battery_sample_mv() {
    // ★ C2 — REFUSE rather than guess. If battery_init()'s two-pull probe found GPIO 37 floating, the polarity is a
    //   coin flip; driving the line then has a 50 % chance of sampling a dead divider AND of parking it enabled. The
    //   panel gets `--`, which is true ("we cannot measure this"), and the divider is left where boot parked it —
    //   which since [[B123]] round 2 is `kAdcCtrlFailsafePark`, the documented-inactive level, not the detected one.
    if (!s_adc_polarity_known) return -1;
    analogReadResolution(kAdcBits);
    digitalWrite(MR_UI_ADC_CTRL, s_adc_active_high ? HIGH : LOW);    // ENABLE the divider
    uint32_t raw = 0;
    for (uint8_t i = 0; i < kAdcSamples; ++i) raw += uint32_t(analogRead(MR_UI_VBAT_READ));
    raw /= kAdcSamples;
    digitalWrite(MR_UI_ADC_CTRL, s_adc_active_high ? LOW : HIGH);    // DISABLE — no standing leak between samples
    const int32_t mv = int32_t(kVbatAdcScale * (kAdcRefV / kAdcFullScale) * float(raw) * 1000.0f);
    return (mv > kBattMinMv && mv < kBattMaxMv) ? mv : -1;
}

}  // namespace mrui

// ---- ★★ THE mr_ui_* SEAM IS NO LONGER HERE — deleted by Task 6, deliberately, and this note replaces it. ----------
//
// UI-5 defined `mr_ui_init` / `mr_ui_tick` / `mr_ui_on_push` in this file and marked them TEMPORARY. They existed for
// exactly one reason: `fw_main` calls all three UNCONDITIONALLY and `MR_FEAT_OLED=1` removes `mr_ui.h`'s inline stubs,
// so UI-5 could not link without SOMEBODY defining them. UI-6 is that somebody — `src/firmware_ui.cpp`.
//
// ⛔ DO NOT RE-ADD THEM HERE. Two definitions of the same three externs is a duplicate-symbol link failure across the
//    heltec_v3 / heltec_mobile / gateway_heltec images, and the render policy they would carry (the MAC-idle predicate,
//    the <=2 Hz dirty throttle, page pacing, the blank timer, the battery cadence, push correlation) does not belong in
//    a board file at all: rule U3, and spec §2.1 keeps raw `Push`es away from the model for a safety reason.
//    That is also why `mr_ui.h` and `command.h` are no longer included above — this TU has no use for either.
//
// ⓘ The `--gc-sections` reachability that `mr_ui_init()` used to provide (§B88) now comes from the real caller:
//    `firmware_ui.cpp` calls `board_init`, `begin_frame`, `next_page`, `set_font`, `draw_text`, `draw_hline`,
//    `set_power_save`, `button_pressed` and `battery_sample_mv` — all nine canvas entry points, so none is collected.

#endif  // MR_FEAT_OLED
