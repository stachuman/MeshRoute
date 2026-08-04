// phA5 PROBE — compiles the REAL variants/heltec_v3/board_ui.cpp against counting shims for Arduino + U8g2 and
// measures the behaviours no native test and no simulator can reach. Every check prints its own denominator.
#include "board_ui.h"
#include <Arduino.h>
#include <U8g2lib.h>
#include "mr_ui.h"
#include "command.h"
#include <cstdio>
#include <cstring>

// ---- shim storage ------------------------------------------------------------------------------------------------
ProbeGpio  g_gpio;
ProbeU8g2  g_u8;
const u8g2_cb_t u8g2_cb_r0{0};
const uint8_t u8g2_font_6x10_tf[1]  = {0};
const uint8_t u8g2_font_10x20_tf[1] = {0};

void pinMode(uint8_t pin, uint8_t mode)      { g_gpio.pinmode_calls++; if (pin < 64) g_gpio.mode[pin] = mode; }
void digitalWrite(uint8_t pin, uint8_t lvl)  { g_gpio.write_calls++;   if (pin < 64) g_gpio.level[pin] = lvl; }
int  digitalRead(uint8_t)                    { g_gpio.read_calls++;    return g_gpio.read_returns; }
void analogReadResolution(uint8_t)           {}
int  analogRead(uint8_t)                     { return 0; }

// ---- tiny harness ------------------------------------------------------------------------------------------------
static int g_pass = 0, g_fail = 0;
#define CHK(label, expr) do {                                                              \
    const bool ok_ = (expr);                                                               \
    if (ok_) ++g_pass; else { ++g_fail; printf("  FAIL %-58s  %s\n", (label), #expr); }    \
} while (0)

static void reset_counters() { g_u8 = ProbeU8g2(); g_gpio = ProbeGpio(); }

int main() {
    // ================================================================================================ P1
    // EDGE-TRIGGERED BLANKING. set_power_save must reach the panel ONCE per transition, never per tick.
    // Revert the `if (on == s_asleep) return;` latch and this reads 10 instead of 2.
    mrui::board_init();
    reset_counters();
    for (int i = 0; i < 5; ++i) mrui::set_power_save(true);
    CHK("P1a five blank calls -> ONE setPowerSave",      g_u8.setPowerSave == 1);
    CHK("P1b ... and its argument is 1 (DISPLAYOFF)",    g_u8.last_power_save_arg == 1);
    for (int i = 0; i < 5; ++i) mrui::set_power_save(false);
    CHK("P1c five wake calls  -> ONE more setPowerSave", g_u8.setPowerSave == 2);
    CHK("P1d ... and its argument is 0 (DISPLAYON)",     g_u8.last_power_save_arg == 0);
    CHK("P1e ten calls, ZERO page transfers",            g_u8.nextPage == 0);

    // ================================================================================================ P2
    // A BLANKED PANEL PRODUCES NO REPEATED I2C TRAFFIC (spec §12). Ticking a blanked panel for 100 passes must
    // reach the bus not once.
    mrui::set_power_save(true);                       // the TRANSITION (P1 left the panel awake) — costs 1 command
    reset_counters();                                 // ...now count only what the STEADY blanked state costs
    for (int i = 0; i < 100; ++i) { mrui::set_power_save(true); (void)mrui::next_page(); }
    CHK("P2a 100 blanked passes -> 0 bus ops",         g_u8.bus_ops() == 0);
    mrui::set_power_save(false);                      // leave awake for the rest

    // ================================================================================================ P3
    // PAGE CHUNKING. begin_frame() must touch NO bus; a frame must take exactly EIGHT next_page() calls, one page
    // transfer each; the call after the last must be a no-op.
    reset_counters();
    mrui::begin_frame();
    CHK("P3a begin_frame composes only: firstPage 1",   g_u8.firstPage == 1);
    CHK("P3b begin_frame pushes NO page",               g_u8.nextPage == 0);
    int pages = 0;
    while (mrui::next_page()) { if (++pages > 32) break; }
    ++pages;                                          // the call that returned false also transferred a page
    CHK("P3c exactly 8 page transfers per frame",       pages == 8 && g_u8.nextPage == 8);
    const int after = g_u8.nextPage;
    CHK("P3d next_page() past the frame end is false",  mrui::next_page() == false);
    CHK("P3e ... and reaches the bus 0 more times",     g_u8.nextPage == after);

    // ================================================================================================ P4
    // A BLANK ABANDONS AN OPEN PAGE LOOP — no tick may push a page into a dark panel.
    reset_counters();
    mrui::begin_frame();
    (void)mrui::next_page();                          // 1 of 8 pushed
    mrui::set_power_save(true);
    const int at_blank = g_u8.nextPage;
    for (int i = 0; i < 10; ++i) CHK("P4a next_page() after blank is false", mrui::next_page() == false);
    CHK("P4b ... and pushed no further page",          g_u8.nextPage == at_blank);
    mrui::set_power_save(false);

    // ================================================================================================ P5
    // THE SCENE IS RE-DRAWN ONCE PER PAGE (spec §5's corrected note: U8g2 clips per page and accumulates nothing).
    // The boot frame draws 2 strings + 1 hline per pass, so a correct loop reads 16 / 8, a draw-once loop 2 / 1.
    reset_counters();
    mr_ui_init();
    CHK("P5a boot frame: 8 page transfers",             g_u8.nextPage == 8);
    CHK("P5b boot frame: scene re-drawn per page",      g_u8.drawStr == 16 && g_u8.drawHLine == 8);
    CHK("P5c panel brought up exactly once",            g_u8.begin == 1);
    CHK("P5d both fonts selected during the frame",     g_u8.setFont >= 16);

    // ================================================================================================ P6
    // BOARD WIRING, as board_init() actually programs it.
    CHK("P6a button pin is INPUT_PULLUP",              g_gpio.mode[MR_UI_BTN_PIN] == (int)INPUT_PULLUP);
    CHK("P6b Vext (36) driven OUTPUT",                 g_gpio.mode[36] == (int)OUTPUT);
    CHK("P6c Vext driven to the proven level (LOW)",    g_gpio.level[36] == (int)LOW);

    // ================================================================================================ P7
    // BUTTON POLARITY — INPUT_PULLUP, so pressed == LOW.
    g_gpio.read_returns = LOW;   CHK("P7a LOW  -> pressed",      mrui::button_pressed() == true);
    g_gpio.read_returns = HIGH;  CHK("P7b HIGH -> not pressed",  mrui::button_pressed() == false);

    // ================================================================================================ P8
    // BATTERY IS AN HONEST `unavailable` STUB until Task 9 — never a fabricated millivolt value.
    CHK("P8a battery_sample_mv() < 0 (unavailable)",   mrui::battery_sample_mv() < 0);

    // ================================================================================================ P9
    // THE SEAM HOOKS TASK 6 OWNS ARE STILL INERT — no render policy has leaked into the board file.
    reset_counters();
    for (int i = 0; i < 50; ++i) mr_ui_tick(1000u * (uint32_t)i);
    CHK("P9a mr_ui_tick x50 reaches the bus 0 times",  g_u8.bus_ops() == 0 && g_u8.firstPage == 0);
    MESHROUTE_NS::Push pu{};
    for (int i = 0; i < 50; ++i) mr_ui_on_push(pu);
    CHK("P9b mr_ui_on_push x50 reaches the bus 0 times", g_u8.bus_ops() == 0);

    printf("phA5 board_ui probe: %d passed / %d failed / %d total\n", g_pass, g_fail, g_pass + g_fail);
    return g_fail == 0 ? 0 : 1;
}
