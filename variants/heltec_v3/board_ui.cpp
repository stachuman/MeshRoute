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
//   MISSING   battery_sample_mv() is an honest `unavailable` (-1) STUB. The real reader — auto-detected ADC_CTRL
//             polarity, mean of 8 samples, NO settling delay — is plan Task 9 / slice UI-9 (spec §7). Until then the
//             status bar renders `--`, which is this project's rule for an unavailable reading (console_json.h:126),
//             never a plausible wrong number.
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

// Battery: an empty stub so this TU links and the panel renders `--`. Plan Task 9 / slice UI-9 replaces both halves
// (this and mrui::battery_sample_mv below) in place; it is deliberately at file scope, outside namespace mrui, so
// Task 9's block drops straight in.
static void battery_init() {}

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

// MISSING by design — plan Task 9 / slice UI-9. <0 is the documented "unavailable" answer of board_ui.h's contract,
// so the status bar shows `--`. Do NOT make this return a guess.
int32_t battery_sample_mv() { return -1; }

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
