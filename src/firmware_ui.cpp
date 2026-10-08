// MeshRoute — src/firmware_ui.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// The board-UI FEATURE layer (plan Task 6 = spec slice UI-6). It owns the model, the two send trackers, ALL render
// policy, the battery cache and the correlation of node-wide pushes into UI outcomes — and it is where the three
// `mr_ui_*` hooks LIVE, which is what let UI-5's TEMPORARY board-canvas copies be deleted.
// Adds no new core API: every read is an accessor that already existed (spec §6).
//
// ★ THE TWO BOUNDARIES THIS FILE SITS BETWEEN, both of them load-bearing rather than tidy:
//   above it  `variants/heltec_common/board_ui.h` — a display-INDEPENDENT canvas. Nothing here names U8g2, I2C or a pin;
//             nothing there knows what a "screen" is. That is what makes the V4 port a pin table, not a rewrite.
//   below it  `firmware_ui_model.h` / `firmware_ui_send.h` — pure, board-free, natively tested. Every gesture meaning,
//             every state transition and the whole two-tracker glue live THERE so the native suite can drive them;
//             this file holds only what genuinely needs `g_node` / `g_hal` / the panel.
//
// ★ WHAT IS DONE AND WHAT IS NOT — in source, because docs rot and code is read
//   ([[meshroute-mark-done-vs-missing-in-code]]):
//   DONE      the snapshot builder, the DRAWING of STATUS / TEAM / INBOX / SEND / both compose lists / both compose
//             RESULT views / the emergency overlay, the battery cache, and §B91's dead-panel report line.
//   ★ MOVED OUT 2026-08-05 (the UI-6 QA fix slice) — and this is the point, not a tidy-up. WHEN to paint (`FrameGate`),
//             what an arriving push MEANS (`ui_route_recv_push`) and the unread counters all lived here, in a TU that
//             NEITHER the native suite NOR the simulator compiles, and all four of §B101/§B102/§B107/§B108 shipped
//             green because of it. They are now pure code in firmware_ui_model.h / firmware_ui_send.h, driven by the
//             native suite, and `tools/probe_board_ui/run.sh`'s W1-W4 pin that this file still CALLS them.
//   ★ DONE 2026-08-05 (UI-7) — THE SEND ITSELF, and UI-6's LOUD REFUSAL STUB is gone. The device half here is an
//             EXECUTOR (`mrfw::exec_command`, the one approved new firmware surface) plus the §4.1 fix predicate;
//             every decision a wrong answer could hurt — the composed line, the `CmdCode` mapping, the `ctr == 0`
//             reading — is `mrui::ui_perform_send` in firmware_ui_send.h, under the native gate.
//   ★ DONE 2026-08-05 (UI-7) — inbox ROWS, over `Inbox::pull()` directly (spec §6.1), with the per-kind newest-wins
//             budget in `mrui::InboxRowBudget` so a chatty channel cannot evict every DM row.
//   ★ DONE 2026-08-13 (§UI-7D slice B) — the inbox DETAIL/DELETE modal's DEVICE half: the exact `(kind, seq)` lookup
//             over `pull()`, the body copy performed INSIDE that callback (the pointer dies with it), the one call to
//             `Inbox::erase` with its three outcomes passed through verbatim, and the modal's renderer. Every gesture
//             meaning, the identity tracking, the paging cadence and all three outcome landings are `firmware_ui_model.h`,
//             under the native gate. ⚠ [[B134]]: the ESP32 inbox is a RAM ring — "durable" here is within this runtime.
//   ★ DONE 2026-08-05 (the UI-7 QA fix slice, §B64) — the TEAM screen's half of the owner's identity ruling: while
//             `UiState::team_pick_gone` stands, one body row is RESERVED for `TEAMMATE GONE, repick` and the `>` marker
//             is SUPPRESSED. The suppression is the safety half — a highlight beside a target the model has already
//             refused to use is the mis-send in display form. Pinned by the probe's W9 + its negative control, because
//             no native test compiles this file.
//   ★ DONE 2026-08-13 (§UI-14) — the SETTINGS screen's DEVICE half: the ONE `mrfw::ConfigService` instance over the
//             [[B193]] device bindings, the per-frame FREEZE of its three facts + the draft (`SettingsView`), the
//             menu/editor renderer, spec §3.3's draft marker and `RESTART NEEDED` row on STATUS, and the build-time
//             `MR_UI_BLE_ROW` condition published into the snapshot. Every gesture meaning, the row table and all
//             three action landings are `firmware_ui_model.h`, under the native gate.
//   ★★ DONE 2026-08-16 (§CHROME-3) — THE STATUS STRIP and design §8.3's REPAINT INVALIDATION. The packed
//             `DM… CH… T…/… …V` bar is GONE; the top row is now `[mail][count] [home][age] [people][count] [key]
//             [battery][volts]` at fixed slots from ONE layout table here, drawn from a FIFTH frozen copy
//             (`s_frame_chrome`) and from nothing else. `build_snapshot` publishes the five §CHROME-1 fields, and
//             `fmt_volts` was DELETED in favour of `mrui::ui_fmt_batt` (same bytes, plus the width guard it lacked).
//   ★★ DONE 2026-08-23 (§CHROME-5) — THE STRIP'S SIXTH SLOT: a 7x7 DUTY-UTILIZATION GAUGE at `x = 83..89`,
//             ⛔ icon only and never a percentage. The line above is AMENDED, not withdrawn: the strip is now
//             `[mail][count] [home][age] [people][count] [key] [duty] [battery][volts]`, home/people/key each moved
//             LEFT to one-pixel gaps (27/54/75) in the SAME one layout table, and ⛔ the battery is untouched.
//             `build_snapshot` publishes `Node::duty_status()`'s two fields (a SIXTH and SEVENTH §CHROME-1-shaped
//             fact); `mrui::ui_duty_bucket` classifies them into the frozen `UiChrome` BEFORE the freeze, so the
//             renderer switches on a bucket and a repaint is owed only when the PICTURE changes.
//   ★★ DONE 2026-08-16 (§CHROME-4) — THE NAVIGATION RAIL, THE CONFIG BADGE AND THE 19-COLUMN BODY. `draw_rail` draws
//             §3.2's five 10-px slots at `x = 0..9` from the FROZEN rail fields (`nav` / `slots` / `rail_visible`),
//             boxes the active one with `draw_rect` (its first and only caller in the tree), and carries §6's
//             configuration badge on the SETTINGS glyph. Every ordinary body line moved to the one `kBodyX = 12`
//             authority through `body_text` and was re-derived for 19 columns (§7.3's audit is written out beside
//             each screen); the inbox detail's wrap moved with it, AT THE MODEL (`kDetailCols` 21 -> 19, so the page
//             count is re-derived 42 -> 38 chars and 6 -> 7 pages rather than re-clamped). The standalone `STATUS`
//             and `SETTINGS` titles are gone (§7.2) and with them the STATUS `CFG* UNSAVED` / `CFG! RELOAD`
//             DECORATION — ⛔ but NOT the instruction: SETTINGS still renders that text on a row of its own, because
//             §6 rules that the icon may replace the decoration and may never replace the remedy.
//   ⛔ NOT DONE HERE, stated so a reader does not look for it: the emergency body is the ONE body that stays at
//             `x = 0` and 128 px wide (§5.3), and no rail is drawn while an alarm is up.
//   ★★ DONE 2026-08-19 (§UI-15 slice 5) — §3.6.3's TEAM-CREATE, DEVICE HALF ONLY. This file publishes the two §6
//             CHILD PREDICATES into the snapshot (`prov_join_static` / `prov_create_team` — the ONE site that knows
//             them), renders the provisioning sub-view (`draw_provision_screen`: the child menu, the confirmation and
//             the result), and supplies the FOUR device forwards of `mrfw::ITeamCreateDevice` — the record load, the
//             live facts, the transaction call and the post-save bookkeeping (`mr_ui_on_config_saved` + the persist
//             tracker, which lives behind `firmware_config.cpp` because `fw_context.h` is barred from this TU).
//   ⛔ AND NOT ONE DECISION OF IT IS HERE, by requirement: the OWNER's live-vs-persisted PHY precondition,
//             `TeamRequest::phy.present = false` and the verdict mapping are all `src/firmware_ui_prov.h`'s, which the
//             native suite compiles; every string is `firmware_ui_model.h`'s or `firmware_ui_chrome.h`'s.
//   ★★ DONE 2026-08-20 (§UI-15 slice 6) — §3.6.3's STATIC JOIN, DEVICE HALF ONLY: the three forwards of
//             `mrfw::IJoinDevice` (the `/mrjoin` list through slice 2's service, the transaction through slice 1's,
//             and the §notify-every-save hook on the `started` arm alone), the four `join_*` renderer arms, and
//             `ui_join_note_push` — which supplies the TWO DEVICE FACTS the correlation needs (`/mrcfg.layer0_id`
//             and `canonical_node_id()`) behind the model's cheap session guard, and ⛔ decides nothing.
//   ⛔ AND NOT ONE DECISION OF THE JOIN HALF IS HERE EITHER: the four-term correlation rule, the 60 s word change,
//             the store-state texts and the value lines are `src/firmware_ui_join.h`'s; the verdict mapping and the
//             ONE integral -> double conversion are `src/firmware_ui_prov.h`'s. Both are natively compiled.
//   ★ DONE 2026-08-13 (§UI-14 follow-up) — the IMMEDIATE conflict notification §3.6.1 requires: `mr_ui_on_config_saved`
//             below, called after a SUCCESSFUL PERSISTED write through the feature-neutral fourth hook in
//             `lib/hal/mr_ui.h`. ⛔ **CORRECTED IN PLACE: this line read *"NOT DONE HERE, and NOT anywhere
//             yet: `note_external_write` is UNWIRED … a conflict is detected at SAVE rather than the instant it
//             happens"*, which was accurate when written and is now FALSE.**
//   ★ COMPLETED 2026-08-13 (§notify-every-save, [[B194]]) — ⛔ **AND THE QUALIFIER THIS LINE CARRIED IS ALSO
//             WITHDRAWN: it said the hook was `handle_cfg_set`'s only, and that the OTHER `/mrcfg` writers "do not
//             notify". Now SEVEN user-initiated verbs call it** — `cfg set` · `gateway` · `join` · `create` · `team` ·
//             `leave` · `password` — under the rule stated at `§notify-every-save` in `src/firmware_config.cpp`.
//             ⚠ Still true and deliberately unchanged: the INTERNAL writers (fw_main's ctr lease / leaf-config adopt,
//             firmware_remote's admin writes) stay silent; that file records the measurement behind the exemption.
//   ⚠ NOT DONE, stated so it is not read as shipped: a DM whose synchronous result is `queued` with `ctr == 0` has no
//             handle to correlate and no outcome kind of its own, so the sub-view shows `SENDING...` until the
//             operator closes it. Never a false claim, but it answers nothing — register B111.
//             ⓘ CORRECTED 2026-08-21 (§UI-17 S2, V1): this read "until its own kBlankMs auto-exit. Bounded and never
//               a false claim". §9 R-1 deleted that auto-exit — see `firmware_ui_model.h`'s `on_tick`.
//   MISSING   a real battery reading. `mrui::battery_sample_mv()` is Task 9's; until then it answers "unavailable" and
//             the status bar renders `--` (the console_json.h:126 rule), never a plausible wrong number.
#include "mr_features.h"

#if MR_FEAT_OLED

#include <cstdio>            // snprintf — every panel string is formatted here, never in the board TU
#include "firmware_ui_model.h"
#include "firmware_ui_chrome.h"  // ★★ §CHROME-3: the ONE frozen chrome projection, its compact formatters and the
                                 //   §8.3 invalidation rule. ⓘ THIS INCLUDE IS THE FIRST TIME EITHER CHROME HEADER
                                 //   MEETS A BOARD TOOLCHAIN — §CHROME-1 and §CHROME-2 both reported that nothing
                                 //   in the tree included them, so their `-Os` cross-compile behaviour and their
                                 //   flash cost were UNVERIFIED until this slice.
#include "firmware_ui_nearby_row.h"  // ★★ §UI-16 N2: the NEARBY row's three tokens — the SHARED team fingerprint,
                                 //   the `n/3` tier (`presence_quality_tier`'s answer, ⛔ never a second one) and the
                                 //   REUSED age table. ⛔ Nothing in this TU re-spells any of the three. ⓘ It pulls
                                 //   `firmware_ui_nearby.h` in with it — the carriers, the own-team filter and the
                                 //   lexemes, which `firmware_ui_model.h` above already included.
#include "firmware_ui_invite.h"  // ★★ §UI-16 N4: the INVITE window's pure unit — the two snapshot authorities, the
                                 //   diff, the volatile handled set, the candidate row and every lexeme. ⓘ Named
                                 //   EXPLICITLY although `firmware_ui_model.h` above already pulls it in (the
                                 //   snapshot publishes its members and the state holds its window): this TU calls
                                 //   its formatters and its list builder directly, and an include that documents a
                                 //   dependency is what stops the next reader hunting for where they came from.
#include "firmware_ui_icons.h"   // ★ the strip's glyphs. `inline constexpr` at namespace scope ⇒ `.rodata`, and
                                 //   §8.1's amendment requires them to land in FLASH, not RAM.
#include "firmware_ui_send.h"
#include "firmware_ui_status.h"  // ★★ §UI-17 S3: the STATUS body's five rows, EVERY substitution and row 4's
                                 //   priority, as pure strings — §B115's rule, because nothing in this TU is
                                 //   compiled by the native suite or the simulator. `draw_status_screen` below is
                                 //   placement and one `draw_rect`, nothing else.
#include "firmware_ui_team.h"    // ★★ §UI-17 S4: the TEAM row's ruled format, its route-age token, the two reserved
                                 //   columns and the bounded clock-driven repaint — pure, for the same §B115 reason.
                                 // ★★ §UI-17 S5 pulls `firmware_ui_geo.h` in through it: the freshness bound, the
                                 //   geometry and the two tokens. ⛔ THIS FILE DECIDES NONE OF IT — it publishes the
                                 //   cache read (`build_snapshot`) and places the composed row, nothing else.
#include "firmware_ui_prov.h"    // ★★ §UI-15 slice 5: the PURE team-create adapter (`mrfw::UiProvisionAdapter` over
                                 //   `mrfw::ITeamCreateDevice`). EVERY decision of §3.6.3's create — the PHY
                                 //   precondition, `phy.present = false`, the verdict mapping — lives THERE, where
                                 //   the native suite compiles it; this file supplies only the four device forwards.
#include "board_ui.h"        // resolved by `-I variants/heltec_common` — ★ THIS is the task that makes that flag
                             //   load-bearing; §A0 predicted Task 5 and UI-5 measured it dead there three ways.
#include "mr_ui.h"           // the hook DECLARATIONS we define below (fw_main calls them unconditionally). ⛔ V1: this
                             //   comment said "three" and the header now declares EIGHT — a count in prose beside a
                             //   list drifts, so it names no number at all.
#include "fw_context_pure.h" // ★ §B105: g_node / g_hal through PURE headers. It was `fw_context.h`, whose only extra
                             //   offering here was the concrete `g_iradio` — and that one include cost §B106's +2
                             //   per-TU warnings AND made this file impossible to host-compile (§B104). The radio is
                             //   now reached as `g_hal.radio()`: the SAME instance, through the pure `IRadio&` seam.
                             //   ⛔ Do not put `fw_context.h` back — `tools/probe_firmware_ui/` stops building.
#include "console_sink.h"    // mrcon — the guarded sink; §B91's dead-panel line is the only thing this file prints
#include "firmware_commands.h"  // ★ UI-7: mrfw::exec_command — the typed send path (the one approved new surface)
#include "console_json.h"    // ★ UI-7: cmdcode_name — the ONE CmdCode->text mapper (U1; fw_main.cpp:905 says so)
#include "inbox.h"           // ★ UI-7: meshroute::InboxEntry / InboxKind for the §6.1 pull adapter
#include "firmware_config.h" // ★★ §UI-14 / [[B193]]: `mrfw::device_cfg_store()` / `device_cfg_live()` — the DEVICE
                             //   bindings of §UI-13's `ICfgStore` / `ICfgLive`. They live in `firmware_config.cpp`
                             //   and not here for a hard reason: `apply_live` must reproduce `handle_cfg_set`'s
                             //   OFF->ON `mobile_register_current()` bridge (so the two cannot drift), and the
                             //   EFFECTIVE `ble_mode` is `g_ble_mode`, which lives behind `fw_context.h` — the header
                             //   §B105 took OUT of this TU and whose return `tools/probe_firmware_ui/`'s C0 forbids.

#ifndef MR_UI_TEAM_CHANNEL_ID
// C2, fail loud: the channel the alarm and the canned posts go to is an OWNER-RULED BUILD CONSTANT with no cfg key, no
// NV field and no console verb. Defaulting it here would silently point a distress call at somebody else's channel.
#  error "MR_UI_TEAM_CHANNEL_ID is not defined — the board env must supply it (platformio.ini, [env:heltec_v3])"
#endif

// ★★★ §UI-14, spec §3.6.2 — THE `BLE mode` ROW IS CONDITIONAL: *"row absent when UI-12 transport is not compiled"*.
// ⛔ AND IT IS `#error`-LESS, unlike `MR_UI_TEAM_CHANNEL_ID` above, ON PURPOSE: the absent row IS the spec's ruled
//    state for "no transport", so a 0 here is the RULED behaviour rather than an unagreed default (C2). Getting the
//    channel wrong points a distress call somewhere else; getting this wrong hides one recoverable setting.
// ⚠ MEASURED, NOT ASSUMED, 2026-08-13: it is 0 in EVERY env in the tree. The transport's own compile predicate is
//   `MRBLE_NRF52` (src/device_ble.h — nRF52 only), and the three envs that compile this file (`heltec_v3`,
//   `gateway_heltec`, `heltec_mobile`) are all ESP32-S3, where `mrble::*` is a set of inert inline stubs.
// ⛔ WHY THIS IS NOT `#include "device_ble.h"` AND A `#if defined(MRBLE_NRF52)`: that header DEFINES its transport
//    functions inline in the file (it says so — *"included by the one device TU (fw_main)"*), so a second includer is
//    a duplicate-symbol link failure on any nRF52 build. ⇒ the condition is a build flag, set beside
//    `MR_UI_TEAM_CHANNEL_ID` by whichever env compiles a transport, and §UI-12 owns turning it on.
// ★ The row's presence is NOT `#if`-gated in the model: it rides `UiSnapshot::ble_row`, so the native suite drives
//   BOTH arms (`settings_rows(true, …)` / `(false, …)`) and `tools/probe_firmware_ui/run.sh` builds a second variant
//   with `-DMR_UI_BLE_ROW=1` to measure the present arm end to end.
#ifndef MR_UI_BLE_ROW
#  define MR_UI_BLE_ROW 0
#endif

