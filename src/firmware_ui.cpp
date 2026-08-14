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
//             handle to correlate and no outcome kind of its own, so the sub-view shows `SENDING...` until its own
//             kBlankMs auto-exit. Bounded and never a false claim, but it answers nothing — register B111.
//   MISSING   a real battery reading. `mrui::battery_sample_mv()` is Task 9's; until then it answers "unavailable" and
//             the status bar renders `--` (the console_json.h:126 rule), never a plausible wrong number.
#include "mr_features.h"

#if MR_FEAT_OLED

#include <cstdio>            // snprintf — every panel string is formatted here, never in the board TU
#include "firmware_ui_model.h"
#include "firmware_ui_send.h"
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
// ★ WHEN to paint (§B107). The frozen copies above are WHAT to paint; this owns the lifecycle that decides when they
//   are refreshed — including the `dirty` consumption, which belongs to the FREEZE and not to the final page.
mrui::FrameGate  s_gate;

// ---- §5 rule 1: paint only when the MAC is idle ------------------------------------------------------------------
// The SAME predicate fw_main.cpp:1406 uses to decide it may sleep (U1 — do not invent a second one). A full 1024 B
// frame is ~25 ms of blocking I2C against a `cts_to_data_gap_ms` of 5, so this gate is a correctness constraint: it is
// what stops the panel from breaking an in-flight RTS/CTS/DATA exchange.
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
void fmt_age(char* out, size_t cap, uint32_t s) {
    if (s == UINT32_MAX)  { snprintf(out, cap, "--"); return; }
    if (s < 60)           { snprintf(out, cap, "%us", unsigned(s)); return; }
    if (s < 3600)         { snprintf(out, cap, "%um", unsigned(s / 60)); return; }
    if (s < 86400)        { snprintf(out, cap, "%uh%02u", unsigned(s / 3600), unsigned((s % 3600) / 60)); return; }
    snprintf(out, cap, "%ud", unsigned(s / 86400));
}
// Volts, never a percentage: a percentage needs a chemistry and a discharge curve nobody has approved (spec §3.3).
void fmt_volts(char* out, size_t cap, int32_t mv) {
    if (mv < 0) { snprintf(out, cap, "--"); return; }
    snprintf(out, cap, "%u.%uV", unsigned(mv / 1000), unsigned((mv % 1000) / 100));
}
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
bool ui_have_fix() {
    const MESHROUTE_NS::NodeConfig& cfg = g_node.config();
    return cfg.lat_e7 != 0 || cfg.lon_e7 != 0;
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
// 128x64, two fonts only (spec §11: do not link the full font set). 6x10 gives 21 columns and a 10 px line pitch;
// 10x20 gives 12 columns and is used for the emergency headline alone.
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
//   warnings at kLineCap 24 — all benign truncations, all still ten new warnings against a pinned census. The widest
//   provable line is the status bar (two uint16_t counts at 5 digits, two uint8_t at 3, plus an 11-char volts field
//   because `int32_t/1000` can be 7 digits) at 37 bytes, and the REPLY detail (`who` 14 + `text` 20) at 37. 48 proves
//   both. The panel clips at 21 columns regardless — this buffer bounds the FORMATTER, not the display.
constexpr int kLineCap     = 48;

int body_y(int row) { return kBodyY0 + row * kBodyDy; }

// ★★ THE FAILURE DETAIL, in the two alphabets that exist (spec §2.1 rule 6). A refusal the user cannot act on is the
//    thing C2 and §err-reason exist to prevent — but the honest limit is real: five different walls all come back as
//    `err_unsupported` (no key / no identity / no fix / empty / unsealable), so the compact reason CANNOT name them
//    and the plan rules "show the generic refusal AND THE CODE; do not invent a specific reason".
// ★ `cmdcode_name` is the ONE mapper (U1) — `fw_main.cpp:905` already calls it that and refuses a second switch. A
//   raw enum NUMBER would be exactly the "do not use it to make the comment go away" the frozen field warns about.
// ⓘ A `parser` refusal has no `CmdCode` at all (the line never became a `Command`), and `RefuseReason::parser` IS
//   that predicate — so the code line is suppressed there rather than printing a `queued` that means "not applicable".
void draw_failure_lines(const OutcomeView& v) {
    mrui::draw_text(0, body_y(1), refuse_text(v.refuse));
    if (v.refuse == mrui::RefuseReason::parser) return;
    char l[kLineCap];
    snprintf(l, sizeof l, "%s", MESHROUTE_NS::console::cmdcode_name(v.refuse_code));
    mrui::draw_text(0, body_y(2), l);
}

// Which slice of a longer list is on screen. A cursor may address up to kMaxTeamRows entries while only kBodyRows fit.
uint8_t list_first(uint8_t cursor, uint8_t n, uint8_t rows) {
    if (n <= rows || cursor < rows) return 0;
    const uint8_t first = uint8_t(cursor - rows + 1);
    return uint8_t((first + rows > n) ? (n - rows) : first);
}

void draw_status_bar(const mrui::UiSnapshot& s) {
    char volts[12]; fmt_volts(volts, sizeof volts, s.batt_mv);   // 7-digit volts + ".xV" + NUL — see kLineCap
    char bar[kLineCap];
    if (s.team_build)
        snprintf(bar, sizeof bar, "DM%u CH%u T%u/%u %s", unsigned(s.unread_dm), unsigned(s.unread_ch),
                 unsigned(s.team_shown), unsigned(s.team_total), volts);
    else
        snprintf(bar, sizeof bar, "DM%u CH%u %s", unsigned(s.unread_dm), unsigned(s.unread_ch), volts);
    mrui::draw_text(0, kBarBaseline, bar);
    mrui::draw_hline(0, kBarRuleY, 128);
}

// ★★★ §UI-14, spec §3.3 — THE DRAFT MARKER LIVES HERE TOO, and it is on STATUS rather than only on SETTINGS because
//     the whole point of it is to be seen WITHOUT cycling to the screen that owns the draft.
// ★ TWO PLACES FOR TWO INDEPENDENT FACTS, because one row cannot carry both honestly:
//     · the TITLE row takes the DRAFT marker — `CFG! RELOAD` if the persisted record moved under us, else
//       `CFG* UNSAVED`. `STATUS` + one space + the 12/11-char marker is 19-20 of the panel's 21 columns, so it fits
//       WITHOUT shortening the status bar (§3.3 forbids that explicitly).
//     · the LAST body row takes `RESTART NEEDED`, REPLACING the `batt <n>mV` line. ⚠ Stated rather than smoothed
//       over: that costs the millivolt reading while the state stands — the bar keeps showing volts (§3.3's ruled
//       render), so the information lost is the extra precision, not the battery. The alternative was folding two
//       facts into one row, which is exactly the collapse §3.6.1 warns against, and a reboot-required state has to
//       STAY VISIBLE UNTIL THE REBOOT (§3.6.5) so it cannot ride the transient note.
// ⓘ Both are silent until the operator has actually opened SETTINGS: a draft cannot exist before then, and the
//   service is not open, so `freeze_settings` reports all three false. Nothing is claimed about a config nobody edited.
void draw_status_screen(const mrui::UiSnapshot& s, const SettingsView& c) {
    char l[kLineCap], age[10];
    const char* marker = mrui::cfg_marker_text(c.unsaved, c.conflict);
    if (marker[0]) { snprintf(l, sizeof l, "STATUS %s", marker); mrui::draw_text(0, body_y(0), l); }
    else           mrui::draw_text(0, body_y(0), "STATUS");
    snprintf(l, sizeof l, "me T%u  team %08lx", unsigned(s.my_team_id), (unsigned long)s.team_id);
    mrui::draw_text(0, body_y(1), l);
    fmt_age(age, sizeof age, s.last_dm_age_s);
    snprintf(l, sizeof l, "DM %u, newest %s", unsigned(s.unread_dm), age);
    mrui::draw_text(0, body_y(2), l);
    fmt_age(age, sizeof age, s.last_ch_age_s);
    snprintf(l, sizeof l, "CH %u, newest %s", unsigned(s.unread_ch), age);
    mrui::draw_text(0, body_y(3), l);
    // §3.6.5: a saved-but-reboot-required state stays visible until the reboot — so it OWNS this row while it stands.
    if (c.reboot) { mrui::draw_text(0, body_y(4), mrui::kCfgRestartText); return; }
    if (s.batt_mv >= 0) snprintf(l, sizeof l, "batt %ldmV", (long)s.batt_mv);
    else                snprintf(l, sizeof l, "batt --");
    mrui::draw_text(0, body_y(4), l);
}

void draw_team_screen(const mrui::UiState& st, const mrui::UiSnapshot& s) {
    if (s.team_shown == 0) {
        mrui::draw_text(0, body_y(0), "TEAM");
        mrui::draw_text(0, body_y(1), "no teammates heard");
        return;
    }
    // ★★★ §B64 (owner-ruled 2026-08-05) — THE LOUD HALF OF THE REFUSAL, AND THE SUPPRESSED HIGHLIGHT IS THE OTHER HALF.
    //     The teammate the cursor was on has left the roster, so `UiModel::activate` refuses to send. C2 says a refusal
    //     must be sayable, so one row is RESERVED for the reason — the same way `draw_inbox_screen` reserves its header
    //     row — rather than overwriting a teammate.
    // ★ AND THE `>` MARKER GOES AWAY. Leaving it beside whatever now occupies that row would be the mis-send in DISPLAY
    //   form: the panel would name a target the model has already refused to use. The two must agree, always.
    const uint8_t rows  = st.team_pick_gone ? uint8_t(kBodyRows - 1) : uint8_t(kBodyRows);
    const uint8_t first = list_first(st.cursor, s.team_shown, rows);
    for (uint8_t row = 0; row < rows && first + row < s.team_shown; ++row) {
        const mrui::TeamRow& t = s.team[first + row];
        char age[10]; fmt_age(age, sizeof age, t.last_heard_s);
        char l[kLineCap];
        snprintf(l, sizeof l, "%c%-10s %4s %uh",
                 (!st.team_pick_gone && first + row == st.cursor) ? '>' : ' ', t.label, age, unsigned(t.hops));
        mrui::draw_text(0, body_y(row), l);
    }
    // 21 characters exactly, so it cannot be clipped: the panel is 21 columns in Font::small (spec §3.3).
    if (st.team_pick_gone) mrui::draw_text(0, body_y(kBodyRows - 1), "TEAMMATE GONE, repick");
}

// ★ UI-7: the real rows (spec §6.1). BLOCK ORDER — every DM row, then every channel row — never chronological: the
//   two seq spaces are independent and there is no shared clock to interleave on, so an interleaved list would be an
//   ordering claim the data does not support.
// ★ TRUNCATION IS STATED, never implied: `inbox_total` is what `pull` VISITED, so a screen showing 8 of 40 says so
//   rather than presenting the cap as the whole mailbox (the same rule the TEAM screen's `T4/12` follows).
void draw_inbox_screen(const mrui::UiState& st, const mrui::UiSnapshot& s) {
    char l[kLineCap], age[10];
    if (s.inbox_shown == 0) {
        mrui::draw_text(0, body_y(0), "INBOX");
        // ⚠ NOT "no messages": an inbox with no durable store installed (`Inbox::enabled()` false ⇒ `pull` returns 0)
        //   is indistinguishable here from an empty one, and the unread counters below are the honest thing we DO
        //   know. Claiming emptiness would be a statement we cannot support.
        fmt_age(age, sizeof age, s.last_dm_age_s);
        snprintf(l, sizeof l, "DM %u  newest %s", unsigned(s.unread_dm), age);
        mrui::draw_text(0, body_y(1), l);
        fmt_age(age, sizeof age, s.last_ch_age_s);
        snprintf(l, sizeof l, "CH %u  newest %s", unsigned(s.unread_ch), age);
        mrui::draw_text(0, body_y(2), l);
        mrui::draw_text(0, body_y(4), "no stored rows");
        return;
    }
    snprintf(l, sizeof l, "INBOX %u/%u", unsigned(s.inbox_shown), unsigned(s.inbox_total));
    mrui::draw_text(0, body_y(0), l);
    // ★★★ §UI-7D slice B — THE LOUD HALF OF THE ACTIVATION REFUSAL, and the suppressed highlight is the other half. It
    //     is §B64's TEAM treatment applied to the record identity: the selected `(kind, seq)` is no longer in the store,
    //     so `UiModel::activate` refused to open (and therefore refused to put a DELETE two presses from) whatever now
    //     occupies that row. One body row is RESERVED for the reason rather than overwriting a message, and the `>`
    //     marker goes away — a highlight beside a record the model has already refused to act on is the same wrong in
    //     display form. The two must agree, always.
    const uint8_t rows  = st.inbox_pick_gone ? uint8_t(kBodyRows - 2) : uint8_t(kBodyRows - 1);
    const uint8_t first = list_first(st.cursor, s.inbox_shown, rows);
    for (uint8_t row = 0; row < rows && first + row < s.inbox_shown; ++row) {
        const mrui::InboxRow& e = s.inbox[first + row];
        char tag[6];
        // ⓘ §UI-7D: the tag comes from `kind`, the row's ONLY kind field. `is_dm` is gone from the tree.
        if (e.kind == mrui::InboxKind::dm) snprintf(tag, sizeof tag, "DM");
        else                               snprintf(tag, sizeof tag, "CH%u", unsigned(e.channel_id));
        fmt_age(age, sizeof age, e.rx_age_s);
        snprintf(l, sizeof l, "%c%-3s %-9s %4s",
                 (!st.inbox_pick_gone && first + row == st.cursor) ? '>' : ' ', tag, e.text, age);
        mrui::draw_text(0, body_y(row + 1), l);
    }
    // Spec §3.5's own words for the refusal, on the last body row — the same place the TEAM screen puts its reason.
    if (st.inbox_pick_gone) mrui::draw_text(0, body_y(kBodyRows - 1), "MESSAGE GONE");
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
        mrui::draw_text(0, body_y(0), "MESSAGE GONE");
        mrui::draw_text(0, body_y(1), "evicted or deleted");
        mrui::draw_text(0, body_y(4), "press = back");
        return;
    }
    mrui::inbox_detail_head(l, sizeof l, st.detail_kind, st.detail_origin, st.detail_channel,
                            st.detail_page, st.detail_pages, st.detail_del_failed);
    mrui::draw_text(0, body_y(0), l);
    for (uint8_t row = 0; row < mrui::kDetailBodyRows; ++row)
        mrui::draw_text(0, body_y(row + 1), st.detail_line[row]);
    // ★ `back` FIRST and selected on entry, so deletion costs the deliberate short -> double (spec §3.5).
    snprintf(l, sizeof l, "%cback",   (st.detail_action == mrui::InboxAction::back) ? '>' : ' ');
    mrui::draw_text(0, body_y(3), l);
    snprintf(l, sizeof l, "%cdelete", (st.detail_action == mrui::InboxAction::del)  ? '>' : ' ');
    mrui::draw_text(0, body_y(4), l);
}

