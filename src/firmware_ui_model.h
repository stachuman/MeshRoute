// MeshRoute — src/firmware_ui_model.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Pure screen/state model for the one-button board UI (UI-2). Consumes a gesture plus a plain-data snapshot and
// produces what to draw. Knows nothing of g_node, Arduino or the display — that is what keeps it native-testable and
// every hardware concern in variants/heltec_v3/board_ui.cpp. See
// docs/superpowers/specs/2026-07-31-onboard-oled-ui-design.md §2-§5.
//
// DONE here (UI-2): screens + the list-aware cursor, the compose modal and its two canned lists, the send REQUEST
// slots, blanking, the dirty flag.
// DONE here (UI-3): the emergency state machine (arm/cancel/fire, three accepted transmissions, the blocked deadline
// and its next_ms==0 backoff, PICKED UP / NOT HEARD, the sticky REPLY whitelist, the kEmgHoldMs panel hold) and the DM
// outcome machine incl. the late-ack upgrade.
// DONE here (UI-3 QA fixes, §B72-§B75): the retry deadline no longer reserves an arithmetic value, `channel_failed`
// exists and is TERMINAL, `DmState::submitting` is written on hand-off, and an async failure carries its
// `SendFailReason` all the way to the panel.
// DONE here (§B78, owner-ruled 2026-08-04): a terminal `failed` alarm is RETAINED and holds the panel for a full
// `kEmgHoldMs` window from the failure's OWN arrival time — both the synchronous refusal and the async seal failure.
// DONE here (§B71, owner-ruled 2026-08-04, landed by UI-6): the emergency screen's EXIT — a SHORT press on a RETAINED
// outcome returns `_emg` to `idle`. It lives in this pure unit and not in firmware_ui.cpp on purpose: it is gesture
// SEMANTICS, the one thing this header owns, and putting it here is what makes it natively testable (the render layer
// has no automated cover at all). `emg_outcome_retained()` is the derived predicate; see it for the vacuous fifth state.
// DONE here (2026-08-05, the UI-6 QA fix slice — §B101/§B102/§B107/§B108): `FrameGate` (the whole render-policy
// lifecycle: the §5 MAC-idle gate, the blank, page continuation, the 2 Hz throttle and the emergency bypass), the
// `dirty` consumption moved to the FREEZE, `UiInboxCounters` + the unread clear driven by a COMPLETE and VISIBLE Inbox
// frame, `mark_dirty()`, the retained-outcome PRESENTED latch behind B71's exit, and `long_fire` closing the compose
// modal. ★ All five lived in `src/firmware_ui.cpp`, which neither the native suite nor the simulator compiles — which
// is exactly why all five shipped green. See `FrameGate`'s own block for the argument.
// DONE here (2026-08-05, the UI-7 QA fix slice — §B113 + §B64, the two behavioural blockers independent QA raised):
// §B113 `on_send_accepted` gains its THIRD arm, so `ChanState::waiting` is no longer a state nothing could reach and an
// accepted canned post finally reads `SENT, waiting` instead of a `SENDING...` that never resolved; §B64 (OWNER-RULED)
// the TEAM cursor tracks the TEAMMATE by team-plane identity (`_team_sel_id` + `sync_team_cursor`/`note_team_cursor`),
// and a teammate that has left the roster REFUSES the activation loudly (`UiState::team_pick_gone`) instead of
// retargeting the DM to whatever row the stale index happened to land on.
// DONE here (2026-08-05, the two OWNER RULINGS that closed UI-6's open decisions — §R1/§R2, register B109/B110):
// §R1 an accepted REPLY un-blanks the panel (`on_reply`, one line, past both scope guards — it is a TRANSITION, and
// the "not wake-on-any-push" half is the placement); §R2 the emergency overlay ABSORBS a `double` entirely
// (`on_gesture`, its OWN arm — see the ⚠⚠ there for why it must never be folded into §B102's latched short-press arm).
// DONE here (2026-08-13, §UI-7D slice B — the inbox DETAIL/DELETE modal, spec §3.5): the preview row's identity PAIR
// (`InboxRow::kind` + `seq`, replacing `is_dm` — one kind authority), the identity-tracking INBOX cursor
// (`sync_inbox_cursor` / `note_inbox_cursor`, §B64's shape one plane over), `activate`'s INBOX arm and its loud refusal,
// the modal itself (`InboxModal` / `InboxAction`, `detail_gesture`, the 42-char paging and its 2 s cadence on THIS
// tick), the open/erase REQUEST-and-typed-ANSWER seam, all three `InboxEraseResult` landings, the `long_arm` close, and
// `FrameGate`'s exclusion of the modal from the unread clear.
// DONE here (2026-08-13, §UI-14 — the SETTINGS screen, the draft marker and the save/discard/reboot states, spec
// §3.6.2/§3.6.1/§3.3): the fifth cycle slot (`Screen::settings`, in BOTH cycles), the row TABLE and its two CONDITIONAL
// rows (`CfgRow` / `settings_rows` — the BLE row's transport condition and the RELOAD row's conflict condition, both
// PARAMETERS so the native suite drives both arms), the `Settings{closed,browsing,editing}` state that separates
// `short`'s two modes, the value cycle (`cfg_menu_next`) and its MENU-only narrowing of `ble_mode`, the activation of
// SAVE / DISCARD / RELOAD / BACK through §UI-13's `ConfigService`, the transient note (`settings_note`), spec §3.3's
// three literals (`cfg_marker_text`, `kCfgRestartText`), the row-identity cursor (`sync_settings` /
// `note_settings_cursor` — §B64's rule on a third screen, live because the RELOAD row appears under the cursor), and
// the `long_arm` close of the editor.
// DONE here (2026-08-19, §UI-15 slice 4 — the PURE provisioning STATE MODEL and its two gates, plan §4/§5/§6): the
// fourth `Settings` arm (`provisioning`) and the SEPARATE eight-arm `Provision` enum it carries, the §4 unsaved/conflict
// entry gate (two DISTINCT refusals with two DIFFERENT remedies — `ProvBlock`), the §6 availability model
// (`ProvRow` / `provision_rows`, both child predicates as snapshot PARAMETERS), the confirm-state cursor and its BACK
// default (`ProvConfirm`), and the close-on-leave invariant (`provision_reset_on_leave`).
// DONE here (2026-08-19, §UI-15 slice 5 — the TEAM-CREATE half of §3.6.3, plan §2.1/§8): the PURE intent carrier and
// its typed answer (`UiProvIntent` / `UiProvAnswer` / `IUiProvision` — the adapter that fills them is
// `src/firmware_ui_prov.h`), the `menu -> create_confirm` entry, the confirmation's toggle-and-perform gestures
// (`provision_confirm_gesture`), the ONE act (`run_create_team`, whose four statements ARE §8's "no screen claims
// success before the save returns"), the terminal `create_result` arm, both screens' PURE strings
// (`kProvCreateTitle` / `prov_confirm_label` / `prov_result_head` / `prov_result_detail`), and the OWNER-RULED
// hiding of the PARENT `PROVISION` row when no child is available (`provision_has_child`, `settings_rows`).
// DONE here (2026-08-20, §UI-15 slice 6 — the STATIC-JOIN half of §3.6.3 and its ASYNCHRONOUS outcome, plan
// §2.3/§3/§8): the `menu -> join_select` entry and its ONE `/mrjoin` read (`load_join_profiles`), the slot list and
// its gestures (`join_select_gesture`), the confirmation and its shared BACK-default toggle (`join_confirm_gesture` /
// `prov_confirm_toggle`), the ONE act (`run_join_static`, whose statement order IS §8 pin 2 for the join half), the
// three new outcomes (`joining` / `adopted` / `join_refused` — ⛔ never a `JOINED` before a correlated adopt), the
// 60 s `STILL JOINING` word change as an EDGE-TRIGGERED latch in `on_tick`, the session (`_join`, whose whole
// lifetime is plan §2.3's) and the push entry point `on_join_push`. ★ The FOUR-TERM CORRELATION RULE itself and every
// panel string of these screens live in `src/firmware_ui_join.h`, which has its OWN mutation battery.
// ⛔ WHAT IS STILL MISSING AND WHY (this is the [[meshroute-mark-done-vs-missing-in-code]] statement): §3.6.4's
//    nearby-team scan and its sealed key grant are §UI-16's, and ⛔ [[B215]] — the audit finding that
//    `reset_join_for_reprovision()` cancels only the claim guard and not the old listen/retry timers — is ITS OWN
//    slice by owner disposition (plan §11 q2), so nothing here compensates for it.
// ⛔ NOT here, by unit boundary: the SERVICE's own state (draft/baseline/latch) — it lives in `ConfigService`, which
//    this unit POINTS AT and never copies; and the DEVICE bindings of `ICfgStore`/`ICfgLive`, which are
//    `src/firmware_config.cpp`'s ([[B193]]). ⛔ NOT here at all, by scope: §3.6.3's SCREENS and its adapters to
//    `ProvisioningService` / the typed join transaction (slices 5/6), and §3.6.4's nearby-team scan (§UI-16).
// ⛔ NOT here, by unit boundary: `pull()` / `erase()` themselves and the pull callback that copies the body — those need
//    `g_node` and live in src/firmware_ui.cpp. ⚠ [[B134]]: on ESP32 the inbox is a RAM ring, so "durable" here means the
//    tombstone was appended IN THIS RUNTIME; nothing claims survival across a power cycle.
// NOT here yet (UI-4, [[meshroute-mark-done-vs-missing-in-code]]): NOTHING correlates outcomes. Every `on_outcome` /
// `on_send_accepted` / `on_send_refused` call must come from the Task-4 send tracker, which matches ctr/peer/channel
// FIRST — feeding this model a raw Push would let an unrelated channel post complete an emergency (spec §2.1).
// ⚠ `_last_try_ms` (UI-4's outcome window) is the ONE field still written-but-unread here. §B75: the claim that
// `DmState::submitting` was also written-but-unread was FALSE — nothing assigned it at all; `take_send_request` does
// now, and `dm_state()` reads it.
// NOT here at all, by unit boundary: what a screen LOOKS like and the send itself live in src/firmware_ui.cpp
// (UI-6/UI-7); the model only ever emits an index and asks. ⓘ V1 comment repair 2026-08-05: this line also claimed the
// canned message TEXTS were over there — they are not, and have not been since §B66 moved `kDmTexts`/`kChannelTexts`
// HERE (lines 80-89) so `back`'s positional identity has a single `sizeof`-derived declaration.
// DONE here (2026-08-05, §B115): the emergency DISPLAY ORDINAL and the one string that renders it
// (`emg_attempt_ordinal` / `emg_attempt_line`) — presentation split cleanly from `_tries`, which stays the limit's only
// truth. The string is formatted in this pure unit precisely so a native test can assert the VISIBLE bytes.
#pragma once
#include <cstddef>   // std::size_t (UI-3's copy_clamped) — do NOT rely on <cstdint> to drag it in transitively
#include <cstdint>
#include <cstdio>    // snprintf — §B115: the ONE panel string this pure unit formats, so the native suite can assert
                     // the VISIBLE BYTES (see `emg_attempt_line`). Free: every TU that includes this header already
                     // pulls <cstdio> through firmware_ui_send.h or firmware_ui.cpp.
// ★ §B73: the ONE lib/core dependency, and it is deliberate. Spec §2.1 rule 6 requires the WHOLE `SendFailReason` to
// reach the UI ("others -> a compact reason"), so the alternative was a parallel 18-value UI mirror of a core enum
// that command.h documents as APPEND-ONLY — the exact fork U1 forbids. `command.h` is the app seam: typed PODs, no
// Arduino, no heap, no `Node` (it is what fw_main and the sim both parse INTO), so the unit stays native-testable and
// board-free. Precedent: src/firmware_config_parse.h includes protocol_constants.h for the same reason.
#include "command.h"
// ★ §UI-7D slice B: THE SECOND lib/core dependency, and it is the SAME argument as `command.h`'s above. Spec §3.5/§6.1
// rule that a preview row's selection identity is the PAIR `(InboxKind, seq)` and that the modal's delete calls
// `Inbox::erase(InboxKind, seq)` — so this unit must name the CORE's `InboxKind` and its three-valued
// `InboxEraseResult`. ⛔ A `mrui::` mirror of either would be the parallel enum U1 forbids, and — worse here — a
// SECOND kind authority: the displayed kind and the erase target could then disagree, i.e. we would delete out of the
// other store. `inbox.h` is app-seam-shaped exactly like `command.h` (typed PODs + an abstract store interface, no
// Arduino, no heap, no `Node`), and it also carries `protocol::inbox_max_body`, the one number the modal's body buffer
// may be sized from.
#include "inbox.h"
// ★★★ §CHROME-1: THE THIRD lib/core dependency, and the argument is `inbox.h`'s taken one step further. The chrome
// projection (`src/firmware_ui_chrome.h`, design §4.2) must classify the MOBILE-HOME LINK's four states into an icon,
// and `Node::MobileHomeLink` is a NESTED enum — it cannot be forward-declared without its class.
// ⛔ THE TWO ALTERNATIVES WERE BOTH WORSE, and this is a decision rather than a default:
//   · a `mrui::HomeLink` MIRROR of the four enumerators is the parallel enum U1 forbids, and here it is also a second
//     authority for a safety-adjacent state (§4.2 forbids the word "connected" on this plane; two enums = two places
//     to get that wrong);
//   · classifying at the PUBLISHER (`src/firmware_ui.cpp`'s `build_snapshot`) would put the four-state -> icon switch
//     in a TU that neither the native suite nor the simulator compiles — §B115's rule, and the reason every panel
//     string in this file is formatted HERE.
// ⇒ the snapshot names the CORE state and the projection classifies it, so all four arms are natively drivable.
// ⓘ COST, MEASURED not assumed: `src/firmware_ui.cpp` already pulls `node.h` through `fw_context_pure.h:29`, so the
//   only TUs that gain it are the native test TUs — which already compile `node.h` in a dozen other cases. `node.h` is
//   Arduino-free by construction (the simulator compiles it), so this unit stays board-free and host-testable.
#include "node.h"
// ★★★ §UI-14: THE FOURTH lib-free dependency, and it is the SAME argument as `command.h`'s and `inbox.h`'s — except
// that this one is not even cross-library: `src/firmware_config_service.h` is §UI-13's TYPED STAGED-CONFIGURATION
// SERVICE, a PURE unit (no Arduino, no `Print`, no `Node`, no strings) with its own native suite. Spec §3.6.1 rules
// that the OLED owns an explicit `ConfigDraft` and ⛔ must NOT loop through `handle_cfg_set` or manufacture command
// strings, so the SETTINGS screen's rows, its editor and its SAVE/DISCARD/RELOAD actions all speak to that service.
// ⇒ the model holds a POINTER to it rather than emitting a request the way it does for the inbox (`InboxReq`): the
//   request idiom exists because `Inbox` needs `g_node`, and this service needs nothing at all. Keeping the calls HERE
//   is what puts every gesture meaning AND every save/discard landing under the native gate.
// ⛔ THE MARKER IS `config_unsaved`, NEVER `dirty` — `UiState::dirty` (below) already means "a repaint is owed", and
//   this file is the one place both are read. That collision is why §3.6.1 named the field in advance.
#include "firmware_config_service.h"
// ★★★ §UI-15 slice 6 — the STATIC-JOIN pure unit: the profile-list carrier this file stores in `UiState`, the four
//     store-state panel strings, the confirmation's value lines, the waiting screen's two headlines and THE
//     FOUR-TERM CORRELATION RULE. ⛔ It is a file of its own rather than more of this one so that the rule gets its
//     OWN mutation battery (`--target=uijoin`) — see its header. ⓘ It costs this unit NO new dependency:
//     `device_nv.h` already arrives through `firmware_config_service.h` above.
#include "firmware_ui_join.h"
#include "firmware_ui_input.h"

