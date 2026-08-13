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

// ---------------------------------------------------------------- the plan's seven cases

TEST_CASE("ui-model: short press is LIST-AWARE: it walks TEAM before leaving it") {
    UiModel m; const auto s = snap();
    m.on_gesture(Gesture::short_press, s); CHECK(m.state().screen == Screen::team);   CHECK(m.state().cursor == 0);
    m.on_gesture(Gesture::short_press, s); CHECK(m.state().screen == Screen::team);   CHECK(m.state().cursor == 1);
    m.on_gesture(Gesture::short_press, s); CHECK(m.state().screen == Screen::team);   CHECK(m.state().cursor == 2);
    m.on_gesture(Gesture::short_press, s); CHECK(m.state().screen == Screen::inbox);
    m.on_gesture(Gesture::short_press, s); CHECK(m.state().screen == Screen::send);
    // ★ §UI-14 (spec §3.1): SETTINGS is appended to the cycle, so SEND no longer wraps straight to STATUS. It is a
    //   LIST screen too — with no service attached the row list is still built, so the walk takes its rows first.
    m.on_gesture(Gesture::short_press, s); CHECK(m.state().screen == Screen::settings);
}

TEST_CASE("ui-model: an empty TEAM list is passed through, not a dead end") {
    UiModel m; auto s = snap(); s.team_shown = 0; s.team_total = 0;
    m.on_gesture(Gesture::short_press, s); CHECK(m.state().screen == Screen::team);
    m.on_gesture(Gesture::short_press, s); CHECK(m.state().screen == Screen::inbox);
}

TEST_CASE("ui-model: double on TEAM opens the DM sub-view bound to the highlighted peer") {
    UiModel m; const auto s = snap();
    m.on_gesture(Gesture::short_press, s); m.on_gesture(Gesture::short_press, s);   // cursor 1
    m.on_gesture(Gesture::double_press, s);
    CHECK(m.state().compose == Compose::dm);
    CHECK(m.state().compose_peer == s.team[1].id);
    CHECK(m.state().cursor == 0);
}

TEST_CASE("ui-model: sub-view: `back` leaves without sending") {
    UiModel m; const auto s = snap(); SendReq req{};
    m.on_gesture(Gesture::short_press, s); m.on_gesture(Gesture::double_press, s);
    m.on_gesture(Gesture::short_press, s); m.on_gesture(Gesture::short_press, s);    // -> back
    m.on_gesture(Gesture::double_press, s);
    CHECK(m.state().compose == Compose::none);
    CHECK(m.state().screen  == Screen::team);
    CHECK(m.take_send_request(req) == false);
}

TEST_CASE("ui-model: sub-view: double on a message emits a DM request for the bound peer") {
    UiModel m; const auto s = snap(); SendReq req{};
    m.on_gesture(Gesture::short_press, s); m.on_gesture(Gesture::double_press, s);
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

TEST_CASE("ui-model: sub-view auto-exits on inactivity WITHOUT sending") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::short_press, snap(1000)); m.on_gesture(Gesture::double_press, snap(1100));
    m.on_tick(snap(1100 + kBlankMs + 1));
    CHECK(m.state().compose == Compose::none);
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
    m.on_gesture(Gesture::short_press, s);   // -> team
    m.on_gesture(Gesture::short_press, s); m.on_gesture(Gesture::short_press, s);
    m.on_gesture(Gesture::short_press, s);   // -> inbox
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
    for (int i = 0; i < 4; ++i) m.on_gesture(Gesture::short_press, s);   // status -> team(0,1,2) -> inbox
    CHECK(m.state().screen == Screen::inbox);
    m.on_gesture(Gesture::double_press, s);
    CHECK(m.state().detail == InboxModal::closed);
    CHECK(m.state().inbox_pick_gone == false);
    CHECK(m.take_inbox_request(rq) == false);
    CHECK(m.take_send_request(req) == false);
}