// ★★★★ §UI-14 — THE SETTINGS SCREEN (spec §3.6.2). Everything it reads is FROZEN: `st` is the model's copy, `c` is
//     the service's copy taken at the same instant, and the row LIST is rebuilt from those two frozen inputs through
//     the SAME pure builder the model bounds its cursor with (U1) — so the highlighted row and the row an activation
//     would act on cannot disagree by construction.
// ★★ WHAT EACH LINE SAYS, and why none of it is derived here:
//     · the TITLE carries the `*` §3.3 asks for, through the ONE marker function (`CFG! RELOAD` is the SERVICE's
//       ruled string and is CALLED, never re-spelled);
//     · a VALUE row shows the DRAFT's value — never the effective one — because the draft is what SAVE would write;
//     · the value is BRACKETED while that row is being edited, so `short`'s two modes are distinguishable ON THE
//       PANEL and not only in the model;
//     · `RESTART NEEDED` is its own row, from `reboot_required()`, independent of the unsaved marker above it.
void draw_settings_screen(const mrui::UiState& st, const mrui::UiSnapshot& s, const SettingsView& c) {
    char l[kLineCap];
    const char* marker = mrui::cfg_marker_text(c.unsaved, c.conflict);
    snprintf(l, sizeof l, "SETTINGS %s", marker);      // ⓘ an empty marker leaves a trailing space; the panel clips
    mrui::draw_text(0, body_y(0), l);
    // ⛔ C2, FAIL LOUD: the store could not produce a record, so there is no baseline, nothing may be saved, and every
    //    activation below is refused by the model. Saying so is the whole of this screen in that state — rendering an
    //    editable-looking menu over no draft is the "success that isn't".
    if (!c.open) { mrui::draw_text(0, body_y(2), "CFG UNAVAILABLE"); return; }
    // The note and the reboot fact share the last row, and the ORDER is deliberate: the note describes the act the
    // operator just performed and is transient, so while it stands it is what they are looking for; `RESTART NEEDED`
    // is durable and comes back the moment the note is retired by the next press.
    const char* note = mrui::settings_note(st);
    const uint8_t rows = uint8_t(kBodyRows - 2);       // row 0 is the title, the last row is the note/reboot line
    const mrui::CfgRowList list = mrui::settings_rows(s.ble_row, c.conflict);
    const uint8_t first = list_first(st.cursor, list.n, rows);
    for (uint8_t row = 0; row < rows && first + row < list.n; ++row) {
        mrui::CfgRow r{};
        if (!list.at(uint8_t(first + row), r)) break;
        const bool here = (first + row == st.cursor);
        mrfw::CfgField f{};
        if (mrui::cfg_row_field(r, f)) {
            const char* v = mrui::cfg_value_text(f, c.draft.at(f));
            const bool  ed = here && st.settings == mrui::Settings::editing;
            if (ed) snprintf(l, sizeof l, "%c%-10s [%s]", here ? '>' : ' ', mrui::settings_row_label(r), v);
            else    snprintf(l, sizeof l, "%c%-10s %s",   here ? '>' : ' ', mrui::settings_row_label(r), v);
        } else {
            snprintf(l, sizeof l, "%c%s", here ? '>' : ' ', mrui::settings_row_label(r));
        }
        mrui::draw_text(0, body_y(row + 1), l);
    }
    if (note[0])   mrui::draw_text(0, body_y(kBodyRows - 1), note);
    else if (c.reboot) mrui::draw_text(0, body_y(kBodyRows - 1), mrui::kCfgRestartText);
}