namespace mrui {

inline constexpr uint32_t kBlankMs      = 15000;
inline constexpr uint8_t  kMaxTeamRows  = 8;    // spec §11: a 3-10 member group; the snapshot reports the TRUE total too
inline constexpr uint8_t  kMaxInboxRows = 8;
inline constexpr uint8_t  kLabelCap     = 14;   // display-clamped teammate label

// ⚠ ALIASES, not UI enums — they ARE `MESHROUTE_NS::InboxKind` / `MESHROUTE_NS::InboxEraseResult`, exactly as
// `FailReason` below is `MESHROUTE_NS::SendFailReason`. They exist so this header names its cross-namespace
// dependencies in one place; ⛔ never redeclare, renumber or mirror either.
using InboxKind        = MESHROUTE_NS::InboxKind;
using InboxEraseResult = MESHROUTE_NS::InboxEraseResult;

// ★ §UI-14 appends SETTINGS to the cycle (spec §3.1): STATUS -> TEAM -> INBOX -> SEND -> SETTINGS, or
//   STATUS -> INBOX -> SETTINGS on a `!MR_FEAT_TEAM` build. `next_screen` is the ONE place that skips the team slots,
//   and SETTINGS is deliberately NOT one of them — the four covered fields exist on every build.
enum class Screen  : uint8_t { status = 0, team, inbox, send, settings, count };
enum class Compose : uint8_t { none = 0, dm, channel };

// ================================================================================= §UI-14 — the SETTINGS screen
// ★★★ THE STATE THAT SEPARATES `short`'s TWO MODES, AND IT IS A TERNARY BECAUSE THE DOMAIN IS.
// Spec §3.6.2: *"`short` advances rows OR CYCLES A FINITE VALUE WHILE EDITING; `double` enters/accepts"* — so one
// gesture has two meanings and something has to say which. The three states, and each is reachable and distinct:
//   `closed`   — SETTINGS is not the current screen. `short` is the ordinary list-aware advance of §3.2, and the
//                editor MUST NOT survive here: leaving the screen closes it (see `sync_settings`).
//   `browsing` — the menu is up. `short` walks the rows; `double` ENTERS a value row or performs an action row.
//   `editing`  — one value row is open. `short` CYCLES that row's finite value in the RAM draft; `double` ACCEPTS.
//   `provisioning` — §UI-15: §3.6.3's sub-view OWNS THE BODY, so it owns the press. WHICH provisioning state is up is
//                NOT encoded here — it is the separate `Provision` enum below (see it for why).
// ⓘ It is the `InboxModal` precedent one screen over (§UI-7D slice B: short toggles, double activates, and the modal
//   state is what makes the two readable), deliberately rather than a second idiom (U1/U3).
// ⛔ NOT derived from `screen == Screen::settings`: that predicate cannot express "editing", and a bare `bool editing`
//   beside it would leave the third state unnamed — the binary-test-over-a-ternary-domain defect this arc has hit five
//   times (see `tools/probe_ui_model_mutations.py`'s `arm_backup` roll-call).
enum class Settings : uint8_t { closed = 0, browsing, editing, provisioning };

// ================================================================================ §UI-15 slice 4 — PROVISIONING (§3.6.3)
// ★★★ THE PROVISIONING SUB-STATE, AND IT IS ITS OWN ENUM RATHER THAN MORE `Settings` ARMS (plan §5, adopted). The two
//     questions are genuinely different — *which view owns the press* (`Settings`) and *where inside provisioning we
//     are* (here) — and folding the second into the first would give `Settings` eleven arms, every one of which the
//     editor's `short`/`double` switch would then have to name.
// ⛔⛔ AND IT IS ⛔ NEVER A `bool in_provision`. `Settings`'s own block above names the defect: a binary test over a
//     domain that is not binary, five occurrences this arc. Eight states cannot be a flag.
// ★ THE INVARIANT, and it is enforced by TWO primitives and nowhere else (`enter_provision` / `close_provisioning`,
//   both of which delegate the SUB-STATE half to the pure `provision_reset_on_leave` below):
//   `Settings::provisioning` ⟺ `Provision != closed`. ⇒ never assign either field at a call site.
//
// WHAT IS REACHABLE IN THIS SLICE vs WHAT ARRIVES WITH THE ADAPTERS — stated per arm because a half-built state
// machine that does not say which half it is, is the thing this project registers as "a success that isn't". ★ The
// ruling is [[B222]]'s: a transition lands WITH the flow behind it, never one slice ahead of it.
//   `closed`         — LIVE. Not in provisioning. The SETTINGS menu owns the press.
//   `menu`           — LIVE: the child list (§6, `provision_rows`), the cycling cursor, and BACK (-> `closed`).
//                      ⓘ §UI-15 slice 6 gave JOIN NETWORK its landing too (`provision_menu_gesture`).
//   `create_confirm` — LIVE (slice 5): §3.6.3's confirmation, opened on BACK, `short` toggles, `double` performs.
//   `create_result`  — LIVE (slice 5): the transaction's verdict, and it is entered ONLY by `run_create_team`, i.e.
//                      only by the act that established it. Terminal; either press returns to the menu.
//   `join_select`    — LIVE (slice 6): the four `/mrjoin` slots (`join_sel_rows` over `UiState::join_list`, read ONCE
//                      on the transition) plus BACK. A non-`ok` store offers no slot and SAYS why (`join_store_head`).
//   `join_confirm`   — LIVE (slice 6): design §3.6.3's *"the complete values before confirmation, with BACK selected
//                      initially"*; `short` toggles, `double` performs. BACK returns to `join_select`, ⛔ not the menu.
//   `join_waiting`   — LIVE (slice 6): entered ONLY by a `started` transaction. It shows `JOINING`, becomes
//                      `STILL JOINING` at 60 s (⛔ never a failure), and either press LEAVES IT WITHOUT CANCELLING
//                      ANYTHING (plan §2.3 rule 4). Its completion is the four-term rule's (`join_push_correlates`).
//   `join_result`    — LIVE (slice 6): entered by the act that established it (`run_join_static` for a refusal or a
//                      failed save) or by a CORRELATED adopt (`on_join_push`). ⛔ By nothing else. Terminal.
enum class Provision : uint8_t {
    closed = 0, menu, create_confirm, create_result, join_select, join_confirm, join_waiting, join_result
};

// ★★ THE CONFIRMATION'S TWO ACTIONS, AND `back` IS FIRST BECAUSE §3.6.3 REQUIRES IT TO BE THE DEFAULT — verbatim:
//    *"opens a confirmation with `BACK` selected initially; reaching CREATE requires `short` then `double`"*. It is
//    the `InboxAction` shape one screen over (§3.5's delete costs the same deliberate sequence), deliberately the same
//    idiom and ⛔ never identified positionally (§B66: a two-member enum cannot be turned into a CONFIRM by somebody
//    adding a row).
// ⓘ §UI-15 slice 5 gave it its dispatch: `provision_confirm_gesture` toggles it on `short` and acts on it on
//   `double`, and `prov_confirm_label` renders it. The DEFAULT is unchanged and stays plan §5's closing pin — `back`
//   is the zero value, so a `ProvConfirm{}` anywhere is already the safe one, and every transition primitive
//   re-establishes it. ★ §UI-15 slice 6's JOIN confirmation REUSES this same pair (its labels are
//   `join_confirm_label`'s) rather than growing a second two-member enum — U1, as that note anticipated.
enum class ProvConfirm : uint8_t { back = 0, confirm };

// ★★★ §4's REFUSAL, AND IT IS A THREE-VALUED DOMAIN BECAUSE THE REMEDIES DIFFER — plan §4 in as many words: *"v1
//     CONFLATED TWO STATES"*. `conflict()` and `config_unsaved()` are different comparisons (see
//     firmware_config_service.h:21-22) with different escapes, so ⛔ one `bool provision_blocked` would be the same
//     binary-test-over-a-ternary-domain defect `Settings` warns about, and the panel would then have to guess which
//     remedy to print — the guess being SAVE, the one operation a conflict REFUSES.
enum class ProvBlock : uint8_t { none = 0, conflict, unsaved };

// ★★★★ §UI-15 slice 4 / [[B223]] — **THE CLOSE-ON-LEAVE RESET, AS A PURE FUNCTION**, and it is hoisted out of
//     `settings_follow_screen` for the [[B212]]/[[B220]] reason, the fourth time this arc: the decision was
//     UNREACHABLE where it was written — while the sub-view OWNS THE PRESS no gesture can move `_st.screen` out of
//     SETTINGS underneath it — so a guard living only at that call site is a guard no suite can drive and no mutation
//     can redden. ⛔ Stating that gap is not discharging the requirement. ⇒ THE DECISION LIVES HERE, where the suite
//     drives ALL EIGHT arms directly, and both call sites are forwards.
// ★ IT RESETS BOTH FIELDS, because they are two facts and not one. `Provision` says WHERE IN PROVISIONING we were: a
//   stale `create_confirm` surviving into the next visit would re-open a confirmation the operator never asked for,
//   which §3.6.5 rule 1 forbids in as many words. `ProvConfirm` says what a confirmation would OPEN ON: a stale
//   `confirm` would re-open it with the destructive choice already selected, one `double` from CREATE TEAM.
// ⓘ Returns whether anything CHANGED, so a caller repaints for a real close and not for every tick spent off-screen.
inline bool provision_reset_on_leave(Provision& arm, ProvConfirm& confirm) {
    const bool changed = arm != Provision::closed || confirm != ProvConfirm::back;
    arm = Provision::closed;
    confirm = ProvConfirm::back;
    return changed;
}

// ★★★ THE MENU'S ROWS AS STABLE IDENTITIES, NEVER ROW INDICES (§3.2.2's rule, and §B66's): the visible list is built
// per frame and THREE of its rows are CONDITIONAL (§UI-15 slice 5 made `provision` the third), so a row's meaning may
// not be derived from its position.
// Order is §3.6.2's own: the value rows, PROVISION, then the actions.
enum class CfgRow : uint8_t {
    ble_mode = 0, e2e_dm, intro_attach, mobile_autoregister,   // the four covered fields (§UI-13's CfgField)
    provision,                                                 // §3.6.3's entry point — see `settings_activate`
    reload,                                                    // CONDITIONAL: only while `conflict()` stands
    save, discard, back,
    count
};
inline constexpr uint8_t kMaxCfgRows = uint8_t(CfgRow::count);

// The visible list, built fresh from the two conditions. ⛔ A fixed array + a count, never a container: this is
// embedded code and the count is what `list_len` returns, so the two must come from ONE construction.
struct CfgRowList {
    CfgRow  row[kMaxCfgRows] = {};
    uint8_t n = 0;
    // ⛔ FAILS CLOSED (C2): an out-of-range index names NO row and the caller must do nothing, rather than being
    //    handed a plausible one. `back` would be the safe action, but "the safe action" is still an action the user
    //    did not choose — and `discard` is one row away from it.
    bool at(uint8_t i, CfgRow& out) const { if (i >= n) return false; out = row[i]; return true; }
};

// ★★ THE ONE ROW-LIST BUILDER (U1), and ALL THREE conditions are PARAMETERS rather than `#if`s so the native suite can
//    drive both arms of each. That is not a testing convenience — it is the only way §3.6.2's conditional BLE row can
//    be measured at all in a tree where the transport does not exist.
//   `ble_row`  — spec §3.6.2: *"row absent when UI-12 transport is not compiled"*. ⚠ `ble_mode` being a COVERED FIELD
//                of §UI-13's service is NOT a reason to render the row: the field is durable on every board, while the
//                row would offer the operator a setting that changes nothing this build can act on. The condition is
//                supplied at the ONE call site (`src/firmware_ui.cpp`, `MR_UI_BLE_ROW`), and is FALSE in every env in
//                the tree today — measured, not assumed: `MRBLE_NRF52` (src/device_ble.h) is the transport's own
//                predicate and no ESP32 env defines it, and the three envs that compile the OLED are all ESP32.
//   `conflict` — §3.6.1 requires a conflict to be resolved by *"`RELOAD` or `DISCARD`"*, and §3.6.2's table lists only
//                SAVE/DISCARD/BACK. ⇒ RELOAD is offered EXACTLY WHEN IT APPLIES rather than standing permanently in a
//                menu where it would mean nothing. ★ Reported as a decision, not as a reading of the table: without a
//                RELOAD row the ONLY escape from a conflict is DISCARD, which throws the operator's edits away — the
//                cost [[B192]]'s ruling explicitly declines to charge them.
//   `provision`— ★★★ THE THIRD CONDITIONAL ROW, and it is an OWNER RULING (2026-08-19, reported form): **the PARENT
//                row is HIDDEN when NO child is available.** Slice 4 left it unconditional, so `gateway_heltec`
//                (OLED=1, `MR_N_LAYERS=2` ⇒ neither child) offered a menu whose only entry was BACK — a row that
//                costs the operator a walk and a `double` to discover it offers nothing, which is the same complaint
//                [[B209]] makes about a refusing stub one level down. ⇒ the predicate is the CHILD LIST's own
//                (`provision_has_child`), so the parent and its children can never disagree.
inline CfgRowList settings_rows(bool ble_row, bool conflict, bool provision) {
    CfgRowList l{};
    if (ble_row) l.row[l.n++] = CfgRow::ble_mode;
    l.row[l.n++] = CfgRow::e2e_dm;
    l.row[l.n++] = CfgRow::intro_attach;
    l.row[l.n++] = CfgRow::mobile_autoregister;
    if (provision) l.row[l.n++] = CfgRow::provision;
    if (conflict) l.row[l.n++] = CfgRow::reload;
    l.row[l.n++] = CfgRow::save;
    l.row[l.n++] = CfgRow::discard;
    l.row[l.n++] = CfgRow::back;
    return l;
}

// Which rows EDIT A COVERED FIELD, and which `CfgField` each one is. ⛔ `default`-LESS so `-Werror=switch` fails the
// build when a row is added without stating whether it is a value row — the same discipline as `cfg_field_name`.
inline bool cfg_row_field(CfgRow r, mrfw::CfgField& out) {
    switch (r) {
        case CfgRow::ble_mode:            out = mrfw::CfgField::ble_mode;            return true;
        case CfgRow::e2e_dm:              out = mrfw::CfgField::e2e_dm;              return true;
        case CfgRow::intro_attach:        out = mrfw::CfgField::intro_attach;        return true;
        case CfgRow::mobile_autoregister: out = mrfw::CfgField::mobile_autoregister; return true;
        case CfgRow::provision: case CfgRow::reload: case CfgRow::save:
        case CfgRow::discard:   case CfgRow::back:   case CfgRow::count: return false;
    }
    return false;
}

// ★ THE ROW LABELS, in this PURE unit for the §B115 reason: a string built in `src/firmware_ui.cpp` is a string no
//   automated gate can read. ⚠ WIDTH IS A CONSTRAINT, NOT A PREFERENCE.
// ⛔ RE-DERIVED 2026-08-16 (§CHROME-4): this block used to read *"the panel is 21 small-font columns and the row
//    renders as `<marker><label padded to 10><space><value>`, so a label over 10 characters would push the value off
//    the panel"*. The rail moved the ordinary body to **19 columns** and the row now renders as
//    `<marker><label padded to 8><space><value>` while browsing and `<marker><label padded to 8>[<value>]` while
//    editing — see `draw_settings_screen`, where the arithmetic is written out **per row**, because the widest VALUE
//    (`periodic`, 8 columns) belongs to the shortest LABEL (`BLE`, 3) and a label x value worst case would be a bound
//    no row can actually reach. The longest label here is still `key attach` at exactly 10, which simply pushes its
//    own (3-column) value right: 1 + 10 + 1 + 3 = 15 of 19.
inline const char* settings_row_label(CfgRow r) {
    switch (r) {
        case CfgRow::ble_mode:            return "BLE";
        case CfgRow::e2e_dm:              return "DM crypt";
        case CfgRow::intro_attach:        return "key attach";
        case CfgRow::mobile_autoregister: return "auto reg";
        case CfgRow::provision:           return "PROVISION";
        case CfgRow::reload:              return "RELOAD";
        case CfgRow::save:                return "SAVE";
        case CfgRow::discard:             return "DISCARD";
        case CfgRow::back:                return "BACK";
        case CfgRow::count:               return "?";
    }
    return "?";
}
// The VALUE as the operator reads it. ⓘ `periodic` is `ble_mode == 2`: §3.6.2 keeps it OUT of the menu (it is to be
// retired from the firmware) but a value already persisted by serial/BLE must still be RENDERED HONESTLY rather than
// shown as one of the two the menu offers — the service's domain is 0..2 and this unit does not get to narrow it.
inline const char* cfg_value_text(mrfw::CfgField f, uint8_t v) {
    if (f == mrfw::CfgField::ble_mode) return (v == 0) ? "off" : (v == 1) ? "on" : (v == 2) ? "periodic" : "?";
    return (v == 0) ? "off" : (v == 1) ? "on" : "?";
}
// ★★ THE CYCLE, i.e. what `short` does WHILE EDITING. It is the MENU's domain, which for `ble_mode` is deliberately
//    NARROWER than the field's (§3.6.2 offers off/on only). A value outside the menu — `periodic`, or anything a
//    future writer persisted — steps to the FIRST offered value rather than being preserved: the operator is looking
//    at the row and pressed the button, so this is a deliberate edit, and it lands in the RAM DRAFT where SAVE or
//    DISCARD still decides its fate. ⛔ It does NOT narrow `cfg_field_valid`: that domain is shared with serial/BLE.
inline uint8_t cfg_menu_next(mrfw::CfgField f, uint8_t v) {
    switch (f) {
        case mrfw::CfgField::ble_mode:            return (v == 0) ? uint8_t(1) : uint8_t(0);   // off <-> on; 2 -> off
        case mrfw::CfgField::e2e_dm:
        case mrfw::CfgField::intro_attach:
        case mrfw::CfgField::mobile_autoregister: return v ? uint8_t(0) : uint8_t(1);
    }
    return 0;
}

// ============================================================ §UI-15 slice 4 — §6 AVAILABILITY: THE PROVISION MENU
// ★★★ THE CHILDREN OF §3.6.3, AS STABLE IDENTITIES — the `CfgRow` rule one level down, and it is LIVE here for the
//     same reason: two of the three rows are CONDITIONAL, so a row's meaning may not be derived from its position.
enum class ProvRow : uint8_t { create_team = 0, join_static, back, count };
inline constexpr uint8_t kMaxProvRows = uint8_t(ProvRow::count);

// ⛔ NOT `CfgRowList` GENERALISED INTO A TEMPLATE, and that is C1 rather than laziness: turning `CfgRowList` into
//    `RowList<CfgRow, kMaxCfgRows>` is a refactor of a shipped type, and this slice is a feature — the two may not
//    ride together. Both are four lines and both FAIL CLOSED identically; if a third row list ever appears, unify
//    them in a slice of their own.
struct ProvRowList {
    ProvRow row[kMaxProvRows] = {};
    uint8_t n = 0;
    // ⛔ FAILS CLOSED (C2), exactly as `CfgRowList::at` does: an out-of-range index names NO row and the caller must
    //    do nothing. Here the row one press from `back` is CREATE TEAM, which REPLACES an existing membership.
    bool at(uint8_t i, ProvRow& out) const { if (i >= n) return false; out = row[i]; return true; }
};

// ★★★★ PLAN §6 — AVAILABILITY IS GOVERNED BY THE ACTUAL CHILD PREDICATE, AND THE TWO CHILDREN DO NOT SHARE ONE.
// ★ Per [[B209]] an unsupported child is **HIDDEN** (the conditional-row pattern `settings_rows` already uses for
//   `CfgRow::reload`), ⛔ never a refusing stub: a row that exists only to say "not on this build" is a menu that
//   lies about what it offers, and the operator pays a walk plus a `double` to find out.
// ⛔⛔ AND THE TWO PREDICATES ARE **SEPARATE PARAMETERS**, WHICH IS THE WHOLE POINT OF §6: plan v1 hid static join
//    when `MR_FEAT_TEAM` was off, and **static join has nothing to do with the team plane**. Measured, not assumed:
//    `handle_join` and `handle_create`/`handle_team` are ALL compiled out by `#if MR_N_LAYERS < 2`
//    (src/firmware_config.h:55, src/firmware_commands.cpp:1053), so
//      · JOIN NETWORK is available iff `MR_N_LAYERS < 2`;
//      · CREATE TEAM needs that AND the team plane (`MR_FEAT_TEAM`).
//    They coincide in every env in the tree today (`MR_FEAT_TEAM 0` arrives only with `MR_PROFILE_GATEWAY`, which
//    sets `MR_N_LAYERS=2`) — which is exactly why they must be two parameters here: a coincidence is not a rule, and
//    the native suite drives the combination the tree cannot build.
// ★ C3, and it is the `settings_rows(bool, bool)` pattern verbatim: the platform facts arrive as PARAMETERS, ⛔ never
//   as `#if` inside this pure unit, so both arms of each are natively drivable.
// ⓘ `back` is UNCONDITIONAL. Leaving must never depend on a build flag, a store or a service — the same rule
//   `ui14-open`'s "BACK still works" case pins one level down. ⇒ a build with neither child still opens a menu the
//   operator can leave; it just offers nothing, which is the honest answer for `gateway_heltec` (OLED=1, N_LAYERS=2).
inline ProvRowList provision_rows(bool create_team, bool join_static) {
    ProvRowList l{};
    if (create_team) l.row[l.n++] = ProvRow::create_team;
    if (join_static) l.row[l.n++] = ProvRow::join_static;
    l.row[l.n++] = ProvRow::back;
    return l;
}

// ★★★★ §UI-15 slice 5 / OWNER RULING 2026-08-19 — **IS THERE A CHILD AT ALL?**, i.e. the PARENT row's own condition
//      (`settings_rows`'s third parameter). ⛔ IT IS DERIVED FROM THE CHILD LIST AND NOT RE-SPELLED AS
//      `create_team || join_static`: the two predicates would then be two authorities, and slice 6 (or §UI-16's
//      nearby-team child) could add a child the parent row never learned about — a menu entry that opens a sub-view
//      the operator cannot see. ⓘ `back` is UNCONDITIONAL, so it is excluded BY IDENTITY rather than by assuming it
//      is last: the list is built by position and §B66's rule is that position is never an identity.
inline bool provision_has_child(bool create_team, bool join_static) {
    const ProvRowList l = provision_rows(create_team, join_static);
    for (uint8_t i = 0; i < l.n; ++i)
        if (l.row[i] != ProvRow::back) return true;
    return false;
}

// ★ The child labels, in this PURE unit for the §B115 reason (a string built in `src/firmware_ui.cpp` is a string no
//   automated gate can read). ⚠ WIDTH IS A CONSTRAINT: an action row renders as `%c%s` in the rail's 19-column body,
//   so the bound is `1 + strlen <= 19` and `chrome4-audit` walks it.
inline const char* provision_row_label(ProvRow r) {
    switch (r) {
        case ProvRow::create_team: return "CREATE TEAM";
        case ProvRow::join_static: return "JOIN NETWORK";
        case ProvRow::back:        return "BACK";
        case ProvRow::count:       return "?";
    }
    return "?";
}

// ================================================ §UI-15 slice 5 — the INTENT, its TYPED ANSWER and the ONE SEAM
// ★★★ THE INTENT IS PURE AND MODEL-OWNED (plan §2.1), AND IT IS ⛔ NOT A `TeamRequest`. What the operator asked for is
//     "create a new team"; a `TeamRequest` carries a build floor, a PHY, a key pair and a mint flag — device facts this
//     unit cannot reach and must not invent. The ADAPTER (`src/firmware_ui_prov.h`, PURE and natively compiled) is what
//     turns one into the other, and it is where plan §2.1's `phy.present = false` rule lives.
// ⓘ A one-field struct rather than a bare enum, deliberately: slice 6's join intent carries the SELECTED PROFILE with
//   it, and a carrier that cannot grow a field is how a second parallel intent type gets born instead (U1).
// ★★ §UI-15 slice 6 GREW IT EXACTLY AS THAT NOTE ANTICIPATED, and the growth is the point: `join_static` is a SECOND
//    op on the SAME seam, so the `default`-less dispatch in `UiProvisionAdapter::perform` forces a reader to state
//    what performs it. ⛔ A parallel `IUiJoin` seam would have been the second dispatch U1 forbids.
enum class UiProvOp : uint8_t { none = 0, create_team, join_static };
struct UiProvIntent {
    UiProvOp op = UiProvOp::none;
    // ★★★ THE SELECTED PROFILE, CARRIED WHOLE (U2) — ⛔ never a slot INDEX the adapter would re-read the store for.
    //     WHAT WAS SHOWN IS WHAT IS JOINED: design §3.6.3 requires the complete values on the panel BEFORE the
    //     confirmation, so re-reading `/mrjoin` at CONFIRM time would let a serial `joinprofile set` between the two
    //     presses join something the operator never saw. ⓘ MEANINGFUL ONLY when `op == join_static`.
    // ⓘ It is the STORE's own record type, integral Hz and all: the ONE Hz -> MHz/kHz conversion belongs to the
    //   REQUEST and is `mrfw::join_request_from_profile`'s, which the adapter calls (U2 — one conversion path).
    mrnv::JoinProfile join{};
};

// ★★★ THE TYPED ANSWER, AND THE FOUR OUTCOMES ARE FOUR DIFFERENT THINGS TO SAY — ⛔ never one `bool ok`. `created` is
//     the only one that may show an id; `phy_differs` is the owner's REFUSAL WITH A REMEDY (plan §2.1: the operator is
//     sent to the serial console, because the OLED create is a MEMBERSHIP operation and may not retune); `save_failed`
//     is the durable write that came back false — §3.6.5's "no screen claims success before the save returns" is what
//     makes it a state of its own rather than a flavour of `refused`; `refused` is a STAGING refusal, which by the
//     transaction's contract spent zero writes and zero airtime.
// ★★★ §UI-15 slice 6 ADDS THREE, AND ⛔ NONE OF THEM REUSES `created`/`refused`: a JOIN is not a CREATE, and the two
//     verbs' words must differ on the panel or the operator cannot tell which operation answered. The three:
//       `joining`      — the transaction STARTED, i.e. exactly one durable write happened and DAD has BEGUN.
//                        ⛔⛔ IT IS NOT "JOINED" AND MUST NEVER BE RENDERED AS ONE (plan §2.3 rule 1): the real
//                        outcome arrives later as a push, and correlating it is `join_push_correlates`'s job.
//       `adopted`      — a CORRELATED `join_adopted` landed (plan §2.3 rule 2). ★ IT IS THE ONLY OUTCOME THAT MAY
//                        SHOW A NODE ID, and it is written by `on_join_push` and by nothing else — so no earlier
//                        state can produce it, which is §8 pin 2 for the asynchronous half.
//       `join_refused` — a validation / load refusal from the transaction: ZERO writes, ZERO airtime.
enum class UiProvOutcome : uint8_t {
    none = 0, created, phy_differs, save_failed, refused, joining, adopted, join_refused
};
// ⓘ `reason` IS A POINTER TO STATIC STORAGE and never an owned buffer: the adapter fills it from
//   `mrfw::prov_err_name`, whose arms are string literals. ⛔ It is never null — `""` is the "nothing to add" value, so
//   the renderer needs no null test and cannot print a stray pointer. Keeping the token as the SERVICE's own name is
//   what stops a second `ProvErr`-to-text table being born in the UI (U1).
struct UiProvAnswer {
    UiProvOutcome outcome = UiProvOutcome::none;
    // ★ §UI-15 slice 6: the ADOPTED node id, and ⛔ MEANINGFUL ONLY when `outcome == adopted`. It is a SEPARATE
    //   field from `team_id` because they are different planes' identities and one carrier holding "the id" would be
    //   the display-shaped field that eventually makes an addressing decision (the rule that killed B48).
    // ⓘ COST, MEASURED: it lands in the padding after `outcome`, so `sizeof(UiProvAnswer)` stays 16 on the host and
    //   12 on a 32-bit board.
    uint8_t       node_id = 0;
    uint32_t      team_id = 0;     // ⛔ MEANINGFUL ONLY when `outcome == created`
    const char*   reason  = "";    // a STATIC token; "" when the outcome carries no second line
};

// ★★ THE SEAM THE CONFIRMATION PERFORMS THROUGH, and it is the `ConfigService` pattern rather than the `InboxReq`
//    one (U3): the act is SYNCHRONOUS and returns a verdict, so a request/poll shape would make "the result is
//    rendered only after the transaction returned" a matter of scheduling instead of a matter of structure.
// ⓘ NULL IS A REAL STATE and it FAILS CLOSED — an unattached model refuses the act and says so (see `run_create_team`).
//   That is what a build with no provisioning children, or a partially-wired probe, looks like.
struct IUiProvision {
    virtual ~IUiProvision() = default;
    virtual UiProvAnswer perform(const UiProvIntent& intent) = 0;
    // ★★ §UI-15 slice 6 — THE SELECT SCREEN's ONE READ, and it is a METHOD rather than a fourth `UiProvOp` because
    //    its answer is a 112-byte RECORD, not a verdict: stuffing it into `UiProvAnswer` would make every create
    //    refusal carry a profile store it has nothing to do with.
    // ⛔ IT IS CALLED ONCE, ON THE `menu -> join_select` TRANSITION, and never per tick or per page: it reads flash.
    //    The result is held in `UiState` (frozen with the frame) precisely so the renderer never asks.
    virtual UiJoinList profiles() = 0;
};

// ★ THE CONFIRMATION'S TITLE — design §3.6.3's own name for the operation (*"`CREATE NEW TEAM` opens a confirmation"*),
//   ⛔ not a second wording of the menu row it was reached from. 15 of the rail's 19 columns.
inline constexpr const char* kProvCreateTitle = "CREATE NEW TEAM";
// The confirmation's two actions, by IDENTITY (§B66: ⛔ never by position). Design §3.6.3 names both: *"a confirmation
// with `BACK` selected initially; reaching CREATE requires `short` then `double`"*.
inline const char* prov_confirm_label(ProvConfirm a) {
    switch (a) {
        case ProvConfirm::back:    return "BACK";
        case ProvConfirm::confirm: return "CREATE";
    }
    return "?";
}
// ★★★ THE RESULT'S TWO LINES. ⛔ THE HEADLINE IS NEVER DERIVED FROM ANYTHING BUT THE OUTCOME THE TRANSACTION RETURNED
//     — that is §8 pin 2 ("no screen claims success before the save returns") expressed as code: `created` is set by
//     `run_create_team` from `ProvVerdict::applied` and by nothing else, so no earlier state can produce this string.
// ★ `SAVE FAILED` is §UI-13's RULED string and is CALLED, never re-spelled (U1) — the same treatment `settings_note`
//   gives it one screen up. `PHY DIFFERS` / `USE SERIAL` is the owner's ruled refusal (plan §2.1), split across the
//   two rows by §7.1 rule 5 because the ruled sentence is 24 columns against a 19-column body: ⛔ the panel may not
//   clip an actionable remedy, and neither half may be reworded.
// ⚠ REPORTED, NOT INVENTED: design §3.6.3 requires "explicit success/failure states" and names the SUCCESS CONTENT
//   (the full id + the short fingerprint) but rules no LEXEME for the two states themselves. `TEAM CREATED` /
//   `CREATE REFUSED` are this file's house style applied to a state the design demands; they are one line each and are
//   pinned by a native case, so an owner ruling changes them here and nowhere else.
inline const char* prov_result_head(const UiProvAnswer& a) {
    switch (a.outcome) {
        case UiProvOutcome::created:     return "TEAM CREATED";
        case UiProvOutcome::phy_differs: return "PHY DIFFERS";
        case UiProvOutcome::save_failed: return mrfw::cfg_save_panel(mrfw::CfgSave::nv_failed);   // "SAVE FAILED"
        case UiProvOutcome::refused:     return "CREATE REFUSED";
        // ★★ §UI-15 slice 6. ⛔ `joining` CALLS the waiting screen's own word rather than re-spelling it (U1): one
        //    declaration of `JOINING`, so the transaction's verdict and the screen the operator is looking at can
        //    never drift apart. ⛔⛔ AND THERE IS NO `JOINED`-SHAPED STRING ON THIS PATH AT ALL — plan §2.3 rule 1.
        case UiProvOutcome::joining:      return join_wait_head(/*still=*/false);
        // ⚠ REPORTED, NOT INVENTED (the slice-5 precedent): design §3.6.3 requires *"`JOINING`, adopted/refused, and
        //   the resulting node id"* and names the STATE `adopted`, but rules no LEXEME for the headline.
        //   `ADOPTED` is the design's own word; `JOIN REFUSED` is `CREATE REFUSED`'s house-style twin. Both are one
        //   line each and pinned by a native case, so an owner ruling changes them here and nowhere else.
        case UiProvOutcome::adopted:      return "ADOPTED";
        case UiProvOutcome::join_refused: return "JOIN REFUSED";
        case UiProvOutcome::none:        return "";
    }
    return "";
}
// The SECOND row: the remedy, or the typed reason the transaction gave. ⓘ `created` has none — its second row is the
// new team's id, which is a VALUE and not a string this unit owns.
inline const char* prov_result_detail(const UiProvAnswer& a) {
    switch (a.outcome) {
        case UiProvOutcome::phy_differs: return "USE SERIAL";
        // ★ The transaction's own guarantee, stated to the operator rather than left to be inferred: a failed save
        //   applies NOTHING (`ProvVerdict::nv_failed` — zero live applies, zero airtime). ⛔ It is NOT a claim about
        //   the flash bytes; `firmware_provisioning_service.h`'s header forbids that claim in as many words.
        case UiProvOutcome::save_failed: return "NOTHING CHANGED";
        case UiProvOutcome::refused:     return a.reason;      // the SERVICE's own token (U1) — never a second table
        // ★ §UI-15 slice 6: the TRANSACTION's own `JoinErr` token, exactly as `refused` carries `ProvErr`'s — ⛔ never
        //   a second JoinErr-to-text table. ⓘ `adopted`'s second row is the NODE ID, which is a VALUE and not a
        //   string this unit owns; `joining` has nothing to add, and the waiting screen says its own words.
        case UiProvOutcome::join_refused: return a.reason;
        case UiProvOutcome::joining:
        case UiProvOutcome::adopted:
        case UiProvOutcome::created:
        case UiProvOutcome::none:        return "";
    }
    return "";
}

// ★★★ SPEC §3.3's THREE LITERALS, AND THEY ARE THREE FACTS RATHER THAN ONE STATE.
// `config_unsaved` and `reboot_required` are INDEPENDENT (§3.6.1: a save that needs a reboot is durably saved and NO
// LONGER unsaved), and `conflict` is a third comparison again — so they are rendered from three separate predicates
// and never collapsed into a single "config is odd" flag.
// ⛔ `CFG! RELOAD` IS NOT RE-SPELLED HERE: it is §UI-13's ruled string and is CALLED from `cfg_save_panel`, so the
//    panel text has exactly one declaration (U1). Only the strings §UI-14 owns are written out below.
inline const char* cfg_marker_text(bool unsaved, bool conflict) {
    if (conflict) return mrfw::cfg_save_panel(mrfw::CfgSave::conflict);   // "CFG! RELOAD" — the SERVICE's string
    return unsaved ? "CFG* UNSAVED" : "";
}
inline constexpr const char* kCfgRestartText = "RESTART NEEDED";   // §3.3: a durable save whose effect is boot-only

// ★★★ §UI-7D slice B — THE INBOX DETAIL MODAL'S GEOMETRY (spec §3.5), DERIVED AND NOT RESTATED ([[B120]]).
// Two body rows of the panel's small-font columns expose `kDetailCols * kDetailBodyRows` characters per page, so the
// largest stored body needs `ceil(inbox_max_body / that)` pages. ⛔ The page count is a CONSEQUENCE of those three
// numbers; it is asserted by a native case and appears in no arithmetic here.
//
// ★★★★ §CHROME-4 / design §7.3 — **21 → 19 COLUMNS, AND THE PAGE COUNT IS RE-DERIVED, NOT RE-CLAMPED.** The
//      navigation rail takes `x = 0..9`, so the ordinary body starts at `x = 12` and is 116 px wide = 19 small-font
//      columns (`src/firmware_ui.cpp`'s `kBodyX` / `kBodyCols`). ⛔ THE WRAP HAD TO MOVE **HERE**, AT THE MODEL'S
//      FREEZE POINT, and not merely at the draw origin: `refresh_detail_page` slices the body into `detail_line[]`
//      when the page is FROZEN, so a renderer that shifted x while the model still wrapped at 21 would (a) let u8g2
//      clip the last two characters of every full row and (b) make `detail_pages` a LIE — a count computed from 42
//      characters a page over rows the panel can only show 38 of.
//      ⇒ 19 x 2 = **38** characters a page, and `ceil(241 / 38)` = **7** pages for the largest body (was 6 at 42).
//      ⓘ Every one of those figures is DERIVED below; the only edited number is `kDetailCols`.
inline constexpr uint8_t  kDetailCols      = 19;
inline constexpr uint8_t  kDetailBodyRows  = 2;
inline constexpr uint8_t  kDetailPageChars = uint8_t(kDetailCols * kDetailBodyRows);
inline constexpr uint8_t  kDetailMaxPages  =
    uint8_t((MESHROUTE_NS::protocol::inbox_max_body + kDetailPageChars - 1) / kDetailPageChars);
inline constexpr uint32_t kDetailPageMs    = 2000;   // spec §3.5: long bodies advance every 2 s and CYCLE

// ★★ THE ONE DISPLAY-BYTE SANITIZER (U1), shared by the preview row and the detail body. An inbox body is NOT a C
// string — it is raw record bytes that may contain NUL, control bytes and high-bit bytes — and spec §3.5 requires
// unsupported bytes to be replaced VISIBLY rather than treated as control characters. `'.'` is the policy the UI-7
// preview already shipped; this is that expression, moved to where both callers can reach it, not a second one.
inline char ui_display_byte(uint8_t b) { return (b >= 0x20 && b < 0x7f) ? char(b) : '.'; }

// ★★ §B66 CLOSED HERE 2026-08-05 (UI-7) — THE COUNT IS NOW DERIVED FROM THE TABLE, so the two cannot disagree.
// The LAST row of a compose list is `back, don't send` and the model identifies it POSITIONALLY (`cursor + 1 == n`),
// so a table that grows without its count turns `back` into a SEND. UI-6 bound them with a `static_assert` across a TU
// boundary — a build failure instead of a mis-send, which was the right interim but still TWO declarations. UI-7 needs
// the strings in a PURE header anyway (`ui_compose_send_line` composes the console line and the native suite asserts
// it byte-for-byte), so the tables moved here and the counts are `sizeof`-derived. ⇒ one declaration, no assert to
// keep in step, and B66's own "durable cure: one table with the count derived from it" verbatim.
// ★ Owner-fixed strings (plan §"Constants fixed by the owner"). ⛔ The emergency body is NOT a compose row — it has no
//   list and no cursor — so it lives beside them rather than inside either table.
inline const char* const kDmTexts[]      = { "Are you OK?",      "I'm OK",   "back, don't send" };
inline const char* const kChannelTexts[] = { "Got your message", "All good", "back, don't send" };
inline constexpr uint8_t kDmTextCount      = uint8_t(sizeof kDmTexts      / sizeof kDmTexts[0]);
inline constexpr uint8_t kChannelTextCount = uint8_t(sizeof kChannelTexts / sizeof kChannelTexts[0]);
// ★ The SENDABLE prefix of each table — everything but the trailing `back` row. Derived, never restated, for exactly
//   the B66 reason: this is the bound `ui_compose_send_line` refuses on, so a hand-written `2` here would be the same
//   positional coupling one level down. An index at or past it names `back` (or nothing) and must REFUSE, not clamp.
inline constexpr uint8_t kDmSendableTexts      = uint8_t(kDmTextCount - 1);
inline constexpr uint8_t kChannelSendableTexts = uint8_t(kChannelTextCount - 1);
inline constexpr const char* kEmergencyText = "I'm in danger";

// The model NEVER sends — it ASKS. firmware_ui.cpp drains the request, performs the send and feeds back a typed outcome.
enum class SendKind : uint8_t { emergency = 0, dm, channel_canned };
struct SendReq { SendKind kind = SendKind::emergency; uint8_t peer_id = 0; uint8_t text_index = 0; };

struct TeamRow {
    uint8_t  id = 0; uint32_t last_heard_s = 0; int16_t score_q4 = 0; uint8_t hops = 0;
    char     label[kLabelCap + 1] = {};   // resolved name / 0xhash / bare id, already clamped (spec §3.3)
};
// ★★★★ THE PREVIEW ROW, AND ITS IDENTITY IS THE PAIR `(kind, seq)` — spec §3.5/§6.1, verbatim in substance: "not the
// visible row index, origin, message counter or body. DM and channel sequence spaces are independent, so `seq` alone is
// insufficient."
// ⛔ WHAT THIS STRUCT USED TO CARRY: `bool is_dm`, and it was LOAD-BEARING — `InboxRowBudget::add()` branched on it to
//    pick the per-kind ring. It is REPLACED by `kind` rather than joined by it: two kind fields are two authorities that
//    can drift, and the failure that drift produces is deleting from the OTHER STORE while the panel shows this one.
//    ⇒ rendering AND budgeting both derive from `kind`. ⚠ A grep for `is_dm` still returns hits and ALL of them are
//    COMMENTS recording the removal — [[B77]]'s trap, so read the sentence rather than counting the match; no CODE in
//    the tree names it.
// ⓘ `seq == 0` means NO IDENTITY: store sequences are 1-based (inbox.h:129, "seq 0 is the 'before everything' pull
//    cursor"), and `Inbox::erase` documents `seq == 0` as `not_found`. `note_inbox_cursor` therefore refuses to select
//    such a row at all, rather than carrying a selection that can only ever resolve to somebody else's record.
// ⓘ `text` stays a RENDERING field — 20 display characters of preview — and is never an identity (spec §6.1).
struct InboxRow {
    InboxKind kind = InboxKind::dm;
    uint32_t  seq  = 0;
    uint8_t  channel_id = 0; uint32_t rx_age_s = 0;
    // ⓘ 20 bytes of PREVIEW plus its NUL. §CHROME-4 draws it through `%-8.8s`, so what reaches the panel is an
    //   explicitly bounded 8 columns of the rail's 19-column row — the buffer is the CARRIER's capacity, not the
    //   display's, and the whole body is one press away in the detail modal (spec §3.5).
    char     text[21] = {};
};

struct UiSnapshot {
    uint32_t now_ms = 0;
    // ★★ §B108 round 2 — TWO THINGS, PUBLISHED TOGETHER, and that togetherness is the point. `unread_*` is the
    // DISPLAY count, clamped to `kUnreadCap` because the bar has three digits; `arr_*` is the uncapped ARRIVAL SERIAL
    // those digits were derived from. `UiInboxCounters::publish` writes all four in one call, so a frame can never
    // freeze a serial that its own rendered number did not reflect. Never assign one without the other.
    uint16_t unread_dm = 0, unread_ch = 0;
    uint32_t arr_dm = 0, arr_ch = 0;
    uint32_t last_dm_age_s = UINT32_MAX, last_ch_age_s = UINT32_MAX;
    uint8_t  team_shown = 0, team_total = 0;      // shown <= kMaxTeamRows; total = rt_team_count() (spec §3.3)
    TeamRow  team[kMaxTeamRows] = {};
    uint8_t  inbox_shown = 0; uint16_t inbox_total = 0;
    InboxRow inbox[kMaxInboxRows] = {};
    uint8_t  my_team_id = 0; uint32_t team_id = 0;
    int32_t  batt_mv = -1;                        // <0 = unavailable -> render "--", never a guess
    bool     team_build = true;
    // ★ §UI-14: is the companion BLE transport COMPILED INTO THIS BUILD? It rides the snapshot for exactly the reason
    //   `team_build` does (U3): a build-time fact the PURE model must branch on, published once at the one call site
    //   that knows it (`src/firmware_ui.cpp`) so the model stays `#if`-free and both arms are natively drivable.
    //   ⛔ FALSE by default, which is §3.6.2's own ruled state for "the transport is not compiled" — not an invented
    //   fallback. It is false in every env in the tree today (see `settings_rows`).
    bool     ble_row = false;
    // ★★★ §UI-15 slice 4 — §6's TWO CHILD PREDICATES, one field each, published from the ONE site that knows them
    //     exactly as `team_build` / `ble_row` / `mobile_build` are (U3). See `provision_rows` for what each one IS and
    //     for why they may NEVER be collapsed into one "provisioning is supported" flag.
    // ⛔ FALSE BY DEFAULT, and that direction is ruled rather than convenient: a false parameter HIDES the child, so a
    //    build (or a partially-wired probe) that has published nothing offers no operation it may be unable to
    //    perform. The opposite default would offer CREATE TEAM on a gateway.
    // ⓘ DECLARED HERE, PUBLISHED WITH THE SCREENS (slice 5) — the §CHROME-1 precedent immediately below, and the same
    //   argument: nothing in THIS slice renders a child, and the model must be able to EXPRESS the fact before the
    //   site that knows it is asked for it. ⚠ Until then both read FALSE on device, i.e. the PROVISION menu offers
    //   only BACK. Stated rather than discovered.
    // ⓘ COST, MEASURED not assumed (the §CHROME-1 placement rule below, applied): the two bools land in the padding
    //   the tail already carried, so `sizeof(UiSnapshot)` stays **608** and `sizeof(UiState)` stays **72** — this
    //   slice adds ZERO bytes to either, on a struct instantiated twice on the OLED envs.
    bool     prov_create_team = false;   // `MR_N_LAYERS < 2 && MR_FEAT_TEAM` — §3.6.3's primary path
    bool     prov_join_static = false;   // `MR_N_LAYERS < 2`                — §3.6.3's secondary path

