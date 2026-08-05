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
int32_t battery_sample_mv();        // one sample; <0 = unavailable. Caller decides WHEN (spec §7)

}  // namespace mrui