void draw_send_screen() {
    mrui::draw_text(0, body_y(0), "SEND to team");
    mrui::draw_text(0, body_y(2), "double = pick a text");
    mrui::draw_text(0, body_y(3), "long   = EMERGENCY");
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
void draw_compose_result(const mrui::UiState& st, const OutcomeView& v) {
    char l[kLineCap];
    if (st.compose == mrui::Compose::dm) {
        char label[mrui::kLabelCap + 1]; label_for_team_id(st.compose_peer, label, uint8_t(sizeof label));
        switch (v.dm) {
            case mrui::DmState::idle:
            case mrui::DmState::submitting:    mrui::draw_text(0, body_y(1), "SENDING..."); break;
            // ★★ §T3 — `QUEUED`, NOT `SENT`. `waiting_ack` is reached at CORE ADMISSION (`on_send_accepted` after
            //    `tr.accept(r.ctr)`): the core minted a counter and queued the message. Five measured gaps still sit
            //    between that and the air — the core queue, the oversize reject, the ring-full drop, `pump_tx`'s
            //    failed arm and a lost TxDone — so saying SENT here was the §B69 false confirmation one layer out.
            case mrui::DmState::waiting_ack:   mrui::draw_text(0, body_y(1), "QUEUED"); break;
            // ★★ ...and THIS is where `SENT, waiting` moved to: the string is unchanged, verbatim, and is now EARNED.
            //    `aired_waiting` is reached only by a correlated `send_aired`, i.e. the SX1262 TxDone edge for this
            //    exact flight — the physical act, established by the act.
            case mrui::DmState::aired_waiting: mrui::draw_text(0, body_y(1), "SENT, waiting"); break;
            case mrui::DmState::delivered:
                snprintf(l, sizeof l, "DELIVERED to %s", label); mrui::draw_text(0, body_y(1), l); break;
            // §3.4 — a genuine dead end on-device: the 2026-07-29 ruling forbids the node auto-issuing `reqpubkey`,
            // so this needs a QR ceremony or a typed command. Say so plainly instead of a generic failure.
            case mrui::DmState::no_key:        mrui::draw_text(0, body_y(1), "NO KEY"); break;
            // ⚠ NOT "failed": command.h insists the distinction is "delivery was never CONFIRMED, not that it failed".
            case mrui::DmState::not_confirmed: mrui::draw_text(0, body_y(1), "NO CONFIRM"); break;
            case mrui::DmState::failed:        draw_failure_lines(v); break;
        }
    } else {
        switch (v.chan) {
            case mrui::ChanState::idle:
            case mrui::ChanState::submitting: mrui::draw_text(0, body_y(1), "SENDING..."); break;
            // ★★ §T3, the channel twin of the DM lines above: acceptance is `QUEUED`, the TxDone edge is `SENT`.
            case mrui::ChanState::waiting:    mrui::draw_text(0, body_y(1), "QUEUED"); break;
            case mrui::ChanState::aired:      mrui::draw_text(0, body_y(1), "SENT, waiting"); break;
            case mrui::ChanState::relayed:    mrui::draw_text(0, body_y(1), "PICKED UP"); break;
            // §B38: `relayed` is FIRST RELAY ONLY, never coverage — on a fully-1-hop team this is the CORRECT reading
            // at 100 % delivery. It reports what was MEASURED, not what it implies about delivery. ★ That argument
            // is unchanged by the rename below and moves with it.
            // ★★ §T3 RENAMED `SENT, no relay` -> `NO RELAY HEARD`. It is the SAME `ChanState`, rendered by the SAME
            //    function as the two lines above, so keeping the word SENT on a state reached without any airing
            //    evidence would have contradicted the rule those lines state. `NO RELAY HEARD` also reads more
            //    truthfully: `channel_no_relay` means the re-offer exhausted without OVERHEARING a relay — an
            //    observation about what was heard, which is exactly what the new string says.
            case mrui::ChanState::no_relay:   mrui::draw_text(0, body_y(1), "NO RELAY HEARD"); break;
            // ★★★ §B69. It is NOT "SENT" and it is NOT "no relay": with no local handle we never listened, and on the
            //     `-t` line this UI sends the two surviving `ctr == 0` producers are a pre-TX block and a SEAL
            //     FAILURE — neither of them a success (see firmware_ui_model.h's EmgEvidence block for the source
            //     measurement). Saying SENT here would be the §2.1 false confirmation the obligation was written to
            //     prevent. ⇒ report exactly what is known.
            case mrui::ChanState::unconfirmed: mrui::draw_text(0, body_y(1), "NOT CONFIRMED");
                                               mrui::draw_text(0, body_y(2), "no send handle"); break;
            case mrui::ChanState::blocked:    mrui::draw_text(0, body_y(1), "BLOCKED"); break;
            case mrui::ChanState::failed:     draw_failure_lines(v); break;
        }
    }
    mrui::draw_text(0, body_y(4), "press = back");
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
    mrui::draw_text(0, body_y(0), head);
    if (st.compose_result) { draw_compose_result(st, v); return; }
    const char* const* texts = dm ? mrui::kDmTexts : mrui::kChannelTexts;
    const uint8_t n = dm ? mrui::kDmTextCount : mrui::kChannelTextCount;
    const uint8_t first = list_first(st.cursor, n, kBodyRows - 1);
    for (uint8_t row = 0; row + 1 < kBodyRows && first + row < n; ++row) {
        char l[kLineCap];
        snprintf(l, sizeof l, "%c%s", (first + row == st.cursor) ? '>' : ' ', texts[first + row]);
        mrui::draw_text(0, body_y(row + 1), l);
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
void draw_frame(const mrui::UiState& st, const mrui::UiSnapshot& s, const OutcomeView& v, const SettingsView& c) {
    mrui::set_font(mrui::Font::small);
    draw_status_bar(s);
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
    // ★★ §UI-14: hand the model the ONE staged-config service. ⛔ It is NOT opened here — `open()` snapshots the
    //    persisted record and records a baseline, and doing that at boot would read `/mrcfg` on every node that never
    //    touches SETTINGS. The model opens it the first time the operator actually reaches the screen.
    s_model.attach_config(s_cfg);
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
            mrui::begin_frame();
            break;
        case mrui::FrameStep::next_page:
            mrui::set_power_save(false);
            break;
    }
    // ★ U8g2 page mode redraws the WHOLE scene per page — the draw calls are CLIPPED, not accumulated. Drawing once at
    //   frame start and then only advancing pages (an earlier draft) leaves seven of eight pages blank. `open` and
    //   `next_page` therefore share this tail, which is what makes "once per page" structural rather than a rule.
    draw_frame(s_frame_state, s_frame_snap, s_frame_out, s_frame_cfg);   // the FROZEN copies — the image cannot tear
    s_gate.on_page(mrui::next_page(), s_model, s_counters);
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
            break;
    }
}

#endif  // MR_FEAT_OLED