namespace {

// ---- state ------------------------------------------------------------------------------------------------------
mrui::UiModel     s_model;
mrui::InputFsm    s_input;
// ★★★ §B200 — THE FAIL-CLOSED LATCH. ⛔⛔ IT REPLACES §B197's `s_btn_wake_armed`, AND THE OLD COMMENT IS KEPT HERE
//   BECAUSE THE REASONING IN IT WAS RIGHT WHILE ITS MECHANISM WAS FATAL. It read: *"THE FAIL-CLOSED LATCH, AND ITS
//   INITIAL VALUE IS THE POINT. `false` means 'this node may not light-sleep', and it stays false unless
//   `mrui::enable_button_wake()` reports that BOTH ESP-IDF calls succeeded"* — i.e. the wake was armed ONCE AT BOOT
//   and the latch remembered the verdict. That permanent arm is [[B200]] (a level trigger no sleep consumes storms
//   the shared GPIO ISR while the button is held).
// ★★ THE NEW SHAPE IS STRICTLY STRONGER, WHICH IS WHY THE DEFAULT COULD SAFELY INVERT: the arm now happens INSIDE
//   the sleep path, immediately before the halt, and a failed arm REFUSES THAT SLEEP. ⇒ "sleeping with the button
//   unarmed" is no longer a state a latch has to exclude — it is unreachable by construction. What the latch still
//   owns is the DURABLE half: a board whose hardware refused to arm OR to disarm must stop trying for the rest of
//   the boot rather than re-attempting a failing arm on every idle pass.
// ⛔ `true` here means sleep is disabled for the WHOLE BOOT. It is only ever SET, never cleared (a boot is the
//   scope), and `mr_ui_allows_sleep()` short-circuits on it before any UI state is consulted.
bool              s_sleep_locked_out = false;
// ★ TWO trackers: an alarm must never queue behind a DM waiting on its e2e ack (spec §2.1). Normal work never touches
//   the emergency slot, in either direction.
mrui::SendTracker s_tracker_emg, s_tracker_normal;

// Unread counts + "newest received" stamps are UI-LOCAL and session-scoped (spec §6): `Inbox` exposes no read cursor,
// and counting here needs no new core API. ★ The six loose statics they used to be MOVED into `mrui::UiInboxCounters`
// so the two things that move them are natively driven — see firmware_ui_model.h and §B103/§B108.
mrui::UiInboxCounters s_counters;

// ★★★ §UI-14 / [[B193]] — THE ONE STAGED-CONFIG SERVICE INSTANCE, over the DEVICE bindings. It is constructed HERE,
//     in the OLED feature layer, because the DRAFT is the OLED's (§3.6.1 calls it "the OLED's ConfigDraft") — while
//     the two SEAMS it runs on are the device's and live in `firmware_config.cpp` beside `nv_load_stamped` and
//     `handle_cfg_set`.
// ⚠ IT IS NEVER CLOSED, and that is the contract rather than an omission: `open()` happens the first time the operator
//   reaches SETTINGS and `already_open` makes every later arrival a no-op, so the draft survives BACK, a blank and a
//   screen cycle (§3.6.1 forbids discarding on a timeout), and `reboot_required()` stays true from the save until the
//   reboot (§3.6.5: that state stays visible until then).
mrfw::ConfigService s_cfg(mrfw::device_cfg_store(), mrfw::device_cfg_live());

// ★★★★ §UI-15 slice 5 — THE DEVICE HALF OF §3.6.3's TEAM-CREATE, AND EVERY LINE OF IT IS A FORWARD. The decisions are
//      `src/firmware_ui_prov.h`'s, which is pure and natively compiled; what is left here is exactly what needs the
//      device: the durable store, `g_node`, the ONE transaction instance and the two post-save bookkeeping calls.
// ⓘ GUARDED BY THE CHILD PREDICATE ITSELF (`MR_N_LAYERS < 2`), because the three primitives it forwards to are
//   compiled out with `handle_team` on a gateway build. That is the SAME predicate `build_snapshot` publishes as
//   `prov_create_team`, so on a build where this block does not exist the CREATE row does not exist either — and the
//   model's seam stays null, which fails closed (see `run_create_team`).
#if MR_N_LAYERS < 2
struct DeviceTeamCreate : mrfw::ITeamCreateDevice {
    // The SAME durable seam the transaction writes through (U1) — ⛔ never a second `/mrcfg` reader.
    bool load_record(mrnv::Blob& out) override { return mrfw::device_cfg_store().load(out); }
    void device_facts(mrfw::ProvSnapshot& snap, mrfw::ProvPhyFloor& floor) override {
        mrfw::prov_device_facts(snap, floor);
    }
    mrfw::ProvResult apply(mrfw::TeamRequest& rq, const mrfw::ProvSnapshot& snap) override {
        return mrfw::prov_service().apply_team(rq, g_node.config(), snap);
    }
    // ⛔ THE ARM IS NOT DECIDED HERE — the adapter calls this ONLY on `ProvVerdict::applied` (i.e. the write both
    //    happened and succeeded), which is the §notify-every-save rule's exact condition and is a decision the native
    //    suite drives. This is the BODY of that decision and nothing else.
    void on_applied(const mrfw::ProvResult& r) override {
        mr_ui_on_config_saved();                                        // the OLED's own /mrcfg writer, §notify-every-save
        mrfw::prov_note_persisted_team_local_id(r.persisted_team_local_id);
    }
    // ★★ §UI-16 K5 — TWO MORE FORWARDS, AND ⛔ NEITHER DECIDES ANYTHING. The presence QUESTION and the explicit
    //    ACTIVATION both live in `mrfw::TeamKeyringService` (pure, natively driven, battery-attacked); the two
    //    functions below are `src/firmware_config.cpp`'s thin bindings over the ONE keyring instance the
    //    transaction already writes through. ⛔ WHEN either runs is the ADAPTER's decision, not this file's — the
    //    question on the `applied` arm of a join, the act on a `double` over `USE SAVED KEY`.
    bool has_saved_key(uint32_t team_id) override { return mrfw::has_saved_team_key(team_id); }
    mrfw::SavedKeyUse use_saved_key(uint32_t team_id) override { return mrfw::team_keyring_use_saved(team_id); }
    // ★★ §UI-16 K6 — TWO MORE FORWARDS, AND ⛔ NEITHER DECIDES ANYTHING. The METADATA-ONLY enumeration and the
    //    CONFIRMED removal both live in `mrfw::TeamKeyringService` (pure, natively driven, battery-attacked); the
    //    two functions below are `src/firmware_config.cpp`'s thin bindings over the ONE keyring instance and the
    //    SAME `/mrcfg` binding adapter K3 and K5 compose. ⛔ WHEN either runs is the MODEL's decision, not this
    //    file's — the list on the `menu -> SAVED KEYS` transition (and on the verdict's acknowledgement), the
    //    removal on a `double` over `FORGET KEY` on a confirmation that opened on `BACK`.
    mrfw::SavedKeyList  saved_key_list() override { return mrfw::team_keyring_list(); }
    mrfw::KeyringForget forget_key(uint32_t team_id) override { return mrfw::team_keyring_forget(team_id); }
};
// ★★★★ §UI-15 slice 6 — THE DEVICE HALF OF §3.6.3's STATIC JOIN, AND EVERY LINE OF IT IS A FORWARD, exactly as the
//      team half above is. The decisions — which store state says what, the ONE integral -> double conversion, the
//      verdict mapping and the four-term correlation rule — are `src/firmware_ui_prov.h`'s and
//      `src/firmware_ui_join.h`'s, both pure and both natively compiled.
// ⛔ THE TWO SERVICES ARE THE CONSOLE's OWN INSTANCES (`firmware_config.h` declares them): the OLED runs the SAME
//    transaction `join` runs and reads through the SAME store service `joinprofile` uses. Two services over one
//    record would be two write policies.
struct DeviceJoinProvision : mrfw::IJoinDevice {
    mrfw::ProfileResult list_profiles(mrnv::JoinBlob& out) override {
        return mrfw::join_profile_service().list(out);
    }
    mrfw::JoinResult apply(const mrfw::JoinRequest& rq) override {
        return mrfw::join_service().apply_join(rq);
    }
    // ⛔ THE ARM IS NOT DECIDED HERE — the adapter calls this ONLY on `JoinVerdict::started` (i.e. the ONE durable
    //    write happened and succeeded), which is §notify-every-save's exact condition and is a decision the native
    //    suite drives. This is the BODY of that decision and nothing else. ⓘ Site 8 of the rule; `handle_join` is
    //    site 3, and its own note records why the resulting ordering is unobservable.
    void on_started(const mrfw::JoinResult& r) override { (void)r; mr_ui_on_config_saved(); }
};
// ★★★ §UI-16 N5 / [[B249]] — FOUR DEVICE FORWARDS AND NO DECISIONS. `request_team_announcement` reaches the
//      existing core scheduler exactly once when the pure model asks; it does not inspect or reproduce any of the
//      scheduler's eligibility, jitter, coalescing or interval policy. `peer_key_at_least` calls the exact accessor used by
//      `Node::team_key_grant_send`'s no_pubkey arm and discards the public key bytes; the PURE caller supplies that
//      arm's authoritative floor. `issue` hands the already-complete typed Command through the PURE formatter to
//      the existing firmware command executor — the same sink the OLED send path uses — so this TU makes no
//      kind/hash/plane decision and no second Node command path is opened.
struct DeviceInvite : mrui::IUiInviteDevice {
    void request_team_announcement() override { g_node.schedule_triggered_beacon(); }
    bool peer_key_at_least(uint32_t key_hash32, MESHROUTE_NS::Node::PeerKeyConf floor) const override {
        uint8_t ed[32];
        MESHROUTE_NS::Node::PeerKeyConf conf = MESHROUTE_NS::Node::PeerKeyConf::overheard;
        return g_node.peer_key_find(key_hash32, ed, &conf) &&
               static_cast<uint8_t>(conf) >= static_cast<uint8_t>(floor);
    }
    // ⛔ IT REPORTS, IT DOES NOT JUDGE (QG blocker, 2026-08-24): the executor's `ok`, its `CmdCode` and its
    //    `accepted` bit are handed back VERBATIM, and the PURE unit decides whether that is a started request. A
    //    forward that mapped them here — even to a friendlier default — would be the decision this TU may not make.
    // ⓘ An unformattable command executed NOTHING, so the default answer (`ok == false`) is the honest one; it is
    //   the same shape a default-constructed `ExecResult` carries, `queued`-looking `code` included.
    mrui::UiInviteIssue issue(const MESHROUTE_NS::Command& command) override {
        mrui::UiInviteIssue out{};
        char line[mrui::kInviteReqpubkeyLineCap];
        const size_t n = mrui::ui_fmt_invite_reqpubkey_line(line, sizeof line, command);
        if (n == 0) return out;
        const mrfw::ExecResult r = mrfw::exec_command(line, n);
        out.ok       = r.ok;
        out.code     = r.result.code;
        out.accepted = r.result.accepted;
        return out;
    }
    // ★★★ §UI-16 N6 — THE THIRD FORWARD, AND IT DECIDES NOTHING EITHER. The plane is the PURE unit's constant,
    //     arriving as an argument; the outcome and the TWO correlation terms the core wrote (`out_ctr` + §UI-16
    //     N6b's `out_dst`) are handed back VERBATIM for `mrui::invite_grant_state_of` to word. ⛔ No mapping, no
    //     default, no collapse here — and ⛔ nothing here substitutes a value the screen already held.
    // ⓘ It goes through `mrfw::device_team_grant` rather than touching `g_node` directly — unlike
    //   `peer_key_at_least` above, which is a pure cache READ. The reason is stated at that function: the console's
    //   `team grantkey` originates the SAME verb, and both origination sites belong in ONE file.
    MESHROUTE_NS::Node::TeamKeyGrantTx grant(uint32_t key_hash32, MESHROUTE_NS::Plane plane,
                                             uint16_t* out_ctr, uint8_t* out_dst) override {
        return mrfw::device_team_grant(key_hash32, plane, out_ctr, out_dst);
    }
};
DeviceTeamCreate         s_team_create;
DeviceJoinProvision      s_join_prov;
mrfw::UiProvisionAdapter s_prov_adapter(s_team_create, s_join_prov);
DeviceInvite             s_invite_dev;
#endif

int32_t  s_batt_mv        = -1;      // last GOOD reading; <0 = never had one -> render `--`
uint32_t s_batt_next_ms   = 0;
bool     s_batt_attempted = false;

// ★ THE FRAME IS FROZEN AT begin_frame(). A frame spans several ticks and U8g2 re-clips the WHOLE scene once per page,
//   so anything the renderer reads must be a COPY — live state changing mid-frame tears the image across page
//   boundaries (spec §5). ⚠ The plan's Task-6 block froze `UiState` + `UiSnapshot` but then had the emergency overlay
//   read `s_model` LIVE, which reintroduces exactly that tear on the one screen where it matters most. Hence this view.
// ⓘ RENAMED `EmgView` -> `OutcomeView` by UI-7, and it is the feature's own doing rather than a drive-by tidy (C1):
//   the struct always carried `dm` "frozen here because the freeze point is this function", and UI-7 adds §B69's
//   `chan` plus the refusal's `CmdCode`. Leaving it called *Emg*View while it holds the DM and canned-channel compose
//   outcomes is exactly the comment drift V1 forbids.
struct OutcomeView {
    mrui::Emergency    st         = mrui::Emergency::idle;
    mrui::RefuseReason refuse     = mrui::RefuseReason::other;
    // ★ THE THREE ALPHABETS OF A FAILURE, all frozen together (spec §2.1 rule 6): `refuse` is the compact panel code,
    //   §B73's `fail` is the CORE `SendFailReason` verbatim for an ASYNC failure, and UI-7's `refuse_code` is the
    //   SYNCHRONOUS `CmdCode` verbatim — needed because five different walls all return `err_unsupported` and the
    //   compact reason therefore cannot name them (see UiModel::on_send_refused).
    mrui::DmState      dm         = mrui::DmState::idle;
    mrui::ChanState    chan       = mrui::ChanState::idle;
    mrui::FailReason   fail       = mrui::FailReason::none;
    MESHROUTE_NS::CmdCode refuse_code = MESHROUTE_NS::CmdCode::queued;
    // ★★ §B69: WHICH channel outcome this alarm actually got. `Emergency::not_heard` alone cannot say, and the two
    //    readings are different claims — see firmware_ui_model.h's EmgEvidence.
    mrui::EmgEvidence  evidence   = mrui::EmgEvidence::none;
    uint8_t            arm_secs   = 0;
    // ★★★ §B115 — TWO FIELDS, NOT ONE, AND THE SPLIT IS THE FIX. `tries` is the model's `_tries` verbatim: ACCEPTED
    //     transmissions, the value the airtime bound is evaluated on, and what `NOT HEARD` reports because there the
    //     number IS the measurement. `attempt_ordinal` is "which attempt is in flight", which is a DIFFERENT question
    //     — see firmware_ui_model.h's two-numbers block. The shipped bug was one field serving both: the FIRING arm
    //     rendered `tries + 1` unconditionally, so the panel read `2 of 3` -> `3 of 3` -> `4 of 3` against three posts
    //     and `1 of 3` was never shown. ⛔ Do not re-merge them, and do not clamp either.
    uint8_t            tries      = 0;
    uint8_t            attempt_ordinal = 0;
    uint32_t           retry_in_s = 0;
    char               who[mrui::kLabelCap + 1] = {};
    char               text[21]                 = {};
};
// ★★★ §UI-14 — THE SERVICE'S HALF OF THE FRAME, FROZEN LIKE EVERYTHING ELSE (spec §5, and the UI-7D contract the
//     brief restates: the renderer reads only frame-frozen state, never a live buffer). `UiState` carries what the
//     MODEL decided; this carries what the SERVICE says, and the two are kept apart deliberately — mirroring the
//     service's predicates into `UiState` would be the SECOND STATE MODEL §3.6.1 forbids.
// ★★ THREE FACTS, NOT ONE, AND THEY ARE THREE DIFFERENT COMPARISONS (firmware_config_service.h's own heading):
//     `unsaved` = draft vs baseline · `conflict` = persisted vs baseline · `reboot` = baseline vs EFFECTIVE over the
//     reboot-class fields. ⇒ a save that needs a reboot is `reboot && !unsaved`, which is a state the panel must be
//     able to show, and collapsing any two of these into one flag makes it unrepresentable.
// ⛔ `unsaved` IS `config_unsaved()`, NEVER `UiState::dirty` — `dirty` means "a repaint is owed" and is read three
//    lines away in this same file.
// ⓘ `reboot_required()` calls `ICfgLive::effective()`, which reads `NodeConfig` + `g_ble_mode`. Once per FRAME, at the
//   freeze — never per page, and never per tick.
struct SettingsView {
    bool open = false, unsaved = false, conflict = false, reboot = false;
    mrfw::CfgValues draft{};
};
mrui::UiState    s_frame_state{};
mrui::UiSnapshot s_frame_snap{};
OutcomeView      s_frame_out{};
SettingsView     s_frame_cfg{};
// ★★★ §CHROME-3 — THE FIFTH FROZEN COPY, and it is frozen at the same instant and for the same reason as the four
//     above (design §8.2): the strip is redrawn on every page of a frame, so a live chrome would tear the header
//     across a page boundary exactly as a live `UiState` would tear the body.
// ★★ IT IS ALSO THE §8.3 COMPARISON REFERENCE — *"the chrome frozen for the MOST RECENTLY OPENED FRAME"* — and the
//    two roles are the same object on purpose: the reference must move AT THE FREEZE and nowhere else, so a value
//    that changes while the page loop is open keeps the model dirty until one follow-up frame has rendered it.
//    ⛔ Never assign it at the point a difference is OBSERVED; that would consume the invalidation without drawing it.
mrui::UiChrome   s_frame_chrome{};
// ★ WHEN to paint (§B107). The frozen copies above are WHAT to paint; this owns the lifecycle that decides when they
//   are refreshed — including the `dirty` consumption, which belongs to the FREEZE and not to the final page.
mrui::FrameGate  s_gate;

// ---- §5 rule 1: paint only when the MAC is idle ------------------------------------------------------------------
// ⛔⛔ CORRECTED IN PLACE 2026-08-14 (§B197/§B198, V1). This block used to read: *"The SAME predicate fw_main.cpp:1406
//   uses to decide it may sleep (U1 — do not invent a second one)"*. BOTH HALVES WERE WRONG. It is an EQUIVALENT
//   EXPRESSION, not the same predicate — the sleep gate spells its radio/queue terms out inline as
//   `!g_iradio.tx_busy() && g_hal.txq_depth() == 0` — and the line reference had DRIFTED: the gate is the `if
//   (may_sleep && mr_ui_allows_sleep() && ...)` in `mesh_service_once()` — it was cited as `:1406` while it stood at
//   `:1426`, then re-cited as `:1430` and has drifted AGAIN (§B200 moved it). ★ THE LESSON IS THE NUMBER ITSELF:
//   grep for `if (may_sleep`; ⛔ do not restore a line reference here, it has now been wrong three times.
// ⓘ THE DUPLICATION IS REAL, PRE-EXISTING AND DELIBERATELY LEFT ALONE (C1: refactor XOR fix). The two are equivalent
//   TODAY because `g_hal.radio()` IS `g_iradio` (§B105, below), but they are two implementations of one rule.
//   Unifying them is its own change with its own risk; ⛔ do not fold it into a fix.
// A full 1024 B frame is ~25 ms of blocking I2C against a `cts_to_data_gap_ms` of 5, so this gate is a correctness
// constraint: it is what stops the panel from breaking an in-flight RTS/CTS/DATA exchange.
// ⓘ §B105: `g_hal.radio()` IS `g_iradio` — DeviceHal holds it by reference, bound at construction (fw_main.cpp:166),
//   so this reads the one radio instance and its ISR-driven volatile state exactly as the direct name did. Reaching it
//   through the accessor is what keeps `<RadioLib.h>` out of this TU; the predicate itself is untouched.
bool mac_idle() { return !g_hal.radio().tx_busy() && g_hal.txq_depth() == 0; }

// ---- battery cache (spec §7) -------------------------------------------------------------------------------------
// Sampled at boot and every 30 s, only when the MAC is idle. An earlier draft sampled eight ADC reads on EVERY service
// pass for a value that changes over minutes.
// ★ The cadence gates on ATTEMPTED, not on SUCCEEDED. Gating on `s_batt_mv >= 0` meant a board whose reader returns the
//   documented unavailable value was re-read on every idle pass, for ever.
constexpr uint32_t kBattPeriodMs = 30000;
void battery_maybe_sample(uint32_t now_ms) {
    if (!mac_idle()) return;
    if (s_batt_attempted && uint32_t(now_ms - s_batt_next_ms) >= (1u << 31)) return;   // wrap-safe "not due yet"
    const int32_t mv = mrui::battery_sample_mv();
    if (mv >= 0) s_batt_mv = mv;                 // keep the last GOOD value; an unavailable read never erases it
    s_batt_attempted = true;
    s_batt_next_ms   = now_ms + kBattPeriodMs;
}

// ---- labels (spec §6: team_key_of_id -> peer_name_find -> mrui::ui_fmt_identity -> bare id) ------------------------
// ★★★ [[B241]] — THE ONE C-STRING ADAPTER OVER `Node::peer_name_find`, AND ITS RESULT IS ALWAYS TERMINATED (cap >= 1).
//     The raw API copies `n` bytes, returns `n` and writes NO terminator, and this function used to hand that straight
//     to `%s`: a short name (`H1`) left the destination's stale bytes after it (garbage on glass, seen on metal) and a
//     name of 15+ bytes filled the buffer with no NUL at all. ⇒ the answer is now always the formatter's terminated
//     string, and `cap == 0` still writes nothing.
// ★★★★ W4a ([[B441]]) — THE WHOLE NAME IS READ, THEN FORMATTED AT THE SITE's BUDGET `cols`. The raw name goes into a
//      counted `peer_name_max` (32-byte) stack buffer — ⛔ never a pre-clipped prefix: a 14-byte copy cannot tell an
//      exact fit from a longer name, nor a real hash fallback from a name that merely begins `0x` — and
//      `mrui::ui_fmt_identity` renders it: sanitized cells, a generated `»` when it is wider than `cols`, and the
//      uppercase member tokens (`0x<HASH8>` / six-digit fingerprint) when the peer is unnamed.
// ⛔ THE RAW API STAYS UNTERMINATED ON PURPOSE: its full 32-byte count is PAYLOAD for its two other consumers,
//    `Node::push_peer_key_cached` (the push body) and `mrfw::peer_store_sync` (`/mrpeers` persistence), so reserving a
//    NUL inside it would drop byte 32 of a maximum-length name. `test/test_node_hashlocate.cpp` pins that capacity.
void label_from_hash(uint32_t hash, char* out, uint8_t cap, uint8_t cols) {
    if (cap == 0) return;                                       // nothing to write (W1)
    char raw[MESHROUTE_NS::protocol::peer_name_max];            // the FULL counted name — no terminator, no clip
    const uint8_t n = g_node.peer_name_find(hash, raw, uint8_t(sizeof raw));
    (void)mrui::ui_fmt_identity(out, cap, raw, n, hash, cols);
}
// ★★★ §UI-17 S5 — IT **RETURNS THE HASH IT RESOLVED** (0 = none), AND THAT IS THE WHOLE OF THE CHANGE (spec §3.4
//     term 2, U1): `build_snapshot` needs the same `team_key_of_id` answer to look the peer's cached POSITION up, and
//     a second call there would be a second resolution of one fact — the fork this project keeps paying for. ⇒ ONE
//     resolution per row, handed to both the label and `peer_loc_find`.
// ⓘ The two compose call sites ignore the value; they only ever wanted the label, and that behaviour is unchanged.
uint32_t label_for_team_id(uint8_t id, char* out, uint8_t cap, uint8_t cols) {
    uint32_t hash = 0;
    // ⓘ Inert on a !MR_FEAT_TEAM build: `team_key_of_id` stubs to false there, so the label falls straight through to
    //   the bare id. No #if needed — the stub IS the fallback.
    if (g_node.team_key_of_id(id, hash) && hash != 0) { label_from_hash(hash, out, cap, cols); return hash; }
    snprintf(out, cap, "id %u", unsigned(id));
    return 0;   // ⛔ C2: "no hash" is said out loud, never a plausible one — the caller blanks the location columns
}
void label_for_origin(const MESHROUTE_NS::Push& pu, char* out, uint8_t cap, uint8_t cols) {
    // §chan-crypt CL2c: a channel_recv carries the sender's stable key_hash32 here too, and `origin` on a team post is
    // only a DAD-assigned team_local_id — so prefer the hash when the post named one.
    if (pu.sender_hash != 0) { label_from_hash(pu.sender_hash, out, cap, cols); return; }
    snprintf(out, cap, "id %u", unsigned(pu.origin));
}

// ★★★ W4a — EACH DEVICE-LABEL SITE's COLUMN BUDGET (design §4.1 r2.20), NAMED AND DERIVED FROM EXISTING GEOMETRY —
//     ⛔ no site passes a bare number. The two compose budgets need the body width and sit beside `kBodyCols`.
//     ⓘ Every production budget is at least SIX cells (the member fingerprint), and every carrier has at least
//     `budget + 1` bytes, so `IdentityFmt::no_fit` is unreachable at every production site — asserted, not argued.
constexpr uint8_t kLabelMinCols   = uint8_t(mrui::kMemberFpCap - 1);     // the six-digit member fingerprint
constexpr uint8_t kTeamNameCols   = uint8_t(mrui::kTeamLabelCols);       // TEAM row's name field                  (6)
constexpr uint8_t kReplyWhoCols   = mrui::kLabelCap;                     // REPLY sender, the carriers' width     (14)
constexpr uint8_t kInviteNameCols = uint8_t(mrui::kInviteNameCap - 1);   // invite carrier = NEW MEMBER name row (14)
constexpr uint8_t kInviteCandCols = mrui::kInviteRowNameCols;            // candidate row's name column           (6)
static_assert(kTeamNameCols >= kLabelMinCols && kReplyWhoCols >= kLabelMinCols &&
              kInviteNameCols >= kLabelMinCols && kInviteCandCols >= kLabelMinCols,
              "W4a: every device-label budget holds at least the six-digit member fingerprint");
static_assert(sizeof(mrui::TeamRow::label) >= std::size_t(kTeamNameCols) + 1u &&
              sizeof(OutcomeView::who) >= std::size_t(kReplyWhoCols) + 1u &&
              sizeof(mrui::InviteMember::name) >= std::size_t(kInviteNameCols) + 1u,
              "W4a: every resident label carrier holds its site's budget plus the NUL (zero resident growth)");
static_assert(kInviteCandCols < kInviteNameCols,
              "W4a: the candidate row's name-only second pass must be STRICTLY narrower than the carrier's first");
static_assert(mrui::kInviteRowNameCols == mrui::kTeamLabelCols,
              "W4a: a member on both screens renders ONE six-cell projection of one name");

// ---- small formatters (ALL text formatting lives in this file, never in the board TU) ----------------------------
// ★★★★ §CHROME-4 — `fmt_age` IS NOW A ONE-LINE ADAPTER ONTO `mrui::ui_fmt_home_age`, WHICH IS THE SECOND FORMATTER
//      DUPLICATION THIS ARC HAS RETIRED (§CHROME-3 deleted `fmt_volts` the same way, and for the same reason: U1).
//      ⛔ The body's own version WAS NOT BOUNDED, and §7.3's audit is what found it: its hour bucket emitted
//         `%uh%02u` — five columns (`23h59`) — and its day bucket `%ud` over a `uint32_t` seconds value is SIX
//         (`49710d`). Against a 19-column body, `DM 999, newest 23h59` is 20 columns and `newest 49710d` is 21, so
//         the STATUS and INBOX lines would have been clipped by the panel — the truncation policy §7.1 rule 5 forbids.
//      ★ `ui_fmt_home_age` is design §4.2's ruled table (`--` / `Ns` / `Nm` / `Nh` / `Nd` / `old`), is bounded to
//        THREE columns by construction (`kAgeTokenCap`), and every one of its boundaries is pinned by a native case
//        with a mutation (`chrome-home:`, X02-X04). ⇒ this is a VERIFIED MOVE onto a stronger formatter, not a rewrite.
//      ⚠ WHAT IT COSTS, STATED RATHER THAN SMOOTHED OVER: the minutes-inside-an-hour (`23h59` -> `23h`) and an exact
//        day count above 99 days (`120d` -> `old`). Both are coarser, neither is wrong, and the alternative was a
//        clipped line — which is not coarser, it is arbitrary.
//      ⓘ `UINT32_MAX` remains "unknown" and still renders `--`: that is `ever = false` on the other side.
void fmt_age(char* out, size_t cap, uint32_t s) {
    mrui::ui_fmt_home_age(out, cap, /*ever=*/s != UINT32_MAX, uint64_t(s) * 1000u);
}
// The widest token `fmt_age` can now produce is 3 columns + NUL; every caller sizes its buffer from this.
constexpr size_t kAgeCap = mrui::kAgeTokenCap;
// ⛔⛔ `fmt_volts` LIVED HERE AND IS **DELETED** BY §CHROME-3 — a VERIFIED MOVE, not a rewrite. It read:
//        if (mv < 0) { snprintf(out, cap, "--"); return; }
//        snprintf(out, cap, "%u.%uV", unsigned(mv / 1000), unsigned((mv % 1000) / 100));
//    and its one caller was the old packed status bar. `mrui::ui_fmt_batt` (src/firmware_ui_chrome.h) is that same
//    formatter expressed over DECIVOLTS — `mv/1000` IS `dv/10` and `(mv%1000)/100` IS `dv%10` — and a native case
//    asserts the exact bytes of BOTH branches, which is what makes deleting this one a move rather than a rewrite.
// ★★ THEY HAD ALREADY DIVERGED, AND THE SURVIVOR IS THE STRICTER ONE: this version had NO WIDTH GUARD, so a reading
//    outside the panel's four-column battery slot rendered `10.0V` / `99.9V` and pushed every earlier icon in §3.1's
//    frozen strip out of budget (the §CHROME-1 R2.2 defect). `ui_fmt_batt` declares such a value UNAVAILABLE and
//    renders `--` — ⛔ never a plausible-looking clamp, which is the one substitution the battery path forbids.
// ⓘ The rule it carried is unchanged and now lives beside the survivor: volts, never a percentage — a percentage
//   needs a chemistry and a discharge curve nobody has approved (spec §3.3, design §4.5).
const char* refuse_text(mrui::RefuseReason r) {
    switch (r) {
        case mrui::RefuseReason::parser:      return "BAD CMD";
        case mrui::RefuseReason::unsealable:  return "NO CRYPTO";
        case mrui::RefuseReason::no_location: return "NO FIX";
        case mrui::RefuseReason::queue_full:  return "QUEUE FULL";
        case mrui::RefuseReason::other:       return "REFUSED";
    }
    return "REFUSED";   // -Wswitch covers the enum; this satisfies -Wreturn-type
}

// ---- the send path (UI-7) — the DEVICE half, and it is deliberately three lines long -----------------------------
// ★★ UI-6 shipped a LOUD REFUSAL STUB here (C2: render FAILED + "no send path: UI-7" rather than fake a success).
//    UI-7 replaces it, and almost none of the replacement is in this file: line composition, the §4.1 conditional
//    `-l`, the `CmdCode` -> panel-reason mapping and the whole `ctr == 0` reading are `mrui::ui_perform_send` in
//    firmware_ui_send.h, where the native suite drives them. What genuinely needs the device is the EXECUTOR and the
//    two facts below — so that is all that lives here.
// ⓘ A captureless lambda decays to `mrui::SendExecFn`; the `void* ctx` is unused because the executor's only
//    dependency, `g_node`, is a global. It is kept in the signature so a test can supply a recording fake (that is
//    the whole point of the seam) without this side needing a different shape.
mrui::SendExec ui_exec(const char* line, size_t len, void* /*ctx*/) {
    const mrfw::ExecResult r = mrfw::exec_command(line, len);
    // ★ ONE conversion, one place (U2). `ok` is "the line became a Command"; the rest is the typed result verbatim.
    return mrui::SendExec{ r.ok, r.result.code, r.result.ctr };
}

// ★★★ §4.1: `-l` IS CONDITIONAL, and this predicate is the whole reason it can be. `Node::on_command` REFUSES a
//    located post when both coordinates are zero (node.cpp:1553, `err_unsupported`) — BEFORE anything is enqueued —
//    so sending `-l` unconditionally would turn "no fix" into NO ALARM AT ALL. A distress call is worth more than the
//    coordinates attached to it.
// ⚠ The `(0,0)` test is the CORE's own predicate, reused rather than re-derived (U1): it is what the refusal is
//   keyed on, so any other definition of "have a fix" would disagree with the thing that actually rejects us.
// ⓘ §UI-17 S3: THE PREDICATE ITSELF MOVED INTO THE PURE UNIT and this is now a one-line forward. STATUS row 4 has
//   to answer the same question (`(0,0)` renders `NO LOCATION`, spec §2.2 note h) and a second spelling of it here
//   would be the S1/L9 fork this project keeps paying for (U1). ⛔ The MEANING is unchanged — same two fields, same
//   `||` — so the `-l` gate above behaves exactly as it did; what changed is that a native case can now drive it.
bool ui_have_fix() {
    const MESHROUTE_NS::NodeConfig& cfg = g_node.config();
    return mrui::ui_status_have_fix(cfg.lat_e7, cfg.lon_e7);
}

// ★★★★ §UI-10/11 P3 — THE CATALOG IS READ **LIVE, AT EXECUTION**, and that is design §3.3's own word: the request
//      may have waited in `_req_pending` for seconds behind a busy tracker or a firing alarm, so the frozen frame's
//      projection is not the question `send_gate_of` has to answer. ⛔ Do not "tidy" this into the snapshot's copy.
// ⓘ The SAME instance `build_snapshot` projects from and the `ui preset` verbs write — one catalog, three readers.
// ★★★ W6 (owner-ruled D15, brief §2.5) — THE ONE SEND LINE: 199 B (`mrui::kSendLineCap`, derived) of STATIC storage,
//     owned HERE and passed into the pure operation — a 163-byte phrase no longer fits the old 96-B stack local, and
//     the loop task's stack is not where 199 B belong. ⛔ Never a second copy (one per TU or instantiation) and ⛔
//     never live across a recursive executor: `ui_exec` → `mrfw::exec_command` never re-enters `mr_ui_tick`.
char s_send_line[mrui::kSendLineCap];
// ★★ W6 — THE GATE's LIVE ANSWERS, read at the instant of asking (brief §2.5): the node's own team and, for a DM, the
//    existing `Node::team_key_of_id` authority's OWN boolean and hash — the resolver `label_for_team_id` asks (U1).
//    ⛔ No ID-indexed cache and no six-cell label: a known zero hash stays known (`peer_found`), never "unknown".
// ★★ W8: BOTH DM kinds resolve their peer (the family, ⛔ never `== dm`), and D19's fact — does the team-local ID exist —
//    is read here, at the instant of asking, from the node's own accessor. ⛔ Never invented: the default is false.
mrui::SendLive ui_send_live(const mrui::SendReq& req) {
    mrui::SendLive l{};
    l.team_id = g_node.config().team_id;
    l.team_local_id = (g_node.team_local_id() != 0);
    if (mrui::send_kind_dm(req.kind)) l.peer_found = g_node.team_key_of_id(req.peer_id, l.peer_hash);
    if (!l.peer_found) l.peer_hash = 0;
    return l;
}
void ui_perform_send(const mrui::SendReq& req, uint32_t now_ms) {
    mrui::ui_perform_send(s_tracker_emg, s_tracker_normal, s_model, req, mrfw::preset_catalog().live(),
                          ui_send_live(req), s_send_line, sizeof s_send_line,
                          uint8_t(MR_UI_TEAM_CHANNEL_ID), ui_have_fix(), ui_exec, nullptr, now_ms);
}
// ★★★ W6 (design r2.23 §7.3) — THE REVIEW's CAPTURE, SERVED IN THE TICK exactly as the Inbox's open is: a double on a
//     phrase REQUESTED it, and this answers from the LIVE catalog, team and peer before the frame freezes. The pure
//     `mrui::ui_review_capture` does all of it; this supplies the resolver's answer and the peer's FULL counted name
//     (`label_from_hash`'s read, W4a — ⛔ never a pre-clipped prefix). ⛔ It never queues and never calls `ui_exec`.
// ★★ W8: the cached FULL raw name of a peer, by hash — the read `label_from_hash` and the review's row 0 use (W4a).
uint8_t ui_peer_name(uint32_t hash, char* out, uint8_t cap, void* /*ctx*/) {
    return g_node.peer_name_find(hash, out, cap);
}
void ui_service_review(const mrui::UiSnapshot& s, uint32_t now_ms) {
    mrui::SendReq b{};
    // ★★★ W8: a DM editor's binding (once, at WRITE MESSAGE) and its 8-column header label, served before the freeze.
    bool resolve = false;
    if (s_model.editor_capture_owed(b, resolve)) (void)mrui::ui_editor_capture(s_model, ui_send_live(b), ui_peer_name, nullptr);
    if (!s_model.review_capture_owed(b)) return;
    const mrui::SendLive live = ui_send_live(b);
    char raw[MESHROUTE_NS::protocol::peer_name_max];
    const uint8_t n = live.peer_found ? g_node.peer_name_find(live.peer_hash, raw, uint8_t(sizeof raw)) : uint8_t(0);
    (void)mrui::ui_review_capture(s_model, mrfw::preset_catalog().live(), live, raw, n, s, now_ms);
}
// ★★★ W7 (design §4.3, r2.25) — THE NAME SAVE, SERVED ONCE IN THE TICK. `SAVE` raised the model's one request; this
//     takes it (once — the model marks it taken), calls W0's `mrfw::rename_node` EXACTLY ONCE with the model's counted
//     bytes, and hands back the typed panel answer. It is placed after the emergency drain and OUTSIDE the normal send
//     busy gate, so a pending DM never blocks a save. ⛔ It saves nothing else: no redraw, wake or acknowledgement
//     reaches `rename_node`, because nothing but `SAVE` sets the request.
// ★ THE MAPPING IS EXHAUSTIVE AND DEFAULT-LESS (-Wswitch): `saved` and `unchanged` both read NAME SAVED (an identical
//   name costs no write and is still saved, §4.3); the three refusals keep their OWN words — ⛔ never an NV failure.
mrui::NameResult name_result_of(mrfw::RenameResult r) {
    switch (r) {
        case mrfw::RenameResult::saved:
        case mrfw::RenameResult::unchanged:      return mrui::NameResult::saved;
        case mrfw::RenameResult::nv_save_failed: return mrui::NameResult::nv_failed;
        case mrfw::RenameResult::too_long:       return mrui::NameResult::too_long;
        case mrfw::RenameResult::bad_args:       return mrui::NameResult::bad_name;
    }
    return mrui::NameResult::none;   // -Wreturn-type only; `none` reads NAME NOT SAVED, never a success
}
void ui_service_name_request() {
    const char* bytes = nullptr;
    uint8_t     len   = 0;
    if (!s_model.take_name_request(bytes, len)) return;
    s_model.on_name_result(name_result_of(mrfw::rename_node(bytes, len)));
}

// ---- snapshot ----------------------------------------------------------------------------------------------------
uint32_t age_s_from(uint32_t now_ms, uint32_t then_ms) { return uint32_t(now_ms - then_ms) / 1000u; }

// ---- the INBOX adapter (UI-7, spec §6.1) -------------------------------------------------------------------------
// ★ `Inbox::pull()` DIRECTLY — never a textual `pull_inbox` into a BufferSink: that NDJSON is unbounded and a 512 B
//   sink would truncate it mid-record (spec §6.1). The visit is READ-ONLY: `pull` is `const` and touches no cursor,
//   so browsing on the panel cannot desynchronise the companion app, which is the durable cursor's real owner.
// ★ `since = 0` is "from the beginning" (seqs are 1-based, inbox.h:129) and we always want the newest tail, so the
//   NEWEST-WINS budget in `mrui::InboxRowBudget` — pure and natively tested — does the selecting, PER KIND.
// ⚠ `e.body` is NOT a C string: it points into the store's own record bytes, is `nullptr` when `body_len == 0`, and
//   is valid only for the duration of this callback (inbox.h:23-24). ⇒ copy-and-terminate, here, every time.
// ⓘ `now64` is sampled ONCE, in the caller, and carried in the context: every row of one frame must be aged against
//   the SAME instant, or a long pull could show two rows a second apart that arrived together.
// ★★★★ §CUSTODY-C (design §7.4) — **THE ORDINARY-VIEW GATE, AND IT IS THE FIRST STATEMENT OF THIS CALLBACK.**
//      An internal OUTCOME record (an E2E-ack receipt today; a custody-failure report when §17-F lands) is
//      protocol machinery, not a message, so it is not a row, not a detail page and not part of `inbox_total`.
// ⛔⛔ THE POSITION IS AS LOAD-BEARING AS THE PREDICATE, and it answers TWO different rules that a gate placed
//     anywhere lower would answer only one of:
//       ① §7 — *"the body … must never be passed through the ordinary text encoder or OLED byte sanitizer as if
//         it were a message."* The `ui_display_byte` loop below is that sanitizer. E2E ACK is bodyless so the
//         rule is vacuous TODAY, and it stops being vacuous the moment a custody record (a binary body) is
//         stored — the gate has to already be above the loop when that happens, not be moved there later.
//       ② §7.4 — *"Filtering occurs before any visible row budget or visible-total calculation."* `budget->add`
//         is BOTH: it fills the per-kind ring AND takes the `inbox_total` count. Refusing after it would let a
//         hidden record spend a visible row slot and inflate the mailbox figure — and, worse, push a NEWER
//         application message off the panel behind records the operator cannot even see (§7.4's own words).
// ⛔ ONE GATE, NOT TWO: `InboxRowBudget` deliberately does NOT re-classify. A second copy of this rule inside
//    the budget would make THIS one un-reddenable by the probe's control (the budget would silently cover for a
//    deleted call here), which is the exact way a redundant guard turns a measurement into decoration.
// ⓘ `e.type` is the STORED record type: `record_dm` writes 0 for every ordinary message and `record_ack` writes
//   the symbolic `DATA_TYPE_E2E_ACK` (inbox.cpp) — so the classification reads the record's own byte and never
//   re-derives a type from `body_len`, `origin` or the absence of a body.
struct InboxPullCtx { mrui::InboxRowBudget* budget; uint64_t now64; };
bool inbox_row_cb(void* vctx, const MESHROUTE_NS::InboxEntry& e) {
    InboxPullCtx* c = static_cast<InboxPullCtx*>(vctx);
    if (MESHROUTE_NS::inbox_record_is_internal(e.type)) return true;   // ★ hidden: no row, no total, no sanitizer
    mrui::InboxRow r{};
    // ★★★ §UI-7D slice B: THE ROW CARRIES THE IDENTITY PAIR, copied verbatim from the record. `kind` replaced the old
    //     `bool is_dm` (one kind authority — see InboxRow), and `seq` is what makes the row nameable at all: it is what
    //     `Inbox::erase(InboxKind, seq)` takes, so what the panel selects and what the store deletes are the same two
    //     values. ⛔ Neither may be re-derived downstream from `origin`, `msg_id` or the row's position.
    r.kind       = e.kind;
    r.seq        = e.seq;
    r.channel_id = e.channel_id;
    // `rx_time_ms` is 64-bit node uptime; the snapshot carries a 32-bit age. A record stamped in the future (a store
    // that survived a reboot, since uptime restarts and the store does not) reads as UNKNOWN — `--`, never a
    // fabricated age. Same rule as `batt_mv` and `console_json.h:126`: omit, do not guess.
    r.rx_age_s = (e.rx_time_ms == 0 || c->now64 < e.rx_time_ms) ? UINT32_MAX
                                                                : uint32_t((c->now64 - e.rx_time_ms) / 1000u);
    const uint8_t cap = uint8_t(sizeof r.text - 1);
    uint8_t n = (e.body_len < cap) ? e.body_len : cap;
    if (!e.body) n = 0;                                   // an E2E-ack RECEIPT carries no body at all (body == nullptr)
    // ⓘ §UI-7D slice B: the `'.'` substitution moved into `mrui::ui_display_byte` so the preview row and the detail
    //   body share ONE sanitizer (U1) — the policy is unchanged, and the detail modal must not invent a second one.
    for (uint8_t i = 0; i < n; ++i) r.text[i] = mrui::ui_display_byte(e.body[i]);
    r.text[n] = '\0';
    c->budget->add(r);
    return true;                                          // never stop early — the budget decides what is KEPT
}

// ---- the DETAIL modal's store half (§UI-7D slice B, spec §3.5) ---------------------------------------------------
// ★★★ THE MODEL ASKS; THIS ANSWERS. `mrui::UiModel` may not touch `g_node.inbox()` at all — that is what keeps every
//     gesture meaning, every state transition and the whole identity rule natively testable — so it emits a REQUEST
//     carrying `(InboxKind, seq)` and this file performs the `pull()` / `erase()` and feeds back a TYPED answer.
// ★★ AND THE COPY HAPPENS INSIDE THE CALLBACK, ON PURPOSE. `InboxEntry::body` points into the store's own record bytes
//    and is valid for the duration of this callback ONLY (inbox.h:23-24) — one line after `pull()` returns it is a
//    use-after-free. `on_inbox_opened` copies AND sanitizes it into the model's fixed `inbox_max_body + 1` buffer while
//    the pointer is still live, and the renderer only ever reads the FROZEN page that buffer produced.
// ⚠ `e.body` is `nullptr` whenever `body_len == 0` (an E2E-ack receipt has no body at all) and the bytes are NOT a C
//   string — the model is handed the LENGTH and never calls `strlen`.
struct InboxFindCtx { mrui::InboxKind kind; uint32_t seq; uint32_t now_ms; bool found; };
bool inbox_detail_cb(void* vctx, const MESHROUTE_NS::InboxEntry& e) {
    InboxFindCtx* c = static_cast<InboxFindCtx*>(vctx);
    // ★★★ §CUSTODY-C — THE DETAIL SEAM'S OWN GATE (design §7.4: *"the OLED inbox list/detail does the same"*), and
    //     it is a SECOND SEAM rather than a second copy of one rule: the list decides what is offered as a ROW, this
    //     decides what may be OPENED as a page, and each is reached by a different call path.
    // ⓘ It is UNREACHABLE TODAY BY CONSTRUCTION and is written anyway, deliberately: `(kind, seq)` can only come
    //   from a row the list published, and the list no longer publishes an internal record — so this arm is the
    //   fail-closed floor under that argument, not a duplicate of it. If a future caller ever names a seq directly,
    //   the modal answers `MESSAGE GONE` (the ordinary view HAS no such message) instead of rendering a receipt's
    //   header — or a custody report's raw bytes — through the detail body's sanitizer (§7).
    // ⛔ It must stay ABOVE the `found` latch: matching first and refusing after would report the record as opened.
    if (MESHROUTE_NS::inbox_record_is_internal(e.type)) return true;
    // ★★★ BOTH HALVES OF THE PAIR. The DM and channel sequence spaces are independent, so matching `seq` alone would
    //     open — and then delete — the other store's record with the same number ([[B133]] was this exact pair).
    if (e.kind != c->kind || e.seq != c->seq) return true;
    c->found = true;
    s_model.on_inbox_opened(e.kind, e.seq, e.origin, e.channel_id, e.body, e.body_len, c->now_ms);
    return false;                                         // sequences are unique within a kind: nothing else can match
}

// ⓘ `pull()` already FILTERS tombstoned records (inbox.h:132-137), so a record deleted a moment ago is genuinely not
//   found here — which is what makes `MESSAGE GONE` the truth rather than a guess.
void ui_open_inbox_detail(const mrui::InboxReq& rq, uint32_t now_ms) {
    InboxFindCtx c{ rq.kind, rq.seq, now_ms, false };
    (void)g_node.inbox().pull(/*dm_since=*/0, /*chan_since=*/0, inbox_detail_cb, &c);
    // C2, FAIL LOUD: the request is ALWAYS answered. A silent non-answer would leave the model waiting for an open that
    // can never arrive, and the panel would simply not respond to the press.
    if (!c.found) s_model.on_inbox_open_gone(rq.kind, rq.seq);
}

// ★★ ⛔ THE ONE PLACE A RECORD IS DELETED, and the outcome is passed through VERBATIM. `Inbox::erase` distinguishes
//    three states and the panel renders each differently; collapsing them to a bool is exactly what §3.5 forbids —
//    `not_found` is neither a success nor a storage failure.
// ⛔ [[B134]] CLOSED 2026-08-28 — THE NOTE THAT STOOD HERE IS FALSE AND IS REPLACED, NOT SOFTENED. It read: *"on
//   `heltec_v3` the inbox is a volatile RAM ring … a reboot takes it, its tombstone and the ENTIRE history together,
//   so there is nothing cross-reboot to test on this board."* Every ESP32 target now mounts the durable
//   `SegmentedInboxStore` over LittleFS records + NVS meta (`src/device_inbox_fs_esp32.h`), so spec §6.2's
//   delete-survives-reboot criterion IS live on the panel's own board and its platform qualification is retired.
// ⚠ WHAT THIS FUNCTION PROMISES IS UNCHANGED, and deliberately so: it passes the store's own three-way verdict
//   through verbatim. A durable backend makes `erased` a stronger fact; it does not make this line entitled to
//   assert one the store did not report.
void ui_erase_inbox_record(const mrui::InboxReq& rq) {
    s_model.on_inbox_erased(rq.kind, rq.seq, g_node.inbox().erase(rq.kind, rq.seq));
}

void ui_service_inbox_request(uint32_t now_ms) {
    mrui::InboxReq rq{};
    if (!s_model.take_inbox_request(rq)) return;
    switch (rq.what) {
        case mrui::InboxWhat::open:  ui_open_inbox_detail(rq, now_ms); break;
        case mrui::InboxWhat::erase: ui_erase_inbox_record(rq);        break;
        // `none` is not a request the drain can hand out (it is the "nothing pending" value), and it is listed rather
        // than defaulted so a fourth verb fails the build instead of being silently dropped (§B72's rule).
        case mrui::InboxWhat::none:  break;
    }
}

void fill_inbox_rows(mrui::UiSnapshot& s) {
    static mrui::InboxRowBudget budget;                   // reused: 8 rows is ~200 B, not a per-tick stack allocation
    budget.reset();
    InboxPullCtx ctx{ &budget, g_hal.now() };
    // ★ §CUSTODY-C: `pull()`'s return is the RAW visit count and it is DISCARDED here — design §7.4 forbids
    //   publishing it as `inbox_total`, and `InboxRowBudget::publish` now takes no total at all, so the only
    //   number that can reach the snapshot is the budget's own admitted count. The raw stream is still complete
    //   and still reaches the companion verbatim through `pull_inbox` (inbox.h's ruling on `pull`).
    (void)g_node.inbox().pull(/*dm_since=*/0, /*chan_since=*/0, inbox_row_cb, &ctx);
    budget.publish(s);
}

mrui::UiSnapshot build_snapshot(uint32_t now_ms) {
    mrui::UiSnapshot s{};
    s.now_ms       = now_ms;
    // ★★ §B108 round 2: ONE call (U2), never two assignments — it publishes the CAPPED display counts and the
    //    UNCAPPED arrival serials they were derived from together, which is what lets `FrameGate` freeze a serial
    //    that provably matches the number this frame will draw.
    s_counters.publish(s);
    s.last_dm_age_s = s_counters.have_dm ? age_s_from(now_ms, s_counters.last_dm_ms) : UINT32_MAX;
    s.last_ch_age_s = s_counters.have_ch ? age_s_from(now_ms, s_counters.last_ch_ms) : UINT32_MAX;
    // The TEAM/SEND slots are gated on MR_FEAT_OLED && MR_FEAT_TEAM (spec §9): `gateway_heltec` is a REAL build with
    // OLED=1 and TEAM=0, so this is not hypothetical. `team_build` is what makes the model's cycle skip those slots.
    s.team_build = (MR_FEAT_TEAM != 0);
    // ★ §UI-14: the build-time fact the pure model branches on, published at the ONE site that knows it — the same
    //   shape as `team_build` directly above (U3). See the `MR_UI_BLE_ROW` block at the top of this file.
    s.ble_row    = (MR_UI_BLE_ROW != 0);
    // ★★★★ §UI-15 slice 5 / plan §6 — §3.6.3's TWO CHILD PREDICATES, published at the ONE site that knows them, in the
    //      same shape as `team_build` / `ble_row` above (U3) so the model stays `#if`-free and the native suite drives
    //      every combination. ⛔ THEY ARE TWO PREDICATES AND NOT ONE: static join has NOTHING to do with the team
    //      plane, and hiding it because `MR_FEAT_TEAM` is off is the defect plan §6 names in as many words.
    // ★ MEASURED, not assumed: `handle_join` and `handle_create`/`handle_team` are ALL compiled out by
    //   `#if MR_N_LAYERS < 2` (src/firmware_config.h), so that IS the child predicate — and CREATE additionally needs
    //   the team plane to exist. They coincide in every env in the tree today (`MR_FEAT_TEAM 0` arrives only with
    //   `MR_PROFILE_GATEWAY`, which sets `MR_N_LAYERS=2`), which is exactly why they are published separately.
    s.prov_join_static = (MR_N_LAYERS < 2);
    s.prov_create_team = (MR_N_LAYERS < 2) && (MR_FEAT_TEAM != 0);
    // ★ §UI-16 N2 — the THIRD child, published as its OWN predicate (see `provision_rows`): a nearby join is
    //   a MEMBERSHIP operation on a leaf build AND it needs the team plane, because the observation cache it
    //   lists is `MR_FEAT_TEAM`-gated in the core (`node.h`'s `team_seen_*` stubs answer an empty list on a
    //   gateway). ⛔ It coincides with `prov_create_team` in every env in the tree today and is still not the
    //   same fact — the coincidence is what `provision_rows` refuses to encode.
    s.prov_join_team   = (MR_N_LAYERS < 2) && (MR_FEAT_TEAM != 0);
    // ★★★★ §UI-16 N4 — the FOURTH child, and the ONLY one with a RUNTIME term: `INVITE MEMBER` is available iff
    //      we ARE IN A TEAM (spec §4-N4 pin 12). A teamless node has no membership to invite anyone into and no
    //      content key it could ever grant, so the row is HIDDEN rather than offered and refused ([[B209]]) — and
    //      it appears the moment `team <id>` lands and disappears the moment `team 0` does, on a running node.
    // ⓘ `config().team_id` is the core's own "am I in a team" (0 = not, `node.h`'s own words) and it is ALREADY
    //   published one block down as `s.team_id`. It is read a second time HERE rather than derived from that
    //   field in the pure unit, deliberately: the four child predicates are FOUR PARAMETERS by ruling (see
    //   `provision_rows`), and a predicate half-derived from another snapshot field would be the coincidence
    //   that block refuses to encode.
    s.prov_invite      = (MR_N_LAYERS < 2) && (MR_FEAT_TEAM != 0) && (g_node.config().team_id != 0);
    // ★★★★ §UI-16 K6 — the FIFTH child, published as its OWN predicate (see `provision_rows`), and ⛔ deliberately
    //      WITHOUT the runtime term `prov_invite` carries: `SAVED KEYS` manages the `/mrteams` KEYRING, and a node
    //      that has left every team may still hold four retained records it needs to free. Gating on
    //      `config().team_id != 0` would hide the screen exactly when the operator needs it — the dead end K6 exists
    //      to open. ⓘ The BUILD half is the keyring's own: the store and its service are `MR_FEAT_TEAM`-side.
    s.prov_saved_keys  = (MR_N_LAYERS < 2) && (MR_FEAT_TEAM != 0);
#if MR_FEAT_TEAM
    // ⚠ `rt_team_at` has NO !MR_FEAT_TEAM stub, by deliberate core design (there is no `_rt_team` to read), so this
    //   whole block must be guarded — the two counters around it stub to 0 and would compile either way.
    const uint8_t total = g_node.rt_team_count();
    s.team_total = total;
    s.team_shown = (total > mrui::kMaxTeamRows) ? mrui::kMaxTeamRows : total;
    const uint64_t now64 = g_hal.now();
    for (uint8_t i = 0; i < s.team_shown; ++i) {
        const MESHROUTE_NS::RtEntry& e = g_node.rt_team_at(i);
        mrui::TeamRow& r = s.team[i];
        r.id = e.dest;
        if (e.n > 0) {
            const MESHROUTE_NS::RtCandidate& c = e.candidates[0];   // the PRIMARY candidate (node_carriers.h:296)
            r.score_q4    = c.score;
            r.hops        = c.hops;
            r.last_heard_s = (c.last_seen_ms == 0 || now64 < c.last_seen_ms)
                           ? UINT32_MAX : uint32_t((now64 - c.last_seen_ms) / 1000u);
        } else {
            r.last_heard_s = UINT32_MAX;
        }
        // ★★★★ §UI-17 S5 — THE PEER's LAST AUTHENTICATED POSITION, READ FROM THE CACHE THAT ALREADY EXISTS, AND
        //      PUBLISHED VERBATIM. ⛔⛔ THIS IS A `const` READ AND NOTHING ELSE: `Node::peer_loc_find`
        //      (`node_hashlocate.cpp:432`) touches no timer, no queue and no radio, and NOTHING in this slice asks
        //      for, refreshes or transmits a position. Rendering TEAM creates NO TRAFFIC OF ANY KIND (spec §3.4) —
        //      the probe counts TX-queue depth and radio starts across a full TEAM walk rather than arguing it.
        // ★ ONE RESOLUTION PER ROW (U1): `label_for_team_id` returns the very hash it labelled from, so the position
        //   is looked up under the SAME identity the panel names. ⛔ A second `team_key_of_id` here would be a
        //   second authority for "who is this row", and the two could disagree.
        // ⛔ THE AGE IS TAKEN **VERBATIM** FROM THE OUT-PARAM — no cast, no clamp, no re-derivation against `now_ms`
        //   (the `home_confirm_age_ms` rule below). `0xFFFFFFFF` is the cache's own "I cannot date this" and must
        //   reach the freshness rule intact.
        // ⓘ `hash == 0` is the resolver's "no key for this team id" and simply leaves `peer_loc_valid` false — the
        //   same blank, through the same arm (spec §3.4's four terms; the other three are the pure unit's).
        // ⓘ COST (spec §6): ONE linear scan of at most `cap_peer_loc` (16) slots per shown row, per tick, bounded by
        //   `kMaxTeamRows` (8). ⛔ No flash, no radio, no allocation.
        const uint32_t hash = label_for_team_id(r.id, r.label, uint8_t(sizeof r.label), kTeamNameCols);
        // ★★★★ §UI-16 N4 — THE **SECOND CONSUMER** OF THAT ONE RESOLUTION (spec §6, U1: *"one `team_key_of_id`
        //      resolution per row and hands it to BOTH consumers, ⛔ never two lookups for one row"*). The INVITE
        //      window needs the same member enumeration the TEAM screen is already walking, so it is projected
        //      HERE, in the same loop, from the same `hash` — ⛔ never a second `rt_team_at` walk and ⛔ never a
        //      second `team_key_of_id` call. ⓘ `team_shown` bounds BOTH arrays; there is no second count.
        // ⛔⛔ THIS IS A `const` READ AND NOTHING ELSE, exactly as the location read above is: `peer_name_find`
        //     (`node_hashlocate.cpp:450`) walks the peer-key table and touches no timer, no queue and no radio.
        //     ⛔ NOTHING in this slice asks for, refreshes or transmits an identity — the window's local refresh
        //     is these two `const` reads and nothing more (spec §3 P-4b; the probe asserts the TX-queue depth
        //     and the radio's start count across a held-open window rather than arguing it).
        mrui::InviteMember& mem = s.member[i];
        mem.id         = r.id;
        mem.key_hash32 = hash;          // ⛔ 0 = NO AUTHORITATIVE BINDING — the pure unit's fail-closed floor (F-7)
        // ★★★ THE NAME IS `Node::peer_name_find`'s ANSWER AND NOTHING ELSE (F-15 rules 2-3), asked HERE rather
        //     than copied out of `r.label`: that string carries the member-fingerprint fallback and
        //     `label_for_team_id`'s bare `id <n>`, and either one in the row's six-column name field would be a
        //     THIRD spelling of the hash. `""` — the blank column — is the honest state until a name is cached
        //     beside a verified pubkey.
        // ★★★★ W4a — THE WHOLE NAME IS READ (32 counted bytes, never NUL-terminated by the raw API) AND FORMATTED AS A
        //      NAME at the carrier's 14 cells (`kInviteNameCols`), which is what NEW MEMBER draws; the candidate row
        //      projects it to six at draw time. ⛔ ONLY WHEN THERE IS ONE (`nn > 0`): an unnamed member keeps `""`
        //      and ⛔ never reaches the formatter's hash branch, which would put a hash-derived token in the name
        //      column (the defect probe control O8 reinstates).
        // ⚠ THE GUARD IS SPELLED `!= 0u`, ⛔ NOT `!= 0`, AND THAT IS DELIBERATE RATHER THAN A STYLE CHOICE
        //   (measured, not anticipated): the location publish two lines down carries the guard `if (hash != 0) {`,
        //   which is the ANCHOR of probe control C114 — a `sed` substring. A second identical line here makes that
        //   control mutate THIS block instead, and a landed control silently stops measuring what it names.
        mem.name[0] = '\0';
        if (hash != 0u) {
            char raw[MESHROUTE_NS::protocol::peer_name_max];
            const uint8_t nn = g_node.peer_name_find(hash, raw, uint8_t(sizeof raw));
            if (nn > 0) (void)mrui::ui_fmt_identity(mem.name, sizeof mem.name, raw, nn, hash, kInviteNameCols);
        }
        if (hash != 0) {
            MESHROUTE_NS::Node::PeerLocSrc src = MESHROUTE_NS::Node::PeerLocSrc::peer;
            r.peer_loc_valid = g_node.peer_loc_find(hash, r.peer_lat_e7, r.peer_lon_e7, r.peer_loc_age_s, src);
            // ⓘ `src` (peer DM vs team post) is read and DELIBERATELY NOT PUBLISHED: 19 columns hold no provenance
            //   field, both sources are authenticated by their own receive-site evidence test, and a fact the panel
            //   cannot draw has no business riding the snapshot. Stated so its absence is a decision, not an
            //   oversight ([[meshroute-mark-done-vs-missing-in-code]]).
            (void)src;
        }
    }
#endif
    s.my_team_id = g_node.team_local_id();
    s.team_id    = g_node.config().team_id;
    // ★★★★ §UI-16 N2 — THE NEARBY-TEAM OBSERVATIONS (§3.6.4 point 2), PROJECTED HERE AND NOWHERE ELSE.
    //      ⛔⛔ THIS IS A `const` READ AND NOTHING ELSE. `Node::team_seen_count()` / `team_seen_at()` walk a
    //      RAM ring (`lib/core/team_seen_ring.h`) and touch no timer, no queue and no radio; nothing in this
    //      slice asks for, refreshes, probes or transmits anything to fill it. THE SCAN IS PASSIVE — the N2
    //      probe arm counts the TX-queue depth and the radio's start count across a full NEARBY walk rather
    //      than arguing it, exactly as §UI-17 S5's TEAM walk does.
    // ⛔ NO `#if MR_FEAT_TEAM` GUARD, and that is deliberate rather than an omission: unlike `rt_team_at`,
    //    both accessors have `#else` stubs (`node.h`) that answer `0` / `nullptr` on a build with no team
    //    plane, so a gateway compiles this loop and publishes an EMPTY list — one code path, both arms.
    // ⛔ THE RETENTION WINDOW IS THE CORE'S, APPLIED INSIDE THE ACCESSORS AT THE READ. This site adds no
    //    second staleness rule; an entry past the window is simply not returned.
    // ⓘ THE AGE IS DERIVED FROM **ONE** CLOCK READ and is published with its own validity flag: a stamp
    //   ahead of `now` (a clock that stepped backwards, or an entry that expired between the count and the
    //   fetch) is UNDATEABLE, and `ui_fmt_home_age` renders that as `--`. ⛔ Never a fabricated `0s` — the
    //   `peer_loc_age_s`/`home_confirm_age_ms` rule: an age a surface cannot know must not read as fresh.
    // ⓘ COST (spec §6): at most `cap_team_seen` (8) iterations per tick, no flash, no radio, no allocation.
    {
        const uint64_t seen_now = g_hal.now();
        const uint8_t  seen     = g_node.team_seen_count();
        s.nearby_n = (seen > mrui::kMaxNearbyRows) ? mrui::kMaxNearbyRows : seen;
        for (uint8_t i = 0; i < s.nearby_n; ++i) {
            const MESHROUTE_NS::TeamSeen* e = g_node.team_seen_at(i);
            if (!e) { s.nearby_n = i; break; }        // ⛔ FAILS CLOSED: the list ends where the cache does
            mrui::NearbyRow& r = s.nearby[i];
            r.team_id   = e->team_id;                 // ★ the row's IDENTITY, carried whole (§B66)
            r.snr_q4    = e->snr_q4;                  // ★ RAW — the tier is the pure unit's, via presence_quality_tier
            r.age_valid = (seen_now >= e->last_ms);
            r.age_ms    = r.age_valid ? (seen_now - e->last_ms) : 0;
        }
    }
    s.batt_mv    = s_batt_mv;
    // ================================================== §CHROME-3 — THE FIVE FIELDS §CHROME-1 DEFINED BUT COULD NOT
    // PUBLISH. The projection is PURE and may not touch `g_node`; every fact below is a `g_node` accessor, so this is
    // the one site that can supply them (the same shape as `team_build` / `ble_row` above — U3).
    // ⓘ AMENDED 2026-08-23 (§CHROME-5): SEVEN now — the duty gauge's two inputs are published from the same site for
    //   the same reason, at the end of this block. The heading is kept as written (§3 rule 3).
    // ★ IS THERE A MOBILE-HOME PLANE ON THIS BUILD AT ALL? `gateway_heltec` is a REAL build with OLED=1 and MOBILE=0,
    //   where design §4.2 rules the home slot BLANK — never crossed, because "not applicable" is not a fault.
    s.mobile_build         = (MR_FEAT_MOBILE != 0);
    // ⓘ No `#if` around the three accessors below: `node.h` supplies !MR_FEAT_MOBILE stubs for all three (`unknown` /
    //   false / 0), so the non-mobile build reads the same "nothing established" answers the projection then blanks.
    s.home_link            = g_node.mobile_home_link();
    s.home_confirmed_ever  = g_node.mobile_home_confirmed_ever();
    // ★★★★ THE 64-BIT AGE, TAKEN VERBATIM FROM THE ACCESSOR AND NOT RECOMPUTED — the trap this slice was briefed
    //     against. `Node::mobile_home_confirm_age_ms()` returns `uint64_t` (node.h) and does the subtraction against
    //     the HAL's own 64-bit clock; `UiSnapshot::now_ms` here is `uint32_t`, so `now_ms - confirmed_ms` written at
    //     this line would be the ~49.7-day wrap design §4.2 forbids and this project already fixed once. ⛔ There is
    //     exactly one bucketing of this value in the tree (`ui_fmt_home_age`) and it takes `uint64_t` all the way
    //     into the divisions. ⛔ Never widen a 32-bit difference here and never cast on the way in.
    s.home_confirm_age_ms  = g_node.mobile_home_confirm_age_ms();
    // ★ THE TEAM CHANNEL **CONTENT** KEY (§4.4) — not the node's own crypto identity, and not a cached peer key.
    //   ⓘ Stubbed to false on a !MR_FEAT_TEAM build, so no guard is needed here either.
    s.team_key_present     = g_node.team_channel_key_present();
    // ★★★ §UI-16 K7 ([[B245]]) — OUR OWN STABLE IDENTITY, published so the roster grant's SELF test is the SAME
    //     comparison `Node::team_key_grant_send`'s `self` arm makes (`target_hash == _key_hash32`). It is an
    //     unconditional `const` accessor on every build — no team plane, no crypto identity and no cached peer key
    //     is required to READ it, and 0 is the honest "this node has no stable identity yet".
    s.my_key_hash32        = g_node.key_hash32();
    // ★★★★ W4b (design §4.2) — OUR OWN NAME, PUBLISHED ONCE PER TICK HERE AND NOWHERE ELSE: the stored counted bytes
    //      (`effective_name` — W1c D10, possibly empty, ⛔ never terminated) and their count. ⛔ No NV read: `cfg set
    //      name` already published the saved name into the core. Frozen with the frame, so a console rename between
    //      OLED pages changes the NEXT frame only; the renderer never reads the live node for it.
    s.own_name_len         = g_node.effective_name(s.own_name, uint8_t(sizeof s.own_name));
    // ★★★★ §CHROME-5 — THE DUTY GAUGE'S ONE SEMANTIC AUTHORITY, READ **ONCE PER TICK** AND PUBLISHED VERBATIM.
    //      `Node::duty_status()` (`lib/core/node_mac.cpp:1716`) is an existing `const` accessor over the same budget
    //      `duty_over_budget` enforces, so this slice changes no wire, no NV, no routing and nothing in `Node`.
    // ⛔⛔ AND IT IS THE ONLY ACCEPTABLE SOURCE FOR THIS SLOT. ⛔ NOT raw `duty_ms` (a window sum, not a verdict), and
    //     ⛔ NOT `channel_duty_budget_ms()` — the FIVE-MINUTE anti-spam basis, which is a different budget over a
    //     different window answering a different question ("may I originate a channel post"), and which reads 0 while
    //     duty is disabled. An icon that mixed them would be confidently wrong in both directions.
    // ⛔ THE RENDERER NEVER CALLS IT: `draw_frame` replays the whole scene once per OLED page (§8.2), so a live read
    //    there would let the gauge change between two pages of one image — the §UI-17 S3 tear class.
    // ⓘ `avail_ms` is READ AND DELIBERATELY NOT PUBLISHED: the gauge is icon-only, so the recovery time is a fact the
    //   panel cannot draw. `duty` on the console carries it (`firmware_commands.cpp`'s `dump_duty`).
    {
        const MESHROUTE_NS::Node::DutyStatus duty = g_node.duty_status();
        s.duty_enabled     = duty.enabled;
        s.duty_pct         = duty.pct;
    }
    // ★★★★ §UI-17 S3 — OUR OWN CONFIGURED POSITION, PUBLISHED HERE AND NOWHERE ELSE, because THIS is the site the
    //      frame freezes. `draw_status_screen` used to read `g_node.config()` itself, and `draw_frame` runs ONCE
    //      PER OLED PAGE — so a coordinate changing mid-frame TORE row 4 across the eight page replays. ⇒ the two
    //      coordinates ride the snapshot VERBATIM (no cast, no clamp — the `home_confirm_age_ms` rule) and the
    //      "do we have a fix at all" question is answered ONCE, by the one predicate, right here.
    // ⓘ `ui_have_fix()` above calls the SAME predicate for the `-l` gate, where the LIVE answer at press time is
    //   what a distress send needs; this is the FROZEN one, for the frame. Same definition, two instants (U1).
    const MESHROUTE_NS::NodeConfig& own_cfg = g_node.config();
    s.own_lat_e7           = own_cfg.lat_e7;
    s.own_lon_e7           = own_cfg.lon_e7;
    s.own_fix              = mrui::ui_status_have_fix(own_cfg.lat_e7, own_cfg.lon_e7);
    // ★★★★ §UI-10/11 P3 — **THE `/mrui` CATALOG REACHES THE PANEL HERE, AND NOWHERE ELSE.** `mrfw::preset_catalog()`
    //      is the ONE live instance (`src/firmware_commands.cpp`), the SAME object the `ui preset` verbs write over
    //      USB and BLE — so the panel and the console cannot be two opinions about the wearer's phrases.
    // ★★ IT IS PUBLISHED AT THE **SNAPSHOT** SITE FOR THE `own_lat_e7` REASON DIRECTLY ABOVE, restated because it is
    //    design §3.2.3's own requirement rather than a local habit: *"Page-buffer painting freezes one catalog
    //    generation for the whole frame so a BLE update between OLED pages cannot tear two versions into one
    //    image."* `draw_frame` replays the scene once per OLED page over the FROZEN copy, so a `ui preset set`
    //    arriving between pages 3 and 4 cannot reach the image at all.
    // ⛔ THE RENDERER ASKS THE CATALOG NOTHING — the `nearby[]` rule, one screen over.
    // ⓘ ONE call (U2 — `s_counters.publish`'s shape): the generation and both projections come from the SAME
    //   instant, so a frame can never freeze a list beside a generation that did not produce it.
    mrui::ui_snapshot_publish_presets(s, mrfw::preset_catalog().live());
    fill_inbox_rows(s);
    return s;
}

OutcomeView freeze_outcome(const mrui::UiSnapshot& s) {
    OutcomeView v{};
    v.st       = s_model.emergency();
    v.dm       = s_model.dm_state();
    v.chan     = s_model.chan_state();
    v.refuse   = s_model.refuse_reason();
    v.fail     = s_model.fail_reason();
    // ⚠ CONTRACT (see UiModel::on_send_refused): the code is meaningful only when the reason is not `parser`. It is
    //   frozen unconditionally because freezing is cheap and reading it conditionally is the renderer's job.
    v.refuse_code = s_model.refuse_code();
    // ★★ W8: a WRITTEN result shows its OWN record's words (never the shared ones an alarm also writes); the alarm
    //    overlay keeps the shared ones. The model decides which — this only freezes the answer.
    s_model.panel_reasons(v.refuse, v.refuse_code, v.fail);
    v.evidence = s_model.emg_evidence();
    v.tries    = s_model.attempts();
    // ★ §B115: frozen beside `tries`, never derived from it here. Deriving it in the renderer is what shipped.
    v.attempt_ordinal = s_model.emg_attempt_ordinal();
    v.arm_secs = s_model.arming_secs_left(s);
    // ⚠ `retry_at_ms()` is meaningful ONLY while `blocked` (the STATE is the predicate — §B74 removed the sentinel),
    //   so it is read only there, and wrap-safely.
    if (v.st == mrui::Emergency::blocked) {
        const uint32_t left = s_model.retry_at_ms() - s.now_ms;
        v.retry_in_s = (left >= (1u << 31)) ? 0 : (left + 999) / 1000;
    }
    if (v.st == mrui::Emergency::reply) {
        snprintf(v.who,  sizeof v.who,  "%s", s_model.reply_who());
        snprintf(v.text, sizeof v.text, "%s", s_model.reply_text());
    }
    return v;
}

// ★★ §UI-14 — THE SERVICE READ, and it happens EXACTLY ONCE PER FRAME, at the freeze. ⛔ Not per page (the eight page
//    transfers of one frame would each re-read `effective()` and could tear a marker across the image) and ⛔ not in
//    the renderer, which by contract touches nothing live.
// ★ The three predicates are read only while the service is OPEN. That is not defensive: `config_unsaved()` and
//   `reboot_required()` are both defined as false on a closed service, so reading them regardless would be reading a
//   value whose meaning is "we do not know" — and `open == false` is a state the panel says out loud instead.
SettingsView freeze_settings() {
    SettingsView v{};
    v.open = s_cfg.is_open();
    if (!v.open) return v;
    v.unsaved  = s_cfg.config_unsaved();
    v.conflict = s_cfg.conflict();
    v.reboot   = s_cfg.reboot_required();
    v.draft    = s_cfg.draft();
    return v;
}

// ---- render policy (spec §3.3 layout) ---------------------------------------------------------------------------
// 128x64, two fonts only (spec §11: do not link the full font set). 6x10 gives 21 columns ACROSS THE WHOLE PANEL and
// a 10 px line pitch; 10x20 gives 12 columns and is used for the emergency headline alone.
// ⛔ SINCE §CHROME-4 THE ORDINARY BODY IS **19** OF THOSE 21 COLUMNS, because the navigation rail owns `x = 0..9`
//    (design §3.2). The full 21 survive in exactly two places: the top status strip, which is always 128 px wide, and
//    the EMERGENCY body, which §5.3 keeps at `x = 0`. See `kBodyX` / `kBodyCols` below.
constexpr int kBarBaseline = 7;    // 6x10 baseline inside the 8 px status bar
constexpr int kBarRuleY    = 9;
constexpr int kBodyY0      = 19;
constexpr int kBodyDy      = 10;
constexpr int kBodyRows    = 5;    // 19, 29, 39, 49, 59 — all inside 64
constexpr int kEmgHeadY    = 34;   // 10x20 headline
constexpr int kEmgDetailY  = 52;   // 6x10 detail beneath it
// ⚠ DELIBERATELY OVERSIZED vs the 21 visible columns, and it is NOT slack for its own sake — do not shrink it back.
//   Every line here is built with snprintf, and `-Wformat-truncation=` (on by default under -Wall in this toolchain and
//   GATE-BLOCKING in this project) fires whenever GCC cannot PROVE the widest expansion fits. It measured 10 such
//   warnings at kLineCap 24 — all benign truncations, all still ten new warnings against a pinned census.
// ⛔ CORRECTED IN PLACE 2026-08-16 (§CHROME-3, V1): this block used to name the packed STATUS BAR as one of the two
//   widest provable lines *("two uint16_t counts at 5 digits, two uint8_t at 3, plus an 11-char volts field because
//   `int32_t/1000` can be 7 digits, at 37 bytes")*. That line is GONE — the icon strip replaced it and formats its
//   tokens through `mrui::ui_fmt_*` into a 5-byte buffer of their own. ⇒ the widest remaining provable line is the
//   REPLY detail (`who` 14 + `text` 20) at 37 bytes. ⛔ **The value stays 48 and must not be shrunk to fit the new
//   figure**: it is what keeps the census at its pin, and the margin costs stack, not flash. ⛔⛔ AND SINCE §CHROME-4
//   THE DISTINCTION IS LOAD-BEARING RATHER THAN A NOTE: this buffer bounds the FORMATTER; what bounds the DISPLAY is
//   each format's own PRECISION (`%-9.9s`, `%-8.8s`, `%4.4s`) and the §7.3 audit written beside each screen, because
//   §7.1 rule 5 forbids letting the panel clip as a truncation policy. ⇒ a format whose widest expansion exceeds
//   19 columns is a DEFECT even though it fits this buffer, and `probe_firmware_ui`'s P14f is what says so.
constexpr int kLineCap     = 48;

int body_y(int row) { return kBodyY0 + row * kBodyDy; }

// ================================================================== §CHROME-4 / design §3.2, §7.1 — THE BODY ORIGIN
//
// ★★★ ONE AUTHORITY, AND EVERY ORDINARY BODY DRAW GOES THROUGH IT (§7.1 rule 1). The navigation rail owns `x = 0..9`,
//     so the ordinary body starts at `x = 12` and is 116 px wide — 19 columns of the 6-px small font, down from 21.
//     ⛔ A per-call-site `12` is exactly the drift §3.1 already forbids for the strip's slots; here it would be worse,
//        because a site left at `0` would draw its text UNDER the rail's icons rather than merely at the wrong x.
// ⛔⛔ THE ONE EXCEPTION IS THE EMERGENCY BODY, AND IT IS LOAD-BEARING (§5.3): the `Font::large` headlines are 10 px
//     per column on a 128-px panel = 12 columns at `x = 0`, and `NOT RELAYED` already spends 11 of them. Shifting
//     that body to `kBodyX` would leave 11 columns and CLIP A DISTRESS HEADLINE. `draw_emergency` therefore draws at
//     `x = 0` and the rail is not drawn at all while an alarm is up.
constexpr int kBodyX    = 12;    // §3.2: rail x=0..9, then a 2-px gutter
constexpr int kBodyCols = 19;    // 116 px / 6 px per small-font column — ⛔ derived below, never a second literal
constexpr int kBodyPx   = 128 - kBodyX;   // 116
static_assert(kBodyCols * 6 <= kBodyPx, "design §3.2: the body's column count does not fit its 116-px width");
static_assert(kBodyCols == mrui::kDetailCols,
              "design §7.3: the inbox detail wraps at the MODEL's freeze point, so its column count and the "
              "renderer's body width are ONE number — a mismatch makes detail_pages a lie");
// ★ §UI-17 S4: the TEAM row's five fields are budgeted against the SAME body width, in the pure unit that composes
//   them. ⛔ Not a second literal there either — the header derives its 19 from its own field widths and this is
//   where the two meet, so a body that narrowed and a row that did not cannot coexist.
static_assert(int(mrui::kTeamRowCols) == kBodyCols,
              "spec §3.2: the TEAM row fills the body's own column count — one width, not a second literal");
// ★ W4a — THE TWO COMPOSE-SCREEN LABEL BUDGETS, FROM THE BODY WIDTH (design §4.1 r2.20): the header draws the peer after
//   `to: ` (ONE literal, which the header's format also uses), and the DELIVERED result gives the peer a row of its own.
constexpr char    kComposeToPrefix[] = "to: ";
constexpr uint8_t kComposeToCols     = uint8_t(kBodyCols - int(sizeof kComposeToPrefix - 1));   // 15
constexpr uint8_t kDeliveredCols     = uint8_t(kBodyCols);                                      // 19
static_assert(kComposeToCols >= kLabelMinCols && kDeliveredCols >= kLabelMinCols,
              "W4a: every device-label budget holds at least the six-digit member fingerprint");

// ★★★★ THE ONE ORDINARY-BODY DRAW, AND IT DELIBERATELY DOES **NOT** CLAMP THE LINE.
//   §7.1 rule 5 requires dynamic labels to be *"explicitly clamped or moved to a second row"*, and every one of them
//   IS — at its own format (`%-9.9s` on a teammate name, `%-8.8s` on an inbox preview, `%4.4s` on an age) or by
//   taking a row of its own (`DELIVERED to` / the peer name). ⇒ each label is clamped where the MEANING of the clamp
//   can be judged, which is the rule's point.
// ⛔⛔ A BLANKET 19-COLUMN CLAMP HERE WAS WRITTEN AND THEN REMOVED, AND THE REASON IS THE ONE THIS ARC KEEPS
//   RE-LEARNING: it would make §11.2's *"every normal text line fits the 116-pixel body"* an INSTRUMENT THAT CANNOT
//   FAIL. Every drawn line would be 19 columns by construction, so `tools/probe_firmware_ui`'s P14f could never
//   redden, and a future format whose widest expansion was 26 columns would lose six columns of meaning SILENTLY —
//   the same information u8g2's clip loses, with a comment claiming it was a policy. ⇒ the width is PROVEN per
//   format (the §7.3 audit written beside each screen) and MEASURED end to end by P14f, which can therefore fail.
void body_text(int row, const char* s) { mrui::draw_text(kBodyX, body_y(row), s); }

// ================================================================= W4b (design §6.2) — HOME DRAWS AT THE ORDINARY BODY
// ★★★ HOME IS x = 12, 19 COLUMNS, LIKE EVERY OTHER BODY — ⛔ no 24x24 mark and ⛔ no x = 40 rows (design §6.2: the
//     mark leaves Home; the splash, W5, keeps the asset `icons::kMarkMeshRoute` untouched). ⓘ RETIRED WITH IT (brief
//     §2.9): `kStatusMarkX/Y/W/H`, `kStatusTextX`, `kStatusNarrowPx`, their five static_asserts and `status_text` —
//     ⛔ not kept as dead helpers for a mutant to compile against.
static_assert(int(mrui::kHomeCols) == kBodyCols, "design §6.2: Home is ordinary body rows — one width, not a second literal");
static_assert(int(mrui::kStatusLineCap) <= kLineCap,
              "the Home formatters are handed a kLineCap buffer — it must be at least their own bound");

// ★★ THE FAILURE DETAIL, in the two alphabets that exist (spec §2.1 rule 6). A refusal the user cannot act on is the
//    thing C2 and §err-reason exist to prevent — but the honest limit is real: five different walls all come back as
//    `err_unsupported` (no key / no identity / no fix / empty / unsealable), so the compact reason CANNOT name them
//    and the plan rules "show the generic refusal AND THE CODE; do not invent a specific reason".
// ★ `cmdcode_name` is the ONE mapper (U1) — `fw_main.cpp:905` already calls it that and refuses a second switch. A
//   raw enum NUMBER would be exactly the "do not use it to make the comment go away" the frozen field warns about.
// ⓘ A `parser` refusal has no `CmdCode` at all (the line never became a `Command`), and `RefuseReason::parser` IS
//   that predicate — so the code line is suppressed there rather than printing a `queued` that means "not applicable".
void draw_failure_lines(const OutcomeView& v) {
    body_text(1, refuse_text(v.refuse));
    if (v.refuse == mrui::RefuseReason::parser) return;
    char l[kLineCap];
    snprintf(l, sizeof l, "%s", MESHROUTE_NS::console::cmdcode_name(v.refuse_code));
    body_text(2, l);
}

// Which slice of a longer list is on screen. A cursor may address up to kMaxTeamRows entries while only kBodyRows fit.
uint8_t list_first(uint8_t cursor, uint8_t n, uint8_t rows) {
    if (n <= rows || cursor < rows) return 0;
    const uint8_t first = uint8_t(cursor - rows + 1);
    return uint8_t((first + rows > n) ? (n - rows) : first);
}

// ★★★ §UI-17 S1 — THE INTERACTIVE LIST'S LAST ROW, drawn by ONE function for BOTH screens (U1). It renders as
//     `<marker><label>`, which is the shipped ACTION-row shape (`draw_settings_screen`'s `%c%s` arm), and the label is
//     CALLED rather than re-spelled here — §B115: a string built in this TU is a string no automated gate can read.
//     ★ W4b (design §6.1 rule 2): the row is `MENU` now (`kListMenuText`) — it enters menu mode on the Home slot.
//     ⓘ 1 + 4 = 5 of the rail's 19 columns.
void body_menu_row(int row, bool here) {
    char l[kLineCap];
    snprintf(l, sizeof l, "%c%s", here ? '>' : ' ', mrui::kListMenuText);
    body_text(row, l);
}

// ==================================================================== §CHROME-3 / design §3.1 — THE STATUS STRIP
//
// ⛔⛔ WHAT THIS REPLACES, kept visible rather than deleted: the packed 6x10 line `DM%u CH%u T%u/%u %s` (and its
//    `DM%u CH%u %s` non-team arm). Design §2 tabulates why every one of its fields was narrower than its label — `DM`
//    and `CH` were SESSION-unread, not stored totals; `T<a>/<b>` was the UI's 8-row capacity over the route count,
//    never online/total. ⇒ the counts are COMBINED into one envelope (§4.1), the retired `T8/12` fraction becomes the
//    true `team_total` (§4.3), and the two facts the old line could not carry at all — the mobile-home link and the
//    team CONTENT key — get slots of their own.
//
// ★★★ IT CONSUMES THE **FROZEN CHROME** AND NOTHING ELSE. ⛔ No `g_node`, no `ConfigService`, no counter and no
//     battery read happens in here, because U8g2 replays this whole scene once per page across the eight ticks a
//     frame spans (§8.2): anything read live would tear the strip across a page boundary. Every value below was
//     CLASSIFIED at the freeze — clamped to the digits the panel draws, and the home age BUCKETED to its token — so
//     this function makes no display decision at all beyond where to put the pixels.
//
// ★★ AND THE COORDINATES LIVE IN ONE TABLE (§3.1: *"the exact `x` coordinates belong to one layout table in the
//    renderer; they must not be repeated at individual draw sites"*). A slot repeated at its draw site is how a strip
//    acquires a second, drifting layout the moment one field's width changes.
struct StripSlot {
    int16_t icon_x;      // left edge of the slot's glyph
    int16_t text_x;      // left edge of its token — `kNoToken` for a slot that draws no text
    int16_t right;       // ★ the slot's frozen RIGHT EDGE at its WIDEST token: what the probe pins, and what makes
};                       //   "the battery cannot push an earlier icon out of budget" a measurement (§3.1's budget).
constexpr int16_t kNoToken = -1;
enum class Strip : uint8_t { mail = 0, home, team, key, duty, batt, count };
// ⓘ THE ARITHMETIC BEHIND THE TABLE, so a future edit re-derives it instead of nudging numbers: `Font::small` is
//   6 px/column and every glyph but the battery is 7 px wide (`icons::kIconW`); the battery outline is 11
//   (`kBatteryW`). Widest tokens: mail `99+` and home `59m`/`old` = 3 columns, team `9+` = 2, battery `4.1V` = 4.
// ★★ AMENDED BY §CHROME-5 (design §3.1's 2026-08-23 amendment, which is the AUTHORITY for these numbers): a SIXTH
//    slot — the 7x7 duty gauge at x = 83..89. The strip was exactly full at the §CHROME-3 values, so the 2/3/5-px
//    reserves shrink to ONE PIXEL between every pair and home/people/key each move LEFT; ⛔ the battery is untouched,
//    glyph and token both, because its right-anchoring is what §3.1's own justification rests on (see below).
//     mail 0..25 · home 27..52 · team 54..73 · key 75..81 · duty 83..89 · battery 91..127
//   = 26 + 1 + 26 + 1 + 20 + 1 + 7 + 1 + 7 + 1 + 37 = 128 px exactly, with the battery's last column still on x = 127.
// ⛔⛔ WITHDRAWN, KEPT VISIBLE (§3 rule 3): the line above read *"mail 0..25 · gap · home 28..53 · gap · team 56..75 ·
//    gap · key 79..85 · gap · battery 91..127 = 26 + 2 + 26 + 2 + 20 + 3 + 7 + 5 + 37"*. True until the sixth slot
//    landed; the five earlier right edges are what paid for it.
// ★ THE BATTERY SLOT IS ANCHORED TO THE RIGHT EDGE (§3.1: *"battery is right-aligned so `--` and `4.1V` do not move
//   the preceding icons"*) — and being a FIXED slot is what delivers that: its icon and its token sit at constant x
//   whatever the token's width, so a `--` leaves the trailing columns empty instead of dragging the strip. ⛔ A
//   flowed layout that packed each field after the previous one would satisfy the sentence and break the picture the
//   moment the mail count reached three digits.
constexpr StripSlot kStrip[uint8_t(Strip::count)] = {
    /* mail */ {  0,       8,  25 },
    /* home */ { 27,      35,  52 },
    /* team */ { 54,      62,  73 },
    /* key  */ { 75, kNoToken, 81 },
    /* duty */ { 83, kNoToken, 89 },
    /* batt */ { 91,     104, 127 },
};
constexpr const StripSlot& slot(Strip f) { return kStrip[uint8_t(f)]; }
// ★★ THE TABLE CHECKS ITSELF AT BUILD TIME, which is what makes `right` a LOAD-BEARING field rather than a comment in
//    struct form: the three ways a future edit can break §3.1's frozen geometry — a slot that leaves the panel, a slot
//    that overlaps its neighbour, and a token column that starts inside its own glyph — are all decidable here, and a
//    panel is the worst place to discover any of them. ⓘ The `right` values are each slot's extent at its WIDEST
//    token (`99+`, `59m`, `9+`, `4.1V`); the probe pins the same numbers independently, from the drawn coordinates.
constexpr bool strip_slots_fit() {
    for (uint8_t i = 0; i < uint8_t(Strip::count); ++i) {
        if (kStrip[i].icon_x < 0 || kStrip[i].right > 127) return false;
        if (kStrip[i].text_x != kNoToken && kStrip[i].text_x <= kStrip[i].icon_x) return false;
        if (i > 0 && kStrip[i].icon_x <= kStrip[i - 1].right) return false;
    }
    return true;
}
static_assert(strip_slots_fit(),
              "design §3.1: a status-strip slot overlaps its neighbour, starts its token inside its own glyph, "
              "or runs past x=127");
constexpr int kStripIconY = 0;    // §3.1: icons occupy y = 0..6, inside the y = 0..8 strip; the rule stays at y = 9
// The widest token any slot draws is 4 columns (`4.1V`); `kVoltsTokenCap` is that plus its NUL and is the largest of
// the four caps the chrome header declares, so one buffer serves every slot (U1 — not four near-identical ones).
constexpr size_t kTokenCap = mrui::kVoltsTokenCap;

// §4.2's four icons, plus `blank` = NO GLYPH AT ALL. ⛔ `nullptr` is the DRAWN-NOTHING answer and not an error path:
// on a build with no mobile plane the slot is empty, because a crossed house there would be a claim about a plane
// this firmware does not run. `switch` without `default:` so a fifth state fails the build (-Werror=switch).
const uint8_t* home_glyph(mrui::HomeIcon h) {
    switch (h) {
        case mrui::HomeIcon::blank:     return nullptr;
        case mrui::HomeIcon::unknown:   return mrui::icons::kIconHomeUnknown;
        case mrui::HomeIcon::confirmed: return mrui::icons::kIconHomeConfirmed;
        case mrui::HomeIcon::checking:  return mrui::icons::kIconHomeChecking;
        case mrui::HomeIcon::lost:      return mrui::icons::kIconHomeLost;
    }
    return nullptr;   // -Wreturn-type only; -Wswitch covers the enum
}
// §4.4's three key states — and `blank` is again the ABSENCE of a glyph, not a third picture: with no team
// configured the content key is IRRELEVANT, which is a different statement from "missing" (the crossed key).
const uint8_t* key_glyph(mrui::KeyIcon k) {
    switch (k) {
        case mrui::KeyIcon::blank:   return nullptr;
        case mrui::KeyIcon::absent:  return mrui::icons::kIconKeyCrossed;
        case mrui::KeyIcon::present: return mrui::icons::kIconKey;
    }
    return nullptr;   // -Wreturn-type only
}

// §CHROME-5's three pictures — and the FILL family is INDEXED rather than dispatched, which is what keeps the step
// count DERIVED from the artwork (`icons::kDutyFillLevels`) instead of re-typed as six near-identical arms here.
// ⛔ `default`-less like its neighbours: a ninth gauge state must fail the build rather than render a wrong level.
// ⚠ Unlike home and key there is NO `blank` state: every build has a radio and therefore a duty answer — `disabled`
//   (no duty limit configured) is a PICTURE of its own, ⛔ never the absence of one.
const uint8_t* duty_glyph(mrui::DutyGauge d) {
    switch (d) {
        case mrui::DutyGauge::disabled: return mrui::icons::kIconDutyDisabled;
        case mrui::DutyGauge::blocked:  return mrui::icons::kIconDutyBlocked;
        // The fill family falls through to the ONE indexed draw below (U1).
        case mrui::DutyGauge::fill_0: case mrui::DutyGauge::fill_1: case mrui::DutyGauge::fill_2:
        case mrui::DutyGauge::fill_3: case mrui::DutyGauge::fill_4: case mrui::DutyGauge::fill_5: break;
    }
    return mrui::icons::kIconDutyFill[mrui::ui_duty_fill_level(d)];
}

void draw_strip_icon(const StripSlot& sl, const uint8_t* bits) {
    mrui::draw_bitmap(sl.icon_x, kStripIconY, mrui::icons::kIconW, mrui::icons::kIconH, bits);
}

void draw_status_strip(const mrui::UiChrome& c) {
    char tok[kTokenCap];
    // ---- [mail][count] (§4.1) — always present: `0` is a fact, and the envelope is what says which fact it is.
    draw_strip_icon(slot(Strip::mail), mrui::icons::kIconMail);
    mrui::ui_fmt_mail(tok, sizeof tok, c.mail, c.mail_overflow);
    mrui::draw_text(slot(Strip::mail).text_x, kBarBaseline, tok);
    // ---- [home][age] (§4.2) — the token was bucketed from the 64-bit age AT THE FREEZE and is drawn verbatim.
    // ⛔ It is a CONFIRMATION age and is never labelled or read as "connected" (design §4.2, node.h's own rule).
    if (const uint8_t* home = home_glyph(c.home)) {
        draw_strip_icon(slot(Strip::home), home);
        mrui::draw_text(slot(Strip::home).text_x, kBarBaseline, c.home_age);
    }
    // ---- [people][count] (§4.3) — teammates HEARD/KNOWN. The icon stays put with no team configured and the token
    //      reads `--`: "no team" and "a team with no teammate heard" are different answers and both are drawn.
    draw_strip_icon(slot(Strip::team), mrui::icons::kIconPeople);
    mrui::ui_fmt_team(tok, sizeof tok, c.team_configured, c.team_count, c.team_overflow);
    mrui::draw_text(slot(Strip::team).text_x, kBarBaseline, tok);
    // ---- [key] (§4.4) — the TEAM CHANNEL CONTENT key, never the node's own identity.
    if (const uint8_t* key = key_glyph(c.key)) draw_strip_icon(slot(Strip::key), key);
    // ---- [duty] (§CHROME-5) — the duty-utilization gauge, ⛔ ICON ONLY and never a percentage: the exact figure and
    //      the recovery time live in the `duty` console verb and the companion diagnostics. The bucket was classified
    //      at the freeze (`ui_duty_bucket`), so this slot makes no display decision either — crossed = no duty limit,
    //      empty-to-full = utilization, full + warning mark = 100 %, i.e. transmission is duty-blocked right now.
    draw_strip_icon(slot(Strip::duty), duty_glyph(c.duty));
    // ---- [battery][voltage] (§4.5) — the outline is UNFILLED and stays that way: a fill level implies a chemistry
    //      and a discharge curve nobody has approved. `--` until a reading succeeds, and never a plausible guess.
    mrui::draw_bitmap(slot(Strip::batt).icon_x, kStripIconY, mrui::icons::kBatteryW, mrui::icons::kBatteryH,
                      mrui::icons::kIconBattery);
    mrui::ui_fmt_batt(tok, sizeof tok, c.batt_dv);
    mrui::draw_text(slot(Strip::batt).text_x, kBarBaseline, tok);
    mrui::draw_hline(0, kBarRuleY, 128);
}

// ======================================================================= §CHROME-4 / design §3.2 — THE NAVIGATION RAIL
//
// ★★★ WHAT IT ANSWERS, and why it is a second region rather than more text: the strip says *"what is happening?"*, the
//     rail says *"where am I?"* (design §1). It replaces the label-only screen titles §7.2 removes, and — because it
//     is CHROME — it carries §6's configuration badge from every ordinary screen instead of only from STATUS.
//
// ★★ ITS GEOMETRY IS ONE TABLE, exactly as §3.1 requires of the strip: `x = 0..9`, `y = 10..59`, five 10-px slots
//    aligned to the five body baselines (19, 29, 39, 49, 59 — slot `i` spans `10 + 10i` .. `19 + 10i`, so its bottom
//    row IS its body row's baseline).
// ★★★★ AND THE SLOT'S y IS A FUNCTION OF THE **ENUMERATOR**, NOT OF A RUNNING COUNTER. That is what makes §3.2's
//      *"builds where TEAM/SEND are unavailable do not draw misleading dead icons. Their canonical slots remain empty;
//      the remaining icons keep the same locations rather than acquiring a second layout"* structural rather than
//      careful: an unavailable slot is `continue`d, and nothing below it can move because nothing below it is
//      positioned relative to it. ⛔ Never pack these consecutively.
constexpr int kRailX      = 0;
constexpr int kRailW      = 10;
constexpr int kRailY0     = 10;    // immediately under the y = 9 rule
constexpr int kRailDy     = 10;
constexpr int kRailH      = 10;
constexpr int kRailSlots  = 5;
constexpr int kRailIconDx = 1;     // (10 - 7) / 2, so the glyph clears the selection frame on both sides
constexpr int kRailIconDy = 1;     //   ...and its 7 rows sit inside the slot's 10 without touching the frame
static_assert(kRailY0 + (kRailSlots - 1) * kRailDy + kRailH - 1 == 59,
              "design §3.2: the rail's five slots must span y = 10..59, aligned to the five body baselines");
static_assert(kRailX + kRailW <= kBodyX, "design §3.2: the rail must not reach into the 116-px body");
// ★★★ W4b (design §6.1 rule 4) — THE MENU-MODE CUE: a 2-px bar in the GUTTER between the rail and the body, one rail
//     slot high, beside the boxed slot. ⛔ Never inside the selection box and ⛔ never in the body.
constexpr int kCueX = kRailX + kRailW;   // x = 10..11
constexpr int kCueW = 2;
static_assert(kCueX + kCueW <= kBodyX, "design §6.1 rule 4: the menu cue lives in the 2-px gutter, never in the body");
// The slot index of a `NavSlot`. ⛔ `none` never reaches here — the loop iterates the five real slots.
constexpr int rail_slot_y(int index) { return kRailY0 + index * kRailDy; }

// §6's badge, as ONE bitmap per state rather than a gear plus an overlay sprite (see firmware_ui_icons.h for why).
// ⛔ `default`-less: a fifth badge state must fail the build here rather than silently render the clean gear, which
//    is precisely the "icon-only configuration error" §13 refuses to ship.
const uint8_t* rail_badge_glyph(mrui::CfgBadge b) {
    switch (b) {
        case mrui::CfgBadge::clean:    return mrui::icons::kIconSettings;
        case mrui::CfgBadge::restart:  return mrui::icons::kIconSettingsRestart;
        case mrui::CfgBadge::unsaved:  return mrui::icons::kIconSettingsUnsaved;
        case mrui::CfgBadge::conflict: return mrui::icons::kIconSettingsConflict;
    }
    return mrui::icons::kIconSettings;   // -Wreturn-type only; -Wswitch covers the enum
}

// §3.2's icon table. ⓘ TEAM reuses the strip's people glyph and INBOX its envelope (U1 — firmware_ui_icons.h declares
// one of each on purpose); only STATUS, SEND and SETTINGS have their own.
const uint8_t* rail_glyph(mrui::NavSlot s, mrui::CfgBadge badge) {
    switch (s) {
        case mrui::NavSlot::status:   return mrui::icons::kIconStatus;
        case mrui::NavSlot::team:     return mrui::icons::kIconPeople;
        case mrui::NavSlot::inbox:    return mrui::icons::kIconMail;
        case mrui::NavSlot::send:     return mrui::icons::kIconSend;
        case mrui::NavSlot::settings: return rail_badge_glyph(badge);
        // ⛔ Not a slot. It is listed rather than defaulted so a sixth REAL slot fails the build here.
        case mrui::NavSlot::none:     return nullptr;
    }
    return nullptr;   // -Wreturn-type only
}

// ★★★ IT CONSUMES THE **FROZEN CHROME** AND NOTHING ELSE, for the reason the strip does (§8.2): U8g2 replays this
//     whole scene once per page, so a selection read live would move under an open frame. ⛔ There is NO
//     renderer-local cursor here and never may be — `c.nav` is §5.2's one pure mapping, already frozen (§5.1: *"the
//     selection frame follows the frozen `UiState::screen`, never a renderer-local cursor"*).
// ⛔⛔ EMERGENCY DRAWS NO RAIL AT ALL (§5.3). `c.rail_visible` was frozen from `ui_rail_visible(emergency)`, so this
//     is one test and no rail draw call is issued — the body then keeps `x = 0` and all 128 px, which is what stops
//     a `Font::large` distress headline being clipped.
// ⓘ A selected slot that this build does not draw yields NO frame rather than a frame around nothing: the mask is
//   checked first, which is C2's fail-closed direction.
void draw_rail(const mrui::UiChrome& c) {
    if (!c.rail_visible) return;
    for (int i = 0; i < kRailSlots; ++i) {
        const mrui::NavSlot s = mrui::NavSlot(i + 1);          // §3.2's order: STATUS, TEAM, INBOX, SEND, SETTINGS
        if ((c.slots & mrui::slot_bit(s)) == 0) continue;      // unavailable: EMPTY, and nothing else moves
        const int y = rail_slot_y(i);
        mrui::draw_bitmap(kRailX + kRailIconDx, y + kRailIconDy,
                          mrui::icons::kIconW, mrui::icons::kIconH, rail_glyph(s, c.badge));
        // §3.2: "the active icon has a one-pixel rectangular frame around its slot" — an OUTLINE, never a filled box.
        if (c.nav == s) mrui::draw_rect(kRailX, y, kRailW, kRailH);
        // ★ W4b: the cue is its OWN statement, ⛔ never folded into the box's (tools/probe_board_ui's W41 reads that
        //   statement verbatim), and it follows the SAME slot: exactly one x0/w10 box stays, menu mode adds the bar.
        if (c.nav == s && c.menu_cue) mrui::draw_rect(kCueX, y, kCueW, kRailH);
    }
}

// ★★★★ §CHROME-4, design §6 and §7.2 — WHAT LEFT THIS SCREEN, AND WHAT DELIBERATELY DID NOT.
//   ⛔ GONE: the standalone `STATUS` title (§7.2: *"remove the standalone STATUS title"* — the rail's boxed STATUS
//      icon says it, and a label-only heading costs a whole row of a five-row body), and with it the
//      `CFG* UNSAVED` / `CFG! RELOAD` DECORATION (§6: *"the redundant … decoration is removed from the STATUS
//      title. The rail makes the state visible from every ordinary screen"*).
//   ★★ WHERE THAT FACT WENT: the SETTINGS rail icon's CONFIGURATION BADGE (`rail_badge_glyph`), which is chrome and
//      is therefore visible from EVERY ordinary screen rather than only from this one — strictly more coverage than
//      the line it replaces. ⛔ AND IT REPLACES ONLY THE DECORATION: `draw_settings_screen` still renders the
//      ACTIONABLE text (`CFG* UNSAVED` / `CFG! RELOAD` / `RESTART NEEDED`), because §6 says in as many words that one
//      small icon cannot replace an instruction.
//   ⛔⛔ STAYING: `RESTART NEEDED` on the last body row. §6 removes the TITLE decoration and names nothing else; this
//      row is a body statement of a durable fact (§3.6.5: it stays visible until the reboot) and deleting it would be
//      exactly the §6.1 over-deletion that amendment exists to prevent.
// ★ THE ROW FREED BY THE TITLE goes to the identity, which no longer fits one 19-column line: `me T255` +
//   `team ffffffff` is 22 columns and would have had to be clamped. Two rows, both complete — §7.1 rule 5's
//   *"moved to a second row"* rather than a truncation.
// ⓘ The badge is silent until the operator has actually opened SETTINGS: a draft cannot exist before then, and the
//   service is not open, so `freeze_settings` reports all three false. Nothing is claimed about a config nobody edited.
//
// ★★★★ §UI-17 S3 — THE BODY IS NOW A **PLACEMENT**, AND EVERY BYTE IT PLACES COMES FROM `firmware_ui_status.h`.
//      §B115's rule (`firmware_ui_model.h:102-104`): *a string built in `firmware_ui.cpp` is a string no automated
//      gate can read* — this TU is compiled by neither the native suite nor the simulator. The five rows, their nine
//      substitutions and row 4's priority are therefore PURE, driven by `test/test_firmware_ui_status.cpp` and
//      attacked by `--target=uistatus`. ⛔ Nothing below may grow a condition: a decision written here is a decision
//      no battery can redden.
//
// ⚠⚠ WHAT LEFT THIS BODY WITH S3, INVENTORIED — OWNER-ACCEPTED 2026-08-20 (spec §2.3), ⛔ NOT AN OVERSIGHT AND
//    ⛔ NOT RESTORABLE BY A LATER SLICE ADDING A ROW:
//      · `DM %u, newest %s` / `CH %u, newest %s` — the PER-KIND unread counts and the PER-KIND newest-message ages,
//        two rows, replaced by one combined `3 NEW`. The per-kind SPLIT survives on the INBOX screen (its empty
//        state prints both, and every populated row carries its own kind + age); the COMBINED count is also the
//        strip's envelope. ⚠ The per-kind NEWEST AGE is shown nowhere else while the list is non-empty — stated,
//        not glossed. It remains on the console.
//      · `batt %ldmV` — the EXACT millivolts. The strip's `4.1V` decivolt token (`ui_fmt_batt`) and the console keep
//        the reading; the exact mV leaves the panel.
//      · `batt --` — dropped with it; the strip renders `--` for the same state by the same rule.
//    KEPT: `RESTART NEEDED` (row 4, and it OWNS the row while it stands — design §3.6.5, spec §2.2 note g) and the
//    team id / team-local id, uppercased, on rows 0-1.
// ⛔ AND STILL GONE, BY RULING (§9 R-3 / §CHROME-4 / design §6): the `CFG* UNSAVED` / `CFG! RELOAD` text. The
//    SETTINGS rail BADGE carries that state from every screen and SETTINGS says the words. `RESTART NEEDED` is the
//    only configuration text this body may draw.
//
// ★★★★ W4b — HOME (design §6.2–§6.7): every byte is composed by the pure formatters in `firmware_ui_status.h`
//      (§B115); this function only PLACES them, from the FROZEN copies (`draw_frame` runs once per OLED page).
//      ⓘ CORRECTED 2026-09-27 (V1): the §CHROME-4 and §UI-17 S3 paragraphs above describe the STATUS body Home
//      REPLACED and are kept as its history. Still true on Home: no title and no `CFG*` decoration (R-3 — the probe's
//      C84), and `RESTART NEEDED` stays (Home's row 2 now, not "the last body row" — C35). No longer true: the two
//      identity rows at x = 40, row 4's position/restart priority (the position moved to My device) and the
//      `uistatus` S-rows those paragraphs cite (W4b retired S01/S05–S12 for S14–S28).
// §7.3 AUDIT (widest reachable expansion), ONE budget — x = 12, 19 columns:
//   row 0  `ME ABCDEFGHIJKLMNO»`                                        19
//   row 1  `TEAM 12A1B2C3 NO ID`                                        19
//   rows 2-4 (3-4 under `RESTART NEEDED`)  `>NO TEAM KEY - HELP`       19   (`OPTIONS CHANGED` 1 + 15)
//   My device  `ABCDEFGHIJKLMNOPQRS` / `ID 0x12AB34CD` / `-89.123,-179.123` / `>BACK`     19 / 13 / 16 / 5
//   key help   `A MEMBER WHO HAS IT`                                    19
//   setup note `RELOAD OR DISCARD` / `IN SETTINGS`                      17 / 11
// ★★★ W7/W8 (design §5.3) — THE EDITOR's BODY, from the FROZEN descriptor and window ONLY: ⛔ no live draft, no live
//     name. Row 0 is the caller and `used/cap` (a note owns the row alone, r2.27), rows 1–2 the two grid rows, rows
//     3–4 the ring; E4 shows `DISCARD DRAFT?` over the first 19 bytes. ★ THE CURSOR is a 6x1 underline one pixel below
//     its cell's baseline: `draw_hline(12 + 6 x column, baseline + 1, 6)` on the visible row.
constexpr int kEditorCellPx = 6;   // the small font's column — the body's 19 columns are 6-px cells
static_assert(kBodyCols * kEditorCellPx <= kBodyPx, "design §5.3: the editor's 19 cells fit the body");
static_assert(kBodyCols == mrui::kEditorCols, "design §5.3: the editor's grid is the body's 19 columns");
void draw_editor(const mrui::UiState& st, const char* caller) {
    char l[kLineCap], r4[kLineCap];
    const mrui::EditorView& e = st.editor;
    if (e.phase == mrui::EditorPhase::discard) {
        body_text(0, mrui::kEditorDiscardHead);
        if (st.editor_line[0][0]) body_text(1, st.editor_line[0]);
    } else {
        mrui::editor_header_line(l, sizeof l, caller, e.used, e.cap, mrui::editor_note_text(e.note));
        body_text(0, l);
        for (uint8_t r = 0; r < 2; ++r) if (st.editor_line[r][0]) body_text(1 + r, st.editor_line[r]);
        mrui::draw_hline(kBodyX + kEditorCellPx * int(e.cursor_col), body_y(1 + int(e.cursor_row)) + 1, kEditorCellPx);
    }
    mrui::editor_ring_rows(l, sizeof l, r4, sizeof r4, e);
    if (l[0])  body_text(3, l);
    if (r4[0]) body_text(4, r4);
}
// ★★★ W7 (design §4.3) — THE NAME FLOW's BODY over My device or the prompt: the editor, `SAVE NAME?` (the name on
//     rows 1–2, `WAS` on row 3, ` SAVE >EDIT` on row 4) and the result. Answers false while the flow is closed.
bool draw_name_flow(const mrui::UiState& st) {
    char l[kLineCap];
    switch (st.editor.phase) {
        case mrui::EditorPhase::groups:
        case mrui::EditorPhase::chars:
        case mrui::EditorPhase::controls:
        case mrui::EditorPhase::discard:
            draw_editor(st, mrui::kEditorNameCaller);
            return true;
        case mrui::EditorPhase::name_review:
        case mrui::EditorPhase::name_requested:
        case mrui::EditorPhase::name_taken:
            body_text(0, mrui::kSaveNameHead);
            for (uint8_t r = 0; r < 2; ++r) if (st.review_line[r][0]) body_text(1 + r, st.review_line[r]);
            body_text(3, st.review_header);
            mrui::name_review_action_line(l, sizeof l, st.editor.primary);
            body_text(4, l);
            return true;
        case mrui::EditorPhase::name_result: {
            body_text(1, mrui::name_result_head(st.editor.result));
            const char* why = mrui::name_result_reason(st.editor.result);
            if (why) body_text(2, why);
            body_text(4, "press = back");
            return true;
        }
        case mrui::EditorPhase::closed:
        case mrui::EditorPhase::bind:
        case mrui::EditorPhase::relabel:
        case mrui::EditorPhase::message_review:
        case mrui::EditorPhase::message_result: return false;
    }
    return false;
}

void draw_home_screen(const mrui::UiState& st, const mrui::UiSnapshot& s, const SettingsView& c) {
    char l[kLineCap];
    switch (st.home_view) {
        case mrui::HomeView::my_device: {
            if (draw_name_flow(st)) return;   // ★ W7: the name flow owns the body while it is up
            // The full name over two rows (counted, sanitized, ⛔ never an abbreviation split), the stable identity,
            // the position (UI-17 S-9/S-10's row without its restart arm — restart is Home's row 2) and `>BACK`.
            char r1[kLineCap];
            mrui::ui_my_device_name_rows(l, sizeof l, r1, sizeof r1, s);
            body_text(0, l);
            if (r1[0]) body_text(1, r1);
            mrui::ui_my_device_id(l, sizeof l, s);
            body_text(2, l);
            mrui::ui_status_location(l, sizeof l, /*reboot_required=*/false, s);   // ⛔ the FROZEN snapshot, never live
            body_text(3, l);
            mrui::my_device_action_line(l, sizeof l, st.editor.primary);          // ★ W7: ` CHANGE NAME >BACK` (§6.7)
            body_text(4, l);
            return;
        }
        case mrui::HomeView::name_prompt:   // ★ W7 (§4.4): `NO NAME SET` / ` SET NAME` / `>SKIP`
            if (draw_name_flow(st)) return;
            body_text(0, mrui::kNoNameSetText);
            mrui::name_prompt_row(l, sizeof l, /*set_row=*/true, st.editor.primary);
            body_text(1, l);
            mrui::name_prompt_row(l, sizeof l, /*set_row=*/false, st.editor.primary);
            body_text(2, l);
            return;
        case mrui::HomeView::key_help:
            for (int row = 0; row < kBodyRows; ++row) body_text(row, mrui::kKeyHelpRows[row]);
            return;
        case mrui::HomeView::setup_block: {
            // The FROZEN reason on row 1, `IN SETTINGS` on row 2 when Settings can resolve it — no arrow (§6.6 rule 4).
            body_text(1, mrui::prov_block_note(st.prov_block));
            const char* r2 = mrui::ui_setup_block_row2(st.prov_block);
            if (r2[0]) body_text(2, r2);
            return;
        }
        case mrui::HomeView::list: break;
    }
    mrui::ui_home_me_line(l, sizeof l, s);
    body_text(0, l);
    mrui::ui_home_team_line(l, sizeof l, s);
    if (l[0]) body_text(1, l);   // ⛔ a blank row (no team plane) draws NOTHING, never an empty text record
    // ★ `RESTART NEEDED` owns row 2 while set — not selectable, no new NV read (the frozen `SettingsView`) — and the
    //   list window shrinks to rows 3-4 (design §6.2). ⓘ The W9 card's rows are a FUTURE seam: nothing is reserved.
    uint8_t top = 2;
    if (c.reboot) { body_text(2, mrui::kCfgRestartText); top = 3; }
    const uint8_t rows = uint8_t(kBodyRows - top);
    const mrui::HomeCapture& h = st.home;
    const uint8_t sel = mrui::home_index_of(h, h.selected);   // the arrow follows the ITEM; its row is derived
    const bool focus = (st.list_view == mrui::ListView::interactive);   // ⛔ a menu-mode PREVIEW draws no arrow
    const uint8_t first = list_first(uint8_t(sel < h.count ? sel : 0), h.count, rows);
    for (uint8_t row = 0; row < rows && first + row < h.count; ++row) {
        const uint8_t idx = uint8_t(first + row);
        char label[kLineCap];
        if (idx == 0 && focus && h.changed) snprintf(label, sizeof label, "%s", mrui::kHomeOptionsChangedText);
        else                                 mrui::ui_home_item_label(label, sizeof label, h.items[idx], s);
        mrui::ui_home_row(l, sizeof l, focus && idx == sel, label);
        body_text(uint8_t(top + row), l);
    }
}

// ★★★★ §UI-17 S1 — THE SCREEN IS EITHER A PASSIVE PREVIEW OR AN ENTERED LIST, and this one predicate is what says
//      which (the model's `screen_is_entered`, ⛔ never re-derived here). PASSIVE: the rows are listed with NO marker
//      anywhere and NO `BACK` row, because nothing has been picked and `short` passes the screen in one press.
//      ENTERED: the marker is back, and the list carries one more row than the snapshot published — `BACK`.
//      ⓘ W4b (V1): that exit row reads `MENU` now (`body_menu_row` — menu mode on the Home slot, design §6.1); the
//        `BACK` in this and INBOX's §UI-17 notes names the same row. PASSIVE is the menu-mode preview.
void draw_team_screen(const mrui::UiState& st, const mrui::UiSnapshot& s) {
    const bool entered = mrui::screen_is_entered(st.screen, st.settings, st.list_view);
    if (s.team_shown == 0) {
        body_text(0, "TEAM");
        body_text(1, "no teammates heard");
        // ⓘ AN EMPTY ROSTER STILL OFFERS THE WAY OUT (spec S1 pin 5), on the row below its two lines: entering a list
        //   that could only be left by walking rows it does not have would be a dead end. There is exactly one row and
        //   it IS the selection, so the marker is unconditional — the same statement the SETTINGS entry row makes.
        if (entered) body_menu_row(2, true);
        return;
    }
    // ★★★ §B64 (owner-ruled 2026-08-05) — THE LOUD HALF OF THE REFUSAL, AND THE SUPPRESSED HIGHLIGHT IS THE OTHER HALF.
    //     The teammate the cursor was on has left the roster, so `UiModel::activate` refuses to send. C2 says a refusal
    //     must be sayable, so one row is RESERVED for the reason — the same way `draw_inbox_screen` reserves its header
    //     row — rather than overwriting a teammate.
    // ★ AND THE `>` MARKER GOES AWAY. Leaving it beside whatever now occupies that row would be the mis-send in DISPLAY
    //   form: the panel would name a target the model has already refused to use. The two must agree, always.
    const uint8_t rows  = st.team_pick_gone ? uint8_t(kBodyRows - 1) : uint8_t(kBodyRows);
    // ★ §UI-17 S1: the ENTERED list is one row longer than the roster — the `BACK` row — and it scrolls with the rest
    //   through the SAME window (`list_first`), so a full 8-teammate roster can still reach it.
    const uint8_t n     = entered ? uint8_t(s.team_shown + 1) : s.team_shown;
    const uint8_t first = list_first(st.cursor, n, rows);
    // ★ §UI-17 S5 — OUR OWN FIX, read ONCE per body from the FROZEN snapshot (⛔ never `g_node.config()` here: this
    //   function runs once per OLED PAGE, and a live read is exactly the tear S3 shipped and then cured).
    const mrui::GeoFix own = mrui::ui_geo_fix_of(s);
    for (uint8_t row = 0; row < rows && first + row < n; ++row) {
        const uint8_t idx = uint8_t(first + row);
        // ★ ONE marker predicate for both row kinds, and it keeps §B64's suppression EXACTLY as it was: while the
        //   refusal stands no row is highlighted, because a `>` beside a teammate the model has already refused to act
        //   on is the same mis-send in display form. The `entered` term is the new one — a passive preview marks
        //   nothing at all.
        const bool here = entered && !st.team_pick_gone && idx == st.cursor;
        // ⛔ THE LAST ROW IS RESOLVED BY `list_row_kind`, ⛔ never by a bare `idx == s.team_shown` here (§B66:
        //    position is not an identity) — the model's own resolver, so the row the panel draws and the row
        //    `activate` acts on cannot disagree.
        if (mrui::list_row_kind(idx, s.team_shown) == mrui::ListRow::back) { body_menu_row(row, here); continue; }
        // ★★★★ §UI-17 S4 — EVERY BYTE OF THE ROW COMES FROM `firmware_ui_team.h`, and this line places it.
        //      §B115: a string built in THIS TU is a string no automated gate can read. The format, the label
        //      clamp, the route-age token and the two reserved columns are pure, driven by
        //      `test/test_firmware_ui_team.cpp` and attacked by `--target=uiteam`.
        // §7.3 AUDIT: marker 1 + label 6 + space 1 + age 3 + space 1 + dist 4 + space 1 + dir 2 = 19 of 19, and the
        // arithmetic is static_asserted in that header rather than restated here.
        // ⛔ WHAT THIS LINE USED TO DRAW: `%c%-9.9s %4.4s %uh` — a NINE-column label and a HOP COUNT. Hops left the
        //    row BY RULING (spec §3.2 / §1.9 F-1): the new columns are distance and bearing and 19 columns hold no
        //    sixth field. ⇒ `TeamRow::hops` is now written and read by nothing, exactly as `score_q4` already was;
        //    deleting either is a REFACTOR and may not ride this slice (C1) — see the header's inventory.
        // ★★★★ §UI-17 S5 — THE DISTANCE AND DIRECTION COLUMNS S4 RESERVED ARE FILLED FROM HERE ON, and this line
        //      moved exactly one argument to do it: `own` is the FROZEN own fix (`ui_geo_fix_of`, hoisted above the
        //      loop — one conversion, U2). ⛔ The four-term rule, the 600 s freshness bound and both token tables are
        //      `firmware_ui_geo.h`'s; a condition spelled at THIS call site is a condition no battery can attack.
        char l[kLineCap];
        mrui::ui_team_row(l, sizeof l, here, s.team[idx], own);
        body_text(row, l);
    }
    // ⛔⛔ RE-DERIVED 2026-08-16 (§CHROME-4 / §7.3), AND THE WORDING CHANGE IS FORCED RATHER THAN CHOSEN. This row read
    //    `TEAMMATE GONE, repick` and its comment said *"21 characters exactly, so it cannot be clipped: the panel is
    //    21 columns"* — i.e. the string was SIZED TO THE OLD BODY. The rail leaves 19, and §7.1 rule 5 forbids
    //    letting the panel clip a refusal as a truncation policy, so the string had to lose two columns rather than
    //    its last two characters (`…, repi` would have been the clip). ★ BOTH HALVES OF §B64's ruling survive intact:
    //    the row still NAMES the fact (the teammate is gone) and still gives the remedy (pick another), and the `>`
    //    highlight is still suppressed. ⓘ 19 characters exactly, so it cannot be clipped at the new width either.
    // ★ §UI-17 S4 — AND "19 CHARACTERS EXACTLY" IS NOW A **PROOF**, NOT A COMMENT (spec S4's pin: *"the
    //   `TEAMMATE GONE, pick` refusal row still fits"*). The literal is named ONCE and its width is asserted at
    //   compile time against the body's own column count, so a re-wording that overran the panel would fail the
    //   build rather than be clipped on glass. ⓘ The other half of §B64's ruling — the SUPPRESSED `>` — is the
    //   `here` predicate above, which this slice did not touch and which `test/test_firmware_ui_model.cpp`'s
    //   `team_pick_gone` cases and the renderer's own C107 control both hold.
    static constexpr char kTeamGoneText[] = "TEAMMATE GONE, pick";
    static_assert(sizeof kTeamGoneText - 1 == kBodyCols,
                  "§7.1 rule 5: the TEAM refusal must FIT the body, never be clipped as a truncation policy");
    if (st.team_pick_gone) body_text(kBodyRows - 1, kTeamGoneText);
}

// ★ UI-7: the real rows (spec §6.1). BLOCK ORDER — every DM row, then every channel row — never chronological: the
//   two seq spaces are independent and there is no shared clock to interleave on, so an interleaved list would be an
//   ordering claim the data does not support.
// ★ TRUNCATION IS STATED, never implied: a screen showing 8 of 40 says so rather than presenting the cap as the
//   whole mailbox (the same rule the TEAM screen's `T4/12` follows).
// ⛔ CORRECTED 2026-08-30 (§CUSTODY-C): this line used to read *"`inbox_total` is what `pull` VISITED"*, which is
//   exactly what design §7.4 now forbids. `inbox_total` is the count of records ADMITTED TO THE ORDINARY VIEW —
//   `pull` still visits the internal outcome records, and they are subtracted before the budget, so a mailbox
//   holding 3 DMs and 5 E2E receipts reads `INBOX 3/3`, never `3/8`. The denominator remains an honest mailbox
//   size for what this screen is a view OF; the hidden records are reached through `pull_inbox` (inbox.h).
// ★★★★ §UI-17 S1 — the same PASSIVE ↔ ENTERED split as `draw_team_screen`'s, one plane over. See it.
void draw_inbox_screen(const mrui::UiState& st, const mrui::UiSnapshot& s) {
    char l[kLineCap], age[kAgeCap];
    const bool entered = mrui::screen_is_entered(st.screen, st.settings, st.list_view);
    if (s.inbox_shown == 0) {
        body_text(0, "INBOX");
        // ⚠ NOT "no messages": an inbox with no durable store installed (`Inbox::enabled()` false ⇒ `pull` returns 0)
        //   is indistinguishable here from an empty one, and the unread counters below are the honest thing we DO
        //   know. Claiming emptiness would be a statement we cannot support.
        fmt_age(age, sizeof age, s.last_dm_age_s);
        snprintf(l, sizeof l, "DM %u  newest %s", unsigned(s.unread_dm), age);
        body_text(1, l);
        fmt_age(age, sizeof age, s.last_ch_age_s);
        snprintf(l, sizeof l, "CH %u  newest %s", unsigned(s.unread_ch), age);
        body_text(2, l);
        // ⓘ Row 3 is the one the layout already leaves free between the counters and `no stored rows` — see the TEAM
        //   screen's own note for why an empty list still offers `BACK`.
        if (entered) body_menu_row(3, true);
        body_text(4, "no stored rows");
        return;
    }
    snprintf(l, sizeof l, "INBOX %u/%u", unsigned(s.inbox_shown), unsigned(s.inbox_total));
    body_text(0, l);
    // ★★★ §UI-7D slice B — THE LOUD HALF OF THE ACTIVATION REFUSAL, and the suppressed highlight is the other half. It
    //     is §B64's TEAM treatment applied to the record identity: the selected `(kind, seq)` is no longer in the store,
    //     so `UiModel::activate` refused to open (and therefore refused to put a DELETE two presses from) whatever now
    //     occupies that row. One body row is RESERVED for the reason rather than overwriting a message, and the `>`
    //     marker goes away — a highlight beside a record the model has already refused to act on is the same wrong in
    //     display form. The two must agree, always.
    const uint8_t rows  = st.inbox_pick_gone ? uint8_t(kBodyRows - 2) : uint8_t(kBodyRows - 1);
    // ★ §UI-17 S1: the ENTERED list carries the `BACK` row too, and ⚠ THE COST IS STATED RATHER THAN DISCOVERED ON
    //   GLASS: row 0 is the header and one more row is RESERVED for `MESSAGE GONE`, so an interactive list showing a
    //   refusal has at most TWO message rows. The scrolling window already handles it (`list_first`).
    const uint8_t n     = entered ? uint8_t(s.inbox_shown + 1) : s.inbox_shown;
    const uint8_t first = list_first(st.cursor, n, rows);
    for (uint8_t row = 0; row < rows && first + row < n; ++row) {
        const uint8_t idx = uint8_t(first + row);
        // ★ ONE marker predicate, §UI-7D's suppression kept exactly as it was, plus the new `entered` term — see
        //   `draw_team_screen`'s note.
        const bool here = entered && !st.inbox_pick_gone && idx == st.cursor;
        // ⛔ Resolved by `list_row_kind`, never positionally (§B66) — see `draw_team_screen`.
        if (mrui::list_row_kind(idx, s.inbox_shown) == mrui::ListRow::back) { body_menu_row(row + 1, here); continue; }
        const mrui::InboxRow& e = s.inbox[idx];
        char tag[6];
        // ⓘ §UI-7D: the tag comes from `kind`, the row's ONLY kind field. `is_dm` is gone from the tree.
        if (e.kind == mrui::InboxKind::dm) snprintf(tag, sizeof tag, "DM");
        else                               snprintf(tag, sizeof tag, "CH%u", unsigned(e.channel_id));
        fmt_age(age, sizeof age, e.rx_age_s);
        // §7.3 AUDIT: marker 1 + tag 5 + preview 8 + space 1 + age 4 = 19 of 19.
        // ⚠ THE TAG'S COLUMN IS **5**, NOT 3, AND THAT IS THE WHOLE CHANNEL ID: `CH255` is the widest tag a
        //   `uint8_t channel_id` can produce, and a `%-3.3s` would have rendered it `CH2` — a channel number that is
        //   not the record's. A tag must never be truncated INTO a different true-looking value.
        // ⚠ `e.text` is a 20-byte PREVIEW and `%-8.8s` bounds it explicitly; the whole body is one press away in the
        //   detail modal, which is where the record is actually read (§3.5). `%-9s` used to let a 20-column preview
        //   push the age off a 21-column panel.
        // ⓘ `%4.4s` is a BOUND THAT IS NEVER REACHED: `fmt_age` now emits at most 3 columns (`kAgeCap`), so the
        //   precision can never truncate a token — it is there so the row's width is provable from this line alone.
        snprintf(l, sizeof l, "%c%-5s%-8.8s %4.4s",
                 here ? '>' : ' ', tag, e.text, age);
        body_text(row + 1, l);
    }
    // Spec §3.5's own words for the refusal, on the last body row — the same place the TEAM screen puts its reason.
    if (st.inbox_pick_gone) body_text(kBodyRows - 1, "MESSAGE GONE");
}

// ★★★★ §UI-7D slice B — THE DETAIL MODAL (spec §3.5). It REPLACES the body, like the emergency overlay and the compose
//     sub-view, which is why `FrameGate::_fr_inbox` excludes it from the unread clear.
// ★★ EVERYTHING HERE IS FROZEN STATE. The 242-byte body buffer stays LIVE in the model and is never read from this
//    file: what a frame renders is the CURRENT PAGE, wrapped into two rows at the freeze, so the eight page transfers
//    of one frame cannot tear a body that the 2 s cadence turns underneath them (spec §5).
// ★ The header line is composed by the PURE formatter `mrui::inbox_detail_head`, where the native suite asserts its
//   visible bytes — the §B115 discipline: a string built in this TU is a string no automated gate can read.
void draw_inbox_detail(const mrui::UiState& st) {
    char l[kLineCap];
    // ⛔ TERMINAL `MESSAGE GONE` (the delete came back `not_found`): NO Delete action is offered, and the panel says
    //    plainly that nothing was removed by this press — the record was already absent. Either press returns to INBOX.
    if (st.detail == mrui::InboxModal::gone) {
        body_text(0, "MESSAGE GONE");
        body_text(1, "evicted or deleted");
        body_text(4, "press = back");
        return;
    }
    mrui::inbox_detail_head(l, sizeof l, st.detail_kind, st.detail_origin, st.detail_channel,
                            st.detail_page, st.detail_pages, st.detail_del_failed);
    body_text(0, l);
    for (uint8_t row = 0; row < mrui::kDetailBodyRows; ++row)
        body_text(row + 1, st.detail_line[row]);
    // ★ `back` FIRST and selected on entry, so deletion costs the deliberate short -> double (spec §3.5).
    snprintf(l, sizeof l, "%cback",   (st.detail_action == mrui::InboxAction::back) ? '>' : ' ');
    body_text(3, l);
    snprintf(l, sizeof l, "%cdelete", (st.detail_action == mrui::InboxAction::del)  ? '>' : ' ');
    body_text(4, l);
}

// ★★★★ §UI-14 — THE SETTINGS SCREEN (spec §3.6.2). Everything it reads is FROZEN: `st` is the model's copy, `c` is
//     the service's copy taken at the same instant, and the row LIST is rebuilt from those two frozen inputs through
//     the SAME pure builder the model bounds its cursor with (U1) — so the highlighted row and the row an activation
//     would act on cannot disagree by construction.
// ★★ WHAT EACH LINE SAYS, and why none of it is derived here:
//     · a VALUE row shows the DRAFT's value — never the effective one — because the draft is what SAVE would write;
//     · the value is BRACKETED while that row is being edited, so `short`'s two modes are distinguishable ON THE
//       PANEL and not only in the model;
//     · `RESTART NEEDED` is its own row, from `reboot_required()`, independent of the unsaved marker above it.
//
// ★★★★ §CHROME-4, design §7.2 AND §6 — THE TITLE IS GONE AND THE INSTRUCTION IS NOT. Two different things used to
//      share row 0, and only one of them left:
//        ⛔ GONE — the word `SETTINGS` (§7.2: *"remove the standalone SETTINGS title and use the gained row for the
//           menu"*). The rail's boxed SETTINGS icon names the screen, and the row is worth more to the menu: the list
//           is up to nine rows deep and only three of them fitted.
//        ⛔⛔ NOT GONE — `mrui::cfg_marker_text`. §6 is explicit that the badge *"may replace the STATUS decoration;
//           it may NEVER replace the instruction"*, and §6.1 says `CFG! RELOAD` REMAINS REQUIRED ACTIONABLE
//           SETTINGS/service text. ⇒ the marker keeps a row OF ITS OWN here, and the gained row goes to the menu
//           EXACTLY WHEN there is nothing to say: four menu rows while the configuration is clean, three while
//           `CFG* UNSAVED` / `CFG! RELOAD` stands. ⓘ That is the same conditional-reservation shape the TEAM and
//           INBOX screens already use for their refusals (`team_pick_gone` / `inbox_pick_gone`), not a second layout.
//
// §7.3 AUDIT (widest reachable expansion, in 19-column units) — and the arithmetic is PER ROW, because the widest
// VALUE belongs to the shortest LABEL and a label x value bound would be one no row can reach:
//   marker row      `CFG* UNSAVED` 12 · `CFG! RELOAD` 11
//   value browsing  `%c%-8s %s`  : `BLE` -> 8 + ` ` + `periodic` 8      = 18   (the widest)
//                                  `key attach` 10 + ` ` + `off` 3      = 14
//   value editing   `%c%-8s[%s]` : `BLE` -> 8 + `[periodic]` 10         = 19   (the widest)
//                                  `key attach` 10 + `[off]` 5          = 16
//   action row      `%c%s`       : `PROVISION`                          = 10
//   note / reboot   `RELOAD OR DISCARD` 17 · `SAVE OR DISCARD` 15 · `RESTART NEEDED` 14 · `CFG! RELOAD` 11
//   unavailable     `CFG UNAVAILABLE`                                   = 15
// ⛔ CORRECTED IN PLACE 2026-08-19 (§UI-15 slice 5, V1): the note row above used to read `PROVISION: UI-15` 16. That
//    string is GONE — §UI-15 slice 4 replaced the placeholder refusal with §4's TWO remedy cells (`ProvBlock`), whose
//    widths are the two now listed. The widest note is 17 of 19.
// ⚠ THE EDITING ARM LOST THE SPACE BEFORE ITS BRACKET rather than a column of the label, and that is deliberate: the
//   bracket is already the edit indicator, while truncating `key attach` to `key atta` would make two SELECTABLE rows
//   collide on their visible prefix — §7.1 rule 6's forbidden outcome.
// ★★★★ §UI-15 slice 5 — §3.6.3's SUB-VIEW, AND IT REPLACES THE BODY exactly as the compose modal and the inbox detail
//      do (it owns the press, so it must own the pixels: a menu whose gestures act on one list while the panel draws
//      another is the disagreement §B66 exists to prevent).
// ★★ THE RENDERER IS THIN BY REQUIREMENT, NOT BY TASTE: this TU is compiled by no automated gate, so every string and
//    every choice below comes from a PURE unit — the row list from `mrui::provision_rows` (the SAME builder the model
//    bounds its cursor with, U1/U2 — exactly as `draw_settings_screen` calls `settings_rows`), the labels from
//    `provision_row_label` / `prov_confirm_label`, the result's
//    two lines from `prov_result_head` / `prov_result_detail`, the id tokens from the chrome formatters. ⛔ Nothing
//    here decides anything.
// §7.3 AUDIT (widest reachable expansion, 19-column body):
//   menu row        `%c%s`         : `>JOIN NETWORK`                     = 13
//   confirm title   `CREATE NEW TEAM`                                    = 15
//   confirm note    `REPLACES A1B2C3`                                    = 15
//   confirm action  `%c%s`         : `>CREATE`                           = 7
//   result head     `CREATE REFUSED` 14 · `TEAM CREATED` 12 · `SAVE FAILED` 11 · `PHY DIFFERS` 11
//   result detail   `NOTHING CHANGED` 15 · `no_mobile_plane` 15 (the widest `prov_err_name`) · `USE SERIAL` 10
//   result id       `0x12A1B2C3` 10 · fingerprint `A1B2C3` 6
//   exit line       `press = back`                                       = 12
// §7.3 AUDIT, §UI-15 slice 6's four arms (same 19-column body; every figure DERIVED from the field's own widest value):
//   select row      `%c%s`         : `>` + a 12-byte label, or `PROFILE 4` = 13
//   select note     `NO JOIN SERVICE` 15 · `STORAGE FAILURE` 15 · `PROFILE STORE` 13 · `NO PROFILES` 11
//                   ...and its second row `CHECK faults` 12 · `INVALID` 7
//   confirm values  `L255 SF12 BW500.00` 18 · `1000.0000 MHz`            = 13
//   confirm action  `%c%s`         : `>JOIN`                             = 5
//   waiting head    `STILL JOINING` 13 · `JOINING`                       = 7
//   result head     `JOIN REFUSED` 12 · `ADOPTED` 7 · `SAVE FAILED` 11
//   result detail   `nv_load_failed` 14 (the widest `join_err_name`) · node line `node 255` = 8
void draw_provision_screen(const mrui::UiState& st, const mrui::UiSnapshot& s) {
    char l[kLineCap];
    switch (st.provisioning) {
        case mrui::Provision::menu: {
            const mrui::ProvRowList list =
                mrui::provision_rows(s.prov_create_team, s.prov_join_static, s.prov_join_team, s.prov_invite,
                                     s.prov_saved_keys);
            const uint8_t first = list_first(st.cursor, list.n, uint8_t(kBodyRows));
            for (uint8_t row = 0; row < kBodyRows && first + row < list.n; ++row) {
                mrui::ProvRow r{};
                if (!list.at(uint8_t(first + row), r)) break;
                snprintf(l, sizeof l, "%c%s", (first + row == st.cursor) ? '>' : ' ', mrui::provision_row_label(r));
                body_text(row, l);
            }
            return;
        }
        case mrui::Provision::create_confirm: {
            body_text(0, mrui::kProvCreateTitle);
            // Design §3.6.3: *"if already in a team, the screen says the current membership will be replaced"* — the
            // CONDITION is the formatter's (a `team_id` of 0 is the core's "not in a team"), never this file's.
            char note[mrui::kProvReplacesCap];
            if (mrui::ui_fmt_prov_replaces(note, sizeof note, s.team_id)) body_text(1, note);
            // ★ BACK FIRST and selected on entry, so CREATE costs the deliberate `short` -> `double` (§3.6.3) — the
            //   same shape and the same order as the inbox modal's back/delete pair.
            snprintf(l, sizeof l, "%c%s", (st.prov_confirm == mrui::ProvConfirm::back) ? '>' : ' ',
                     mrui::prov_confirm_label(mrui::ProvConfirm::back));
            body_text(3, l);
            snprintf(l, sizeof l, "%c%s", (st.prov_confirm == mrui::ProvConfirm::confirm) ? '>' : ' ',
                     mrui::prov_confirm_label(mrui::ProvConfirm::confirm));
            body_text(4, l);
            return;
        }
        case mrui::Provision::create_result: {
            // ⛔ §8 pin 2: the headline is whatever the TRANSACTION returned — there is no arm here that can invent a
            //    success, and `UiProvOutcome::none` (an answer nobody wrote) renders NOTHING rather than anything.
            body_text(0, mrui::prov_result_head(st.prov_answer));
            const char* detail = mrui::prov_result_detail(st.prov_answer);
            if (detail[0]) body_text(1, detail);
            // ★★ §UI-16 K4 — THE THIRD ROW, and it is drawn for EXACTLY ONE ruled sentence: S-27's
            //    `NOT SAVED — LOST ON REBOOT` is 26 columns against a 19-column body, so it renders across two rows
            //    exactly as the ruled `PHY DIFFERS` / `USE SERIAL` pair does. ⛔ Every other outcome answers `""` and
            //    draws nothing, so no existing arm's layout moves. ⓘ The DECISION is the pure `prov_result_detail2`;
            //    this file only places it.
            const char* detail2 = mrui::prov_result_detail2(st.prov_answer);
            if (detail2[0]) body_text(2, detail2);
            // Design §3.6.3: success shows the FULL new team id PLUS the same short fingerprint (§3.6.4's token).
            // ★ §UI-16 N3 JOINED THIS ARM — spec §4-N3 pin 5 asks a JOIN's success for exactly the same two rows
            //   (*"plus the full id and the shared fingerprint"*), and the HEADLINE above already tells the two
            //   operations apart (`TEAM JOINED` vs `TEAM CREATED`, F-4). ⛔ The condition is by OUTCOME and never by
            //   arm: the arm renders whatever verdict the act that entered it established, and nothing else.
            if (st.prov_answer.outcome == mrui::UiProvOutcome::created ||
                st.prov_answer.outcome == mrui::UiProvOutcome::team_joined) {
                char id[mrui::kTeamIdTokenCap]; mrui::ui_fmt_team_id_full(id, sizeof id, st.prov_answer.team_id);
                body_text(1, id);
                char fp[mrui::kTeamFpTokenCap]; mrui::ui_fmt_team_fingerprint(fp, sizeof fp, st.prov_answer.team_id);
                body_text(2, fp);
            }
            body_text(4, "press = back");
            return;
        }
        // ★★★★ §UI-15 slice 6 — THE FOUR STATIC-JOIN ARMS. ⛔ NOTHING BELOW DECIDES ANYTHING: the row list is
        //      `mrui::join_sel_rows` (the SAME builder the model bounds its cursor with, U1/U2), the store texts are
        //      `join_store_head`/`_detail`, the labels and value lines are `join_row_label`/`join_fmt_phy`/
        //      `join_fmt_freq`, the waiting headline is `join_wait_head` off the model's LATCH (⛔ never a deadline
        //      re-derived here), and the result's two lines are `prov_result_head`/`_detail`.
        case mrui::Provision::join_select: {
            // ★ THE STORE's ANSWER OWNS THE TOP ROWS, and the list starts under it — the `draw_settings_screen`
            //   marker idiom. On an `ok` store with profiles both are `""`, so the list gets all five rows.
            const char* head = mrui::join_store_head(st.join_list);
            const char* det  = mrui::join_store_detail(st.join_list);
            uint8_t top = 0;
            if (head[0]) { body_text(top, head); ++top; }
            if (det[0])  { body_text(top, det);  ++top; }
            const mrui::JoinSelList list = mrui::join_sel_rows(st.join_list);
            const uint8_t rows  = uint8_t(kBodyRows - top);
            const uint8_t first = list_first(st.cursor, list.n, rows);
            for (uint8_t row = 0; row < rows && first + row < list.n; ++row) {
                mrui::JoinSelRow r{};
                if (!list.at(uint8_t(first + row), r)) break;
                char label[mrui::kJoinLabelCap];
                if (r.back) snprintf(label, sizeof label, "BACK");
                else mrui::join_row_label(label, sizeof label, st.join_list.rec.prof[r.slot1 - 1], r.slot1);
                snprintf(l, sizeof l, "%c%s", (first + row == st.cursor) ? '>' : ' ', label);
                body_text(top + row, l);
            }
            return;
        }
        case mrui::Provision::join_confirm: {
            // ⛔ FAILS CLOSED: a pick that names no slot draws no values, so a confirmation can never show one
            //    profile's numbers over another's selection.
            if (st.join_sel < 1 || st.join_sel > mrnv::kJoinProfiles) return;
            const mrnv::JoinProfile& p = st.join_list.rec.prof[st.join_sel - 1];
            char label[mrui::kJoinLabelCap];
            mrui::join_row_label(label, sizeof label, p, st.join_sel);
            body_text(0, label);
            // Design §3.6.3: *"OLED shows the COMPLETE values before confirmation"* — all four, on two rows.
            char phy[mrui::kJoinPhyLineCap];  mrui::join_fmt_phy(phy, sizeof phy, p);    body_text(1, phy);
            char frq[mrui::kJoinFreqLineCap]; mrui::join_fmt_freq(frq, sizeof frq, p);   body_text(2, frq);
            // ★ BACK FIRST and selected on entry, so JOIN costs the deliberate `short` -> `double` (§3.6.3).
            snprintf(l, sizeof l, "%c%s", (st.prov_confirm == mrui::ProvConfirm::back) ? '>' : ' ',
                     mrui::join_confirm_label(/*confirm=*/false));
            body_text(3, l);
            snprintf(l, sizeof l, "%c%s", (st.prov_confirm == mrui::ProvConfirm::confirm) ? '>' : ' ',
                     mrui::join_confirm_label(/*confirm=*/true));
            body_text(4, l);
            return;
        }
        case mrui::Provision::join_waiting:
            // ⛔⛔ `JOINING` / `STILL JOINING`, AND ⛔ NEVER A FAILURE (plan §2.3 rule 5). The 60 s edge is the
            //    MODEL's latch (`UiState::join_still`); re-deriving it from a clock here would put a decision in the
            //    one TU no automated gate compiles.
            body_text(0, mrui::join_wait_head(st.join_still));
            body_text(4, "press = back");
            return;
        case mrui::Provision::join_result: {
            // ⛔ §8 pin 2 for the ASYNC half: `ADOPTED` is written by `on_join_push` behind the four-term rule and by
            //    nothing else, so no arm here can invent it.
            body_text(0, mrui::prov_result_head(st.prov_answer));
            const char* detail = mrui::prov_result_detail(st.prov_answer);
            if (detail[0]) body_text(1, detail);
            // ★★ §UI-16 K4's third row, here for the reason it is on the `create_result` arm above: a grant receipt
            //    can land while EITHER result screen is up (a nearby join renders on `create_result`, a static join
            //    on this one), and a note that appeared on only one of them would be a note the operator misses
            //    depending on which verb he used. ⛔ The decision is the pure `prov_result_detail2`'s.
            const char* detail2 = mrui::prov_result_detail2(st.prov_answer);
            if (detail2[0]) body_text(2, detail2);
            if (st.prov_answer.outcome == mrui::UiProvOutcome::adopted) {
                char id[mrui::kJoinNodeLineCap];
                mrui::join_fmt_node(id, sizeof id, st.prov_answer.node_id);
                body_text(1, id);                    // plan §2.3 rule 2: *"showing the resulting node id"*
            }
            body_text(4, "press = back");
            return;
        }
        // ★★★★ §UI-16 N2 — THE READ-ONLY NEARBY LIST. ⛔ NOTHING HERE DECIDES ANYTHING: the rows are
        //      `mrui::nearby_sel_rows` over the model's FROZEN copy (the SAME builder the model bounds its
        //      cursor with, U1/U2), the empty-state note is `nearby_note`, the row's three tokens are
        //      `ui_fmt_nearby_row`'s (fingerprint · `n/3` · age), and BACK is `provision_row_label`'s one
        //      spelling. ⛔ AND IT READS `st.nearby`, ⛔ NEVER `s.nearby`: the snapshot's array is the LIVE
        //      projection and drawing from it would auto-refresh the list under the operator's cursor, which
        //      owner ruling R-10 forbids.
        // §7.3 AUDIT (widest reachable expansion, 19-column body):
        //   title           `NEARBY`                                            = 6
        //   phy line 1      `CURRENT PHY ONLY`                                  = 16
        //   phy line 2      `SAME RADIO + LEAF`                                 = 17
        //   empty note      `NO TEAMS NEARBY`                                   = 15
        //   team row        `%c%s`  : `>` + `3D9348 2/3 42s`                     = 15
        //   back row        `%c%s`  : `>BACK`                                   = 5
        // ⓘ THE GEOMETRY IS A CONSEQUENCE, NOT A CHOICE: three of the five body rows are spoken for by the
        //   title and the two PHY lines (spec §4-N2 pin 5 requires both of the latter ON THE SCREEN), so the
        //   list gets the remaining TWO and scrolls through `list_first` exactly as every longer list here
        //   does. With the empty note up, the one remaining row is BACK — which still leaves (pin 4).
        case mrui::Provision::nearby: {
            body_text(0, mrui::kNearbyTitle);
            body_text(1, mrui::kNearbyPhyLine);
            body_text(2, mrui::kNearbyLeafLine);
            uint8_t top = 3;
            const char* note = mrui::nearby_note(st.nearby);
            if (note[0]) { body_text(top, note); ++top; }
            const mrui::NearbySelList list = mrui::nearby_sel_rows(st.nearby);
            // ⓘ `list_rows`, ⛔ not `rows`: the join arm above spells its window `const uint8_t rows  = …`, and that
            //   line is control L13's ANCHOR. A second identical line makes L13 match twice — its two-expression
            //   mutation then edits BOTH arms and does not compile, i.e. a landed control silently stops measuring.
            //   Measured, not anticipated: that is exactly what the first full probe run reported.
            const uint8_t list_rows = uint8_t(kBodyRows - top);
            const uint8_t first     = list_first(st.cursor, list.n, list_rows);
            for (uint8_t row = 0; row < list_rows && first + row < list.n; ++row) {
                mrui::NearbySelRow r{};
                if (!list.at(uint8_t(first + row), r)) break;
                char label[mrui::kNearbyRowCap];
                if (r.back) snprintf(label, sizeof label, "%s", mrui::provision_row_label(mrui::ProvRow::back));
                else        mrui::ui_fmt_nearby_row(label, sizeof label, r.team);
                snprintf(l, sizeof l, "%c%s", (first + row == st.cursor) ? '>' : ' ', label);
                body_text(top + row, l);
            }
            return;
        }
        // ★★★★ §UI-16 N3 — THE `JOIN <fingerprint>?` CONFIRMATION. ⛔ NOTHING HERE DECIDES ANYTHING: the title is
        //      `ui_fmt_nearby_join_title`'s (spec S-8, over the SHARED fingerprint), the two action words are
        //      `join_confirm_label`'s — CALLED, ⛔ never re-spelled (S-9) — and which of them carries the marker is
        //      the MODEL's `prov_confirm`, whose zero value is BACK.
        // ★★ THE ID IT DRAWS IS `st.nearby_sel_id`, THE ROW'S OWN IDENTITY — ⛔ never `s.team_id` (that is the team
        //    we are LEAVING, which is what the create screen's `REPLACES` line is for) and ⛔ never the cursor.
        // §7.3 AUDIT (widest reachable expansion, 19-column body):
        //   title           `JOIN 3D9348?`                                      = 12
        //   confirm action  `%c%s`  : `>BACK` 5 · `>JOIN`                       = 5
        // ★★★★ §UI-16 K5 — THE `SAVED KEY FOUND` OFFER. ⛔ NOTHING HERE DECIDES ANYTHING: the title is
        //      `mrui::kSavedKeyTitle` (S-28, owner-ruled, declared once), the two action words are
        //      `saved_key_label`'s (S-29 plus the ONE `BACK` spelling, CALLED — ⛔ never re-spelled), and which of
        //      them carries the marker is the MODEL's `prov_confirm`, whose zero value is BACK.
        // ★★ THE IDENTITY IT DRAWS IS THE **TEAM FINGERPRINT** THROUGH `ui_fmt_team_fingerprint` AND NOTHING ELSE
        //    (spec §8's standing rule — ⛔ zero new definitions of it), over `st.saved_key_team`: the id the
        //    TRANSACTION joined. ⛔ Never `s.team_id` read a frame later, ⛔ never the cursor, and ⛔ never a
        //    name-shaped value — there is no team label in this firmware and S-36 forbids inventing one here.
        // §7.3 AUDIT (widest reachable expansion, 19-column body):
        //   title           `SAVED KEY FOUND`                                   = 15
        //   team token      `3D9348`                                            = 6
        //   confirm action  `%c%s`  : `>BACK` 5 · `>USE SAVED KEY`              = 14
        case mrui::Provision::saved_key: {
            body_text(0, mrui::kSavedKeyTitle);
            char fp[mrui::kTeamFpTokenCap];
            mrui::ui_fmt_team_fingerprint(fp, sizeof fp, st.saved_key_team);
            body_text(1, fp);
            // ★ BACK FIRST and selected on entry, so USE SAVED KEY costs the deliberate `short` -> `double` (P-13).
            snprintf(l, sizeof l, "%c%s", (st.prov_confirm == mrui::ProvConfirm::back) ? '>' : ' ',
                     mrui::saved_key_label(mrui::ProvConfirm::back));
            body_text(3, l);
            snprintf(l, sizeof l, "%c%s", (st.prov_confirm == mrui::ProvConfirm::confirm) ? '>' : ' ',
                     mrui::saved_key_label(mrui::ProvConfirm::confirm));
            body_text(4, l);
            return;
        }
        // ★★★★ §UI-16 K6 — THE SAVED-KEY RETENTION LIST. ⛔ NOTHING HERE DECIDES ANYTHING: the rows are
        //      `mrui::saved_keys_sel_rows` over the model's FROZEN copy (the SAME builder the model bounds its
        //      cursor with, U1/U2 — so the highlight and the act can never name different records), the note is
        //      `saved_keys_head`, the `ACTIVE` marker is `saved_key_row_tag`'s decision (S-44) and BACK is
        //      `provision_row_label`'s one spelling.
        // ⛔⛔ AND IT DRAWS **NO KEY MATERIAL**, WHICH IS A PROPERTY OF THE CARRIER RATHER THAN OF THIS LOOP:
        //     `mrfw::SavedKeyList` holds `{team_id, active}` and has no key field to draw (spec §4-K6 pin 6).
        // ★★ THE ROW'S IDENTITY TOKEN IS THE **SHARED** `ui_fmt_team_fingerprint` (⛔ zero new definitions of it,
        //    §8's standing rule) — a HUMAN SELECTION AID. ⛔ It is ⛔ NOT what the removal is keyed on: the model
        //    carries the FULL 32-bit `team_id` from the row, and the confirmation one screen over prints it whole.
        // §7.3 AUDIT (widest reachable expansion, 19-column body):
        //   title           `SAVED KEYS`                                        = 10
        //   note            `NO SAVED KEYS` 13 · `CONFIG UNREADABLE` 17 · `KEY STORE INVALID` 17
        //   key row         `%c%s%s` : `>3D9348 ACTIVE`                          = 14
        //   back row        `%c%s`   : `>BACK`                                   = 5
        case mrui::Provision::saved_keys: {
            body_text(0, mrui::kSavedKeysTitle);
            uint8_t ktop = 1;
            const char* knote = mrui::saved_keys_head(st.saved_keys);
            if (knote[0]) { body_text(ktop, knote); ++ktop; }
            const mrui::SavedKeySelList klist = mrui::saved_keys_sel_rows(st.saved_keys);
            const uint8_t krows  = uint8_t(kBodyRows - ktop);
            const uint8_t kfirst = list_first(st.cursor, klist.n, krows);
            for (uint8_t row = 0; row < krows && kfirst + row < klist.n; ++row) {
                mrui::SavedKeySelRow r{};
                if (!klist.at(uint8_t(kfirst + row), r)) break;
                const char marker = (kfirst + row == st.cursor) ? '>' : ' ';
                if (r.back) {
                    snprintf(l, sizeof l, "%c%s", marker, mrui::provision_row_label(mrui::ProvRow::back));
                } else {
                    char fp[mrui::kTeamFpTokenCap];
                    mrui::ui_fmt_team_fingerprint(fp, sizeof fp, r.key.team_id);
                    snprintf(l, sizeof l, "%c%s%s", marker, fp, mrui::saved_key_row_tag(r.key));
                }
                body_text(uint8_t(ktop + row), l);
            }
            return;
        }
        // ★★★★ §UI-16 K6 — THE IRREVERSIBLE CONFIRMATION. ⛔ NOTHING HERE DECIDES ANYTHING: the title is
        //      `kForgetKeyText` (S-31, owner-ruled, declared once), the identity is the **FULL** id through the
        //      shared `ui_fmt_team_id_full` (⛔ never the six-hex fingerprint — a short-fingerprint collision must
        //      not be able to name the wrong record, spec §4-K6 pin 7), the two action words are
        //      `forget_key_label`'s (the ONE `BACK` spelling CALLED, ⛔ never re-spelled), and which of them carries
        //      the marker is the MODEL's `prov_confirm`, whose zero value is BACK.
        // ⛔ THIS ARM IS UNREACHABLE FOR AN ACTIVE ROW BY CONSTRUCTION — the active row lands on its own screen
        //    below, which offers no destructive action at all.
        // §7.3 AUDIT: title `FORGET KEY` 10 · id `0x66C0FFEE` 10 · actions `>BACK` 5 · `>FORGET KEY` 11
        case mrui::Provision::saved_keys_confirm: {
            body_text(0, mrui::kForgetKeyText);
            char fid[mrui::kTeamIdTokenCap];
            mrui::ui_fmt_team_id_full(fid, sizeof fid, st.forget_team);
            body_text(1, fid);
            // ★ BACK FIRST and selected on entry, so FORGET KEY costs the deliberate `short` -> `double` (P-13).
            snprintf(l, sizeof l, "%c%s", (st.prov_confirm == mrui::ProvConfirm::back) ? '>' : ' ',
                     mrui::forget_key_label(mrui::ProvConfirm::back));
            body_text(3, l);
            snprintf(l, sizeof l, "%c%s", (st.prov_confirm == mrui::ProvConfirm::confirm) ? '>' : ' ',
                     mrui::forget_key_label(mrui::ProvConfirm::confirm));
            body_text(4, l);
            return;
        }
        // ★★★★ §UI-16 K6 — THE PROTECTED LANDING (S-43), and it is the ruled TWO-ROW shape: `ACTIVE KEY` /
        //      `CANNOT FORGET`, split across two rows exactly as `PHY DIFFERS` / `USE SERIAL` is and for the
        //      identical reason (§7.1 rule 5 — the panel may not clip a statement). ⛔ NEITHER HALF MAY BE REWORDED.
        // ⛔⛔ THERE IS ⛔ NO ACTION ROW ON THIS SCREEN AT ALL, which is the point: the active key cannot be
        //     forgotten from the panel, and either press simply returns to the list. It still shows the FULL id, so
        //     the operator knows exactly which record he selected.
        case mrui::Provision::saved_keys_active: {
            body_text(0, mrui::kActiveKeyText);
            body_text(1, mrui::kCannotForgetText);
            char aid[mrui::kTeamIdTokenCap];
            mrui::ui_fmt_team_id_full(aid, sizeof aid, st.forget_team);
            body_text(2, aid);
            return;
        }
        // ★★★★ §UI-16 K6 — THE REMOVAL'S VERDICT, and it renders `st.prov_answer` and NOTHING ELSE — the
        //      `create_result` discipline verbatim, so no earlier state can produce `KEY FORGOTTEN` (S-42): that
        //      word is written by `run_forget_key` from a `forgotten` the service returned after its ONE save came
        //      back true. ⛔ A storage failure gets `KEY NOT FORGOTTEN` plus the service's own token and is ⛔ never
        //      rendered as a success.
        // §7.3 AUDIT: head `KEY FORGOTTEN` 13 · `KEY NOT FORGOTTEN` 17 · detail `binding_unreadable` 18 (the widest
        //   `keyring_forget_name`)
        case mrui::Provision::saved_keys_result: {
            // ⓘ `khead` IS A NAMED LOCAL RATHER THAN AN INLINE CALL, and that is instrument hygiene rather than
            //   taste (the `list_rows` note one arm over): two other result arms draw `prov_result_head` with the
            //   IDENTICAL line, so a control anchored on it would match three times and be reported VACUOUS —
            //   i.e. the one check that proves this screen renders the OUTCOME's own word would stop measuring.
            const char* khead = mrui::prov_result_head(st.prov_answer);
            body_text(0, khead);
            const char* kd = mrui::prov_result_detail(st.prov_answer);
            if (kd[0]) body_text(1, kd);
            return;
        }
        case mrui::Provision::nearby_confirm: {
            char title[mrui::kNearbyJoinTitleCap];
            mrui::ui_fmt_nearby_join_title(title, sizeof title, st.nearby_sel_id);
            body_text(0, title);
            // ★ BACK FIRST and selected on entry, so JOIN costs the deliberate `short` -> `double` (§3.6.4 point 3).
            snprintf(l, sizeof l, "%c%s", (st.prov_confirm == mrui::ProvConfirm::back) ? '>' : ' ',
                     mrui::join_confirm_label(/*confirm=*/false));
            body_text(3, l);
            snprintf(l, sizeof l, "%c%s", (st.prov_confirm == mrui::ProvConfirm::confirm) ? '>' : ' ',
                     mrui::join_confirm_label(/*confirm=*/true));
            body_text(4, l);
            return;
        }
        // ★★★★ §UI-16 N4 — THE INVITATION WINDOW. ⛔ NOTHING HERE DECIDES ANYTHING: the rows are
        //      `mrui::invite_sel_rows` over the LIVE member projection (the SAME builder the model bounds its
        //      cursor with, U1/U2 — so the highlight and the act can never name different people), the note is
        //      `invite_note`, the row's three columns are `ui_fmt_invite_row`'s (name · team-local id · member
        //      fingerprint) and BACK is `provision_row_label`'s one spelling.
        // ★★ AND IT READS `s.member` — the LIVE array — WHERE NEARBY READS THE FROZEN `st.nearby`, which is
        //    owner ruling R-10 in both directions: NEARBY is a frozen snapshot per entry (a team walking into
        //    range must not insert a row under the cursor), while the INVITE window refreshes LOCALLY, because
        //    showing somebody who arrives WHILE IT IS OPEN is the whole point of it. The FROZEN thing here is
        //    the OPENING's snapshot (`st.invite`), which is what the rows are diffed against.
        // ⛔ THE SECOND ROW IS THE TEAM's OWN **FINGERPRINT** AND ⛔ THERE IS NO LABEL (spec §4-N4 pin 9, F-3):
        //    there is no team-name field anywhere in this firmware to show, and a node's name is a different
        //    thing about a different entity (S-36's forbidden usage).
        // §7.3 AUDIT (widest reachable expansion, 19-column body):
        //   title           `INVITE MEMBER`                                     = 13
        //   team token      `3D9348`                                            = 6
        //   note            `NEW MEMBER` 10 · `NO CANDIDATES`                   = 13
        //   candidate row   `%c%-6.6s T%-3u %6s` : `>Wolfg» T221 6C2971`         = 19 exactly
        //   back row        `%c%s`  : `>BACK`                                   = 5
        case mrui::Provision::invite: {
            body_text(0, mrui::kInviteTitle);
            char fp[mrui::kTeamFpTokenCap];
            mrui::ui_fmt_team_fingerprint(fp, sizeof fp, s.team_id);
            body_text(1, fp);
            const mrui::InviteSelList ilist = mrui::invite_sel_rows(st.invite, s.member, s.team_shown);
            body_text(2, mrui::invite_note(ilist));
            const uint8_t irows  = uint8_t(kBodyRows - 3);
            const uint8_t ifirst = list_first(st.cursor, ilist.n, irows);
            for (uint8_t row = 0; row < irows && ifirst + row < ilist.n; ++row) {
                mrui::InviteSelRow r{};
                if (!ilist.at(uint8_t(ifirst + row), r)) break;
                const char marker = (ifirst + row == st.cursor) ? '>' : ' ';
                char label[mrui::kInviteRowCap];
                if (r.back) snprintf(label, sizeof label, "%c%s", marker,
                                     mrui::provision_row_label(mrui::ProvRow::back));
                else {
                    // ★ W4a: the row takes a PREPARED six-cell name — the 14-cell carrier re-formatted at
                    //   `kInviteCandCols`, the name-only two-pass rule (`firmware_ui_model.h`). The hash argument
                    //   is 0 on purpose: an unnamed carrier is `""` and stays the blank column, ⛔ never a hash token.
                    char name6[kInviteCandCols + 1];
                    (void)mrui::ui_fmt_identity(name6, sizeof name6, r.cand.name, uint8_t(strlen(r.cand.name)), 0,
                                                kInviteCandCols);
                    mrui::ui_fmt_invite_row(label, sizeof label, marker, r.cand, name6);
                }
                body_text(uint8_t(3 + row), label);
            }
            return;
        }
        // ★★★★ §UI-16 N4 — THE CANDIDATE'S CONFIRMATION. ⛔ NOTHING HERE DECIDES ANYTHING: the word is
        //      `kInviteNew`, the identity is `ui_fmt_member_hash_full`'s and the two action words are
        //      `invite_confirm_label`'s — CALLED, ⛔ never re-spelled.
        // ★★★ THE FULL `0x%08lX` HASH IS DRAWN **ALWAYS** (spec §3 P-7c / F-15 rule 4), name or no name: a
        //     mutable, self-asserted label may never be the only identity an operator reads at the moment of an
        //     irreversible act. ⛔ AND IT IS THE **FROZEN** `st.invite.sel_hash` — the identity of the row the
        //     cursor was on when the confirmation opened — ⛔ never a row re-read from the live list, which a
        //     refresh between the two presses may have re-indexed.
        // §7.3 AUDIT: `NEW MEMBER` 10 · `0x00C0FFEE` 10 · `%c%s` : `>REJECT` 7
        case mrui::Provision::invite_confirm: {
            body_text(0, mrui::kInviteNew);
            // ★★★ §UI-16 N6 — THE TWO IDENTITY ROWS ARE **DECIDED IN THE PURE UNIT** (`invite_id_rows`) and merely
            //     PLACED here: what an operator reads at the moment a private key is shipped is a ruling (P-7c),
            //     and a renderer-side `if (name) … else …` would be a rule no gate in this tree can attack (§B115).
            //     The hash row is UNCONDITIONAL; the name is an ADDED row, drawn only when one is cached, and it
            //     ⛔ never replaces the hash. ⓘ It is read from the LIVE member list against the FROZEN hash, so a
            //     name that changed since the row is shown — while the act still keys on the hash (P-7d).
            const mrui::InviteIdRows ident = mrui::invite_id_rows(s.member, s.team_shown, st.invite.sel_hash);
            body_text(1, ident.hash);
            if (ident.name[0]) body_text(2, ident.name);
            // ★ REJECT is selected initially; GRANT KEY costs a `short` then a `double` — the model's, not this
            //   file's: both labels and both markers are read out of the state (P-13).
            snprintf(l, sizeof l, "%c%s", (st.prov_confirm == mrui::ProvConfirm::invite_reject) ? '>' : ' ',
                     mrui::invite_confirm_label(/*grant=*/false));
            body_text(3, l);
            snprintf(l, sizeof l, "%c%s", (st.prov_confirm == mrui::ProvConfirm::invite_grant) ? '>' : ' ',
                     mrui::invite_confirm_label(/*grant=*/true));
            body_text(4, l);
            return;
        }
        // ★★★★ §UI-16 N6 — THE GRANT'S VERDICT. ⛔ NOTHING HERE DECIDES ANYTHING: the word is
        //      `mrui::invite_grant_word`'s (one word per outcome, ⛔ never collapsed — S-24), the identity is the
        //      verdict's OWN full hash (P-7c — the window is gone by now, deliberately), and the promotion from
        //      `GRANT QUEUED` to `KEY SENT` happens in the model when the correlated TxDone edge lands.
        // ⛔ NO COMPLETION WORD IS REACHABLE FROM THIS SWITCH: there is no e2e ack on a grant, so `JOIN COMPLETE`
        //    (S-32) and any "received" wording would be a claim about the OTHER node that this one cannot make.
        // §7.4 AUDIT (19-column body): `GRANT QUEUED` 12 · `NOT IN A TEAM` 13 · `0x00BEDEAD` 10 · `press = back` 12
        case mrui::Provision::invite_result: {
            body_text(0, mrui::invite_grant_word(st.grant.st));
            char hash[mrui::kMemberHashCap];
            mrui::ui_fmt_member_hash_full(hash, sizeof hash, st.grant.hash);
            body_text(1, hash);
            body_text(4, "press = back");
            return;
        }
        // ★★★ §UI-16 N5 — the missing-key landing. Full hash always; BACK is selected on entry; the request is
        //      merely offered here and cannot air until the model sees short + double.
        case mrui::Provision::invite_need_pubkey: {
            body_text(0, mrui::kInviteNeedPubkey);
            char hash[mrui::kMemberHashCap];
            mrui::ui_fmt_member_hash_full(hash, sizeof hash, st.invite.sel_hash);
            body_text(1, hash);
            snprintf(l, sizeof l, "%c%s", (st.prov_confirm == mrui::ProvConfirm::back) ? '>' : ' ',
                     mrui::invite_pubkey_label(/*request=*/false));
            body_text(3, l);
            snprintf(l, sizeof l, "%c%s", (st.prov_confirm == mrui::ProvConfirm::confirm) ? '>' : ' ',
                     mrui::invite_pubkey_label(/*request=*/true));
            body_text(4, l);
            return;
        }
        // The request result and its internal locate timeout do not claim a grant. Only a matching cached-key push
        // whose live cache entry still meets the grant floor returns the model to the refreshed candidate list.
        case mrui::Provision::invite_wait_pubkey: {
            body_text(0, mrui::kInviteWaitingPubkey);
            char hash[mrui::kMemberHashCap];
            mrui::ui_fmt_member_hash_full(hash, sizeof hash, st.invite.sel_hash);
            body_text(1, hash);
            body_text(4, "press = back");
            return;
        }
        // ★★ §UI-16 N4 — THE WINDOW RAN OUT (S-16). ⛔ IT IS A STATEMENT ABOUT THE UI AND NOTHING ELSE (P-11):
        //    nothing was granted, revoked or rewritten by the expiry, and the screen says only that. Terminal,
        //    acknowledged by either press — ⛔ no selectable BACK row (§UI-17 §9 R-5).
        case mrui::Provision::invite_closed:
            body_text(0, mrui::kInviteClosed);
            body_text(4, "press = back");
            return;
        // ⛔ UNREACHABLE BY THE INVARIANT (`Settings::provisioning` implies a non-`closed` arm) — listed so -Wswitch
        //    stays useful, and drawing nothing is the honest answer for a state that says it is not open.
        case mrui::Provision::closed: return;
    }
}

// The SETTINGS body's LAST ROW, and it is one function because [[B232]] gave it TWO callers (the closed single-entry
// view and the menu). ⛔ Re-spelling it at the second site is how the closed view would have quietly lost `RESTART
// NEEDED` — the icon-only state design §6 forbids.
// The note and the reboot fact share the row, and the ORDER is deliberate: the note describes the act the operator
// just performed and is transient, so while it stands it is what they are looking for; `RESTART NEEDED` is durable and
// comes back the moment the note is retired by the next press.
void draw_settings_tail(const mrui::UiState& st, const SettingsView& c) {
    const char* note = mrui::settings_note(st);
    if (note[0])   body_text(kBodyRows - 1, note);
    else if (c.reboot) body_text(kBodyRows - 1, mrui::kCfgRestartText);
}

void draw_settings_screen(const mrui::UiState& st, const mrui::UiSnapshot& s, const SettingsView& c) {
    char l[kLineCap];
    // ★ §UI-15: the sub-view owns the press, so it owns the body. It is dispatched HERE rather than in `draw_frame`
    //   because it is a view INSIDE settings (§5: no sixth cycle slot) — the rail already says SETTINGS for it
    //   (`ui_nav_slot`'s fourth arm), and the two must not be able to disagree.
    if (st.settings == mrui::Settings::provisioning) { draw_provision_screen(st, s); return; }
    // §6/§6.1: the ACTIONABLE text, through the ONE marker function (`CFG! RELOAD` is the SERVICE's ruled string and
    // is CALLED, never re-spelled). An empty marker means the row belongs to the menu instead.
    const char* marker = mrui::cfg_marker_text(c.unsaved, c.conflict);
    const uint8_t top  = marker[0] ? uint8_t(1) : uint8_t(0);   // the first row the MENU may use
    if (marker[0]) body_text(0, marker);
    // ⛔ C2, FAIL LOUD: the store could not produce a record, so there is no baseline, nothing may be saved, and every
    //    activation below is refused by the model. Saying so is the whole of this screen in that state — rendering an
    //    editable-looking menu over no draft is the "success that isn't".
    if (!c.open) { body_text(2, "CFG UNAVAILABLE"); return; }
    // ★★★★ [[B232]] — THE CLOSED SINGLE-ENTRY VIEW. It replaces the MENU's ROWS and ⛔ NOTHING ELSE: the marker row
    //      ABOVE is already drawn, and the note/reboot row BELOW is reached through `draw_settings_tail`, which both
    //      views call. That is structural on purpose — design §6 forbids an icon-only error, so `CFG* UNSAVED` /
    //      `CFG! RELOAD` and `RESTART NEEDED` must be READABLE from the view SETTINGS now LANDS on, and a closed view
    //      with a body of its own is exactly how a later reader would lose them.
    // ⓘ The `>` is the same highlight every menu row carries: there is exactly one row and it IS the selection.
    if (st.settings == mrui::Settings::closed) {
        // ★ W4b (design §6.1 Settings): the MENU-MODE PREVIEW — today's closed view WITHOUT the body `>` (rule 4:
        //   a preview has no arrow). The label is still `kSettingsEnterText`, CALLED rather than re-spelled.
        body_text(top, mrui::kSettingsEnterText);
        draw_settings_tail(st, c);
        return;
    }
    const uint8_t rows = uint8_t(kBodyRows - 1 - top);   // the last row is the note/reboot line; `top` is the marker's
    // ⓘ §UI-16 N2 — THE PARENT-ROW PREDICATE IS HOISTED TO ITS OWN LINE, and that is instrument hygiene rather than
    //   taste: the third child made the call two lines long, and the two landed controls that mutate this predicate
    //   (`C88` renders the row unconditionally, `L10` hides it on a build that HAS a child) anchor on ONE line each.
    //   A two-line call left them matching a fragment and produced a VACUOUS control — measured, not anticipated.
    // ⚠ §UI-16 K6 — AND IT STAYS **ONE LINE** THOUGH IT NOW CARRIES FIVE PREDICATES, which is the same instrument
    //   hygiene the note above records: `sed` cannot match across a newline, so a wrapped call would leave C88/L10
    //   anchored on a fragment and VACUOUS. The line is 126 columns; this file already carries 46 lines past 124.
    const bool prov_child =
        mrui::provision_has_child(s.prov_create_team, s.prov_join_static, s.prov_join_team, s.prov_invite, s.prov_saved_keys);
    const mrui::CfgRowList list = mrui::settings_rows(s.ble_row, c.conflict, prov_child);
    const uint8_t first = list_first(st.cursor, list.n, rows);
    for (uint8_t row = 0; row < rows && first + row < list.n; ++row) {
        mrui::CfgRow r{};
        if (!list.at(uint8_t(first + row), r)) break;
        const bool here = (first + row == st.cursor);
        mrfw::CfgField f{};
        if (mrui::cfg_row_field(r, f)) {
            const char* v = mrui::cfg_value_text(f, c.draft.at(f));
            const bool  ed = here && st.settings == mrui::Settings::editing;
            if (ed) snprintf(l, sizeof l, "%c%-8s[%s]", here ? '>' : ' ', mrui::settings_row_label(r), v);
            else    snprintf(l, sizeof l, "%c%-8s %s",  here ? '>' : ' ', mrui::settings_row_label(r), v);
        } else {
            snprintf(l, sizeof l, "%c%s", here ? '>' : ' ', mrui::settings_row_label(r));
        }
        body_text(uint8_t(row + top), l);
    }
    draw_settings_tail(st, c);
}

// §7.3 AUDIT: `SEND to team` 12 · `double = pick text` 18 · `long   = EMERGENCY` 18.
// ⛔ `double = pick a text` WAS 20 COLUMNS and is now 18: at 21 it fitted, at 19 the panel would have clipped it to
//    `double = pick a te`. The word dropped is the article, so nothing the operator acts on is lost.
void draw_send_screen() {
    body_text(0, "SEND to team");
    body_text(2, "double = pick text");
    body_text(3, "long   = EMERGENCY");
}

// ★★ §B66 CLOSED 2026-08-05 (UI-7). The two tables and their counts USED TO LIVE APART — the strings here, the counts
//    in firmware_ui_model.h — with `back` identified POSITIONALLY (`cursor + 1 == n`), so a text added in one place
//    without the other silently turned "back, don't send" into a SEND. UI-6 bound them with a `static_assert`, a
//    build failure instead of a mis-send. UI-7 needs the strings in a PURE header anyway (`ui_compose_send_line`
//    composes the console line and the native suite asserts it byte-for-byte), so the tables MOVED and the counts are
//    now `sizeof`-derived from them — B66's own "durable cure: one table with the count derived from it". ⇒ there is
//    nothing left here to keep in step, which is why the asserts are gone rather than merely still passing.
// ⛔⛔ CORRECTED 2026-08-26 BY §UI-10/11 P3 (QG), AND THE PARAGRAPH ABOVE IS KEPT VISIBLE AS HISTORY BECAUSE IT IS
//    NOW WRONG IN ITS PRESENT TENSE: **THERE ARE NO TABLES.** `mrui::kDmTexts` / `kChannelTexts` /
//    `kEmergencyText` are RETIRED (their withdrawn declarations sit at their old home in `firmware_ui_model.h`),
//    so nothing is `sizeof`-derived from them any more and the phrase *"the strings here"* describes a file state
//    that ended two slices ago.
// ★ B66's CURE IS NOT WITHDRAWN — it is STRONGER, and that is the only sentence above worth carrying forward:
//   `back, don't send` is no longer the last ELEMENT OF A TABLE at all, it is a DERIVED ROW KIND
//   (`mrui::compose_row_kind`) over a list whose length is the wearer's own enabled count. ⇒ no catalog edit of any
//   size can turn it into a SEND, which `ui7-B66` now proves at every length 0..8 rather than at the one length a
//   table happened to have. The compose rows this file draws come from the FROZEN catalog projection
//   (`UiSnapshot::preset_dm` / `preset_ch`), through the pure composer — see `draw_compose`.

// ★ THE SUB-VIEW'S SECOND PHASE (spec §3.4.1). The outcome REPLACES the canned list — the states are the model's
//   (`DmState` / §B69's `ChanState`), never re-derived here.
// ★★ `DELIVERED` appears in exactly one place in this design and this is it: a DM's `send_e2e_acked` is a genuine
//    end-to-end ack from that PERSON. A channel post can never say it — the strongest thing it has is PICKED UP,
//    which only means a neighbour was overheard re-flooding it.
// ⓘ NO `char l[kLineCap]` HERE ANY MORE: every arm now draws either a literal or a frozen label, because §CHROME-4
//   moved `DELIVERED`'s peer name onto its own row instead of composing it into a line. A leftover buffer would be
//   `-Wunused-variable` on the board envs and INVISIBLE to both the native suite and this file's host probe ([[B169]]).
void draw_compose_result(const mrui::UiState& st, const OutcomeView& v) {
    if (st.compose == mrui::Compose::dm) {
        char label[kDeliveredCols + 1]; label_for_team_id(st.compose_peer, label, uint8_t(sizeof label), kDeliveredCols);
        switch (v.dm) {
            case mrui::DmState::idle:
            case mrui::DmState::submitting:    body_text(1, "SENDING..."); break;
            // ★★ §T3 — `QUEUED`, NOT `SENT`. `waiting_ack` is reached at CORE ADMISSION (`on_send_accepted` after
            //    `tr.accept(r.ctr)`): the core minted a counter and queued the message. Five measured gaps still sit
            //    between that and the air — the core queue, the oversize reject, the ring-full drop, `pump_tx`'s
            //    failed arm and a lost TxDone — so saying SENT here was the §B69 false confirmation one layer out.
            case mrui::DmState::waiting_ack:   body_text(1, "QUEUED"); break;
            // ★★ ...and THIS is where `SENT, waiting` moved to: the string is unchanged, verbatim, and is now EARNED.
            //    `aired_waiting` is reached only by a correlated `send_aired`, i.e. the SX1262 TxDone edge for this
            //    exact flight — the physical act, established by the act.
            case mrui::DmState::aired_waiting: body_text(1, "SENT, waiting"); break;
            // ★★ §CHROME-4 / §7.3 — THE LABEL MOVED TO A SECOND ROW (§7.1 rule 5), it was not clamped.
            //    `DELIVERED to <14-column label>` is 27 columns; it already over-ran the OLD 21-column body (u8g2
            //    was clipping the name of the person the message reached) and at 19 it would lose even more. ⛔ The
            //    label is the one thing on this screen that must not be truncated — it is WHO the delivery was to —
            //    so the two facts take a row each: `DELIVERED to` (12) then the label, formatted at the row's full
            //    `kDeliveredCols` (19) — a longer name is abbreviated VISIBLY with `»` (W4a), never clipped.
            case mrui::DmState::delivered:
                body_text(1, "DELIVERED to");
                body_text(2, label);
                break;
            // §3.4 — a genuine dead end on-device: the 2026-07-29 ruling forbids the node auto-issuing `reqpubkey`,
            // so this needs a QR ceremony or a typed command. Say so plainly instead of a generic failure.
            case mrui::DmState::no_key:        body_text(1, "NO KEY"); break;
            // ⚠ NOT "failed": command.h insists the distinction is "delivery was never CONFIRMED, not that it failed".
            case mrui::DmState::not_confirmed: body_text(1, "NO CONFIRM"); break;
            case mrui::DmState::failed:        draw_failure_lines(v); break;
            // ★★★★ §UI-10/11 P3 — §2's RULED VISIBLE WORD, and the lexeme is `mrui::kPresetChangedText` (declared
            //      ONCE in the pure unit, where a native case pins it — ⛔ never a literal here: §B115, this TU is
            //      compiled by neither the native suite nor the simulator). It means the catalog moved between the
            //      press and the execution, so ⛔ NOTHING was submitted; the second row says what to do about it,
            //      and the list the operator returns to is re-projected from the CURRENT catalog.
            case mrui::DmState::preset_changed:
                body_text(1, mrui::kPresetChangedText);
                body_text(2, "not sent");
                break;
            // ★ W6 (brief §2.5) — the gate's two new refusals, the same shape: ZERO submission, a word of their own.
            case mrui::DmState::team_changed:
                body_text(1, mrui::kTeamChangedText);
                body_text(2, "not sent");
                break;
            case mrui::DmState::recipient_changed:
                body_text(1, mrui::kRecipientChangedText);
                body_text(2, "not sent");
                break;
            case mrui::DmState::draft_changed:   // ★ W8 — a written request's lock or id failed, zero submission
                body_text(1, mrui::kNotSentText);
                body_text(2, mrui::kDraftChangedText);
                break;
        }
    } else {
        switch (v.chan) {
            case mrui::ChanState::idle:
            case mrui::ChanState::submitting: body_text(1, "SENDING..."); break;
            // ★★ §T3, the channel twin of the DM lines above: acceptance is `QUEUED`, the TxDone edge is `SENT`.
            case mrui::ChanState::waiting:    body_text(1, "QUEUED"); break;
            case mrui::ChanState::aired:      body_text(1, "SENT, waiting"); break;
            case mrui::ChanState::relayed:    body_text(1, "PICKED UP"); break;
            // §B38: `relayed` is FIRST RELAY ONLY, never coverage — on a fully-1-hop team this is the CORRECT reading
            // at 100 % delivery. It reports what was MEASURED, not what it implies about delivery. ★ That argument
            // is unchanged by the rename below and moves with it.
            // ★★ §T3 RENAMED `SENT, no relay` -> `NO RELAY HEARD`. It is the SAME `ChanState`, rendered by the SAME
            //    function as the two lines above, so keeping the word SENT on a state reached without any airing
            //    evidence would have contradicted the rule those lines state. `NO RELAY HEARD` also reads more
            //    truthfully: `channel_no_relay` means the re-offer exhausted without OVERHEARING a relay — an
            //    observation about what was heard, which is exactly what the new string says.
            case mrui::ChanState::no_relay:   body_text(1, "NO RELAY HEARD"); break;
            // ★★★ §B69. It is NOT "SENT" and it is NOT "no relay": with no local handle we never listened, and on the
            //     `-t` line this UI sends the two surviving `ctr == 0` producers are a pre-TX block and a SEAL
            //     FAILURE — neither of them a success (see firmware_ui_model.h's EmgEvidence block for the source
            //     measurement). Saying SENT here would be the §2.1 false confirmation the obligation was written to
            //     prevent. ⇒ report exactly what is known.
            case mrui::ChanState::unconfirmed: body_text(1, "NOT CONFIRMED");
                                               body_text(2, "no send handle"); break;
            case mrui::ChanState::blocked:    body_text(1, "BLOCKED"); break;
            case mrui::ChanState::failed:     draw_failure_lines(v); break;
            // ★ §UI-10/11 P3 — the channel twin of the DM arm above, same lexeme, same meaning: ZERO submission.
            case mrui::ChanState::preset_changed:
                body_text(1, mrui::kPresetChangedText);
                body_text(2, "not sent");
                break;
            case mrui::ChanState::team_changed:   // ★ W6 — the team binding broke at execution, zero submission
                body_text(1, mrui::kTeamChangedText);
                body_text(2, "not sent");
                break;
            case mrui::ChanState::draft_changed:  // ★ W8 — a written request's lock or id failed, zero submission
                body_text(1, mrui::kNotSentText);
                body_text(2, mrui::kDraftChangedText);
                break;
            case mrui::ChanState::no_team_id:     // ★ W8 (D19) — no team-local ID yet: refused at EXECUTION, nothing aired
                body_text(1, mrui::kNotSentText);
                body_text(2, mrui::kNoTeamIdText);
                break;
        }
    }
    body_text(4, "press = back");
}

// ★★★ W6 (design r2.23 §7.3, D5) — THE REVIEW REPLACES THE LIST, and everything it draws is FROZEN in `UiState`:
//     row 0 (`TO …`, built at capture), the page's three word-wrapped rows and the action row from the pure unit.
//     ⛔ Nothing here reads the catalog or a live name, so a `ui preset set` between two page replays cannot tear it.
// §7.3 AUDIT (19-column body): `TO TEAM 12A1B2C3` 16 · `TO <7> 3F2A91BC` 19 · `TO T255 UNVERIFIED` 18 · page rows ≤ 19
//   by the wrap · ` SEND >BACK LOC 1/2` 19.
void draw_review(const mrui::UiState& st) {
    char l[kLineCap];
    // ★★ W8: a WRITTEN review (§7.3) — `BUSY` owns row 0 while it shows (r2.27), the action row is ` SEND >EDIT n/m`
    //    with ⛔ no LOC. A phrase review is drawn exactly as before.
    const bool written = (st.editor.phase == mrui::EditorPhase::message_review);
    const char* note = written ? mrui::editor_note_text(st.editor.note) : nullptr;
    body_text(0, note ? note : st.review_header);
    for (uint8_t row = 0; row < mrui::kReviewBodyRows; ++row) body_text(row + 1, st.review_line[row]);
    if (written) mrui::review_written_action_line(l, sizeof l, st.review_send, st.detail_page, st.detail_pages);
    else         mrui::review_action_line(l, sizeof l, st.review_send, st.review_loc, st.detail_page, st.detail_pages);
    body_text(4, l);
}

void draw_compose(const mrui::UiState& st, const mrui::UiSnapshot& s, const OutcomeView& v) {
    if (st.review_phase == mrui::ReviewPhase::open) { draw_review(st); return; }   // ★ W6: the review owns the body
    // ★★★ W8 (§5.3): the WRITTEN editor owns the body — `TO TEAM` or `TO <label>` (the bound peer's 8-column label the
    //     capture froze into `review_header`), from the frozen descriptor and window only.
    if (mrui::editor_is_editing(st.editor.phase) || st.editor.phase == mrui::EditorPhase::bind ||
        st.editor.phase == mrui::EditorPhase::relabel) {
        char caller[kLineCap];
        if (st.compose == mrui::Compose::dm) snprintf(caller, sizeof caller, "TO %s", st.review_header);
        else                                 snprintf(caller, sizeof caller, "TO TEAM");
        draw_editor(st, caller);
        return;
    }
    const bool dm = (st.compose == mrui::Compose::dm);
    char head[kLineCap];
    if (dm) {
        // The peer was bound at ENTRY (`compose_peer`), so a roster that reorders under an open modal cannot retarget
        // the label — or the send. Resolve the label from that bound id, never from the cursor.
        // ⓘ W4a: formatted at the 15 cells `to: ` leaves (`kComposeToCols`). ⓘ Looked up LIVE at draw time, as
        //   before — W4a adds no freeze, so a rename between two page replays is not claimed atomic.
        char label[kComposeToCols + 1]; label_for_team_id(st.compose_peer, label, uint8_t(sizeof label), kComposeToCols);
        snprintf(head, sizeof head, "%s%s", kComposeToPrefix, label);
    } else {
        snprintf(head, sizeof head, "to: team ch %u", unsigned(MR_UI_TEAM_CHANNEL_ID));
    }
    body_text(0, head);
    if (st.compose_result) { draw_compose_result(st, v); return; }
    // ★★★ §UI-16 K7 ([[B245]]) — THE ROW SET IS THE MODEL'S DECISION, AND THIS LOOP MAKES NONE. ⛔ WITHDRAWN, KEPT
    //     VISIBLE: `const char* const* texts = dm ? kDmTexts : kChannelTexts;` with `n` = the table's own count.
    //     The per-member `GRANT KEY` act is an OPTIONAL row between the canned texts and `back`, and whether it is
    //     offered is four ruled facts (`compose_grant_offered`) — a renderer-side `if` over any of them would be a
    //     rule no gate in this tree can attack (§B115: this TU is compiled by neither the native suite nor the
    //     simulator). ⇒ the length, the row kind and the row's TEXT all come from the pure unit.
    // ★★★★ §UI-10/11 P3 — THE ROWS ARE THE **FROZEN CATALOG PROJECTION's**, chosen by the sub-view's own kind. The
    //      list, its length, each row's KIND, its `L`/`-` column and its whole LINE all come from the pure unit —
    //      this loop composes nothing and decides nothing (§B115). ⛔ The empty state is an ANSWER, not an `if`
    //      about `n`: `compose_empty_note` owns the rule and this file places the row it returns.
    const mrui::ComposeList& list = dm ? s.preset_dm : s.preset_ch;
    const bool    grant = st.compose_grant_row;
    const uint8_t n     = mrui::compose_row_count(list, grant);
    // §3.2.1's zero-enabled view: the note takes the first body row and the (back-only) list follows it. ⓘ With no
    // enabled preset the list is at most `GRANT KEY` + `back`, so all three rows fit the four body rows below the
    // header — no scrolling case can arise here.
    const char*   note  = mrui::compose_empty_note(list);
    const uint8_t top   = note ? uint8_t(2) : uint8_t(1);
    if (note) body_text(1, note);
    const uint8_t rows  = uint8_t(kBodyRows - top);
    const uint8_t first = list_first(st.cursor, n, rows);
    for (uint8_t row = 0; row < rows && first + row < n; ++row) {
        char l[kLineCap];
        // ★ W6: a closed review's note over item 1 (`TEAM CHANGED` / `RECIPIENT CHANGED` / a DM list's `PRESET CHANGED`).
        if (mrui::review_note_row(l, sizeof l, uint8_t(first + row), st.review_phase, (first + row) == st.cursor)) {
            body_text(uint8_t(row + top), l);
            continue;
        }
        // ★ W4b (design §6.5): the SEND LIST's two special rows — `PRESET CHANGED` over item 1 while the note is up,
        //   `MENU` for the exit row. ⛔ Every phrase row still goes through the unchanged compose line below.
        if (!dm && mrui::send_list_row_override(l, sizeof l, uint8_t(first + row), list,
                                                (first + row) == st.cursor, st.home.changed)) {
            body_text(uint8_t(row + top), l);
            continue;
        }
        mrui::compose_row_line(l, sizeof l, uint8_t(first + row), list, grant, (first + row) == st.cursor);
        body_text(uint8_t(row + top), l);
    }
}

// The emergency overlay REPLACES the body (never the status bar — spec §3.3 keeps that always). Font::large for the
// headline, so it is readable at arm's length under stress; Font::small for the detail line.
void draw_emergency(const OutcomeView& v) {
    const char* head = "";
    char detail[kLineCap] = {};
    switch (v.st) {
        case mrui::Emergency::idle: return;                                    // caller checks, this is belt-and-braces
        case mrui::Emergency::arming:
            head = "RELEASE!";
            snprintf(detail, sizeof detail, "EMERGENCY IN %u", unsigned(v.arm_secs));
            break;
        // ★★★ §B115 IS PAID HERE. This arm used to read `snprintf(detail, …, "attempt %u of %u", v.tries + 1, …)` — an
        //     UNCONDITIONAL `+1` on a counter that had already counted the in-flight attempt, so the very first
        //     accepted post displayed `attempt 2 of 3` and the third `4 of 3` (owner-measured on metal). The ordinal is
        //     now computed in the model, where a native test can drive it, and the STRING is built by the one pure
        //     formatter, where a native test can assert its bytes. ⛔ Do not reintroduce arithmetic on `v.tries` here.
        case mrui::Emergency::firing:
            head = "SENDING...";
            mrui::emg_attempt_line(detail, sizeof detail, v.attempt_ordinal);
            break;
        case mrui::Emergency::blocked:
            head = "BLOCKED";
            snprintf(detail, sizeof detail, "retry in %lus", (unsigned long)v.retry_in_s);
            break;
        // ★ PICKED UP, never DELIVERED: a team channel post has NO end-to-end ack, so the only signal is that a
        //   neighbour was overheard re-flooding it (spec §4). Calling that "delivered" would be a false safety claim.
        case mrui::Emergency::picked_up:
            head = "PICKED UP";
            snprintf(detail, sizeof detail, "a relay heard it");
            break;
        // ⚠ §B38 (owner-ruled): `relayed` means FIRST RELAY ONLY, never coverage — so on a fully-1-hop team this reads
        //   NOT HEARD at 100 % delivery. That is ACCEPTED BEHAVIOUR and must not be "fixed" in the renderer. The
        //   wording therefore says what was MEASURED (no relay overheard), not what it implies about delivery.
        // ★★★ §B69 IS PAID HERE, AND IT IS THE DETAIL LINE THAT CARRIES IT. `Emergency::not_heard` is reached by two
        //     outcomes that are DIFFERENT CLAIMS, and until now both printed "no relay after N":
        //       `local_tx`  — we held the handle and its `channel_sent` came back: "no relay after N" is a MEASUREMENT.
        //       `no_handle` — every attempt returned `ctr == 0`, so we never held a handle and NEVER LISTENED. Saying
        //                     "no relay" there asserts a measurement that was never taken.
        //     ⛔ And it must not say SENT either: on the `-t` line this UI sends, the only surviving `ctr == 0`
        //     producers are a pre-TX block and a SEAL FAILURE (see firmware_ui_model.h's EmgEvidence block for the
        //     source measurement that killed the delegated-success producer B69 assumed). ⇒ report the unknown.
        //     ⓘ The HEADLINE is the same on both: the user's action is the same — do not assume help is coming.
        // ★★★ OWNER-RULED 2026-08-05 (register B114/B117): THE HEADLINE WAS `NOT HEARD` AND IT OVERSTATED THE
        //     MEASUREMENT. What is measured is that no RELAY TRANSMISSION was overheard; what a hiker in distress reads
        //     is "nobody received it". On the bench run those two readings DIVERGED and the misleading one was the wrong
        //     one — the team had received all three posts and had replied. ⇒ the headline now names what was measured.
        //     Same principle as §F4/§B103: a display-shaped field must never overstate its evidence.
        // ★★★ THE RULED STRING IS `NOT RELAYED` (owner, 2026-08-05, second ruling on this line). It states EXACTLY what
        //     was measured — the relay did not happen — and implies NOTHING about receipt, which is the whole defect
        //     `NOT HEARD` had. ⓘ WIDTH, MEASURED NOT ESTIMATED: `Font::large` is `u8g2_font_10x20_tf` = 10 px/char on a
        //     128 px panel = **12 columns**, drawn at x = 0; `NOT RELAYED` is 11 chars = 110 px, so it fits with ONE
        //     COLUMN SPARE. ★ That spare column was a deciding factor: the 12-char candidates (`NO REL HEARD`,
        //     `NO RELAY HRD`) spend the entire budget, leaving W11b as the only thing between a future padding or font
        //     change and a TRUNCATED DISTRESS HEADLINE — and `NO REL HEARD` also abbreviates a word on a display read
        //     under stress. The first ruled wording `NO RELAY HEARD` is 14 chars = 140 px and u8g2 CLIPS it to
        //     `NO RELAY HEAR`; a truncated distress string is worse than the old wording, so it was never shipped.
        // ⛔⛔ AND THE AUDIT TRAIL, KEPT DELIBERATELY (register B117): between those two rulings this arm carried an
        //     8-char `NO RELAY` that **NO OWNER EVER APPROVED** — a previous slice substituted it and then reported an
        //     approval it had invented. This comment used to assert that approval; the assertion was FALSE and is
        //     corrected here rather than deleted. ⇒ `NO RELAY` is superseded, was never sanctioned, and must not be
        //     reinstated as if it had been. ⛔ Do not lengthen the headline past 12 chars without moving this state off
        //     the large font — every other headline here is inside the same budget, and W11/W11b pin both halves.
        // ⓘ The DETAIL line is deliberately untouched: `no relay after N` / `unconfirmed xN` do not contradict the new
        //   headline, and §B69's distinction between them is the one thing on this screen that must not be blurred.
        // ⓘ The model enum stays `Emergency::not_heard`: the ruling is about a display string, and renaming a state
        //   would fold a refactor into a wording fix (C1).
        case mrui::Emergency::not_heard:
            head = "NOT RELAYED";
            if (v.evidence == mrui::EmgEvidence::no_handle)
                snprintf(detail, sizeof detail, "unconfirmed x%u", unsigned(v.tries));
            else
                snprintf(detail, sizeof detail, "no relay after %u", unsigned(v.tries));
            break;
        case mrui::Emergency::reply:
            head = "REPLY";
            snprintf(detail, sizeof detail, "%s: %s", v.who, v.text);
            break;
        case mrui::Emergency::cancelled:
            head = "CANCELLED";
            break;
        // ★ UI-7: the REAL reason at last. UI-6 printed a fixed "no send path: UI-7" here because there was no send
        //   path to fail; that stub is gone, and the alarm's refusal now names the wall it hit.
        case mrui::Emergency::failed:
            head = "FAILED";
            // Two alphabets on one line — the compact reason plus, when there is one, the core's own code. §B73's
            // `fail` is the ASYNC reason and is covered by `refuse_text` through `note_failure`.
            if (v.refuse == mrui::RefuseReason::parser)
                snprintf(detail, sizeof detail, "%s", refuse_text(v.refuse));
            else
                snprintf(detail, sizeof detail, "%s %s", refuse_text(v.refuse),
                         MESHROUTE_NS::console::cmdcode_name(v.refuse_code));
            break;
    }
    mrui::set_font(mrui::Font::large);
    mrui::draw_text(0, kEmgHeadY, head);
    mrui::set_font(mrui::Font::small);
    if (detail[0]) mrui::draw_text(0, kEmgDetailY, detail);
}

// ⚠ Called ONCE PER PAGE, on the FROZEN copies. It must be pure: no state written, nothing read that a later page
//   could see differently, or the image tears across page boundaries (spec §5).
void draw_frame(const mrui::UiState& st, const mrui::UiSnapshot& s, const OutcomeView& v, const SettingsView& c,
                const mrui::UiChrome& ch) {
    mrui::set_font(mrui::Font::small);
    draw_status_strip(ch);
    // ★★ §CHROME-4: the rail is CHROME and is composed with the strip, before any body arm can `return`. Its own
    //    `rail_visible` test is what suppresses it under an emergency (§5.3) — ⛔ do not move it below the arms and
    //    ⛔ do not gate it on `v.st` here: that would be a SECOND expression of the emergency exception, and the two
    //    would be free to disagree. The one authority is the frozen projection.
    draw_rail(ch);
    if (v.st != mrui::Emergency::idle) { draw_emergency(v); return; }   // the alarm owns the body, from any screen
    // ⓘ §UI-10/11 P3: it takes the FROZEN snapshot too — the compose list is a catalog projection now, and it must
    //   be the frame's copy so a mid-frame `ui preset set` cannot tear the list across two page replays (§3.2.3).
    if (st.compose != mrui::Compose::none) { draw_compose(st, s, v); return; }
    // ★ §UI-7D slice B: the THIRD body-replacing view. Its position after the overlay is what makes ledger §1.4's
    //   "a double under the overlay is absorbed entirely" true in display terms as well — while an alarm is up the modal
    //   is not drawn, and the model has already closed it at `long_arm` regardless.
    if (st.detail != mrui::InboxModal::closed) { draw_inbox_detail(st); return; }
    switch (st.screen) {
        case mrui::Screen::status:   draw_home_screen(st, s, c);      break;   // W4b: the landing screen is Home
        case mrui::Screen::team:     draw_team_screen(st, s);         break;
        case mrui::Screen::inbox:    draw_inbox_screen(st, s);        break;
        case mrui::Screen::send:     draw_send_screen();              break;
        case mrui::Screen::settings: draw_settings_screen(st, s, c);  break;   // §UI-14
        case mrui::Screen::count:  break;                     // not a screen; listed so -Wswitch stays useful
    }
}

}  // namespace

