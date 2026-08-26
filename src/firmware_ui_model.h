// MeshRoute — src/firmware_ui_model.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Pure screen/state model for the one-button board UI (UI-2). Consumes a gesture plus a plain-data snapshot and
// produces what to draw. Knows nothing of g_node, Arduino or the display — that is what keeps it native-testable and
// every hardware concern in variants/heltec_common/board_ui.cpp. See
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
// DONE here (2026-08-20, [[B232]] — the SETTINGS SINGLE ENTRY, owner-ruled): SETTINGS now LANDS on a CLOSED view of
// ONE row (`kSettingsEnterText`), so `short` passes the screen in ONE press like every other screen and `double`
// ENTERS the menu (`open_settings_menu` / `close_settings_menu` — the PROVISION-child enter-by-double idiom).
// ⛔ IT REVERSES §UI-14's DOCUMENTED LANDING, in which `sync_settings` auto-entered `browsing` on arrival and the
//    cursor had to walk all nine rows before the screen advanced. ★ The service is STILL OPENED ON ARRIVAL — the
//    §3.6.1 baseline, the conflict latch and the rail badge all depend on it, and deferring `open()` to the menu is
//    the tempting wrong fix.
// DONE here (2026-08-20, §UI-17 slice 1 — TEAM and INBOX migrated onto that same PASSIVE ↔ INTERACTIVE idiom, spec
// §3.1/§3.2): both screens now LAND PASSIVE — a preview list with NO marker and NO recorded pick, which `short` passes
// in ONE press — and are ENTERED by a `double` (`ListView`, `open_list_view` / `close_list_view`). The interactive
// list's last row is `BACK` (`list_row_kind` / `kListBackText`), the walk off it returns to the FIRST row and ⛔ NEVER
// leaves the screen, and `BACK` returns to the PASSIVE form of the SAME screen. "Is this screen entered" is ONE
// `default`-less predicate for all three list screens (`screen_is_entered`, reading `Settings` for SETTINGS and
// `ListView` for TEAM/INBOX), and leaving is one pure reset (`list_view_reset_on_leave`, the [[B223]] extraction).
// ★ §B64/§UI-7D's identity cursors are UNCHANGED and are now gated on the interactive arm: a passive screen records no
//   pick, so `activate` there cannot queue anything at all.
// DONE here (2026-08-25, §UI-17 keyrecv — OWNER-RULED, shape (a)): acknowledging the `TEAM KEY RECEIVED` note LEAVES
// the provisioning flow for the PASSIVE STATUS screen (`team_key_note_ack_landed`, keyed on the ANSWER so BOTH result
// arms share one rule). ⛔ ONE landing moved: the failure pair (`TEAM KEY ACTIVE` / `NOT SAVED` / `LOST ON REBOOT`) and
// every other terminal — `TEAM CREATED`, `TEAM JOINED`, `ADOPTED`, every refusal, K5's two endings — land exactly where
// they landed before, and ⛔ the note's ARRIVAL still navigates nothing and wakes nothing (spec §4-K4 pin 3).
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
// ⛔⛔ CORRECTED AGAIN 2026-08-26 BY §UI-10/11 P3 (QG), and the 2026-08-05 repair above is KEPT VISIBLE because its
//    successor is the same class of drift: **there are no `kDmTexts`/`kChannelTexts` any more, and no line 80-89.**
//    P3 RETIRED both tables (their withdrawn declarations sit beside `kComposeBackText` below), so nothing here is
//    `sizeof`-derived. ⇒ the model still *emits* rather than sends, but what it emits is a **STABLE `/mrui` SLOT
//    plus the catalog GENERATION the wearer saw** (`SendReq`), and the words themselves live in the record
//    (`mrfw::PresetCatalog`), reaching the panel as a per-frame projection (`UiSnapshot::preset_dm`/`preset_ch`).
//    ⓘ A line number in prose is a fact with no gate behind it; this block now names SYMBOLS only.
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
// ★ §UI-16 N2 — the NEARBY scan screen's pure unit, MODEL SIDE: the row carrier, its identity, the
//   own-team filter, the rows the operator walks and every lexeme. `UiSnapshot` publishes its `NearbyRow`
//   array and `UiState` holds the FROZEN copy, so the types must be visible here (the `UiJoinList`
//   precedent above). ⓘ Its row TOKENS live in `firmware_ui_nearby_row.h`, which this unit must NOT
//   include: that file needs `firmware_ui_chrome.h`, and chrome includes THIS header — see the split's own
//   note. `src/firmware_ui.cpp`, the tests and the probe include both.
#include "firmware_ui_nearby.h"
// ★ §UI-16 N4 — the INVITE window's pure unit: the member projection `UiSnapshot` publishes, the window state
//   `UiState` holds (the two snapshot authorities, the volatile handled set and the frozen selection), the diff,
//   the candidate row and every lexeme. It is included HERE for the reason `firmware_ui_nearby.h` is — the types
//   ride the snapshot and the state — and it is ONE file rather than two because, unlike N2's row, nothing in it
//   needs `firmware_ui_chrome.h` (see its own opening note for the measured include chain).
#include "firmware_ui_invite.h"
// ★★★★ §UI-16 K6 — the `/mrteams` keyring's PURE POLICY, included for ONE type and one predicate: the
//      **METADATA-ONLY** `mrfw::SavedKeyList` (`{team_id, active}` per record — ⛔ never key bytes) that `UiState`
//      holds FROZEN for the `SAVED KEYS` screen, and the outcome the removal reports. That is `UiJoinList`'s rule
//      applied a second time: the panel holds the SERVICE's own answer VERBATIM rather than a `mrui::` mirror free
//      to drift from it (the `UiState::cfg_save` argument).
// ⓘ IT COSTS THIS UNIT EXACTLY ONE NEW DEPENDENCY, NAMED SO IT IS A DECISION AND NOT A DRIFT: `device_nv.h` already
//   arrives through `firmware_config_service.h`, so the only addition is `monocypher.h` (that header's
//   `SecretWipeGuard` needs `crypto_wipe`). MEASURED, ⛔ not assumed: every TU that compiles this file already has
//   `lib/monocypher/src` on its include path — the native suite, the three OLED board envs and
//   `tools/probe_firmware_ui`. ⛔ `tools/probe_board_ui` does NOT, and it is unaffected: it compiles `board_ui.cpp`
//   and its own `probe_main.cpp`, neither of which includes this header (its W35/W36 checks are the rule that they
//   never will, and they are greps rather than compiles).
// ⛔ AND IT IS ⛔ NOT A LICENCE FOR THE MODEL TO REACH THE KEYRING: this unit calls no service. It holds an answer a
//    seam handed it, exactly as it holds `mrfw::ProfileResult`.
#include "firmware_team_keyring.h"
// ★★★★ §UI-10/11 P3 — THE `/mrui` PRESET CATALOG'S PURE UNIT, included for the RECORD and the STABLE SLOT SPACE:
//      `mrnv::UiPresetBlob` / `UiPresetSlot` (what a compose list is PROJECTED FROM) and `mrfw::kPresetPerKind` /
//      `preset_kind_of` / `kPresetEmergency` (the slot ids a row CARRIES). P1 declared this dependency in advance —
//      *"this header must stay model-INCLUDABLE (P3 needs it from inside `firmware_ui_model.h`)"* — and the direction
//      is the §UI-16 N2 rule: a header the MODEL includes may never include the model.
// ⛔ IT IS ⛔ NOT A LICENCE FOR THE MODEL TO REACH THE SERVICE, and that is the same sentence the keyring block above
//    ends with, for the same reason: this unit constructs no `PresetCatalog`, calls no verb and performs no write.
//    It reads a RECORD that `build_snapshot` published and that the frame FROZE (§3.2.3's per-frame generation
//    freeze) — the `mrfw::SavedKeyList` discipline one feature over.
#include "firmware_ui_presets.h"
#include "firmware_ui_input.h"

