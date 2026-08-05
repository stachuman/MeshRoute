// phA5 PROBE — compiles the REAL variants/heltec_v3/board_ui.cpp against counting shims for Arduino + U8g2 + Wire and
// measures the behaviours no native test and no simulator can reach. Every check prints its own denominator.
//
// ⚠⚠ §UI-6 CHANGED WHAT THIS PROBE CAN SEE, and the change is worth stating rather than quietly absorbing. UI-5's
//   `mr_ui_init/tick/on_push` lived in board_ui.cpp TEMPORARILY, so this probe could link them and assert two caller
//   properties through them: "the scene is re-drawn once per page" (P5) and "the seam hooks are inert" (P9). Task 6
//   moved all three to src/firmware_ui.cpp — which pulls fw_context.h, i.e. RadioLib and the whole device stack — so
//   they are NO LONGER HOST-COMPILABLE HERE, and this probe no longer includes mr_ui.h or command.h at all.
//   ⇒ P5 now drives the page loop from the PROBE, which still pins the CANVAS half of the contract (exactly 8 page
//     transfers, one per next_page(), and the loop body runs 8 times). The CALLER half — that firmware_ui.cpp actually
//     re-draws inside the loop — is covered STRUCTURALLY by run.sh, and that is weaker. It is recorded in the register.
//   ⇒ P9 is replaced by P9/P10: the hooks must be ABSENT from this TU (the duplicate-symbol tripwire, checked by nm in
//     run.sh) and board_init() must REPORT the panel ACK honestly (§B91).
#include "board_ui.h"
#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <cstdio>
#include <cstring>

// ---- shim storage ------------------------------------------------------------------------------------------------
ProbeGpio  g_gpio;
ProbeU8g2  g_u8;
ProbeWire    g_wire;
ProbeTwoWire Wire;
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

static void reset_counters() { g_u8 = ProbeU8g2(); g_gpio = ProbeGpio(); g_wire = ProbeWire(); }

// The scene UI-5's boot frame drew, kept as the probe's own stand-in caller: 2 strings + 1 hline per pass. A correct
// page loop therefore reads 16 drawStr / 8 drawHLine; a draw-once-per-frame loop reads 2 / 1.
static void probe_scene() {
    mrui::set_font(mrui::Font::large);
    mrui::draw_text(6, 26, "MeshRoute");
    mrui::draw_hline(0, 32, 128);
    mrui::set_font(mrui::Font::small);
    mrui::draw_text(6, 46, "probe scene");
}

int main() {
    // ================================================================================================ P1
    // EDGE-TRIGGERED BLANKING. set_power_save must reach the panel ONCE per transition, never per tick.
    // Revert the `if (on == s_asleep) return;` latch and this reads 10 instead of 2.
    (void)mrui::board_init();
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
    // THE CANVAS SUPPORTS A ONCE-PER-PAGE REDRAW (spec §5's corrected note: U8g2 clips per page and accumulates
    // nothing). Driven from the PROBE now that the hooks have moved — so this pins the canvas contract that makes the
    // caller's `do { draw all } while (next_page())` correct: the body must run 8 times for 8 page transfers.
    reset_counters();
    mrui::begin_frame();
    int body_runs = 0;
    do { probe_scene(); if (++body_runs > 32) break; } while (mrui::next_page());
    CHK("P5a one frame: 8 page transfers",              g_u8.nextPage == 8);
    CHK("P5b ... and the loop body ran once per page",  body_runs == 8);
    CHK("P5c ... i.e. the scene was drawn 8 times",     g_u8.drawStr == 16 && g_u8.drawHLine == 8);
    CHK("P5d both fonts selected during the frame",     g_u8.setFont >= 16);

    // ================================================================================================ P6
    // BOARD WIRING, as board_init() actually programs it. Re-run it so the counters cover THIS call.
    reset_counters();
    const bool init_ack = mrui::board_init();
    CHK("P6a button pin is INPUT_PULLUP",              g_gpio.mode[MR_UI_BTN_PIN] == (int)INPUT_PULLUP);
    CHK("P6b Vext (36) driven OUTPUT",                 g_gpio.mode[36] == (int)OUTPUT);
    CHK("P6c Vext driven to the proven level (LOW)",    g_gpio.level[36] == (int)LOW);
    CHK("P6d panel brought up exactly once",           g_u8.begin == 1);

    // ================================================================================================ P7
    // BUTTON POLARITY — INPUT_PULLUP, so pressed == LOW.
    g_gpio.read_returns = LOW;   CHK("P7a LOW  -> pressed",      mrui::button_pressed() == true);
    g_gpio.read_returns = HIGH;  CHK("P7b HIGH -> not pressed",  mrui::button_pressed() == false);

    // ================================================================================================ P8
    // BATTERY IS AN HONEST `unavailable` STUB until Task 9 — never a fabricated millivolt value.
    CHK("P8a battery_sample_mv() < 0 (unavailable)",   mrui::battery_sample_mv() < 0);

    // ================================================================================================ P9
    // ★ §B91 — THE PANEL-ACK REPORT, and the point is that it can say NO. Before UI-6, `board_init()` was void and
    // U8g2's own begin() returns 1 unconditionally (it never reads the bus), so a dead panel and a live one were
    // indistinguishable to the firmware. board_init() must probe the address and pass the answer up truthfully.
    CHK("P9a board_init() probed one address",         g_wire.begin_tx == 1 && g_wire.end_tx == 1);
    CHK("P9b ... and it was the SSD1306's 0x3C",       g_wire.last_addr == 0x3C);
    CHK("P9c ACK (0) is reported as present",          init_ack == true);
    g_wire.end_returns = 2;                            // Arduino's "address NACK"
    const bool nak = mrui::board_init();
    CHK("P9d a NACK is reported as ABSENT",            nak == false);
    g_wire.end_returns = 0;
    // ⓘ NOT fatal by contract: the caller (src/firmware_ui.cpp) logs one line and keeps meshing. This probe cannot see
    //   that half — firmware_ui.cpp is not host-compilable here (fw_context.h -> RadioLib) — so run.sh checks it
    //   structurally instead.

    // ================================================================================================ P10
    // A DEAD PANEL DOES NOT STOP THE CANVAS. Even when nothing ACKed, the page loop must still be well-formed, or a
    // node with a broken display would wedge its UI (and, via the MAC-idle gate, nothing else — but the panel must not
    // become a second failure mode).
    reset_counters();
    g_wire.end_returns = 2;
    (void)mrui::board_init();
    mrui::begin_frame();
    int dead_pages = 0;
    do { probe_scene(); if (++dead_pages > 32) break; } while (mrui::next_page());
    CHK("P10a a NACKed panel still runs 8 pages",      dead_pages == 8 && g_u8.nextPage == 8);
    g_wire.end_returns = 0;

    printf("phA5 board_ui probe: %d passed / %d failed / %d total\n", g_pass, g_fail, g_pass + g_fail);
    return g_fail == 0 ? 0 : 1;
}
