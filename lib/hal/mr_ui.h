// MeshRoute — lib/hal/mr_ui.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §featuresplit slice 4: the board-UI seam (MR_FEAT_OLED). A board with a display (e.g. the heltec_v3's on-board
// SSD1306) implements these SEVEN hooks (⛔ corrected in place THREE times — 2026-08-13 this line said "three" while
// the block at the fourth one already called it "THE FOURTH HOOK", 2026-08-14 §B197/§B198 added the fifth, and
// 2026-08-15 §B200 added the arm/disarm PAIR; a count in prose beside a list is exactly the thing that drifts, so it
// is checked against the list when either changes)
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

// ★★★ §B200 — THE ANSWER THE SLEEP PATH NEEDS, AND IT IS THREE-VALUED BECAUSE TWO OF THE OUTCOMES ARE NOT FAULTS.
// Declared OUTSIDE the `#if` because BOTH arms return it: the OLED arm from the real board, the non-OLED arm as a
// compile-time constant. `fw_main` must be able to tell the three apart WITHOUT knowing what a button is:
//   ok          — the sleep may proceed. On an OLED board the level-triggered wake is now ARMED and the caller owes
//                 it a `mr_ui_disarm_button_wake()` before it runs any further; on every other profile there was
//                 nothing to arm, which is the same permission.
//   button_down — ⛔ the button is PHYSICALLY HELD RIGHT NOW. Nothing was armed and nothing is owed. This is a
//                 NORMAL race, not a fault: arming a LOW-level wake on an already-low pin is [[B200]] itself (the
//                 interrupt cannot be cleared while the level holds ⇒ ISR storm ⇒ Interrupt WDT). Skip this sleep.
//   failed      — the platform REFUSED to arm (or to disarm). The UI latches sleep off for the whole boot; the
//                 caller's only job is to count it and not sleep.
enum class MrUiWakeArm : uint8_t { ok = 0, button_down = 1, failed = 2 };

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
// ★★★ THE FIFTH HOOK (§B197/§B198). Same shape and the same reason as the four above: `src/fw_main.cpp`'s idle
// light-sleep gate must be able to ask *"is the UI in the middle of something?"* WITHOUT knowing that a panel, a
// button pin or a page loop exists. ⛔ NO `MR_FEAT_OLED` MAY REACH THE SLEEP GATE — the call stays unconditional and
// this header supplies the always-true stub, so a non-OLED profile's sleep behaviour is BYTE-IDENTICAL to before.
// ⚠ THE SENSE IS "MAY I SLEEP?", never "am I busy?": `true` = the UI has no objection. The non-OLED answer is
//   therefore `true`, and an OLED board that could not ARM ITS WAKE SOURCE answers `false` FOR THE WHOLE BOOT —
//   ★★ FAIL CLOSED. A node that sleeps with the user button unarmed is [[B197]] made permanent and invisible: the
//   only inputs left are a DIO1 RxDone and the ≤1 s deadline timer, and neither is reachable by the operator.
// ⓘ Called every service pass, so it must stay cheap: the OLED side is three boolean reads of state it already owns.
// ★★★ THE SIXTH AND SEVENTH HOOKS (§B200), AND THEY ARE A PAIR — never one without the other. §B197 armed the
// button's light-sleep wake ONCE AT BOOT and never disarmed it; the trigger is LEVEL-triggered (the platform admits
// no other kind for light sleep), a level interrupt cannot be cleared while the level holds, and RadioLib's DIO1
// attach means the shared GPIO ISR is live ⇒ ⛔⛔ HOLDING THE BUTTON STORMED THE ISR AND TRIPPED THE INTERRUPT
// WATCHDOG (`Core 1 panic'ed (Interrupt wdt timeout on CPU1)`, captured on metal 2026-08-15).
// ⇒ THE ARM NOW LIVES AT THE SLEEP, NOT AT THE BOOT: `board_sleep_until()` arms IMMEDIATELY before
//   `esp_light_sleep_start()` and disarms IMMEDIATELY after it returns, so the level source exists only while the CPU
//   is halted and can never storm a running core. ⛔ No arm may survive a sleep, and ⛔ no `mr_ui_init()` may arm.
// ⚠ THE CALLER'S CONTRACT, and both halves are load-bearing:
//   (1) `ok` ⇒ you MUST call `mr_ui_disarm_button_wake()` on EVERY path out, before the CPU does anything else;
//   (2) anything else ⇒ NOTHING was armed, nothing is owed, and you MUST NOT SLEEP this pass.
// ⚠ `mr_ui_disarm_button_wake()` returns false only for a HARDWARE failure; the UI has already latched sleep off for
//   the boot by then, so the caller's remaining duty is to COUNT it (a disarm that fails is the storm made durable).
// ⓘ WHAT IS DONE AND WHAT IS NOT, stated here because docs rot and code is read: the ONLY caller today is the
//   ESP32 light-sleep branch of `board_sleep_until()`. The nRF52 branch uses WFE, has no armable GPIO wake source
//   and no panel, so it calls neither — correct now, and ⛔ a FUTURE nRF52 panel port must wire the pair into that
//   branch itself; the stubs below would otherwise let it light-sleep with nothing armed, silently.
#if MR_FEAT_OLED
void mr_ui_init();                                // boot: bring the panel up (called once, end of setup())
void mr_ui_tick(uint32_t now_ms);                 // main loop: periodic refresh — THROTTLE inside (called every service pass)
void mr_ui_on_push(const meshroute::Push& pu);    // event: an app Push worth surfacing (RX DM / channel / ACK / send-failed)
void mr_ui_on_config_saved();                     // event: a SUCCESSFUL, PERSISTED /mrcfg write by serial/BLE (§3.6.1)
bool mr_ui_allows_sleep();                        // policy: may the CPU light-sleep now? (false = panel lit / gesture / open frame)
MrUiWakeArm mr_ui_arm_button_wake();              // §B200: arm the button wake FOR THIS SLEEP ONLY — call immediately before halting
bool mr_ui_disarm_button_wake();                  // §B200: ...and immediately after waking, on EVERY path (false = hardware failure)
#else
// No display on this profile -> every hook inlines to nothing (the call sites stay unconditional).
inline void mr_ui_init() {}
inline void mr_ui_tick(uint32_t /*now_ms*/) {}
inline void mr_ui_on_push(const meshroute::Push& /*pu*/) {}
inline void mr_ui_on_config_saved() {}
inline bool mr_ui_allows_sleep() { return true; }
// ⓘ No panel ⇒ no button ⇒ nothing to arm, so the answer is the PERMISSION (`ok`) and the disarm is a no-op that
//   cannot fail. Both fold to constants at the call site, which is what keeps a non-OLED profile's sleep path
//   behaviourally identical to before this slice.
inline MrUiWakeArm mr_ui_arm_button_wake() { return MrUiWakeArm::ok; }
inline bool mr_ui_disarm_button_wake() { return true; }
#endif
