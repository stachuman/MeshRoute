// MeshRoute — test_firmware_ui_model.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN — test_airtime.cpp provides main().
// NB: the native build is -fno-exceptions, so doctest's REQUIRE is a HARD COMPILE ERROR (measured: doctest.h:2824
//     "static assertion failed: Exceptions are disabled!"). Every "REQUIRE" the plan wrote is a CHECK here, with an
//     `if` guard wherever a later step depends on it.
//
// UI-2 (plan Task 2, spec §3.1/§3.2/§3.2.1/§5): the pure screen/cursor/compose model. Drives screens, the
// list-aware cursor, the one modal and blanking with no radio, no Arduino and no g_node — which is the entire
// reason firmware_ui_model.h is a pure header (reachable via `-I src`, platformio.ini:83).
// UI-3 (plan Task 3, spec §4/§4.1-§4.4/§3.4.1) appends the emergency + DM outcome machines below the UI-2 block.
// UI-3 QA fixes (§B72-§B75) are the LAST block: the retry deadline that collided with its own sentinel, the missing
// `channel_failed` kind, `DmState::submitting` becoming reachable, and the async failure reason reaching the panel.
//
// ⚠ `take_send_request()` DRAINS. Never write `CHECK(take(..) == true); if (!take(..)) return;` — that is two calls,
// the second is false, and the `return` silently aborts the case (measured: it cost the two most important emergency
// cases all but one assertion each). One call, into a local, then CHECK the local and guard on the local.
#include "doctest.h"
#include "firmware_ui_model.h"
#include <cstdint>
#include <cstring>   // strlen/strncmp — the UI-3 reply-clamping case checks copy_clamped's exact result
#include <initializer_list>   // §UI-14: the range-for over a braced CfgSave list (not dragged in transitively)

using namespace mrui;

// A 3-member team, nothing in the inbox. `now_ms` is the only field the timing cases vary.
static UiSnapshot snap(uint32_t now_ms = 1000) {
    UiSnapshot s{};
    s.now_ms = now_ms; s.team_shown = 3; s.team_total = 3; s.unread_dm = 2; s.unread_ch = 5; s.batt_mv = 3900;
    for (uint8_t i = 0; i < 3; ++i) { s.team[i].id = uint8_t(10 + i); s.team[i].last_heard_s = 60; }
    return s;
}

// Same, plus `n` inbox rows — the INBOX screen is list-aware exactly like TEAM (spec §12).
// ★ §UI-7D slice B: every row now carries its IDENTITY PAIR. `seq` starts at 1 because store sequences are 1-based and
//   `seq == 0` deliberately means "no identity" (see InboxRow) — a helper handing out 0 would have made every case
//   below exercise the refusal path instead of the feature.
static UiSnapshot snap_inbox(uint8_t n, uint32_t now_ms = 1000) {
    UiSnapshot s = snap(now_ms);
    s.inbox_shown = n; s.inbox_total = n;
    for (uint8_t i = 0; i < n && i < kMaxInboxRows; ++i) {
        s.inbox[i].kind = ((i % 2) == 0) ? InboxKind::dm : InboxKind::channel;
        s.inbox[i].seq = uint32_t(i + 1);
        s.inbox[i].rx_age_s = i;
    }
    return s;
}

// ★★★★ §UI-17 S1 — REACH THE **INTERACTIVE** TEAM LIST AT ITS FIRST ROW: one `short` to the screen, one `double` to
//      enter it. TEAM and INBOX now LAND PASSIVE (a preview with no marker and no recorded pick, passed by ONE
//      `short`), so every case that walks or activates a row starts here — the [[B232]] `to_settings_menu` prefix,
//      one screen over. ⇒ what changed for those cases is exactly ONE PRESS OF PREFIX and nothing else; the landing
//      itself is asserted by the `ui17-` cases rather than here.
// ⚠ ASSERTED by the caller afterwards, never assumed.
static void to_team(UiModel& m, const UiSnapshot& s) {
    m.on_gesture(Gesture::short_press,  s);   // STATUS -> TEAM, passive
    m.on_gesture(Gesture::double_press, s);   // ...and ENTER the list, cursor on row 0
}
// The same, one plane over: STATUS -> TEAM -> INBOX (one press each, both passive) and then the `double` that enters.
static void to_inbox(UiModel& m, const UiSnapshot& s) {
    m.on_gesture(Gesture::short_press,  s);
    m.on_gesture(Gesture::short_press,  s);
    m.on_gesture(Gesture::double_press, s);
}

// ---------------------------------------------------------------- the plan's seven cases

// ★★★★ REWRITTEN IN PLACE BY §UI-17 S1 (the §B101/[[B232]] precedent: a case whose behaviour a slice changes is
//      REWRITTEN, never deleted, with a heading saying what changed).
// ⛔ WHAT IT USED TO PIN: that ONE `short` from STATUS landed on TEAM and the next THREE walked its three roster rows
//    before the screen advanced — i.e. passing TEAM cost a press per teammate. §UI-17's contract is one press per
//    screen; the list-awareness did not go away, it MOVED behind the `double` that enters, and the second half below
//    is where it is now measured.
TEST_CASE("ui-model: short press is SCREEN-AWARE: one press per screen, the LIST is behind the double") {
    UiModel m; const auto s = snap();
    m.on_gesture(Gesture::short_press, s); CHECK(m.state().screen == Screen::team);   CHECK(m.state().cursor == 0);
    m.on_gesture(Gesture::short_press, s); CHECK(m.state().screen == Screen::inbox);
    m.on_gesture(Gesture::short_press, s); CHECK(m.state().screen == Screen::send);
    // ★ §UI-14 (spec §3.1): SETTINGS is appended to the cycle, so SEND no longer wraps straight to STATUS. It is
    //   entered by a `double` too ([[B232]]), so it costs one press to pass like every other screen.
    m.on_gesture(Gesture::short_press, s); CHECK(m.state().screen == Screen::settings);
    m.on_gesture(Gesture::short_press, s); CHECK(m.state().screen == Screen::status);
    // ...and the walk the first half used to do is exactly what the ENTERED list still does.
    to_team(m, s);
    CHECK(m.state().screen == Screen::team);   CHECK(m.state().cursor == 0);
    m.on_gesture(Gesture::short_press, s); CHECK(m.state().screen == Screen::team);   CHECK(m.state().cursor == 1);
    m.on_gesture(Gesture::short_press, s); CHECK(m.state().screen == Screen::team);   CHECK(m.state().cursor == 2);
}

TEST_CASE("ui-model: an empty TEAM list is passed through, not a dead end") {
    UiModel m; auto s = snap(); s.team_shown = 0; s.team_total = 0;
    m.on_gesture(Gesture::short_press, s); CHECK(m.state().screen == Screen::team);
    m.on_gesture(Gesture::short_press, s); CHECK(m.state().screen == Screen::inbox);
}

TEST_CASE("ui-model: double on TEAM opens the DM sub-view bound to the highlighted peer") {
    UiModel m; const auto s = snap();
    to_team(m, s); m.on_gesture(Gesture::short_press, s);   // cursor 1
    m.on_gesture(Gesture::double_press, s);
    CHECK(m.state().compose == Compose::dm);
    CHECK(m.state().compose_peer == s.team[1].id);
    CHECK(m.state().cursor == 0);
}

TEST_CASE("ui-model: sub-view: `back` leaves without sending") {
    UiModel m; const auto s = snap(); SendReq req{};
    to_team(m, s); m.on_gesture(Gesture::double_press, s);
    m.on_gesture(Gesture::short_press, s); m.on_gesture(Gesture::short_press, s);    // -> back
    m.on_gesture(Gesture::double_press, s);
    CHECK(m.state().compose == Compose::none);
    CHECK(m.state().screen  == Screen::team);
    CHECK(m.take_send_request(req) == false);
}

TEST_CASE("ui-model: sub-view: double on a message emits a DM request for the bound peer") {
    UiModel m; const auto s = snap(); SendReq req{};
    to_team(m, s); m.on_gesture(Gesture::double_press, s);
    m.on_gesture(Gesture::double_press, s);
    CHECK(m.take_send_request(req) == true);            // plan wrote REQUIRE: unavailable (-fno-exceptions)
    CHECK(req.kind == SendKind::dm); CHECK(req.peer_id == s.team[0].id); CHECK(req.text_index == 0);
    // ★★ REWRITTEN, NOT DELETED, BY UI-7 (the §B101 precedent). This line used to assert `compose == none` — i.e. it
    //    PINNED the modal closing as it sent. Spec §3.2.1/§3.4.1 require the OUTCOME to replace the list *in the
    //    sub-view* (`SENDING...` -> `DELIVERED to <label>` / `NO KEY` / `NO CONFIRM`), and with the modal closed on
    //    send every one of those states had NO RENDERER AT ALL. ⇒ the modal stays open in its RESULT phase.
    CHECK(m.state().compose == Compose::dm);            // still the DM modal: the header must still name the peer
    CHECK(m.state().compose_result == true);            // ...but showing the outcome, not the canned list
    CHECK(m.state().compose_peer == s.team[0].id);      // and still bound to who it went to
    CHECK(m.dm_state() == DmState::submitting);         // §B75: draining the request IS the hand-off point
}

// ★★★★ REWRITTEN IN PLACE BY §UI-17 S2 (the §B101/[[B232]] precedent: a case whose behaviour a slice changes is
//      REWRITTEN, never deleted, with a heading saying what changed).
// ⛔ WHAT IT USED TO PIN, under the heading *"sub-view auto-exits on inactivity WITHOUT sending"*: that `on_tick`
//    CLOSED the compose sub-view at `kBlankMs`. Spec §9 R-1 (owner-ruled 2026-08-20) deleted that timeout — blanking
//    is a POWER action and may not discard the operator's compose choice. ★ The half that MATTERED is unchanged and
//    is still asserted here: the inactivity window still SENDS NOTHING.
TEST_CASE("ui-model: the sub-view SURVIVES inactivity, and inactivity still sends nothing") {
    UiModel m; SendReq req{};
    to_team(m, snap(1000));                                       // §UI-17 S1: the list is ENTERED first...
    m.on_gesture(Gesture::double_press, snap(1100));              // ...and this is what opens the sub-view
    CHECK(m.state().compose == Compose::dm);
    m.on_tick(snap(1100 + kBlankMs + 1));
    CHECK(m.state().compose == Compose::dm);                      // ★ §3.3: the interaction outlives the blank
    CHECK(m.state().compose_peer == snap().team[0].id);           // ★ ...still bound to the SAME peer
    CHECK(m.state().cursor == 0);                                 // ★ ...on the same canned message
    CHECK(m.state().blanked == true);                             // ...and the panel is dark, on its own deadline
    CHECK(m.take_send_request(req) == false);
}

TEST_CASE("ui-model: panel blanks and the waking SHORT press is consumed") {
    UiModel m;
    m.on_tick(snap(1000)); m.on_tick(snap(1000 + kBlankMs + 1));
    CHECK(m.state().blanked == true);
    m.on_gesture(Gesture::short_press, snap(1000 + kBlankMs + 10));
    CHECK(m.state().blanked == false);
    CHECK(m.state().screen  == Screen::status);
}

// ---------------------------------------------------------------- coverage the plan's seven leave open

// Spec §3.1: "On a non-team build the cycle is STATUS -> INBOX." The gate is a snapshot flag, so the same binary
// is exercised both ways here — and TEAM/SEND must be unreachable, not merely unhelpful.
// ★ §UI-14 UPDATED THE EXPECTATION, not the property: the non-team cycle is now STATUS -> INBOX -> SETTINGS (spec
//   §3.1), because the four covered fields are durable on every build — `gateway_heltec` is a real OLED=1/TEAM=0 env.
//   ⛔ TEAM and SEND must STILL be unreachable, which is what the loop below asserts.
TEST_CASE("ui-model: a non-team build cycles STATUS -> INBOX -> SETTINGS and never reaches TEAM or SEND") {
    UiModel m; auto s = snap(); s.team_build = false;
    m.on_gesture(Gesture::short_press, s); CHECK(m.state().screen == Screen::inbox);
    m.on_gesture(Gesture::short_press, s); CHECK(m.state().screen == Screen::settings);
    for (int i = 0; i < 40; ++i) {
        m.on_gesture(Gesture::short_press, s);
        CHECK((m.state().screen == Screen::status || m.state().screen == Screen::inbox ||
               m.state().screen == Screen::settings));
    }
}

TEST_CASE("ui-model: a non-team build cannot open a compose modal") {
    UiModel m; auto s = snap(); s.team_build = false; SendReq req{};
    for (int i = 0; i < 6; ++i) {
        m.on_gesture(Gesture::double_press, s);
        CHECK(m.state().compose == Compose::none);
        m.on_gesture(Gesture::short_press, s);
    }
    CHECK(m.take_send_request(req) == false);
}

// ★★★ REWRITTEN IN PLACE 2026-08-13 BY §UI-7D slice B ([[B101]]'s precedent: a case whose behaviour a slice changes is
//     REWRITTEN, never deleted or disabled, with a heading saying what changed).
// ⛔ WHAT IT USED TO ASSERT: "double on STATUS and on INBOX activates nothing" — INBOX included, on a THREE-ROW list.
//    That was correct for UI-7 (the inbox had no `double` action at all) and is now WRONG: spec §3.2/§3.5 give `double`
//    on a highlighted inbox row the detail modal. STATUS keeps its no-op, and INBOX keeps it only when there is nothing
//    to open — which is what the second half now pins.
// ★ AND THE INBOX HALF MUST STILL QUEUE NO SEND. The modal is a storage view; the two send slots are untouched by it, so
//   the original case's real invariant (no send request escapes an inbox double) is kept rather than dropped.
TEST_CASE("ui-model: double on STATUS activates nothing; on INBOX it now OPENS THE DETAIL MODAL (§UI-7D)") {
    UiModel m; const auto s = snap_inbox(3); SendReq req{};
    m.on_gesture(Gesture::double_press, s);
    CHECK(m.state().screen == Screen::status); CHECK(m.state().compose == Compose::none);
    CHECK(m.state().detail == InboxModal::closed);
    to_inbox(m, s);                          // §UI-17 S1: STATUS -> TEAM -> INBOX, then the `double` that ENTERS
    CHECK(m.state().screen == Screen::inbox);
    m.on_gesture(Gesture::double_press, s);
    CHECK(m.state().compose == Compose::none); CHECK(m.state().screen == Screen::inbox);
    CHECK(m.take_send_request(req) == false);                    // ★ the ORIGINAL invariant: no send is ever queued
    // ⇒ what it does instead: a REQUEST naming the highlighted row's identity pair. The modal itself opens only when the
    //   device half answers — nothing here reads the store, which is the whole point of the seam.
    InboxReq rq{};
    const bool asked = m.take_inbox_request(rq);
    CHECK(asked == true);
    if (asked) {
        CHECK(rq.what == InboxWhat::open);
        CHECK(rq.kind == InboxKind::dm);                         // row 0 of snap_inbox is a DM...
        CHECK(rq.seq == 1u);                                     // ...with seq 1
    }
}

// ★ THE OTHER HALF OF THE OLD ASSERTION, KEPT AS ITS OWN CASE: with nothing to open, a `double` on INBOX still does
//   nothing at all — and it does not raise the refusal either, because an empty list already says why (the same
//   carve-out the empty TEAM roster has).
TEST_CASE("ui-model: double on an EMPTY INBOX still activates nothing and says nothing (§UI-7D)") {
    UiModel m; auto s = snap_inbox(0); SendReq req{}; InboxReq rq{};
    to_inbox(m, s);                          // §UI-17 S1: ...and the entering `double` is itself one of the two here
    CHECK(m.state().screen == Screen::inbox);
    m.on_gesture(Gesture::double_press, s);
    CHECK(m.state().detail == InboxModal::closed);
    CHECK(m.state().inbox_pick_gone == false);
    CHECK(m.take_inbox_request(rq) == false);
    CHECK(m.take_send_request(req) == false);
}

// ★★★ REWRITTEN IN PLACE BY §UI-17 S1 (the §B101 precedent). ⛔ WHAT IT USED TO PIN: that the LAST row's `short`
//     walked OFF the screen onto SEND. The list is now CONTAINED — its last row is `BACK` and the walk past it
//     returns to the FIRST row — so the property "the cursor walks its rows" is unchanged and what happens at the END
//     has moved. Leaving is `BACK`'s job, and `ui17-back` pins it.
TEST_CASE("ui-model: INBOX is list-aware too — the cursor walks its rows before the screen moves") {
    UiModel m; const auto s = snap_inbox(3);
    to_inbox(m, s);
    CHECK(m.state().screen == Screen::inbox); CHECK(m.state().cursor == 0);
    m.on_gesture(Gesture::short_press, s); CHECK(m.state().screen == Screen::inbox); CHECK(m.state().cursor == 1);
    m.on_gesture(Gesture::short_press, s); CHECK(m.state().screen == Screen::inbox); CHECK(m.state().cursor == 2);
    m.on_gesture(Gesture::short_press, s); CHECK(m.state().screen == Screen::inbox); CHECK(m.state().cursor == 3);
    CHECK(list_row_kind(m.state().cursor, s.inbox_shown) == ListRow::back);          // ...the BACK row
    m.on_gesture(Gesture::short_press, s); CHECK(m.state().screen == Screen::inbox); CHECK(m.state().cursor == 0);
}

// The SEND screen is single-item, so `short` just moves on; `double` opens the CHANNEL list (spec §3.2.2), whose
// rows are "Got your message" / "All good" / back — three, like the DM list, but a different SendKind.
TEST_CASE("ui-model: SEND double opens the channel compose list and index 1 sends the second canned text") {
    UiModel m; const auto s = snap(); SendReq req{};
    for (int i = 0; i < 3; ++i) m.on_gesture(Gesture::short_press, s);   // STATUS -> TEAM -> INBOX -> SEND
    CHECK(m.state().screen == Screen::send);
    m.on_gesture(Gesture::double_press, s);
    CHECK(m.state().compose == Compose::channel);
    CHECK(m.state().compose_peer == 0);          // a channel post has no peer
    CHECK(m.state().cursor == 0);
    m.on_gesture(Gesture::short_press, s);       // -> "All good"
    m.on_gesture(Gesture::double_press, s);
    CHECK(m.take_send_request(req) == true);
    CHECK(req.kind == SendKind::channel_canned);
    CHECK(req.text_index == 1);
    // ★★ REWRITTEN BY UI-7, same reason as the DM case above: the modal now enters its RESULT phase instead of
    //    closing, because the canned post's outcome (§B69's ChanState) has nowhere else to be shown.
    CHECK(m.chan_state() == ChanState::submitting);
    CHECK(req.peer_id == 0);
    CHECK(m.state().compose == Compose::channel);   // UI-7: RESULT phase, not closed — see the note above
    CHECK(m.state().compose_result == true);
    // ...and the parent underneath is unchanged, so acknowledging the result returns to SEND rather than to the
    // cycle start. Asserted through the CLOSE, because that is the moment the claim is actually about.
    m.on_gesture(Gesture::double_press, s);
    CHECK(m.state().compose == Compose::none);
    CHECK(m.state().compose_result == false);
    CHECK(m.state().screen  == Screen::send);    // the modal returns to its PARENT, not to the cycle start
}

TEST_CASE("ui-model: the channel list's last row is `back` and sends nothing") {
    UiModel m; const auto s = snap(); SendReq req{};
    for (int i = 0; i < 3; ++i) m.on_gesture(Gesture::short_press, s);   // -> SEND, one press per screen
    m.on_gesture(Gesture::double_press, s);
    m.on_gesture(Gesture::short_press, s); m.on_gesture(Gesture::short_press, s);   // -> back (index 2)
    CHECK(m.state().cursor == 2);
    m.on_gesture(Gesture::double_press, s);
    CHECK(m.state().compose == Compose::none);
    CHECK(m.take_send_request(req) == false);
}

TEST_CASE("ui-model: the compose cursor wraps within the list, so `back` is always reachable") {
    UiModel m; const auto s = snap();
    to_team(m, s); m.on_gesture(Gesture::double_press, s);   // §UI-17 S1: enter the list, then the DM list, cursor 0
    m.on_gesture(Gesture::short_press, s); m.on_gesture(Gesture::short_press, s);
    CHECK(m.state().cursor == 2);
    m.on_gesture(Gesture::short_press, s);
    CHECK(m.state().cursor == 0);                // wrapped, still inside the list
    CHECK(m.state().compose == Compose::dm);     // and the modal did NOT close on the wrap
}

// The peer is bound at ENTRY on purpose: the roster is rebuilt every tick and can reorder under an open modal,
// which would silently retarget a DM the user already aimed.
TEST_CASE("ui-model: the bound peer survives a roster REORDER under the open modal") {
    UiModel m; auto s = snap(); SendReq req{};
    to_team(m, s); m.on_gesture(Gesture::short_press, s);   // §UI-17 S1: enter, then TEAM cursor 1 -> id 11
    m.on_gesture(Gesture::double_press, s);
    CHECK(m.state().compose_peer == 11);
    s.team[0].id = 77; s.team[1].id = 88; s.team[2].id = 99;                        // roster churn
    m.on_gesture(Gesture::double_press, s);                                         // send text 0
    CHECK(m.take_send_request(req) == true);
    CHECK(req.peer_id == 11);                                                       // NOT 88
}

// ★★★★ §B64 — REWRITTEN, NOT DELETED, BY THE UI-7 QA FIX SLICE (the §B101 precedent: a test that pinned a
//      since-ruled-against behaviour is rewritten so the change is visible in the diff).
// ⛔ WHAT THIS CASE USED TO PIN: `activate()` read `s.team[_st.cursor % s.team_shown]`, so a cursor on row 2 meeting a
//    1-row roster opened the DM modal bound to ROW 0 — and the case asserted that outcome as a "documented
//    consequence". It is a MIS-SEND: "Are you OK?" went to a teammate the user did not highlight.
// ★★ OWNER RULING 2026-08-05: *preserve the selection by teammate IDENTITY across roster refreshes; the cursor tracks
//    the teammate, not the row index; if that teammate has disappeared, REFUSE the activation and repaint — never
//    silently select another row.*
// ★★★ AND THE ASSERTION IS THE SIDE EFFECT, NOT A CURSOR ENUM: it is the QUEUED SEND REQUEST. That is what
//     discriminates the ruling from every tempting near-miss, because all of them SEND SOMETHING:
//       `% team_shown` -> id 10 · clamp to `shown - 1` -> id 11 · clamp to 0 -> id 10 · the ruling -> NOTHING AT ALL.
//     ⚠ Two doubles, deliberately: the first is the activation, the second is what would actually SEND from a modal
//       the first must never have opened. Asserting only `compose == none` would be green against a real mis-send the
//       moment anything else opened the view (§B110's measurement).
TEST_CASE("ui-model: B64 — a teammate that VANISHED from the roster REFUSES the activation and sends NOTHING") {
    UiModel m; auto s = snap(); SendReq req{};
    to_team(m, s);                                                       // §UI-17 S1: enter the list first
    m.on_gesture(Gesture::short_press, s); m.on_gesture(Gesture::short_press, s);   // TEAM, cursor 2 -> id 12
    CHECK(m.state().cursor == 2);
    CHECK(s.team[2].id == 12);
    s.team_shown = 2; s.team_total = 2;                                  // teammate 12 is GONE; 10 and 11 remain
    m.on_gesture(Gesture::double_press, s);                              // the would-be activation
    CHECK(m.state().compose == Compose::none);                           // no modal opened...
    m.on_gesture(Gesture::double_press, s);                              // ...so a second double cannot send from one
    const bool queued = m.take_send_request(req);                        // ⚠ §B70: ONE call, into a local
    CHECK(queued == false);                                              // ★ NOTHING was addressed to ANYBODY
    // ...and the refusal is LOUD (C2) + repaints, which is the other half of the ruling.
    CHECK(m.state().team_pick_gone == true);
    CHECK(m.state().dirty          == true);
}

// ★★ THE REFUSAL IS ANNOUNCED BY A PLAIN TICK TOO, and it is EDGE-TRIGGERED (spec §5). The panel is repainted from the
//    frozen state, so the loss must be recorded by whatever pass first observes it — not only by the press that would
//    have sent. ⚠ And it must not re-dirty the frame on every subsequent tick, or the 2 Hz throttle repaints for ever.
TEST_CASE("ui-model: B64 — a tick announces the lost pick, exactly once") {
    UiModel m; auto s = snap();
    to_team(m, s);                                                       // §UI-17 S1: enter the list first
    m.on_gesture(Gesture::short_press, s); m.on_gesture(Gesture::short_press, s);   // TEAM, cursor 2 -> id 12
    CHECK(m.state().team_pick_gone == false);
    s.team_shown = 2; s.team_total = 2;                                  // teammate 12 leaves
    m.clear_dirty();
    m.on_tick(s);
    CHECK(m.state().team_pick_gone == true);
    CHECK(m.state().dirty          == true);                             // "and repaint"
    m.clear_dirty();
    m.on_tick(s);
    CHECK(m.state().dirty          == false);                            // ★ edge-triggered: no per-tick repaint
    CHECK(m.state().team_pick_gone == true);                             // ...and the message is still standing
}

// ★★★ THE OTHER HALF OF THE RULING, AND IT IS THE CASE THAT TELLS IDENTITY-TRACKING APART FROM CLAMPING. The roster
//     REORDERS at the SAME size, and teammate 12 lands on row 1 — an index that no clamp can produce:
//       `cursor % 3` = 2 -> id 13 · clamp to `shown - 1` = 2 -> id 13 · clamp to 0 -> id 11 · identity -> id 12.
//     A control that cannot separate those is vacuous, which is exactly what the shrink-only case would have been.
TEST_CASE("ui-model: B64 — a roster REORDER follows the TEAMMATE, so the send goes where the user pointed") {
    UiModel m; auto s = snap(); SendReq req{};
    to_team(m, s);                                                       // §UI-17 S1: enter the list first
    m.on_gesture(Gesture::short_press, s); m.on_gesture(Gesture::short_press, s);   // TEAM, cursor 2 -> id 12
    CHECK(s.team[2].id == 12);
    s.team[0].id = 11; s.team[1].id = 12; s.team[2].id = 13;             // same size; 12 moved from row 2 to row 1
    m.on_tick(s);                                                        // a PLAIN TICK re-anchors the highlight...
    CHECK(m.state().cursor == 1);                                        // ★ ...so the FRAME the user sees cannot lie
    m.on_gesture(Gesture::double_press, s);
    CHECK(m.state().compose == Compose::dm);
    CHECK(m.state().compose_peer == 12);
    m.on_gesture(Gesture::double_press, s);                              // send the first canned text
    const bool queued = m.take_send_request(req);
    CHECK(queued == true);
    if (queued) CHECK(req.peer_id == 12);                                // ★ NOT 13 (the row) and NOT 11 (a clamp)
}

// ★★★ THE COMPOSE GUARD, and it is a control rather than a feature: while the sub-view is open `_st.cursor` is the
//     MODAL's list index, not a team row, so a resync that forgot to check `compose` would silently walk the MESSAGE
//     selection under the user's fingers. Asserted on the SENT request, never on the cursor alone.
// ⚠⚠ THE ROSTER CHURN HERE MUST KEEP TEAMMATE 11 PRESENT — MEASURED, NOT STYLED. The first writing of this case
//    replaced ALL THREE ids (77/88/99), and the mutation that drops the compose guard then went **0 / 0**: with the
//    remembered teammate absent, the unguarded resync takes its VANISH arm, which does not touch the cursor at all.
//    The control named the right scenario and could not fail — the §M6/§M10 class, for the third time in this arc.
// ⇒ teammate 11 MOVES to row 2 instead. The unguarded resync then drags the modal's cursor from 1 to 2, and row 2 of a
//   3-row canned list is `back, don't send` — so the harm it produces is that "I'm OK" SENDS NOTHING AT ALL, which the
//   `queued` assertion catches directly.
TEST_CASE("ui-model: B64 — the roster resync must NOT touch an open compose modal's cursor") {
    UiModel m; auto s = snap(); SendReq req{};
    to_team(m, s); m.on_gesture(Gesture::short_press, s);   // §UI-17 S1: enter, then TEAM cursor 1 -> id 11
    m.on_gesture(Gesture::double_press, s);
    CHECK(m.state().compose_peer == 11);
    m.on_gesture(Gesture::short_press, s);                               // the MODAL's cursor -> 1 ("I'm OK")
    CHECK(m.state().cursor == 1);
    s.team[0].id = 77; s.team[1].id = 88; s.team[2].id = 11;             // churn, and teammate 11 moves to ROW 2
    m.on_tick(s);
    CHECK(m.state().cursor == 1);                                        // ★ the modal's selection is untouched
    m.on_gesture(Gesture::double_press, s);
    const bool queued = m.take_send_request(req);
    CHECK(queued == true);                                               // ★ it really SENT — not `back`
    if (queued) {
        CHECK(req.peer_id    == 11);                                     // still the ENTRY-bound peer
        CHECK(req.text_index == 1);                                      // ★ and still the text the user chose
    }
}

// ★ THE REFUSAL IS NOT A DEAD END: the user re-picks by walking, and the next activation works normally. Without this
//   the ruling could have been implemented as a permanent lockout of the TEAM screen and still looked correct.
TEST_CASE("ui-model: B64 — after a refusal the user re-picks by walking, and the next send targets that teammate") {
    UiModel m; auto s = snap(); SendReq req{};
    to_team(m, s); m.on_gesture(Gesture::short_press, s);   // §UI-17 S1: enter, then TEAM cursor 1 -> id 11
    CHECK(m.state().cursor == 1);
    s.team[0].id = 10; s.team[1].id = 12; s.team[2].id = 13;             // teammate 11 is GONE, still three rows
    m.on_gesture(Gesture::double_press, s);
    CHECK(m.state().compose == Compose::none);                           // refused, as ruled
    CHECK(m.state().team_pick_gone == true);
    m.on_gesture(Gesture::short_press, s);                               // walk on: cursor 1 -> 2 -> teammate 13
    CHECK(m.state().cursor == 2);
    CHECK(m.state().team_pick_gone == false);                            // re-picking retires the message
    m.on_gesture(Gesture::double_press, s);
    CHECK(m.state().compose == Compose::dm);
    m.on_gesture(Gesture::double_press, s);
    const bool queued = m.take_send_request(req);
    CHECK(queued == true);
    if (queued) CHECK(req.peer_id == 13);                                // the teammate now under the cursor
}

// ★ THE MESSAGE IS RETIRED BEFORE THE SCREEN CAN COME BACK ROUND — otherwise a stale "TEAMMATE GONE, pick" would
//   reappear the next lap, describing a pick from minutes ago.
// ★★★ REWRITTEN IN PLACE BY §UI-17 S1 (the §B101 precedent). ⛔ WHAT IT USED TO PIN: that the `short` which walked
//     OFF the TEAM screen retired the message. The list is CONTAINED now, so that press cannot leave the screen at
//     all — it walks the list — and the way out is `BACK`. ⇒ the property is unchanged and its DRIVER moved: the
//     refusal is retired by moving off the lost pick (`note_team_cursor`), so the passive screen the operator leaves
//     to, and every later lap, carry no message. ⓘ `note_team_cursor`'s screen clause is now structurally
//     unreachable and says so in source ([[meshroute-mark-done-vs-missing-in-code]]).
TEST_CASE("ui-model: B64 — the refusal is retired before TEAM can be left, and never comes back") {
    UiModel m; auto s = snap();
    to_team(m, s);                                                       // §UI-17 S1: enter the list first
    m.on_gesture(Gesture::short_press, s); m.on_gesture(Gesture::short_press, s);   // TEAM, cursor 2 -> id 12
    s.team_shown = 2; s.team_total = 2;
    m.on_gesture(Gesture::double_press, s);
    CHECK(m.state().team_pick_gone == true);
    // cursor 2 is the BACK row of the 2-row roster now, so the walk WRAPS to row 0 — ⛔ it does not leave the screen
    m.on_gesture(Gesture::short_press, s);
    CHECK(m.state().screen == Screen::team);
    CHECK(m.state().cursor == 0);
    CHECK(m.state().team_pick_gone == false);                            // ★ re-picking retires it
    // ...and leaving through BACK lands on a PASSIVE TEAM that says nothing, then passes the screen in one press
    m.on_gesture(Gesture::short_press, s); m.on_gesture(Gesture::short_press, s);   // -> the BACK row (index 2)
    m.on_gesture(Gesture::double_press, s);
    CHECK(m.state().screen == Screen::team);
    CHECK(m.state().list_view == ListView::passive);
    CHECK(m.state().team_pick_gone == false);
    m.on_gesture(Gesture::short_press, s);
    CHECK(m.state().screen == Screen::inbox);
    CHECK(m.state().team_pick_gone == false);
}

TEST_CASE("ui-model: an empty TEAM list opens no modal on double") {
    UiModel m; auto s = snap(); s.team_shown = 0; s.team_total = 0; SendReq req{};
    m.on_gesture(Gesture::short_press, s);
    CHECK(m.state().screen == Screen::team);
    m.on_gesture(Gesture::double_press, s);
    CHECK(m.state().compose == Compose::none);
    CHECK(m.take_send_request(req) == false);
}

TEST_CASE("ui-model: a send request is drained exactly once") {
    UiModel m; const auto s = snap(); SendReq req{};
    to_team(m, s); m.on_gesture(Gesture::double_press, s);   // §UI-17 S1: enter the list, then open the DM sub-view
    m.on_gesture(Gesture::double_press, s);
    CHECK(m.take_send_request(req) == true);
    CHECK(m.take_send_request(req) == false);    // no duplicate send on the next service pass
}

TEST_CASE("ui-model: Gesture::none is inert — it neither navigates nor refreshes the blank timer") {
    UiModel m; const auto s = snap();
    m.clear_dirty();
    m.on_gesture(Gesture::none, s);
    CHECK(m.state().screen == Screen::status);
    CHECK(m.state().dirty  == false);            // a no-op gesture must not force a repaint
}

// Spec §5 rule 2: paint only when the model reports dirty. A tick that changes nothing must not set it, or the
// panel repaints at tick rate and the 25 ms I2C frame starts competing with the MAC.
TEST_CASE("ui-model: dirty starts true, is cleared on demand, and an idle tick does not set it") {
    UiModel m;
    CHECK(m.state().dirty == true);               // the first frame must be drawn
    m.clear_dirty(); CHECK(m.state().dirty == false);
    m.on_tick(snap(1000));  CHECK(m.state().dirty == false);
    m.on_tick(snap(2000));  CHECK(m.state().dirty == false);
    m.on_gesture(Gesture::short_press, snap(2100));
    CHECK(m.state().dirty == true);               // navigation is visible -> repaint
}

TEST_CASE("ui-model: the blank transition sets dirty exactly once") {
    UiModel m;
    m.on_tick(snap(1000)); m.clear_dirty();
    m.on_tick(snap(1000 + kBlankMs + 1));
    CHECK(m.state().blanked == true); CHECK(m.state().dirty == true);
    m.clear_dirty();
    m.on_tick(snap(1000 + kBlankMs + 5000));
    CHECK(m.state().dirty == false);              // edge-triggered: no repeated blank work (spec §5)
}

// ★★★ REWRITTEN IN PLACE BY §UI-17 S2. ⛔ WHAT IT USED TO PIN, as *"the modal does NOT auto-exit before the blank
//     window elapses"*: the modal open at `kBlankMs - 1` and CLOSED at `kBlankMs` — i.e. the deadline's exact edge.
//     §9 R-1 deleted the deadline, so what is measured now is that BOTH sides of it are open, and that the BLANK
//     itself still lands on the unmoved edge (spec S2 pin 5: deleting the timeouts may not extend `kBlankMs`).
TEST_CASE("ui-model: the modal is open on BOTH sides of the blank edge, and the blank still lands on it") {
    UiModel m; SendReq req{};
    to_team(m, snap(1000));                                            // §UI-17 S1: enter the list first
    m.on_gesture(Gesture::double_press, snap(1100));
    m.on_tick(snap(1100 + kBlankMs - 1));
    CHECK(m.state().compose == Compose::dm);
    CHECK(m.state().blanked == false);                                 // ★ one millisecond early: still lit
    m.on_tick(snap(1100 + kBlankMs));
    CHECK(m.state().compose == Compose::dm);                           // ★ the modal is RETAINED...
    CHECK(m.state().blanked == true);                                  // ★ ...and the blank fired on the same edge
    CHECK(m.take_send_request(req) == false);
}

// ★★ REWRITTEN IN PLACE BY §UI-17 S2. ⛔ WHAT IT USED TO PIN, as *"a gesture inside the modal refreshes its
//    inactivity window"*: that a `short` inside the sub-view restarted the modal's own timeout. There is no such
//    timeout any more, so that case would have been VACUOUS — an assertion true whatever the implementation does.
//    ★ The property it really rested on SURVIVES and is what is measured now: a press inside the modal is INPUT, so
//    it moves `_last_input_ms` and therefore the BLANK deadline — which is the one clock the sub-view still shares.
TEST_CASE("ui-model: a gesture inside the modal refreshes the BLANK window (the only clock it still shares)") {
    UiModel m;
    to_team(m, snap(1000));                                            // §UI-17 S1: enter the list first
    m.on_gesture(Gesture::double_press, snap(1100));
    m.on_gesture(Gesture::short_press, snap(1100 + kBlankMs - 100));   // still browsing the list
    m.on_tick(snap(1100 + kBlankMs + 10));
    CHECK(m.state().compose == Compose::dm);
    CHECK(m.state().blanked == false);                                 // ★ window restarted, not expired
    CHECK(m.state().cursor  == 1);                                     // ...and the press did move the list
    m.on_tick(snap(1100 + kBlankMs - 100 + kBlankMs));                 // one full window after THAT press
    CHECK(m.state().blanked == true);
    CHECK(m.state().compose == Compose::dm);                           // ★ ...and the modal is still there under it
    CHECK(m.state().cursor  == 1);                                     // ★ ...on the row the operator left it on
}

// ★★★★ REWRITTEN IN PLACE BY §UI-17 S2 — SPEC S2 PIN 1, driven through the shipped press sequence.
// ⛔ WHAT IT USED TO PIN, as *"blanking with a modal open closes the modal and the waking press shows the parent"*:
//    exactly the behaviour §9 R-1 reverses. Blanking is a POWER action; the consumed wake press restores the SAME
//    interaction, ⛔ never its parent.
TEST_CASE("ui-model: blanking KEEPS the modal, and the consumed waking press puts it back on the panel") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::short_press, snap(1000));
    m.on_gesture(Gesture::double_press, snap(1050));   // §UI-17 S1: enter the TEAM list
    m.on_gesture(Gesture::double_press, snap(1100));   // ...and open the DM sub-view on row 0
    CHECK(m.state().compose == Compose::dm);
    m.on_tick(snap(1100 + kBlankMs + 1));
    CHECK(m.state().blanked == true);
    CHECK(m.state().compose == Compose::dm);           // ★ the blank discarded nothing
    m.on_gesture(Gesture::short_press, snap(1100 + kBlankMs + 50));
    CHECK(m.state().blanked == false);
    CHECK(m.state().screen  == Screen::team);          // the parent screen is still underneath...
    CHECK(m.state().compose == Compose::dm);           // ★ ...but the SUB-VIEW is what the press restored
    CHECK(m.state().cursor  == 0);                     // ★ ...on the same canned message: the press was CONSUMED
    CHECK(m.take_send_request(req) == false);          // ⛔ and nothing was sent by any of it
}

TEST_CASE("ui-model: a DOUBLE press also only wakes a blanked panel") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::short_press, snap(1000));            // -> TEAM
    m.on_tick(snap(1000 + kBlankMs + 1));
    CHECK(m.state().blanked == true);
    m.on_gesture(Gesture::double_press, snap(1000 + kBlankMs + 10));
    CHECK(m.state().blanked == false);
    CHECK(m.state().compose == Compose::none);                 // did NOT open the DM modal
    CHECK(m.take_send_request(req) == false);
}

TEST_CASE("ui-model: blanking is wrap-safe across millis() rollover") {
    UiModel m;
    m.on_gesture(Gesture::short_press, snap(0xFFFFF000u));     // last input just before the wrap
    m.on_tick(snap(0x00000500u));                              // 5376 ms later in real time
    CHECK(m.state().blanked == false);                          // NOT ~4.29e9 ms "elapsed"
    m.on_tick(snap(0x00003000u));                              // 16 384 ms after the input
    CHECK(m.state().blanked == true);
}

// ★★ REWRITTEN IN PLACE BY §UI-17 S2. ⛔ WHAT IT USED TO PIN, as *"the modal inactivity exit is wrap-safe too"*: that
//    the modal's OWN `kBlankMs` deadline handled a `millis()` rollover. That deadline is deleted (§9 R-1), so the
//    wrap-safety that remains belongs to the BLANK — and the case is re-pointed onto it WITH a modal open, which is
//    the combination the rollover could break: a wrapped comparison would blank the panel ~5 s early under a modal.
TEST_CASE("ui-model: the blank is wrap-safe with a modal open, and the modal rides across the wrap") {
    UiModel m;
    to_team(m, snap(0xFFFFF000u));                                     // §UI-17 S1: enter the list first
    m.on_gesture(Gesture::double_press, snap(0xFFFFF100u));
    CHECK(m.state().compose == Compose::dm);
    m.on_tick(snap(0x00000500u));
    CHECK(m.state().blanked == false);                          // ~5 s elapsed, not a wrapped eternity
    CHECK(m.state().compose == Compose::dm);
    m.on_tick(snap(0x00003000u));
    CHECK(m.state().blanked == true);                           // 16 s after the press: blanked, across the wrap
    CHECK(m.state().compose == Compose::dm);                    // ★ ...and the sub-view came across it intact
}

// ★ UI-3 replaced UI-2's inert stubs here. Before UI-3 this case pinned "a long gesture queues NOTHING"; it now pins
// the live contract, which is the same §4.2 ordering plus a real alarm: arming queues nothing, firing queues one.
TEST_CASE("ui-model: a long gesture pre-empts blank-wake; ARM queues nothing, FIRE queues one alarm") {
    UiModel m; SendReq req{};
    m.on_tick(snap(1000)); m.on_tick(snap(1000 + kBlankMs + 1));
    CHECK(m.state().blanked == true);
    m.on_gesture(Gesture::long_arm, snap(1000 + kBlankMs + 10));
    CHECK(m.state().blanked == false);               // §4.2: long gestures pre-empt blank-wake consumption
    CHECK(m.emergency() == Emergency::arming);
    CHECK(m.emergency_pending() == false);           // arming commits nothing to the air
    CHECK(m.take_send_request(req) == false);
    m.on_gesture(Gesture::long_fire, snap(1000 + kBlankMs + 20));
    CHECK(m.emergency() == Emergency::firing);
    CHECK(m.emergency_pending() == true);
    const bool got = m.take_send_request(req);
    CHECK(got == true);
    if (got) CHECK(req.kind == SendKind::emergency);
}

TEST_CASE("ui-model: a long gesture is not swallowed by an open modal (spec 4.2 ordering)") {
    UiModel m; const auto s = snap();
    to_team(m, s); m.on_gesture(Gesture::double_press, s);   // §UI-17 S1: enter the list, then the DM sub-view
    CHECK(m.state().compose == Compose::dm);
    m.clear_dirty();
    m.on_gesture(Gesture::long_arm, s);
    CHECK(m.state().dirty == true);                  // it reached the emergency branch, not compose_gesture
    CHECK(m.state().cursor == 0);                    // and did NOT move the modal's highlight
}

// The bounds the renderer and the snapshot builder both depend on (spec §11: 8 rows, not 16).
TEST_CASE("ui-model: the model's declared bounds are the ones the spec fixed") {
    CHECK(kMaxTeamRows  == 8);
    CHECK(kMaxInboxRows == 8);
    CHECK(kBlankMs      == 15000u);
    CHECK(kDmTextCount      == 3);                   // "Are you OK?", "I'm OK", back without sending
    CHECK(kChannelTextCount == 3);                   // "Got your message", "All good", back without sending
    CHECK(uint8_t(Screen::count) == 5);              // §UI-14: STATUS/TEAM/INBOX/SEND/SETTINGS (spec §3.1)
    UiSnapshot s{};
    CHECK(s.batt_mv == -1);                          // <0 = unavailable -> render "--", never a guess
    CHECK(s.team_build == true);
    // ★ §UI-14: the BLE row's condition defaults to ABSENT, which is spec §3.6.2's ruled state for "the UI-12
    //   transport is not compiled" — and it is the state of every env in the tree today.
    CHECK(s.ble_row == false);
    CHECK(sizeof(s.team) / sizeof(s.team[0]) == kMaxTeamRows);
    CHECK(sizeof(s.inbox) / sizeof(s.inbox[0]) == kMaxInboxRows);
}

// ★★★ REWRITTEN IN PLACE BY §UI-17 S1 (the §B101 precedent). ⛔ WHAT IT USED TO PIN: that the ninth `short` walked
//     OFF the eight-row roster onto INBOX. The FULL roster is the worst case for the contained walk, so it is the
//     case that proves `BACK` is still REACHABLE at the cap — and that the walk past it comes home rather than
//     leaving.
TEST_CASE("ui-model: a full team roster walks every one of the eight rows, then BACK, then home") {
    UiModel m; UiSnapshot s{};
    s.now_ms = 1000; s.team_shown = kMaxTeamRows; s.team_total = 12;   // truncated view, true total larger
    for (uint8_t i = 0; i < kMaxTeamRows; ++i) s.team[i].id = uint8_t(20 + i);
    to_team(m, s);
    CHECK(m.state().screen == Screen::team);
    for (uint8_t i = 1; i < kMaxTeamRows; ++i) {
        m.on_gesture(Gesture::short_press, s);
        CHECK(m.state().screen == Screen::team);
        CHECK(m.state().cursor == i);
    }
    m.on_gesture(Gesture::short_press, s);
    CHECK(m.state().screen == Screen::team);
    CHECK(m.state().cursor == kMaxTeamRows);                          // ★ the BACK row exists even at the cap...
    CHECK(list_row_kind(m.state().cursor, s.team_shown) == ListRow::back);
    m.on_gesture(Gesture::short_press, s);
    CHECK(m.state().screen == Screen::team);                          // ⛔ ...and the walk past it NEVER leaves
    CHECK(m.state().cursor == 0);
}

// =================================================================================================== UI-3
// Emergency + DM outcome machines (plan Task 3, spec §4/§4.1-§4.4 and §3.4.1). The model never sees a raw Push —
// every outcome below is one the Task-4 tracker has already correlated by ctr/peer/channel (spec §2.1), which is
// what makes a false PICKED UP structurally impossible.

// ---------------------------------------------------------------- the plan's twelve cases

TEST_CASE("ui-model: arm then cancel never emits a send") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::long_arm, snap(1000));   CHECK(m.emergency() == Emergency::arming);
    m.on_gesture(Gesture::long_cancel, snap(2000));CHECK(m.emergency() == Emergency::cancelled);
    CHECK(m.take_send_request(req) == false);
}

TEST_CASE("ui-model: cancelled auto-returns to idle after its window") {
    UiModel m;
    m.on_gesture(Gesture::long_arm, snap(1000)); m.on_gesture(Gesture::long_cancel, snap(2000));
    m.on_tick(snap(2000 + kCancelledMs + 1));
    CHECK(m.emergency() == Emergency::idle);
}

TEST_CASE("ui-model: arming countdown is visible and decreases") {
    UiModel m;
    m.on_gesture(Gesture::long_arm, snap(1000));
    const uint8_t a = m.arming_secs_left(snap(1200));
    const uint8_t b = m.arming_secs_left(snap(2400));
    CHECK(b < a);
}

TEST_CASE("ui-model: attempts are counted on ACCEPTANCE, not on request") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::long_arm, snap(1000)); m.on_gesture(Gesture::long_fire, snap(4500));
    const bool got = m.take_send_request(req);           // ★ ONE call — it DRAINS; see the file header note
    CHECK(got == true);
    if (!got) return;
    m.on_send_refused(SendKind::emergency, RefuseReason::parser, MESHROUTE_NS::CmdCode::err_unsupported, 4600);   // put nothing on air
    CHECK(m.emergency() == Emergency::failed);
    CHECK(m.attempts() == 0);                                          // no alarm consumed
}

TEST_CASE("ui-model: exactly THREE accepted transmissions, then sticky NOT HEARD") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::long_arm, snap(1000)); m.on_gesture(Gesture::long_fire, snap(4500));
    for (int i = 1; i <= 3; ++i) {
        const bool got = m.take_send_request(req);       // ★ ONE call per attempt
        CHECK(got == true);
        if (!got) return;
        m.on_send_accepted(SendKind::emergency, 5000u * uint32_t(i));
        CHECK(m.attempts() == i);
        m.on_outcome(SendOutcome::channel_no_relay(), 5000u * uint32_t(i) + 100);
    }
    CHECK(m.emergency() == Emergency::not_heard);
    CHECK(m.take_send_request(req) == false);
}

TEST_CASE("ui-model: blocked computes the deadline from the OUTCOME time, not the gesture") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::long_arm, snap(1000)); m.on_gesture(Gesture::long_fire, snap(4500));
    m.take_send_request(req); m.on_send_accepted(SendKind::emergency, 5000);
    m.on_outcome(SendOutcome::blocked(10000), /*now_ms=*/60000);
    CHECK(m.emergency() == Emergency::blocked);
    CHECK(m.retry_at_ms() == 70000);                    // 60000 + 10000, NOT 4500 + 10000
    m.on_tick(snap(69000)); CHECK(m.take_send_request(req) == false);
    m.on_tick(snap(70001)); CHECK(m.take_send_request(req) == true);
}

TEST_CASE("ui-model: next_ms == 0 backs off instead of spinning, and consumes no attempt") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::long_arm, snap(1000)); m.on_gesture(Gesture::long_fire, snap(4500));
    m.take_send_request(req); m.on_send_accepted(SendKind::emergency, 5000);
    m.on_outcome(SendOutcome::blocked(0), 5100);
    CHECK(m.emergency() == Emergency::blocked);
    CHECK(m.retry_at_ms() == 5100 + kBlockedBackoffMinMs);
    m.on_tick(snap(5100 + kBlockedBackoffMinMs - 1)); CHECK(m.take_send_request(req) == false);
    m.on_tick(snap(5100 + kBlockedBackoffMinMs + 1)); CHECK(m.take_send_request(req) == true);
    CHECK(m.attempts() == 1);                            // the block did not consume an alarm
}

TEST_CASE("ui-model: retry deadline is wrap-safe") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::long_arm, snap(1000)); m.on_gesture(Gesture::long_fire, snap(4500));
    m.take_send_request(req); m.on_send_accepted(SendKind::emergency, 0xFFFFF000u);
    m.on_outcome(SendOutcome::blocked(0x2000), 0xFFFFF000u);   // deadline wraps past 2^32
    m.on_tick(snap(0x00001001u));
    CHECK(m.take_send_request(req) == true);
}

TEST_CASE("ui-model: long gestures work from inside a compose sub-view") {
    UiModel m; const auto s = snap();
    to_team(m, s); m.on_gesture(Gesture::double_press, s);   // §UI-17 S1: enter the list, then the DM sub-view
    CHECK(m.state().compose == Compose::dm);
    m.on_gesture(Gesture::long_arm, s);
    CHECK(m.emergency() == Emergency::arming);
}

TEST_CASE("ui-model: long gestures work from a blanked panel") {
    UiModel m;
    m.on_tick(snap(1000)); m.on_tick(snap(1000 + kBlankMs + 1));
    CHECK(m.state().blanked == true);
    m.on_gesture(Gesture::long_arm, snap(1000 + kBlankMs + 10));
    CHECK(m.emergency() == Emergency::arming);
    CHECK(m.state().blanked == false);
}

TEST_CASE("ui-model: a matching teammate reply becomes sticky human confirmation") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::long_arm, snap(1000)); m.on_gesture(Gesture::long_fire, snap(4500));
    m.take_send_request(req); m.on_send_accepted(SendKind::emergency, 5000);
    m.on_outcome(SendOutcome::channel_relayed(), 5100);
    CHECK(m.emergency() == Emergency::picked_up);
    m.on_reply("Ann", "on my way", 6000);
    CHECK(m.emergency() == Emergency::reply);
}

TEST_CASE("ui-model: DM outcomes are independent of the emergency machine") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::long_arm, snap(1000)); m.on_gesture(Gesture::long_fire, snap(4500));
    m.take_send_request(req); m.on_send_accepted(SendKind::emergency, 5000);
    m.on_send_accepted(SendKind::dm, 5100);                       // a DM in flight alongside
    m.on_outcome(SendOutcome::dm_no_key(), 5200);
    CHECK(m.dm_state()  == DmState::no_key);
    CHECK(m.emergency() == Emergency::firing);                    // UNTOUCHED
    m.on_outcome(SendOutcome::channel_relayed(), 5300);
    CHECK(m.emergency() == Emergency::picked_up);
}

// ---------------------------------------------------------------- coverage the plan's twelve leave open

// ★ B65, RULED FIX (plan "Findings from Tasks 1-2"): the blank timer measures "time since the user last acted", and
// before the first tick there is no such time. NV format-on-corrupt is a shipped path and delays boot by design, so a
// first tick at millis() > kBlankMs is real — and with _last_input_ms = 0 it blanked a panel that never drew.
TEST_CASE("ui-model: B65 — a FIRST tick long after boot must not blank the panel") {
    UiModel m;
    m.on_tick(snap(90000));                       // first ever tick, 90 s after boot (slow NV self-heal)
    CHECK(m.state().blanked == false);             // the panel gets its full window from HERE
    CHECK(m.state().dirty   == true);              // and the frame it owes is still pending
    m.on_tick(snap(90000 + kBlankMs - 1));
    CHECK(m.state().blanked == false);
    m.on_tick(snap(90000 + kBlankMs));
    CHECK(m.state().blanked == true);              // blanks a full window later, not immediately
}

TEST_CASE("ui-model: B65 — a gesture before the first tick still owns the blank timer") {
    UiModel m;
    m.on_gesture(Gesture::short_press, snap(1000));      // the user acted at 1000
    m.on_tick(snap(30000));                              // first tick arrives 29 s later
    CHECK(m.state().blanked == true);                    // 29 s of no input -> blank, NOT re-seeded to 30000
}

// Spec §4.3: mark dirty ONLY when the visible countdown digit changes, or the emergency repaints at tick rate.
TEST_CASE("ui-model: the arming countdown marks dirty only when its DIGIT changes") {
    UiModel m;
    m.on_gesture(Gesture::long_arm, snap(1000));         // fire_at = 4500
    m.on_tick(snap(1200)); m.clear_dirty();              // digit 4
    m.on_tick(snap(1300));
    CHECK(m.state().dirty == false);                     // still 4 -> no repaint
    m.on_tick(snap(1600));
    CHECK(m.state().dirty == true);                      // 3 -> repaint
}

// Spec §4.3 + §5: the hold is a DEADLINE. It must beat the kBlankMs blank, and it must expire at kEmgHoldMs.
// ⚠ Every hold case below is written against `kEmgHoldMs`, never a literal — the owner re-ruled the value on
// 2026-08-04 and a test carrying the old number would have pinned the wrong behaviour while still passing.
TEST_CASE("ui-model: a firing emergency holds the panel past kBlankMs and blanks at kEmgHoldMs") {
    UiModel m;
    m.on_gesture(Gesture::long_arm, snap(1000)); m.on_gesture(Gesture::long_fire, snap(4500));
    m.on_tick(snap(4500 + kBlankMs + 1));
    CHECK(m.state().blanked == false);                   // held: sending is what the screen is about
    m.on_tick(snap(4500 + kEmgHoldMs + 1));
    CHECK(m.state().blanked == true);                    // one window, then blank with state RETAINED
    CHECK(m.emergency() == Emergency::firing);
}

// ★ Spec §4.3's table: the hold is reset by long_fire AND by EVERY retained outcome — picked_up, not_heard, blocked,
// reply. The plan's code block set it only on long_fire and on_reply; this case is what makes the difference visible.
TEST_CASE("ui-model: PICKED UP refreshes the kEmgHoldMs hold from the outcome time") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::long_arm, snap(1000)); m.on_gesture(Gesture::long_fire, snap(4500));
    m.take_send_request(req); m.on_send_accepted(SendKind::emergency, 5000);
    m.on_outcome(SendOutcome::channel_relayed(), 60000);           // relay evidence 55 s after the fire
    CHECK(m.emergency() == Emergency::picked_up);
    m.on_tick(snap(4500 + kEmgHoldMs + 1));
    CHECK(m.state().blanked == false);                             // the fire-anchored deadline has passed; refreshed
    m.on_tick(snap(60000 + kEmgHoldMs + 1));
    CHECK(m.state().blanked == true);
}

TEST_CASE("ui-model: NOT HEARD and BLOCKED refresh the hold too") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::long_arm, snap(1000)); m.on_gesture(Gesture::long_fire, snap(4500));
    m.take_send_request(req); m.on_send_accepted(SendKind::emergency, 5000);
    m.on_outcome(SendOutcome::blocked(10000), 50000);
    CHECK(m.emergency() == Emergency::blocked);
    m.on_tick(snap(4500 + kEmgHoldMs + 1));
    CHECK(m.state().blanked == false);                              // blocked refreshed it
    UiModel n; SendReq r2{};
    n.on_gesture(Gesture::long_arm, snap(1000)); n.on_gesture(Gesture::long_fire, snap(4500));
    for (int i = 1; i <= 3; ++i) {
        const bool got = n.take_send_request(r2);
        CHECK(got == true);
        if (!got) return;
        n.on_send_accepted(SendKind::emergency, 40000u + 1000u * uint32_t(i));
        n.on_outcome(SendOutcome::channel_no_relay(), 40000u + 1000u * uint32_t(i) + 100u);
    }
    CHECK(n.emergency() == Emergency::not_heard);
    n.on_tick(snap(4500 + kEmgHoldMs + 1));
    CHECK(n.state().blanked == false);                              // not_heard refreshed it
}

TEST_CASE("ui-model: the emergency hold is wrap-safe") {
    UiModel m;
    m.on_gesture(Gesture::long_arm, snap(0xFFFFF000u));
    m.on_gesture(Gesture::long_fire, snap(0xFFFFF000u + 3500u));   // hold deadline wraps past 2^32
    m.on_tick(snap(0x00002000u));                                   // ~8 s past the wrap, inside kEmgHoldMs
    CHECK(m.state().blanked == false);
    CHECK(m.emergency() == Emergency::firing);
}

// Spec §4.4: only firing/blocked/picked_up/not_heard/reply may become REPLY, and only after a transmission was
// ACCEPTED — otherwise coincident channel-0 chatter manufactures confirmation of a message never sent.
TEST_CASE("ui-model: a reply during ARMING, CANCELLED, FAILED or IDLE is refused") {
    UiModel idle; idle.on_reply("Ann", "hi", 1000);
    CHECK(idle.emergency() == Emergency::idle);

    UiModel arming; arming.on_gesture(Gesture::long_arm, snap(1000));
    arming.on_reply("Ann", "hi", 1200);
    CHECK(arming.emergency() == Emergency::arming);                 // the user has not even committed yet

    UiModel canc; canc.on_gesture(Gesture::long_arm, snap(1000)); canc.on_gesture(Gesture::long_cancel, snap(2000));
    canc.on_reply("Ann", "hi", 2100);
    CHECK(canc.emergency() == Emergency::cancelled);

    UiModel fail; SendReq req{};
    fail.on_gesture(Gesture::long_arm, snap(1000)); fail.on_gesture(Gesture::long_fire, snap(4500));
    fail.take_send_request(req); fail.on_send_refused(SendKind::emergency, RefuseReason::unsealable, MESHROUTE_NS::CmdCode::err_unsupported, 4600);
    fail.on_reply("Ann", "hi", 5000);
    CHECK(fail.emergency() == Emergency::failed);                   // nothing went out -> no confirmation
}

TEST_CASE("ui-model: a reply while FIRING with zero accepted attempts is refused") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::long_arm, snap(1000)); m.on_gesture(Gesture::long_fire, snap(4500));
    m.take_send_request(req);                                       // requested, never accepted
    CHECK(m.attempts() == 0);
    m.on_reply("Ann", "on my way", 5000);
    CHECK(m.emergency() == Emergency::firing);                      // NOT reply
}

TEST_CASE("ui-model: a reply from NOT HEARD is accepted and is sticky") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::long_arm, snap(1000)); m.on_gesture(Gesture::long_fire, snap(4500));
    for (int i = 1; i <= 3; ++i) {
        const bool got = m.take_send_request(req);
        CHECK(got == true);
        if (!got) return;
        m.on_send_accepted(SendKind::emergency, 5000u * uint32_t(i));
        m.on_outcome(SendOutcome::channel_no_relay(), 5000u * uint32_t(i) + 100);
    }
    CHECK(m.emergency() == Emergency::not_heard);
    m.on_reply("Bob", "coming", 30000);
    CHECK(m.emergency() == Emergency::reply);                        // a human answered after all
    m.on_outcome(SendOutcome::channel_no_relay(), 31000);
    CHECK(m.emergency() == Emergency::reply);                        // sticky: a late outcome cannot undo it
}

TEST_CASE("ui-model: reply text and sender are clamped and NUL-terminated") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::long_arm, snap(1000)); m.on_gesture(Gesture::long_fire, snap(4500));
    m.take_send_request(req); m.on_send_accepted(SendKind::emergency, 5000);
    m.on_reply("AnnabelleTheVeryLongName", "a reply far longer than the panel can ever show", 6000);
    CHECK(m.emergency() == Emergency::reply);
    CHECK(std::strlen(m.reply_who())  == kLabelCap);
    CHECK(std::strlen(m.reply_text()) == 20);
    CHECK(std::strncmp(m.reply_who(), "AnnabelleTheV", 13) == 0);
    m.on_reply(nullptr, nullptr, 7000);                             // a missing label must not read off the end
    CHECK(std::strlen(m.reply_who())  == 0);
    CHECK(std::strlen(m.reply_text()) == 0);
}

// Spec §2.1: unrelated channel/DM traffic must never move the emergency. The tracker filters by ctr/peer, but the
// model is the second line of defence — an outcome arriving in a non-live state is ignored.
TEST_CASE("ui-model: a channel outcome cannot start, resurrect or alter a non-live emergency") {
    UiModel idle; idle.on_outcome(SendOutcome::channel_relayed(), 1000);
    CHECK(idle.emergency() == Emergency::idle);                      // never became picked_up

    UiModel arming; arming.on_gesture(Gesture::long_arm, snap(1000));
    arming.on_outcome(SendOutcome::channel_relayed(), 1200);
    CHECK(arming.emergency() == Emergency::arming);
    arming.on_outcome(SendOutcome::blocked(5000), 1300);
    CHECK(arming.emergency() == Emergency::arming);                  // and a blocked DM cannot show BLOCKED

    UiModel canc; canc.on_gesture(Gesture::long_arm, snap(1000)); canc.on_gesture(Gesture::long_cancel, snap(2000));
    canc.on_outcome(SendOutcome::channel_relayed(), 2100);
    CHECK(canc.emergency() == Emergency::cancelled);
}

TEST_CASE("ui-model: PICKED UP is sticky against a later no-relay outcome") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::long_arm, snap(1000)); m.on_gesture(Gesture::long_fire, snap(4500));
    m.take_send_request(req); m.on_send_accepted(SendKind::emergency, 5000);
    m.on_outcome(SendOutcome::channel_relayed(), 5100);
    CHECK(m.emergency() == Emergency::picked_up);
    m.on_outcome(SendOutcome::channel_no_relay(), 5200);
    CHECK(m.emergency() == Emergency::picked_up);
    CHECK(m.take_send_request(req) == false);                        // and it queued no retry
}

TEST_CASE("ui-model: the blocked backoff doubles and caps at kBlockedBackoffMaxMs, consuming no attempt") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::long_arm, snap(1000)); m.on_gesture(Gesture::long_fire, snap(4500));
    m.take_send_request(req); m.on_send_accepted(SendKind::emergency, 5000);
    uint32_t t = 5100, expect = kBlockedBackoffMinMs;
    for (int i = 0; i < 6; ++i) {
        m.on_outcome(SendOutcome::blocked(0), t);
        CHECK(m.retry_at_ms() == t + expect);
        m.on_tick(snap(t + expect + 1));                            // fires the retry, re-arms the slot
        CHECK(m.take_send_request(req) == true);
        t += expect + 10;
        expect = (expect * 2 > kBlockedBackoffMaxMs) ? kBlockedBackoffMaxMs : expect * 2;
    }
    CHECK(expect == kBlockedBackoffMaxMs);
    CHECK(m.attempts() == 1);                                       // six blocks, still one accepted transmission
}

TEST_CASE("ui-model: a fresh long_fire re-arms the alarm from a sticky NOT HEARD") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::long_arm, snap(1000)); m.on_gesture(Gesture::long_fire, snap(4500));
    for (int i = 1; i <= 3; ++i) {
        const bool got = m.take_send_request(req);
        CHECK(got == true);
        if (!got) return;
        m.on_send_accepted(SendKind::emergency, 5000u * uint32_t(i));
        m.on_outcome(SendOutcome::channel_no_relay(), 5000u * uint32_t(i) + 100);
    }
    CHECK(m.emergency() == Emergency::not_heard);
    m.on_gesture(Gesture::long_arm,  snap(40000));
    m.on_gesture(Gesture::long_fire, snap(43500));
    CHECK(m.emergency() == Emergency::firing);
    CHECK(m.attempts() == 0);                                       // the budget resets with the new alarm
    CHECK(m.take_send_request(req) == true);
}

TEST_CASE("ui-model: cancelled returns to idle and a later arm works normally") {
    UiModel m;
    m.on_gesture(Gesture::long_arm, snap(1000)); m.on_gesture(Gesture::long_cancel, snap(2000));
    m.on_tick(snap(2000 + kCancelledMs + 1));
    CHECK(m.emergency() == Emergency::idle);
    m.on_gesture(Gesture::long_arm, snap(5000));
    CHECK(m.emergency() == Emergency::arming);
    CHECK(m.arming_secs_left(snap(5100)) > 0);
}

// ★ Spec §2.1's two slots: an emergency must never wait behind, or be overwritten by, normal UI work.
TEST_CASE("ui-model: a queued alarm drains BEFORE a queued DM and neither is lost") {
    UiModel m; const auto s = snap(); SendReq req{};
    to_team(m, s); m.on_gesture(Gesture::double_press, s);                           // DM modal
    m.on_gesture(Gesture::double_press, s);                                          // queue the canned DM
    m.on_gesture(Gesture::long_arm,  s);
    m.on_gesture(Gesture::long_fire, s);                                             // queue the alarm
    const bool first = m.take_send_request(req);
    CHECK(first == true);
    if (first) CHECK(req.kind == SendKind::emergency);                                // ALARM FIRST
    const bool second = m.take_send_request(req);
    CHECK(second == true);
    if (second) { CHECK(req.kind == SendKind::dm); CHECK(req.peer_id == s.team[0].id); }   // and the DM survived
    CHECK(m.take_send_request(req) == false);
}

TEST_CASE("ui-model: a compose send cannot overwrite a queued alarm") {
    UiModel m; const auto s = snap(); SendReq req{};
    m.on_gesture(Gesture::long_arm,  s); m.on_gesture(Gesture::long_fire, s);
    CHECK(m.emergency_pending() == true);
    for (int i = 0; i < 3; ++i) m.on_gesture(Gesture::short_press, s);   // navigate to SEND
    m.on_gesture(Gesture::double_press, s);                             // channel modal
    m.on_gesture(Gesture::double_press, s);                             // queue a canned channel post
    CHECK(m.emergency_pending() == true);                               // untouched
    const bool got = m.take_send_request(req);
    CHECK(got == true);
    if (got) CHECK(req.kind == SendKind::emergency);
}

// ---- the DM outcome machine (spec §3.4.1)

TEST_CASE("ui-model: the DM machine walks accepted -> waiting -> delivered") {
    UiModel m;
    CHECK(m.dm_state() == DmState::idle);
    m.on_send_accepted(SendKind::dm, 1000);
    CHECK(m.dm_state() == DmState::waiting_ack);
    m.on_outcome(SendOutcome::dm_acked(), 2000);
    CHECK(m.dm_state() == DmState::delivered);
}

TEST_CASE("ui-model: NO CONFIRM is reachable and a LATE ack upgrades it to DELIVERED") {
    UiModel m;
    m.on_send_accepted(SendKind::dm, 1000);
    m.on_outcome(SendOutcome::dm_timeout(), 61000);
    CHECK(m.dm_state() == DmState::not_confirmed);          // spec §3.4.1: e2e_ack_timeout -> NO CONFIRM
    m.on_outcome(SendOutcome::dm_acked(), 65000);
    CHECK(m.dm_state() == DmState::delivered);              // the core permits a late ack; truth beats tidiness
}

TEST_CASE("ui-model: a DM refusal and a DM failure are terminal and distinct from no_key") {
    UiModel m;
    m.on_send_refused(SendKind::dm, RefuseReason::parser, MESHROUTE_NS::CmdCode::err_unsupported, 1000);
    CHECK(m.dm_state() == DmState::failed);
    CHECK(m.refuse_reason() == RefuseReason::parser);
    UiModel n;
    n.on_send_accepted(SendKind::dm, 1000);
    n.on_outcome(SendOutcome::dm_failed(FailReason::no_route), 2000);   // §B73: the reason is now REQUIRED
    CHECK(n.dm_state() == DmState::failed);
    UiModel o;
    o.on_send_accepted(SendKind::dm, 1000);
    o.on_outcome(SendOutcome::dm_no_key(), 2000);
    CHECK(o.dm_state() == DmState::no_key);                 // the one failure the user cannot resolve on-device
}

TEST_CASE("ui-model: a DM outcome never touches a live emergency, in either direction") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::long_arm, snap(1000)); m.on_gesture(Gesture::long_fire, snap(4500));
    m.take_send_request(req); m.on_send_accepted(SendKind::emergency, 5000);
    m.on_outcome(SendOutcome::dm_acked(), 5100);
    CHECK(m.emergency() == Emergency::firing);
    m.on_outcome(SendOutcome::dm_timeout(), 5200);
    CHECK(m.emergency() == Emergency::firing);
    CHECK(m.take_send_request(req) == false);               // and no DM outcome queued an alarm retry
    m.on_outcome(SendOutcome::blocked(5000), 5300);         // the emergency's own block still lands
    CHECK(m.emergency() == Emergency::blocked);
    CHECK(m.dm_state()  == DmState::not_confirmed);         // the DM's state is untouched by it
}

// §B68's eighth kind. It is a SUCCESS with no local handle, unreachable on the team-plane alarm path — see the
// in-source note and register B69 for the render obligation it still lacks a carrier for.
TEST_CASE("ui-model: channel_remote_mint is handled explicitly and never claims PICKED UP") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::long_arm, snap(1000)); m.on_gesture(Gesture::long_fire, snap(4500));
    m.take_send_request(req); m.on_send_accepted(SendKind::emergency, 5000);
    m.on_outcome(SendOutcome::channel_remote_mint(), 5100);
    CHECK(m.emergency() != Emergency::picked_up);           // no relay evidence exists -> must not claim it
    CHECK(m.emergency() == Emergency::firing);              // treated as unconfirmed: bounded retry continues
    CHECK(m.attempts() == 1);
    CHECK(m.take_send_request(req) == true);
}

// ---- the four UI-3 QA fixes (§B72-§B75). Each case below FAILS against the pre-fix header, which is the only
// evidence that it tests the fix rather than the code.

// ★★ §B74. `_retry_at_ms` was compared against a `0xFFFFFFFF` "no deadline" SENTINEL while being computed as an
// UNBOUNDED `now_ms + next_ms` — so an ordinary block could produce exactly the reserved value and tick_emergency
// then refused to examine the deadline at all: BLOCKED for ever, on the alarm path, with two of three alarms unspent.
// Pre-fix this case reads `retried == false`.
TEST_CASE("ui-model: a retry deadline of 0xFFFFFFFF is a DEADLINE, not 'no deadline'") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::long_arm,  snap(0xFFFFF000u - kArmToFireMs));
    m.on_gesture(Gesture::long_fire, snap(0xFFFFF000u));
    const bool got = m.take_send_request(req);
    CHECK(got == true);
    if (!got) return;
    m.on_send_accepted(SendKind::emergency, 0xFFFFF000u);
    m.on_outcome(SendOutcome::blocked(0xFFFu), 0xFFFFF000u);   // 0xFFFFF000 + 0xFFF == 0xFFFFFFFF, the old sentinel
    CHECK(m.emergency()   == Emergency::blocked);
    CHECK(m.retry_at_ms() == 0xFFFFFFFFu);                     // the value itself is legal and is kept verbatim
    m.on_tick(snap(0xFFFFFFFEu));
    CHECK(m.take_send_request(req) == false);                  // one tick early: still blocked
    m.on_tick(snap(0xFFFFFFFFu));                              // now == deadline
    const bool retried = m.take_send_request(req);
    CHECK(retried == true);                                    // pre-fix: FALSE, and never true again
    if (!retried) return;
    CHECK(req.kind      == SendKind::emergency);
    CHECK(m.emergency() == Emergency::firing);
    CHECK(m.attempts()  == 1);                                 // the block consumed no alarm
}

// ★★ §B72. `channel_failed` did not exist, so a pre-enqueue SEAL failure had no representation: `match_channel_sent`
// can never fire for a ctr==0 result, and the alarm sat on SENDING... for ever. Pre-fix this case does not COMPILE
// (`'channel_failed' is not a member of 'mrui::SendOutcome'`), which is the strongest form of failing first.
TEST_CASE("ui-model: a channel SEAL FAILURE is terminal and names its reason, never a stuck SENDING") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::long_arm, snap(1000)); m.on_gesture(Gesture::long_fire, snap(4500));
    const bool got = m.take_send_request(req);
    CHECK(got == true);
    if (!got) return;
    m.on_send_accepted(SendKind::emergency, 5000);
    m.on_outcome(SendOutcome::channel_failed(FailReason::unsealable), 5100);
    CHECK(m.emergency()     == Emergency::failed);              // terminal + actionable
    CHECK(m.refuse_reason() == RefuseReason::unsealable);       // "encrypt / grant the key", not a generic failure
    CHECK(m.fail_reason()   == FailReason::unsealable);
    CHECK(m.take_send_request(req) == false);                   // PERMANENT for this route -> must NOT retry
    CHECK(m.attempts()      == 1);                              // and it consumed no further alarm
}

TEST_CASE("ui-model: a channel seal failure outside a live alarm is dropped whole") {
    UiModel idle;
    idle.on_outcome(SendOutcome::channel_failed(FailReason::unsealable), 1000);
    CHECK(idle.emergency()     == Emergency::idle);              // spec 2.1: it cannot manufacture a FAILED alarm
    CHECK(idle.fail_reason()   == FailReason::none);
    CHECK(idle.refuse_reason() == RefuseReason::other);          // the construction default, untouched

    UiModel picked; SendReq req{};                               // nor demote a sticky success
    picked.on_gesture(Gesture::long_arm, snap(1000)); picked.on_gesture(Gesture::long_fire, snap(4500));
    picked.take_send_request(req); picked.on_send_accepted(SendKind::emergency, 5000);
    picked.on_outcome(SendOutcome::channel_relayed(), 5100);
    CHECK(picked.emergency() == Emergency::picked_up);
    picked.on_outcome(SendOutcome::channel_failed(FailReason::unsealable), 5200);
    CHECK(picked.emergency() == Emergency::picked_up);
}

// ★ §B75. Spec 3.4.1 requires `submitting` while the command is in flight to dispatch. The enumerator existed and
// NOTHING assigned it — and the header claimed it was "written-but-unread", which was false in both halves.
// Pre-fix this case reads `idle` at the submitting CHECK.
TEST_CASE("ui-model: draining a DM request enters SUBMITTING, and only a DM does") {
    UiModel m; const auto s = snap(); SendReq req{};
    to_team(m, s);                                               // -> TEAM, and ENTER the list (§UI-17 S1)
    m.on_gesture(Gesture::double_press, s);                      // -> DM modal, bound to row 0
    m.on_gesture(Gesture::double_press, s);                      // queue "Are you OK?"
    CHECK(m.dm_state() == DmState::idle);                        // QUEUED is not yet submitted
    m.clear_dirty();                                             // so the repaint below is the DRAIN's, not the gesture's
    const bool got = m.take_send_request(req);
    CHECK(got == true);
    if (!got) return;
    CHECK(req.kind     == SendKind::dm);
    CHECK(m.dm_state() == DmState::submitting);                  // spec 3.4.1 -> "SENDING..."
    CHECK(m.state().dirty == true);                              // and the panel is asked to say so
    m.on_send_accepted(SendKind::dm, 2000);
    CHECK(m.dm_state() == DmState::waiting_ack);

    UiModel e; SendReq er{};                                     // an ALARM drain must not touch the DM machine
    e.on_gesture(Gesture::long_arm, snap(1000)); e.on_gesture(Gesture::long_fire, snap(4500));
    const bool egot = e.take_send_request(er);
    CHECK(egot == true);
    if (!egot) return;
    CHECK(er.kind      == SendKind::emergency);
    CHECK(e.dm_state() == DmState::idle);
}

// ★★ §B73. `dm_failed()` carried no reason, so every async DM failure landed on `refuse_reason() == other` — spec
// 2.1 rule 6 ("the FULL SendFailReason reaches the UI, not a boolean") unsatisfiable. Pre-fix the first block reads
// `other` and there is no `fail_reason()` to read at all.
TEST_CASE("ui-model: an async DM failure carries its reason instead of collapsing to 'other'") {
    UiModel m;
    m.on_send_accepted(SendKind::dm, 1000);
    m.on_outcome(SendOutcome::dm_failed(FailReason::unsealable), 2000);
    CHECK(m.dm_state()      == DmState::failed);
    CHECK(m.refuse_reason() == RefuseReason::unsealable);        // pre-fix: RefuseReason::other
    CHECK(m.fail_reason()   == FailReason::unsealable);

    UiModel n;                                                   // a reason with no compact code still survives whole
    n.on_send_accepted(SendKind::dm, 1000);
    n.on_outcome(SendOutcome::dm_failed(FailReason::no_route), 2000);
    CHECK(n.refuse_reason() == RefuseReason::other);             // ...the panel shows the generic line...
    CHECK(n.fail_reason()   == FailReason::no_route);            // ...but UI-7 can still NAME it (spec 2.1 rule 6)
}

TEST_CASE("ui-model: the compact reason mapping is the three whose remedy differs, and nothing else") {
    struct { FailReason in; RefuseReason out; } cases[] = {
        { FailReason::unsealable,          RefuseReason::unsealable  },   // encrypt / obtain the team key
        { FailReason::no_location,         RefuseReason::no_location },   // get a fix / cfg set lat,lon
        { FailReason::queue_full,          RefuseReason::queue_full  },   // transient: retry
        { FailReason::no_pubkey,           RefuseReason::other       },
        { FailReason::e2e_ack_timeout,     RefuseReason::other       },
        { FailReason::reprovisioned,       RefuseReason::other       },
        { FailReason::gateway_unreachable, RefuseReason::other       },
        { FailReason::none,                RefuseReason::other       },
    };
    for (const auto& c : cases) {
        UiModel m;
        m.on_send_accepted(SendKind::dm, 1000);
        m.on_outcome(SendOutcome::dm_failed(c.in), 2000);
        CHECK(m.refuse_reason() == c.out);
        CHECK(m.fail_reason()   == c.in);                        // lossless in every one of the eight
    }
}

// A SYNCHRONOUS refusal never became a core send, so it must not leave a stale core reason behind it.
TEST_CASE("ui-model: a synchronous refusal clears the core reason it does not have") {
    UiModel m;
    m.on_send_accepted(SendKind::dm, 1000);
    m.on_outcome(SendOutcome::dm_failed(FailReason::unsealable), 2000);
    CHECK(m.fail_reason() == FailReason::unsealable);
    m.on_send_accepted(SendKind::dm, 3000);                      // a second DM, refused by the parser this time
    m.on_send_refused(SendKind::dm, RefuseReason::parser, MESHROUTE_NS::CmdCode::err_unsupported, 3100);
    CHECK(m.dm_state()      == DmState::failed);
    CHECK(m.refuse_reason() == RefuseReason::parser);
    CHECK(m.fail_reason()   == FailReason::none);                // NOT the previous send's `unsealable`
}

// ---- §B78 (owner-ruled 2026-08-04): a TERMINAL `failed` alarm is retained and holds the panel like every other
// emergency outcome. ⚠ All three cases are written against `kEmgHoldMs` and `kBlankMs`, never a literal.
// ★ Each one also pins the ANCHOR: the failure timestamp is deliberately placed a full window PAST the gesture, so a
// `retain()` that used `_last_input_ms` instead of the passed `now_ms` blanks the panel on the very first tick.

// Pre-fix (`failed` absent from hold_active's set) the first CHECK reads `blanked == true`.
TEST_CASE("ui-model: a synchronous emergency REFUSAL holds the panel for a full kEmgHoldMs window") {
    UiModel m; SendReq req{};
    const uint32_t refused_at = 4500 + kEmgHoldMs + 1000;        // the gesture-anchored window is already CLOSED
    m.on_gesture(Gesture::long_arm, snap(1000)); m.on_gesture(Gesture::long_fire, snap(4500));
    const bool got = m.take_send_request(req);
    CHECK(got == true);
    if (!got) return;
    m.on_send_refused(SendKind::emergency, RefuseReason::parser, MESHROUTE_NS::CmdCode::err_unsupported, refused_at);
    CHECK(m.emergency() == Emergency::failed);
    m.on_tick(snap(refused_at + 1));
    CHECK(m.state().blanked == false);          // held — and by the REFUSAL's deadline, the gesture's expired long ago
    m.on_tick(snap(refused_at + kEmgHoldMs - 1));
    CHECK(m.state().blanked == false);           // still inside its own window
    m.on_tick(snap(refused_at + kEmgHoldMs + 1));
    CHECK(m.state().blanked == true);            // and it DOES expire — bounded hold, not a stuck-on panel
    CHECK(m.emergency() == Emergency::failed);   // with the state retained behind it (spec 5)
}

// Same contract on the ASYNCHRONOUS path. Pre-fix this case does not compile at all (no `channel_failed`).
TEST_CASE("ui-model: a channel SEAL FAILURE holds the panel for a full kEmgHoldMs window too") {
    UiModel m; SendReq req{};
    const uint32_t failed_at = 4500 + kEmgHoldMs + 1000;
    m.on_gesture(Gesture::long_arm, snap(1000)); m.on_gesture(Gesture::long_fire, snap(4500));
    const bool got = m.take_send_request(req);
    CHECK(got == true);
    if (!got) return;
    m.on_send_accepted(SendKind::emergency, 5000);
    m.on_outcome(SendOutcome::channel_failed(FailReason::unsealable), failed_at);
    CHECK(m.emergency() == Emergency::failed);
    m.on_tick(snap(failed_at + 1));
    CHECK(m.state().blanked == false);
    m.on_tick(snap(failed_at + kEmgHoldMs - 1));
    CHECK(m.state().blanked == false);
    m.on_tick(snap(failed_at + kEmgHoldMs + 1));
    CHECK(m.state().blanked == true);
    CHECK(m.emergency()     == Emergency::failed);
    CHECK(m.refuse_reason() == RefuseReason::unsealable);   // the reason survives the blank, so UI-6 can still show it
}

// ★ The other half of the ruling: only the EMERGENCY branch retains. A DM refusal arriving mid-alarm must not push the
// alarm's deadline out — that would keep a resolved emergency screen alive on unrelated traffic.
TEST_CASE("ui-model: a DM refusal does NOT extend the emergency hold") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::long_arm, snap(1000)); m.on_gesture(Gesture::long_fire, snap(4500));
    m.take_send_request(req); m.on_send_accepted(SendKind::emergency, 5000);
    m.on_outcome(SendOutcome::channel_relayed(), 5100);            // picked_up: hold anchored at 5100
    m.on_send_refused(SendKind::dm, RefuseReason::parser, MESHROUTE_NS::CmdCode::err_unsupported, 5100 + kEmgHoldMs / 2);   // a DM refusal, mid-window
    CHECK(m.emergency() == Emergency::picked_up);                  // the alarm state is untouched
    CHECK(m.dm_state()  == DmState::failed);
    m.on_tick(snap(5100 + kEmgHoldMs + 1));
    CHECK(m.state().blanked == true);                              // the DM refusal did NOT refresh the alarm's window
}

TEST_CASE("ui-model: the UI-3 constants are the ones spec 4 fixed") {
    CHECK(kEmgMaxTries         == 3);          // THREE TRANSMISSIONS, counted on acceptance
    CHECK(kEmgHoldMs           == 30000u);   // ★ owner re-ruled 2026-08-04 (was 120000). The ONLY literal in the suite
    CHECK(kCancelledMs         == 1000u);
    CHECK(kBlockedBackoffMinMs == 2000u);
    CHECK(kBlockedBackoffMaxMs == 30000u);
    CHECK(kArmToFireMs         == 3500u);      // must match InputCfg::fire_ms or arm/fire disagree
    CHECK(InputCfg{}.fire_ms   == kArmToFireMs);
}

// ============================================================================================ §B115 — the FORMATTER
// ★ The DISPLAY side of §B115, at unit level: the model's ordinal is driven through the shipped glue in
//   test_firmware_ui_send.cpp (`ui7-b115: …`, where the whole `1 of 3` -> `2 of 3` -> `3 of 3` sequence lives). These
//   two cases pin the ONE formatter itself — the bytes, and the deliberate ABSENCE of a clamp.
TEST_CASE("ui-model: §B115 the attempt line renders the ordinal verbatim, against kEmgMaxTries") {
    char l[48];
    emg_attempt_line(l, sizeof l, 1);
    CHECK(std::strcmp(l, "attempt 1 of 3") == 0);
    emg_attempt_line(l, sizeof l, 3);
    CHECK(std::strcmp(l, "attempt 3 of 3") == 0);
}

// ★★★ THE NON-CLAMP IS A DECISION, SO IT IS PINNED. `4 of 3` is impossible by construction — `on_outcome` refuses to
//     queue a fourth attempt once `_tries >= kEmgMaxTries` — so if the panel ever shows it again, a REAL accounting
//     defect has returned and must be visible. A clamp here would have shown `2 -> 3 -> 3` on the bench run and B115
//     would still be undiscovered ([[B108]]'s rejected "clamp instead of fix"). ⇒ this case turns RED on a clamp, which
//     is exactly the discussion a future clamp must lose before it lands.
TEST_CASE("ui-model: §B115 the attempt line is DELIBERATELY not clamped to kEmgMaxTries") {
    char l[48];
    emg_attempt_line(l, sizeof l, uint8_t(kEmgMaxTries + 1));
    CHECK(std::strcmp(l, "attempt 4 of 3") == 0);
}

// ============================================================================================ §B71 — UI-6's EXIT
// ★★★ OWNER-RULED 2026-08-04, implemented by UI-6. Before it, `_emg` had NO path back to `idle`: a fired alarm owned
// the panel until reboot, and spec §4's "double acknowledges" / "double re-fires" were a contradiction that did
// neither. These cases pin the complete ruled table, including the two rows that say NOTHING happens.
//
// ⓘ The ruling's table lists a fifth exitable state, "final `blocked`". It is VACUOUS in this model and the assertion
//   below is what makes that visible rather than a silent judgement call: `on_outcome`'s blocked arm ALWAYS arms a
//   retry, so a `blocked` alarm is by construction still in flight. Four states exit, not five.

// A fired alarm carried to a RETAINED outcome, with one accepted transmission behind it (so on_reply's whitelist and
// the `_tries == 0` guard are both satisfied for the reply case).
static UiModel fired_with_outcome(const SendOutcome& o, uint32_t at_ms) {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::long_arm,  snap(1000));
    m.on_gesture(Gesture::long_fire, snap(4500));
    const bool got = m.take_send_request(req);          // ⚠ DRAINS — one call, into a local
    CHECK(got == true);
    m.on_send_accepted(SendKind::emergency, 5000);
    m.on_outcome(o, at_ms);
    return m;
}

// ★★ §B102/F3: B71's exit now requires the outcome to have been PRESENTED, not merely reached. `FrameGate` reports
// that when a frame COMPLETES; these cases predate the latch and need the fact rather than eight simulated pages.
// ⓘ The frame-driven version of the same rule is covered by the §B102 cases at the bottom of this file.
static void present(UiModel& m) { m.mark_outcome_presented(m.emergency(), m.emg_news()); }

TEST_CASE("ui-model: B71 — a short press on PICKED UP acknowledges and restores the cycle") {
    UiModel m = fired_with_outcome(SendOutcome::channel_relayed(), 5100);
    CHECK(m.emergency() == Emergency::picked_up);
    CHECK(m.emg_outcome_retained() == false);          // §B102: terminal, but not yet SEEN
    present(m);
    CHECK(m.emg_outcome_retained() == true);
    const Screen before = m.state().screen;
    m.on_gesture(Gesture::short_press, snap(6000));
    CHECK(m.emergency()      == Emergency::idle);      // ★ the exit that did not previously exist
    CHECK(m.state().screen   == before);               // the acknowledging press is SPENT on the alarm, not on the cycle
    CHECK(m.state().dirty    == true);
    // ...and the cycle is navigable again from the very next press.
    m.on_gesture(Gesture::short_press, snap(6500));
    CHECK(m.state().screen != before);
}

TEST_CASE("ui-model: B71 — NOT HEARD and FAILED are exitable too (the §B78 composition)") {
    UiModel a = fired_with_outcome(SendOutcome::channel_no_relay(), 5100);
    a.on_send_accepted(SendKind::emergency, 5200);
    a.on_outcome(SendOutcome::channel_no_relay(), 5300);
    a.on_send_accepted(SendKind::emergency, 5400);
    a.on_outcome(SendOutcome::channel_no_relay(), 5500);   // third transmission -> sticky NOT HEARD
    CHECK(a.emergency() == Emergency::not_heard);
    present(a);
    a.on_gesture(Gesture::short_press, snap(6000));
    CHECK(a.emergency() == Emergency::idle);

    // §B78 put `failed` in the retained set precisely so the hiker is never trapped on a failure screen.
    UiModel b = fired_with_outcome(SendOutcome::channel_failed(FailReason::unsealable), 5100);
    CHECK(b.emergency() == Emergency::failed);
    present(b);
    b.on_gesture(Gesture::short_press, snap(6000));
    CHECK(b.emergency() == Emergency::idle);
}

TEST_CASE("ui-model: B71 — a REPLY is exitable, and the exit does not resurrect it") {
    UiModel m = fired_with_outcome(SendOutcome::channel_relayed(), 5100);
    m.on_reply("Ann", "on my way", 5200);
    CHECK(m.emergency() == Emergency::reply);
    present(m);
    m.on_gesture(Gesture::short_press, snap(6000));
    CHECK(m.emergency() == Emergency::idle);
    // ★ on_reply's whitelist excludes `idle`, so a LATER teammate post cannot re-open a dismissed alarm.
    m.on_reply("Bob", "me too", 7000);
    CHECK(m.emergency() == Emergency::idle);
}

// ⛔ THE STICKY ROWS. An outcome the hiker never saw is the failure SAFETY-FIRST exists to prevent, so an alarm still
//    in flight must ignore a short press entirely.
TEST_CASE("ui-model: B71 — an IN-FLIGHT alarm does not exit (firing / arming / blocked-with-a-retry)") {
    UiModel f; SendReq req{};
    f.on_gesture(Gesture::long_arm,  snap(1000));
    CHECK(f.emergency() == Emergency::arming);
    f.on_gesture(Gesture::short_press, snap(1500));
    CHECK(f.emergency() == Emergency::arming);                 // arming is not an outcome
    f.on_gesture(Gesture::long_fire, snap(4500));
    const bool got = f.take_send_request(req);
    CHECK(got == true);
    f.on_send_accepted(SendKind::emergency, 5000);
    CHECK(f.emergency() == Emergency::firing);
    f.on_gesture(Gesture::short_press, snap(5500));
    CHECK(f.emergency() == Emergency::firing);                 // ⛔ SENDING... is sticky
    CHECK(f.emg_outcome_retained() == false);

    // ★ THE VACUOUS FIFTH ROW, asserted: a `blocked` alarm always has a live retry, so it is never "final".
    f.on_outcome(SendOutcome::blocked(5000), 5600);
    CHECK(f.emergency() == Emergency::blocked);
    CHECK(f.emg_outcome_retained() == false);                  // ⇒ "final blocked" does not exist in this model
    f.on_gesture(Gesture::short_press, snap(5700));
    CHECK(f.emergency() == Emergency::blocked);
    f.on_tick(snap(5600 + 5000 + 1));                          // the deadline arrives and it re-fires by itself
    CHECK(f.emergency() == Emergency::firing);
}

// ★ THE ROW THAT MAKES THE SHORT PRESS SAFE. A blanked panel consumes its waking press, so a retained outcome is
//   ALWAYS displayed before any press can dismiss it — that is the whole basis of the owner's ruling.
TEST_CASE("ui-model: B71 — from a BLANKED panel the first press only WAKES; the second acknowledges") {
    UiModel m = fired_with_outcome(SendOutcome::channel_relayed(), 5100);
    // The hold outranks the blank timer, so blanking can only happen after kEmgHoldMs — which is > kBlankMs.
    m.on_tick(snap(5100 + kEmgHoldMs - 1));
    CHECK(m.state().blanked == false);
    m.on_tick(snap(5100 + kEmgHoldMs + 1));
    CHECK(m.state().blanked == true);
    CHECK(m.emergency()     == Emergency::picked_up);          // state RETAINED behind the dark panel
    const uint32_t woke = 5100 + kEmgHoldMs + 2000;
    m.on_gesture(Gesture::short_press, snap(woke));
    CHECK(m.state().blanked == false);
    CHECK(m.emergency()     == Emergency::picked_up);          // ★ CONSUMED: the outcome is now on screen, not dismissed
    present(m);                                                // ...and §B102: a frame of it actually completed
    m.on_gesture(Gesture::short_press, snap(woke + 1000));
    CHECK(m.emergency()     == Emergency::idle);               // the press AFTER it acknowledges
}

TEST_CASE("ui-model: B71 — `double` gets NO emergency job, and `long` still re-fires") {
    UiModel d = fired_with_outcome(SendOutcome::channel_relayed(), 5100);
    CHECK(d.state().screen == Screen::status);                 // where `activate()` does nothing
    d.on_gesture(Gesture::double_press, snap(6000));
    CHECK(d.emergency() == Emergency::picked_up);              // ⛔ double neither acknowledges nor re-fires

    UiModel l = fired_with_outcome(SendOutcome::channel_no_relay(), 5100);
    l.on_send_accepted(SendKind::emergency, 5200);
    l.on_outcome(SendOutcome::channel_no_relay(), 5300);
    l.on_send_accepted(SendKind::emergency, 5400);
    l.on_outcome(SendOutcome::channel_no_relay(), 5500);
    CHECK(l.emergency() == Emergency::not_heard);
    SendReq req{};
    l.on_gesture(Gesture::long_arm,  snap(6000));
    l.on_gesture(Gesture::long_fire, snap(9600));
    CHECK(l.emergency() == Emergency::firing);                 // a sticky NOT HEARD is always re-fireable
    CHECK(l.attempts()  == 0);                                 // ...with a FRESH three-transmission budget
    const bool requeued = l.take_send_request(req);
    CHECK(requeued == true);
}

// ⚠ THE ORDERING THAT MATTERS: the exit is tested BEFORE the compose branch, so an acknowledging press acts on the
//   ALARM the user is looking at and never on the list underneath it.
// ★★ REWRITTEN 2026-08-05 by §B101/F5. This case used to PIN the opposite: `long_fire` left the compose sub-view open
//    underneath the overlay, and the exit had to out-rank it. That was the defect — an invisible canned message stayed
//    selected and armed, so the next `double` after the alarm was dismissed would have SENT it. Committing an alarm
//    now closes the modal and resets the cursor, which removes the collision instead of arbitrating it.
TEST_CASE("ui-model: B101 — committing an alarm CLOSES the compose modal and resets its cursor") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::short_press,  snap(1000));           // status -> team (passive)
    CHECK(m.state().screen == Screen::team);
    m.on_gesture(Gesture::double_press, snap(1050));           // §UI-17 S1: ENTER the list...
    m.on_gesture(Gesture::double_press, snap(1100));           // ...and open the DM compose sub-view
    CHECK(m.state().compose == Compose::dm);
    m.on_gesture(Gesture::short_press,  snap(1150));           // ...and move OFF the `back` row onto a real message
    CHECK(m.state().cursor == 1);
    m.on_gesture(Gesture::long_arm,  snap(1200));
    CHECK(m.state().compose == Compose::dm);                   // ARMING is cancellable, so it leaves the modal alone
    m.on_gesture(Gesture::long_fire, snap(4800));
    CHECK(m.state().compose == Compose::none);                 // ★ committing CLOSES it...
    CHECK(m.state().cursor  == 0);                             // ★ ...and disarms the selection
    const bool got = m.take_send_request(req);
    CHECK(got == true);
    CHECK(req.kind == SendKind::emergency);                    // ★ the alarm's own slot, not the modal's
    m.on_send_accepted(SendKind::emergency, 5000);
    m.on_outcome(SendOutcome::channel_relayed(), 5100);
    present(m);
    m.on_gesture(Gesture::short_press, snap(6000));
    CHECK(m.emergency()     == Emergency::idle);               // the press acted on the ALARM
    CHECK(m.state().compose == Compose::none);                 // ...and there is no armed modal left behind it
    // ★ THE HARM THAT IS NOW UNREACHABLE: a `double` after the dismissal must not send the message that was selected
    //   under the overlay. On the TEAM screen it re-opens the modal at the `back` row instead.
    SendReq stale{};
    m.on_gesture(Gesture::double_press, snap(6500));
    CHECK(m.state().compose == Compose::dm);
    CHECK(m.state().cursor  == 0);
    CHECK(m.take_send_request(stale) == false);
}

// ================================================ §B107 / QA finding F1 — a newer UI state LOST during a paged frame
// ★★★ THE DEFECT: the shipped tick cleared `dirty` when the LAST PAGE went out. A frame spans eight ticks, so an
// outcome or a gesture landing DURING those eight ticks set `dirty` — and the completing OLD frame then cleared it
// unconditionally. PICKED UP / REPLY / FAILED could be lost outright.
// ⚠⚠ ASSERT THE SIDE EFFECT, NOT THE STATE. `m.emergency()` is `picked_up` either way — the harm is A FRAME THAT
//    NEVER HAPPENS, so every case below asserts the PAINT SEQUENCE: does the next eligible pass return `open`?
//    A post-hoc enum assertion passes against this bug, which is exactly how it shipped green (§B97/§B98).

// Drive `n` page pushes of an already-open frame, answering `next_page()` as a real 8-page panel would.
static void page_out(FrameGate& g, UiModel& m, UiSnapshot& s, UiInboxCounters& c, uint8_t pages_left) {
    for (uint8_t i = 0; i < pages_left; ++i) {
        const FrameStep st = g.step(m, s, /*mac_idle=*/true);
        CHECK(st == FrameStep::next_page);
        g.on_page(/*more=*/(i + 1) < pages_left, m, c);
    }
}

TEST_CASE("ui-frame: F1 — an outcome arriving MID-FRAME is still painted after the frame completes") {
    FrameGate g; UiModel m; UiInboxCounters c{}; UiSnapshot s = snap(10000);
    // The alarm is live and awaiting evidence, and a frame for it opens.
    m.on_gesture(Gesture::long_arm, s);
    s.now_ms = 13500; m.on_gesture(Gesture::long_fire, s);
    SendReq req{}; const bool got = m.take_send_request(req); CHECK(got == true);
    m.on_send_accepted(SendKind::emergency, 13500);
    const FrameStep first = g.step(m, s, true);
    CHECK(first == FrameStep::open);
    CHECK(m.state().dirty == false);          // ★ consumed AT THE FREEZE, not eight ticks later
    g.on_page(true, m, c);   // page 0 of 8 is out; seven to go

    // ...and NOW, mid-frame, the answer arrives. This is the moment the shipped code threw away.
    m.on_outcome(SendOutcome::channel_relayed(), 13600);
    CHECK(m.emergency()    == Emergency::picked_up);
    CHECK(m.state().dirty  == true);
    page_out(g, m, s, c, /*pages_left=*/7);      // the OLD frame — still SENDING... — finishes paging out
    CHECK(g.frame_open() == false);

    // ★★ THE ASSERTION THAT DISCRIMINATES: the very next eligible pass must OPEN a frame for PICKED UP.
    //    Against the shipped code this is `idle` — the alarm's answer is never painted at all.
    s.now_ms = 13700;                         // still inside the 500 ms throttle, but an emergency bypasses it
    CHECK(g.step(m, s, true) == FrameStep::open);
}

TEST_CASE("ui-frame: F1 — a mid-frame GESTURE is not swallowed by the completing frame") {
    FrameGate g; UiModel m; UiInboxCounters c{}; UiSnapshot s = snap(10000);
    CHECK(g.step(m, s, true) == FrameStep::open);     // the boot frame (the model starts dirty)
    g.on_page(true, m, c);
    m.on_gesture(Gesture::short_press, s);            // the user advances to TEAM while page 1 of 8 is going out
    CHECK(m.state().screen == Screen::team);
    CHECK(m.state().dirty  == true);
    page_out(g, m, s, c, 7);
    s.now_ms = 10600;                                 // past the 2 Hz throttle
    CHECK(g.step(m, s, true) == FrameStep::open);     // ← `idle` against the shipped code: TEAM is never drawn
}

TEST_CASE("ui-frame: F1 — the ARMING countdown does not swallow digits across a frame") {
    FrameGate g; UiModel m; UiInboxCounters c{}; UiSnapshot s = snap(10000);
    m.on_gesture(Gesture::long_arm, s);
    CHECK(m.emergency() == Emergency::arming);
    CHECK(g.step(m, s, true) == FrameStep::open);
    g.on_page(true, m, c);
    s.now_ms = 11000;  m.on_tick(s);                  // the visible digit ticks over mid-frame
    CHECK(m.state().dirty == true);
    page_out(g, m, s, c, 7);
    CHECK(g.step(m, s, true) == FrameStep::open);     // ← `idle` against the shipped code: the digit is lost
}

TEST_CASE("ui-frame: F1 — a BLANK does not discard an invalidation raised while the panel is dark") {
    FrameGate g; UiModel m; UiInboxCounters c{}; UiSnapshot s = snap(10000);
    CHECK(g.step(m, s, true) == FrameStep::open);
    g.on_page(false, m, c);                                  // a complete frame; nothing pending
    m.on_tick(s);                                      // §B65: the FIRST tick only SEEDS the blank timer
    s.now_ms = 10000 + kBlankMs; m.on_tick(s);
    CHECK(m.state().blanked == true);
    CHECK(g.step(m, s, true) == FrameStep::blank);
    CHECK(g.frame_open()    == false);                 // the open page loop is abandoned with the panel
    // ⓘ HONEST SCOPE: `blanked` is only ever cleared by a gesture, and BOTH writers also set `dirty` — so no wake can
    //   observe a discarded invalidation today. This pins the property, not a reproduced harm. See §B107.
    CHECK(m.state().dirty == true);                    // ← false against the shipped code
}

// The gate's OTHER three rules, which §B104 recorded as having no behavioural probe at all.
TEST_CASE("ui-frame: the MAC-idle gate never starts OR continues a paint mid-exchange") {
    FrameGate g; UiModel m; UiInboxCounters c{}; UiSnapshot s = snap(10000);
    CHECK(g.step(m, s, /*mac_idle=*/false) == FrameStep::mac_busy);
    CHECK(m.state().dirty == true);                    // ...and a refused pass consumes nothing
    CHECK(g.step(m, s, true) == FrameStep::open);
    g.on_page(true, m, c);
    CHECK(g.step(m, s, false) == FrameStep::mac_busy); // an OPEN frame is held, not advanced
    CHECK(g.frame_open()      == true);
    CHECK(g.step(m, s, true)  == FrameStep::next_page);
}

TEST_CASE("ui-frame: the 2 Hz throttle holds a clean repaint, and an emergency bypasses it") {
    FrameGate g; UiModel m; UiInboxCounters c{}; UiSnapshot s = snap(10000);
    CHECK(g.step(m, s, true) == FrameStep::open);
    g.on_page(false, m, c);
    CHECK(g.step(m, s, true) == FrameStep::idle);      // nothing dirty
    m.on_gesture(Gesture::short_press, s);             // dirty again, but only 0 ms since the last paint
    CHECK(g.step(m, s, true) == FrameStep::idle);
    s.now_ms = 10000 + kPaintThrottleMs - 1;
    CHECK(g.step(m, s, true) == FrameStep::idle);
    s.now_ms = 10000 + kPaintThrottleMs;
    CHECK(g.step(m, s, true) == FrameStep::open);
    g.on_page(false, m, c);
    // The emergency bypass: dirty again, well inside the throttle.
    s.now_ms += 10; m.on_gesture(Gesture::long_arm, s);
    CHECK(m.emergency()      == Emergency::arming);
    CHECK(g.step(m, s, true) == FrameStep::open);
}

TEST_CASE("ui-frame: a frame spans exactly the pages the panel reports, and only then reopens") {
    FrameGate g; UiModel m; UiInboxCounters c{}; UiSnapshot s = snap(10000);
    CHECK(g.step(m, s, true) == FrameStep::open);
    g.on_page(true, m, c);
    for (uint8_t i = 0; i < 7; ++i) {
        CHECK(g.step(m, s, true) == FrameStep::next_page);
        CHECK(g.frame_open()     == true);
        g.on_page(i < 6, m, c);
    }
    CHECK(g.frame_open()     == false);
    CHECK(g.step(m, s, true) == FrameStep::idle);      // clean and complete: nothing more to do
}

// ================================ §R2 (OWNER-RULED 2026-08-05) — A DOUBLE UNDER THE EMERGENCY OVERLAY IS ABSORBED
// ★★★ THE HAZARD, and it is a HIDDEN MIS-SEND DURING AN ALARM. While the overlay is up it OWNS the body (`draw_frame`
// returns straight after `draw_emergency`), but `on_gesture` let a `double` fall through to `activate()` /
// `compose_gesture()`. So TWO doubles could open a compose view the user cannot see and then SEND from it — and with a
// modal left open under ARMING (which §B101 deliberately does not close, because arming is cancellable) ONE was enough.
// ⇒ OWNER RULING: the overlay ABSORBS the double. NO emergency action (consistent with §B71's "double gets no
//   emergency job"), NO operation of the screen underneath, NO dismiss and NO re-fire. The complete gesture contract
//   under the overlay: SHORT = §B71's exit once §B102's latch says the result was presented · LONG = re-fire ·
//   DOUBLE = nothing.
// ⚠⚠ IT IS ITS OWN ARM, NOT A REUSE OF §B102/F3's PRESENTED-LATCH. F3 consumes a PREMATURE SHORT press; folding the
//    double into that arm would make `double` inherit the latch and DISMISS a presented outcome — which §B71
//    withdrew. The last case in this block is the one that distinguishes them, and it fails against exactly that fold.
// ⚠ ASSERT THE SIDE EFFECT — the message that is or is not QUEUED — never the `compose` enum alone: the shipped code
//   CLOSES the modal on its way out, so a bare post-hoc `compose == none` is green against the very defect this names.
// ⓘ Why the existing §B71 `double` case could not have caught any of this: it says so itself —
//   `CHECK(d.state().screen == Screen::status); // where activate() does nothing`. These cases sit on TEAM instead.

// A fired alarm carried to `o`, with the screen left on TEAM — the screen where `double` really does compose.
static UiModel on_team_with_outcome(const SendOutcome& o, uint32_t at_ms) {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::short_press, snap(1000));             // status -> team (passive)
    CHECK(m.state().screen == Screen::team);
    // ★★ §UI-17 S1: the list is ENTERED before the alarm, deliberately — `double` only composes from an entered
    //    list, so without this prefix the R2 cases below would be green against a missing overlay guard.
    m.on_gesture(Gesture::double_press, snap(1050));
    m.on_gesture(Gesture::long_arm,  snap(1100));
    m.on_gesture(Gesture::long_fire, snap(4700));
    const bool got = m.take_send_request(req);                  // ⚠ DRAINS (§B70) — one call, into a local
    CHECK(got == true);
    m.on_send_accepted(SendKind::emergency, 5000);
    m.on_outcome(o, at_ms);
    return m;
}

TEST_CASE("ui-model: R2 — two DOUBLES under the overlay cannot open and then SEND an invisible compose view") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::short_press, snap(1000));             // status -> team (passive)
    m.on_gesture(Gesture::double_press, snap(1050));            // §UI-17 S1: ENTER the list — see `on_team_with_outcome`
    CHECK(m.state().screen == Screen::team);
    m.on_gesture(Gesture::long_arm,  snap(1100));
    m.on_gesture(Gesture::long_fire, snap(4700));
    CHECK(m.emergency()     == Emergency::firing);              // SENDING... owns the body from here on
    CHECK(m.state().compose == Compose::none);                  // §B101 closed the modal when the alarm committed
    const bool alarm = m.take_send_request(req);
    CHECK(alarm == true);
    CHECK(req.kind == SendKind::emergency);

    m.on_gesture(Gesture::double_press, snap(4800));
    CHECK(m.state().compose == Compose::none);                  // ← `dm` against the shipped code: an INVISIBLE modal
    m.on_gesture(Gesture::double_press, snap(4900));
    // ★★ THE ASSERTION THAT DISCRIMINATES: nothing was QUEUED. `compose` is `none` after the second double either way,
    //    because the shipped path closes the modal as it sends — so only the queued request separates the two.
    SendReq mis{};
    const bool queued = m.take_send_request(mis);
    CHECK(queued == false);                                     // ← true, a real DM, against the shipped code
    CHECK(m.emergency()    == Emergency::firing);               // ...and no emergency job either (§B71)
    CHECK(m.state().screen == Screen::team);                    // ...and the screen underneath never moved
}

TEST_CASE("ui-model: R2 — a DOUBLE cannot SEND from a compose modal left open under ARMING") {
    UiModel m;
    m.on_gesture(Gesture::short_press,  snap(1000));            // status -> team (passive)
    m.on_gesture(Gesture::double_press, snap(1050));            // §UI-17 S1: ENTER the list...
    m.on_gesture(Gesture::double_press, snap(1100));            // ...and open the DM compose modal
    CHECK(m.state().compose == Compose::dm);
    CHECK(m.state().cursor  == 0);
    // ★ `long_arm` deliberately leaves the modal alone (§B101: arming is cancellable), so the modal is LIVE and
    //   INVISIBLE beneath the RELEASE! overlay. That is the state in which ONE double was enough.
    m.on_gesture(Gesture::long_arm, snap(1200));
    CHECK(m.emergency()     == Emergency::arming);
    CHECK(m.state().compose == Compose::dm);
    m.on_gesture(Gesture::double_press, snap(1300));
    SendReq mis{};
    const bool queued = m.take_send_request(mis);
    CHECK(queued == false);                                     // ← true, a real DM, against the shipped code
    CHECK(m.emergency()     == Emergency::arming);              // no emergency job
    CHECK(m.state().compose == Compose::dm);                    // ← `none` against the shipped code: it SENT and closed
}

TEST_CASE("ui-model: R2 — the overlay absorbs a DOUBLE in every non-idle emergency state") {
    // BLOCKED — in flight, waiting on its retry deadline.
    UiModel b = on_team_with_outcome(SendOutcome::blocked(5000), 5100);
    CHECK(b.emergency() == Emergency::blocked);
    b.on_gesture(Gesture::double_press, snap(5200));
    CHECK(b.state().compose == Compose::none);                  // ← `dm` against the shipped code
    CHECK(b.emergency()     == Emergency::blocked);

    // FAILED — terminal (§B78 holds it on the panel, §B71 gives it a SHORT exit). Even PRESENTED, a double does nothing.
    UiModel f = on_team_with_outcome(SendOutcome::channel_failed(FailReason::unsealable), 5100);
    CHECK(f.emergency() == Emergency::failed);
    present(f);
    f.on_gesture(Gesture::double_press, snap(5200));
    CHECK(f.state().compose == Compose::none);                  // ← `dm` against the shipped code
    CHECK(f.emergency()     == Emergency::failed);              // ...and a double never dismisses (§B71)

    // CANCELLED — the brief toast is still an overlay that owns the body.
    UiModel x;
    x.on_gesture(Gesture::short_press, snap(1000));
    x.on_gesture(Gesture::long_arm,    snap(1100));
    x.on_gesture(Gesture::long_cancel, snap(1200));
    CHECK(x.emergency() == Emergency::cancelled);
    x.on_gesture(Gesture::double_press, snap(1300));
    CHECK(x.state().compose == Compose::none);                  // ← `dm` against the shipped code
    CHECK(x.emergency()     == Emergency::cancelled);
}

// ★★★ THE ANTI-FOLD CONTROL the ruling explicitly asked for. It runs BOTH gestures against BOTH states of §B102's
// presented-latch, so the two arms are separated by measurement rather than by argument.
// ⓘ It is green against the SHIPPED tree by construction on its `picked_up`+`double` rows — it is a CONTROL, not one
//   of the RED cases. What it fails against is the wrong FIX: fold R2 into F3's latched arm and the last-but-one row
//   flips to `idle`.
TEST_CASE("ui-frame: R2 vs F3 — the presented-latch gates SHORT only; a DOUBLE is absorbed either way") {
    FrameGate g; UiInboxCounters c{};
    UiModel m = on_team_with_outcome(SendOutcome::channel_relayed(), 5100);
    UiSnapshot s = snap(5200);
    CHECK(m.emergency() == Emergency::picked_up);
    // UNPRESENTED: F3 consumes the short press, R2 absorbs the double. Same visible result, different mechanisms.
    m.on_gesture(Gesture::double_press, s);
    CHECK(m.emergency()     == Emergency::picked_up);
    CHECK(m.state().compose == Compose::none);                  // ← `dm` against the shipped code
    m.on_gesture(Gesture::short_press, s);
    CHECK(m.emergency()     == Emergency::picked_up);           // §B102: not yet SEEN, so no exit
    // ...and now a whole frame of PICKED UP actually reaches the panel.
    CHECK(g.step(m, s, true) == FrameStep::open);
    g.on_page(true, m, c);
    page_out(g, m, s, c, /*pages_left=*/7);
    CHECK(m.emg_outcome_retained() == true);                    // the latch is OPEN
    s.now_ms = 6000;
    // ★★ THE DISTINCTION: with the latch open, SHORT exits — DOUBLE still does nothing at all.
    m.on_gesture(Gesture::double_press, s);
    CHECK(m.emergency()     == Emergency::picked_up);           // ← `idle` if R2 were folded into F3's latched arm
    CHECK(m.state().compose == Compose::none);                  // ← `dm` against the shipped code
    m.on_gesture(Gesture::short_press, s);
    CHECK(m.emergency()     == Emergency::idle);                // §B71's ruled exit is UNCHANGED by R2
    CHECK(m.state().screen  == Screen::team);                   // the acknowledging press is spent on the alarm
}

// ============================================================================================== UI-7 — B69's CARRIER
// ★★★ B69: `channel_no_relay` and `channel_remote_mint` share `Emergency::not_heard`, and they are DIFFERENT CLAIMS.
// `NOT HEARD`'s detail line — "no relay after N" — asserts a MEASUREMENT: we transmitted and overheard nothing. An
// alarm whose attempts all came back `ctr == 0` never held a handle and never listened, so it never took that
// measurement. The evidence flag is what lets the renderer stop making the claim.
// ⚠ AND ITS PREMISE IS CORRECTED HERE, MEASURED: B69 rules the kind must render as **SENT**, on the strength of
//   B39's producer (3) — a registered mobile's delegated GLOBAL post, a genuine success. That producer is
//   STRUCTURALLY DEAD on the `-t` line this UI sends (`node.cpp:1401` makes `want_global` false whenever `-t` is set
//   without `-g`, so `do_send_channel_delegated` is never entered). ⇒ "SENT" would be a FALSE CONFIRMATION, and the
//   honest rendering is UNCONFIRMED. See firmware_ui_model.h's EmgEvidence block.

static UiModel fired_and_taken() {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::long_arm, snap(1000));
    m.on_gesture(Gesture::long_fire, snap(4500));
    const bool got = m.take_send_request(req); CHECK(got == true);
    m.on_send_accepted(SendKind::emergency, 5000);
    return m;
}

TEST_CASE("ui7-B69: an alarm whose every attempt had NO LOCAL HANDLE reports `no_handle`, not a measurement") {
    UiModel m = fired_and_taken();
    CHECK(m.emg_evidence() == EmgEvidence::none);
    m.on_outcome(SendOutcome::channel_remote_mint(), 5100);
    CHECK(m.emg_evidence() == EmgEvidence::no_handle);
    CHECK(m.emergency() == Emergency::firing);                 // unconfirmed ⇒ bounded retry, exactly as before
}

TEST_CASE("ui7-B69: ONE locally-heard transmission makes the measurement true, and it is STICKY") {
    UiModel m = fired_and_taken();
    m.on_outcome(SendOutcome::channel_no_relay(), 5100);       // we held the handle and it came back
    CHECK(m.emg_evidence() == EmgEvidence::local_tx);
    // a later handle-less attempt must NOT erase it — "we listened and heard nothing" stays true for this alarm
    SendReq req{}; const bool got = m.take_send_request(req); CHECK(got == true); if (!got) return;
    m.on_send_accepted(SendKind::emergency, 6000);
    m.on_outcome(SendOutcome::channel_remote_mint(), 6100);
    CHECK(m.emg_evidence() == EmgEvidence::local_tx);          // ★ monotone
}

TEST_CASE("ui7-B69: a NEW alarm starts with NO evidence — it does not inherit the last one's") {
    UiModel m = fired_and_taken();
    m.on_outcome(SendOutcome::channel_no_relay(), 5100);
    CHECK(m.emg_evidence() == EmgEvidence::local_tx);
    m.on_gesture(Gesture::long_fire, snap(9000));               // re-fire: a NEW alarm, new budget
    CHECK(m.attempts() == 0);
    CHECK(m.emg_evidence() == EmgEvidence::none);               // ★ the budget and the evidence reset together
}

// ★★★ THE §2.1 CONTROL. An outcome that may not MOVE the alarm may not DESCRIBE it either — otherwise a coincident
//     channel post relabels a distress result it had no part in. The evidence is written PAST the live-alarm guard.
// ⚠⚠ THIS CASE WAS VACUOUS ON ITS FIRST WRITING AND ONLY A MUTATION CAUGHT IT — the same class as §R1's control, one
//    slice later. v1 settled the alarm on PICKED UP first, which sets `local_tx`; the STICKY rule (`remote_mint`
//    writes only while the evidence is still `none`) then masked the wrong placement completely, so moving the write
//    ahead of the guard failed NOTHING. ⇒ every arm below starts from `EmgEvidence::none`, which is the only state
//    in which a misplaced write is observable at all. An unrelated rule neutralising a wrong fix is an ACCIDENT OF
//    RULE ORDER, never a safety property.
TEST_CASE("ui7-B69: an outcome that cannot move the alarm cannot relabel its evidence either") {
    // arm 1 — NO ALARM AT ALL. The strongest form: a channel post arriving on an idle model.
    UiModel idle_m;
    CHECK(idle_m.emg_evidence() == EmgEvidence::none);
    idle_m.on_outcome(SendOutcome::channel_remote_mint(), 5200);
    CHECK(idle_m.emergency() == Emergency::idle);
    CHECK(idle_m.emg_evidence() == EmgEvidence::none);          // ★ nothing to describe, so nothing is described
    idle_m.on_outcome(SendOutcome::channel_no_relay(), 5300);
    CHECK(idle_m.emg_evidence() == EmgEvidence::none);
    // arm 2 — a SETTLED alarm that never got any evidence: a synchronous refusal lands `failed` with evidence `none`,
    // so a later unrelated post is the exact scenario the guard exists for and the sticky rule cannot hide it.
    UiModel m = fired_and_taken();
    m.on_send_refused(SendKind::emergency, RefuseReason::other, MESHROUTE_NS::CmdCode::err_no_binding, 5100);
    CHECK(m.emergency() == Emergency::failed);
    CHECK(m.emg_evidence() == EmgEvidence::none);
    m.on_outcome(SendOutcome::channel_remote_mint(), 5200);     // an unrelated post, after the alarm settled
    CHECK(m.emergency() == Emergency::failed);                  // the state is untouched (the shipped guard)...
    CHECK(m.emg_evidence() == EmgEvidence::none);               // ★ ...and so is the evidence
    // arm 3 — the sticky rule is still what the other case measures; assert it does NOT double as this guard.
    UiModel picked = fired_and_taken();
    picked.on_outcome(SendOutcome::channel_relayed(), 5100);
    CHECK(picked.emergency() == Emergency::picked_up);
    picked.on_outcome(SendOutcome::channel_remote_mint(), 5200);
    CHECK(picked.emg_evidence() == EmgEvidence::local_tx);
}

TEST_CASE("ui7-B69: three handle-less attempts end in NOT HEARD carrying `no_handle`") {
    UiModel m = fired_and_taken();
    m.on_outcome(SendOutcome::channel_remote_mint(), 5100);
    SendReq req{};
    for (int i = 2; i <= 3; ++i) {
        const bool got = m.take_send_request(req); CHECK(got == true); if (!got) return;
        m.on_send_accepted(SendKind::emergency, 5000u + 1000u * uint32_t(i));
        m.on_outcome(SendOutcome::channel_remote_mint(), 5100u + 1000u * uint32_t(i));
    }
    CHECK(m.attempts() == 3);
    CHECK(m.emergency() == Emergency::not_heard);
    CHECK(m.emg_evidence() == EmgEvidence::no_handle);          // ★ so the panel says "unconfirmed", not "no relay"
    CHECK(m.take_send_request(req) == false);                   // still bounded — B69 changes no airtime behaviour
}

// ---- the canned-channel outcome machine (§B69's other half) -----------------------------------------------------

TEST_CASE("ui7-chan: a refused canned post is TERMINAL, not a permanent SENDING...") {
    UiModel m; SendReq req{};
    for (int i = 0; i < 3; ++i) m.on_gesture(Gesture::short_press, snap(1000));   // STATUS -> TEAM -> INBOX -> SEND
    m.on_gesture(Gesture::double_press, snap(1000));
    CHECK(m.state().compose == Compose::channel);
    m.on_gesture(Gesture::double_press, snap(1100));
    const bool got = m.take_send_request(req); CHECK(got == true); if (!got) return;
    CHECK(m.chan_state() == ChanState::submitting);
    m.on_send_refused(SendKind::channel_canned, RefuseReason::other, MESHROUTE_NS::CmdCode::err_no_binding, 1200);
    CHECK(m.chan_state() == ChanState::failed);                 // ★ the arm that did not exist before UI-7
    CHECK(m.refuse_code() == MESHROUTE_NS::CmdCode::err_no_binding);
    CHECK(m.emergency() == Emergency::idle);                    // and a canned refusal never fabricates an alarm
    CHECK(m.dm_state() == DmState::idle);                       // ...nor touches the DM machine
}

TEST_CASE("ui7-chan: a DM outcome offered to the canned entry point is REFUSED, not absorbed") {
    UiModel m;
    m.on_channel_outcome(SendOutcome::dm_acked(), 1000);
    CHECK(m.chan_state() == ChanState::idle);                   // `_dm` has exactly one writer set
    CHECK(m.dm_state() == DmState::idle);
}

// ============================================================================================== UI-7 — THE RESULT PHASE
// ★★ Spec §3.2.1/§3.4.1: the OUTCOME replaces the canned list IN the sub-view. UI-2 closed the modal as it sent, so
//    `DELIVERED to <label>` / `NO KEY` / `NO CONFIRM` had no renderer at all.

static UiModel dm_sent() {
    UiModel m; SendReq req{};
    to_team(m, snap(1000));                                     // -> TEAM, and ENTER the list (§UI-17 S1)
    m.on_gesture(Gesture::double_press, snap(1000));            // -> DM compose for team[0]
    m.on_gesture(Gesture::double_press, snap(1000));            // -> send "Are you OK?"
    const bool got = m.take_send_request(req); CHECK(got == true);
    return m;
}

TEST_CASE("ui7-result: a send switches the sub-view to its RESULT phase instead of closing it") {
    UiModel m = dm_sent();
    CHECK(m.state().compose == Compose::dm);
    CHECK(m.state().compose_result == true);
    CHECK(m.state().compose_peer == snap().team[0].id);         // still bound to who it went to, for the label
    CHECK(m.dm_state() == DmState::submitting);
}

// ★★★ THE MIS-SEND CONTROL FOR THE NEW PHASE, and it asserts the QUEUED REQUEST rather than the enum (§B110's
//     method): if the result phase fell through to the list logic, a second `double` would SEND AGAIN — from a view
//     showing an outcome, with a cursor the user never moved.
TEST_CASE("ui7-result: NEITHER gesture can queue a second send from the result phase") {
    UiModel m = dm_sent(); SendReq req{};
    m.on_gesture(Gesture::double_press, snap(1100));
    CHECK(m.take_send_request(req) == false);                   // ★ nothing was queued
    UiModel m2 = dm_sent(); SendReq req2{};
    m2.on_gesture(Gesture::short_press, snap(1100));
    CHECK(m2.take_send_request(req2) == false);                 // ★ nor by the other gesture
}

TEST_CASE("ui7-result: either press acknowledges and returns to the PARENT screen") {
    UiModel m = dm_sent();
    m.on_gesture(Gesture::double_press, snap(1100));
    CHECK(m.state().compose == Compose::none);
    CHECK(m.state().compose_result == false);
    CHECK(m.state().screen == Screen::team);                    // the parent, not the cycle start
    UiModel m2 = dm_sent();
    m2.on_gesture(Gesture::short_press, snap(1100));
    CHECK(m2.state().compose == Compose::none);                 // §3.2's "at the end of a list, move on" — the
    CHECK(m2.state().compose_result == false);                  // result phase has no list, so every position is the end
}

// ★★ Without clearing the flag, a modal re-opened later would render an OUTCOME view against a stale result — the
//    user would see the previous message's verdict over a list they have not sent yet.
// ★★★ REWRITTEN IN PLACE BY §UI-17 S2. ⛔ WHAT IT USED TO PIN, as *"the kBlankMs auto-exit clears the result phase,
//     so a re-opened modal shows its LIST"*: the TIMEOUT as the thing that cleared `compose_result`. §9 R-1 deleted
//     it — the RESULT phase now rides the blank with the rest of the sub-view, exactly as spec S2 pin 1 requires.
//     ★ The property this case exists for is UNCHANGED and is still what it ends on: whatever clears the modal must
//     clear the phase with it, or a re-opened compose renders an outcome view against a stale result. What differs
//     is only WHICH exit does it — the operator's acknowledgement, not a timer.
TEST_CASE("ui7-result: the result phase RIDES the blank, and the acknowledgement is what clears it") {
    UiModel m = dm_sent();
    m.on_tick(snap(1000 + kBlankMs + 1));
    CHECK(m.state().blanked == true);
    CHECK(m.state().compose == Compose::dm);                     // ★ the sub-view is retained...
    CHECK(m.state().compose_result == true);                     // ★ ...in its RESULT phase, with the verdict on it
    // The waking press is CONSUMED (spec :378), so the operator sees the outcome they walked away from...
    m.on_gesture(Gesture::double_press, snap(2000 + kBlankMs));
    CHECK(m.state().blanked == false);
    CHECK(m.state().compose_result == true);
    // ...and the NEXT press is the acknowledgement that retires it (§9 R-5: either press acknowledges a result).
    m.on_gesture(Gesture::double_press, snap(2100 + kBlankMs));
    CHECK(m.state().compose == Compose::none);
    CHECK(m.state().compose_result == false);
    CHECK(m.state().screen == Screen::team);
    // ...so the modal re-opened after that shows its LIST, never the previous message's verdict.
    m.on_gesture(Gesture::double_press, snap(2200 + kBlankMs));
    CHECK(m.state().compose == Compose::dm);
    CHECK(m.state().compose_result == false);                    // ★ the LIST, not a stale outcome
    CHECK(m.state().cursor == 0);
}

TEST_CASE("ui7-result: §B101's alarm close-out clears the result phase too") {
    UiModel m = dm_sent();
    m.on_gesture(Gesture::long_fire, snap(1100));
    CHECK(m.state().compose == Compose::none);
    CHECK(m.state().compose_result == false);
    CHECK(m.emergency() == Emergency::firing);
}

// ============================================================================================== UI-7 — THE INBOX BUDGET
// ★★★ Spec §6.1's NAMED HAZARD, and it is the kind that reads as obviously-satisfied: `Inbox::pull()` visits the DM
//     block FIRST and the channel block SECOND, with NO limit parameter of any kind. So "keep the newest 8" over one
//     shared pool lets a chatty channel evict EVERY DM row — on the one screen whose purpose is showing both.

// ⓘ §UI-7D slice B: `bool dm` stays the TEST's parameter (it reads better at 20 call sites) but the ROW carries `kind`
//   — the row has exactly one kind field, and the budget branches on it.
static InboxRow row(bool dm, uint8_t ch, uint32_t age, uint32_t seq = 1) {
    InboxRow r{};
    r.kind = dm ? InboxKind::dm : InboxKind::channel;
    r.seq = seq; r.channel_id = ch; r.rx_age_s = age;
    return r;
}

TEST_CASE("ui7-inbox: a chatty channel CANNOT evict the DM rows — the budget is PER KIND") {
    InboxRowBudget b; UiSnapshot s{};
    for (uint8_t i = 0; i < 2; ++i) b.add(row(true, 0, i));       // 2 DMs arrive first, as pull() delivers them
    for (uint8_t i = 0; i < 20; ++i) b.add(row(false, 7, i));     // then a flood of channel traffic
    b.publish(s, 22);
    CHECK(b.dm_count() == 2);                                     // ★ both DMs survive the flood
    CHECK(b.ch_count() == kInboxRowsPerKind);
    CHECK(s.inbox_shown == uint8_t(2 + kInboxRowsPerKind));
    CHECK(s.inbox[0].kind == InboxKind::dm);                      // block order: DM rows first
    CHECK(s.inbox[1].kind == InboxKind::dm);
    CHECK(s.inbox[2].kind == InboxKind::channel);
}

// ★★ RETENTION, AND IT IS UNCHANGED BY [[B231]]: which rows SURVIVE is `add()`'s newest-wins eviction; only the order
//    they are PUBLISHED in moved. `rx_age_s` carries the arrival counter here, so the surviving SET is asserted
//    independently of where each row landed — a case that only read `inbox[0]` could not tell the two apart.
TEST_CASE("ui7-inbox: within a kind the NEWEST rows win, because pull() hands them oldest-first") {
    InboxRowBudget b; UiSnapshot s{};
    for (uint8_t i = 0; i < uint8_t(kInboxRowsPerKind + 3); ++i) b.add(row(true, 0, i));
    b.publish(s, uint16_t(kInboxRowsPerKind + 3));
    CHECK(s.inbox_shown == kInboxRowsPerKind);
    // the three oldest (0, 1, 2) were displaced; 3 .. kInboxRowsPerKind+2 survive, newest at the TOP
    CHECK(s.inbox[0].rx_age_s == uint32_t(kInboxRowsPerKind + 2));
    CHECK(s.inbox[kInboxRowsPerKind - 1].rx_age_s == 3u);
    for (uint8_t i = 0; i < kInboxRowsPerKind; ++i)
        CHECK(s.inbox[i].rx_age_s == uint32_t(kInboxRowsPerKind + 2 - i));
}

// ★★★★ [[B231]] — OWNER RULED 2026-08-20: THE NEWEST MESSAGE IS AT THE TOP. The store hands `pull()`'s rows
//      oldest-first, so this is the ONE place the two orders differ — and the row the operator's cursor starts on is
//      the one this decides.
// ⛔ AND THE BLOCK ORDER IS ASSERTED IN THE SAME CASE, deliberately: the ruling is newest-first WITHIN a block, and
//    the spec §6.1 block order (all DM rows, then all channel rows) is a SEPARATE decision that this may not disturb.
//    A case that checked only the DM rows would pass on an implementation that had started interleaving.
TEST_CASE("ui7-inbox B231: within each block the NEWEST row is at the TOP, and the BLOCK order is untouched") {
    InboxRowBudget b; UiSnapshot s{};
    for (uint32_t i = 1; i <= 3; ++i) b.add(row(true,  0, i, i));   // pull()'s order: the DM block, oldest-first...
    for (uint32_t i = 1; i <= 3; ++i) b.add(row(false, 7, i, i));   // ...then the channel block, oldest-first
    b.publish(s, 6);
    CHECK(s.inbox_shown == 6);
    CHECK(s.inbox[0].kind == InboxKind::dm);       CHECK(s.inbox[0].seq == 3u);   // ★ the NEWEST DM is row 0
    CHECK(s.inbox[1].kind == InboxKind::dm);       CHECK(s.inbox[1].seq == 2u);
    CHECK(s.inbox[2].kind == InboxKind::dm);       CHECK(s.inbox[2].seq == 1u);   // ...the oldest DM is LAST of its block
    CHECK(s.inbox[3].kind == InboxKind::channel);  CHECK(s.inbox[3].seq == 3u);   // ⛔ still a BLOCK, never interleaved
    CHECK(s.inbox[4].kind == InboxKind::channel);  CHECK(s.inbox[4].seq == 2u);
    CHECK(s.inbox[5].kind == InboxKind::channel);  CHECK(s.inbox[5].seq == 1u);
}

// ★ A block with ONE row and an EMPTY block: the reversed loops must handle both, and an empty ring must not publish a
//   zeroed row. ⓘ `_n_ch == 0` is where a reversed `for (i = _n_ch; i > 0; --i)` written as an underflowing
//   `for (i = _n_ch - 1; i >= 0; --i)` on a `uint8_t` would run 256 times — stated because that is the tempting form.
TEST_CASE("ui7-inbox B231: one row, and an empty block, publish exactly what they hold") {
    InboxRowBudget b; UiSnapshot s{};
    b.add(row(true, 0, 9, 9));
    b.publish(s, 1);
    CHECK(s.inbox_shown == 1);
    CHECK(s.inbox[0].kind == InboxKind::dm); CHECK(s.inbox[0].seq == 9u);
    CHECK(b.ch_count() == 0);
    InboxRowBudget b2; UiSnapshot s2{};
    b2.add(row(false, 7, 5, 5));
    b2.publish(s2, 1);
    CHECK(s2.inbox_shown == 1);
    CHECK(s2.inbox[0].kind == InboxKind::channel); CHECK(s2.inbox[0].seq == 5u);
}

TEST_CASE("ui7-inbox: truncation is VISIBLE — total is what pull visited, not what fitted") {
    InboxRowBudget b; UiSnapshot s{};
    for (uint8_t i = 0; i < 30; ++i) b.add(row(i % 2 == 0, 1, i));
    b.publish(s, 30);
    CHECK(s.inbox_shown == kMaxInboxRows);
    CHECK(s.inbox_total == 30);                                   // ★ the cap is never presented as the mailbox
}

TEST_CASE("ui7-inbox: reset() really empties it, so a frame cannot inherit the previous pull's rows") {
    InboxRowBudget b; UiSnapshot s{};
    for (uint8_t i = 0; i < 6; ++i) b.add(row(true, 0, i));
    b.reset();
    b.publish(s, 0);
    CHECK(b.dm_count() == 0); CHECK(b.ch_count() == 0);
    CHECK(s.inbox_shown == 0); CHECK(s.inbox_total == 0);
}

// ★ §B66's durable closure, asserted rather than assumed: the counts are DERIVED from the tables, so they cannot
//   drift. The `back` row is the LAST row of each, and it is one row, not two (spec §3.2.2).
TEST_CASE("ui7-B66: the canned counts are derived from the tables and `back` is the last row of each") {
    CHECK(kDmTextCount == uint8_t(sizeof kDmTexts / sizeof kDmTexts[0]));
    CHECK(kChannelTextCount == uint8_t(sizeof kChannelTexts / sizeof kChannelTexts[0]));
    CHECK(kDmSendableTexts == uint8_t(kDmTextCount - 1));
    CHECK(kChannelSendableTexts == uint8_t(kChannelTextCount - 1));
    CHECK(std::strcmp(kDmTexts[kDmTextCount - 1], "back, don't send") == 0);
    CHECK(std::strcmp(kChannelTexts[kChannelTextCount - 1], "back, don't send") == 0);
}

// ================================================================================ §UI-7D slice B — INBOX DETAIL/DELETE
// ★★★★ SPEC §3.5. The requirement most of this block exists for is IDENTITY: selection identity is the PAIR
//     `(InboxKind, seq)` — never the visible row index, origin, message counter or body — because the DM and channel
//     sequence spaces are INDEPENDENT and `Inbox::pull()` hands records oldest-first while the per-kind budget keeps the
//     NEWEST, so ONE arriving message renumbers every retained row. [[B133]] was this exact pair at another site.
// ⚠ A case that only drives a STATIC list proves nothing about that, so the identity cases below all MOVE the rows.
// ⚠ [[B134]]: on the panel's own board (ESP32) the inbox is a volatile RAM ring. Nothing here asserts, or may assert,
//   survival across a power cycle — `erased` means the tombstone was appended within this runtime.

// ⓘ `to_inbox` MOVED to the top of this file with §UI-17 S1 — the early cases need the same prefix now, and a
//   second copy here would be the parallel helper U1 forbids. See it for what the prefix is.
// The device half, in three lines, exactly as `firmware_ui.cpp` performs it: drain the request, look the record up by
// the PAIR, and answer. Returns false if no open request was raised at all.
static bool open_detail(UiModel& m, const UiSnapshot& s, const uint8_t* body, uint8_t len, uint8_t origin = 48) {
    m.on_gesture(Gesture::double_press, s);
    InboxReq rq{};
    if (!m.take_inbox_request(rq) || rq.what != InboxWhat::open) return false;
    m.on_inbox_opened(rq.kind, rq.seq, origin, rq.kind == InboxKind::dm ? 0 : 7, body, len, s.now_ms);
    return true;
}
// Drain a delete request and answer it with `r`. Returns false if the model raised no erase request.
static bool answer_erase(UiModel& m, InboxEraseResult r, uint32_t* out_seq = nullptr, InboxKind* out_kind = nullptr) {
    InboxReq rq{};
    if (!m.take_inbox_request(rq) || rq.what != InboxWhat::erase) return false;
    if (out_seq)  *out_seq  = rq.seq;
    if (out_kind) *out_kind = rq.kind;
    m.on_inbox_erased(rq.kind, rq.seq, r);
    return true;
}
static const uint8_t kBody7[] = { 'h', 'e', 'l', 'l', 'o', '!', '?' };

// ---------------------------------------------------------------------------------------------------------- IDENTITY

TEST_CASE("ui7d-identity: a refresh that MOVES the rows keeps the highlight on the same (kind, seq)") {
    UiModel m; auto s = snap_inbox(4);
    to_inbox(m, s);
    m.on_gesture(Gesture::short_press, s);                       // cursor 1 -> the CHANNEL row, seq 2
    CHECK(m.state().cursor == 1);
    // A new message arrives: `pull()` is oldest-first and the budget keeps the newest, so every row shifts DOWN one.
    UiSnapshot s2 = s;
    for (uint8_t i = 0; i + 1 < 4; ++i) s2.inbox[i] = s.inbox[i + 1];
    s2.inbox[3].kind = InboxKind::channel; s2.inbox[3].seq = 99; s2.inbox[3].rx_age_s = 0;
    m.on_tick(s2);
    CHECK(m.state().cursor == 0);                                // ★ the HIGHLIGHT FOLLOWED the record, not the index
    CHECK(m.state().inbox_pick_gone == false);
    // ...and activating now names that record, not whatever row 1 became.
    m.on_gesture(Gesture::double_press, s2);
    InboxReq rq{};
    const bool asked = m.take_inbox_request(rq);
    CHECK(asked == true);
    if (asked) { CHECK(rq.kind == InboxKind::channel); CHECK(rq.seq == 2u); }
}

TEST_CASE("ui7d-identity: a DM and a channel record SHARING a seq do not cross-select") {
    UiModel m; UiSnapshot s = snap(1000);
    s.inbox_shown = 2; s.inbox_total = 2;
    s.inbox[0].kind = InboxKind::dm;      s.inbox[0].seq = 4;     // the SAME number in both stores
    s.inbox[1].kind = InboxKind::channel; s.inbox[1].seq = 4;
    to_inbox(m, s);
    CHECK(m.state().cursor == 0);                                // the DM with seq 4 is the pick
    // Now SWAP the two rows. ★ A `seq`-only match walks the list in order, finds the CHANNEL row first because it is now
    //   row 0, and comes to rest on it — the same number, the other store, one index away from a Delete.
    UiSnapshot s2 = s;
    s2.inbox[0] = s.inbox[1]; s2.inbox[1] = s.inbox[0];
    m.on_tick(s2);
    CHECK(m.state().cursor == 1);                                // ★ the DM's new index, not the first seq-4 hit
    CHECK(m.state().inbox_pick_gone == false);
    m.on_gesture(Gesture::double_press, s2);
    InboxReq rq{};
    const bool asked = m.take_inbox_request(rq);
    CHECK(asked == true);
    if (asked) { CHECK(rq.kind == InboxKind::dm); CHECK(rq.seq == 4u); }   // ⛔ never the channel row with the same seq
}

// ★ THE GESTURE PATH RE-ANCHORS TOO, and it has to: `FrameGate` freezes immediately after `on_tick`, but a press can
//   arrive against a snapshot the cursor has never been reconciled with. If only `on_tick` re-anchored, the panel would
//   show `>` beside one record while the activation opened another — §B64's harm arriving from the display side.
TEST_CASE("ui7d-identity: a double press with no tick in between still leaves the highlight on what it opened") {
    UiModel m; auto s = snap_inbox(3);
    to_inbox(m, s);
    m.on_gesture(Gesture::short_press, s);                       // cursor 1 -> channel seq 2
    UiSnapshot s2 = s;                                           // two arrivals shift it to row 3 without any tick
    s2.inbox[0].kind = InboxKind::dm;      s2.inbox[0].seq = 7;
    s2.inbox[1].kind = InboxKind::dm;      s2.inbox[1].seq = 8;
    s2.inbox[2].kind = InboxKind::channel; s2.inbox[2].seq = 2;
    m.on_gesture(Gesture::double_press, s2);
    CHECK(m.state().cursor == 2);
    InboxReq rq{};
    const bool asked = m.take_inbox_request(rq);
    CHECK(asked == true);
    if (asked) { CHECK(rq.kind == InboxKind::channel); CHECK(rq.seq == 2u); }
}

TEST_CASE("ui7d-identity: a VANISHED record refuses activation with MESSAGE GONE and touches no other row") {
    UiModel m; auto s = snap_inbox(3);
    to_inbox(m, s);
    m.on_gesture(Gesture::short_press, s);                       // cursor 1 -> channel seq 2
    // seq 2 is evicted; the survivors keep their identities and slide up, and a newer record joins the tail.
    UiSnapshot s2 = snap(1000);
    s2.inbox_shown = 3; s2.inbox_total = 3;
    s2.inbox[0].kind = InboxKind::dm;      s2.inbox[0].seq = 1;
    s2.inbox[1].kind = InboxKind::dm;      s2.inbox[1].seq = 3;
    s2.inbox[2].kind = InboxKind::channel; s2.inbox[2].seq = 5;
    m.on_tick(s2);
    CHECK(m.state().inbox_pick_gone == true);                    // ★ announced, EDGE-triggered
    InboxReq rq{};
    m.on_gesture(Gesture::double_press, s2);
    CHECK(m.take_inbox_request(rq) == false);                    // ⛔ NOTHING is requested — no open, no delete
    CHECK(m.state().detail == InboxModal::closed);
    CHECK(m.state().inbox_pick_gone == true);
    // the OTHER rows are untouched: the next press walks the list and re-picks normally
    m.on_gesture(Gesture::short_press, s2);                      // cursor 1 -> 2
    CHECK(m.state().screen == Screen::inbox);
    CHECK(m.state().cursor == 2);
    CHECK(m.state().inbox_pick_gone == false);
    m.on_gesture(Gesture::double_press, s2);
    const bool asked = m.take_inbox_request(rq);
    CHECK(asked == true);
    if (asked) { CHECK(rq.kind == InboxKind::channel); CHECK(rq.seq == 5u); }
}

TEST_CASE("ui7d-identity: the loss is announced ONCE — a second tick does not re-dirty the frame") {
    UiModel m; auto s = snap_inbox(2);
    to_inbox(m, s);
    UiSnapshot s2 = snap(1000); s2.inbox_shown = 0;              // everything evicted
    m.on_tick(s2);
    CHECK(m.state().inbox_pick_gone == true);
    m.clear_dirty();
    m.on_tick(s2);
    CHECK(m.state().dirty == false);                             // ★ edge-triggered, not per-tick
}

TEST_CASE("ui7d-identity: a row with seq 0 is NOT selectable — an unidentifiable row is refused, never guessed at") {
    UiModel m; UiSnapshot s = snap(1000);
    s.inbox_shown = 1; s.inbox_total = 1;
    s.inbox[0].kind = InboxKind::dm; s.inbox[0].seq = 0;         // inbox.h: seq 0 is the "before everything" cursor
    to_inbox(m, s);
    InboxReq rq{};
    m.on_gesture(Gesture::double_press, s);
    CHECK(m.take_inbox_request(rq) == false);
    CHECK(m.state().detail == InboxModal::closed);
    CHECK(m.state().inbox_pick_gone == true);                    // refused LOUDLY, by the one refusal path
}

TEST_CASE("ui7d-identity: a CROSSED open answer opens nothing") {
    UiModel m; auto s = snap_inbox(2);
    to_inbox(m, s);
    m.on_gesture(Gesture::double_press, s);                      // asks for (dm, 1)
    InboxReq rq{};
    CHECK(m.take_inbox_request(rq) == true);
    m.on_inbox_opened(InboxKind::channel, rq.seq, 9, 7, kBody7, sizeof kBody7, 1000);   // right seq, WRONG kind
    CHECK(m.state().detail == InboxModal::closed);
    m.on_inbox_opened(rq.kind, 77, 9, 0, kBody7, sizeof kBody7, 1000);                  // right kind, WRONG seq
    CHECK(m.state().detail == InboxModal::closed);
    m.on_inbox_opened(rq.kind, rq.seq, 9, 0, kBody7, sizeof kBody7, 1000);              // the PAIR -> opens
    CHECK(m.state().detail == InboxModal::body);
}

TEST_CASE("ui7d-identity: the ERASE target is the modal's own record, even after the rows move underneath it") {
    UiModel m; auto s = snap_inbox(3);
    to_inbox(m, s);
    m.on_gesture(Gesture::short_press, s);                       // cursor 1 -> channel seq 2
    CHECK(open_detail(m, s, kBody7, sizeof kBody7) == true);
    CHECK(m.state().detail_kind == InboxKind::channel);
    CHECK(m.state().detail_seq == 2u);
    // The list is rebuilt under the open modal and every row moves.
    UiSnapshot s2 = snap(2000);
    s2.inbox_shown = 3; s2.inbox_total = 3;
    s2.inbox[0].kind = InboxKind::dm;      s2.inbox[0].seq = 3;
    s2.inbox[1].kind = InboxKind::dm;      s2.inbox[1].seq = 5;
    s2.inbox[2].kind = InboxKind::channel; s2.inbox[2].seq = 2;
    m.on_tick(s2);
    m.on_gesture(Gesture::short_press, s2);                      // toggle to `delete`
    CHECK(m.state().detail_action == InboxAction::del);
    m.on_gesture(Gesture::double_press, s2);
    uint32_t seq = 0; InboxKind kind = InboxKind::dm;
    CHECK(answer_erase(m, InboxEraseResult::erased, &seq, &kind) == true);
    CHECK(kind == InboxKind::channel);                           // ★ the record we READ, not the row under the cursor
    CHECK(seq == 2u);
}

// ------------------------------------------------------------------------------------------- THE MODAL AND ITS PAGING

// ★★★★ §CHROME-4 / design §7.3 — RE-DERIVED FOR THE 19-COLUMN BODY, AND THE POINT IS THAT ONLY ONE NUMBER MOVED.
//      The rail takes `x = 0..9`, so the ordinary body is 116 px = 19 small-font columns. `kDetailCols` is that 19;
//      `kDetailPageChars` and `kDetailMaxPages` are computed FROM it and are asserted here as CONSEQUENCES.
//   ⛔ THE PAGE COUNT IS RE-DERIVED, NEVER RE-CLAMPED: 19 x 2 = 38 characters a page, and `ceil(241 / 38)` = 7 pages
//      for the largest body — one MORE page than the six a 42-character page needed. A renderer that had moved only
//      the draw origin would still have reported six, i.e. a pagination that lies about a body the panel cannot show.
TEST_CASE("ui7d-modal: the geometry is DERIVED — 38 chars a page, seven pages for the largest body, 2 s a page") {
    CHECK(kDetailCols == 19);                                    // ★ the ONE edited number (was 21 before §CHROME-4)
    CHECK(kDetailBodyRows == 2);
    CHECK(kDetailPageChars == 38);                               // 19 x 2, derived
    CHECK(kDetailMaxPages == 7);                                 // ceil(241 / 38), derived
    CHECK(kDetailPageMs == 2000u);
    // ★ THE PAGINATION MUST NOT LIE: the pages the modal offers must between them cover the largest storable body.
    //   At 19 columns a stale `6` fails this outright (6 x 38 = 228 < 241), which is what makes the re-derivation
    //   measured rather than asserted.
    CHECK(uint16_t(kDetailMaxPages) * kDetailPageChars >= MESHROUTE_NS::protocol::inbox_max_body);
    // ...and it must not over-count either: one page fewer would already be short.
    CHECK(uint16_t(kDetailMaxPages - 1) * kDetailPageChars < MESHROUTE_NS::protocol::inbox_max_body);
}

// ★ §CHROME-4: every boundary re-derived at 38 characters a page. ⛔ The old boundaries (42/43, 84/85) are NOT
//   translated mechanically — they are recomputed, and 38/39 and 76/77 are where a body now gains a page. A 42-char
//   body is TWO pages at 19 columns and was ONE at 21, which is exactly the off-by-a-page a re-clamped count hides.
TEST_CASE("ui7d-modal: pages = max(1, ceil(body_len / 38)) at every boundary, and NEVER zero") {
    static uint8_t big[MESHROUTE_NS::protocol::inbox_max_body];
    for (uint16_t i = 0; i < sizeof big; ++i) big[i] = uint8_t('a' + (i % 26));
    struct { uint16_t len; uint8_t pages; } cases[] = {
        {0, 1}, {1, 1}, {38, 1}, {39, 2}, {42, 2}, {76, 2}, {77, 3},
        {MESHROUTE_NS::protocol::inbox_max_body, 7},
    };
    for (const auto& c : cases) {
        UiModel m; auto s = snap_inbox(1);
        to_inbox(m, s);
        CHECK(open_detail(m, s, big, uint8_t(c.len)) == true);
        CHECK(m.state().detail == InboxModal::body);
        CHECK(m.state().detail_pages == c.pages);
        CHECK(m.state().detail_page == 0);
        CHECK(m.detail_body_len() == c.len);
    }
}

TEST_CASE("ui7d-modal: an EMPTY body (body == nullptr) is one page of nothing, not a refusal") {
    UiModel m; auto s = snap_inbox(1);
    to_inbox(m, s);
    CHECK(open_detail(m, s, nullptr, 0) == true);                // an E2E-ack receipt carries no body at all
    CHECK(m.state().detail == InboxModal::body);
    CHECK(m.state().detail_pages == 1);
    CHECK(m.detail_body_len() == 0);
    CHECK(m.state().detail_line[0][0] == '\0');
    CHECK(m.state().detail_line[1][0] == '\0');
}

TEST_CASE("ui7d-modal: a non-null body with body_len 0 still yields no bytes (the LENGTH is the truth)") {
    UiModel m; auto s = snap_inbox(1);
    to_inbox(m, s);
    CHECK(open_detail(m, s, kBody7, 0) == true);                 // ⛔ strlen(kBody7) would have shown 7 bytes
    CHECK(m.detail_body_len() == 0);
    CHECK(m.state().detail_line[0][0] == '\0');
}

// ★★ §CHROME-4: 19-column rows, and the ROW BYTES are asserted rather than only the lengths — that is what makes a
//    renderer-only migration (draw origin moved, model still wrapping at 21) fail here instead of on the panel.
TEST_CASE("ui7d-modal: the body is WRAPPED into two 19-column rows without dropping a byte") {
    static uint8_t b[45];
    for (uint8_t i = 0; i < sizeof b; ++i) b[i] = uint8_t('A' + (i % 26));
    UiModel m; auto s = snap_inbox(1);
    to_inbox(m, s);
    CHECK(open_detail(m, s, b, sizeof b) == true);
    CHECK(m.state().detail_pages == 2);
    CHECK(std::strlen(m.state().detail_line[0]) == 19);
    CHECK(std::strlen(m.state().detail_line[1]) == 19);
    CHECK(std::strcmp(m.state().detail_line[0], "ABCDEFGHIJKLMNOPQRS") == 0);
    CHECK(std::strcmp(m.state().detail_line[1], "TUVWXYZABCDEFGHIJKL") == 0);
    // ⛔ NO ROW MAY EXCEED THE PANEL'S BODY. The buffer is `kDetailCols + 1`, so an over-wide wrap could not even be
    //    stored — but the row that MATTERS is the one the renderer draws, and this is where its width is fixed.
    for (uint8_t r = 0; r < kDetailBodyRows; ++r) CHECK(std::strlen(m.state().detail_line[r]) <= kDetailCols);
    // page 2 holds the remaining SEVEN bytes, and every one of the 45 has now been shown
    m.on_tick(snap_inbox(1, 1000 + kDetailPageMs));
    CHECK(m.state().detail_page == 1);
    CHECK(std::strcmp(m.state().detail_line[0], "MNOPQRS") == 0);
    CHECK(m.state().detail_line[1][0] == '\0');
}

TEST_CASE("ui7d-modal: unsupported display bytes are replaced VISIBLY — NUL, control and high-bit alike") {
    const uint8_t b[] = { 'o', 0x00, 'k', 0x07, 0x1f, 0x7f, 0x80, 0xff, 'z' };
    UiModel m; auto s = snap_inbox(1);
    to_inbox(m, s);
    CHECK(open_detail(m, s, b, sizeof b) == true);
    CHECK(m.detail_body_len() == sizeof b);                      // ★ the embedded NUL did NOT truncate the record
    CHECK(std::strcmp(m.state().detail_line[0], "o.k.....z") == 0);
    CHECK(ui_display_byte(0x20) == ' ');                         // the boundaries of the one policy
    CHECK(ui_display_byte(0x7e) == '~');
    CHECK(ui_display_byte(0x7f) == '.');
    CHECK(ui_display_byte(0x1f) == '.');
}

TEST_CASE("ui7d-modal: a long body advances every 2 s and CYCLES, marking dirty each time") {
    static uint8_t b[100];
    for (uint8_t i = 0; i < sizeof b; ++i) b[i] = 'x';
    UiModel m; auto s = snap_inbox(1);
    to_inbox(m, s);
    CHECK(open_detail(m, s, b, sizeof b) == true);
    CHECK(m.state().detail_pages == 3);
    m.clear_dirty();
    m.on_tick(snap_inbox(1, 1000 + kDetailPageMs - 1));
    CHECK(m.state().detail_page == 0);                           // not yet due
    CHECK(m.state().dirty == false);
    m.on_tick(snap_inbox(1, 1000 + kDetailPageMs));
    CHECK(m.state().detail_page == 1);
    CHECK(m.state().dirty == true);
    m.on_tick(snap_inbox(1, 1000 + 2 * kDetailPageMs));
    CHECK(m.state().detail_page == 2);
    m.on_tick(snap_inbox(1, 1000 + 3 * kDetailPageMs));
    CHECK(m.state().detail_page == 0);                           // ★ CYCLES
}

TEST_CASE("ui7d-modal: a ONE-page body never advances, so a short body cannot flicker") {
    UiModel m; auto s = snap_inbox(1);
    to_inbox(m, s);
    CHECK(open_detail(m, s, kBody7, sizeof kBody7) == true);
    CHECK(m.state().detail_pages == 1);
    m.clear_dirty();
    for (uint32_t k = 1; k <= 5; ++k) m.on_tick(snap_inbox(1, 1000 + k * kDetailPageMs));
    CHECK(m.state().detail_page == 0);
    CHECK(m.state().dirty == false);
}

// ★★★ THE CLAUSE THAT IS EASIEST TO GET WRONG: a page turn marks the model dirty but DOES NOT reset the user-inactivity
//     deadline (spec §3.5). If it did, a long body would hold the PANEL LIT for ever — and, through `ui_allows_sleep`,
//     stop the node light-sleeping at all.
// ★★★★ REWRITTEN IN PLACE BY §UI-17 S2 — SPEC S2 PIN 4. ⛔ WHAT IT USED TO PIN, as *"paging does NOT postpone the
//      inactivity timeout — the modal still closes at kBlankMs"*: the modal CLOSING at the deadline the paging failed
//      to postpone. §9 R-1 deleted that close. The clause under test is unchanged — the deadline is not postponed —
//      and it is now read off the BLANK, which is the deadline that still exists. ⇒ a long body cycles for exactly
//      one attention window and then the panel goes dark **with the modal retained**.
TEST_CASE("ui7d-modal: paging does NOT postpone the deadline — the panel blanks on time, modal RETAINED") {
    static uint8_t b[241];
    for (uint16_t i = 0; i < sizeof b; ++i) b[i] = 'y';
    UiModel m; auto s = snap_inbox(1);
    to_inbox(m, s);
    CHECK(open_detail(m, s, b, uint8_t(sizeof b)) == true);
    CHECK(m.state().detail_pages == 7);                          // §CHROME-4: 38 chars a page, so the largest body is 7
    uint32_t turns = 0;
    for (uint32_t k = 1; k * kDetailPageMs < kBlankMs; ++k) { m.on_tick(snap_inbox(1, 1000 + k * kDetailPageMs)); ++turns; }
    CHECK(turns == 7);                                           // ⛔ non-vacuity: it really did page across the window
    CHECK(m.state().detail == InboxModal::body);                 // still open just inside the window
    CHECK(m.state().blanked == false);
    m.on_tick(snap_inbox(1, 1000 + kBlankMs));
    CHECK(m.state().blanked == true);                            // ★ blanked on time, having paged seven times
    CHECK(m.state().detail == InboxModal::body);                 // ★ ...and the modal is RETAINED underneath the dark
    CHECK(m.state().screen == Screen::inbox);
}

// ★★★ REWRITTEN IN PLACE BY §UI-17 S2 — SPEC S2 PIN 2's storage half. ⛔ WHAT IT USED TO PIN, as *"the ordinary
//     timeout deletes NOTHING"*: that the deleted timeout closed an `armed-delete` modal without erasing. There is no
//     ordinary timeout (§9 R-1), so the stronger property is measured instead: the armed `delete` SURVIVES the blank
//     with the modal — and still nothing is asked of storage. ⓘ The safety half that made the timeout attractive is
//     paid by the EMERGENCY exception, not by a timer: `long_arm` closes this modal (see the S2 pin-3 case).
TEST_CASE("ui7d-modal: an armed `delete` survives the blank and STILL asks storage for nothing") {
    UiModel m; auto s = snap_inbox(1);
    to_inbox(m, s);
    CHECK(open_detail(m, s, kBody7, sizeof kBody7) == true);
    m.on_gesture(Gesture::short_press, s);                       // arm `delete`, then walk away
    CHECK(m.state().detail_action == InboxAction::del);
    m.on_tick(snap_inbox(1, 1000 + kBlankMs));
    CHECK(m.state().blanked == true);
    CHECK(m.state().detail == InboxModal::body);                 // ★ retained, exactly as the operator left it
    CHECK(m.state().detail_action == InboxAction::del);          // ★ ...including the armed action
    InboxReq rq{};
    CHECK(m.take_inbox_request(rq) == false);                    // ⛔ no erase was ever requested
}

// ------------------------------------------------------------------------------------------------------- THE GESTURES

TEST_CASE("ui7d-gesture: `back` is selected on entry, so deletion costs the deliberate short -> double") {
    UiModel m; auto s = snap_inbox(2);
    to_inbox(m, s);
    CHECK(open_detail(m, s, kBody7, sizeof kBody7) == true);
    CHECK(m.state().detail_action == InboxAction::back);         // ★ never `delete`
    m.on_gesture(Gesture::short_press, s);
    CHECK(m.state().detail_action == InboxAction::del);
    m.on_gesture(Gesture::short_press, s);
    CHECK(m.state().detail_action == InboxAction::back);         // toggles, both ways
}

TEST_CASE("ui7d-gesture: a `double` on `back` closes the modal and requests NOTHING of storage") {
    UiModel m; auto s = snap_inbox(2);
    to_inbox(m, s);
    CHECK(open_detail(m, s, kBody7, sizeof kBody7) == true);
    m.on_gesture(Gesture::double_press, s);                      // `back` is selected
    CHECK(m.state().detail == InboxModal::closed);
    CHECK(m.state().screen == Screen::inbox);
    InboxReq rq{};
    CHECK(m.take_inbox_request(rq) == false);                    // ⛔ nothing to serve = nothing can be deleted
    CHECK(m.detail_body_len() == 0);                             // the buffer is released with the modal
}

TEST_CASE("ui7d-gesture: an immediate `double` cannot delete — the FIRST double is what opened the modal") {
    UiModel m; auto s = snap_inbox(2);
    to_inbox(m, s);
    CHECK(open_detail(m, s, kBody7, sizeof kBody7) == true);
    m.on_gesture(Gesture::double_press, s);                      // the second double lands on `back`
    InboxReq rq{};
    CHECK(m.take_inbox_request(rq) == false);
}

// ------------------------------------------------------------------------------------------- THE THREE DELETE OUTCOMES

TEST_CASE("ui7d-delete: `erased` closes the modal and preserves the NEIGHBOURING selection") {
    UiModel m; auto s = snap_inbox(3);
    to_inbox(m, s);
    m.on_gesture(Gesture::short_press, s);                       // cursor 1 (channel seq 2); neighbour = row 2, dm seq 3
    CHECK(open_detail(m, s, kBody7, sizeof kBody7) == true);
    m.on_gesture(Gesture::short_press, s);
    m.on_gesture(Gesture::double_press, s);
    uint32_t seq = 0; CHECK(answer_erase(m, InboxEraseResult::erased, &seq) == true);
    CHECK(seq == 2u);
    CHECK(m.state().detail == InboxModal::closed);
    CHECK(m.state().inbox_pick_gone == false);
    // the store now returns two rows; the highlight lands on the neighbour, wherever it moved to
    UiSnapshot s2 = snap(1100);
    s2.inbox_shown = 2; s2.inbox_total = 2;
    s2.inbox[0].kind = InboxKind::dm; s2.inbox[0].seq = 1;
    s2.inbox[1].kind = InboxKind::dm; s2.inbox[1].seq = 3;
    m.on_tick(s2);
    CHECK(m.state().cursor == 1);
    m.on_gesture(Gesture::double_press, s2);
    InboxReq rq{};
    const bool asked = m.take_inbox_request(rq);
    CHECK(asked == true);
    if (asked) { CHECK(rq.kind == InboxKind::dm); CHECK(rq.seq == 3u); }
}

TEST_CASE("ui7d-delete: deleting the LAST row falls back to the row BEFORE it") {
    UiModel m; auto s = snap_inbox(3);
    to_inbox(m, s);
    m.on_gesture(Gesture::short_press, s); m.on_gesture(Gesture::short_press, s);   // cursor 2 = the last row, seq 3
    CHECK(m.state().cursor == 2);
    CHECK(open_detail(m, s, kBody7, sizeof kBody7) == true);
    m.on_gesture(Gesture::short_press, s);
    m.on_gesture(Gesture::double_press, s);
    CHECK(answer_erase(m, InboxEraseResult::erased) == true);
    UiSnapshot s2 = snap(1100);
    s2.inbox_shown = 2; s2.inbox_total = 2;
    s2.inbox[0].kind = InboxKind::dm;      s2.inbox[0].seq = 1;
    s2.inbox[1].kind = InboxKind::channel; s2.inbox[1].seq = 2;
    m.on_tick(s2);
    CHECK(m.state().cursor == 1);                                // the predecessor, seq 2
}

TEST_CASE("ui7d-delete: deleting the ONLY row leaves the cursor at home rather than past the end") {
    UiModel m; auto s = snap_inbox(1);
    to_inbox(m, s);
    CHECK(open_detail(m, s, kBody7, sizeof kBody7) == true);
    m.on_gesture(Gesture::short_press, s);
    m.on_gesture(Gesture::double_press, s);
    CHECK(answer_erase(m, InboxEraseResult::erased) == true);
    CHECK(m.state().cursor == 0);
    UiSnapshot s2 = snap(1100); s2.inbox_shown = 0;
    m.on_tick(s2);
    CHECK(m.state().inbox_pick_gone == false);                   // nothing was LOST — it was deleted on purpose
}

TEST_CASE("ui7d-delete: `not_found` is TERMINAL, has NO active Delete, and returns to the list on either press") {
    UiModel m; auto s = snap_inbox(2);
    to_inbox(m, s);
    CHECK(open_detail(m, s, kBody7, sizeof kBody7) == true);
    m.on_gesture(Gesture::short_press, s);
    m.on_gesture(Gesture::double_press, s);
    CHECK(answer_erase(m, InboxEraseResult::not_found) == true);
    CHECK(m.state().detail == InboxModal::gone);                 // ★ the modal STAYS, saying so
    CHECK(m.state().detail_del_failed == false);                 // ⛔ this is not a storage failure
    // ⛔ no action can be selected, so no second delete is one press away
    m.on_gesture(Gesture::short_press, s);
    CHECK(m.state().detail == InboxModal::closed);
    CHECK(m.state().screen == Screen::inbox);
    InboxReq rq{};
    CHECK(m.take_inbox_request(rq) == false);
    // ...and the DOUBLE half of "either press" behaves the same way
    UiModel m2; auto s2 = snap_inbox(2);
    to_inbox(m2, s2);
    CHECK(open_detail(m2, s2, kBody7, sizeof kBody7) == true);
    m2.on_gesture(Gesture::short_press, s2);
    m2.on_gesture(Gesture::double_press, s2);
    CHECK(answer_erase(m2, InboxEraseResult::not_found) == true);
    m2.on_gesture(Gesture::double_press, s2);
    CHECK(m2.state().detail == InboxModal::closed);
    CHECK(m2.take_inbox_request(rq) == false);
}

TEST_CASE("ui7d-delete: `io_error` STAYS in the modal, says DELETE FAILED, and resets the selection to `back`") {
    UiModel m; auto s = snap_inbox(2);
    to_inbox(m, s);
    CHECK(open_detail(m, s, kBody7, sizeof kBody7) == true);
    m.on_gesture(Gesture::short_press, s);
    m.on_gesture(Gesture::double_press, s);
    CHECK(answer_erase(m, InboxEraseResult::io_error) == true);
    CHECK(m.state().detail == InboxModal::body);                 // ⛔ NOT closed: nothing was deleted
    CHECK(m.state().detail_del_failed == true);
    CHECK(m.state().detail_action == InboxAction::back);         // ★ back on the SAFE action
    CHECK(m.state().detail_kind == InboxKind::dm);                // still the same record, still readable
    CHECK(m.state().detail_seq == 1u);
    CHECK(m.detail_body_len() == sizeof kBody7);
    // ⇒ a retry costs short -> double all over again
    m.on_gesture(Gesture::double_press, s);                      // this one lands on `back`
    CHECK(m.state().detail == InboxModal::closed);
    InboxReq rq{};
    CHECK(m.take_inbox_request(rq) == false);
}

TEST_CASE("ui7d-delete: after DELETE FAILED a fresh short -> double DOES retry, with the same identity") {
    UiModel m; auto s = snap_inbox(2);
    to_inbox(m, s);
    CHECK(open_detail(m, s, kBody7, sizeof kBody7) == true);
    m.on_gesture(Gesture::short_press, s);
    m.on_gesture(Gesture::double_press, s);
    CHECK(answer_erase(m, InboxEraseResult::io_error) == true);
    m.on_gesture(Gesture::short_press, s);
    m.on_gesture(Gesture::double_press, s);
    uint32_t seq = 0; InboxKind kind = InboxKind::channel;
    CHECK(answer_erase(m, InboxEraseResult::erased, &seq, &kind) == true);
    CHECK(kind == InboxKind::dm); CHECK(seq == 1u);
    CHECK(m.state().detail == InboxModal::closed);
}

TEST_CASE("ui7d-delete: a CROSSED erase answer changes nothing at all") {
    UiModel m; auto s = snap_inbox(2);
    to_inbox(m, s);
    CHECK(open_detail(m, s, kBody7, sizeof kBody7) == true);
    m.on_gesture(Gesture::short_press, s);
    m.on_gesture(Gesture::double_press, s);
    InboxReq rq{};
    CHECK(m.take_inbox_request(rq) == true);
    m.on_inbox_erased(InboxKind::channel, rq.seq, InboxEraseResult::erased);    // wrong kind
    CHECK(m.state().detail == InboxModal::body);
    m.on_inbox_erased(rq.kind, 42, InboxEraseResult::erased);                   // wrong seq
    CHECK(m.state().detail == InboxModal::body);
    m.on_inbox_erased(rq.kind, rq.seq, InboxEraseResult::erased);               // the PAIR
    CHECK(m.state().detail == InboxModal::closed);
}

TEST_CASE("ui7d-delete: an UNSOLICITED answer is ignored — no request, no effect") {
    UiModel m; auto s = snap_inbox(2);
    to_inbox(m, s);
    CHECK(open_detail(m, s, kBody7, sizeof kBody7) == true);
    m.on_inbox_erased(m.state().detail_kind, m.state().detail_seq, InboxEraseResult::erased);
    CHECK(m.state().detail == InboxModal::body);                 // nothing was asked for, so nothing may happen
    m.on_inbox_open_gone(m.state().detail_kind, m.state().detail_seq);
    CHECK(m.state().detail == InboxModal::body);
    CHECK(m.state().inbox_pick_gone == false);
}

// ★★ AN ANSWER MUST CORRESPOND TO AN OPERATION SOMEBODY ACTUALLY PERFORMED. Until the request has been HANDED OUT
//    nothing can have read the store, so an answer arriving first is not a measurement — it is a fabrication, and the
//    model refuses it. (The same guard is what stops a second answer to an already-answered request.)
TEST_CASE("ui7d-delete: an answer that arrives BEFORE the request was drained is refused") {
    UiModel m; auto s = snap_inbox(2);
    to_inbox(m, s);
    m.on_gesture(Gesture::double_press, s);                      // the request is raised but NOT yet taken
    m.on_inbox_opened(InboxKind::dm, 1, 9, 0, kBody7, sizeof kBody7, 1000);
    CHECK(m.state().detail == InboxModal::closed);               // ⛔ nothing performed it, so nothing may show it
    InboxReq rq{};
    CHECK(m.take_inbox_request(rq) == true);                     // ...and the request is still there to be served
    m.on_inbox_opened(rq.kind, rq.seq, 9, 0, kBody7, sizeof kBody7, 1000);
    CHECK(m.state().detail == InboxModal::body);
    m.on_inbox_opened(rq.kind, rq.seq, 9, 0, nullptr, 0, 1000);  // a SECOND answer to the same request is refused too
    CHECK(m.detail_body_len() == sizeof kBody7);
}

TEST_CASE("ui7d-delete: an activation whose record has vanished refuses at the ANSWER too") {
    UiModel m; auto s = snap_inbox(2);
    to_inbox(m, s);
    m.on_gesture(Gesture::double_press, s);
    InboxReq rq{};
    CHECK(m.take_inbox_request(rq) == true);
    m.on_inbox_open_gone(rq.kind, rq.seq);                       // the pull found nothing
    CHECK(m.state().detail == InboxModal::closed);               // ⛔ never opened
    CHECK(m.state().inbox_pick_gone == true);                    // ...and said so
}

// ============================================================= [[B233]] — THE WHOLE TICK, BECAUSE THE DEFECT IS THE ORDER
// ★★★★ WHY A TICK HARNESS AT ALL, and it is the reason the owner found this on metal and no case above could:
//      every case in this file drives the model with a HAND-BUILT snapshot, so none of them can see a defect that lives
//      in the ORDER `src/firmware_ui.cpp`'s `mr_ui_tick` calls them in — build the snapshot FIRST, gesture, tick, SERVE
//      THE ERASE MID-TICK (`:1643`, deliberately before the gate so the press is not a no-op for a whole frame), then
//      freeze. The frame that freezes therefore carries PRE-ERASE rows *and* consumes `dirty`, and on a clean model
//      `FrameGate::step` answers `idle` for ever after.
// ⚠ IT IS HAND-REPLICATED WIRING, and §B97's caution stands unchanged: this cannot fail for a MIS-WIRED `mr_ui_tick` —
//   only `tools/probe_firmware_ui` compiles that TU. What it measures is the model's contract UNDER that order, which
//   is exactly where the defect was.
// ⓘ ONE PAGE PER FRAME here, not eight: `on_page(false, ...)` completes the frame inside the tick. The page count is
//   `FrameGate`'s own property and is measured by its own cases; what these need is "a frame opened and froze THESE
//   rows".
struct InboxTick {
    UiModel         m;
    FrameGate       g;
    UiInboxCounters ctr{};
    // The store, in `Inbox::pull()`'s OWN order: the DM block first, then the channel block, each oldest-first
    // (inbox.h:106-109). ⛔ Not the panel's order — deriving the panel's order from this is the thing under test.
    InboxRow  store[2 * kInboxRowsPerKind]{};
    uint8_t   n_store = 0;
    uint32_t  ticks = 0, pulls = 0, frames = 0;   // ★ COUNTED, not argued — see the no-extra-pulls case
    UiSnapshot frozen{};                          // the frame the panel is actually showing right now
    bool      erase_fails = false;                // the store refuses: `io_error`, i.e. NOTHING was deleted

    void push(InboxKind k, uint32_t seq) {
        if (n_store >= uint8_t(sizeof store / sizeof store[0])) return;
        uint8_t at = n_store;                                     // a DM slots in ahead of the channel block
        if (k == InboxKind::dm)
            while (at > 0 && store[at - 1].kind == InboxKind::channel) { store[at] = store[at - 1]; --at; }
        store[at] = InboxRow{}; store[at].kind = k; store[at].seq = seq; store[at].rx_age_s = seq;
        ++n_store;
    }
    bool store_has(InboxKind k, uint32_t seq) const {
        for (uint8_t i = 0; i < n_store; ++i) if (store[i].kind == k && store[i].seq == seq) return true;
        return false;
    }
    InboxEraseResult erase(InboxKind k, uint32_t seq) {
        for (uint8_t i = 0; i < n_store; ++i) {
            if (store[i].kind != k || store[i].seq != seq) continue;
            for (uint8_t j = uint8_t(i + 1); j < n_store; ++j) store[j - 1] = store[j];
            --n_store;
            return InboxEraseResult::erased;
        }
        return InboxEraseResult::not_found;
    }
    // `build_snapshot` + `fill_inbox_rows`: ONE pull per tick, through the REAL `InboxRowBudget`.
    UiSnapshot build(uint32_t now_ms) {
        ++pulls;
        UiSnapshot s = snap(now_ms);
        InboxRowBudget b;
        for (uint8_t i = 0; i < n_store; ++i) b.add(store[i]);
        b.publish(s, n_store);
        return s;
    }
    // `ui_service_inbox_request`, all of it: the pull-by-pair, its refusal, and the erase.
    void serve(uint32_t now_ms) {
        InboxReq rq{};
        if (!m.take_inbox_request(rq)) return;
        if (rq.what == InboxWhat::open) {
            if (store_has(rq.kind, rq.seq))
                m.on_inbox_opened(rq.kind, rq.seq, 48, rq.kind == InboxKind::dm ? 0 : 7,
                                  kBody7, sizeof kBody7, now_ms);
            else m.on_inbox_open_gone(rq.kind, rq.seq);
        } else if (rq.what == InboxWhat::erase) {
            m.on_inbox_erased(rq.kind, rq.seq,
                              erase_fails ? InboxEraseResult::io_error : erase(rq.kind, rq.seq));
        }
    }
    // ONE `mr_ui_tick` pass, in `mr_ui_tick`'s own order.
    void tick(uint32_t now_ms, Gesture ges = Gesture::none) {
        ++ticks;
        const UiSnapshot s = build(now_ms);
        m.on_gesture(ges, s);
        m.on_tick(s);
        serve(now_ms);                                   // ★ MID-TICK, before the gate — firmware_ui.cpp:1643
        switch (g.step(m, s, /*mac_idle=*/true)) {
            case FrameStep::open:      frozen = s; ++frames; g.on_page(false, m, ctr); break;
            case FrameStep::next_page: g.on_page(false, m, ctr); break;
            default: break;                              // mac_busy / blank / idle draw nothing at all
        }
    }
    bool frozen_has(InboxKind k, uint32_t seq) const {
        for (uint8_t i = 0; i < frozen.inbox_shown; ++i)
            if (frozen.inbox[i].kind == k && frozen.inbox[i].seq == seq) return true;
        return false;
    }
};
// ⓘ 600 ms a tick, so `kPaintThrottleMs` never decides anything these cases are asking about: every dirty tick is free
//   to open its frame. A tighter step would make "no repaint came" ambiguous between the defect and the throttle.
static constexpr uint32_t kTickStep = 600;
// STATUS -> TEAM -> INBOX and the `double` that ENTERS the list, one press a tick, exactly as `to_inbox` does it
// without one (§UI-17 S1).
static uint32_t to_inbox_ticks(InboxTick& h, uint32_t t) {
    h.tick(t += kTickStep, Gesture::short_press);
    h.tick(t += kTickStep, Gesture::short_press);
    h.tick(t += kTickStep, Gesture::double_press);
    return t;
}

TEST_CASE("ui7d-B233: a serviced DELETE of a MIDDLE row repaints — the panel may not keep a record the store lost") {
    InboxTick h;
    for (uint32_t i = 1; i <= 3; ++i) h.push(InboxKind::dm, i);
    uint32_t t = to_inbox_ticks(h, 1000);
    h.tick(t += kTickStep, Gesture::short_press);                // cursor 1 = the MIDDLE row, seq 2 in EITHER order
    CHECK(h.m.state().cursor == 1);
    h.tick(t += kTickStep, Gesture::double_press);               // open it
    CHECK(h.m.state().detail == InboxModal::body);
    CHECK(h.m.state().detail_seq == 2u);
    h.tick(t += kTickStep, Gesture::short_press);                // arm `delete`
    CHECK(h.m.state().detail_action == InboxAction::del);
    const uint32_t frames_before = h.frames;
    h.tick(t += kTickStep, Gesture::double_press);               // confirm — the erase is SERVED inside this tick
    CHECK(h.store_has(InboxKind::dm, 2) == false);               // ★ the STORE is the authority, never the panel
    // ⓘ THIS frame legitimately still carries the deleted row: it froze the snapshot built at the top of the tick, and
    //   re-ordering the tick to avoid that is fix shape (b), which this fix deliberately did not take.
    CHECK(h.frozen_has(InboxKind::dm, 2) == true);
    // ★★ AND THIS IS THE HYPOTHESIS THE REGISTER ASKED TO BE CONFIRMED, asserted as the CAUSE rather than narrated:
    //    the delete leaves the cursor on a still-valid neighbour AT THE SAME INDEX, so on every later tick
    //    `sync_inbox_cursor` finds it where it already was, changes nothing, and marks nothing dirty. Nothing else in
    //    the model is watching the rows at all. (The LAST-row arm below is the same mechanism with the other answer.)
    CHECK(h.m.state().cursor == 1);
    CHECK(h.m.state().inbox_pick_gone == false);
    // ⇒ the NEXT frame must show the rebuilt list. ⛔ Before [[B233]] was fixed this frame NEVER CAME.
    for (int i = 0; i < 4; ++i) h.tick(t += kTickStep);
    CHECK(h.m.state().cursor == 1);                              // ★ still index 1, i.e. still nothing to notice
    CHECK(h.frozen_has(InboxKind::dm, 2) == false);
    CHECK(h.frozen.inbox_shown == 2);
    // ★ EXACTLY ONE further frame: the latch is ONE-SHOT. A latch that never cleared would repaint at tick rate for
    //   ever — the opposite defect, and just as invisible in a case that only asked "did it repaint at all".
    CHECK(h.frames == frames_before + 2);
    // ★★ COUNTED, NOT ARGUED (the brief's pin 6): the fix costs NO extra `Inbox::pull()`. One pull per tick, on the
    //    mutation tick like every other. Fix shape (b) — re-filling the rows after the drain — would read one more.
    CHECK(h.pulls == h.ticks);
}

// ★★ THE ARM THAT ALREADY WORKED, kept so the fix cannot regress it. The owner measured on metal that deleting the LAST
//    message refreshed correctly, and the mechanism above says why: the predecessor the cursor falls back to sits at a
//    DIFFERENT index afterwards, so `sync_inbox_cursor` moves the highlight and marks the frame dirty as a SIDE EFFECT.
// ⓘ The doomed record's `seq` is READ from the modal rather than assumed: which record is last depends on the publish
//   order, and this case is about the LAST ROW, not about a number.
TEST_CASE("ui7d-B233: deleting the LAST row still repaints (the arm that worked before the fix)") {
    InboxTick h;
    for (uint32_t i = 1; i <= 3; ++i) h.push(InboxKind::dm, i);
    uint32_t t = to_inbox_ticks(h, 1000);
    h.tick(t += kTickStep, Gesture::short_press);
    h.tick(t += kTickStep, Gesture::short_press);                // cursor 2 = the LAST row
    CHECK(h.m.state().cursor == 2);
    h.tick(t += kTickStep, Gesture::double_press);
    CHECK(h.m.state().detail == InboxModal::body);
    const uint32_t doomed = h.m.state().detail_seq;
    h.tick(t += kTickStep, Gesture::short_press);
    h.tick(t += kTickStep, Gesture::double_press);               // confirm
    CHECK(h.store_has(InboxKind::dm, doomed) == false);
    // ⓘ MEASURED: the cursor is still on the OLD index at the end of the erase tick — `sync_inbox_cursor` ran during
    //   `on_tick`, i.e. BEFORE the erase was served. The re-anchoring happens on the NEXT tick...
    CHECK(h.m.state().cursor == 2);
    h.tick(t += kTickStep);
    CHECK(h.m.state().cursor == 1);   // ★ ...where the fallback MOVED — and THAT is what re-dirtied this arm all along
    for (int i = 0; i < 3; ++i) h.tick(t += kTickStep);
    CHECK(h.frozen_has(InboxKind::dm, doomed) == false);
    CHECK(h.frozen.inbox_shown == 2);
    CHECK(h.pulls == h.ticks);
}

// ★★ THE LATCH'S SCOPE, from the other side: a delete that FAILED changed nothing in the store, so the rows the panel
//    is showing are still the truth and no repaint is owed. Raising the latch on every erase ANSWER is the tempting
//    simplification, and it would spend a frame saying nothing — on a device whose panel is its power budget.
TEST_CASE("ui7d-B233: a FAILED delete owes NO extra repaint — nothing was deleted, so nothing went stale") {
    InboxTick h; h.erase_fails = true;
    for (uint32_t i = 1; i <= 3; ++i) h.push(InboxKind::dm, i);
    uint32_t t = to_inbox_ticks(h, 1000);
    h.tick(t += kTickStep, Gesture::short_press);
    h.tick(t += kTickStep, Gesture::double_press);                // open
    h.tick(t += kTickStep, Gesture::short_press);                 // arm `delete`
    const uint32_t frames_before = h.frames;
    h.tick(t += kTickStep, Gesture::double_press);                // confirm -> the store refuses
    CHECK(h.m.state().detail == InboxModal::body);                // ⛔ NOT closed: nothing was deleted
    CHECK(h.m.state().detail_del_failed == true);
    CHECK(h.store_has(InboxKind::dm, 2) == true);
    for (int i = 0; i < 4; ++i) h.tick(t += kTickStep);
    CHECK(h.frames == frames_before + 1);                         // ★ the failure's OWN frame, and not one more
    CHECK(h.pulls == h.ticks);
}

// ★★★★ [[B231]]'s RIPPLE, AND IT IS THE HALF THAT COULD HAVE GONE WRONG SILENTLY — §B64's identity rule, now driven
//      from the OTHER DIRECTION. Under the old oldest-first order an arrival pushed the retained rows UP; newest-first
//      pushes them DOWN, and every row above the cursor moves. ⛔ The highlight must follow the RECORD, so the
//      arrival must not silently re-target the selection onto the message that has taken its index — which is one
//      press from an open and two from a Delete.
TEST_CASE("ui7d-B231: a message arriving ABOVE the cursor moves the highlight WITH the record, never onto the newcomer") {
    InboxTick h;
    for (uint32_t i = 1; i <= 3; ++i) h.push(InboxKind::dm, i);
    uint32_t t = to_inbox_ticks(h, 1000);
    h.tick(t += kTickStep, Gesture::short_press);                // cursor 1 = seq 2 (rows are 3, 2, 1)
    CHECK(h.m.state().cursor == 1);
    CHECK(h.frozen.inbox[1].seq == 2u);
    h.push(InboxKind::dm, 4);                                    // a newer DM lands at the TOP and shifts the rest down
    h.tick(t += kTickStep);
    CHECK(h.frozen.inbox[0].seq == 4u);                          // ★ the newcomer is row 0...
    CHECK(h.m.state().cursor == 2);                              // ★ ...and the highlight moved WITH seq 2, to row 2
    CHECK(h.m.state().inbox_pick_gone == false);
    // ...and the activation names the record the highlight is on, which is the whole point of tracking by identity.
    h.tick(t += kTickStep, Gesture::double_press);
    CHECK(h.m.state().detail == InboxModal::body);
    CHECK(h.m.state().detail_kind == InboxKind::dm);
    CHECK(h.m.state().detail_seq == 2u);                         // ⛔ never 4, the message that took its index
}

// ------------------------------------------------------------------------------------------- THE EMERGENCY INTERPLAY

TEST_CASE("ui7d-emergency: `long_arm` closes the modal BEFORE arming, and a long_cancel does not bring it back") {
    UiModel m; auto s = snap_inbox(2);
    to_inbox(m, s);
    CHECK(open_detail(m, s, kBody7, sizeof kBody7) == true);
    m.on_gesture(Gesture::short_press, s);
    CHECK(m.state().detail_action == InboxAction::del);           // Delete is selected and about to be hidden
    m.on_gesture(Gesture::long_arm, snap_inbox(2, 1100));
    CHECK(m.emergency() == Emergency::arming);
    CHECK(m.state().detail == InboxModal::closed);                // ★ closed at ARM, not at fire
    CHECK(m.state().detail_action == InboxAction::back);
    m.on_gesture(Gesture::long_cancel, snap_inbox(2, 1200));
    CHECK(m.emergency() == Emergency::cancelled);
    CHECK(m.state().detail == InboxModal::closed);                // ★★ the modal and its Delete do NOT reappear
    CHECK(m.state().detail_action == InboxAction::back);
    // and a double press while the cancelled overlay is up is absorbed entirely (ledger §1.4) — it cannot re-open
    m.on_gesture(Gesture::double_press, snap_inbox(2, 1250));
    CHECK(m.state().detail == InboxModal::closed);
    InboxReq rq{};
    CHECK(m.take_inbox_request(rq) == false);
}

TEST_CASE("ui7d-emergency: a re-opened modal after an alarm starts on `back`, never on the hidden Delete") {
    UiModel m; auto s = snap_inbox(2);
    to_inbox(m, s);
    CHECK(open_detail(m, s, kBody7, sizeof kBody7) == true);
    m.on_gesture(Gesture::short_press, s);
    m.on_gesture(Gesture::long_arm,  snap_inbox(2, 1100));
    m.on_gesture(Gesture::long_fire, snap_inbox(2, 4700));
    CHECK(m.emergency() == Emergency::firing);
    CHECK(m.state().detail == InboxModal::closed);
    SendReq sr{};
    CHECK(m.take_send_request(sr) == true);                      // the alarm is what happened, not a delete
    CHECK(sr.kind == SendKind::emergency);
    InboxReq rq{};
    CHECK(m.take_inbox_request(rq) == false);
}

TEST_CASE("ui7d-emergency: while the overlay is up a double cannot open the modal from INBOX") {
    UiModel m; auto s = snap_inbox(2);
    to_inbox(m, s);
    m.on_gesture(Gesture::long_arm, snap_inbox(2, 1100));
    m.on_gesture(Gesture::double_press, snap_inbox(2, 1200));
    CHECK(m.state().detail == InboxModal::closed);
    InboxReq rq{};
    CHECK(m.take_inbox_request(rq) == false);
}

// --------------------------------------------------------------------------------------- THE HEADER'S VISIBLE BYTES

TEST_CASE("ui7d-head: the modal's header line, asserted as BYTES") {
    char l[48];
    inbox_detail_head(l, sizeof l, InboxKind::dm, 48, 0, 0, 6, false);
    CHECK(std::strcmp(l, "DM from 48     1/6") == 0);
    inbox_detail_head(l, sizeof l, InboxKind::channel, 48, 7, 2, 6, false);
    CHECK(std::strcmp(l, "CH7 from 48    3/6") == 0);
    inbox_detail_head(l, sizeof l, InboxKind::channel, 255, 255, 5, 6, false);
    // §CHROME-4 / §7.3: the widest real expansion is 18 columns, which still fits the 19-column body the rail leaves.
    // ⓘ `pages` is at most `kDetailMaxPages` = 7, so both counters stay one digit and the line cannot grow.
    CHECK(std::strcmp(l, "CH255 from 255 6/6") == 0);
    CHECK(std::strlen(l) <= kDetailCols);
    // ★ the failure REPLACES the from-line and KEEPS the reader's place
    inbox_detail_head(l, sizeof l, InboxKind::dm, 48, 0, 1, 2, true);
    CHECK(std::strcmp(l, "DELETE FAILED 2/2") == 0);
    CHECK(std::strlen(l) <= kDetailCols);
}

// ------------------------------------------------------------------------- §F: THE FRAME GATE MUST NOT COUNT THE MODAL

TEST_CASE("ui7d-frame: a completed DETAIL frame does NOT clear the session unread counters") {
    UiModel m; FrameGate g; UiInboxCounters c;
    c.arr_dm = 4; c.arr_ch = 2;
    auto s = snap_inbox(2);
    c.publish(s);
    to_inbox(m, s);
    CHECK(open_detail(m, s, kBody7, sizeof kBody7) == true);
    auto s2 = snap_inbox(2, 9000); c.publish(s2);
    CHECK(g.step(m, s2, true) == FrameStep::open);
    for (int p = 0; p < 7; ++p) g.on_page(true, m, c);
    g.on_page(false, m, c);                                     // the frame COMPLETES
    CHECK(c.read_dm == 0u);                                     // ★ the panel showed the MODAL, not the list
    CHECK(c.read_ch == 0u);
    CHECK(c.unread_dm() == 4u);
    // ...and the very same gate does clear them once the LIST is what completed
    m.on_gesture(Gesture::double_press, snap_inbox(2, 9100));   // `back` -> the list
    CHECK(m.state().detail == InboxModal::closed);
    auto s3 = snap_inbox(2, 19000); c.publish(s3);
    CHECK(g.step(m, s3, true) == FrameStep::open);
    for (int p = 0; p < 7; ++p) g.on_page(true, m, c);
    g.on_page(false, m, c);
    CHECK(c.read_dm == 4u);
    CHECK(c.unread_dm() == 0u);
}

// ==================================================================================================================
// §UI-14 — THE SETTINGS SCREEN, THE DRAFT MARKER, AND THE SAVE / DISCARD / REBOOT STATES (spec §3.6.2, §3.6.1, §3.3)
// ==================================================================================================================
// ★★ WHAT THESE CASES MEASURE, AND WHAT THEY DELIBERATELY DO NOT. They drive the PURE model against a PURE service
//    over a fake store, so every gesture meaning, every row-list condition and every action's landing is machine-
//    checked. ⛔ THEY PROVE NOTHING ABOUT STORAGE: there is no NVS/LittleFS here, so no flash write, no wear and NO
//    reset-during-write behaviour is exercised — that is the DEVICE BINDING's half ([[B193]]) and it is a bench check.
//    §UI-13's formulation holds unchanged: a green suite says the LOGIC is right, never that the storage is.
//
// ⓘ THE FAKE BELOW IS NOT §UI-13's, AND THE DIFFERENCE IS THE MEASUREMENT, NOT AN OVERSIGHT (U1 considered and
//   answered): `test_firmware_config_service.cpp`'s `FakeCfgStore` is a WRITE COUNTER built to prove "zero writes" /
//   "exactly one write" / "live strictly after durable success" — properties of the SERVICE, already proven there.
//   What these cases need is a SCRIPTABLE record with two failure switches, so the PANEL can be driven into
//   `CFG! RELOAD`, `SAVE FAILED` and `NV READ FAILED`. Extracting one shared fixture into `test/support/` is the U1
//   move if a third consumer appears; it is not done here because it is a refactor and this slice is a feature (C1).
namespace {

struct UiFakeStore : mrfw::ICfgStore {
    mrnv::Blob rec{};
    bool can_load = true;      // false -> `load` reports no usable record (the ONLY producer of no_record/nv_failed)
    bool can_save = true;      // false -> the durable write FAILS and the record is untouched
    int  writes   = 0;
    UiFakeStore() {
        rec.magic = mrnv::kMagic; rec.version = mrnv::kVersion;
        rec.e2e_dm = 0; rec.intro_attach = 1; rec.mobile_autoregister = 0; rec.ble_mode = 0;
        rec.node_id = 42; rec.channel_ctr = 7;      // two NON-covered fields, so a save that dropped them is visible
    }
    bool load(mrnv::Blob& out) override { if (!can_load) return false; out = rec; return true; }
    bool save(const mrnv::Blob& b) override { ++writes; if (!can_save) return false; rec = b; return true; }
};
// The EFFECTIVE seam. `eff` starts equal to the persisted record (a freshly booted node) and `apply_live` MOVES it,
// so "the live state did not change" is a measurement rather than an absence.
struct UiFakeLive : mrfw::ICfgLive {
    mrfw::CfgValues eff{};
    int applies = 0;
    mrfw::CfgValues effective() const override { return eff; }
    void apply_live(const mrfw::CfgLiveFields& f) override {
        ++applies;
        eff.at(mrfw::CfgField::e2e_dm)              = f.e2e_dm ? 1 : 0;
        eff.at(mrfw::CfgField::intro_attach)        = f.intro_attach ? 1 : 0;
        eff.at(mrfw::CfgField::mobile_autoregister) = f.mobile_autoregister ? 1 : 0;
    }
};
struct CfgFix {
    UiFakeStore store;
    UiFakeLive  live;
    mrfw::ConfigService svc{store, live};
    UiModel     m;
    CfgFix() { live.eff = mrfw::cfg_values_from_blob(store.rec); m.attach_config(svc); }
};

// Walk to SETTINGS with real short presses, exactly as the operator would. ⚠ ASSERTED by the caller afterwards, never
// assumed: the walk is bounded, so a cycle change that made SETTINGS unreachable fails the caller's first check
// instead of looping.
void to_settings(UiModel& m, const UiSnapshot& s) {
    for (int i = 0; i < 40 && m.state().screen != Screen::settings; ++i) m.on_gesture(Gesture::short_press, s);
    // ⚠ AND THEN A TICK, because that is the shipped order (`mr_ui_tick`: on_gesture -> on_tick -> the freeze) and it
    //   is when `sync_settings` OPENS the service. Without it the baseline would be snapshotted later — at whatever
    //   moment the next tick happened — and a case that writes the fake store in between would be measuring an open
    //   that had not happened yet rather than the behaviour it meant to drive. (Observed exactly that, once.)
    m.on_tick(s);
}
// ★★★★ [[B232]] — WALK TO SETTINGS **AND OPEN THE MENU**, which is now a `double`. Every §UI-14/§UI-15 case below
//      was written against a screen that auto-entered `browsing` on arrival; the owner's ruling replaced that landing
//      with the CLOSED single-entry view, so what changed for those cases is exactly ONE PRESS OF PREFIX and nothing
//      else. ⇒ they keep their subject, and the landing itself is asserted by the `b232-` cases rather than here.
// ⚠ ASSERTED by the caller afterwards, never assumed — same rule as `to_settings`'s own walk.
// ⚠ THE `double` IS CONDITIONAL, and that is what makes this the exact analogue of the landing it replaces: a caller
//   that is ALREADY on SETTINGS with the menu up walked zero presses before and must walk zero now — an
//   unconditional `double` there would ACTIVATE the highlighted row instead of opening anything.
void to_settings_menu(UiModel& m, const UiSnapshot& s) {
    to_settings(m, s);
    if (m.state().settings == Settings::closed) m.on_gesture(Gesture::double_press, s);
    m.on_tick(s);
}
// The row the cursor is CURRENTLY on, by identity — the same question the renderer asks.
// ⚠ THE RAW `cursor` IS ONLY MEANINGFUL AFTER A SYNC, and that is a property of the design rather than of this
//   helper: `sync_settings` re-anchors the highlight onto the remembered ROW, and it runs at the top of every gesture
//   AND in `on_tick` — which is where the frame freezes (`mr_ui_tick`: on_gesture -> on_tick -> FrameGate::step). So
//   the panel never shows an un-synced index, and a test that reads one is reading a state no frame can contain.
bool row_under_cursor(UiModel& m, const UiSnapshot& s, CfgRow& out) {
    return m.settings_row_list(s).at(m.state().cursor, out);
}
// Put the cursor on a named row by pressing `short` until the HIGHLIGHTED ROW IS THAT ROW. ⛔ Never by walking to a
// hardcoded index: two rows are conditional, so an index means different things in different arms — the exact
// coupling §B66 exists to warn about, and §UI-14's own RELOAD row makes it live. It wraps through the cycle the way
// the operator would, and it is BOUNDED so a missing row fails the caller's check instead of looping.
// ★★ [[B232]]: THE MENU HAS TO BE ENTERED, and the walk can now land BACK on the closed view (the walk off the last
//    row), so this presses `double` there — exactly what the operator does. ⛔ The `browsing` term is NOT belt-and-
//    braces: `settings_row_list(s).at(0, …)` answers a row on the closed view too, so without it a `cursor_to` for
//    whatever row 0 happens to be would return true from a view that shows no rows at all.
bool cursor_to(UiModel& m, const UiSnapshot& s, CfgRow want) {
    for (int i = 0; i < 60; ++i) {
        CfgRow r{};
        m.on_tick(s);          // ...which is why this ticks first — see `row_under_cursor`
        if (m.state().screen == Screen::settings && m.state().settings == Settings::closed) {
            m.on_gesture(Gesture::double_press, s); continue;
        }
        if (m.state().screen == Screen::settings && m.state().settings == Settings::browsing &&
            row_under_cursor(m, s, r) && r == want) return true;
        m.on_gesture(Gesture::short_press, s);
    }
    return false;
}
UiSnapshot cfg_snap(uint32_t now_ms = 1000, bool ble_row = false) {
    UiSnapshot s = snap(now_ms); s.ble_row = ble_row;
    // ★ §UI-15 slice 5: BOTH child predicates ON, which is the build every §UI-14 case above was written against —
    //   the PROVISION row was UNCONDITIONAL until the owner's 2026-08-19 ruling made it follow its children. Setting
    //   them here PRESERVES those cases' subject; the ruling's own arms are driven explicitly (`ui15-parent`).
    s.prov_create_team = true; s.prov_join_static = true;
    return s;
}

}  // namespace

// ---------------------------------------------------------------------------------------------- the row list
TEST_CASE("ui14-rows: the menu is §3.6.2's, and the BLE row is ABSENT when the transport is not compiled") {
    const CfgRowList l = settings_rows(/*ble_row=*/false, /*conflict=*/false, /*provision=*/true);
    CHECK(l.n == 7);
    CfgRow r{};
    CHECK(l.at(0, r)); CHECK(r == CfgRow::e2e_dm);
    CHECK(l.at(1, r)); CHECK(r == CfgRow::intro_attach);
    CHECK(l.at(2, r)); CHECK(r == CfgRow::mobile_autoregister);
    CHECK(l.at(3, r)); CHECK(r == CfgRow::provision);
    CHECK(l.at(4, r)); CHECK(r == CfgRow::save);
    CHECK(l.at(5, r)); CHECK(r == CfgRow::discard);
    CHECK(l.at(6, r)); CHECK(r == CfgRow::back);
    // ⛔ FAILS CLOSED: an out-of-range index names NO row, so the caller does nothing rather than acting on a
    //    plausible one — and `discard` is one row from the end.
    CHECK(l.at(7, r) == false);
    CHECK(l.at(255, r) == false);
}

TEST_CASE("ui14-rows: the BLE row is PRESENT when the transport's condition is met, and it comes first") {
    const CfgRowList l = settings_rows(/*ble_row=*/true, /*conflict=*/false, /*provision=*/true);
    CHECK(l.n == 8);
    CfgRow r{};
    CHECK(l.at(0, r)); CHECK(r == CfgRow::ble_mode);
    CHECK(l.at(1, r)); CHECK(r == CfgRow::e2e_dm);
    // ...and the rest of the menu is UNMOVED in meaning: every other row is still found by identity
    int n_save = 0, n_ble = 0;
    for (uint8_t i = 0; i < l.n; ++i) { CHECK(l.at(i, r)); if (r == CfgRow::save) ++n_save; if (r == CfgRow::ble_mode) ++n_ble; }
    CHECK(n_save == 1);
    CHECK(n_ble == 1);
}

TEST_CASE("ui14-rows: RELOAD appears ONLY while a conflict stands, exactly once, and before SAVE") {
    const CfgRowList clean = settings_rows(false, /*conflict=*/false, /*provision=*/true);
    CfgRow r{};
    for (uint8_t i = 0; i < clean.n; ++i) { CHECK(clean.at(i, r)); CHECK(r != CfgRow::reload); }
    const CfgRowList conf = settings_rows(false, /*conflict=*/true, /*provision=*/true);
    CHECK(conf.n == uint8_t(clean.n + 1));
    int i_reload = -1, i_save = -1, n_reload = 0;
    for (uint8_t i = 0; i < conf.n; ++i) {
        CHECK(conf.at(i, r));
        if (r == CfgRow::reload) { i_reload = i; ++n_reload; }
        if (r == CfgRow::save)   i_save = i;
    }
    CHECK(n_reload == 1);
    CHECK(i_reload >= 0);
    CHECK(i_save > i_reload);
}

TEST_CASE("ui14-rows: exactly the four covered fields are value rows, and each names its own CfgField") {
    mrfw::CfgField f{};
    CHECK(cfg_row_field(CfgRow::ble_mode, f));            CHECK(f == mrfw::CfgField::ble_mode);
    CHECK(cfg_row_field(CfgRow::e2e_dm, f));              CHECK(f == mrfw::CfgField::e2e_dm);
    CHECK(cfg_row_field(CfgRow::intro_attach, f));        CHECK(f == mrfw::CfgField::intro_attach);
    CHECK(cfg_row_field(CfgRow::mobile_autoregister, f)); CHECK(f == mrfw::CfgField::mobile_autoregister);
    CHECK(cfg_row_field(CfgRow::provision, f) == false);
    CHECK(cfg_row_field(CfgRow::reload, f)    == false);
    CHECK(cfg_row_field(CfgRow::save, f)      == false);
    CHECK(cfg_row_field(CfgRow::discard, f)   == false);
    CHECK(cfg_row_field(CfgRow::back, f)      == false);
}

// ★ THE VISIBLE BYTES, asserted here because `src/firmware_ui.cpp` is compiled by neither the native suite nor the
//   simulator (§B115's rule). ⚠ The 10-column label bound is a PANEL constraint, not a preference — the row renders
//   as `<marker><label:10><space><value>` on a 21-column display.
TEST_CASE("ui14-rows: the row labels are the panel's, and none exceeds the 10-column budget") {
    CHECK(strcmp(settings_row_label(CfgRow::e2e_dm), "DM crypt") == 0);
    CHECK(strcmp(settings_row_label(CfgRow::intro_attach), "key attach") == 0);
    CHECK(strcmp(settings_row_label(CfgRow::mobile_autoregister), "auto reg") == 0);
    CHECK(strcmp(settings_row_label(CfgRow::ble_mode), "BLE") == 0);
    CHECK(strcmp(settings_row_label(CfgRow::provision), "PROVISION") == 0);
    CHECK(strcmp(settings_row_label(CfgRow::save), "SAVE") == 0);
    CHECK(strcmp(settings_row_label(CfgRow::discard), "DISCARD") == 0);
    CHECK(strcmp(settings_row_label(CfgRow::back), "BACK") == 0);
    CHECK(strcmp(settings_row_label(CfgRow::reload), "RELOAD") == 0);
    for (uint8_t i = 0; i < kMaxCfgRows; ++i) CHECK(strlen(settings_row_label(CfgRow(i))) <= 10u);
}

TEST_CASE("ui14-rows: a value renders as the operator reads it, and `periodic` is RENDERED though the menu omits it") {
    CHECK(strcmp(cfg_value_text(mrfw::CfgField::e2e_dm, 0), "off") == 0);
    CHECK(strcmp(cfg_value_text(mrfw::CfgField::e2e_dm, 1), "on") == 0);
    CHECK(strcmp(cfg_value_text(mrfw::CfgField::ble_mode, 0), "off") == 0);
    CHECK(strcmp(cfg_value_text(mrfw::CfgField::ble_mode, 1), "on") == 0);
    // ★ §3.6.2 keeps `periodic` OUT OF THE MENU, and the service's domain still accepts it — so a value written by
    //   serial/BLE must be shown HONESTLY rather than as one of the two the menu offers.
    CHECK(strcmp(cfg_value_text(mrfw::CfgField::ble_mode, 2), "periodic") == 0);
    CHECK(mrfw::cfg_field_valid(mrfw::CfgField::ble_mode, 2) == true);   // ...and the SERVICE's domain is unnarrowed
}

TEST_CASE("ui14-rows: the MENU's cycle is off/on, and an out-of-menu value steps to the first offered one") {
    CHECK(cfg_menu_next(mrfw::CfgField::e2e_dm, 0) == 1);
    CHECK(cfg_menu_next(mrfw::CfgField::e2e_dm, 1) == 0);
    CHECK(cfg_menu_next(mrfw::CfgField::intro_attach, 1) == 0);
    CHECK(cfg_menu_next(mrfw::CfgField::mobile_autoregister, 0) == 1);
    CHECK(cfg_menu_next(mrfw::CfgField::ble_mode, 0) == 1);
    CHECK(cfg_menu_next(mrfw::CfgField::ble_mode, 1) == 0);
    CHECK(cfg_menu_next(mrfw::CfgField::ble_mode, 2) == 0);      // `periodic` -> `off`, a deliberate edit
    // every menu step lands INSIDE the service's typed domain — the menu narrows, it never produces an invalid value
    for (uint8_t v = 0; v <= 2; ++v)
        CHECK(mrfw::cfg_field_valid(mrfw::CfgField::ble_mode, cfg_menu_next(mrfw::CfgField::ble_mode, v)));
}

// ---------------------------------------------------------------------------------------------- the cycle
TEST_CASE("ui14-cycle: SETTINGS is the last slot, and it is LIST-AWARE like TEAM and INBOX") {
    CfgFix f; const auto s = cfg_snap();
    to_settings_menu(f.m, s);
    CHECK(f.m.state().screen == Screen::settings);
    CHECK(f.m.state().settings == Settings::browsing);
    CHECK(f.m.state().cursor == 0);
    const uint8_t n = f.m.settings_row_list(s).n;
    CHECK(n == 7);
    for (uint8_t i = 1; i < n; ++i) {                      // the short press WALKS the rows...
        f.m.on_gesture(Gesture::short_press, s);
        CHECK(f.m.state().screen == Screen::settings);
        CHECK(f.m.state().cursor == i);
    }
    // ★★ [[B232]] CORRECTED THE LANDING IN PLACE: the walk off the last row used to leave the SCREEN, and it now
    //    returns to the CLOSED single-entry view. The property "`short` walks the rows and leaves only at the end"
    //    is unchanged; what the end IS has moved one step inwards, and the screen is left by the press after it.
    f.m.on_gesture(Gesture::short_press, s);               // ...and leaves the MENU only at the end (§3.2)
    CHECK(f.m.state().screen == Screen::settings);
    CHECK(f.m.state().settings == Settings::closed);
    CHECK(f.m.state().cursor == 0);                        // the single entry row IS the selection
    f.m.on_gesture(Gesture::short_press, s);               // ...and one more press passes the screen
    CHECK(f.m.state().screen == Screen::status);
    CHECK(f.m.state().settings == Settings::closed);       // ⛔ the editor state never survives the screen
}

// ------------------------------------------------------------------------------- [[B232]] the SETTINGS single entry
// ★★★★ THE OWNER'S RULING (2026-08-20), AND IT REVERSES A DOCUMENTED §UI-14 BEHAVIOUR: SETTINGS used to auto-enter
//      the menu on arrival, so cycling past it cost one short press PER ROW — up to nine, where every other screen
//      costs one. It now LANDS CLOSED on a single entry row; `short` passes, `double` enters.
// ⇒ WHAT THIS BLOCK MEASURES: the LANDING, the one-press cycle parity, the `double` entry, the service still opening
//   ON ARRIVAL (the §3.6.1 baseline and its conflict latch), the remedy WORDS still readable from the closed view,
//   and BOTH exits from the menu returning THERE rather than off the screen.
// ⛔ NOT measured here: the closed view's own PIXELS (`draw_settings_screen`'s branch and the rail badge beside it) —
//   no test in this file compiles `src/firmware_ui.cpp`; that is `tools/probe_firmware_ui`'s P7/P14g.
TEST_CASE("b232-entry: SETTINGS LANDS CLOSED on ONE entry row, and `short` passes it in ONE press") {
    CfgFix f; const auto s = cfg_snap();
    to_settings(f.m, s);                                   // ⚠ the WALK ONLY — ⛔ no `double`, which is the subject
    CHECK(f.m.state().screen == Screen::settings);
    CHECK(f.m.state().settings == Settings::closed);       // ★ THE RULING: the menu is NOT up on arrival
    CHECK(f.m.state().cursor == 0);
    // ★★ ONE PRESS, and it is asserted as the CYCLE PARITY rather than as a single transition: the menu has seven
    //    rows in this build, so a screen that still auto-entered would need seven more presses to reach STATUS.
    f.m.on_gesture(Gesture::short_press, s);
    CHECK(f.m.state().screen == Screen::status);
    CHECK(f.m.state().settings == Settings::closed);
    // ...and it stays one press per LAP. ⚠ Counted as "presses spent WHILE SETTINGS is up", not as a whole-cycle
    //   total: TEAM and INBOX are legitimately list-aware (§3.2) and their rosters make a total meaningless. This is
    //   the register's own measurement — *"cycling past SETTINGS costs a short press per row, up to 9"*.
    int on_settings = 0;
    for (int lap = 0; lap < 3; ++lap) {
        for (int i = 0; i < 60 && (i == 0 || f.m.state().screen != Screen::settings); ++i) {
            f.m.on_gesture(Gesture::short_press, s); f.m.on_tick(s);
        }
        CHECK(f.m.state().screen == Screen::settings);
        while (f.m.state().screen == Screen::settings && on_settings < 20) {
            f.m.on_gesture(Gesture::short_press, s); f.m.on_tick(s); ++on_settings;
        }
    }
    CHECK(on_settings == 3);                               // ⛔ one press per lap, never one per row
}

TEST_CASE("b232-entry: `double` ENTERS the menu at its FIRST row — the PROVISION-child idiom") {
    CfgFix f; const auto s = cfg_snap();
    to_settings(f.m, s);
    CHECK(f.m.state().settings == Settings::closed);
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().settings == Settings::browsing);
    CHECK(f.m.state().screen == Screen::settings);
    CHECK(f.m.state().cursor == 0);
    f.m.on_tick(s);
    CfgRow r{};
    CHECK(row_under_cursor(f.m, s, r));
    CHECK(r == CfgRow::e2e_dm);                            // whatever row 0 IS, by identity — ⛔ never an index
    // ⛔ AND THE ENTRY PRESS PERFORMED NOTHING: it is a navigation, so no draft moved, nothing was written and no
    //    action row fired. (`e2e_dm` is row 0 here, so a `double` mis-read as a menu activation would be an EDITOR.)
    CHECK(f.store.writes == 0);
    CHECK(f.live.applies == 0);
    CHECK(f.svc.config_unsaved() == false);
}

TEST_CASE("b232-entry: the entry row's label is the panel's, and it fits the 19-column body") {
    CHECK(strcmp(kSettingsEnterText, "ENTER SETTINGS") == 0);
    // the row renders as `<marker><label>` (`draw_settings_screen`), so the marker's column counts too
    CHECK(strlen(kSettingsEnterText) + 1 <= 19u);
    CHECK(kSettingsEnterText[0] != '\0');                  // ⛔ C2: an entry row nobody can read is no entry at all
}

TEST_CASE("b232-open: the ConfigService is OPENED ON ARRIVAL, while the CLOSED view is up") {
    CfgFix f; const auto s = cfg_snap();
    to_settings(f.m, s);                                   // ⚠ again the WALK ONLY: the menu is never entered here
    CHECK(f.m.state().settings == Settings::closed);
    // ★★★ THE PIN. §3.6.1's baseline is snapshotted by `open()`, and the rail badge, the conflict latch and every
    //     later SAVE are all comparisons AGAINST it. Deferring the open to the menu is the tempting wrong fix — it
    //     would leave a node whose operator only glanced at SETTINGS with no baseline at all.
    CHECK(f.svc.is_open() == true);
    CHECK(f.store.writes == 0);                            // ⛔ ...and opening is still a READ (§3.6.1)
    CHECK(f.live.applies == 0);
    // ⇒ AND THE LATCH FIRES FROM HERE: a companion write lands while the closed view is up and is SEEN.
    f.store.rec.e2e_dm = 1;
    f.svc.note_external_write(f.store.rec);
    CHECK(f.svc.conflict() == true);
    CHECK(f.m.state().settings == Settings::closed);       // ...without the menu ever having been opened
}

TEST_CASE("b232-remedy: the remedy WORDS stand from the CLOSED view — every badge-table cell (§6)") {
    CfgFix f; const auto s = cfg_snap();
    // (a) clean: no marker, and ⛔ nothing invented
    to_settings(f.m, s);
    CHECK(f.m.state().settings == Settings::closed);
    CHECK(strcmp(cfg_marker_text(f.svc.config_unsaved(), f.svc.conflict()), "") == 0);
    // (b) UNSAVED, read from the closed view the operator lands on: enter, edit, walk back out to the closed view
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(cursor_to(f.m, s, CfgRow::e2e_dm));
    f.m.on_gesture(Gesture::double_press, s);
    f.m.on_gesture(Gesture::short_press, s);
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.svc.config_unsaved() == true);
    CHECK(cursor_to(f.m, s, CfgRow::back));
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().settings == Settings::closed);
    CHECK(strcmp(cfg_marker_text(f.svc.config_unsaved(), f.svc.conflict()), "CFG* UNSAVED") == 0);
    // (c) CONFLICT wins over unsaved, and it is the SERVICE's own string — ⛔ never SAVE, which `save()` refuses
    f.store.rec.intro_attach = 0;
    f.svc.note_external_write(f.store.rec);
    CHECK(f.svc.conflict() == true);
    CHECK(f.m.state().settings == Settings::closed);
    CHECK(strcmp(cfg_marker_text(f.svc.config_unsaved(), f.svc.conflict()), "CFG! RELOAD") == 0);
    CHECK(strcmp(cfg_marker_text(f.svc.config_unsaved(), f.svc.conflict()),
                 mrfw::cfg_save_panel(mrfw::CfgSave::conflict)) == 0);
    // (d) the TRANSIENT note and the durable RESTART line are `UiState`'s, so they render from the closed view by
    //     construction — the renderer draws them OUTSIDE its closed/menu branch (`draw_settings_tail`).
    CHECK(strcmp(settings_note(f.m.state()), "") == 0);
}

TEST_CASE("b232-exit: BOTH menu exits return to the CLOSED view — ⛔ never straight off the screen") {
    CfgFix f; const auto s = cfg_snap();
    // (a) the BACK ROW
    to_settings_menu(f.m, s);
    CHECK(cursor_to(f.m, s, CfgRow::back));
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().screen == Screen::settings);
    CHECK(f.m.state().settings == Settings::closed);
    CHECK(f.m.state().cursor == 0);
    // (b) THE WALK OFF THE LAST ROW, reached by walking rather than by naming an index
    f.m.on_gesture(Gesture::double_press, s);              // back into the menu
    CHECK(f.m.state().settings == Settings::browsing);
    // ★ RE-ENTRY OPENS ON THE FIRST ROW — ⛔ never on the row the operator left the menu from, which was `BACK`:
    //   the closed view is the parent here and it has ONE row, so there is no pick for a remembered row to restore.
    f.m.on_tick(s);
    CHECK(f.m.state().cursor == 0);
    { CfgRow r0{}; CHECK(row_under_cursor(f.m, s, r0)); CHECK(r0 == CfgRow::e2e_dm); }
    for (int i = 0; i < 20 && f.m.state().settings == Settings::browsing; ++i)
        f.m.on_gesture(Gesture::short_press, s);
    CHECK(f.m.state().screen == Screen::settings);
    CHECK(f.m.state().settings == Settings::closed);
    CHECK(f.m.state().cursor == 0);
    // ★ AND ONLY THEN DOES THE SCREEN CHANGE — the "where am I" jump the ruling exists to remove.
    f.m.on_gesture(Gesture::short_press, s);
    CHECK(f.m.state().screen == Screen::status);
    CHECK(f.m.state().settings == Settings::closed);
}

// ---------------------------------------------------------------------------------------------- short's two modes
TEST_CASE("ui14-edit: `double` ENTERS a value row and `short` then CYCLES ITS VALUE — the draft only") {
    CfgFix f; const auto s = cfg_snap();
    to_settings_menu(f.m, s);
    CHECK(cursor_to(f.m, s, CfgRow::e2e_dm));
    CHECK(f.svc.is_open());
    CHECK(f.svc.draft().at(mrfw::CfgField::e2e_dm) == 0);
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().settings == Settings::editing);      // ★ THE STATE THAT SEPARATES short's TWO MODES
    const uint8_t cur = f.m.state().cursor;
    f.m.on_gesture(Gesture::short_press, s);
    CHECK(f.svc.draft().at(mrfw::CfgField::e2e_dm) == 1);  // the value moved...
    CHECK(f.m.state().cursor == cur);                      // ...and the cursor did NOT (this is the trap)
    CHECK(f.m.state().screen == Screen::settings);
    // ⛔ RAM ONLY (§3.6.1): no durable write, no live mutation
    CHECK(f.store.writes == 0);
    CHECK(f.live.applies == 0);
    CHECK(f.store.rec.e2e_dm == 0);
    CHECK(f.svc.config_unsaved() == true);                 // ★ the marker is `config_unsaved`, never `dirty`
    f.m.on_gesture(Gesture::short_press, s);
    CHECK(f.svc.draft().at(mrfw::CfgField::e2e_dm) == 0);  // it CYCLES
    CHECK(f.svc.config_unsaved() == false);                // ...back to the baseline, so the marker clears again
    f.m.on_gesture(Gesture::short_press, s);
    f.m.on_gesture(Gesture::double_press, s);              // `double` ACCEPTS
    CHECK(f.m.state().settings == Settings::browsing);
    CHECK(f.svc.draft().at(mrfw::CfgField::e2e_dm) == 1);  // ...and accepting is not a revert
    f.m.on_gesture(Gesture::short_press, s);               // and NOW a short press walks again
    CHECK(f.m.state().cursor == uint8_t(cur + 1));
}

TEST_CASE("ui14-edit: `dirty` and `config_unsaved` are DIFFERENT FACTS and this is the file where both are read") {
    CfgFix f; const auto s = cfg_snap();
    to_settings_menu(f.m, s);
    CHECK(f.m.state().dirty == true);                      // a repaint is owed (the walk marked it)
    CHECK(f.svc.config_unsaved() == false);                // ...and NOTHING is unsaved
    f.m.clear_dirty();
    CHECK(cursor_to(f.m, s, CfgRow::intro_attach));
    f.m.on_gesture(Gesture::double_press, s);
    f.m.on_gesture(Gesture::short_press, s);               // now the draft really differs
    CHECK(f.svc.config_unsaved() == true);
    f.m.clear_dirty();
    CHECK(f.m.state().dirty == false);                     // ⛔ a consumed repaint does NOT clear the draft marker
    CHECK(f.svc.config_unsaved() == true);
}

// ---------------------------------------------------------------------------------------------- SAVE
TEST_CASE("ui14-save: an edited draft SAVES once, applies live, and is no longer unsaved") {
    CfgFix f; const auto s = cfg_snap();
    to_settings_menu(f.m, s);
    CHECK(cursor_to(f.m, s, CfgRow::mobile_autoregister));
    f.m.on_gesture(Gesture::double_press, s);
    f.m.on_gesture(Gesture::short_press, s);               // OFF -> ON in the draft
    CHECK(f.m.state().settings == Settings::editing);
    f.m.on_gesture(Gesture::double_press, s);              // accept
    CHECK(cursor_to(f.m, s, CfgRow::save));
    CHECK(f.live.applies == 0);                            // ⛔ nothing live has happened yet
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().cfg_have_save == true);
    CHECK(f.m.state().cfg_save == mrfw::CfgSave::saved);
    CHECK(strcmp(settings_note(f.m.state()), "SAVED") == 0);
    CHECK(f.store.writes == 1);                            // EXACTLY one durable write
    CHECK(f.store.rec.mobile_autoregister == 1);
    CHECK(f.store.rec.node_id == 42);                      // ...and the NON-covered fields came through untouched
    CHECK(f.store.rec.channel_ctr == 7u);
    CHECK(f.live.applies == 1);                            // live, and only AFTER the durable write
    CHECK(f.svc.config_unsaved() == false);
    CHECK(f.svc.reboot_required() == false);
}

TEST_CASE("ui14-save: a no-op SAVE says NO CHANGE and writes nothing") {
    CfgFix f; const auto s = cfg_snap();
    to_settings_menu(f.m, s);
    CHECK(cursor_to(f.m, s, CfgRow::save));
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().cfg_save == mrfw::CfgSave::no_change);
    CHECK(strcmp(settings_note(f.m.state()), "NO CHANGE") == 0);
    CHECK(f.store.writes == 0);
    CHECK(f.live.applies == 0);
}

TEST_CASE("ui14-save: a FAILED write says SAVE FAILED and RETAINS the draft and its marker") {
    CfgFix f; const auto s = cfg_snap();
    to_settings_menu(f.m, s);
    CHECK(cursor_to(f.m, s, CfgRow::e2e_dm));
    f.m.on_gesture(Gesture::double_press, s);
    f.m.on_gesture(Gesture::short_press, s);
    f.m.on_gesture(Gesture::double_press, s);
    f.store.can_save = false;
    CHECK(cursor_to(f.m, s, CfgRow::save));
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().cfg_save == mrfw::CfgSave::nv_failed);
    // ★ THE RULED STRING, and it comes from the SERVICE's own formatter — this asserts they are the same bytes.
    CHECK(strcmp(settings_note(f.m.state()), "SAVE FAILED") == 0);
    CHECK(strcmp(settings_note(f.m.state()), mrfw::cfg_save_panel(mrfw::CfgSave::nv_failed)) == 0);
    CHECK(f.store.writes == 1);                            // it was ATTEMPTED...
    CHECK(f.store.rec.e2e_dm == 0);                        // ...and changed nothing
    CHECK(f.live.applies == 0);                            // ⛔ never applied live on a failed write
    CHECK(f.svc.config_unsaved() == true);                 // ★ the marker is RETAINED — nothing was established
    CHECK(f.svc.draft().at(mrfw::CfgField::e2e_dm) == 1);  // ...and so is the draft
}

TEST_CASE("ui14-save: a reboot-class save is SAVED and REBOOT-REQUIRED — two independent facts") {
    CfgFix f; auto s = cfg_snap(); s.ble_row = true;        // the BLE row is the only reboot-class one
    to_settings_menu(f.m, s);
    CHECK(cursor_to(f.m, s, CfgRow::ble_mode));
    f.m.on_gesture(Gesture::double_press, s);
    f.m.on_gesture(Gesture::short_press, s);                // off -> on, in the draft
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(cursor_to(f.m, s, CfgRow::save));
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().cfg_save == mrfw::CfgSave::saved_reboot);
    CHECK(f.store.rec.ble_mode == 1);
    // ★★ THE TWO FACTS, AND THEY MUST NOT COLLAPSE: it IS durably saved and is NO LONGER unsaved, AND a reboot is
    //    still owed because the running stack cannot adopt it.
    CHECK(f.svc.config_unsaved() == false);
    CHECK(f.svc.reboot_required() == true);
    // ⛔ `ble_mode` is structurally absent from the live-apply type, so the panel could not have applied it live
    CHECK(f.live.eff.at(mrfw::CfgField::ble_mode) == 0);
    CHECK(strcmp(settings_note(f.m.state()), "SAVED") == 0);
    CHECK(strcmp(kCfgRestartText, "RESTART NEEDED") == 0);  // §3.3's third literal, rendered from `reboot_required`
    // ...and the reboot fact SURVIVES the transient note (§3.6.5: visible until the reboot)
    f.m.on_gesture(Gesture::short_press, s);
    CHECK(strcmp(settings_note(f.m.state()), "") == 0);
    CHECK(f.svc.reboot_required() == true);
}

// ---------------------------------------------------------------------------------------------- conflict
TEST_CASE("ui14-conflict: SAVE is refused with CFG! RELOAD, and RELOAD then merges the three states") {
    CfgFix f; const auto s = cfg_snap();
    to_settings_menu(f.m, s);
    CHECK(cursor_to(f.m, s, CfgRow::e2e_dm));
    f.m.on_gesture(Gesture::double_press, s);
    f.m.on_gesture(Gesture::short_press, s);                // the operator edits e2e_dm 0 -> 1
    f.m.on_gesture(Gesture::double_press, s);
    // ...and serial/BLE writes a DIFFERENT covered field underneath (its immediate-write path, §3.6.1)
    f.store.rec.intro_attach = 0;
    CHECK(f.m.settings_row_list(s).n == 7);                 // no RELOAD row yet — the conflict is not known
    CHECK(cursor_to(f.m, s, CfgRow::save));
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().cfg_save == mrfw::CfgSave::conflict);
    CHECK(strcmp(settings_note(f.m.state()), "CFG! RELOAD") == 0);
    CHECK(strcmp(settings_note(f.m.state()), mrfw::cfg_save_panel(mrfw::CfgSave::conflict)) == 0);
    CHECK(f.store.writes == 0);                             // ⛔ ZERO writes — last-writer-wins is what this prevents
    CHECK(f.svc.conflict() == true);
    CHECK(strcmp(cfg_marker_text(f.svc.config_unsaved(), f.svc.conflict()), "CFG! RELOAD") == 0);
    // ★ the escape hatch APPEARS, exactly when it applies
    CHECK(f.m.settings_row_list(s).n == 8);
    CHECK(cursor_to(f.m, s, CfgRow::reload));
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.svc.conflict() == false);
    CHECK(f.m.state().cfg_refresh_failed == false);
    // ★★ [[B192]]'s ruled THREE-WAY MERGE, in reported form: the field the operator EDITED stays in the draft, the
    //    field they did NOT touch adopts the companion's value.
    CHECK(f.svc.draft().at(mrfw::CfgField::e2e_dm) == 1);
    CHECK(f.svc.draft().at(mrfw::CfgField::intro_attach) == 0);
    CHECK(f.m.settings_row_list(s).n == 7);                 // ...and the row retires with the conflict
    CHECK(cursor_to(f.m, s, CfgRow::save));
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().cfg_save == mrfw::CfgSave::saved);
    CHECK(f.store.writes == 1);
    CHECK(f.store.rec.e2e_dm == 1);
    CHECK(f.store.rec.intro_attach == 0);
}

// ---------------------------------------------------------------------------------------------- DISCARD / BACK
TEST_CASE("ui14-discard: DISCARD is the explicit full reset, and it clears the marker") {
    CfgFix f; const auto s = cfg_snap();
    to_settings_menu(f.m, s);
    CHECK(cursor_to(f.m, s, CfgRow::e2e_dm));
    f.m.on_gesture(Gesture::double_press, s);
    f.m.on_gesture(Gesture::short_press, s);
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.svc.config_unsaved() == true);
    CHECK(cursor_to(f.m, s, CfgRow::discard));
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.svc.config_unsaved() == false);
    CHECK(f.svc.draft().at(mrfw::CfgField::e2e_dm) == 0);
    CHECK(f.store.writes == 0);                             // a DISCARD writes NOTHING
    CHECK(f.live.applies == 0);
    CHECK(f.m.state().cfg_refresh_failed == false);
}

TEST_CASE("ui14-discard: an unreadable store says NV READ FAILED and the draft SURVIVES") {
    CfgFix f; const auto s = cfg_snap();
    to_settings_menu(f.m, s);
    CHECK(cursor_to(f.m, s, CfgRow::e2e_dm));
    f.m.on_gesture(Gesture::double_press, s);
    f.m.on_gesture(Gesture::short_press, s);
    f.m.on_gesture(Gesture::double_press, s);
    f.store.can_load = false;
    CHECK(cursor_to(f.m, s, CfgRow::discard));
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().cfg_refresh_failed == true);
    CHECK(strcmp(settings_note(f.m.state()), "NV READ FAILED") == 0);
    CHECK(f.svc.draft().at(mrfw::CfgField::e2e_dm) == 1);   // ⛔ a failure never destroys the draft (C2)
    CHECK(f.svc.config_unsaved() == true);
}

// ⛔ TITLE CORRECTED IN PLACE 2026-08-20 ([[B232]]): it read *"it leaves the screen"*, and BACK now leaves the MENU
//    and stays on SETTINGS. §3.6.2's actual words — *"`BACK` is safe and PRESERVES an unsaved draft"* — are what this
//    case measures and they are untouched; the landing was always this model's choice.
TEST_CASE("ui14-back: BACK is safe — it leaves the MENU and PRESERVES the unsaved draft") {
    CfgFix f; const auto s = cfg_snap();
    to_settings_menu(f.m, s);
    CHECK(cursor_to(f.m, s, CfgRow::e2e_dm));
    f.m.on_gesture(Gesture::double_press, s);
    f.m.on_gesture(Gesture::short_press, s);
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.svc.config_unsaved() == true);
    CHECK(cursor_to(f.m, s, CfgRow::back));
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().screen == Screen::settings);         // ★ [[B232]]: the CLOSED view, ⛔ never straight off
    CHECK(f.m.state().settings == Settings::closed);
    CHECK(f.m.state().cursor == 0);
    f.m.on_gesture(Gesture::short_press, s);               // ...and one more press passes the screen
    CHECK(f.m.state().screen == Screen::status);
    // ★★ THE WHOLE POINT: the draft is still there, the marker is still up, and the service is still open.
    CHECK(f.svc.config_unsaved() == true);
    CHECK(f.svc.draft().at(mrfw::CfgField::e2e_dm) == 1);
    CHECK(f.svc.is_open() == true);
    CHECK(f.store.writes == 0);
    // ...and STATUS is where the marker is seen without cycling back (§3.3)
    CHECK(strcmp(cfg_marker_text(f.svc.config_unsaved(), f.svc.conflict()), "CFG* UNSAVED") == 0);
    // RE-ENTERING must not reset it either — `CfgOpen::already_open` is a no-op by contract
    to_settings_menu(f.m, s);
    CHECK(f.m.state().screen == Screen::settings);
    CHECK(f.svc.draft().at(mrfw::CfgField::e2e_dm) == 1);
    CHECK(f.svc.config_unsaved() == true);
}

TEST_CASE("ui14-back: BLANKING preserves the draft too — a timeout may never discard (§3.6.1)") {
    CfgFix f; auto s = cfg_snap();
    to_settings_menu(f.m, s);
    CHECK(cursor_to(f.m, s, CfgRow::e2e_dm));
    f.m.on_gesture(Gesture::double_press, s);
    f.m.on_gesture(Gesture::short_press, s);
    CHECK(f.svc.config_unsaved() == true);
    auto s2 = cfg_snap(s.now_ms + kBlankMs + 1);
    f.m.on_tick(s2);
    CHECK(f.m.state().blanked == true);
    CHECK(f.svc.config_unsaved() == true);                  // ⛔ the forbidden discard did NOT happen
    CHECK(f.svc.draft().at(mrfw::CfgField::e2e_dm) == 1);
    CHECK(f.store.writes == 0);
}

// ================================================================ §UI-15 slice 4 — the PROVISIONING state model
// ★★★ WHAT THIS BLOCK MEASURES, and what it deliberately does NOT. The §UI-14 case it replaces pinned a PLACEHOLDER
//     (`PROVISION: UI-15`, an activation that refused because §3.6.3 had no flow); plan §4/§5/§6 replace that refusal
//     with a GATED STATE MODEL, so the placeholder's assertions are retired WITH the placeholder — nothing they
//     measured survives.
// ⛔⛔ THE REACHABLE SURFACE WAS EXACTLY FOUR THINGS WHEN SLICE 4 WROTE THIS ([[B222]], QG-ruled): the gated entry to
//     `menu`, the menu's cycling, its BACK, and the close-on-leave. NOTHING DEEPER — activating CREATE TEAM or JOIN
//     NETWORK did nothing in this tree, because those entries had to land WITH the adapters behind them, and that
//     absence was itself asserted (`ui15-pending`) rather than argued.
// ✅ CORRECTED IN PLACE 2026-08-20 (§UI-15 slice 6): both entries now HAVE their flows — slice 5 landed CREATE TEAM
//    and slice 6 the static join — so the four-thing surface and the `ui15-pending` pin are both HISTORY. The pin was
//    WITHDRAWN, not deleted (the note where it stood, further down), and the property it really carried — a
//    transition lands WITH its flow, never one slice ahead of it — lives on as `ui15-entry`.
// ⇒ WHAT **THIS BLOCK** MEASURES IS UNCHANGED and is the slice-4 STATE MODEL: the gate, the menu, its BACK and the
//   close-on-leave. The flows themselves are measured in their OWN blocks below (§UI-15 slice 5 and slice 6), and the
//   RENDERER in neither — no test here compiles `src/firmware_ui.cpp` (`tools/probe_firmware_ui` is its cover).
// ⛔ STILL NOT measured anywhere, by scope: §3.6.4's nearby-team scan (§UI-16).
namespace {
// A snapshot whose §6 CHILD PREDICATES are both satisfied. ⛔ They are two SEPARATE parameters, never one flag — see
// `ui15-hide` below, which is the case that would pass on a model that collapsed them.
UiSnapshot prov_snap(bool create_team = true, bool join_static = true, uint32_t now_ms = 1000) {
    UiSnapshot s = cfg_snap(now_ms);
    s.prov_create_team = create_team; s.prov_join_static = join_static;
    return s;
}
// Walk to SETTINGS, put the cursor on PROVISION and open it. ⚠ The caller ASSERTS the landing — this returns the
// gate's verdict rather than claiming one, because "the gate refused" is exactly what half these cases drive.
bool open_provision(UiModel& m, const UiSnapshot& s) {
    to_settings_menu(m, s);
    if (!cursor_to(m, s, CfgRow::provision)) return false;
    m.on_gesture(Gesture::double_press, s);
    return m.state().settings == Settings::provisioning;
}
// The child row the PROVISION cursor is on, by identity — the same question the renderer will ask (slice 5).
bool prov_row_under_cursor(UiModel& m, const UiSnapshot& s, ProvRow& out) {
    return m.provision_row_list(s).at(m.state().cursor, out);
}
// Cycle the sub-view's cursor onto a named child. ⛔ Never a hardcoded index: BOTH children are conditional, so an
// index means different things in different arms — §B66's coupling, one menu deeper. BOUNDED, so a missing row fails
// the caller's check instead of looping.
bool prov_cursor_to(UiModel& m, const UiSnapshot& s, ProvRow want) {
    for (int i = 0; i < 12; ++i) {
        ProvRow r{};
        if (prov_row_under_cursor(m, s, r) && r == want) return true;
        m.on_gesture(Gesture::short_press, s);
    }
    return false;
}
}  // namespace

// ---------------------------------------------------------------------------------------------- §5 — the state shape
TEST_CASE("ui15-model: the Provision enum is the eight ADOPTED arms, and the sub-state is an ENUM not a flag") {
    // ★ The arms and their ORDER are plan §5's, listed one by one rather than counted: a battery that asserted only
    //   a total would pass on a set with the right size and the wrong members.
    CHECK(uint8_t(Provision::closed)         == 0);
    CHECK(uint8_t(Provision::menu)           == 1);
    CHECK(uint8_t(Provision::create_confirm) == 2);
    CHECK(uint8_t(Provision::create_result)  == 3);
    CHECK(uint8_t(Provision::join_select)    == 4);
    CHECK(uint8_t(Provision::join_confirm)   == 5);
    CHECK(uint8_t(Provision::join_waiting)   == 6);
    CHECK(uint8_t(Provision::join_result)    == 7);
    // ⛔ AND THE SUB-VIEW IS THE `Settings` ENUM'S FOURTH ARM, ⛔ never a `bool in_provision` beside it: the domain
    //    is four-valued and this file's own block names the binary-test-over-a-ternary-domain defect five times over.
    CHECK(uint8_t(Settings::provisioning) == 3);
    // The default state of a fresh model is CLOSED on both authorities — the invariant, at rest.
    UiState st{};
    CHECK(st.settings == Settings::closed);
    CHECK(st.provisioning == Provision::closed);
    CHECK(st.prov_confirm == ProvConfirm::back);
    CHECK(st.prov_block == ProvBlock::none);
}

// ---------------------------------------------------------------------------------------------- §4 — the gate
TEST_CASE("ui15-gate: a CLEAN draft OPENS PROVISION on its menu, and nothing was written to get there") {
    CfgFix f; const auto s = prov_snap();
    CHECK(open_provision(f.m, s));
    CHECK(f.m.state().settings == Settings::provisioning);
    CHECK(f.m.state().provisioning == Provision::menu);
    CHECK(f.m.state().screen == Screen::settings);            // ⛔ no sixth cycle slot — PROVISION lives INSIDE SETTINGS
    CHECK(strcmp(settings_note(f.m.state()), "") == 0);       // nothing refused, so nothing is said
    CHECK(f.store.writes == 0);
    CHECK(f.live.applies == 0);
}

TEST_CASE("ui15-gate: an UNSAVED draft refuses with SAVE OR DISCARD, opens nothing and SAVES NOTHING") {
    CfgFix f; const auto s = prov_snap();
    to_settings_menu(f.m, s);
    CHECK(cursor_to(f.m, s, CfgRow::e2e_dm));
    f.m.on_gesture(Gesture::double_press, s);
    f.m.on_gesture(Gesture::short_press, s);                  // the operator edits e2e_dm 0 -> 1, and does not save
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.svc.config_unsaved() == true);
    CHECK(f.svc.conflict() == false);
    CHECK(cursor_to(f.m, s, CfgRow::provision));
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().prov_block == ProvBlock::unsaved);
    CHECK(strcmp(settings_note(f.m.state()), "SAVE OR DISCARD") == 0);
    CHECK(f.m.state().settings == Settings::browsing);        // ⛔ it opened NOTHING
    CHECK(f.m.state().provisioning == Provision::closed);
    // ⛔⛔ C2 / plan §4 — PROVISION NEVER SAVES ON THE OPERATOR'S BEHALF, and this is the COUNTED discriminator
    //    rather than a state assertion: a gate that "helpfully" saved would leave `config_unsaved()` false and every
    //    state check above would still pass.
    CHECK(f.store.writes == 0);
    CHECK(f.live.applies == 0);
    CHECK(f.svc.config_unsaved() == true);                    // the draft is exactly where the operator left it
    CHECK(f.svc.draft().at(mrfw::CfgField::e2e_dm) == 1);
    f.m.on_gesture(Gesture::short_press, s);                  // the note is transient, like every other one
    CHECK(f.m.state().prov_block == ProvBlock::none);
    CHECK(strcmp(settings_note(f.m.state()), "") == 0);
}

TEST_CASE("ui15-gate: a CONFLICT refuses with RELOAD OR DISCARD — ⛔ never SAVE — opens nothing and writes nothing") {
    CfgFix f; const auto s = prov_snap();
    to_settings_menu(f.m, s);
    // An external writer moves a COVERED field under a draft the operator has not touched ⇒ `conflict()` WITHOUT
    // `config_unsaved()`. That is the cell v1 conflated: the two predicates are different comparisons.
    f.store.rec.intro_attach = 0;
    f.svc.note_external_write(f.store.rec);
    CHECK(f.svc.conflict() == true);
    CHECK(f.svc.config_unsaved() == false);
    CHECK(cursor_to(f.m, s, CfgRow::provision));
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().prov_block == ProvBlock::conflict);
    CHECK(strcmp(settings_note(f.m.state()), "RELOAD OR DISCARD") == 0);
    // ★★ THE NEGATIVE HALF IS THE POINT OF §4: the note may NOT point at SAVE, because `save()` REFUSES a conflict
    //    (gate 2a). Asserted as an absence of the word, so a re-worded remedy that still says SAVE fails here.
    CHECK(strstr(settings_note(f.m.state()), "SAVE") == nullptr);
    CHECK(f.m.state().settings == Settings::browsing);
    CHECK(f.m.state().provisioning == Provision::closed);
    CHECK(f.store.writes == 0);                               // ⛔ COUNTED — see the unsaved cell
    CHECK(f.live.applies == 0);
    CHECK(f.svc.conflict() == true);                          // ...and the gate resolved nothing on the way past
}

TEST_CASE("ui15-gate: CONFLICT OUTRANKS UNSAVED — a draft that is BOTH is told to RELOAD, never to SAVE") {
    CfgFix f; const auto s = prov_snap();
    to_settings_menu(f.m, s);
    CHECK(cursor_to(f.m, s, CfgRow::e2e_dm));
    f.m.on_gesture(Gesture::double_press, s);
    f.m.on_gesture(Gesture::short_press, s);                  // an unsaved edit...
    f.m.on_gesture(Gesture::double_press, s);
    f.store.rec.intro_attach = 0;                             // ...UNDER a companion write to a different covered field
    f.svc.note_external_write(f.store.rec);
    // ★ BOTH predicates stand, which is the ORDINARY conflicted state — a conflict is normally also unsaved.
    CHECK(f.svc.conflict() == true);
    CHECK(f.svc.config_unsaved() == true);
    CHECK(cursor_to(f.m, s, CfgRow::provision));
    f.m.on_gesture(Gesture::double_press, s);
    // ⇒ testing `config_unsaved()` FIRST would print SAVE OR DISCARD here: the §4 conflation, arriving through the
    //   ORDER rather than through the predicate. That is why this is its own case.
    CHECK(f.m.state().prov_block == ProvBlock::conflict);
    CHECK(strcmp(settings_note(f.m.state()), "RELOAD OR DISCARD") == 0);
    CHECK(f.m.state().provisioning == Provision::closed);
    CHECK(f.store.writes == 0);
}

// ★★★ RETARGETED 2026-08-20 (QG, on [[B232]]) — SAME CORRECTION AS THE TWO `ui14-open` CASES, and the withdrawn
//     subject is recorded rather than deleted: this case used to walk the menu to `PROVISION` on an unopened service.
//     ⛔ That walk is over rows the renderer does not draw (`CFG UNAVAILABLE`), so the menu is no longer ENTERABLE at
//     all in this state. ⇒ §4's *"neither predicate can be ESTABLISHED without a baseline"* refusal is now paid ONE
//     LAYER OUT — the sub-view cannot be opened because the menu that offers it cannot be opened.
// ⓘ `settings_activate`'s own `!_cfg || !_cfg->is_open()` arm is unchanged and is now unreachable, stated in code.
TEST_CASE("ui15-gate: with NO usable config the MENU never opens, so the gate is never reached") {
    UiFakeStore store; UiFakeLive live;
    store.can_load = false;
    mrfw::ConfigService svc{store, live};
    UiModel m; m.attach_config(svc);
    const auto s = prov_snap();
    to_settings(m, s);
    CHECK(svc.is_open() == false);
    m.on_gesture(Gesture::double_press, s); m.on_tick(s);
    CHECK(m.state().settings == Settings::closed);            // ⛔ no menu, therefore no PROVISION row to activate
    CHECK(m.state().provisioning == Provision::closed);
    CHECK(m.state().prov_block == ProvBlock::none);           // ⛔ §4's remedies do not apply to "there is no draft"
    CHECK(store.writes == 0);
    // ...and the same for a model with NO service at all
    UiModel m2; const auto s2 = prov_snap();
    to_settings(m2, s2);
    m2.on_gesture(Gesture::double_press, s2); m2.on_tick(s2);
    CHECK(m2.state().settings == Settings::closed);
    CHECK(m2.state().provisioning == Provision::closed);
    CHECK(m2.state().prov_block == ProvBlock::none);
}

// ---------------------------------------------------------------------------------------------- §5 — the transitions
TEST_CASE("ui15-menu: BACK closes the sub-view back to the SETTINGS MENU, with the highlight on PROVISION") {
    CfgFix f; const auto s = prov_snap();
    CHECK(open_provision(f.m, s));
    CHECK(prov_cursor_to(f.m, s, ProvRow::back));
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().settings == Settings::browsing);        // ⛔ back to the MENU, never off the screen
    CHECK(f.m.state().provisioning == Provision::closed);
    CHECK(f.m.state().screen == Screen::settings);
    // ★ THE PICK SURVIVED THE SUB-VIEW: `sync_settings` re-anchors from the row identity the operator left on, which
    //   is why the sub-view is forbidden to touch it. Without that guard the cursor would come back on a CHILD index.
    f.m.on_tick(s);
    CfgRow r{};
    CHECK(row_under_cursor(f.m, s, r));
    CHECK(r == CfgRow::provision);
}

TEST_CASE("ui15-menu: `short` CYCLES the children and never walks out of the screen") {
    CfgFix f; const auto s = prov_snap();
    CHECK(open_provision(f.m, s));
    ProvRow r{};
    CHECK(prov_row_under_cursor(f.m, s, r)); CHECK(r == ProvRow::create_team);
    f.m.on_gesture(Gesture::short_press, s);
    CHECK(prov_row_under_cursor(f.m, s, r)); CHECK(r == ProvRow::join_static);
    f.m.on_gesture(Gesture::short_press, s);
    CHECK(prov_row_under_cursor(f.m, s, r)); CHECK(r == ProvRow::back);
    // ★ ...and the next press WRAPS rather than leaving. A sub-view is left by its own BACK, by the long gesture or
    //   by the blank — ⛔ never by `advance_or_next`'s walk-off, which belongs to the ordinary screen cycle.
    f.m.on_gesture(Gesture::short_press, s);
    CHECK(f.m.state().screen == Screen::settings);
    CHECK(f.m.state().settings == Settings::provisioning);
    CHECK(prov_row_under_cursor(f.m, s, r)); CHECK(r == ProvRow::create_team);
}

// ⚠⚠ `ui15-pending` LIVED HERE AND IS WITHDRAWN, NOT DELETED (2026-08-20, §UI-15 slice 6). It asserted that
//    activating JOIN NETWORK *"does NOTHING — the static-join flow is slice 6's"*, which was [[B222]]'s pin and was
//    TRUE until this slice landed the flow behind the row; keeping it would pin the ABSENCE of the very thing being
//    built. ★ The property it really carried — a transition lands WITH its flow, never one slice ahead of it — lives
//    on as `ui15-entry` below, which asserts that the entry READS the profile list, so the screen it opens can never
//    be an empty one it could not have populated.
TEST_CASE("ui15-confirm: the confirmation cursor's DEFAULT is BACK — as MODEL STATE, on the one entry this slice has") {
    // ★★ §3.6.3, VERBATIM IN SUBSTANCE: *"opens a confirmation with BACK selected initially; reaching CREATE requires
    //    `short` then `double`"*. The FLOW that reads it is slice 5/6's ([[B222]]), so the DEFAULT is pinned where it
    //    actually lives — the field's rest value and the value every entry primitive establishes — ⛔ never through a
    //    transition this slice does not have.
    CHECK(uint8_t(ProvConfirm::back) == 0);                   // the zero value IS the safe one: `ProvConfirm{}` is BACK
    CHECK(uint8_t(ProvConfirm::confirm) == 1);
    UiState fresh{};
    CHECK(fresh.prov_confirm == ProvConfirm::back);
    CfgFix f; const auto s = prov_snap();
    // `enter_provision` is the ONE primitive every arm entry goes through, and `menu` is the entry this slice can
    // drive — the same assignment slice 5/6's confirm entries will run.
    CHECK(open_provision(f.m, s));
    CHECK(f.m.state().provisioning == Provision::menu);
    CHECK(f.m.state().prov_confirm == ProvConfirm::back);
    CHECK(f.m.state().cursor == 0);                           // each arm's list starts at its own first row
    // ⛔ AND NOTHING IN THIS TREE MOVES IT: no gesture in the sub-view touches the field, so it cannot drift off BACK
    //    between the entry and the close — walking the whole child menu twice leaves it exactly where it started.
    for (int i = 0; i < 6; ++i) {
        f.m.on_gesture(Gesture::short_press, s);
        CHECK(f.m.state().prov_confirm == ProvConfirm::back);
    }
    CHECK(prov_cursor_to(f.m, s, ProvRow::back));
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().prov_confirm == ProvConfirm::back);      // ...and the close re-establishes it too
    CHECK(f.store.writes == 0);
}

// ---------------------------------------------------------------------------------------------- §5's FIRST PIN
TEST_CASE("ui15-close: ALL EIGHT arms are retired by the close-on-leave reset, cursor included ([[B223]])") {
    // ★★★★ THE REQUIRED GUARD, DRIVEN DIRECTLY. The reset is `provision_reset_on_leave`'s and NOT
    //     `settings_follow_screen`'s (see the helper): at its call site the clause is UNREACHABLE — while the sub-view
    //     owns the press nothing can move `_st.screen` out of SETTINGS underneath it — so a guard written there could
    //     be driven by no test and reddened by no mutation. ⛔ Stating that gap does not discharge the requirement;
    //     hoisting the decision does, and this is the case that spends it.
    // ★ ALL EIGHT ARMS, not the ones a gesture happens to reach: the arms slices 5/6 add are exactly the ones a push
    //   or a timeout may leave standing when the screen moves, which is the situation this guard exists for.
    const Provision arms[] = { Provision::closed,       Provision::menu,        Provision::create_confirm,
                               Provision::create_result, Provision::join_select, Provision::join_confirm,
                               Provision::join_waiting,  Provision::join_result };
    CHECK(sizeof(arms) / sizeof(arms[0]) == 8u);
    for (Provision arm : arms) {
        for (ProvConfirm c : { ProvConfirm::back, ProvConfirm::confirm }) {
            Provision a = arm; ProvConfirm cur = c;
            const bool changed = provision_reset_on_leave(a, cur);
            CHECK(a == Provision::closed);
            // ⛔ AND THE CONFIRM CURSOR RE-ANCHORS TOO: a stale CONFIRM would re-open the next confirmation with the
            //    destructive choice already selected, one `double` from replacing a membership.
            CHECK(cur == ProvConfirm::back);
            // ⓘ "changed" is what the caller repaints on — true for every arm but the one already at rest.
            CHECK(changed == (arm != Provision::closed || c != ProvConfirm::back));
        }
    }
    // ...and it is IDEMPOTENT: a second call on a state already at rest reports no change, so a screen sitting off
    // SETTINGS does not mark the panel dirty on every tick.
    Provision a = Provision::join_waiting; ProvConfirm cur = ProvConfirm::confirm;
    CHECK(provision_reset_on_leave(a, cur) == true);
    CHECK(provision_reset_on_leave(a, cur) == false);
    CHECK(a == Provision::closed);
    CHECK(cur == ProvConfirm::back);
}

TEST_CASE("ui15-close: the reachable arm survives a tick and is closed by the long gesture, on BOTH authorities") {
    // ⓘ THE ARM DRIVEN HERE IS THE ONE THIS SLICE CAN REACH (`menu`); the other seven are driven through the pure
    //   helper above, which is the only honest way to reach them in this tree ([[B222]]/[[B223]]).
    // ★ THE NEGATIVE HALF FIRST: a tick must NOT close the sub-view. `sync_settings` runs on every tick and every
    //   gesture, and a version of it that "tidied up" would make provisioning unusable one frame after it opened.
    CfgFix f; const auto s = prov_snap();
    CHECK(open_provision(f.m, s));
    CHECK(f.m.state().provisioning == Provision::menu);
    f.m.on_tick(s);
    CHECK(f.m.state().provisioning == Provision::menu);       // ...still exactly where the operator left it
    CHECK(f.m.state().settings == Settings::provisioning);
    // ⇒ and the long gesture, which §3.6.5 rule 1 requires to pre-empt it, closes BOTH fields together.
    f.m.on_gesture(Gesture::long_cancel, s);
    CHECK(f.m.state().provisioning == Provision::closed);
    CHECK(f.m.state().settings == Settings::browsing);
}

TEST_CASE("ui15-close: leaving SETTINGS by its BACK row retires the sub-state, and coming back opens the MENU") {
    // ⚠ RE-TITLED AND RE-AIMED at [[B223]]: this case used to be called *"walking OFF the SETTINGS screen closes
    //   provisioning"*, which it never did — it closes the sub-view FIRST (it has to: while the sub-view is open no
    //   gesture reaches the SETTINGS BACK row), so it could not exercise the reset it named. ⇒ it now claims only
    //   what it drives: the ORDINARY leave path leaves nothing behind, and re-entry is a fresh menu. The reset ITSELF
    //   is driven directly, above.
    CfgFix f; const auto s = prov_snap();
    CHECK(open_provision(f.m, s));
    CHECK(prov_cursor_to(f.m, s, ProvRow::back));
    f.m.on_gesture(Gesture::double_press, s);                 // the sub-view -> the SETTINGS menu
    CHECK(f.m.state().settings == Settings::browsing);
    CHECK(cursor_to(f.m, s, CfgRow::back));
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().screen == Screen::settings);            // ★ [[B232]]: BACK leaves the MENU, not the screen
    CHECK(f.m.state().settings == Settings::closed);
    CHECK(f.m.state().provisioning == Provision::closed);
    f.m.on_gesture(Gesture::short_press, s);                  // ...and the press after it leaves the screen
    CHECK(f.m.state().screen == Screen::status);
    CHECK(f.m.state().provisioning == Provision::closed);
    // ★ AND COMING BACK OPENS THE MENU at its first row — ⛔ never something the operator abandoned.
    CHECK(open_provision(f.m, s));
    CHECK(f.m.state().provisioning == Provision::menu);
    CHECK(f.m.state().cursor == 0);
}

TEST_CASE("ui15-emergency: a long press CLOSES the provisioning sub-view before arming (§3.6.5 rule 1)") {
    CfgFix f; const auto s = prov_snap();
    CHECK(open_provision(f.m, s));
    CHECK(f.m.state().provisioning == Provision::menu);
    // ⓘ The cursor is left on CREATE TEAM, the row a `double` would act on — which is what the pre-emption is FOR.
    CHECK(prov_cursor_to(f.m, s, ProvRow::create_team));
    f.m.on_gesture(Gesture::long_arm, s);
    // ⛔ AN UNCONFIRMED DESTRUCTIVE ACTION DOES NOT SURVIVE AN ALARM: the overlay owns the body, so a sub-view left
    //    standing underneath is invisible AND still holding the press, with CREATE TEAM under its cursor.
    CHECK(f.m.state().provisioning == Provision::closed);
    CHECK(f.m.state().settings == Settings::browsing);
    // ...and a cancel does not bring it back — closing is destructive, exactly as it is for the editor and the modal.
    f.m.on_gesture(Gesture::long_cancel, s);
    CHECK(f.m.state().provisioning == Provision::closed);
    CHECK(f.store.writes == 0);
}

// ---------------------------------------------------------------------------------------------- §6 — availability
TEST_CASE("ui15-hide: each child follows its OWN predicate, and STATIC JOIN survives a team-less build") {
    ProvRow r{};
    const ProvRowList both = provision_rows(true, true);
    CHECK(both.n == 3);
    CHECK(both.at(0, r)); CHECK(r == ProvRow::create_team);
    CHECK(both.at(1, r)); CHECK(r == ProvRow::join_static);
    CHECK(both.at(2, r)); CHECK(r == ProvRow::back);
    CHECK(both.at(3, r) == false);                            // ⛔ fails closed, like every other row list here
    CHECK(both.at(255, r) == false);
    // ★★★ PLAN §6's CORRECTION, AND THIS IS THE CASE THAT CARRIES IT: with the TEAM plane absent, CREATE is hidden
    //     and ⛔ STATIC JOIN IS NOT — it has nothing to do with the team plane. A model that governed both children
    //     by one flag passes every other case in this block and fails here.
    const ProvRowList join_only = provision_rows(false, true);
    CHECK(join_only.n == 2);
    CHECK(join_only.at(0, r)); CHECK(r == ProvRow::join_static);
    CHECK(join_only.at(1, r)); CHECK(r == ProvRow::back);
    const ProvRowList create_only = provision_rows(true, false);
    CHECK(create_only.n == 2);
    CHECK(create_only.at(0, r)); CHECK(r == ProvRow::create_team);
    CHECK(create_only.at(1, r)); CHECK(r == ProvRow::back);
    // ⓘ NEITHER child (the `gateway_heltec` shape: OLED=1, MR_N_LAYERS=2) — the menu still has BACK, because leaving
    //   must never depend on a build flag.
    const ProvRowList none = provision_rows(false, false);
    CHECK(none.n == 1);
    CHECK(none.at(0, r)); CHECK(r == ProvRow::back);
    // ...and every row's label fits the rail's 19-column body with its `>` marker
    for (uint8_t i = 0; i < kMaxProvRows; ++i) CHECK(1u + strlen(provision_row_label(ProvRow(i))) <= 19u);
    CHECK(strcmp(provision_row_label(ProvRow::create_team), "CREATE TEAM") == 0);
    CHECK(strcmp(provision_row_label(ProvRow::join_static), "JOIN NETWORK") == 0);
    CHECK(strcmp(provision_row_label(ProvRow::back), "BACK") == 0);
}

TEST_CASE("ui15-hide: a HIDDEN child has NO refusing stub — it cannot be reached, walked to or activated") {
    // The team plane is absent; static join is not. [[B209]]: hide it, ⛔ never render a row that refuses.
    CfgFix f; const auto s = prov_snap(/*create_team=*/false, /*join_static=*/true);
    CHECK(open_provision(f.m, s));
    CHECK(f.m.provision_row_list(s).n == 2);
    // Walk the WHOLE menu twice: the hidden child is on no row, so no press can select it and none can activate it.
    for (int i = 0; i < 5; ++i) {
        ProvRow r{};
        CHECK(prov_row_under_cursor(f.m, s, r));
        CHECK(r != ProvRow::create_team);
        f.m.on_gesture(Gesture::short_press, s);
    }
    // ...and the child that IS supported is on the list, can be walked to and OPENS ITS OWN FLOW — ⚠ the landing was
    // `Provision::menu` until §UI-15 slice 6 landed that flow (it was `ui15-pending`'s state). What §6 owns is which
    // rows EXIST, and that is what is asserted: the SUPPORTED child works while the hidden one is unreachable.
    CHECK(prov_cursor_to(f.m, s, ProvRow::join_static));
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().provisioning == Provision::join_select);
    // ⓘ With no seam attached (`CfgFix` has none) the list is UNSERVED, so it offers BACK alone and joins nothing.
    CHECK(f.m.state().join_list.served == false);
    CHECK(f.store.writes == 0);
}

// ---------------------------------------------------------------- §UI-15 slice 5 — the OWNER's PARENT-ROW RULING
TEST_CASE("ui15-parent: with NO child the PROVISION row is HIDDEN, and it returns when a child predicate holds") {
    // ★★★★ OWNER RULING 2026-08-19 (reported form): **the parent row is HIDDEN when no child is available.**
    // ⚠ WHAT THIS CASE REPLACES, recorded rather than silently overwritten: slice 4's *"a build with NEITHER child
    //   opens a menu that offers only BACK, and BACK still works"*. That menu is now UNREACHABLE BY CONSTRUCTION —
    //   which is the ruling — so the case that walked into it could not survive; the property it really carried
    //   (leaving never depends on a build flag) lives on in `provision_rows(false,false).n == 1` below and in
    //   `ui15-hide`'s list case.
    CHECK(provision_has_child(true,  true)  == true);
    CHECK(provision_has_child(true,  false) == true);
    CHECK(provision_has_child(false, true)  == true);         // ⛔ static join alone STILL earns the parent row
    CHECK(provision_has_child(false, false) == false);
    // ⓘ ...and the child list itself is unchanged: BACK is still unconditional, so the sub-view could still be left
    //   if anything ever opened it. The ruling removes the DOOR, not the exit.
    CHECK(provision_rows(false, false).n == 1);

    // The `gateway_heltec` shape (OLED=1, MR_N_LAYERS=2): the row is on no list, so no press can select or activate it.
    CfgFix f; const auto s = prov_snap(/*create_team=*/false, /*join_static=*/false);
    to_settings_menu(f.m, s);
    const CfgRowList l = f.m.settings_row_list(s);
    CfgRow r{};
    for (uint8_t i = 0; i < l.n; ++i) { CHECK(l.at(i, r)); CHECK(r != CfgRow::provision); }
    CHECK(cursor_to(f.m, s, CfgRow::provision) == false);     // ⛔ unreachable by gesture, not merely unrendered
    // ⛔ AND THE MENU-OFFERING-ONLY-BACK STATE IS NOT REACHABLE AT ALL: walking the WHOLE menu twice never enters
    //    provisioning, and nothing is written to find that out.
    for (int i = 0; i < 2 * kMaxCfgRows; ++i) {
        f.m.on_gesture(Gesture::short_press, s);
        CHECK(f.m.state().settings != Settings::provisioning);
    }
    CHECK(f.m.state().provisioning == Provision::closed);
    CHECK(f.store.writes == 0);
    // ★ ...and ONE child predicate is enough to bring the row back, on the SAME model and the same service.
    const auto s2 = prov_snap(/*create_team=*/false, /*join_static=*/true);
    CHECK(cursor_to(f.m, s2, CfgRow::provision));
    f.m.on_gesture(Gesture::double_press, s2);
    CHECK(f.m.state().settings == Settings::provisioning);
    CHECK(f.m.state().provisioning == Provision::menu);
}

// ============================================================ §UI-15 slice 5 — the TEAM-CREATE CONFIRMATION + RESULT
// ★★ WHAT THIS BLOCK MEASURES: the FLOW and the LANDING — how many times the act runs, from which gesture, what the
//    screen says afterwards, and what a BACK costs (nothing). ⛔ IT DOES NOT MEASURE THE ADAPTER: the PHY
//    precondition, `phy.present = false` and the verdict mapping are `test/test_firmware_ui_prov.cpp`'s, driven
//    against the REAL transaction over its own fakes, where a store write and a live apply are counted for real.
namespace {
// The seam's fake. ★ IT RECORDS WHERE THE MODEL WAS WHEN IT RAN, which is what makes §8 pin 2 a measurement rather
// than a reading of the source: at the instant the transaction is performed the screen must still be the CONFIRMATION
// and there must be no result text anywhere.
struct UiFakeProvision : IUiProvision {
    const UiModel* m = nullptr;
    int          calls   = 0;
    UiProvOp     last_op = UiProvOp::none;
    Provision    arm_at_call = Provision::closed;
    const char*  head_at_call = "?";
    UiProvAnswer answer{};                      // the scripted verdict this fake hands back
    // ★★ §UI-15 slice 6: the JOIN half's script and its instruments. `join_answer` is what the join op returns (the
    //    create op keeps `answer`, so a case cannot accidentally measure the wrong act); `last_join` is the PROFILE
    //    the model handed over, which is what makes "what was SHOWN is what is JOINED" assertable.
    UiProvAnswer      join_answer{};
    mrnv::JoinProfile last_join{};
    UiJoinList        list{};                   // what `profiles()` answers
    int               list_calls = 0;
    UiProvAnswer perform(const UiProvIntent& in) override {
        ++calls;
        last_op = in.op;
        if (in.op == UiProvOp::join_static) last_join = in.join;
        if (m) {
            arm_at_call  = m->state().provisioning;
            head_at_call = prov_result_head(m->state().prov_answer);
        }
        return in.op == UiProvOp::join_static ? join_answer : answer;
    }
    UiJoinList profiles() override { ++list_calls; return list; }
};
struct CreateFix : CfgFix {
    UiFakeProvision prov;
    CreateFix() { prov.m = &m; m.attach_provision(prov); }
};
// Open PROVISION and put the confirmation up. The caller ASSERTS the landing.
bool open_create_confirm(CreateFix& f, const UiSnapshot& s) {
    if (!open_provision(f.m, s)) return false;
    if (!prov_cursor_to(f.m, s, ProvRow::create_team)) return false;
    f.m.on_gesture(Gesture::double_press, s);
    return f.m.state().provisioning == Provision::create_confirm;
}
UiProvAnswer created_answer(uint32_t id) {
    UiProvAnswer a{}; a.outcome = UiProvOutcome::created; a.team_id = id; return a;
}
}  // namespace

TEST_CASE("ui15-create: CREATE TEAM opens the confirmation on BACK, and opening it performs NOTHING") {
    CreateFix f; const auto s = prov_snap();
    CHECK(open_create_confirm(f, s));
    CHECK(f.m.state().settings == Settings::provisioning);
    // ★ §3.6.3: *"opens a confirmation with BACK selected initially"* — and it is `enter_provision` that establishes
    //   it, so the default cannot be forgotten by the entry that lands here.
    CHECK(f.m.state().prov_confirm == ProvConfirm::back);
    // ⛔ NOTHING HAS HAPPENED YET: no transaction, no write, no live apply — and no result text exists.
    CHECK(f.prov.calls == 0);
    CHECK(f.store.writes == 0);
    CHECK(f.live.applies == 0);
    CHECK(f.m.state().prov_answer.outcome == UiProvOutcome::none);
    CHECK(strcmp(prov_result_head(f.m.state().prov_answer), "") == 0);
    // `short` TOGGLES between the two actions and ⛔ never walks out of the screen (the InboxAction pair, §3.5).
    f.m.on_gesture(Gesture::short_press, s);
    CHECK(f.m.state().prov_confirm == ProvConfirm::confirm);
    CHECK(f.m.state().provisioning == Provision::create_confirm);
    f.m.on_gesture(Gesture::short_press, s);
    CHECK(f.m.state().prov_confirm == ProvConfirm::back);
    CHECK(f.prov.calls == 0);                                 // toggling is not acting
}

TEST_CASE("ui15-create: BACK from the confirmation returns to the MENU and drives ZERO transactions") {
    CreateFix f; const auto s = prov_snap();
    f.prov.answer = created_answer(0x12A1B2C3u);              // scripted, so a stray call would be VISIBLE as a result
    CHECK(open_create_confirm(f, s));
    f.m.on_gesture(Gesture::double_press, s);                 // `double` on BACK — the SAFE action, selected by default
    CHECK(f.m.state().provisioning == Provision::menu);
    CHECK(f.m.state().settings == Settings::provisioning);
    // ★ THE PIN: BACK anywhere drives ZERO — counted on all three instruments, not argued.
    CHECK(f.prov.calls == 0);
    CHECK(f.store.writes == 0);
    CHECK(f.live.applies == 0);
    CHECK(f.m.state().prov_answer.outcome == UiProvOutcome::none);
    // ...and the menu's own BACK still leaves, so the round trip strands nobody.
    CHECK(prov_cursor_to(f.m, s, ProvRow::back));
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().settings == Settings::browsing);
    CHECK(f.prov.calls == 0);
}

TEST_CASE("ui15-create: CONFIRM drives EXACTLY ONE transaction, and the RESULT is rendered only after it RETURNED") {
    CreateFix f; const auto s = prov_snap();
    f.prov.answer = created_answer(0x12A1B2C3u);
    CHECK(open_create_confirm(f, s));
    f.m.on_gesture(Gesture::short_press, s);                  // BACK -> CREATE: the deliberate short-then-double
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.prov.calls == 1);                                 // ★ EXACTLY ONE
    CHECK(f.prov.last_op == UiProvOp::create_team);
    // ★★★ §8 PIN 2, MEASURED: at the instant the act ran the screen was still the CONFIRMATION and NO success text
    //     existed anywhere. ⛔ A model that moved to the result first and filled it afterwards fails HERE.
    CHECK(f.prov.arm_at_call == Provision::create_confirm);
    CHECK(strcmp(f.prov.head_at_call, "") == 0);
    // ...and only now does the verdict exist, on the arm that the act established.
    CHECK(f.m.state().provisioning == Provision::create_result);
    CHECK(f.m.state().prov_answer.outcome == UiProvOutcome::created);
    CHECK(f.m.state().prov_answer.team_id == 0x12A1B2C3u);
    CHECK(strcmp(prov_result_head(f.m.state().prov_answer), "TEAM CREATED") == 0);
    // ⛔ AND IT IS NOT RE-RUN BY THE PRESS THAT LEAVES: the result is terminal, either press goes back to the menu.
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().provisioning == Provision::menu);
    CHECK(f.prov.calls == 1);
    // ★ EVERY ENTRY RETIRES THE ANSWER, so a verdict can never sit under a screen that did not establish it.
    CHECK(f.m.state().prov_answer.outcome == UiProvOutcome::none);
    f.m.on_gesture(Gesture::short_press, s);                  // a SHORT press leaves it too (`press = back`)
    CHECK(f.prov.calls == 1);
}

TEST_CASE("ui15-create: a SHORT press leaves the result, and a null seam REFUSES OUT LOUD rather than doing nothing") {
    {
        CreateFix f; const auto s = prov_snap();
        f.prov.answer = created_answer(7u);
        CHECK(open_create_confirm(f, s));
        f.m.on_gesture(Gesture::short_press, s);
        f.m.on_gesture(Gesture::double_press, s);
        CHECK(f.m.state().provisioning == Provision::create_result);
        f.m.on_gesture(Gesture::short_press, s);              // `press = back` means EITHER press
        CHECK(f.m.state().provisioning == Provision::menu);
    }
    // ⓘ NO SEAM AT ALL — a `!MR_N_LAYERS<2` build or a partially-wired probe. C2: it refuses out loud, so a `double`
    //   on CREATE is never indistinguishable from a dead button, and ⛔ it claims nothing.
    CfgFix f; const auto s = prov_snap();
    CHECK(open_provision(f.m, s));
    CHECK(prov_cursor_to(f.m, s, ProvRow::create_team));
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().provisioning == Provision::create_confirm);
    f.m.on_gesture(Gesture::short_press, s);
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().provisioning == Provision::create_result);
    CHECK(f.m.state().prov_answer.outcome == UiProvOutcome::refused);
    CHECK(strcmp(prov_result_head(f.m.state().prov_answer), "CREATE REFUSED") == 0);
    CHECK(strcmp(prov_result_detail(f.m.state().prov_answer), "no service") == 0);
    CHECK(f.store.writes == 0);
}

TEST_CASE("ui15-create: the two screens' STRINGS, and each outcome says a DIFFERENT thing") {
    // ★ §B115's rule: the visible bytes are asserted where the suite can read them, so a reworded panel is a RED
    //   suite rather than a discovery on metal.
    CHECK(strcmp(kProvCreateTitle, "CREATE NEW TEAM") == 0);
    CHECK(strcmp(prov_confirm_label(ProvConfirm::back), "BACK") == 0);
    CHECK(strcmp(prov_confirm_label(ProvConfirm::confirm), "CREATE") == 0);
    UiProvAnswer a{};
    CHECK(strcmp(prov_result_head(a), "") == 0);              // nothing established -> nothing said
    CHECK(strcmp(prov_result_detail(a), "") == 0);
    a.outcome = UiProvOutcome::created;
    CHECK(strcmp(prov_result_head(a), "TEAM CREATED") == 0);
    CHECK(strcmp(prov_result_detail(a), "") == 0);            // its second row is the ID, which is a value
    // ★★ THE OWNER'S RULED REFUSAL, split across the two rows by §7.1 rule 5 (24 columns against a 19-column body).
    a.outcome = UiProvOutcome::phy_differs;
    CHECK(strcmp(prov_result_head(a), "PHY DIFFERS") == 0);
    CHECK(strcmp(prov_result_detail(a), "USE SERIAL") == 0);
    // ★ §UI-13's RULED string, CALLED and never re-spelled — the same treatment `settings_note` gives it.
    a.outcome = UiProvOutcome::save_failed;
    CHECK(strcmp(prov_result_head(a), mrfw::cfg_save_panel(mrfw::CfgSave::nv_failed)) == 0);
    CHECK(strcmp(prov_result_head(a), "SAVE FAILED") == 0);
    CHECK(strcmp(prov_result_detail(a), "NOTHING CHANGED") == 0);
    // ★ A staging refusal carries the SERVICE's own token, never a second ProvErr-to-text table.
    a.outcome = UiProvOutcome::refused; a.reason = "role_refused";
    CHECK(strcmp(prov_result_head(a), "CREATE REFUSED") == 0);
    CHECK(strcmp(prov_result_detail(a), "role_refused") == 0);
    // ⛔ THE FOUR HEADLINES ARE FOUR DIFFERENT STRINGS: a collapse would make two outcomes indistinguishable, which is
    //    the "success that isn't" shape one screen over.
    const UiProvOutcome all[] = { UiProvOutcome::created, UiProvOutcome::phy_differs,
                                  UiProvOutcome::save_failed, UiProvOutcome::refused };
    for (UiProvOutcome x : all) {
        for (UiProvOutcome y : all) {
            UiProvAnswer ax{}; ax.outcome = x; UiProvAnswer ay{}; ay.outcome = y;
            const bool same = strcmp(prov_result_head(ax), prov_result_head(ay)) == 0;
            CHECK(same == (x == y));
        }
    }
    // ...and every one of them fits the rail's 19-column body, with the `>`-less full width available to it.
    for (UiProvOutcome x : all) {
        UiProvAnswer ax{}; ax.outcome = x; ax.reason = "no_mobile_plane";   // the widest prov_err_name
        CHECK(strlen(prov_result_head(ax)) <= 19u);
        CHECK(strlen(prov_result_detail(ax)) <= 19u);
    }
    CHECK(1u + strlen(kProvCreateTitle) <= 19u);
    CHECK(1u + strlen(prov_confirm_label(ProvConfirm::confirm)) <= 19u);
}

TEST_CASE("ui15-create: the newly reachable arms are CLOSED by leaving and PRE-EMPTED by the alarm (§3.6.5 rule 1)") {
    // ★★ [[B223]]'s guard, now earning its coverage on arms a GESTURE can reach — slice 4 could only drive `menu`.
    for (int stage = 0; stage < 2; ++stage) {                 // 0 = the confirmation, 1 = the result
        CreateFix f; const auto s = prov_snap();
        f.prov.answer = created_answer(0x00A1B2C3u);
        CHECK(open_create_confirm(f, s));
        if (stage == 1) {
            f.m.on_gesture(Gesture::short_press, s);
            f.m.on_gesture(Gesture::double_press, s);
            CHECK(f.m.state().provisioning == Provision::create_result);
        } else {
            f.m.on_gesture(Gesture::short_press, s);          // ...with CREATE selected: the destructive choice
            CHECK(f.m.state().prov_confirm == ProvConfirm::confirm);
        }
        const int before = f.prov.calls;
        // ⛔ AN UNCONFIRMED DESTRUCTIVE ACTION DOES NOT SURVIVE AN ALARM (§3.6.5 rule 1): the overlay owns the body,
        //    so a confirmation left standing underneath would be invisible AND still holding the press.
        f.m.on_gesture(Gesture::long_arm, s);
        CHECK(f.m.state().provisioning == Provision::closed);
        CHECK(f.m.state().settings == Settings::browsing);
        CHECK(f.m.state().prov_confirm == ProvConfirm::back);
        CHECK(f.prov.calls == before);                        // ⛔ the alarm confirmed NOTHING on its way past
        f.m.on_gesture(Gesture::long_cancel, s);
        CHECK(f.m.state().provisioning == Provision::closed); // ...and a cancel does not bring it back
        // ⓘ `cancelled` self-clears after `kCancelledMs` (it sent nothing, so it is not sticky) — the tick below is
        //   what returns the panel to the ordinary screens, exactly as the shipped loop does.
        const auto s2 = prov_snap(/*create_team=*/true, /*join_static=*/true, s.now_ms + kCancelledMs + 1);
        f.m.on_tick(s2);
        CHECK(f.m.emergency() == Emergency::idle);
        // ★ RE-ENTERING opens the MENU at its first row — ⛔ never the confirmation the operator abandoned.
        CHECK(open_provision(f.m, s2));
        CHECK(f.m.state().provisioning == Provision::menu);
        CHECK(f.m.state().prov_answer.outcome == UiProvOutcome::none);
    }
}


// ================================================================ §UI-15 slice 6 — the STATIC-JOIN FLOW (§3.6.3)
// ★★ WHAT THIS BLOCK MEASURES: the FLOW, the LANDINGS and the ASYNCHRONOUS OUTCOME as the MODEL sees them — which
//    screen a press reaches, how many times the act runs, what a BACK costs (nothing, on every arm), and which
//    pushes may complete the waiting screen. ⛔ IT DOES NOT MEASURE THE ADAPTER (the ONE Hz -> MHz conversion and the
//    verdict mapping are `test/test_firmware_ui_prov.cpp`'s, driven against the REAL transaction) and ⛔ NOT the
//    correlation RULE itself (`test/test_firmware_ui_join.cpp`'s, term by term).
namespace {
// A `UiJoinList` in the ORDINARY state, with a preset in each slot named by the mask. ⚠ The frequency is the build's
// own carrier: 869.4625 MHz = 869462500 Hz exactly, ⛔ not representable in integral kHz.
UiJoinList ok_join_list(uint8_t slot_mask, uint8_t layer = 4) {
    UiJoinList l{};
    l.served = true;
    l.res.verdict = mrfw::ProfileVerdict::ok;
    mrnv::join_blob_init(l.rec);
    for (uint8_t i = 0; i < mrnv::kJoinProfiles; ++i) {
        if (!(slot_mask & (1u << i))) continue;
        l.rec.prof[i].present    = 1;
        l.rec.prof[i].layer      = layer;
        l.rec.prof[i].routing_sf = 9;
        l.rec.prof[i].freq_hz    = 869462500u;
        l.rec.prof[i].bw_hz      = 125000u;
    }
    return l;
}
UiJoinList fault_join_list(mrfw::ProfileErr e) {
    UiJoinList l{};
    l.served = true;
    l.res.verdict = mrfw::ProfileVerdict::refused;
    l.res.err     = e;
    mrnv::join_blob_init(l.rec);
    return l;
}
UiJoinList absent_join_list() {
    UiJoinList l{};
    l.served = true;
    l.res.verdict = mrfw::ProfileVerdict::empty;
    mrnv::join_blob_init(l.rec);
    return l;
}
// Cycle the SELECT cursor onto a named row. ⛔ Never a hardcoded index: the rows are built from the `present` flags,
// so an index means a different profile in a different record (§B66). BOUNDED, so a missing row fails the caller.
bool join_cursor_to(UiModel& m, JoinSelRow want) {
    const JoinSelList l = join_sel_rows(m.state().join_list);
    const auto s = prov_snap();
    for (uint8_t i = 0; i < uint8_t(l.n + 1u); ++i) {
        JoinSelRow r{};
        if (l.at(m.state().cursor, r) && r.back == want.back && (want.back || r.slot1 == want.slot1)) return true;
        m.on_gesture(Gesture::short_press, s);
    }
    return false;
}
UiProvAnswer joining_answer() { UiProvAnswer a{}; a.outcome = UiProvOutcome::joining; return a; }
// Open PROVISION -> JOIN NETWORK -> the slot list. The caller ASSERTS the landing.
// ⓘ TOLERANT OF ALREADY BEING IN THE SUB-VIEW's MENU, deliberately: `open_provision` walks the SETTINGS cycle, which
//   a model that is ALREADY in provisioning cannot do (the sub-view owns the press). Several cases below leave the
//   list and come back, which is exactly the round trip the operator makes.
bool open_join_select(CreateFix& f, const UiSnapshot& s) {
    if (f.m.state().provisioning != Provision::menu && !open_provision(f.m, s)) return false;
    if (!prov_cursor_to(f.m, s, ProvRow::join_static)) return false;
    f.m.on_gesture(Gesture::double_press, s);
    return f.m.state().provisioning == Provision::join_select;
}
// ...and on to the confirmation for the named slot.
bool open_join_confirm(CreateFix& f, const UiSnapshot& s, uint8_t slot1) {
    if (!open_join_select(f, s)) return false;
    if (!join_cursor_to(f.m, JoinSelRow{slot1, false})) return false;
    f.m.on_gesture(Gesture::double_press, s);
    return f.m.state().provisioning == Provision::join_confirm;
}
// Drive it all the way to `join_waiting` with a scripted `joining` verdict.
bool start_join(CreateFix& f, const UiSnapshot& s, uint8_t slot1 = 1) {
    f.prov.join_answer = joining_answer();
    if (!open_join_confirm(f, s, slot1)) return false;
    f.m.on_gesture(Gesture::short_press, s);          // BACK -> JOIN: the deliberate short-then-double
    f.m.on_gesture(Gesture::double_press, s);
    return f.m.state().provisioning == Provision::join_waiting;
}
// A push, built field by field so every case can move exactly one fact.
MESHROUTE_NS::Push adopt_push(uint8_t leaf, uint8_t dst) {
    MESHROUTE_NS::Push pu{};
    pu.kind = MESHROUTE_NS::PushKind::join_adopted;
    pu.layer_id = leaf;
    pu.dst = dst;
    return pu;
}
}  // namespace

TEST_CASE("ui15-entry: activating JOIN NETWORK opens the SLOT LIST and reads `/mrjoin` EXACTLY ONCE") {
    // ⚠ RE-AIMED 2026-08-20 (§UI-15 slice 6), and the withdrawal is recorded rather than the case deleted: this used
    //   to be `ui15-pending`, which asserted that activating JOIN NETWORK *"does NOTHING — the static-join flow is
    //   slice 6's"*. That was [[B222]]'s pin and it was TRUE until this slice landed the flow behind the row; keeping
    //   it would pin the ABSENCE of the very thing being built. ★ The property it really carried — a transition lands
    //   WITH its flow — survives as the assertion that the list is READ on the transition, so the screen the entry
    //   opens is never an empty one it could not have populated.
    CreateFix f; const auto s = prov_snap();
    f.prov.list = ok_join_list(/*slots=*/0b0101);             // slots 1 and 3 hold presets
    CHECK(open_provision(f.m, s));
    CHECK(prov_cursor_to(f.m, s, ProvRow::join_static));
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().provisioning == Provision::join_select);
    CHECK(f.m.state().settings == Settings::provisioning);
    // ★★ THE ONE READ, ON THE TRANSITION. ⛔ Not per tick and not per page: `profiles()` reaches flash, and U8g2
    //    replays the whole scene eight times per frame.
    CHECK(f.prov.list_calls == 1);
    for (int i = 0; i < 5; ++i) f.m.on_tick(s);
    CHECK(f.prov.list_calls == 1);
    // ★ THE ROWS ARE THE PRESENT SLOTS, BY SLOT NUMBER, plus the unconditional BACK.
    const JoinSelList l = join_sel_rows(f.m.state().join_list);
    CHECK(l.n == 3);
    JoinSelRow r{};
    CHECK(l.at(0, r)); CHECK(r.slot1 == 1);
    CHECK(l.at(1, r)); CHECK(r.slot1 == 3);
    CHECK(l.at(2, r)); CHECK(r.back);
    // ⛔ AND OPENING IT PERFORMED NOTHING: no transaction, no durable write, no live apply, and no note.
    CHECK(f.prov.calls == 0);
    CHECK(f.store.writes == 0);
    CHECK(f.live.applies == 0);
    CHECK(f.m.state().prov_block == ProvBlock::none);
    CHECK(strcmp(settings_note(f.m.state()), "") == 0);
    // ...and BACK from the list returns to the child menu, so the entry cannot strand the operator.
    CHECK(join_cursor_to(f.m, JoinSelRow{0, true}));
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().provisioning == Provision::menu);
    CHECK(f.prov.calls == 0);
}

TEST_CASE("ui15-join: the STORE MATRIX reaches the panel — four states, four texts, and NO slot to join from") {
    // ★★★★ PIN 1. The three faults are three different texts and `io_failed` never reads as absent or as invalid —
    //      the distinction [[B218]] bought. ⛔ AND NONE OF THEM OFFERS A SLOT: a store that could not be read must
    //      not be joined from, so the only row is BACK.
    struct Arm { UiJoinList list; const char* head; const char* detail; };
    const Arm arms[] = {
        { absent_join_list(),                                  "NO PROFILES",     "" },
        { fault_join_list(mrfw::ProfileErr::store_invalid),    "PROFILE STORE",   "INVALID" },
        { fault_join_list(mrfw::ProfileErr::store_io_failed),  "STORAGE FAILURE", "CHECK faults" },
        { ok_join_list(/*slots=*/0),                           "NO PROFILES",     "" },   // valid, four empty slots
    };
    for (const Arm& a : arms) {
        CreateFix f; const auto s = prov_snap();
        f.prov.list = a.list;
        CHECK(open_join_select(f, s));
        CHECK(strcmp(join_store_head(f.m.state().join_list), a.head) == 0);
        CHECK(strcmp(join_store_detail(f.m.state().join_list), a.detail) == 0);
        const JoinSelList l = join_sel_rows(f.m.state().join_list);
        CHECK(l.n == 1);
        JoinSelRow r{};
        CHECK(l.at(0, r));
        CHECK(r.back == true);
        // ⛔ A `double` ON THE ONLY ROW LEAVES — it cannot start anything, because there is nothing to start.
        f.m.on_gesture(Gesture::double_press, s);
        CHECK(f.m.state().provisioning == Provision::menu);
        CHECK(f.prov.calls == 0);
        CHECK(f.store.writes == 0);
        CHECK(f.live.applies == 0);
    }
    // ⓘ A MISSING SEAM IS A FOURTH ANSWER AGAIN, and it is reached without a `CreateFix`: an unattached model.
    CfgFix f; const auto s = prov_snap();
    CHECK(open_provision(f.m, s));
    CHECK(prov_cursor_to(f.m, s, ProvRow::join_static));
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().provisioning == Provision::join_select);
    CHECK(f.m.state().join_list.served == false);
    CHECK(strcmp(join_store_head(f.m.state().join_list), "NO JOIN SERVICE") == 0);
    CHECK(join_sel_rows(f.m.state().join_list).n == 1);
}

TEST_CASE("ui15-join: BACK at select/confirm/waiting/result drives ZERO — and CONFIRM drives EXACTLY ONE") {
    // ★★★★ PIN 2, counted on all three instruments rather than argued. ⓘ `waiting` and `result` are reached through
    //      a real CONFIRM, so the "exactly one" is asserted on the same run as the "zero afterwards".
    CreateFix f; const auto s = prov_snap();
    // ⚠ SLOTS **1 AND 3**, DELIBERATELY: the pick below then sits at ROW INDEX 1 while naming SLOT 3, so a model
    //   that activated the index would join a different profile (§B66). With slots 1 and 2 the two numbers coincide
    //   and the defect is invisible — which is exactly what a first version of this case measured (nothing).
    f.prov.list = ok_join_list(0b0101);
    // --- BACK from the SELECT list
    CHECK(open_join_select(f, s));
    CHECK(join_cursor_to(f.m, JoinSelRow{0, true}));
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().provisioning == Provision::menu);
    CHECK(f.prov.calls == 0);
    // --- BACK from the CONFIRMATION returns to the LIST (⛔ not the menu: it is the screen he was choosing on)
    CHECK(open_join_confirm(f, s, 1));
    CHECK(f.m.state().prov_confirm == ProvConfirm::back);     // §3.6.3: opens on the SAFE action
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().provisioning == Provision::join_select);
    CHECK(f.prov.calls == 0);
    CHECK(f.store.writes == 0);
    CHECK(f.live.applies == 0);
    // --- CONFIRM: exactly ONE, and the profile handed over is the one the SELECTED slot holds
    f.prov.join_answer = joining_answer();
    CHECK(join_cursor_to(f.m, JoinSelRow{3, false}));
    // ★★ THE ROW INDEX AND THE SLOT NUMBER DIFFER HERE, AND THAT IS THE WHOLE POINT OF THE 1-AND-3 FIXTURE: the pick
    //    the model records must be the SLOT (§B66), so a model that stored the index would join slot 2's profile.
    //    ⓘ Read BEFORE the activation, because `enter_provision` re-anchors the cursor to each arm's first row.
    CHECK(f.m.state().cursor == 1);
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().provisioning == Provision::join_confirm);
    CHECK(f.m.state().join_sel == 3);
    f.m.on_gesture(Gesture::short_press, s);
    CHECK(f.m.state().prov_confirm == ProvConfirm::confirm);
    CHECK(f.prov.calls == 0);                                 // ⛔ toggling is not acting
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.prov.calls == 1);                                 // ★ EXACTLY ONE
    CHECK(f.prov.last_op == UiProvOp::join_static);
    CHECK(f.prov.last_join.present == 1);                     // ★ WHAT WAS SHOWN IS WHAT WAS JOINED (slot 3's record)
    CHECK(f.prov.last_join.freq_hz == 869462500u);
    // ★★★ §8 PIN 2: at the instant the act ran the screen was still the CONFIRMATION and no result text existed.
    CHECK(f.prov.arm_at_call == Provision::join_confirm);
    CHECK(strcmp(f.prov.head_at_call, "") == 0);
    // --- BACK from WAITING and from the RESULT: still zero more
    CHECK(f.m.state().provisioning == Provision::join_waiting);
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().provisioning == Provision::menu);
    CHECK(f.prov.calls == 1);
    // ★ AND RE-ENTERING THE LIST RETIRES THE PICK ALONG WITH THE RECORD IT NAMED: the two are refreshed by the SAME
    //   call (`load_join_profiles`), because a slot number means nothing without the record it indexes — that pair
    //   going out of step is how a confirmation ends up showing one profile's values for another's selection.
    CHECK(f.m.state().join_sel == 3);                         // still the old pick while the menu is up...
    CHECK(open_join_select(f, s));
    CHECK(f.m.state().join_sel == 0);                         // ...and retired by the read that rebuilt the list
    CHECK(f.prov.list_calls == 3);
    CHECK(f.prov.calls == 1);
}

TEST_CASE("ui15-join: transaction success shows JOINING — ⛔ and NO `JOINED`-shaped text is reachable before an adopt") {
    // ★★★★ PIN 3, and it is the whole reason `joining` is a state of its own: a successful transaction has written
    //      once and STARTED DAD. Claiming membership there is the *"a success that isn't"* class, one plane over.
    CreateFix f; const auto s = prov_snap();
    f.prov.list = ok_join_list(0b0001);
    CHECK(start_join(f, s));
    CHECK(f.m.state().prov_answer.outcome == UiProvOutcome::joining);
    CHECK(strcmp(prov_result_head(f.m.state().prov_answer), "JOINING") == 0);
    CHECK(strcmp(join_wait_head(f.m.state().join_still), "JOINING") == 0);
    CHECK(f.m.state().join_still == false);
    // ⛔ NO ARM OF EITHER STRING FUNCTION SAYS `JOINED` — walked over every outcome, so the property is about the
    //    VOCABULARY and not about the one state this case happens to be in.
    const UiProvOutcome all[] = { UiProvOutcome::none, UiProvOutcome::created, UiProvOutcome::phy_differs,
                                  UiProvOutcome::save_failed, UiProvOutcome::refused, UiProvOutcome::joining,
                                  UiProvOutcome::adopted, UiProvOutcome::join_refused };
    for (UiProvOutcome o : all) {
        UiProvAnswer a{}; a.outcome = o; a.reason = "nv_load_failed";
        CHECK(strstr(prov_result_head(a), "JOINED") == nullptr);
        CHECK(strstr(prov_result_detail(a), "JOINED") == nullptr);
        CHECK(strlen(prov_result_head(a)) <= 19u);            // ...and every one still fits the rail's body
        CHECK(strlen(prov_result_detail(a)) <= 19u);
    }
    // ★ THE JOIN OUTCOMES SAY DIFFERENT THINGS FROM THE CREATE ONES: a collapse would make the panel unable to say
    //   which operation answered.
    UiProvAnswer jr{}; jr.outcome = UiProvOutcome::join_refused; jr.reason = "invalid_sf";
    CHECK(strcmp(prov_result_head(jr), "JOIN REFUSED") == 0);
    CHECK(strcmp(prov_result_detail(jr), "invalid_sf") == 0);
    UiProvAnswer ad{}; ad.outcome = UiProvOutcome::adopted; ad.node_id = 42;
    CHECK(strcmp(prov_result_head(ad), "ADOPTED") == 0);
    CHECK(strcmp(prov_result_detail(ad), "") == 0);           // its second row is the node ID, a VALUE
}

TEST_CASE("ui15-join: a refusal or a failed save LANDS ON THE RESULT — and arms no session at all") {
    for (int arm = 0; arm < 2; ++arm) {
        CreateFix f; const auto s = prov_snap();
        f.prov.list = ok_join_list(0b0001);
        f.prov.join_answer = UiProvAnswer{};
        if (arm == 0) { f.prov.join_answer.outcome = UiProvOutcome::join_refused;
                        f.prov.join_answer.reason  = "invalid_bw"; }
        else          { f.prov.join_answer.outcome = UiProvOutcome::save_failed; }
        CHECK(open_join_confirm(f, s, 1));
        f.m.on_gesture(Gesture::short_press, s);
        f.m.on_gesture(Gesture::double_press, s);
        CHECK(f.m.state().provisioning == Provision::join_result);
        CHECK(f.prov.calls == 1);
        // ⛔⛔ NO SESSION IS ARMED ON A NON-`joining` VERDICT: a correlation window open for an operation that never
        //    started is the mirror of the "success that isn't", and a later BOOT DAD would then complete a screen.
        CHECK(f.m.join_session_active() == false);
        f.m.on_join_push(adopt_push(4, 42), 4, 42);           // the perfectly-shaped adopt
        CHECK(f.m.state().provisioning == Provision::join_result);
        CHECK(f.m.state().prov_answer.outcome != UiProvOutcome::adopted);
    }
    // ⓘ A NULL SEAM REFUSES OUT LOUD (C2) rather than doing nothing — the dead-button complaint one screen over.
    CfgFix f; const auto s = prov_snap();
    CHECK(open_provision(f.m, s));
    CHECK(prov_cursor_to(f.m, s, ProvRow::join_static));
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().provisioning == Provision::join_select);
    CHECK(join_sel_rows(f.m.state().join_list).n == 1);       // only BACK, so the act is not even reachable
    CHECK(f.m.join_session_active() == false);
}

TEST_CASE("ui15-join: ★★ ONLY A CORRELATED ADOPT COMPLETES — the four uncorrelated shapes, by name") {
    // ★★★★ PIN 4, driven THROUGH THE MODEL: each shape leaves the screen on `JOINING`, leaves the session ARMED, and
    //      says nothing. ⓘ The rule itself is pinned term by term in `test/test_firmware_ui_join.cpp`; this is the
    //      other end — that the model consults it and changes nothing when it answers no.
    struct Shape { const char* name; MESHROUTE_NS::Push pu; uint8_t persisted; uint8_t canonical; bool active; };
    const Shape shapes[] = {
        // a BOOT DAD: perfectly formed, but nobody started a UI join (term 1). Driven by clearing the session below.
        { "boot DAD (no active session)",  adopt_push(4, 42), 4, 42, false },
        // ★★ a HEAL RE-ADOPT on the layer the record CURRENTLY holds, while the screen waits for another (term 2).
        //    ⛔ THE LAYERS SHARE A NIBBLE ON PURPOSE (20 & 0x0F == 4): the two operations' pushes are then IDENTICAL
        //    ON THE WIRE, so nothing but the record's FULL byte can separate them — which is what term 2 is for.
        { "heal re-adopt on another layer", adopt_push(4, 42), 20, 42, true },
        { "wrong layer nibble",             adopt_push(5, 42), 4, 42, true },
        { "a foreign dst",                  adopt_push(4, 43), 4, 42, true },
        { "a ZERO dst",                     adopt_push(4, 0),  4, 0,  true },
    };
    for (const Shape& sh : shapes) {
        CreateFix f; const auto s = prov_snap();
        f.prov.list = ok_join_list(0b0001);
        CHECK(start_join(f, s));
        CHECK(f.m.join_session_active() == true);
        if (!sh.active) {                                     // the boot-DAD shape: no session behind the push
            f.m.on_gesture(Gesture::double_press, s);         // leave the screen...
            CHECK(f.m.state().provisioning == Provision::menu);
        }
        const Provision before = f.m.state().provisioning;
        f.m.on_join_push(sh.pu, sh.persisted, sh.canonical);
        // ⛔ NOTHING MOVED: not the screen, not the answer, not the session.
        CHECK(f.m.state().provisioning == before);
        CHECK(f.m.state().prov_answer.outcome != UiProvOutcome::adopted);
        CHECK(f.m.join_session_active() == sh.active);        // ...and an ARMED session is still armed
        if (sh.active) CHECK(strcmp(join_wait_head(f.m.state().join_still), "JOINING") == 0);
    }
    // ★ THE POSITIVE ARM, on the same fixture: the identical push with every fact correct COMPLETES.
    CreateFix f; const auto s = prov_snap();
    f.prov.list = ok_join_list(0b0001);
    CHECK(start_join(f, s));
    f.m.on_join_push(adopt_push(4, 42), 4, 42);
    CHECK(f.m.state().provisioning == Provision::join_result);
    CHECK(f.m.state().prov_answer.outcome == UiProvOutcome::adopted);
    CHECK(f.m.state().prov_answer.node_id == 42);
    CHECK(strcmp(prov_result_head(f.m.state().prov_answer), "ADOPTED") == 0);
    CHECK(f.m.join_session_active() == false);                // ★ a correlated adopt is the session's ONE ordinary end
    CHECK(f.prov.calls == 1);                                 // ⛔ and it re-ran nothing
}

TEST_CASE("ui15-join: ⛔ EVERY `JoinRefuseReason` IS IGNORED FOR COMPLETION — the screen stays JOINING") {
    // ★★★★ PIN 5 / plan §2.3 rule 6: the shared push cannot separate static DAD, team DAD and an unrelated
    //      wire-version OBSERVATION ABOUT ANOTHER PEER, so ⛔ NO reason terminally fails UI-15 v1.
    const MESHROUTE_NS::JoinRefuseReason reasons[] = {
        MESHROUTE_NS::JoinRefuseReason::wire_version, MESHROUTE_NS::JoinRefuseReason::leaf_full,
        MESHROUTE_NS::JoinRefuseReason::phy_mismatch, MESHROUTE_NS::JoinRefuseReason::sf_list_mismatch,
    };
    for (MESHROUTE_NS::JoinRefuseReason r : reasons) {
        CreateFix f; const auto s = prov_snap();
        f.prov.list = ok_join_list(0b0001);
        CHECK(start_join(f, s));
        MESHROUTE_NS::Push pu = adopt_push(4, 42);            // ...otherwise PERFECTLY correlated
        pu.kind = MESHROUTE_NS::PushKind::join_refused;
        pu.join_reason = r;
        f.m.on_join_push(pu, 4, 42);
        // ⛔ STILL JOINING, on every authority: the arm, the answer and the session.
        CHECK(f.m.state().provisioning == Provision::join_waiting);
        CHECK(f.m.state().prov_answer.outcome == UiProvOutcome::joining);
        CHECK(strcmp(join_wait_head(f.m.state().join_still), "JOINING") == 0);
        CHECK(f.m.join_session_active() == true);
        // ...and the real adopt that follows STILL completes, so a refusal did not poison the session either.
        f.m.on_join_push(adopt_push(4, 42), 4, 42);
        CHECK(f.m.state().provisioning == Provision::join_result);
        CHECK(f.m.state().prov_answer.outcome == UiProvOutcome::adopted);
    }
}

TEST_CASE("ui15-join: 60 s is a WORD CHANGE — ⛔ never a failure — and BACK from WAITING cancels nothing") {
    // ★★★★ PIN 6. Normal adoption is ~23 s and a conflict/retry reaches ~53 s, and retries are NOT finitely bounded.
    CreateFix f; auto s = prov_snap();
    f.prov.list = ok_join_list(0b0001);
    CHECK(start_join(f, s));
    const int writes_at_start = f.store.writes;
    CHECK(f.m.state().join_still == false);
    // ...one millisecond short of the deadline is still `JOINING`.
    auto s1 = prov_snap(true, true, s.now_ms + kJoinStillMs - 1);
    f.m.on_tick(s1);
    CHECK(f.m.state().join_still == false);
    CHECK(strcmp(join_wait_head(f.m.state().join_still), "JOINING") == 0);
    // ...and at the deadline the WORD changes and ⛔ NOTHING ELSE DOES.
    auto s2 = prov_snap(true, true, s.now_ms + kJoinStillMs);
    f.m.on_tick(s2);
    CHECK(f.m.state().join_still == true);
    CHECK(strcmp(join_wait_head(f.m.state().join_still), "STILL JOINING") == 0);
    CHECK(f.m.state().provisioning == Provision::join_waiting);      // ⛔ NOT a result, NOT a failure
    CHECK(f.m.state().prov_answer.outcome == UiProvOutcome::joining);
    CHECK(f.m.join_session_active() == true);
    CHECK(f.prov.calls == 1);                                        // ⛔ nothing was retried
    CHECK(f.store.writes == writes_at_start);
    // ★ AND IT ASKS FOR A REPAINT AT THE EDGE, exactly once: a word that changed without a `dirty` would be true and
    //   INVISIBLE (`FrameGate::step` answers `idle` on a clean model).
    f.m.clear_dirty();
    f.m.on_tick(prov_snap(true, true, s.now_ms + kJoinStillMs + 5000));
    CHECK(f.m.state().dirty == false);                               // ⛔ not every tick past the edge
    // ⓘ AND THE PANEL HAS BLANKED BY NOW, which is honest rather than incidental: `kBlankMs` is 15 s and the waiting
    //   screen needs no attention. ⛔ THE BLANK CANCELS NOTHING EITHER — the same rule BACK obeys, arriving from the
    //   timer instead of the button.
    CHECK(f.m.state().blanked == true);
    CHECK(f.m.join_session_active() == true);
    CHECK(f.m.state().provisioning == Provision::join_waiting);
    f.m.on_gesture(Gesture::short_press, s2);                        // the waking press is CONSUMED (§B102)
    CHECK(f.m.state().blanked == false);
    CHECK(f.m.state().provisioning == Provision::join_waiting);
    // ★★★ PLAN §2.3 RULE 4: BACK during JOINING only LEAVES THE SCREEN. ⛔ It cancels nothing, rolls nothing back and
    //     costs no write — the operation is already persisted and DAD is running.
    f.m.on_gesture(Gesture::double_press, s2);
    CHECK(f.m.state().provisioning == Provision::menu);
    CHECK(f.m.join_session_active() == true);
    CHECK(f.store.writes == writes_at_start);
    CHECK(f.live.applies == 0);
    CHECK(f.prov.calls == 1);
    // ...and the adopt that arrives afterwards still ENDS the session, even though no screen is there to show it.
    f.m.on_join_push(adopt_push(4, 42), 4, 42);
    CHECK(f.m.join_session_active() == false);
    CHECK(f.m.state().provisioning == Provision::menu);              // ⛔ a push may NOT navigate the panel
}

TEST_CASE("ui15-join: the FOUR join arms are CLOSED by leaving and PRE-EMPTED by the alarm — and the session SURVIVES") {
    // ★★★★ PIN 7, over the arms this slice makes reachable. ⓘ AND THE ONE THING THE PRE-EMPTION MUST NOT DO: closing
    //      the SCREEN may not end a persisted join — §3.6.5's words are *"an UNCONFIRMED destructive action does not
    //      survive"*, and a written, DAD-ing join is neither unconfirmed nor cancellable from a screen.
    for (int stage = 0; stage < 4; ++stage) {
        CreateFix f; const auto s = prov_snap();
        f.prov.list = ok_join_list(0b0001);
        f.prov.join_answer = joining_answer();
        switch (stage) {
            case 0: CHECK(open_join_select(f, s)); break;
            case 1: CHECK(open_join_confirm(f, s, 1));
                    f.m.on_gesture(Gesture::short_press, s);          // ...with JOIN selected: the destructive choice
                    CHECK(f.m.state().prov_confirm == ProvConfirm::confirm); break;
            case 2: CHECK(start_join(f, s)); break;
            case 3: CHECK(start_join(f, s));
                    f.m.on_join_push(adopt_push(4, 42), 4, 42);
                    CHECK(f.m.state().provisioning == Provision::join_result); break;
        }
        const int before = f.prov.calls;
        const bool session_before = f.m.join_session_active();
        f.m.on_gesture(Gesture::long_arm, s);
        CHECK(f.m.state().provisioning == Provision::closed);
        CHECK(f.m.state().settings == Settings::browsing);
        CHECK(f.m.state().prov_confirm == ProvConfirm::back);
        CHECK(f.prov.calls == before);                                // ⛔ the alarm confirmed NOTHING on its way past
        CHECK(f.store.writes == 0);
        // ⛔⛔ AND THE SESSION IS UNTOUCHED: the alarm pre-empts the SCREEN, not the operation.
        CHECK(f.m.join_session_active() == session_before);
        f.m.on_gesture(Gesture::long_cancel, s);
        CHECK(f.m.state().provisioning == Provision::closed);         // ...and a cancel does not bring it back
        // ★ RE-ENTERING opens the MENU at its first row — ⛔ never the confirmation the operator abandoned.
        const auto s2 = prov_snap(true, true, s.now_ms + kCancelledMs + 1);
        f.m.on_tick(s2);
        CHECK(f.m.emergency() == Emergency::idle);
        CHECK(open_provision(f.m, s2));
        CHECK(f.m.state().provisioning == Provision::menu);
        CHECK(f.m.state().prov_answer.outcome == UiProvOutcome::none);
    }
}

TEST_CASE("ui15-join: ★★ TRAP 2 — a join at LAYER 17 correlates through the NIBBLE and COMPLETES") {
    // ★★★★ PIN 8, driven THROUGH THE SCREEN at a value above 15: the request persists the FULL byte, the wire carries
    //      the NIBBLE, and v3's full==live comparison would have made this wait for ever.
    CreateFix f; const auto s = prov_snap();
    f.prov.list = ok_join_list(0b0001, /*layer=*/17);
    CHECK(start_join(f, s));
    CHECK(f.prov.last_join.layer == 17);                              // ★ the FULL byte reached the adapter
    // ⛔ THE PUSH CARRYING THE FULL BYTE AS ITS LEAF IS NOT OURS (no leaf nibble can be 17).
    f.m.on_join_push(adopt_push(17, 42), 17, 42);
    CHECK(f.m.state().provisioning == Provision::join_waiting);
    // ★ THE NIBBLE ONE COMPLETES — with `/mrcfg` still holding the FULL 17 (persisted <-> persisted).
    f.m.on_join_push(adopt_push(1, 42), 17, 42);
    CHECK(f.m.state().provisioning == Provision::join_result);
    CHECK(f.m.state().prov_answer.outcome == UiProvOutcome::adopted);
    CHECK(f.m.state().prov_answer.node_id == 42);
}
// ---------------------------------------------------------------------------------------------- the LONG gesture
TEST_CASE("ui14-long: `long_arm` LEAVES THE EDITOR — and a `long_cancel` does not bring it back") {
    CfgFix f; const auto s = cfg_snap();
    to_settings_menu(f.m, s);
    CHECK(cursor_to(f.m, s, CfgRow::e2e_dm));
    f.m.on_gesture(Gesture::double_press, s);
    f.m.on_gesture(Gesture::short_press, s);
    CHECK(f.m.state().settings == Settings::editing);
    const uint8_t cur = f.m.state().cursor;
    f.m.on_gesture(Gesture::long_arm, s);
    // ★★ CLOSED AT `long_arm`, NOT AT `long_fire` — §3.6.2's "the long gesture ALWAYS leaves the editor", and the
    //    exact correction §UI-7D made one modal over.
    CHECK(f.m.state().settings == Settings::browsing);
    CHECK(f.m.emergency() == Emergency::arming);
    f.m.on_gesture(Gesture::long_cancel, s);
    CHECK(f.m.emergency() == Emergency::cancelled);
    CHECK(f.m.state().settings == Settings::browsing);       // ⛔ the editor did NOT come back...
    CHECK(f.m.state().cursor == cur);                        // ...and the cursor is still on the VALUE row it was on
    CfgRow r{};
    CHECK(f.m.settings_row_list(s).at(f.m.state().cursor, r));
    CHECK(r == CfgRow::e2e_dm);                              // ⛔ never a destructive row
    // ★ the draft edit SURVIVES: it is neither confirmed nor destructive (§3.6.5), so nothing may revert it
    CHECK(f.svc.draft().at(mrfw::CfgField::e2e_dm) == 1);
    CHECK(f.store.writes == 0);
}

TEST_CASE("ui14-long: `long_fire` from the editor arms the alarm and the editor is already gone") {
    CfgFix f; const auto s = cfg_snap();
    to_settings_menu(f.m, s);
    CHECK(cursor_to(f.m, s, CfgRow::e2e_dm));
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().settings == Settings::editing);
    f.m.on_gesture(Gesture::long_arm, s);
    f.m.on_gesture(Gesture::long_fire, s);
    CHECK(f.m.emergency() == Emergency::firing);
    CHECK(f.m.state().settings == Settings::browsing);
    SendReq req{};
    const bool got = f.m.take_send_request(req);             // ⚠ DRAINS — one call, into a local (§B70)
    CHECK(got == true);
    CHECK(req.kind == SendKind::emergency);
}

TEST_CASE("ui14-long: the emergency overlay ABSORBS a double over SETTINGS (ledger §1.4)") {
    CfgFix f; const auto s = cfg_snap();
    to_settings_menu(f.m, s);
    CHECK(cursor_to(f.m, s, CfgRow::discard));
    f.m.on_gesture(Gesture::long_arm, s);
    CHECK(f.m.emergency() == Emergency::arming);
    f.m.on_gesture(Gesture::double_press, s);                // ⛔ absorbed ENTIRELY — it must not reach DISCARD
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().settings == Settings::browsing);
    CHECK(f.m.emergency() == Emergency::arming);
    CHECK(f.store.writes == 0);
    CHECK(f.svc.config_unsaved() == false);
}

// ---------------------------------------------------------------------------------------------- the refused open
// ★★★★ RETARGETED 2026-08-20 (QG, on [[B232]]) — AND THE WITHDRAWN SUBJECT IS RECORDED RATHER THAN DELETED. Both
//      cases below used to ENTER `browsing` on an unopened service and WALK ITS ROWS, asserting each activation
//      refused. ⛔ THAT IS A STATE THE RENDERER CANNOT PRODUCE: `draw_settings_screen` prints `CFG UNAVAILABLE` and
//      RETURNS while `open` is false, so every row is INVISIBLE — the cases were driving a cursor over rows nothing
//      draws, which is [[B232]]'s own multi-press defect one double-press deep.
// ⇒ WHAT THEY MEASURE NOW: the `double` LEAVES SETTINGS CLOSED, the closed view keeps its unavailable state, and the
//   screen still passes in ONE short press. ⓘ The refusals they used to assert are unchanged in the source and are
//   now UNREACHABLE by construction — see `settings_activate`'s note, which says so in code.
TEST_CASE("ui14-open: a store that cannot produce a record REFUSES to open, and the MENU never opens") {
    UiFakeStore store; UiFakeLive live;
    store.can_load = false;
    mrfw::ConfigService svc{store, live};
    UiModel m; m.attach_config(svc);
    const auto s = cfg_snap();
    to_settings(m, s);                                       // ⚠ the WALK ONLY — the entry press is the subject
    CHECK(m.state().screen == Screen::settings);
    CHECK(svc.is_open() == false);                           // ⛔ the panel says CFG UNAVAILABLE and offers nothing
    CHECK(m.state().settings == Settings::closed);
    // ★★★ THE GUARD: a `double` opens NOTHING, however many times it is pressed.
    m.on_gesture(Gesture::double_press, s); m.on_tick(s);
    CHECK(m.state().settings == Settings::closed);
    CHECK(m.state().screen == Screen::settings);
    m.on_gesture(Gesture::double_press, s); m.on_tick(s);
    CHECK(m.state().settings == Settings::closed);
    // ⛔ ...and it activated nothing on the way: no editor, no save, no durable write, no live apply
    CHECK(m.state().cfg_have_save == false);
    CHECK(store.writes == 0);
    CHECK(live.applies == 0);
    // ★ AND THE SCREEN STILL COSTS ONE PRESS — the whole point of the ruling holds when the store is broken too.
    m.on_gesture(Gesture::short_press, s);
    CHECK(m.state().screen == Screen::status);
    CHECK(m.state().settings == Settings::closed);
}

TEST_CASE("ui14-open: an UNATTACHED model opens NO menu and refuses every activation (fails closed)") {
    UiModel m; const auto s = cfg_snap();                     // ⛔ no attach_config at all
    to_settings(m, s);
    CHECK(m.state().screen == Screen::settings);
    CHECK(m.config() == nullptr);
    CHECK(m.state().settings == Settings::closed);
    m.on_gesture(Gesture::double_press, s); m.on_tick(s);     // ⛔ a null service fails closed, exactly as a shut one
    CHECK(m.state().settings == Settings::closed);
    CHECK(m.state().cfg_have_save == false);
    m.on_gesture(Gesture::short_press, s);                    // ...and one press still passes the screen
    CHECK(m.state().screen == Screen::status);
}

// ---------------------------------------------------------------------------------------------- the marker's bytes
TEST_CASE("ui14-marker: §3.3's three literals are three separate facts, and CONFLICT wins the marker row") {
    CHECK(strcmp(cfg_marker_text(/*unsaved=*/false, /*conflict=*/false), "") == 0);
    CHECK(strcmp(cfg_marker_text(true,  false), "CFG* UNSAVED") == 0);
    CHECK(strcmp(cfg_marker_text(false, true),  "CFG! RELOAD") == 0);
    // ★ a conflicted draft is BOTH, and the row says the one that BLOCKS the save — the other two facts have their
    //   own places (the SETTINGS marker row, and the RESTART row).
    CHECK(strcmp(cfg_marker_text(true, true), "CFG! RELOAD") == 0);
    // ...and the conflict string is the SERVICE's, not a copy
    CHECK(strcmp(cfg_marker_text(false, true), mrfw::cfg_save_panel(mrfw::CfgSave::conflict)) == 0);
    // ⛔⛔ WIDTH, RETARGETED 2026-08-16 (§CHROME-4 / §6.1) RATHER THAN DELETED. These three lines used to read
    //    `strlen("STATUS ") + strlen(marker) <= 21` — a bound on the STATUS **TITLE**, which design §6 removes in
    //    favour of the SETTINGS rail badge. ⛔ THE STRINGS THEMSELVES ARE NOT REMOVED: §6.1 is explicit that
    //    `CFG! RELOAD` remains required ACTIONABLE SETTINGS text, and it now occupies a row of its own on SETTINGS.
    //    ⇒ the fact these lines pin — *the marker fits the row it is drawn on* — survives; only the row changed, so
    //    the bound is re-derived to the rail's 19-column body instead of the old 21 minus a title.
    CHECK(strlen(cfg_marker_text(true, false)) <= 19u);
    CHECK(strlen(cfg_marker_text(false, true)) <= 19u);
    CHECK(strlen(kCfgRestartText) <= 19u);
}

// ★★★★ §CHROME-4 / design §7.3 — THE AUDIT, AS AN ASSERTION RATHER THAN A READING. §7.1 rule 3 requires every
//      ordinary body line to be PROVEN at or below 116 px = 19 small-font columns, and §7.3 says in as many words
//      that *"it fits" is a MEASUREMENT, not an assumption*. Every panel string this PURE unit owns is walked here.
//   ⛔ THE RENDERER'S FORMATS ARE RESTATED, NOT IMPORTED, and that is deliberate for the same reason the strip's
//      probe states its slot coordinates itself: a bound computed from the code under test agrees with a layout that
//      has drifted. `src/firmware_ui.cpp` is compiled by neither the native suite nor the simulator, so what is
//      pinned here are the COMPONENT widths its per-screen audit is derived from.
//   ⓘ NOT covered here, and named rather than implied: the literals that live in `src/firmware_ui.cpp` itself
//      (`QUEUED`, `SENT, waiting`, `NO RELAY HEARD`, `no teammates heard`, `double = pick text`, …) and the drawn
//      geometry. Those are measured end-to-end by `tools/probe_firmware_ui`'s P14f, which reads the x and the
//      pixel extent of every body draw the shipped renderer makes.
TEST_CASE("chrome4-audit: every PURE panel string fits the rail's 19-column body") {
    constexpr size_t kCols = 19;   // 116 px / 6 px per small-font column — design §3.2

    // ---- the SETTINGS rows. The renderer draws `%c%-8s %s` browsing and `%c%-8s[%s]` editing, so the row's width is
    //      `1 + max(8, label) + 1 + value` and `1 + max(8, label) + 2 + value`. ★ THE ARITHMETIC IS PER ROW, because
    //      the widest VALUE (`periodic`, 8) belongs to the shortest LABEL (`BLE`, 3): a label x value bound would be
    //      19 + something and would fail on a combination no row can reach.
    for (uint8_t i = 0; i < uint8_t(CfgRow::count); ++i) {
        const CfgRow r = CfgRow(i);
        const size_t label = strlen(settings_row_label(r));
        const size_t pad   = label > 8 ? label : 8;
        mrfw::CfgField f{};
        if (!cfg_row_field(r, f)) { CHECK(1u + label <= kCols); continue; }   // an ACTION row: `%c%s`
        // every value the FIELD's domain can hold, not only the two the menu offers (§3.6.2: a `periodic` persisted
        // by serial/BLE must still render honestly)
        for (uint8_t v = 0; v <= 3; ++v) {
            const size_t val = strlen(cfg_value_text(f, v));
            CHECK(1u + pad + 1u + val <= kCols);          // browsing
            CHECK(1u + pad + 2u + val <= kCols);          // editing, `[` + value + `]`
        }
    }
    // ---- the transient note and the two durable literals
    UiState st{}; st.cfg_have_save = true;
    for (mrfw::CfgSave r : { mrfw::CfgSave::saved, mrfw::CfgSave::saved_reboot, mrfw::CfgSave::no_change,
                             mrfw::CfgSave::invalid, mrfw::CfgSave::conflict, mrfw::CfgSave::nv_failed,
                             mrfw::CfgSave::not_open }) {
        st.cfg_save = r; CHECK(strlen(settings_note(st)) <= kCols);
    }
    st.cfg_have_save = false;
    st.cfg_refresh_failed = true;  CHECK(strlen(settings_note(st)) <= kCols);
    st.cfg_refresh_failed = false;
    // §UI-15 slice 4: §4's two refusal notes, and the PROVISION menu's own action rows (`%c%s`, like SETTINGS')
    for (ProvBlock b : { ProvBlock::none, ProvBlock::unsaved, ProvBlock::conflict }) {
        st.prov_block = b; CHECK(strlen(settings_note(st)) <= kCols);
    }
    st.prov_block = ProvBlock::none;
    for (uint8_t i = 0; i < kMaxProvRows; ++i) CHECK(1u + strlen(provision_row_label(ProvRow(i))) <= kCols);
    CHECK(strlen(kCfgRestartText) <= kCols);
    // §UI-15 slice 5: the CONFIRMATION's title and its two action rows (`%c%s`), and the RESULT's two lines — the
    // detail at its widest reachable value, which is the longest `ProvErr` name the panel can be handed.
    CHECK(1u + strlen(kProvCreateTitle) <= kCols);
    for (ProvConfirm a : { ProvConfirm::back, ProvConfirm::confirm })
        CHECK(1u + strlen(prov_confirm_label(a)) <= kCols);
    // ⚠ §UI-15 slice 6 WIDENED THIS LOOP to the three new outcomes, and the reason is the trap this whole audit
    //   exists around: a walk over a HARDCODED SUBSET of an enum measures only the arms somebody remembered. The
    //   widest reason a JOIN answer can carry is `nv_load_failed` (14), the longest `mrfw::join_err_name`.
    for (UiProvOutcome o : { UiProvOutcome::none, UiProvOutcome::created, UiProvOutcome::phy_differs,
                             UiProvOutcome::save_failed, UiProvOutcome::refused, UiProvOutcome::joining,
                             UiProvOutcome::adopted, UiProvOutcome::join_refused }) {
        UiProvAnswer a{}; a.outcome = o; a.reason = "no_mobile_plane";   // the widest mrfw::prov_err_name
        CHECK(strlen(prov_result_head(a)) <= kCols);
        CHECK(strlen(prov_result_detail(a)) <= kCols);
        a.reason = mrfw::join_err_name(mrfw::JoinErr::nv_load_failed);
        CHECK(strlen(prov_result_detail(a)) <= kCols);
    }
    // §UI-15 slice 6: the SELECT screen's four store notes, the slot labels (`%c%s` with a 12-byte label or the
    // `PROFILE n` default), the CONFIRMATION's two value lines at their widest reachable values and its two action
    // rows, the WAITING screen's two words and the RESULT's node line.
    {
        UiJoinList l{};                                        // unserved
        CHECK(strlen(join_store_head(l)) <= kCols);
        CHECK(strlen(join_store_detail(l)) <= kCols);
        l.served = true;
        for (mrfw::ProfileVerdict v : { mrfw::ProfileVerdict::ok, mrfw::ProfileVerdict::unchanged,
                                        mrfw::ProfileVerdict::empty, mrfw::ProfileVerdict::refused,
                                        mrfw::ProfileVerdict::nv_failed }) {
            for (mrfw::ProfileErr e : { mrfw::ProfileErr::none, mrfw::ProfileErr::store_invalid,
                                        mrfw::ProfileErr::store_io_failed }) {
                l.res.verdict = v; l.res.err = e;
                CHECK(strlen(join_store_head(l)) <= kCols);
                CHECK(strlen(join_store_detail(l)) <= kCols);
            }
        }
        mrnv::JoinProfile p{};
        p.present = 1; p.layer = 255; p.routing_sf = 12; p.freq_hz = 1000000000u; p.bw_hz = 500000u;
        memcpy(p.name, "ABCDEFGHIJKL", sizeof p.name); p.name_len = uint8_t(sizeof p.name);
        char tok[48];
        join_row_label(tok, sizeof tok, p, 4);   CHECK(1u + strlen(tok) <= kCols);
        p.name_len = 0;
        join_row_label(tok, sizeof tok, p, 4);   CHECK(1u + strlen(tok) <= kCols);   // the `PROFILE 4` default
        join_fmt_phy(tok, sizeof tok, p);        CHECK(strlen(tok) <= kCols);
        join_fmt_freq(tok, sizeof tok, p);       CHECK(strlen(tok) <= kCols);
        join_fmt_node(tok, sizeof tok, 255);     CHECK(strlen(tok) <= kCols);
        for (bool still : { false, true })  CHECK(strlen(join_wait_head(still)) <= kCols);
        for (bool c : { false, true })      CHECK(1u + strlen(join_confirm_label(c)) <= kCols);
    }

    // ---- the inbox detail's header, at its widest reachable expansion (`pages` is bounded by kDetailMaxPages)
    char l[48];
    inbox_detail_head(l, sizeof l, InboxKind::channel, 255, 255, uint8_t(kDetailMaxPages - 1), kDetailMaxPages, false);
    CHECK(strlen(l) <= kCols);
    inbox_detail_head(l, sizeof l, InboxKind::dm, 255, 0, uint8_t(kDetailMaxPages - 1), kDetailMaxPages, true);
    CHECK(strlen(l) <= kCols);
    // ...and the wrapped body rows themselves
    CHECK(size_t(kDetailCols) <= kCols);

    // ---- the compose presets, WITH their `>` marker (`%c%s`)
    for (uint8_t i = 0; i < kDmTextCount; ++i)      CHECK(1u + strlen(kDmTexts[i])      <= kCols);
    for (uint8_t i = 0; i < kChannelTextCount; ++i) CHECK(1u + strlen(kChannelTexts[i]) <= kCols);
    // ★★ §7.1 RULE 6 — *"two selectable preset strings must not become visually identical after clamping"*. The rows
    //    of ONE list are what the operator chooses between, so the comparison is WITHIN each table, over the VISIBLE
    //    prefix (marker + 18 columns of text). ⛔ Relying on a hidden suffix is forbidden outright.
    for (uint8_t a = 0; a < kDmTextCount; ++a)
        for (uint8_t b = uint8_t(a + 1); b < kDmTextCount; ++b)
            CHECK(strncmp(kDmTexts[a], kDmTexts[b], kCols - 1) != 0);
    for (uint8_t a = 0; a < kChannelTextCount; ++a)
        for (uint8_t b = uint8_t(a + 1); b < kChannelTextCount; ++b)
            CHECK(strncmp(kChannelTexts[a], kChannelTexts[b], kCols - 1) != 0);
    // ⓘ The emergency body is EXEMPT and stays 21 columns at x = 0 (§5.3) — `kEmergencyText` is not a body row at
    //   all (it is the wire text of the alarm), and the `Font::large` headlines have their own 12-column budget,
    //   pinned by `tools/probe_board_ui`'s W11b.
}

TEST_CASE("ui14-marker: every SAVE outcome has its own words, and none of them says SAVED for a refusal") {
    UiState st{};
    CHECK(strcmp(settings_note(st), "") == 0);               // nothing attempted -> nothing claimed
    st.cfg_have_save = true;
    st.cfg_save = mrfw::CfgSave::saved;        CHECK(strcmp(settings_note(st), "SAVED") == 0);
    st.cfg_save = mrfw::CfgSave::saved_reboot; CHECK(strcmp(settings_note(st), "SAVED") == 0);
    st.cfg_save = mrfw::CfgSave::no_change;    CHECK(strcmp(settings_note(st), "NO CHANGE") == 0);
    st.cfg_save = mrfw::CfgSave::invalid;      CHECK(strcmp(settings_note(st), "BAD VALUE") == 0);
    st.cfg_save = mrfw::CfgSave::conflict;     CHECK(strcmp(settings_note(st), "CFG! RELOAD") == 0);
    st.cfg_save = mrfw::CfgSave::nv_failed;    CHECK(strcmp(settings_note(st), "SAVE FAILED") == 0);
    st.cfg_save = mrfw::CfgSave::not_open;     CHECK(strcmp(settings_note(st), "NO CONFIG") == 0);
    // ⛔ NOT ONE refusal reads as a success
    for (mrfw::CfgSave r : { mrfw::CfgSave::invalid, mrfw::CfgSave::conflict, mrfw::CfgSave::nv_failed,
                             mrfw::CfgSave::not_open }) {
        st.cfg_save = r;
        CHECK(strstr(settings_note(st), "SAVED") == nullptr);
    }
    // the other two note sources OUTRANK a stale save outcome, and each has its own words
    st.cfg_save = mrfw::CfgSave::saved;
    st.cfg_refresh_failed = true;  CHECK(strcmp(settings_note(st), "NV READ FAILED") == 0);
    // ★ §UI-15 slice 4 replaced the placeholder `PROVISION: UI-15` with §4's TWO CELLS, and they OUTRANK both of the
    //   above for the same reason those outranked the save: the note belongs to the act just performed.
    st.prov_block = ProvBlock::unsaved;   CHECK(strcmp(settings_note(st), "SAVE OR DISCARD") == 0);
    st.prov_block = ProvBlock::conflict;  CHECK(strcmp(settings_note(st), "RELOAD OR DISCARD") == 0);
    for (uint8_t i = 0; i < 8; ++i) CHECK(strlen(settings_note(st)) <= 21u);   // every note fits the panel
}

// ★★★★ THE ROW SHIFT, AND IT IS LIVE RATHER THAN HYPOTHETICAL: the conditional RELOAD row appears at the exact
//     moment a refused SAVE raises the conflict, so the list grows by one UNDER THE CURSOR. A cursor held as an INDEX
//     would then be highlighting RELOAD while the operator was aiming at SAVE — §B64's ruling and §B66's lesson, on a
//     third screen. ⓘ The negative half is asserted too: the identity does NOT move on its own.
TEST_CASE("ui14-cursor: the highlight follows the ROW when a conflict inserts one above it") {
    CfgFix f; const auto s = cfg_snap();
    to_settings_menu(f.m, s);
    CHECK(cursor_to(f.m, s, CfgRow::save));
    const uint8_t before = f.m.state().cursor;
    CHECK(f.m.settings_row_list(s).n == 7);
    f.store.rec.intro_attach = 0;                     // an external write moves a covered field...
    f.m.on_gesture(Gesture::double_press, s);         // ...so this SAVE is refused and the conflict is raised
    CHECK(f.m.state().cfg_save == mrfw::CfgSave::conflict);
    CHECK(f.svc.conflict() == true);
    CHECK(f.m.settings_row_list(s).n == 8);           // the list GREW under the cursor
    f.m.on_tick(cfg_snap(s.now_ms + 10));             // the next frame re-anchors (the freeze happens right after)
    CHECK(f.m.state().cursor == uint8_t(before + 1)); // ★ the index moved...
    CfgRow r{};
    CHECK(row_under_cursor(f.m, s, r));
    CHECK(r == CfgRow::save);                         // ...because the ROW did not
    // ⛔ AND NOTHING ELSE MOVED IT: a second tick with the same list leaves the highlight exactly where it is.
    const uint8_t held = f.m.state().cursor;
    f.m.on_tick(cfg_snap(s.now_ms + 20));
    CHECK(f.m.state().cursor == held);
}

TEST_CASE("ui14-cursor: when the RELOAD row retires, the highlight lands on the SAFE action, never on DISCARD") {
    CfgFix f; const auto s = cfg_snap();
    to_settings_menu(f.m, s);
    f.store.rec.intro_attach = 0;
    CHECK(cursor_to(f.m, s, CfgRow::save));
    f.m.on_gesture(Gesture::double_press, s);         // refused -> the conflict stands and RELOAD appears
    CHECK(f.svc.conflict() == true);
    CHECK(cursor_to(f.m, s, CfgRow::reload));
    f.m.on_gesture(Gesture::double_press, s);         // RELOAD resolves it, so its own row retires
    CHECK(f.svc.conflict() == false);
    f.m.on_tick(cfg_snap(s.now_ms + 10));
    CfgRow r{};
    CHECK(row_under_cursor(f.m, s, r));
    CHECK(r == CfgRow::back);                         // ⛔ the SAFE action, and ⛔ NOT `discard`
    CHECK(f.store.writes == 0);
}

// ==================================================================================================================
// §UI-14 follow-up — THE IMMEDIATE EXTERNAL-WRITE NOTIFICATION (spec §3.6.1), i.e. what `note_external_write` buys
// ==================================================================================================================
// ★★★ THE CASE BELOW IS THE ONE THE SAVE-TIME COMPARISON CANNOT CATCH, AND IT IS WHY THE NOTIFICATION HAS TO BE
//     IMMEDIATE RATHER THAN EVENTUAL. `save()`'s gate 2b re-reads `/mrcfg` and refuses when the bytes differ from the
//     baseline — which covers a companion write that is still standing. ⛔ It does NOT cover
//     `external change → external REVERT → SAVE`: by save time the bytes match the baseline again, so 2b passes, and
//     without a notification the latch was never raised either. The operator's draft then overwrites a record the
//     companion touched twice, having been told nothing.
// ★ SAME SHAPE AS §UI-13's OWN BLOCKER, ONE LAYER EARLIER: there the latch existed and `save()` ignored it; here the
//   latch is never SET. Both are the third state of the same ternary — {bytes differ} · {bytes match, latch clear} ·
//   {BYTES MATCH, LATCH SET} — and only the third needs the notification.
TEST_CASE("ui14-notify: change -> REVERT -> SAVE is refused, which the byte comparison alone cannot do") {
    CfgFix f; const auto s = cfg_snap();
    to_settings_menu(f.m, s);
    CHECK(cursor_to(f.m, s, CfgRow::e2e_dm));
    f.m.on_gesture(Gesture::double_press, s);
    f.m.on_gesture(Gesture::short_press, s);                 // the operator edits e2e_dm 0 -> 1
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.svc.config_unsaved() == true);
    // the companion writes a DIFFERENT covered field and is NOTIFIED immediately (the device hook's job)...
    f.store.rec.intro_attach = 0;
    f.svc.note_external_write(f.store.rec);
    CHECK(f.svc.conflict() == true);
    // ...and then puts it back. The BYTES now match the baseline again.
    f.store.rec.intro_attach = 1;
    f.svc.note_external_write(f.store.rec);
    CHECK(mrfw::cfg_values_from_blob(f.store.rec) == f.svc.baseline());   // ★ the byte comparison would PASS here
    CHECK(f.svc.conflict() == true);                                       // ...and the LATCH still says otherwise
    CHECK(cursor_to(f.m, s, CfgRow::save));
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().cfg_save == mrfw::CfgSave::conflict);
    CHECK(strcmp(settings_note(f.m.state()), "CFG! RELOAD") == 0);
    CHECK(f.store.writes == 0);                              // ⛔ ZERO writes — the whole point
    // ...and the ways out are the ruled two, both of which clear it
    CHECK(f.m.settings_row_list(s).n == 8);                  // the RELOAD row is offered
    CHECK(cursor_to(f.m, s, CfgRow::discard));
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.svc.conflict() == false);
}

TEST_CASE("ui14-notify: a NON-COVERED external write raises NOTHING, and a SAVE still goes through") {
    CfgFix f; const auto s = cfg_snap();
    to_settings_menu(f.m, s);
    CHECK(cursor_to(f.m, s, CfgRow::e2e_dm));
    f.m.on_gesture(Gesture::double_press, s);
    f.m.on_gesture(Gesture::short_press, s);
    f.m.on_gesture(Gesture::double_press, s);
    // ⛔ THE NEGATIVE HALF IS STRUCTURAL, NOT A FILTER LIST: only the four covered fields are ever extracted, so a
    //    write that moved a leased counter, an identity or the radio floor cannot raise a marker at all.
    f.store.rec.channel_ctr = 4242;
    f.store.rec.node_id     = 77;
    f.svc.note_external_write(f.store.rec);
    CHECK(f.svc.conflict() == false);
    CHECK(f.m.settings_row_list(s).n == 7);                  // ...so no RELOAD row appears either
    CHECK(strcmp(cfg_marker_text(f.svc.config_unsaved(), f.svc.conflict()), "CFG* UNSAVED") == 0);
    CHECK(cursor_to(f.m, s, CfgRow::save));
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().cfg_save == mrfw::CfgSave::saved);
    CHECK(f.store.writes == 1);
    // ★ AND THE NON-COVERED FIELDS THE COMPANION WROTE SURVIVE THE PANEL'S SAVE — the one-write-of-the-RELOADED-blob
    //   rule, measured rather than assumed.
    CHECK(f.store.rec.channel_ctr == 4242u);
    CHECK(f.store.rec.node_id == 77);
    CHECK(f.store.rec.e2e_dm == 1);
}

// ★★★ §notify-every-save / [[B194]] — THE `leave` SHAPE, WHICH IS THE LARGEST COVERED-FIELD CHANGE ANY VERB MAKES.
//     `handle_leave` does `b = mrnv::Blob{}` and restores only magic/version/freq/the radio defaults/beacon/duty/the
//     anti-spam knobs ⇒ ALL FOUR covered fields land at 0, whatever they were, and the record is persisted. Before
//     this slice that write told the panel nothing, so an open draft kept its `CFG* UNSAVED` marker over a record that
//     had been wiped underneath it. ⓘ The CALL SITE is in a file no host build compiles — `tools/probe_board_ui/`'s
//     W18 is the instrument for that half; this case is the SEMANTICS.
// ⚠⚠ AND THE LIMIT OF THIS CASE IS MEASURED AND STATED RATHER THAN LEFT TO BE ASSUMED: its CENTRAL claim — the SAVE
//    over the wiped record is refused — has NO single-mutation witness, and that is a property of the design rather
//    than a hole in the battery. The `leave` change is STANDING at save time, so `save()` refuses it TWICE over: gate
//    2a on the latch this notification raised, and gate 2b on the byte comparison. Dropping either one alone
//    (`C32` / `C04`, both verified) leaves the refusal intact. ⇒ what this case uniquely measures is reddened by
//    `C14` (the CfgValues equality, 1 assertion) and `C23` (DISCARD keeping the draft, 1 assertion); the property
//    THIS SLICE adds — that the notification arrives at all — is measured by `probe_firmware_ui`'s P8f (reddened by
//    C37 "the hook never tells the service" and C38 "raised but no repaint") and by the CALL SITE check W18.
//    ★ The sibling `change → REVERT → SAVE` case above is the one where 2b cannot help, which is exactly why it, and
//      not this one, is the case that carries `C32`.
TEST_CASE("ui14-notify: a LEAVE-shaped external write (all four covered fields reset) conflicts and refuses SAVE") {
    CfgFix f; const auto s = cfg_snap();
    f.store.rec.e2e_dm = 1; f.store.rec.intro_attach = 1; f.store.rec.mobile_autoregister = 1; f.store.rec.ble_mode = 2;
    f.live.eff = mrfw::cfg_values_from_blob(f.store.rec);
    to_settings_menu(f.m, s);
    CHECK(f.svc.draft().at(mrfw::CfgField::mobile_autoregister) == 1);
    CHECK(cursor_to(f.m, s, CfgRow::intro_attach));
    f.m.on_gesture(Gesture::double_press, s);
    f.m.on_gesture(Gesture::short_press, s);                 // the operator edits intro_attach 1 -> 0
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.svc.config_unsaved() == true);
    CHECK(f.svc.conflict() == false);
    // ...and now `leave` runs on the serial/BLE side: the whole record is rebuilt from a zeroed Blob.
    f.store.rec.e2e_dm = 0; f.store.rec.intro_attach = 0; f.store.rec.mobile_autoregister = 0; f.store.rec.ble_mode = 0;
    f.svc.note_external_write(f.store.rec);
    CHECK(f.svc.conflict() == true);
    CHECK(strcmp(cfg_marker_text(f.svc.config_unsaved(), f.svc.conflict()), "CFG! RELOAD") == 0);
    const int writes_before = f.store.writes;
    CHECK(cursor_to(f.m, s, CfgRow::save));
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().cfg_save == mrfw::CfgSave::conflict);
    CHECK(f.store.writes == writes_before);                  // ⛔ ZERO writes over the wiped record
    CHECK(f.m.settings_row_list(s).n == 8);                  // the RELOAD row is offered as the non-destructive way out
    CHECK(cursor_to(f.m, s, CfgRow::discard));
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.svc.conflict() == false);
    CHECK(f.svc.draft().at(mrfw::CfgField::mobile_autoregister) == 0);   // ...on the record `leave` actually left
}

// ★★ THE NEGATIVE HALF, AND IT IS WHAT MAKES THE SYSTEMATIC RULE DEFENSIBLE RATHER THAN MERELY LOUD. `join` persists
//    `/mrcfg` too and now notifies — but it assigns NONE of the four covered fields, so the notification must raise
//    NOTHING. ⛔ This is structural, not a filter list: only the four covered fields are ever extracted, so no
//    provisioning field can reach the marker however many of them move.
TEST_CASE("ui14-notify: a JOIN-shaped external write moves no covered field and raises NOTHING") {
    CfgFix f; const auto s = cfg_snap();
    to_settings_menu(f.m, s);
    CHECK(cursor_to(f.m, s, CfgRow::e2e_dm));
    f.m.on_gesture(Gesture::double_press, s);
    f.m.on_gesture(Gesture::short_press, s);
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.svc.config_unsaved() == true);
    // exactly the fields `handle_join` assigns before its save
    f.store.rec.freq_mhz = 869.525; f.store.rec.bw_hz = 125000; f.store.rec.routing_sf = 9;
    f.store.rec.leaf_id = 3; f.store.rec.layer0_id = 3;
    f.store.rec.node_id = 0; f.store.rec.joined = 0; f.store.rec.lineage_id = 0; f.store.rec.config_epoch = 0;
    f.store.rec.leaf_name_len = 0;
    f.svc.note_external_write(f.store.rec);
    CHECK(f.svc.conflict() == false);
    CHECK(f.m.settings_row_list(s).n == 7);                  // ...so no RELOAD row appears
    CHECK(strcmp(cfg_marker_text(f.svc.config_unsaved(), f.svc.conflict()), "CFG* UNSAVED") == 0);
    CHECK(cursor_to(f.m, s, CfgRow::save));
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().cfg_save == mrfw::CfgSave::saved);     // the operator's own save still goes through
    CHECK(f.store.rec.e2e_dm == 1);
    CHECK(f.store.rec.routing_sf == 9);                      // ...carrying join's fields through untouched
    CHECK(f.store.rec.leaf_id == 3);
}

TEST_CASE("ui14-notify: a write while the service is CLOSED is harmless, and does not poison a later open") {
    CfgFix f; const auto s = cfg_snap();
    CHECK(f.svc.is_open() == false);                         // the operator has never reached SETTINGS
    f.store.rec.e2e_dm = 1;
    f.svc.note_external_write(f.store.rec);                  // ⇒ a no-op by construction
    CHECK(f.svc.conflict() == false);
    to_settings_menu(f.m, s);
    CHECK(f.svc.is_open() == true);
    // ★ the baseline is whatever the record holds NOW, so the companion's change is simply the starting state
    CHECK(f.svc.draft().at(mrfw::CfgField::e2e_dm) == 1);
    CHECK(f.svc.config_unsaved() == false);
    CHECK(f.svc.conflict() == false);
    CHECK(f.m.settings_row_list(s).n == 7);
}


// ================================================================ §B197/§B198 — `ui_allows_sleep`, the UI sleep policy
// ★★★ WHAT THIS BLOCK IS FOR. `src/fw_main.cpp`'s idle light-sleep gate is compiled by neither the native suite nor
//   the simulator, so before this the sleep policy could only ever be GREPPED. `mrui::ui_allows_sleep` is the pure
//   predicate that gate calls through `mr_ui_allows_sleep()`, and these cases drive it against the REAL `UiModel`,
//   `InputFsm` and `FrameGate` — not against booleans standing in for them.
// ⚠ EVERY FALSE TERM IS EXERCISED SEPARATELY AND WITH THE OTHER TWO PERMISSIVE, which is what makes each one
//   individually load-bearing: a case that left two terms false could not tell which one was doing the work.

// Bring a model to the BLANKED state the way the device does: seed the inactivity clock, then tick again once
// kBlankMs has passed with no gesture. ⛔ Never by poking `UiState` — `blanked` is the model's own.
// ⚠ `base` is not decoration: `on_tick` seeds `_last_input_ms` on the FIRST call only, so a helper that always seeded
//   from 0 would, on an already-seeded model, blank via an UNSIGNED WRAP instead of via the real 15 s window — a case
//   that passes for the wrong reason.
static void blank_the_model(UiModel& m, uint32_t base = 0) {
    UiSnapshot s0 = snap(base);                 m.on_tick(s0);
    UiSnapshot s1 = snap(base + kBlankMs + 1);  m.on_tick(s1);
}

TEST_CASE("ui-sleep: blank + idle input + no open frame is the ONLY state that permits sleep") {
    UiModel m; InputFsm in; FrameGate g;
    blank_the_model(m);
    CHECK(m.state().blanked == true);
    CHECK(in.active()       == false);
    CHECK(g.frame_open()    == false);
    CHECK(ui_allows_sleep(m, in, g) == true);
}

TEST_CASE("ui-sleep: a LIT panel forbids sleep — the operator is looking at it") {
    UiModel m; InputFsm in; FrameGate g;
    UiSnapshot s = snap(0); m.on_tick(s);           // seeded, never blanked
    CHECK(m.state().blanked == false);
    CHECK(in.active() == false);                    // ★ the other two terms are PERMISSIVE, so `blanked` is what fails
    CHECK(g.frame_open() == false);
    CHECK(ui_allows_sleep(m, in, g) == false);
    // ...and the bounded 15 s attention window is what ends it, with nothing else changing.
    blank_the_model(m);
    CHECK(m.state().blanked == true);
    CHECK(ui_allows_sleep(m, in, g) == true);
}

TEST_CASE("ui-sleep: a gesture being CLASSIFIED forbids sleep, even on a blank panel") {
    UiModel m; InputFsm in; FrameGate g;
    blank_the_model(m);
    CHECK(ui_allows_sleep(m, in, g) == true);       // the baseline this case moves ONE term away from
    in.update(true, 20000);                         // the raw edge: undecided
    CHECK(in.active() == true);
    CHECK(m.state().blanked == true);               // ★ still blanked — the gesture alone is what forbids it
    CHECK(g.frame_open() == false);
    CHECK(ui_allows_sleep(m, in, g) == false);
    // ⛔ AND IT STAYS FORBIDDEN ACROSS THE WHOLE UNDECIDED WINDOW. A ≤1 s sleep pass anywhere in here is exactly what
    //    turned a real tap into nothing on metal: debounce is 25 ms, the double window 350 ms, the arm 800 ms.
    in.update(true,  20030); CHECK(ui_allows_sleep(m, in, g) == false);   // debounced press
    in.update(false, 20100); CHECK(ui_allows_sleep(m, in, g) == false);   // release debounce
    in.update(false, 20130); CHECK(ui_allows_sleep(m, in, g) == false);   // pending single-vs-double
    CHECK(in.update(false, 20450) == Gesture::short_press);
    CHECK(in.active() == false);
    // ⓘ The press is a real gesture through the real classifier, so the model consumes it as the WAKING press and the
    //   panel lights — which is the correct combined answer, not a weakening of this case.
    m.on_gesture(Gesture::short_press, snap(20450));
    CHECK(m.state().blanked == false);
    CHECK(ui_allows_sleep(m, in, g) == false);
}

TEST_CASE("ui-sleep: an OPEN page-buffer frame forbids sleep until its last page is out (B198)") {
    UiModel m; InputFsm in; FrameGate g; UiInboxCounters c{};
    UiSnapshot s = snap(10000);
    m.on_tick(s);
    m.mark_dirty();
    CHECK(g.step(m, s, /*mac_idle=*/true) == FrameStep::open);
    CHECK(g.frame_open() == false);                 // ⓘ `_open` is set by on_page, not by step
    g.on_page(/*more=*/true, m, c);
    CHECK(g.frame_open() == true);
    // ★★ THE B198 STATE ITSELF, AND IT IS THE ONE THAT MEASURED ~8 s ON METAL: seven pages still to push. Make the
    //    other two terms PERMISSIVE so `frame_open` is provably the term doing the work.
    blank_the_model(m, 10000);
    CHECK(m.state().blanked == true);
    CHECK(in.active() == false);
    CHECK(ui_allows_sleep(m, in, g) == false);
    // Push the remaining seven pages; only the LAST one closes the loop.
    for (int i = 0; i < 6; ++i) {
        g.on_page(/*more=*/true, m, c);
        CHECK(g.frame_open() == true);
        CHECK(ui_allows_sleep(m, in, g) == false);
    }
    g.on_page(/*more=*/false, m, c);                // the frame is COMPLETE
    CHECK(g.frame_open() == false);
    CHECK(ui_allows_sleep(m, in, g) == true);
}

// ★★ THE THREE-TERM MATRIX, stated as one case so no combination is left to inference: sleep is permitted in EXACTLY
//    one of the eight cells. Without this, three independent "this term forbids it" cases would still be satisfied by
//    a predicate that ORed them, or by one that dropped a term the other cases never varied alone.
TEST_CASE("ui-sleep: exactly one of the eight term combinations permits sleep") {
    for (int bits = 0; bits < 8; ++bits) {
        const bool want_blank = (bits & 1) != 0, want_input = (bits & 2) != 0, want_frame = (bits & 4) != 0;
        UiModel m; InputFsm in; FrameGate g; UiInboxCounters c{};
        UiSnapshot s0 = snap(0); m.on_tick(s0);                  // seed the inactivity clock at 0
        // ⚠ ORDER MATTERS: `FrameGate::step` answers `blank` on a blanked model and would never open a frame, so the
        //   frame is opened FIRST and the panel blanked afterwards. That is also the real sequence B198 measured.
        if (want_frame) {
            // ⓘ 10000 ms, not a small number: `FrameGate` has a 2 Hz throttle, so a `step` at t = 100 answers `idle`
            //   and the frame would never open — a harness detail that would silently make three cells vacuous.
            UiSnapshot s = snap(10000);
            m.mark_dirty();
            CHECK(g.step(m, s, /*mac_idle=*/true) == FrameStep::open);
            g.on_page(/*more=*/true, m, c);
        }
        if (want_blank) blank_the_model(m);
        if (want_input) in.update(true, 40000);
        CHECK(m.state().blanked == want_blank);
        CHECK(in.active()       == want_input);
        CHECK(g.frame_open()    == want_frame);
        CHECK(ui_allows_sleep(m, in, g) == (want_blank && !want_input && !want_frame));
    }
}

// ==================================================================================================================
// §UI-17 slice 1 — TEAM AND INBOX: THE PASSIVE ↔ INTERACTIVE MIGRATION (spec §1.2/§1.3, slice S1's eight pins)
// ==================================================================================================================
// ★★★★ THE CONTRACT, IN ONE PLACE: every top-level screen costs ONE `short` to pass; a screen that HAS an interaction
//      is ENTERED by a `double`; the interactive list's last row is `BACK`; `BACK` returns to the PASSIVE form of the
//      SAME screen and ⛔ never to another one. It is [[B232]]'s SETTINGS idiom applied twice, so what is measured
//      here is the two NEW arms and the four properties the migration could silently have broken: §B64/§UI-7D's
//      identity refusals, the empty-list carve-outs, blank/wake retention and the emergency pre-emption.
// ⛔ NOT measured here: what any of it LOOKS like — the suppressed marker, the drawn `BACK` row and the reserved
//    refusal rows are `src/firmware_ui.cpp`'s, which no test in this file compiles. That is
//    `tools/probe_firmware_ui`'s P6h-P6k and P14d.

TEST_CASE("ui17-lex: the list's BACK row is the SHIPPED spelling, and there is only one of it") {
    CHECK(strcmp(kListBackText, "BACK") == 0);
    // ★ THE POINT OF THE CASE: the same act is spelled the same way on every screen, so an operator reads one exit.
    //   ⛔ A second spelling is what this measures against — not the constant's own value.
    CHECK(strcmp(kListBackText, settings_row_label(CfgRow::back)) == 0);
    CHECK(strcmp(kListBackText, provision_row_label(ProvRow::back)) == 0);
    // the row renders as `<marker><label>` (`body_back_row`), so the marker's column counts too
    CHECK(strlen(kListBackText) + 1 <= 19u);
    CHECK(kListBackText[0] != '\0');                       // ⛔ C2: an exit nobody can read is no exit at all
}

TEST_CASE("ui17-rowkind: the BACK row is resolved by IDENTITY, and it fails CLOSED past the end") {
    CHECK(list_row_kind(0, 3) == ListRow::member);
    CHECK(list_row_kind(2, 3) == ListRow::member);
    CHECK(list_row_kind(3, 3) == ListRow::back);           // the row AFTER the published rows
    // ★★ FAILS CLOSED, and it is the reachable case rather than defensive: a roster that shrinks under an interactive
    //    list leaves the cursor past the end for exactly one tick, and the row it names must be the one that SENDS
    //    NOTHING — never a member row the caller would then read out of range.
    CHECK(list_row_kind(4, 3) == ListRow::back);
    CHECK(list_row_kind(0, 0) == ListRow::back);           // an empty list is ONE row, and it is the way out
}

TEST_CASE("ui17-entered: ONE predicate answers `is this screen entered`, for every screen") {
    // STATUS and SEND have no interaction to enter, whatever the other two states say
    CHECK(screen_is_entered(Screen::status, Settings::browsing, ListView::interactive) == false);
    CHECK(screen_is_entered(Screen::send,   Settings::browsing, ListView::interactive) == false);
    // TEAM and INBOX read `ListView`...
    CHECK(screen_is_entered(Screen::team,  Settings::closed, ListView::passive)     == false);
    CHECK(screen_is_entered(Screen::team,  Settings::closed, ListView::interactive) == true);
    CHECK(screen_is_entered(Screen::inbox, Settings::closed, ListView::passive)     == false);
    CHECK(screen_is_entered(Screen::inbox, Settings::closed, ListView::interactive) == true);
    // ...and SETTINGS reads `Settings`, so [[B232]]'s closed view is the SAME fact expressed on the third screen
    CHECK(screen_is_entered(Screen::settings, Settings::closed,       ListView::interactive) == false);
    CHECK(screen_is_entered(Screen::settings, Settings::browsing,     ListView::passive)     == true);
    CHECK(screen_is_entered(Screen::settings, Settings::editing,      ListView::passive)     == true);
    CHECK(screen_is_entered(Screen::settings, Settings::provisioning, ListView::passive)     == true);
    CHECK(screen_is_entered(Screen::count,    Settings::browsing,     ListView::interactive) == false);
}

// ★★★ [[B223]] — THE LEAVE RESET IS DRIVEN DIRECTLY, because in the model it is UNREACHABLE: an interactive list
//     cannot be walked off its own screen, so a guard written only where it is currently reachable is a guard no
//     mutation can redden. Both arms and the CHANGED report are driven here.
TEST_CASE("ui17-reset: the leave reset is pure, reports the change, and is idempotent") {
    ListView v = ListView::interactive;
    CHECK(list_view_reset_on_leave(v) == true);            // it CHANGED, so a caller repaints
    CHECK(v == ListView::passive);
    CHECK(list_view_reset_on_leave(v) == false);           // ...and not again, so an off-screen tick is free
    CHECK(v == ListView::passive);
    ListView p = ListView::passive;
    CHECK(list_view_reset_on_leave(p) == false);
}

TEST_CASE("ui17-passive: TEAM and INBOX LAND PASSIVE, and ONE press passes each of them") {
    UiModel m; const auto s = snap_inbox(3);
    m.on_gesture(Gesture::short_press, s);
    CHECK(m.state().screen == Screen::team);
    CHECK(m.state().list_view == ListView::passive);       // ★ the landing: a preview, not a selector
    CHECK(m.state().cursor == 0);
    m.on_gesture(Gesture::short_press, s);
    CHECK(m.state().screen == Screen::inbox);              // ★ ONE press, with a 3-row roster behind it
    CHECK(m.state().list_view == ListView::passive);
    m.on_gesture(Gesture::short_press, s);
    CHECK(m.state().screen == Screen::send);               // ★ ONE press, with a 3-row inbox behind it
}

// ★★★ THE OTHER HALF OF THE LANDING, AND IT IS THE SAFETY ONE: a passive screen records NO pick, so the `double`
//     that arrives there can only ENTER — it cannot open a DM, cannot open a record and cannot queue anything.
TEST_CASE("ui17-passive: a `double` on a passive list ENTERS it and queues NOTHING") {
    UiModel m; const auto s = snap_inbox(3); SendReq req{}; InboxReq rq{};
    m.on_gesture(Gesture::short_press, s);                 // -> TEAM, passive
    m.on_gesture(Gesture::double_press, s);
    CHECK(m.state().list_view == ListView::interactive);   // ...it ENTERED
    CHECK(m.state().screen == Screen::team);
    CHECK(m.state().cursor == 0);
    CHECK(m.state().compose == Compose::none);             // ⛔ ...and opened no sub-view
    CHECK(m.take_send_request(req) == false);              // ⛔ ...and addressed nobody
    CHECK(m.state().team_pick_gone == false);              // ⛔ ...and raised no refusal either
    // the same on INBOX: leave through BACK, walk on, and the entering double asks the store for nothing
    m.on_gesture(Gesture::short_press, s); m.on_gesture(Gesture::short_press, s);
    m.on_gesture(Gesture::short_press, s);                 // cursor 3 = the BACK row of a 3-row roster
    m.on_gesture(Gesture::double_press, s);                // -> passive TEAM
    m.on_gesture(Gesture::short_press, s);                 // -> INBOX, passive
    CHECK(m.state().screen == Screen::inbox);
    CHECK(m.state().list_view == ListView::passive);
    m.on_gesture(Gesture::double_press, s);
    CHECK(m.state().list_view == ListView::interactive);
    CHECK(m.state().detail == InboxModal::closed);
    CHECK(m.take_inbox_request(rq) == false);              // ⛔ the entering press touched no storage
}

// ★★★★ SPEC S1 PIN 2 — THE WALK IS **CONTAINED**. It ends on `BACK` and the press past it comes HOME; ⛔ it never
//      leaves the screen and ⛔ never wraps into an action.
TEST_CASE("ui17-walk: the interactive walk ends on BACK and comes home, on BOTH screens") {
    UiModel m; const auto s = snap_inbox(3);
    to_team(m, s);
    CHECK(m.state().cursor == 0);
    m.on_gesture(Gesture::short_press, s); CHECK(m.state().cursor == 1);
    m.on_gesture(Gesture::short_press, s); CHECK(m.state().cursor == 2);
    m.on_gesture(Gesture::short_press, s);
    CHECK(m.state().cursor == 3);
    CHECK(list_row_kind(m.state().cursor, s.team_shown) == ListRow::back);
    CHECK(m.state().screen == Screen::team);
    m.on_gesture(Gesture::short_press, s);
    CHECK(m.state().screen == Screen::team);               // ⛔ still here...
    CHECK(m.state().cursor == 0);                          // ★ ...and back on the FIRST row
    CHECK(m.state().compose == Compose::none);             // ⛔ and the wrap performed nothing
    // INBOX, the same shape
    UiModel n; InboxReq rq{};
    to_inbox(n, s);
    for (int i = 0; i < 3; ++i) n.on_gesture(Gesture::short_press, s);
    CHECK(n.state().cursor == 3);
    CHECK(list_row_kind(n.state().cursor, s.inbox_shown) == ListRow::back);
    n.on_gesture(Gesture::short_press, s);
    CHECK(n.state().screen == Screen::inbox);
    CHECK(n.state().cursor == 0);
    CHECK(n.take_inbox_request(rq) == false);
}

// ★★★★ SPEC S1 PIN 3 — `BACK` RETURNS TO THE **PASSIVE FORM OF THE SAME SCREEN**, and the press after it passes the
//      screen exactly as a fresh arrival would. ⛔ It is never a jump to another screen — the "where am I" move
//      [[B232]] removed one screen over.
TEST_CASE("ui17-back: BACK closes the list to the SAME screen, and one further press then passes it") {
    UiModel m; const auto s = snap_inbox(3); SendReq req{};
    to_team(m, s);
    for (int i = 0; i < 3; ++i) m.on_gesture(Gesture::short_press, s);      // -> the BACK row
    m.on_gesture(Gesture::double_press, s);
    CHECK(m.state().screen == Screen::team);               // ⛔ NOT the next screen
    CHECK(m.state().list_view == ListView::passive);       // ★ the PASSIVE form of it
    CHECK(m.state().cursor == 0);
    CHECK(m.state().compose == Compose::none);
    CHECK(m.take_send_request(req) == false);              // ⛔ leaving sent nothing
    m.on_gesture(Gesture::short_press, s);
    CHECK(m.state().screen == Screen::inbox);              // ★ and one press then passes it, like a fresh arrival
    // ...and coming back round finds TEAM passive again — the view never outlives the visit
    for (int i = 0; i < 4; ++i) m.on_gesture(Gesture::short_press, s);
    CHECK(m.state().screen == Screen::team);
    CHECK(m.state().list_view == ListView::passive);
}

// ★★★ SPEC S1 PIN 5 — AN EMPTY LIST IS NOT A TRAP. Entering one that has no rows must still offer the way out, or
//     the operator is inside a screen whose only gesture refuses.
TEST_CASE("ui17-empty: an empty roster and an empty inbox still offer BACK, and still leave") {
    UiModel m; auto s = snap_inbox(0); s.team_shown = 0; s.team_total = 0; SendReq req{}; InboxReq rq{};
    m.on_gesture(Gesture::short_press, s);
    CHECK(m.state().screen == Screen::team);
    m.on_gesture(Gesture::double_press, s);
    CHECK(m.state().list_view == ListView::interactive);
    CHECK(list_row_kind(m.state().cursor, s.team_shown) == ListRow::back);   // the ONE row is the exit
    m.on_gesture(Gesture::short_press, s);
    CHECK(m.state().cursor == 0);                          // ⛔ a one-row list cannot walk anywhere
    CHECK(m.state().screen == Screen::team);
    m.on_gesture(Gesture::double_press, s);
    CHECK(m.state().list_view == ListView::passive);       // ★ ...and it LEAVES
    CHECK(m.state().team_pick_gone == false);              // ⛔ ...saying nothing about a pick nobody made
    CHECK(m.take_send_request(req) == false);
    m.on_gesture(Gesture::short_press, s);
    CHECK(m.state().screen == Screen::inbox);
    m.on_gesture(Gesture::double_press, s);
    CHECK(m.state().list_view == ListView::interactive);
    m.on_gesture(Gesture::double_press, s);
    CHECK(m.state().list_view == ListView::passive);
    CHECK(m.state().inbox_pick_gone == false);
    CHECK(m.take_inbox_request(rq) == false);
}

// ★★★★ SPEC S1 PIN 4 — §B64 AND §UI-7D's IDENTITY REFUSALS SURVIVE THE MIGRATION, and the ORDER is what this case
//      is really about: when the roster SHRINKS the lost pick's index can BE the `BACK` index, and a `double` there
//      must still REFUSE — resolving the row first would turn a refusal into a silent "leave".
TEST_CASE("ui17-refuse: a vanished pick REFUSES from the interactive list, and does not close it") {
    UiModel m; auto s = snap(); SendReq req{};
    to_team(m, s);
    m.on_gesture(Gesture::short_press, s); m.on_gesture(Gesture::short_press, s);   // cursor 2 -> teammate id 12
    CHECK(m.state().cursor == 2);
    s.team_shown = 2; s.team_total = 2;                    // ...and now cursor 2 IS the BACK index of a 2-row list
    CHECK(list_row_kind(m.state().cursor, s.team_shown) == ListRow::back);
    m.on_gesture(Gesture::double_press, s);
    CHECK(m.state().team_pick_gone == true);               // ★ REFUSED, loudly (C2)
    CHECK(m.state().compose == Compose::none);
    CHECK(m.take_send_request(req) == false);              // ★ NOTHING was addressed to anybody
    CHECK(m.state().list_view == ListView::interactive);   // ⛔ ...and the refusal did not read as "leave"
    // ★ AND IT IS NOT A DEAD END: moving off the lost pick retires the message, and BACK then works normally.
    m.on_gesture(Gesture::short_press, s);
    CHECK(m.state().team_pick_gone == false);
    CHECK(m.state().cursor == 0);
}

TEST_CASE("ui17-refuse: a vanished RECORD refuses from the interactive INBOX list too") {
    UiModel m; auto s = snap_inbox(3); InboxReq rq{};
    to_inbox(m, s);
    m.on_gesture(Gesture::short_press, s); m.on_gesture(Gesture::short_press, s);   // cursor 2, seq 3
    auto s2 = snap_inbox(2);                               // the record is gone; cursor 2 is now the BACK index
    CHECK(list_row_kind(m.state().cursor, s2.inbox_shown) == ListRow::back);
    m.on_gesture(Gesture::double_press, s2);
    CHECK(m.state().inbox_pick_gone == true);              // ★ MESSAGE GONE, not a silent leave
    CHECK(m.state().detail == InboxModal::closed);
    CHECK(m.take_inbox_request(rq) == false);              // ★ the store was not touched
    CHECK(m.state().list_view == ListView::interactive);
    m.on_gesture(Gesture::short_press, s2);
    CHECK(m.state().inbox_pick_gone == false);             // moving off the lost pick retires it
}

// ★★★★ SPEC S1 PIN 6 — THE INTERACTIVE LIST SURVIVES BLANK/WAKE, and it gets NO timeout of its own. §3.3 forbids an
//      attention timeout discarding an interaction; the panel going dark in a pocket must not silently return the
//      operator to a preview with a different row under the cursor.
TEST_CASE("ui17-retain: blanking KEEPS the list interactive and its selection, and the wake press is consumed") {
    UiModel m; InboxReq rq{};
    to_inbox(m, snap_inbox(3, 1000));
    m.on_gesture(Gesture::short_press, snap_inbox(3, 1100));         // cursor 1
    CHECK(m.state().cursor == 1);
    m.on_tick(snap_inbox(3, 1100 + kBlankMs + 1));
    CHECK(m.state().blanked == true);
    CHECK(m.state().list_view == ListView::interactive);             // ★ the blank discards nothing
    m.on_gesture(Gesture::short_press, snap_inbox(3, 1100 + kBlankMs + 10));
    CHECK(m.state().blanked == false);
    CHECK(m.state().list_view == ListView::interactive);             // ★ ...and the waking press is CONSUMED
    CHECK(m.state().cursor == 1);                                    // ★ ...on the SAME row
    CHECK(m.take_inbox_request(rq) == false);
}

// ============================================================================ §UI-17 S2 — §3.3 RETENTION CONFORMANCE
// ★★★★ THE RULING THIS BLOCK MEASURES (spec §9 R-1, owner, 2026-08-20): **blanking is a POWER action.** It may not
//      discard a draft, a detail selection or a compose choice, so the two shipped `kBlankMs` modal auto-exits are
//      DELETED. ⛔ The exits that remain are a CLOSED LIST — an explicit `BACK`, a completed terminal operation,
//      leaving the screen, and the EMERGENCY exception — and each of the three cases below drives one of the spec's
//      five S2 pins through the shipped press sequence rather than through a flag.
// ⓘ PIN 5 (the blank still fires on time — deleting the timeouts must not extend `kBlankMs`) is asserted INSIDE each
//   case below rather than in a fourth: every one of them blanks the panel on the unmoved deadline as its precondition,
//   which is the only way "the modal survived it" can mean anything at all.

// ★★★★ SPEC S2 PIN 1 — THE COMPOSE SUB-VIEW, WITH A NON-DEFAULT SELECTION. ⛔ The selection is deliberately NOT row 0:
//      a retention that reset the cursor would be indistinguishable from a correct one on a freshly opened modal, and
//      "the SAME cursor" is half of what the ruling protects. ⛔ And nothing may be SENT by any of it — the pocketed
//      device the ruling was argued against is exactly this state.
TEST_CASE("ui17-hold: the compose sub-view survives the blank with its list, cursor and phase intact") {
    UiModel m; const auto s = snap(); SendReq req{};
    to_team(m, s);
    m.on_gesture(Gesture::short_press, s);                          // cursor 1 -> teammate id 11
    m.on_gesture(Gesture::double_press, s);                         // -> the DM sub-view, bound to that teammate
    m.on_gesture(Gesture::short_press, snap(1100));                 // ...and pick the SECOND canned message
    CHECK(m.state().compose == Compose::dm);
    CHECK(m.state().compose_peer == s.team[1].id);
    CHECK(m.state().cursor == 1);
    CHECK(m.state().compose_result == false);                       // the LIST phase, not a result
    m.on_tick(snap(1100 + kBlankMs));                               // ★ pin 5: the blank lands on the unmoved edge
    CHECK(m.state().blanked == true);
    CHECK(m.state().compose == Compose::dm);                        // ★ the interaction is preserved...
    CHECK(m.state().compose_peer == s.team[1].id);                  // ★ ...bound to the same teammate...
    CHECK(m.state().cursor == 1);                                   // ★ ...on the same canned message...
    CHECK(m.state().compose_result == false);                       // ★ ...in the same phase
    m.on_gesture(Gesture::short_press, snap(1100 + kBlankMs + 40)); // the WAKE press, consumed
    CHECK(m.state().blanked == false);
    CHECK(m.state().compose == Compose::dm);
    CHECK(m.state().cursor == 1);                                   // ★ consumed: it did NOT walk the message list
    CHECK(m.take_send_request(req) == false);                       // ⛔ nothing was sent by any press or tick here
}

// ★★★★ SPEC S2 PIN 2 — THE INBOX DETAIL MODAL, AND THE AUTHORITY FOR "NOTHING WAS DELETED" IS THE REQUEST QUEUE. A
//      modal that quietly re-opened on a different record, or lost `back` to `delete`, would be two presses from an
//      erase the operator never chose.
// ⛔⛔ **WITHDRAWN IN PLACE, KEPT VISIBLE** (QG, 2026-08-21) — this note stood above the case and BOTH of its claims
//    are now false, which is why it is quoted rather than deleted: *"ⓘ A ONE-PAGE body on purpose: the page CADENCE
//    is a device action that keeps running while dark, so pinning 'the same page' against a cycling body would
//    measure the cadence, not the retention. The cadence's own interaction with the blank is pin 4's case."*
//    ⇒ ① the cadence does NOT keep running while dark any more — it is suspended (`!_st.blanked`), which is the
//    property S05 measures; and ② the one-page fixture it justified was VACUOUS, which is exactly what let the
//    drift through. The reasoning it replaced is below.
// ⚠⚠ THE FIXTURE IS **MULTI-PAGE ON PURPOSE**, and the one-page version this case shipped with was VACUOUS one
//    parameter deep (QG, 2026-08-21): with `detail_pages == 1` the cadence at `on_tick` is gated off by its own
//    `detail_pages > 1` term, so `detail_page == 0` held whatever the implementation did — it could detect neither a
//    reset nor a drift. ⇒ the body below is the largest one the store accepts (7 pages) and the case lands on a
//    NONZERO page before blanking.
static uint8_t* big_body_7pages() {
    static uint8_t b[241];
    for (uint16_t i = 0; i < sizeof b; ++i) b[i] = 'y';
    return b;
}
TEST_CASE("ui17-hold: the detail modal survives the blank with its record, page and selected action") {
    UiModel m; auto s = snap_inbox(3); InboxReq rq{};
    to_inbox(m, s);
    m.on_gesture(Gesture::short_press, s);                          // cursor 1 -> the CHANNEL row, seq 2
    CHECK(open_detail(m, s, big_body_7pages(), 241) == true);
    CHECK(m.state().detail == InboxModal::body);
    CHECK(m.state().detail_kind == InboxKind::channel);
    CHECK(m.state().detail_seq == 2u);
    CHECK(m.state().detail_pages == 7);                             // ⛔ non-vacuity: the cadence is actually ARMED
    CHECK(m.state().detail_action == InboxAction::back);
    // Walk the cadence onto a NONZERO page while the panel is still lit — this is the DEVICE acting, as it always has.
    m.on_tick(snap_inbox(3, 1000 + kDetailPageMs));
    m.on_tick(snap_inbox(3, 1000 + 2 * kDetailPageMs));
    CHECK(m.state().detail_page == 2);                              // ★ the page the operator is looking at
    // ★★★ THE BLANKING TICK IS ALSO A PAGE-DUE TICK — 13 s since the last turn, well past `kDetailPageMs` — so this
    //     is the BOTH-DUE boundary, and the blank must win. ⛔ THE LITERAL 2 IS THE POINT: this used to capture
    //     whatever the tick left behind and assert it was merely nonzero, which DEFINED the drift as correct instead
    //     of catching it (QG, 2026-08-21). The page a blank hides must be the page the operator last saw.
    m.on_tick(snap_inbox(3, 1000 + kBlankMs));                      // ★ pin 5: blanked on the unmoved deadline
    CHECK(m.state().blanked == true);
    CHECK(m.state().detail_page == 2);                              // ★★ the LAST VISIBLE page, not a turned one
    CHECK(m.state().detail == InboxModal::body);                    // ★ the modal is preserved...
    CHECK(m.state().detail_kind == InboxKind::channel);             // ★ ...on the same record, by IDENTITY...
    CHECK(m.state().detail_seq == 2u);
    CHECK(m.state().detail_action == InboxAction::back);            // ★ ...with `back` still selected, never `delete`
    m.on_gesture(Gesture::short_press, snap_inbox(3, 1000 + kBlankMs + 40));   // the WAKE press, consumed
    CHECK(m.state().blanked == false);
    CHECK(m.state().detail == InboxModal::body);
    CHECK(m.state().detail_page == 2);                              // ★ ...and on the SAME page, not a reset one
    CHECK(m.state().detail_action == InboxAction::back);            // ★ consumed: it did NOT toggle the action
    CHECK(m.take_inbox_request(rq) == false);                       // ⛔ the store was never asked for anything
}

// ★★★★ §UI-17 S2 — **THE RETAINED PAGE, ACROSS THE DARK AND THROUGH THE REAL WAKE PASS** (QG-ruled 2026-08-21).
//      ⛔⛔ THE DEFECT THIS CLOSES, AND IT IS TWO INDEPENDENT HALVES: with the modal now retained across a blank,
//      (a) the 2 s page cadence kept running on a panel nobody can see, so the retained modal DRIFTED pages in the
//      dark; and (b) even suspended, `_detail_page_at_ms` was still the pre-blank stamp, so the WAKE PASS ITSELF
//      banked the whole dark interval and turned the page before the first frame.
//      ⓘ AND A THIRD HALF FOUND BY QG (2026-08-21), which is why the case now names a LITERAL page throughout:
//      (c) `on_tick` runs the page advance BEFORE the blank transition, so the tick that CROSSES the blank deadline
//      turned the page and hid it in the same pass — the operator woke onto a page they never saw. ⛔ THE EARLIER
//      SHAPE OF THIS CASE **EXHIBITED** THAT RATHER THAN CATCHING IT: it captured `dark_page` from whatever the
//      crossing tick left behind and compared everything to that, which DEFINES the drift as correct. Every page
//      assertion below is now against a literal derived from the walk.
// ★★★ THE WAKE IS DRIVEN IN THE **REAL LOOP'S ORDER**, ⛔ NOT AN IDEALISED ONE: `mr_ui_tick` builds ONE snapshot,
//      calls `on_gesture(...)` and then `on_tick(s)` — same object, same `now_ms`, same pass
//      (`src/firmware_ui.cpp`). Half (b) is INVISIBLE to a harness that only presses and asserts, because the tick
//      that loses the page is the very one the press shares its snapshot with. ⇒ mirrored here exactly.
TEST_CASE("ui17-hold: a multi-page detail keeps its page in the dark AND through the real wake pass") {
    UiModel m; auto s = snap_inbox(1);
    to_inbox(m, s);
    CHECK(open_detail(m, s, big_body_7pages(), 241) == true);
    CHECK(m.state().detail_pages == 7);                             // ⛔ non-vacuity: >1, so the cadence is armed
    m.on_tick(snap_inbox(1, 1000 + kDetailPageMs));
    m.on_tick(snap_inbox(1, 1000 + 2 * kDetailPageMs));
    CHECK(m.state().detail_page == 2);                              // ⛔ NONZERO, and not 1: a reset OR a drift shows
    // ---- (c) THE CROSSING TICK — **BOTH DEADLINES DUE AT ONCE**. 13 s have passed since the last page turn (well
    //      past `kDetailPageMs`) and this is also the tick the blank deadline lands on. The blank must WIN: the page
    //      the operator last saw is the page the dark panel keeps. ⛔ Asserted as the LITERAL 2, so a turn here
    //      cannot be absorbed into the expectation.
    m.on_tick(snap_inbox(1, 1000 + kBlankMs));
    CHECK(m.state().blanked == true);
    CHECK(m.state().detail_page == 2);                              // ★★ the blank outranks the page turn
    // ---- (a) DARK TICKS. Each is well past `kDetailPageMs`, so an ungated cadence would turn the page every one.
    for (uint32_t k = 1; k <= 4; ++k) m.on_tick(snap_inbox(1, 1000 + kBlankMs + k * kDetailPageMs));
    CHECK(m.state().blanked == true);
    CHECK(m.state().detail_page == 2);                              // ★ ⛔ the panel is dark: nothing may turn
    CHECK(m.state().detail == InboxModal::body);
    // ---- (b) THE WAKE PASS, IN THE REAL ORDER: one snapshot, on_gesture then on_tick, same now_ms.
    const uint32_t wake_ms = 1000 + kBlankMs + 9 * kDetailPageMs;   // a LONG dark interval to bank, if it could
    const UiSnapshot ws = snap_inbox(1, wake_ms);
    m.on_gesture(Gesture::short_press, ws);                         // the consumed wake
    m.on_tick(ws);                                                  // ...and the tick that shares its snapshot
    CHECK(m.state().blanked == false);
    CHECK(m.state().detail_page == 2);                              // ★★ the SAME page the operator left
    CHECK(m.state().detail == InboxModal::body);
    // ...and the cadence is RESTARTED, not stopped: it resumes from the wake, one full period later.
    m.on_tick(snap_inbox(1, wake_ms + kDetailPageMs - 1));
    CHECK(m.state().detail_page == 2);                              // ⛔ not a millisecond early
    m.on_tick(snap_inbox(1, wake_ms + kDetailPageMs));
    CHECK(m.state().detail_page == 3);                              // ★ ...and it does resume, on the NEXT page
}

// ★★★★ SPEC S2 PIN 3 — THE EMERGENCY EXCEPTION IS INTACT, AND IT IS THE HALF THAT PAYS FOR THE TWO DELETED TIMEOUTS.
//      A hidden Delete may not survive under an alarm overlay (§UI-7D), and a selected canned message may not survive
//      a COMMITTED alarm (§B101) — so `long_arm` still closes the detail modal and `long_fire` still closes compose.
//      ⛔ Both are driven AFTER a blank/wake, i.e. over exactly the modals this slice now preserves.
TEST_CASE("ui17-hold: the emergency still closes both modals — long_arm the detail, long_fire the compose") {
    // (a) the DETAIL modal, with `delete` armed: `long_arm` closes it before the alarm is armed.
    UiModel m; auto s = snap_inbox(2); InboxReq rq{};
    to_inbox(m, s);
    CHECK(open_detail(m, s, kBody7, sizeof kBody7) == true);
    m.on_gesture(Gesture::short_press, s);                          // arm `delete`
    CHECK(m.state().detail_action == InboxAction::del);
    m.on_tick(snap_inbox(2, 1000 + kBlankMs));
    CHECK(m.state().blanked == true);
    CHECK(m.state().detail == InboxModal::body);                    // retained across the blank (pin 2)
    m.on_gesture(Gesture::long_arm, snap_inbox(2, 1000 + kBlankMs + 40));
    CHECK(m.emergency() == Emergency::arming);
    CHECK(m.state().detail == InboxModal::closed);                  // ⛔ ...and the armed Delete does NOT survive it
    CHECK(m.take_inbox_request(rq) == false);                       // ⛔ closing erased nothing either
    // (b) the COMPOSE sub-view: `long_arm` leaves it (arming is cancellable, §B101), `long_fire` closes it.
    UiModel n; const auto t = snap(); SendReq req{};
    to_team(n, t);
    n.on_gesture(Gesture::double_press, t);                         // -> the DM sub-view
    n.on_tick(snap(1000 + kBlankMs));
    CHECK(n.state().blanked == true);
    CHECK(n.state().compose == Compose::dm);                        // retained across the blank (pin 1)
    n.on_gesture(Gesture::long_arm, snap(1000 + kBlankMs + 40));
    CHECK(n.emergency() == Emergency::arming);
    CHECK(n.state().compose == Compose::dm);                        // ★ arming is cancellable ⇒ the list position stays
    n.on_gesture(Gesture::long_fire, snap(1000 + kBlankMs + 80));
    CHECK(n.emergency() == Emergency::firing);
    CHECK(n.state().compose == Compose::none);                      // ⛔ ...but COMMITTING an alarm closes it
    CHECK(n.state().compose_result == false);
    const bool got = n.take_send_request(req);
    CHECK(got == true);                                             // the alarm itself, and ⛔ never a canned DM
    if (got) CHECK(req.kind == SendKind::emergency);
}

// ★★★★ SPEC S1 PIN 7 — THE EMERGENCY PRE-EMPTS EVERYTHING AND STILL DOES NOT CLOSE THE LIST. Nothing can be sent
//      from a list and arming is cancellable (§B101's own argument), so destroying the operator's position for a
//      press they may still cancel would be a second, smaller wrong. ⛔ The DETAIL modal keeps its own close — a
//      hidden Delete may not survive under an overlay (§UI-7D).
TEST_CASE("ui17-emergency: long_arm leaves the interactive list alone, and still closes the detail modal") {
    UiModel m; const auto s = snap();
    to_team(m, s);
    m.on_gesture(Gesture::short_press, s);
    CHECK(m.state().cursor == 1);
    m.on_gesture(Gesture::long_arm, snap(1100));
    CHECK(m.emergency() == Emergency::arming);
    CHECK(m.state().list_view == ListView::interactive);   // ★ the list is NOT closed
    CHECK(m.state().cursor == 1);                          // ★ ...and the selection is where the operator left it
    m.on_gesture(Gesture::long_cancel, snap(1200));
    CHECK(m.state().list_view == ListView::interactive);
    // ...while the INBOX detail modal still closes at ARM, with the list underneath it intact
    UiModel n; auto si = snap_inbox(2);
    to_inbox(n, si);
    CHECK(open_detail(n, si, kBody7, sizeof kBody7) == true);
    n.on_gesture(Gesture::long_arm, snap_inbox(2, 1100));
    CHECK(n.state().detail == InboxModal::closed);         // ⛔ the modal DOES close
    CHECK(n.state().list_view == ListView::interactive);   // ★ ...and the list it was opened from does not
}

// ★★★ SPEC S1 PIN 8 — THE MODAL RETURNS TO THE LIST IT WAS OPENED FROM. A `back` that landed on the PASSIVE preview
//     would cost the operator a re-entry and lose their place in the list, every single time they read a message.
TEST_CASE("ui17-detail: the modal's `back` returns to the INTERACTIVE list, not to the preview") {
    UiModel m; auto s = snap_inbox(3);
    to_inbox(m, s);
    m.on_gesture(Gesture::short_press, s);                 // cursor 1
    CHECK(open_detail(m, s, kBody7, sizeof kBody7) == true);
    CHECK(m.state().detail == InboxModal::body);
    m.on_gesture(Gesture::double_press, s);                // `back` is selected on entry
    CHECK(m.state().detail == InboxModal::closed);
    CHECK(m.state().screen == Screen::inbox);
    CHECK(m.state().list_view == ListView::interactive);   // ★ the list it came from, still entered
    CHECK(m.state().cursor == 1);                          // ★ ...on the same record
}

// ★★★★ THE PICK'S OWN CASE, AND THE HARM IT MEASURES IS NOT THE MARKER: a pick recorded on a screen nobody entered
//      lets `sync_*_cursor` ANNOUNCE ITS LOSS — `TEAMMATE GONE` / `MESSAGE GONE` on a PREVIEW, about a choice the
//      operator never made. ⇒ "while PASSIVE nothing is picked" is asserted through the refusal it would enable,
//      never through a private flag.
TEST_CASE("ui17-passive: a passive preview records NO pick, so a roster change raises no refusal") {
    UiModel m; auto s = snap();
    m.on_gesture(Gesture::short_press, s);                 // -> TEAM, passive, cursor 0 over a 3-row roster
    CHECK(m.state().list_view == ListView::passive);
    m.on_tick(s);
    CHECK(m.state().team_pick_gone == false);
    s.team[0].id = 77; s.team[1].id = 88; s.team[2].id = 99;   // the whole roster is replaced under the preview
    m.on_tick(s);
    CHECK(m.state().team_pick_gone == false);              // ⛔ nothing was picked, so nothing can have been lost
    s.team_shown = 0; s.team_total = 0;                    // ...and the same when it empties entirely
    m.on_tick(s);
    CHECK(m.state().team_pick_gone == false);
    // the INBOX side, one plane over
    UiModel n; auto si = snap_inbox(3);
    n.on_gesture(Gesture::short_press, si); n.on_gesture(Gesture::short_press, si);
    CHECK(n.state().screen == Screen::inbox);
    CHECK(n.state().list_view == ListView::passive);
    n.on_tick(si);
    CHECK(n.state().inbox_pick_gone == false);
    n.on_tick(snap_inbox(1));                              // two records evicted under the preview
    CHECK(n.state().inbox_pick_gone == false);
    n.on_tick(snap_inbox(0));
    CHECK(n.state().inbox_pick_gone == false);
}

// ★★★★ THE THREE HOISTED DECISIONS, DRIVEN DIRECTLY (QG-RULED 2026-08-21). TEAM and INBOX asked each of these
//      questions TWICE, so a mutation could redden one arm and leave the other unprotected — [[B217]]'s own shape.
//      They are ONE pure function each now, and these cases drive every arm at the helper, which is also what gives
//      the [[B223]] arm the model cannot reach a driver at all.
TEST_CASE("ui17-len: the list's length is ONE decision, and a screen nobody entered is ONE row") {
    CHECK(list_len_of(/*entered=*/false, 0) == 1);
    CHECK(list_len_of(/*entered=*/false, 3) == 1);          // ⛔ NOT `shown` clamped — the rows are not on offer
    CHECK(list_len_of(/*entered=*/false, kMaxTeamRows) == 1);
    CHECK(list_len_of(/*entered=*/true,  0) == 1);          // an empty list is just its exit row
    CHECK(list_len_of(/*entered=*/true,  3) == 4);          // ...and an entered one is the rows PLUS that exit
    CHECK(list_len_of(/*entered=*/true,  kMaxTeamRows) == kMaxTeamRows + 1);
}

TEST_CASE("ui17-act: what a `double` MEANS on a list screen — every arm, and the ORDER") {
    // (1) a PASSIVE preview offers exactly one gesture, whatever the cursor or the roster say
    CHECK(list_activate(/*entered=*/false, /*gone=*/false, 0, 3) == ListAct::enter);
    CHECK(list_activate(/*entered=*/false, /*gone=*/false, 2, 3) == ListAct::enter);
    CHECK(list_activate(/*entered=*/false, /*gone=*/true,  9, 0) == ListAct::enter);
    // (2) an entered list: a member row activates, the row after the published ones leaves
    CHECK(list_activate(/*entered=*/true, /*gone=*/false, 0, 3) == ListAct::member);
    CHECK(list_activate(/*entered=*/true, /*gone=*/false, 2, 3) == ListAct::member);
    CHECK(list_activate(/*entered=*/true, /*gone=*/false, 3, 3) == ListAct::leave);
    CHECK(list_activate(/*entered=*/true, /*gone=*/false, 0, 0) == ListAct::leave);   // an empty list: only the exit
    // ★★★★ (3) THE ORDER, AND IT IS THE WHOLE CASE: §B64's refusal OUTRANKS the row. A roster that shrank leaves the
    //      lost pick's index sitting ON the `BACK` index, and resolving the row first would turn a refusal into a
    //      silent "leave" — the mis-send arriving as a missing message.
    CHECK(list_activate(/*entered=*/true, /*gone=*/true, 2, 2) == ListAct::refuse);   // ⛔ NOT `leave`
    CHECK(list_activate(/*entered=*/true, /*gone=*/true, 1, 3) == ListAct::refuse);   // ...and on a member row too
    CHECK(list_activate(/*entered=*/true, /*gone=*/true, 0, 0) == ListAct::refuse);
    // ⓘ ...but a refusal can never PRE-EMPT the entry: a passive screen records no pick, so it cannot have lost one.
    CHECK(list_activate(/*entered=*/false, /*gone=*/true, 2, 2) == ListAct::enter);
}

TEST_CASE("ui17-note: what the WRITE side does with the cursor — every arm, [[B223]]'s included") {
    // ★★ THE ARM THE MODEL CANNOT REACH ([[B223]]): leaving the screen retires the message. Since S1 a refusal can
    //    only stand while the list is entered and the only way out is `BACK`, which retires it first — so this is
    //    driven HERE or nowhere, and a guard no suite drives is a guard no mutation can redden.
    CHECK(list_note_kind(/*on_screen=*/false, /*entered=*/true,  0, 3) == ListNote::retire);
    CHECK(list_note_kind(/*on_screen=*/false, /*entered=*/false, 0, 3) == ListNote::retire);
    // a PASSIVE preview records NOTHING — so a roster change cannot announce the loss of a pick nobody made
    CHECK(list_note_kind(/*on_screen=*/true, /*entered=*/false, 0, 3) == ListNote::keep);
    CHECK(list_note_kind(/*on_screen=*/true, /*entered=*/false, 3, 3) == ListNote::keep);
    // an ENTERED list: a member row IS the new pick...
    CHECK(list_note_kind(/*on_screen=*/true, /*entered=*/true, 0, 3) == ListNote::record);
    CHECK(list_note_kind(/*on_screen=*/true, /*entered=*/true, 2, 3) == ListNote::record);
    // ...and coming to rest on `BACK` records nobody AND retires the message, which is what stops an emptied roster
    // from trapping the operator in a list where every `double` refuses and `BACK` is one of them.
    CHECK(list_note_kind(/*on_screen=*/true, /*entered=*/true, 3, 3) == ListNote::retire);
    CHECK(list_note_kind(/*on_screen=*/true, /*entered=*/true, 4, 3) == ListNote::retire);   // fails closed past the end
    CHECK(list_note_kind(/*on_screen=*/true, /*entered=*/true, 0, 0) == ListNote::retire);   // an empty entered list
}

// ================================================================ §UI-17 S8 — WAKE ON RECEIVE, the PURE MODEL half
// ★★★★ THE RULING (owner, 2026-08-20, spec §9 R-6/R-7): **a received message lights the panel.** The SCOPE — which
//      push may call this at all — belongs to `mrui::ui_route_recv_push` and is pinned in `test_firmware_ui_send.cpp`
//      (the sealed-vs-cleartext pair). What THESE cases own is the EFFECT and its five invariants: the separate
//      deadline, the untouched input clock, no navigation, no emergency write, and the quiet node's sleep.
// ⓘ EVERY CASE DRIVES `on_msg_wake` DIRECTLY, which is exactly what the router does with it — ⛔ no flag is poked and
//   no deadline is written by hand, so a mutation inside the model is what these fail against.
TEST_CASE("ui17-wake: a message lights a BLANKED panel and asks for the repaint") {
    UiModel m; auto s = snap(1000);
    m.on_tick(s);                                                   // B65: the first tick seeds the attention clock
    m.on_tick(snap(1000 + kBlankMs));
    CHECK(m.state().blanked == true);
    m.clear_dirty();
    m.on_msg_wake(1000 + kBlankMs + 500);
    CHECK(m.state().blanked == false);                              // ★ spec pin 1
    CHECK(m.state().dirty   == true);                               // ★ ...and the panel is owed a frame
}

// ★★★★ SPEC PIN 9 — **THE WINDOW IS `kBlankMs` FROM THE MESSAGE, NOT FROM THE LAST PRESS**, and the case is built so
//      the two answers differ by 10 s: the press is at 1 000 and the message at 11 000, so a wake that merely cleared
//      `blanked` would let the very next tick blank it again (the one-frame flash the spec names), and one that
//      inherited the press's deadline would blank at 16 000 instead of 26 000.
TEST_CASE("ui17-wake: the panel stays lit a FULL window measured from the MESSAGE, then blanks by itself") {
    UiModel m; auto s = snap(1000);
    m.on_gesture(Gesture::short_press, s);                          // a real press at 1 000
    m.on_msg_wake(11000);                                           // ...and a message 10 s later, panel still lit
    m.on_tick(snap(16000));
    CHECK(m.state().blanked == false);                              // ⛔ NOT the press's deadline (1 000 + 15 000)
    m.on_tick(snap(11000 + kBlankMs - 1));
    CHECK(m.state().blanked == false);                              // ⛔ not a millisecond early...
    m.on_tick(snap(11000 + kBlankMs));
    CHECK(m.state().blanked == true);                               // ★ ...and exactly on the message's own edge
}

// ★★ AND THE SAME EDGE FROM THE DARK SIDE: a message that wakes a blanked panel buys a full window from ITSELF.
TEST_CASE("ui17-wake: a wake from the dark re-blanks kBlankMs after the message") {
    UiModel m; auto s = snap(1000);
    m.on_tick(s);
    m.on_tick(snap(1000 + kBlankMs));
    CHECK(m.state().blanked == true);
    const uint32_t msg = 40000;                                     // long after the press's window closed
    m.on_msg_wake(msg);
    m.on_tick(snap(msg + 10));
    CHECK(m.state().blanked == false);                              // ⛔ the next tick does NOT re-blank it
    m.on_tick(snap(msg + kBlankMs - 1));
    CHECK(m.state().blanked == false);
    m.on_tick(snap(msg + kBlankMs));
    CHECK(m.state().blanked == true);                               // ★ pin 9, from the dark
}

// ★★★★ SPEC PIN 10 — A WAKE ON AN ALREADY-LIT PANEL MOVES **ONLY THE DEADLINE**. ⛔ It does not re-dirty the model
//      (the router's `mark_dirty` is the one that does, on every arm), and it does not restart the page cadence — a
//      reader mid-page is not interrupted by traffic. Both are asserted, because "changes nothing" is otherwise a
//      claim about code rather than about behaviour.
TEST_CASE("ui17-wake: a wake while ALREADY LIT moves only the deadline") {
    UiModel m; auto s = snap_inbox(1);
    to_inbox(m, s);
    CHECK(open_detail(m, s, big_body_7pages(), 241) == true);
    CHECK(m.state().detail_pages == 7);                             // ⛔ non-vacuity: the cadence is armed
    m.on_tick(snap_inbox(1, 1000 + kDetailPageMs));
    CHECK(m.state().detail_page == 1);
    m.clear_dirty();
    m.on_msg_wake(1000 + kDetailPageMs + 500);                      // ...a message arrives mid-page
    CHECK(m.state().blanked == false);
    CHECK(m.state().dirty   == false);                              // ★ the model itself asked for nothing
    CHECK(m.state().detail_page == 1);                              // ★ ...and turned nothing
    // ★ THE CADENCE IS NOT RESTARTED: the next page still turns on the ORIGINAL stamp's schedule, not the message's.
    m.on_tick(snap_inbox(1, 1000 + 2 * kDetailPageMs));
    CHECK(m.state().detail_page == 2);
    // ...and the deadline DID move: the panel is still lit a full window after the message, past the press's edge.
    m.on_tick(snap_inbox(1, 1000 + kBlankMs + 100));
    CHECK(m.state().blanked == false);
}

// ★★★★ SPEC PIN 6 — ⛔ **A PUSH NEVER NAVIGATES.** The wake lights the CURRENT screen with the operator's place on it
//      intact: the [[B233]] class. Driven over the state that has the most to lose — an ENTERED interactive INBOX
//      list with a non-default row selected — and every field the operator can see is compared across the wake.
TEST_CASE("ui17-wake: the wake NAVIGATES NOTHING — screen, list, cursor and both picks survive it") {
    UiModel m; InboxReq rq{};
    to_inbox(m, snap_inbox(3, 1000));
    m.on_gesture(Gesture::short_press, snap_inbox(3, 1100));         // cursor 1, an entered list
    CHECK(m.state().screen    == Screen::inbox);
    CHECK(m.state().list_view == ListView::interactive);
    CHECK(m.state().cursor    == 1);
    m.on_tick(snap_inbox(3, 1100 + kBlankMs));
    CHECK(m.state().blanked == true);
    const UiState before = m.state();
    m.on_msg_wake(1100 + kBlankMs + 200);
    CHECK(m.state().blanked         == false);
    CHECK(m.state().screen          == before.screen);               // ⛔ never switched to INBOX-or-anything
    CHECK(m.state().list_view       == before.list_view);            // ⛔ the list did not fall back to passive
    CHECK(m.state().cursor          == before.cursor);               // ⛔ the highlight did not move
    CHECK(m.state().compose         == before.compose);
    CHECK(m.state().detail          == before.detail);
    CHECK(m.state().team_pick_gone  == before.team_pick_gone);       // ⛔ no transient note was retired...
    CHECK(m.state().inbox_pick_gone == before.inbox_pick_gone);
    CHECK(m.take_inbox_request(rq)  == false);                       // ⛔ ...and the store was asked for nothing
}

// ★★★★ SPEC PIN 7 — ⛔ **EMERGENCY OVERLAYS OWN THE PANEL.** The wake writes no `_emg`, no `_tries`, no hold and no
//      news counter, so a RETAINED outcome (§B71/§B102 — terminal AND presented) is still retained, still says the
//      same thing, and is still one press from being dismissed rather than zero.
TEST_CASE("ui17-wake: a message disturbs NO emergency field, over a retained outcome") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::long_arm, snap(1000)); m.on_gesture(Gesture::long_fire, snap(4500));
    const bool got = m.take_send_request(req);
    CHECK(got == true);
    m.on_send_accepted(SendKind::emergency, 5000);
    m.on_outcome(SendOutcome::channel_relayed(), 6000);
    CHECK(m.emergency() == Emergency::picked_up);
    m.mark_outcome_presented(Emergency::picked_up, m.emg_news());   // a COMPLETED frame put it on the glass
    CHECK(m.emg_outcome_retained() == true);
    const uint8_t  tries_before = m.attempts();
    const uint32_t news_before  = m.emg_news();
    const uint32_t dark_ms = 6000 + kEmgHoldMs + kBlankMs;          // the hold has expired and the panel is dark
    m.on_tick(snap(dark_ms));
    CHECK(m.state().blanked == true);
    m.on_msg_wake(dark_ms + 100);
    CHECK(m.state().blanked        == false);                       // it lights...
    CHECK(m.emergency()            == Emergency::picked_up);        // ★ ...showing the SAME outcome
    CHECK(m.attempts()             == tries_before);                // ⛔ no attempt was spent
    CHECK(m.emg_news()             == news_before);                 // ⛔ no news was manufactured
    CHECK(m.emg_outcome_retained() == true);                        // ⛔ and it is still there to be acknowledged
}

// ★★★★ SPEC PIN 8 — ⛔ **THE WAKE DOES NOT STAMP THE INPUT CLOCK.** ⓘ REPORTED HONESTLY RATHER THAN OVERCLAIMED: with
//      §9 R-1 having deleted both modal auto-exits, `blank_due` is the ONLY reader of `_last_input_ms` left, and a
//      `kBlankMs`-from-now wake window is ARITHMETICALLY IDENTICAL to writing that field — so the write alone is
//      unobservable today. What IS observable is the tempting fix's full shape: `on_gesture` stamps the pair
//      `_last_input_ms = now; _seeded = true;`, and copying THAT into the wake re-creates [[B65]] exactly — a message
//      arriving before the first tick would consume the seed, and the panel would be DARK the first time the operator
//      looks at it. ⇒ that is the property this case measures, and it is the shape the battery mutates.
TEST_CASE("ui17-wake: a wake before the first tick does not consume the B65 seed") {
    UiModel m;
    m.on_msg_wake(1000);                                            // a message arrives BEFORE any tick or press
    CHECK(m.state().blanked == false);
    m.on_tick(snap(200000));                                        // the first tick, long after (a slow NV boot)
    CHECK(m.state().blanked == false);                              // ★ the seed is still this tick's, so no blank...
    m.on_tick(snap(200000 + kBlankMs - 1));
    CHECK(m.state().blanked == false);
    m.on_tick(snap(200000 + kBlankMs));
    CHECK(m.state().blanked == true);                               // ★ ...and the window runs from the FIRST TICK
}

// ★★★★ SPEC PIN 11 — **THE QUIET NODE IS UNTOUCHED, AND THAT INCLUDES PAST 2^31 ms OF UPTIME.** ⛔ THE DEFECT THIS
//      CLOSES IS THE `_msg_wake_armed` FLAG's WHOLE REASON: a wrap-safe "now < deadline" reads the initial 0 as a
//      deadline 24.8 DAYS AHEAD for every `now_ms > 2^31`, so a node that has merely been up four weeks and received
//      NOTHING would never blank again — and, through `ui_allows_sleep`, never light-sleep again.
TEST_CASE("ui17-wake: a node that received NOTHING blanks and sleeps exactly as before, at any uptime") {
    // ⚠ THE FOURTH BASE IS THE ONE THE FLAG EXISTS FOR, and it is chosen rather than round: `2^32 - 20 000` puts the
    //   BLANK TICK inside the last `kBlankMs` before the millis wrap, which is the only window in which the initial
    //   deadline of 0 is less than one window ahead — i.e. the only place the bound alone cannot see it.
    InputFsm in; FrameGate g;
    for (uint32_t base : { uint32_t(1000), uint32_t(0x80000000u), uint32_t(0xFFFF0000u),
                           uint32_t(0xFFFFFFFFu - 20000u) }) {
        UiModel m;
        m.on_tick(snap(base));
        CHECK(m.state().blanked == false);
        m.on_tick(snap(base + kBlankMs));
        CHECK(m.state().blanked == true);                           // ★ it still blanks on the unmoved deadline...
        in.update(/*pressed=*/false, base + kBlankMs);
        CHECK(ui_allows_sleep(m, in, g) == true);                   // ★ ...and it still light-sleeps
    }
}

// ★★★★ **THE WINDOW IS BOUNDED ABOVE, AND THIS IS THE CASE THAT SAYS WHY** — ⛔ MEASURED IN THE PROBE BEFORE IT COULD
//      REACH METAL, and it is a SECOND, INDEPENDENT half of pin 11. A bare wrap-safe "now < deadline" is only correct
//      for half the counter: once `now` has run 2^31 ms past an EXPIRED deadline the comparison flips back and the
//      ancient wake reads as live again. ⇒ a node that received ONE message and then nothing would stop blanking
//      ~24.8 days later, for the next ~24.8 days. ⓘ `_msg_wake_armed` cannot see this — it never clears — so the
//      `left <= kBlankMs` bound is what closes it: a LIVE window is at most one window ahead, by construction.
TEST_CASE("ui17-wake: a message received 37 days ago does not revive as a live wake") {
    UiModel m;
    m.on_tick(snap(1000));
    m.on_msg_wake(2000);                                            // the ONE message this node ever received
    m.on_tick(snap(2000 + kBlankMs));
    CHECK(m.state().blanked == true);                               // its window closed normally, that same minute
    const uint32_t much_later = 2000 + 0xC0000000u;                 // ~37 days on: deliberately PAST 2^31
    m.on_gesture(Gesture::short_press, snap(much_later));           // the operator looks at it — the wake press
    CHECK(m.state().blanked == false);
    m.on_tick(snap(much_later + kBlankMs - 1));
    CHECK(m.state().blanked == false);
    m.on_tick(snap(much_later + kBlankMs));
    CHECK(m.state().blanked == true);                               // ★ ...and it still blanks on the press's edge
}

// ★★★★ THE COMPOSITION WITH §UI-17 S2's RETENTION, IN THE REAL LOOP'S ORDER — `mr_ui_tick` builds ONE snapshot and
//      calls `on_gesture` then `on_tick` against it, and `mr_ui_on_push` runs BEFORE that pass with the same clock.
//      ⛔ THE HALF THAT IS INVISIBLE TO A HARNESS THAT ONLY WAKES AND ASSERTS: without `unblank`'s cadence restart
//      the waking tick itself banks the whole dark interval and turns the page before a single frame has shown the
//      operator what they came back to (S06's property, arriving through the message door instead of the press one).
TEST_CASE("ui17-wake: a message wake keeps the retained modal's page, through the real wake pass") {
    UiModel m; auto s = snap_inbox(1);
    to_inbox(m, s);
    CHECK(open_detail(m, s, big_body_7pages(), 241) == true);
    CHECK(m.state().detail_pages == 7);                             // ⛔ non-vacuity: >1, so the cadence is armed
    m.on_tick(snap_inbox(1, 1000 + kDetailPageMs));
    m.on_tick(snap_inbox(1, 1000 + 2 * kDetailPageMs));
    CHECK(m.state().detail_page == 2);
    m.on_tick(snap_inbox(1, 1000 + kBlankMs));
    CHECK(m.state().blanked == true);
    CHECK(m.state().detail_page == 2);
    for (uint32_t k = 1; k <= 4; ++k) m.on_tick(snap_inbox(1, 1000 + kBlankMs + k * kDetailPageMs));
    CHECK(m.state().detail_page == 2);                              // ⛔ the dark cadence is suspended (S05)
    // ---- THE MESSAGE, then the tick that shares its clock: a LONG dark interval is banked if the restart is missing.
    const uint32_t wake_ms = 1000 + kBlankMs + 9 * kDetailPageMs;
    m.on_msg_wake(wake_ms);
    m.on_tick(snap_inbox(1, wake_ms));
    CHECK(m.state().blanked     == false);
    CHECK(m.state().detail      == InboxModal::body);
    CHECK(m.state().detail_page == 2);                              // ★★ the SAME page the operator left
    CHECK(m.state().detail_seq  == 1u);                             // ★ ...of the same record
    // ...and the cadence resumes from the WAKE, one full period later — never a millisecond early.
    m.on_tick(snap_inbox(1, wake_ms + kDetailPageMs - 1));
    CHECK(m.state().detail_page == 2);
    m.on_tick(snap_inbox(1, wake_ms + kDetailPageMs));
    CHECK(m.state().detail_page == 3);
}