TEST_CASE("ui-model: INBOX is list-aware too — the cursor walks its rows before the screen moves") {
    UiModel m; const auto s = snap_inbox(3);
    for (int i = 0; i < 4; ++i) m.on_gesture(Gesture::short_press, s);   // status -> team(0,1,2) -> inbox
    CHECK(m.state().screen == Screen::inbox); CHECK(m.state().cursor == 0);
    m.on_gesture(Gesture::short_press, s); CHECK(m.state().screen == Screen::inbox); CHECK(m.state().cursor == 1);
    m.on_gesture(Gesture::short_press, s); CHECK(m.state().screen == Screen::inbox); CHECK(m.state().cursor == 2);
    m.on_gesture(Gesture::short_press, s); CHECK(m.state().screen == Screen::send);  CHECK(m.state().cursor == 0);
}

// The SEND screen is single-item, so `short` just moves on; `double` opens the CHANNEL list (spec §3.2.2), whose
// rows are "Got your message" / "All good" / back — three, like the DM list, but a different SendKind.
TEST_CASE("ui-model: SEND double opens the channel compose list and index 1 sends the second canned text") {
    UiModel m; const auto s = snap(); SendReq req{};
    for (int i = 0; i < 5; ++i) m.on_gesture(Gesture::short_press, s);   // -> send
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
    for (int i = 0; i < 5; ++i) m.on_gesture(Gesture::short_press, s);
    m.on_gesture(Gesture::double_press, s);
    m.on_gesture(Gesture::short_press, s); m.on_gesture(Gesture::short_press, s);   // -> back (index 2)
    CHECK(m.state().cursor == 2);
    m.on_gesture(Gesture::double_press, s);
    CHECK(m.state().compose == Compose::none);
    CHECK(m.take_send_request(req) == false);
}

TEST_CASE("ui-model: the compose cursor wraps within the list, so `back` is always reachable") {
    UiModel m; const auto s = snap();
    m.on_gesture(Gesture::short_press, s); m.on_gesture(Gesture::double_press, s);   // DM list, cursor 0
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
    m.on_gesture(Gesture::short_press, s); m.on_gesture(Gesture::short_press, s);   // TEAM cursor 1 -> id 11
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
    for (int i = 0; i < 3; ++i) m.on_gesture(Gesture::short_press, s);   // TEAM, cursor 2 -> teammate id 12
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
    for (int i = 0; i < 3; ++i) m.on_gesture(Gesture::short_press, s);   // TEAM, cursor 2 -> teammate id 12
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
    for (int i = 0; i < 3; ++i) m.on_gesture(Gesture::short_press, s);   // TEAM, cursor 2 -> teammate id 12
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
    m.on_gesture(Gesture::short_press, s); m.on_gesture(Gesture::short_press, s);   // TEAM cursor 1 -> id 11
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
    m.on_gesture(Gesture::short_press, s); m.on_gesture(Gesture::short_press, s);   // TEAM cursor 1 -> id 11
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

// ★ LEAVING THE SCREEN RETIRES THE MESSAGE TOO — otherwise a stale "TEAMMATE GONE, repick" would reappear the next time
//   the cycle came back round to TEAM, describing a pick from minutes ago.
TEST_CASE("ui-model: B64 — cycling off the TEAM screen retires the refusal message") {
    UiModel m; auto s = snap();
    for (int i = 0; i < 3; ++i) m.on_gesture(Gesture::short_press, s);   // TEAM, cursor 2 -> teammate id 12
    s.team_shown = 2; s.team_total = 2;
    m.on_gesture(Gesture::double_press, s);
    CHECK(m.state().team_pick_gone == true);
    m.on_gesture(Gesture::short_press, s);                               // cursor 2 is past the 2-row list -> INBOX
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
    m.on_gesture(Gesture::short_press, s); m.on_gesture(Gesture::double_press, s);
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

TEST_CASE("ui-model: the modal does NOT auto-exit before the blank window elapses") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::short_press, snap(1000));
    m.on_gesture(Gesture::double_press, snap(1100));
    m.on_tick(snap(1100 + kBlankMs - 1));
    CHECK(m.state().compose == Compose::dm);
    m.on_tick(snap(1100 + kBlankMs));
    CHECK(m.state().compose == Compose::none);
    CHECK(m.take_send_request(req) == false);
}

