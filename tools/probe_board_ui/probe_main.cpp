// Shared Heltec canvas probe — compiles the REAL variants/heltec_common/board_ui.cpp against counting shims under the
// V3 and V4 trait sets and measures behavior no native test or simulator can reach.
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
#include "board_ui_traits.h"
#include <U8g2lib.h>
#include <Wire.h>
// §B197: the two ESP-IDF shims this file DEFINES (the counters and the scripted return codes live in
// fakes/esp_wake_probe.h, which both of these pull in).
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <cstdio>
#include <cstring>

#ifndef MR_PROBE_V4
#  error "MR_PROBE_V4 must identify the trait arm under test"
#endif

// ---- shim storage ------------------------------------------------------------------------------------------------
ProbeGpio  g_gpio;
ProbeU8g2  g_u8;
ProbeWire    g_wire;
ProbeTwoWire Wire;
ProbeWake  g_wake;   // §B197: the two ESP-IDF wake calls (fakes/esp_wake_probe.h)
const u8g2_cb_t u8g2_cb_r0{0};
const uint8_t u8g2_font_6x10_tf[1]  = {0};
const uint8_t u8g2_font_10x20_tf[1] = {0};

void pinMode(uint8_t pin, uint8_t mode) {
    g_gpio.pinmode_calls++;
    if (pin >= 64) return;
    g_gpio.mode[pin] = mode;
    if (mode == INPUT_PULLUP)   g_gpio.saw_pullup[pin]     = true;
    if (mode == INPUT_PULLDOWN) g_gpio.saw_pulldown[pin]   = true;
    if (mode == INPUT)          g_gpio.saw_bare_input[pin] = true;   // no pull => an indeterminate read (§UI-9)
}
void digitalWrite(uint8_t pin, uint8_t lvl)  { g_gpio.write_calls++;   if (pin < 64) g_gpio.level[pin] = lvl; }
// ★ §UI-9: the answer depends on the pin's CURRENT MODE, so the two-pull polarity probe can be driven — including the
//   FLOATING case, where a pull-up read and a pull-down read disagree. Unscripted modes fall through to `read_returns`,
//   which is what every pre-UI-9 case (P7's button polarity, P6's idle-HIGH board) relies on.
int  digitalRead(uint8_t pin) {
    g_gpio.read_calls++;
    const int mode = (pin < 64) ? g_gpio.mode[pin] : -1;
    if (mode == (int)INPUT_PULLUP   && g_gpio.read_under_pullup   >= 0) return g_gpio.read_under_pullup;
    if (mode == (int)INPUT_PULLDOWN && g_gpio.read_under_pulldown >= 0) return g_gpio.read_under_pulldown;
    return g_gpio.read_returns;
}
void delayMicroseconds(uint32_t) {}   // the polarity probe's µs settle; nothing here measures wall time
void analogReadResolution(uint8_t bits)      { g_gpio.analog_res_bits = bits; }
// ★ §UI-9: every conversion SNAPSHOTS the control line. That is what turns "the divider is enabled around the burst"
//   from a claim about source order into a measurement — see the ProbeGpio note in fakes/Arduino.h.
int  analogRead(uint8_t pin) {
    ++g_gpio.analog_calls;
    g_gpio.analog_pin = pin;
    if (g_gpio.ctrl_pin >= 0 && g_gpio.ctrl_pin < 64) {
        if      (g_gpio.level[g_gpio.ctrl_pin] == (int)HIGH) ++g_gpio.ctrl_high_during_read;
        else if (g_gpio.level[g_gpio.ctrl_pin] == (int)LOW)  ++g_gpio.ctrl_low_during_read;
    }
    return g_gpio.analog_returns;
}

// ---- §B197/§B200: the four ESP-IDF wake calls --------------------------------------------------------------------
// ★ Each records its ARGUMENTS and an ORDER STAMP, and each hands back a SCRIPTED code, so every failure arm of
//   `arm_button_wake()` / `disarm_button_wake()` is reachable from the host. None has any side effect — a shim that
//   "worked" would be pretending to know something about the silicon, which is precisely what the bench has to settle.
esp_err_t gpio_wakeup_enable(gpio_num_t gpio_num, gpio_int_type_t intr_type) {
    ++g_wake.gpio_wakeup_calls;
    g_wake.last_gpio = int(gpio_num);
    g_wake.last_intr = int(intr_type);
    g_wake.seq_gpio_wakeup = g_wake.next_seq++;
    return g_wake.gpio_wakeup_result;
}
esp_err_t esp_sleep_enable_gpio_wakeup(void) {
    ++g_wake.sleep_enable_calls;
    g_wake.seq_sleep_enable = g_wake.next_seq++;
    return g_wake.sleep_enable_result;
}
esp_err_t gpio_wakeup_disable(gpio_num_t gpio_num) {
    ++g_wake.gpio_disable_calls;
    g_wake.last_disable_gpio = int(gpio_num);
    g_wake.seq_gpio_disable = g_wake.next_seq++;
    return g_wake.gpio_disable_result;
}
esp_err_t gpio_set_intr_type(gpio_num_t gpio_num, gpio_int_type_t intr_type) {
    ++g_wake.set_intr_type_calls;
    g_wake.last_intr_type_gpio = int(gpio_num);
    g_wake.last_intr_type = int(intr_type);
    g_wake.seq_set_intr_type = g_wake.next_seq++;
    return g_wake.set_intr_type_result;
}
esp_err_t esp_sleep_disable_wakeup_source(esp_sleep_source_t source) {
    ++g_wake.sleep_disable_calls;
    g_wake.last_disable_src = int(source);
    g_wake.seq_sleep_disable = g_wake.next_seq++;
    return g_wake.sleep_disable_result;
}