    // ================================================================== §CHROME-1 — the status strip's new authorities
    // ★★★ DEFINED HERE, PUBLISHED IN SLICE 3. Design §8.2's chrome projection is PURE and may not touch `g_node`, but
    //     every fact below lives behind a `g_node` accessor. ⇒ slice 1 declares the fields and builds the projection
    //     from them; `src/firmware_ui.cpp`'s `build_snapshot` gains the four assignments in slice 3. Until then these
    //     hold their declared defaults, which are the honest "nothing established" states — ⛔ NOT plausible values.
    //
    // ★ IS THERE A MOBILE-HOME PLANE ON THIS BUILD AT ALL? Published from `MR_FEAT_MOBILE` at the ONE site that knows
    //   it, exactly as `team_build` and `ble_row` above are (U3), so the model stays `#if`-free and both arms are
    //   natively drivable. `gateway_heltec` is a REAL build with OLED=1 and MOBILE=0, so this is not hypothetical.
    //   ⛔ FALSE by default: design §4.2 rules that a non-mobile build draws the home slot BLANK, never crossed —
    //      "not applicable" must not render as a fault.
    bool     mobile_build = false;
    // ★★ THE CORE STATE, NAMED NOT MIRRORED (see the `node.h` include note above). Authority:
    //    `Node::mobile_home_link()` — "a recent correlated bidirectional exchange with the SELECTED home".
    // ⛔⛔ IT IS NOT CONNECTIVITY. §4.2: never labelled or described as "connected", "mesh online" or "last packet
    //    heard"; a team message, a foreign beacon or a one-way receive must not refresh it. `node.h`'s own
    //    `home_link_name` block says the word "connected" is forbidden on every surface, and this is a surface.
    MESHROUTE_NS::Node::MobileHomeLink home_link = MESHROUTE_NS::Node::MobileHomeLink::unknown;
    // Authority: `Node::mobile_home_confirmed_ever()`. ★ SEPARATE FROM THE AGE ON PURPOSE — node.h:609's own note: a
    // surface must be able to OMIT the field rather than render a 0 that reads as "confirmed just now".
    bool     home_confirmed_ever = false;
    // ★ THE TEAM CHANNEL **CONTENT** KEY (§4.4). Authority: `Node::team_channel_key_present()`.
    // ⛔ It does NOT mean the node has its own crypto identity, and NOT that peer public keys are cached. Those are
    //   different facts with different remedies, and one icon may only carry the one it measures.
    // ⓘ DECLARED HERE, BESIDE THE HOME BOOLS RATHER THAN AFTER THE 64-BIT AGE, AND THE PLACEMENT IS MEASURED: this is
    //   `node.h`'s padding-placement rule applied to `UiSnapshot`. The struct's tail was `...team_build, ble_row` +
    //   2 pad bytes at 592; the four 1-byte members fill that hole and the two that follow it, so the 8-aligned
    //   `home_confirm_age_ms` lands at 600 and `sizeof(UiSnapshot)` is 592 -> **608**. Declared AFTER the age instead
    //   it measures **616** — the bool alone would have cost EIGHT. ⚠ `UiSnapshot` is instantiated twice on the OLED
    //   envs (the frozen `s_frame_snap` plus the per-tick build), so the 8 bytes are worth the two lines of ordering.
    bool     team_key_present = false;
    // ★★★★ `uint64_t`, AND THE TYPE IS THE WHOLE POINT (design §4.2, and the trap this slice was briefed against).
    //     Authority: `Node::mobile_home_confirm_age_ms()`, which is `uint64_t`. ⛔⛔ NEVER a `uint32_t` millisecond
    //     age: that cast re-creates the ~49.7-day wrap this project already fixed once (see node.h's §MH-S4 ledger
    //     line, where a u32 stamp was MEASURED as free and DECLINED under M3 for exactly this reason), and it would
    //     make a four-month-old confirmation render as a fresh one.
    // ⚠ THE SNAPSHOT'S OWN IDIOM INVITES THE BUG: `now_ms` above is `uint32_t` and `last_dm_age_s` is a `uint32_t`
    //   seconds age, so "age = now_ms - confirmed_ms" would be written naturally and would be wrong. ⇒ the age is
    //   carried WHOLE and is bucketed exactly once, in `ui_fmt_home_age` (src/firmware_ui_chrome.h).
    // ⓘ Meaningful only while `home_confirmed_ever` is true; 0 with `!ever` is "never", not "just now".
    uint64_t home_confirm_age_ms = 0;
};

// ★ THE UI-LOCAL UNREAD / RECENCY COUNTERS (spec §6). They were six file-static variables in firmware_ui.cpp, and
// they moved here for the §UI-6 GLUE reason: BOTH things that move them — a push arriving (`ui_route_recv_push`) and
// an Inbox frame actually reaching the panel (`FrameGate`) — are decided by code the native suite drives, and a
// counter no test can reach is exactly where §B108 hid. `have_*` keeps "never received" distinct from "received at
// t = 0"; without it a fresh boot renders "0s ago". Session-scoped by design: `Inbox` exposes no read cursor, and a
// reboot resetting them reads, for a glanceable bar, as "since you last looked".
inline constexpr uint16_t kUnreadCap = 999;   // the bar renders 3 digits — ★ A DISPLAY LIMIT ONLY, see below
struct UiInboxCounters {
    // ★★★ §B108 ROUND 2 — ARRIVAL IDENTITY IS SEPARATE FROM THE DISPLAY CAP, and conflating them re-created the very
    // harm B108 exists to prevent. The first fix stored a CAPPED `unread_*` and had a completed frame SUBTRACT the
    // count it had frozen. At saturation that loses a message: the frame freezes 999, an arrival during the eight
    // paging ticks cannot raise 999 (`if (unread < kUnreadCap) ++unread`), and the completion subtracts 999 -> 0. The
    // message is marked read having NEVER been on the panel. ⚠ The old code half-knew this — its clamp's own comment
    // named the saturation case. A CLAMP HIDES IT; IT DOES NOT FIX IT.
    // ⇒ `arr_*` counts EVERY arrival, monotonically and uncapped. `read_*` is a watermark that a COMPLETE and VISIBLE
    //   Inbox frame advances to the serial that frame FROZE. `unread_* = arr_* - read_*`, and the cap is applied
    //   nowhere but `publish`, on its way to the pixels.
    // ★ WRAPAROUND, chosen rather than inherited: `uint32_t` with UNSIGNED MODULAR subtraction. Unsigned overflow is
    //   defined in C++, so the serials themselves wrapping is harmless — the ONLY invariant is that the TRUE unread
    //   count stays below 2^32 between two reads. `uint16_t` would have cost 8 B less and wrapped at 65 536: a device
    //   left unattended for a week on a channel carrying one post per 10 s reaches ~60 000, the SAME ORDER, so it
    //   would have been "probably fine" — the reasoning class that produced this bug. 2^32 is 136 years at one
    //   arrival per second: unreachable, not merely unlikely.
    uint32_t arr_dm  = 0, arr_ch  = 0;    // monotonic arrival serials — NEVER capped, NEVER reset
    uint32_t read_dm = 0, read_ch = 0;    // read watermarks — moved ONLY by FrameGate::on_page
    uint32_t last_dm_ms = 0, last_ch_ms = 0;
    bool     have_dm = false, have_ch = false;

