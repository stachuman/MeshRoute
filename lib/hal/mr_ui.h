// MeshRoute — lib/hal/mr_ui.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §featuresplit slice 4: the board-UI seam (MR_FEAT_OLED). A board with a display (e.g. the heltec_v3's on-board
// SSD1306) implements these three hooks in a TU compiled under `#if MR_FEAT_OLED` (src/board_ui.cpp). EVERY other
// profile gets the inline no-ops below, so the fw_main call sites are UNCONDITIONAL (no `#if` sprawl at the call
// site — the same stub pattern as the TEAM/MOBILE features). The next board-UI PR just fills the seam; it pulls
// g_node / the config itself inside the .cpp, so this header stays dependency-light (only a Push forward-decl).
//
// Wiring (see src/fw_main.cpp): mr_ui_init() once at the end of setup(); mr_ui_tick() every mesh service pass
// (throttle inside); mr_ui_on_push() for each app Push drained (RX DM / channel / ACK / send-failed).
#pragma once
#include <cstdint>
#include "mr_features.h"

namespace meshroute { struct Push; }

#if MR_FEAT_OLED
void mr_ui_init();                                // boot: bring the panel up (called once, end of setup())
void mr_ui_tick(uint32_t now_ms);                 // main loop: periodic refresh — THROTTLE inside (called every service pass)
void mr_ui_on_push(const meshroute::Push& pu);    // event: an app Push worth surfacing (RX DM / channel / ACK / send-failed)
#else
// No display on this profile -> every hook inlines to nothing (the call sites stay unconditional).
inline void mr_ui_init() {}
inline void mr_ui_tick(uint32_t /*now_ms*/) {}
inline void mr_ui_on_push(const meshroute::Push& /*pu*/) {}
#endif
