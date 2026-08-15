// MeshRoute — variants/heltec_v3/board_ui.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// The display-INDEPENDENT canvas the UI feature layer draws through (spec 2026-07-31-onboard-oled-ui-design §2's hard
// boundary; plan Task 5 = slice UI-5). Nothing above this line knows U8g2 exists; nothing below it knows what a
// "screen" is. src/firmware_ui.cpp owns every decision about WHAT is drawn; board_ui.cpp owns only how pixels reach
// the panel — which is what makes the Heltec V4 port a pin table instead of a rewrite (same panel, zero render change).
//
// ★ THIS HEADER MUST NOT INCLUDE firmware_ui_model.h. An earlier plan draft did, and that inverted the boundary the
//   spec promises. <cstdint> is the ONLY include, deliberately: the invariant is then greppable, not merely stated.
//
// ★ THE PAINT CONTRACT EXISTS FOR A TIMING REASON, NOT A STYLISTIC ONE (spec §5). A full 128x64 SSD1306 frame is
//   1024 B, i.e. ~25 ms of BLOCKING I2C at 400 kHz, while the MAC's cts_to_data_gap_ms is 5 and measured turnarounds
//   are 5-8 ms — long enough to break an in-flight RTS/CTS/DATA exchange. So a frame is pushed ONE 128 B page at a
//   time (~3 ms), spread across service passes:
//
//       begin_frame();                                        // composes; touches no bus
//       do { draw the WHOLE scene; } while (next_page());      // exactly ONE page transfer per next_page()
//
//   ⚠ U8g2's page loop RE-CLIPS the whole scene per page and accumulates nothing, so the caller must re-draw
//     everything before EVERY next_page() — not once at frame start (spec §5's corrected note; an earlier draft would
//     have left seven of eight pages blank). Freeze the frame's inputs at begin_frame(): a frame spans several ticks
//     and live state changing mid-frame tears the image across page boundaries.
#pragma once
#include <cstdint>

namespace mrui {

enum class Font : uint8_t { small = 0, large };   // 6x10 / 10x20

// ★ §B91 (Task 6): board_init() REPORTS. It used to be `void`, so a dead panel was indistinguishable from a live one
//   — U8g2's own begin() always returns 1 (it performs no I2C ack check), which is exactly why the bench had no way to
//   tell "the panel is not on this rail" from "the render policy never painted". true = the panel ACKed its address;
//   false = nothing answered, and the CALLER (src/firmware_ui.cpp) is what turns that into a console line. The canvas
//   still knows nothing about consoles.
// ⚠ It is NOT a fatal error: a node with a dead panel must keep meshing. The UI keeps running blind.
bool board_init();
void begin_frame();                 // compose a new frame; does NOT touch the bus
bool next_page();                   // push ONE page (~3 ms); true while pages remain
void set_font(Font f);
void draw_text(int x, int y, const char* s);
void draw_hline(int x, int y, int w);
void set_power_save(bool on);       // panel off/on WITHOUT clearing display RAM; latched, repeat calls are no-ops
bool button_pressed();
// ★★★ §B200 — ARM THE BUTTON AS A LIGHT-SLEEP WAKE SOURCE, FOR ONE SLEEP ONLY. ⛔⛔ THE OLD CONTRACT IS WITHDRAWN
//   AND ITS WORDING IS KEPT HERE SO THE MISTAKE IS NOT REPEATED: §B197 declared `bool enable_button_wake()`, *"Called
//   ONCE, after board_init() … by src/firmware_ui.cpp's mr_ui_init()"*. That permanent arm is [[B200]]: the trigger is
//   LEVEL-triggered (light sleep admits no other kind), a level interrupt CANNOT BE CLEARED while the level holds, and
//   RadioLib's DIO1 attach keeps the shared GPIO ISR installed ⇒ a HELD BUTTON storms the ISR and trips the Interrupt
//   watchdog (`Core 1 panic'ed (Interrupt wdt timeout on CPU1)`, captured on metal 2026-08-15).
// ⇒ ★ THE CALL SITE IS NOW `src/fw_main.cpp`'s `board_sleep_until()`, IMMEDIATELY BEFORE `esp_light_sleep_start()`,
//   with `disarm_button_wake()` IMMEDIATELY AFTER IT RETURNS. The armed level then exists only while the CPU is
//   halted — the one state in which it cannot storm anything. ⛔ Nothing may arm this at boot, ever again.
// ★★ IT RE-SAMPLES THE PIN ITSELF, and that is deliberately INSIDE this function rather than left to the caller: a
//   caller cannot forget what it never had to remember. `button_down` means the finger is on the button at this
//   instant, so arming a LOW-level source would arm an ALREADY-ASSERTED interrupt — the storm, exactly. The UI's own
//   `InputFsm` is NOT an acceptable substitute: it is updated at tick time and the press can land between the tick
//   and the sleep. The ACTIVE-LOW level is not a new fact either — it is the same INPUT_PULLUP / button_pressed()
//   contract above, so there is ONE polarity authority, not two.
// ★★ armed       = BOTH platform calls succeeded; a press can now wake the halted CPU, and the caller OWES a disarm.
//    button_down = nothing was armed, nothing is owed, and the caller MUST NOT SLEEP this pass. Not a fault.
//    failed      = the platform refused. Any PARTIAL arm has been ROLLED BACK before returning (a half-armed pin is
//                  the storm), nothing is owed, and the caller must not sleep — for the whole boot (fail closed).
// ⚠ Neither call touches the radio's DIO1 `ext1` wake in fw_main.cpp's board_sleep_until(). Whether the RTC-domain
//   ext1 source and this digital-domain GPIO source COEXIST in ESP32-S3 light sleep is the design's one UNPROVEN
//   HARDWARE ASSUMPTION and is settled only on metal, independently per source
//   (docs/superpowers/specs/2026-08-14-b197-b198-ui-sleep-wake-design.md §3.1.2; bench script Part 23).
// ⓘ GPIO0 is also the ESP32-S3 boot strap, but that pin is sampled ONLY during RESET — arming it as a RUNTIME wake
//   source adds nothing to that hazard. Holding it through a reset still enters serial-download mode; if a board
//   looks bricked, RELEASE THE BUTTON AND RESET AGAIN (spec §3.1.1).
enum class WakeArm : uint8_t { armed = 0, button_down = 1, failed = 2 };
WakeArm arm_button_wake();
// ★★★ THE OTHER HALF, AND IT IS NOT OPTIONAL ON ANY PATH. Disarms the pin's level interrupt AND withdraws the GPIO
//   source from the next sleep, so a running CPU never carries an armed level. false = the platform refused, which
//   the caller must treat as fatal-to-sleeping for the boot: a level source it cannot take down is [[B200]] durable.
// ⓘ Calling it when nothing is armed is harmless — it is the same two withdrawals against state that is already
//   withdrawn — but the caller is told exactly when it is owed, so it never has to rely on that.
bool disarm_button_wake();
// One sample, taken now: enable the divider, read, disable it. <0 = UNAVAILABLE — no reader, or a reading outside the
// 1S-LiPo plausible window — and the caller renders `--` for it, never a substituted default (spec §7).
// ★ The CALLER decides WHEN: spec §7's boot + ~30 s cadence under the §5 MAC-idle predicate. This canvas owns no
//   cadence; a board file that acquired one would be deciding policy (spec §2.1, rule U3).
int32_t battery_sample_mv();

}  // namespace mrui