    uint32_t unread_dm() const { return arr_dm - read_dm; }   // modular; see the wraparound note above
    uint32_t unread_ch() const { return arr_ch - read_ch; }
    static uint16_t capped(uint32_t n) { return n < uint32_t(kUnreadCap) ? uint16_t(n) : kUnreadCap; }
    // ★ THE ONE CONVERSION PATH into a frame's snapshot (U2) — never rebuild these four at a call site, because the
    //   whole correctness argument is that the display count and the serial come from the SAME instant.
    void publish(UiSnapshot& s) const {
        s.unread_dm = capped(unread_dm()); s.arr_dm = arr_dm;
        s.unread_ch = capped(unread_ch()); s.arr_ch = arr_ch;
    }
};

// ★★★ THE INBOX ROW BUDGET (UI-7, spec §6.1), AND IT IS PURE FOR ONE MEASURABLE REASON: `Inbox::pull()` visits the DM
// block FIRST and the channel block SECOND, both oldest-first, with NO limit parameter of any kind (inbox.h:106-109
// — the only flow control is returning false from the callback). So "keep the newest 8" over one shared pool lets a
// chatty channel evict EVERY DM row, on a screen whose entire purpose is showing both. The spec calls that out and
// the plan repeats it; it is also exactly the kind of rule that reads as obviously-satisfied and is not.
// ⇒ TWO independent rings, `kMaxInboxRows / 2` each, filled newest-wins, and the whole thing is host-testable with a
//   handful of pushes. `firmware_ui.cpp` owns only the `pull()` trampoline and the text clamping.
// ⓘ The panel order stays BLOCK order (all DM rows, then all channel rows), never chronological: the two seq spaces
//   are independent and there is no shared clock to interleave on — spec §6.1 says adopting interleaving needs a
//   stated reboot/uptime rule first.
inline constexpr uint8_t kInboxRowsPerKind = uint8_t(kMaxInboxRows / 2);
class InboxRowBudget {
public:
    void reset() { _n_dm = 0; _n_ch = 0; }
    // Newest-wins: `pull` hands rows oldest-first, so once a ring is full each further row displaces the OLDEST it
    // holds. Shifting `kInboxRowsPerKind - 1` small structs is bounded and happens only past the cap.
    // ⓘ §UI-7D slice B: the ring is selected from `r.kind`, which is now the row's ONLY kind field. The predicate is
    //   deliberately written out at each use rather than cached in a second member — see InboxRow's block.
    void add(const InboxRow& r) {
        const bool dm = (r.kind == InboxKind::dm);
        InboxRow* buf = dm ? _dm : _ch;
        uint8_t&  n   = dm ? _n_dm : _n_ch;
        if (n < kInboxRowsPerKind) { buf[n++] = r; return; }
        for (uint8_t i = 1; i < kInboxRowsPerKind; ++i) buf[i - 1] = buf[i];
        buf[kInboxRowsPerKind - 1] = r;
    }
    // ★ THE ONE CONVERSION PATH into the snapshot (U2), like `UiInboxCounters::publish`. `total` is what `pull`
    //   VISITED, so the screen can say the list is truncated instead of implying it is complete (spec §6.1).
    void publish(UiSnapshot& s, uint16_t total) const {
        uint8_t k = 0;
        for (uint8_t i = 0; i < _n_dm && k < kMaxInboxRows; ++i) s.inbox[k++] = _dm[i];
        for (uint8_t i = 0; i < _n_ch && k < kMaxInboxRows; ++i) s.inbox[k++] = _ch[i];
        s.inbox_shown = k;
        s.inbox_total = total;
    }
    uint8_t dm_count() const { return _n_dm; }
    uint8_t ch_count() const { return _n_ch; }
private:
    InboxRow _dm[kInboxRowsPerKind] = {};
    InboxRow _ch[kInboxRowsPerKind] = {};
    uint8_t  _n_dm = 0, _n_ch = 0;
};

// ---------------------------------------------------------------------------------------------------- UI-3
// ★★ OWNER RE-RULED 2026-08-04: 120000 -> 30000. ⚠ THIS LINE AND THE CONSTANTS TEST ARE THE ONLY TWO PLACES THE
// NUMBER MAY APPEAR. Nothing else — no comment, no test, no doc line — restates it: the first §B78 write-up hardcoded
// "120 s" in prose and went stale the instant the owner re-ruled, so every other reference derives from `kEmgHoldMs`.
inline constexpr uint32_t kEmgHoldMs            = 30000;
inline constexpr uint32_t kCancelledMs          = 1000;
inline constexpr uint8_t  kEmgMaxTries          = 3;      // THREE TRANSMISSIONS, counted on acceptance
inline constexpr uint32_t kBlockedBackoffMinMs  = 2000;   // next_ms==0 policy: 2s, doubling, capped
inline constexpr uint32_t kBlockedBackoffMaxMs  = 30000;
inline constexpr uint32_t kArmToFireMs          = 3500;   // MUST match InputCfg::fire_ms (pinned by a test)

// ★★★ §B115 — THE TWO NUMBERS THE ALARM CARRIES, AND WHICH ONE IS THE TRUTH. Stated here because the shipped defect
// was EXACTLY a drift between them: the panel rendered `attempts() + 1` UNCONDITIONALLY while the airtime bound read
// `_tries`, so three posts on the wire displayed `2 of 3` -> `3 of 3` -> `4 of 3` and **`1 of 3` was NEVER SHOWN**
// (owner-measured on metal; the bound itself HELD — exactly three `M` ids went out, which is correct).
//   `_tries` — ★ THE SINGLE SOURCE OF TRUTH FOR THE LIMIT. It counts ACCEPTED transmissions, moves ONLY in
//              `on_send_accepted` (§B84's unbounded-airtime argument rests on that), and it is the ONLY value
//              `>= kEmgMaxTries` may ever be evaluated on. It is also what `NOT HEARD`'s detail line reports, because
//              there the number IS the measurement ("we transmitted three times and overheard nothing").
//   the ORDINAL (`UiModel::emg_attempt_ordinal`) — ★ PRESENTATION ONLY, and it may NEVER gate a send. It answers
//              "which of the three attempts is in flight right now", which is NOT `_tries`: an attempt that came back
//              `ctr == 0` IS in flight and is DELIBERATELY uncounted (spec §2.1 rule 2 — the bounded expiry spends it
//              later), so the in-flight ordinal is `_tries + 1` there and plain `_tries` once the attempt has been
//              ACCEPTED. An unconditional `+1` is the shipped bug; an unconditional `+0` prints `0 of 3` on the
//              `ctr == 0` first attempt. Both wrong answers have their own native control.
// ⚠ DELIBERATELY NOT CLAMPED to `kEmgMaxTries`. The raw render is the ONLY reason this defect was ever visible; a
//   clamp would have shown `2 -> 3 -> 3` — still wrong on every attempt — and hidden it permanently ([[B108]]'s
//   rejected "clamp instead of fix"). The ordinal is bounded by CONSTRUCTION instead: `on_outcome` refuses to queue a
//   fourth attempt once `_tries >= kEmgMaxTries`. ⇒ a number above `kEmgMaxTries` on the panel is a REAL accounting
//   defect and must stay visible.
//
// ★ THE ONE PLACE THE FIRING DETAIL LINE IS FORMATTED (U1), and it lives in this pure header rather than in the
// renderer for the §UI-6-GLUE reason: `src/firmware_ui.cpp` includes `fw_context.h` (RadioLib, the whole device
// stack), so NOTHING in it is host-compilable and no automated gate can read a string it builds. Here the native
// suite asserts the VISIBLE BYTES. ⚠ That matters specifically for §B115: its first two readings — `2 of 3` and
// `3 of 3` — are individually PLAUSIBLE, so a check asking "does it say N of 3" passes on the bug. The test asserts
// the exact text of the FIRST attempt. `firmware_ui.cpp` must CALL this; `tools/probe_board_ui/run.sh`'s W10 pins it.
inline void emg_attempt_line(char* out, std::size_t cap, uint8_t ordinal) {
    snprintf(out, cap, "attempt %u of %u", unsigned(ordinal), unsigned(kEmgMaxTries));
}

// ================================================================================== §UI-7D slice B — the DETAIL MODAL
// ★★★ THE MODAL'S STATE, and the two terminal answers are STATES rather than a message the renderer infers:
//   `body` — the record is open: header, the current 42-character page, and the two actions.
//   `gone` — the delete came back `not_found` (evicted, already deleted, or `seq == 0`). ⛔ TERMINAL, and there is NO
//            ACTIVE DELETE in it: either press returns to a rebuilt INBOX. Nothing was deleted, and it must not read
//            as though something had been.
// ⓘ There is deliberately no `opening` state: `firmware_ui.cpp` serves the request inside the SAME service pass (the
//   `take_send_request` precedent), so the gap is unobservable — and a request that is never served opens NOTHING,
//   which fails closed rather than showing a record we have not read.
enum class InboxModal  : uint8_t { closed = 0, body, gone };
// ★ `back` is FIRST and is what a freshly opened modal selects, because spec §3.5 requires deletion to cost the
//   deliberate sequence short -> double. ⛔ Not identified positionally (§B66's lesson): a two-member enum cannot be
//   turned into a delete by somebody adding a row.
enum class InboxAction : uint8_t { back = 0, del };
// The model NEVER touches `g_node.inbox()` — it ASKS, exactly as it does for a send. `firmware_ui.cpp` drains the
// request, performs the `pull()` / `erase()`, and feeds back a TYPED answer.
enum class InboxWhat   : uint8_t { none = 0, open, erase };
struct InboxReq { InboxWhat what = InboxWhat::none; InboxKind kind = InboxKind::dm; uint32_t seq = 0; };

// ★★ THE MODAL'S HEADER LINE, formatted in this PURE unit for the §B115 reason: `src/firmware_ui.cpp` is compiled by
// neither the native suite nor the simulator, so a string it builds is a string no automated gate can read. Here the
// native suite asserts the VISIBLE BYTES.
// ⓘ Spec §3.5's layout: `DM from <origin>` (or `CH<n> from <origin>`) with a `<page>/<pages>` indicator. Widest real
//   expansion `CH255 from 255 7/7` = 18 columns, which fits the rail's 19-column body (§CHROME-4). ⓘ `pages` cannot
//   exceed `kDetailMaxPages` = 7, so both counters stay one digit and the line cannot grow.
// ★ `del_failed` REPLACES the from-line rather than a body row: the body rows are the message's own bytes, and dropping
//   half a page while the error stands would make the page indicator name bytes the panel is not showing. The indicator
//   itself is kept, so the failure does not cost the reader their place. Spec §3.5: "on storage failure stay in the
//   modal and show DELETE FAILED".
inline void inbox_detail_head(char* out, std::size_t cap, InboxKind kind, uint8_t origin, uint8_t channel_id,
                              uint8_t page, uint8_t pages, bool del_failed) {
    if (del_failed) { snprintf(out, cap, "DELETE FAILED %u/%u", unsigned(page) + 1u, unsigned(pages)); return; }
    // 32 bytes so -Wformat-truncation can PROVE the widest expansion fits: "CH" + 10 + " from " + 10 + NUL = 29. This
    // bounds the FORMATTER, exactly as `kLineCap` does in the renderer; what bounds the DISPLAY is the 19-column
    // audit above (`chrome4-audit:` asserts this line at its widest reachable expansion).
    char from[32];
    if (kind == InboxKind::dm) snprintf(from, sizeof from, "DM from %u", unsigned(origin));
    else                       snprintf(from, sizeof from, "CH%u from %u", unsigned(channel_id), unsigned(origin));
    snprintf(out, cap, "%-14s %u/%u", from, unsigned(page) + 1u, unsigned(pages));
}

enum class Emergency : uint8_t { idle = 0, arming, firing, blocked, picked_up, not_heard, reply, cancelled, failed };
// ★ §T3: `aired_waiting` is APPENDED, and it is the ONE state that may say "SENT" for a DM. `waiting_ack` now means
// exactly what it can establish — the core ACCEPTED the send and minted a ctr — and renders `QUEUED`; `aired_waiting`
// is reached only by a correlated `send_aired`, i.e. the SX1262 TxDone edge for THIS flight, and renders the existing
// `SENT, waiting` string, now earned. Both are non-terminal; every terminal DM state outranks them (see on_send_aired).
enum class DmState   : uint8_t { idle = 0, submitting, waiting_ack, delivered, no_key, not_confirmed, failed, aired_waiting };
// ★★★ §B69's CARRIER, HALF ONE (UI-7) — THE CANNED-CHANNEL OUTCOME MACHINE, and it is the DmState of the channel path.
// Until now the canned channel post had NO model state at all: `ui_pump_trackers` had to CONSUME the normal tracker's
// expiry and throw it away, with `⛔ Do not "fix" this by calling on_outcome` beside it, because `on_outcome` is the
// EMERGENCY entry point and a canned post's outcome would have moved a live alarm. This enum is the missing entry
// point, and it is what lets B69's two kinds be told apart at the only place that can say them out loud.
// ★ EVERY MEMBER IS REACHABLE ONLY FROM A PATH THAT ESTABLISHED IT — the §2.1 rule this whole arc exists for:
//   `waiting`     — accepted with OUR ctr; the `channel_sent` verdict has not come back yet (it can take ~36 s).
//   `no_relay`    — a `channel_sent` came back for our ctr with `relayed == false`: we transmitted and OVERHEARD NOTHING.
//                   ★ §T3 renamed its STRING `SENT, no relay` -> `NO RELAY HEARD` (`firmware_ui.cpp`). The state and
//                   its meaning are unchanged; what changed is that the word SENT no longer appears on a state that
//                   establishes no airing — the same rule `waiting`/`aired` above now follow. ⓘ §B38's argument
//                   (`no_relay` is CORRECT at 100 % delivery on a 1-hop pair) survives the rename untouched.
//                   ⚠ NOT to be confused with the EMERGENCY headline, where `NO RELAY HEARD` was ruled OUT on width
//                   (14 chars x Font::large = 140 px > the 128 px panel; that headline is `NOT RELAYED`). This state
//                   renders in Font::small in the rail's 19-column body (§CHROME-4), so 14 still fits with 5 to spare.
//   `unconfirmed` — §B69: `ctr == 0`, so NO LOCAL HANDLE ever existed. We never listened, so we may not say "no relay";
//                   we cannot establish transmission either, so we may not say SENT. See the ★★ correction below.
//   `relayed`     — a neighbour was overheard re-flooding it. The only member that may say PICKED UP.
//   `aired`       — ★ §T3, APPENDED: a correlated `send_aired` — the post's M-frame physically left the radio.
//                   It is the channel twin of `DmState::aired_waiting` and the ONLY channel state below `relayed`
//                   that may say SENT. `waiting` (core acceptance) now renders `QUEUED`, because acceptance is
//                   five measured gaps short of the air.
enum class ChanState : uint8_t { idle = 0, submitting, waiting, relayed, no_relay, unconfirmed, blocked, failed, aired };
// ★★★ §B69's CARRIER, HALF TWO — THE EMERGENCY'S EVIDENCE, because the alarm's two channel outcomes collapse into ONE
// `Emergency` state and the renderer cannot ask which happened. `on_outcome` maps `channel_no_relay` AND
// `channel_remote_mint` down the SAME path (neither carries relay evidence ⇒ neither may claim PICKED UP ⇒ bounded
// retry), and after the third attempt BOTH land in `Emergency::not_heard`.
// ⇒ `NOT HEARD` is a CLAIM ABOUT A MEASUREMENT — "we transmitted and overheard no relay". An alarm that never held a
//   handle never listened, so on that path the claim is unfounded and the DETAIL LINE must not make it.
//
// ★★★★ B69's PREMISE IS CORRECTED HERE, MEASURED IN SOURCE 2026-08-05, AND THE CORRECTION IS THE OPPOSITE OF THE
//      OBLIGATION AS WRITTEN. B69 (and spec §2.1 rule 2, and this file's own §B68 block) rule the kind must render as
//      **SENT**, on the strength of B39's producer (3): a registered mobile's DELEGATED GLOBAL post, where the HOME
//      mints the ctr and a real MOBILE_SEND DM flies — a genuine SUCCESS. ⛔ **THAT PRODUCER IS STRUCTURALLY DEAD ON
//      THE LINE THIS UI SENDS.** `node.cpp:1401` computes `want_global = c.u.channel.global || !c.u.channel.team`, and
//      every UI channel post carries `-t` with no `-g` ⇒ `want_global == false` ⇒ the `do_send_channel_delegated`
//      branch (`node.cpp:1591-1601`) is never entered. On `-t -e` exactly TWO producers of `queued`/`ctr == 0` remain,
//      and NEITHER is a success: a pre-TX self-gate (`node_channel.cpp:650`, which also pushes `send_blocked`) and a
//      post-mint SEAL FAILURE (`node_channel.cpp:744`). The first normally resolves through `match_blocked` inside the
//      window; the second is the one that reaches expiry.
// ⇒ **RENDERING IT AS "SENT" WOULD BE THE §2.1 FALSE CONFIRMATION THE OBLIGATION WAS WRITTEN TO PREVENT.** The kind
//   stays a SUCCESS SHAPE inside the tracker — §B68's argument is untouched, "a delivered message called failed" is
//   still the error to avoid, and the tracker is GENERIC (a future plain/`-g` UI post would revive producer 3). What
//   changes is only what the PANEL says: **UNCONFIRMED**, never SENT and never "no relay". Reported to the owner as a
//   design change, not edited into the plan.
// ★ STICKY AND ORDERED, `local_tx` > `no_handle` > `none`, and the ordering is the correctness argument: ONE
//   locally-originated attempt whose `channel_sent` came back makes "we listened and heard nothing" TRUE for the alarm
//   as a whole, so a later handle-less attempt must not erase it. Reset only by a NEW alarm (`long_fire`), beside
//   `_tries` — the budget and the evidence describe the same alarm and must start together.
// ⓘ Deliberately NOT a ninth `Emergency` state: the distinction is orthogonal to the machine (it says what the
//   evidence WAS, not where the alarm IS), and a ninth state would have to be threaded through `hold_active`,
//   `emg_outcome_retained` and B71's exit set for no behavioural gain. A flag was B69's own first-named option.
enum class EmgEvidence : uint8_t { none = 0, no_handle, local_tx };
// The COMPACT panel reason. Deliberately NOT a mirror of SendFailReason: `parser` has no core equivalent (the line
// never became a Command), and the three that do are the ones whose remedy differs — encrypt / get a fix / retry
// later. Everything else is `other`, and §B73's `fail_reason()` carries the core reason verbatim beside it, so
// nothing is discarded. Spec §2.1 rule 6, §3.4.1.
enum class RefuseReason : uint8_t { parser = 0, unsealable, no_location, queue_full, other };
// ⚠ An ALIAS, not a UI enum — it IS `MESHROUTE_NS::SendFailReason`, so `mrui::FailReason::x` and
// `meshroute::SendFailReason::x` are the same value of the same type. It exists so this header names its one
// cross-namespace dependency in exactly one place; never redeclare or renumber it here.
using FailReason = MESHROUTE_NS::SendFailReason;

// A correlated outcome. Built ONLY by the send tracker (UI-4) after it has matched ctr/peer/channel — the model never
// sees a raw Push, which is what makes a false PICKED UP structurally impossible (spec §2.1).
struct SendOutcome {
    // ★★ §B68: `channel_remote_mint` is the EIGHTH kind and it is a SUCCESS. Without it Task 4 must call a DELIVERED
    // message failed: on a registered mobile a plain/`-g` GLOBAL post goes through `do_send_channel_delegated` — a real
    // MOBILE_SEND DM flies — but the HOME mints the channel ctr, so `ctr` stays 0 (`lib/core/node.cpp:1565-1573`,
    // register B39). ⇒ `ctr != 0` = we own the handle, exact correlation valid; `ctr == 0` = **no LOCAL handle exists
    // and whether anything flew is not answerable synchronously.**
    // ⓘ Unreachable on the team-plane alarm path (`MR_UI_TEAM_CHANNEL_ID`), so this is type correctness, not a live
    // safety hole. ⚠ Render it as SENT, never as PICKED UP — see register B69: the model has no state that carries
    // that distinction, so the obligation currently rests entirely on UI-6/UI-7's channel path.
    // ★★ §B72: `channel_failed` is the NINTH kind and it is the pre-enqueue failure — a SEAL failure returns
    // `queued` with ctr == 0, so `match_channel_sent` can never fire for it. Without this kind the alarm sits on
    // `SENDING...` for ever after a seal failure: a safety-path defect, not a typing nicety. It is TERMINAL, never a
    // retry — `unsealable` / `no_location` are documented PERMANENT in command.h, so retrying burns the three-alarm
    // budget for nothing.
    // ★★ §B73: both failure kinds CARRY their `SendFailReason`. A reasonless `dm_failed()` left `refuse_reason()`
    // pinned at `other`, so the panel could not say WHY — spec §2.1 rule 6 exists to prevent exactly that. The
    // parameter is REQUIRED (no defaulted `none`): a caller that has a reason must not be able to drop it silently.
    enum class Kind : uint8_t { channel_relayed, channel_no_relay, channel_remote_mint, channel_failed,
                                blocked, dm_acked, dm_no_key, dm_failed, dm_timeout };
    Kind       kind    = Kind::channel_no_relay;
    uint32_t   next_ms = 0;
    FailReason reason  = FailReason::none;   // meaningful for channel_failed / dm_failed ONLY
    static SendOutcome channel_relayed()     { return {Kind::channel_relayed, 0}; }
    static SendOutcome channel_no_relay()    { return {Kind::channel_no_relay, 0}; }
    static SendOutcome channel_remote_mint() { return {Kind::channel_remote_mint, 0}; }   // §B68: accepted, ctr minted elsewhere
    static SendOutcome channel_failed(FailReason r) { return {Kind::channel_failed, 0, r}; }   // §B72: pre-enqueue, terminal
    static SendOutcome blocked(uint32_t n)   { return {Kind::blocked, n}; }
    static SendOutcome dm_acked()            { return {Kind::dm_acked, 0}; }
    static SendOutcome dm_no_key()           { return {Kind::dm_no_key, 0}; }
    static SendOutcome dm_failed(FailReason r) { return {Kind::dm_failed, 0, r}; }             // §B73: reason REQUIRED
    static SendOutcome dm_timeout()          { return {Kind::dm_timeout, 0}; }
};

struct UiState {
    Screen  screen = Screen::status;
    uint8_t cursor = 0;
    Compose compose = Compose::none;
    uint8_t compose_peer = 0;   // bound at ENTRY: the roster can reorder under an open modal, which would retarget it
    // ★★ UI-7: THE SUB-VIEW'S SECOND PHASE. Spec §3.2.1/§3.4.1 require the OUTCOME to replace the canned list *in the
    //    sub-view* ("`SENDING...`", "`DELIVERED to <label>`", "`NO KEY`"), and UI-2 shipped `compose_gesture` CLOSING
    //    the modal as it queued the send — so every state `DmState` can reach had no renderer at all and the one thing
    //    `-a` buys over a channel post ("delivered to that PERSON") was invisible. ⇒ a send switches the same modal
    //    from `list` to `result`; it does not close it.
    // ⓘ A separate flag rather than two more `Compose` members: `Compose` says WHICH list (and therefore which peer and
    //   which text table), and that stays true in the result phase — `draw_compose` still needs it for the header.
    bool    compose_result = false;
    // ★★★ §B64 (OWNER-RULED 2026-08-05): the teammate the TEAM cursor was on has LEFT the roster, so the activation was
    //    REFUSED. It rides `UiState` — the frozen display struct — because it is a thing the panel must SAY (C2: the
    //    refusal is loud, never silent) and because that puts it on the existing freeze path (U2), with no second
    //    plumbing for the renderer to keep in step. The renderer also SUPPRESSES the `>` marker while it is set: a
    //    highlight beside somebody the user did not pick is the same mis-send in display form.
    // ⛔ NOT derived from `cursor >= team_shown`. That predicate happens to be equivalent today, and it is exactly the
    //    positional coupling §B66 exists to warn about — one row added or one clamp changed and it silently means
    //    something else. The flag says what it means.
    bool    team_pick_gone = false;
    // ★★★ §UI-7D slice B — THE INBOX SIDE OF THE SAME IDENTITY RULING, and the same reason it rides `UiState`: the
    //    refusal is a thing the panel must SAY (C2), and `UiState` is the struct the frame FREEZES, so it needs no
    //    second plumbing (U2). Set when activation is refused because the selected `(kind, seq)` is no longer in the
    //    store; cleared by the next navigation press. ⇒ spec §3.5's "activation refuses with MESSAGE GONE rather than
    //    opening or deleting its replacement", and the renderer suppresses the `>` marker while it stands for the §B64
    //    reason — a highlight beside a record the model has already refused to act on is the same wrong in display form.
    bool    inbox_pick_gone = false;
    // ★★★★ THE DETAIL MODAL'S FROZEN DISPLAY STATE (spec §3.5). ⛔ THE 242-BYTE BODY BUFFER IS **NOT** HERE: it stays
    //     LIVE in the model (`_detail_body`) and only the CURRENT PAGE is frozen, so a frame copies ~60 bytes instead of
    //     a quarter of a kilobyte while still satisfying the §5 freeze contract — the renderer reads none of the live
    //     buffer.
    // ★ `detail_kind` / `detail_seq` are the modal's SINGLE identity authority: `activate` writes them from the
    //   selection, the open answer is REFUSED unless it names the same pair, and the erase request is built from them.
    //   ⛔ Never re-derive the erase target from a snapshot row — rows move.
    InboxModal  detail        = InboxModal::closed;
    InboxAction detail_action = InboxAction::back;
    bool        detail_del_failed = false;
    uint8_t     detail_page = 0, detail_pages = 1;   // `pages` is never 0 — an empty body is ONE page
    InboxKind   detail_kind = InboxKind::dm;
    uint32_t    detail_seq  = 0;
    uint8_t     detail_origin = 0, detail_channel = 0;
    char        detail_line[kDetailBodyRows][kDetailCols + 1] = {};   // the current page, already sanitized + wrapped
    // ★★★ §UI-14 — WHAT THE MODEL DECIDED ABOUT SETTINGS, frozen with everything else. ⛔ WHAT IS DELIBERATELY *NOT*
    //     HERE: `config_unsaved`, `conflict`, `reboot_required` and the draft VALUES. Those are the SERVICE's, read
    //     through `ConfigService` at the freeze (`src/firmware_ui.cpp`'s `SettingsView`) — mirroring them into
    //     `UiState` would be a SECOND state model, which §3.6.1 forbids in as many words.
    Settings settings = Settings::closed;
    // The last ACTION's outcome, kept VERBATIM as the service's own typed result (⛔ never a `mrui::` mirror of
    // `CfgSave` — that is the parallel enum U1 forbids, and the panel would then be able to claim an outcome the
    // service never returned). `cfg_have_save` is the separate "there is one" flag, because `CfgSave` reserves no
    // value for "nothing has been attempted" (§B74's discipline: no arithmetic value stands in for a state).
    // ★ TRANSIENT BY DESIGN, exactly like `team_pick_gone`: the next navigation press clears it, so what the panel
    //   shows always belongs to the act the operator just performed.
    bool          cfg_have_save = false;
    mrfw::CfgSave cfg_save      = mrfw::CfgSave::not_open;
    // DISCARD / RELOAD have only ONE failure between them — the store could not be read (`CfgRefresh::nv_failed`) —
    // and on that path the draft SURVIVES. Success needs no note: the marker itself disappearing IS the feedback.
    bool     cfg_refresh_failed = false;
    // ★★★ §UI-15 slice 4 — THE PROVISIONING SUB-STATE, frozen with everything else so the renderer (slice 5) reads it
    //     from the frame and from nothing else. ⛔ `Settings::provisioning` alone cannot say WHICH arm is up; see the
    //     `Provision` block for the invariant that binds the two and for the two primitives that are allowed to move
    //     them.
    Provision   provisioning = Provision::closed;
    // §3.6.3: *"a confirmation with BACK selected initially"*. ⓘ It is the CONFIRM ARMS' cursor and nothing else's —
    // the `menu`'s own cursor is `UiState::cursor`, the ordinary list index every other screen uses (U1).
    ProvConfirm prov_confirm = ProvConfirm::back;
    // §4's refusal, TRANSIENT exactly like `cfg_have_save` / `team_pick_gone`: it describes the act the operator just
    // performed, and the next navigation press retires it (`clear_settings_note`).
    // ⛔ It REPLACES §UI-14's `bool cfg_provision_na` ("PROVISION: UI-15"), which was the placeholder refusal for a
    //    flow that did not exist. The flow exists now, so the only remaining refusal is §4's precondition — and that
    //    one has TWO cells with TWO remedies, which is why a bool could not carry it.
    ProvBlock   prov_block = ProvBlock::none;
    // ★★★ §UI-15 slice 5 — WHAT THE TRANSACTION ANSWERED, frozen with everything else so `create_result` renders the
    //     verdict and nothing else. ⛔ IT IS NOT A SECOND STATE MODEL: it is the ADAPTER's typed answer stored
    //     verbatim, exactly as `cfg_save` stores the SERVICE's `CfgSave` verbatim one screen up (⛔ never a `mrui::`
    //     mirror of `ProvVerdict`/`ProvErr` — that is the parallel enum U1 forbids, and the panel could then claim an
    //     outcome the transaction never returned).
    // ★ EVERY ARM ENTRY CLEARS IT (`enter_provision`) and only `run_create_team` writes it, immediately after
    //   `perform()` RETURNED — so a stale verdict can never be under a screen that did not establish it.
    // ⓘ COST, MEASURED not assumed, and ⚠ NATIVE ALIGNMENT HIDES THE BOARD FIGURE (D2's standing warning — the
    //   `const char*` is 8 bytes on the host and 4 on ARM): on the host `sizeof(UiProvAnswer)` is 16 and
    //   `sizeof(UiState)` moves 72 -> 96; on a 32-bit board the carrier is 12 bytes. ⛔ The authoritative number is a
    //   per-board `RAM_used` diff, which is the board gate's — this struct is instantiated TWICE on the OLED envs
    //   (the model's and the frame's frozen copy). `sizeof(UiSnapshot)` is UNCHANGED at 608 (slice 4's two bools
    //   already landed in the tail's padding).
    UiProvAnswer prov_answer{};
    // ★★★ §UI-15 slice 6 — THE PROFILE LIST THE SELECT SCREEN WALKS, READ ONCE AND FROZEN WITH THE FRAME. ⛔ It is
    //     NOT re-read per tick or per page: `IUiProvision::profiles()` reaches flash, and U8g2 replays the whole
    //     scene eight times per frame — a renderer that asked would pay eight reads for one picture.
    // ★ AND IT IS WHAT THE CONFIRMATION SHOWS **AND** WHAT THE TRANSACTION IS GIVEN (`run_join_static` builds the
    //   intent from `slot[join_sel - 1]`), so "what was shown is what is joined" is structural rather than intended.
    // ⓘ COST, MEASURED not assumed, and ⚠ NATIVE ALIGNMENT HIDES THE BOARD FIGURE (D2's standing warning): on the
    //   host `sizeof(UiJoinList)` is 108 and `sizeof(UiState)` moves 96 -> **200** — the list plus `join_sel` and
    //   `join_still`, which land in its tail padding and therefore cost ZERO further bytes. `sizeof(UiSnapshot)` is
    //   UNCHANGED at 608 (this slice adds no snapshot field: the profile list is a MODEL fact, read on a transition,
    //   ⛔ never republished per tick). ⛔ The authoritative number is a per-board `RAM_used` diff, which is the board
    //   gate's — this struct is instantiated TWICE on the OLED envs, so the host figure is +208 B of model state.
    UiJoinList join_list{};
    // The SELECTED slot, 1..kJoinProfiles, and ⛔ 0 = nothing picked. It is a SLOT NUMBER and never a row index:
    // rows are built from the `present` flags, so an index means a different profile in a different record (§B66).
    uint8_t     join_sel = 0;
    // ★ Plan §2.3 rule 5's word change, LATCHED rather than recomputed at the draw: `FrameGate::step` returns `idle`
    //   while the model is clean, so a text that changed without a `dirty` would be true and INVISIBLE until some
    //   unrelated event repainted the panel. `on_tick` moves it exactly at the edge. ⛔ It is not a failure state.
    bool        join_still = false;
    bool    blanked = false;
    bool    dirty   = true;
};

// ★★ THE ONE-LINE NOTE THE SETTINGS PANEL SHOWS AFTER AN ACTION — formatted in this PURE unit so the native suite can
//    assert the VISIBLE BYTES (§B115's rule; `src/firmware_ui.cpp` is compiled by neither the native suite nor the
//    simulator). It returns `""` when there is nothing to say, and the three sources are MUTUALLY EXCLUSIVE by
//    construction: every activation clears the other two before recording its own.
// ★★★ "A FACT IS ESTABLISHED BY THE ACT": every string below names an outcome the SERVICE RETURNED. ⛔ There is no
//     path that prints `SAVED` before `save()` came back, and none that prints it for a refusal — `invalid`,
//     `conflict` and `nv_failed` each have their own words, and the last two are the SERVICE's ruled ones.
inline const char* settings_note(const UiState& st) {
    // ★★★★ §4's TWO CELLS, AND THE WHOLE POINT IS THAT THEY SAY DIFFERENT THINGS. `conflict()` means `/mrcfg` moved
    //     under the draft, so ⛔ the note MUST NOT suggest SAVE: `save()` refuses a conflict outright
    //     (firmware_config_service.h's gate 2a), and pointing the operator at an operation that cannot succeed is the
    //     conflation plan §4 exists to correct. RELOAD (the three-way merge) and DISCARD are the two that work.
    // ⓘ `default`-less, so a third block reason cannot be added without a word for it.
    switch (st.prov_block) {
        case ProvBlock::conflict: return "RELOAD OR DISCARD";
        case ProvBlock::unsaved:  return "SAVE OR DISCARD";
        case ProvBlock::none:     break;
    }
    if (st.cfg_refresh_failed) return "NV READ FAILED";
    if (!st.cfg_have_save)     return "";
    switch (st.cfg_save) {
        case mrfw::CfgSave::saved:        return "SAVED";
        // ⓘ The SAME word as `saved`, deliberately: it IS saved, and `RESTART NEEDED` is a SEPARATE fact rendered from
        //   `reboot_required()` on its own row (§3.3's third literal). Folding them into one string would be the
        //   two-facts-as-one collapse §3.6.1 warns about — and the reboot row must outlive this transient note.
        case mrfw::CfgSave::saved_reboot: return "SAVED";
        case mrfw::CfgSave::no_change:    return "NO CHANGE";
        case mrfw::CfgSave::invalid:      return "BAD VALUE";
        case mrfw::CfgSave::conflict:                       // "CFG! RELOAD" — §UI-13's ruled string, CALLED not copied
        case mrfw::CfgSave::nv_failed:    return mrfw::cfg_save_panel(st.cfg_save);   // "SAVE FAILED", likewise
        // Unreachable from the panel (the screen refuses to offer SAVE at all when the service could not open), and
        // listed rather than defaulted so an eighth outcome fails the build (§B72's rule).
        case mrfw::CfgSave::not_open:     return "NO CONFIG";
    }
    return "";
}

class UiModel {
public:
    void on_gesture(Gesture g, const UiSnapshot& s) {
        if (g == Gesture::none) return;
        _last_input_ms = s.now_ms; _seeded = true;
        // ★ spec §4.2: emergency gestures pre-empt EVERYTHING — blank-wake and the compose modal both.
        if (g == Gesture::long_arm || g == Gesture::long_fire || g == Gesture::long_cancel) {
            _st.blanked = false; emergency_gesture(g, s); _st.dirty = true; return;
        }
        if (_st.blanked) { _st.blanked = false; _st.dirty = true; return; }   // the waking press is CONSUMED
        // ★★★ §B71 (OWNER-RULED 2026-08-04, implemented by UI-6): once the alarm has been sent AND ITS RESULT SEEN,
        // the next SHORT press acknowledges it and restores the normal cycle. Before this there was NO exit at all —
        // `_emg` had no path back to `idle`, so a fired alarm owned the panel until reboot.
        // ★ IT IS SAFE BECAUSE THREE RULES COMPOSE, and the ORDER of the two lines above is two of them:
        //   1. long gestures are handled first, so `long` still re-fires from a sticky outcome;
        //   2. a blanked panel consumes its waking press ABOVE, so a retained outcome is ALWAYS displayed before any
        //      press can dismiss it — that is what makes a SHORT press (not a compound gesture) acceptable here;
        //   3. only RETAINED outcomes qualify: an alarm still in flight (`arming`/`firing`/`blocked`-with-a-live-retry)
        //      is sticky, because an outcome the hiker never saw is the failure SAFETY-FIRST exists to prevent.
        // ★ It cannot trap the user: retries are BOUNDED, so the machine always terminates in a retained outcome.
        // ⚠ It is placed BEFORE the compose branch on purpose: the press must act on what the user is LOOKING AT,
        //   which is the alarm overlay, never the list underneath it.
        // ⓘ CORRECTED 2026-08-05 by §B101/F5. This comment used to say a long press "does NOT close" the compose
        //   sub-view, so the overlay rendered over a still-open modal. `long_fire` now closes it and resets the cursor
        //   — see `emergency_gesture`. `long_arm` still does not, because arming is cancellable.
        // ⓘ `double` deliberately gets NO emergency job (spec §4's "double acknowledges" AND "double re-fires" are both
        //   withdrawn — they were the contradiction B71 resolved). ⓘ CORRECTED 2026-08-05 by §R2: this line used to
        //   end "so it falls through to `activate()` as usual", and that fall-through WAS the hidden-mis-send hazard.
        //   The overlay now absorbs the double outright — see the R2 arm below.
        // ★★ §B102/F3: while the overlay is up it OWNS the body (draw_frame returns straight after draw_emergency),
        //    so a short press must NEVER operate the screen underneath — the user cannot see what they would change.
        //    It is CONSUMED either way; it DISMISSES only once the outcome has actually been presented.
        if (g == Gesture::short_press && _emg != Emergency::idle) {
            if (emg_outcome_retained()) { _emg = Emergency::idle; _st.dirty = true; }
            return;
        }
        // ★★★ R2 (OWNER-RULED 2026-08-05) — THE OVERLAY ABSORBS A DOUBLE, ENTIRELY. No emergency action, no operation
        // of the screen underneath, no dismiss, no re-fire. It is a `return`, and nothing else, on purpose.
        // ★ THE HAZARD IT CLOSES IS A HIDDEN MIS-SEND DURING AN ALARM. The overlay OWNS the body (`draw_frame` returns
        //   straight after `draw_emergency`), but a `double` used to fall through to `activate()` / `compose_gesture()`
        //   below — so TWO doubles opened a compose view the user cannot see and then SENT from it, and with a modal
        //   left open under ARMING (which §B101 deliberately does not close, because arming is cancellable) ONE was
        //   enough. That completes what §B102/F3 did for the SHORT press: the overlay is now opaque to BOTH.
        // ⚠⚠ IT IS ITS OWN ARM AND MUST STAY ONE — do NOT merge it into the branch above. That branch is gated on
        //    §B102's presented-latch (`emg_outcome_retained()`), which is F3's answer to a PREMATURE SHORT press.
        //    Sharing it would give `double` the latch, and a double would then DISMISS a presented outcome — the
        //    duty §B71 explicitly WITHDREW ("double gets no emergency job"). A test distinguishes the two arms
        //    directly (`ui-frame: R2 vs F3 …`); it is what fails if a later reader folds them.
        // ⓘ Truthfully NOT "no effect at all": the press still refreshed `_last_input_ms` at the top of this function,
        //   because the user genuinely did act. That is the input-liveness layer, not the gesture contract, and the
        //   hold deadline (§4.3) is what governs the overlay's panel time regardless.
        if (g == Gesture::double_press && _emg != Emergency::idle) return;
        // ★★ §UI-7D slice B: the DETAIL modal owns the gesture while it is open, and it is checked BEFORE compose
        //    deliberately even though the two are mutually exclusive by construction (compose opens from TEAM/SEND,
        //    detail from INBOX) — the modal that owns the BODY must own the press, and stating the order here means a
        //    later screen that can reach both cannot make it ambiguous.
        if (_st.detail != InboxModal::closed) { detail_gesture(g); return; }
        if (_st.compose != Compose::none) { compose_gesture(g); return; }
        // ★★★ §B64: re-anchor the TEAM cursor onto the TEAMMATE it was placed on, BEFORE the gesture acts on it — the
        //    roster is rebuilt every tick and can have reordered since the last one. See `sync_team_cursor`.
        sync_team_cursor(s);
        sync_inbox_cursor(s);            // ★ §UI-7D: the same re-anchoring for the INBOX row, by `(kind, seq)`
        sync_settings(s);                // ★ §UI-14: the screen owns the editor's lifetime — see the function
        // ★★★ §UI-14 — THE EDITOR OWNS `short`, AND THIS BRANCH IS THE WHOLE OF "short's two modes". While a value row
        //     is open a short press CYCLES that row's value and must NOT walk the list: leaving the branch out is the
        //     defect where the value the operator is looking at scrolls away under their finger.
        // ⚠ It is checked AFTER the emergency arms above (§4.2: emergency pre-empts everything, and `long` has already
        //   left the editor) and after the two modals, which cannot coexist with it — the same ordering statement
        //   §UI-7D made for the detail modal, for the same reason: the view that owns the BODY owns the press.
        // ★★★ §UI-15 slice 4 — THE PROVISIONING SUB-VIEW OWNS THE PRESS, and it is checked here for the reason this
        //     file has now stated three times: THE VIEW THAT OWNS THE BODY OWNS THE PRESS. It cannot coexist with the
        //     editor (one `Settings` value names one of them), so the order between the two is free — and it is
        //     written down anyway, so a later reader cannot make it ambiguous.
        if (_st.settings == Settings::provisioning) { provision_gesture(g, s); return; }
        if (_st.settings == Settings::editing) { settings_edit_gesture(g, s); return; }
        // ⚠ `note_settings_cursor` runs AFTER the move and `sync_settings` BEFORE it (at the top of this function) —
        //   the same split as the other two cursors, and it is load-bearing: syncing after the move would drag the
        //   highlight straight back onto the row the operator just left.
        if (g == Gesture::short_press)  { advance_or_next(s); note_team_cursor(s); note_inbox_cursor(s);
                                          settings_follow_screen(); note_settings_cursor(s);
                                          clear_settings_note(); _st.dirty = true; }
        else if (g == Gesture::double_press) { activate(s);   _st.dirty = true; }
    }

    void on_tick(const UiSnapshot& s) {
        // ★ B65 (ruled 2026-08-03): the blank timer measures "time since the user last acted", and before the first
        // tick there is no such time. Seeding from 0 blanked the panel on its FIRST tick whenever mr_ui_init() ran
        // >kBlankMs after boot — reachable because NV format-on-corrupt is a shipped path that delays boot by design —
        // leaving a safety device dark the first time it is looked at. A gesture seeds it too, so an early press keeps
        // ownership of the window rather than being overwritten by a late first tick.
        if (!_seeded) { _last_input_ms = s.now_ms; _seeded = true; }
        tick_emergency(s);
        if (_st.compose != Compose::none && elapsed(s.now_ms, _last_input_ms) >= kBlankMs) {
            close_compose();                                                 // never outlive attention; sends nothing
        }
        // ★ §UI-7D slice B, spec §3.5: "ordinary modal timeout returns to INBOX without deleting" — the SAME kBlankMs
        //   window the compose sub-view uses (U1), for the same reason: a modal that outlives the user's attention is
        //   one whose selected action eventually gets pressed by accident. It deletes nothing, by construction.
        if (_st.detail != InboxModal::closed && elapsed(s.now_ms, _last_input_ms) >= kBlankMs) close_detail();
        // ★★★ THE 2 s PAGE ADVANCE, AND IT RIDES THIS TICK ON PURPOSE: `TimerWheel::kCap` is 91 and every id is
        //     allocated, so a `Node` timer was not available — and would have been the wrong layer anyway, since a
        //     display cadence is not protocol time.
        // ★ IT MARKS THE MODEL DIRTY BUT DELIBERATELY DOES NOT TOUCH `_last_input_ms` (spec §3.5): the page turning is
        //   the DEVICE acting, not the user, so it must not postpone the blank or the modal's own timeout above. ⇒ a
        //   long body cycles for exactly as long as the inactivity window, and then the panel blanks as it always would.
        // ⓘ The resulting repaint still obeys §5's MAC-idle/page-buffer gate — `FrameGate` is the only thing that
        //   decides a frame may open, and this only asks.
        if (_st.detail == InboxModal::body && _st.detail_pages > 1 &&
            elapsed(s.now_ms, _detail_page_at_ms) >= kDetailPageMs) {
            _st.detail_page = uint8_t((_st.detail_page + 1) % _st.detail_pages);   // ★ CYCLES, never stops at the last
            _detail_page_at_ms = s.now_ms;
            refresh_detail_page();
            _st.dirty = true;
        }
        // ★★★★ §UI-15 slice 6 / plan §2.3 rule 5 — 60 s ⇒ `STILL JOINING`, ⛔ **AND NOTHING ELSE HAPPENS**. No state
        //     moves, no transaction is re-run, the session is untouched and ⛔ NO FAILURE IS DECLARED: normal
        //     adoption is ~23 s, one conflict/retry reaches ~53 s, and retries are NOT finitely bounded — so a
        //     deadline that failed would LIE about an operation that is still progressing.
        // ★ EDGE-TRIGGERED, exactly as `mr_ui_on_config_saved`'s latch is, and for the same measured reason:
        //   `FrameGate::step` answers `idle` while the model is clean, so a word that changed without a `dirty`
        //   would be TRUE AND INVISIBLE until something unrelated repainted the panel.
        // ⓘ IT RIDES THE SESSION's CLOCK, not `_last_input_ms`: the operator pressing nothing is not the join taking
        //   longer, and the panel's own blank timer must keep its separate meaning.
        if (_st.provisioning == Provision::join_waiting) {
            const bool still = _join.active && elapsed(s.now_ms, _join.started_ms) >= kJoinStillMs;
            if (still != _st.join_still) { _st.join_still = still; _st.dirty = true; }
        }
        // ★★★ §B64, AND THE PLACEMENT IS THE POINT: `FrameGate::step` FREEZES the state immediately after this call
        //    (`mr_ui_tick`: on_gesture -> on_tick -> step), so the highlight must already name the remembered teammate
        //    IN THIS SNAPSHOT. Re-anchoring only on a gesture would leave the panel showing `>` beside one teammate
        //    while `activate()` addressed another — the mis-send this ruling closes, arriving from the other side.
        //    ⓘ After the auto-exit above, deliberately: a modal that just closed gets its team cursor back the same tick.
        sync_team_cursor(s);
        sync_inbox_cursor(s);            // ★ §UI-7D: same placement, same argument — the frozen frame must show the
                                         //   highlight beside the record `activate()` would actually open.
        sync_settings(s);                // ★ §UI-14: same placement, same argument — the frame FREEZES immediately
                                         //   after this call, so the service must already be open when it does.
        if (!_st.blanked && !hold_active(s.now_ms) &&
            elapsed(s.now_ms, _last_input_ms) >= kBlankMs) {
            _st.blanked = true; _st.dirty = true;
            // ★★ §3.6.1, VERBATIM IN SUBSTANCE: *"`BACK` and blanking PRESERVE the draft; silently discarding because
            //    attention timed out is FORBIDDEN."* `on_blank()` is the named seam the service exposes for exactly
            //    this event, and it is a draft-preserving no-op BY CONSTRUCTION. ⇒ calling it is what makes the
            //    property ASSERTABLE (mutation C29 turns it into a `discard()` and the suite reddens); leaving the
            //    blank unreported would put the obligation in a comment instead of in a call.
            if (_cfg) _cfg->on_blank();
        }
    }

