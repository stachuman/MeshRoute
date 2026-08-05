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
// NOT here yet (UI-4, [[meshroute-mark-done-vs-missing-in-code]]): NOTHING correlates outcomes. Every `on_outcome` /
// `on_send_accepted` / `on_send_refused` call must come from the Task-4 send tracker, which matches ctr/peer/channel
// FIRST — feeding this model a raw Push would let an unrelated channel post complete an emergency (spec §2.1).
// ⚠ `_last_try_ms` (UI-4's outcome window) is the ONE field still written-but-unread here. §B75: the claim that
// `DmState::submitting` was also written-but-unread was FALSE — nothing assigned it at all; `take_send_request` does
// now, and `dm_state()` reads it.
// NOT here at all, by unit boundary: what a screen looks like, the canned message TEXTS and the send itself all live
// in src/firmware_ui.cpp (UI-6/UI-7); the model only ever emits an index and asks.
#pragma once
#include <cstddef>   // std::size_t (UI-3's copy_clamped) — do NOT rely on <cstdint> to drag it in transitively
#include <cstdint>
// ★ §B73: the ONE lib/core dependency, and it is deliberate. Spec §2.1 rule 6 requires the WHOLE `SendFailReason` to
// reach the UI ("others -> a compact reason"), so the alternative was a parallel 18-value UI mirror of a core enum
// that command.h documents as APPEND-ONLY — the exact fork U1 forbids. `command.h` is the app seam: typed PODs, no
// Arduino, no heap, no `Node` (it is what fw_main and the sim both parse INTO), so the unit stays native-testable and
// board-free. Precedent: src/firmware_config_parse.h includes protocol_constants.h for the same reason.
#include "command.h"
#include "firmware_ui_input.h"

