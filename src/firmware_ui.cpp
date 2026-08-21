// MeshRoute — src/firmware_ui.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// The board-UI FEATURE layer (plan Task 6 = spec slice UI-6). It owns the model, the two send trackers, ALL render
// policy, the battery cache and the correlation of node-wide pushes into UI outcomes — and it is where the three
// `mr_ui_*` hooks LIVE, which is what let UI-5's TEMPORARY copies in variants/heltec_v3/board_ui.cpp be deleted.
// Adds no new core API: every read is an accessor that already existed (spec §6).
//
// ★ THE TWO BOUNDARIES THIS FILE SITS BETWEEN, both of them load-bearing rather than tidy:
//   above it  `variants/heltec_v3/board_ui.h` — a display-INDEPENDENT canvas. Nothing here names U8g2, I2C or a pin;
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
#include "firmware_ui_icons.h"   // ★ the strip's glyphs. `inline constexpr` at namespace scope ⇒ `.rodata`, and
                                 //   §8.1's amendment requires them to land in FLASH, not RAM.
#include "firmware_ui_send.h"
#include "firmware_ui_status.h"  // ★★ §UI-17 S3: the STATUS body's five rows, EVERY substitution and row 4's
                                 //   priority, as pure strings — §B115's rule, because nothing in this TU is
                                 //   compiled by the native suite or the simulator. `draw_status_screen` below is
                                 //   placement and one `draw_rect`, nothing else.
#include "firmware_ui_team.h"    // ★★ §UI-17 S4: the TEAM row's ruled format, its route-age token, the two reserved
                                 //   columns and the bounded clock-driven repaint — pure, for the same §B115 reason.
#include "firmware_ui_prov.h"    // ★★ §UI-15 slice 5: the PURE team-create adapter (`mrfw::UiProvisionAdapter` over
                                 //   `mrfw::ITeamCreateDevice`). EVERY decision of §3.6.3's create — the PHY
                                 //   precondition, `phy.present = false`, the verdict mapping — lives THERE, where
                                 //   the native suite compiles it; this file supplies only the four device forwards.
#include "board_ui.h"        // resolved by `-I variants/heltec_v3` — ★ THIS is the task that makes that flag
                             //   load-bearing; §A0 predicted Task 5 and UI-5 measured it dead there three ways.
#include "mr_ui.h"           // the three hook DECLARATIONS we define below (fw_main calls them unconditionally)
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
DeviceTeamCreate         s_team_create;
DeviceJoinProvision      s_join_prov;
mrfw::UiProvisionAdapter s_prov_adapter(s_team_create, s_join_prov);
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

// ---- labels (spec §6: team_key_of_id -> peer_name_find -> 0x<hash> -> bare id) ------------------------------------
void label_from_hash(uint32_t hash, char* out, uint8_t cap) {
    if (g_node.peer_name_find(hash, out, cap) == 0) snprintf(out, cap, "0x%08lx", (unsigned long)hash);
}
void label_for_team_id(uint8_t id, char* out, uint8_t cap) {
    uint32_t hash = 0;
    // ⓘ Inert on a !MR_FEAT_TEAM build: `team_key_of_id` stubs to false there, so the label falls straight through to
    //   the bare id. No #if needed — the stub IS the fallback.
    if (g_node.team_key_of_id(id, hash) && hash != 0) { label_from_hash(hash, out, cap); return; }
    snprintf(out, cap, "id %u", unsigned(id));
}
void label_for_origin(const MESHROUTE_NS::Push& pu, char* out, uint8_t cap) {
    // §chan-crypt CL2c: a channel_recv carries the sender's stable key_hash32 here too, and `origin` on a team post is
    // only a DAD-assigned team_local_id — so prefer the hash when the post named one.
    if (pu.sender_hash != 0) { label_from_hash(pu.sender_hash, out, cap); return; }
    snprintf(out, cap, "id %u", unsigned(pu.origin));
}

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