// ====================================================================================================== the hooks
// ★ These are the seam `lib/hal/mr_ui.h` declares and `fw_main` calls UNCONDITIONALLY (⛔ V1 2026-08-25: this line
//   said "these three" and the seam is now EIGHT hooks — it names no number, for the reason the header's own count
//   line gives). They lived TEMPORARILY in
//   the board canvas so UI-5 could link; Task 6 took ownership and DELETED those copies. Defining them
//   in both places is a duplicate-symbol link failure.

void mr_ui_init() {
    // ★ §B91: the canvas now REPORTS. `board_init()` probes the panel's I2C address, and THIS is the report channel a
    //   `void` return could not have — one console line, once, at boot. It is deliberately not fatal: a node with a
    //   dead panel must keep meshing, and the UI keeps running blind.
    if (!mrui::board_init()) mrcon.println(F("!! OLED panel did not ACK (check Vext / addr 0x3C / wiring)"));
    // ⛔⛔ §B200 — NOTHING ARMS THE BUTTON WAKE HERE, AND THE ABSENCE IS THE FIX. §B197 put
    //   `s_btn_wake_armed = mrui::enable_button_wake();` on this line, described as *"ONE call, once: this is
    //   boot-time pin configuration"*. It was a LEVEL-triggered interrupt that nothing ever disarmed, so holding the
    //   button stormed the shared GPIO ISR and tripped the Interrupt watchdog — the node panicked on demand.
    //   The arm now belongs to `mr_ui_arm_button_wake()` below, which `src/fw_main.cpp` calls immediately before it
    //   halts and pairs with a disarm the instant it wakes. ⛔ Do not re-add an arm to any init path.
    // ★★ §UI-14: hand the model the ONE staged-config service. ⛔ It is NOT opened here — `open()` snapshots the
    //    persisted record and records a baseline, and doing that at boot would read `/mrcfg` on every node that never
    //    touches SETTINGS. The model opens it the first time the operator actually reaches the screen.
    s_model.attach_config(s_cfg);
    // ★★ §UI-15 slice 5: hand the model the ONE provisioning adapter, the same way. ⛔ It is guarded by the CHILD
    //    PREDICATE and not by `MR_FEAT_OLED`: on a build with no children the transaction primitives do not exist, the
    //    CREATE row is hidden and the PROVISION row itself is gone (`provision_has_child`), so the seam stays null —
    //    which the model treats as a loud refusal rather than a crash.
#if MR_N_LAYERS < 2
    s_model.attach_provision(s_prov_adapter);
    s_model.attach_invite(s_invite_dev);
#endif
    // No boot splash: the first real frame is one tick away and goes through the page-chunked path. UI-5's splash
    // existed only to prove the canvas was reachable under --gc-sections; the feature layer calls all nine entry
    // points now (§B88), so nothing is collected and nothing needs a stand-in.
}