namespace mrui {

inline constexpr uint32_t kBlankMs      = 15000;
inline constexpr uint8_t  kMaxTeamRows  = 8;    // spec §11: a 3-10 member group; the snapshot reports the TRUE total too
inline constexpr uint8_t  kMaxInboxRows = 8;
inline constexpr uint8_t  kLabelCap     = 14;   // display-clamped teammate label

enum class Screen  : uint8_t { status = 0, team, inbox, send, count };
enum class Compose : uint8_t { none = 0, dm, channel };

// ⚠ These counts MUST match the text tables firmware_ui.cpp renders (spec §3.2.2), because the LAST row is `back
// without sending` and is identified positionally (cursor + 1 == n). A table that grows without its count here turns
// `back` into a send. UI-6/UI-7 owns the strings: DM = "Are you OK?", "I'm OK", back — channel = "Got your message",
// "All good", back.
inline constexpr uint8_t kDmTextCount      = 3;   // "Are you OK?", "I'm OK", back
inline constexpr uint8_t kChannelTextCount = 3;   // "Got your message", "All good", back

// The model NEVER sends — it ASKS. firmware_ui.cpp drains the request, performs the send and feeds back a typed outcome.
enum class SendKind : uint8_t { emergency = 0, dm, channel_canned };
struct SendReq { SendKind kind = SendKind::emergency; uint8_t peer_id = 0; uint8_t text_index = 0; };

struct TeamRow {
    uint8_t  id = 0; uint32_t last_heard_s = 0; int16_t score_q4 = 0; uint8_t hops = 0;
    char     label[kLabelCap + 1] = {};   // resolved name / 0xhash / bare id, already clamped (spec §3.3)
};
struct InboxRow {
    bool     is_dm = false; uint8_t channel_id = 0; uint32_t rx_age_s = 0;
    char     text[21] = {};               // clamped to the panel width
};

struct UiSnapshot {
    uint32_t now_ms = 0;
    uint16_t unread_dm = 0, unread_ch = 0;
    uint32_t last_dm_age_s = UINT32_MAX, last_ch_age_s = UINT32_MAX;
    uint8_t  team_shown = 0, team_total = 0;      // shown <= kMaxTeamRows; total = rt_team_count() (spec §3.3)
    TeamRow  team[kMaxTeamRows] = {};
    uint8_t  inbox_shown = 0; uint16_t inbox_total = 0;
    InboxRow inbox[kMaxInboxRows] = {};
    uint8_t  my_team_id = 0; uint32_t team_id = 0;
    int32_t  batt_mv = -1;                        // <0 = unavailable -> render "--", never a guess
    bool     team_build = true;
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

enum class Emergency : uint8_t { idle = 0, arming, firing, blocked, picked_up, not_heard, reply, cancelled, failed };
enum class DmState   : uint8_t { idle = 0, submitting, waiting_ack, delivered, no_key, not_confirmed, failed };
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
    bool    blanked = false;
    bool    dirty   = true;
};

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
        // ⚠ It is placed BEFORE the compose branch on purpose. A long press fires from inside a compose sub-view
        //   (spec §4.2) and does NOT close it, so the emergency overlay renders OVER an open modal — the press must act
        //   on what the user is looking at, which is the alarm, not on the list underneath it.
        // ⓘ `double` deliberately gets NO emergency job (spec §4's "double acknowledges" AND "double re-fires" are both
        //   withdrawn — they were the contradiction B71 resolved), so it falls through to `activate()` as usual.
        if (g == Gesture::short_press && emg_outcome_retained()) {
            _emg = Emergency::idle; _st.dirty = true; return;
        }
        if (_st.compose != Compose::none) { compose_gesture(g); return; }
        if (g == Gesture::short_press)  { advance_or_next(s); _st.dirty = true; }
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
            _st.compose = Compose::none; _st.cursor = 0; _st.dirty = true;   // never outlive attention; sends nothing
        }
        if (!_st.blanked && !hold_active(s.now_ms) &&
            elapsed(s.now_ms, _last_input_ms) >= kBlankMs) { _st.blanked = true; _st.dirty = true; }
    }

    const UiState& state() const { return _st; }
    void clear_dirty() { _st.dirty = false; }
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
        return true;
    }
    bool emergency_pending() const { return _emg_req_pending; }

    // ---------------------------------------------------------------------------------- UI-3: emergency + DM
    Emergency emergency() const { return _emg; }
    DmState   dm_state()  const { return _dm; }
    uint8_t   attempts()  const { return _tries; }
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
    uint8_t   arming_secs_left(const UiSnapshot& s) const {
        if (_emg != Emergency::arming) return 0;
        const uint32_t left = _arm_fire_at_ms - s.now_ms;
        return (left > 60000u) ? 0 : uint8_t((left + 999) / 1000);        // wrap-safe: a huge value means past-due
    }

    void on_send_accepted(SendKind k, uint32_t now_ms) {
        if (k == SendKind::emergency) { ++_tries; _last_try_ms = now_ms; }
        else if (k == SendKind::dm)   { _dm = DmState::waiting_ack; }
        _st.dirty = true;
    }
    // The SYNCHRONOUS refusal path (a parser reject or an immediate `err_*`) — it never became a core send, so there
    // is no `SendFailReason` for it and `_fail` is cleared to `none` rather than left describing an older failure.
    // ★★ §B78 (owner-ruled 2026-08-04): a terminal FAILED alarm is RETAINED and holds the panel like every other
    // emergency outcome. ⚠ `now_ms` is a PARAMETER and not `_last_input_ms` on purpose: the refusal can arrive well
    // after the gesture that caused it, and anchoring the window on the gesture is the same defect §4.3 was written to
    // kill (the outcome inherits a leftover window and the panel blanks seconds after the news). Only the EMERGENCY
    // branch retains — a DM refusal must not extend the alarm's window.
    void on_send_refused(SendKind k, RefuseReason r, uint32_t now_ms) {
        _refuse = r; _fail = FailReason::none;
        if (k == SendKind::emergency) { _emg = Emergency::failed; retain(now_ms); }   // terminal + actionable, never a stuck SENDING...
        else if (k == SendKind::dm)   { _dm  = DmState::failed; }
        _st.dirty = true;
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
    }
    const char* reply_who()  const { return _reply_who; }
    const char* reply_text() const { return _reply_text; }

    // ★★ §B71's exit PREDICATE — "an alarm has reached a terminal, readable answer". DERIVED as a set, not named from
    // the ruling's prose, and one member of that prose is VACUOUS: the ruling lists "final `blocked`", but `blocked` is
    // never final in this model — `on_outcome`'s `K::blocked` arm ALWAYS sets `_retry_armed`, and `tick_emergency` always
    // re-fires from it, so a `blocked` alarm is by construction still in flight. Including it would have made the exit
    // fire mid-retry, which is precisely what the ruling's first row forbids. ⇒ four states, not five.
    // ⓘ `cancelled` is excluded deliberately: nothing was sent, and it self-clears after kCancelledMs, so there is no
    // outcome to acknowledge. `arming` / `firing` are the in-flight rows.
    bool emg_outcome_retained() const {
        return _emg == Emergency::picked_up || _emg == Emergency::not_heard ||
               _emg == Emergency::reply     || _emg == Emergency::failed;
    }