    const UiState& state() const { return _st; }
    // ★★ §UI-14 — THE STAGED-CONFIG SERVICE, ATTACHED RATHER THAN OWNED. `src/firmware_ui.cpp` constructs it over the
    //    DEVICE bindings ([[B193]]) and hands it here once, at `mr_ui_init`; the native suite hands over one built on
    //    fakes. ⛔ The model never constructs one: a service is a thing with a durable store behind it, and a model
    //    that made its own would be a second draft nobody could save.
    // ⓘ NULL IS A REAL STATE and it fails CLOSED: an unattached model shows the menu's rows but every activation
    //   refuses. That is what a `!MR_FEAT_OLED`-shaped build or a partially-wired probe looks like, and it must not
    //   crash a safety device.
    void attach_config(mrfw::ConfigService& c) { _cfg = &c; }
    mrfw::ConfigService*       config()       { return _cfg; }
    const mrfw::ConfigService* config() const { return _cfg; }
    // ★★ §UI-15 slice 5 — THE PROVISIONING SEAM, ATTACHED exactly as the config service is (U3). `src/firmware_ui.cpp`
    //    constructs the adapter over the device bindings and hands it here once, at `mr_ui_init`; the native suite
    //    hands over the SAME pure adapter built on the transaction's own fakes. ⛔ The model never constructs one.
    void attach_provision(IUiProvision& p) { _prov = &p; }
    // ★★★ §UI-15 slice 6 — THE ASYNCHRONOUS OUTCOME's TWO ENTRY POINTS.
    // `join_session_active()` is the DEVICE's cheap guard and NOTHING ELSE: `src/firmware_ui.cpp` must read
    // `/mrcfg` and `g_node` to supply term 2 and term 4, and paying a flash read on every inbound push would be a
    // per-push cost for a state nothing else on the device needs. ⛔ It is NOT half of the rule — the rule is
    // `join_push_correlates` and lives in one place, so the guard and the decision cannot drift.
    // ⛔ CORRECTED IN PLACE 2026-08-20 ([[B228]], V1): this comment used to bound the cost by saying the session is
    //    *"up for a minute or two in a device's life"*. That was FALSE and it mattered — see `_join` below: BACK, the
    //    blank, the screen cycle and the 60 s deadline all leave the session RUNNING, so a join that is never adopted
    //    keeps it active for the rest of the uptime. ⇒ the guard alone was never a bound on the cost, and
    //    `src/firmware_ui.cpp` carries the kind PREFILTER that actually is one.
    bool join_session_active() const { return _join.active; }
    // ★★★★ THE ONE PLACE A PUSH MAY TOUCH THIS SCREEN, and it is a FORWARD to the pure four-term rule plus the two
    //      state changes a correlated adopt earns. ⛔ EVERY non-correlated push — a boot DAD, a heal re-adopt, any
    //      `join_refused` reason whatsoever — returns here having changed NOTHING: no screen move, no failure text,
    //      no session end. That is plan §2.3 rules 2 and 6 expressed as one early return.
    void on_join_push(const MESHROUTE_NS::Push& pu, uint8_t persisted_layer0_id, uint8_t canonical_node_id) {
        if (!join_push_correlates(_join, pu.kind, pu.layer_id, pu.dst, persisted_layer0_id, canonical_node_id))
            return;
        _join.active = false;                       // ★ the session's ONLY ordinary end (see `_join`'s block)
        // ⛔ THE SCREEN IS COMPLETED ONLY IF IT IS THE SCREEN THAT IS UP. A push may not navigate the panel; if the
        //    operator has left `join_waiting` the operation still completed, and the STATUS screen is where a node
        //    id belongs — ⛔ not a result view that appeared under his thumb.
        if (_st.provisioning == Provision::join_waiting) {
            enter_provision(Provision::join_result);
            UiProvAnswer a{};
            a.outcome = UiProvOutcome::adopted;
            a.node_id = pu.dst;                     // ★ the ADOPTED id, read off the push the rule accepted
            _st.prov_answer = a;
        }
        _st.dirty = true;
    }
    // The visible SETTINGS rows for THIS snapshot — one construction, shared by the cursor bound (`list_len`), the
    // activation and the renderer (U1/U2). ⛔ Never rebuild the list at a call site: a renderer whose list differed
    // from the model's by one row would highlight one thing and act on another.
    CfgRowList settings_row_list(const UiSnapshot& s) const {
        return settings_rows(s.ble_row, _cfg && _cfg->conflict(),
                             provision_has_child(s.prov_create_team, s.prov_join_static));
    }
    // ★ §UI-15 slice 4 — the same rule one level down (U1/U2): ONE construction of the PROVISION menu's children,
    //   shared by the cursor bound, the activation and (slice 5) the renderer. ⛔ Never rebuild it at a call site.
    ProvRowList provision_row_list(const UiSnapshot& s) const {
        return provision_rows(s.prov_create_team, s.prov_join_static);
    }
    void clear_dirty() { _st.dirty = false; }
    // ★★ §B108: AN ARRIVAL IS A REASON TO REPAINT. `mr_ui_on_push` moved the unread counters and the recency stamps
    // and then asked for nothing, so a new message sat unshown until some UNRELATED gesture or timer happened to
    // invalidate the panel. The counts ride the STATUS BAR, which every screen draws, so this is not Inbox-specific.
    void mark_dirty() { _st.dirty = true; }
    // ★ TWO independent slots, emergency first. One shared slot would let a normal compose action OVERWRITE a queued
    // alarm, and (with the tick's in-flight gate) serialise the emergency behind a DM awaiting its e2e ack — which
    // defeats "long press fires from any screen". Normal work never touches the emergency slot. Spec §2.1.
    // ⚠ THIS CALL DRAINS (register B70) — call it ONCE into a local, never twice in an assertion plus its guard.
    // ★ §B75: draining a DM request enters `DmState::submitting` (spec §3.4.1: "the command was handed to dispatch"
    // -> `SENDING...`). This IS the hand-off point — firmware_ui.cpp performs the send inside the same service pass,
    // so there is no observable gap — and putting it here is what makes the state reachable at all: the enumerator
    // existed but nothing assigned it, so a DM showed `idle` until its result came back. Emergency and canned-channel
    // requests leave `_dm` alone, exactly as on_send_accepted / on_send_refused do.
    bool take_send_request(SendReq& out) {
        if (_emg_req_pending) { _emg_req_pending = false; out = SendReq{SendKind::emergency, 0, 0}; return true; }
        if (!_req_pending) return false;
        _req_pending = false; out = _req;
        if (out.kind == SendKind::dm) { _dm = DmState::submitting; _st.dirty = true; }
        // ★ UI-7: the canned-channel twin, and it also CLEARS a previous transaction's terminal state. Without the
        //   reset a second post would open its result phase still showing the FIRST one's verdict for the instant
        //   before `ui_perform_send` returns — a stale outcome attributed to a message that has not been sent yet.
        else if (out.kind == SendKind::channel_canned) { _chan = ChanState::submitting; _st.dirty = true; }
        return true;
    }
    bool emergency_pending() const { return _emg_req_pending; }

    // ------------------------------------------------------------------- §UI-7D slice B: the inbox detail/delete seam
    // ★★★ THE WHOLE SEAM IN FIVE STEPS, and it exists because this unit may not touch `g_node.inbox()`:
    //   1. the model emits a REQUEST carrying `(kind, seq)` — `take_inbox_request` below;
    //   2. `firmware_ui.cpp` performs the `pull()` / `erase()`;
    //   3. its pull callback COPIES AND SANITIZES the body BEFORE RETURNING (it calls `on_inbox_opened` from INSIDE the
    //      callback, while `InboxEntry::body` is still valid — that pointer is a use-after-free one line later);
    //   4. a TYPED answer comes back here;
    //   5. the renderer reads only the FROZEN `UiState`, never the live buffer.
    // ⚠ THIS IS NOT `take_send_request` AND MUST NOT BE CONFUSED WITH IT ([[B70]]): it does not blank the request, it
    //   MARKS IT TAKEN, because the PAIR has to survive until the answer can be checked against it. A second call
    //   returns false, so calling it twice cannot lose a request — the opposite failure mode from B70's.
    bool take_inbox_request(InboxReq& out) {
        if (_inbox_req.what == InboxWhat::none || _inbox_taken) return false;
        _inbox_taken = true; out = _inbox_req; return true;
    }
    // ★★ THE OPEN ANSWER. `body` is the store's own record bytes and `body_len` is their COUNT — ⛔ never `strlen`: an
    //    inbox body is not a C string and legitimately contains NUL (and `body` itself is `nullptr` whenever
    //    `body_len == 0`, e.g. an E2E-ack receipt). The copy through `ui_display_byte` happens HERE, i.e. inside the
    //    caller's pull callback, which is what makes the pointer safe to read at all.
    // ⛔ A CROSSED ANSWER IS ACTED ON IN NO WAY: unless a request is outstanding AND names this exact pair, nothing
    //    opens. That is the identity assertion at the second of its three sites (snapshot, activation, erase).
    void on_inbox_opened(InboxKind kind, uint32_t seq, uint8_t origin, uint8_t channel_id,
                         const uint8_t* body, uint8_t body_len, uint32_t now_ms) {
        if (!inbox_answer_is(InboxWhat::open, kind, seq)) return;
        clear_inbox_request();
        _st.detail_kind = kind; _st.detail_seq = seq;
        _st.detail_origin = origin; _st.detail_channel = channel_id;
        uint8_t n = body ? body_len : 0;                      // no body at all is a legitimate record, not an error
        if (n > MESHROUTE_NS::protocol::inbox_max_body) n = MESHROUTE_NS::protocol::inbox_max_body;
        for (uint8_t i = 0; i < n; ++i) _detail_body[i] = ui_display_byte(body[i]);
        _detail_body[n] = '\0'; _detail_len = n;
        // ★ pages = max(1, ceil(len / kDetailPageChars)) — ⛔ never zero: an empty body still has a page, or the modal
        //   would render `1/0` and the cycling arithmetic above would divide by zero.
        const uint8_t p = uint8_t((n + kDetailPageChars - 1) / kDetailPageChars);
        _st.detail_pages = p ? p : uint8_t(1);
        _st.detail_page = 0;
        _st.detail_action = InboxAction::back;                // spec §3.5: deletion costs short -> double, always
        _st.detail_del_failed = false;
        _st.detail = InboxModal::body;
        _st.inbox_pick_gone = false;
        _detail_page_at_ms = now_ms;
        refresh_detail_page();
        _st.dirty = true;
    }
    // The record named by the request was not in the store. ⇒ spec §3.5: REFUSE — do not open, and never open or delete
    // whatever now occupies that row. The INBOX list is rebuilt from the store every tick, so returning to it with the
    // refusal displayed IS the rebuilt list.
    void on_inbox_open_gone(InboxKind kind, uint32_t seq) {
        if (!inbox_answer_is(InboxWhat::open, kind, seq)) return;
        clear_inbox_request();
        _inbox_sel_valid = false; _st.inbox_pick_gone = true; _st.dirty = true;
    }
    // ★★★ THE DELETE ANSWER — all three outcomes, and NONE of them may make a record LOOK deleted without the store
    //     having said so (spec §3.5: "a visual disappearance without durable success is forbidden").
    // ⚠⚠ [[B134]], AND THE DIRECTION MATTERS: on every ESP32 target, `heltec_v3` included, the inbox is a VOLATILE RAM
    //    ring. `erased` therefore means the tombstone was appended and the record is gone from every future `pull()` IN
    //    THIS RUNTIME. ⛔ It is not a claim about surviving a power cycle — and ⛔⛔ that is NOT because the message would
    //    come back: a reboot destroys the record, its tombstone AND THE WHOLE INBOX together
    //    (`fixed_inbox_store.h`: `persisted_next_seq()` = 0, "seq restarts at 1 each boot"). ⇒ cross-reboot delete
    //    durability is owed by a DURABLE backend and is not testable on this board at all.
    void on_inbox_erased(InboxKind kind, uint32_t seq, InboxEraseResult r) {
        if (!inbox_answer_is(InboxWhat::erase, kind, seq)) return;
        clear_inbox_request();
        switch (r) {
            case InboxEraseResult::erased:
                // ★ "preserve the neighbouring selection where possible" (spec §3.5) — by IDENTITY, captured when the
                //   modal opened, so the next `sync_inbox_cursor` walks the highlight onto whatever row that record now
                //   occupies. With no neighbour (we deleted the only row) there is nothing to preserve and the cursor
                //   goes home rather than pointing past the end of a shorter list.
                _inbox_sel_kind  = _inbox_nb_kind; _inbox_sel_seq = _inbox_nb_seq;
                _inbox_sel_valid = _inbox_nb_valid;
                if (!_inbox_sel_valid) _st.cursor = 0;
                _st.inbox_pick_gone = false;
                close_detail();
                break;
            case InboxEraseResult::not_found:
                _st.detail = InboxModal::gone;        // TERMINAL, and it has no Delete action at all
                _st.detail_del_failed = false;
                _st.dirty = true;
                break;
            case InboxEraseResult::io_error:
                // Stay in the modal, say so, and put the selection back on the SAFE action: a retry then costs the
                // deliberate short -> double again rather than being one twitch away.
                _st.detail_del_failed = true;
                _st.detail_action = InboxAction::back;
                _st.dirty = true;
                break;
        }
    }
    // Diagnostics for the native suite and the probes: what the modal is showing, and the identity it will act on.
    InboxModal  detail_state()  const { return _st.detail; }
    InboxAction detail_action() const { return _st.detail_action; }
    InboxKind   detail_kind()   const { return _st.detail_kind; }
    uint32_t    detail_seq()    const { return _st.detail_seq; }
    uint8_t     detail_body_len() const { return _detail_len; }
    const char* detail_body()   const { return _detail_body; }

    // ---------------------------------------------------------------------------------- UI-3: emergency + DM
    Emergency emergency() const { return _emg; }
    DmState   dm_state()  const { return _dm; }
    ChanState chan_state() const { return _chan; }
    // ★★ §B69: WHICH of the two collapsed channel outcomes the LIVE alarm actually got. Read it beside
    //    `emergency()`; `not_heard` means two different things depending on it (see EmgEvidence).
    EmgEvidence emg_evidence() const { return _emg_evidence; }
    uint8_t   attempts()  const { return _tries; }
    // ★★★ §B115's DISPLAY ORDINAL — "which of the three attempts is in flight" — and it is PRESENTATION ONLY. See the
    // two-numbers block above `kEmgMaxTries` for the whole argument; the short form is: `_tries` is the LIMIT's single
    // source of truth and this is not a second copy of it, it is a different question. ⛔ Never test it against
    // `kEmgMaxTries`, and never clamp it.
    // ⚠ CONTRACT, the same discipline as `retry_at_ms()`: meaningful only while `emergency() == Emergency::firing`. It
    //   describes an attempt IN FLIGHT, and in every other state there is none — no arithmetic value is reserved to
    //   say so (§B74), the STATE is the predicate.
    uint8_t   emg_attempt_ordinal() const { return uint8_t(_tries + (_emg_attempt_counted ? 0 : 1)); }
    // ★ The compose sub-view's lifetime, which UI-7 needs OUTSIDE the model: it is what bounds a `late_ack` tracker
    //   slot (spec §3.4.1 upgrades NO CONFIRM -> DELIVERED only "while the sub-view is still showing") and, with it,
    //   the ONE normal send slot. See `ui_pump_trackers` — the obligation is discharged there, as a gate, not here.
    bool compose_open() const { return _st.compose != Compose::none; }
    // ⚠ Meaningful ONLY while `emergency() == Emergency::blocked`; after the retry fires the value is the spent
    // deadline. §B74: it is no longer sentinel-encoded, so there is no "no deadline" value to test for — the STATE is
    // the predicate. Any 32-bit value, `0xFFFFFFFF` included, is a legitimate deadline.
    uint32_t  retry_at_ms() const { return _retry_at_ms; }
    // The read side of the reason, in both alphabets. Spec §3.4.1/§4 require the panel to show WHICH refusal it was —
    // a generic failure is one the user cannot act on. `refuse_reason()` is the compact panel code (and the only one a
    // parser refusal has); §B73's `fail_reason()` is the core reason verbatim, for the async failures RefuseReason has
    // no code for.
    // ⚠ CONTRACT, same discipline as retry_at_ms(): both are written only when the model ENTERS a failure state, so
    // read them only while `dm_state() == DmState::failed` or `emergency() == Emergency::failed`. `no_key` /
    // `not_confirmed` need neither — those states ARE their reason (`no_pubkey` / `e2e_ack_timeout`), which is exactly
    // why the tracker gives them their own `Kind` instead of a generic failure plus a reason.
    RefuseReason refuse_reason() const { return _refuse; }
    FailReason   fail_reason()   const { return _fail; }
    // ★ UI-7: the SYNCHRONOUS refusal's `CmdCode`, verbatim. Meaningful only while `refuse_reason() != parser` — see
    //   `on_send_refused`. It is the third alphabet beside the compact reason and §B73's core reason, and it exists
    //   because `err_unsupported` covers five different walls that the panel must at least be able to NAME.
    MESHROUTE_NS::CmdCode refuse_code() const { return _refuse_code; }
    uint8_t   arming_secs_left(const UiSnapshot& s) const {
        if (_emg != Emergency::arming) return 0;
        const uint32_t left = _arm_fire_at_ms - s.now_ms;
        return (left > 60000u) ? 0 : uint8_t((left + 999) / 1000);        // wrap-safe: a huge value means past-due
    }

    // ★★★ §B113 (found by independent QA on UI-7, FIXED 2026-08-05) — THE THIRD ARM, AND WITHOUT IT
    // `ChanState::waiting` WAS A DEAD STATE: assigned zero times in the whole tree, referenced once, by
    // `firmware_ui.cpp`'s renderer arm. An ACCEPTED canned post therefore stayed on `submitting`, so the panel
    // read `SENDING...` until either the `channel_sent` verdict (up to ~36 s on a team post) or — first, on the common
    // path — the sub-view's own 15 s auto-exit. ⇒ a SUCCESSFUL send whose only feedback was a spinner that never
    // resolved, contradicting the bench guide's required sequence.
    // ⓘ **§T3 2026-08-14 — WHAT THAT ARM PRINTS HAS CHANGED, and the two sentences above are corrected rather than
    //   left drifting (V1):** `ChanState::waiting` renders **`QUEUED`**, because acceptance is five measured gaps
    //   short of the air; `SENT, waiting` moved to `ChanState::aired`, reached only by a correlated `send_aired`.
    //   ⛔ B113's defect and this arm's necessity are UNCHANGED — a post that never leaves `SENDING...` is still the
    //   regression; what moved is only which word acceptance is allowed to print.
    // ★ `waiting` MEANS WE HOLD A HANDLE, and that is the whole reason it may be reached only here: `ui_perform_send`
    //   calls this ONLY after `tr.accept(r.ctr)` with a non-zero ctr (§B39 — a `ctr == 0` result is parked in
    //   `awaiting` and never reaches acceptance), so the state cannot claim a transmission we do not own.
    // ⚠ THE `else` IS THE THIRD ARM OF A THREE-MEMBER ENUM, matching `on_send_refused` line for line (U3) — the two
    //   functions are the accept/refuse twins for the same three kinds and read as such. It must NOT become an
    //   unconditional write: an alarm's acceptance would then relabel a coincident canned post `SENT, waiting`, which
    //   is exactly the §2.1 crossover the two slots exist to prevent. A control pins that directly.
    void on_send_accepted(SendKind k, uint32_t now_ms) {
        // ★ §B115: `++_tries` COUNTS the attempt now in flight, so the flag is SET here (`= true`) and the display
        //   ordinal stops adding one for an attempt `_tries` already includes. This is the only place the flag is SET,
        //   exactly as `queue()` is the only place it is CLEARED (a freshly requested attempt is not yet counted) —
        //   one accept, one request, and the ordinal can never drift from the attempt it names.
        // ⚠ THE SEPARATION IS THE WHOLE FIX, so keep it: `_tries` stays THE LIMIT'S SINGLE SOURCE OF TRUTH and this
        //   remains its ONLY writer (§B84's unbounded-airtime argument rests on that single writer), while the ordinal
        //   is PRESENTATION ONLY and may never gate a send. Full argument: the two-numbers block above `kEmgMaxTries`.
        if (k == SendKind::emergency) { ++_tries; _last_try_ms = now_ms; _emg_attempt_counted = true; }
        else if (k == SendKind::dm)   { _dm = DmState::waiting_ack; }
        else                          { _chan = ChanState::waiting; }   // §B113: the canned-channel twin of waiting_ack
        _st.dirty = true;
    }
    // The SYNCHRONOUS refusal path (a parser reject or an immediate `err_*`) — it never became a core send, so there
    // is no `SendFailReason` for it and `_fail` is cleared to `none` rather than left describing an older failure.
    // ★★ §B78 (owner-ruled 2026-08-04): a terminal FAILED alarm is RETAINED and holds the panel like every other
    // emergency outcome. ⚠ `now_ms` is a PARAMETER and not `_last_input_ms` on purpose: the refusal can arrive well
    // after the gesture that caused it, and anchoring the window on the gesture is the same defect §4.3 was written to
    // kill (the outcome inherits a leftover window and the panel blanks seconds after the news). Only the EMERGENCY
    // branch retains — a DM refusal must not extend the alarm's window.
    // ★ UI-7: `code` is the SYNCHRONOUS `CmdCode` verbatim, and it is REQUIRED — no default (the §B73 precedent: a
    //   caller that has a reason must not be able to drop it silently). It is here because `CmdCode` CANNOT be mapped
    //   onto `RefuseReason` without inventing distinctions the core does not make: `no_key`, `no_identity`, `no_fix`,
    //   `empty` and `unsealable` ALL return `err_unsupported` (node.cpp:1530/1543/1553/1568) and differ only in a
    //   telemetry string the UI never sees. The plan's instruction is exact — "show the generic refusal AND THE CODE;
    //   do not invent a specific reason" — so the compact reason stays generic and the code rides beside it, rendered
    //   through `cmdcode_name` (U1: fw_main.cpp:905 already calls it "the ONE mapper, no second switch").
    // ⚠ CONTRACT, like `retry_at_ms()`: `refuse_code()` is meaningful only while `refuse_reason() != parser`. A line
    //   that never became a `Command` has no `CmdCode` at all, and `RefuseReason::parser` IS that predicate — no
    //   arithmetic value is reserved to mean "none" (§B74's discipline).
    void on_send_refused(SendKind k, RefuseReason r, MESHROUTE_NS::CmdCode code, uint32_t now_ms) {
        _refuse = r; _refuse_code = code; _fail = FailReason::none;
        if (k == SendKind::emergency) { _emg = Emergency::failed; retain(now_ms); }   // terminal + actionable, never a stuck SENDING...
        else if (k == SendKind::dm)   { _dm  = DmState::failed; }
        // ★ UI-7: the canned-channel arm was MISSING, and it was not a cosmetic gap — a refused canned post left
        //   `_chan` on `submitting`, i.e. the sub-view sat on `SENDING...` for ever for a send that never happened.
        //   That is §B72's defect on the non-alarm path, and the same C2 argument applies: fail LOUD, terminally.
        else                          { _chan = ChanState::failed; }
        _st.dirty = true;
    }
    // ★★★ THE CANNED-CHANNEL OUTCOME ENTRY POINT (UI-7), AND IT EXISTS BECAUSE `on_outcome` MUST NOT BE USED FOR THIS.
    // `on_outcome` is the EMERGENCY-capable path: any channel kind it receives may move a LIVE alarm, so routing a
    // canned post's outcome (or its expiry) through it lets an unrelated compose action alter a distress call — the
    // §2.1 false-confirmation class, reached from the one direction the tracker cannot filter (both are channel kinds
    // and both are correctly correlated; only the SLOT distinguishes them). `ui_pump_trackers` therefore had to drain
    // the normal expiry and DISCARD it, with the gap named in-source as Task 7's. This is that entry point.
    // ★ §B69 IS PAID HERE. `channel_no_relay` and `channel_remote_mint` land in DIFFERENT states, because they are
    //   different claims: one is "we transmitted and overheard nothing", the other is "we never held a handle and
    //   never listened". The renderer can finally distinguish them instead of printing one reading for both.
    // ⚠⚠ CORRECTED 2026-08-05 (V1). This paragraph used to end *"can finally say SENT for the second without saying it
    //   for the first"* — B69's obligation as written — AND THE CODE BELOW DELIBERATELY DOES NOT DO THAT. The comment
    //   was the stale half: `channel_remote_mint` maps to `unconfirmed`, which renders `NOT CONFIRMED`, never SENT.
    //   ★ THE MEASUREMENT THAT INVERTED THE OBLIGATION: B69 justified SENT with B39's producer (3), a registered
    //   mobile's DELEGATED GLOBAL post — a genuine success. `node.cpp:1401` computes
    //       want_global = c.u.channel.global || !c.u.channel.team
    //   and every channel line this UI sends carries `-t` with no `-g` (`ui_compose_send_line`), so `want_global` is
    //   FALSE and `do_send_channel_delegated` (node.cpp:1591-1601) is unreachable. On `-t -e` the only surviving
    //   `ctr == 0` producers are a pre-TX self-gate (node_channel.cpp:650) and a post-mint SEAL FAILURE (:744) —
    //   NEITHER a success. ⇒ SENT here would be the §2.1 false confirmation the obligation was written to prevent.
    //   ⓘ The `SendOutcome` kind stays a SUCCESS SHAPE inside the tracker (§B68 is untouched) and the tracker is
    //   generic, so a future plain/`-g` UI post would legitimately revive producer (3). Only the RENDERING differs.
    // ⓘ The DM kinds are REFUSED here rather than handled: a DM outcome belongs to `on_outcome`'s DM arms, which are
    //   already independent of the emergency. Two entry points writing `_dm` would be the fork U1 forbids.
    void on_channel_outcome(const SendOutcome& o, uint32_t now_ms) {
        using K = SendOutcome::Kind;
        (void)now_ms;   // no deadline here: the sub-view's own kBlankMs auto-exit is the display window (spec §3.2.1)
        switch (o.kind) {
            case K::channel_relayed:     _chan = ChanState::relayed;     break;
            case K::channel_no_relay:    _chan = ChanState::no_relay;    break;
            case K::channel_remote_mint: _chan = ChanState::unconfirmed; break;   // ★ §B69: never "no relay", never SENT
            case K::channel_failed:      _chan = ChanState::failed; note_failure(o.reason); break;
            case K::blocked:             _chan = ChanState::blocked;    break;
            // A DM outcome must never reach here — `_dm` has exactly one writer set (on_outcome). Listed explicitly,
            // with no `default:`, so a tenth SendOutcome::Kind fails the build instead of landing silently (§B72).
            case K::dm_acked: case K::dm_no_key: case K::dm_failed: case K::dm_timeout: return;
        }
        _st.dirty = true;
    }
    // ★★★★ §T3 — THE SCOPED MONOTONIC RANK, AND THE WORD "SCOPED" IS THE CORRECTION THAT MADE IT SAFE.
    // A `send_aired` is an ATTEMPT-level fact raised into a send-level surface. It is safe to raise because it can
    // only ever be an UPGRADE (`queued -> sent`) and no later attempt can contradict it — but it is NOT terminal and
    // it must never behave as though it were. The rank, applied ONLY when applying a correlated `send_aired` inside
    // one transaction, is:
    //        queued  <  aired  <  EVERY logical terminal outcome
    //   · `waiting_ack` / `waiting`      MAY become `aired_waiting` / `aired`;
    //   · an already-aired state is IDEMPOTENT (which is what removes any need for a de-duplication bit in `Node` —
    //     a repeated attempt for the same flight costs one push and changes nothing, so `sizeof(Node)` is unmoved);
    //   · ⛔ EVERY terminal state REFUSES it. All of them, not an enumerated pair: a delayed `send_aired` arriving
    //     after `DELIVERED`, `NO RELAY HEARD`, `NO CONFIRM`, `BLOCKED` or `FAILED` must change NOTHING.
    //   · ⛔ `idle` / `submitting` also refuse: a model on those states belongs to a NEWER transaction (the sub-view
    //     reset under a still-open tracker), and a new-transaction reset stays authoritative.
    // ⛔⛔ AND THIS RANK GOVERNS NOTHING ELSE. The existing terminal transitions — including the DELIBERATE late
    //    `NO CONFIRM -> DELIVERED` upgrade at `on_outcome`'s `K::dm_acked` arm — are untouched and remain
    //    authoritative. Stating the rank over ALL transitions (the first draft) would have broken both that upgrade
    //    and the reset a new send performs.
    // ★ BOTH SWITCHES ARE `default:`-LESS ON PURPOSE. A future `DmState`/`ChanState` must be CLASSIFIED as
    //   queued-like / aired / terminal by whoever adds it, rather than silently inheriting "refuse" (or, worse,
    //   "promote") from a catch-all. That is the enumerated-subset failure this rule exists to prevent.
    // ⓘ `now_ms` is unused: `send_aired` starts no window and retains no panel — the sub-view's own lifetime and the
    //   terminal outcome that follows own the display. Kept in the signature so every model entry point reads alike.
    void on_send_aired(SendKind k, uint32_t now_ms) {
        (void)now_ms;
        // ⛔ THE EMERGENCY SLOT NEVER REACHES HERE, and the caller is what guarantees it (`ui_route_send_push`).
        //   An attempt-level fact must not move a live alarm: `Emergency`, `ChanState` and `EmgEvidence` are the
        //   alarm's own evidence, and "the frame left the radio" is not evidence that anyone heard it. Guarded
        //   here as well, so a future second caller cannot re-open the hole.
        if (k == SendKind::emergency) return;
        if (k == SendKind::dm) {
            switch (_dm) {
                case DmState::waiting_ack:   _dm = DmState::aired_waiting; _st.dirty = true; return;   // queued -> aired
                case DmState::aired_waiting: return;                                                   // idempotent
                case DmState::idle: case DmState::submitting: return;                                  // a newer transaction owns the panel
                case DmState::delivered: case DmState::no_key:
                case DmState::not_confirmed: case DmState::failed: return;                             // ⛔ terminal: refuse
            }
            return;
        }
        switch (_chan) {
            case ChanState::waiting: _chan = ChanState::aired; _st.dirty = true; return;               // queued -> aired
            case ChanState::aired:   return;                                                           // idempotent
            case ChanState::idle: case ChanState::submitting: return;                                  // a newer transaction owns the panel
            case ChanState::relayed: case ChanState::no_relay: case ChanState::unconfirmed:
            case ChanState::blocked: case ChanState::failed: return;                                   // ⛔ terminal: refuse
        }
    }
    void on_outcome(const SendOutcome& o, uint32_t now_ms) {
        using K = SendOutcome::Kind;
        switch (o.kind) {   // DM outcomes are independent of the emergency and are handled first
            case K::dm_acked:   _dm = DmState::delivered;     _st.dirty = true; return;   // incl. the LATE-ack upgrade
            case K::dm_no_key:  _dm = DmState::no_key;        _st.dirty = true; return;
            case K::dm_timeout: _dm = DmState::not_confirmed; _st.dirty = true; return;
            case K::dm_failed:  _dm = DmState::failed;        note_failure(o.reason); _st.dirty = true; return;
            // ★ The channel kinds fall through to the emergency section below. They are listed EXPLICITLY and this
            // switch has NO `default:` — §B72 was a kind the type did not carry, and a `default:` is precisely what
            // would let a tenth kind land silently instead of failing the build on -Werror=switch.
            case K::channel_relayed: case K::channel_no_relay: case K::channel_remote_mint:
            case K::channel_failed:  case K::blocked: break;
        }
        // Only a LIVE alarm may be moved by a channel outcome. Anything else — idle, arming, cancelled, failed, and the
        // sticky picked_up / not_heard / reply — is left alone, so coincident channel traffic cannot manufacture or
        // resurrect an emergency (spec §2.1, second line of defence behind the tracker's ctr match). ⓘ That includes
        // channel_failed: a seal failure belonging to no live alarm is dropped whole, reason included.
        if (_emg != Emergency::firing && _emg != Emergency::blocked) return;
        // ★★★ §B69's CARRIER, WRITTEN HERE AND ONLY HERE. It is recorded AFTER the live-alarm guard on purpose: an
        // outcome that may not move the alarm may not describe its evidence either, or a coincident canned post's
        // verdict would relabel a distress result it had no part in (§2.1, the same argument as the guard itself).
        // ★ Monotone, never downgraded — `local_tx` is a fact about the alarm as a whole, and one locally-originated
        //   attempt that came back is what makes "we listened and heard nothing" TRUE. See EmgEvidence.
        if (o.kind == K::channel_relayed || o.kind == K::channel_no_relay) {
            _emg_evidence = EmgEvidence::local_tx;
        } else if (o.kind == K::channel_remote_mint && _emg_evidence == EmgEvidence::none) {
            _emg_evidence = EmgEvidence::no_handle;
        }
        if (o.kind == K::blocked) {
            _emg = Emergency::blocked;
            const uint32_t d = (o.next_ms > 0) ? o.next_ms : next_backoff();
            _retry_at_ms = now_ms + d; _retry_armed = true;    // ★ from the OUTCOME time, not the gesture
            retain(now_ms); _st.dirty = true; return;
        }
        // ★ §B72: pre-enqueue failure. TERMINAL and actionable — never one of the three alarms, and never a retry:
        // `unsealable` / `no_location` are PERMANENT for this route (command.h), so a retry would burn the budget and
        // still fail. Same landing state as the synchronous refusal, because it is the same event arriving late.
        // ★★ §B78 (owner-ruled 2026-08-04): it RETAINS, from the OUTCOME time. This lands identically to
        // on_send_refused because it is the same event arriving late, and `failed` is now in `hold_active()`'s set —
        // which is also what lets UI-6's short press acknowledge it (B71), so the hiker is never trapped on a failure
        // screen they were never shown.
        if (o.kind == K::channel_failed) {
            _emg = Emergency::failed; note_failure(o.reason); retain(now_ms); _st.dirty = true; return;
        }
        if (o.kind == K::channel_relayed) { _emg = Emergency::picked_up; retain(now_ms); _st.dirty = true; return; }
        // ★ channel_no_relay and channel_remote_mint (§B68) share this path deliberately: NEITHER carries relay
        // evidence, so neither may claim PICKED UP, and both leave the alarm unconfirmed ⇒ bounded retry. They differ
        // only in what the RENDERER should say (SENT vs NOT HEARD), and the model has no state for that — register B69.
        if (_tries >= kEmgMaxTries) { _emg = Emergency::not_heard; retain(now_ms); _st.dirty = true; return; }
        _emg = Emergency::firing; queue(SendKind::emergency, 0, 0); _st.dirty = true;
    }
    // ★ Whitelist + "an alarm actually went out". Accepting every non-idle state would let a coincident channel-0 post
    // become REPLY during `arming` (before the user even committed), or after `cancelled`/`failed` — manufacturing
    // confirmation of a message that was never sent. Spec §4.4.
    void on_reply(const char* who, const char* text, uint32_t now_ms) {
        const bool ok = (_emg == Emergency::firing || _emg == Emergency::blocked ||
                         _emg == Emergency::picked_up || _emg == Emergency::not_heard || _emg == Emergency::reply);
        if (!ok || _tries == 0) return;
        copy_clamped(_reply_who,  who,  sizeof _reply_who);
        copy_clamped(_reply_text, text, sizeof _reply_text);
        _emg = Emergency::reply; retain(now_ms); _st.dirty = true;
        // ★★★ R1 (OWNER-RULED 2026-08-05) — AN ARRIVING REPLY UN-BLANKS THE PANEL, and this line is the whole fix.
        // Before it NOTHING un-blanked on an incoming push: `dirty` was set, but `blanked` stayed true, so
        // `FrameGate::step` kept answering `blank` and the answer to a distress call waited behind a panel that is OFF
        // until the hiker happened to press the button. `dirty` alone was never enough — the blank is tested FIRST.
        // ★ IT IS HERE, PAST BOTH GUARDS, AND THAT PLACEMENT IS THE RULING'S "NOT WAKE-ON-ANY-PUSH" HALF:
        //   ① the caller (`ui_route_recv_push`) has already applied §4.4's team scope — a stranger's channel-0 post
        //      never reaches this function at all (§B103); and
        //   ② the whitelist + `_tries == 0` above have already refused everything that is not an answer to an alarm we
        //      actually transmitted. ⇒ what wakes the panel is a REPLY, not team chatter. Both are pinned by their own
        //      controls in test_firmware_ui_send.cpp; without them this fix is indistinguishable from wake-on-any-push.
        // ★ EDGE-TRIGGERED, still (spec §5): this is a STATE TRANSITION, not a per-tick write. The board latches
        //   `set_power_save`, and the ONE resulting DISPLAYON is asserted as a command SEQUENCE, not as a flag.
        // ⓘ NO SECOND TIMER (U1/C2): `retain()` on the line above already gives §4.3's kEmgHoldMs deadline, measured
        //   from THIS reply's own arrival — so the woken panel stays lit for a full window and then blanks with the
        //   state retained. `_last_input_ms` is deliberately untouched, and it is inert either way because
        //   kEmgHoldMs > kBlankMs (asserted in the test, not argued here).
        _st.blanked = false;
    }
    const char* reply_who()  const { return _reply_who; }
    const char* reply_text() const { return _reply_text; }