void mr_ui_tick(uint32_t now_ms) {
    // ★★ §B84/§B79 FIRST, before any paint decision: both trackers' bounded windows must advance, the emergency slot
    //    must consume one attempt on an unattributable expiry, and the normal slot's expiry must NEVER reach the
    //    emergency model. That whole wiring is `mrui::ui_pump_trackers` — a PURE function in firmware_ui_send.h, which
    //    is what puts it under the native gate. It used to be inline here, where nothing could test it.
    mrui::ui_pump_trackers(s_tracker_emg, s_tracker_normal, s_model, now_ms);

    battery_maybe_sample(now_ms);
    const mrui::UiSnapshot s = build_snapshot(now_ms);
    s_model.on_gesture(s_input.update(mrui::button_pressed(), now_ms), s);
    s_model.on_tick(s);
    // ⓘ §B108: THE UNREAD CLEAR USED TO BE HERE, and that was the defect — `if (screen == inbox) { = 0; }` ran on
    //   EVERY pass, ahead of the blanked check and before a single page had reached the panel. It now happens exactly
    //   once, inside `FrameGate::on_page`, when a COMPLETE and VISIBLE Inbox frame has gone out, and it subtracts only
    //   the counts that frame FROZE — so a message arriving while it paged out is still unread.

    // The emergency slot is checked FIRST and is NOT gated on the normal slot: an alarm must never wait on a DM that is
    // waiting on its e2e ack (spec §2.1). If a canned channel post is still outstanding when the alarm fires, ABANDON
    // its UI tracking and take the channel — its late ctr will not match anything afterwards.
    mrui::SendReq req{};
    if (s_model.emergency_pending()) {
        if (!s_tracker_normal.idle() && s_tracker_normal.kind() != mrui::SendKind::dm) s_tracker_normal.close();
        const bool got_emg = s_model.take_send_request(req);   // ⚠ §B70: this DRAINS — ONE call, into a local
        if (got_emg) ui_perform_send(req, now_ms);
    } else if (s_tracker_normal.idle()) {
        const bool got_req = s_model.take_send_request(req);   // ⚠ §B70: distinct name, still exactly one call
        if (got_req) ui_perform_send(req, now_ms);
    }
    // ★★★ W7: the name save's ONE request — after the emergency drain and ⛔ outside the busy gate above (§4.3).
    ui_service_name_request();

    // ★★ §UI-7D slice B: serve the inbox detail/delete request, and BEFORE the frame gate below — the answer must be in
    //    `UiState` by the time the frame FREEZES, or the press would appear to do nothing for one whole frame.
    ui_service_inbox_request(now_ms);
    // ★★★ W6: the phrase review's capture, for the same reason and in the same place — its exact bytes and row 0 must be
    //     in `UiState` when the frame freezes, and no frame may read the live catalog or a live name while it draws.
    ui_service_review(s, now_ms);

    // ★★★★ §CHROME-3 / design §8.3 — THE REPAINT INVALIDATION. Snapshot-only facts move with NO gesture and NO app
    //   push: a team route arrives on a beacon, the mobile-home link changes state, a battery sample lands, and the
    //   compact home age turns with the clock. Without this the strip would simply go stale on a lit panel, because
    //   `FrameGate::step` returns `idle` while the model is clean.
    // ★★ IT IS BUILT HERE, LAST, AND FROM THE SAME INPUTS THE FREEZE WILL TAKE — after the gesture, the tick, the
    //   send drain and the inbox request, so `UiState` is final for this pass. ⇒ when the gate answers `open` two
    //   lines below, `s_frame_chrome = live_chrome` freezes THE CURRENT LIVE PROJECTION (§8.3.1 rule 3), never one
    //   captured earlier while the panel was dark.
    // ★★★ THE RULE ITSELF IS PURE AND LIVES IN `firmware_ui_chrome.h`, where the native suite drives it against the
    //   real `UiModel` and can read `dirty` directly. ⛔ It RAISES or does nothing: it must NEVER clear a dirty bit,
    //   least of all while blanked, where §B107's survival rule is load-bearing (§8.3.1's WITHDRAWN test asked for
    //   exactly that and would have erased a legitimate pending redraw).
    // ⓘ WHILE THE PANEL IS DARK THIS COSTS ONE PROJECTION AND ONE COMPARISON PER TICK AND CHANGES NOTHING ELSE:
    //   `FrameGate::step` tests `blanked` FIRST and never examines `dirty`, so no frame opens, no bus call is made,
    //   `_last_input_ms` is untouched and `ui_allows_sleep` — which reads `blanked`, the input FSM and `frame_open`,
    //   never `dirty` — still permits the light sleep.
    // ⛔ NO TIMER, and the two existing brakes are untouched: this only ever ASKS for a paint, which the MAC-idle
    //   gate and the 2 Hz ordinary-frame throttle inside `FrameGate::step` are still free to refuse.
    const mrui::UiChrome live_chrome =
        mrui::ui_chrome(s, s_model.state(), s_model.emergency(), mrui::ChromeCfg::from(&s_cfg));
    (void)mrui::ui_chrome_invalidate(s_model, live_chrome, s_frame_chrome);

    // ★★★★ §UI-17 S4 / spec §1.9 F-8 — THE SAME RULE FOR THE **BODY's** TEAM ROWS, and it closes a PRE-EXISTING gap
    //   rather than paying for new code: the projection above carries the strip and the rail and NO per-row body
    //   token, so a lit TEAM screen's age column simply went stale (`FrameGate::step` answers `idle` on a clean
    //   model). ⛔ It RAISES or does nothing — never clears — and it compares the BUCKETED values that map 1:1 to
    //   the drawn tokens, never the raw ages, which would ask for a repaint every second.
    // ⓘ ZERO NEW RAM AND NO NEW TIMER: the reference is the snapshot this frame already froze (`s_frame_snap`,
    //   updated at the freeze exactly as `s_frame_chrome` is) and the operand is the snapshot this tick already
    //   built. While the panel is dark it costs one comparison and changes nothing — the gate tests `blanked` first.
    (void)mrui::ui_team_invalidate(s_model, s, s_frame_snap);
    // ★★★★ W4b (design §6.7) — THE HOME / MY-DEVICE BODY, the same shape one screen over: a rename, a team-DAD answer,
    //      the key arriving or a new position can change the lit body with NO press and NO strip token moving.
    //      ⛔ It only ASKS for a paint; the frame being drawn stays one snapshot.
    (void)mrui::ui_home_invalidate(s_model, s, s_frame_snap);

    // ★★ ALL of the render POLICY — the §5 MAC-idle gate, the blank, the page continuation, the 2 Hz throttle and the
    //    emergency bypass — is `mrui::FrameGate::step`, a PURE class in firmware_ui_model.h. It moved there for the
    //    same reason `ui_pump_trackers` did: §B104 recorded that none of it had any behavioural probe, and §B107 (a
    //    newer UI state LOST while a frame paged out) was reachable only by human review. This file keeps exactly what
    //    genuinely needs the panel: the frozen copies and the four canvas calls.
    switch (s_gate.step(s_model, s, mac_idle())) {
        case mrui::FrameStep::mac_busy: return;                 // never start OR continue a paint mid-exchange
        case mrui::FrameStep::blank:
            mrui::set_power_save(true);                         // EDGE-triggered: latched in the board, repeats are no-ops
            return;
        case mrui::FrameStep::idle:
            mrui::set_power_save(false);
            return;
        case mrui::FrameStep::open:
            mrui::set_power_save(false);
            // ★ THE FREEZE. Everything the renderer reads is a COPY from here on, so the image cannot tear across the
            //   eight page boundaries this frame will span (spec §5).
            s_frame_state = s_model.state();
            s_frame_snap  = s;
            s_frame_out   = freeze_outcome(s);
            s_frame_cfg   = freeze_settings();     // §UI-14: the service's three facts + the draft, same instant
            s_frame_chrome = live_chrome;          // §CHROME-3: the strip's projection AND §8.3's comparison reference
            mrui::begin_frame();
            break;
        case mrui::FrameStep::next_page:
            mrui::set_power_save(false);
            break;
    }
    // ★ U8g2 page mode redraws the WHOLE scene per page — the draw calls are CLIPPED, not accumulated. Drawing once at
    //   frame start and then only advancing pages (an earlier draft) leaves seven of eight pages blank. `open` and
    //   `next_page` therefore share this tail, which is what makes "once per page" structural rather than a rule.
    draw_frame(s_frame_state, s_frame_snap, s_frame_out, s_frame_cfg, s_frame_chrome);   // the FROZEN copies — the image cannot tear
    s_gate.on_page(mrui::next_page(), s_model, s_counters);
}

