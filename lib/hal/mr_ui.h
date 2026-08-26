// MeshRoute — lib/hal/mr_ui.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §featuresplit slice 4: the board-UI seam (MR_FEAT_OLED). A board with a display (e.g. the heltec_v3's on-board
// SSD1306) implements these EIGHT hooks (⛔ corrected in place FOUR times — 2026-08-13 this line said "three" while
// the block at the fourth one already called it "THE FOURTH HOOK", 2026-08-14 §B197/§B198 added the fifth,
// 2026-08-15 §B200 added the arm/disarm PAIR, and 2026-08-25 [[B243]] added the eighth; a count in prose beside a
// list is exactly the thing that drifts, so it is checked against the list when either changes)
// in a TU compiled under `#if MR_FEAT_OLED` (variants/heltec_common/board_ui.cpp, specialized by required board
// traits). EVERY other
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
// ★★★★ THE EIGHTH HOOK ([[B243]], §UI-16 K3+K4 correction 2026-08-25) — **THE FAILED SAVE'S ONLY DOOR**, and it
// exists because the seven above structurally could NOT carry it. §UI-16's F-10 rules that a `team_key_received`
// push whose durable save FAILED must ⛔ NOT be forwarded through `mr_ui_on_push` — that refusal is precisely what
// makes `TEAM KEY RECEIVED` true by construction rather than by a gate a reviewer must trust. ⇒ the HONEST half of
// the verdict (the key IS live in RAM, and it will NOT survive a reboot) had no door at all, and on device a failed
// save was SILENT on the panel: [[B243]], measured and registered — ⛔ never a false success, but never the truth
// either. This is that door, and it carries nothing else.
// ⚠ THE CONTRACT IS AS NARROW AS THE FOURTH HOOK's AND FOR THE SAME REASON: call it ONLY when a `team_key_received`
//   push was WITHHELD because the persistence forward answered "not saved". ⛔ Not on a success — the two notes
//   would race for one result slot and the panel would show the last writer; ⛔ not on any other `PushKind`; and
//   ⛔ never INSTEAD of `mr_ui_on_push` for a push that WAS forwarded. There is exactly ONE call site and it is the
//   `else` of the drain loop's forward gate in `src/fw_main.cpp` — where `fw_main` gains a CALL, ⛔ not a decision.
// ⛔⛔ EVERY §UI-16 K4 NEGATIVE BINDS THIS DOOR EXACTLY AS IT BINDS THE PUSH ARM, and structurally rather than by
//    restatement: the OLED side funnels into the SAME `UiModel::on_team_key_note` (U1 — one entry point, one set of
//    refusals), so it ⛔ navigates nothing, ⛔ opens nothing, ⛔ moves no cursor, ⛔ writes no emergency field and
//    ⛔ does NOT WAKE a dark panel. (§UI-17 R-7 scoped the wake to a DM addressed to us and a SEALED channel post;
//    a grant receipt is neither, and widening it is a new owner ruling — so the omission is a decision.)
// ⛔⛔ **CORRECTED IN PLACE 2026-08-25 (§UI-16 K6's QG blocker), AND THE WITHDRAWN CLAIM IS KEPT VISIBLE BECAUSE IT
//     WAS NORMATIVE:** this read *"ⓘ IT TAKES NO ARGUMENT, and that is the note's own rule arriving at the seam: the
//     note carries no team id (left at 0 deliberately — an id off the air is not an operator's selection) and the
//     granter's optional `name=` is never read at all (F-3/P-5, spec §8 S-36's forbidden usage). There is nothing
//     true this hook could pass that the panel would be allowed to DRAW, so passing anything would only invite a
//     future renderer to draw it."*
// ★★★ THE HALF THAT HELD IS THE ONE THAT MATTERED, AND IT IS WHY THE PARAMETER IS SAFE: there is still nothing this
//     hook may pass that the panel may **DRAW** — ⛔ no team id, ⛔ no name, ⛔ no key byte, and ⛔ not one of the
//     three ruled rows changes. What the parameter carries is a **LANDING**: *was this receipt's durable refusal the
//     FULL keyring?* Spec §K6 (`:987`) rules that a `KEYRING FULL` result — of **either origin**, a `team new` or a
//     received grant — acknowledges into the `SAVED KEYS` list, where the dead end can be resolved. Without it the
//     fifth RECEIVED grant showed three true rows and then walked the operator to a menu that says nothing.
// ★ ONE DOOR, ONE FACT — ⛔ deliberately NOT a ninth hook: a second door for the SAME event is exactly the race this
//   block warns about two paragraphs up (*"the two notes would race for one result slot"*). The fact is a TYPED
//   derivation of `mrfw::KeyringErr::keyring_full` made by a pure unit the native suite drives; this seam only
//   carries it, and `fw_main` only forwards it (U3 — it gains a CALL, ⛔ never a decision).
// ⛔ AND IT AUTHORISES NOTHING ELSE: the landing deletes no record and ⛔ retries no grant — the removal remains the
//    operator's own separate, confirmed transaction, as everywhere in K6.
#if MR_FEAT_OLED
void mr_ui_init();                                // boot: bring the panel up (called once, end of setup())
void mr_ui_tick(uint32_t now_ms);                 // main loop: periodic refresh — THROTTLE inside (called every service pass)
void mr_ui_on_push(const meshroute::Push& pu);    // event: an app Push worth surfacing (RX DM / channel / ACK / send-failed)
void mr_ui_on_config_saved();                     // event: a SUCCESSFUL, PERSISTED /mrcfg write by serial/BLE (§3.6.1)
void mr_ui_on_team_key_unsaved(bool keyring_full);// [[B243]]: a grant receipt whose durable save FAILED (RAM-only)
                                                  //           §UI-16 K6: `keyring_full` = the refusal was P-15's FULL store ⇒ the ack lands in SAVED KEYS
bool mr_ui_allows_sleep();                        // policy: may the CPU light-sleep now? (false = panel lit / gesture / open frame)
MrUiWakeArm mr_ui_arm_button_wake();              // §B200: arm the button wake FOR THIS SLEEP ONLY — call immediately before halting
bool mr_ui_disarm_button_wake();                  // §B200: ...and immediately after waking, on EVERY path (false = hardware failure)
#else
// No display on this profile -> every hook inlines to nothing (the call sites stay unconditional).
inline void mr_ui_init() {}
inline void mr_ui_tick(uint32_t /*now_ms*/) {}
inline void mr_ui_on_push(const meshroute::Push& /*pu*/) {}
inline void mr_ui_on_config_saved() {}
inline void mr_ui_on_team_key_unsaved(bool /*keyring_full*/) {}
inline bool mr_ui_allows_sleep() { return true; }
// ⓘ No panel ⇒ no button ⇒ nothing to arm, so the answer is the PERMISSION (`ok`) and the disarm is a no-op that
//   cannot fail. Both fold to constants at the call site, which is what keeps a non-OLED profile's sleep path
//   behaviourally identical to before this slice.
inline MrUiWakeArm mr_ui_arm_button_wake() { return MrUiWakeArm::ok; }
inline bool mr_ui_disarm_button_wake() { return true; }
#endif