    // ★★ §B71's exit PREDICATE — "an alarm has reached a terminal, readable answer". DERIVED as a set, not named from
    // the ruling's prose, and one member of that prose WAS VACUOUS: the ruling as first recorded listed "final
    // `blocked`", but `blocked` is never final in this model — `on_outcome`'s `K::blocked` arm ALWAYS sets
    // `_retry_armed`, and `tick_emergency` always re-fires from it, so a `blocked` alarm is by construction still in
    // flight. Including it would have made the exit fire mid-retry, which is precisely what the ruling's first row
    // forbids. ⇒ four states, not five.
    // ✅ §B100, OWNER-AGREED 2026-08-05: the phantom member is now TRIMMED FROM THE RULING ITSELF (the plan's B71
    // table), so document and code finally enumerate the same four. ⛔ Nothing here changed — the trim removed a phantom
    // obligation from a doc, never a behaviour from this predicate; the vacuity stays ASSERTED by the "an IN-FLIGHT
    // alarm does not exit" test, which drives a real `blocked` outcome and checks this returns false.
    // ⓘ `cancelled` is excluded deliberately: nothing was sent, and it self-clears after kCancelledMs, so there is no
    // outcome to acknowledge. `arming` / `firing` are the in-flight rows.
    bool emg_outcome_retained() const {
        const bool terminal = _emg == Emergency::picked_up || _emg == Emergency::not_heard ||
                              _emg == Emergency::reply     || _emg == Emergency::failed;
        return terminal && _emg_seen == _emg_news;   // ★★ §B102: terminal AND ACTUALLY PRESENTED
    }

    // ★★★ §B102/F3 — "AN OUTCOME THE HIKER ACTUALLY SAW", made TRUE rather than ASSUMED.
    // B71's ruling permits a SHORT press to acknowledge an alarm "once its result has been seen", and the argument for
    // safety rested on "a retained outcome is ALWAYS displayed before any press can dismiss it". That was an
    // assumption about TIMING: a frame takes eight ticks, `InputFsm` delivers a gesture that was already in progress,
    // and the MAC-idle gate can hold every one of those ticks — so a press could dismiss a distress result before its
    // FIRST page reached the panel. The user then sees the alarm vanish and never learns the answer.
    // ⇒ `retain()` — which EVERY retained outcome goes through, and which a NEW reply re-enters with new text — bumps
    //   `_emg_news`. `FrameGate` reports back the news value a COMPLETED frame actually put in front of the user. The
    //   exit opens only when the two agree, so "seen" is a measurement.
    // ⓘ NO ARITHMETIC VALUE IS RESERVED (§B74's discipline): both counters start at 0 and the FIRST `retain()` makes
    //   `_emg_news` 1, while `terminal` is false for `idle` — so the initial 0 == 0 can never open the exit.
    uint32_t emg_news() const { return _emg_news; }
    void mark_outcome_presented(Emergency shown, uint32_t news) {
        if (shown == _emg && news == _emg_news) _emg_seen = news;   // the frame must match the CURRENT news, not an older one
    }

protected:
    // Wrap-safe elapsed time. millis() wraps at ~49.7 days; `a >= b` would break across it, this does not.
    static uint32_t elapsed(uint32_t now, uint32_t then) { return now - then; }
    void queue(SendKind k, uint8_t peer, uint8_t idx) {
        // ★★ §B115: THE ORDINAL'S ONE WRITE POINT ON THE REQUEST SIDE, and `queue()` is chosen because it is the ONE
        // choke point all three alarm requests go through — `long_fire`, `on_outcome`'s bounded retry and
        // `tick_emergency`'s blocked retry. A new attempt is requested and `_tries` has not counted it yet, so the
        // ordinal is `_tries + 1` from here until `on_send_accepted` (or the §B84 expiry that stands in for it) counts
        // it. ⛔ Do not also reset it in `long_fire`: `long_fire` ends by calling this, and a second writer is how the
        // two numbers drift apart again.
        if (k == SendKind::emergency) { _emg_req_pending = true; _emg_attempt_counted = false; return; }   // its own slot; never overwritten
        _req = {k, peer, idx}; _req_pending = true;
    }

    // ★ Spec §4.3: every retained emergency state refreshes the `kEmgHoldMs` panel-on DEADLINE — long_fire, then
    // blocked / picked_up / not_heard / reply, and (§B78) `failed`. Anchoring it only at long_fire (an earlier draft)
    // meant an outcome or a reply arriving a whole window later inherited the leftover time and the panel blanked
    // seconds after the news arrived.
    void retain(uint32_t now_ms) { _emg_hold_until_ms = now_ms + kEmgHoldMs; ++_emg_news; }

    UiState  _st{};
    uint32_t _last_input_ms = 0;
    bool     _seeded = false;            // B65: _last_input_ms is meaningless until the first tick/gesture
    SendReq  _req{};
    bool     _req_pending = false;
    bool     _emg_req_pending = false;   // separate slot: normal work can never clobber a queued alarm

    // UI-3 state. Every deadline comparison is a wrap-safe unsigned difference, never `now >= then`.
    // ★★ §B74: `_retry_armed` replaces a `0xFFFFFFFF` "no deadline" SENTINEL, and the bug it fixes was on the alarm
    // path. `_retry_at_ms = now_ms + next_ms` is unbounded, so it can land exactly on any 32-bit value — `now =
    // 0xFFFFF000`, `next_ms = 0xFFF` produces `0xFFFFFFFF` from perfectly ordinary inputs. tick_emergency then refused
    // to even examine the deadline and the emergency stayed BLOCKED FOR EVER. ⇒ a separate flag, so NO arithmetic
    // value is reserved. Never reintroduce a magic deadline value here.
    Emergency    _emg    = Emergency::idle;
    DmState      _dm     = DmState::idle;
    ChanState    _chan   = ChanState::idle;    // UI-7: the canned-channel twin of _dm; §B69's carrier for that path
    RefuseReason _refuse = RefuseReason::other;
    FailReason   _fail   = FailReason::none;   // §B73: the core reason, verbatim, beside the compact one
    // UI-7: the SYNCHRONOUS refusal's CmdCode, verbatim. Read only while `_refuse != parser` — see on_send_refused.
    MESHROUTE_NS::CmdCode _refuse_code = MESHROUTE_NS::CmdCode::queued;
    // ★★ §B69: which of the two collapsed channel outcomes THIS alarm actually got. Sticky, monotone, reset by a new
    //    alarm. It is what stops `NOT HEARD`'s detail line from claiming a measurement the alarm never took.
    EmgEvidence  _emg_evidence = EmgEvidence::none;
    uint8_t  _tries = 0;                 // ACCEPTED transmissions, never requests (spec §4) — ★ THE LIMIT'S ONLY TRUTH
    // ★★ §B115: has `_tries` counted the attempt CURRENTLY IN FLIGHT? Set false by `queue()` (a new attempt is asked
    // for), true by `on_send_accepted` (it has been counted). Read ONLY by `emg_attempt_ordinal()`, i.e. by the panel —
    // ⛔ it is not an input to any airtime, retry or terminal decision, and must never become one.
    bool     _emg_attempt_counted = false;
    bool     _retry_armed       = false; // §B74: the blocked-retry deadline is live (NOT encoded in _retry_at_ms)
    uint32_t _retry_at_ms       = 0;
    uint32_t _last_try_ms       = 0;     // UI-4's outcome window; written here, unread until then
    uint32_t _arm_fire_at_ms    = 0;
    uint32_t _cancelled_until_ms = 0;
    uint32_t _emg_hold_until_ms = 0;
    uint32_t _backoff_ms        = 0;     // the next_ms==0 UI backoff, doubling to kBlockedBackoffMaxMs
    uint8_t  _last_countdown    = 0;     // so ARMING repaints only when the visible digit changes (spec §4.3)
    uint32_t _emg_news = 0, _emg_seen = 0;   // §B102: retained-outcome news vs. what a COMPLETED frame presented
    // ★★ §B64: the TEAM cursor's selection, held by team-plane IDENTITY rather than by row index. See
    //    `sync_team_cursor` for the whole argument, the C3 plane note and why no arithmetic value is reserved.
    uint8_t  _team_sel_id    = 0;
    bool     _team_sel_valid = false;
    char     _reply_who[kLabelCap + 1] = {};
    char     _reply_text[21]           = {};

    // ★★★★ §UI-7D slice B's STATE. The INBOX selection is held by IDENTITY for the §B64 reason one plane over: the row
    //     index is not the message. `_inbox_nb_*` is the neighbour a successful delete falls back to.
    // ⓘ NO ARITHMETIC VALUE IS RESERVED (§B74's discipline): `_inbox_sel_valid` is a separate flag, so seq 0 — which
    //   inbox.h documents as the "before everything" cursor and `erase()` reports as `not_found` — needs no special case
    //   and can never be confused with "nothing is selected".
    InboxKind _inbox_sel_kind = InboxKind::dm;
    uint32_t  _inbox_sel_seq  = 0;
    bool      _inbox_sel_valid = false;
    InboxKind _inbox_nb_kind  = InboxKind::dm;
    uint32_t  _inbox_nb_seq   = 0;
    bool      _inbox_nb_valid = false;
    // The in-flight request. ★ `_inbox_taken` says the request has been HANDED OUT and an answer is owed; the pair stays
    //   readable until that answer arrives, because checking the answer against it is the identity assertion.
    InboxReq  _inbox_req{};
    bool      _inbox_taken = false;
    // ★★ THE MODAL'S BODY, LIVE AND NOT FROZEN — `inbox_max_body + 1` bytes, held for the modal's lifetime so nothing
    //    ever dereferences the callback-owned `InboxEntry::body` after `pull()` has returned (spec §3.5; that pointer is
    //    into the store's own record bytes and is valid for the callback only). The FRAME freezes just the current page
    //    (`UiState::detail_line`), which is what keeps a 242-byte buffer off the per-frame copy.
    char      _detail_body[MESHROUTE_NS::protocol::inbox_max_body + 1] = {};
    uint8_t   _detail_len = 0;
    uint32_t  _detail_page_at_ms = 0;   // when the visible page was last turned; the 2 s cadence measures from here
    // ★★ §UI-14's ONE new member: a POINTER to the staged-config service, never an instance (see `attach_config`).
    //    ⇒ this model gains 8 bytes and no config state of its own — the draft, the baseline and the conflict latch
    //    all live in the service, which is what makes "there is one draft" structural rather than a convention.
    mrfw::ConfigService* _cfg = nullptr;
    // ★ §UI-15 slice 5's ONE new member, and it is the same shape and the same argument as `_cfg` above: a POINTER to
    //   the ADAPTER, never an instance. The transaction, its store and its entropy all live behind it, so this model
    //   gains 4/8 bytes and no provisioning state of its own beyond the answer it was handed.
    IUiProvision* _prov = nullptr;
    // ★★★★ §UI-15 slice 6 — THE JOIN SESSION, AND IT IS DELIBERATELY **NOT** IN `UiState`: it is not rendered (the
    //      one thing the panel needs from it, the 60 s word change, is latched into `UiState::join_still`), and it
    //      must OUTLIVE the screen. Freezing it with the frame would suggest it belonged to a picture.
    // ★★★ ITS LIFETIME IS PLAN §2.3's, TERM BY TERM, AND EVERY EDGE IS THE PLAN's RATHER THAN A CHOICE MADE HERE:
    //      · it STARTS when a transaction answers `started` (§2.3 rule 1 — the write happened and DAD began);
    //      · ⛔ BACK does NOT end it (§2.3 rule 4: *"BACK during JOINING only LEAVES THE SCREEN — it does not cancel
    //        or roll back an already-persisted operation"*). Neither does the blank, nor the screen cycle;
    //      · ⛔ THE 60 s DEADLINE DOES NOT END IT EITHER (§2.3 rule 5: retries are not finitely bounded, so a
    //        deadline that ended the session would be the failure the rule forbids, wearing a different name);
    //      · ⛔ THE ALARM DOES NOT END IT (§8 rule 1 pre-empts the SCREEN; *"an unconfirmed destructive action does
    //        not survive"* — a join that is already persisted is neither unconfirmed nor cancellable from here);
    //      · it ENDS on a CORRELATED `join_adopted` (§2.3 rule 2), and on a NEW `started` transaction replacing it.
    // ⚠ CONSEQUENCE, STATED RATHER THAN DISCOVERED: a correlated adopt that lands while the operator has walked away
    //   from the waiting screen ENDS the session and shows nothing. ⛔ It does NOT drag the panel to a result screen
    //   the operator did not ask for — nothing in §2.3 or §3.6.5 authorises a push to navigate, and §3.6.5's own rule
    //   is that these states are *"never triggered by the waking press"*. The screen that was up is the screen that
    //   completes.
    UiJoinSession _join{};
    // ★ The SETTINGS cursor's selection, held by ROW IDENTITY rather than by index — see `sync_settings` for why that
    //   is live here and not merely tidy. ⓘ NO ARITHMETIC VALUE IS RESERVED (§B74): `_cfg_sel_valid` is its own flag,
    //   so row 0 needs no special case and cannot be confused with "nothing is selected".
    CfgRow _cfg_sel_row   = CfgRow::back;
    bool   _cfg_sel_valid = false;