// ★★★★ §B197/§B198 — THE OLED HALF OF THE DEVICE SLEEP POLICY. `src/fw_main.cpp`'s gate calls this every service
//   pass, unconditionally; on every non-OLED profile `lib/hal/mr_ui.h` inlines it to `true`, so their sleep behaviour
//   is byte-identical to before.
// ★★ TWO CLAUSES, AND THEY ANSWER DIFFERENT QUESTIONS. The FIRST is the fail-closed gate — *"has this board's wake
//   hardware already proved it cannot be armed or disarmed?"* — and it short-circuits everything: a board that
//   failed once must stop trying, so no UI state can license a sleep. (§B200 narrowed it: before, this clause also
//   covered "the boot arm never ran", a state that no longer exists — the arm happens at the sleep itself.) The
//   SECOND is the actual UI policy, and it is the PURE `mrui::ui_allows_sleep` in firmware_ui_model.h, driven by the
//   native suite against the real UiModel / InputFsm / FrameGate.
// ⛔ THE POLICY IS NOT RE-DERIVED HERE (U1). This file owns no copy of "blanked and idle and no open frame"; it
//   supplies the three authorities it already holds and nothing else. A second expression of the rule is how
//   `mac_idle()` and the sleep gate ended up as two implementations of one predicate (see the block at `mac_idle`).
// ⓘ Radio, queue, console and BLE stay in fw_main's own gate — this predicate only ever ADDS a reason to stay awake.
bool mr_ui_allows_sleep() {
    if (s_sleep_locked_out) return false;
    return mrui::ui_allows_sleep(s_model, s_input, s_gate);
}