protected:
    // Wrap-safe elapsed time. millis() wraps at ~49.7 days; `a >= b` would break across it, this does not.
    static uint32_t elapsed(uint32_t now, uint32_t then) { return now - then; }
    void queue(SendKind k, uint8_t peer, uint8_t idx) {
        if (k == SendKind::emergency) { _emg_req_pending = true; return; }   // its own slot; never overwritten
        _req = {k, peer, idx}; _req_pending = true;
    }

    // ★ Spec §4.3: every retained emergency state refreshes the `kEmgHoldMs` panel-on DEADLINE — long_fire, then
    // blocked / picked_up / not_heard / reply, and (§B78) `failed`. Anchoring it only at long_fire (an earlier draft)
    // meant an outcome or a reply arriving a whole window later inherited the leftover time and the panel blanked
    // seconds after the news arrived.
    void retain(uint32_t now_ms) { _emg_hold_until_ms = now_ms + kEmgHoldMs; }

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
    RefuseReason _refuse = RefuseReason::other;
    FailReason   _fail   = FailReason::none;   // §B73: the core reason, verbatim, beside the compact one
    uint8_t  _tries = 0;                 // ACCEPTED transmissions, never requests (spec §4)
    bool     _retry_armed       = false; // §B74: the blocked-retry deadline is live (NOT encoded in _retry_at_ms)
    uint32_t _retry_at_ms       = 0;
    uint32_t _last_try_ms       = 0;     // UI-4's outcome window; written here, unread until then
    uint32_t _arm_fire_at_ms    = 0;
    uint32_t _cancelled_until_ms = 0;
    uint32_t _emg_hold_until_ms = 0;
    uint32_t _backoff_ms        = 0;     // the next_ms==0 UI backoff, doubling to kBlockedBackoffMaxMs
    uint8_t  _last_countdown    = 0;     // so ARMING repaints only when the visible digit changes (spec §4.3)
    char     _reply_who[kLabelCap + 1] = {};
    char     _reply_text[21]           = {};

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
    void activate(const UiSnapshot& s) {
        if (_st.screen == Screen::team && s.team_shown > 0) {
            _st.compose = Compose::dm; _st.compose_peer = s.team[_st.cursor % s.team_shown].id; _st.cursor = 0;
        } else if (_st.screen == Screen::send) {
            _st.compose = Compose::channel; _st.compose_peer = 0; _st.cursor = 0;
        }
    }
    void compose_gesture(Gesture g) {
        const uint8_t n = (_st.compose == Compose::dm) ? kDmTextCount : kChannelTextCount;
        if (g == Gesture::short_press) { _st.cursor = uint8_t((_st.cursor + 1) % n); _st.dirty = true; return; }
        if (g != Gesture::double_press) return;
        if (_st.cursor + 1 == n) { _st.compose = Compose::none; _st.cursor = 0; _st.dirty = true; return; }  // `back`
        queue(_st.compose == Compose::dm ? SendKind::dm : SendKind::channel_canned, _st.compose_peer, _st.cursor);
        _st.compose = Compose::none; _st.cursor = 0; _st.dirty = true;
    }
    uint8_t list_len(const UiSnapshot& s) const {
        if (_st.screen == Screen::team)  return s.team_shown;
        if (_st.screen == Screen::inbox) return s.inbox_shown;
        return 1;
    }
    static Screen next_screen(Screen cur, const UiSnapshot& s) {
        for (uint8_t i = 1; i <= uint8_t(Screen::count); ++i) {
            const Screen cand = Screen((uint8_t(cur) + i) % uint8_t(Screen::count));
            if (s.team_build || cand == Screen::status || cand == Screen::inbox) return cand;
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
    if (g == Gesture::long_arm)    { _emg = Emergency::arming; _arm_fire_at_ms = s.now_ms + kArmToFireMs; return; }
    if (g == Gesture::long_cancel) { _emg = Emergency::cancelled; _cancelled_until_ms = s.now_ms + kCancelledMs; return; }
    // long_fire — a NEW alarm: the three-transmission budget, the backoff and any armed retry all reset, so a sticky
    // NOT HEARD can always be re-fired by another long press. (§B74: clearing `_retry_armed` here is belt-and-braces —
    // `_emg` is `firing` from this line on, and only an `on_outcome` block can return it to `blocked`, which re-arms
    // the flag itself. It is written so the flag can never be read stale, not because a stale read is reachable.)
    _emg = Emergency::firing; _tries = 0; _backoff_ms = 0; _retry_armed = false;
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

}  // namespace mrui