    uint32_t next_backoff() {
        _backoff_ms = (_backoff_ms == 0) ? kBlockedBackoffMinMs
                                         : ((_backoff_ms * 2 > kBlockedBackoffMaxMs) ? kBlockedBackoffMaxMs : _backoff_ms * 2);
        return _backoff_ms;
    }
    static void copy_clamped(char* dst, const char* src, std::size_t cap) {
        std::size_t i = 0; for (; src && src[i] && i + 1 < cap; ++i) dst[i] = src[i]; dst[i] = '\0';
    }
    // §B73: record an ASYNC failure in both alphabets — the core reason verbatim, plus the compact code the panel
    // reads. `default:` is deliberate and is NOT the -Wswitch hole §B72 was: command.h's `SendFailReason` is
    // documented APPEND-ONLY and grows on core's schedule, a new reason is legitimately "generic" to this panel, and
    // `_fail` carries it losslessly regardless. The three mapped reasons are the ones whose remedy differs.
    void note_failure(FailReason r) {
        _fail = r;
        switch (r) {
            case FailReason::unsealable:  _refuse = RefuseReason::unsealable;  break;
            case FailReason::no_location: _refuse = RefuseReason::no_location; break;
            case FailReason::queue_full:  _refuse = RefuseReason::queue_full;  break;
            default:                      _refuse = RefuseReason::other;       break;
        }
    }

private:
    void advance_or_next(const UiSnapshot& s) {
        const uint8_t n = list_len(s);
        if (n > 1 && _st.cursor + 1 < n) { ++_st.cursor; return; }
        _st.screen = next_screen(_st.screen, s); _st.cursor = 0;
    }
    // ★★★★ §B64 IS PAID HERE (OWNER-RULED 2026-08-05) — THE SEND TARGET IS THE REMEMBERED TEAMMATE, NEVER A ROW INDEX.
    // ⛔ WHAT THIS LINE USED TO BE: `_st.compose_peer = s.team[_st.cursor % s.team_shown].id`. The modulo kept the read
    //    in range when a later snapshot carried fewer rows than the cursor the previous tick left behind — but its
    //    EFFECT was that a cursor on row 2 meeting a 2-row roster opened the DM modal bound to ROW 0. That is a
    //    MIS-SEND, not a display glitch: "Are you OK?" went to a teammate the user never highlighted. Plan `:135`
    //    deferred it to Tasks 6/7 with *"that is a MIS-SEND … it needs a ruling before Task 7 wires real sends"*, and
    //    Task 7 wired them without resolving it.
    // ★ THE RULING, VERBATIM: *preserve the selection by teammate IDENTITY across roster refreshes; the cursor tracks
    //   the teammate, not the row index; if that teammate has disappeared from the roster, REFUSE activation and
    //   repaint — never silently select another row.*
    // ⛔ AND IT IS NOT A CLAMP. Clamping to `shown - 1` or to `0` is the tempting near-miss and it is the SAME class of
    //   defect one index over: it still SENDS, just to a different wrong teammate. The refusal is what makes the
    //   difference measurable — every clamp queues a request; the ruling queues nothing.
    void activate(const UiSnapshot& s) {
        if (_st.screen == Screen::team) {
            if (!_team_sel_valid) {
                // C2 — FAIL LOUD. `sync_team_cursor` has already announced a pick that vanished; announce it here too
                // so the refusal is self-contained rather than relying on which call ran first.
                // ⓘ An EMPTY roster is left alone: that screen already says "no teammates heard", which IS the reason.
                if (s.team_shown > 0) _st.team_pick_gone = true;
                _st.dirty = true;                        // "and repaint" — stated here, not inherited from the caller
                return;                                  // ⇒ NOTHING is queued. That is the whole assertion.
            }
            _st.compose = Compose::dm; _st.compose_peer = _team_sel_id; _st.cursor = 0;
        } else if (_st.screen == Screen::send) {
            _st.compose = Compose::channel; _st.compose_peer = 0; _st.cursor = 0;
        } else if (_st.screen == Screen::inbox) {
            // ★★★★ §UI-7D slice B — WHAT USED TO BE A DELIBERATE NO-OP. Spec §3.2: a `double` on INBOX opens the detail
            //     modal. It is the SAME shape as §B64's TEAM activation and for the same reason: the thing activated is
            //     the remembered RECORD, never the row the cursor happens to be sitting on.
            // ⛔ AND IT REFUSES RATHER THAN CLAMPING OR RE-READING THE ROW. A selection that is no longer in the store
            //    (or was never identifiable) queues NOTHING and says so — because the alternative shape, "open whatever
            //    is at this index now", opens somebody else's message and puts a DELETE two presses away from it.
            // ⓘ An EMPTY list is left silent: the screen already says it has no stored rows, which IS the reason. Same
            //   carve-out as the empty TEAM roster.
            if (!_inbox_sel_valid) {
                if (s.inbox_shown > 0) _st.inbox_pick_gone = true;
                _st.dirty = true;
                return;                                  // ⇒ NOTHING is requested. That is the whole assertion.
            }
            note_inbox_neighbour(s);                     // captured NOW, so a successful delete can land beside it
            _inbox_req = { InboxWhat::open, _inbox_sel_kind, _inbox_sel_seq };
            _inbox_taken = false;
        } else if (_st.screen == Screen::settings) {
            settings_activate(s);
        }
    }
    // ================================================================================== §UI-14 — the SETTINGS screen
    // ★★★ THE EDITOR'S LIFETIME BELONGS TO THE SCREEN, AND THAT IS THE POINT OF THIS FUNCTION. It runs on every
    //     gesture AND every tick (the two places the other two cursors re-anchor), so:
    //       · arriving on SETTINGS OPENS the service — `open()` snapshots the persisted covered fields and records the
    //         baseline (§3.6.1). ⚠ RE-ENTERING returns `already_open`, which is a NO-OP BY CONTRACT: the draft
    //         SURVIVES leaving and coming back, because losing it there would be the attention-timeout discard §3.6.1
    //         forbids, arriving through the door instead of the timer.
    //       · leaving SETTINGS closes the EDITOR but ⛔ NEVER the service. There is no `close()` and there must not be
    //         one: the draft, the conflict latch and `reboot_required` all have to outlive the screen — §3.6.5 says a
    //         saved-but-reboot-required state stays visible until the reboot.
    // ⓘ `open()` on a tick is free after the first: `already_open` returns before touching the store, so this is not a
    //   flash read per tick.
    // ★★ THE EDITOR MAY NEVER OUTLIVE ITS SCREEN, AND THIS IS THE ONE PRIMITIVE THAT ENFORCES IT — called from BOTH
    //    paths that can leave SETTINGS (the `short` walk off the last row, and `sync_settings` itself), because a
    //    guard belongs to the INVARIANT and not to the site where it was first needed. ⛔ Leaving it to `sync_settings`
    //    alone is a real hole: the walk-off happens INSIDE a gesture, after that gesture's sync has already run, so
    //    the state would stay `browsing`/`editing` for the rest of the pass — and a frame frozen in between would
    //    render a SETTINGS editor over the STATUS screen.
    void settings_follow_screen() {
        if (_st.screen == Screen::settings) return;
        if (_st.settings != Settings::closed) { _st.settings = Settings::closed; _st.dirty = true; }
        // ★★★★ §UI-15 slice 4, plan §5's FIRST PIN — **PROVISIONING IS CLOSED WHENEVER SETTINGS IS LEFT**, paid HERE
        //     because this is the one primitive BOTH leave-paths already run (the `short` walk off the last row and
        //     the `back` row), and a guard belongs to the INVARIANT rather than to the site where it was needed.
        // ★★ THE DECISION IS NOT WRITTEN HERE, and that is [[B223]]'s correction rather than a style choice: for every
        //    state the model can reach TODAY this call cannot change anything, because the sub-view OWNS THE PRESS —
        //    while it is open no gesture reaches `advance_or_next` or the `back` row, so `_st.screen` cannot leave
        //    SETTINGS underneath it. An in-line reset would therefore be undrivable and unmutatable HERE, so the
        //    reset is `provision_reset_on_leave`'s (see it) and this is a forward. The invariant is "leaving closes
        //    it", not "the paths that can leave today close it": the first slice-5/6 arm that a push or a timeout
        //    moves the screen out of finds this already true.
        if (provision_reset_on_leave(_st.provisioning, _st.prov_confirm)) _st.dirty = true;
        _cfg_sel_valid = false;
    }
    void sync_settings(const UiSnapshot& s) {
        settings_follow_screen();
        if (_st.screen != Screen::settings) return;
        if (_st.settings == Settings::closed) { _st.settings = Settings::browsing; _st.dirty = true; }
        if (_cfg && !_cfg->is_open()) {
            // ⛔ A REFUSED OPEN IS NOT RETRIED SILENTLY BEHIND A WORKING-LOOKING MENU: `no_record` means the store
            //    could not produce a record, so there is no baseline and nothing may be saved. The renderer says so
            //    (C2) and every activation below refuses, because `is_open()` stays false.
            (void)_cfg->open();
        }
        // ★★★★ THE CURSOR TRACKS THE ROW, NOT THE INDEX — §B64's ruling and §B66's lesson, arriving on a THIRD screen
        //     and for a reason that is LIVE rather than hypothetical: the RELOAD row is CONDITIONAL, so the list grows
        //     by one at the exact moment a refused SAVE raises the conflict. ⇒ a cursor left on index 4 was pointing
        //     at SAVE and would now be pointing at RELOAD, and the operator's next `double` — aimed at the row they
        //     were looking at one press ago — would perform a DIFFERENT action. Re-anchoring by identity is what makes
        //     the highlight and the act agree.
        // ⓘ WHY THE LOST-ROW LANDING IS DIFFERENT FROM TEAM's AND INBOX's, and it is a reasoned difference rather
        //   than a weaker rule: there, a vanished selection meant somebody ELSE's message or teammate could be hit, so
        //   activation is REFUSED. Here the only row that can vanish is RELOAD, it vanishes only because its own
        //   conflict was just resolved, and nothing in this menu can address the wrong record — so the cursor lands on
        //   the SAFE action (BACK, which leaves and preserves) and the panel shows that highlight before any press.
        // ★★ §UI-15 slice 4 — THE SUB-VIEW'S GUARD, AND IT IS `sync_team_cursor`'s COMPOSE GUARD VERBATIM (U3): while
        //    PROVISION is open `_st.cursor` is the PROVISION menu's index, not a SETTINGS row, so re-anchoring it here
        //    would drag the highlight back onto the PROVISION row every tick — and the operator's next press would
        //    then act on whatever child that index named. ⓘ The SETTINGS pick itself is deliberately left INTACT, so
        //    closing the sub-view lands the highlight back on PROVISION where the operator left it.
        if (_st.settings == Settings::provisioning) return;
        const CfgRowList l = settings_row_list(s);
        if (!_cfg_sel_valid) { note_settings_cursor(s); return; }   // first arrival: whatever row 0 is IS the pick
        for (uint8_t i = 0; i < l.n; ++i) {
            if (l.row[i] != _cfg_sel_row) continue;
            if (_st.cursor != i) { _st.cursor = i; _st.dirty = true; }   // the row MOVED -> the highlight follows
            return;
        }
        for (uint8_t i = 0; i < l.n; ++i)
            if (l.row[i] == CfgRow::back) { _st.cursor = i; _cfg_sel_row = CfgRow::back; _st.dirty = true; return; }
    }
    // The WRITE side, exactly as `note_team_cursor` / `note_inbox_cursor` are: whatever row the cursor has come to
    // rest on IS the new selection, so "the pick" is always something the operator's last press actually pointed at.
    void note_settings_cursor(const UiSnapshot& s) {
        // ⛔ The same sub-view guard as `sync_settings`'s, and it belongs to the INVARIANT rather than to the site
        //    where it was first needed: a PROVISION-menu index read as a SETTINGS row would REPLACE the remembered
        //    pick with whatever row that number happens to name — `DISCARD`, one row from `BACK`.
        if (_st.settings == Settings::provisioning) return;
        CfgRow r{};
        if (_st.screen == Screen::settings && settings_row_list(s).at(_st.cursor, r)) {
            _cfg_sel_row = r; _cfg_sel_valid = true; return;
        }
        _cfg_sel_valid = false;
    }
    // ★★★ `short` WHILE EDITING — THE SECOND OF ITS TWO MODES. It CYCLES the highlighted row's finite value in the RAM
    //     DRAFT and nothing else: ⛔ no live mutation, no radio retune, no flash write (§3.6.1). `double` ACCEPTS,
    //     which is a pure state change — the value is ALREADY in the draft, so "accept" costs nothing and "leave" is
    //     not a revert. ⓘ That is deliberate and is what the LONG-press rule below depends on.
    void settings_edit_gesture(Gesture g, const UiSnapshot& s) {
        if (g != Gesture::short_press && g != Gesture::double_press) return;
        if (g == Gesture::double_press) { _st.settings = Settings::browsing; _st.dirty = true; return; }
        CfgRow r{}; mrfw::CfgField f{};
        // ⛔ FAILS CLOSED at both steps: a cursor that names no row, or a row that edits no field, cycles NOTHING.
        //    Neither is reachable today (only a value row enters `editing`), and both are written out rather than
        //    assumed because the alternative — writing `_draft.at(<whatever>)` — is a wrong-field edit.
        if (!settings_row_list(s).at(_st.cursor, r) || !cfg_row_field(r, f)) { _st.settings = Settings::browsing; return; }
        if (!_cfg) return;
        (void)_cfg->set(f, cfg_menu_next(f, _cfg->draft().at(f)));
        _st.dirty = true;
    }
    // ★★★ `double` IN THE MENU — enters a value row, or performs an action row. EVERY branch that changes anything
    //     goes through the SERVICE (§3.6.1: ⛔ never a loop over `handle_cfg_set`, never a manufactured command line).
    void settings_activate(const UiSnapshot& s) {
        clear_settings_note();                       // the note belongs to the act about to happen, never to the last
        CfgRow r{};
        if (!settings_row_list(s).at(_st.cursor, r)) return;     // fails closed — see CfgRowList::at
        mrfw::CfgField f{};
        if (cfg_row_field(r, f)) {
            // ⛔ Refused while the service is not open: there is no draft to edit, and an editor over nothing would
            //    show a value the operator could change and never save.
            if (_cfg && _cfg->is_open()) { _st.settings = Settings::editing; _st.dirty = true; }
            return;
        }
        switch (r) {
            // ★★★★ §UI-15 slice 4 — §3.6.3's PRECONDITION, AND PLAN §4 SPLITS IT INTO **TWO CELLS WITH TWO
            //     REMEDIES**. §3.6.2's table says only *"opens §3.6.3"*; §3.6.3 adds *"if a settings draft is
            //     unsaved, PROVISION first requires SAVE or DISCARD"*; and plan §4 records that v1 CONFLATED the two
            //     states the service actually distinguishes:
            //       · `conflict()`       — `/mrcfg` moved under the draft. ⛔ The note may NOT say SAVE: `save()`
            //                              refuses a conflict (gate 2a), so RELOAD or DISCARD are the ways out.
            //       · `config_unsaved()` — the ordinary unsaved draft. SAVE or DISCARD.
            //     ⇒ CONFLICT IS CHECKED FIRST, and the order is the behaviour rather than a style: a conflicted draft
            //     is USUALLY also unsaved, so testing `config_unsaved()` first would print SAVE for a conflict — the
            //     exact conflation, arriving through the ordering.
            // ⛔⛔ AND IT NEVER SAVES ON THE OPERATOR'S BEHALF (C2). A gate that "helpfully" saved would spend a flash
            //     write and change the persisted config for a press that asked for neither — and on the conflict cell
            //     it would be the last-writer-wins outcome §3.6.1 forbids. The suite counts the writes.
            // ⓘ NO CONFIG SERVICE, or one that could not open: refuse, opening nothing. Neither predicate can be
            //   established without a baseline, and the file's standing rule is that a null service FAILS CLOSED.
            //   ⛔ It is deliberately NOT reported as a `ProvBlock`: those two are §4's cells and carry §4's remedies,
            //   neither of which applies when there is no draft at all — the panel already says `CFG UNAVAILABLE`.
            case CfgRow::provision:
                if (!_cfg || !_cfg->is_open())      break;
                if (_cfg->conflict())               { _st.prov_block = ProvBlock::conflict; break; }
                if (_cfg->config_unsaved())         { _st.prov_block = ProvBlock::unsaved;  break; }
                enter_provision(Provision::menu);
                break;
            case CfgRow::reload:
                // The conflict's OTHER way out ([[B192]], owner-ruled: the three-way merge). Reported form: fields the
                // operator did not edit adopt the current persisted values; fields they did edit stay in the draft.
                if (_cfg) _st.cfg_refresh_failed = (_cfg->reload() == mrfw::CfgRefresh::nv_failed);
                break;
            case CfgRow::save:
                // ★★ A FACT IS ESTABLISHED BY THE ACT: the outcome is whatever `save()` RETURNED, recorded verbatim.
                //    ⛔ Nothing here decides that a save happened, and nothing clears a marker — `config_unsaved()`
                //    goes false because the SERVICE moved its baseline, and only on the path where it wrote.
                if (_cfg) { _st.cfg_save = _cfg->save(); _st.cfg_have_save = true; }
                break;
            case CfgRow::discard:
                // §3.6.1's explicit full reset. ⓘ NO confirmation step: §3.6.2 makes DISCARD *"a separate deliberate
                //   action"*, which it already is — reaching the row costs a walk and the act costs a `double` — and
                //   inventing a confirmation screen here would be inventing UI the spec does not describe.
                if (_cfg) _st.cfg_refresh_failed = (_cfg->discard() == mrfw::CfgRefresh::nv_failed);
                break;
            case CfgRow::back:
                // ★ §3.6.2: *"`BACK` is safe and PRESERVES an unsaved draft"*. `on_back()` is the service's named seam
                //   for it — a draft-preserving no-op by construction, called so the property is assertable rather
                //   than merely intended (mutation C28 turns it into a `discard()`).
                if (_cfg) _cfg->on_back();
                _st.screen = Screen::status; _st.cursor = 0;
                // ★ §UI-15 slice 4: the sub-view state is retired through the ONE primitive that owns the invariant
                //   (it closes the editor, `Provision` and the row pick together), rather than by re-spelling half of
                //   it here — which is how the `Provision` half would have been forgotten.
                settings_follow_screen();
                break;
            case CfgRow::ble_mode: case CfgRow::e2e_dm: case CfgRow::intro_attach:
            case CfgRow::mobile_autoregister:   // handled above by `cfg_row_field`; listed so -Wswitch stays useful
            case CfgRow::count: break;
        }
        _st.dirty = true;
    }
    // The note is TRANSIENT — it describes the last act, so the next navigation press retires it (the `team_pick_gone`
    // idiom). ⛔ It is cleared as a SET, in one place: three flags cleared at three sites would be the drift that lets
    // a `SAVED` from two presses ago sit under a fresh `BAD VALUE`.
    void clear_settings_note() {
        _st.cfg_have_save = false; _st.cfg_refresh_failed = false; _st.prov_block = ProvBlock::none;
    }
    // ================================================================ §UI-15 slice 4 — the PROVISIONING transitions
    // ★★★ THE TWO PRIMITIVES THAT MAY MOVE THE PROVISIONING STATE, AND NOTHING ELSE MAY (see `Provision`'s block):
    //     `Settings::provisioning` ⟺ `Provision != closed` is an invariant over TWO fields, and two fields assigned
    //     at N call sites is the drift that puts the panel in a state the model says it is not in.
    // ★ EVERY ARM CHANGE GOES THROUGH `enter_provision`, so §3.6.3's *"a confirmation opens with BACK selected"* is
    //   STRUCTURAL rather than remembered: BOTH confirm arms (create and join) land through here, so neither can
    //   through here and cannot forget to re-anchor the cursor. ⓘ Resetting it on the NON-confirm arms too is
    //   deliberate — the field then has exactly one meaning ("what a confirmation would open on"), and no arm can
    //   inherit a CONFIRM selected in a previous, abandoned confirmation. ⇒ the default is asserted on the one entry
    //   this slice CAN drive (`menu`), which is the same assignment every later entry will run.
    void enter_provision(Provision p) {
        _st.settings = Settings::provisioning; _st.provisioning = p;
        _st.prov_confirm = ProvConfirm::back;
        _st.cursor = 0;                        // each arm's list starts at its own first row
        // ★ §UI-15 slice 5: the ANSWER is retired by EVERY entry, so the only thing `create_result` can ever render is
        //   the verdict `run_create_team` wrote one statement after the transaction returned. ⛔ A result that
        //   survived into a later screen would be the "success that isn't" this project has already registered once.
        _st.prov_answer = UiProvAnswer{};
        _st.dirty = true;
    }
    // ⓘ Back to `browsing`, ⛔ never to `closed`: leaving PROVISION returns to the SETTINGS MENU, not off the screen.
    //   The highlight lands back on the PROVISION row by itself — `sync_settings` re-anchors from `_cfg_sel_row`,
    //   which this sub-view was forbidden to touch, and it runs before any frame can freeze.
    // ⓘ ONE spelling of "retire the sub-state" (U1), shared with the leave-the-screen path: the two transitions differ
    //   only in where they LAND (`browsing` here, `closed` there), and the fields they retire are the same two.
    void close_provisioning() {
        _st.settings = Settings::browsing;
        provision_reset_on_leave(_st.provisioning, _st.prov_confirm);
        _st.dirty = true;
    }
    // ★★★ THE SUB-VIEW'S GESTURES. `short` CYCLES within the arm's list and ⛔ never walks out of the screen — the
    //     `InboxModal` / compose rule (a sub-view is left by its own BACK, by the long gesture or by the blank), not
    //     `advance_or_next`'s walk-off, which belongs to the ordinary screen cycle.
    // ★★ §UI-15 slice 6 LANDS THE JOIN HALF, so the four `join_*` arms below have left the leave-only fail-safe and
    //    have their own flows. ⛔ WHAT IS STILL NOT HERE, by scope: §3.6.4's nearby-team scan (§UI-16).
    void provision_gesture(Gesture g, const UiSnapshot& s) {
        if (g != Gesture::short_press && g != Gesture::double_press) return;
        switch (_st.provisioning) {
            case Provision::menu:           provision_menu_gesture(g, s);    return;
            case Provision::create_confirm: provision_confirm_gesture(g);    return;
            // ★★ THE RESULT IS TERMINAL AND EITHER PRESS LEAVES IT (the panel says `press = back`, the same contract
            //    the send result and the `MESSAGE GONE` modal already carry). ⛔ Nothing is re-run and nothing is
            //    confirmed here: the act is over, and the only thing this arm owns is the way out.
            case Provision::create_result:  enter_provision(Provision::menu); return;
            case Provision::join_select:    join_select_gesture(g);          return;
            case Provision::join_confirm:   join_confirm_gesture(g, s);      return;
            // ★★★★ THE WAITING SCREEN, AND EITHER PRESS **ONLY LEAVES IT** — plan §2.3 rule 4, verbatim in
            //      substance: *"BACK during `JOINING` only LEAVES THE SCREEN; ⛔ it does not cancel or roll back an
            //      already-persisted operation"*. ⇒ the session is untouched here, deliberately and visibly: the
            //      join was durably written and DAD is running, and there is no verb on this device that could
            //      un-write it. ⛔ Nothing is cancelled, nothing is retried, nothing is said.
            case Provision::join_waiting:   enter_provision(Provision::menu); return;
            // The join RESULT is terminal in exactly the same way the create one is.
            case Provision::join_result:    enter_provision(Provision::menu); return;
            // ⛔ UNREACHABLE BY THE INVARIANT (`Settings::provisioning` implies a non-`closed` arm) and handled rather
            //    than defaulted: if it is ever reached the two fields have drifted, and the safe answer is to put them
            //    back in step instead of interpreting a press against a state that does not exist.
            case Provision::closed: close_provisioning(); return;
        }
    }
    void provision_menu_gesture(Gesture g, const UiSnapshot& s) {
        const ProvRowList l = provision_row_list(s);
        if (g == Gesture::short_press) {
            if (l.n) _st.cursor = uint8_t((_st.cursor + 1) % l.n);   // CYCLES — a sub-view is never walked out of
            _st.dirty = true;
            return;
        }
        ProvRow r{};
        if (!l.at(_st.cursor, r)) return;                            // fails closed — see ProvRowList::at
        switch (r) {
            // ★★ §UI-15 slice 5 — CREATE OPENS ITS CONFIRMATION, and it lands WITH the adapter behind it ([[B222]]'s
            //    rule honoured rather than waived): `enter_provision` is what re-anchors the cursor on BACK, so
            //    §3.6.3's *"reaching CREATE requires `short` then `double`"* is structural.
            case ProvRow::create_team: enter_provision(Provision::create_confirm); return;
            // ★★★★ §UI-15 slice 6 — THE JOIN FLOW's ENTRY, LANDING WITH THE FLOW BEHIND IT ([[B222]]'s rule
            //      honoured rather than waived: slice 5 left this arm a `return` precisely because the list it
            //      selects from did not exist yet). ⛔ THE LIST IS READ **HERE AND ONCE** — on the transition, not
            //      per tick and not per page: `profiles()` reaches flash.
            // ⓘ A refusing store is NOT a refusing transition: the screen OPENS and SAYS what is wrong
            //   (`join_store_head`/`_detail`), because "there are no profiles" and "the store is corrupt" are facts
            //   the operator came here to learn. ⛔ Silently refusing to open would be indistinguishable from a dead
            //   button — the complaint `run_create_team`'s null-seam arm is built against.
            case ProvRow::join_static: load_join_profiles(); enter_provision(Provision::join_select); return;
            case ProvRow::back:        close_provisioning(); return;
            case ProvRow::count:       return;   // the enum's BOUND, listed so -Wswitch stays useful
        }
    }
    // ★★★★ THE CONFIRMATION, AND ITS TWO GESTURES ARE THE `InboxAction` PAIR ONE SCREEN OVER (§3.5, U3): `short`
    //      TOGGLES between the two actions and `double` PERFORMS the selected one. ⛔ `short` may not walk out of the
    //      screen and ⛔ `double` on BACK may not fall through into the act — the two are separate branches for the
    //      same reason the delete modal keeps them separate: one press must never be able to mean the destructive one.
    // ⛔ NO SNAPSHOT IS READ HERE, deliberately: the act's inputs are the DEVICE's (the record, the live PHY, the
    //    build floor) and they are gathered by the adapter at the instant it runs — ⛔ never frozen a frame earlier
    //    into a UI snapshot, which is how a stale PHY reading would silently pass the precondition.
    // ★ §UI-15 slice 6 hoisted the TOGGLE into one spelling (U1) because the JOIN confirmation is the same pair with
    //   a different landing — two copies of "flip the cursor" is how one of them eventually stops marking dirty.
    //   ⛔ The two LANDINGS stay separate functions: they perform different acts and return to different screens,
    //   and one function branching on the arm is how a `double` on BACK eventually reaches the wrong one.
    void prov_confirm_toggle() {
        _st.prov_confirm = (_st.prov_confirm == ProvConfirm::back) ? ProvConfirm::confirm : ProvConfirm::back;
        _st.dirty = true;
    }
    void provision_confirm_gesture(Gesture g) {
        if (g == Gesture::short_press) { prov_confirm_toggle(); return; }
        if (_st.prov_confirm == ProvConfirm::back) { enter_provision(Provision::menu); return; }
        run_create_team();
    }
    // ★★★★ THE ACT ITSELF, AND THE ORDER OF THESE FOUR STATEMENTS IS §8 PIN 2: the transaction RUNS, RETURNS, and only
    //      then does the screen move and the verdict land. ⛔ There is no path that enters `create_result` first and
    //      fills the answer afterwards, and none that shows a success text before `perform()` came back — which is why
    //      the "no screen claims success before the save returns" rule is structural here rather than remembered.
    // ⓘ A NULL SEAM REFUSES OUT LOUD (C2) rather than doing nothing: a `double` on CREATE that changed no pixel is
    //   indistinguishable from a dead button. ⛔ It is NOT a `ProvErr` — the transaction never ran — so the reason is
    //   this unit's own token, in the same shape `settings_note` uses for a service that could not open.
    void run_create_team() {
        UiProvAnswer a{};
        if (_prov) {
            UiProvIntent in{};
            in.op = UiProvOp::create_team;
            a = _prov->perform(in);
        } else {
            a.outcome = UiProvOutcome::refused;
            a.reason  = "no service";
        }
        enter_provision(Provision::create_result);
        _st.prov_answer = a;
    }

    // ============================================================== §UI-15 slice 6 — the STATIC-JOIN flow (§3.6.3)
    // ★★ THE ONE READ OF `/mrjoin`, and it happens on the TRANSITION (see `provision_menu_gesture`). ⛔ A null seam
    //    is a REAL STATE and fails closed: `served` stays false, so the screen offers no slot and says so.
    void load_join_profiles() {
        _st.join_list = _prov ? _prov->profiles() : UiJoinList{};
        _st.join_sel  = 0;
    }
    // `short` CYCLES the slot list and ⛔ never walks out of the screen (the sub-view rule, three menus deep);
    // `double` opens the CONFIRMATION for the selected slot, or leaves.
    void join_select_gesture(Gesture g) {
        const JoinSelList l = join_sel_rows(_st.join_list);
        if (g == Gesture::short_press) {
            // ⓘ SPELLED OUT rather than shared with `provision_menu_gesture`'s identical line: the two walk
            //   different lists, and one function branching on the arm is how a press eventually acts on the other.
            if (l.n) _st.cursor = uint8_t((_st.cursor + 1) % l.n);   // CYCLES — the sub-view rule, three menus deep
            _st.dirty = true;
            return;
        }
        JoinSelRow r{};
        if (!l.at(_st.cursor, r)) return;                            // fails closed — see JoinSelList::at
        if (r.back) { enter_provision(Provision::menu); return; }
        // ★ THE PICK IS THE SLOT NUMBER, ⛔ never the row index (§B66): the rows are built from the `present` flags,
        //   so index 1 is slot 2 on one record and slot 3 on another. `enter_provision` re-anchors the cursor and the
        //   BACK default, so the confirmation cannot open on the destructive choice.
        _st.join_sel = r.slot1;
        enter_provision(Provision::join_confirm);
    }
    // ★★★★ THE JOIN CONFIRMATION — the `InboxAction`/create pair a third time (U3): `short` TOGGLES, `double`
    //      PERFORMS the selected one, and ⛔ `double` on BACK may NOT fall through into the act. Its landing differs
    //      from the create one's by design: BACK returns to the SLOT LIST the operator came from, not to the child
    //      menu, because that is the screen he was choosing on.
    // ⓘ THE SNAPSHOT IS READ HERE, and this is the ONE deliberate difference from `provision_confirm_gesture`'s
    //   "no snapshot" rule: the act starts a SESSION whose only clock fact is when it began (plan §2.3 rule 5's
    //   60 s word change). ⛔ It is not an INPUT to the transaction — the profile values are — and nothing about the
    //   device is read from the frame.
    void join_confirm_gesture(Gesture g, const UiSnapshot& s) {
        if (g == Gesture::short_press) { prov_confirm_toggle(); return; }
        if (_st.prov_confirm == ProvConfirm::back) { enter_provision(Provision::join_select); return; }
        run_join_static(s);
    }
    // ★★★★ THE ACT, AND THE ORDER OF ITS STATEMENTS IS §8 PIN 2 EXACTLY AS `run_create_team`'s IS: the transaction
    //      RUNS, RETURNS, and only then does the screen move and the verdict land. ⛔ There is no path that shows a
    //      `JOINING` before `perform()` came back, and ⛔⛔ NO PATH AT ALL that shows an ADOPTED — that outcome is
    //      `on_join_push`'s alone, behind the four-term rule.
    // ⓘ A NULL SEAM, OR A PICK THAT NAMES NO PRESENT SLOT, REFUSES OUT LOUD (C2) rather than doing nothing — the
    //   dead-button complaint, and here it also covers a list that moved under a stale `join_sel`.
    void run_join_static(const UiSnapshot& s) {
        UiProvAnswer a{};
        const uint8_t sel = _st.join_sel;
        // ⓘ ⚠ THE SLOT CLAUSES ARE A FLOOR AND ARE **UNREACHABLE BY CONSTRUCTION**, and that is MARKED rather
        //   than claimed as tested (the slice-5 `no_change`-arm precedent): `join_confirm` is entered ONLY by
        //   `join_select_gesture`, which has just read the slot off a row built from the `present` flags. ⇒ a
        //   mutation dropping them stays GREEN, so ⛔ no battery entry counts them as covered. They are written
        //   because C2 requires the act to fail closed, not because a path reaches them today.
        const bool ok = _prov && sel >= 1 && sel <= mrnv::kJoinProfiles && _st.join_list.rec.prof[sel - 1].present;
        uint8_t requested_layer = 0;
        if (ok) {
            UiProvIntent in{};
            in.op   = UiProvOp::join_static;
            in.join = _st.join_list.rec.prof[sel - 1];   // ★ WHAT WAS SHOWN IS WHAT IS JOINED (U2 — the whole record)
            requested_layer = in.join.layer;             // the FULL byte, cached for the correlation's term 2/3
            a = _prov->perform(in);
        } else {
            a.outcome = UiProvOutcome::join_refused;
            a.reason  = "no service";
        }
        // ★★★ THE TWO LANDINGS, AND THE `joining` ONE IS THE POINT OF THE WHOLE SLICE: a started transaction has
        //     written once and begun DAD, so the screen WAITS — it does not report a result it does not have.
        if (a.outcome == UiProvOutcome::joining) {
            enter_provision(Provision::join_waiting);
            // ⛔ THE SESSION IS ARMED ONLY HERE, AFTER `perform()` RETURNED `joining`. Arming it before (or on a
            //    refusal) would leave a correlation window open for an operation that never started — the mirror of
            //    the "success that isn't", one layer down.
            _join.active          = true;
            _join.requested_layer = requested_layer;
            _join.started_ms      = s.now_ms;
            _st.join_still        = false;               // a fresh session starts at `JOINING`, never mid-word
        } else {
            enter_provision(Provision::join_result);
        }
        _st.prov_answer = a;
    }
    // ★★★★ §B64 — THE TEAMMATE THE CURSOR IS ON, HELD BY IDENTITY, AND RE-FOUND IN EVERY SNAPSHOT.
    // ★ THE IDENTITY IS THE ROW'S OWN `id`, DERIVED AND NOT INVENTED (U1): it is the team-plane id the snapshot already
    //   carries, the id `compose_peer` already stores, and the id `ui_compose_send_line` already puts on the wire
    //   (`send <id> "<text>" -t -a`). ⇒ the thing tracked and the thing addressed are the SAME value, so they cannot
    //   drift. No new snapshot field was added.
    // ⛔ NOT the row's `label`. That is a DISPLAY string (a resolved name, else `0x<hash>`, else the bare id) and a
    //   display-shaped field must never make an addressing decision — the same rule that killed B48.
    // ⚠ C3, PLANE DISCIPLINE: `_team_sel_id` is a TEAM-plane local id. It is only ever COMPARED against a snapshot
    //   row's `id` and copied into `compose_peer`; it indexes nothing and never reaches a static `node_id`-keyed array.
    //   There is no write path here at all, and on a `!MR_FEAT_TEAM` build `team_build` is false, so `Screen::team` is
    //   unreachable and every line below is inert.
    // ⓘ NO ARITHMETIC VALUE IS RESERVED (§B74's discipline): `_team_sel_valid` is a separate flag, so id 0 — which
    //   `Node::team_local_id()` documents as "not team-DAD'd" — needs no special case and can never be confused with
    //   "nothing is selected".
    void sync_team_cursor(const UiSnapshot& s) {
        // The selection belongs to the TEAM list alone. ⚠ THE COMPOSE GUARD IS LOAD-BEARING: while the sub-view is open
        // `_st.cursor` is the MODAL's list index, not a team row, so touching it here would walk the message selection
        // under the user's fingers — turning "I'm OK" into "Are you OK?", or into `back`. A control pins it directly.
        if (_st.screen != Screen::team || _st.compose != Compose::none) return;
        if (!_team_sel_valid) return;                    // nothing picked yet, or a pick already lost (and announced)
        for (uint8_t i = 0; i < s.team_shown; ++i) {
            if (s.team[i].id != _team_sel_id) continue;
            if (_st.cursor != i) { _st.cursor = i; _st.dirty = true; }   // the teammate MOVED -> the highlight follows
            return;
        }
        // GONE. The cursor may not silently come to rest on somebody else, so the selection is DROPPED and the loss is
        // announced. ★ EDGE-TRIGGERED (spec §5): clearing `_team_sel_valid` is what stops this from re-firing, and
        // therefore from marking the frame dirty, on every subsequent tick.
        _team_sel_valid = false; _st.team_pick_gone = true; _st.dirty = true;
    }
    // The WRITE side: whatever row the cursor has just come to rest on IS the new selection. Called after every cursor
    // or screen move, so "the pick" is always something the user's last press actually pointed at.
    void note_team_cursor(const UiSnapshot& s) {
        if (_st.screen == Screen::team && _st.cursor < s.team_shown) {
            _team_sel_id = s.team[_st.cursor].id; _team_sel_valid = true; _st.team_pick_gone = false;
            return;
        }
        _team_sel_valid = false;                         // an empty roster, or a screen that has no teammates at all
        if (_st.screen != Screen::team) _st.team_pick_gone = false;   // leaving the screen retires its message
    }
    // ★★★★ §UI-7D slice B — THE INBOX CURSOR, HELD BY THE RECORD'S IDENTITY, AND RE-FOUND IN EVERY SNAPSHOT.
    // ★ THE IDENTITY IS THE PAIR `(kind, seq)`, DERIVED AND NOT INVENTED (U1): both halves are fields the snapshot row
    //   already carries and both are what `Inbox::erase(InboxKind, seq)` takes, so the thing tracked, the thing
    //   displayed and the thing deleted are the SAME two values and cannot drift.
    // ⛔ NOT the row index. `Inbox::pull()` hands records oldest-first and the per-kind budget keeps the NEWEST, so a
    //    single arriving message shifts every retained row by one — an index-based selection then names its neighbour,
    //    and the modal's Delete deletes that neighbour. ⛔ NOT `seq` alone either: the DM and channel sequence spaces are
    //    independent, so seq 4 names two different messages. [[B133]] was this exact pair at another site.
    // ⛔ NOT `origin`, the message counter or the preview text: `text` is a display field, and a display-shaped field
    //    must never make a storage decision (the rule that killed B48).
    // ⚠ It runs while the DETAIL modal is open too, deliberately: the list underneath must stay honest, and the modal
    //   holds its OWN copy of the identity (`UiState::detail_*`), so a selection lost here cannot retarget an open
    //   modal's delete. What it does mean is that returning from a modal whose record vanished lands on the refusal —
    //   which is true.
    void sync_inbox_cursor(const UiSnapshot& s) {
        if (_st.screen != Screen::inbox || _st.compose != Compose::none) return;
        if (!_inbox_sel_valid) return;                    // nothing picked yet, or a pick already lost (and announced)
        for (uint8_t i = 0; i < s.inbox_shown; ++i) {
            if (s.inbox[i].kind != _inbox_sel_kind || s.inbox[i].seq != _inbox_sel_seq) continue;   // ★ BOTH halves
            if (_st.cursor != i) { _st.cursor = i; _st.dirty = true; }     // the record MOVED -> the highlight follows
            return;
        }
        // GONE. The cursor may not silently come to rest on another message, so the selection is DROPPED and the loss
        // is announced. ★ EDGE-TRIGGERED (spec §5): clearing `_inbox_sel_valid` is what stops this re-marking the frame
        // dirty on every subsequent tick.
        _inbox_sel_valid = false; _st.inbox_pick_gone = true; _st.dirty = true;
    }
    // The WRITE side: whatever row the cursor has come to rest on IS the new selection — so "the pick" is always
    // something the user's last press actually pointed at. ⓘ A row with `seq == 0` has no identity and is NOT selected
    // (see InboxRow): the refusal path above then applies, rather than a selection that could only resolve wrongly.
    void note_inbox_cursor(const UiSnapshot& s) {
        if (_st.screen == Screen::inbox && _st.cursor < s.inbox_shown && s.inbox[_st.cursor].seq != 0) {
            _inbox_sel_kind = s.inbox[_st.cursor].kind; _inbox_sel_seq = s.inbox[_st.cursor].seq;
            _inbox_sel_valid = true; _st.inbox_pick_gone = false;
            return;
        }
        _inbox_sel_valid = false;                        // an empty list, an unidentifiable row, or another screen
        if (_st.screen != Screen::inbox) _st.inbox_pick_gone = false;   // leaving the screen retires its message
    }
    // The row a successful delete should leave the highlight on: the one AFTER the selection, else the one BEFORE it.
    // Captured at activation because that is the last snapshot in which the doomed record is still present.
    // ⓘ The arithmetic is done in `uint16_t` so `cursor + 1` cannot wrap into a valid-looking index 0. The cursor is
    //   bounded by `list_len` today, so that is a shape rule rather than a reachable bug — and it is the shape that
    //   turns "no neighbour" into "row 0", which is a wrong row, which is the whole class this slice guards against.
    void note_inbox_neighbour(const UiSnapshot& s) {
        _inbox_nb_valid = false;
        const uint16_t cur = _st.cursor, n = s.inbox_shown;
        uint16_t nb = 0; bool have = false;
        if (cur + 1u < n)          { nb = uint16_t(cur + 1u); have = true; }
        else if (cur > 0 && cur <= n) { nb = uint16_t(cur - 1u); have = true; }
        if (!have || nb >= n || s.inbox[nb].seq == 0) return;
        _inbox_nb_kind = s.inbox[nb].kind; _inbox_nb_seq = s.inbox[nb].seq; _inbox_nb_valid = true;
    }
    // ★★★ THE MODAL'S GESTURES (spec §3.5): short TOGGLES the action, double ACTIVATES it, and `back` is selected on
    //     entry — so deletion always costs the deliberate sequence short -> double.
    void detail_gesture(Gesture g) {
        // The TERMINAL `MESSAGE GONE` state: there is no action to toggle and nothing left to activate, so either press
        // means "I have read it" and returns to the (rebuilt) INBOX. Same derivation as the compose result phase.
        if (_st.detail == InboxModal::gone) {
            if (g == Gesture::short_press || g == Gesture::double_press) close_detail();
            return;
        }
        if (g == Gesture::short_press) {
            _st.detail_action = (_st.detail_action == InboxAction::back) ? InboxAction::del : InboxAction::back;
            _st.dirty = true; return;
        }
        if (g != Gesture::double_press) return;
        // ⚠ A request already in flight is not doubled. The answer lands in the same service pass, so this is defensive
        //   rather than reachable — but a second `erase` of the same pair would come back `not_found` and read as though
        //   the first one had failed to find it.
        if (_inbox_req.what != InboxWhat::none) return;
        if (_st.detail_action == InboxAction::back) { close_detail(); return; }   // ⇒ storage is not touched at all
        // ★★ THE ERASE TARGET IS THE MODAL'S OWN PAIR — the record we actually read and are actually showing. ⛔ Never
        //    the snapshot row under `_st.cursor`: rows move, and this is the third of the identity's three assertion
        //    sites (snapshot, activation, erase).
        _inbox_req = { InboxWhat::erase, _st.detail_kind, _st.detail_seq };
        _inbox_taken = false;
        _st.dirty = true;
    }
    // ★ ONE exit for the modal (U1/U2). Five call sites reach it — `back`, the terminal state's acknowledgement,
    //   `on_tick`'s timeout, a successful delete, and §UI-7D's `long_arm` — and every field must be cleared together or
    //   a re-opened modal would render one record's page under another's header.
    void close_detail() {
        _st.detail = InboxModal::closed;
        _st.detail_action = InboxAction::back;
        _st.detail_del_failed = false;
        _st.detail_page = 0; _st.detail_pages = 1;
        _st.detail_seq = 0; _st.detail_origin = 0; _st.detail_channel = 0;
        _detail_len = 0; _detail_body[0] = '\0';
        for (uint8_t r = 0; r < kDetailBodyRows; ++r) _st.detail_line[r][0] = '\0';
        _inbox_nb_valid = false;
        _st.dirty = true;
    }
    // The current page, wrapped into the two body rows WITHOUT DROPPING BYTES: `kDetailCols` columns each (19 since
    // §CHROME-4 — the rail's 116-px body), taken in order. The
    // bytes are already sanitized (`on_inbox_opened`), so this is a slice and nothing else — and it reads `_detail_len`
    // rather than looking for a terminator, because the length is the truth about the record.
    void refresh_detail_page() {
        const uint16_t off = uint16_t(uint16_t(_st.detail_page) * kDetailPageChars);
        for (uint8_t row = 0; row < kDetailBodyRows; ++row) {
            char* dst = _st.detail_line[row];
            uint8_t n = 0;
            for (; n < kDetailCols; ++n) {
                const uint16_t i = uint16_t(off + uint16_t(row) * kDetailCols + n);
                if (i >= _detail_len) break;
                dst[n] = _detail_body[i];
            }
            dst[n] = '\0';
        }
    }
    bool inbox_answer_is(InboxWhat w, InboxKind k, uint32_t seq) const {
        return _inbox_taken && _inbox_req.what == w && _inbox_req.kind == k && _inbox_req.seq == seq;
    }
    void clear_inbox_request() { _inbox_req = InboxReq{}; _inbox_taken = false; }