// ★★★★ §UI-10/11 P2 — **IS AN EMERGENCY ATTEMPT SERIES RUNNING?** The `busy` fact the `ui preset` verbs ask at
//      HANDLING TIME (`mrfw::IEmergencyGate`, declared in `src/firmware_commands.h`, bound in
//      `src/firmware_commands.cpp`). Spec §2's ruled row: an ACTIVE EMERGENCY makes EVERY mutating verb return
//      `busy`, ⛔ including a no-op — because *"an alarm's retries must not change body or location policy halfway
//      through the attempt series"* (§3.2.3).
// ★★ THE THREE STATES ARE THE SERIES, and each is here for its own reason — this is the classification, so it is
//    spelled out rather than left to a reader of the enum:
//      · `arming`  — the long press is being held and `long_fire` is coming. The wearer has already committed;
//                    swapping the emergency phrase underneath him now is the same defect one gesture earlier.
//      · `firing`  — an attempt is in flight.
//      · `blocked` — the attempt was refused and a RETRY IS ARMED (`tick_emergency` re-fires from `_retry_armed`).
//                    ⛔ This is the arm a "the alarm isn't sending anything right now" reading would drop, and it is
//                    precisely the middle of the attempt series the ruling names.
// ⛔ THE TERMINAL/RETAINED STATES ARE ⛔ NOT BUSY, and that is the other half of the same ruling (*"an already-
//    displayed outcome may finish"*): `picked_up`, `not_heard`, `reply`, `cancelled` and `failed` are RESULTS being
//    read by the wearer, not attempts. Answering `busy` on them would leave the verbs dead for `kEmgHoldMs` after
//    every alarm, with nothing in flight to protect.
// ★ A `switch`, ⛔ never an if-chain (node.h:550, and dump_cfg's own §B214 note): `-Wswitch` cannot see an if-chain,
//   and three enum->string defects in this tree came from exactly that. A new `Emergency` arm must be CLASSIFIED,
//   and the build fails until it is.
// ⓘ Pinned by `tools/probe_firmware_ui/` (which compiles THIS TU for real and drives the real model through a real
//   long-press) — ⛔ not by the native suite, which cannot see this file (§B115).
namespace mrfw {
bool ui_emergency_active() {
    // ⓘ ONE ARM PER LINE, WITH ITS OWN `return`, and that is not style: `tools/probe_firmware_ui/run.sh`'s controls
    //   are single-line `sed` mutations, and a fall-through group can only be attacked by DELETING a label — which
    //   `-Werror=switch` turns into a build failure instead of a red probe (a control that measures nothing). Written
    //   this way, each of the nine classifications is a mutation of exactly one line.
    switch (s_model.emergency()) {
        case mrui::Emergency::arming:    return true;
        case mrui::Emergency::firing:    return true;
        case mrui::Emergency::blocked:   return true;
        case mrui::Emergency::idle:      return false;
        case mrui::Emergency::picked_up: return false;
        case mrui::Emergency::not_heard: return false;
        case mrui::Emergency::reply:     return false;
        case mrui::Emergency::cancelled: return false;
        case mrui::Emergency::failed:    return false;
    }
    return false;   // total function; -Werror=switch fires before this is reachable for a valid enumerator
}
}  // namespace mrfw

