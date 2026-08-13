// MeshRoute — lib/hal/mr_ui.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §featuresplit slice 4: the board-UI seam (MR_FEAT_OLED). A board with a display (e.g. the heltec_v3's on-board
// SSD1306) implements these FOUR hooks (⛔ corrected in place 2026-08-13 — this line said "three" while the block at
// the fourth one already called it "THE FOURTH HOOK", so the file contradicted itself two lines apart)
// in a TU compiled under `#if MR_FEAT_OLED` (variants/heltec_v3/board_ui.cpp — §A0 2026-08-03; the port is
// per-BOARD, so V4 brings its own variants/heltec_v4/board_ui.cpp). EVERY other
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

// ★★★ THE FOURTH HOOK (§UI-14 follow-up, spec §3.6.1's IMMEDIATE notification requirement). Same shape and the same
// reason as the three above: the CONFIG cluster must be able to say *"the durable record just changed"* WITHOUT
// knowing that a panel exists. ⛔ NO `MR_FEAT_OLED` MAY LEAK INTO `src/firmware_config.cpp` — the call site stays
// unconditional and this header supplies the no-op, exactly as it does for `mr_ui_on_push`.
// ⚠ THE CONTRACT IS NARROW AND IT IS THE WHOLE POINT: call it ONLY after a `/mrcfg` write that ACTUALLY SUCCEEDED
//   and was ACTUALLY PERSISTED. ⛔ Not on a failed write (nothing changed, so a conflict would be invented), and
//   ⛔ not on a live-only `cfg set` (there is no durable record to disagree with). The OLED side re-reads the record
//   and compares; it must never be told "something changed" when nothing did.
// ★★ AND THE OTHER HALF OF THE CONTRACT, ADDED 2026-08-13 ([[B194]]): call it after EVERY such write from a
//   USER-INITIATED verb, not only where a covered field provably moved — the alternative makes every future writer
//   re-derive which fields the OLED covers, and one that forgets is silently non-compliant. It is safe because the
//   OLED side compares ONLY the covered fields, so a save that moved nothing covered raises nothing. ⛔ The INTERNAL
//   writers (fw_main's ctr lease / leaf-config adopt, firmware_remote's admin writes) stay SILENT: they are not
//   user-initiated, they assign no covered field, and the lease is on a timer. The rule, the seven call sites and the
//   measurement behind the exemption live at `§notify-every-save` in `src/firmware_config.cpp`.
#if MR_FEAT_OLED
void mr_ui_init();                                // boot: bring the panel up (called once, end of setup())
void mr_ui_tick(uint32_t now_ms);                 // main loop: periodic refresh — THROTTLE inside (called every service pass)
void mr_ui_on_push(const meshroute::Push& pu);    // event: an app Push worth surfacing (RX DM / channel / ACK / send-failed)
void mr_ui_on_config_saved();                     // event: a SUCCESSFUL, PERSISTED /mrcfg write by serial/BLE (§3.6.1)
#else
// No display on this profile -> every hook inlines to nothing (the call sites stay unconditional).
inline void mr_ui_init() {}
inline void mr_ui_tick(uint32_t /*now_ms*/) {}
inline void mr_ui_on_push(const meshroute::Push& /*pu*/) {}
inline void mr_ui_on_config_saved() {}
#endif
