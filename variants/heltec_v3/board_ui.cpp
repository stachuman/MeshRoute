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
//   MISSING   this canvas cannot REPORT a dead panel: board_init() is void and U8g2's begin() always returns 1 (it
//             performs no I2C ack check). MeshCore probes the address instead (SSD1306Display::i2c_probe). Giving the
//             canvas a failure channel needs a caller that can surface it, which is Task 6 — registered as B91.
//   TEMPORARY the three mr_ui_* hooks at the bottom of this file. See the block comment there before deleting them.
#include "mr_features.h"

#if MR_FEAT_OLED

#include <Arduino.h>       // pinMode / digitalRead / digitalWrite + the LOW/HIGH/INPUT_PULLUP levels. U8g2 pulls this
                           // in transitively (U8x8lib.h:43), but this TU uses it directly, so it says so.
#include <U8g2lib.h>
#include "board_ui.h"
#include "mr_ui.h"
#include "command.h"       // meshroute::Push — only the (still-inert) push hook needs the type

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
static constexpr uint8_t kOledRst = 21;
static constexpr uint8_t kOledScl = 18;
static constexpr uint8_t kOledSda = 17;

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

void board_init() {
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

// ---- the mr_ui_* seam — TEMPORARY. Task 6 moves these to src/firmware_ui.cpp and DELETES this whole block. --------
//
// They cannot simply be omitted here: fw_main calls all three unconditionally and MR_FEAT_OLED=1 removes mr_ui.h's
// inline stubs, so a build without definitions does not link. The plan's Task-5 code block drops them and would not
// have linked; see the slice report.
//
// ★ mr_ui_init() also does Task 5's ONE piece of real work on metal, and it is load-bearing twice over:
//   1. it is the slice's acceptance test — plan Step 5's "the panel lights". Nothing else in Task 5 ever calls the
//      canvas, so without this the panel would stay dark and the step would be untestable.
//   2. it is what makes the canvas REACHABLE. This platform links with -Wl,--gc-sections, so a canvas that nothing
//      calls is garbage-collected and a Task 5 that wired nothing would have measured a flash delta of nothing.
//   The frame is painted through the SAME page loop the feature layer will use, so it demonstrates the §5 contract on
//   hardware: the WHOLE scene is re-drawn before EVERY next_page().
void mr_ui_init() {
    mrui::board_init();
    mrui::begin_frame();
    do {
        mrui::set_font(mrui::Font::large);
        mrui::draw_text(6, 26, "MeshRoute");
        mrui::draw_hline(0, 32, 128);
        mrui::set_font(mrui::Font::small);
        mrui::draw_text(6, 46, "OLED UI-5 ok");
    } while (mrui::next_page());
}

void mr_ui_tick(uint32_t /*now_ms*/) {
    // Deliberately INERT. ALL render policy — the MAC-idle predicate, the <=2 Hz dirty throttle, page pacing, the
    // blank timer and the battery cadence — is plan Task 6 / slice UI-6, in src/firmware_ui.cpp. Painting from here
    // without that predicate is precisely the CTS->DATA-gap break spec §5 exists to prevent, and feature logic does
    // not belong in a board file (U3).
}

void mr_ui_on_push(const meshroute::Push& /*pu*/) {
    // Deliberately INERT. Push correlation is Task 4's SendTracker, driven from Task 6's firmware_ui.cpp. A board file
    // must never see a Push — spec §2.1 keeps raw pushes away from the model for the same reason.
}

#endif  // MR_FEAT_OLED