TEST_CASE("ui-model: a gesture inside the modal refreshes its inactivity window") {
    UiModel m;
    m.on_gesture(Gesture::short_press, snap(1000));
    m.on_gesture(Gesture::double_press, snap(1100));
    m.on_gesture(Gesture::short_press, snap(1100 + kBlankMs - 100));   // still browsing the list
    m.on_tick(snap(1100 + kBlankMs + 10));
    CHECK(m.state().compose == Compose::dm);                           // window restarted, not expired
}

TEST_CASE("ui-model: blanking with a modal open closes the modal and the waking press shows the parent") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::short_press, snap(1000));
    m.on_gesture(Gesture::double_press, snap(1100));
    m.on_tick(snap(1100 + kBlankMs + 1));
    CHECK(m.state().blanked == true);
    CHECK(m.state().compose == Compose::none);
    m.on_gesture(Gesture::short_press, snap(1100 + kBlankMs + 50));
    CHECK(m.state().blanked == false);
    CHECK(m.state().screen  == Screen::team);      // the parent screen, and the press was consumed
    CHECK(m.state().compose == Compose::none);
    CHECK(m.take_send_request(req) == false);
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

TEST_CASE("ui-model: the modal inactivity exit is wrap-safe too") {
    UiModel m;
    m.on_gesture(Gesture::short_press, snap(0xFFFFF000u));
    m.on_gesture(Gesture::double_press, snap(0xFFFFF100u));
    CHECK(m.state().compose == Compose::dm);
    m.on_tick(snap(0x00000500u));
    CHECK(m.state().compose == Compose::dm);                    // ~5 s elapsed, not a wrapped eternity
    m.on_tick(snap(0x00003000u));
    CHECK(m.state().compose == Compose::none);
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
    m.on_gesture(Gesture::short_press, s); m.on_gesture(Gesture::double_press, s);
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

TEST_CASE("ui-model: a full team roster walks every one of the eight rows") {
    UiModel m; UiSnapshot s{};
    s.now_ms = 1000; s.team_shown = kMaxTeamRows; s.team_total = 12;   // truncated view, true total larger
    for (uint8_t i = 0; i < kMaxTeamRows; ++i) s.team[i].id = uint8_t(20 + i);
    m.on_gesture(Gesture::short_press, s);
    CHECK(m.state().screen == Screen::team);
    for (uint8_t i = 1; i < kMaxTeamRows; ++i) {
        m.on_gesture(Gesture::short_press, s);
        CHECK(m.state().screen == Screen::team);
        CHECK(m.state().cursor == i);
    }
    m.on_gesture(Gesture::short_press, s);
    CHECK(m.state().screen == Screen::inbox);
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
    m.on_gesture(Gesture::short_press, s); m.on_gesture(Gesture::double_press, s);
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
    m.on_gesture(Gesture::short_press, s); m.on_gesture(Gesture::double_press, s);   // DM modal
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
    for (int i = 0; i < 5; ++i) m.on_gesture(Gesture::short_press, s);   // navigate to SEND
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
    m.on_gesture(Gesture::short_press,  s);                      // -> TEAM
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
    m.on_gesture(Gesture::short_press,  snap(1000));           // status -> team
    CHECK(m.state().screen == Screen::team);
    m.on_gesture(Gesture::double_press, snap(1100));           // open the DM compose sub-view
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
    m.on_gesture(Gesture::short_press, snap(1000));             // status -> team
    CHECK(m.state().screen == Screen::team);
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
    m.on_gesture(Gesture::short_press, snap(1000));             // status -> team
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
    m.on_gesture(Gesture::short_press,  snap(1000));            // status -> team
    m.on_gesture(Gesture::double_press, snap(1100));            // open the DM compose modal
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
    m.on_gesture(Gesture::short_press, snap(1000));  // -> team
    for (int i = 0; i < 3; ++i) m.on_gesture(Gesture::short_press, snap(1000));   // walk team -> inbox -> send
    m.on_gesture(Gesture::short_press, snap(1000));
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
    m.on_gesture(Gesture::short_press, snap(1000));             // -> TEAM
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
TEST_CASE("ui7-result: the kBlankMs auto-exit clears the result phase, so a re-opened modal shows its LIST") {
    UiModel m = dm_sent();
    m.on_tick(snap(1000 + kBlankMs + 1));
    CHECK(m.state().compose == Compose::none);
    CHECK(m.state().compose_result == false);
    // ⓘ The same tick that auto-exits the modal also BLANKS the panel (both deadlines are kBlankMs from the last
    //   input), and the waking press is CONSUMED (spec :378) — so re-opening takes two gestures here, not one. That
    //   is the shipped blank contract, not an artefact of this case.
    m.on_gesture(Gesture::double_press, snap(2000 + kBlankMs));  // wakes; consumed
    CHECK(m.state().blanked == false);
    m.on_gesture(Gesture::double_press, snap(2100 + kBlankMs));  // re-open from TEAM
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

TEST_CASE("ui7-inbox: within a kind the NEWEST rows win, because pull() hands them oldest-first") {
    InboxRowBudget b; UiSnapshot s{};
    for (uint8_t i = 0; i < uint8_t(kInboxRowsPerKind + 3); ++i) b.add(row(true, 0, i));
    b.publish(s, uint16_t(kInboxRowsPerKind + 3));
    CHECK(s.inbox_shown == kInboxRowsPerKind);
    CHECK(s.inbox[0].rx_age_s == 3u);                             // the three oldest were displaced
    CHECK(s.inbox[kInboxRowsPerKind - 1].rx_age_s == uint32_t(kInboxRowsPerKind + 2));
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

// Walk status -> team(0,1,2) -> inbox, leaving the cursor on inbox row 0 (snap_inbox's roster is 3 members).
static void to_inbox(UiModel& m, const UiSnapshot& s) {
    for (int i = 0; i < 4; ++i) m.on_gesture(Gesture::short_press, s);
}
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

TEST_CASE("ui7d-modal: the geometry is DERIVED — 42 chars a page, six pages for the largest body, 2 s a page") {
    CHECK(kDetailCols == 21);
    CHECK(kDetailBodyRows == 2);
    CHECK(kDetailPageChars == 42);
    CHECK(kDetailMaxPages == 6);                                 // spec §3.5's "at most six pages", as a consequence
    CHECK(kDetailPageMs == 2000u);
    CHECK(uint16_t(kDetailMaxPages) * kDetailPageChars >= MESHROUTE_NS::protocol::inbox_max_body);
}

TEST_CASE("ui7d-modal: pages = max(1, ceil(body_len / 42)) at every boundary, and NEVER zero") {
    static uint8_t big[MESHROUTE_NS::protocol::inbox_max_body];
    for (uint16_t i = 0; i < sizeof big; ++i) big[i] = uint8_t('a' + (i % 26));
    struct { uint16_t len; uint8_t pages; } cases[] = {
        {0, 1}, {1, 1}, {42, 1}, {43, 2}, {84, 2}, {85, 3}, {MESHROUTE_NS::protocol::inbox_max_body, 6},
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

TEST_CASE("ui7d-modal: the body is WRAPPED into two 21-column rows without dropping a byte") {
    static uint8_t b[45];
    for (uint8_t i = 0; i < sizeof b; ++i) b[i] = uint8_t('A' + (i % 26));
    UiModel m; auto s = snap_inbox(1);
    to_inbox(m, s);
    CHECK(open_detail(m, s, b, sizeof b) == true);
    CHECK(m.state().detail_pages == 2);
    CHECK(std::strlen(m.state().detail_line[0]) == 21);
    CHECK(std::strlen(m.state().detail_line[1]) == 21);
    CHECK(std::strncmp(m.state().detail_line[0], "ABCDEFGHIJKLMNOPQRSTU", 21) == 0);
    CHECK(std::strncmp(m.state().detail_line[1], "VWXYZABCDEFGHIJKLMNOP", 21) == 0);
    // page 2 holds the remaining three bytes, and every one of the 45 has now been shown
    m.on_tick(snap_inbox(1, 1000 + kDetailPageMs));
    CHECK(m.state().detail_page == 1);
    CHECK(std::strcmp(m.state().detail_line[0], "QRS") == 0);
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
//     deadline (spec §3.5). If it did, a long body would hold the modal — and its selected action — open for ever.
TEST_CASE("ui7d-modal: paging does NOT postpone the inactivity timeout — the modal still closes at kBlankMs") {
    static uint8_t b[241];
    for (uint16_t i = 0; i < sizeof b; ++i) b[i] = 'y';
    UiModel m; auto s = snap_inbox(1);
    to_inbox(m, s);
    CHECK(open_detail(m, s, b, uint8_t(sizeof b)) == true);
    CHECK(m.state().detail_pages == 6);
    for (uint32_t k = 1; k * kDetailPageMs < kBlankMs; ++k) m.on_tick(snap_inbox(1, 1000 + k * kDetailPageMs));
    CHECK(m.state().detail == InboxModal::body);                 // still open just inside the window
    m.on_tick(snap_inbox(1, 1000 + kBlankMs));
    CHECK(m.state().detail == InboxModal::closed);               // ★ closed on time, having paged seven times
    CHECK(m.state().screen == Screen::inbox);                    // ...and back on the list
}

TEST_CASE("ui7d-modal: the ordinary timeout deletes NOTHING") {
    UiModel m; auto s = snap_inbox(1);
    to_inbox(m, s);
    CHECK(open_detail(m, s, kBody7, sizeof kBody7) == true);
    m.on_gesture(Gesture::short_press, s);                       // arm `delete`, then walk away
    CHECK(m.state().detail_action == InboxAction::del);
    m.on_tick(snap_inbox(1, 1000 + kBlankMs));
    CHECK(m.state().detail == InboxModal::closed);
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
    CHECK(std::strcmp(l, "CH255 from 255 6/6") == 0);            // the widest real expansion: 18 of 21 columns
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
bool cursor_to(UiModel& m, const UiSnapshot& s, CfgRow want) {
    for (int i = 0; i < 60; ++i) {
        CfgRow r{};
        m.on_tick(s);          // ...which is why this ticks first — see `row_under_cursor`
        if (m.state().screen == Screen::settings && row_under_cursor(m, s, r) && r == want) return true;
        m.on_gesture(Gesture::short_press, s);
    }
    return false;
}
UiSnapshot cfg_snap(uint32_t now_ms = 1000, bool ble_row = false) {
    UiSnapshot s = snap(now_ms); s.ble_row = ble_row; return s;
}

}  // namespace

// ---------------------------------------------------------------------------------------------- the row list
TEST_CASE("ui14-rows: the menu is §3.6.2's, and the BLE row is ABSENT when the transport is not compiled") {
    const CfgRowList l = settings_rows(/*ble_row=*/false, /*conflict=*/false);
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
    const CfgRowList l = settings_rows(/*ble_row=*/true, /*conflict=*/false);
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
    const CfgRowList clean = settings_rows(false, /*conflict=*/false);
    CfgRow r{};
    for (uint8_t i = 0; i < clean.n; ++i) { CHECK(clean.at(i, r)); CHECK(r != CfgRow::reload); }
    const CfgRowList conf = settings_rows(false, /*conflict=*/true);
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
    to_settings(f.m, s);
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
    f.m.on_gesture(Gesture::short_press, s);               // ...and leaves only at the end (§3.2)
    CHECK(f.m.state().screen == Screen::status);
    CHECK(f.m.state().settings == Settings::closed);       // ⛔ the editor state never survives the screen
}

// ---------------------------------------------------------------------------------------------- short's two modes
TEST_CASE("ui14-edit: `double` ENTERS a value row and `short` then CYCLES ITS VALUE — the draft only") {
    CfgFix f; const auto s = cfg_snap();
    to_settings(f.m, s);
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
    to_settings(f.m, s);
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
    to_settings(f.m, s);
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
    to_settings(f.m, s);
    CHECK(cursor_to(f.m, s, CfgRow::save));
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().cfg_save == mrfw::CfgSave::no_change);
    CHECK(strcmp(settings_note(f.m.state()), "NO CHANGE") == 0);
    CHECK(f.store.writes == 0);
    CHECK(f.live.applies == 0);
}

TEST_CASE("ui14-save: a FAILED write says SAVE FAILED and RETAINS the draft and its marker") {
    CfgFix f; const auto s = cfg_snap();
    to_settings(f.m, s);
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
    to_settings(f.m, s);
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
    to_settings(f.m, s);
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
    to_settings(f.m, s);
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
    to_settings(f.m, s);
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

TEST_CASE("ui14-back: BACK is safe — it leaves the screen and PRESERVES the unsaved draft") {
    CfgFix f; const auto s = cfg_snap();
    to_settings(f.m, s);
    CHECK(cursor_to(f.m, s, CfgRow::e2e_dm));
    f.m.on_gesture(Gesture::double_press, s);
    f.m.on_gesture(Gesture::short_press, s);
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.svc.config_unsaved() == true);
    CHECK(cursor_to(f.m, s, CfgRow::back));
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().screen == Screen::status);
    CHECK(f.m.state().settings == Settings::closed);
    CHECK(f.m.state().cursor == 0);
    // ★★ THE WHOLE POINT: the draft is still there, the marker is still up, and the service is still open.
    CHECK(f.svc.config_unsaved() == true);
    CHECK(f.svc.draft().at(mrfw::CfgField::e2e_dm) == 1);
    CHECK(f.svc.is_open() == true);
    CHECK(f.store.writes == 0);
    // ...and STATUS is where the marker is seen without cycling back (§3.3)
    CHECK(strcmp(cfg_marker_text(f.svc.config_unsaved(), f.svc.conflict()), "CFG* UNSAVED") == 0);
    // RE-ENTERING must not reset it either — `CfgOpen::already_open` is a no-op by contract
    to_settings(f.m, s);
    CHECK(f.m.state().screen == Screen::settings);
    CHECK(f.svc.draft().at(mrfw::CfgField::e2e_dm) == 1);
    CHECK(f.svc.config_unsaved() == true);
}

TEST_CASE("ui14-back: BLANKING preserves the draft too — a timeout may never discard (§3.6.1)") {
    CfgFix f; auto s = cfg_snap();
    to_settings(f.m, s);
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

// ---------------------------------------------------------------------------------------------- PROVISION
TEST_CASE("ui14-provision: the row is PRESENT and INERT — it refuses out loud and changes nothing (§UI-15)") {
    CfgFix f; const auto s = cfg_snap();
    to_settings(f.m, s);
    CHECK(cursor_to(f.m, s, CfgRow::e2e_dm));
    f.m.on_gesture(Gesture::double_press, s);
    f.m.on_gesture(Gesture::short_press, s);                 // leave an UNSAVED draft standing...
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(cursor_to(f.m, s, CfgRow::provision));
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().cfg_provision_na == true);
    CHECK(strcmp(settings_note(f.m.state()), "PROVISION: UI-15") == 0);
    CHECK(f.m.state().screen == Screen::settings);           // ⛔ it opened NOTHING
    CHECK(f.m.state().settings == Settings::browsing);
    CHECK(f.store.writes == 0);
    CHECK(f.live.applies == 0);
    // ⛔ AND §3.6.3's "an unsaved draft requires SAVE or DISCARD first" is NOT implemented here — that precondition is
    //    §UI-15's. The unsaved draft is untouched and was not a factor in the refusal.
    CHECK(f.svc.config_unsaved() == true);
    f.m.on_gesture(Gesture::short_press, s);                 // the note is transient, like every other one
    CHECK(f.m.state().cfg_provision_na == false);
}

// ---------------------------------------------------------------------------------------------- the LONG gesture
TEST_CASE("ui14-long: `long_arm` LEAVES THE EDITOR — and a `long_cancel` does not bring it back") {
    CfgFix f; const auto s = cfg_snap();
    to_settings(f.m, s);
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
    to_settings(f.m, s);
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
    to_settings(f.m, s);
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
TEST_CASE("ui14-open: a store that cannot produce a record REFUSES to open, and every activation is refused") {
    UiFakeStore store; UiFakeLive live;
    store.can_load = false;
    mrfw::ConfigService svc{store, live};
    UiModel m; m.attach_config(svc);
    const auto s = cfg_snap();
    to_settings(m, s);
    CHECK(m.state().screen == Screen::settings);
    CHECK(svc.is_open() == false);                           // ⛔ the panel says CFG UNAVAILABLE and offers nothing
    CHECK(cursor_to(m, s, CfgRow::e2e_dm));
    m.on_gesture(Gesture::double_press, s);
    CHECK(m.state().settings == Settings::browsing);          // ⛔ no editor over a draft that does not exist
    CHECK(cursor_to(m, s, CfgRow::save));
    m.on_gesture(Gesture::double_press, s);
    CHECK(m.state().cfg_save == mrfw::CfgSave::not_open);
    CHECK(store.writes == 0);
    CHECK(live.applies == 0);
    // ...and BACK still works, because leaving must never depend on the store
    CHECK(cursor_to(m, s, CfgRow::back));
    m.on_gesture(Gesture::double_press, s);
    CHECK(m.state().screen == Screen::status);
}

TEST_CASE("ui14-open: an UNATTACHED model shows the menu and refuses every activation (fails closed)") {
    UiModel m; const auto s = cfg_snap();                     // ⛔ no attach_config at all
    to_settings(m, s);
    CHECK(m.state().screen == Screen::settings);
    CHECK(m.config() == nullptr);
    CHECK(m.settings_row_list(s).n == 7);
    m.on_gesture(Gesture::double_press, s);                   // on e2e_dm: nothing to edit
    CHECK(m.state().settings == Settings::browsing);
    CHECK(m.state().cfg_have_save == false);
}

// ---------------------------------------------------------------------------------------------- the marker's bytes
TEST_CASE("ui14-marker: §3.3's three literals are three separate facts, and CONFLICT wins the marker row") {
    CHECK(strcmp(cfg_marker_text(/*unsaved=*/false, /*conflict=*/false), "") == 0);
    CHECK(strcmp(cfg_marker_text(true,  false), "CFG* UNSAVED") == 0);
    CHECK(strcmp(cfg_marker_text(false, true),  "CFG! RELOAD") == 0);
    // ★ a conflicted draft is BOTH, and the row says the one that BLOCKS the save — the other two facts have their
    //   own places (the SETTINGS title, and the RESTART row).
    CHECK(strcmp(cfg_marker_text(true, true), "CFG! RELOAD") == 0);
    // ...and the conflict string is the SERVICE's, not a copy
    CHECK(strcmp(cfg_marker_text(false, true), mrfw::cfg_save_panel(mrfw::CfgSave::conflict)) == 0);
    // WIDTH: `STATUS ` + the marker must fit the panel's 21 small-font columns (§3.3 forbids shortening the bar)
    CHECK(strlen("STATUS ") + strlen(cfg_marker_text(true, false)) <= 21u);
    CHECK(strlen("STATUS ") + strlen(cfg_marker_text(false, true)) <= 21u);
    CHECK(strlen(kCfgRestartText) <= 21u);
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
    st.cfg_provision_na   = true;  CHECK(strcmp(settings_note(st), "PROVISION: UI-15") == 0);
    for (uint8_t i = 0; i < 8; ++i) CHECK(strlen(settings_note(st)) <= 21u);   // every note fits the panel
}

// ★★★★ THE ROW SHIFT, AND IT IS LIVE RATHER THAN HYPOTHETICAL: the conditional RELOAD row appears at the exact
//     moment a refused SAVE raises the conflict, so the list grows by one UNDER THE CURSOR. A cursor held as an INDEX
//     would then be highlighting RELOAD while the operator was aiming at SAVE — §B64's ruling and §B66's lesson, on a
//     third screen. ⓘ The negative half is asserted too: the identity does NOT move on its own.
TEST_CASE("ui14-cursor: the highlight follows the ROW when a conflict inserts one above it") {
    CfgFix f; const auto s = cfg_snap();
    to_settings(f.m, s);
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
    to_settings(f.m, s);
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
    to_settings(f.m, s);
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
    to_settings(f.m, s);
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
    to_settings(f.m, s);
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
    to_settings(f.m, s);
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
    to_settings(f.m, s);
    CHECK(f.svc.is_open() == true);
    // ★ the baseline is whatever the record holds NOW, so the companion's change is simply the starting state
    CHECK(f.svc.draft().at(mrfw::CfgField::e2e_dm) == 1);
    CHECK(f.svc.config_unsaved() == false);
    CHECK(f.svc.conflict() == false);
    CHECK(f.m.settings_row_list(s).n == 7);
}