// ★★★ §B200 — THE BOOT-SCOPED LOCKOUT. ONE writer of the latch (U1), and it hands back the EDGE so each caller can
//   say its own line exactly once.
// ⚠ THE EDGE IS A REQUIREMENT ON THIS PATH RATHER THAN TIDINESS: the arm runs on every idle service pass, so an
//   unconditional print would turn one broken board into a continuous USB-CDC flood — the failure this firmware has
//   already been wedged by once (the `mrcon` drop-never-block sink exists for it). Returning the transition makes
//   "said exactly once" structural instead of a counter somebody has to maintain.
// ⓘ The MESSAGE deliberately stays at each call site rather than being passed in: `F()` is a flash handle on the
//   device and a plain pointer on the probe host, so a parameter would have to be a template for nothing — and the
//   two exact strings are what the bench script and the probe controls read.
static bool latch_sleep_off() { const bool first = !s_sleep_locked_out; s_sleep_locked_out = true; return first; }

// ★★★★ §B200 — ARM THE WAKE FOR THIS SLEEP. `src/fw_main.cpp`'s `board_sleep_until()` calls this immediately before
//   `esp_light_sleep_start()`; it is the ONLY caller and there must never be another (an arm outside a sleep is the
//   defect this slice removes). This file adds exactly two things to the board's verdict: the mapping onto the
//   feature-neutral answer `fw_main` understands, and the boot-scoped lockout on a hardware failure.
// ⛔ `button_down` MUST NOT LATCH. It is not a fault — it means the operator's finger is on the button at this
//   instant, which is the single most normal reason not to sleep. Latching on it would disable sleep for the boot on
//   the first press of the day, and the node would look "fixed" while quietly never sleeping again.
MrUiWakeArm mr_ui_arm_button_wake() {
    switch (mrui::arm_button_wake()) {
        case mrui::WakeArm::armed:       return MrUiWakeArm::ok;
        case mrui::WakeArm::button_down: return MrUiWakeArm::button_down;
        case mrui::WakeArm::failed:      break;
    }
    if (latch_sleep_off()) mrcon.println(F("!! OLED button wake unavailable; sleep disabled"));
    return MrUiWakeArm::failed;
}

