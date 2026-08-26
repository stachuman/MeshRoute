// MeshRoute — variants/heltec_common/board_ui.h
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
// ★★★ §CHROME-2 — THE TWO GENERIC CANVAS PRIMITIVES THE STATUS STRIP AND THE NAVIGATION RAIL WILL DRAW THROUGH
//   (design `docs/superpowers/specs/2026-08-15-heltec-mobile-status-navigation-ui-design.md` §8.1). They are
//   GENERIC ON PURPOSE and the boundary above is the whole point of the redesign: ⛔ this canvas must NEVER gain a
//   `draw_mail_icon()` / `draw_home_icon()` and ⛔ must never include `firmware_ui_model.h`, `firmware_ui_chrome.h`
//   or `firmware_ui_icons.h`. The firmware owns icon IDENTITY, PLACEMENT and STATE SELECTION; the board copies
//   pixels. That is exactly what lets the Heltec V4 port reuse the same bitmaps and the same renderer unchanged.
// ★ COMPOSE-ONLY, like every other draw call here: they write the page buffer and touch NO bus. The one and only
//   bus boundary remains `next_page()` (see the paint contract at the top of this file).
//
// ⛔⛔ THE BYTE-ORDER CONTRACT IS **NOT** RESTATED HERE, AND THAT IS DELIBERATE (§8.1's 2026-08-16 amendment): it is
//   defined ONCE, in `src/firmware_ui_icons.h`'s header block — U8g2/XBM convention, row-major, LSB-first, 1 bit per
//   pixel, rows padded to whole bytes (`stride = (w + 7) / 8`). ⚠ A second, drifting copy of that contract in a board
//   file is precisely how the same asset renders MIRRORED on one port and BIT-REVERSED on another. `bits` must point
//   at `stride * h` readable bytes; the CALLER owns that buffer and its lifetime.
// ★ IT MAPS 1:1 ONTO U8g2's NATIVE CALL — no conversion, no per-board reinterpretation (`u8g2_DrawXBM`,
//   `clib/u8g2_bitmap.c:167`, walks rows with `blen = (w + 7) >> 3` and `u8g2_DrawHXBM` starts its mask at bit 0).
void draw_bitmap(int x, int y, int w, int h, const uint8_t* bits);
// ★ A ONE-PIXEL RECTANGLE OUTLINE — the rail's selection frame (§3.2), never a filled box.
// ⚠⚠ NAMED `draw_rect`, NOT the design's `draw_frame`, AND THE DEVIATION IS RECORDED AS A DESIGN AMENDMENT
//   (§8.1, 2026-08-16). `draw_frame` ALREADY EXISTS in `src/firmware_ui.cpp:946` as the WHOLE-SCREEN composer, so
//   the slice-3 renderer would otherwise call two `draw_frame`s meaning "compose the entire screen" and "draw a
//   rectangle outline" in the same function. The names are separable by namespace, but a reader's mistake is not.
void draw_rect(int x, int y, int w, int h);
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
//    failed      = the platform refused. ★★ EITHER failure path runs the FULL teardown before returning — not just
//                  the second one, and not just the wake bit: the IDF's `gpio_wakeup_enable()` writes the pin
//                  registers even on the path where it returns an error, so "it failed" does not mean "it wrote
//                  nothing". Nothing is owed afterwards, and the caller must not sleep — for the whole boot.
// ⚠ Neither call touches the radio's DIO1 `ext1` wake in fw_main.cpp's board_sleep_until(). Whether the RTC-domain
//   ext1 source and this digital-domain GPIO source COEXIST in ESP32-S3 light sleep is the design's one UNPROVEN
//   HARDWARE ASSUMPTION and is settled only on metal, independently per source
//   (docs/superpowers/specs/2026-08-14-b197-b198-ui-sleep-wake-design.md §3.1.2; bench script Part 23).
// ⓘ GPIO0 is also the ESP32-S3 boot strap, but that pin is sampled ONLY during RESET — arming it as a RUNTIME wake
//   source adds nothing to that hazard. Holding it through a reset still enters serial-download mode; if a board
//   looks bricked, RELEASE THE BUTTON AND RESET AGAIN (spec §3.1.1).
enum class WakeArm : uint8_t { armed = 0, button_down = 1, failed = 2 };
WakeArm arm_button_wake();
// ★★★ THE OTHER HALF, AND IT IS NOT OPTIONAL ON ANY PATH. THREE withdrawals, in this order: the pin's INTERRUPT
//   TYPE **first** (that is the field which actually drives the interrupt, and after a GPIO wake the CPU is running
//   with the button still low), then the pin's WAKE-ENABLE bit, then the GPIO wake source. false = the platform
//   refused, which the caller must treat as fatal-to-sleeping for the boot: a level source it cannot take down is
//   [[B200]] made durable.
// ★★ It is ONE shared teardown, also used by `arm_button_wake()`'s rollback paths — ⛔ never a second copy. Round 3
//   updated the disarm and not the rollback, and the rollback went on leaving the interrupt type set.
// ⛔⛔ THE INTERRUPT TYPE IS THE ROUND-3 ADDITION AND IT IS WHY THE PANIC CAME BACK: `gpio_wakeup_disable()` clears
//   ONLY the wake-enable bit (verified in the linked driver — see the block at the definition), so GPIO0 kept
//   `GPIO_INTR_LOW_LEVEL` across our disarm AND across a CPU-only reset, and the NEXT boot stormed as soon as
//   RadioLib installed the shared GPIO ISR.
// ★★★ CALLING IT WHEN NOTHING IS ARMED IS NOT MERELY HARMLESS — IT IS A SUPPORTED USE, and `src/fw_main.cpp` relies
//   on it: this is also the BOOT SCRUB, run once before the radio ISR is installed, because a reset taken WHILE
//   ARMED (a panic or WDT during sleep) leaves an armed level that no disarm ever ran for. ⇒ the "source was never
//   enabled" answer (`ESP_ERR_INVALID_STATE`) is treated as SUCCESS; only a genuine refusal reports false.
bool disarm_button_wake();
// One sample, taken now: enable the divider, read, disable it. <0 = UNAVAILABLE — no reader, or a reading outside the
// 1S-LiPo plausible window — and the caller renders `--` for it, never a substituted default (spec §7).
// ★ The CALLER decides WHEN: spec §7's boot + ~30 s cadence under the §5 MAC-idle predicate. This canvas owns no
//   cadence; a board file that acquired one would be deciding policy (spec §2.1, rule U3).
int32_t battery_sample_mv();

}  // namespace mrui