    void compose_gesture(Gesture g) {
        // ★★ UI-7: THE RESULT PHASE. Once a send has been issued the modal shows its OUTCOME instead of the list
        //    (spec §3.2.1/§3.4.1), so there is nothing to walk and nothing to activate — the only thing either gesture
        //    can mean is "I have read it".
        // ★ `double` closes because the spec says so verbatim ("the sub-view closes to its parent on an explicit
        //   `double`, or after a bounded display window" — the window is `on_tick`'s kBlankMs auto-exit).
        // ★ `short` closes too, and that is DERIVED from the shipped gesture contract rather than invented: §3.2's
        //   `short` is "advance within the current list; AT THE END, move to the next screen". The result phase has no
        //   list, so every position is the end. ⛔ The alternative — ignore it — would let a user tapping `short`
        //   hold a modal open indefinitely, since every gesture refreshes `_last_input_ms` and so postpones the very
        //   auto-exit that is supposed to bound it. Neither choice can send: this branch queues nothing.
        if (_st.compose_result) {
            if (g == Gesture::short_press || g == Gesture::double_press) close_compose();
            return;
        }
        const uint8_t n = (_st.compose == Compose::dm) ? kDmTextCount : kChannelTextCount;
        if (g == Gesture::short_press) { _st.cursor = uint8_t((_st.cursor + 1) % n); _st.dirty = true; return; }
        if (g != Gesture::double_press) return;
        if (_st.cursor + 1 == n) { close_compose(); return; }                                                // `back`
        queue(_st.compose == Compose::dm ? SendKind::dm : SendKind::channel_canned, _st.compose_peer, _st.cursor);
        // ★★ UI-7: THE MODAL STAYS OPEN. UI-2 closed it here, which left every `DmState` the spec defines with NO
        //    RENDERER — `DELIVERED to <label>` (the one thing `-a` buys that a channel post can never offer),
        //    `NO KEY`, `NO CONFIRM` — all unreachable on the panel. The cursor is still reset, so a re-opened modal
        //    starts on the first message (H7-02/H7-04), and `compose_peer` is untouched so the result can name who it
        //    went to. ⓘ It cannot outlive attention: `on_tick`'s kBlankMs auto-exit applies to BOTH phases.
        _st.compose_result = true; _st.cursor = 0; _st.dirty = true;
    }
    // ★ ONE exit for the sub-view (U1/U2). Four call sites reach it — `back`, the result phase's acknowledgement,
    //   `on_tick`'s auto-exit and §B101's `long_fire` — and the phase flag MUST be cleared with the modal or a
    //   re-opened compose would render an outcome list against a stale result. It sends nothing, by construction.
    void close_compose() { _st.compose = Compose::none; _st.compose_result = false; _st.cursor = 0; _st.dirty = true; }
    uint8_t list_len(const UiSnapshot& s) const {
        if (_st.screen == Screen::team)  return s.team_shown;
        if (_st.screen == Screen::inbox) return s.inbox_shown;
        // ★ §UI-14: SETTINGS is list-aware exactly like TEAM and INBOX (§3.2: *"`short` walks the list and leaves only
        //   at the end"*), and the length is the ONE row-list construction — never a hand-written count, which is
        //   §B66's defect one screen over: two conditional rows mean the number is not a constant.
        if (_st.screen == Screen::settings) return settings_row_list(s).n;
        return 1;
    }
    static Screen next_screen(Screen cur, const UiSnapshot& s) {
        for (uint8_t i = 1; i <= uint8_t(Screen::count); ++i) {
            const Screen cand = Screen((uint8_t(cur) + i) % uint8_t(Screen::count));
            // ★ §UI-14: SETTINGS is in BOTH cycles (spec §3.1). Only TEAM and SEND are team-gated — the four covered
            //   fields are durable on every build, including `gateway_heltec` (OLED=1, TEAM=0).
            if (s.team_build || cand == Screen::status || cand == Screen::inbox || cand == Screen::settings) return cand;
        }
        return Screen::status;
    }
    // Defined inline below the class (UI-3): on_gesture/on_tick call them, so they are declared with the members.
    void emergency_gesture(Gesture g, const UiSnapshot& s);
    void tick_emergency(const UiSnapshot& s);
    bool hold_active(uint32_t now_ms) const;
};

// ---------------------------------------------------------------------------------------------------- UI-3 bodies

inline void UiModel::emergency_gesture(Gesture g, const UiSnapshot& s) {
    // ★★★★ §UI-7D slice B — THE DETAIL MODAL CLOSES AT `long_arm`, i.e. BEFORE the alarm is armed, and ⛔ NOT at
    //     `long_fire` the way the compose sub-view does. Spec §3.5 is explicit: "long press closes the detail modal
    //     before arming emergency; the hidden Delete action cannot survive underneath an emergency overlay."
    // ★ WHY THE COMPOSE RULE IS THE WRONG ONE TO COPY: §B101 deliberately leaves compose open through `long_arm` because
    //   arming is cancellable and destroying the user's list position for a press they may still cancel is a second,
    //   smaller wrong. Here the surviving selection can be DELETE, and ledger §1.4 has the overlay ABSORB a double
    //   outright — so a modal left open underneath is unreachable, invisible, and still armed the moment the overlay
    //   goes away. The asymmetry is the point, not an inconsistency.
    // ⓘ `long_fire` needs no clause of its own: `InputFsm::update` emits it only with `_armed` set
    //   (firmware_ui_input.h:39), and `long_arm` is the only thing that sets `_armed` — so the modal is provably already
    //   closed by then. A `long_cancel` cannot bring it back either: closing is destructive.
    if (_st.detail != InboxModal::closed) close_detail();
    // ★★★★ §UI-14 — THE SETTINGS EDITOR CLOSES AT `long_arm` TOO, i.e. BEFORE the alarm is armed, and ⛔ NOT at
    //     `long_fire`. Spec §3.6.2 is explicit: *"the long gesture ALWAYS leaves the editor and arms emergency"* — and
    //     copying compose's fire-time close was exactly §UI-7D's correction one modal over. ⇒ the same placement, the
    //     same unconditional form, and therefore the same property: a `long_cancel` cannot bring the editor back,
    //     because closing is destructive and `long_arm` has already happened by then (`InputFsm::update` emits
    //     `long_fire`/`long_cancel` only with `_armed` set, and only `long_arm` sets it).
    // ★ WHAT IT DOES NOT DO, deliberately: it does not REVERT the value the operator had cycled. That value is already
    //   in the RAM draft, unsaved, and §3.6.5 rules that *"a draft survives; an UNCONFIRMED DESTRUCTIVE ACTION does
    //   not"* — a draft edit is neither confirmed nor destructive, and discarding it here would be the silent discard
    //   §3.6.1 forbids, triggered by a press the operator may still cancel.
    // ⓘ The cursor is left where it was, which is a VALUE row by construction: only a value row can be in `editing`
    //   at all, so this can never come back to rest on `DISCARD`.
    // ★★★★ §UI-15 slice 4 — AND THE PROVISIONING SUB-VIEW CLOSES AT `long_arm` TOO, one level deeper and for the
    //     STRONGER of the two reasons: §3.6.5 rule 1 is that *"emergency PRE-EMPTS any provisioning screen — an
    //     unconfirmed destructive action does not survive"*. A sub-view left standing under an overlay that owns the
    //     body is invisible AND still holds the press, and the row one press from BACK is CREATE TEAM (which
    //     replaces a membership) or a JOIN CONFIRMATION (which re-provisions the node).
    // ★★ §UI-15 slice 6, AND IT IS THE ONE THING THE PRE-EMPTION DELIBERATELY DOES **NOT** DO: closing the sub-view
    //    does ⛔ NOT end a join SESSION. §3.6.5's words are *"an UNCONFIRMED destructive action does not survive"* —
    //    a join that is already written and already DAD-ing is neither unconfirmed nor cancellable from a screen, and
    //    pretending otherwise would be plan §2.3 rule 4's forbidden rollback arriving through the alarm.
    // ⚠ IT IS PLACED **BEFORE** THE EDITOR'S LINE DELIBERATELY, and the reason is instrument hygiene rather than
    //   behaviour (the two states are mutually exclusive): the mutation battery's M36/M37 anchor on the editor line
    //   TOGETHER WITH the `long_arm` line below it, and an insertion between them would silently drop both to match
    //   count 0 — VACUOUS. This file has already lost two entries that way (see M27/M28's re-anchoring note).
    if (_st.settings == Settings::provisioning) close_provisioning();
    if (_st.settings == Settings::editing) { _st.settings = Settings::browsing; _st.dirty = true; }
    if (g == Gesture::long_arm)    { _emg = Emergency::arming; _arm_fire_at_ms = s.now_ms + kArmToFireMs; return; }
    if (g == Gesture::long_cancel) { _emg = Emergency::cancelled; _cancelled_until_ms = s.now_ms + kCancelledMs; return; }
    // long_fire — a NEW alarm: the three-transmission budget, the backoff and any armed retry all reset, so a sticky
    // NOT HEARD can always be re-fired by another long press. (§B74: clearing `_retry_armed` here is belt-and-braces —
    // `_emg` is `firing` from this line on, and only an `on_outcome` block can return it to `blocked`, which re-arms
    // the flag itself. It is written so the flag can never be read stale, not because a stale read is reachable.)
    // ★ §B69: the EVIDENCE resets with the budget. `_tries` and `_emg_evidence` describe the SAME alarm — "three
    //   attempts, and this is what came back" — so a new alarm inheriting the old one's evidence would let a previous
    //   call's locally-heard transmission justify a `NOT HEARD` this one never measured.
    _emg = Emergency::firing; _tries = 0; _backoff_ms = 0; _retry_armed = false; _emg_evidence = EmgEvidence::none;
    // ★★ §B101/F5: COMMITTING an alarm CLOSES the compose modal and resets its cursor. The overlay covers the body,
    //    so a canned message left selected underneath is invisible — and still armed: after the alarm is dismissed the
    //    next `double` sends whatever the cursor happened to be on. An avoidable later mis-send on a safety device.
    // ⓘ `long_arm` deliberately does NOT do this. Arming is cancellable, so destroying the user's list position for a
    //    press they may still cancel would be a second, smaller wrong.
    // ⓘ UI-7 routed it through `close_compose()` so the new RESULT phase is cleared with the modal (one exit, U1).
    close_compose();
    retain(s.now_ms);
    queue(SendKind::emergency, 0, 0);
}

inline void UiModel::tick_emergency(const UiSnapshot& s) {
    if (_emg == Emergency::cancelled && elapsed(s.now_ms, _cancelled_until_ms) < (1u << 31)) { _emg = Emergency::idle; _st.dirty = true; }
    if (_emg == Emergency::blocked && _retry_armed &&
        elapsed(s.now_ms, _retry_at_ms) < (1u << 31)) {                 // wrap-safe "now >= deadline"
        _retry_armed = false; _emg = Emergency::firing; queue(SendKind::emergency, 0, 0); _st.dirty = true;
    }
    if (_emg == Emergency::arming) {                                     // dirty ONLY when the visible digit changes
        const uint8_t d = arming_secs_left(s);
        if (d != _last_countdown) { _last_countdown = d; _st.dirty = true; }
    }
}

// ★ The hold is a DEADLINE, not a duration. Returning kEmgHoldMs and letting on_tick measure it from _last_input_ms
// (an earlier draft) meant a reply that set a fresh _emg_hold_until_ms never actually extended the window, and
// picked_up fell back to the ordinary 15 s blank. Read the field; compare wrap-safely. Spec §4.3.
// ⓘ `arming` is in the retained set per the plan's code, but nothing writes the deadline on long_arm — spec §4.3's
// table lists only long_fire and the retained OUTCOMES. It is inert either way: the long_arm gesture just refreshed
// _last_input_ms, and arming lasts kArmToFireMs, so the kBlankMs blank cannot fire during it. Left as the plan has
// it; do not
// "fix" it by writing the deadline on arm without a ruling, and do not rely on it.
// ★★ §B78 (owner-ruled 2026-08-04): `failed` joins the set. Every OTHER emergency outcome held the panel; the one
// that says the alarm did NOT go out fell back to the ordinary 15 s blank — the worst place to lose the message. Both
// producers now `retain()` from their own arrival time (on_send_refused, and channel_failed in on_outcome).
inline bool UiModel::hold_active(uint32_t now_ms) const {
    const bool retained = (_emg == Emergency::arming    || _emg == Emergency::firing  ||
                           _emg == Emergency::blocked   || _emg == Emergency::picked_up ||
                           _emg == Emergency::not_heard || _emg == Emergency::reply    ||
                           _emg == Emergency::failed);
    return retained && elapsed(_emg_hold_until_ms, now_ms) < (1u << 31);   // now < deadline, wrap-safe
}

// ================================================================================================ §B107 — the FRAME GATE
// ★★★ THE RENDER-POLICY LIFECYCLE, AND IT LIVES HERE FOR THE §UI-6 GLUE REASON. It was six lines of `mr_ui_tick` in
// `src/firmware_ui.cpp` — a TU neither the native suite nor the simulator compiles — and §B104 records the result:
// render policy, the MAC-idle gate and the paint throttle had NO behavioural probe at all, which is why §B107 was
// reachable only by human review. As a pure class it is driven by the native suite and turns red on a revert.
//
// ★ THE FRAME IS FROZEN WHEN IT OPENS. U8g2 page mode re-clips the WHOLE scene once per page, so a frame spans several
//   ticks and everything the renderer reads must be a COPY — live state changing mid-frame tears the image across page
//   boundaries (spec §5). This class owns WHEN; `firmware_ui.cpp` owns WHAT (it holds the frozen copies).
//
// ★★★ §B107 — THE DEFECT, AND IT IS THE ONE THIS CLASS EXISTS TO MAKE UNREPRESENTABLE. The shipped tick cleared
//   `dirty` when the LAST PAGE went out (`if (!s_frame_open) s_model.clear_dirty();`). A frame takes eight ticks, and
//   an outcome or a gesture landing DURING those eight ticks sets `dirty` — which the completing OLD frame then
//   cleared unconditionally. The new state was never painted: PICKED UP / REPLY / FAILED could be lost outright, and
//   on the emergency screen the arming countdown swallowed digits.
// ⇒ `dirty` is CONSUMED AT THE FREEZE, because the freeze is the instant the frame stops tracking the model. Anything
//   raised after it belongs to the NEXT frame. Final-page completion does PRESENTATION BOOKKEEPING ONLY.
//
// ⚠ THE BLANKED BRANCH CLEARED IT TOO, and it does not any more — but the honest measurement is that the second half
//   was INERT: both writers of `blanked = false` (on_gesture's emergency pre-empt and its waking press) also set
//   `dirty = true`, so no wake could observe the discarded invalidation. It is fixed because it is wrong by
//   construction, not because a harm was reproduced.
// ⓘ CORRECTED 2026-08-05 by §R1 (V1). This block used to end: "What IS real and is NOT this class's to fix: nothing
//   un-blanks on an incoming push, so a REPLY arriving at a dark panel waits for a button press. That is a spec
//   question." The owner has since ruled, and it is fixed — but NOT here, and the boundary is the point: the un-blank
//   belongs to the event that decides a post IS a reply (`UiModel::on_reply`), not to the class that merely observes
//   `blanked`. This class still clears nothing and wakes nothing; it reads the flag the model owns.
enum class FrameStep : uint8_t {
    mac_busy = 0,   // §5 rule 1: the MAC is mid-exchange — touch NOTHING, not even the power-save latch
    blank,          // the panel is dark: set_power_save(true) and abandon any open page loop
    idle,           // awake, nothing to paint this pass (not dirty, or inside the throttle)
    next_page,      // draw the FROZEN copies again and push one more page
    open            // freeze, begin_frame, draw, push page 0
};

// 2 Hz. An emergency BYPASSES it (but never the MAC-idle gate), and the model marks itself dirty only when the visible
// countdown digit changes, so the alarm screen does not repaint at tick rate either.
inline constexpr uint32_t kPaintThrottleMs = 500;

class FrameGate {
public:
    // ★ ONE call that DECIDES and commits the state its decision implies, so there is no "remember to tell the gate
    //   what you decided" obligation — the exact shape the §UI-6 GLUE block was created to kill. The single thing it
    //   cannot know is how many pages the panel has left, hence `on_page`.
    FrameStep step(UiModel& m, const UiSnapshot& s, bool mac_idle) {
        if (!mac_idle) return FrameStep::mac_busy;
        if (m.state().blanked) {
            // ⚠ The blank is tested BEFORE the open-frame continuation, deliberately: `set_power_save(true)` abandons
            //   the board's page loop, so holding `_open` across a blank would leave this half of the loop describing
            //   a frame the board has already dropped. Dropping both together keeps the two halves in step.
            _open = false;
            return FrameStep::blank;   // ★ §B107: nothing is CLEARED here — an invalidation raised while dark survives
        }
        if (_open) return FrameStep::next_page;
        if (!m.state().dirty) return FrameStep::idle;
        const bool emg = m.emergency() != Emergency::idle;
        if (!emg && elapsed(s.now_ms, _last_paint_ms) < kPaintThrottleMs) return FrameStep::idle;
        m.clear_dirty();               // ★★ §B107: CONSUMED AT THE FREEZE, never at the final page
        _last_paint_ms = s.now_ms;
        // ★★ §B108 — WHAT THIS FRAME WILL ACTUALLY SHOW, frozen with everything else. "Visible" mirrors `draw_frame`'s
        //   two early returns exactly: the emergency overlay REPLACES the body, and so does the compose modal — a
        //   frame showing either has not shown the Inbox, whatever `screen` says underneath it.
        // ★★ §UI-7D slice B: THE DETAIL MODAL IS THE THIRD BODY-REPLACING VIEW and is EXCLUDED for exactly the reason
        //    the other two are — `draw_frame` returns straight after drawing it, so a frame showing the modal HAS NOT
        //    SHOWN THE INBOX LIST, whatever `screen` says underneath. Counting it would clear the session unread
        //    counters for messages the panel never listed, which is [[B108]]'s harm in a new location.
        const UiState& st = m.state();
        _fr_inbox = (m.emergency() == Emergency::idle && st.compose == Compose::none &&
                     st.detail == InboxModal::closed && st.screen == Screen::inbox);
        // ★★ THE SERIALS, not the counts (§B108 round 2). These come from the same `publish` that produced the
        //   `unread_*` this frame will render, so "what the user saw" and "what we will mark read" cannot diverge.
        _fr_arr_dm = s.arr_dm;
        _fr_arr_ch = s.arr_ch;
        _fr_emg   = m.emergency();      // §B102: which outcome these eight pages will put in front of the user...
        _fr_news  = m.emg_news();       // ...and WHICH news it is, so a newer one is not credited to this frame
        return FrameStep::open;
    }

    // The board's `next_page()` verdict: true = more pages to come, false = the frame is COMPLETE.
    // ★★ §B108 — THE ONLY PLACE UNREAD COUNTS ARE MARKED READ. The shipped tick zeroed them on EVERY pass while
    //   `screen == inbox`, ahead of the blanked check and before a single page had reached the panel — so messages
    //   were discarded unseen while blanked, while the MAC was busy, under the emergency overlay, or simply because
    //   the screen had been cycled to. ⇒ a COMPLETE and ACTUALLY VISIBLE Inbox frame is the event.
    // ★ AND IT MARKS READ ONLY WHAT THE FRAME FROZE. A bare `= 0` here would still lose a message that arrived during
    //   the eight ticks the frame took to page out: it was never on the panel, so it was never read.
    void on_page(bool more, UiModel& m, UiInboxCounters& c) {
        _open = more;
        if (more) return;
        // ★★ §B102: the frame is COMPLETE — this is the only moment anything may be called "seen".
        m.mark_outcome_presented(_fr_emg, _fr_news);
        if (!_fr_inbox) return;
        // ★★★ §B108 ROUND 2 — ADVANCE THE WATERMARK; NEVER SUBTRACT A COUNT. Assignment is exact, so there is no
        //   underflow to clamp — and the clamp is what used to hide the defect. The old form subtracted the frozen
        //   COUNT, which at `kUnreadCap` was 999 both before and after a mid-frame arrival, so the arrival was marked
        //   read having never been on the panel. A serial has no saturation, so the arithmetic simply cannot lose it.
        // ⓘ Why the watermark goes to the FULL frozen serial even when the panel rendered a clamped "999": the Inbox
        //   is a glanceable summary with no read cursor, so a complete visible frame means "the user looked" — the
        //   three digits are a rendering limit, never a statement about how many messages were accounted for.
        c.read_dm = _fr_arr_dm;
        c.read_ch = _fr_arr_ch;
        _fr_inbox = false;   // spent: a completed frame marks its OWN arrivals read exactly once
    }

    bool     frame_open()    const { return _open; }
    uint32_t last_paint_ms() const { return _last_paint_ms; }   // diagnostic; the throttle's own state

protected:
    static uint32_t elapsed(uint32_t now, uint32_t then) { return now - then; }   // wrap-safe, like UiModel's
    bool     _open          = false;
    uint32_t _last_paint_ms = 0;
    // The frozen frame DESCRIPTOR (§B108): what the pages now going out will actually put in front of the user.
    bool      _fr_inbox = false;
    uint32_t  _fr_arr_dm = 0, _fr_arr_ch = 0;   // ARRIVAL SERIALS, not counts — round 2; the cap is display-only
    Emergency _fr_emg = Emergency::idle;
    uint32_t  _fr_news = 0;
};

// ============================================================== §B197/§B198 — MAY THE DEVICE LIGHT-SLEEP RIGHT NOW?
// ★★★ THE UI HALF OF THE SLEEP POLICY, AND IT IS HERE FOR THE SAME REASON `FrameGate` IS: `src/fw_main.cpp`'s sleep
//   gate is compiled by neither the native suite nor the simulator, so a predicate written there could only ever be
//   grepped. As a pure function over the THREE EXISTING AUTHORITIES it is driven by the native suite against the real
//   `UiModel` / `InputFsm` / `FrameGate` state machines.
//
// ★★ THE THREE TERMS ARE THREE DIFFERENT DEFECTS, not one property spelled three ways:
//   `blanked`      — while the panel is intentionally LIT the operator is looking at it, and the ~15 s attention
//                    window (`kBlankMs`) is already bounded, so the CPU stays up for it. ⛔ Note the two clocks are
//                    INDEPENDENT: a dark panel says nothing about `MR_BOOT_GRACE_MS`, and vice versa ([[B197]]).
//   `input.active` — [[B197]]: a ≤1 s sleep pass is longer than debounce (25 ms), the double window (350 ms) and the
//                    arm threshold (800 ms), so sleeping mid-gesture is what turns one short tap into "nothing
//                    happened" and a long hold into the only input that ever registers.
//   `frame_open`   — [[B198]]: the panel paints ONE 128 B page per service pass, so a sleep pass between pages costs
//                    the WHOLE FRAME up to 8 × MR_MAX_SLEEP_MS ≈ 8 s. Measured on metal, on the EMERGENCY screen.
//
// ⛔ `FrameGate::frame_open()` IS the page-loop authority and it already existed — the board's private `s_painting`
//   must NOT be exported to answer this ([[B198]]'s own correction). The two are kept in step by `on_page`.
// ⓘ This says nothing about the RADIO or the host: `!tx_busy && txq_depth == 0 && !serial_has_input && !ble` stay
//   exactly where they are, in the fw_main gate. This predicate only ever ADDS a reason to stay awake.
inline bool ui_allows_sleep(const UiModel& m, const InputFsm& in, const FrameGate& g) {
    return m.state().blanked && !in.active() && !g.frame_open();
}

}  // namespace mrui