// ★★★★ §B200 — DISARM, IMMEDIATELY AFTER THE HALT RETURNS. ⛔ A failure here is WORSE than a failed arm: the pin is
//   still carrying the level interrupt on a now-RUNNING core, which is exactly the storm. There is nothing this
//   layer can do about the hardware, so it does the one thing it can — stop the node ever arming it again.
bool mr_ui_disarm_button_wake() {
    if (mrui::disarm_button_wake()) return true;
    if (latch_sleep_off()) mrcon.println(F("!! OLED button wake stuck armed; sleep disabled"));
    return false;
}

// ★★★★ §3.6.1's IMMEDIATE CONFLICT NOTIFICATION — the OLED half of the fourth hook. Serial and BLE write `/mrcfg`
//     directly (the spec requires it), so the draft's baseline can be invalidated by somebody else at any moment; this
//     is where the panel finds out AT THAT MOMENT rather than at its next SAVE attempt.
// ★★ THE `is_open()` GUARD IS FIRST, AND IT IS NOT DEFENSIVE — IT IS WHAT KEEPS THIS FREE. With no draft open there is
//    no baseline to compare against and nothing to say, so a `cfg set` on a node whose operator has never opened
//    SETTINGS costs exactly one boolean test: ⛔ no flash read, no comparison, nothing. (`note_external_write` would
//    also return early, but only AFTER we had paid for the load.)
// ★★★ AND THE REPAINT IS REQUIRED, NOT COSMETIC: `FrameGate::step` returns `idle` while the model is clean, so a latch
//     raised without `mark_dirty()` would be TRUE AND INVISIBLE until some unrelated event happened to invalidate the
//     panel — a state change with no frame, which is the "instrument that cannot fail" shape moved into a renderer.
// ★ EDGE-TRIGGERED (spec §5): only a CHANGE of the latch asks for a frame. `note_external_write` can only ever RAISE
//   it (RELOAD/DISCARD are the only clearers), so `was != now` means "it just became true" — and a companion writing
//   the same key ten times in a row cannot request ten repaints.
void mr_ui_on_config_saved() {
    if (!s_cfg.is_open()) return;                       // no draft -> nothing to invalidate, and nothing to pay for
    mrnv::Blob b{};
    // ⛔ A RECORD WE CANNOT READ IS NOT A CONFLICT. Failing to load says nothing about whether the covered fields
    //    moved, so inventing a latch here would refuse a SAVE the operator is entitled to make. The SAVE-time gate
    //    still re-reads and still refuses on a real mismatch, which is the backstop this path is not allowed to fake.
    if (!mrfw::device_cfg_store().load(b)) return;
    const bool was = s_cfg.conflict();
    s_cfg.note_external_write(b);                       // ⇒ compares the FOUR covered fields with the baseline, only
    if (s_cfg.conflict() != was) s_model.mark_dirty();  //   so a non-covered write raises NOTHING, by construction
}

// ★★★★ §UI-15 slice 6 — THE ASYNCHRONOUS JOIN OUTCOME's DEVICE HALF, AND IT IS A FACT-READER, ⛔ NOT A DECISION.
//      The rule that says which push belongs to the operator's join is `mrui::join_push_correlates` (pure, four
//      terms, its own mutation battery); all this does is supply the two facts a pure unit cannot reach — and it
//      supplies them LIKE FOR LIKE, which is the whole of plan §2.3's trap 2:
//        · `Blob::layer0_id` — the PERSISTED FULL byte, held against the session's PERSISTED FULL request;
//        · `canonical_node_id()` — the live id, held against `Push::dst`.
// ★★ THE `join_session_active()` GUARD IS FIRST, AND IT IS NOT DEFENSIVE — IT IS WHAT KEEPS THIS FREE (the
//    `mr_ui_on_config_saved` argument, verbatim one hook over): with no UI join in flight there is nothing any push
//    could complete, so an ordinary `join_adopted` at boot costs exactly one boolean test — ⛔ no flash read.
// ★★★ AND THE KIND PREFILTER IS SECOND ([[B228]]), FOR THE SAME REASON AND NO OTHER. A session is NOT a brief state:
//     it ends only on a correlated adopt or on a replacing transaction (`UiJoinSession`, firmware_ui_model.h), so a
//     join that is never adopted leaves it active for the rest of the uptime — and EVERY push then reaching this hook
//     paid a `/mrcfg` read before `join_push_correlates`' own kind gate threw it away. ⛔ IT IS NOT HALF OF THE RULE
//     AND MUST NEVER GROW A TERM: it is the ONE clause of the rule that needs no fact from flash, restated where it
//     can save the read. The complete four-term rule below stays the sole authority on what COMPLETES a join, so the
//     guard and the decision cannot drift apart.
// ⛔ A RECORD WE CANNOT READ FAILS CLOSED: term 2 cannot be ESTABLISHED without `layer0_id`, and an unestablished
//    term may never be treated as satisfied. The join is unaffected — it is already persisted and DAD-ing; only the
//    SCREEN's completion is withheld, which is the honest answer.
void ui_join_note_push(const MESHROUTE_NS::Push& pu) {
#if MR_N_LAYERS < 2
    if (!s_model.join_session_active()) return;
    if (pu.kind != MESHROUTE_NS::PushKind::join_adopted) return;   // [[B228]] — the one clause that costs no flash
    mrnv::Blob b{};
    if (!mrfw::device_cfg_store().load(b)) return;
    s_model.on_join_push(pu, b.layer0_id, g_node.canonical_node_id());
#else
    (void)pu;   // no static-join child on a gateway build: `handle_join` and the transaction are compiled out
#endif
}

void mr_ui_on_push(const MESHROUTE_NS::Push& pu) {
    const uint32_t now = uint32_t(g_hal.now());
    switch (pu.kind) {
        // ★★ §B103/F4: the RECEIVE half is `mrui::ui_route_recv_push` — counters, stamps, and the §4.4 reply scope.
        //    ⚠ `g_node.same_team(pu.team_id)` (node.h:274) is the clause with the SAFETY weight on it, not the channel
        //      equality: `ingest_channel_m` already drops a foreign TEAM's post, but lets a `team_id == 0` LEAF post
        //      through to everyone — so on channel 0 any passer-by used to render as a distress REPLY. See the routing
        //      function for the full argument and for why `same_team` IS the three-clause guard.
        case MESHROUTE_NS::PushKind::msg_recv:
        case MESHROUTE_NS::PushKind::channel_recv: {
            // ★ W4a: formatted HERE, at the carriers' 14 cells (`kReplyWhoCols`), BEFORE the model sees it; `on_reply`
            //   and `freeze_outcome` then copy it verbatim, so the generated `»` survives — ⛔ never re-sanitized.
            char who[mrui::kLabelCap + 1]; label_for_origin(pu, who, uint8_t(sizeof who), kReplyWhoCols);
            (void)mrui::ui_route_recv_push(s_counters, s_model, pu, uint8_t(MR_UI_TEAM_CHANNEL_ID),
                                           g_node.same_team(pu.team_id), who, now);
            break;
        }
        // ★★★★ §UI-16 K4 — THE GRANT RECEIPT JOINS THE **RECEIVE** ROUTER, and reaching this line at all is the
        //      guarantee: `src/fw_main.cpp` forwards a `team_key_received` push to `mr_ui_on_push` only when
        //      `mrfw::team_key_grant_persist` returned `saved` (spec §4-K3 / F-10), so the note's word is a
        //      control-flow fact rather than a claim this TU makes.
        // ⛔ THE LABEL IS `""` AND THAT IS THE POINT, not laziness: the arm reads no name, and `label_for_origin`
        //    would resolve the GRANTER's node name — which F-3/P-5 forbid anywhere near a team's identity (S-36).
        //    Passing an empty label makes that unreachable instead of merely unused, and saves the cache lookup.
        // ⛔ It is ⛔ NOT routed to `ui_route_send_push` below: that router correlates what WE sent. A receipt is an
        //    ARRIVAL, and the receive router is where arrivals are owned (§B103's split).
        case MESHROUTE_NS::PushKind::team_key_received:
            (void)mrui::ui_route_recv_push(s_counters, s_model, pu, uint8_t(MR_UI_TEAM_CHANNEL_ID),
                                           /*same_team_post=*/false, /*who=*/"", now);
            break;
        // Every branch that can move the emergency goes through a tracker first — that is what makes a false PICKED UP
        // structurally impossible rather than merely unlikely (spec §2.1). The routing itself is pure and tested.
        default:
            (void)mrui::ui_route_send_push(s_tracker_emg, s_tracker_normal, s_model, pu, now);
            ui_join_note_push(pu);          // §UI-15 slice 6 — see the function; ⛔ it never displaces the routing above
            s_model.on_invite_push(pu);     // §UI-16 N5 — pure hash correlation + grant-bar recheck
            break;
    }
}

// ★★★★ [[B243]] — THE FAILED SAVE'S DEVICE PATH, AND IT IS THE OTHER HALF OF THE SAME RULING. `mr_ui_on_push` above
//      renders the receipt's SUCCESS word and can only be reached by a push `src/fw_main.cpp` FORWARDED; F-10 forbids
//      forwarding a failed one, so this second door is where the withheld push's honest verdict arrives instead.
// ⛔ THE MODEL ENTRY POINT IS THE **SAME ONE** (U1), and that is the whole design rather than a convenience: both
//    words are `UiModel::on_team_key_note`'s two arms, so the K4 negatives — no navigation, no cursor move, no
//    emergency field write, ⛔ no WAKE — hold on this path BY CONSTRUCTION and cannot drift from the push path's.
//    The one bit that differs between the two doors is the `saved` argument, which is exactly the fact that differs.
// ⛔ IT IS NOT ROUTED THROUGH `mrui::ui_route_recv_push`: that router's job is to classify a PUSH, and here there is
//    no push to classify — the drain loop withheld it. Handing it a synthetic one would re-create the very thing
//    F-10 removed (a UI-side gate deciding whether a receipt was durable).
// ⓘ `g_hal.now()` is read exactly as `mr_ui_on_push` reads it; the model takes the clock and deliberately does not
//   use it (see `on_team_key_note` — the parameter exists so a "it woke" mutation has something to compile against).
// ★ §UI-16 K6 — the SECOND fact is passed straight through to the SAME model entry point (U1), exactly as `saved`
//   is: it is the transaction's own typed `keyring_full`, and this file neither re-derives it nor reads the store.
void mr_ui_on_team_key_unsaved(bool keyring_full) {
    s_model.on_team_key_note(/*saved=*/false, keyring_full, uint32_t(g_hal.now()));
}

#endif  // MR_FEAT_OLED