// ---- tiny harness ------------------------------------------------------------------------------------------------
static int g_pass = 0, g_fail = 0;
#define CHK(label, expr) do {                                                              \
    const bool ok_ = (expr);                                                               \
    if (ok_) ++g_pass; else { ++g_fail; printf("  FAIL %-58s  %s\n", (label), #expr); }    \
} while (0)

static void reset_counters() { g_u8 = ProbeU8g2(); g_gpio = ProbeGpio(); g_wire = ProbeWire(); g_wake = ProbeWake(); }

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
    reset_counters();                                 // fresh ProbeGpio -> read_returns is HIGH again
    (void)mrui::board_init();
    CHK("P6a button trait is GPIO0 and INPUT_PULLUP",   MR_UI_BTN_PIN == 0 && g_gpio.mode[0] == (int)INPUT_PULLUP);
    CHK("P6b Vext trait is GPIO36 driven OUTPUT",       MR_UI_VEXT_PIN == 36 && g_gpio.mode[36] == (int)OUTPUT);
    CHK("P6c Vext uses this board's ruled LOW level",    g_gpio.level[36] == (int)LOW);
    CHK("P6d panel brought up exactly once",           g_u8.begin == 1);
    CHK("P6h OLED reset trait reaches U8g2 as GPIO21", g_probe_u8_ctor_rst == 21);
    CHK("P6i OLED clock trait reaches U8g2 as GPIO18", g_probe_u8_ctor_scl == 18);
    CHK("P6j OLED data trait reaches U8g2 as GPIO17",  g_probe_u8_ctor_sda == 17);
    CHK("P6k battery traits are CTRL=37 and VBAT=1",   MR_UI_ADC_CTRL == 37 && MR_UI_VBAT_READ == 1);
#if MR_PROBE_V4
    CHK("P6e V4 ADC_CTRL ends board_init() as an OUTPUT", g_gpio.mode[MR_UI_ADC_CTRL] == (int)OUTPUT);
    CHK("P6f V4 ADC_CTRL is parked INACTIVE (LOW)",       g_gpio.level[MR_UI_ADC_CTRL] == (int)LOW);
#else
    // §UI-9: the battery CONTROL line is board wiring too, and its boot state is the half that costs current if it is
    // wrong. The idle level here is HIGH (the shim's default), so the auto-detect must resolve active = LOW and PARK
    // the line HIGH. A line left floating, or parked active, drains the cell between samples for ever.
    CHK("P6e ADC_CTRL (37) ends board_init() as an OUTPUT", g_gpio.mode[MR_UI_ADC_CTRL] == (int)OUTPUT);
    CHK("P6f ... parked INACTIVE for an idle-HIGH board",   g_gpio.level[MR_UI_ADC_CTRL] == (int)HIGH);
#endif
    CHK("P6g ... and no conversion happened at boot",       g_gpio.analog_calls == 0);

    // ================================================================================================ P7
    // BUTTON POLARITY — INPUT_PULLUP, so pressed == LOW.
    g_gpio.read_returns = LOW;   CHK("P7a LOW  -> pressed",      mrui::button_pressed() == true);
    g_gpio.read_returns = HIGH;  CHK("P7b HIGH -> not pressed",  mrui::button_pressed() == false);

    // ================================================================================================ P8
#if !MR_PROBE_V4
    // ★★ THE BATTERY READER (plan Task 9 / slice UI-9, spec §7). Until this slice `battery_sample_mv()` was a
    //    hardcoded `-1` and the only check here was "it is still -1". Now it reads hardware, so what has to be
    //    measured is the SHAPE of the read, not just its answer:
    //      · the divider is ENABLED for the burst and DISABLED after it, with no per-tick residue;
    //      · the polarity is the AUTO-DETECTED one, in BOTH worlds — Heltec inverted the line past rev 3.2 while
    //        keeping the "V3" name, so a hardcoded level is wrong on half the boards;
    //      · an implausible reading answers UNAVAILABLE (<0 -> the panel's `--`), never a fabricated voltage.
    // ⓘ WHAT THIS PROBE STILL CANNOT SEE, stated rather than implied: whether the COMBINED ADC SCALE and the DETECTED
    //   POLARITY are right for the board in the operator's hand. The shim invents the raw counts. Only a multimeter
    //   closes that, which is why Task 9 owes a bench entry and not just this file.
    //
    // ★ THE EXPECTED MILLIVOLTS ARE DERIVED FROM THE REFERENCE PORT, NOT COPIED FROM THE FILE UNDER TEST — otherwise
    //   the check would restate the implementation and pass by construction. Source: MeshCore's working V3 port,
    //   ~/MeshCore/variants/heltec_v3/HeltecV3Board.h `getBattMilliVolts()` :79-92 —
    //       analogReadResolution(10); mv = (5.42 * (3.3 / 1024.0) * mean_of_8_raw) * 1000
    //   evaluated here in DOUBLE while the firmware uses float, hence the ±1 mV tolerance (measured: the two agree
    //   exactly at every raw tried below; the tolerance is for the boundary cases nobody has enumerated).
    const double kRefMvPerCount = 5.42 * (3.3 / 1024.0) * 1000.0;
    auto ref_mv = [&](int raw) { return int32_t(kRefMvPerCount * double(raw)); };
    auto near1  = [](int32_t a, int32_t b) { return a - b <= 1 && b - a <= 1; };

    // ---- polarity world A: the pin idles HIGH under BOTH pulls -> externally held -> ACTIVE is LOW (pre-3.2) -------
    g_gpio = ProbeGpio();                                  // read_returns = HIGH under every mode
    g_gpio.read_under_pullup = HIGH; g_gpio.read_under_pulldown = HIGH;
    (void)mrui::board_init();
    // ★★ THE POLARITY PROBE ITSELF, and this is the QA finding that produced it: `pinMode(pin, INPUT)` selects NO
    //    PULL, so on a line nothing external holds, the read is INDETERMINATE and the "inactive" park becomes a coin
    //    flip that leaves the divider ENABLED half the time. Two opposite pulls make the floating case DETECTABLE.
    CHK("P8t the polarity probe asked for BOTH internal pulls",
        g_gpio.saw_pullup[MR_UI_ADC_CTRL] && g_gpio.saw_pulldown[MR_UI_ADC_CTRL]);
    CHK("P8u ... and never a bare INPUT (no pull = indeterminate)", !g_gpio.saw_bare_input[MR_UI_ADC_CTRL]);
    g_gpio.ctrl_pin       = MR_UI_ADC_CTRL;                // snapshot the control line at every conversion
    g_gpio.analog_returns = 223;                           // a plausible cell: ~3.9 V through the 5.42 ADC scale
    const int writes_before = g_gpio.write_calls;
    const int32_t mv_a = mrui::battery_sample_mv();
    CHK("P8a resolution set to the 10 bits the divisor assumes", g_gpio.analog_res_bits == 10);
    CHK("P8b exactly 8 conversions (the mean of 8)",       g_gpio.analog_calls == 8);
    CHK("P8c ... all on the VBAT ADC input",               g_gpio.analog_pin == MR_UI_VBAT_READ);
    CHK("P8d the divider was ACTIVE (LOW) for every one",  g_gpio.ctrl_low_during_read == 8);
    CHK("P8e ... and never sampled while it was inactive", g_gpio.ctrl_high_during_read == 0);
    CHK("P8f exactly two control writes: enable + disable", g_gpio.write_calls - writes_before == 2);
    CHK("P8g the line is parked INACTIVE afterwards",      g_gpio.level[MR_UI_ADC_CTRL] == (int)HIGH);
    CHK("P8h a plausible raw yields the reference mV",     near1(mv_a, ref_mv(223)));

    // ---- the `--` contract: an implausible reading is UNAVAILABLE, never a number ----------------------------------
    // The window is this tree's existing 1S-LiPo one (src/firmware_commands.cpp's read_batt_mv), so the four edges are
    // checked from OUTSIDE the file under test: 114 -> 1991 mV (reject), 115 -> 2008 (accept), 257 -> 4488 (accept),
    // 258 -> 4506 (reject). raw 0 is the disconnected-divider case, and `0.0V` on a safety panel is the defect.
    g_gpio.analog_returns = 0;    CHK("P8i raw 0 (dead divider) -> UNAVAILABLE, not 0.0V", mrui::battery_sample_mv() < 0);
    g_gpio.analog_returns = 114;  CHK("P8j just below the 1S window -> UNAVAILABLE",       mrui::battery_sample_mv() < 0);
    g_gpio.analog_returns = 115;  CHK("P8k the bottom of the window -> a reading",         near1(mrui::battery_sample_mv(), ref_mv(115)));
    g_gpio.analog_returns = 257;  CHK("P8l the top of the window -> a reading",            near1(mrui::battery_sample_mv(), ref_mv(257)));
    g_gpio.analog_returns = 258;  CHK("P8m just above the window -> UNAVAILABLE",          mrui::battery_sample_mv() < 0);
    CHK("P8n an UNAVAILABLE read still parks the line",    g_gpio.level[MR_UI_ADC_CTRL] == (int)HIGH);

    // ---- polarity world B: the pin idles LOW under BOTH pulls -> ACTIVE is HIGH (boards past rev 3.2) -------------
    // This is the half a hardcoded level gets wrong, and the reason the reference port probes instead of defining a
    // constant. ⓘ `read_returns = LOW` alone reproduces it (both pulls fall through to the same answer).
    g_gpio = ProbeGpio();
    g_gpio.read_returns = LOW;
    (void)mrui::board_init();
    CHK("P8o idle-LOW board: parked INACTIVE means LOW",   g_gpio.level[MR_UI_ADC_CTRL] == (int)LOW);
    g_gpio.ctrl_pin       = MR_UI_ADC_CTRL;
    g_gpio.analog_returns = 223;
    const int32_t mv_b = mrui::battery_sample_mv();
    CHK("P8p ... the divider was ACTIVE (HIGH) for every conversion", g_gpio.ctrl_high_during_read == 8);
    CHK("P8q ... and never sampled while it was inactive",            g_gpio.ctrl_low_during_read == 0);
    CHK("P8r ... parked back to LOW afterwards",                      g_gpio.level[MR_UI_ADC_CTRL] == (int)LOW);
    // ⚠ P8s AND P6g ARE NAMED HERE AS THE TWO P8-ERA CHECKS NO CONTROL REDDENS, rather than left to look covered.
    //   P8s is a VACUITY GUARD on this probe, not a property of board_ui.cpp: it says the shim's raw count reaches the
    //   formula the same way in both polarity worlds, so P8h is comparing something real in each. P6g is NEGATIVE
    //   SPACE (boot takes no conversion); reddening it needs a mutation that ADDS a call, which no sed of this file
    //   can express cleanly. Both are cheap and honest; neither may be counted as measured coverage.
    CHK("P8s ... and the VALUE is polarity-independent",              mv_b == mv_a);

    // ---- polarity world C: the line FLOATS — the two pulls DISAGREE. THE CASE THE GUARD EXISTS FOR. ---------------
    // ★★ Nothing in this tree or in the vendor port establishes that GPIO 37 has a defined idle level: the reference
    //    port reads it under a BARE `INPUT` on two different boards and documents no pull, and its own V4 board drops
    //    the probe and hardcodes the level. ⇒ a floating line is a REAL possibility, and the wrong answer is not a bad
    //    voltage — it is parking the divider ENABLED for ever on a battery-powered safety device ([[B90]] restated).
    // ⇒ C2: REFUSE. The reader answers UNAVAILABLE (panel `--`) and takes NO conversion at all, rather than driving a
    //   line whose polarity is a coin flip. ⛔ This does NOT make the park provably safe — no level is known-inactive
    //   on EVERY revision when the line floats — it makes the refusal detectable and the park documented. The residual
    //   is owed to the owner and is stated at `kAdcCtrlFailsafePark`.
    g_gpio = ProbeGpio();
    g_gpio.read_under_pullup = HIGH; g_gpio.read_under_pulldown = LOW;   // pulls win => nothing external holds it
    (void)mrui::board_init();
    g_gpio.ctrl_pin       = MR_UI_ADC_CTRL;
    g_gpio.analog_returns = 223;                           // a raw that WOULD be a plausible cell, to prove refusal
    const int w_before_float = g_gpio.write_calls;
    CHK("P8v a FLOATING control line -> UNAVAILABLE, never a guessed voltage", mrui::battery_sample_mv() < 0);
    CHK("P8w ... and not one conversion is taken",         g_gpio.analog_calls == 0);
    CHK("P8x ... and the line is not driven at all",       g_gpio.write_calls == w_before_float);
    // ★★★ P8y IS THE ASSERTION THIS WHOLE FINDING TURNS ON, AND UNTIL 2026-08-06 IT ASSERTED THE DEFECT.
    //    It used to read `== HIGH` and was labelled *"the boot park is still DETERMINISTIC"* — which was true and
    //    beside the point. HIGH is the level Heltec's V3.2 hardware update log documents as the MEASURING one
    //    (*"now need to pull up the ADC_Ctrl(GPIO 37)"*), so the refusal path parked the divider **ENABLED** and the
    //    probe agreed with it. ⇒ the property is not "deterministic", it is **documented-INACTIVE**: LOW.
    //    ⛔ The whole shipped 20-control set passed straight over this. A control that only proves the park is stable
    //       cannot separate a safe park from an unsafe one — that is the instrument-that-cannot-fail shape again.
    CHK("P8y ... and the boot park is the DOCUMENTED-INACTIVE level (LOW), not the coin flip",
        g_gpio.level[MR_UI_ADC_CTRL] == (int)LOW);
    CHK("P8z ... on a pin that is still a driven OUTPUT, never left floating",
        g_gpio.mode[MR_UI_ADC_CTRL] == (int)OUTPUT);

    // ---- polarity world C', THE MIRROR: the fallback must not be a FUNCTION OF THE READS AT ALL. -----------------
    // ⚠ HONEST ABOUT WHAT THIS WORLD IS: an internal pull-up cannot read LOW on a genuinely floating line, so this is
    //   a SHIM-ONLY input, not a board state. Its job is to separate the fix from the tempting wrong one — "park the
    //   DETECTED level even though detection failed". That expression yields HIGH in world C and LOW here, so a single
    //   world could be satisfied by it half the time; asserting the SAME park in both pins the fallback as a constant.
    g_gpio = ProbeGpio();
    g_gpio.read_under_pullup = LOW; g_gpio.read_under_pulldown = HIGH;
    (void)mrui::board_init();
    CHK("P8aa the floating fallback ignores the (meaningless) reads: still LOW",
        g_gpio.level[MR_UI_ADC_CTRL] == (int)LOW);
    g_gpio.ctrl_pin       = MR_UI_ADC_CTRL;
    g_gpio.analog_returns = 223;
    CHK("P8ab ... and the refusal still holds in the mirrored world", mrui::battery_sample_mv() < 0);

    g_gpio = ProbeGpio();                                  // leave the shim as the later cases expect (read_returns HIGH)
#else
    // V4 has one fixed battery-control contract: GPIO37 is HIGH only during the conversion burst and LOW at rest.
    // It must never run V3's polarity detector. Script contradictory pull answers to make any accidental probe visible.
    g_gpio = ProbeGpio();
    g_gpio.read_under_pullup = HIGH;
    g_gpio.read_under_pulldown = LOW;
    (void)mrui::board_init();
    CHK("P8a V4 requests neither ADC-control pull", !g_gpio.saw_pullup[MR_UI_ADC_CTRL] &&
                                                     !g_gpio.saw_pulldown[MR_UI_ADC_CTRL]);
    CHK("P8b V4 never reads ADC-control polarity",  g_gpio.read_calls == 0);
    CHK("P8c V4 ADC_CTRL is an OUTPUT",             g_gpio.mode[MR_UI_ADC_CTRL] == (int)OUTPUT);
    CHK("P8d V4 ADC_CTRL parks LOW",                g_gpio.level[MR_UI_ADC_CTRL] == (int)LOW);

    g_gpio.ctrl_pin = MR_UI_ADC_CTRL;
    g_gpio.analog_returns = 223;
    const int writes_before = g_gpio.write_calls;
    const int32_t mv = mrui::battery_sample_mv();
    const double kRefMvPerCount = 5.42 * (3.3 / 1024.0) * 1000.0;
    const int32_t ref_mv = int32_t(kRefMvPerCount * 223.0);
    CHK("P8e V4 sets the 10-bit ADC resolution",     g_gpio.analog_res_bits == 10);
    CHK("P8f V4 takes exactly eight conversions",   g_gpio.analog_calls == 8);
    CHK("P8g V4 samples only the VBAT input",        g_gpio.analog_pin == MR_UI_VBAT_READ);
    CHK("P8h V4 holds ADC_CTRL HIGH for every conversion", g_gpio.ctrl_high_during_read == 8);
    CHK("P8i V4 never samples while ADC_CTRL is LOW",       g_gpio.ctrl_low_during_read == 0);
    CHK("P8j V4 performs exactly enable + disable writes",  g_gpio.write_calls - writes_before == 2);
    CHK("P8k V4 parks ADC_CTRL LOW after sampling",         g_gpio.level[MR_UI_ADC_CTRL] == (int)LOW);
    CHK("P8l V4 uses the approved empirical ADC scale",     mv - ref_mv <= 1 && ref_mv - mv <= 1);

    g_gpio.analog_returns = 0;
    CHK("P8m V4 dead divider is UNAVAILABLE, not 0.0V", mrui::battery_sample_mv() < 0);
    CHK("P8n V4 refusal still parks ADC_CTRL LOW",      g_gpio.level[MR_UI_ADC_CTRL] == (int)LOW);
    g_gpio = ProbeGpio();
#endif

    // ================================================================================================ P9
    // ★ §B91 — THE PANEL-ACK REPORT, and the point is that it can say NO. Before UI-6, `board_init()` was void and
    // U8g2's own begin() returns 1 unconditionally (it never reads the bus), so a dead panel and a live one were
    // indistinguishable to the firmware. board_init() must probe the address and pass the answer up truthfully.
    // ⚠ SELF-CONTAINED SINCE §UI-9: this used to count Wire traffic from P6's board_init() and read `init_ack` from
    //   there. P8's two polarity worlds each re-run board_init(), so a probe that carried P6's counters forward would
    //   have read 3 or 4 transmissions and failed for a harness reason. Re-arm here instead of counting across cases.
    reset_counters();
    const bool init_ack = mrui::board_init();
    CHK("P9a board_init() probed one address",         g_wire.begin_tx == 1 && g_wire.end_tx == 1);
    CHK("P9b ... and it was the ruled 0x3C address",    g_wire.last_addr == 0x3C);
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

    // ================================================================================================ P11
    // ★★★ §B197/§B200 — THE BUTTON AS A LIGHT-SLEEP WAKE SOURCE, ARMED PER SLEEP AND ALWAYS DISARMED. The pair has no
    //     observable effect on a host beyond WHICH platform calls it makes, WITH WHAT, IN WHAT ORDER, and WHAT IT
    //     DOES WITH THEIR RETURN CODES — so those four things are exactly what is measured. Nothing here proves the
    //     silicon wakes; that is metal.
    // ⛔⛔ THESE CHECKS WERE RETARGETED, NOT EXTENDED. The §B197 versions pinned "arm once, at boot" and would have
    //     gone GREEN against [[B200]] — the level trigger left armed on a running core, which panics the node on a
    //     long press. A check that passes against the defect is the failure mode this project keeps recording.
    // ⚠ P11a is NEGATIVE SPACE and is checked FIRST, before anything arms anything: `board_init()` must NOT arm the
    //   wake source as a side effect. Arming is an explicit, separately-reported step precisely so its FAILURE can be
    //   propagated — folded into `board_init()` it would be indistinguishable from a dead panel.
    reset_counters();
    (void)mrui::board_init();
    CHK("P11a board_init() arms NO wake source (it is a separate, reported step)",
        g_wake.gpio_wakeup_calls == 0 && g_wake.sleep_enable_calls == 0);

    // ---- the success path ----------------------------------------------------------------------------------------
    // ⓘ `read_returns` defaults to HIGH = NOT pressed (INPUT_PULLUP), so the re-sample below passes here.
    reset_counters();
    const mrui::WakeArm wake_ok = mrui::arm_button_wake();
    CHK("P11b both platform calls succeed -> arm_button_wake() reports armed", wake_ok == mrui::WakeArm::armed);
    CHK("P11c exactly one gpio_wakeup_enable",          g_wake.gpio_wakeup_calls == 1);
    CHK("P11d ... on the USER BUTTON pin",              g_wake.last_gpio == MR_UI_BTN_PIN);
    // ★★ THE POLARITY, AND IT IS THE SAME FACT `button_pressed()` READS: INPUT_PULLUP -> pressed is LOW. A HIGH-level
    //    source would wake the node CONTINUOUSLY while the button is NOT pressed — i.e. it would never sleep, which is
    //    a defect that LOOKS like the fix working. The edge triggers are the other wrong answers and are expressible.
    CHK("P11e ... with the ACTIVE-LOW level trigger",   g_wake.last_intr == GPIO_INTR_LOW_LEVEL);
    CHK("P11f exactly one esp_sleep_enable_gpio_wakeup", g_wake.sleep_enable_calls == 1);
    // ★ ORDER: the pin must be configured BEFORE the source is admitted to the next sleep. Two "was it called" flags
    //   cannot see a swap; the sequence stamps can.
    CHK("P11g ... and it runs AFTER the pin was configured",
        g_wake.seq_gpio_wakeup > 0 && g_wake.seq_sleep_enable > g_wake.seq_gpio_wakeup);
    // ⛔ A SUCCESSFUL ARM MUST NOT TEAR ITSELF DOWN. The rollback checked below is for the FAILURE path only; a
    //   disarm on the success path would hand the caller an `armed` verdict over a pin that is no longer armed.
    CHK("P11g2 ... and nothing was disarmed on the success path",
        g_wake.gpio_disable_calls == 0 && g_wake.sleep_disable_calls == 0);

    // ---- ★★★ P11k — THE RE-SAMPLE. THIS IS THE [[B200]] CHECK, AND IT IS THE ONE THAT WOULD HAVE CAUGHT IT --------
    // ⛔⛔ Arming a LOW-LEVEL wake on a pin that is ALREADY LOW arms an interrupt that is asserted the instant it
    //    exists and cannot be cleared while the finger stays down. With the CPU still running (the caller must not
    //    sleep after a refusal) that is an ISR storm ⇒ `Interrupt wdt timeout on CPU1`, which is exactly what the
    //    owner reproduced on metal with a long press. ⇒ the pin is read FIRST and the platform is not called at all.
    // ⚠ It must be the PIN, not the UI's debounced FSM: the FSM is updated at tick time and the press can land
    //   between that tick and this call. This shim only has a pin, which is the point.
    reset_counters();
    g_gpio.read_returns = LOW;                       // the finger is DOWN at the instant of the arm
    const mrui::WakeArm held = mrui::arm_button_wake();
    CHK("P11k a HELD button REFUSES the arm (button_down), it does not arm anyway",
        held == mrui::WakeArm::button_down);
    CHK("P11k2 ... and NOT ONE platform call was made (the storm is never armed)",
        g_wake.gpio_wakeup_calls == 0 && g_wake.sleep_enable_calls == 0 &&
        g_wake.gpio_disable_calls == 0 && g_wake.sleep_disable_calls == 0);
    g_gpio.read_returns = HIGH;

    // ---- failure arm 1: the pin cannot be configured --------------------------------------------------------------
    // ⛔ THE WHOLE FAIL-SAFE RESTS ON THIS REPORTING `failed`. `armed` here is [[B197]] made permanent: the caller
    //    would let the node sleep with no user wake source at all, leaving only DIO1 and the ≤1 s timer.
    // ★★★ AND IT MUST ROLL BACK IN FULL — round 4's find, and it is driver-evidenced rather than defensive: in the
    //    linked `gpio_wakeup_enable()`, `rtc_gpio_wakeup_enable()`'s result is moved into the RETURN register and
    //    control FALLS THROUGH to the digital register writes. ⇒ a NON-OK return can already have set INT_TYPE and
    //    WAKEUP_ENABLE. "It failed" does not mean "it wrote nothing".
    reset_counters();
    g_wake.gpio_wakeup_result = ESP_ERR_INVALID_ARG;
    CHK("P11h gpio_wakeup_enable FAILS -> failed (the fail-closed input)",
        mrui::arm_button_wake() == mrui::WakeArm::failed);
    CHK("P11h2 ... and nothing is admitted to sleep after that failure", g_wake.sleep_enable_calls == 0);
    CHK("P11h3 ... and it STILL tears down in full (the writes may have happened)",
        g_wake.set_intr_type_calls == 1 && g_wake.gpio_disable_calls == 1 &&
        g_wake.sleep_disable_calls == 1 && g_wake.last_intr_type == GPIO_INTR_DISABLE);

    // ---- failure arm 2: the source cannot be admitted to sleep, AND THE PARTIAL ARM IS ROLLED BACK ----------------
    // ★ A SEPARATE ARM, not a duplicate: checking only the first return is a real and tempting half-fix — the pin is
    //   configured, nothing complains, and the source is still never admitted to `esp_light_sleep_start()`.
    // ⛔⛔ AND THE ROLLBACK IS THE [[B200]] HALF OF IT: a pin left carrying a live level interrupt that NO SLEEP will
    //    ever consume is the storm arrived at by the failure path instead of by a held button. The caller cannot fix
    //    it — it is told `failed` and owes no disarm — so this function must undo its own writes.
    // ⛔⛔⛔ P11j WAS RETARGETED IN ROUND 4 AND ITS OLD FORM IS THE REASON THIS COMMENT EXISTS: it asserted only
    //    `gpio_disable_calls == 1`, i.e. it PINNED the insufficient rollback and stayed GREEN while that path
    //    recreated exactly the residue round 3 had just removed. **Fourth instrument in this arc to enforce the
    //    shape it should forbid.** It now requires the FULL teardown, in order.
    reset_counters();
    g_wake.sleep_enable_result = ESP_ERR_INVALID_ARG;
    const mrui::WakeArm arm2 = mrui::arm_button_wake();
    CHK("P11i esp_sleep_enable_gpio_wakeup FAILS -> failed", arm2 == mrui::WakeArm::failed);
    CHK("P11j ... and the pin HAD been configured first",   g_wake.gpio_wakeup_calls == 1);
    CHK("P11j2 ... the rollback clears the INTERRUPT TYPE too, not just bit 10",
        g_wake.set_intr_type_calls == 1 && g_wake.last_intr_type_gpio == MR_UI_BTN_PIN &&
        g_wake.last_intr_type == GPIO_INTR_DISABLE);
    CHK("P11j2b ... and the wake bit and the source as well (the FULL teardown)",
        g_wake.gpio_disable_calls == 1 && g_wake.last_disable_gpio == MR_UI_BTN_PIN &&
        g_wake.sleep_disable_calls == 1 && g_wake.last_disable_src == ESP_SLEEP_WAKEUP_GPIO);
    CHK("P11j3 ... rolled back BEFORE returning, i.e. after the failed admit",
        g_wake.seq_set_intr_type > g_wake.seq_sleep_enable);
    // ★★ ORDER, ON THE ROLLBACK PATH TOO: the storming field goes first. A rollback that cleared bit 10 first would
    //    be measurably the same code as the shipped disarm only if BOTH are checked, which is what keeps the one
    //    shared helper honest from the behavioural side.
    CHK("P11j4 ... interrupt type cleared BEFORE the wake bit",
        g_wake.seq_set_intr_type < g_wake.seq_gpio_disable);

    // ---- ★★★ P11l — THE DISARM. The half [[B197]] did not have at all ---------------------------------------------
    // ⛔ Both withdrawals are required: `gpio_wakeup_disable` takes the LEVEL INTERRUPT off the pin (the half that
    //    storms a running core) and `esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO)` takes the SOURCE out of
    //    the next sleep (so no later sleep inherits an arm nobody asked for). Either alone is a plausible half-fix.
    // ⛔ ...and it must be the GPIO source, NEVER `ESP_SLEEP_WAKEUP_EXT1` — that is the radio's DIO1 wake, owned by
    //    fw_main. Disabling it here would silently take the radio's wake path down, which no panel fix may do.
    reset_counters();
    CHK("P11l disarm_button_wake() succeeds when the platform agrees", mrui::disarm_button_wake() == true);
    CHK("P11l2 ... it clears the pin's WAKE-ENABLE bit",
        g_wake.gpio_disable_calls == 1 && g_wake.last_disable_gpio == MR_UI_BTN_PIN);
    CHK("P11l3 ... and withdraws the GPIO source (not EXT1 — that is the radio's)",
        g_wake.sleep_disable_calls == 1 && g_wake.last_disable_src == ESP_SLEEP_WAKEUP_GPIO);
    CHK("P11l4 ... and it arms nothing while tearing down",
        g_wake.gpio_wakeup_calls == 0 && g_wake.sleep_enable_calls == 0);
    // ★★★ P11l5/l6 — THE ROUND-3 CHECK, AND THE PANIC CAME BACK THROUGH ITS ABSENCE. `gpio_wakeup_disable()` clears
    //   the WAKE-ENABLE bit and nothing else (verified in the linked driver), so the pin kept `GPIO_INTR_LOW_LEVEL`
    //   across the disarm AND across a CPU-only reset, and the next boot stormed as soon as the shared GPIO ISR was
    //   installed. ⛔ The type must be set to GPIO_INTR_DISABLE — not merely "some other level", which is why the
    //   value is asserted rather than just the call.
    CHK("P11l5 ... and it DISABLES the pin's interrupt TYPE (B200 round 3)",
        g_wake.set_intr_type_calls == 1 && g_wake.last_intr_type_gpio == MR_UI_BTN_PIN);
    CHK("P11l6 ... to GPIO_INTR_DISABLE, not to another level",
        g_wake.last_intr_type == GPIO_INTR_DISABLE);
    // ★★★ P11l7 — THE ORDER, AND IT IS A SAFETY PROPERTY (round 4). INT_TYPE is the field that MAKES the pin
    //   interrupt; the wake-enable bit only lets that interrupt end a sleep. After a GPIO wake the CPU is RUNNING
    //   and the button is still LOW — that is what woke it — so clearing the wake bit first spends operations with
    //   the storm still live. ⇒ the type goes FIRST, and a swap must be visible, which needs the sequence stamps.
    CHK("P11l7 ... and the TYPE is cleared FIRST, before the wake bit",
        g_wake.seq_set_intr_type > 0 && g_wake.seq_set_intr_type < g_wake.seq_gpio_disable);
    CHK("P11l8 ... and the source is withdrawn last",
        g_wake.seq_sleep_disable > g_wake.seq_gpio_disable);

    // ---- the three disarm failure arms, separately ----------------------------------------------------------------
    // ★ Each is checked ALONE, because a teardown that stops at its first failure leaves precisely the state it
    //   exists to remove — so EVERY withdrawal must be attempted even when an earlier one refuses.
    reset_counters();
    g_wake.gpio_disable_result = ESP_ERR_INVALID_ARG;
    CHK("P11m the pin refusing to disarm is reported as failure", mrui::disarm_button_wake() == false);
    CHK("P11m2 ... and the OTHER TWO withdrawals still ran (no early return)",
        g_wake.set_intr_type_calls == 1 && g_wake.sleep_disable_calls == 1);
    reset_counters();
    g_wake.sleep_disable_result = ESP_ERR_INVALID_ARG;
    CHK("P11n the source refusing to withdraw is reported as failure", mrui::disarm_button_wake() == false);
    CHK("P11n2 ... and the pin's wake bit and type were STILL taken down",
        g_wake.gpio_disable_calls == 1 && g_wake.set_intr_type_calls == 1);
    reset_counters();
    g_wake.set_intr_type_result = ESP_ERR_INVALID_ARG;
    CHK("P11o the TYPE refusing to clear is reported as failure", mrui::disarm_button_wake() == false);
    CHK("P11o2 ... and the other two withdrawals still ran",
        g_wake.gpio_disable_calls == 1 && g_wake.sleep_disable_calls == 1);

    // ---- ★★★ P11p — "NOTHING WAS ENABLED" IS SUCCESS, AND THIS IS WHAT KEEPS THE BOOT SCRUB SAFE -----------------
    // ⛔⛔ The same teardown runs ONCE AT BOOT as a scrub (a reset taken while armed leaves a level nothing disarmed).
    //   At boot no source has been enabled, and the real `esp_sleep_disable_wakeup_source()` answers
    //   `ESP_ERR_INVALID_STATE` in exactly that case — verified in the linked `sleep_modes.c.obj`, where a clear
    //   trigger bit falls through every branch to that return. ⇒ if this were treated as a failure, the node would
    //   latch sleep off on EVERY boot, for ever. "Already withdrawn" IS the post-state this function produces.
    // ⚠ It must stay narrow: only INVALID_STATE is forgiven (P11n proves INVALID_ARG still fails).
    reset_counters();
    g_wake.sleep_disable_result = ESP_ERR_INVALID_STATE;
    CHK("P11p a source that was NEVER ENABLED is not a failure (boot scrub)",
        mrui::disarm_button_wake() == true);
    CHK("P11p2 ... and the pin was still fully quietened anyway",
        g_wake.gpio_disable_calls == 1 && g_wake.set_intr_type_calls == 1 &&
        g_wake.last_intr_type == GPIO_INTR_DISABLE);

    // ================================================================================================ P12
    // ★★★ §CHROME-2 — THE TWO GENERIC CANVAS PRIMITIVES (design §8.1). They have no observable effect on a host
    //     beyond WHICH U8g2 call they choose, WITH WHAT ARGUMENTS, IN WHAT ORDER, and WHETHER THEY REACH THE BUS —
    //     so those four things are exactly what is measured, which is the same rule P11 follows.
    // ⚠⚠ A CALL **COUNT** IS NOT COVERAGE HERE, and that is the whole reason the shim records arguments: "drawXBM was
    //    called once" stays green against a board that transposes x with y, swaps w with h, or copies the bitmap into
    //    a scratch buffer and hands over a different pointer. U8g2 CLIPS silently, so every one of those defects
    //    renders as a blank or a smear on metal and as a PASS in a count-only probe — the instrument-that-cannot-fail
    //    shape this arc has recorded five times. ⇒ every argument is asserted, and the four coordinates are
    //    DELIBERATELY ALL DIFFERENT so a transposition cannot coincide with the right answer.
    // ⓘ The bytes are arbitrary: this probe measures the FORWARD, not the glyph. §CHROME-1's native test is what
    //   decodes real assets against the row-major/LSB-first contract.
    static const uint8_t kProbeBits[8] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80};

    // ---- draw_bitmap: the RIGHT U8g2 call, the arguments through unchanged, and nothing else touched -------------
    reset_counters();
    mrui::draw_bitmap(3, 11, 7, 5, kProbeBits);
    CHK("P12a draw_bitmap forwards to drawXBM exactly once",   g_u8.drawXBM == 1);
    // ★★ THE `P` VARIANT IS A REAL WRONG ANSWER, NOT A HYPOTHETICAL: `drawXBMP` reads through `u8g2_pgm_read`, the
    //    AVR separate-address-space form. On ESP32-S3/nRF52 flash is memory-mapped and §CHROME-1's assets are plain
    //    `inline constexpr` objects, so the two are indistinguishable HERE and differ only in which platform they
    //    corrupt — which is precisely why the choice is pinned by an assertion rather than by a comment.
    CHK("P12b ... and NEVER to drawXBMP (the AVR PROGMEM form)", g_u8.drawXBMP == 0);
    CHK("P12c ... x,y,w,h arrive in that order, unchanged",
        g_u8.xbm_x == 3 && g_u8.xbm_y == 11 && g_u8.xbm_w == 7 && g_u8.xbm_h == 5);
    // ★ THE POINTER ITSELF, not a value derived from it: §8.1 forbids the board copying a bitmap into a buffer, and a
    //   content comparison would pass over exactly that.
    CHK("P12d ... and the byte POINTER is forwarded, never copied", g_u8.xbm_bits == kProbeBits);
    CHK("P12e ... and it draws nothing ELSE (no frame, box, text or rule)",
        g_u8.drawFrame == 0 && g_u8.drawBox == 0 && g_u8.drawStr == 0 && g_u8.drawHLine == 0);
    // ⛔ COMPOSE-ONLY (§11.2). `bus_ops()` counts only begin/nextPage/setPowerSave — the three calls that actually
    //    reach the panel — so a primitive that pushed a page, or flushed, or blanked, is visible here and nowhere else.
    CHK("P12f ... and it reaches the BUS not once (compose-only)", g_u8.bus_ops() == 0);

    // ---- draw_rect: an OUTLINE, never a filled box ---------------------------------------------------------------
    reset_counters();
    mrui::draw_rect(1, 12, 9, 6);
    CHK("P12g draw_rect forwards to drawFrame exactly once",   g_u8.drawFrame == 1);
    // ⚠ `drawBox` is the tempting wrong answer — same signature, one letter apart — and on the panel it INVERTS the
    //   rail slot, hiding the very icon the selection frame exists to point at (§3.2).
    CHK("P12h ... and NEVER to drawBox (a filled rectangle)",   g_u8.drawBox == 0);
    CHK("P12i ... x,y,w,h arrive in that order, unchanged",
        g_u8.rect_x == 1 && g_u8.rect_y == 12 && g_u8.rect_w == 9 && g_u8.rect_h == 6);
    CHK("P12j ... and it draws nothing ELSE",
        g_u8.drawXBM == 0 && g_u8.drawXBMP == 0 && g_u8.drawStr == 0 && g_u8.drawHLine == 0);
    CHK("P12k ... and it reaches the BUS not once (compose-only)", g_u8.bus_ops() == 0);

    // ---- the panel's edge: a LEGAL request must still be legal after the board has handled it ---------------------
    // ⚠ HONEST ABOUT WHAT THIS IS (the P8s/P6g convention): it is NOT independent coverage. U8g2 clips silently, so
    //   "it did not crash" proves nothing and the probe must assert the coordinates it passed — but any mutation that
    //   moves them also breaks P12c/P12i, so no negative control reddens THIS pair alone. It is cheap and it states
    //   the boundary in the file where a future offset/padding change would be written; it is not measured coverage.
    reset_counters();
    mrui::draw_bitmap(120, 56, 8, 8, kProbeBits);     // the bottom-right corner: last pixel exactly (127, 63)
    mrui::draw_rect(118, 54, 10, 10);
    CHK("P12l a corner bitmap stays inside 128x64 after the board handled it",
        g_u8.xbm_x + g_u8.xbm_w == 128 && g_u8.xbm_y + g_u8.xbm_h == 64);
    CHK("P12m ... and so does a corner outline",
        g_u8.rect_x + g_u8.rect_w == 128 && g_u8.rect_y + g_u8.rect_h == 64);

    // ---- inside a real page loop: the ONE bus boundary is still next_page() ---------------------------------------
    // ★ THE PROPERTY §11.2 ACTUALLY NAMES: "bitmap/frame calls touch no I2C outside the existing next_page()
    //   boundary". Drawing both primitives on every page of a full frame must cost EXACTLY the eight page transfers a
    //   text-only frame costs — no more, and not fewer (fewer would mean the frame stopped completing).
    reset_counters();
    mrui::begin_frame();
    CHK("P12n begin_frame still composes only, with the new primitives present", g_u8.bus_ops() == 0);
    int chrome_pages = 0;
    do {
        probe_scene();
        mrui::draw_bitmap(0, 0, 7, 7, kProbeBits);
        mrui::draw_rect(0, 10, 10, 10);
        if (++chrome_pages > 32) break;
    } while (mrui::next_page());
    CHK("P12o a frame drawing both primitives is still exactly 8 page transfers",
        chrome_pages == 8 && g_u8.nextPage == 8);
    CHK("P12p ... each primitive ran once per page (U8g2 re-clips, it accumulates nothing)",
        g_u8.drawXBM == 8 && g_u8.drawFrame == 8);
    CHK("P12q ... and the ONLY bus traffic in the whole frame is those 8 transfers",
        g_u8.bus_ops() == 8 && g_u8.begin == 0 && g_u8.setPowerSave == 0);

    reset_counters();   // leave the shims as constructed for any later case

    printf("Heltec %s board_ui probe: %d passed / %d failed / %d total\n",
           MR_PROBE_V4 ? "V4" : "V3", g_pass, g_fail, g_pass + g_fail);
    return g_fail == 0 ? 0 : 1;
}