namespace mrui {

inline constexpr uint32_t kBlankMs      = 15000;
inline constexpr uint8_t  kMaxTeamRows  = 8;    // spec §11: a 3-10 member group; the snapshot reports the TRUE total too
inline constexpr uint8_t  kMaxInboxRows = 8;
inline constexpr uint8_t  kLabelCap     = 14;   // display-clamped teammate label
// ★★ §UI-16 N4 — THE TWO BOUNDS THE INVITE UNIT COULD NOT STATE FOR ITSELF, ASSERTED RATHER THAN COMMENTED.
//    `firmware_ui_invite.h` may not include this header (this one includes IT), so its capacities are declared
//    there and TIED here: the member array is filled by the SAME loop, bounded by the SAME count, as `team[]`
//    (spec §6: one enumeration, one `team_key_of_id` resolution, two consumers), and the cached name it carries
//    must hold a whole `kLabelCap` label so the invite row and the TEAM row clamp ONE name at ONE width.
static_assert(kMaxInviteRows >= kMaxTeamRows,
              "§UI-16 N4: the invite member array is filled from the TEAM enumeration and must not be shorter");
static_assert(std::size_t(kInviteNameCap) == std::size_t(kLabelCap) + 1u,
              "§UI-16 N4: the cached name must be stored at the label's own width — one name, one truncation");

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
//   `closed`   — the menu is NOT up. ★★ [[B232]] gave this arm a SECOND, VISIBLE meaning and it is the ruling's
//                whole shape: it is still what SETTINGS is off-screen (the editor MUST NOT survive there — leaving
//                the screen closes it, see `sync_settings`), and it is ALSO the view SETTINGS now LANDS on — one
//                entry row (`kSettingsEnterText`), `short` passes the screen, `double` opens the menu.
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

// ============================================================== §UI-17 slice 1 — TEAM/INBOX: PASSIVE ↔ INTERACTIVE
// ★★★ THE SAME TWO-STATE IDIOM [[B232]] PROVED ON SETTINGS, APPLIED TO THE OTHER TWO LIST SCREENS (spec §3.1/§3.2):
//     a screen is either a PASSIVE preview — no marker, no recorded pick, ONE row, so `short` passes it in ONE press —
//     or it has been ENTERED, in which case `short` walks its rows and the last row is `BACK`.
// ⛔ IT IS DELIBERATELY **NOT** FOLDED INTO `Settings`. That enum is SETTINGS's own richer four-arm state (it also has
//    to separate `short`'s two modes and name the provisioning sub-view); folding this into it would be a refactor of
//    shipped code riding a feature slice — C1 — and would give TEAM and INBOX two arms they can never be in.
// ⛔ AND IT IS ⛔ NOT A SECOND AUTHORITY on "which screen is open": there is ONE field, because only the CURRENT screen
//    can be entered and leaving RESETS it (`list_view_reset_on_leave`). The question *"is this screen entered"* is
//    answered for all three list screens by the ONE `default`-less predicate below.
enum class ListView : uint8_t { passive = 0, interactive };

// ★★ THE ROW KINDS OF AN INTERACTIVE TEAM/INBOX LIST. Two, because the list is *"the rows the snapshot published"*
//    plus the ONE row that leaves it — and §B66's rule is that a row's meaning may never be derived from its position
//    at a call site.
enum class ListRow : uint8_t { member = 0, back };

// ★★★★ THE `BACK` ROW RESOLVER, AND IT IS A FUNCTION RATHER THAN A `cursor == shown` AT EACH SITE (§B66). FOUR
//      production call sites ask it — `list_activate`, `list_note_kind` and the two renderers — and a fifth would be
//      the one that got it wrong. ⓘ CORRECTED IN PLACE 2026-08-21 (QG, S1 close-out): this read "Three call sites —
//      `activate`'s two arms and the renderer", written before the QG-ruled hoist moved the decisions into the two
//      shared helpers.
// ⛔ IT FAILS CLOSED: `>=`, not `==`. A cursor left BEYOND the published rows (the roster shrank under an interactive
//    list between two ticks) names `BACK`, which leaves and sends nothing — never a member row it would then have to
//    read out of range. ⓘ The `short` walk repairs the cursor on the very next press (`advance_or_next` wraps it to 0).
inline ListRow list_row_kind(uint8_t cursor, uint8_t shown) {
    return (cursor >= shown) ? ListRow::back : ListRow::member;
}

// ★ The row's label, in this PURE unit for the §B115 reason (a string built in `src/firmware_ui.cpp` is a string no
//   automated gate can read), and it is DELIBERATELY the word the two shipped row tables already use
//   (`settings_row_label(CfgRow::back)` / `provision_row_label(ProvRow::back)`) — spec §8's S-12: one spelling for one
//   act, so an operator reads the same exit on every screen. A native case asserts the three agree.
// ⓘ ⛔ AND THE THREE ARE NOT UNIFIED INTO ONE TABLE HERE: those two are other screens' row tables, and merging them is
//   a refactor of shipped code, which may not ride a feature slice (C1).
inline constexpr const char* kListBackText = "BACK";

// ★★★★ §UI-17 S1 — **HOW LONG IS A LIST SCREEN?** ⛔ ONE decision for BOTH lists (QG-RULED 2026-08-21): TEAM and
//      INBOX had this written out twice, so a mutation could redden one arm and leave the other unprotected —
//      the shape [[B217]] exists to prevent. Only the DATA differs per screen (which roster), never the decision.
// ★ A screen that has NOT been entered is ONE row, which is the whole of "one press passes the screen"; an ENTERED
//   list is the published rows PLUS the `BACK` row that leaves it.
inline uint8_t list_len_of(bool entered, uint8_t shown) {
    return entered ? uint8_t(shown + 1) : uint8_t(1);
}

// ★★★★ **WHAT DOES A `double` ON A LIST SCREEN MEAN?** — the second decision the two screens shared, and the one
//      whose ORDER is load-bearing. ⛔ ONE site (QG-RULED 2026-08-21), so each of its three rules is mutated once.
enum class ListAct : uint8_t { enter = 0, refuse, leave, member };

// ★★★ THE ORDER IS THE CONTRACT, and each line is a separate ruling:
//   1. a PASSIVE preview offers exactly ONE gesture — the one that ENTERS it. ⛔ It cannot send a DM, open a record
//      or raise a refusal, because it has recorded no pick at all (`list_note_kind` below).
//   2. **§B64's REFUSAL OUTRANKS THE `BACK` ROW.** When the roster SHRINKS under an entered list the lost pick's
//      index can BE the `BACK` index (cursor 2 meeting a 2-row roster; a record erased out of band does the same one
//      plane over), and resolving the row FIRST would silently swallow a refusal into a "leave" — the mis-send
//      arriving as a missing message. `pick_gone` is the flag `sync_*_cursor` has ALREADY raised for a vanished pick,
//      and it is what separates "the pick was LOST" from "the cursor is simply on BACK, which picks nobody".
//   3. only then the row itself, through `list_row_kind` — ⛔ never a bare `cursor == shown` (§B66).
inline ListAct list_activate(bool entered, bool pick_gone, uint8_t cursor, uint8_t shown) {
    if (!entered)  return ListAct::enter;
    if (pick_gone) return ListAct::refuse;
    return (list_row_kind(cursor, shown) == ListRow::back) ? ListAct::leave : ListAct::member;
}

// ★★★★ **WHAT DOES THE WRITE SIDE DO WITH THIS CURSOR?** — the third shared decision (`note_team_cursor` /
//      `note_inbox_cursor` had it twice). ⛔ ONE site (QG-RULED 2026-08-21).
enum class ListNote : uint8_t { record = 0, retire, keep };

// ★★★ THREE ANSWERS, AND EACH IS A RULE THIS SLICE OWES:
//   · `retire` when the screen has been LEFT — the message is TRANSIENT, so a stale `TEAMMATE GONE` may never
//     reappear a lap later describing a pick from minutes ago. ⛔ Through the model this arm is UNREACHABLE today
//     (an entered list cannot be walked off its screen), which is exactly why the decision lives HERE where the
//     suite drives it and a mutation can redden it — [[B223]], for the sixth time in this arc.
//   · `keep` on a PASSIVE preview: nothing was pointed at, so nothing is recorded — and a roster change therefore
//     cannot announce the loss of a pick the operator never made.
//   · on an ENTERED list the row decides: a MEMBER row is the new pick; the `BACK` row records nobody AND retires
//     the message, because that row IS the way out and `list_activate`'s refusal arm deliberately outranks it —
//     without this, a lost pick whose roster then EMPTIED would leave the operator inside a list where every
//     `double` refuses and `BACK` is one of them, a dead end with no way back to the cycle.
inline ListNote list_note_kind(bool on_screen, bool entered, uint8_t cursor, uint8_t shown) {
    if (!on_screen) return ListNote::retire;
    if (!entered)   return ListNote::keep;
    return (list_row_kind(cursor, shown) == ListRow::member) ? ListNote::record : ListNote::retire;
}

// ★★★★ §UI-17 S1 / [[B223]] — **THE LEAVE RESET, AS A PURE FUNCTION**, hoisted out of its call sites for the reason
//      `provision_reset_on_leave` states one screen over, and it is the FIFTH time this arc: the decision is
//      UNREACHABLE where it would otherwise be written — an interactive TEAM/INBOX list cannot be walked off its own
//      screen (`advance_or_next` contains it), so today NOTHING can move `_st.screen` away while `interactive` stands
//      — and a guard living only at that call site is a guard no suite can drive and no mutation can redden.
//      ⇒ THE DECISION LIVES HERE, where the suite drives both arms directly, and both call sites are forwards.
// ★ The INVARIANT is "leaving the screen closes the view", ⛔ not "the paths that can leave today close it": the first
//   arm that a push, a timeout or a future screen moves the screen out of finds this already true.
// ⓘ Returns whether anything CHANGED, so a caller repaints for a real close and not for every tick spent off-screen.
inline bool list_view_reset_on_leave(ListView& view) {
    const bool changed = view != ListView::passive;
    view = ListView::passive;
    return changed;
}

// ★★★★ **HAS THIS SCREEN BEEN ENTERED?** — ONE predicate, for all three list screens, and it is what keeps `ListView`
//      from becoming a second authority. It reads `Settings` for SETTINGS ([[B232]]'s closed view) and `ListView` for
//      TEAM and INBOX; STATUS and SEND have no interaction to enter, so they answer false and their `double` stays the
//      no-op it has always been.
// ⛔ `default`-LESS, so `-Werror=switch` fails the build when a sixth screen is added without stating which side of
//    this question it is on — the same discipline `cfg_row_field` and `cfg_field_name` hold one screen over. That is
//    the whole reason it is a `switch` over `Screen` rather than a two-term boolean expression.
// ⓘ `Screen::count` is not a screen; it answers false rather than being left to a fall-through.
inline bool screen_is_entered(Screen sc, Settings settings, ListView view) {
    switch (sc) {
        case Screen::team:
        case Screen::inbox:    return view == ListView::interactive;
        case Screen::settings: return settings != Settings::closed;
        case Screen::status:
        case Screen::send:
        case Screen::count:    return false;
    }
    return false;
}

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
//   `create_result`  — LIVE (slice 5): the transaction's verdict, and it is entered ONLY by an ACT THAT RAN, i.e.
//                      only by the act that established it. Terminal; either press returns to the menu.
//                      ⛔ CORRECTED IN PLACE 2026-08-23 (§UI-16 N3), AND THE WITHDRAWN WORDING IS KEPT VISIBLE:
//                      this line read *"entered ONLY by `run_create_team`"*. It is now entered by `run_create_team`
//                      AND by `run_join_team` — **two acts, one TERMINAL RESULT SCREEN**, because the screen
//                      renders `UiState::prov_answer` and nothing else, and N3's scope is ⛔ ONE new `Provision`
//                      arm (spec §4-N3), which the CONFIRMATION spends. ⓘ The property the old wording protected is
//                      UNCHANGED and is what matters: ⛔ no path enters this arm without having just run a
//                      transaction and written its verdict — see both `run_*` bodies, whose last two statements are
//                      the same two in the same order.
//   `join_select`    — LIVE (slice 6): the four `/mrjoin` slots (`join_sel_rows` over `UiState::join_list`, read ONCE
//                      on the transition) plus BACK. A non-`ok` store offers no slot and SAYS why (`join_store_head`).
//   `join_confirm`   — LIVE (slice 6): design §3.6.3's *"the complete values before confirmation, with BACK selected
//                      initially"*; `short` toggles, `double` performs. BACK returns to `join_select`, ⛔ not the menu.
//   `join_waiting`   — LIVE (slice 6): entered ONLY by a `started` transaction. It shows `JOINING`, becomes
//                      `STILL JOINING` at 60 s (⛔ never a failure), and either press LEAVES IT WITHOUT CANCELLING
//                      ANYTHING (plan §2.3 rule 4). Its completion is the four-term rule's (`join_push_correlates`).
//   `join_result`    — LIVE (slice 6): entered by the act that established it (`run_join_static` for a refusal or a
//                      failed save) or by a CORRELATED adopt (`on_join_push`). ⛔ By nothing else. Terminal.
//   `nearby`         — LIVE (§UI-16 N2): design §3.6.4 point 2's READ-ONLY scan. The retained nearby-team
//                      observations (`Node::team_seen_*`), captured ONCE on the transition into
//                      `UiState::nearby` and rendered as `fingerprint · n/3 · age` plus BACK. ⛔ It performs
//                      NOTHING — no join, no confirmation, no airtime — and BACK returns to the PROVISION
//                      menu. ✅ §UI-16 N3 LANDED ITS ACT: a `double` on a team row now opens `nearby_confirm`
//                      below ([[B222]]'s rule paid off — the transition arrived WITH its flow, one slice later).
//   `nearby_confirm` — LIVE (§UI-16 N3): design §3.6.4 point 3's *"`JOIN <fingerprint>?` with BACK selected
//                      initially"*. `short` toggles, `double` performs, and ⛔ BACK returns to the NEARBY
//                      LIST (⛔ not the menu) — the `join_confirm` -> `join_select` containment, one screen
//                      over. ★ WHAT IT CARRIES IS `UiState::nearby_sel_id`, the FULL 32-bit team id of the
//                      row the operator was standing on — ⛔ never the cursor index and ⛔ never a value
//                      re-derived from the six-character fingerprint (spec §3 P-7, [[B48]]'s class).
//   `invite`         — LIVE (§UI-16 N4): design §3.6.4 point 1's BOUNDED INVITATION WINDOW. It lists the members
//                      that appeared AFTER the snapshot the OPENING took (`UiState::invite`, the two authorities
//                      F-11 rules) and REFRESHES LOCALLY while it is up (R-10) — ⛔ no scan, ⛔ nothing
//                      transmitted, it only re-reads member state the node already holds. ★ IT HAS ITS OWN
//                      DEADLINE (`_invite_until_ms` / `window_active`, 5 minutes, R-3) which ⛔ does NOT hold the
//                      panel lit and ⛔ never writes `_last_input_ms`.
//   `invite_confirm` — LIVE (§UI-16 N4/N5/N6): the grant-ready confirmation. It FREEZES the selected hash/id,
//                      draws the FULL `0x%08lX` hash beside any cached name (P-7c), opens on REJECT and enables
//                      GRANT KEY only after N5's preflight. ✅ §UI-16 N6 LANDED ITS ACT: a `double` on GRANT KEY
//                      now performs the ONE forward to `Node::team_key_grant_send` and lands `invite_result`.
//   `invite_result`  — LIVE (§UI-16 N6): the grant's VERDICT (`UiState::grant`), in the outcome's own word.
//                      Terminal, either press returns to the menu. ★ IT OUTLIVES THE WINDOW deliberately — the act
//                      ended it — which is why the verdict carries its own hash and its own `{dst, ctr}` handle,
//                      and why `provision_is_invite` excludes this arm (an expiring window may ⛔ not overwrite a
//                      verdict with `WINDOW CLOSED`).
//   `invite_need_pubkey` / `invite_wait_pubkey` — LIVE (§UI-16 N5): the explicit BACK-default request and the
//                      post-request wait. No request is automatic; a matching cached-key push returns to the
//                      locally refreshed candidate row, whose next double re-runs the grant's own preflight.
//   `saved_key`      — LIVE (§UI-16 K5): design §3.6.4 point 4's *"`SAVED KEY FOUND` with `BACK` selected and an
//                      explicit `USE SAVED KEY`"*. ★★★ IT IS REACHED FROM THE **ACKNOWLEDGEMENT OF A `team_joined`
//                      RESULT**, and only when the JOINED team has a RETAINED `/mrteams` record — ⛔ never from the
//                      confirmation, ⛔ never from the join act itself. That placement IS P-2b: the membership
//                      transaction has already run, returned and been REPORTED before the key question is asked, so
//                      nothing about the key can be read as part of joining. `short` toggles, `double` performs the
//                      selected one; `BACK` lands on the MENU exactly where the acknowledgement would have, and
//                      ★ CHANGES **NO KEY STATE AT ALL** — nothing installed, nothing cleared, the retained record
//                      untouched. (⛔ ⛔ It does ⛔ NOT "leave the node keyless": after a membership change under
//                      the offer the node may legitimately hold the CURRENT team's key, and declining an offer
//                      about another team may never destroy it.)
//   `invite_closed`  — LIVE (§UI-16 N4): the window RAN OUT (`WINDOW CLOSED`). Terminal, either press returns to
//                      the menu. ⛔ ENTERING IT GRANTS, REVOKES AND REWRITES NOTHING (P-11) — it moves a screen
//                      and drops RAM, and `enter_provision` is what discards the window's whole state with it.
enum class Provision : uint8_t {
    closed = 0, menu, create_confirm, create_result, join_select, join_confirm, join_waiting, join_result,
    nearby, nearby_confirm, invite, invite_confirm, invite_closed, invite_need_pubkey, invite_wait_pubkey,
    invite_result,  // §UI-16 N6 — APPENDED, ⛔ never inserted: `provision_reset_on_leave`'s arm sweep and every
                    // renderer switch enumerate this type, and an insertion renumbers arms nothing else can see
    saved_key,      // §UI-16 K5 — APPENDED for the same reason, and it is the slice's ONE new arm (its RESULT
                    // reuses `create_result`, which renders `prov_answer` and nothing else — N3's own precedent)
    // ★★★★ §UI-16 K6 — FOUR ARMS, APPENDED (⛔ never inserted, for the reason the two above state), and the split
    //      into four is the SAFETY ARGUMENT rather than screen-count taste:
    //   `saved_keys`         — LIVE: the RETENTION list. The keyring's METADATA-ONLY enumeration (⛔ never key
    //                          bytes), read ONCE on the `menu -> saved_keys` transition — it reaches flash — and
    //                          held FROZEN in `UiState::saved_keys`, exactly as `/mrjoin`'s slot list is. Rows are
    //                          the shared fingerprint plus the `ACTIVE` marker (S-44); BACK is the unconditional
    //                          last row and returns to the PROVISION MENU. ⛔ It performs NOTHING.
    //   `saved_keys_confirm` — LIVE: the IRREVERSIBLE confirmation for an **INACTIVE** row. It shows the FULL 32-bit
    //                          id, opens on `BACK` and offers `FORGET KEY` (S-31). BACK returns to the LIST, ⛔ not
    //                          the menu (the `join_confirm -> join_select` containment).
    //   `saved_keys_active`  — LIVE: the PROTECTED landing for an **ACTIVE** row — `ACTIVE KEY` / `CANNOT FORGET`
    //                          (S-43, the `PHY DIFFERS` / `USE SERIAL` two-row precedent). ★★★★ IT IS A SEPARATE
    //                          ARM RATHER THAN A CONDITIONAL INSIDE THE CONFIRMATION, AND THAT IS THE POINT: on
    //                          this screen there IS no destructive action to select, so "the active key cannot be
    //                          forgotten from the panel" is STRUCTURAL. A shared arm that merely hid the row would
    //                          leave `ProvConfirm::confirm` reachable by a `short`, one `double` from the act.
    //   `saved_keys_result`  — LIVE: the removal's VERDICT (`KEY FORGOTTEN`, S-42, or the failure word plus the
    //                          SERVICE's own token). Terminal; either press acknowledges it and returns to the
    //                          **REFRESHED** list (the read runs again on that landing). ⛔ A storage failure gets
    //                          its own screen and its own word — it is ⛔ never rendered as a success.
    saved_keys, saved_keys_confirm, saved_keys_active, saved_keys_result
};

// ★★ IS THIS ARM THE INVITATION WINDOW ITSELF? Asked in ONE place and answered for THREE readers (U1): the
//    window's own deadline (`window_active`'s CLEARING TERM — `hold_active`'s shape, U3), the tick that closes
//    an expired window, and `enter_provision`'s discard of the volatile per-window state (F-13).
// ⛔ `invite_closed` IS DELIBERATELY NOT ONE OF THEM: the window is over on that screen, so a candidate list may
//    not be produced there and the handled set must already be gone.
// ⛔⛔ AND `invite_result` IS NOT ONE OF THEM EITHER, for a SHARPER reason (§UI-16 N6): a grant's verdict is
//     terminal and must survive until the operator acknowledges it, so an expiring window may ⛔ not replace
//     `KEY SENT` with `WINDOW CLOSED`. Excluding it here is what makes `tick_invite` leave the verdict alone AND
//     what makes `enter_provision` drop the window's whole state on the way into it — the act ended the window.
inline bool provision_is_invite(Provision p) {
    return p == Provision::invite || p == Provision::invite_confirm ||
           p == Provision::invite_need_pubkey || p == Provision::invite_wait_pubkey;
}

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
enum class ProvConfirm : uint8_t {
    back = 0, confirm,
    // Same two-slot carrier, named by IDENTITY on the ready screen rather than interpreted positionally (§B66).
    invite_reject = back, invite_grant = confirm
};

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
// ★★★★ §UI-16 N4 ADDS THE **THIRD** FACT, AND IT IS THE SAME ARGUMENT A THIRD TIME: the invitation window's state
//      (the two authorities, the VOLATILE handled set and the frozen selection) may ⛔ not survive a leave either.
//      P-14 is explicit — *"an unconfirmed destructive action does not survive"* an emergency — and F-13 is that the
//      handled set dies WITH the window. An alarm that left the set standing would mean a candidate the operator
//      rejected in a window they never returned to stays suppressed in the NEXT one.
// ⛔ IT IS RESET HERE RATHER THAN AT THE TWO CALL SITES for the reason the other two fields are: a decision written
//    at a call site is a decision the suite drives through a screen instead of directly, and this one has THREE
//    writers (`close_provisioning`, `settings_follow_screen` and — for every non-window arm — `enter_provision`).
// ⓘ Returns whether anything CHANGED, so a caller repaints for a real close and not for every tick spent off-screen.
inline bool provision_reset_on_leave(Provision& arm, ProvConfirm& confirm, InviteWindow& window) {
    const bool changed = arm != Provision::closed || confirm != ProvConfirm::back || window.taken;
    arm = Provision::closed;
    confirm = ProvConfirm::back;
    window = InviteWindow{};
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
//     same reason: ⛔ CORRECTED 2026-08-23 (§UI-16 N2) — this line read *"two of the three rows are CONDITIONAL"*;
//     it is now THREE of FOUR, and the rule it states is unchanged: a row's meaning may not be derived from its
//     position.
// ★ §UI-16 N2 adds the THIRD child (`join_team`), and the code above ANTICIPATED it by name — see
//   `provision_has_child`, which derives the parent row's condition FROM this list precisely so a new child
//   is picked up with zero change there.
// ★★★★ §UI-16 N4 ADDS THE FOURTH CHILD (`invite`), and it is APPENDED before `back` rather than inserted:
//      §3.6.4's own order puts the creator's invitation first, but every landed row here keeps its position for
//      the operator who has learned this menu — and position is not identity anyway (§B66), so the ORDER is a
//      usability choice while the ROW is the meaning.
// ★★★★ §UI-16 K6 ADDS THE FIFTH CHILD (`saved_keys`), APPENDED before `back` for the reason `invite` was: every
//      landed row keeps the position the operator has learned, and position is not identity anyway (§B66).
enum class ProvRow : uint8_t { create_team = 0, join_static, join_team, invite, saved_keys, back, count };
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
// ★★★★ §UI-16 N2 — AND THE THIRD PREDICATE IS A **THIRD SEPARATE PARAMETER**, for the reason the first two
//      are separate and not because three reads better than one: `join_team` is a TEAM-plane operation on a
//      LEAF build (it needs `MR_FEAT_TEAM` for the observation cache to exist at all and `MR_N_LAYERS < 2`
//      for the membership verbs), which happens to coincide with `create_team` in every env in the tree
//      today — and a coincidence is not a rule. ⇒ the native suite drives the combination the tree cannot
//      build, and a later profile that splits them cannot silently take this row with it.
// ✅ OQ-1 RULED (owner, 2026-08-22): `JOIN TEAM` OPENS THE NEARBY LIST **DIRECTLY** — ⛔ never a submenu.
//    A submenu arrives only when a SECOND join method exists; in v1 NEARBY is this row's only child, so a
//    submenu would be exactly the *"row that costs the operator a walk and a `double` to discover it offers
//    nothing"* the 2026-08-19 hiding ruling refuses.
// ★★★★ §UI-16 N4 — AND THE FOURTH PREDICATE IS A **FOURTH SEPARATE PARAMETER**, for the reason the first three
//      are separate: `invite` is the only child whose condition includes a RUNTIME fact — ⛔ it is not merely a
//      build shape, it requires that WE ARE IN A TEAM (spec §4-N4 pin 12), because a teamless node has no
//      membership to invite into and no key it could ever grant. ⇒ the row appears and disappears with
//      `team <id>` / `team 0` on a running node, which no `#if` could express and which the native suite drives
//      both ways. ⓘ On `gateway_heltec` (OLED=1, `MR_N_LAYERS=2`, `MR_FEAT_TEAM=0`) it is false on the BUILD
//      half alone, which is the same pin from the other side.
// ★★★★ §UI-16 K6 — AND THE FIFTH PREDICATE IS A **FIFTH SEPARATE PARAMETER**, for the reason the first four are
//      separate and ⛔ not because five reads better than four: `SAVED KEYS` manages the `/mrteams` KEYRING, which
//      exists only where the team plane does — and unlike `invite` it carries ⛔ NO runtime term, because a node
//      that is in no team may still hold retained records it needs to free (that is the whole dead end K6 exists to
//      open). ⇒ it coincides with `create_team` in every env in the tree today and is still not the same fact.
inline ProvRowList provision_rows(bool create_team, bool join_static, bool join_team, bool invite,
                                  bool saved_keys) {
    ProvRowList l{};
    if (create_team) l.row[l.n++] = ProvRow::create_team;
    if (join_static) l.row[l.n++] = ProvRow::join_static;
    if (join_team)   l.row[l.n++] = ProvRow::join_team;
    if (invite)      l.row[l.n++] = ProvRow::invite;
    if (saved_keys)  l.row[l.n++] = ProvRow::saved_keys;
    l.row[l.n++] = ProvRow::back;
    return l;
}

// ★★★★ §UI-15 slice 5 / OWNER RULING 2026-08-19 — **IS THERE A CHILD AT ALL?**, i.e. the PARENT row's own condition
//      (`settings_rows`'s third parameter). ⛔ IT IS DERIVED FROM THE CHILD LIST AND NOT RE-SPELLED AS
//      `create_team || join_static`: the two predicates would then be two authorities, and slice 6 (or §UI-16's
//      nearby-team child) could add a child the parent row never learned about — a menu entry that opens a sub-view
//      the operator cannot see. ⓘ `back` is UNCONDITIONAL, so it is excluded BY IDENTITY rather than by assuming it
//      is last: the list is built by position and §B66's rule is that position is never an identity.
// ✅ AND IT NEEDED NO CHANGE OF SHAPE WHEN §UI-16 N2 ADDED THE THIRD CHILD — only the parameter it forwards.
//    That is the note above being paid off rather than re-argued: `back` is excluded BY IDENTITY, so the
//    predicate learned about `join_team` the moment `provision_rows` did.
// ✅ AND IT NEEDED NO CHANGE OF SHAPE WHEN §UI-16 K6 ADDED THE FIFTH CHILD EITHER — only the parameter it forwards,
//    which is the same note being paid off for the third slice running.
inline bool provision_has_child(bool create_team, bool join_static, bool join_team, bool invite,
                                bool saved_keys) {
    const ProvRowList l = provision_rows(create_team, join_static, join_team, invite, saved_keys);
    for (uint8_t i = 0; i < l.n; ++i)
        if (l.row[i] != ProvRow::back) return true;
    return false;
}

// ================================================ §UI-16 K6 — THE SAVED-KEY RETENTION SCREENS' LEXEMES AND ROWS
// ★ DECLARED ONCE, IN THIS PURE UNIT, so an owner re-ruling changes each in exactly one place and a native case can
//   pin the exact bytes (§B115: a string built in `src/firmware_ui.cpp` is a string no automated gate can read).
//   ⚠ WIDTH IS A CONSTRAINT: the rail leaves a 19-column body and an action row renders as `%c%s`.
// ⛔⛔ THIS IS SAVED-KEY **RETENTION MANAGEMENT** AND ⛔ NEVER "KEY ROTATION" — the ruling's own words. Nothing on
//     these screens re-keys anything; they remove ONE retained record so the fixed four-record bound stops being a
//     dead end. ⓘ And ⛔ NO KEY BYTE reaches any of them: the rows carry a PUBLIC team id and one status bit.
inline constexpr const char* kSavedKeysTitle    = "SAVED KEYS";      // S-40 — the PROVISION child row AND the title
inline constexpr const char* kSavedKeysEmpty    = "NO SAVED KEYS";   // S-41 — the keyring holds no retained record
inline constexpr const char* kKeyForgottenText  = "KEY FORGOTTEN";   // S-42 — ⛔ reachable ONLY after the save returned
inline constexpr const char* kActiveKeyText     = "ACTIVE KEY";      // S-43 row 1 — the PROTECTED landing
inline constexpr const char* kCannotForgetText  = "CANNOT FORGET";   // S-43 row 2 — the two-row `PHY DIFFERS` shape
inline constexpr const char* kSavedKeyActiveTag = "ACTIVE";          // S-44 — the LIST's row marker (status only)
inline constexpr const char* kForgetKeyText     = "FORGET KEY";      // S-31 — ACTIVATED 2026-08-25; the one act word
// ★★★★ THE FAILURE HEADLINE. ⚠ **REPORTED, NOT INVENTED** (the S-39 precedent, verbatim in method): §8 rules a
//      lexeme for the SUCCESS (S-42) and ⛔ none for what a failed removal says, and ⛔ no ruled lexeme is true on
//      every failing arm — checked one by one: `SAVE FAILED` is FALSE on the five arms that spend ZERO writes;
//      `NO SAVED KEYS` is FALSE when four are stored; `ACTIVE KEY` is FALSE for a store that would not open. ⇒ §8's
//      standing rule is applied (the house style over the SEMANTIC the code establishes), in S-39's exact shape: it
//      states **THE ACT'S OUTCOME** and ⛔ never the store's inventory, so it is true on ALL SIX failing arms — the
//      key was not forgotten, whatever the reason, and the SECOND row carries the service's own token.
// ⛔ SILENCE WAS CONSIDERED AND REFUSED for K5's reason: the operator PRESSED, and a `double` that changes no pixel
//    is the dead-button complaint C2 exists against. ⓘ 17 of the rail's 19 columns.
inline constexpr const char* kKeyNotForgottenText = "KEY NOT FORGOTTEN";

// ------------------------------------------------------------------- the rows, AS IDENTITIES and never as indices
// ★★ §B66's rule, a fifth menu deep, and here it has TEETH: the list SKIPS a corrupt zero-id record (the service
//    does), so a row's position is ⛔ not its identity — and the identity this screen acts on selects a stored
//    SECRET for destruction.
// ⛔ THE ROW CARRIES THE WHOLE `mrfw::SavedKeyEntry` (U2), ⛔ not an index into the list and ⛔ not the six-hex
//    fingerprint the panel prints: that token is the LOW 24 BITS (`ui_fmt_team_fingerprint`) and is a HUMAN
//    SELECTION AID, ⛔ never an authority — 255 other teams share it, and pin 7 drives exactly that collision.
struct SavedKeySelRow {
    mrfw::SavedKeyEntry key{};    // MEANINGFUL ONLY while `!back`
    bool                back = false;
};
struct SavedKeySelList {
    SavedKeySelRow row[mrnv::kTeamKeyRecs + 1] = {};   // every retained record at most, plus the UNCONDITIONAL BACK
    uint8_t        n = 0;
    // ⛔ FAILS CLOSED (C2), exactly as `ProvRowList::at` / `NearbySelList::at` do: an out-of-range index names NO
    //    row and the caller must do nothing rather than being handed a plausible one — and here the plausible one
    //    would open an irreversible confirmation about somebody else's key.
    bool at(uint8_t i, SavedKeySelRow& out) const { if (i >= n) return false; out = row[i]; return true; }
};
// ⓘ BACK IS UNCONDITIONAL — the rule `provision_rows` / `nearby_sel_rows` / `join_sel_rows` all state: leaving must
//   never depend on a store, a build flag or, here, on whether anything was retained.
// ⛔ A LIST THAT WAS NOT ESTABLISHED OFFERS ⛔ NO ROW AT ALL (C2), and the three terms are three different failures
//    the head below tells apart: no seam answered · the ACTIVE marker's authority could not be read · the keyring
//    itself is absent/corrupt/unreachable. ⓘ The service already returns `n == 0` on each, so this states the
//    property rather than relying on it — the `join_sel_rows` treatment of exactly the same hazard.
inline SavedKeySelList saved_keys_sel_rows(const mrfw::SavedKeyList& l) {
    SavedKeySelList out{};
    if (l.served && l.binding_read && l.st == mrnv::TeamKeyRead::ok) {
        for (uint8_t i = 0; i < l.n && i < mrnv::kTeamKeyRecs; ++i) {
            out.row[out.n].key  = l.rec[i];      // ⛔ the WHOLE entry (U2), never rebuilt field by field
            out.row[out.n].back = false;
            ++out.n;
        }
    }
    out.row[out.n].back = true;
    ++out.n;
    return out;
}
// ★★★ THE ROW's SUFFIX — the `ACTIVE` marker (S-44), AND THE DECISION LIVES HERE rather than as an `if` at the draw
//     site (§B115: `src/firmware_ui.cpp` is compiled by neither the native suite nor the simulator, so a condition
//     written there is a condition no gate can drive and no mutation can redden).
// ⛔ IT IS A **STATUS WORD AND NOTHING MORE** (spec §8 S-44's own note): it does ⛔ not authorise, gate or perform
//    anything. The full binding predicate (`mrfw::saved_key_is_active`) remains the authority, and it is what
//    `forget` re-asks of the PERSISTED record at the instant of the act.
// ⓘ The leading space is part of the token: the row draws `%c%s%s` = marker · six-hex fingerprint · this.
//   `>3D9348 ACTIVE` = 1 + 6 + 7 = 14 of the 19-column body.
inline const char* saved_key_row_tag(const mrfw::SavedKeyEntry& e) { return e.active ? " ACTIVE" : ""; }

// ★★★ THE LIST'S ONE NOTE ROW, and the four states are four different sentences because they take four different
//     operator actions — the `join_store_head` ruling one feature over, applied to the keyring's own four-valued
//     read. ⛔ Collapsing any two would tell an operator that a store which would not open holds nothing.
// ⚠ REPORTED, NOT INVENTED: §8 rules `NO SAVED KEYS` (S-41) and ⛔ no lexeme for the three failures. `NO KEYRING` is
//   the `NO JOIN SERVICE` token's shape for a missing seam; `CONFIG UNREADABLE` names the `/mrcfg` record the ACTIVE
//   marker depends on; `STORAGE FAILURE` is `join_store_head`'s own word for the identical device fault (and the
//   console's — `src/firmware_config.cpp` says `STORAGE FAILURE` for an unopenable store). All three are one line
//   each and pinned by native cases, so an owner ruling changes them here and nowhere else.
// ★ AN **ABSENT** STORE IS `NO SAVED KEYS` AND ⛔ NEVER AN ERROR: a device that has never stored a team key read its
//   store perfectly; there was simply nothing in it (`team_key_read_unreadable`'s own distinction).
inline const char* saved_keys_head(const mrfw::SavedKeyList& l) {
    if (!l.served)       return "NO KEYRING";
    // ⛔ FAIL CLOSED, AND IT IS SAID OUT LOUD: without the binding the panel cannot tell the PROTECTED record from
    //    the ones that may go, so it offers none — and an operator who is told why can act on it.
    if (!l.binding_read) return "CONFIG UNREADABLE";
    switch (l.st) {
        case mrnv::TeamKeyRead::ok:        return l.n == 0 ? kSavedKeysEmpty : "";
        case mrnv::TeamKeyRead::absent:    return kSavedKeysEmpty;      // ⛔ never an error
        case mrnv::TeamKeyRead::invalid:   return "KEY STORE INVALID";  // the RECORD is wrong — a teammate re-grants
        case mrnv::TeamKeyRead::io_failed: return "STORAGE FAILURE";    // the STORE would not open — a DEVICE fault
    }
    return "";
}
// ★ The child labels, in this PURE unit for the §B115 reason (a string built in `src/firmware_ui.cpp` is a string no
//   automated gate can read). ⚠ WIDTH IS A CONSTRAINT: an action row renders as `%c%s` in the rail's 19-column body,
//   so the bound is `1 + strlen <= 19` and `chrome4-audit` walks it.
inline const char* provision_row_label(ProvRow r) {
    switch (r) {
        case ProvRow::create_team: return "CREATE TEAM";
        case ProvRow::join_static: return "JOIN NETWORK";
        case ProvRow::join_team:   return "JOIN TEAM";     // §UI-16 S-1 — the design's own path word (§3.6.4 :797)
        // ★ §UI-16 S-12 — the design's own words (§3.6.4 :800), CALLED from the window's title too: ONE spelling
        //   for one operation, so the row the operator pressed and the screen it opened cannot drift apart.
        case ProvRow::invite:      return kInviteTitle;    // "INVITE MEMBER" — 1 + 13 = 14 of the 19-column body
        // ★ §UI-16 S-40 — the SAME declaration the screen's own title uses, CALLED (U1): one spelling for one
        //   operation, so the row the operator pressed and the screen it opened cannot drift apart (the `invite`
        //   treatment one line up). ⓘ 1 + 10 = 11 of the 19-column body.
        case ProvRow::saved_keys:  return kSavedKeysTitle;
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
// ★★★ §UI-16 N3 GREW IT A THIRD TIME, and the note above is being paid off for the second slice running: the nearby
//     join is a THIRD op on the SAME seam, so `UiProvisionAdapter::perform`'s `default`-less dispatch forces a reader
//     to state what performs it. ⛔ Not a fourth `IUiNearbyJoin` seam and ⛔ not a `bool mint` on the create op — the
//     two verbs answer with different WORDS on the panel (`TEAM JOINED` vs `TEAM CREATED`, F-4), so they are two ops.
// ★★★★ §UI-16 K5 ADDS THE FOURTH OP, AND IT IS AN OP RATHER THAN A FLAG ON `join_team` BECAUSE IT IS A DIFFERENT
//      ACT PERFORMED AT A DIFFERENT TIME BY A SEPARATE OPERATOR DECISION (P-2b): the join runs, returns and is
//      REPORTED; only then may `use_saved_key` run, and only on a `double` over a confirmation whose default is
//      `BACK`. ⛔ A `bool install_saved` on the join intent would make the two one transaction again, which is
//      exactly what the ruling forbids. ⓘ `UiProvisionAdapter::perform`'s dispatch is `default`-less, so this line
//      forced a reader to state what performs it.
// ★★★★ §UI-16 K6 ADDS THE FIFTH OP, AND IT IS AN OP ON THE **SAME** SEAM rather than a `bool remove` on any of the
//      four above, for the ruling's own reason: **TWO EXPLICIT TRANSACTIONS, NEVER ONE DISGUISED ONE.** A removal
//      completes and reports its own verdict; the create/grant that ran out of room is retried BY THE OPERATOR.
//      ⛔ A `bool evict_first` on `create_team` would make the two ONE act across TWO durable records — which cannot
//      be one atomic commit, and which would let a failed create destroy an unrelated saved key. ⓘ
//      `UiProvisionAdapter::perform`'s dispatch is `default`-less, so this line forced a reader to state what
//      performs it.
enum class UiProvOp : uint8_t { none = 0, create_team, join_static, join_team, use_saved_key, forget_key };
struct UiProvIntent {
    UiProvOp op = UiProvOp::none;
    // ★★★ THE SELECTED PROFILE, CARRIED WHOLE (U2) — ⛔ never a slot INDEX the adapter would re-read the store for.
    //     WHAT WAS SHOWN IS WHAT IS JOINED: design §3.6.3 requires the complete values on the panel BEFORE the
    //     confirmation, so re-reading `/mrjoin` at CONFIRM time would let a serial `joinprofile set` between the two
    //     presses join something the operator never saw. ⓘ MEANINGFUL ONLY when `op == join_static`.
    // ⓘ It is the STORE's own record type, integral Hz and all: the ONE Hz -> MHz/kHz conversion belongs to the
    //   REQUEST and is `mrfw::join_request_from_profile`'s, which the adapter calls (U2 — one conversion path).
    mrnv::JoinProfile join{};
    // ★★★★ §UI-16 N3 — THE OBSERVED TEAM'S **FULL 32-BIT** ID, CARRIED WHOLE (spec §4-N3 pin 2, §3 P-1/P-7).
    //      ⓘ MEANINGFUL ONLY when `op == join_team`.
    // ⛔⛔ IT IS THE ROW'S OWN IDENTITY AND ⛔ NEVER THE CURSOR INDEX: the NEARBY list is the OWN-TEAM-FILTERED copy,
    //     so index 1 names a different team on a different node (§B66, the same argument `join_sel` makes for slots).
    // ⛔⛔ AND IT IS ⛔ NEVER RE-DERIVED FROM THE SIX-CHARACTER FINGERPRINT the confirmation shows: that token is the
    //     LOW 24 BITS (`ui_fmt_team_fingerprint`, `src/firmware_ui_chrome.h:212`) and is a HUMAN SELECTION AID, never
    //     an authority — a display-shaped value that makes a membership decision is [[B48]]'s exact class, and the
    //     top eight bits it cannot carry are 255 other teams whose fingerprint is identical.
    // ⓘ COST, MEASURED not assumed (host, `-DMR_N_LAYERS=2`): `sizeof(UiProvIntent)` 28 -> **32**. There was no hole
    //   to land in — `op` at 0, `join` (24 B) at 4, the struct ending at 28 with 4-byte alignment — so the field
    //   costs its own four bytes WHEREVER it is declared (both placements measured 32). ⛔ The spec's §6 estimate
    //   said *"marginal cost likely 0"*; it is 4, and this line is the measurement that replaces the estimate.
    uint32_t team_id = 0;
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
// ★★★★ §UI-16 N3 ADDS THE EIGHTH, AND F-4 IS THE WHOLE REASON IT EXISTS: a nearby JOIN lands `ProvVerdict::applied`
//      on the SAME transaction a create does, so without an arm of its own the panel would say **`TEAM CREATED`**
//      for a join — *"a JOIN is not a CREATE, and the two verbs' words must differ on the panel or the operator
//      cannot tell which operation answered"*, three paragraphs up, in the case that block was written against.
//        `team_joined` — the TEAM-membership transaction returned `applied`: exactly one durable write happened and
//                        this node is now a member of the OBSERVED team. ★ IT IS THE SECOND OUTCOME THAT MAY SHOW A
//                        TEAM ID (`created` is the other), and it shows the id the operator SELECTED rather than a
//                        minted one. ⛔⛔ IT SAYS NOTHING ABOUT A KEY, and that is the point of P-2: `set_team_id`
//                        destroys the live content key on a switch (`lib/core/node.cpp:683`), so the joiner is a
//                        **keyless member** — the screen may ⛔ never imply readership, and the forbidden lexeme
//                        `KEYLESS` (S-33) is not how it says so: it simply does not claim one.
// ★★★★ §UI-16 K4 ADDS THE NINTH AND TENTH, AND THEY ARE THE ONLY TWO IN THIS ENUM THAT NO TRANSACTION PRODUCES —
//      a push does (spec §4-K4). ⛔ THAT IS DELIBERATE AND IT IS WHY THEY LIVE HERE RATHER THAN IN A NEW CARRIER:
//      `UiProvAnswer` is the panel's ONE transient "what just happened to this node's team" slot — retired by every
//      `enter_provision` — and the durable-adoption note is the newest fact about exactly that. A second slot would
//      be a second thing to retire, a second thing to render, and two answers free to disagree on one screen.
//        `team_key_received` — ★ REACHABLE **ONLY** THROUGH A PUSH K3 FORWARDED (F-10): the persistence runs first
//                              and ⛔ only a `saved` verdict lets the push reach the renderer, so `TEAM KEY RECEIVED`
//                              (S-25) is true BY CONSTRUCTION rather than by a gate a reviewer must trust.
//        `team_key_unsaved`  — ★ THE KEY IS LIVE IN RAM AND WILL NOT SURVIVE A REBOOT, said as the two true things
//                              the owner ruled: `TEAM KEY ACTIVE` (S-26) / `NOT SAVED — LOST ON REBOOT` (S-27).
//                              ⛔ NEVER `TEAM KEY RECEIVED` — that word claims durability.
//      ⛔⛔ AND NEITHER MAY EVER CARRY A LABEL. The granter's optional `name=` rides the push and stops there
//         (`lib/core/node.cpp:264-266`); rendering it here would make an unauthenticated, self-asserted string the
//         TEAM's identity on a screen (F-3 / P-5), which is the forbidden USAGE spec §8 S-36 exists to name.
// ★★★★ §UI-16 K5 ADDS THE ELEVENTH AND TWELFTH: ONE REUSES A RULED LEXEME, THE OTHER **HAS ITS OWN, S-39**.
//      ⛔ CORRECTED IN PLACE 2026-08-25 (the owner's S-39 ruling), AND THE WITHDRAWN CLAIM IS KEPT VISIBLE BECAUSE
//      IT WAS NORMATIVE: this block read *"AND ⛔ **NEITHER INVENTS A LEXEME** — spec §8 … rules ⛔ NO result word
//      for what follows it. ⇒ REPORTED, ⛔ NOT INVENTED: both arms END ON A RULED LEXEME THAT IS TRUE OF THE
//      RESULTING STATE"*. ★ THE PROCESS HELD AND IS WHY THE RULING EXISTS — nothing was invented silently, the
//      candidate was reported — but the OUTCOME moved: ⛔ no ruled lexeme was true on every failing arm, so the
//      owner RULED A NEW ONE (**S-39 `KEY NOT INSTALLED`**, 2026-08-25). ⇒ the success arm reuses S-26 and the
//      failure arm has S-39, and ⛔ neither claims an EVENT:
//        `saved_key_used`   — ★ `TEAM KEY ACTIVE` (S-26). The key IS live: `adopt_key` re-derived and accepted the
//                             record, and `commit_active` returned true, so the five-term boot predicate holds.
//                             ⛔ IT DELIBERATELY SHOWS ⛔ NO SECOND ROW: S-27's `NOT SAVED — LOST ON REBOOT` is the
//                             OTHER screen's, and this one is durable. ⛔ NOT `TEAM KEY RECEIVED` (S-25): nothing
//                             was received — the key may well be one this node MINTED and later left behind — and
//                             K4's rule that S-25 appears only on a K3-forwarded push stays intact.
//        `saved_key_failed` — ★ **`KEY NOT INSTALLED`** (spec §8 **S-39**, OWNER-RULED 2026-08-25) plus the
//                             service's own token. It states **THE ACT'S OUTCOME** and ⛔ never the node's key
//                             inventory, which is what makes it true on every failing arm. ⛔ Never `SAVE FAILED`
//                             (only one arm is a failed write) and ⛔ never `JOIN REFUSED` (the join SUCCEEDED —
//                             that is why this screen exists).
//                             ⛔⛔ **CORRECTED IN PLACE 2026-08-25 (QG blocker 1), AND THE WITHDRAWN LEXEME AND ITS
//                             ARGUMENT ARE KEPT VISIBLE BECAUSE BOTH WERE NORMATIVE:** this read *"★ `NO TEAM KEY`
//                             (S-24) … the SAME sentence the STATUS body already prints for exactly this state …
//                             and it is TRUE on every failing arm, because every one of them leaves the node
//                             KEYLESS (the keyring's governance clears)"*. ⛔ **THE PREMISE DIED WITH THE CLEARING
//                             FUNNEL**: the refusals are now SURGICAL (they must be — a stale-target refusal may
//                             not destroy the current team's innocent key), so after one the node may very well
//                             still hold a team key, and `NO TEAM KEY` became a sentence that can be FALSE.
//      ⚠ THE ONE THING A READER MUST NOT MISREAD: `saved_key_used` and `team_key_unsaved` SHARE A HEADLINE, and the
//      screens differ by the two rows K4 added. That is safe in the ONLY direction that matters — the SHORT screen
//      is producible ⛔ ONLY after a committed activation, so it can never be the durable claim on a RAM-only key.
// ★★★★ §UI-16 K6 ADDS THE THIRTEENTH AND FOURTEENTH, AND ⛔ NEITHER REUSES AN EXISTING WORD — a REMOVAL is not a
//      create, a join, a save or an activation, and the panel must say which operation answered (F-4's rule, a
//      fourth time):
//        `key_forgotten`     — ★ `KEY FORGOTTEN` (S-42). Reachable ⛔ ONLY after the keyring's ONE save RETURNED
//                              TRUE: the record is gone, the survivors are compacted and the vacated slot is wiped.
//        `key_forget_failed` — ★ `KEY NOT FORGOTTEN` plus the SERVICE's own token. It states **THE ACT'S OUTCOME**
//                              and ⛔ never the store's inventory, which is what makes it true on all six failing
//                              arms — including `active_key`, where refusing was the CORRECT behaviour and nothing
//                              was written. ⛔ Never `SAVE FAILED` (five arms spend zero writes).
enum class UiProvOutcome : uint8_t {
    none = 0, created, phy_differs, save_failed, refused, joining, adopted, join_refused, team_joined,
    team_key_received, team_key_unsaved, saved_key_used, saved_key_failed, key_forgotten, key_forget_failed
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
    // ★★★★ §UI-16 K5 — *"the team just joined has a key RETAINED in `/mrteams`"*, and it is ⛔ MEANINGFUL ONLY when
    //      `outcome == team_joined`. It is a REPORT, ⛔ never an instruction: nothing is installed by it, and the one
    //      thing it earns is the OFFER screen with `BACK` selected (P-2b — the whole reason K5 is a screen).
    // ⛔ IT IS ANSWERED BY THE **KEYRING**, through the device seam, and ⛔ never inferred from the id, the
    //    fingerprint or a name: the question is *"is there a record for this exact 32-bit team id"*.
    // ⓘ COST, MEASURED (host, `-DMR_N_LAYERS=2`): it lands at offset 2, in the hole between `node_id` and the
    //   4-aligned `team_id`, so `sizeof(UiProvAnswer)` is **UNCHANGED at 16** and ⛔ no landed field moved. The
    //   placement is pinned by `offsetof` in `test/test_firmware_ui_model.cpp`, ⛔ not asserted in prose. ⚠ The
    //   board figure (12, from the 4-byte `reason` pointer) is UNVERIFIED HERE — no board is built by this slice —
    //   but the hole it lands in exists on both ABIs.
    bool          saved_key = false;
    // ★★★★ §UI-16 K6 — *"this refusal was `KEYRING FULL`"*, and it is ⛔ MEANINGFUL ONLY when `outcome == refused`.
    //      It exists so the acknowledgement can land on the SAVED KEYS list — the ruling's *"a `KEYRING FULL` result
    //      does not choose a victim; its acknowledgement enters the saved key list"* — and it earns ⛔ NOTHING ELSE:
    //      ⛔ nothing is deleted, ⛔ no victim is chosen, and ⛔ the create is ⛔ NOT replayed. The operator makes the
    //      separate selection, the separate confirmation, and then retries the create himself.
    // ⛔⛔ IT IS A **TYPED FLAG SET FROM `ProvErr::keyring_full`**, and ⛔ never a comparison of `reason`'s TEXT: a
    //     navigation decision taken by matching a display token is the display-shaped-field class [[B48]] is about,
    //     and `reason` is explicitly *"the SERVICE's own token"* for the operator to read — not for code to switch on.
    // ⓘ COST, MEASURED not assumed (host, `-DMR_N_LAYERS=2`): it lands at offset 3, in the SAME hole between
    //   `node_id`/`saved_key` and the 4-aligned `team_id`, so `sizeof(UiProvAnswer)` is **UNCHANGED at 16** and ⛔ no
    //   landed field moved. The placement is pinned by `offsetof` in `test/test_firmware_ui_model.cpp`.
    bool          keyring_full = false;
    // ⛔ MEANINGFUL ONLY when `outcome == created` or `team_joined` (§UI-16 N3 — the second writer of it, and the
    //    only other outcome the transaction may hand an id to).
    uint32_t      team_id = 0;
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
    // ★★ §UI-16 K6 — THE SAVED-KEY LIST's ONE READ, and it is a METHOD rather than a sixth `UiProvOp` for
    //    `profiles()`'s exact reason: its answer is a RECORD (four `{team_id, active}` rows plus three store facts),
    //    not a verdict, and stuffing it into `UiProvAnswer` would make every create refusal carry a keyring
    //    enumeration it has nothing to do with.
    // ⛔ IT IS CALLED ONCE, ON A TRANSITION (`menu -> saved_keys`, and again on the RESULT's acknowledgement so the
    //    list the operator returns to is REFRESHED), ⛔ never per tick and ⛔ never per page: it reads flash. The
    //    result is held in `UiState` — frozen with the frame — precisely so the renderer never asks.
    // ⛔⛔ AND IT RETURNS **METADATA ONLY**: `mrfw::SavedKeyList` has no key field and may never grow one.
    virtual mrfw::SavedKeyList saved_keys() = 0;
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
// ★★★★ §UI-16 K5 — THE SAVED-KEY OFFER's TITLE AND ITS TWO ACTIONS, both OWNER-RULED (spec §8 S-28 / S-29) and both
//      carried VERBATIM. 15 and 13 of the rail's 19 columns; with the `>` marker the action row is 14.
// ⛔⛔ `FORGET KEY` (S-31) IS **STILL NOT ON THIS SCREEN AND MAY NOT BE ADDED TO IT** — ⛔ CORRECTED IN PLACE
//     2026-08-25 (§UI-16 K6), AND THE WITHDRAWN WORDING IS KEPT VISIBLE BECAUSE ITS SECOND HALF EXPIRED: it read
//     *"the owner named it a FUTURE verb and spec §4-K5 says in as many words that it is not in this spec"*. S-31 is
//     now LIVE (spec §4-K6) and is declared once as `kForgetKeyText` — but it belongs to the **RETENTION** screens,
//     ⛔ not to this OFFER. ★ The rule this line really carries is unchanged and is now sharper: an offer to INSTALL
//     a retained key may never grow an action that DESTROYS one, and the two flows share no screen. Its absence
//     HERE is still a test, ⛔ not a preference.
inline constexpr const char* kSavedKeyTitle = "SAVED KEY FOUND";
// ★★★★ THE ACTIVATION'S REFUSAL WORD — spec §8 **S-39**, ★ **OWNER-RULED 2026-08-25**, declared here ONCE so a
//      re-ruling changes exactly one line. ⓘ IT WAS PROPOSED RATHER THAN INVENTED: §8 ruled S-28/S-29 for the OFFER
//      and no lexeme for what follows it, and ⛔ none of the ruled ones is true on every failing arm (the full check
//      is written at `prov_result_head`'s arm), so §8's own standing rule was applied — the house style, over the
//      SEMANTIC the code establishes — and the candidate was reported for the ruling it has now had.
// ★★★ WHAT MAKES IT SAFE: it states **THE ACT'S OUTCOME** and ⛔ never the node's key inventory. The refusals are
//     SURGICAL — a stale-target refusal must ⛔ not destroy the current team's innocent live key — so "this node has
//     no team key" is a sentence the panel is ⛔ no longer entitled to make here, while "the saved key was not
//     installed" is true on all seven arms. ⓘ The SECOND row carries the service's own token (`not_our_team`,
//     `rejected`, …), so the operator still learns WHICH way it refused.
inline constexpr const char* kSavedKeyFailedText = "KEY NOT INSTALLED";
// The offer's two actions, by IDENTITY (§B66: ⛔ never by position). ★ `BACK` is `prov_confirm_label`'s spelling
// CALLED — ⛔ never re-spelled (the S-9 treatment, one screen over) — so the panel has ONE `BACK`.
// ★★★ THE SAFE ARM IS THE ZERO VALUE AND THEREFORE THE DEFAULT (P-13): `enter_provision` re-establishes
//     `ProvConfirm::back` on every entry, so *"`SAVED KEY FOUND` with `BACK` selected"* is STRUCTURAL rather than
//     remembered — and reaching the act costs `short` THEN `double`.
inline const char* saved_key_label(ProvConfirm a) {
    switch (a) {
        case ProvConfirm::back:    return prov_confirm_label(ProvConfirm::back);
        case ProvConfirm::confirm: return "USE SAVED KEY";
    }
    return "?";
}
// ★★★★ §UI-16 K6 — THE IRREVERSIBLE CONFIRMATION'S TWO ACTIONS, by IDENTITY (§B66: ⛔ never by position). ★ `BACK`
//      is `prov_confirm_label`'s spelling CALLED — ⛔ never re-spelled (the S-9 treatment, a third screen over) —
//      and `FORGET KEY` is the owner's S-31, ACTIVATED 2026-08-25 and declared ONCE (`kForgetKeyText`).
// ★★★ THE SAFE ARM IS THE ZERO VALUE AND THEREFORE THE DEFAULT (P-13): `enter_provision` re-establishes
//     `ProvConfirm::back` on every entry, so *"the confirmation opens with `BACK` selected"* is STRUCTURAL rather
//     than remembered — and reaching an IRREVERSIBLE act costs `short` THEN `double`.
// ⛔⛔ THIS PAIR IS UNREACHABLE FOR AN **ACTIVE** ROW, AND ⛔ NOT BY A CONDITION HERE: the active row lands on its
//     OWN arm (`Provision::saved_keys_active`, `ACTIVE KEY` / `CANNOT FORGET`, S-43), which offers no action to
//     select at all. Hiding the word on a shared screen would leave `ProvConfirm::confirm` one `short` away.
inline const char* forget_key_label(ProvConfirm a) {
    switch (a) {
        case ProvConfirm::back:    return prov_confirm_label(ProvConfirm::back);
        case ProvConfirm::confirm: return kForgetKeyText;
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
        // ★★★ §UI-16 N3 / spec §8 S-10 — **`TEAM JOINED`**, and it is a NEW lexeme by F-4's ruling: ⛔ it may not
        //     reuse `TEAM CREATED` (which is what this switch would have answered without the arm, since a join by
        //     id lands `ProvVerdict::applied` on the same transaction). ⓘ 11 of the rail's 19 columns.
        // ⛔ AND THE THREE FORBIDDEN NEIGHBOURS ARE NOT HERE AND MAY NOT BE ADDED: `JOIN COMPLETE` (S-32) claims an
        //    end-to-end outcome nothing acknowledged, `KEYLESS` (S-33) is the design's own banned word for a member,
        //    and `WAITING FOR KEY` (S-34) is ambiguous between two different secrets. Their ABSENCE is a test.
        case UiProvOutcome::team_joined: return "TEAM JOINED";
        // ★★★ §UI-16 K4 / spec §8 S-25 — **`TEAM KEY RECEIVED`**, 17 of the rail's 19 columns, and it is §3.6.4
        //     point 6's own word carried VERBATIM. ⛔ It is reachable only through a K3-forwarded push, so it can
        //     never appear for a key that is RAM-only. ⛔ `JOIN COMPLETE` (S-32) is NOT its neighbour and may not be
        //     added: nothing acknowledged anything end-to-end here.
        case UiProvOutcome::team_key_received: return "TEAM KEY RECEIVED";
        // ★★★ S-26 — the FAILED-SAVE headline, owner-ruled. ★ IT IS TRUE: `Node::team_key_grant_receive` adopted the
        //     pair into RAM before it ever pushed, so the key genuinely IS active. The panel says both true things —
        //     it works now (this row) and it will not survive a reboot (the two rows below).
        // ★★★★ §UI-16 K5 — THE ACTIVATION'S TWO ENDINGS: the success arm REUSES S-26, the failure arm has its own
        //      owner-ruled **S-39** (see `UiProvOutcome`'s block for the full reasoning, the alternatives refused
        //      and the withdrawn "neither invents a lexeme" claim).
        //   ⛔ `saved_key_used` SHARES S-26's SPELLING BY **FALLING THROUGH**, ⛔ never by a second string literal:
        //      one lexeme, one place, so an owner re-ruling S-26 moves both screens at once (U1 — the rule this
        //      whole switch is built on). ★ The two screens are told apart by the rows BELOW, which is where the
        //      durability claim lives, and only this arm is producible after a COMMITTED activation.
        case UiProvOutcome::saved_key_used:
        case UiProvOutcome::team_key_unsaved:  return "TEAM KEY ACTIVE";
        // ★★★★ ⛔⛔ **CORRECTED IN PLACE 2026-08-25 (QG blocker 1), AND THE WITHDRAWN LEXEME IS KEPT VISIBLE:** this
        //      arm returned **`NO TEAM KEY`** (S-24), on the argument that *"the keyring's governance CLEARS on
        //      every non-installing path, so the node really holds none"*. ⛔ **THAT ARGUMENT DIED WITH THE
        //      CLEARING FUNNEL.** The refusals are now SURGICAL — they must be, or a stale-target refusal would
        //      destroy the CURRENT team's innocent live key — so after a refusal the node may very well still hold
        //      a team key: the one belonging to the team it moved to, or one a serial import installed between the
        //      offer and the press. ⇒ `NO TEAM KEY` became a sentence that can be **FALSE**, which is the class
        //      this project registers.
        // ★★★★ **REPORTED, NOT INVENTED — AND ★ OWNER-RULED 2026-08-25 AS SPEC §8 S-39.** §8 ruled S-28/S-29 for the
        //      OFFER and ⛔ no lexeme for what follows it, and NO ruled lexeme is true on every failing arm (checked
        //      one by one: `NO TEAM KEY` false when the node still holds one · `NOT IN A TEAM` false — we are in a
        //      team, just not that one · `SAVE FAILED` false — most arms spend ZERO writes · `JOIN REFUSED` false —
        //      the join SUCCEEDED, which is why this screen exists at all). ⇒ this is §8's own standing rule applied
        //      (*"where a ruling settles a SEMANTIC and no lexeme, the wording is this cluster's house style applied
        //      to it, one line each, pinned by a native case"*): the word states **THE ACT'S OUTCOME**, ⛔ never the
        //      node's key inventory, so it is true on ALL SEVEN arms and claims nothing about any other key.
        //      ⛔ SILENCE WAS CONSIDERED AND REFUSED: K3's `suppressed` ruling (*"no true sentence ⇒ say nothing"*)
        //      was made for an UNSOLICITED push; here the OPERATOR PRESSED A BUTTON, and a `double` that changes no
        //      pixel is the dead-button complaint C2 exists against. ⓘ 17 of the rail's 19 columns.
        //      ★ ONE PLACE TO RE-RULE IT: `kSavedKeyFailedText`, declared once beside the offer's own two lexemes.
        case UiProvOutcome::saved_key_failed:  return kSavedKeyFailedText;
        // ★★★ §UI-16 K6 / spec §8 S-42 — **`KEY FORGOTTEN`**, owner-ruled, declared once and CALLED. ⛔ It is
        //     reachable ONLY after `TeamKeyringService::forget` returned `forgotten`, i.e. after its ONE save came
        //     back TRUE — so §8 pin 2 (*"no screen claims success before the save returns"*) is structural here, not
        //     remembered. ⓘ 13 of the rail's 19 columns.
        case UiProvOutcome::key_forgotten:     return kKeyForgottenText;
        // ★★★ THE FAILURE — see `kKeyNotForgottenText`'s declaration for the full REPORTED-NOT-INVENTED check
        //     (every ruled lexeme tested against every failing arm) and for why the word names THE ACT'S OUTCOME
        //     rather than the store's inventory.
        case UiProvOutcome::key_forget_failed: return kKeyNotForgottenText;
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
        // ⓘ §UI-16 N3: `team_joined`'s second row is the JOINED TEAM's full id — a VALUE, and this unit owns no
        //   string for it, exactly as `created`'s and `adopted`'s second rows are values. ⛔ It is deliberately NOT
        //   a reassuring sentence about keys: the node is a keyless member and the panel neither says so in the
        //   banned word nor implies the opposite (P-2 / S-33).
        // ★★★ §UI-16 K4 / spec §8 S-27 — THE FIRST HALF of the ruled sentence `NOT SAVED — LOST ON REBOOT`. It is
        //     26 columns against a 19-column body, so it renders across TWO rows — ⛔ exactly as the ruled
        //     `PHY DIFFERS` / `USE SERIAL` pair immediately above does, and for the identical reason (§7.1 rule 5:
        //     the panel may not clip an actionable statement). ⛔ NEITHER HALF MAY BE REWORDED OR CLIPPED, and the
        //     em dash is the ROW BREAK rather than a character: `drawStr` is not UTF-8 aware
        //     (`variants/heltec_common/board_ui.cpp`), so a literal `—` would draw two garbage glyphs. The
        //     precedent is the same one this pair copies — the design writes `PHY DIFFERS — USE SERIAL` and the
        //     panel renders the two halves on two rows with no dash.
        case UiProvOutcome::team_key_unsaved: return "NOT SAVED";
        // ★★ §UI-16 K5 — the SERVICE's own token (`mrfw::saved_key_use_name`), carried exactly as `refused`'s and
        //    `join_refused`'s are: ⛔ never a second SavedKeyUse-to-text table, and ⛔ never key material — the
        //    tokens name FACTS (`rejected`, `binding_failed`, `no_record`, `store_failed`).
        case UiProvOutcome::saved_key_failed: return a.reason;
        // ★★ §UI-16 K6 — the SERVICE's own token (`mrfw::keyring_forget_name`), carried exactly as the three arms
        //    above carry theirs: ⛔ never a second `KeyringForget`-to-text table, and ⛔ never key material — the
        //    tokens name FACTS (`active_key`, `no_record`, `store_failed`, `nv_save_failed`).
        case UiProvOutcome::key_forget_failed: return a.reason;
        // ⛔⛔ AND `key_forgotten` HAS ⛔ NO SECOND ROW: the headline is the whole message. The FULL id was on the
        //     confirmation the operator just pressed through, and re-printing it here would invite a reader to
        //     believe the removal is keyed on what the screen shows rather than on the id the service was handed.
        case UiProvOutcome::key_forgotten:
        // ⛔⛔ AND `saved_key_used` HAS ⛔ NO SECOND ROW, WHICH IS THE HALF THAT KEEPS THE HEADLINE HONEST: S-27's
        //     `NOT SAVED` belongs to the RAM-only screen, and this key is durable. Adding a reassuring sentence here
        //     would be inventing the very lexeme spec §8 declined to rule.
        case UiProvOutcome::saved_key_used:
        // ⓘ `team_key_received`'s second row is deliberately EMPTY: the headline is the whole message, and the only
        //   thing this arm could add is the granter's `name=` — which is exactly what F-3/P-5 forbid.
        case UiProvOutcome::team_key_received:
        case UiProvOutcome::team_joined:
        case UiProvOutcome::joining:
        case UiProvOutcome::adopted:
        case UiProvOutcome::created:
        case UiProvOutcome::none:        return "";
    }
    return "";
}
// ★★★ §UI-16 K4 — THE THIRD ROW, AND IT EXISTS FOR EXACTLY ONE RULED SENTENCE (spec §8 S-27's second half). ⛔ It is
//     NOT a general "detail 2" slot inviting every future outcome to grow one: every other arm answers `""`, the
//     switch is `default`-less so a new outcome must decide here too, and the renderer skips an empty answer.
// ⓘ WHY A THIRD ROW AT ALL, stated so it is not read as sprawl: `TEAM KEY ACTIVE` + `NOT SAVED — LOST ON REBOOT` is
//   TWO ruled sentences, the second of which itself needs two rows — three rows in total, against the two the
//   head/detail pair carries. The alternative was clipping a statement about durability, which §7.1 rule 5 forbids.
inline const char* prov_result_detail2(const UiProvAnswer& a) {
    switch (a.outcome) {
        case UiProvOutcome::team_key_unsaved:  return "LOST ON REBOOT";
        // ⛔⛔ §UI-16 K5 — AND THE TWO NEW ARMS ANSWER `""`, WHICH IS THE POINT RATHER THAN AN OMISSION: this row is
        //     the durability WARNING, and a `USE SAVED KEY` that succeeded is durable while one that failed left no
        //     key at all. Either arm answering `LOST ON REBOOT` would be a false sentence in one direction or the
        //     other. ⓘ The `default`-less switch is what forced this decision to be written down.
        case UiProvOutcome::saved_key_used:
        case UiProvOutcome::saved_key_failed:
        // ⛔⛔ §UI-16 K6 — AND THE TWO REMOVAL ARMS ANSWER `""` TOO, WHICH IS THE POINT: this row is the DURABILITY
        //     warning, and a removal has no durability to warn about in either direction. `LOST ON REBOOT` on a
        //     forget would be a sentence about a key that is gone.
        case UiProvOutcome::key_forgotten:
        case UiProvOutcome::key_forget_failed:
        case UiProvOutcome::team_key_received:
        case UiProvOutcome::phy_differs:
        case UiProvOutcome::save_failed:
        case UiProvOutcome::refused:
        case UiProvOutcome::join_refused:
        case UiProvOutcome::team_joined:
        case UiProvOutcome::joining:
        case UiProvOutcome::adopted:
        case UiProvOutcome::created:
        case UiProvOutcome::none:              return "";
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

// ★★★ [[B232]]'s ONE NEW STRING — the CLOSED view's single entry row (owner's suggested lexeme *"enter settings"*).
// ⚠ IT IS UPPER-CASE BECAUSE THE HOUSE IDIOM FOR AN "ENTERS SOMETHING" ROW IS (`PROVISION`, `CREATE TEAM`,
//   `JOIN NETWORK`, `BACK`); the lower-case labels in `settings_row_label` are the VALUE rows, which this is not.
// ⓘ WIDTH: the row renders as `<marker><label>` on the rail's 19-column body, so 1 + 14 = 15 of 19 (`draw_settings_screen`).
inline constexpr const char* kSettingsEnterText = "ENTER SETTINGS";

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
//
// ★★★★ **RETIRED 2026-08-26 BY §UI-10/11 P3, AND THE WITHDRAWN TABLES ARE KEPT VISIBLE** (the correction idiom this
//      file uses everywhere; the tables themselves are DELETED so there can be no second source of truth, and what
//      survives is the RECORD of what they were and why they stopped being firmware policy):
//
//        inline const char* const kDmTexts[]      = { "Are you OK?",      "I'm OK",   "back, don't send" };
//        inline const char* const kChannelTexts[] = { "Got your message", "All good", "back, don't send" };
//        inline constexpr uint8_t kDmTextCount      = uint8_t(sizeof kDmTexts      / sizeof kDmTexts[0]);
//        inline constexpr uint8_t kChannelTextCount = uint8_t(sizeof kChannelTexts / sizeof kChannelTexts[0]);
//        inline constexpr uint8_t kDmSendableTexts      = uint8_t(kDmTextCount - 1);
//        inline constexpr uint8_t kChannelSendableTexts = uint8_t(kChannelTextCount - 1);
//        inline constexpr const char* kEmergencyText = "I'm in danger";
//
// ⛔⛔ WHY THEY COULD NOT SIMPLY STAY BESIDE THE CATALOG: design §3.2.2 rules that *"the original hard-coded strings
//    become DEFAULTS, not firmware policy"*, and P1 already carries those five spellings as `mrfw::kPresetDefaults`.
//    Two live copies of the wearer's phrases is the U1 rot with a safety edge — the panel would show one and the
//    `ui preset` verbs would edit the other — which is exactly why P1's own block called the duplication *"TEMPORARY,
//    DELIBERATE AND BOUNDED"* and named THIS slice as the one that closes it. ⇒ the defaults live in ONE place
//    (`mrfw::kPresetDefaults`), the live catalog in ONE place (`mrfw::PresetCatalog::live()`), and the panel reads a
//    PROJECTION of that record. ⓘ P1's drift-fence case (`test_firmware_ui_presets.cpp`, which asserted the two
//    spellings byte-for-byte against each other) is DISCHARGED with the duplication it fenced.
// ★ WHAT SURVIVES UNCHANGED, and B66's cure with it: `back, don't send` is still an ACTION, still DERIVED, and still
//   the final row of every compose list — `compose_row_count` derives it from the projected list's own length, so a
//   catalog that grows an enabled slot can no more turn `back` into a SEND than a table that grew a string could.
inline constexpr const char* kComposeBackText = "back, don't send";
// ★★★ §3.2.1's EMPTY STATE, and it is a LEXEME here rather than an `if` in the renderer (§B115: `src/firmware_ui.cpp`
//     is compiled by neither the native suite nor the simulator, so a word decided there is a word no gate can read).
// ⛔ SHORTENED FROM THE DESIGN'S OWN `no presets configured`, AND THE WITHDRAWN SPELLING IS KEPT VISIBLE: that is 21
//    columns and the §CHROME-2 body is **19** (`kBodyCols`), so u8g2 would have clipped it to `no presets configure`.
//    The precedent is `draw_send_screen`'s own (`double = pick a text` -> `double = pick text` when the body narrowed
//    from 21): drop a word, never a suffix, so nothing the operator acts on is lost. ⓘ `set` is the verb that fixes
//    it (`ui preset set …`), which is what makes this shortening say MORE than the original rather than less.
inline constexpr const char* kNoPresetsText = "no presets set";
// ★★★ §2's RULED VISIBLE WORD for the stale-generation refusal (owner-approved 2026-08-25): *"a specific UI result
//     with ZERO core submission, followed by a repaint from the current catalog. ⛔ Never a generic parser failure,
//     never a silent fall-through."* Spelled ONCE, in the pure unit, for `kPresetInvalidLine`'s reason.
// ⓘ 14 columns, inside the 19-column body.
inline constexpr const char* kPresetChangedText = "PRESET CHANGED";

// ================================================================== §UI-10/11 P3 — THE COMPOSE LIST'S FROZEN ROWS
// ★★★★ ONE ROW OF A COMPOSE LIST, AS THE FRAME FREEZES IT. §3.2.3's last paragraph: *"Page-buffer painting likewise
//      freezes one catalog generation for the whole frame so a BLE update between OLED pages cannot tear two versions
//      into one image."* `draw_frame` replays the whole scene once per OLED page over the FROZEN copies, so the row's
//      TEXT must be a COPY here — a pointer into the live catalog would be re-read on page 5 and the panel would show
//      two catalogs in one image. That is the §UI-17 S3 defect class, verbatim, and it must not return.
// ★★★★ `slot` IS THE **STABLE SLOT ID**, ⛔ NEVER THE ROW INDEX (§3.2.2, §B66's cure): *"Gaps are valid: for example
//      `dm1`, `dm4` and `dm8` may be the three visible rows. Therefore every visible row carries its stable slot
//      identifier; code must never derive `dmN` from the current row index."* It is `mrfw`'s record index (0 =
//      emergency, 1..8 = dm1..dm8, 9..16 = channel1..channel8), carried whole so the `SendReq` it seals names the
//      record and nothing else.
struct ComposeSlot {
    // 17 characters + the terminator — `mrnv::kUiPresetTextMax` is OQ-A's owner ruling and is ⛔ never re-typed here.
    char    text[mrnv::kUiPresetTextMax + 1] = {};
    uint8_t slot = 0;        // ★ the STABLE slot id — see the block above
    bool    loc  = false;    // `include_location` — the row's `L` / `-` column (§3.2.2)
};
// The ENABLED slots of ONE kind, in stable-slot order. ⛔ The capacity is `mrfw::kPresetPerKind`, DERIVED from the
// record's own constant and ⛔ never a hand-written 8 (§B66's lesson, one record over).
// ⓘ COST, MEASURED not assumed: `sizeof(ComposeSlot)` is **20** (alignof 1 — 18 char + 2 uint8, no padding), so
//   `sizeof(ComposeList)` is 8 x 20 + 1 = **161** and the pair on `UiSnapshot` costs 322. The `offsetof` proof is in
//   `test_firmware_ui_model.cpp`; ⚠ native alignment hides the BOARD figure (D2).
struct ComposeList {
    ComposeSlot row[mrfw::kPresetPerKind] = {};
    uint8_t     n = 0;                            // how many of `row[]` are real
};
// ★★★ THE PROJECTION, AND IT IS THE ONE PLACE A COMPOSE LIST IS DERIVED FROM THE RECORD (U1/U2 — ⛔ never rebuilt
//     row-by-row at a second site). Three ruled properties, each its own mutation:
//       · ENABLED ONLY — a disabled slot has no row at all (§3.2.2: *"The OLED lists only enabled slots"*);
//       · STABLE-SLOT ORDER, gaps intact — the walk is over the record's own index space, so `dm1`/`dm4`/`dm8`
//         project to rows 0/1/2 carrying slots 1/4/8;
//       · KIND-PURE — *"DM presets never appear in the channel list and channel presets never appear in the DM
//         list"*, asked at `mrfw::preset_kind_of`, the record's own authority (⛔ not a range re-derived here).
// ⛔ THE EMERGENCY SLOT IS NEVER A ROW, on either list: `preset_kind_of(0)` is `emergency` and neither call asks for
//    that kind — it is *"long press only; never appears in a compose list"* (§3.2.2's table).
// ⓘ The text is copied `len` bytes and terminated. A canonical record already zeroes the tail, so the terminator is
//   belt-and-braces against a record that reached here another way — ⛔ never a licence to skip `presets_canonical`.
inline void compose_project(const mrnv::UiPresetBlob& cat, mrfw::PresetKind kind, ComposeList& out) {
    out = ComposeList{};
    for (uint8_t i = 0; i < mrnv::kUiPresets && out.n < mrfw::kPresetPerKind; ++i) {
        const mrnv::UiPresetSlot& s = cat.slot[i];
        if (!s.enabled) continue;                                   // ★ ENABLED ONLY
        if (mrfw::preset_kind_of(i) != kind) continue;              // ★ KIND-PURE
        ComposeSlot& r = out.row[out.n++];
        r.slot = i;
        r.loc  = (s.loc != 0);
        uint8_t n = s.len < mrnv::kUiPresetTextMax ? s.len : mrnv::kUiPresetTextMax;
        for (uint8_t k = 0; k < n; ++k) r.text[k] = s.text[k];
        r.text[n] = '\0';
    }
}
// ★★ §3.2.1's EMPTY STATE, as an ANSWER rather than as a renderer's `if` — `mrfw::preset_boot_line`'s idiom, and its
//    reason: `nullptr` is the third answer and a caller that printed an empty string would draw a blank row.
inline const char* compose_empty_note(const ComposeList& l) { return l.n == 0 ? kNoPresetsText : nullptr; }

// ======================================================= §UI-16 K7 — THE ROSTER GRANT'S ENTRY, AS A COMPOSE ROW
// ★★★★ [[B245]], OWNER-RULED 2026-08-25 (spec §K7, option 1). A member who joined BEFORE the invitation window was
//      opened can never be one of its candidates (N4 pin 2, and that ruling is CORRECT), so the panel had no grant
//      path to them at all. ⇒ an OPERATOR-INITIATED per-member act, and this block is WHERE IT HANGS.
// ★★★★ **WHY THE DM COMPOSE SUB-VIEW AND NOT A NEW SURFACE — DERIVED FROM THE TREE, ⛔ NOT CHOSEN.** In the landed
//      entered-TEAM model (§UI-17 S1) a roster row offers exactly ONE act: the `double` that opens this sub-view
//      with the member's identity FROZEN AT ENTRY (`compose_peer`, and now `compose_grant_hash` beside it). The
//      alternatives were measured against the tree and each one breaks a landed ruling:
//        · a row on the ENTERED TEAM LIST itself — walking onto it RETIRES the pick (`list_note_kind`'s `retire`
//          arm), so the act would be pressed with nothing selected;
//        · a gesture of its own — `short`/`double`/`long` are all spoken for, `long` by the emergency alarm;
//        · a screen of its own — ⛔ forbidden by §K7 in as many words.
//      ⇒ this sub-view IS the per-member act list, and `GRANT KEY` becomes one of its rows.
// ⓘ THE ROW ORDER IS FIXED AND DERIVED: the canned texts keep indices `0 .. sendable-1` (so `SendReq::text_index`
//   is UNCHANGED and every landed compose case is byte-identical), the act sits at `sendable`, and `back` stays
//   LAST. With the act absent the list is exactly today's, index for index.
// ⓘ ★ §UI-10/11 P3, R-1 — **THE ORDER IS UNCHANGED; ITS TWO NOUNS ARE NOT.** ⛔⛔ CORRECTED 2026-08-26 (QG), AND THE
//   WITHDRAWN CLAIM IS THE LINE DIRECTLY ABOVE, KEPT VISIBLE: my first P3 note said *"the sentence above is still
//   exactly true"*, and that was FALSE in one word — the sentence names **`SendReq::text_index`**, which P3
//   **DELETED**. A `SendReq` now carries `{slot, generation}` (see its own block), so nothing about it is
//   "UNCHANGED", and `sendable` is no longer a compile-time count.
// ★ WHAT IS ACTUALLY TRUE, restated so the ORDER claim survives without the two dead nouns: enabled preset rows
//   occupy `0 .. n-1` where **`n` is the PROJECTED LIST's own length** (`ComposeList::n`), `GRANT KEY` sits at `n`,
//   and `back` stays LAST. With the compiled defaults `n` is the SAME 2 the retired tables gave, so the list is
//   index-for-index today's and every landed K7 case is byte-identical (`ui10-p3-r1` pins exactly that equivalence,
//   and pins the position for EVERY `n` in 0..8). ⛔ R-1: the preset rework may not move, gate or re-anchor K7's
//   row semantics — and it does not.
enum class ComposeRow : uint8_t { text = 0, grant, back };

// ★★★ THE OFFER, AND IT **HIDES** RATHER THAN REFUSING — the design decision §K7 asks to be reported either way.
//     Hiding is what the four child rows of PROVISION already do (`provision_rows`: a build or a runtime that
//     cannot perform an operation does not list it), and it is the direction C2 requires: a row that is drawn and
//     then refuses teaches the operator that `GRANT KEY` sometimes does nothing, which is exactly how a real
//     refusal stops being read.
// ⛔ EVERY TERM IS LOAD-BEARING, and each one is a separate mutation:
//   1. `dm` — the CHANNEL compose has no member at all (`compose_peer == 0`), so it can offer no per-member act;
//   2. `can_grant` — the BUILD/RUNTIME predicate, and it is `UiSnapshot::prov_invite` REUSED (U1): the same three
//      terms (`MR_N_LAYERS < 2`, `MR_FEAT_TEAM`, `team_id != 0`) decide whether this node has a grant adapter and
//      a membership to grant into. ⛔ Not a second predicate that could disagree with the INVITE row's;
//   3. `team_key_present` — ★ **A KEYLESS NODE OFFERS NOTHING** (§K7 pin 6). It is `Node::team_channel_key_present()`
//      as the snapshot already publishes it; there is no content key to ship, so there is no act;
//   4. `member_hash32 != 0` — F-7's authoritative floor, the SAME one the invite list applies: a route-only member
//      has no seal target, so it is not grantable (⛔ and never with an invented all-zero identity);
//   5. `member_hash32 != own_hash32` — ★ **NO SELF-GRANT**, asked at the identity `Node::team_key_grant_send`'s own
//      `self` arm asks it at (`target_hash == _key_hash32`), ⛔ never at the mutable team-local id. The core still
//      refuses independently — this row simply never offers the press.
inline bool compose_grant_offered(bool dm, bool can_grant, bool team_key_present,
                                  uint32_t member_hash32, uint32_t own_hash32) {
    if (!dm || !can_grant || !team_key_present) return false;
    if (member_hash32 == 0) return false;
    if (member_hash32 == own_hash32) return false;
    return true;
}

// ★★★ THE ACT'S TARGET, RESOLVED ONCE AND FROZEN — §UI-17 S5's rule (*"one `team_key_of_id` resolution per row,
//     handed to BOTH consumers"*) CONSUMED, ⛔ never repeated. `UiSnapshot::member[]` is that one resolution's
//     second consumer (`build_snapshot` fills it in the SAME loop, from the SAME `hash` the TEAM row was labelled
//     with), so this reads a value the TEAM chain already produced — ⛔ it is not a second authority that could
//     disagree with the row the operator is looking at.
// ⓘ IT IS FOUND BY THE ROW's TEAM-LOCAL ID because that is what §B64 remembers as THE PICK and what the operator's
//   press genuinely pointed at. ⛔⛔ THE ID IS NOT THE TARGET: the HASH it yields is frozen at entry and is what
//   every later screen and the act itself carry (P-7d) — a member that re-runs team-DAD or is renamed between the
//   entry and the confirmation is still granted THE SAME KEY, and the send-time `dst` the core reports covers the
//   id half of that (§UI-16 N6b).
// ⛔ 0 IS THE HONEST ANSWER for an id this snapshot does not carry and for a route-only member: `key_hash32 == 0`
//    means NO AUTHORITATIVE BINDING throughout this cluster (F-7), and `compose_grant_offered` refuses it.
inline uint32_t team_member_hash_of(const InviteMember* mem, uint8_t n, uint8_t id) {
    if (!mem) return 0;
    if (n > kMaxInviteRows) n = kMaxInviteRows;
    for (uint8_t i = 0; i < n; ++i) if (mem[i].id == id) return mem[i].key_hash32;
    return 0;
}

// ★★ THE LIST'S LENGTH AND ITS ROW RESOLVER, AS FUNCTIONS — §B66's rule, applied to the one list that still
//    identified its `back` row by a bare `cursor + 1 == n`. Three call sites ask (the gesture, the renderer and
//    the label below), so a fourth cannot get it wrong.
// ⓘ §UI-10/11 P3 — IT TAKES THE **LIST**, ⛔ not a `bool dm`, and the change is forced rather than chosen: with the
//   catalog configurable there is no compile-time count to ask, and the honest bound is the projection's own `n`.
//   ⛔ THE `bool dm` SIGNATURE IS WITHDRAWN AND KEPT VISIBLE — `compose_row_count(bool dm, bool grant)` returning
//   `(dm ? kDmSendableTexts : kChannelSendableTexts) + grant + 1` — because it is the shape a reader will try to
//   restore, and restoring it means resurrecting the retired tables (a battery entry attacks exactly that).
inline uint8_t compose_row_count(const ComposeList& l, bool grant) {
    return uint8_t(l.n + (grant ? 1u : 0u) + 1u);
}
// ⛔ FAILS CLOSED, exactly as `list_row_kind` does: anything at or past the last offered row names `back`, which
//    leaves and sends nothing — ⛔ never a text row it would then send, and ⛔ never the grant.
inline ComposeRow compose_row_kind(uint8_t idx, const ComposeList& l, bool grant) {
    if (idx < l.n) return ComposeRow::text;
    if (grant && idx == l.n) return ComposeRow::grant;
    return ComposeRow::back;
}
// ★★★ THE ROW'S **STABLE SLOT**, AND THIS IS §B66's CURE ITSELF (§3.2.2: *"code must never derive `dmN` from the
//     current row index"*). ⛔ It answers `mrfw::kPresetEmergency` — a slot that is never a compose row — for
//     anything that is not a text row, so a caller that ignored `compose_row_kind` cannot get a sendable slot out
//     of `back` or out of `GRANT KEY`. ⓘ FAILS CLOSED rather than clamping to the last row (C2).
inline uint8_t compose_row_slot(uint8_t idx, const ComposeList& l) {
    return idx < l.n ? l.row[idx].slot : mrfw::kPresetEmergency;
}
// The row's text, decided HERE and not in the renderer (§B115: `src/firmware_ui.cpp` is compiled by neither the
// native suite nor the simulator, so a renderer-side `if` is a rule no gate in this tree can attack).
// ★ `kInviteGrantKey` is S-17, DECLARED ONCE in `firmware_ui_invite.h` and REUSED verbatim — ⛔ §K7 adds no lexeme.
// ★ §UI-10/11 P3: a TEXT row's words are the PROJECTION's, ⛔ never a table's — that is what "the catalog reaches
//   the panel" means, and resurrecting a fixed table here is a battery entry of its own.
inline const char* compose_row_text(uint8_t idx, const ComposeList& l, bool grant) {
    switch (compose_row_kind(idx, l, grant)) {
        case ComposeRow::text:  return l.row[idx].text;
        case ComposeRow::grant: return kInviteGrantKey;
        case ComposeRow::back:  return kComposeBackText;
    }
    return "";
}
// ★★★★ THE ROW's **LOCATION COLUMN** — OQ-A's owner ruling of 2026-08-25, and its premise is that there are ALWAYS
//      EXACTLY TWO STATES: *"the row ALWAYS shows `L` **or** `-` per the parent design, so BOTH states consume
//      selection marker 1 + location marker 1 + text ⇒ 17 in 19 columns unconditionally"*. That is why
//      `mrnv::kUiPresetTextMax` is 17 and why a conditional bound was WRONG. ⛔ There is no third answer for a
//      PRESET row: a blank column would silently mean `-`, i.e. *"this message carries no coordinates"*, on a row
//      whose flag might say the opposite — the wearer confirms this column as part of the double press.
// ★★ `'\0'` IS THE ANSWER FOR AN **ACTION** ROW, AND IT IS R-1, NOT AN OMISSION: `GRANT KEY` and `back, don't send`
//    are ACTS, not messages — they carry no `include_location` and never will. Giving them a column would (a) put a
//    location claim on a row that transmits no body and (b) shift K7's landed row by one character, which R-1
//    forbids in as many words (*"byte-identical — position, gating, semantics"*). ⇒ an action row's line is exactly
//    what it was before this slice, and `tools/probe_firmware_ui`'s landed `" GRANT KEY"` / `" back, don't send"`
//    checks re-run untouched.
inline char compose_row_loc_marker(uint8_t idx, const ComposeList& l, bool grant) {
    switch (compose_row_kind(idx, l, grant)) {
        case ComposeRow::text:  return l.row[idx].loc ? 'L' : '-';
        case ComposeRow::grant: return '\0';
        case ComposeRow::back:  return '\0';
    }
    return '\0';
}
// ★★★ THE WHOLE ROW, COMPOSED IN THE PURE UNIT (§B115) so the native suite asserts the VISIBLE BYTES — the
//     `emg_attempt_line` / `ui_compose_send_line` discipline. The renderer places the answer; it decides nothing.
// ⓘ The order is the design's: selection marker · `L`/`-` · text. A PRESET row is `>LAre you OK?`; an ACTION row is
//   `>GRANT KEY`, exactly as it has always been.
inline void compose_row_line(char* out, std::size_t cap, uint8_t idx, const ComposeList& l, bool grant,
                             bool selected) {
    if (!out || cap == 0) return;
    const char m = compose_row_loc_marker(idx, l, grant);
    if (m) snprintf(out, cap, "%c%c%s", selected ? '>' : ' ', m, compose_row_text(idx, l, grant));
    else   snprintf(out, cap, "%c%s",   selected ? '>' : ' ', compose_row_text(idx, l, grant));
}

// The model NEVER sends — it ASKS. firmware_ui.cpp drains the request, performs the send and feeds back a typed outcome.
enum class SendKind : uint8_t { emergency = 0, dm, channel_canned };
// ★★★★ §UI-10/11 P3 — **`{slot, generation}` REPLACES ROW-INDEX IDENTITY**, and this is design §3.3's freeze
//      paragraph in a struct: *"A `SendReq` identifies both the enabled stable slot selected by the wearer and the
//      generation they saw; it never stores only the compacted visible-row index. If the slot is disabled or the
//      generation no longer matches at execution, refuse and repaint — never resolve the same row index to newly
//      configured words."*
// ⛔ THE WITHDRAWN MEMBER IS KEPT VISIBLE: `uint8_t text_index` — the compacted VISIBLE-ROW index, whose whole
//    defect is that it means something different the instant the catalog changes. A request sealed on row 1 of
//    `dm1,dm4` and executed against `dm1,dm2,dm4` would have sent the wearer's `dm2` phrase to a person he chose
//    a `dm4` phrase for.
// ★ `generation == 0` MEANS **NOT SEALED**, and it is reserved by construction one record over: the persisted
//   generation starts at 1 and SKIPS ZERO on wrap (`mrfw::preset_generation_next`), so no live catalog can ever
//   carry it. It is the EMERGENCY's value — see `send_gate_of` in firmware_ui_send.h, where an alarm is
//   DELIBERATELY exempt from the stale-generation refusal (R-3/§4.1: an alarm outranks its coordinates, and it
//   outranks a phrase edit too).
struct SendReq {
    SendKind kind       = SendKind::emergency;
    uint8_t  peer_id    = 0;
    uint8_t  slot       = 0;      // ★ the STABLE `/mrui` slot, ⛔ never a row index
    uint32_t generation = 0;      // ★ the generation the wearer SAW; 0 = not sealed (the emergency's)
};

struct TeamRow {
    // ⛔⛔ hops / score_q4 are WRITTEN AND READ BY NOTHING since §UI-17 S4 (spec §1.9 F-1/F-2) — deleting them is
    //    their own refactor slice (C1). ⓘ `hops` LEFT THE ROW by ruling (19 columns hold no sixth field once
    //    distance and direction arrived); `score_q4` was never rendered at all, so design §3.3's promised "signal
    //    quality" has never been on the panel. Both are still FILLED by `build_snapshot`; this line is what stops a
    //    reader from concluding the renderer merely forgot them ([[meshroute-mark-done-vs-missing-in-code]]).
    uint8_t  id = 0; uint32_t last_heard_s = 0; int16_t score_q4 = 0; uint8_t hops = 0;
    char     label[kLabelCap + 1] = {};   // resolved name / 0xhash / bare id, already clamped (spec §3.3)
    // ★★★★ §UI-17 S5 — THE PEER's LAST **AUTHENTICATED** POSITION, AS THE CACHE HANDED IT OVER. Published by
    //      `build_snapshot` from `Node::peer_loc_find` (a `const` read of the §AB4 ring, `node_hashlocate.cpp:432`);
    //      the freshness / distance / bearing DECISIONS are `firmware_ui_geo.h`'s, where the native suite drives
    //      them (§B115). ⛔ NOTHING here or downstream requests, refreshes or transmits a position — rendering TEAM
    //      creates no traffic of any kind (spec §3.4).
    // ⓘ COST, MEASURED BY `offsetof` NOT ASSUMED (the `own_fix` placement rule further down, applied again): the
    //   `bool` lands at **26**, inside the 26..27 pad the array already carried after `label`, so it costs ZERO; the
    //   three 4-byte fields then take 28 / 32 / 36 and `sizeof(TeamRow)` is 28 -> **40**. ⚠ There are
    //   `kMaxTeamRows` (8) of these inside `UiSnapshot`, so that struct measures 616 -> **712**.
    // ★★ WHAT THAT COSTS ON A BOARD, MEASURED BY **ELF INSPECTION AND A CONTROLLED A/B**, ⛔ not by counting
    //    declarations (QG, 2026-08-22): the image holds exactly **ONE** static `UiSnapshot` — `s_frame_snap`,
    //    0x268 -> 0x2c8 — and `s_model` embeds none (0x248, unchanged). ⇒ **+96 B of static RAM** and **+96 B of
    //    TRANSIENT loop-task stack** (the per-tick `build_snapshot` local), which the two ruled envs confirm:
    //    `heltec_v3` RAM 216 684 -> 216 780 and `gateway_heltec` 241 636 -> 241 732, **+96 on both**.
    // ⛔ THE "INSTANTIATED TWICE ON THE OLED ENVS" WORDING IN THE OLDER BLOCKS BELOW IS AN INHERITED ESTIMATE AND
    //    THE ELF DISAGREES WITH IT. Those lines are left as their slices wrote them (the correction idiom), but
    //    ⛔ never re-derive a RAM figure from them — the frame's frozen copy is the ONE static; the "second
    //    instance" is a stack value that exists only during a tick.
    // ⛔ `peer_loc_valid` FALSE MEANS THE OTHER THREE ARE MEANINGLESS, and it covers BOTH "this team id resolves to
    //    no peer hash" and "the cache holds no position for that hash". `(0,0)` here is never a coordinate.
    bool     peer_loc_valid = false;
    // ⛔⛔ `peer_loc_age_s` IS **VERBATIM FROM THE ACCESSOR's OUT-PARAM** — no cast, no clamp, no re-derivation at
    //    the publish site (the `home_confirm_age_ms` precedent below). `0xFFFFFFFF` is the cache's own "I cannot
    //    date this" for a backwards clock (`node_hashlocate.cpp:441`), and it must reach the freshness rule INTACT:
    //    re-deriving it from `now_ms` here would turn an undateable position into a fresh-looking one.
    int32_t  peer_lat_e7 = 0, peer_lon_e7 = 0;   // 1e-7 degrees — the scale the config and the wire already use
    uint32_t peer_loc_age_s = 0;
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
    //   ⓘ ⚠ **THE TWO TOTALS ABOVE ARE HISTORICAL — they are §UI-15 slice 4's own measurement (2026-08-19) and are
    //     kept as written.** The CLAIM they support is still true (these two bools cost nothing), but neither
    //     figure is the current one: `sizeof(UiSnapshot)` is **712** — 608 -> 616 when §UI-17 S3 published
    //     `own_fix` + `own_lat_e7` + `own_lon_e7` for the frame freeze (+8, see that block below), then 616 -> 712
    //     when §UI-17 S5 gave `TeamRow` its four peer-location fields (+12 each, x8 rows) — and `sizeof(UiState)` is
    //     **200** since §UI-15 slices 5-6 (`prov_answer`, then `join_list`). ⛔ Do not read a past slice's
    //     "stays N" as a statement about today's struct — that is how this line went stale.
    bool     prov_create_team = false;   // `MR_N_LAYERS < 2 && MR_FEAT_TEAM` — §3.6.3's primary path
    bool     prov_join_static = false;   // `MR_N_LAYERS < 2`                — §3.6.3's secondary path
    // ★ §UI-16 N2's THIRD child predicate, published from the same one site (U3) and ⛔ never collapsed into
    //   `prov_create_team`: see `provision_rows` for why a coincidence in today's envs is not a rule.
    //   ⓘ COST, MEASURED not assumed: it lands in the padding this run of bools already carried — the slice
    //   reports the `sizeof(UiSnapshot)` figure, which moves only by the `nearby[]` array further down.
    bool     prov_join_team   = false;   // `MR_N_LAYERS < 2 && MR_FEAT_TEAM` — §3.6.4's nearby-join path
    // ★★ §UI-16 N4's FOURTH child predicate, published from the same one site (U3) and ⛔ never collapsed into
    //    `prov_create_team`: alone among the four it carries a RUNTIME term — `config().team_id != 0` — so this
    //    row appears and disappears on a running node (spec §4-N4 pin 12). See `provision_rows`.
    //    ⓘ COST, MEASURED not assumed: it lands in the padding this run of bools already carried (`offsetof`
    //    proof in the slice report), so the flag itself costs ZERO; what this slice adds to the struct is the
    //    member array further down.
    bool     prov_invite      = false;   // `MR_N_LAYERS < 2 && MR_FEAT_TEAM && team_id != 0` — §3.6.4's window
    // ★★★★ §UI-16 K6 — the FIFTH child, published as its OWN predicate for the reason all four above are (see
    //      `provision_rows`): `SAVED KEYS` manages the `/mrteams` KEYRING, which exists only where the team plane
    //      does. ⛔ It carries ⛔ NO runtime term — deliberately, and it is the one place this row differs from
    //      `prov_invite`: a node that has LEFT every team may still hold four retained records, and freeing one is
    //      exactly the dead end K6 exists to open. Gating on `team_id != 0` would hide the screen precisely when it
    //      is needed most.
    // ⓘ COST, MEASURED not assumed: it lands in the padding this run of bools already carries, so the flag costs
    //   ZERO (`offsetof`-proved in the slice report; ⚠ native alignment hides the board figure — D2).
    bool     prov_saved_keys  = false;   // `MR_N_LAYERS < 2 && MR_FEAT_TEAM` — §4-K6's retention screen

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
    //   ⓘ ⚠ **FIGURES QUALIFIED IN PLACE 2026-08-21, AND THE WORDING IS KEPT AS WRITTEN.** The PLACEMENT ARGUMENT is
    //     still LIVE and still correct — `home_confirm_age_ms` is still measured at offset **600** — but the two
    //     TOTALS are §CHROME-3's (2026-08-16): the struct is **616** today, because §UI-17 S3 appended
    //     `own_lat_e7`/`own_lon_e7` past that age for the frame freeze. ⛔⛔ AND THE `616` ON THE LINE ABOVE IS A
    //     **COUNTERFACTUAL FROM THAT ERA** — "what this bool WOULD have cost declared after the age" — which now
    //     collides numerically with today's REAL total by pure coincidence. They are different numbers about
    //     different structs; ⛔ do not read one as evidence for the other.
    //   ⓘ ⚠ **RE-QUALIFIED 2026-08-21 (§UI-17 S5), same rule again:** every ABSOLUTE offset in this block is now
    //     96 bytes further on — `TeamRow` grew 28 -> 40 and `team[]` holds eight of them, ahead of everything here —
    //     so `home_confirm_age_ms` MEASURES **696** and the struct **712**. The PLACEMENT ARGUMENT (this bool is
    //     free in a pad that already existed) is untouched by that shift and is still the reason for the ordering.
    bool     team_key_present = false;
    // ★★★★ §UI-17 — OUR OWN CONFIGURED POSITION, AND IT IS ON THE SNAPSHOT BECAUSE THE FRAME MUST FREEZE IT.
    //      `draw_frame` runs ONCE PER OLED PAGE over the frozen copies (`src/firmware_ui.cpp`'s own rule: *"nothing
    //      read that a later page could see differently, or the image tears across page boundaries"*). S3's first
    //      cut read `g_node.config()` INSIDE `draw_status_screen`, so a `cfg set lat` landing between two of the
    //      eight page replays would have drawn half a coordinate row from the old fix and half from the new one.
    //      ⇒ published ONCE per tick in `build_snapshot`, exactly like every other body input.
    // ⓘ SCOPE, STATED SO IT IS NOT MISREAD AS SCOPE CREEP: these three are S5's `UiSnapshot` fields, PULLED FORWARD
    //   into S3 by QG direction (2026-08-21) for the freeze above, and NOTHING ELSE of S5 came with them.
    //   ✅ CORRECTED IN PLACE 2026-08-21: this note ended *"the PEER-location fields on `TeamRow` … are still S5's,
    //     and nothing here calls `peer_loc_find`"*. S5 has since LANDED — `TeamRow` carries all four (see its own
    //     block) and `build_snapshot` performs the `peer_loc_find` read per shown row. The half that is still true
    //     and still load-bearing: ⛔ NOTHING requests or refreshes a position, here or anywhere in the UI.
    // ★★ `own_fix` IS THE PUBLISHED ANSWER OF THE **ONE** PREDICATE (`mrui::ui_status_have_fix`,
    //    `firmware_ui_status.h`), written at the single site that can see `NodeConfig` — the `team_build` /
    //    `mobile_build` idiom (U3). ⛔ It is NOT a second definition and ⛔ the renderer must not re-derive it: the
    //    core itself refuses a located send when BOTH coordinates are zero, and one surface disagreeing with that
    //    refusal is the whole failure this field exists to prevent.
    // ⓘ COST, MEASURED BY `offsetof` not assumed (the `team_key_present` placement rule above, applied again):
    //   `own_fix` lands at **596**, inside the 596..599 pad that already sat before the 8-aligned
    //   `home_confirm_age_ms` — which does **not** move, still **600** — so the bool costs ZERO. The two `int32_t`
    //   then take **608** and **612** and cost their own 8. ⇒ `sizeof(UiSnapshot)` 608 -> **616**, and 616 needs no
    //   tail pad at alignof 8. ⛔ CORRECTED 2026-08-22 (S5's ELF inspection disproved the premise): ~~"Instantiated
    //   TWICE on the OLED envs plus a per-tick stack local, so that is ~+16 B of static RAM"~~ — the image holds
    //   ONE static `UiSnapshot` (`s_frame_snap`); `s_model` embeds NONE (it takes one as a parameter). ⇒ S3's cost
    //   is **~+8 B static and ~+8 B TRANSIENT loop-task stack** — read the doubling off the IMAGE, never infer it
    //   from the freeze pattern. ⛔ Declared AFTER the age instead, the two int32 would still
    //   cost 8 but the bool would have opened a new hole — which is why the two halves sit where they do.
    //   ⓘ ⚠ **THE FOUR OFFSETS ABOVE ARE S3's OWN MEASUREMENT (2026-08-21) AND ARE KEPT AS WRITTEN**; every one of
    //     them moved +96 when §UI-17 S5 grew `TeamRow` (28 -> 40, x8 rows) ahead of them, so `own_fix` MEASURES
    //     **692** today and the struct **712**. ⛔ The claim this block makes — the bool is free, the two `int32_t`
    //     cost 8 between them — is about RELATIVE placement and is unaffected; ⛔ never read an absolute offset here
    //     as current, re-measure it (the host reveal in spec §6).
    bool     own_fix = false;
    // ★ §UI-16 N2 — HOW MANY OF `nearby[]` BELOW ARE REAL. ⓘ ITS PLACEMENT IS MEASURED, not reasoned: declared
    //   HERE it takes one of the three pad bytes that already sat between `own_fix` and the 8-aligned
    //   `home_confirm_age_ms`, so the count byte costs ZERO. Declared beside the array it would have opened a
    //   fresh 8-byte quantum. (The `node.h` padding-placement rule, applied to `UiSnapshot`.)
    uint8_t  nearby_n = 0;
    // ★★★★ §CHROME-5 — THE DUTY GAUGE'S TWO INPUTS, AND THEY ARE `Node::duty_status()`'s OWN FIELDS, CARRIED
    //      VERBATIM. Authority: `duty_status()` (`lib/core/node_mac.cpp:1716`, `DutyStatus{pct, avail_ms, enabled}`)
    //      — ⛔ NEVER raw `duty_ms` and ⛔ NEVER the separate five-minute anti-spam budget
    //      (`channel_duty_budget_ms`), which answers "may I originate a channel post" rather than "how much of the
    //      rolling-window airtime budget have I spent". Two different questions; one icon may only carry the one it
    //      measures ([[B210]]/[[B214]]: a display-shaped field must never make an airtime decision, and the reverse
    //      reading — an airtime-shaped field drawn as something else — is the same defect from the other side).
    // ⛔ THE CLASSIFICATION IS **NOT** HERE: these are the raw reading, and `ui_duty_bucket` (firmware_ui_chrome.h)
    //    turns them into the picture BEFORE the freeze. The pair is the `batt_mv` -> `batt_dv` shape exactly (U3).
    // ⛔ `avail_ms` IS DELIBERATELY NOT PUBLISHED: the gauge is ICON ONLY (design §3.1's amendment), so the recovery
    //    time is a fact the panel cannot draw — and a fact the panel cannot draw has no business riding the snapshot.
    //    Stated so its absence is a decision, not an oversight ([[meshroute-mark-done-vs-missing-in-code]]).
    // ⓘ FALSE/0 BY DEFAULT, and that is the honest unpublished state rather than a plausible one: `duty_status()`
    //   answers `{0, 0, false}` for a node with no duty limit, which is exactly what these defaults say.
    // ⓘ COST, **MEASURED AND NOT FREE** — and the honest number is stated here rather than a hoped-for zero, because
    //   this is the FIRST application of the `team_key_present` / `own_fix` / `nearby_n` placement rule in this struct
    //   that FINDS NO HOLE. The run of bools ends at `nearby_n` (694/695 native) and `home_confirm_age_ms` is already
    //   8-ALIGNED at 696, so there is nothing to fill: the two bytes push that `uint64_t` to 704 and
    //   `sizeof(UiSnapshot)` measures **1000 -> 1008 (+8)**. ⇒ ~+8 B static (ONE image-wide `UiSnapshot`,
    //   `s_frame_snap`) and ~+8 B of TRANSIENT loop-task stack (`build_snapshot`'s local) — read off the image, ⛔
    //   never doubled from the freeze pattern (§UI-17 S3's standing correction).
    // ⚠ TWO GENUINE HOLES EXIST AND BOTH WERE **DECLINED** (M3: never contort a design to fit a spare byte): 26..27,
    //   between `team_total` and the 4-aligned `team[]`, and 673..675, between `my_team_id` and the 4-aligned
    //   `team_id`. Either placement measures +0 — and either one files the duty gauge's two inputs inside the TEAM
    //   block, where no reader looking for them would find them and where a future edit to the team rows would sit on
    //   top of them. `nearby_n`'s separation from `nearby[]` is the precedent for taking such a hole; it stayed in a
    //   plausible neighbourhood (a count beside counts), and this pair would not.
    // ⓘ Native alignment hides the BOARD figure (D2); the authoritative number is the per-board `RAM_used` diff.
    bool     duty_enabled = false;
    uint8_t  duty_pct     = 0;      // 0..100 of the rolling-window budget; 100 = the node must stay silent
    // ★★★★ `uint64_t`, AND THE TYPE IS THE WHOLE POINT (design §4.2, and the trap this slice was briefed against).
    //     Authority: `Node::mobile_home_confirm_age_ms()`, which is `uint64_t`. ⛔⛔ NEVER a `uint32_t` millisecond
    //     age: that cast re-creates the ~49.7-day wrap this project already fixed once (see node.h's §MH-S4 ledger
    //     line, where a u32 stamp was MEASURED as free and DECLINED under M3 for exactly this reason), and it would
    //     make a four-month-old confirmation render as a fresh one.
    // ⚠ THE SNAPSHOT'S OWN IDIOM INVITES THE BUG: `now_ms` above is `uint32_t` and `last_dm_age_s` is a `uint32_t`
    //   seconds age, so "age = now_ms - confirmed_ms" would be written naturally and would be wrong. ⇒ the age is
    //   carried WHOLE and is bucketed exactly once, in `ui_fmt_home_age` (src/firmware_ui_chrome.h).
    // ⓘ Meaningful only while `home_confirmed_ever` is true; 0 with `!ever` is "never", not "just now".
    // ★★★★ §UI-16 K7 ([[B245]]) — OUR OWN STABLE IDENTITY, published so the roster grant can ask the SELF question
    //      at the identity the CORE answers it at. Authority: `Node::key_hash32()`, and
    //      `Node::team_key_grant_send`'s own `self` arm is `target_hash == _key_hash32` — ⇒ the panel's hide
    //      predicate and the core's refusal are the SAME comparison over the SAME value and cannot drift.
    // ⛔ IT IS ⛔ NOT `my_team_id`. A team-local id is MUTABLE (team-DAD re-runs), it lives in a different
    //   namespace from a key hash (C3), and an act keyed off it would be the display-shaped-field-makes-an-airtime
    //   -decision class this project already registered as [[B48]].
    // ⓘ COST, MEASURED not assumed (host, `offsetof`-proved in `test_firmware_ui_model.cpp`): it costs **ZERO** and
    //   ⛔ MOVES NOTHING — it lands at **700**, inside the alignment pad that already sat between the `bool` run
    //   above and the 8-aligned `home_confirm_age_ms` below, so `sizeof(UiSnapshot)` stays **1008** and every landed
    //   offset (`prov_invite` 689, `prov_saved_keys` 690, `team_key_present` 694) is UNMOVED.
    // ⚠ THE PLACEMENT IS THEREFORE LOAD-BEARING AND WAS MEASURED, ⛔ not reasoned — the FIFTEENTH application of
    //   `node.h`'s padding-placement rule. Its SEMANTIC home is beside `my_team_id`/`team_id`, and measured THERE it
    //   also costs 0 bytes of total — but it shifts the whole `bool` run by 4, so `prov_invite` reads 693 and
    //   `team_key_present` 698, i.e. it moves five landed offsets for nothing. Declared after `member[]` instead it
    //   measures **1016**: the array ends exactly on the struct's 8-boundary, so appending opens a fresh quantum.
    uint32_t my_key_hash32 = 0;
    // ⚠ THE MEMBER BELOW IS 8-ALIGNED AND ITS PAD IS WHAT THE LINE ABOVE SPENDS — see that note before reordering.
    uint64_t home_confirm_age_ms = 0;
    // 1e-7 degrees, the scale `NodeConfig::lat_e7` / the wire already use — carried VERBATIM from the config, ⛔ no
    // cast, no clamp, no re-derivation at the publish site (the `home_confirm_age_ms` precedent directly above).
    // ⓘ Meaningful only while `own_fix` is true; `(0,0)` with `own_fix` false is "no position", ⛔ never the Gulf of
    //   Guinea, which is why the row draws `NO LOCATION` rather than `0.000,0.000`.
    int32_t  own_lat_e7 = 0, own_lon_e7 = 0;
    // ★★★★ §UI-16 N2 — THE NEARBY-TEAM OBSERVATIONS, PROJECTED FROM `Node::team_seen_count()` /
    //      `team_seen_at()` AT THE ONE SITE THAT MAY TOUCH `g_node` (`build_snapshot`), exactly as every
    //      other body input is. ⛔ The renderer asks the node NOTHING: `draw_frame` runs once per OLED page,
    //      so a cache observation landing between two of the eight replays would TEAR the list — the §UI-17
    //      S3 defect class, which must not return.
    // ⛔ THIS ARRAY IS **NOT** WHAT THE SCREEN WALKS. It is the LIVE projection, republished every tick; the
    //    screen walks `UiState::nearby`, the FROZEN copy the model captures ONCE on the `menu -> nearby`
    //    transition (owner ruling R-10: NEARBY is a frozen snapshot per entry, manual refresh only). Reading
    //    this array from the renderer would auto-refresh the list and let a team walking into range insert a
    //    row under the operator's cursor — which is exactly what R-5's structural order exists to prevent.
    // ⓘ COST, MEASURED not assumed: `sizeof(NearbyRow)` is **16** (align 8; `age_ms` at 0, `team_id` at 8,
    //   `snr_q4` at 12, `age_valid` at 14, the NAMED `reserved` at 15) x `kMaxNearbyRows` (8) = **128**, and
    //   `sizeof(UiSnapshot)` moves 712 -> **840**. The array lands at offset 712 — the struct's old END, already
    //   8-aligned — so it opens NO hole, and `nearby_n` above costs ZERO in the pad at 694. ⚠ Native alignment
    //   hides the BOARD figure (D2); the authoritative number is the per-board `RAM_used` diff, which is the
    //   board gate's. ★ AND THE STATIC/STACK SPLIT IS READ OFF THE IMAGE, ⛔ never inferred from the freeze
    //   pattern (spec §6's standing lesson): there is exactly ONE static `UiSnapshot` (`s_frame_snap`) plus the
    //   per-tick `build_snapshot` local, i.e. +128 B static and +128 B of TRANSIENT loop-task stack.
    NearbyRow nearby[kMaxNearbyRows] = {};
    // ★★★★ §UI-16 N4 — THE TEAM's MEMBERS AS THE INVITE WINDOW NEEDS THEM, PROJECTED IN THE **SAME** `rt_team_at`
    //      WALK THAT FILLS `team[]` ABOVE, FROM THE **SAME** SINGLE `team_key_of_id` RESOLUTION (spec §6, U1:
    //      *"one `team_key_of_id` resolution per row … handed to both consumers, ⛔ never two lookups for one
    //      row"*). ⛔ It is NOT a second enumeration and ⛔ NOT a second resolution — it is the second CONSUMER.
    // ⛔ THERE IS NO SECOND COUNT: `team_shown` bounds BOTH arrays, because ONE loop fills both. A `member_n`
    //    beside it would be the second authority §B108 exists to warn about, able to disagree with the very
    //    count its own loop ran to.
    // ⚠⚠ AND THE WINDOW THEREFORE SEES AT MOST `kMaxTeamRows` MEMBERS — the TEAM screen's own bound, INHERITED
    //    rather than chosen, and STATED so it is a known limit rather than a discovery ([[meshroute-mark-done-vs
    //    -missing-in-code]]): on a team larger than eight, the ninth member is invisible to BOTH screens and can
    //    therefore never be offered as a candidate. `team_total` is where the TRUE count is reported (spec §3.3),
    //    and the console's `team`/`peers` verbs remain the complete view. ⛔ Widening this is NOT this slice's:
    //    it moves `TeamRow` x N and is a resource decision of its own (spec §11 sizes the group at 3-10).
    // ⛔ AND IT IS NOT `TeamRow`: that row's `label` is a DISPLAY string with two FORBIDDEN fallbacks for this
    //    screen (`0x<hash>` truncated into six columns is a third spelling of the hash; a bare `id <n>` is not a
    //    name at all). `InviteMember::name` is `peer_name_find`'s answer or `""`, and nothing else (F-15).
    // ⓘ COST, MEASURED not assumed (host, `offsetof`-proved): `sizeof(InviteMember)` is **20** — `key_hash32` at
    //   0, `id` at 4, the 15-byte `name` at 5, no tail hole at alignof 4 — x `kMaxInviteRows` (8) = **160**,
    //   landing at offset **840**, the struct's old 8-aligned END, so it opens NO hole and
    //   `sizeof(UiSnapshot)` moves 840 -> **1000**. ⓘ The slice's other snapshot field, the `prov_invite` bool,
    //   costs **ZERO** — measured by REMOVAL, ⛔ not by argument: with it deleted the struct still measures 1000,
    //   because it lands in the pad that already sat before the 8-aligned `home_confirm_age_ms` at 696.
    // ⚠ Native alignment hides the BOARD figure (D2); the authoritative number is the per-board `RAM_used`
    //   diff. ★ The static/stack split is read off the IMAGE (spec §6's standing lesson): ONE static
    //   `UiSnapshot` (`s_frame_snap`) plus the per-tick `build_snapshot` local ⇒ +160 static and +160 of
    //   TRANSIENT loop-task stack, ⛔ never 2 x 160 of static.
    InviteMember member[kMaxInviteRows] = {};
    // ★★★★ §UI-10/11 P3 — **THE `/mrui` CATALOG, PROJECTED ONCE PER TICK AND FROZEN BY THE FRAME.** Published by
    //      `build_snapshot` from `mrfw::preset_catalog().live()` — the ONE instance the `ui preset` verbs write
    //      (`src/firmware_commands.cpp`), so the panel and the console can never be two opinions about the wearer's
    //      phrases. The renderer asks the catalog NOTHING, for the reason `nearby[]` states two blocks up: a BLE
    //      `ui preset set` landing between two of the eight page replays would TEAR the list, which is design
    //      §3.2.3's own *"page-buffer painting freezes one catalog generation for the whole frame"*.
    // ★★ THE GENERATION RIDES THE SAME SNAPSHOT AND IS THE **EQUALITY TOKEN** (§3.2.3 — compared for equality, never
    //    ordering, which is what makes uint32 wrap harmless). Three consumers, one fact: the compose entry SEALS it
    //    (`UiState::compose_gen`), a `SendReq` SEALS it, and a MOVE is what closes an open selection-phase modal.
    // ⛔ EMPTY / GENERATION 0 BY DEFAULT, and that is the HONEST UNPUBLISHED STATE rather than a PLAUSIBLE one —
    //    the rule every `bool` in this struct states about itself (*"FALSE by default … the opposite default would
    //    offer CREATE TEAM on a gateway"*). ⛔ Defaulting to the COMPILED CATALOG was considered and REFUSED: it is
    //    a perfectly plausible list, so a build that forgot to publish would show two sendable messages and NOTHING
    //    would look wrong — whereas an unpublished snapshot now lands on §3.2.1's visible empty state, which is a
    //    LOUD symptom (C2). ⓘ It is not the "absent record" case either: THAT is a published projection of
    //    `preset_defaults`, which is what `PresetCatalog::begin()` runs and what `build_snapshot` therefore reads.
    // ⓘ COST, MEASURED BY `offsetof` NOT ASSUMED (the `team_key_present` / `own_fix` placement rule, applied again):
    //   `member[]` ends at **1008**, the struct's 8-aligned end, so the `uint32_t` lands at 1008 for free and the two
    //   alignof-1 lists follow at **1012** and **1173**. `sizeof(UiSnapshot)` measures **1008 -> 1336 (+328)**.
    //   ⚠ Declared with the lists FIRST the generation would need its own 4-align pad and the struct still measures
    //   1336 — no worse, but the count-before-the-thing-counted ordering is the legible one (`nearby_n`'s precedent).
    // ⛔⛔ THE FIVE FIGURES ABOVE WERE WRITTEN AS **1000 / 1004 / 1165 / 1000 -> 1328** AND WERE STALE ON ARRIVAL —
    //    CORRECTED 2026-08-26 (QG). They were carried over from the §UI-16 N4-era prose one block up (`sizeof` was
    //    1000 before §CHROME-5 appended the duty gauge's two bytes and pushed the struct to 1008). ★ THE EXECUTABLE
    //    ASSERTIONS WERE RIGHT ALL ALONG and are the authority: `ui10-p3-resources` pins 1008 / 1012 / 1173 / 1336
    //    with `offsetof` and is green. ⇒ when this prose and a case disagree, the CASE is the measurement and the
    //    prose is the drift — which is why the numbers live in a case at all.
    //   ⇒ ~+328 B of static RAM (ONE image-wide `UiSnapshot`, `s_frame_snap`) and ~+328 B of TRANSIENT loop-task
    //   stack (`build_snapshot`'s local) — read off the IMAGE, ⛔ never doubled from the freeze pattern.
    // ⚠⚠ AND THE **BOARD** COST IS **NOT** JUST THIS FIELD'S — D2's warning, MEASURED rather than repeated (QG,
    //    2026-08-26, with the env's own toolchain family `xtensa-esp-elf` GCC 13.2, ILP32). ⛔ CORRECTED IN PLACE,
    //    AND THE WITHDRAWN WORDING IS KEPT VISIBLE BECAUSE IT WAS **WRONG, NOT MERELY INCOMPLETE**: it read *"the
    //    panel TU's static RAM moves +344, not +328"*, which quotes a SYMBOL-SIZE sum as if it were the RAM the
    //    device pays. It is not. ★ THE THREE FIGURES ARE THREE DIFFERENT THINGS, and [[B246]] states them apart:
    //      · **SYMBOL-SIZE growth = +344 B** — the object-level measurement of this TU: `s_frame_snap` **+328**
    //        plus `s_frame_state` **+8** and `s_model` **+8** (`sizeof(UiState)` is **496 on the board**, not the
    //        host's 504, so `UiState::compose_gen` finds NO hole there — see that field's own block);
    //      · **LINKED `heltec_mobile` RAM = 218 564 -> 218 900 = +336 B** — the IMAGE truth, and the only number
    //        that is a device cost;
    //      · ⇒ **8 B of the symbol growth was ABSORBED by existing section/alignment padding** at link time.
    //    ⛔ Never quote the host's +328 as the board figure, and ⛔ never quote the +344 as RAM: one is what the
    //    symbols measure, the other is what the image pays, and only a LINK can tell you the second.
    uint32_t    preset_generation = 0;
    ComposeList preset_dm{};
    ComposeList preset_ch{};
};
// ★ THE DEFAULT SNAPSHOT CARRIES THE COMPILED CATALOG — see the block above. It is a FUNCTION rather than a member
//   initialiser so `UiSnapshot` stays an aggregate (`UiSnapshot s{}` is written all over this tree and in every test),
//   and it is the ONE place the defaults are projected, so `build_snapshot`, the native suite and the probe cannot
//   come to disagree about what an unconfigured device shows (U1/U2).
inline void ui_snapshot_publish_presets(UiSnapshot& s, const mrnv::UiPresetBlob& cat) {
    s.preset_generation = cat.generation;
    compose_project(cat, mrfw::PresetKind::dm,      s.preset_dm);
    compose_project(cat, mrfw::PresetKind::channel, s.preset_ch);
}

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
//   stated reboot/uptime rule first. ★ WITHIN a block the rows are NEWEST-FIRST since [[B231]] — see `publish`, which
//   is the only place that decides presentation order.
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
    // ★★★ [[B231]] — OWNER RULED 2026-08-20: THE NEWEST MESSAGE IS AT THE TOP OF ITS BLOCK, so each ring is copied
    //     in REVERSE. `add()` stores in `pull()`'s order (oldest-first) and the retention is newest-wins, so a ring's
    //     LAST slot is its NEWEST row — which is why the newest message used to render at the BOTTOM. ⛔ The retention
    //     itself is UNCHANGED: which rows survive is `add()`'s business, and only their presentation moved here.
    // ⛔ THE BLOCK ORDER IS UNTOUCHED AND IS NOT WHAT THIS RULES ON — all DM rows, then all channel rows, never
    //    interleaved. The two seq spaces share no clock and spec §6.1 requires a stated reboot/uptime rule before any
    //    interleaving; newest-first WITHIN a block needs no such rule, and this is not a step toward one.
    void publish(UiSnapshot& s, uint16_t total) const {
        uint8_t k = 0;
        for (uint8_t i = _n_dm; i > 0 && k < kMaxInboxRows; --i) s.inbox[k++] = _dm[i - 1];
        for (uint8_t i = _n_ch; i > 0 && k < kMaxInboxRows; --i) s.inbox[k++] = _ch[i - 1];
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
// ★★★★ §UI-10/11 P3 — `preset_changed` IS APPENDED, and it is a state of its own rather than a `failed` with a
//      reason, for the reason §2's ruling gives it a WORD of its own: it means **ZERO CORE SUBMISSION**. Nothing was
//      composed, nothing reached `mrfw::exec_command`, no tracker slot was opened and no airtime was spent — so
//      calling it `failed` would say the send was attempted and did not work, which is the opposite of what
//      happened, and would render `draw_failure_lines`' generic wording that the ruling forbids in as many words
//      (*"⛔ Never a generic parser failure, never a silent fall-through"*).
// ⓘ APPENDED, ⛔ never inserted: `DmState` is compared and switched on in several places and its ORDER is not
//   otherwise meaningful, but appending keeps every landed value stable and makes the diff readable.
enum class DmState   : uint8_t { idle = 0, submitting, waiting_ack, delivered, no_key, not_confirmed, failed, aired_waiting, preset_changed };
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
//   `preset_changed` — ★ §UI-10/11 P3, the channel twin of `DmState::preset_changed`: the sealed `{slot, generation}`
//                   no longer matches the live catalog, so the request was REFUSED WITHOUT SUBMISSION. ⛔ It is not
//                   `failed` (nothing was attempted) and not `blocked` (nothing was throttled) — see the DM block.
enum class ChanState : uint8_t { idle = 0, submitting, waiting, relayed, no_relay, unconfirmed, blocked, failed, aired, preset_changed };
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
    // ★★★★ §UI-16 K7 ([[B245]]) — THE ROSTER GRANT'S TARGET AND ITS ROW, **BOUND AT ENTRY BESIDE `compose_peer`**
    //      and for the SAME reason the line above gives: the roster can reorder, be renamed or re-run team-DAD
    //      under an open sub-view, and any of those would retarget an act that re-read it later.
    // ★★★ THEY ARE **TWO** FIELDS AND ⛔ NOT ONE (§B74 — no arithmetic value is reserved). `compose_grant_hash` is
    //     the member's IDENTITY, whatever it is; `compose_grant_row` is whether the ACT IS OFFERED, which four
    //     independent facts can each veto (`compose_grant_offered`). Folding "we are keyless" and "this member has
    //     no authoritative binding" into a single `hash == 0` would make one mutation break both and would put a
    //     lie about the MEMBER on a field that is really about US.
    // ⓘ COST, MEASURED not assumed (host, `offsetof`-proved): `sizeof(UiState)` moves 496 -> **504**, ONE 8-byte
    //   quantum for five bytes of state, and that is the FLOOR rather than a placement mistake. The `uint32_t`
    //   needs 4-alignment and takes 4..7 (pushing `compose_result` 4 -> 9); the `bool` then takes byte 8, i.e. it
    //   costs NOTHING on top. ⚠ THE PAIR MUST STAY A PAIR, AND THAT WAS MEASURED RATHER THAN ASSUMED: SPLITTING
    //   them — the `bool` moved down beside `dirty`, the hash left here — measures **512**, i.e. the separation
    //   alone costs another quantum. Moving the whole pair down beside `prov_answer` measures 504 too, so the
    //   semantic home is also the cheapest one. ⛔ There is no 4-aligned four-byte hole anywhere in this struct to
    //   hide the hash in for free: the only pad near the head is the THREE bytes before `detail_seq`.
    uint32_t compose_grant_hash = 0;
    // ★★★★ §UI-10/11 P3 — **THE CATALOG GENERATION THE OPEN SUB-VIEW IS SHOWING**, frozen at ENTRY in the same
    //      breath as `compose_peer` and `compose_grant_hash`, and for a THIRD reason on top of theirs: §2's ruled
    //      modal table. *"A successful CHANGED mutation … closes a selection-phase compose without sending"*, while
    //      *"an identical no-op"* and *"a validation/storage failure"* must ⛔ NOT close it. The TRIGGER is the
    //      GENERATION MOVE and nothing else (P2's threading decision — ⛔ no new hook), which delivers all three
    //      rows from ONE fact: P1 stamps the next generation into the record only on a SUCCESSFUL durable write, so
    //      a no-op and a failure leave this comparison equal BY CONSTRUCTION rather than by a rule written here.
    // ⛔ 0 WHILE NO SUB-VIEW IS OPEN, and the value is reserved one record over (the persisted generation starts at
    //    1 and skips zero on wrap), so a closed compose can never accidentally compare equal to a live catalog.
    // ⓘ COST, MEASURED not assumed (host, `offsetof`-proved in `test_firmware_ui_model.cpp`): on the HOST it is
    //   **FREE** — `sizeof(UiState)` stays 504 and `sizeof(UiModel)` stays 928, because it lands in a 4-byte hole
    //   the host's 8-byte pointers had already opened.
    // ⚠⚠ ON THE **BOARD** IT IS NOT FREE, AND THIS IS D2's WARNING PAYING OUT — MEASURED 2026-08-26 (QG) with the
    //    env's own toolchain family (`xtensa-esp-elf` GCC 13.2, ILP32, the heltec_mobile flag set): `sizeof(UiState)`
    //    **496 -> 504 (+8)** and `sizeof(UiModel)` **904 -> 912 (+8)**. There is no hole there, because `UiModel`'s
    //    adapter POINTERS are 4 bytes and the host's are 8. ⇒ this field is carried **TWICE** by the panel TU's
    //    statics — `s_frame_state` and `s_model` are both whole `UiState`-bearing objects — i.e. **+16 of SYMBOL
    //    SIZE** on top of `UiSnapshot`'s +328.
    // ⛔ CORRECTED IN PLACE (QG, 2026-08-26), AND THE WITHDRAWN CLAUSE IS KEPT VISIBLE BECAUSE IT WAS **WRONG**:
    //    it read *"which is the +16 that turns `UiSnapshot`'s +328 into the TU's measured +344"* and stopped
    //    there — inviting +344 to be read as the device's RAM cost. ★ THE THREE FIGURES, per [[B246]]:
    //      · SYMBOL-SIZE growth **+344 B** (328 + 8 + 8) — what the objects measure;
    //      · LINKED `heltec_mobile` RAM **218 564 -> 218 900 = +336 B** — what the image pays;
    //      · ⇒ **8 B absorbed by existing section/alignment padding** at link time.
    //    ⛔ Never report the host figure as the board's — and ⛔ never report the symbol sum as the RAM.
    uint32_t compose_gen        = 0;
    bool     compose_grant_row  = false;
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
    // ★★★ §UI-17 slice 1 — WHETHER THE CURRENT TEAM/INBOX LIST HAS BEEN ENTERED, frozen with everything else because
    //     the renderer needs it: the `>` marker and the `BACK` row exist only while it is `interactive`, and a
    //     renderer that read it live would disagree with the frame it is drawing (§5's freeze contract).
    // ⛔ ONE field for BOTH screens, deliberately: only the CURRENT screen can be entered, and leaving resets it
    //    (`list_view_reset_on_leave`). Two fields would be two authorities that could disagree.
    // ⓘ COST: it lands in this struct's tail padding — the slice REPORTS the measured `sizeof(UiState)`.
    ListView list_view = ListView::passive;
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
    //   ⓘ ⚠ **HISTORICAL FIGURE (§UI-15 slice 5, 2026-08-19), kept as written.** The claim is about THIS slice and
    //     is unchanged — it adds no snapshot field — but 608 is no longer the current total: `sizeof(UiSnapshot)`
    //     is **712** (§UI-17 S3's frame-freeze fields took it to 616; S5's four `TeamRow` peer-location fields, at
    //     eight rows, took it the rest of the way). The one place that figure is maintained is `own_fix`'s own block
    //     above, which carries the `offsetof` proof; ⛔ never re-derive it from a line like this one.
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
    //   ⓘ ⚠ **HISTORICAL FIGURE (§UI-15 slice 6, 2026-08-20), kept as written**, and the same qualification as
    //     `prov_answer`'s directly above: the "this slice adds no snapshot field" claim stands, but the current
    //     `sizeof(UiSnapshot)` is **712** (§UI-17 S3's `own_fix` / `own_lat_e7` / `own_lon_e7`, then S5's four
    //     `TeamRow` peer-location fields x8 rows). ⓘ `sizeof(UiState)` **200** on this line IS still current —
    //     neither S3 nor S5 added anything to `UiState`.
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
    // ★★★★ §UI-16 N2 — THE NEARBY LIST THE SCREEN WALKS, CAPTURED ONCE AND FROZEN UNTIL THE OPERATOR LEAVES.
    //      ⛔ It is NOT `UiSnapshot::nearby`, and the two are deliberately different things: the snapshot
    //      carries the LIVE projection (republished every tick), this carries what the operator ENTERED with.
    //      Owner ruling R-10 — *"NEARBY teams = a frozen snapshot per entry, manual refresh only (leave and
    //      re-enter)"* — cannot be expressed by a per-tick array alone, and R-5's first-observed order stops
    //      meaning anything if a row can appear mid-walk under the cursor.
    // ★ IT IS THE `join_list` DISCIPLINE ONE ARM OVER (U3), with the same "what was shown is what is acted
    //   on" consequence for N3: the id that confirmation will carry is the id in THIS copy.
    // ⓘ It is the OWN-TEAM-FILTERED list (`nearby_capture`) — the filter is the READER's by ruling, so it is
    //   applied exactly once, here, at capture.
    // ⓘ COST, MEASURED not assumed, and ⚠ NATIVE ALIGNMENT HIDES THE BOARD FIGURE (D2's standing warning): on the
    //   host `sizeof(NearbyList)` is 136 (8 x 16 B rows + the count in the array's tail padding) and
    //   `sizeof(UiState)` moves 200 -> **336**, i.e. EXACTLY the carrier and not one byte more.
    // ⚠ ITS DECLARATION ORDER IS LOAD-BEARING AND THAT TOO WAS MEASURED, not reasoned: declared ABOVE the two
    //   `bool`s (beside `join_sel`, where it reads better) it measures **344** — the 8-aligned array pushes
    //   `blanked`/`dirty` past its end and opens a fresh 8-byte tail quantum. Declared here, after them, the bools
    //   keep the slot they already had. The `node.h` padding-placement rule, applied to `UiState`.
    NearbyList  nearby{};
    // ★★★★ §UI-16 N3 — THE TEAM THE CONFIRMATION IS ABOUT, HELD BY **IDENTITY** (§B66, spec §4-N3 pin 2). It is the
    //      `join_sel` discipline one screen over (U3) with the same consequence: what was SHOWN is what is JOINED.
    // ⛔ IT IS THE FULL 32-BIT `team_id` OF THE ROW THE CURSOR WAS ON — ⛔ never the cursor index (the list is
    //    own-team-FILTERED, so an index names a different team on a different node) and ⛔ never re-derived from the
    //    fingerprint the confirmation prints (24 of the 32 bits; [[B48]]'s display-shaped-field class).
    // ⓘ 0 = NOTHING SELECTED, and it needs no companion flag: N1's write gate is `peer_team != 0`, so no observed row
    //   can carry 0, and `run_join_team` refuses a 0 out loud rather than handing it to the transaction (where it
    //   would mean `team 0` — a LEAVE).
    // ⓘ COST, MEASURED not assumed, and ⚠ NATIVE ALIGNMENT HIDES THE BOARD FIGURE (D2's standing warning): on the
    //   host `sizeof(UiState)` moves 336 -> **344**. There is no hole for it anywhere in the struct (`join_sel`,
    //   `join_still`, `blanked` and `dirty` fill 196-199 exactly, and `nearby`'s own tail padding belongs to
    //   `NearbyList`), so the four bytes cost eight to the 8-aligned tail wherever they are declared — measured in
    //   both placements, ⛔ not reasoned. This struct is instantiated TWICE on the OLED envs.
    uint32_t    nearby_sel_id = 0;
    // ★★★★ §UI-16 K5 — THE TEAM THE SAVED-KEY OFFER IS ABOUT, HELD BY **IDENTITY** exactly as `nearby_sel_id` is,
    //      and for a SHARPER reason: this value selects a stored SECRET. It is the full 32-bit id the **TRANSACTION**
    //      reported (`UiProvAnswer::team_id`, i.e. the team the durable write actually joined) — ⛔ never the cursor,
    //      ⛔ never the six-hex fingerprint the screen prints (24 of 32 bits, [[B48]]'s class), and ⛔ never a
    //      name-shaped value (S-36's forbidden usage: there is no team label in this firmware at all).
    // ⛔ IT IS ⛔ NOT `nearby_sel_id` REUSED, although the two are equal on the ordinary path: that field is *"what
    //    the operator picked"* and this one is *"what the transaction joined"*, and the act must be keyed on the
    //    authority that WROTE. ⓘ They can differ only if some future caller reaches the seam another way, which is
    //    precisely when the difference matters.
    // ⓘ 0 = NO OFFER IS OPEN. It is retired by EVERY `enter_provision` (the `prov_answer` rule, one field over) and
    //   re-written immediately after the entry that opens the offer — so no later screen can act on a stale one.
    // ⓘ COST, MEASURED not assumed (host, `-DMR_N_LAYERS=2`): `sizeof(UiState)` **is unchanged at 456** — the four
    //   bytes land in the tail quantum the grant verdict already opened, so the carrier costs ZERO. ⛔ Not reasoned:
    //   the size AND the placement (`offsetof` 340, between `nearby_sel_id` and `invite`) are pinned by
    //   `test/test_firmware_ui_model.cpp`'s `ui16-k5-resources` case. ⚠ Native alignment hides the board figure.
    uint32_t    saved_key_team = 0;
    // ★★★★ §UI-16 K6 — THE RECORD THE IRREVERSIBLE CONFIRMATION IS ABOUT, HELD BY **IDENTITY** exactly as
    //      `nearby_sel_id` and `saved_key_team` are, and for the sharpest reason of the three: this value selects a
    //      stored SECRET **FOR DESTRUCTION**. It is the FULL 32-bit `team_id` of the row the cursor was on — ⛔ never
    //      the cursor index (the list skips a corrupt zero-id record, so an index is not an identity), ⛔ never the
    //      six-hex fingerprint the confirmation prints (24 of 32 bits: 255 other teams share it, which is exactly
    //      what pin 7 drives), and ⛔ never a name (there is no team label in this firmware — S-36).
    // ⓘ 0 = NOTHING SELECTED. It is retired by EVERY `enter_provision` (the `saved_key_team` rule, one field up) and
    //   re-written immediately after the entry that opens the confirmation, so ⛔ no later screen can act on a stale
    //   one — and `run_forget_key` refuses a 0 out loud rather than handing it to the service.
    // ⓘ COST, MEASURED not assumed (host, `-DMR_N_LAYERS=2`) and pinned by `offsetof` in
    //   `test/test_firmware_ui_model.cpp` — ⚠ native alignment hides the board figure (D2).
    uint32_t    forget_team = 0;
    // ★★★★ §UI-16 K6 — THE **FROZEN** SAVED-KEY LIST, held for `join_list`'s reason and read on the same schedule:
    //      the enumeration reaches FLASH, so it is taken ONCE on the transition (and once more when the removal's
    //      verdict is acknowledged, which is what *"returns to the REFRESHED list"* means) — ⛔ never per tick and
    //      ⛔ never per page. Freezing it also means a record cannot appear or vanish under the operator's cursor
    //      between the row he highlighted and the confirmation he is reading (owner ruling R-10's shape, one
    //      feature over).
    // ⛔⛔ IT IS **METADATA ONLY** AND CARRIES ⛔ NO KEY BYTE — see `mrfw::SavedKeyList`, whose shape is what makes
    //     that a property of the type rather than a discipline at the call site.
    // ⓘ It is deliberately NOT retired by `enter_provision`: `saved_keys -> saved_keys_confirm -> saved_keys` is one
    //   visit to one list, exactly as `nearby -> nearby_confirm -> nearby` is, and re-reading flash on a change of
    //   mind would be the auto-refresh both screens refuse.
    mrfw::SavedKeyList saved_keys{};
    // ★★★★ §UI-16 N4 — THE INVITATION WINDOW's WHOLE STATE: the TWO snapshot authorities taken AT OPEN (F-11),
    //      the VOLATILE handled set (F-13) and the FROZEN selection. It is ONE carrier because they share ONE
    //      lifetime — `enter_provision` takes it on the way into the window and DISCARDS it on the way out of
    //      it, so "the handled set is discarded when the window closes" is structural rather than remembered.
    // ⛔ IT IS NOT `UiSnapshot::member`, and the two are deliberately different things: the snapshot carries the
    //    LIVE members (republished every tick — that is what makes the window refresh locally, R-10), this
    //    carries what the window OPENED WITH. Diffing the live list against itself would announce nobody.
    // ⓘ COST, MEASURED not assumed (host): `sizeof(InviteWindow)` is **104** (`offsetof` proof: `hash` 0,
    //   `handled` 32, `id_bits` 64, `sel_hash` 96, `n` 100, `handled_n` 101, `sel_id` 102, `taken` 103 — ⛔ not
    //   one padding byte in it) and `sizeof(UiState)` moves 344 -> **448**, i.e. exactly the carrier.
    // ⚠ ITS DECLARATION ORDER WAS MEASURED TOO, ⛔ not reasoned (the `nearby` placement rule directly above,
    //   which is where this struct learned the lesson): declared HERE — after the two `bool`s, beside the other
    //   frozen selection — it costs its own 104 and not one byte more; declared ABOVE those bools it measures
    //   **456**, because the 4-aligned array pushes `blanked`/`dirty` past its end into a fresh tail quantum.
    //   This struct is instantiated TWICE on the OLED envs (the model's and the frame's frozen copy).
    InviteWindow invite{};
    // ★★★★ §UI-16 N6 — THE GRANT'S VERDICT, AND IT IS A **SEPARATE** CARRIER FROM THE WINDOW ON PURPOSE. The two
    //      have DIFFERENT LIFETIMES: the window dies when the operator leaves it (F-13), while the verdict must
    //      survive precisely that — the act ends the window and the panel must still be able to draw what happened,
    //      to whom (`hash`, P-7c), and to promote it when the TxDone edge lands. Folding it into `InviteWindow`
    //      would have made "the handled set is discarded with the window" and "the verdict survives" one rule, and
    //      they are two.
    // ⓘ COST, MEASURED not assumed (host, `offsetof`-proved in `test_firmware_ui_invite.cpp`):
    //   `sizeof(InviteGrantResult)` is **8** (`hash` 0, `ctr` 4, `dst` 6, `st` 7 — ⛔ not one padding byte) and
    //   `sizeof(UiState)` moves 448 -> **456**: the 4-aligned carrier lands in the tail quantum the `invite` array
    //   already opened, so it costs exactly itself. ⚠ Native alignment hides the board figure (D2's standing
    //   warning), and this struct is instantiated TWICE on the OLED envs.
    InviteGrantResult grant{};
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
            unblank(s.now_ms); emergency_gesture(g, s); _st.dirty = true; return;
        }
        if (_st.blanked) { unblank(s.now_ms); _st.dirty = true; return; }     // the waking press is CONSUMED
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
        if (_st.compose != Compose::none) { compose_gesture(g, s); return; }
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
        // ⚠ §UI-17 S1: `list_follow_screen` runs BEFORE the two `note_*_cursor` calls, and the order is load-bearing —
        //   they are gated on the view, so a press that left TEAM/INBOX must have retired the view before the write
        //   side reads it. (Both would refuse anyway on the `screen` term; stating the order means a later reader
        //   cannot make it ambiguous.)
        if (g == Gesture::short_press)  { advance_or_next(s); list_follow_screen();
                                          note_team_cursor(s); note_inbox_cursor(s);
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
        // ★★★★ §UI-10/11 P3 — **THE PRESET MODAL CLOSE, ON THE TICK.** §3.2.3: *"A preset update while a
        //      selection-phase compose modal is open closes that modal without sending."* A `ui preset set` arrives
        //      over USB or BLE with NO gesture at all, and `on_gesture` returns early for `Gesture::none` — so this
        //      is the path that carries the ordinary case, and `compose_gesture`'s copy of the question covers only
        //      the tick that also carries a press. ⛔ ONE authority (`preset_generation_moved`), two callers.
        // ⛔ IT IS ⛔ NOT A MODAL TIMEOUT and it does not re-open §UI-17 R-1's deleted auto-exit: the trigger is a
        //    DURABLE CHANGE TO THE WEARER'S CATALOG, never the clock. The block below still stands verbatim.
        if (preset_generation_moved(s)) close_compose();
        // ★★★★ §UI-17 S2 — **THERE ARE NO MODAL TIMEOUTS HERE ANY MORE**, and the absence is stated rather than left
        //      as a gap somebody re-fills. §3.3 (owner-ruled 2026-08-20, spec §9 R-1) is that **blanking is a POWER
        //      action**: it may not discard a draft, a detail selection or a compose choice. The panel goes dark, the
        //      interaction and its stable selection stay exactly as the operator left them, and the consumed wake
        //      press (`on_gesture`'s blanked arm) puts the SAME screen back.
        // ⛔ **WITHDRAWN IN PLACE, KEPT VISIBLE** — the two statements that stood here, and the reasoning that put
        //    them there, so nobody re-derives them from first principles a third time.
        // ⚠ EACH IS DELIBERATELY WRAPPED MID-CONDITION so that no single-line pattern can match it: this file is the
        //   mutation battery's `model` target, a pattern is a plain substring over the whole file, and an entry that
        //   silently anchored on a COMMENT would mutate nothing, compile, stay green and be reported as a measured
        //   property. (The same VACUOUS class M27/M28 and M06 were re-anchored for.)
        //      if (_st.compose != Compose::none
        //              && elapsed(s.now_ms, _last_input_ms) >= kBlankMs) {
        //          close_compose();                                         // never outlive attention; sends nothing
        //      }
        //      // ★ §UI-7D slice B, spec §3.5: "ordinary modal timeout returns to INBOX without deleting" — the SAME
        //      //   kBlankMs window the compose sub-view uses (U1), for the same reason: a modal that outlives the
        //      //   user's attention is one whose selected action eventually gets pressed by accident.
        //      if (_st.detail != InboxModal::closed
        //              && elapsed(s.now_ms, _last_input_ms) >= kBlankMs) close_detail();
        //    ⇒ design §3.2.1 (*"A modal that can outlive the user's attention is a modal that eventually sends the
        //    wrong thing"*) and design §3.5 are both SUPERSEDED for these two timers. The trade was put to the owner
        //    with the pocketed-device argument on the table and §3.3 won for BOTH.
        // ★★ WHAT STILL RETIRES A MODAL, and it is a CLOSED LIST — verified against the call sites, not recalled:
        //    an explicit `back` (`compose_gesture`'s last row / `detail_gesture`'s `back` action); a completed
        //    terminal operation (the RESULT phase's acknowledgement, the `gone` screen's, and a SERVED erase); and
        //    the EMERGENCY exception — `long_arm` closes the detail modal and `long_fire` closes compose
        //    (`emergency_gesture`), because a hidden Delete or a selected canned message may not survive under an
        //    alarm overlay.
        // ⓘ "LEAVING THE SCREEN" IS NOT ON THAT LIST, and deliberately so rather than by omission: a modal OWNS the
        //   press (`on_gesture` dispatches to `compose_gesture`/`detail_gesture` before `advance_or_next` is
        //   reached), so no gesture can move `_st.screen` while one is open. The list above is therefore exhaustive.
        // ⛔ AND THE BLANK ITSELF IS UNCONDITIONAL, which is the other half of the ruling: the deadline below neither
        //    knows nor asks whether a modal is open. Making it conditional would keep a safety device's panel LIT for
        //    ever behind an open modal — and, through `ui_allows_sleep` (which requires `blanked`), stop it
        //    light-sleeping at all.
        // ★★★ THE 2 s PAGE ADVANCE, AND IT RIDES THIS TICK ON PURPOSE: `TimerWheel::kCap` is 91 and every id is
        //     allocated, so a `Node` timer was not available — and would have been the wrong layer anyway, since a
        //     display cadence is not protocol time.
        // ★ IT MARKS THE MODEL DIRTY BUT DELIBERATELY DOES NOT TOUCH `_last_input_ms` (spec §3.5): the page turning is
        //   the DEVICE acting, not the user, so it must not postpone the blank. ⇒ a long body cycles for exactly as
        //   long as the inactivity window, and then the panel blanks as it always would.
        //   ⓘ CORRECTED 2026-08-21 (§UI-17 S2, V1): this read *"must not postpone the blank or the modal's own timeout
        //     above"*. There is no modal timeout above any more (§9 R-1) — only the blank is postponed-or-not, and the
        //     rule is unchanged for it. ⛔ THE GATE BELOW MUST NOT COUPLE THE TWO CLOCKS IN THE OTHER DIRECTION EITHER:
        //     suspending the cadence while dark does NOT feed back into `_last_input_ms`, so the blank deadline is
        //     exactly where it was.
        // ★★★★ §UI-17 S2 — **AND IT IS SUSPENDED WHILE THE PANEL IS DARK.** ⛔ MEASURED, not anticipated: with the
        //     modal now RETAINED across a blank (§9 R-1) this cadence kept running on a panel nobody can see, so a
        //     retained modal drifted pages in the dark and the operator woke to a DIFFERENT page than they left —
        //     which is precisely the "preserve the interaction and its stable selection" the ruling is about, lost
        //     through the one clock the ruling did not name.
        // ★ THE PAGE VALUE IS NEVER RESET BY ANY OF THIS — dark or lit, `detail_page` only ever moves by this advance.
        //   The wake restarts the CADENCE (`unblank`), so the first dark-to-lit pass cannot bank the whole dark
        //   interval and turn the page before the operator has seen it.
        // ⓘ The resulting repaint still obeys §5's MAC-idle/page-buffer gate — `FrameGate` is the only thing that
        //   decides a frame may open, and this only asks.
        // ★★★★ §UI-17 S2 — **AND THE BLANK OUTRANKS THE PAGE TURN WHEN BOTH ARE DUE ON THE SAME TICK** (QG-ruled
        //      2026-08-21). ⛔ THE BOUNDARY THIS CLOSES, and it is a real tick on real hardware rather than a
        //      contrived one: `on_tick` runs this advance BEFORE the blank transition below, so a sparse or delayed
        //      tick that crosses BOTH deadlines turned the page and then immediately hid it — the operator woke onto
        //      a page THEY NEVER SAW. That is the same loss §3.3 forbids, arriving through the ordering instead of
        //      through the dark.
        // ★ THE RULE, STATED AS THE CODE READS IT: **the page a blank hides must be the page the operator last saw.**
        //   ⇒ on the crossing tick the panel simply goes dark on the current page; the cadence then restarts at the
        //   wake (`unblank`), so nothing is owed and nothing is banked.
        // ⛔⛔ IT IS THE **ORDER** THAT CHANGES, ⛔ NEVER EITHER DEADLINE. `blank_due` is the SAME predicate the
        //     transition below uses — ONE authority, called twice (U1) — so the blank still fires on exactly the edge
        //     it always did (pin 5), and this gate still does not touch `_last_input_ms`, so a page turn still cannot
        //     postpone the blank (pin 4). The two clocks stay independent in both directions.
        if (!_st.blanked && !blank_due(s) && _st.detail == InboxModal::body && _st.detail_pages > 1 &&
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
        // ★★★★ §UI-16 N4 — THE BOUNDED WINDOW's EXPIRY, and it rides THIS tick for the reason the page cadence
        //      does: it is a DISPLAY deadline, not protocol time, and `TimerWheel::kCap` is fully allocated
        //      anyway. ⛔ It is placed BEFORE the blank transition below so that a tick which crosses BOTH
        //      deadlines closes the window and then goes dark on `WINDOW CLOSED` — the operator wakes to the
        //      truth, ⛔ never to a candidate list five minutes past its own expiry.
        tick_invite(s);
        // ★★★ §B64, AND THE PLACEMENT IS THE POINT: `FrameGate::step` FREEZES the state immediately after this call
        //    (`mr_ui_tick`: on_gesture -> on_tick -> step), so the highlight must already name the remembered teammate
        //    IN THIS SNAPSHOT. Re-anchoring only on a gesture would leave the panel showing `>` beside one teammate
        //    while `activate()` addressed another — the mis-send this ruling closes, arriving from the other side.
        //    ⓘ CORRECTED 2026-08-21 (§UI-17 S2, V1). This line read *"After the auto-exit above, deliberately: a modal
        //      that just closed gets its team cursor back the same tick"* — and the auto-exit it named is DELETED. The
        //      placement is unchanged and still load-bearing for the reason the paragraph above gives (the frame
        //      freezes immediately after this call); it simply no longer has a timeout above it to be "after".
        // ★★ §UI-17 S1 — the SECOND forward to `list_view_reset_on_leave`, and it is here for the reason
        //    `settings_follow_screen`'s own block gives: the frame FREEZES immediately after this call, so an
        //    entered list must already have been retired if the screen has moved on underneath it.
        list_follow_screen();
        sync_team_cursor(s);
        // ★★★★ [[B233]] — THE SERVICED MUTATION'S ONE EXTRA FRAME, and the defect it closes is in the TICK's ORDER
        //      rather than in any single call: `mr_ui_tick` builds the snapshot FIRST, serves the erase MID-TICK
        //      (firmware_ui.cpp:1643 — deliberately, or the press would do nothing for a whole frame), and the frame
        //      that then freezes carries the PRE-ERASE rows *and* consumes `dirty`. The next tick pulls fresh,
        //      tombstone-filtered rows — but on a CLEAN model `FrameGate::step` answers `idle`, so on metal the
        //      deleted row stayed on the panel INDEFINITELY and a `double` on it opened its neighbour.
        // ★ THIS SNAPSHOT IS THE FIRST ONE BUILT AFTER THE STORE CHANGED ⇒ the repaint is owed NOW. Consumed here, so
        //   a latch that re-raised itself cannot repaint at tick rate for ever.
        // ⛔ IT PULLS NOTHING AND RE-READS NOTHING — this unit may not touch `g_node.inbox()` at all. It only ASKS for
        //   a paint, exactly as §CHROME-3's invalidation does; the tick's existing single pull is what supplies the
        //   fresh rows, so the fix costs no extra store traffic (counted in the native suite, not argued).
        // ⓘ WHY THE ARTEFACT WAS NOT UNIVERSAL, measured both ways: deleting the LAST row leaves the fallback cursor
        //   on a predecessor that now sits at a DIFFERENT index, so `sync_inbox_cursor` below moved the highlight and
        //   marked the frame dirty as a SIDE EFFECT — that arm always refreshed. A middle-row delete leaves the
        //   neighbour at the SAME index, nothing moves, and nothing else in this model watches the rows.
        if (_inbox_rows_stale) { _inbox_rows_stale = false; _st.dirty = true; }
        sync_inbox_cursor(s);            // ★ §UI-7D: same placement, same argument — the frozen frame must show the
                                         //   highlight beside the record `activate()` would actually open.
        sync_settings(s);                // ★ §UI-14: same placement, same argument — the frame FREEZES immediately
                                         //   after this call, so the service must already be open when it does.
        if (blank_due(s)) {
            _st.blanked = true; _st.dirty = true;
            // ★★★★ §UI-16 N4 / ✅ OQ-3's CLARIFICATION, AND IT IS THE ONE PLACE THE TWO HALVES DIFFER: **the
            //      WINDOW survives blanking; an UNFINISHED CONFIRMATION does not.** ⇒ the arm falls back to the
            //      LIST here, at the blank itself, so nothing stale is retained in the dark — the operator wakes
            //      onto the candidate list, ⛔ never onto a confirmation whose act is one press away and which
            //      they may not remember opening (§3.6.5 rule 1, the argument `provision_reset_on_leave` makes
            //      for a stale `create_confirm`, arriving through the display clock instead of through a leave).
            // ⛔ THE WINDOW's DEADLINE IS UNTOUCHED BY THIS: `enter_provision(invite)` re-anchors the cursor and
            //    the BACK default and re-takes NOTHING — the five minutes keep running through the dark, which
            //    is what "the window survives the blank" MEANS.
            // ⛔ §UI-16 N6: `invite_result` IS DELIBERATELY NOT IN THIS LIST. It is not an unfinished confirmation
            //    one press from an act — it is a COMPLETED act's verdict, and the operator must wake to it (and to
            //    its `KEY SENT` promotion, which may land while the panel is dark) rather than to a screen that
            //    lost it. The window's expiry cannot overwrite it either — see `provision_is_invite`.
            if (_st.provisioning == Provision::invite_confirm ||
                _st.provisioning == Provision::invite_need_pubkey)
                enter_provision(Provision::invite);
            // ★★★★ §UI-16 K5 — THE SAVED-KEY OFFER IS AN **UNFINISHED CONFIRMATION** AND THEREFORE DOES NOT SURVIVE
            //      THE BLANK EITHER (OQ-3, the same clause the two arms above answer to). ⛔ It is one `double` from
            //      installing a stored SECRET, so waking onto it — on a screen the operator may not remember
            //      opening — is exactly what that rule forbids.
            // ★ IT FALLS BACK TO THE **MENU**, ⛔ not to a list and ⛔ not to the result it was reached from: the
            //   join is finished and its verdict was acknowledged (that acknowledgement is what opened this), so
            //   the honest landing is the one `BACK` itself takes. ⓘ `enter_provision` retires `saved_key_team`
            //   with it, so nothing stale is retained in the dark — the offer must be earned by a fresh join.
            if (_st.provisioning == Provision::saved_key) enter_provision(Provision::menu);
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
    // ★ §UI-16 N5 — one pointer to the two device facts the pure model cannot own: the grant-bar cache read and
    //   the final typed-command forward. The decisions (floor, hash, kind and TEAM plane) all live in
    //   `firmware_ui_invite.h`; the device implementation is deliberately policy-free.
    void attach_invite(IUiInviteDevice& d) { _invite_dev = &d; }
    void on_invite_push(const MESHROUTE_NS::Push& pu) {
        if (_st.provisioning != Provision::invite_wait_pubkey) return;
        if (!invite_peer_key_cached_matches(pu, _st.invite.sel_hash)) return;
        // Re-run the accessor rather than trusting/caching the push's answer: the grant does the same aged read.
        if (!invite_grant_preflight(_invite_dev, _st.invite.sel_hash)) return;
        // The push completes the WAIT, not the operator's next choice. Returning to the list exposes the name that
        // `build_snapshot` reads from the existing cache; a push never supplies a parallel UI name field.
        enter_provision(Provision::invite);
    }
    // ★★★★ §UI-16 N6 — THE GRANT'S OUTCOME PUSH, AND IT ARRIVES THROUGH THE **ONE PURE PUSH ROUTER**
    //      (`mrui::ui_route_send_push`, `src/firmware_ui_send.h`) rather than through a second device call: that
    //      router is already the single place a `send_aired` / `send_failed` is attributed to a UI slot, it is
    //      compiled by the native suite, and it is reached from the ONE device entry point. ⛔ A parallel tap in
    //      `firmware_ui.cpp` would be a second correlation site, i.e. two rules to keep in step.
    // ⛔ IT IS SCOPED TO THE VERDICT SCREEN: once the operator has acknowledged the result there is nothing to
    //    upgrade, and a promotion applied to a discarded verdict would be a claim about a screen nobody is looking
    //    at. The correlation itself (both terms, and the queued-only upgrade) is the PURE unit's.
    // ⓘ Returns whether the push was CLAIMED — diagnostic, exactly as the router's own arms do.
    bool on_invite_grant_push(const MESHROUTE_NS::Push& pu) {
        if (_st.provisioning != Provision::invite_result) return false;
        if (!invite_grant_apply_push(_st.grant, pu)) return false;
        _st.dirty = true;                 // the word on the panel changed — ⛔ an invisible promotion is not one
        return true;
    }
    // ★★★★ §UI-16 K4 — THE GRANT RECEIPT'S NOTE, AND WHAT IT DOES **NOT** DO IS THE SPECIFICATION.
    //      ⛔⛔ **A PUSH NEVER NAVIGATES** (spec §4-K4 pin 3): this writes ONE transient field and raises the repaint
    //         flag. ⛔ No `screen`, no `settings`, no `provisioning` arm, no `cursor`, no `compose`, no `detail`, no
    //         EMERGENCY field — an arrival is not an act by the operator, and a note that moved the panel under his
    //         thumb would be the §UI-7D lesson inverted.
    //      ⛔⛔ **AND IT DOES NOT WAKE** — ⛔ no `unblank`, no `on_msg_wake`. ★ THAT IS A DECISION, ⛔ NOT AN
    //         OMISSION: §UI-17 R-7 scoped the wake to a DM ADDRESSED TO US and a SEALED channel post, and widening it
    //         is a new owner ruling this spec explicitly declined to make (§4-K4). ⇒ v1 leaves a dark panel dark; the
    //         note is there when the operator next looks. `mark_dirty()` is the whole effect on a LIT panel.
    //      ⓘ ITS **ACKNOWLEDGEMENT** IS A DIFFERENT QUESTION AND HAS A DIFFERENT ANSWER since 2026-08-25 (§UI-17
    //        keyrecv, owner-ruled): the operator's press on the SUCCESS note leaves the flow for the STATUS screen
    //        (`team_key_note_ack_landed`). ⛔ That does not weaken one word of the two negatives above — the ARRIVAL
    //        still moves nothing and wakes nothing; only a PRESS chooses a destination. The FAILURE note's press is
    //        unchanged and stays in the flow, where the remedies are.
    // ★ THE SLOT IS `prov_answer`, THE PANEL'S ONE TRANSIENT "what just happened to this node's team" ANSWER — so
    //   this note is retired by every `enter_provision`, exactly as a create/join verdict is, and ⛔ can never sit
    //   under a screen that did not establish it. ⓘ It REPLACES a previous verdict deliberately: `TEAM JOINED`
    //   followed by `TEAM KEY RECEIVED` is the true sequence of two facts about one team, newest last.
    // ⓘ `saved` is the K3 verdict and NOTHING ELSE decides the word (spec §4-K4 pins 1-2). ⛔ There is deliberately
    //   no third arm and no default: two outcomes, two ruled sentences.
    // ⛔⛔ WHAT IS **DONE**, stated in the code because docs rot ([[meshroute-mark-done-vs-missing-in-code]]) —
    //    ✅ **BOTH ARMS ARE NOW WIRED END TO END ON DEVICE**, and the two doors are DIFFERENT doors by ruling:
    //      · ✅ `saved == true`  — `src/fw_main.cpp` -> `mrfw::team_key_grant_persist` -> (only on `saved`)
    //        `mr_ui_on_push` -> `mrui::ui_route_recv_push`'s arm -> here.
    //      · ✅ `saved == false` — the SAME gate's `else` -> `mr_ui_on_team_key_unsaved()` (the EIGHTH hook in
    //        `lib/hal/mr_ui.h`) -> `src/firmware_ui.cpp` -> here. ⛔ CORRECTED IN PLACE 2026-08-25, [[B243]]
    //        CLOSED: this block used to say the failure arm was *"NOT REACHED ON DEVICE YET"* because
    //        `mr_ui_on_push` was the only door the seam declared and F-10 forbids forwarding a failed push through
    //        it — accurate when written, and the gap it named (a failed save SILENT on the panel; ⛔ never a false
    //        `TEAM KEY RECEIVED`) is what the second door closed. Bench §7.5's forced-save-failure step is now
    //        WRITABLE rather than owed.
    //    ★ The two doors converge HERE on purpose (U1): one entry point means the failure path's negatives — no
    //      navigation, no cursor move, no emergency write, ⛔ no wake — are the push path's negatives, not a second
    //      set free to drift from them.
    // ⛔⛔ AND THERE IS A **THIRD** OUTCOME THAT REACHES NEITHER ARM, stated here because its absence is a DECISION
    //    (QG, 2026-08-25): a receipt refused BEFORE the live-key re-check — the pair was wiped, we had left the
    //    team, or the receipt named team 0 — routes to `GrantUiRoute::suppressed` and the panel says NOTHING. ⛔ It
    //    must never arrive here with `saved = false`: `TEAM KEY ACTIVE` would be FALSE, and a panel inventing an
    //    active key is the same defect class as one inventing a durable key. The classification is
    //    `mrfw::grant_ui_route_of`'s; this function only has the two arms for which a true sentence exists.
    // ⓘ `now_ms` IS TAKEN AND DELIBERATELY UNUSED, and the parameter stays for two measured reasons rather than
    //   symmetry: every other model entry point on this seam takes the clock, and — the load-bearing half — the two
    //   things this function must NOT do (`unblank(now_ms)`, a retained deadline) are the ones that WOULD need it,
    //   so a control that adds one has something to compile against. ⛔ Dropping the parameter would make the
    //   "it does not wake" mutation UNUSABLE rather than RED, which is coverage lost to a tidier signature.
    // ★★★★ §UI-16 K6 — THE NEW PARAMETER IS A **LANDING**, ⛔ NEVER A WORD (see the assignment below).
    // ⛔⛔ IT SITS **BEFORE** `now_ms` AND HAS ⛔ **NO DEFAULT**, AND THAT IS A MEASURED CORRECTION RATHER THAN A
    //     PREFERENCE. The first cut put it LAST with `= false`, so the SAVED arm's landed call site could stay
    //     byte-identical — and within minutes the device forward was written `(false, keyring_full, now)`, which
    //     COMPILES: `bool`->`uint32_t` and `uint32_t`->`bool` both convert silently, so every unsaved receipt
    //     carried `keyring_full = (now != 0)` = **true** and acknowledged into the removal list. ⓘ `tools/
    //     probe_firmware_ui` caught it (twenty cascading panel failures from P15f on); ⛔ no native case could —
    //     the model ignores the clock. ⇒ THE TRAP IS REMOVED BY CONSTRUCTION: three arguments, no default, so a
    //     stale two-argument call is a **BUILD FAILURE** and every caller must state the fact.
    void on_team_key_note(bool saved, bool keyring_full, uint32_t now_ms) {
        (void)now_ms;
        // ⛔ THE SLOT IS COMPOSED FRESH, ⛔ never patched in place: a note carries NO id, NO node id and NO reason,
        //    and a previous verdict's fields surviving under this headline is one screen showing two acts' data.
        UiProvAnswer note{};
        note.outcome = saved ? UiProvOutcome::team_key_received : UiProvOutcome::team_key_unsaved;
        // ★★★★ §UI-16 K6 — THE **ONLY** THING THE NEW FACT EARNS, AND IT IS NOT A PIXEL: it selects where the
        //      acknowledging PRESS lands (spec §K6 `:987` — a `KEYRING FULL` result of EITHER origin enters the
        //      saved-key list). ⛔ Not one of the three ruled rows moves: `TEAM KEY ACTIVE` / `NOT SAVED` /
        //      `LOST ON REBOOT` (S-26/S-27) are three TRUE sentences about a receipt whose key IS live in RAM and
        //      WILL be gone after a reboot, and they are rendered from `outcome` alone, exactly as before.
        // ⛔⛔ IT IS REFUSED ON THE `saved` ARM **BY CONSTRUCTION**, ⛔ not by a discipline at the call sites: a
        //     receipt that PERSISTED has no dead end to send anyone to, and a `TEAM KEY RECEIVED` screen whose
        //     acknowledgement opened a removal list would be the "success that isn't" from the other side.
        note.keyring_full = !saved && keyring_full;
        _st.prov_answer = note;
        // ⛔⛔ `team_id` IS DELIBERATELY LEFT AT 0 AND THE RENDERER MUST NOT SHOW ONE. The id rows belong to
        //    `created` / `team_joined`, whose id came from an operator's own selection; a push's id came off the
        //    air. ⓘ And the granter's `name=` is not carried AT ALL — F-3/P-5: an advertiser's self-asserted string
        //    may never occupy a team's identity on a screen (spec §8 S-36, the forbidden USAGE).
        _st.dirty = true;                      // a repaint is owed; ⛔ a wake is not (see above)
    }
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
                             provision_has_child(s.prov_create_team, s.prov_join_static, s.prov_join_team,
                                                 s.prov_invite, s.prov_saved_keys));
    }
    // ★ §UI-15 slice 4 — the same rule one level down (U1/U2): ONE construction of the PROVISION menu's children,
    //   shared by the cursor bound, the activation and (slice 5) the renderer. ⛔ Never rebuild it at a call site.
    ProvRowList provision_row_list(const UiSnapshot& s) const {
        return provision_rows(s.prov_create_team, s.prov_join_static, s.prov_join_team, s.prov_invite,
                              s.prov_saved_keys);
    }
    void clear_dirty() { _st.dirty = false; }
    // ★★ §B108: AN ARRIVAL IS A REASON TO REPAINT. `mr_ui_on_push` moved the unread counters and the recency stamps
    // and then asked for nothing, so a new message sat unshown until some UNRELATED gesture or timer happened to
    // invalidate the panel. The counts ride the STATUS BAR, which every screen draws, so this is not Inbox-specific.
    void mark_dirty() { _st.dirty = true; }
    // ★★★★ §UI-17 S8 (owner-ruled 2026-08-20, spec §9 R-6/R-7) — **A RECEIVED MESSAGE LIGHTS THE PANEL.** THE ONE
    //      ENTRY POINT, and the scope decision is ⛔ NOT here: `mrui::ui_route_recv_push` calls this from its
    //      `msg_recv` arm unconditionally and from its `channel_recv` arm ONLY when `pu.enc` — a DM is addressed to
    //      us either way, a CLEARTEXT channel post must NOT wake (§8.15's *"a stranger's post does not light a dark
    //      panel"* survives BY CONSTRUCTION, R-7). This function is the EFFECT; the router owns the RULE.
    // ★★★ IT USES A **SEPARATE** DEADLINE AND ⛔ NEVER WRITES `_last_input_ms`. That field is written only by a real
    //      gesture and the first-tick seed, and a push postponing it would postpone whatever else it ever comes to
    //      drive — which is exactly what the ruling forbids. ⇒ `_msg_wake_until_ms` + `wake_active()`, read beside
    //      `hold_active()` in `blank_due`, so S2 and S8 are ORDER-INDEPENDENT: the two modal deadlines behave
    //      identically whether or not S2 has landed. **The window is `kBlankMs` measured from the MESSAGE's own
    //      arrival** (the ruling's *"the standard blank timeout re-applies after the wake"*); ⛔ no second constant.
    // ⓘ WHY NOT `on_reply`'s trick of relying on an existing deadline: `on_reply` needs none because
    //   `kEmgHoldMs > kBlankMs` keeps the panel lit through `hold_active`. A plain message has no hold, so clearing
    //   `blanked` alone would blank again on the very next tick — a one-frame flash. Stated because that is the
    //   tempting one-line version of this slice.
    // ⛔⛔ IT NAVIGATES NOTHING (spec pin 6, the [[B233]] class): no screen, no cursor, no list view, no selection, no
    //     `*_pick_gone`, no modal, no transient note — the wake lights **the CURRENT screen**, whatever it is, which
    //     is what makes it safe to combine with §3.3's retention. ⛔ AND IT WRITES NO EMERGENCY FIELD (pin 7): if an
    //     alarm state is up the panel lights showing the alarm, which is the safety-first answer.
    // ★★ THE UNBLANK GOES THROUGH `unblank` (U1) — §UI-17 S2 made that **THE ONE WAY OUT OF `blanked`** precisely so
    //    the display cadence restarts with the light: without the restart `_detail_page_at_ms` is still the pre-blank
    //    stamp and the waking pass ITSELF turns the page of a retained modal before a single frame has shown the
    //    operator what they came back to. A message wake owes that exactly as a press and a reply do ([[B223]]: a
    //    guard installed only where the defect was first seen is not a guard).
    // ⛔ A WAKE ON AN ALREADY-LIT PANEL MOVES **ONLY THE DEADLINE** (pin 10): no repeat power command (the board
    //   latches it and the frame gate is the only thing that talks to the panel), and ⛔ no cadence restart either —
    //   a reader mid-page is not interrupted by traffic. `dirty` is the ROUTER's, on every arm, unchanged (pin 5).
    void on_msg_wake(uint32_t now_ms) {
        _msg_wake_until_ms = now_ms + kBlankMs;
        _msg_wake_armed = true;
        if (_st.blanked) { unblank(now_ms); _st.dirty = true; }
    }
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
        // ⓘ §UI-10/11 P3 — the alarm's request names `mrfw::kPresetEmergency` and seals generation 0 ("not sealed").
        //   ⛔ THE ZERO IS LOAD-BEARING: `send_gate_of` exempts the emergency kind, and a sealed generation on this
        //   path would be a way for a phrase edit to refuse a distress call (R-3/§4.1 rule the opposite).
        if (_emg_req_pending) { _emg_req_pending = false;
                                out = SendReq{SendKind::emergency, 0, mrfw::kPresetEmergency, 0}; return true; }
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
                // ★★★ [[B233]] — THE STORE JUST CHANGED UNDER A SNAPSHOT THAT IS ALREADY BUILT. `close_detail()`
                //     below marks the model dirty, but the frame that consumes that `dirty` freezes THIS TICK's
                //     rows — pulled before the erase ran. ⇒ ONE MORE repaint is owed, from the NEXT tick's fresh
                //     pull. ⛔ Raised ONLY on `erased`: `not_found` and `io_error` changed nothing in the store, so
                //     the rows they were rendered from are still the truth.
                _inbox_rows_stale = true;
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
    // ⓘ §UI-17 S2 2026-08-21 (V1): the 15 s auto-exit that made it arrive FIRST is DELETED (§9 R-1). B113's defect and
    //   this arm's necessity are unchanged — the paragraph above is the historical record of why the arm exists.
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
    // ★★★★ §UI-10/11 P3 — **THE STALE-GENERATION REFUSAL REACHES THE PANEL**, and it is its OWN entry point rather
    //      than a `RefuseReason` because it describes something no refusal reason can: ⛔ NOTHING WAS SUBMITTED. No
    //      line was composed, `mrfw::exec_command` was never called, no tracker slot was opened and no airtime was
    //      spent — so `on_send_refused`'s `failed` states would tell the wearer his message was attempted and did
    //      not work. §2's ruling: *"a specific UI result with ZERO core submission, followed by a repaint from the
    //      current catalog. ⛔ Never a generic parser failure, never a silent fall-through."*
    // ⛔ THERE IS NO EMERGENCY ARM, AND ITS ABSENCE IS THE RULING (R-3/§4.1 — an alarm outranks its coordinates and
    //    outranks a phrase edit): `send_gate_of` exempts `SendKind::emergency` outright, so no alarm can ever reach
    //    this function. The `else` below therefore covers the CANNED-CHANNEL kind only, and an emergency arriving
    //    here would be a caller defect — which is why the emergency's own states are left untouched rather than
    //    quietly mapped onto a channel state that would move a live alarm (§2.1's crossover).
    // ★ THE REPAINT IS `_st.dirty` PLUS THE PROJECTION ITSELF: the compose list is re-projected from the LIVE
    //   catalog every tick, so "repaint from the current catalog" is a property of the freeze path rather than an
    //   extra step here (U2 — one publication path, ⛔ never a second refresh hook).
    void on_preset_changed(SendKind k, uint32_t now_ms) {
        (void)now_ms;   // no deadline: the display window is the operator's acknowledgement (§UI-17 R-1)
        if (k == SendKind::dm)                   _dm   = DmState::preset_changed;
        else if (k == SendKind::channel_canned)  _chan = ChanState::preset_changed;
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
        // ⓘ CORRECTED 2026-08-21 (§UI-17 S2, V1): this read *"no deadline here: the sub-view's own kBlankMs auto-exit
        //   is the display window (spec §3.2.1)"*. There is no auto-exit any more (§9 R-1) — the display window is now
        //   the operator's own acknowledgement. The absence of a deadline HERE is unchanged and is still the point.
        (void)now_ms;
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
                // ★ §UI-10/11 P3: a request REFUSED before submission holds no handle at all, so no `send_aired`
                //   can correlate to it — and if one did it would belong to an older transaction. ⛔ Terminal.
                case DmState::preset_changed: return;
            }
            return;
        }
        switch (_chan) {
            case ChanState::waiting: _chan = ChanState::aired; _st.dirty = true; return;               // queued -> aired
            case ChanState::aired:   return;                                                           // idempotent
            case ChanState::idle: case ChanState::submitting: return;                                  // a newer transaction owns the panel
            case ChanState::relayed: case ChanState::no_relay: case ChanState::unconfirmed:
            case ChanState::blocked: case ChanState::failed: return;                                   // ⛔ terminal: refuse
            case ChanState::preset_changed: return;                                                    // ★ §UI-10/11 P3 — see the DM arm
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
        _emg = Emergency::firing; queue(SendKind::emergency, 0, mrfw::kPresetEmergency, 0); _st.dirty = true;
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
        // ★★ §UI-17 S2: routed through `unblank` (U1) rather than writing the flag. A reply lights the panel over a
        //   RETAINED detail modal exactly as a press does, so it owes the same cadence restart — a guard installed
        //   only where the defect was first seen is not a guard ([[B223]]). ⛔ It still does not touch
        //   `_last_input_ms`; only the display clock moves.
        unblank(now_ms);
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
    // ⓘ §UI-10/11 P3 — `slot` is the STABLE `/mrui` slot and `gen` the generation the wearer SAW (⛔ withdrawn:
    //   `uint8_t idx`, the compacted visible-row index — see `SendReq`). The EMERGENCY arm takes neither: its slot
    //   is `mrfw::kPresetEmergency` by definition and it seals NO generation, because an alarm may never be refused
    //   by a phrase edit (R-3/§4.1).
    void queue(SendKind k, uint8_t peer, uint8_t slot, uint32_t gen) {
        // ★★ §B115: THE ORDINAL'S ONE WRITE POINT ON THE REQUEST SIDE, and `queue()` is chosen because it is the ONE
        // choke point all three alarm requests go through — `long_fire`, `on_outcome`'s bounded retry and
        // `tick_emergency`'s blocked retry. A new attempt is requested and `_tries` has not counted it yet, so the
        // ordinal is `_tries + 1` from here until `on_send_accepted` (or the §B84 expiry that stands in for it) counts
        // it. ⛔ Do not also reset it in `long_fire`: `long_fire` ends by calling this, and a second writer is how the
        // two numbers drift apart again.
        if (k == SendKind::emergency) { _emg_req_pending = true; _emg_attempt_counted = false; return; }   // its own slot; never overwritten
        _req = {k, peer, slot, gen}; _req_pending = true;
    }

    // ★ Spec §4.3: every retained emergency state refreshes the `kEmgHoldMs` panel-on DEADLINE — long_fire, then
    // blocked / picked_up / not_heard / reply, and (§B78) `failed`. Anchoring it only at long_fire (an earlier draft)
    // meant an outcome or a reply arriving a whole window later inherited the leftover time and the panel blanked
    // seconds after the news arrived.
    void retain(uint32_t now_ms) { _emg_hold_until_ms = now_ms + kEmgHoldMs; ++_emg_news; }

    UiState  _st{};
    uint32_t _last_input_ms = 0;
    // ★★ §UI-17 S8 — THE MESSAGE WAKE's OWN DEADLINE, beside the panel's other display clock and ⛔ deliberately NOT
    //    inside it: `_last_input_ms` means "the operator acted" and a push is not the operator (see `on_msg_wake`).
    uint32_t _msg_wake_until_ms = 0;
    // ★★ §UI-16 N4 — THE INVITATION WINDOW's OWN DEADLINE, beside the panel's other two clocks and ⛔ deliberately
    //    not inside either: it is neither an operator action (`_last_input_ms`) nor an arrival (`_msg_wake_*`),
    //    and it must NOT keep the panel awake. Armed by `load_invite` at the OPEN, read by `window_active`.
    // ⓘ IT NEEDS NO ARMED FLAG, unlike `_msg_wake_armed`: `window_active`'s clearing term is the ARM, and a
    //   deadline is never consulted outside the two screens the open established (see that predicate's note).
    // ⓘ COST, MEASURED not assumed, and ⛔ the measurement DISAGREES with the easy guess: `sizeof(UiModel)`
    //   856 -> **864** on the host — the `uint32_t` takes the struct's whole 8-byte tail step, exactly as §UI-17
    //   S8's `bool _msg_wake_armed` did (that slice measured the opposite pairing: its `uint32_t` was free and
    //   its `bool` cost 8). ⚠ MEASURED IN TWO PLACEMENTS, ⛔ not reasoned: declared here beside the other two
    //   display clocks, and declared inside the emergency block beside `_last_countdown` — **864 both ways**, so
    //   there is no hole anywhere in this struct for it to land in and the legible placement is free of charge.
    //   D2's warning applies for the board figure, which is the per-board `RAM_used` diff's.
    uint32_t _invite_until_ms = 0;
    bool     _seeded = false;            // B65: _last_input_ms is meaningless until the first tick/gesture
    // ★★★ §UI-17 S8 — **THE ARMED FLAG IS LOAD-BEARING, NOT FASTIDIOUS** (§B74's discipline, and here it is a real
    //     defect rather than a principle): a wrap-safe "now < deadline" reads the initial 0 as a deadline **24.8 days
    //     IN THE FUTURE** for every `now_ms > 2^31`. ⇒ without this flag a node that has simply been up for four
    //     weeks and has received NOTHING would report a live wake for the next 24.8 days: the panel would never
    //     blank, and — through `ui_allows_sleep`, which requires `blanked` — it would never light-sleep again. That
    //     is spec pin 11 (*"quiet node: sleep unaffected"*) failing on the one node the ruling promises to leave
    //     alone. ⛔ NO ARITHMETIC VALUE IS RESERVED instead; this is `_retry_armed`'s shape and its argument.
    // ⚠ IT IS ONLY HALF THE GUARD, AND THE OTHER HALF IS IN `wake_active`: this flag never clears, so it says nothing
    //   about a deadline that has EXPIRED — see the BOUND there, and the measured defect it closes.
    // ⓘ MEASURED, NOT ASSUMED (spec §6 expected `+4`): the `uint32_t` above costs **ZERO** — it lands in existing
    //   padding — and this `bool` costs the struct's whole 8-byte tail step on the host (`sizeof(UiModel)` 600 -> 608
    //   with both; 600 with the deadline alone). D2's warning applies in the usual direction for the board figure.
    bool     _msg_wake_armed = false;
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
    // ★★★★ [[B233]] — "THE ROWS I WAS SHOWN ARE OLDER THAN THE STORE". Raised by a serviced MUTATION, consumed by the
    //     next `on_tick`, and it is a ONE-SHOT LATCH rather than a counter or a timestamp for the reason §B74 gives:
    //     no arithmetic value is reserved to mean "no repaint owed", the flag IS the predicate. See `on_tick`.
    bool      _inbox_rows_stale = false;
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
    // §UI-16 N5's device forward; like `_prov`, attached once and never owned by the model.
    // ⓘ COST, MEASURED on the host: this pointer moves `sizeof(UiModel)` **864 -> 872** and changes none of
    //   `UiState` (448), `UiSnapshot` (1008) or `InviteWindow` (104). It is 4 B on the 32-bit board targets; the
    //   stateless device object's vptr is another 4 B there. The native resource pin also preserves every
    //   `InviteWindow` offset stated at its declaration, so a later reorder cannot hide inside the total.
    IUiInviteDevice* _invite_dev = nullptr;
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
        // ★★★ [[B232]] — WALKING OFF THE LAST SETTINGS ROW RETURNS TO THE CLOSED SINGLE-ENTRY VIEW, ⛔ NOT off the
        //     screen. It is the PROVISION-child containment idiom (`close_provisioning` lands back in the menu it was
        //     opened from), and it is the whole reason the ruling exists: leaving the screen from the last row is the
        //     "where am I" jump. ⇒ one more `short` then passes SETTINGS, exactly as it does from a fresh arrival.
        if (_st.screen == Screen::settings && _st.settings == Settings::browsing) { close_settings_menu(); return; }
        // ★★★★ §UI-17 S1 — THE INTERACTIVE TEAM/INBOX LIST IS **CONTAINED**: the walk off its last row (`BACK`)
        //      returns to the FIRST row, and ⛔ it never leaves the screen and never wraps into an action. It is the
        //      same "where am I" ruling [[B232]] made one screen over, with the one difference the spec states: there
        //      the walk-off LANDS on the closed view (SETTINGS has a parent view of its own with one row), here it
        //      lands back on row 0 and the list is left by `double`-ing `BACK`.
        // ⓘ It also REPAIRS a cursor left beyond the published rows by a roster that shrank between two ticks — which
        //   is why it is `>=` in `list_row_kind` and a wrap to 0 here rather than a decrement.
        // ⚠ The SETTINGS arm above owns its own containment and has already returned, so this can only be TEAM/INBOX
        //   today; it is written through the shared predicate so a later entered screen inherits the rule.
        if (screen_is_entered(_st.screen, _st.settings, _st.list_view)) { _st.cursor = 0; _st.dirty = true; return; }
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
        // ★★★★ §UI-17 S1 — **THE TWO LIST SCREENS ASK ONE QUESTION, AND IT IS ASKED ONCE** (QG-RULED 2026-08-21).
        //      What a `double` means on a list screen is `list_activate`'s pure decision — the enter, §B64's refusal
        //      and the contained `BACK`, in that order — so each of those three rules is written once and mutated
        //      once. ⛔ Only the DATA differs per screen (which roster, which refusal flag); the DECISION does not,
        //      and duplicating it is how one arm ends up protected and the other not.
        // ⓘ `member` deliberately FALLS THROUGH to the per-screen activation below: what a member row DOES is
        //   genuinely different on the two screens (a DM compose vs a store request), and that half is not shared.
        if (_st.screen == Screen::team || _st.screen == Screen::inbox) {
            const bool    team  = (_st.screen == Screen::team);
            const uint8_t shown = team ? s.team_shown : s.inbox_shown;
            const bool    gone  = team ? _st.team_pick_gone : _st.inbox_pick_gone;
            switch (list_activate(screen_is_entered(_st.screen, _st.settings, _st.list_view),
                                  gone, _st.cursor, shown)) {
                // ⛔ QUEUES NOTHING: a screen the operator has not entered cannot send a DM, cannot open a record and
                //    cannot raise a refusal — the whole point of the passive form.
                case ListAct::enter:  open_list_view(s);  return;
                // C2 — FAIL LOUD, and it RE-STATES the loss rather than raising it: `sync_*_cursor` announced it
                // edge-triggered, and this arm queues NOTHING, which is the whole assertion.
                case ListAct::refuse: _st.dirty = true;   return;
                // The list's last row leaves the LIST, ⛔ NEVER the SCREEN — the `close_provisioning` containment
                // idiom, for the third time in this arc.
                case ListAct::leave:  close_list_view(s); return;
                case ListAct::member: break;
            }
        }
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
            _st.compose_gen = s.preset_generation;   // ★ §UI-10/11 P3 — the generation this sub-view SHOWS (see UiState)
            // ★★★★ §UI-16 K7 ([[B245]]) — THE ROSTER GRANT'S IDENTITY, FROZEN IN THE SAME BREATH AS `compose_peer`.
            //      The hash is resolved from the TEAM chain's OWN one-lookup-per-row answer, keyed by the pick §B64
            //      remembers; the OFFER is then decided ONCE, here, so nothing later in the sub-view's life can
            //      make a row appear or a target move under the operator's finger.
            _st.compose_grant_hash = team_member_hash_of(s.member, s.team_shown, _team_sel_id);
            _st.compose_grant_row  = compose_grant_offered(/*dm=*/true, s.prov_invite, s.team_key_present,
                                                           _st.compose_grant_hash, s.my_key_hash32);
        } else if (_st.screen == Screen::send) {
            _st.compose = Compose::channel; _st.compose_peer = 0; _st.cursor = 0;
            _st.compose_gen = s.preset_generation;   // ★ §UI-10/11 P3 — same freeze, the channel list's own entry
        } else if (_st.screen == Screen::inbox) {
            // ★★★★ §UI-7D slice B — WHAT USED TO BE A DELIBERATE NO-OP. Spec §3.2: a `double` on INBOX opens the detail
            //     modal. It is the SAME shape as §B64's TEAM activation and for the same reason: the thing activated is
            //     the remembered RECORD, never the row the cursor happens to be sitting on.
            // ⛔ AND IT REFUSES RATHER THAN CLAMPING OR RE-READING THE ROW. A selection that is no longer in the store
            //    (or was never identifiable) queues NOTHING and says so — because the alternative shape, "open whatever
            //    is at this index now", opens somebody else's message and puts a DELETE two presses away from it.
            // ⓘ An EMPTY list is left silent: the screen already says it has no stored rows, which IS the reason. Same
            //   carve-out as the empty TEAM roster.
            // ⓘ §UI-17 S1: the enter, the refusal and the contained `BACK` were resolved ABOVE, by the ONE shared
            //   `list_activate` decision — this arm is reached only for a MEMBER row.
            if (!_inbox_sel_valid) {
                if (s.inbox_shown > 0) _st.inbox_pick_gone = true;
                _st.dirty = true;
                return;                                  // ⇒ NOTHING is requested. That is the whole assertion.
            }
            note_inbox_neighbour(s);                     // captured NOW, so a successful delete can land beside it
            _inbox_req = { InboxWhat::open, _inbox_sel_kind, _inbox_sel_seq };
            _inbox_taken = false;
        } else if (_st.screen == Screen::settings) {
            // ★★★ [[B232]] — `double` ON THE CLOSED VIEW OPENS THE MENU, and that is the ONE gesture the single entry
            //     row offers. It is checked here rather than inside `settings_activate` because that function is
            //     "`double` IN THE MENU" — it reads a `CfgRow` out of the row list, and the closed view has none.
            // ★★★★ ⛔⛔ ...AND ONLY WHEN THERE IS A MENU TO DRAW (QG-RULED 2026-08-20 — [[B232]]'s own defect, one
            //      double-press deep). `draw_settings_screen` prints `CFG UNAVAILABLE` and RETURNS while the service
            //      is not open (`src/firmware_ui.cpp`), so EVERY ROW IS INVISIBLE — opening `browsing` there would
            //      hand the operator a cursor walking rows nothing draws, and up to nine presses before the screen
            //      advanced. That is exactly the multi-press problem this ruling exists to remove, re-created behind
            //      a `double`. ⇒ THE VIEW STAYS CLOSED: it keeps rendering its unavailable state, and `short` still
            //      passes the screen in ONE press (`list_len` answers 1 while closed, whatever the service says).
            // ⓘ A NULL `_cfg` FAILS CLOSED the same way — this file's standing rule for an unattached model, and the
            //   panel says `CFG UNAVAILABLE` for that too (`freeze_settings` reports `open == false` for both).
            if (_st.settings == Settings::closed) {
                if (_cfg && _cfg->is_open()) open_settings_menu();
                return;
            }
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
    //    paths that can leave SETTINGS (the `short` that advances the CYCLE, and `sync_settings` itself), because a
    //    guard belongs to the INVARIANT and not to the site where it was first needed. ⛔ Leaving it to `sync_settings`
    //    alone is a real hole: the advance happens INSIDE a gesture, after that gesture's sync has already run, so
    //    the state would stay `browsing`/`editing` for the rest of the pass — and a frame frozen in between would
    //    render a SETTINGS editor over the STATUS screen.
    // ⛔ CORRECTED IN PLACE 2026-08-20 ([[B232]], V1): the first of those two paths used to be *"the `short` walk off
    //    the last row"*, and it is not one any more — walking off the last row now lands on the CLOSED single-entry
    //    view and stays on SETTINGS (`advance_or_next`). The screen is left by the `short` taken FROM that closed
    //    view, which reaches this function through the same branch. ⓘ The `BACK` row was a third path and no longer
    //    is: it calls `close_settings_menu()` and the panel stays put.
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
        if (provision_reset_on_leave(_st.provisioning, _st.prov_confirm, _st.invite)) _st.dirty = true;
        _cfg_sel_valid = false;
    }
    // ★★★ [[B232]]'s TWO PRIMITIVES, and they are two because the menu is now ENTERED and LEFT rather than merely
    //     "the state SETTINGS is in". Both re-establish the SAME three facts, which is why neither is spelled out at
    //     a call site (the `enter_provision`/`close_provisioning` pairing one level down, U1/U3):
    //       · the arm, · the cursor at row 0, · and `_cfg_sel_valid` false so `sync_settings` re-anchors the pick.
    // ⓘ THE MENU ALWAYS OPENS ON ITS FIRST ROW, exactly as `enter_provision`'s *"each arm's list starts at its own
    //   first row"* does. ⛔ It deliberately does NOT restore the row the operator left on: the closed view is the
    //   PARENT here and it has one row, so there is no parent pick that a remembered child row could disagree with.
    void open_settings_menu() {
        _st.settings = Settings::browsing; _st.cursor = 0; _cfg_sel_valid = false; _st.dirty = true;
    }
    void close_settings_menu() {
        _st.settings = Settings::closed;   _st.cursor = 0; _cfg_sel_valid = false; _st.dirty = true;
    }
    // ★★★ §UI-17 S1's TWO PRIMITIVES, and they are the `open_settings_menu` / `close_settings_menu` pair above
    //     VERBATIM in shape (U1/U3) because the idiom is the same one: the list is now ENTERED and LEFT rather than
    //     being "the state TEAM/INBOX is in". Both re-establish the SAME three facts, which is why neither is spelled
    //     out at a call site:
    //       · the arm, · the cursor at row 0, · and the PICK re-established from that row — see below.
    // ★★ THE PICK IS THE ONE PLACE THE TWO PAIRS DIFFER, AND THE DIFFERENCE IS §B64's. SETTINGS clears its pick and
    //    lets `sync_settings` re-anchor on the next pass (*"first arrival: whatever row 0 is IS the pick"*). ⛔ TEAM
    //    and INBOX MUST NOT do that: `sync_team_cursor`/`sync_inbox_cursor` treat an invalid pick as *"nothing picked
    //    yet, OR a pick already LOST and announced"* — re-noting it there would silently re-select whatever row now
    //    sits under the cursor, which is the exact mis-send §B64 forbids. ⇒ the pick is established HERE, by the press
    //    that entered, because that press genuinely pointed at row 0. `note_*_cursor` is the ONE write side (U1), and
    //    on close it does the opposite by construction: it is gated on `interactive`, so a passive screen records no
    //    pick and `activate` there can queue nothing.
    // ⓘ THE LIST ALWAYS OPENS ON ITS FIRST ROW, exactly as `open_settings_menu` does and for the same reason: the
    //   passive preview is the PARENT here and it has one row, so there is no parent pick to disagree with.
    void open_list_view(const UiSnapshot& s) {
        _st.list_view = ListView::interactive; _st.cursor = 0; _st.dirty = true;
        note_team_cursor(s); note_inbox_cursor(s);
    }
    void close_list_view(const UiSnapshot& s) {
        _st.list_view = ListView::passive;     _st.cursor = 0; _st.dirty = true;
        note_team_cursor(s); note_inbox_cursor(s);
    }
    // ★★ THE VIEW MAY NEVER OUTLIVE ITS SCREEN, and this is the one primitive that enforces it — the
    //    `settings_follow_screen` shape one screen over, forwarding its DECISION to the pure
    //    `list_view_reset_on_leave` for the [[B223]] reason stated there. Both call sites are forwards: the `short`
    //    that advances the CYCLE, and `on_tick`, so a frame frozen between two gestures cannot render an entered list
    //    over another screen.
    void list_follow_screen() {
        if (_st.screen == Screen::team || _st.screen == Screen::inbox) return;
        if (list_view_reset_on_leave(_st.list_view)) _st.dirty = true;
    }
    void sync_settings(const UiSnapshot& s) {
        settings_follow_screen();
        if (_st.screen != Screen::settings) return;
        if (_cfg && !_cfg->is_open()) {
            // ⛔ A REFUSED OPEN IS NOT RETRIED SILENTLY BEHIND A WORKING-LOOKING MENU: `no_record` means the store
            //    could not produce a record, so there is no baseline and nothing may be saved. The renderer says so
            //    (C2) and every activation below refuses, because `is_open()` stays false.
            (void)_cfg->open();
        }
        // ★★★★ [[B232]] — ARRIVAL LANDS ON THE **CLOSED** SINGLE-ENTRY VIEW, and this early return is where the old
        //      auto-enter used to be (`_st.settings = Settings::browsing` on the first tick after arrival). That is
        //      what cost the operator up to NINE short presses to cycle past a screen every other screen passes in one.
        // ⛔⛔ AND IT IS PLACED **BELOW** THE `open()` ABOVE, WHICH IS THE POINT RATHER THAN THE ORDER FALLING OUT:
        //     the SERVICE still opens ON ARRIVAL. §3.6.1's baseline is snapshotted here, `note_external_write`'s
        //     conflict latch fires against it while the closed view is up, and the rail badge reads the three
        //     predicates it establishes. Deferring `open()` to the menu is the tempting wrong fix — it would leave a
        //     node whose operator glanced at SETTINGS with no baseline, so a companion write raised no conflict at all.
        if (_st.settings == Settings::closed) return;
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
        // ★ [[B232]]: only the BROWSING view's cursor is a `CfgRow` index. On the CLOSED single-entry view it names
        //   the one entry row, so reading it as a menu row would record `_cfg_sel_row = <whatever row 0 is>` from a
        //   press that pointed at nothing of the sort — and the menu would then open on a pick nobody made.
        // ⚠ STATED HONESTLY ([[meshroute-mark-done-vs-missing-in-code]]): NO SUITE CAN DRIVE THIS TERM TODAY, so it
        //   has no mutation of its own. `open_settings_menu` clears `_cfg_sel_valid` on the one path back into the
        //   menu, so a pick recorded here would be discarded before anything read it. It is written because the
        //   INVARIANT is "a cursor is only a row while the rows are up", not because a reader is reachable.
        if (_st.screen == Screen::settings && _st.settings == Settings::browsing &&
            settings_row_list(s).at(_st.cursor, r)) {
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
            // ⚠ NO LONGER REACHABLE, AND STATED RATHER THAN QUIETLY KEPT ([[meshroute-mark-done-vs-missing-in-code]]):
            //   since [[B232]]'s QG correction the MENU ITSELF cannot be entered while the service is not open
            //   (`activate`'s closed-view guard), and `_open` is never cleared once set (firmware_config_service.h) —
            //   so no gesture sequence reaches this line with a shut service. ⇒ its mutation was WITHDRAWN from the
            //   battery (M50; the property is now measured one layer out, by M105) and this is defence in depth, the
            //   same standing as the `CfgRow::provision` arm's `!_cfg || !_cfg->is_open()` line below, which has
            //   never had a mutation for exactly this reason.
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
                // ★★★ [[B232]] — BACK NOW LEAVES THE **MENU**, NOT THE SCREEN. It used to jump to STATUS, which was
                //     the "where am I" jump from the other direction; the panel stays on SETTINGS showing the closed
                //     single-entry view, and one more `short` passes the screen. ⓘ §3.6.2 says only that *"`BACK` is
                //     safe and PRESERVES an unsaved draft"* — which `on_back()` above is, unchanged — so the landing
                //     was always this file's choice rather than a documented one.
                // ★ §UI-15 slice 4's argument survives verbatim, one primitive over: the state is retired through the
                //   ONE function that owns it rather than by re-spelling half of it here. ⓘ `Provision` needs no
                //   retiring on this path — the sub-view owns the press while it is open, so BACK is unreachable from
                //   it, and `settings_follow_screen` still closes everything the moment the screen itself is left.
                close_settings_menu();
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
        // ★★★★ §UI-16 N4 / F-13 — **THE WINDOW'S STATE IS DISCARDED BY EVERY ENTRY THAT IS NOT THE WINDOW**, and
        //      that is what makes "volatile, per-window" STRUCTURAL rather than remembered. The handled set, the
        //      two authorities and the frozen selection all die together here — on BACK to the menu, on the
        //      expiry that lands `invite_closed`, on the alarm's `close_provisioning`, on any other arm.
        // ⛔ IT IS ⛔ NOT A `taken = false` ALONE: a set that merely stopped being CONSULTED is a set that comes
        //    back the moment somebody re-arms the flag, and the ruling is that the hashes themselves do not
        //    survive the window (spec §3 P-11b: after close and re-open the set is EMPTY, which is a native pin).
        // ⓘ The two INVITE arms keep it, deliberately: `invite -> invite_confirm -> invite` is one window, and a
        //   confirmation opened and abandoned may not silently forget what the operator already rejected.
        if (!provision_is_invite(p)) _st.invite = InviteWindow{};
        // ★★★ §UI-16 N6 — THE VERDICT IS RETIRED BY EVERY ENTRY THAT IS NOT THE VERDICT'S OWN SCREEN, which is the
        //     `prov_answer` rule three lines up applied to the grant (U3): a result that survived into a later
        //     screen is the "success that isn't" this project has already registered once. ⛔ The one arm excluded
        //     is `invite_result` itself, because `run_invite_grant` writes the verdict and THEN enters it.
        if (p != Provision::invite_result) _st.grant = InviteGrantResult{};
        // ★★★ §UI-16 K5 — THE OFFER'S TARGET IS RETIRED BY **EVERY** ENTRY, which is the `prov_answer` rule three
        //     blocks up applied to the field that names a stored SECRET (U3). ⛔ It is retired on the way INTO the
        //     offer as well — the caller re-writes it immediately afterwards, from the answer the transaction just
        //     returned, exactly as `run_join_team` writes `prov_answer` after entering the result screen. ⇒ a stale
        //     target cannot survive into a later screen, and ⛔ no arm but the one that just joined can arm this act.
        _st.saved_key_team = 0;
        // ★★★ §UI-16 K6 — THE REMOVAL'S TARGET IS RETIRED BY **EVERY** ENTRY, which is the rule one line up applied
        //     to the field that names a stored secret **FOR DESTRUCTION** (U3). ⛔ It is retired on the way INTO the
        //     confirmation as well — the caller re-writes it immediately afterwards, from the row the cursor was on
        //     — so a stale target cannot survive into a later screen, and ⛔ no arm but the one that just selected a
        //     row can arm this act. ⓘ The LIST itself is deliberately not retired here (see `UiState::saved_keys`).
        _st.forget_team = 0;
        _st.dirty = true;
    }
    // ⓘ Back to `browsing`, ⛔ never to `closed`: leaving PROVISION returns to the SETTINGS MENU, not off the screen.
    //   The highlight lands back on the PROVISION row by itself — `sync_settings` re-anchors from `_cfg_sel_row`,
    //   which this sub-view was forbidden to touch, and it runs before any frame can freeze.
    // ⓘ ONE spelling of "retire the sub-state" (U1), shared with the leave-the-screen path: the two transitions differ
    //   only in where they LAND (`browsing` here, `closed` there), and the fields they retire are the same two.
    void close_provisioning() {
        _st.settings = Settings::browsing;
        provision_reset_on_leave(_st.provisioning, _st.prov_confirm, _st.invite);
        _st.dirty = true;
    }
    // ★★★ THE SUB-VIEW'S GESTURES. `short` CYCLES within the arm's list and ⛔ never walks out of the screen — the
    //     `InboxModal` / compose rule (a sub-view is left by its own BACK, by the long gesture or by the blank), not
    //     `advance_or_next`'s walk-off, which belongs to the ordinary screen cycle.
    // ★★ §UI-15 slice 6 LANDS THE JOIN HALF, so the four `join_*` arms below have left the leave-only fail-safe and
    //    have their own flows. ⛔ CORRECTED IN PLACE 2026-08-23 (§UI-16 N4, V1), AND THE WITHDRAWN LINE IS KEPT
    //    VISIBLE: it read *"⛔ WHAT IS STILL NOT HERE, by scope: §3.6.4's nearby-team scan (§UI-16)"*. N2/N3
    //    landed the scan and its join, N4 the creator's window, and N5 now lands the explicit BACK-default
    //    `REQUEST PUBKEY` plus the side-effect-free gate that enables `GRANT KEY`. ⛔ WHAT IS STILL NOT HERE is
    //    the GRANT ACT and its `send_aired` correlation (N6); selecting GRANT below therefore remains inert.
    void provision_gesture(Gesture g, const UiSnapshot& s) {
        if (g != Gesture::short_press && g != Gesture::double_press) return;
        switch (_st.provisioning) {
            case Provision::menu:           provision_menu_gesture(g, s);    return;
            case Provision::create_confirm: provision_confirm_gesture(g);    return;
            // ★★ THE RESULT IS TERMINAL AND EITHER PRESS LEAVES IT (the panel says `press = back`, the same contract
            //    the send result and the `MESSAGE GONE` modal already carry). ⛔ Nothing is re-run and nothing is
            //    confirmed here: the act is over, and the only thing this arm owns is the way out.
            // ⛔ CORRECTED IN PLACE 2026-08-25 (§UI-16 K5), AND THE WITHDRAWN LINE IS KEPT VISIBLE: it read
            //    `case Provision::create_result:  enter_provision(Provision::menu); return;`. The two properties it
            //    carried are UNCHANGED — either press acknowledges, and ⛔ nothing is re-run — and what is added is
            //    WHERE the acknowledgement lands when the verdict itself has a follow-up question.
            case Provision::create_result:  create_result_gesture();          return;
            case Provision::join_select:    join_select_gesture(g);          return;
            case Provision::join_confirm:   join_confirm_gesture(g, s);      return;
            // ★★★★ THE WAITING SCREEN, AND EITHER PRESS **ONLY LEAVES IT** — plan §2.3 rule 4, verbatim in
            //      substance: *"BACK during `JOINING` only LEAVES THE SCREEN; ⛔ it does not cancel or roll back an
            //      already-persisted operation"*. ⇒ the session is untouched here, deliberately and visibly: the
            //      join was durably written and DAD is running, and there is no verb on this device that could
            //      un-write it. ⛔ Nothing is cancelled, nothing is retried, nothing is said.
            case Provision::join_waiting:   enter_provision(Provision::menu); return;
            // The join RESULT is terminal in exactly the same way the create one is.
            // ⛔ CORRECTED IN PLACE 2026-08-25 (§UI-17 keyrecv), AND THE WITHDRAWN LINE IS KEPT VISIBLE: it read
            //    `case Provision::join_result:    enter_provision(Provision::menu); return;`. `ADOPTED` — and every
            //    other verdict this screen can carry — still lands on the MENU, byte for byte. What is added is the
            //    ONE answer whose landing the owner moved: a `TEAM KEY RECEIVED` note can be sitting in `prov_answer`
            //    on THIS screen too (a static join renders here), and the note's landing is the NOTE's, ⛔ not the
            //    screen's — see `team_key_note_ack_landed`.
            // ⛔ AMENDED IN PLACE 2026-08-25 (§UI-16 K6's QG blocker): a static join's result screen renders the
            //    SAME note, so the FULL-KEYRING receipt must find its landing here too — ⛔ or one note would have
            //    two landings, which is exactly the drift the answer-keyed helpers exist to prevent.
            case Provision::join_result:
                if (!team_key_note_ack_landed() && !team_key_full_ack_landed()) enter_provision(Provision::menu);
                return;
            // §UI-16 N2 — the READ-ONLY scan list. Its `short`/`double` are `join_select`'s shape, ⛔ not
            // TEAM's: it opens on its first row and its last row is BACK.
            case Provision::nearby:         nearby_select_gesture(g);        return;
            // §UI-16 N3 — the `JOIN <fingerprint>?` confirmation. Its shape is `join_confirm`'s (`short` toggles,
            // `double` performs the SELECTED action) and ⛔ its landing differs: BACK returns to the NEARBY LIST.
            case Provision::nearby_confirm: nearby_confirm_gesture(g);       return;
            // §UI-16 K5 — the `SAVED KEY FOUND` offer. Its shape is `nearby_confirm`'s (`short` toggles, `double`
            // performs the SELECTED action) and its BACK lands on the MENU — exactly where the acknowledgement it
            // was reached from would have landed, so declining costs the operator nothing.
            case Provision::saved_key:      saved_key_gesture(g);            return;
            // §UI-16 K6 — the RETENTION list. Its `short`/`double` are `nearby`'s shape (it opens on its first row
            // and its last row is BACK), and it walks the FROZEN copy the transition captured.
            case Provision::saved_keys:         saved_keys_select_gesture(g);  return;
            // §UI-16 K6 — the IRREVERSIBLE confirmation. `nearby_confirm`'s shape (`short` toggles, `double`
            // performs the SELECTED one) and ⛔ its landing is the LIST, not the menu.
            case Provision::saved_keys_confirm: saved_keys_confirm_gesture(g); return;
            // ★★★★ §UI-16 K6 — THE PROTECTED LANDING, AND EITHER PRESS ONLY LEAVES IT. ⛔ There is no action to
            //      select here and ⛔ nothing to perform: the active key cannot be forgotten from the panel, and
            //      this arm is what makes that structural rather than conditional. It returns to the LIST — the
            //      screen the operator was choosing on — ⛔ not to the menu.
            case Provision::saved_keys_active:  enter_provision(Provision::saved_keys); return;
            // §UI-16 K6 — the removal's VERDICT. Terminal in the way every result screen is; its landing RE-READS
            // the list, which is what *"returns to the refreshed list"* means (see `saved_keys_result_gesture`).
            case Provision::saved_keys_result:  saved_keys_result_gesture(); return;
            // §UI-16 N4 — the INVITATION WINDOW. Its `short`/`double` are the scan list's shape (it opens on its
            // first row and its last row is BACK), and it needs the SNAPSHOT because the list it walks is the
            // LIVE one: the window refreshes locally while it is open (R-10).
            case Provision::invite:         invite_select_gesture(g, s);     return;
            // §UI-16 N6 — the grant-ready confirmation needs the SNAPSHOT for the window's own deadline: the act
            // must be refused on an EXPIRED window, and the tick that would close it has not run yet.
            case Provision::invite_confirm: invite_confirm_gesture(g, s);    return;
            case Provision::invite_need_pubkey: invite_need_pubkey_gesture(g); return;
            // The request has already been authorised and issued. Either press merely returns to the window;
            // it cannot cancel, retry or grant anything (the join-waiting idiom one flow over).
            case Provision::invite_wait_pubkey: enter_provision(Provision::invite); return;
            // The EXPIRY screen is terminal in exactly the way the two result screens are: either press leaves
            // it, and ⛔ nothing is re-run, re-opened or confirmed — the window is over.
            case Provision::invite_closed:  enter_provision(Provision::menu); return;
            // ★★ §UI-16 N6 — THE GRANT'S VERDICT IS TERMINAL IN THE SAME WAY (pin 9): either press acknowledges it
            //    and ⛔ NOTHING is re-run — a second grant would be a second private key on the air for one
            //    operator decision. ⓘ It lands on the MENU and ⛔ not back in the window: the act ended the
            //    window, and a re-opened one takes a FRESH snapshot in which the member just granted is simply
            //    present (and therefore, correctly, no longer a candidate).
            case Provision::invite_result:  enter_provision(Provision::menu); return;
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
            // ★★★★ §UI-16 N2 / ✅ OQ-1 — `JOIN TEAM` OPENS THE NEARBY LIST **DIRECTLY**, ⛔ never a submenu.
            //      ⛔ THE CACHE IS READ **HERE AND ONCE** — on the transition, ⛔ not per tick and ⛔ not per
            //      page (owner ruling R-10). The read itself is cheap (a bounded `const` walk of at most
            //      `cap_team_seen` entries, no flash and no radio); what the once-ness buys is that the list
            //      cannot re-order or grow under the operator's cursor while they are walking it.
            // ⓘ AN EMPTY SCAN IS NOT A REFUSING TRANSITION: the screen OPENS and says `NO TEAMS NEARBY`,
            //   because "nothing is audible here" is a fact the operator came to learn — the same reasoning
            //   `join_static`'s refusing-store arm carries one row up.
            case ProvRow::join_team:   load_nearby(s);       enter_provision(Provision::nearby);      return;
            // ★★★★ §UI-16 N4 — THE INVITATION WINDOW OPENS, AND **THE SNAPSHOT IS TAKEN HERE**: on the
            //      TRANSITION, ⛔ not at the first render and ⛔ not per tick. That ordering IS the feature —
            //      *"opening invitation mode snapshots the known member identities"* — and taking it at the
            //      first draw would take it AFTER a member could already have arrived, hiding exactly the
            //      candidate the window exists to surface (and re-taking it on every repaint).
            // ⓘ AN EMPTY TEAM IS NOT A REFUSING TRANSITION (the `join_static` / `join_team` rule, a third time):
            //   the window OPENS and says `NO CANDIDATES`, because "nobody new" is what the operator came to
            //   learn. The row itself is hidden when we are in no team at all (`prov_invite`).
            case ProvRow::invite:      load_invite(s);       enter_provision(Provision::invite);      return;
            // ★★★★ §UI-16 K6 — THE RETENTION LIST OPENS, AND ⛔ THE KEYRING IS READ **HERE AND ONCE**: on the
            //      TRANSITION, ⛔ not per tick and ⛔ not per page, because the enumeration reaches FLASH. That is
            //      `join_static`'s rule (`profiles()`) verbatim, and freezing it is also what keeps a record from
            //      appearing or vanishing under the operator's cursor mid-walk.
            // ⓘ AN EMPTY OR REFUSING STORE IS NOT A REFUSING TRANSITION (the `join_static` / `join_team` / `invite`
            //   rule, a fourth time): the screen OPENS and SAYS what is wrong (`saved_keys_head`), because "there
            //   are no retained keys" and "the store would not open" are exactly what the operator came to learn.
            // ⛔ OPENING PERFORMS NOTHING: zero writes, zero evictions, nothing chosen (spec §4-K6 pin 1).
            case ProvRow::saved_keys:  load_saved_keys();    enter_provision(Provision::saved_keys);  return;
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
    // ================================================== §UI-16 N2 — the READ-ONLY NEARBY scan (§3.6.4 point 2)
    // ★★ THE ONE READ OF THE OBSERVATION CACHE, and it happens on the TRANSITION (see `provision_menu_gesture`).
    //    ⛔ IT IS A **COPY**, ⛔ never a pointer into the snapshot: the snapshot is rebuilt every tick, so a
    //    screen holding a reference into it would be the auto-refresh owner ruling R-10 forbids, wearing the
    //    shape of an optimisation.
    // ★ THE OWN-TEAM FILTER IS APPLIED HERE, ONCE, by the pure `nearby_capture` — N1 records our own team like
    //   any other, deliberately, so that *"which teams are audible"* and *"which of them are worth offering"*
    //   have one authority each (spec §4-N1 pin 9 / §4-N2 pin 3).
    void load_nearby(const UiSnapshot& s) {
        _st.nearby = nearby_capture(s.nearby, s.nearby_n, s.team_id);
    }
    // `short` CYCLES the scan list and ⛔ never walks out of the screen (the sub-view rule, three menus deep);
    // `double` on BACK returns to the PROVISION MENU — the `close_provisioning` CONTAINMENT idiom, and ⛔ not
    // off the screen (§UI-17's landed navigation contract: a list's BACK returns to its own parent).
    // ✅ §UI-16 N3 LANDED THE ACT, AND THE WITHDRAWN N2 WORDING IS KEPT VISIBLE: this block read *"⛔⛔ AND A
    //    `double` ON A TEAM ROW DOES NOTHING, DELIBERATELY AND VISIBLY … [[B222]]'s rule is that a transition lands
    //    WITH the flow behind it"*. The flow is here now: a `double` on a team row OPENS THE CONFIRMATION, and
    //    ⛔ STILL PERFORMS NOTHING — no transaction, no write, no airtime — because opening a confirmation is not
    //    acting on it. ★ Reaching the act costs `short` THEN `double` (P-13), which `enter_provision`'s BACK default
    //    makes structural rather than remembered.
    void nearby_select_gesture(Gesture g) {
        const NearbySelList l = nearby_sel_rows(_st.nearby);
        if (g == Gesture::short_press) {
            // ⓘ SPELLED OUT rather than shared with the two identical lines above: the three walk different
            //   lists, and one function branching on the arm is how a press eventually acts on another's.
            if (l.n) _st.cursor = uint8_t((_st.cursor + 1) % l.n);   // CYCLES — the sub-view rule
            _st.dirty = true;
            return;
        }
        NearbySelRow r{};
        if (!l.at(_st.cursor, r)) return;                            // fails closed — see NearbySelList::at
        if (r.back) { enter_provision(Provision::menu); return; }
        // ★★★ THE PICK IS THE ROW'S OWN FULL 32-BIT TEAM ID, ⛔ never the cursor index (§B66) and ⛔ never the
        //     fingerprint the next screen prints (spec §3 P-7): the value acted on is the value the panel drew, and
        //     it rides the row WHOLE from the observation cache (U2). `enter_provision` re-anchors the cursor and
        //     the BACK default, so the confirmation cannot open on the act.
        _st.nearby_sel_id = r.team.team_id;
        enter_provision(Provision::nearby_confirm);
    }
    // ★★★★ §UI-16 N3 — THE JOIN CONFIRMATION: the `InboxAction`/create/join pair a FOURTH time (U3), `short` TOGGLES
    //      and `double` PERFORMS THE SELECTED ONE — and ⛔ a `double` on BACK may NOT fall through into the act.
    // ⛔ ITS LANDING IS THE **NEARBY LIST**, ⛔ NOT THE MENU (spec §4-N3 pin 3): BACK returns to the screen the
    //    operator was choosing on, exactly as the static-join confirmation returns to its SLOT LIST. ⓘ Re-entering
    //    `nearby` does ⛔ NOT re-read the cache — `load_nearby` runs on the `menu -> nearby` transition alone — so
    //    owner ruling R-10's frozen list survives a look at a confirmation and a change of mind.
    // ⛔ NO SNAPSHOT IS READ HERE, deliberately, and this follows `provision_confirm_gesture`'s rule rather than
    //    `join_confirm_gesture`'s exception: the act's inputs are the DEVICE's (the record, the live PHY, the build
    //    floor), gathered by the adapter at the instant it runs, and this act starts no session with a clock.
    void nearby_confirm_gesture(Gesture g) {
        if (g == Gesture::short_press) { prov_confirm_toggle(); return; }
        if (_st.prov_confirm == ProvConfirm::back) { enter_provision(Provision::nearby); return; }
        run_join_team();
    }
    // ★★★★ THE ACT, AND THE ORDER OF ITS STATEMENTS IS §8 PIN 2 EXACTLY AS THE OTHER TWO ACTS' IS: the transaction
    //      RUNS, RETURNS, and only then does the screen move and the verdict land. ⛔ There is no path that shows
    //      `TEAM JOINED` before `perform()` came back with `applied`.
    // ⛔⛔ THE INTENT CARRIES THE **FULL 32-BIT ID** AND NOTHING ELSE THE PANEL DERIVED (spec §4-N3 pin 2, §3 P-1):
    //     the joined team must be BYTE-EQUAL to the observed one, so what travels is the id itself — ⛔ not the
    //     cursor, ⛔ not the fingerprint, ⛔ not a re-read of anything.
    // ⓘ A NULL SEAM, OR A PICK THAT NAMES NO TEAM, REFUSES OUT LOUD (C2) rather than doing nothing — the dead-button
    //   complaint `run_create_team`'s own arm is built against. ★ The 0 clause is a REAL floor and not decoration:
    //   `TeamRequest{ mint=false, team_id=0 }` is the console's `team 0`, i.e. a LEAVE — the one thing a JOIN screen
    //   must never be able to perform by accident.
    // ⛔ IT IS `join_refused`, ⛔ NOT `refused`: the panel must say `JOIN REFUSED` on this path (spec §8 S-11), or the
    //   operator reads `CREATE REFUSED` for an operation that created nothing and was never a create.
    void run_join_team() {
        UiProvAnswer a{};
        if (_prov && _st.nearby_sel_id != 0) {
            UiProvIntent in{};
            in.op      = UiProvOp::join_team;
            in.team_id = _st.nearby_sel_id;      // ★ the row's identity, whole (U2)
            a = _prov->perform(in);
        } else {
            a.outcome = UiProvOutcome::join_refused;
            a.reason  = "no service";
        }
        enter_provision(Provision::create_result);
        _st.prov_answer = a;
    }
    // ★★★★ §UI-17 (OWNER-RULED 2026-08-25, shape (a)) — **ACKNOWLEDGING `TEAM KEY RECEIVED` LANDS ON STATUS**, and
    //      ⛔ NOT back in the provisioning flow. After a team key arrives the operator's next question is *"am I set
    //      up?"*, and that question is the STATUS screen's; the provisioning menu answers a question nobody asked.
    //      ⓘ Shape (b) — a `BACK TO MAIN` row on the result — was considered and REJECTED at the ruling: it would need
    //        a new lexeme and would break the terminal shape every result screen shares.
    // ★★ IT IS KEYED ON THE **ANSWER**, ⛔ never on the arm, and that is what makes it ONE rule rather than two. The
    //    note occupies `prov_answer` (`on_team_key_note`) and the renderer draws it on WHICHEVER result screen happens
    //    to be up — `create_result` for a create/nearby-join flow, `join_result` for a static join
    //    (`src/firmware_ui.cpp:1674` says so at the row it shares). Keying on the screen would give ONE note TWO
    //    landings, which is the drift this file keeps one decision in one place to avoid (U1).
    // ⛔⛔ THE FAILURE PAIR IS DELIBERATELY **NOT** HERE, and its absence is the ruling rather than an oversight:
    //     `TEAM KEY ACTIVE` / `NOT SAVED` / `LOST ON REBOOT` (S-26/S-27) acknowledges to the LANDED landing, because
    //     that is where the REMEDIES are. A save that failed is exactly the moment not to walk the operator out of
    //     the flow that can retry it. ⓘ Every other terminal — `TEAM JOINED`, `ADOPTED`, `TEAM CREATED`, every
    //     refusal, K5's own two endings — is untouched for the same reason: this slice moved ONE landing.
    // ⛔ AND IT IS THE **PRESS** THAT NAVIGATES, ⛔ never the arrival: spec §4-K4 pin 3 (*"a push never navigates"*)
    //    is untouched — `on_team_key_note` still moves no screen, no arm and no cursor and wakes nothing. This
    //    function is reachable ONLY from `provision_gesture`, i.e. only from an operator's own press.
    // ★ THE LANDING IS **PASSIVE** STATUS, in §UI-17's own terms: STATUS has no interactive form (`screen_is_entered`
    //   answers false for it, so its `double` is the no-op it has always been), and the two sub-views that COULD be
    //   left standing are retired through the primitives that own them rather than by assigning their fields here —
    //   `settings_follow_screen()` closes SETTINGS *and* forwards to `provision_reset_on_leave`, `list_follow_screen()`
    //   forwards to `list_view_reset_on_leave`.
    // ⛔⛔ WAITING FOR THE TICK TO RUN THOSE TWO IS A REAL HOLE, ⛔ not a tidier equivalent: `provision_gesture`
    //     RETURNS before `on_gesture` reaches its own `settings_follow_screen()` call, and `FrameGate::step` can
    //     freeze a frame before the next `on_tick` — which would render a provisioning result over the STATUS screen,
    //     the exact artefact `settings_follow_screen`'s own block warns about.
    // ⓘ `list_follow_screen()` cannot change anything TODAY and is called anyway, marked rather than left to look like
    //   coverage ([[meshroute-mark-done-vs-missing-in-code]]): an interactive TEAM/INBOX list cannot coexist with
    //   `Settings::provisioning` (the `short` that left those screens already retired it), so ⛔ no mutation of it can
    //   redden here. It is the `list_view_reset_on_leave` invariant stated at a second leave-path, exactly as
    //   `settings_follow_screen` states its own — "leaving retires it", ⛔ not "the paths that can leave today do".
    // ⓘ `prov_answer` is deliberately NOT retired: retiring is what an ENTRY does (`enter_provision`), and LEAVING has
    //   never done it (`close_provisioning` does not either). Nothing renders it off the two result arms, and the next
    //   entry into PROVISION clears it before a pixel of it can be drawn.
    // ⓘ Returns whether it TOOK the landing, so each terminal arm keeps its own `return` and ⛔ no arm can fall
    //   through into a second landing.
    bool team_key_note_ack_landed() {
        if (_st.prov_answer.outcome != UiProvOutcome::team_key_received) return false;
        _st.screen = Screen::status;
        _st.cursor = 0;
        settings_follow_screen();
        list_follow_screen();
        _st.dirty = true;
        return true;
    }
    // ★★★★ §UI-16 K6 (the QG blocker of 2026-08-25) — **A RECEIVED GRANT REFUSED BY A FULL KEYRING ACKNOWLEDGES
    //      INTO `SAVED KEYS`**, exactly as the `team new` refusal of the same store state already does. Spec §K6
    //      `:987` rules the direction for *"a `KEYRING FULL` result"* — ⛔ **either origin** — and before this the
    //      fifth RECEIVED grant showed three true rows and then landed on a menu that says nothing about why four
    //      keys are one too many: a dead end with no way out, reachable only over the air.
    // ★★ IT IS KEYED ON THE **ANSWER**, ⛔ never on the arm, which is `team_key_note_ack_landed`'s own rule one
    //    function up and the reason both live here: the note occupies `prov_answer` and the renderer draws it on
    //    WHICHEVER result screen happens to be up (`create_result` for a create/nearby-join flow, `join_result` for
    //    a static join). Keying on the screen would give ONE note TWO landings.
    // ⛔⛔ AND IT DOES EXACTLY WHAT THE CREATE-SIDE ARM DOES AND NOTHING MORE (the ruling, word for word):
    //     ⛔ it chooses no VICTIM (the list opens on its first row, nothing selected);
    //     ⛔ it DELETES nothing (opening the list is a read; the removal still costs a row, an irreversible
    //        confirmation and a `double` on `FORGET KEY`);
    //     ⛔ it does not RETRY the grant — **two explicit transactions, never one disguised one**. A grant cannot be
    //        replayed from this node at all (the granter sent it), which makes the point sharper rather than moot:
    //        nothing here asks for it again, and nothing here writes.
    // ⛔ THE CONDITION IS THE **TYPED** FLAG, set by the pure `mrfw::grant_ui_verdict_of` from the transaction's own
    //    `KeyringErr::keyring_full` — ⛔ never a re-read of the store and ⛔ never a comparison of display text.
    // ⓘ Returns whether it TOOK the landing, so each terminal arm keeps its own `return` and ⛔ no arm can fall
    //   through into a second landing (the sibling helper's contract, restated where it is relied on).
    bool team_key_full_ack_landed() {
        if (_st.prov_answer.outcome != UiProvOutcome::team_key_unsaved) return false;
        if (!_st.prov_answer.keyring_full) return false;
        load_saved_keys();
        enter_provision(Provision::saved_keys);
        return true;
    }
    // ============================================ §UI-16 K5 — `SAVED KEY FOUND` / `USE SAVED KEY` (§3.6.4 point 4)
    // ★★★★ THE ACKNOWLEDGEMENT OF THE RESULT SCREEN, AND **WHERE THE OFFER SITS IS THIS FUNCTION** — a design
    //      decision, so it is written down rather than left to be inferred (the N6 `GRANT PARKED` precedent:
    //      reported, ⛔ not assumed). THREE constraints fix it here and nowhere else:
    //        1. ⛔ IT CANNOT PRECEDE THE JOIN. The keyring's boot predicate compares the ACTIVE BINDING against the
    //           MEMBERSHIP `/mrcfg` holds (term (ii), QG blocker 3), so an activation committed before the join's
    //           durable write would name a team this node is not yet in — a binding that lies, and one the very
    //           next boot would refuse. The membership must be written FIRST.
    //        2. ⛔ IT CANNOT REPLACE THE JOIN'S RESULT. Spec §4-N3 pin 5 requires the join's own verdict — the word,
    //           the full id and the fingerprint — and the join *"succeeds or fails exactly as N3 landed it"*:
    //           membership is ⛔ never gated on the key decision, in either direction.
    //        3. ★ AND MAKING IT THE **ACKNOWLEDGEMENT'S** LANDING IS WHAT PUTS P-2b IN THE CONTROL FLOW: the
    //           transaction has run, returned and been REPORTED, and the operator has PRESSED PAST it, before the
    //           key question is even asked. ⇒ nothing about the key can be read as part of joining — which is the
    //           whole reason the ruling asks for a screen instead of a rule.
    // ⛔ THE OFFER IS OPENED BY THE ANSWER, ⛔ NEVER BY THE ARM: the condition is `team_joined` + the KEYRING's own
    //    report + a non-zero id. A create's acknowledgement, a refusal's, a failed save's — all land on the menu
    //    exactly as they always did, and so does a join of a team with NO retained record (spec §4-K5 pin 4).
    // ⛔ AND THE TARGET IS THE **TRANSACTION's** id (`a.team_id`, which `ui_prov_join_team` echoes from
    //    `ProvResult`), ⛔ not the cursor, ⛔ not `nearby_sel_id`, ⛔ not the fingerprint the screen printed.
    void create_result_gesture() {
        // ★ §UI-17 keyrecv (2026-08-25): the GRANT RECEIPT's own landing is checked FIRST and leaves the flow — see
        //   `team_key_note_ack_landed`. It cannot collide with K5's offer below (that arm requires `team_joined`).
        if (team_key_note_ack_landed()) return;
        // ★ §UI-16 K6 (QG, 2026-08-25): ...and the FULL-KEYRING receipt's landing is its sibling. ⛔ The two cannot
        //   collide — one requires `team_key_received`, the other `team_key_unsaved` — and neither can collide with
        //   K5's offer below, which requires `team_joined`.
        if (team_key_full_ack_landed()) return;
        const UiProvAnswer a = _st.prov_answer;       // read BEFORE the entry that retires it
        if (a.outcome == UiProvOutcome::team_joined && a.saved_key && a.team_id != 0) {
            enter_provision(Provision::saved_key);
            _st.saved_key_team = a.team_id;           // ★ the joined team's identity, whole (U2)
            return;
        }
        // ★★★★ §UI-16 K6 — **`KEYRING FULL`'s ACKNOWLEDGEMENT ENTERS THE SAVED-KEY LIST**, and every word of what
        //      it does NOT do is the ruling:
        //        ⛔ it does not choose a VICTIM — the list opens on its first row with nothing selected;
        //        ⛔ it does not DELETE anything — opening the list is a read, and the removal still costs a row
        //           selection, an irreversible confirmation and a `double` on `FORGET KEY`;
        //        ⛔ it does not REPLAY the create — **two explicit transactions, never one disguised one**: after a
        //           successful forget the operator retries `CREATE TEAM` himself. A create resumed here would be
        //           the atomic-across-two-records act the ruling forbids, and a failed one could then destroy an
        //           unrelated saved key on the way.
        //      ⇒ what the operator gains is that the refusal lands him WHERE the dead end can be resolved, instead
        //        of on a menu that says nothing about why four keys are one too many.
        // ⛔ THE CONDITION IS THE **TYPED** FLAG (`a.keyring_full`, set from `ProvErr::keyring_full`), ⛔ never a
        //    comparison of `reason`'s display text — see `UiProvAnswer::keyring_full`.
        // ⓘ THE LIST IS READ HERE, on the transition, exactly as the menu row reads it: one flash read, once.
        if (a.outcome == UiProvOutcome::refused && a.keyring_full) {
            load_saved_keys();
            enter_provision(Provision::saved_keys);
            return;
        }
        enter_provision(Provision::menu);
    }
    // ★★★ THE OFFER'S TWO GESTURES — `nearby_confirm`'s shape, a fifth time (U3): `short` TOGGLES and `double`
    //     PERFORMS THE SELECTED ONE, and ⛔ a `double` on BACK may NOT fall through into the act. The two branches
    //     are separate for the reason the delete modal keeps them separate: one press must never be able to mean
    //     the other — and here "the other" installs a secret.
    // ⛔⛔ `BACK` PERFORMS **NOTHING AT ALL** (spec §4-K5 pin 2): no seam call, no keyring read, no write, no
    //     airtime. ★ IT CHANGES **NO KEY STATE** — ⛔ nothing installed and ⛔ nothing cleared — and the retained
    //     record stays exactly as it was found, byte for byte, with zero writes, which is what the suite counts.
    //     ⓘ *"Changes no key state"* rather than *"leaves the node keyless"*: a membership change under the offer
    //     can leave the CURRENT team's key legitimately live, and declining an offer about ANOTHER team may never
    //     destroy it. It lands on the MENU, i.e. exactly where the acknowledgement that opened this screen would
    //     have landed, so declining costs the operator nothing.
    void saved_key_gesture(Gesture g) {
        if (g == Gesture::short_press) { prov_confirm_toggle(); return; }
        if (_st.prov_confirm == ProvConfirm::back) { enter_provision(Provision::menu); return; }
        run_use_saved_key();
    }
    // ★★★★ THE ACT, AND THE ORDER OF ITS STATEMENTS IS §8 PIN 2 EXACTLY AS THE OTHER THREE ACTS' IS: the service
    //      RUNS, RETURNS, and only then does the screen move and the verdict land. ⛔ There is no path that shows
    //      `TEAM KEY ACTIVE` before `perform()` came back with `saved_key_used`.
    // ⛔ A NULL SEAM, OR AN OFFER THAT NAMES NO TEAM, REFUSES OUT LOUD (C2) — `run_create_team`'s and
    //    `run_join_team`'s own arm, a third time: a `double` that changed no pixel is a dead button. ⓘ The refusal
    //    is `saved_key_failed`, so the panel says `KEY NOT INSTALLED` (S-39) — which is TRUE, because nothing was
    //    installed. ⓘ It names the ACT's outcome and ⛔ not the node's key inventory, so it stays true whatever key
    //    the node happens to hold (the withdrawal of `NO TEAM KEY` is recorded at the lexeme's declaration).
    // ⛔⛔ AND BOTH TERMS ARE **UNREACHABLE FROM THIS LAYER TODAY**, MARKED RATHER THAN LEFT TO LOOK LIKE COVERAGE
    //     ([[meshroute-mark-done-vs-missing-in-code]]): the only entry to this screen is `create_result_gesture`,
    //     which required a NON-NULL seam (it produced the `team_joined` answer) and a NON-ZERO id (a term of its
    //     condition). ⇒ ⛔ no gesture sequence drives this `else`, ⛔ no mutation of it can redden, and it is a
    //     FLOOR for the day another caller reaches the act — exactly the shape `run_join_team`'s zero clause has.
    //     The equivalent floor IS measured one layer down, in `ui_prov_use_saved_key`, where it is reachable.
    void run_use_saved_key() {
        UiProvAnswer a{};
        if (_prov && _st.saved_key_team != 0) {
            UiProvIntent in{};
            in.op      = UiProvOp::use_saved_key;
            in.team_id = _st.saved_key_team;     // ★ the JOINED team's identity, whole (U2)
            a = _prov->perform(in);
        } else {
            a.outcome = UiProvOutcome::saved_key_failed;
            a.reason  = "no service";
        }
        enter_provision(Provision::create_result);
        _st.prov_answer = a;
    }
    // ================================================ §UI-16 K6 — SAVED-KEY RETENTION MANAGEMENT (§4-K6)
    // ⛔⛔ **RETENTION MANAGEMENT, ⛔ NEVER "KEY ROTATION"** — the ruling's own first sentence, restated where the
    //     flow lives. Nothing below re-keys a team; it removes ONE retained record the operator named and confirmed.
    // ★★ THE ONE READ OF `/mrteams`, and it happens on a TRANSITION (see `provision_menu_gesture`, and again on the
    //    result's acknowledgement). ⛔ A null seam is a REAL STATE and fails closed: `served` stays false, so the
    //    screen offers no row and SAYS so (`saved_keys_head` -> `NO KEYRING`).
    void load_saved_keys() {
        _st.saved_keys = _prov ? _prov->saved_keys() : mrfw::SavedKeyList{};
    }
    // `short` CYCLES the retention list and ⛔ never walks out of the screen (the sub-view rule, three menus deep);
    // `double` on BACK returns to the PROVISION MENU, and `double` on a key row opens the row's OWN landing.
    // ★★★★ **WHICH LANDING IS DECIDED HERE, BY THE ROW'S `active` FACT, AND THE TWO ARE DIFFERENT SCREENS.** An
    //      ACTIVE row lands on `ACTIVE KEY` / `CANNOT FORGET` (S-43), which offers ⛔ no destructive action to
    //      select at all; an INACTIVE row lands on the irreversible confirmation, which opens on `BACK`. ⛔ Opening
    //      either PERFORMS NOTHING — no service call, no write, no eviction (spec §4-K6 pins 1 and 2).
    // ⛔ THE `active` FACT IS THE SERVICE'S (`mrfw::saved_key_is_active`, answered against the `/mrcfg` binding when
    //    the list was read), ⛔ never inferred here from membership or from "a key is present" — and it is asked
    //    AGAIN, of the PERSISTED record, by `forget` at the instant of the act. This screen chooses a LANDING; the
    //    service owns the PROTECTION.
    void saved_keys_select_gesture(Gesture g) {
        const SavedKeySelList l = saved_keys_sel_rows(_st.saved_keys);
        if (g == Gesture::short_press) {
            // ⓘ SPELLED OUT rather than shared with the identical lines above: the lists differ, and one function
            //   branching on the arm is how a press eventually acts on another screen's row.
            if (l.n) _st.cursor = uint8_t((_st.cursor + 1) % l.n);   // CYCLES — the sub-view rule
            _st.dirty = true;
            return;
        }
        SavedKeySelRow r{};
        if (!l.at(_st.cursor, r)) return;                            // fails closed — see SavedKeySelList::at
        if (r.back) { enter_provision(Provision::menu); return; }
        // ★★★ THE REMOVAL IS KEYED ON THE ROW'S OWN **FULL 32-BIT** `team_id`, ⛔ never the cursor index (§B66)
        //     and ⛔ never the fingerprint the confirmation prints (spec §3 P-7, pin 7): the record removed is the
        //     record the panel drew, and it rides the row WHOLE from the enumeration (U2). ⓘ It is written for BOTH
        //     landings — the protected screen shows the full id too, and must show the row that was selected.
        // ⚠ AND THE WORDING ABOVE IS DELIBERATELY **NOT** `nearby_select_gesture`'s, which is a real constraint
        //   rather than style: `--target=model`'s landed control N06 anchors on that arm's `if (r.back) …` line
        //   PLUS the comment under it, so a byte-identical twin here makes it match TWICE and be reported VACUOUS —
        //   a landed control silently retired by a new slice. ⓘ MEASURED, ⛔ not anticipated: the first full `model`
        //   pass of this slice reported exactly that (`N06 … match count 2`).
        // ⛔ THE ORDER IS LOAD-BEARING: `enter_provision` RETIRES `forget_team` (and re-anchors the cursor and the
        //    BACK default), so the assignment must FOLLOW it — the `saved_key_team` precedent, one flow up.
        enter_provision(r.key.active ? Provision::saved_keys_active : Provision::saved_keys_confirm);
        _st.forget_team = r.key.team_id;
    }
    // ★★★★ THE IRREVERSIBLE CONFIRMATION — the `InboxAction`/create/join/nearby/K5 pair a SIXTH time (U3): `short`
    //      TOGGLES and `double` PERFORMS THE SELECTED ONE, and ⛔ a `double` on BACK may NOT fall through into the
    //      act. The two branches are separate for the reason the delete modal keeps them separate: one press must
    //      never be able to mean the other — and here "the other" destroys a secret no seed can re-derive.
    // ⛔ ITS LANDING IS THE **LIST**, ⛔ NOT THE MENU: BACK returns to the screen the operator was choosing on,
    //    exactly as the static-join and nearby confirmations return to theirs. ⓘ Re-entering `saved_keys` does ⛔
    //    NOT re-read the keyring — `load_saved_keys` runs on the `menu -> saved_keys` transition and on the result's
    //    acknowledgement alone — so a change of mind costs no flash read and cannot re-order the list.
    // ⛔⛔ `BACK` PERFORMS **NOTHING AT ALL**: no seam call, no keyring read, no write, no eviction, and the four
    //     records stay byte-for-byte as they were found (spec §4-K6 pin 1 — a full store with an UNCONFIRMED
    //     selection is exactly the state that must cost zero).
    void saved_keys_confirm_gesture(Gesture g) {
        if (g == Gesture::short_press) { prov_confirm_toggle(); return; }
        if (_st.prov_confirm == ProvConfirm::back) { enter_provision(Provision::saved_keys); return; }
        run_forget_key();
    }
    // ★★★★ THE ACT, AND THE ORDER OF ITS STATEMENTS IS §8 PIN 2 EXACTLY AS THE OTHER FOUR ACTS' IS: the service
    //      RUNS, RETURNS, and only then does the screen move and the verdict land. ⛔ There is no path that shows
    //      `KEY FORGOTTEN` before `perform()` came back with `key_forgotten`.
    // ⛔⛔ THE INTENT CARRIES THE **FULL 32-BIT ID** AND NOTHING THE PANEL DERIVED: ⛔ not the cursor, ⛔ not the
    //     fingerprint, ⛔ not a re-read of anything. A short-fingerprint collision therefore cannot reach the wrong
    //     record — the value that travels is the value the enumeration produced (spec §4-K6 pin 7).
    // ⓘ A NULL SEAM, OR A SELECTION THAT NAMES NO TEAM, REFUSES OUT LOUD (C2) — the dead-button complaint the other
    //   four acts' arms are built against. ★ The 0 clause is a REAL floor and not decoration: `forget(0)` is
    //   refused by the service too, and a screen that can destroy a stored secret must be unable to reach one with a
    //   wildcard, whichever caller arrives here.
    void run_forget_key() {
        UiProvAnswer a{};
        if (_prov && _st.forget_team != 0) {
            UiProvIntent in{};
            in.op      = UiProvOp::forget_key;
            in.team_id = _st.forget_team;        // ★ the row's identity, whole (U2)
            a = _prov->perform(in);
        } else {
            a.outcome = UiProvOutcome::key_forget_failed;
            a.reason  = "no service";
        }
        enter_provision(Provision::saved_keys_result);
        _st.prov_answer = a;
    }
    // ★★★ THE VERDICT'S ACKNOWLEDGEMENT, AND IT **RE-READS THE KEYRING** — which is the whole of the ruling's
    //     *"returns to the refreshed list"*. ⛔ Returning to the FROZEN copy would show the record that was just
    //     removed still standing, i.e. a panel contradicting an act it had just reported as complete.
    // ⛔ IT RE-READS ON **EVERY** ARM, including the failures, and that is deliberate: after a refusal or a failed
    //    save the store's true contents are exactly what the operator needs to see, and a read costs no write.
    // ⛔ NOTHING IS RE-RUN HERE (the terminal-screen rule every result arm carries): the act is over, and a second
    //    `double` may not remove a second key.
    void saved_keys_result_gesture() {
        load_saved_keys();
        enter_provision(Provision::saved_keys);
    }
    // ================================================ §UI-16 N4 — the BOUNDED INVITATION WINDOW (§3.6.4 point 1)
    // ★★★★ THE OPEN, AND IT IS **TWO** FACTS ESTABLISHED TOGETHER: the two-authority SNAPSHOT and the window's own
    //      DEADLINE. Both belong to the OPENING, so they are written by one function called from one place — a
    //      deadline armed somewhere else would eventually be armed twice, and a snapshot taken somewhere else
    //      would eventually be taken late (which is the mutation spec §4-N4 names).
    // ⛔⛔ AND IT DOES ⛔ **NOT** TOUCH `_last_input_ms` — the §UI-17 S8 rule verbatim: that field means "the
    //     OPERATOR acted", it is written only by a real gesture and the first-tick seed, and it drives the panel
    //     blank. ✅ OQ-3 ruled that the window does NOT hold the panel lit: the panel blanks normally after
    //     `kBlankMs` of silence, the WINDOW SURVIVES the blank, and an unfinished CONFIRMATION does not.
    //     ⓘ The gesture that opened the window did of course stamp `_last_input_ms` at the top of `on_gesture` —
    //     that is the PRESS, not the window, and the difference is measurable: hold the window open past
    //     `kBlankMs` without pressing anything and the panel must go dark with the window still up.
    void load_invite(const UiSnapshot& s) {
        _st.invite      = invite_snapshot_take(s.member, s.team_shown);
        _invite_until_ms = s.now_ms + kInviteWindowMs;
    }
    // ★★★★ THE LOCAL REFRESH IS THE LIST ITSELF (F-14 / R-10): the rows are built from the LIVE snapshot every
    //      time they are needed — here for the cursor's bound, in `draw_provision_screen` for the pixels — so a
    //      member that appears mid-window appears on the panel with ⛔ no scan, ⛔ no query and ⛔ nothing
    //      transmitted (`rt_team_at` / `team_key_of_id` are both `const`, and the publisher already walked them
    //      for the TEAM screen). ⛔ It is deliberately NOT a frozen copy: NEARBY is frozen per entry (R-10's
    //      other half) because a team walking into range must not move a row under the cursor, while the invite
    //      window's whole purpose is to show somebody who ARRIVES while it is open.
    // ★ THE SELECTION IS THEREFORE FROZEN AT THE `double` AND ⛔ NOT HELD AS AN INDEX: a refresh between the two
    //   presses may re-index the list, so what the confirmation carries is the row's own identity (P-7d).
    void invite_select_gesture(Gesture g, const UiSnapshot& s) {
        const InviteSelList l = invite_sel_rows(_st.invite, s.member, s.team_shown);
        if (g == Gesture::short_press) {
            // ⓘ SPELLED OUT rather than shared with the identical lines above: the four lists are different
            //   lists, and one function branching on the arm is how a press eventually acts on another's.
            if (l.n) _st.cursor = uint8_t((_st.cursor + 1) % l.n);   // CYCLES — the sub-view rule
            _st.dirty = true;
            return;
        }
        InviteSelRow r{};
        if (!l.at(_st.cursor, r)) return;                            // fails closed — see InviteSelList::at
        if (r.back) { enter_provision(Provision::menu); return; }
        // ★★★ THE FREEZE (F-14, spec §4-N4's mutation): the hash AND the id of the row the cursor was on, copied
        //     WHOLE out of the row's own identity — ⛔ never the cursor index (a refresh re-indexes) and ⛔ never
        //     re-derived from the six-character fingerprint the row printed (that token is the low 24 bits and
        //     255 other peers share them; [[B48]]'s class).
        _st.invite.sel_hash = r.cand.key_hash32;
        _st.invite.sel_id   = r.cand.id;
        // ★ N5'S SIDE-EFFECT-FREE PREFLIGHT. Merely entering the row asks the same aged cache question the grant
        //   asks and emits NOTHING. A name is deliberately absent from this condition: descriptive text never
        //   enables an airtime-and-secret action.
        enter_provision(invite_grant_preflight(_invite_dev, _st.invite.sel_hash)
                      ? Provision::invite_confirm : Provision::invite_need_pubkey);
    }
    // ★★★★ THE GRANT-READY CONFIRMATION. REJECT is the zero/default choice and remains the local act; ✅ §UI-16 N6
    //      LANDS THE OTHER ARM — the one press on this device that ships a PRIVATE KEY, so it costs the ruled
    //      `short` (REJECT -> GRANT KEY) and then a `double`, and neither press alone can reach it (P-13).
    void invite_confirm_gesture(Gesture g, const UiSnapshot& s) {
        if (g == Gesture::short_press) { prov_confirm_toggle(); return; }
        if (_st.prov_confirm == ProvConfirm::invite_reject) { run_invite_reject(); return; }
        // ★★★★ N6 PIN 8 — **THE GRANT IS UNREACHABLE WITH THE WINDOW CLOSED**, and the guard is HERE rather than
        //      left to `tick_invite` because THE TICK RUNS AFTER THE GESTURE (`mr_ui_tick`: on_gesture -> on_tick ->
        //      step). A `double` landing after the five minutes but before the closing tick would otherwise ship a
        //      private key out of a window that had already expired — the one ordering in which the bound the owner
        //      ruled is not a bound at all. ⇒ the expiry is applied first, and the operator is told (S-16).
        if (!window_active(s.now_ms)) { enter_provision(Provision::invite_closed); return; }
        // ★★★ THE TARGET IS THE **FROZEN `key_hash32`** AND ⛔ NEVER THE DISPLAY NAME (P-7d): the name is a render
        //     input on this very screen, it is MUTABLE (`lib/core/node_hashlocate.cpp`), and a member whose name
        //     changes between the row and this press is still granted THE SAME KEY.
        // ⛔ §UI-16 N6b: ⛔ the FROZEN `_st.invite.sel_id` is deliberately NOT passed — the correlation's second
        //    term is the id the CORE resolved at send time (see `run_invite_grant`).
        run_invite_grant(_st.invite.sel_hash);
    }
    // ★★★ THE EXPLICIT REQUEST CONFIRMATION. BACK is selected on entry and sends nothing. Only the other arm's
    //      double constructs and forwards the existing typed reqpubkey command, then enters the waiting screen.
    void invite_need_pubkey_gesture(Gesture g) {
        if (g == Gesture::short_press) { prov_confirm_toggle(); return; }
        if (_st.prov_confirm == ProvConfirm::back) { enter_provision(Provision::invite); return; }
        // ★★★ THE WAITING SCREEN IS A CLAIM ABOUT A SUCCESSFULLY STARTED (OR LOCALLY COMPLETED) WORKFLOW, SO ONLY
        //     ONE MAY REACH IT — ⛔ "started" here is `invite_request_started`'s term, NOT `CmdResult::accepted`
        //     (QG blocker, 2026-08-24): with no attached seam, or against a synchronous refusal, `WAITING FOR PUBKEY`
        //     would be waiting for an answer to a question nobody asked — and pin 5 then says a timeout leaves that
        //     screen up for ever. ⛔ The verdict is `firmware_ui_invite.h`'s (`invite_request_started`), never this
        //     call site's and never the device TU's.
        // ⓘ A REFUSAL STAYS PUT, ⛔ AND IS DELIBERATELY NOT GIVEN A WORD: `REQUEST PUBKEY` remains selected and a
        //   second double retries. §8's inventory carries no refusal lexeme for this screen, and inventing one here
        //   would be an unruled string on an owner-ruled screen.
        if (invite_issue_reqpubkey(_invite_dev, _st.invite.sel_hash))
            enter_provision(Provision::invite_wait_pubkey);
    }
    // ★★★★ `REJECT`, THE WINDOW'S ONLY MEMBERSHIP-SHAPING ACT UNTIL N6. N5's explicit pubkey request exists beside
    //      it, but that request neither grants nor rejects anything. WHAT REJECT CHANGES IS RAM THAT dies with the
    //      window: the candidate's HASH joins the
    //      volatile handled set (F-13) and the screen returns to the list, where the refresh no longer offers it.
    // ⛔⛔ IT TOUCHES ⛔ NO CORE, RADIO, MEMBERSHIP, KEY OR NV STATE — there is no seam call here at all, which is
    //     why the native cases assert it on the SEAM's call count and the store's write count rather than on a
    //     screen. ★ And the set is DISCARDED when the window closes, so the same candidate returns after a close
    //     and re-open — which is the ruling's own pin, in both directions.
    // ⓘ IT IS KEYED BY THE FROZEN HASH (P-7d), ⛔ never by the row the cursor happens to be on now: a refresh
    //   between the two presses would otherwise reject somebody else.
    void run_invite_reject() {
        (void)invite_handled_add(_st.invite, _st.invite.sel_hash);
        enter_provision(Provision::invite);
    }
    // ★★★★ §UI-16 N6 — **THE GRANT**, AND EVERY DECISION IN IT IS SOMEBODY ELSE'S: the plane, the eight-arm outcome
    //      mapping and the `queued`/`parked` split are `firmware_ui_invite.h`'s (pure, natively driven), the seal
    //      and the send are `Node::team_key_grant_send`'s (ONE forward — ⛔ no second send path, no new payload, no
    //      new frame type, no wire byte). What lives HERE is the ORDER: perform, record the verdict, then move.
    // ⛔ NOTHING IS CLAIMED WHEN NOTHING RAN (C2): with no seam attached, or with no frozen identity, the perform
    //    returns false and the screen STAYS PUT — ⛔ it does not enter a result screen with a word for an act that
    //    never happened, which is the N5 QG blocker's shape one screen over.
    // ★ THE VERDICT IS WRITTEN **BEFORE** `enter_provision`, deliberately: that primitive discards the window's
    //   whole state (the two authorities, the handled set and the frozen selection) on the way to a non-window arm,
    //   so the verdict must already carry its own identity — which is exactly why `InviteGrantResult` holds a hash.
    // ★★★★ §UI-16 N6b (2026-08-24) — **THE FROZEN `dst_id` IS NO LONGER PASSED, AND ⛔ IT MAY NOT BE PUT BACK.**
    //      ⛔ WITHDRAWN, KEPT VISIBLE: `run_invite_grant(target_hash, dst_id)` handing `_st.invite.sel_id` to the
    //      perform, which stored it as the correlation's second term. The core resolves the destination AT SEND
    //      TIME, so a member that re-ran team-DAD inside the window was granted on its NEW id while this screen
    //      waited for a `send_aired` addressed to the id frozen at selection — a wait that never ends.
    //      ⇒ the verdict's `dst` is the CORE's answer, and `_st.invite.sel_id` stays what F-14 made it: the frozen
    //      selection's second half, ⛔ not an addressing or correlation input.
    void run_invite_grant(uint32_t target_hash) {
        InviteGrantResult r{};
        if (!invite_grant_perform(_invite_dev, target_hash, r)) return;
        _st.grant = r;
        enter_provision(Provision::invite_result);
    }
    // ★★★★ THE WINDOW EXPIRES BY ITSELF (spec §3 P-11), AND EXPIRY ⛔ GRANTS, REVOKES AND REWRITES NOTHING: it
    //      moves a screen and drops RAM. Across open -> expire -> re-open the membership, the content key and
    //      the member set are byte-identical, which is a native pin AND a metal step.
    // ⓘ ONE CALL, from `on_tick`, and it is a no-op on every other screen — the window is the only thing in this
    //   model with a deadline of its own besides the emergency hold and the message wake.
    void tick_invite(const UiSnapshot& s) {
        if (!provision_is_invite(_st.provisioning)) return;
        if (window_active(s.now_ms)) return;
        enter_provision(Provision::invite_closed);
    }
    // ================================================ §UI-16 K7 ([[B245]]) — THE ROSTER GRANT'S **ENTRY**, AND ONLY
    // ★★★★ **THIS FUNCTION OPENS A DOOR; IT DOES NOT BUILD A ROOM.** Everything past it is the LANDED N5/N6 chain,
    //      reached verbatim and byte-for-byte: the side-effect-free preflight (`invite_grant_preflight` — the
    //      GRANT'S OWN BAR, reused), the `NEED PUBKEY` / `REQUEST PUBKEY` / `WAITING FOR PUBKEY` ceremony
    //      (`invite_need_pubkey_gesture`), the REJECT-default confirmation with the full `0x%08lX` hash
    //      (`invite_confirm_gesture`), the ONE forward to `Node::team_key_grant_send` on `Plane::TEAM`
    //      (`run_invite_grant` -> `invite_grant_perform`), the eleven-arm outcome mapping and the `{dst, ctr}`
    //      `send_aired` correlation. ⛔ NO new screen, lexeme, send path, state machine or outcome word is added by
    //      §K7, and ⛔ nothing in `src/firmware_ui_invite.h` is touched by it at all — which is what makes "the
    //      invite window is byte-identical" (§K7 pin 3) a DIFF, not an argument.
    // ★★★★ **WHY THE TOP-LEVEL SCREEN MOVES, STATED BECAUSE IT IS THE OTHER HALF OF THE PLACEMENT DECISION.** The
    //      chain's four screens are `Provision` arms, and the provisioning sub-view is dispatched in exactly ONE
    //      place — `draw_settings_screen` (`src/firmware_ui.cpp`), i.e. only while `Screen::settings` is up — while
    //      `settings_follow_screen` force-closes provisioning the moment the screen is anything else. ⇒ reaching
    //      those screens from TEAM means going where they live. The alternatives were both refused: rendering them
    //      from `draw_team_screen` too would FORK an owner-ruled confirmation into a second, unmutatable copy
    //      (§B115), and hoisting the dispatch into `draw_frame` is a REFACTOR of shipped code, which may not ride a
    //      feature slice (C1). ⓘ §UI-17 R-4 makes the rail honest about it: the rail names the BODY.
    // ★★★ **NO SNAPSHOT IS TAKEN, AND THAT IS DELIBERATE (F-11 rule 1).** The window state is CLEARED to a
    //     `taken == false` carrier, so this entry contributes NOTHING to the two authorities and produces NO
    //     candidate list — `invite_is_new` answers *not new* for every member, which is the fail-CLOSED direction.
    //     ⇒ the F-11 diff and the F-13 handled set are untouched by the roster grant in both directions: it neither
    //     reads them nor leaves anything in them, and a `REJECT` here fills an empty per-entry set that dies with it.
    // ★★ THE FIVE-MINUTE BOUND IS THE RULED ONE, REUSED (`kInviteWindowMs`, spec §9 R-3) — ⛔ not a second timer.
    //    It is what makes `invite_confirm_gesture`'s *"the grant is unreachable with the window closed"* guard (N6
    //    pin 8) apply here too: an approval left standing costs the same bounded time whichever door it was opened
    //    from, and an operator who walks away does not leave one press between a bystander and a private key.
    // ⛔ IT FAILS CLOSED (C2): with the act not offered, or with no frozen identity, NOTHING happens — the sub-view
    //    stays exactly where it was and ⛔ no screen is entered for an act that could not be performed.
    void run_roster_grant(const UiSnapshot& s) {
        if (!_st.compose_grant_row) return;
        const uint32_t target = _st.compose_grant_hash;
        if (target == 0) return;
        const uint8_t peer = _st.compose_peer;               // read BEFORE the sub-view is retired
        close_compose();
        // ★ THE SCREEN MOVES FIRST, THEN THE LIST VIEW IS RETIRED THROUGH ITS OWN PRIMITIVES (U1) — the entered
        //   TEAM list may not outlive the screen, and the pick may not survive as something a later press acts on.
        _st.screen = Screen::settings;
        list_follow_screen();
        note_team_cursor(s);
        // ★★★ THE FROZEN SELECTION, WRITTEN INTO THE WINDOW CARRIER THE CHAIN ALREADY READS (U2) — ⛔ never a
        //     second field for the same fact. `sel_hash` is the identity every downstream screen and the act
        //     itself carry; `sel_id` is F-14's second half and stays exactly what it is there: the selection's
        //     record, ⛔ not an addressing or correlation input (§UI-16 N6b).
        _st.invite = InviteWindow{};
        _st.invite.sel_hash = target;
        _st.invite.sel_id   = peer;
        _invite_until_ms    = s.now_ms + kInviteWindowMs;
        // ★ N5's PREFLIGHT, THE GRANT'S OWN BAR — asked here exactly as `invite_select_gesture` asks it one door
        //   over, and it emits NOTHING. A name is deliberately absent from this condition: descriptive text never
        //   enables an airtime-and-secret action ([[B48]]'s class).
        enter_provision(invite_grant_preflight(_invite_dev, target)
                      ? Provision::invite_confirm : Provision::invite_need_pubkey);
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
    // ★★★ §UI-17 S1 — AND ONLY WHILE THE LIST IS ENTERED. A PASSIVE preview records NO pick: nothing there was
    //     pointed at, so `activate` on it has nothing to send to and cannot queue (spec §1.2's *"while PASSIVE
    //     nothing is picked"*). ⛔ Landing on TEAM and recording row 0 is exactly the defect this slice removes — the
    //     panel showed `>` beside a teammate the operator never chose, one `double` from a DM.
    // ⓘ The `cursor < s.team_shown` bound below ALSO excludes the `BACK` row, which is right: `BACK` is not a
    //   teammate, so standing on it picks nobody. It raises no refusal either — see `sync_team_cursor`, which returns
    //   early on an invalid pick.
    // ★★★ §UI-17 S1 — THE THREE ANSWERS ARE `list_note_kind`'s, ⛔ NOT re-derived here (QG-RULED 2026-08-21): the
    //     INBOX write side asked the same three questions, so a guard written twice is a guard one mutation can only
    //     half protect. This function is now the TEAM-plane DATA of that one decision and nothing else.
    void note_team_cursor(const UiSnapshot& s) {
        switch (list_note_kind(_st.screen == Screen::team,
                               _st.list_view == ListView::interactive, _st.cursor, s.team_shown)) {
            case ListNote::record:
                _team_sel_id = s.team[_st.cursor].id; _team_sel_valid = true; _st.team_pick_gone = false;
                return;
            case ListNote::retire:                       // the screen was left, or the cursor rests on `BACK`
                _team_sel_valid = false; _st.team_pick_gone = false; return;
            case ListNote::keep:                         // a PASSIVE preview: nothing pointed at, nothing recorded
                _team_sel_valid = false; return;
        }
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
    // ★★★ §UI-17 S1 — and only while the list is ENTERED, exactly as `note_team_cursor` is. See it.
    // ★★★ §UI-17 S1 — the SAME three answers, from the SAME pure decision (`list_note_kind`); this function is the
    //     INBOX-plane data of it. See `note_team_cursor`.
    void note_inbox_cursor(const UiSnapshot& s) {
        switch (list_note_kind(_st.screen == Screen::inbox,
                               _st.list_view == ListView::interactive, _st.cursor, s.inbox_shown)) {
            case ListNote::record:
                // ⓘ ...AND THE ROW MUST STILL BE IDENTIFIABLE, which is INBOX DATA rather than a shared decision and
                //   is why it stays here: `seq == 0` has no identity (see InboxRow), so it is NOT selected and the
                //   refusal path applies — rather than a selection that could only ever resolve wrongly.
                if (s.inbox[_st.cursor].seq != 0) {
                    _inbox_sel_kind = s.inbox[_st.cursor].kind; _inbox_sel_seq = s.inbox[_st.cursor].seq;
                    _inbox_sel_valid = true; _st.inbox_pick_gone = false;
                    return;
                }
                _inbox_sel_valid = false; return;
            case ListNote::retire:
                _inbox_sel_valid = false; _st.inbox_pick_gone = false; return;
            case ListNote::keep:
                _inbox_sel_valid = false; return;
        }
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
    // ★ ONE exit for the modal (U1/U2). ⓘ CORRECTED 2026-08-21 (§UI-17 S2, V1): it read "Five call sites reach it —
    //   `back`, the terminal state's acknowledgement, `on_tick`'s timeout, a successful delete, and §UI-7D's
    //   `long_arm`". §9 R-1 deleted the `on_tick` timeout, so FOUR reach it: `back`, the terminal state's
    //   acknowledgement, a successful delete ([[B233]]'s `erased` arm), and §UI-7D's `long_arm`. Every field must
    //   still be cleared together or a re-opened modal would render one record's page under another's header.
    // ★★★★ §UI-17 S2 — **THE ONE WAY OUT OF `blanked`**, and it exists so that two facts cannot be separated: the
    //      panel lighting up, and the DISPLAY CADENCE restarting from that instant. ⛔ It does NOT move
    //      `_st.detail_page`: §3.3 says the operator gets back the interaction they left, so the page they were
    //      reading is the page that must be on the glass.
    // ⛔⛔ WHY THE RESTART IS NOT OPTIONAL, AND IT IS THE REAL LOOP'S ORDER THAT MAKES IT SO (`firmware_ui.cpp`:
    //     `on_gesture(...)` then `on_tick(s)` **against the same snapshot, in the same pass**): without it
    //     `_detail_page_at_ms` is still the pre-blank stamp, so `elapsed >= kDetailPageMs` is already true and the
    //     wake pass ITSELF turns the page — before a single frame has shown the operator what they came back to.
    //     ⇒ the dark-suspension above and this restart are INDEPENDENT halves of one property; each has its own
    //     mutation (S05/S06) because either one alone still loses the page.
    // ⓘ `_last_input_ms` is deliberately NOT touched here — `on_gesture` already stamped it for the press, and
    //   `on_reply` deliberately leaves it alone (its own note says why). This function owns the DISPLAY clock only.
    void unblank(uint32_t now_ms) {
        _st.blanked = false;
        _detail_page_at_ms = now_ms;
    }
    // ★★★★ §UI-17 S2 — **"IS THE PANEL GOING DARK ON THIS TICK?"**, asked in ONE place and answered for TWO callers
    //      (U1): the blank transition itself, and the page cadence that must stand aside for it. ⛔ A SECOND COPY OF
    //      THIS PREDICATE IS THE WHOLE HAZARD — the cadence would go on turning pages on exactly the tick the blank
    //      hides them the moment the two spellings drifted, which is the boundary this function exists to close.
    // ⓘ It is a QUESTION, not an action: it moves nothing, so asking it early costs nothing and the transition below
    //   remains the only writer of `blanked`. ⇒ the deadline is unchanged and unmoved; only the ORDER of two effects
    //   on one tick is decided by it.
    // ★★★★ §UI-17 S8 — **`!wake_active` IS THE THIRD TERM**, and it is `hold_active`'s shape verbatim (U3): a
    //      DEADLINE compared wrap-safely, guarded by an ARMED flag so no arithmetic value is reserved (§B74). ⛔ It
    //      is what makes the wake last a full attention window instead of one frame — see `on_msg_wake`.
    // ⓘ ONE predicate, TWO readers, exactly as `blank_due`'s own note says: the wake therefore also keeps the detail
    //   page cadence running (a lit panel turns its pages), and it re-blanks `kBlankMs` after the MESSAGE, not after
    //   the operator's last press — the two clocks stay independent in both directions.
    // ⓘ THE EDGE IS THE PRESS's, NOT `hold_active`'s, AND THE ONE-LINE DIFFERENCE IS DELIBERATE: `left != 0` excludes
    //   the instant the deadline ARRIVES, so a message-woken panel blanks on exactly the tick a press-woken one would
    //   (`elapsed(now, _last_input_ms) >= kBlankMs` fires AT the edge). Spec pin 9 is that edge, to the millisecond.
    // ★★★★ AND THE WINDOW IS **BOUNDED ABOVE**, WHICH IS NOT DECORATION — ⛔ MEASURED, in the probe, before it could
    //      reach metal: `hold_active`'s bare "now < deadline" is only wrap-safe for HALF the counter, so an EXPIRED
    //      deadline reads as a FUTURE one again once `now` has run 2^31 ms past it. ⇒ a node that received ONE
    //      message and then nothing would stop blanking ~24.8 days later, for the next ~24.8 days — the F-10 power
    //      regression arriving from the clock instead of from the traffic. `hold_active` is immune only because its
    //      `retained` term clears; this predicate's `_msg_wake_armed` never does, so the BOUND is what closes it: a
    //      live window is at most `kBlankMs` ahead, by construction, and anything further is a stale deadline.
    bool wake_active(uint32_t now_ms) const {
        const uint32_t left = elapsed(_msg_wake_until_ms, now_ms);          // deadline - now, wrap-safe
        return _msg_wake_armed && left != 0 && left <= kBlankMs;            // i.e. now < deadline, at most one window
    }
    // ★★★★ §UI-16 N4 — **IS THE INVITATION WINDOW STILL OPEN?** It is the SHARED IDIOM (U3): a DEADLINE compared
    //      wrap-safely, guarded by a term that CLEARS, and closing AT the edge as `wake_active` does. ⛔ The
    //      clearing term is what makes it
    //      wrap-safe at all — a bare "now < deadline" reads an EXPIRED deadline as a future one again once `now`
    //      has run 2^31 ms past it (the §UI-17 S8 measurement, and [[B239]]'s lesson) — and here that term is
    //      the ARM ITSELF: the expiry lands `invite_closed`, which `provision_is_invite` excludes, so a stale
    //      deadline can never be consulted. ⓘ That is exactly how `hold_active` is immune: its `retained` term
    //      clears too. ⛔ NO ARITHMETIC VALUE IS RESERVED for "no window" (§B74).
    // ⛔⛔ AND IT IS ⛔ NOT A TERM OF `blank_due`: the window does NOT hold the panel lit (✅ OQ-3), it survives
    //     the blank, and `_last_input_ms` is untouched by every line of this feature.
    // ★★★★ `left != 0` IS `wake_active`'s TERM, ⛔ NOT DECORATION: `elapsed(deadline, now)` is ZERO **at** the
    //      deadline, and a bare `< 2^31` reads that zero as "still open" — so the ruled FIVE MINUTES would run for
    //      five minutes and ONE MILLISECOND. The window must have STRICTLY POSITIVE time remaining, which puts the
    //      close on exactly the tick the deadline arrives — the same edge `elapsed(now, _last_input_ms) >= kBlankMs`
    //      already fires on, so the two clocks in this model agree to the millisecond (U3).
    bool window_active(uint32_t now_ms) const {
        const uint32_t left = elapsed(_invite_until_ms, now_ms);     // deadline - now, wrap-safe
        return provision_is_invite(_st.provisioning) &&
               left != 0 && left < (1u << 31);                       // i.e. now < deadline, STRICTLY
    }
    bool blank_due(const UiSnapshot& s) const {
        return !_st.blanked && !hold_active(s.now_ms) && !wake_active(s.now_ms) &&
               elapsed(s.now_ms, _last_input_ms) >= kBlankMs;
    }
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

    // ⓘ §UI-16 K7 — IT TAKES THE SNAPSHOT NOW, and for one reason only: the roster grant arms the ruled
    //   five-minute approval bound from `now_ms` (see `run_roster_grant`). ⛔ It reads NOTHING else from it —
    //   the sub-view's target, its row set and its offer were all FROZEN AT ENTRY, which is the whole point
    //   of `compose_peer`'s own rule.
    void compose_gesture(Gesture g, const UiSnapshot& s) {
        // ★★ UI-7: THE RESULT PHASE. Once a send has been issued the modal shows its OUTCOME instead of the list
        //    (spec §3.2.1/§3.4.1), so there is nothing to walk and nothing to activate — the only thing either gesture
        //    can mean is "I have read it".
        // ★ `double` closes because the spec says so verbatim ("the sub-view closes to its parent on an explicit
        //   `double`, or after a bounded display window").
        // ⓘ CORRECTED 2026-08-21 (§UI-17 S2, V1): that sentence used to end *"— the window is `on_tick`'s kBlankMs
        //   auto-exit"*, and the paragraph below used to justify `short` by *"⛔ The alternative — ignore it — would
        //   let a user tapping `short` hold a modal open indefinitely, since every gesture refreshes `_last_input_ms`
        //   and so postpones the very auto-exit that is supposed to bound it"*. The auto-exit is DELETED (§9 R-1), so
        //   **BOTH presses acknowledging is now the ONLY bounded display window there is** — which strengthens the
        //   rule rather than weakening it.
        // ★ `short` closes too, and that is DERIVED from the shipped gesture contract rather than invented: §3.2's
        //   `short` is "advance within the current list; AT THE END, move to the next screen". The result phase has no
        //   list, so every position is the end. Neither choice can send: this branch queues nothing.
        if (_st.compose_result) {
            if (g == Gesture::short_press || g == Gesture::double_press) close_compose();
            return;
        }
        // ★★★★ §UI-10/11 P3 — **THE RULED MODAL CLOSE, ASKED BEFORE THE PRESS IS APPLIED** (§2's table + §3.2.3:
        //      *"A preset update while a selection-phase compose modal is open closes that modal without sending"*).
        //      It is checked HERE as well as in `on_tick` because `on_gesture` returns early for `Gesture::none`, so
        //      a tick that carries a press reaches the model through THIS path only: without the question here, a
        //      `double` arriving in the very tick a mutation landed would send a row the operator can no longer be
        //      looking at. ⛔ ONE authority, two callers (`preset_generation_moved`), ⛔ never two predicates.
        // ⓘ It sits AFTER the result-phase branch above, which is the other half of the ruling: *"an already-
        //   displayed outcome may finish"* — an outcome is not a selection, and closing it would discard a verdict
        //   the wearer has not read.
        if (preset_generation_moved(s)) { close_compose(); return; }   // ⛔ CONSUMES the press — nothing is sent
        // ★★★ §UI-16 K7 — THE LIST IS NOW RESOLVED BY ITS OWN FUNCTIONS (§B66). ⛔ WITHDRAWN, KEPT VISIBLE:
        //     `const uint8_t n = (dm) ? kDmTextCount : kChannelTextCount;` with `back` identified by
        //     `_st.cursor + 1 == n`. With the grant act absent both express EXACTLY the same list, index for index
        //     — which is why every landed compose case is byte-identical — but a positional `back` beside an
        //     OPTIONAL row is the shape §B66 exists to forbid: one added row and `back` becomes a SEND.
        const bool    dm    = (_st.compose == Compose::dm);
        const bool    grant = _st.compose_grant_row;
        // ★ §UI-10/11 P3 — THE LIST IS THE SNAPSHOT'S PROJECTION, chosen by the sub-view's own kind. ⛔ DM presets
        //   never appear in the channel list and vice versa (§3.2.2), and the choice is made ONCE, here.
        const ComposeList& list = dm ? s.preset_dm : s.preset_ch;
        const uint8_t n     = compose_row_count(list, grant);
        if (g == Gesture::short_press) { _st.cursor = uint8_t((_st.cursor + 1) % n); _st.dirty = true; return; }
        if (g != Gesture::double_press) return;
        switch (compose_row_kind(_st.cursor, list, grant)) {
            case ComposeRow::back:  close_compose(); return;                                                 // `back`
            // ★★★★ §UI-16 K7 — THE ACT. It performs NO grant and maps NO outcome: it OPENS the landed N5/N6 chain,
            //      whose preflight, ceremony, confirmation, one send forward and eleven-arm outcome mapping are
            //      reached VERBATIM (see `run_roster_grant`). ⛔ Nothing is transmitted by this press.
            case ComposeRow::grant: run_roster_grant(s); return;
            case ComposeRow::text:  break;
        }
        // ★★★★ §UI-10/11 P3 / §B66 — **THE ROW'S IDENTITY IS ITS STABLE SLOT, RESOLVED THROUGH THE PROJECTION.**
        //      ⛔ WITHDRAWN AND KEPT VISIBLE: `queue(…, _st.compose_peer, _st.cursor)` — *"THE TEXT INDEX IS THE
        //      CURSOR, AND IT STILL IS: the canned rows occupy `0 .. sendable-1` by construction"*. That was true of
        //      a FIXED table and is FALSE of a configurable one: with `dm1`, `dm4`, `dm8` enabled, cursor 1 is
        //      `dm4` — and the moment the catalog changes, cursor 1 is somebody else's phrase. §3.2.2 forbids
        //      deriving `dmN` from a row index in as many words, and this line is where that forbidding bites.
        // ★★ AND THE GENERATION IS SEALED WITH IT (design §3.3): the request carries the catalog the wearer SAW, so
        //    execution can refuse rather than resolve the same row to newly configured words.
        queue(dm ? SendKind::dm : SendKind::channel_canned, _st.compose_peer,
              compose_row_slot(_st.cursor, list), _st.compose_gen);
        // ★★ UI-7: THE MODAL STAYS OPEN. UI-2 closed it here, which left every `DmState` the spec defines with NO
        //    RENDERER — `DELIVERED to <label>` (the one thing `-a` buys that a channel post can never offer),
        //    `NO KEY`, `NO CONFIRM` — all unreachable on the panel. The cursor is still reset, so a re-opened modal
        //    starts on the first message (H7-02/H7-04), and `compose_peer` is untouched so the result can name who it
        //    went to.
        // ⓘ CORRECTED 2026-08-21 (§UI-17 S2, V1): this line ended *"ⓘ It cannot outlive attention: `on_tick`'s
        //   kBlankMs auto-exit applies to BOTH phases"*. §9 R-1 deleted that auto-exit — the RESULT phase now
        //   survives blanking with the rest of the sub-view, and either press above acknowledges it.
        _st.compose_result = true; _st.cursor = 0; _st.dirty = true;
    }
    // ★ ONE exit for the sub-view (U1/U2). ⓘ CORRECTED 2026-08-21 (§UI-17 S2): it read "Four call sites reach it —
    //   `back`, the result phase's acknowledgement, `on_tick`'s auto-exit and §B101's `long_fire`". The auto-exit is
    //   gone, so THREE reach it: `back`, the result phase's acknowledgement, and §B101's `long_fire`. The phase flag
    //   MUST still be cleared with the modal or a re-opened compose would render an outcome list against a stale
    //   result. It sends nothing, by construction.
    // ⓘ §UI-16 K7 — THE GRANT'S TWO FROZEN FIELDS ARE RETIRED WITH THE SUB-VIEW, as a set and in the one
    //   place (the `clear_settings_note` rule): a re-opened compose that inherited either of them would
    //   offer, or aim, an irreversible act at whoever the LAST sub-view was about.
    // ⓘ §UI-10/11 P3 — the SEALED GENERATION is retired with the sub-view for the same reason and in the same
    //   place: a re-opened compose that inherited it would be comparing against a catalog it never displayed.
    void close_compose() { _st.compose = Compose::none; _st.compose_result = false;
                           _st.compose_grant_hash = 0; _st.compose_grant_row = false; _st.compose_gen = 0;
                           _st.cursor = 0; _st.dirty = true; }
    // ★★★★ §UI-10/11 P3 — **THE ONE QUESTION BEHIND §2's WHOLE MODAL TABLE** (U1: two callers, `on_tick` and
    //      `compose_gesture`, ⛔ never two predicates). It is TRUE only for an OPEN **SELECTION-PHASE** sub-view
    //      whose sealed generation no longer equals the published one.
    //  · a successful CHANGED mutation (incl. `reset all`, OQ-B) stamps the NEXT generation ⇒ TRUE  ⇒ closes;
    //  · an identical no-op writes nothing and changes no generation      ⇒ FALSE ⇒ ⛔ does NOT close;
    //  · a validation or storage failure publishes nothing                ⇒ FALSE ⇒ ⛔ does NOT close;
    //  · an already-displayed outcome (`compose_result`)                  ⇒ FALSE ⇒ may finish.
    // ⛔ EQUALITY, ⛔ NEVER ORDERING (§3.2.3), which is what makes the uint32 wrap harmless — and `compose_gen` is 0
    //    while nothing is open, a value no live catalog can carry, so a closed sub-view can never answer TRUE.
    bool preset_generation_moved(const UiSnapshot& s) const {
        return _st.compose != Compose::none && !_st.compose_result && _st.compose_gen != s.preset_generation;
    }
    // ★★★★ [[B232]] + §UI-17 S1 — **A SCREEN THAT HAS NOT BEEN ENTERED IS ONE ROW**, and that is the whole of "one
    //      press passes the screen": `advance_or_next` sees `n == 1`, so there is nothing to walk and the cycle
    //      advances. It was [[B232]]'s ruling for the SETTINGS closed view and it is §UI-17's for the passive TEAM and
    //      INBOX previews — ⛔ NOT three separate rules, which is why the three arms share ONE predicate.
    // ⛔ THE PASSIVE LENGTH IS **NOT** `shown` CLAMPED: a passive screen does not offer those rows at all, so its
    //    length is not that list's length made shorter — the same statement the closed view's own note made.
    uint8_t list_len(const UiSnapshot& s) const {
        const bool entered = screen_is_entered(_st.screen, _st.settings, _st.list_view);
        // ★ §UI-17 S1: the interactive list is the published rows PLUS the `BACK` row, which is what makes `BACK`
        //   reachable by walking and the walk itself CONTAINED (`advance_or_next`).
        if (_st.screen == Screen::team)  return list_len_of(entered, s.team_shown);
        if (_st.screen == Screen::inbox) return list_len_of(entered, s.inbox_shown);
        // ★ §UI-14: SETTINGS is list-aware exactly like TEAM and INBOX (§3.2: *"`short` walks the list and leaves only
        //   at the end"*), and the length is the ONE row-list construction — never a hand-written count, which is
        //   §B66's defect one screen over: two conditional rows mean the number is not a constant.
        if (_st.screen == Screen::settings) return entered ? settings_row_list(s).n : uint8_t(1);
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
    queue(SendKind::emergency, 0, mrfw::kPresetEmergency, 0);
}

inline void UiModel::tick_emergency(const UiSnapshot& s) {
    if (_emg == Emergency::cancelled && elapsed(s.now_ms, _cancelled_until_ms) < (1u << 31)) { _emg = Emergency::idle; _st.dirty = true; }
    if (_emg == Emergency::blocked && _retry_armed &&
        elapsed(s.now_ms, _retry_at_ms) < (1u << 31)) {                 // wrap-safe "now >= deadline"
        _retry_armed = false; _emg = Emergency::firing; queue(SendKind::emergency, 0, mrfw::kPresetEmergency, 0); _st.dirty = true;
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