void ui_perform_send(const mrui::SendReq& req, uint32_t now_ms) {
    mrui::ui_perform_send(s_tracker_emg, s_tracker_normal, s_model, req,
                          uint8_t(MR_UI_TEAM_CHANNEL_ID), ui_have_fix(), ui_exec, nullptr, now_ms);
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
struct InboxPullCtx { mrui::InboxRowBudget* budget; uint64_t now64; };
bool inbox_row_cb(void* vctx, const MESHROUTE_NS::InboxEntry& e) {
    InboxPullCtx* c = static_cast<InboxPullCtx*>(vctx);
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
// ⚠ [[B134]]: on `heltec_v3` the inbox is a volatile RAM ring, so `erased` means the tombstone was appended and the
//   record is gone from every future `pull()` IN THIS RUNTIME. ⛔ It is not a claim about surviving a power cycle — and
//   ⛔⛔ not because the record would return: a reboot takes it, its tombstone and the ENTIRE history together, so there
//   is nothing cross-reboot to test on this board (spec §6.2's criterion is platform-qualified for exactly that).
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
    const uint16_t visited = g_node.inbox().pull(/*dm_since=*/0, /*chan_since=*/0, inbox_row_cb, &ctx);
    budget.publish(s, visited);
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
        label_for_team_id(r.id, r.label, uint8_t(sizeof r.label));
    }
#endif
    s.my_team_id = g_node.team_local_id();
    s.team_id    = g_node.config().team_id;
    s.batt_mv    = s_batt_mv;
    // ================================================== §CHROME-3 — THE FIVE FIELDS §CHROME-1 DEFINED BUT COULD NOT
    // PUBLISH. The projection is PURE and may not touch `g_node`; every fact below is a `g_node` accessor, so this is
    // the one site that can supply them (the same shape as `team_build` / `ble_row` above — U3).
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

// ======================================================== §UI-17 S3 / spec §2.1 — THE STATUS BODY'S RESERVED MARK
// ★★★ THE SLOT IS RESERVED **NOW** SO THE ARTWORK CAN LAND LATER WITHOUT MOVING A PIXEL OF TEXT. S3 draws a
//     `draw_rect` placeholder; S6 replaces it with a native 24x24 monochrome XBM (72 B of `.rodata`, or an accepted
//     16x16 centred inside the same slot) and ⛔ moves no geometry and no text — that is the whole point of
//     reserving it. ⛔ NO RUNTIME SCALING, ever.
// ★★ AND THE TEXT ORIGIN IS THE SLOT's CONSEQUENCE, not an independent number: rows 0-2 clear the mark at `x = 40`
//    and therefore have 88 px = **14** columns, while rows 3-4 sit below it and keep the body's own 19 at `kBodyX`.
//    ⛔ The two column budgets are `mrui::kStatusNarrowCols` / `kStatusWideCols` — declared beside the strings they
//       bound — and the pixel arithmetic that justifies them is asserted here, where the pixels are.
constexpr int kStatusMarkX = kBodyX;   // §2.1: x = 12..35 — the mark shares the body's left margin
constexpr int kStatusMarkY = 12;       // §2.1: y = 12..35 — clear of the y = 9 rule
constexpr int kStatusMarkW = 24;
constexpr int kStatusMarkH = 24;
constexpr int kStatusTextX = 40;       // §2.1: rows 0-2 start here, i.e. past the slot plus a 4-px gutter
constexpr int kStatusNarrowPx = 128 - kStatusTextX;   // 88
static_assert(kStatusTextX >= kStatusMarkX + kStatusMarkW,
              "spec §2.1: rows 0-2 must clear the reserved 24x24 mark, never overdraw it");
static_assert(int(mrui::kStatusNarrowCols) * 6 <= kStatusNarrowPx,
              "spec §2.1: the narrowed STATUS rows' column count does not fit their 88-px width");
static_assert(int(mrui::kStatusWideCols) == kBodyCols,
              "spec §2.1: rows 3-4 are ordinary body rows — one width, not a second literal");
static_assert(kStatusMarkY + kStatusMarkH - 1 <= 59,
              "design §3.2: the body ends at y = 59; the mark may not reach past it");
static_assert(int(mrui::kStatusLineCap) <= kLineCap,
              "the STATUS formatters are handed a kLineCap buffer — it must be at least their own bound");
// The narrowed rows' draw. ⛔ A row whose string is EMPTY draws NOTHING rather than an empty text record: `NO TEAM`
// on row 0 with a blank row 1 beneath it is the ruled shape (spec §2.2 note a), and a zero-length draw would put a
// record on the panel's audit trail that the panel itself does not show.
void status_text(int row, const char* s) { if (s[0]) mrui::draw_text(kStatusTextX, body_y(row), s); }

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
//     ⓘ 1 + 4 = 5 of the rail's 19 columns.
void body_back_row(int row, bool here) {
    char l[kLineCap];
    snprintf(l, sizeof l, "%c%s", here ? '>' : ' ', mrui::kListBackText);
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
enum class Strip : uint8_t { mail = 0, home, team, key, batt, count };
// ⓘ THE ARITHMETIC BEHIND THE TABLE, so a future edit re-derives it instead of nudging numbers: `Font::small` is
//   6 px/column and every glyph but the battery is 7 px wide (`icons::kIconW`); the battery outline is 11
//   (`kBatteryW`). Widest tokens: mail `99+` and home `59m`/`old` = 3 columns, team `9+` = 2, battery `4.1V` = 4.
//     mail 0..25 · gap · home 28..53 · gap · team 56..75 · gap · key 79..85 · gap · battery 91..127
//   = 26 + 2 + 26 + 2 + 20 + 3 + 7 + 5 + 37 = 128 px exactly, with the battery's last column landing on x = 127.
// ★ THE BATTERY SLOT IS ANCHORED TO THE RIGHT EDGE (§3.1: *"battery is right-aligned so `--` and `4.1V` do not move
//   the preceding icons"*) — and being a FIXED slot is what delivers that: its icon and its token sit at constant x
//   whatever the token's width, so a `--` leaves the trailing columns empty instead of dragging the strip. ⛔ A
//   flowed layout that packed each field after the previous one would satisfy the sentence and break the picture the
//   moment the mail count reached three digits.
constexpr StripSlot kStrip[uint8_t(Strip::count)] = {
    /* mail */ {  0,       8,  25 },
    /* home */ { 28,      36,  53 },
    /* team */ { 56,      64,  75 },
    /* key  */ { 79, kNoToken, 85 },
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
// §7.3 AUDIT (widest reachable expansion), and it is now TWO budgets — see the mark block beside `body_text`:
//   row 0  x=40, <=14  `TEAM FFFFFFFF`                                 13
//   row 1  x=40, <=14  `ME NO ID`                                       8
//   row 2  x=40, <=14  `NO TEAM KEY`                                   11
//   row 3  x=12, <=19  `99+ NEW / HOME 59m`                            18
//   row 4  x=12, <=19  `-89.123,-179.123`                              16   (`RESTART NEEDED` is 14)
void draw_status_screen(const mrui::UiSnapshot& s, const SettingsView& c) {
    // §2.1's reserved slot. S6 swaps this one call for `draw_bitmap` of the real 24x24 mark; ⛔ nothing else moves.
    mrui::draw_rect(kStatusMarkX, kStatusMarkY, kStatusMarkW, kStatusMarkH);
    char l[kLineCap];
    mrui::ui_status_team(l, sizeof l, s);         status_text(0, l);
    mrui::ui_status_me(l, sizeof l, s);           status_text(1, l);
    mrui::ui_status_known(l, sizeof l, s);        status_text(2, l);
    mrui::ui_status_unread_home(l, sizeof l, s);  body_text(3, l);
    // ⛔ `c.reboot` is handed to the PURE priority, never tested here: spec §2.2 note g's `RESTART NEEDED` >
    //    coordinates > `NO LOCATION` order is the decision, and a decision made at this call site is one no
    //    mutation battery can attack.
    // ⛔⛔ AND THE POSITION COMES FROM THE **FROZEN SNAPSHOT** `s`, ⛔ NEVER FROM `g_node.config()` HERE. This
    //    function runs ONCE PER OLED PAGE (see `draw_frame`'s own rule), so a live read would let a `cfg set lat`
    //    landing between two page replays draw HALF A COORDINATE ROW from each fix — a torn position. The three
    //    fields are published once per tick by `build_snapshot`. ⓘ CORRECTED HERE 2026-08-21 (QG): S3's first cut
    //    read `g_node.config()` on this line, and that is the defect this comment now guards.
    mrui::ui_status_location(l, sizeof l, c.reboot, s);
    body_text(4, l);
}

// ★★★★ §UI-17 S1 — THE SCREEN IS EITHER A PASSIVE PREVIEW OR AN ENTERED LIST, and this one predicate is what says
//      which (the model's `screen_is_entered`, ⛔ never re-derived here). PASSIVE: the rows are listed with NO marker
//      anywhere and NO `BACK` row, because nothing has been picked and `short` passes the screen in one press.
//      ENTERED: the marker is back, and the list carries one more row than the snapshot published — `BACK`.
void draw_team_screen(const mrui::UiState& st, const mrui::UiSnapshot& s) {
    const bool entered = mrui::screen_is_entered(st.screen, st.settings, st.list_view);
    if (s.team_shown == 0) {
        body_text(0, "TEAM");
        body_text(1, "no teammates heard");
        // ⓘ AN EMPTY ROSTER STILL OFFERS THE WAY OUT (spec S1 pin 5), on the row below its two lines: entering a list
        //   that could only be left by walking rows it does not have would be a dead end. There is exactly one row and
        //   it IS the selection, so the marker is unconditional — the same statement the SETTINGS entry row makes.
        if (entered) body_back_row(2, true);
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
        if (mrui::list_row_kind(idx, s.team_shown) == mrui::ListRow::back) { body_back_row(row, here); continue; }
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
        char l[kLineCap];
        mrui::ui_team_row(l, sizeof l, here, s.team[idx]);
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
// ★ TRUNCATION IS STATED, never implied: `inbox_total` is what `pull` VISITED, so a screen showing 8 of 40 says so
//   rather than presenting the cap as the whole mailbox (the same rule the TEAM screen's `T4/12` follows).
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
        if (entered) body_back_row(3, true);
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
        if (mrui::list_row_kind(idx, s.inbox_shown) == mrui::ListRow::back) { body_back_row(row + 1, here); continue; }
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
            const mrui::ProvRowList list = mrui::provision_rows(s.prov_create_team, s.prov_join_static);
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
            // Design §3.6.3: success shows the FULL new team id PLUS the same short fingerprint (§3.6.4's token).
            if (st.prov_answer.outcome == mrui::UiProvOutcome::created) {
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
            if (st.prov_answer.outcome == mrui::UiProvOutcome::adopted) {
                char id[mrui::kJoinNodeLineCap];
                mrui::join_fmt_node(id, sizeof id, st.prov_answer.node_id);
                body_text(1, id);                    // plan §2.3 rule 2: *"showing the resulting node id"*
            }
            body_text(4, "press = back");
            return;
        }
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
        snprintf(l, sizeof l, ">%s", mrui::kSettingsEnterText);
        body_text(top, l);
        draw_settings_tail(st, c);
        return;
    }
    const uint8_t rows = uint8_t(kBodyRows - 1 - top);   // the last row is the note/reboot line; `top` is the marker's
    const mrui::CfgRowList list = mrui::settings_rows(s.ble_row, c.conflict,
                                                      mrui::provision_has_child(s.prov_create_team, s.prov_join_static));
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
        char label[mrui::kLabelCap + 1]; label_for_team_id(st.compose_peer, label, uint8_t(sizeof label));
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
            //    so the two facts take a row each: `DELIVERED to` (12) then the label (<= 14).
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
        }
    }
    body_text(4, "press = back");
}

void draw_compose(const mrui::UiState& st, const OutcomeView& v) {
    const bool dm = (st.compose == mrui::Compose::dm);
    char head[kLineCap];
    if (dm) {
        // The peer was bound at ENTRY (`compose_peer`), so a roster that reorders under an open modal cannot retarget
        // the label — or the send. Resolve the label from that bound id, never from the cursor.
        char label[mrui::kLabelCap + 1]; label_for_team_id(st.compose_peer, label, uint8_t(sizeof label));
        snprintf(head, sizeof head, "to: %s", label);
    } else {
        snprintf(head, sizeof head, "to: team ch %u", unsigned(MR_UI_TEAM_CHANNEL_ID));
    }
    body_text(0, head);
    if (st.compose_result) { draw_compose_result(st, v); return; }
    const char* const* texts = dm ? mrui::kDmTexts : mrui::kChannelTexts;
    const uint8_t n = dm ? mrui::kDmTextCount : mrui::kChannelTextCount;
    const uint8_t first = list_first(st.cursor, n, kBodyRows - 1);
    for (uint8_t row = 0; row + 1 < kBodyRows && first + row < n; ++row) {
        char l[kLineCap];
        snprintf(l, sizeof l, "%c%s", (first + row == st.cursor) ? '>' : ' ', texts[first + row]);
        body_text(row + 1, l);
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
    if (st.compose != mrui::Compose::none) { draw_compose(st, v); return; }
    // ★ §UI-7D slice B: the THIRD body-replacing view. Its position after the overlay is what makes ledger §1.4's
    //   "a double under the overlay is absorbed entirely" true in display terms as well — while an alarm is up the modal
    //   is not drawn, and the model has already closed it at `long_arm` regardless.
    if (st.detail != mrui::InboxModal::closed) { draw_inbox_detail(st); return; }
    switch (st.screen) {
        case mrui::Screen::status:   draw_status_screen(s, c);        break;
        case mrui::Screen::team:     draw_team_screen(st, s);         break;
        case mrui::Screen::inbox:    draw_inbox_screen(st, s);        break;
        case mrui::Screen::send:     draw_send_screen();              break;
        case mrui::Screen::settings: draw_settings_screen(st, s, c);  break;   // §UI-14
        case mrui::Screen::count:  break;                     // not a screen; listed so -Wswitch stays useful
    }
}

}  // namespace

// ====================================================================================================== the hooks
// ★ These three are the seam `lib/hal/mr_ui.h` declares and `fw_main` calls UNCONDITIONALLY. They lived TEMPORARILY in
//   variants/heltec_v3/board_ui.cpp so UI-5 could link; Task 6 took ownership and DELETED those copies. Defining them
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

    // ★★ §UI-7D slice B: serve the inbox detail/delete request, and BEFORE the frame gate below — the answer must be in
    //    `UiState` by the time the frame FREEZES, or the press would appear to do nothing for one whole frame.
    ui_service_inbox_request(now_ms);

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
            char who[mrui::kLabelCap + 1]; label_for_origin(pu, who, uint8_t(sizeof who));
            (void)mrui::ui_route_recv_push(s_counters, s_model, pu, uint8_t(MR_UI_TEAM_CHANNEL_ID),
                                           g_node.same_team(pu.team_id), who, now);
            break;
        }
        // Every branch that can move the emergency goes through a tracker first — that is what makes a false PICKED UP
        // structurally impossible rather than merely unlikely (spec §2.1). The routing itself is pure and tested.
        default:
            (void)mrui::ui_route_send_push(s_tracker_emg, s_tracker_normal, s_model, pu, now);
            ui_join_note_push(pu);          // §UI-15 slice 6 — see the function; ⛔ it never displaces the routing above
            break;
    }
}

#endif  // MR_FEAT_OLED
