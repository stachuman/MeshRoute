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

// ★★★★ §UI-10/11 P3 — **THE COMPILED CATALOG, AS A RECORD**, because that is now what a compose list is projected
//      from. `preset_defaults` is what `PresetCatalog::begin()` runs for an ABSENT `/mrui`, i.e. what an ordinary
//      unconfigured device shows — so a snapshot published from it renders exactly the two DM and two channel rows
//      the panel shipped before this slice. ⓘ It is a helper rather than a global so a case that wants a DIFFERENT
//      catalog (a gapped one, an empty one, a located one) simply builds its own and publishes that.
static mrnv::UiPresetBlob preset_defaults_blob() {
    mrnv::UiPresetBlob b{};
    mrfw::preset_defaults(b);
    return b;
}
// A 3-member team, nothing in the inbox. `now_ms` is the only field the timing cases vary.
// ⓘ §UI-10/11 P3: it PUBLISHES the compiled catalog. ⛔ `UiSnapshot{}` deliberately carries EMPTY lists (the honest
//   unpublished state — see the field's own block), so every case that walks a compose list must say which catalog
//   it is driving. This helper says "the compiled one", which is what the landed cases were always about.
static UiSnapshot snap(uint32_t now_ms = 1000) {
    UiSnapshot s{};
    s.now_ms = now_ms; s.team_shown = 3; s.team_total = 3; s.unread_dm = 2; s.unread_ch = 5; s.batt_mv = 3900;
    for (uint8_t i = 0; i < 3; ++i) { s.team[i].id = uint8_t(10 + i); s.team[i].last_heard_s = 60; }
    ui_snapshot_publish_presets(s, preset_defaults_blob());
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
    CHECK(req.kind == SendKind::dm); CHECK(req.peer_id == s.team[0].id);
    CHECK(req.slot == mrfw::kPresetDmFirst);            // ★ §UI-10/11 P3 — the STABLE slot `dm1`, ⛔ not a row index
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
    CHECK(req.slot == uint8_t(mrfw::kPresetChannelFirst + 1));   // ★ §UI-10/11 P3 — `channel2`, the second row
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
        CHECK(req.slot == uint8_t(mrfw::kPresetDmFirst + 1));            // ★ and still the text the user chose
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
    // ★★★★ §UI-10/11 P3 — the two FIXED COUNTS are RETIRED with their tables, and the WITHDRAWN lines are kept
    //      visible: `CHECK(kDmTextCount == 3)` / `CHECK(kChannelTextCount == 3)`. A compose list's length is no
    //      longer a compile-time bound at all — it is the ENABLED count of the wearer's catalog — so what replaces
    //      them is the CAPACITY (fixed by the record, §3.2.2's *"eight DM slots and eight channel slots"*) plus the
    //      derived row count over a projection of the COMPILED defaults, which is exactly the landed 2 + back = 3.
    CHECK(mrfw::kPresetPerKind == 8);
    {
        ComposeList dm{}, ch{};
        compose_project(preset_defaults_blob(), mrfw::PresetKind::dm,      dm);
        compose_project(preset_defaults_blob(), mrfw::PresetKind::channel, ch);
        CHECK(compose_row_count(dm, /*grant=*/false) == 3);   // "Are you OK?", "I'm OK", back, don't send
        CHECK(compose_row_count(ch, /*grant=*/false) == 3);   // "Got your message", "All good", back, don't send
    }
    CHECK(uint8_t(Screen::count) == 5);              // §UI-14: STATUS/TEAM/INBOX/SEND/SETTINGS (spec §3.1)
    UiSnapshot s{};
    // ★ §UI-10/11 P3: the HONEST UNPUBLISHED STATE — no rows and no generation, so an unpublished snapshot lands on
    //   §3.2.1's visible empty state rather than on a plausible-looking catalog nobody configured.
    CHECK(s.preset_generation == 0u);
    CHECK(s.preset_dm.n == 0);
    CHECK(s.preset_ch.n == 0);
    CHECK(compose_empty_note(s.preset_dm) != nullptr);
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

// ★ §B66's durable closure, asserted rather than assumed. ⛔ THE WITHDRAWN FORM IS KEPT VISIBLE — it read the two
//   counts back out of the two tables (`kDmTextCount == sizeof kDmTexts / sizeof kDmTexts[0]`, and `back` as
//   `kDmTexts[kDmTextCount - 1]`). §UI-10/11 P3 retired the tables; the CURE is unchanged and now stronger, because
//   `back` is no longer the last element of a table at all — it is a DERIVED ROW KIND, so no catalog edit of any
//   size can turn it into a SEND. That is asserted here over catalogs of EVERY length the record allows.
TEST_CASE("ui7-B66: `back, don't send` is the derived LAST row of every compose list, at every catalog size") {
    for (uint8_t enabled = 0; enabled <= mrfw::kPresetPerKind; ++enabled) {
        mrnv::UiPresetBlob b{};
        mrfw::preset_defaults(b);
        for (uint8_t i = 0; i < mrfw::kPresetPerKind; ++i) {
            char t[8] = { 'd', 'm', char('0' + i), 0 };
            mrfw::preset_slot_put(b.slot[mrfw::kPresetDmFirst + i], i < enabled, false, t, 3);
            char c[8] = { 'c', 'h', char('0' + i), 0 };
            mrfw::preset_slot_put(b.slot[mrfw::kPresetChannelFirst + i], i < enabled, false, c, 3);
        }
        ComposeList dm{}, ch{};
        compose_project(b, mrfw::PresetKind::dm,      dm);
        compose_project(b, mrfw::PresetKind::channel, ch);
        CHECK(dm.n == enabled);
        CHECK(ch.n == enabled);
        for (const ComposeList* l : { &dm, &ch })
            for (bool grant : { false, true }) {
                const uint8_t n = compose_row_count(*l, grant);
                CHECK(n == uint8_t(enabled + (grant ? 1 : 0) + 1));
                CHECK(compose_row_kind(uint8_t(n - 1), *l, grant) == ComposeRow::back);   // ★ ONE `back`, and LAST
                CHECK(std::strcmp(compose_row_text(uint8_t(n - 1), *l, grant), "back, don't send") == 0);
                // ⛔ ...and it is ONE row, not two: nothing below the last row is a `back` a walk could reach twice.
                uint8_t backs = 0;
                for (uint8_t i = 0; i < n; ++i) if (compose_row_kind(i, *l, grant) == ComposeRow::back) ++backs;
                CHECK(backs == 1);
                // ⛔ AND `back` CARRIES NO LOCATION COLUMN (R-1's rule, and the reason K7's row is untouched).
                CHECK(compose_row_loc_marker(uint8_t(n - 1), *l, grant) == '\0');
            }
    }
    CHECK(std::strcmp(kComposeBackText, "back, don't send") == 0);
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
// ⚠ §UI-16 N2 ADDED THE THIRD PREDICATE **BEFORE** `now_ms`, deliberately: the three children belong together and a
//   `bool` appended after the clock would read as an afterthought. ⛔ Every call that passed a positional `now_ms`
//   was updated with it — a `uint32_t` landing in a `bool` parameter converts SILENTLY, so the parameter order was
//   chosen to make the compiler's arity the thing that catches a miss rather than a plausible-looking `true`.
// ⚠ §UI-16 N4's `invite` predicate is appended AFTER `now_ms` DELIBERATELY: the three landed children and the
// clock are positional in ~40 call sites, and re-ordering them would be a refactor riding a feature (C1). Its
// default is FALSE, i.e. the row is hidden unless a case asks for it — the same direction the snapshot's own
// default takes, and for the same reason (a fixture that published nothing must offer nothing).
// ⚠ §UI-16 K6's `saved_keys` predicate is appended LAST, after `invite`, for the identical reason that one was
// appended after `now_ms`: the landed children and the clock are positional in ~40 call sites and re-ordering them
// would be a refactor riding a feature (C1). Its default is FALSE — the row is hidden unless a case asks for it, so
// every landed case keeps the exact four-child menu it was written against.
UiSnapshot prov_snap(bool create_team = true, bool join_static = true, bool join_team = true,
                     uint32_t now_ms = 1000, bool invite = false, bool saved_keys = false) {
    UiSnapshot s = cfg_snap(now_ms);
    s.prov_create_team = create_team; s.prov_join_static = join_static; s.prov_join_team = join_team;
    s.prov_invite = invite;
    s.prov_saved_keys = saved_keys;
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
    // ★ §UI-16 N2's arm, APPENDED — the eight above keep their values, so no landed case's arm moved.
    CHECK(uint8_t(Provision::nearby)         == 8);
    // ★ §UI-16 N3's, appended the same way — and it is the slice's ONE new arm (its RESULT reuses `create_result`,
    //   which renders `prov_answer` and nothing else). The nine above keep their values.
    CHECK(uint8_t(Provision::nearby_confirm) == 9);
    // ★ §UI-16 N4's THREE, appended the same way — the ten above keep their values, so no landed case's arm moved.
    //   ⓘ `invite_closed` is a SCREEN and not a sub-state of `invite`: the window is over on it, which is exactly
    //   what `provision_is_invite` (the deadline's clearing term and the handled set's discard) must be able to say.
    CHECK(uint8_t(Provision::invite)         == 10);
    CHECK(uint8_t(Provision::invite_confirm) == 11);
    CHECK(uint8_t(Provision::invite_closed)  == 12);
    CHECK(uint8_t(Provision::invite_need_pubkey) == 13);
    CHECK(uint8_t(Provision::invite_wait_pubkey) == 14);
    // ★ §UI-16 N6's and K5's, appended the same way — the fifteen above keep their values, so no landed case's arm
    //   moved. ⓘ K5's is the slice's ONE new arm: its RESULT reuses `create_result` (which renders `prov_answer` and
    //   nothing else), exactly as N3's does.
    CHECK(uint8_t(Provision::invite_result)  == 15);
    CHECK(uint8_t(Provision::saved_key)      == 16);
    CHECK(provision_is_invite(Provision::saved_key) == false);   // ⛔ it is not a window arm: no snapshot survives it
    CHECK(provision_is_invite(Provision::invite)         == true);
    CHECK(provision_is_invite(Provision::invite_confirm) == true);
    CHECK(provision_is_invite(Provision::invite_need_pubkey) == true);
    CHECK(provision_is_invite(Provision::invite_wait_pubkey) == true);
    CHECK(provision_is_invite(Provision::invite_closed)  == false);
    CHECK(provision_is_invite(Provision::menu)           == false);
    CHECK(provision_is_invite(Provision::nearby)         == false);
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
    // ★ §UI-16 N2's child joined the walk here — the case is EXTENDED rather than re-scoped: what it measures is
    //   that `short` visits every row IN LIST ORDER and then WRAPS, and the list is now three children long.
    CHECK(prov_row_under_cursor(f.m, s, r)); CHECK(r == ProvRow::join_team);
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
    // ★ ALL TEN ARMS, not the ones a gesture happens to reach: the arms slices 5/6 add are exactly the ones a push
    //   or a timeout may leave standing when the screen moves, which is the situation this guard exists for.
    // ⛔ EXTENDED IN PLACE 2026-08-23 (§UI-16 N3), AND THE STALENESS IS REPORTED RATHER THAN TIDIED: this array read
    //   *"ALL EIGHT ARMS"* and listed eight while the enum already had NINE — §UI-16 N2's `nearby` was never added,
    //   so a completeness claim was covering an arm it did not drive. Both §UI-16 arms are here now, and the count
    //   assertion below is what makes the claim checkable instead of trusted.
    // ★ EXTENDED AGAIN 2026-08-23 (§UI-16 N4): its three arms join the sweep, and the count moves with them —
    //   `invite_confirm` is exactly the *"unfinished confirmation"* OQ-3 rules must not survive, so an arm this
    //   guard did not drive would be the one that survived an alarm.
    const Provision arms[] = { Provision::closed,       Provision::menu,        Provision::create_confirm,
                               Provision::create_result, Provision::join_select, Provision::join_confirm,
                               Provision::join_waiting,  Provision::join_result,
                               Provision::nearby,        Provision::nearby_confirm,
                               Provision::invite,        Provision::invite_confirm, Provision::invite_closed,
                               Provision::invite_need_pubkey, Provision::invite_wait_pubkey,
                               // ★ EXTENDED AGAIN 2026-08-25 (§UI-16 K5): `saved_key` is exactly the *"unfinished
                               //   confirmation"* OQ-3 says must not survive — and it is one `double` from
                               //   installing a stored SECRET, so an arm this guard did not drive would be the one
                               //   that survived an alarm. ⛔ AND `invite_result` IS ADDED WITH IT RATHER THAN LEFT
                               //   OUT: §UI-16 N6 appended that arm without extending this sweep, so the block's
                               //   own completeness claim was covering an arm it did not drive — the same staleness
                               //   this array reported (and fixed in place) once already for `nearby`.
                               Provision::invite_result, Provision::saved_key };
    CHECK(sizeof(arms) / sizeof(arms[0]) == 17u);
    for (Provision arm : arms) {
        for (ProvConfirm c : { ProvConfirm::back, ProvConfirm::confirm }) {
            Provision a = arm; ProvConfirm cur = c;
            mrui::InviteWindow w{};
            const bool changed = provision_reset_on_leave(a, cur, w);
            CHECK(a == Provision::closed);
            // ⛔ AND THE CONFIRM CURSOR RE-ANCHORS TOO: a stale CONFIRM would re-open the next confirmation with the
            //    destructive choice already selected, one `double` from replacing a membership.
            CHECK(cur == ProvConfirm::back);
            // ⓘ "changed" is what the caller repaints on — true for every arm but the one already at rest.
            CHECK(changed == (arm != Provision::closed || c != ProvConfirm::back));
            // ★★ §UI-16 N4 — THE THIRD FACT IS RETIRED TOO, from EVERY arm: a window's two authorities, its
            //    VOLATILE handled set and its frozen selection may not survive a leave (F-13, P-14). Driven with
            //    a window that really holds something, so "it was cleared" is a measurement and not a default.
            Provision a2 = arm; ProvConfirm cur2 = c;
            mrui::InviteWindow live{};
            live.taken = true; live.n = 1; live.hash[0] = 0x11223344u;
            live.handled_n = 1; live.handled[0] = 0x11223344u;
            live.sel_hash = 0x11223344u; live.sel_id = 200;
            live.id_bits[25] = 0x01;
            const bool changed2 = provision_reset_on_leave(a2, cur2, live);
            CHECK(live.taken == false);
            CHECK(live.n == 0);
            CHECK(live.handled_n == 0);
            CHECK(live.sel_hash == 0u);
            CHECK(live.sel_id == 0u);
            CHECK(live.id_bits[25] == 0u);
            CHECK(changed2 == true);          // ⛔ a live window is a CHANGE even from the closed arm at rest
        }
    }
    // ...and it is IDEMPOTENT: a second call on a state already at rest reports no change, so a screen sitting off
    // SETTINGS does not mark the panel dirty on every tick.
    Provision a = Provision::join_waiting; ProvConfirm cur = ProvConfirm::confirm;
    mrui::InviteWindow w2{};
    CHECK(provision_reset_on_leave(a, cur, w2) == true);
    CHECK(provision_reset_on_leave(a, cur, w2) == false);
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
    const ProvRowList both = provision_rows(true, true, /*join_team=*/false, /*invite=*/false, /*saved_keys=*/false);
    CHECK(both.n == 3);
    CHECK(both.at(0, r)); CHECK(r == ProvRow::create_team);
    CHECK(both.at(1, r)); CHECK(r == ProvRow::join_static);
    CHECK(both.at(2, r)); CHECK(r == ProvRow::back);
    CHECK(both.at(3, r) == false);                            // ⛔ fails closed, like every other row list here
    CHECK(both.at(255, r) == false);
    // ★★★ PLAN §6's CORRECTION, AND THIS IS THE CASE THAT CARRIES IT: with the TEAM plane absent, CREATE is hidden
    //     and ⛔ STATIC JOIN IS NOT — it has nothing to do with the team plane. A model that governed both children
    //     by one flag passes every other case in this block and fails here.
    const ProvRowList join_only = provision_rows(false, true, /*join_team=*/false, /*invite=*/false, /*saved_keys=*/false);
    CHECK(join_only.n == 2);
    CHECK(join_only.at(0, r)); CHECK(r == ProvRow::join_static);
    CHECK(join_only.at(1, r)); CHECK(r == ProvRow::back);
    const ProvRowList create_only = provision_rows(true, false, /*join_team=*/false, /*invite=*/false, /*saved_keys=*/false);
    CHECK(create_only.n == 2);
    CHECK(create_only.at(0, r)); CHECK(r == ProvRow::create_team);
    CHECK(create_only.at(1, r)); CHECK(r == ProvRow::back);
    // ⓘ NEITHER child (the `gateway_heltec` shape: OLED=1, MR_N_LAYERS=2) — the menu still has BACK, because leaving
    //   must never depend on a build flag.
    const ProvRowList none = provision_rows(false, false, /*join_team=*/false, /*invite=*/false, /*saved_keys=*/false);
    CHECK(none.n == 1);
    CHECK(none.at(0, r)); CHECK(r == ProvRow::back);
    // ...and every row's label fits the rail's 19-column body with its `>` marker
    for (uint8_t i = 0; i < kMaxProvRows; ++i) CHECK(1u + strlen(provision_row_label(ProvRow(i))) <= 19u);
    CHECK(strcmp(provision_row_label(ProvRow::create_team), "CREATE TEAM") == 0);
    CHECK(strcmp(provision_row_label(ProvRow::join_static), "JOIN NETWORK") == 0);
    CHECK(strcmp(provision_row_label(ProvRow::join_team), "JOIN TEAM") == 0);   // §UI-16 S-1
    CHECK(strcmp(provision_row_label(ProvRow::invite), "INVITE MEMBER") == 0);  // §UI-16 S-12
    CHECK(strcmp(provision_row_label(ProvRow::back), "BACK") == 0);
}

TEST_CASE("ui15-hide: a HIDDEN child has NO refusing stub — it cannot be reached, walked to or activated") {
    // The team plane is absent; static join is not. [[B209]]: hide it, ⛔ never render a row that refuses.
    CfgFix f; const auto s = prov_snap(/*create_team=*/false, /*join_static=*/true, /*join_team=*/false);
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
    CHECK(provision_has_child(true,  true,  false, false, false) == true);
    CHECK(provision_has_child(true,  false, false, false, false) == true);
    CHECK(provision_has_child(false, true,  false, false, false) == true);  // ⛔ static join alone STILL earns the parent
    CHECK(provision_has_child(false, false, false, false, false) == false);
    // ★ §UI-16 N2 — AND THE NEARBY CHILD ALONE EARNS IT TOO, which is the whole point of deriving the predicate
    //   from the child list: this line needed no change to `provision_has_child` beyond the parameter it forwards.
    CHECK(provision_has_child(false, false, true, false, false)  == true);
    // ★ §UI-16 N4 — AND THE INVITE CHILD ALONE EARNS IT, for the third time and still with no change to this
    //   predicate beyond the parameter it forwards. ⓘ It is the one child that can be the ONLY one on a real
    //   node: a leaf that is in a team but whose two join paths a future profile compiled out.
    CHECK(provision_has_child(false, false, false, true, false) == true);
    // ⓘ ...and the child list itself is unchanged: BACK is still unconditional, so the sub-view could still be left
    //   if anything ever opened it. The ruling removes the DOOR, not the exit.
    CHECK(provision_rows(false, false, false, false, false).n == 1);

    // The `gateway_heltec` shape (OLED=1, MR_N_LAYERS=2): the row is on no list, so no press can select or activate it.
    CfgFix f; const auto s = prov_snap(/*create_team=*/false, /*join_static=*/false, /*join_team=*/false);
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
    const auto s2 = prov_snap(/*create_team=*/false, /*join_static=*/true, /*join_team=*/false);
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
    // ★★ §UI-16 N3: the NEARBY-JOIN half's script and its ONE instrument. `team_answer` is what the `join_team` op
    //    returns — a THIRD script, so a case cannot accidentally measure another act — and `last_team_id` is the
    //    32-bit id the model handed over, which is what makes *"the act carries the FULL id of the ROW"* assertable
    //    rather than argued. ⓘ 0 is "no join_team intent was ever performed", which no real pick can produce.
    UiProvAnswer      team_answer{};
    uint32_t          last_team_id = 0;
    // ★★ §UI-16 K5: the SAVED-KEY half's script and its ONE instrument — a FOURTH script for the same reason the
    //    third exists (a case must not be able to measure another act by accident), and `last_saved_key_id` is the
    //    32-bit id the model handed over, which is what makes *"the act is keyed on the id the TRANSACTION joined"*
    //    assertable rather than argued. ⓘ 0 = no `use_saved_key` intent was ever performed.
    UiProvAnswer      saved_answer{};
    uint32_t          last_saved_key_id = 0;
    // ★★ §UI-16 K6: the RETENTION half — a FIFTH script for the reason the third and fourth exist (a case must not
    //    be able to measure another act by accident), plus the LIST's own answer and its call counter. ⓘ
    //    `last_forget_id` 0 = no `forget_key` intent was ever performed, which no real selection can produce (the
    //    model refuses a 0 out loud); `keys_calls` is what makes *"the keyring is read ONCE, on the transition"* and
    //    *"the result's acknowledgement REFRESHES it"* measurements rather than readings of the source.
    UiProvAnswer        forget_answer{};
    uint32_t            last_forget_id = 0;
    mrfw::SavedKeyList  keys{};
    int                 keys_calls = 0;
    UiProvAnswer perform(const UiProvIntent& in) override {
        ++calls;
        last_op = in.op;
        if (in.op == UiProvOp::join_static) last_join = in.join;
        if (in.op == UiProvOp::join_team)   last_team_id = in.team_id;
        if (in.op == UiProvOp::use_saved_key) last_saved_key_id = in.team_id;
        if (in.op == UiProvOp::forget_key)  last_forget_id = in.team_id;
        if (m) {
            arm_at_call  = m->state().provisioning;
            head_at_call = prov_result_head(m->state().prov_answer);
        }
        if (in.op == UiProvOp::join_static) return join_answer;
        if (in.op == UiProvOp::join_team)   return team_answer;
        if (in.op == UiProvOp::use_saved_key) return saved_answer;
        if (in.op == UiProvOp::forget_key)  return forget_answer;
        return answer;
    }
    UiJoinList profiles() override { ++list_calls; return list; }
    mrfw::SavedKeyList saved_keys() override { ++keys_calls; return keys; }
};
struct UiFakeInvite : IUiInviteDevice {
    UiModel* model = nullptr;
    int announcement_requests = 0;
    bool snapshot_taken_at_announcement = false;
    uint8_t snapshot_n_at_announcement = 0;
    Provision provisioning_at_announcement = Provision::closed;
    bool present = true;
    MESHROUTE_NS::Node::PeerKeyConf conf = MESHROUTE_NS::Node::PeerKeyConf::authoritative;
    mutable int reads = 0;
    mutable uint32_t read_hash = 0;
    mutable MESHROUTE_NS::Node::PeerKeyConf read_floor = MESHROUTE_NS::Node::PeerKeyConf::overheard;
    int commands = 0;
    MESHROUTE_NS::Command last{};
    // The executor's answer, scripted. Default = the ordinary acceptance; a refusal is what the panel may not
    // report as `WAITING FOR PUBKEY` (QG blocker, 2026-08-24).
    mrui::UiInviteIssue answer{ true, MESHROUTE_NS::CmdCode::queued, true };
    bool peer_key_at_least(uint32_t hash, MESHROUTE_NS::Node::PeerKeyConf floor) const override {
        ++reads; read_hash = hash; read_floor = floor;
        return present && static_cast<uint8_t>(conf) >= static_cast<uint8_t>(floor);
    }
    mrui::UiInviteIssue issue(const MESHROUTE_NS::Command& command) override {
        ++commands; last = command; return answer;
    }
    void request_team_announcement() override {
        ++announcement_requests;
        if (!model) return;
        snapshot_taken_at_announcement = model->state().invite.taken;
        snapshot_n_at_announcement = model->state().invite.n;
        provisioning_at_announcement = model->state().provisioning;
    }
    // ---- §UI-16 N6: the GRANT seam, scripted. ★ The default is the ordinary ADMISSION with a handle, so a case
    //      that does not care about the outcome still exercises the promotable state; the eight arms and the words
    //      themselves belong to `test_firmware_ui_invite.cpp` (they are the PURE unit's), and what is driven HERE
    //      is which gesture may reach the seam at all, what it is handed, and what a push does to the screen.
    MESHROUTE_NS::Node::TeamKeyGrantTx tx = MESHROUTE_NS::Node::TeamKeyGrantTx::queued;
    uint16_t tx_ctr = 4242;
    uint8_t  tx_dst = 0;      // §UI-16 N6b: what the core RESOLVED at send time — ⛔ never the row's frozen id
    int      grants = 0;
    uint32_t grant_hash = 0;
    MESHROUTE_NS::Plane grant_plane = MESHROUTE_NS::Plane::AUTO;
    MESHROUTE_NS::Node::TeamKeyGrantTx grant(uint32_t key_hash32, MESHROUTE_NS::Plane plane,
                                             uint16_t* out_ctr, uint8_t* out_dst) override {
        ++grants; grant_hash = key_hash32; grant_plane = plane;
        if (out_ctr) *out_ctr = tx_ctr;
        if (out_dst) *out_dst = tx_dst;
        return tx;
    }
};
struct CreateFix : CfgFix {
    UiFakeProvision prov;
    UiFakeInvite invite_dev;
    CreateFix() {
        prov.m = &m;
        invite_dev.model = &m;
        m.attach_provision(prov);
        m.attach_invite(invite_dev);
    }
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
        const auto s2 = prov_snap(/*create_team=*/true, /*join_static=*/true, /*join_team=*/true, s.now_ms + kCancelledMs + 1);
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
    // ⛔ NO ARM REACHABLE FROM THE STATIC-JOIN FLOW SAYS `JOINED` — walked over its whole vocabulary, so the property
    //    is about the WORDS and not about the one state this case happens to be in.
    // ⛔⛔ AMENDED IN PLACE 2026-08-23 (§UI-16 N3), AND THE WITHDRAWN SCOPE IS KEPT VISIBLE: this array was written as
    //     *"every outcome"* and the claim as *"NO ARM OF EITHER STRING FUNCTION"*. §UI-16 N3 adds `team_joined`, whose
    //     headline IS `TEAM JOINED` (spec §8 S-10, ruled by F-4) — so the old sweep would now fail, and widening the
    //     word ban to it would forbid the one string the owner ruled. ★ THE TWO ARE NOT IN CONFLICT AND THE
    //     DISTINCTION IS THE POINT: plan §2.3 rule 1 bans a claim of ADOPTION the static join has not got (its
    //     `joining` verdict means one write and a DAD that has only STARTED, and the real answer arrives later as a
    //     correlated push), while `team_joined` is the TEAM transaction's `applied` — a membership that IS
    //     established, durably, before the screen moves. ⇒ the ban is scoped to the flow it was written for, and
    //     `team_joined` is asserted to be OUTSIDE it rather than quietly dropped from a list.
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
    // ★ AND THE ONE EXEMPTION IS NAMED AND DRIVEN, so the scope above is a decision rather than an omission: the
    //   NEARBY join's applied verdict — and ⛔ nothing the STATIC flow can reach — carries the word.
    {
        UiProvAnswer tj{}; tj.outcome = UiProvOutcome::team_joined;
        CHECK(strcmp(prov_result_head(tj), "TEAM JOINED") == 0);
        CHECK(strcmp(prov_result_detail(tj), "") == 0);
        for (UiProvOutcome o : all) {
            UiProvAnswer a{}; a.outcome = o;
            CHECK(strcmp(prov_result_head(a), prov_result_head(tj)) != 0);   // ⛔ it is nobody else's word either
        }
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
    auto s1 = prov_snap(true, true, true, s.now_ms + kJoinStillMs - 1);
    f.m.on_tick(s1);
    CHECK(f.m.state().join_still == false);
    CHECK(strcmp(join_wait_head(f.m.state().join_still), "JOINING") == 0);
    // ...and at the deadline the WORD changes and ⛔ NOTHING ELSE DOES.
    auto s2 = prov_snap(true, true, true, s.now_ms + kJoinStillMs);
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
    f.m.on_tick(prov_snap(true, true, true, s.now_ms + kJoinStillMs + 5000));
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
        const auto s2 = prov_snap(true, true, true, s.now_ms + kCancelledMs + 1);
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

    // ---- ★★★★ §UI-10/11 P3 — THE COMPOSE ROWS, AT THE **WIDEST PHRASE THE CATALOG CAN HOLD**. ⛔ WITHDRAWN AND
    //      KEPT VISIBLE: this used to walk `kDmTexts` / `kChannelTexts` and check `1 + strlen(text) <= kCols`, i.e.
    //      the FIVE compiled strings. Those tables are retired, and — the point of OQ-A — a configured phrase can be
    //      LONGER than any of them. ⇒ the budget is now proved against the BOUND (`mrnv::kUiPresetTextMax`, 17), so
    //      it holds for every phrase the wearer can ever store rather than for the five that shipped.
    // ★★ OQ-A's ARITHMETIC, ASSERTED RATHER THAN RESTATED: selection marker 1 + location marker 1 + text 17 = 19.
    CHECK(1u + 1u + size_t(mrnv::kUiPresetTextMax) == size_t(kCols));
    {
        // A catalog whose two DM and two channel slots all carry a MAXIMUM-LENGTH phrase, in both location states.
        mrnv::UiPresetBlob wide{};
        mrfw::preset_defaults(wide);
        const char* w17 = "ABCDEFGHIJKLMNOPQ";                 // exactly 17
        CHECK(strlen(w17) == size_t(mrnv::kUiPresetTextMax));
        for (uint8_t i : { uint8_t(mrfw::kPresetDmFirst), uint8_t(mrfw::kPresetDmFirst + 1),
                           uint8_t(mrfw::kPresetChannelFirst), uint8_t(mrfw::kPresetChannelFirst + 1) })
            mrfw::preset_slot_put(wide.slot[i], true, (i % 2) == 0, w17, strlen(w17));
        ComposeList dm{}, ch{};
        compose_project(wide, mrfw::PresetKind::dm,      dm);
        compose_project(wide, mrfw::PresetKind::channel, ch);
        char row[48];
        for (const ComposeList* l : { &dm, &ch })
            for (uint8_t i = 0; i < compose_row_count(*l, /*grant=*/true); ++i)
                for (bool sel : { false, true }) {
                    compose_row_line(row, sizeof row, i, *l, /*grant=*/true, sel);
                    CHECK(strlen(row) <= size_t(kCols));
                }
        // ...and the DERIVED rows — `GRANT KEY` and `back, don't send` — with their one marker column.
        CHECK(1u + strlen(kInviteGrantKey)  <= size_t(kCols));
        CHECK(1u + strlen(kComposeBackText) <= size_t(kCols));
        CHECK(strlen(kNoPresetsText)        <= size_t(kCols));
        CHECK(strlen(kPresetChangedText)    <= size_t(kCols));
    }
    // ★★ §7.1 RULE 6 — *"two selectable preset strings must not become visually identical after clamping"*. ⛔ THE
    //    CHECK IS WITHDRAWN AND KEPT VISIBLE, and the reason is a MEASUREMENT rather than a preference: it compared
    //    the five COMPILED strings, and the catalog is now the WEARER's. Nothing in this tree can stop him
    //    configuring `dm1` and `dm2` to the same words — and refusing a duplicate would be a new owner ruling
    //    (§3.2.3's `set` validation has no such clause). ⇒ what survives is the half that is still ours to keep:
    //    a phrase can never be CLAMPED at all, because `validate_preset_text` refuses 18+ bytes outright, so two
    //    distinct phrases can never become identical *through truncation*. That is asserted above.
    // ⓘ The emergency body is EXEMPT and stays 21 columns at x = 0 (§5.3) — the emergency phrase is not a body row
    //   at all (it is the wire text of the alarm), and the `Font::large` headlines have their own 12-column budget,
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

// =====================================================================================================================
// §UI-16 N2 — THE `JOIN TEAM` CHILD AND THE READ-ONLY NEARBY LIST (spec §4-N2)
// =====================================================================================================================
// ★★★ WHAT THIS BLOCK MEASURES AND WHAT IT DOES NOT. The PURE decisions — the own-team filter, the first-observed
//     order, the three tokens and every lexeme — belong to `test/test_firmware_ui_nearby.cpp` and to the `uinearby` /
//     `uinearbyrow` batteries. What lives HERE is the MODEL's half: the third child, the DIRECT landing (OQ-1), the
//     ONE-SHOT capture that makes the list frozen per entry (R-10), BACK's containment, and the fact that a screen
//     which *can only look* performs NOTHING — asserted on the store's write count and the seam's call count, ⛔ never
//     on a state field alone.
// ⛔ THE RENDERER IS MEASURED IN NEITHER: `src/firmware_ui.cpp` is compiled by no automated gate (§B115), and its
//    cover is `tools/probe_firmware_ui`'s NEARBY phase.
namespace {
// A snapshot carrying N observed teams, newest LAST — i.e. in the ring's own first-observed order.
// ⚠ THE SIGNALS DECREASE DOWN THE LIST while the ages INCREASE, so a sort by either key would be visible.
UiSnapshot nearby_snap(uint8_t n, uint32_t own_team_id = 0, uint32_t now_ms = 1000) {
    UiSnapshot s = prov_snap(true, true, true, now_ms);
    s.team_id  = own_team_id;
    s.nearby_n = n;
    for (uint8_t i = 0; i < n && i < mrui::kMaxNearbyRows; ++i) {
        s.nearby[i].team_id   = 0xBEEF0001u + i;
        s.nearby[i].snr_q4    = int16_t(64 - 16 * i);
        s.nearby[i].age_ms    = 1000u * (i + 1);
        s.nearby[i].age_valid = true;
    }
    return s;
}
// Open PROVISION and land on the NEARBY list. The caller ASSERTS the landing.
bool open_nearby(CfgFix& f, const UiSnapshot& s) {
    if (!open_provision(f.m, s)) return false;
    if (!prov_cursor_to(f.m, s, ProvRow::join_team)) return false;
    f.m.on_gesture(Gesture::double_press, s);
    return f.m.state().provisioning == Provision::nearby;
}
}  // namespace

TEST_CASE("ui16-menu: JOIN TEAM is the THIRD child, and a double opens the LIST DIRECTLY — ⛔ no submenu") {
    // ✅ OQ-1 (owner, 2026-08-22): a submenu arrives only when a SECOND join method exists.
    CfgFix f; const auto s = nearby_snap(2);
    CHECK(open_provision(f.m, s));
    const ProvRowList l = f.m.provision_row_list(s);
    ProvRow r{};
    CHECK(l.n == 4);
    CHECK(l.at(0, r)); CHECK(r == ProvRow::create_team);
    CHECK(l.at(1, r)); CHECK(r == ProvRow::join_static);
    CHECK(l.at(2, r)); CHECK(r == ProvRow::join_team);
    CHECK(l.at(3, r)); CHECK(r == ProvRow::back);
    // ⚠ THE WALK IS DONE FROM HERE, ⛔ not through `open_nearby`: the sub-view already OWNS THE PRESS, so a helper
    //   that re-entered SETTINGS from inside it would be driving the wrong list.
    CHECK(prov_cursor_to(f.m, s, ProvRow::join_team));
    f.m.on_gesture(Gesture::double_press, s);
    // ★ THE LANDING IS THE LIST ITSELF — ⛔ never another menu — and it opens on its FIRST row.
    CHECK(f.m.state().provisioning == Provision::nearby);
    CHECK(f.m.state().cursor == 0);
    CHECK(f.m.state().nearby.n == 2);
    // ⛔ AND LOOKING COSTS NOTHING DURABLE: opening the scan spends no write and applies nothing live.
    CHECK(f.store.writes == 0);
    CHECK(f.live.applies == 0);
}

TEST_CASE("ui16-hide: the NEARBY child follows its OWN predicate, and it alone earns the parent row") {
    {   // the child is hidden: no row, and no press can reach the arm
        CfgFix f; auto s = nearby_snap(2);
        s.prov_join_team = false;
        CHECK(open_provision(f.m, s));
        CHECK(f.m.provision_row_list(s).n == 3);
        for (int i = 0; i < 6; ++i) {
            ProvRow r{};
            CHECK(prov_row_under_cursor(f.m, s, r));
            CHECK(r != ProvRow::join_team);
            f.m.on_gesture(Gesture::double_press, s);
            CHECK(f.m.state().provisioning != Provision::nearby);
            if (f.m.state().settings != Settings::provisioning) CHECK(open_provision(f.m, s));
            else if (f.m.state().provisioning != Provision::menu) f.m.on_gesture(Gesture::double_press, s);
            f.m.on_gesture(Gesture::short_press, s);
        }
    }
    {   // ★ ...and on a build where it is the ONLY child, the PARENT row is offered because of it
        CfgFix f; auto s = nearby_snap(1);
        s.prov_create_team = false; s.prov_join_static = false;
        CHECK(provision_has_child(s.prov_create_team, s.prov_join_static, s.prov_join_team,
                                  s.prov_invite, s.prov_saved_keys) == true);
        CHECK(open_provision(f.m, s));
        const ProvRowList l = f.m.provision_row_list(s);
        ProvRow r{};
        CHECK(l.n == 2);
        CHECK(l.at(0, r)); CHECK(r == ProvRow::join_team);
        CHECK(l.at(1, r)); CHECK(r == ProvRow::back);
        CHECK(prov_cursor_to(f.m, s, ProvRow::join_team));
        f.m.on_gesture(Gesture::double_press, s);
        CHECK(f.m.state().provisioning == Provision::nearby);
    }
}

TEST_CASE("ui16-freeze: the cache is read ONCE, ON THE TRANSITION — ⛔ the list does not refresh under the cursor") {
    // ★★★★ OWNER RULING R-10: *"NEARBY teams = a frozen snapshot per entry, manual refresh only (leave and
    //      re-enter)"*. ⇒ a team that walks into range while the operator is walking the list may ⛔ NOT appear, and
    //      one that walks out may ⛔ not vanish — either would move the row under the cursor mid-press.
    CfgFix f; const auto s = nearby_snap(2);
    CHECK(open_nearby(f, s));
    CHECK(f.m.state().nearby.n == 2);
    // The world changes: a third team is now audible and the first has been re-heard with a different signal.
    auto s2 = nearby_snap(3, /*own_team_id=*/0, s.now_ms + 5000);
    s2.nearby[0].snr_q4 = -300;
    f.m.on_tick(s2);
    f.m.on_gesture(Gesture::short_press, s2);
    // ★ THE FROZEN LIST IS UNMOVED — count, identity AND the signal the row was drawn with.
    CHECK(f.m.state().nearby.n == 2);
    CHECK(f.m.state().nearby.row[0].team_id == 0xBEEF0001u);
    CHECK(f.m.state().nearby.row[0].snr_q4 == 64);
    // ...and LEAVING AND RE-ENTERING is the manual refresh — which is the ONLY way to see the third team.
    CHECK(f.m.state().cursor == 1);                // the `short` above moved onto the second team
    f.m.on_gesture(Gesture::short_press, s2);      // ...and this one onto BACK (2 teams + BACK = 3 rows)
    CHECK(f.m.state().provisioning == Provision::nearby);   // ⛔ `short` never walks out of a sub-view
    f.m.on_gesture(Gesture::double_press, s2);     // BACK -> the PROVISION menu
    CHECK(f.m.state().provisioning == Provision::menu);
    CHECK(prov_cursor_to(f.m, s2, ProvRow::join_team));
    f.m.on_gesture(Gesture::double_press, s2);
    CHECK(f.m.state().nearby.n == 3);
    CHECK(f.m.state().nearby.row[0].snr_q4 == -300);
}

TEST_CASE("ui16-filter: OUR OWN team is not offered, and a scan of only our own team is EMPTY") {
    CfgFix f; const auto s = nearby_snap(3, /*own_team_id=*/0xBEEF0002u);
    CHECK(open_nearby(f, s));
    CHECK(f.m.state().nearby.n == 2);
    CHECK(f.m.state().nearby.row[0].team_id == 0xBEEF0001u);
    CHECK(f.m.state().nearby.row[1].team_id == 0xBEEF0003u);   // ★ the survivors keep their relative order
    {   // ⓘ ...and an EMPTY list still OPENS and can still be left (pin 4) — a scan is not a refusal.
        CfgFix g; const auto s1 = nearby_snap(1, /*own_team_id=*/0xBEEF0001u);
        CHECK(open_nearby(g, s1));
        CHECK(g.m.state().nearby.n == 0);
        CHECK(strcmp(mrui::nearby_note(g.m.state().nearby), mrui::kNearbyEmpty) == 0);
        g.m.on_gesture(Gesture::double_press, s1);              // the ONLY row is BACK, and it still leaves
        CHECK(g.m.state().provisioning == Provision::menu);
    }
}

TEST_CASE("ui16-walk: `short` CYCLES the teams then BACK and WRAPS; BACK returns to the PROVISION MENU") {
    // ★ THE CONTAINMENT IS §UI-17's landed contract: a list's BACK returns to ITS OWN PARENT — here the PROVISION
    //   menu (`close_provisioning`'s idiom) — ⛔ never off the screen and ⛔ never to another screen.
    CfgFix f; const auto s = nearby_snap(2);
    CHECK(open_nearby(f, s));
    const mrui::NearbySelList l = mrui::nearby_sel_rows(f.m.state().nearby);
    CHECK(l.n == 3);                                            // two teams + the unconditional BACK
    for (uint8_t i = 0; i < l.n; ++i) {
        CHECK(f.m.state().cursor == i);
        f.m.on_gesture(Gesture::short_press, s);
    }
    CHECK(f.m.state().cursor == 0);                             // ★ WRAPS — a sub-view is never walked out of
    CHECK(f.m.state().provisioning == Provision::nearby);
    CHECK(f.m.state().screen == Screen::settings);
    // ...and a `double` on the LAST row leaves the list for the menu it was opened from.
    for (uint8_t i = 0; i + 1 < l.n; ++i) f.m.on_gesture(Gesture::short_press, s);
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().provisioning == Provision::menu);
    CHECK(f.m.state().settings == Settings::provisioning);      // ⛔ NOT out of the sub-view, NOT out of the screen
    CHECK(f.store.writes == 0);
}

TEST_CASE("ui16-look: a double on a TEAM row OPENS THE CONFIRMATION and still performs NOTHING (§3 P-3/P-4)") {
    // ⛔⛔ AMENDED IN PLACE 2026-08-23 (§UI-16 N3), AND THE WITHDRAWN CASE IS KEPT VISIBLE: it was titled *"a double
    //     on a TEAM row performs NOTHING — N2 can only look"* and asserted `provisioning == Provision::nearby` after
    //     each press, because [[B222]] kept the act one slice out. ★ THE ACT HAS LANDED, so the LANDING moves — and
    //     ⛔ THE MEASUREMENT DOES NOT: opening a confirmation is not acting on it, so the seam, the store and the
    //     live sink must still be untouched. N2's zero-TX rule extends verbatim (spec §4-N3: only a confirmed
    //     `double` reaches the transaction).
    // ★ AND THE AUTHORITY IS THE COUNTERS, ⛔ not the screen: a model that quietly performed a join would look
    //   identical if it also failed to navigate.
    CreateFix f; const auto s = nearby_snap(3);
    CHECK(open_nearby(f, s));
    const int calls = f.prov.calls, writes = f.store.writes, applies = f.live.applies;
    for (uint8_t i = 0; i < 2; ++i) {
        f.m.on_gesture(Gesture::double_press, s);                       // a `double` on a TEAM row (⛔ not on BACK)
        CHECK(f.m.state().provisioning == Provision::nearby_confirm);   // ★ the confirmation, ⛔ not the act
        CHECK(f.m.state().prov_confirm == ProvConfirm::back);           // ...opened on the SAFE arm (P-13)
        f.m.on_gesture(Gesture::double_press, s);                       // BACK -> the NEARBY list (⛔ not the menu)
        CHECK(f.m.state().provisioning == Provision::nearby);
        f.m.on_gesture(Gesture::short_press, s);
    }
    CHECK(f.prov.calls   == calls);                             // ⛔ the seam was never entered
    CHECK(f.store.writes == writes);                            // ⛔ nothing durable was spent
    CHECK(f.live.applies == applies);                           // ⛔ nothing was applied live
    CHECK(f.m.state().prov_answer.outcome == UiProvOutcome::none);
}

TEST_CASE("ui16-blank: the NEARBY list SURVIVES blank/wake, and the wake press is consumed") {
    // §UI-17's landed contract, inherited rather than re-litigated: interaction state survives the blank and the
    // waking press does not act.
    CfgFix f; const auto s = nearby_snap(3);
    CHECK(open_nearby(f, s));
    f.m.on_gesture(Gesture::short_press, s);                    // ...on the SECOND row when the panel goes dark
    CHECK(f.m.state().cursor == 1);
    f.m.on_tick(nearby_snap(3, 0, s.now_ms + kBlankMs + 1));
    CHECK(f.m.state().blanked == true);
    CHECK(f.m.state().provisioning == Provision::nearby);       // ⛔ the blank closes nothing
    const auto sw = nearby_snap(3, 0, s.now_ms + kBlankMs + 50);
    f.m.on_gesture(Gesture::short_press, sw);
    CHECK(f.m.state().blanked == false);
    CHECK(f.m.state().cursor == 1);                             // ★ the wake press was CONSUMED — the cursor stood still
    CHECK(f.m.state().nearby.n == 3);                           // ...and the frozen list is intact
    CHECK(f.m.state().nearby.row[0].team_id == 0xBEEF0001u);
}

TEST_CASE("ui16-alarm: `long_arm` PRE-EMPTS the scan, and re-entering finds the same teams") {
    // ★★ §3.6.5 rule 1 as the tree already implements it: the alarm closes the provisioning sub-view (P-14,
    //    `provision_reset_on_leave`). ⛔ WHAT IT MAY NOT DO is disturb the OBSERVATIONS — they are core RAM, nothing
    //    on this path writes them, and the operator who cancels comes back to the same nearby teams.
    CfgFix f; const auto s = nearby_snap(3);
    CHECK(open_nearby(f, s));
    f.m.on_gesture(Gesture::long_arm, s);
    CHECK(f.m.state().provisioning == Provision::closed);
    CHECK(f.m.state().settings == Settings::browsing);
    CHECK(f.store.writes == 0);                                 // ⛔ the alarm confirmed nothing on its way past
    f.m.on_gesture(Gesture::long_cancel, s);
    const auto s2 = nearby_snap(3, 0, s.now_ms + kCancelledMs + 1);
    f.m.on_tick(s2);
    CHECK(f.m.emergency() == Emergency::idle);
    CHECK(open_nearby(f, s2));                                  // ★ re-entering RE-CAPTURES...
    CHECK(f.m.state().nearby.n == 3);                           // ...the same three teams
    CHECK(f.m.state().cursor == 0);                             // ⛔ and it opens on its FIRST row, never where it was
}

// =====================================================================================================================
// §UI-16 N3 — THE `JOIN <fingerprint>?` CONFIRMATION AND THE ACT (spec §4-N3)
// =====================================================================================================================
// ★★★ WHAT THIS BLOCK MEASURES AND WHAT IT DOES NOT. The ADAPTER — the PHY precondition, `mint = false`, the
//     `phy.present = false` request, the verdict mapping and the keyless consequence — is
//     `test/test_firmware_ui_prov.cpp`'s, driven against the REAL `ProvisioningService` where a write and a live
//     apply are counted for real. What lives HERE is the MODEL's half: WHICH gesture opens the confirmation, WHAT it
//     defaults to, WHERE BACK lands, WHAT the act is handed, and WHEN the result screen may exist.
// ⛔ THE RENDERER IS MEASURED IN NEITHER: `src/firmware_ui.cpp` is compiled by no automated gate (§B115), and its
//    cover is `tools/probe_firmware_ui`'s P22 phase.
namespace {
// Open PROVISION, land on NEARBY, walk to the row whose team id is `want` and open its confirmation.
// ⚠ THE WALK IS BY IDENTITY, ⛔ never by a hardcoded cursor: that is the property half the cases below are about, so
//   a helper that assumed a position would be assuming the answer.
bool open_nearby_confirm(CreateFix& f, const UiSnapshot& s, uint32_t want) {
    if (!open_nearby(f, s)) return false;
    for (uint8_t i = 0; i < mrui::kMaxNearbyRows + 1; ++i) {
        mrui::NearbySelRow r{};
        const mrui::NearbySelList l = mrui::nearby_sel_rows(f.m.state().nearby);
        if (l.at(f.m.state().cursor, r) && !r.back && r.team.team_id == want) break;
        f.m.on_gesture(Gesture::short_press, s);
    }
    f.m.on_gesture(Gesture::double_press, s);
    return f.m.state().provisioning == Provision::nearby_confirm;
}
UiProvAnswer joined_answer(uint32_t id) {
    UiProvAnswer a{}; a.outcome = UiProvOutcome::team_joined; a.team_id = id; return a;
}
}  // namespace

TEST_CASE("ui16-confirm: the confirmation opens on BACK, and reaching JOIN costs `short` THEN `double` (P-13)") {
    CreateFix f; const auto s = nearby_snap(3);
    f.prov.team_answer = joined_answer(0xBEEF0002u);
    CHECK(open_nearby_confirm(f, s, 0xBEEF0002u));
    // ★ §3.6.4 point 3: *"opens a confirmation with BACK selected initially"* — established by `enter_provision`, so
    //   the default cannot be forgotten by the transition that lands here.
    CHECK(f.m.state().prov_confirm == ProvConfirm::back);
    CHECK(f.m.state().settings == Settings::provisioning);
    // ⛔ NOTHING HAS HAPPENED YET: no transaction, no write, no live apply, and no result text exists.
    CHECK(f.prov.calls == 0);
    CHECK(f.store.writes == 0);
    CHECK(f.live.applies == 0);
    CHECK(f.m.state().prov_answer.outcome == UiProvOutcome::none);
    // ...and ONE `short` reaches JOIN, whose label is `join_confirm_label`'s — CALLED, never re-spelled (S-9).
    f.m.on_gesture(Gesture::short_press, s);
    CHECK(f.m.state().prov_confirm == ProvConfirm::confirm);
    CHECK(strcmp(join_confirm_label(false), "BACK") == 0);
    CHECK(strcmp(join_confirm_label(true), "JOIN") == 0);
    CHECK(f.prov.calls == 0);                                   // ⛔ the toggle is not the act
    // ...and the `double` on it is what performs — exactly once, and the screen moves only afterwards.
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.prov.calls == 1);
    CHECK(f.prov.last_op == UiProvOp::join_team);
    CHECK(f.prov.arm_at_call == Provision::nearby_confirm);     // ★ §8 pin 2: the act ran BEFORE the screen moved...
    CHECK(strcmp(f.prov.head_at_call, "") == 0);                // ...and no result text existed while it ran
    CHECK(f.m.state().provisioning == Provision::create_result);
    CHECK(f.m.state().prov_answer.outcome == UiProvOutcome::team_joined);
}

TEST_CASE("ui16-id: ★★★ the act carries the FULL 32-bit team id of the ROW — ⛔ never the index, ⛔ never the token") {
    // ★★★★ SPEC §4-N3 pin 2 / §3 P-1 / P-7, and the fixture is built so BOTH wrong answers are VISIBLE: our own team
    //      is row 0 of the published array, so the FILTERED list re-indexes everything after it — the second visible
    //      row is the FOURTH observation — and every id below shares the SAME low 24 bits, i.e. the same six-hex
    //      fingerprint the confirmation prints. ⇒ an index-derived id and a fingerprint-derived one are each
    //      structurally unable to name the right team.
    CreateFix f;
    auto s = nearby_snap(4, /*own_team_id=*/0xAA123456u);
    s.nearby[0].team_id = 0xAA123456u;                          // ours — filtered out by `nearby_capture`
    s.nearby[1].team_id = 0xBB123456u;
    s.nearby[2].team_id = 0xCC123456u;                          // ★ the target: visible row 1, published row 2
    s.nearby[3].team_id = 0xDD123456u;
    f.prov.team_answer = joined_answer(0xCC123456u);
    CHECK(open_nearby_confirm(f, s, 0xCC123456u));
    CHECK(f.m.state().nearby.n == 3);
    // ⓘ THE TARGET IS VISIBLE ROW 1 AND PUBLISHED ROW 2 — two indices, neither of them the id. (The cursor itself is
    //   back at 0: `enter_provision` re-anchors it, which is exactly why the SELECTION cannot be a cursor read.)
    CHECK(f.m.state().nearby.row[1].team_id == 0xCC123456u);
    CHECK(s.nearby[2].team_id == 0xCC123456u);
    CHECK(f.m.state().cursor == 0);
    CHECK(f.m.state().nearby_sel_id == 0xCC123456u);            // ★ the identity, frozen at the press
    f.m.on_gesture(Gesture::short_press, s);
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.prov.calls == 1);
    // ★★★ BYTE-EQUAL TO THE OBSERVED ID (P-1). ⛔ Not the cursor (1), ⛔ not the published index (2), ⛔ not the
    //     six-hex token (`123456`) — all four values are distinct here, deliberately.
    CHECK(f.prov.last_team_id == 0xCC123456u);
    CHECK(f.prov.last_team_id != 1u);
    CHECK(f.prov.last_team_id != 2u);
    CHECK(f.prov.last_team_id != 0x123456u);
    CHECK(f.prov.last_team_id != s.team_id);                    // ⛔ nor the team we are LEAVING
    // ⓘ ...and the four candidates all SHARE the token the confirmation prints, which is what makes the assertions
    //   above measurements rather than coincidences. (The token itself is `ui_fmt_team_fingerprint`'s and is pinned
    //   in `test/test_firmware_ui_nearby.cpp` — this file does not include chrome, by the same include-order fact
    //   that put the title one header downstream.)
    CHECK((s.nearby[1].team_id & 0x00FFFFFFu) == (s.nearby[2].team_id & 0x00FFFFFFu));
    CHECK((s.nearby[3].team_id & 0x00FFFFFFu) == (s.nearby[2].team_id & 0x00FFFFFFu));
}

TEST_CASE("ui16-back: BACK returns to the NEARBY LIST — ⛔ not the menu — and performs NOTHING") {
    // ★ SPEC §4-N3 pin 3, i.e. §UI-17's landed containment: a confirmation's BACK returns to the screen the operator
    //   was choosing on. ⓘ And re-entering the list does ⛔ NOT rescan: R-10's frozen copy survives the detour.
    CreateFix f; const auto s = nearby_snap(3);
    CHECK(open_nearby_confirm(f, s, 0xBEEF0001u));
    f.m.on_gesture(Gesture::double_press, s);                   // `double` on BACK
    CHECK(f.m.state().provisioning == Provision::nearby);       // ⛔ NOT `menu`, ⛔ not off the screen
    CHECK(f.m.state().settings == Settings::provisioning);
    CHECK(f.m.state().nearby.n == 3);                           // ★ the frozen list is intact...
    CHECK(f.m.state().nearby.row[0].team_id == 0xBEEF0001u);    // ...and unchanged, row for row
    CHECK(f.prov.calls == 0);                                   // ⛔ BACK performed nothing at all
    CHECK(f.store.writes == 0);
    CHECK(f.live.applies == 0);
    CHECK(f.m.state().prov_answer.outcome == UiProvOutcome::none);
    // ⛔ AND A `double` ON BACK MAY NOT FALL THROUGH INTO THE ACT even when the cursor is back on a team row: the
    //    two branches are separate, so one press can never mean the other.
    CHECK(f.m.state().cursor == 0);
    f.m.on_gesture(Gesture::double_press, s);                   // re-open the confirmation
    CHECK(f.m.state().provisioning == Provision::nearby_confirm);
    CHECK(f.prov.calls == 0);
}

TEST_CASE("ui16-result: the RESULT is terminal — either press leaves it, and it carries no BACK row") {
    // ★ UI-17 §9 R-5, inherited: a terminal result is acknowledged by EITHER press and ⛔ never grows a selectable
    //   row. ⓘ "No BACK row" is structural here: the arm has no row list at all — the model bounds no cursor on it
    //   and the renderer draws `press = back`.
    for (int press = 0; press < 2; ++press) {
        CreateFix f; const auto s = nearby_snap(2);
        f.prov.team_answer = joined_answer(0xBEEF0001u);
        CHECK(open_nearby_confirm(f, s, 0xBEEF0001u));
        f.m.on_gesture(Gesture::short_press, s);
        f.m.on_gesture(Gesture::double_press, s);
        CHECK(f.m.state().provisioning == Provision::create_result);
        CHECK(strcmp(prov_result_head(f.m.state().prov_answer), "TEAM JOINED") == 0);
        CHECK(f.m.state().prov_answer.team_id == 0xBEEF0001u);
        const int calls = f.prov.calls;
        f.m.on_gesture(press == 0 ? Gesture::short_press : Gesture::double_press, s);
        CHECK(f.m.state().provisioning == Provision::menu);     // ⛔ either press, and the SAME landing
        CHECK(f.prov.calls == calls);                           // ⛔ acknowledging re-runs nothing
        // ★ AND THE VERDICT IS RETIRED BY THE ENTRY (`enter_provision`), so no later screen can render a stale one.
        CHECK(f.m.state().prov_answer.outcome == UiProvOutcome::none);
    }
}

TEST_CASE("ui16-words: `TEAM JOINED` is the join's own word — ⛔ never `TEAM CREATED`, and no banned lexeme appears") {
    // ★★ F-4: a join by id lands the SAME `ProvVerdict::applied` a create does, so the two must differ IN WORDS or
    //    the operator cannot tell which operation answered. ⛔ And S-32/S-33/S-34 are absent by test, not by taste.
    CHECK(strcmp(prov_result_head(joined_answer(1)), "TEAM JOINED") == 0);
    CHECK(strcmp(prov_result_head(joined_answer(1)), "TEAM CREATED") != 0);
    const UiProvOutcome every[] = { UiProvOutcome::none, UiProvOutcome::created, UiProvOutcome::phy_differs,
                                    UiProvOutcome::save_failed, UiProvOutcome::refused, UiProvOutcome::joining,
                                    UiProvOutcome::adopted, UiProvOutcome::join_refused, UiProvOutcome::team_joined };
    for (UiProvOutcome x : every) {
        UiProvAnswer ax{}; ax.outcome = x; ax.reason = "no_mobile_plane";     // the widest prov_err_name
        // ⛔ THE THREE FORBIDDEN LEXEMES, over the WHOLE vocabulary rather than over the arm this slice added.
        CHECK(strstr(prov_result_head(ax), "KEYLESS") == nullptr);
        CHECK(strstr(prov_result_detail(ax), "KEYLESS") == nullptr);
        CHECK(strstr(prov_result_head(ax), "JOIN COMPLETE") == nullptr);
        CHECK(strstr(prov_result_detail(ax), "JOIN COMPLETE") == nullptr);
        CHECK(strstr(prov_result_head(ax), "WAITING FOR KEY") == nullptr);
        CHECK(strstr(prov_result_detail(ax), "WAITING FOR KEY") == nullptr);
        CHECK(strlen(prov_result_head(ax)) <= 19u);                          // ...and the rail's body still holds it
        CHECK(strlen(prov_result_detail(ax)) <= 19u);
        // ⛔ THE NINE HEADLINES ARE NINE DIFFERENT STRINGS (`none`'s empty one included): a collapse would make two
        //    outcomes indistinguishable, which is the "success that isn't" shape.
        for (UiProvOutcome y : every) {
            UiProvAnswer ay{}; ay.outcome = y;
            CHECK((strcmp(prov_result_head(ax), prov_result_head(ay)) == 0) == (x == y));
        }
    }
}

TEST_CASE("ui16-closed: an UNATTACHED seam FAILS CLOSED, and so does a pick that names no team") {
    {   // ⛔ NO SERVICE: the act refuses OUT LOUD rather than doing nothing — a `double` that changed no pixel is
        //    indistinguishable from a dead button. ⓘ `CfgFix` attaches no provisioning seam at all.
        CfgFix f; const auto s = nearby_snap(2);
        CHECK(open_nearby(f, s));
        f.m.on_gesture(Gesture::double_press, s);                    // -> the confirmation
        CHECK(f.m.state().provisioning == Provision::nearby_confirm);
        f.m.on_gesture(Gesture::short_press, s);
        f.m.on_gesture(Gesture::double_press, s);                    // -> JOIN, with nothing behind it
        CHECK(f.m.state().provisioning == Provision::create_result);
        CHECK(f.m.state().prov_answer.outcome == UiProvOutcome::join_refused);
        CHECK(strcmp(f.m.state().prov_answer.reason, "no service") == 0);
        // ⛔ IT IS `JOIN REFUSED`, ⛔ not `CREATE REFUSED`: the panel names the operation that failed.
        CHECK(strcmp(prov_result_head(f.m.state().prov_answer), "JOIN REFUSED") == 0);
        CHECK(f.store.writes == 0);
        CHECK(f.live.applies == 0);
    }
    {   // ⛔ A PICK OF 0 IS NOT A TEAM — and `TeamRequest{ mint = false, team_id = 0 }` is `team 0`, i.e. a LEAVE:
        //    the ONE operation a JOIN screen must never be able to perform by accident.
        // ⚠ IT IS DRIVEN THROUGH THE REAL PATH RATHER THAN BY POKING THE STATE, and the fixture is what makes that
        //   possible: `nearby_capture`'s filter drops a 0-id row only when WE are teamless (0 == our own id), so a
        //   node that IS in a team can walk a published 0 all the way to the confirmation. ⓘ N1's write gate
        //   (`peer_team != 0`) means the cache cannot produce one today — this is the floor for the day some other
        //   publisher can, which is what a floor is for (C2).
        CreateFix f;
        auto s = nearby_snap(2, /*own_team_id=*/0xAA000001u);
        s.nearby[1].team_id = 0;                                     // a row that names no team at all
        CHECK(open_nearby(f, s));
        CHECK(f.m.state().nearby.n == 2);
        f.m.on_gesture(Gesture::short_press, s);                     // ...onto it
        f.m.on_gesture(Gesture::double_press, s);
        CHECK(f.m.state().provisioning == Provision::nearby_confirm);
        CHECK(f.m.state().nearby_sel_id == 0u);
        f.m.on_gesture(Gesture::short_press, s);                     // -> JOIN
        f.m.on_gesture(Gesture::double_press, s);
        CHECK(f.prov.calls == 0);                                    // ⛔ the seam was never entered
        CHECK(f.m.state().prov_answer.outcome == UiProvOutcome::join_refused);
        CHECK(f.store.writes == 0);
        CHECK(f.live.applies == 0);
    }
}

TEST_CASE("ui16-preempt: the confirmation does NOT survive the alarm or leaving — an unconfirmed act never stands") {
    // ★ P-14 / §3.6.5 rule 1, and OQ-3's clarification: an UNFINISHED CONFIRMATION does not survive. It is
    //   `provision_reset_on_leave`'s rule, inherited rather than re-implemented — and what matters is that the
    //   RE-ENTRY cannot land back on a confirmation the operator never re-opened.
    CreateFix f; const auto s = nearby_snap(3);
    CHECK(open_nearby_confirm(f, s, 0xBEEF0001u));
    f.m.on_gesture(Gesture::short_press, s);                     // ...standing on JOIN when the alarm fires
    CHECK(f.m.state().prov_confirm == ProvConfirm::confirm);
    f.m.on_gesture(Gesture::long_arm, s);
    CHECK(f.m.state().provisioning == Provision::closed);
    CHECK(f.m.state().prov_confirm == ProvConfirm::back);        // ⛔ and ⛔ never re-opens on the destructive arm
    CHECK(f.prov.calls == 0);                                    // ⛔ the alarm confirmed nothing on its way past
    CHECK(f.store.writes == 0);
    f.m.on_gesture(Gesture::long_cancel, s);
    const auto s2 = nearby_snap(3, 0, s.now_ms + kCancelledMs + 1);
    f.m.on_tick(s2);
    CHECK(open_nearby(f, s2));                                   // ★ re-entering lands on the LIST, not the confirm
    CHECK(f.m.state().provisioning == Provision::nearby);
}

// =====================================================================================================================
// §UI-16 K5 — THE `SAVED KEY FOUND` OFFER: WHERE IT SITS, WHAT IT DEFAULTS TO, AND WHAT EACH CHOICE COSTS (spec §4-K5)
// =====================================================================================================================
// ★★★ WHAT THIS BLOCK MEASURES AND WHAT IT DOES NOT. The KEYRING's decisions — the presence question, the
//     handling-time MEMBERSHIP RE-CHECK, the load, the VERIFY-BY-ADOPTING, the activation order and the rule that a
//     refusal INSTALLS NOTHING and PRESERVES unrelated key state (only `binding_failed` undoes its own adoption) —
//     are
//     `test/test_firmware_team_keyring.cpp`'s, driven against counting fakes where writes are counted for real; the
//     ADAPTER's mapping onto panel words is `test/test_firmware_ui_prov.cpp`'s. What lives HERE is the MODEL's half:
//     WHICH gesture opens the offer, WHAT it opens on, WHERE `BACK` lands, WHAT the act is handed, WHEN the result
//     may exist, and what a blank does to an unfinished one.
// ⛔ THE RENDERER IS MEASURED IN NEITHER: `src/firmware_ui.cpp` is compiled by no automated gate (§B115), and its
//    cover is `tools/probe_firmware_ui`'s K5 phase.
namespace {
// A `team_joined` answer that ALSO reports a retained record — i.e. what `ui_prov_join_team` returns after asking
// the keyring. ⚠ THE FLAG IS THE ADAPTER'S REPORT, ⛔ never something the model may infer for itself.
UiProvAnswer joined_with_saved_key(uint32_t id) {
    UiProvAnswer a = joined_answer(id);
    a.saved_key = true;
    return a;
}
// Walk a nearby join of `id` all the way to its RESULT screen, with the given answer scripted.
bool join_to_result(CreateFix& f, const UiSnapshot& s, uint32_t id, const UiProvAnswer& scripted) {
    f.prov.team_answer = scripted;
    if (!open_nearby_confirm(f, s, id)) return false;
    f.m.on_gesture(Gesture::short_press, s);                     // -> JOIN
    f.m.on_gesture(Gesture::double_press, s);                    // ...performs it
    return f.m.state().provisioning == Provision::create_result;
}
}  // namespace

TEST_CASE("ui16-k5: the offer opens on the ACKNOWLEDGEMENT of a join whose team has a retained key — on BACK") {
    // ★★★★ P-2b IN THE CONTROL FLOW: the transaction has RUN, RETURNED and been REPORTED, and the operator has
    //      PRESSED PAST the verdict, before the key question is asked at all. ⇒ nothing about the key can be read
    //      as part of joining, which is why the ruling asks for a screen instead of a rule.
    for (int press = 0; press < 2; ++press) {
        CreateFix f; const auto s = nearby_snap(3);
        CHECK(join_to_result(f, s, 0xBEEF0001u, joined_with_saved_key(0xBEEF0001u)));
        // ★ THE JOIN'S OWN VERDICT IS SHOWN FIRST AND IN FULL — ⛔ the offer does not replace it (spec §4-N3 pin 5).
        CHECK(strcmp(prov_result_head(f.m.state().prov_answer), "TEAM JOINED") == 0);
        CHECK(f.m.state().prov_answer.team_id == 0xBEEF0001u);
        const int calls = f.prov.calls;
        f.m.on_gesture(press == 0 ? Gesture::short_press : Gesture::double_press, s);
        // ★★★ EITHER PRESS ACKNOWLEDGES, and the acknowledgement lands on the OFFER.
        CHECK(f.m.state().provisioning == Provision::saved_key);
        CHECK(f.m.state().settings == Settings::provisioning);
        // ★★★★ AND IT OPENS ON `BACK` (P-13 / spec §4-K5): `enter_provision` re-establishes the zero value, so the
        //      default is STRUCTURAL rather than remembered — reaching the act costs `short` THEN `double`.
        CHECK(f.m.state().prov_confirm == ProvConfirm::back);
        // ⛔⛔ AND ⛔ NOTHING HAS BEEN INSTALLED BY GETTING HERE — the P-2b headline, at the model layer.
        CHECK(f.prov.calls == calls);                            // ⛔ no act ran on the way in
        CHECK(f.prov.last_saved_key_id == 0u);
        CHECK(f.store.writes == 0);
        CHECK(f.live.applies == 0);
        // ★ THE TARGET IS THE ID THE **TRANSACTION** REPORTED, carried whole (⛔ not the cursor, ⛔ not the token).
        CHECK(f.m.state().saved_key_team == 0xBEEF0001u);
        // ...and the join's verdict is retired by the entry, so the offer renders no stale result.
        CHECK(f.m.state().prov_answer.outcome == UiProvOutcome::none);
    }
}

TEST_CASE("ui16-k5: ⛔ NO offer without a retained record, and ⛔ none for a CREATE — the landed flows, unchanged") {
    {   // ★ SPEC §4-K5 PIN 4: a join of a team with no record acknowledges to the MENU, exactly as N3 landed it.
        CreateFix f; const auto s = nearby_snap(3);
        CHECK(join_to_result(f, s, 0xBEEF0001u, joined_answer(0xBEEF0001u)));   // ⛔ `saved_key` false
        f.m.on_gesture(Gesture::double_press, s);
        CHECK(f.m.state().provisioning == Provision::menu);
        CHECK(f.m.state().saved_key_team == 0u);
        CHECK(f.prov.calls == 1);                                // ⛔ only the join ever ran
    }
    {   // ⛔ AND THE FLAG ALONE IS NOT ENOUGH — the OUTCOME must be `team_joined`. A create's acknowledgement may
        //   never open a saved-key offer, whatever a future adapter puts in the answer.
        CreateFix f; const auto s = nearby_snap(3);
        UiProvAnswer created{};
        created.outcome = UiProvOutcome::created;
        created.team_id = 0xBEEF0001u;
        created.saved_key = true;                                // ⚠ deliberately forged: the guard is on the OUTCOME
        f.prov.answer = created;
        CHECK(open_create_confirm(f, s));                        // CREATE TEAM -> its confirmation
        f.m.on_gesture(Gesture::short_press, s);
        f.m.on_gesture(Gesture::double_press, s);                // ...performed
        CHECK(f.m.state().provisioning == Provision::create_result);
        f.m.on_gesture(Gesture::double_press, s);
        CHECK(f.m.state().provisioning == Provision::menu);      // ⛔ NOT the offer
        CHECK(f.m.state().saved_key_team == 0u);
    }
    {   // ⛔ AND NEITHER IS A ZERO ID: an answer that reports a record for no team opens nothing.
        CreateFix f; const auto s = nearby_snap(3);
        UiProvAnswer odd = joined_with_saved_key(0xBEEF0001u);
        odd.team_id = 0;
        CHECK(join_to_result(f, s, 0xBEEF0001u, odd));
        f.m.on_gesture(Gesture::double_press, s);
        CHECK(f.m.state().provisioning == Provision::menu);
    }
}

TEST_CASE("ui16-k5: ★★★ `BACK` performs NOTHING — no key state changes and the record is never touched") {
    // ★★★★ SPEC §4-K5 PIN 2, AND IT IS THE ARM THE DEFAULT SELECTS: declining costs the operator nothing and lands
    //      exactly where the acknowledgement would have. ⛔ No seam call, ⛔ no write, ⛔ no airtime.
    // ⓘ TITLE CORRECTED 2026-08-25 (final QG): it read *"the node stays keyless"*, which OVERSTATES the invariant —
    //   BACK changes ⛔ NO key state, which is a different and stronger thing than "keyless" (a node that
    //   legitimately holds the CURRENT team's key still holds it afterwards). ⛔ No assertion moved with the title.
    CreateFix f; const auto s = nearby_snap(3);
    CHECK(join_to_result(f, s, 0xBEEF0001u, joined_with_saved_key(0xBEEF0001u)));
    f.m.on_gesture(Gesture::double_press, s);                    // acknowledge -> the offer
    CHECK(f.m.state().provisioning == Provision::saved_key);
    const int calls = f.prov.calls;
    f.m.on_gesture(Gesture::double_press, s);                    // `double` on BACK
    CHECK(f.m.state().provisioning == Provision::menu);          // ⛔ the acknowledgement's own landing
    CHECK(f.m.state().settings == Settings::provisioning);
    CHECK(f.prov.calls == calls);                                // ⛔⛔ NOTHING was performed
    CHECK(f.prov.last_op != UiProvOp::use_saved_key);
    CHECK(f.prov.last_saved_key_id == 0u);
    CHECK(f.store.writes == 0);
    CHECK(f.live.applies == 0);
    CHECK(f.m.state().saved_key_team == 0u);                     // ...and the target is retired with the screen
    CHECK(f.m.state().prov_answer.outcome == UiProvOutcome::none);
}

TEST_CASE("ui16-k5: ★★★ reaching `USE SAVED KEY` costs `short` THEN `double`, and the act runs BEFORE the screen moves") {
    CreateFix f; const auto s = nearby_snap(3);
    UiProvAnswer used{};
    used.outcome = UiProvOutcome::saved_key_used;
    f.prov.saved_answer = used;
    CHECK(join_to_result(f, s, 0xCC123456u, joined_with_saved_key(0xCC123456u)));
    f.m.on_gesture(Gesture::double_press, s);                    // acknowledge -> the offer, on BACK
    CHECK(f.m.state().prov_confirm == ProvConfirm::back);
    const int calls = f.prov.calls;
    // ...ONE `short` reaches the act, and the toggle itself performs NOTHING.
    f.m.on_gesture(Gesture::short_press, s);
    CHECK(f.m.state().prov_confirm == ProvConfirm::confirm);
    CHECK(f.prov.calls == calls);
    CHECK(f.m.state().provisioning == Provision::saved_key);
    // ...and the `double` on it is what performs — exactly once.
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.prov.calls == calls + 1);
    CHECK(f.prov.last_op == UiProvOp::use_saved_key);
    // ★★★ §8 PIN 2: the act ran while the screen was still the OFFER, and no result text existed while it ran.
    CHECK(f.prov.arm_at_call == Provision::saved_key);
    CHECK(strcmp(f.prov.head_at_call, "") == 0);
    // ★★★★ AND IT CARRIED THE FULL 32-BIT ID THE **TRANSACTION** JOINED — ⛔ not the cursor, ⛔ not the published
    //      index, ⛔ not the six-hex fingerprint the screen printed (all four values are distinct here).
    CHECK(f.prov.last_saved_key_id == 0xCC123456u);
    CHECK(f.prov.last_saved_key_id != 0u);
    CHECK(f.prov.last_saved_key_id != 1u);
    CHECK(f.prov.last_saved_key_id != 0x123456u);
    // ...and only THEN does the screen move, onto the terminal result carrying the verdict the act returned.
    CHECK(f.m.state().provisioning == Provision::create_result);
    CHECK(f.m.state().prov_answer.outcome == UiProvOutcome::saved_key_used);
    CHECK(strcmp(prov_result_head(f.m.state().prov_answer), "TEAM KEY ACTIVE") == 0);
    // ...which is terminal in exactly the way the join's was: either press acknowledges, and ⛔ the offer is not
    // re-opened (that would put a second activation one press away).
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().provisioning == Provision::menu);
    CHECK(f.prov.calls == calls + 1);
}

TEST_CASE("ui16-k5: a REFUSED join opens no offer — and the act's two floors are MARKED rather than claimed") {
    // ⛔ NO SERVICE: the join itself refuses out loud, and its acknowledgement lands on the MENU. ⓘ `CfgFix`
    //    attaches no provisioning seam at all, which is what a `!MR_FEAT_OLED`-shaped build looks like.
    CfgFix f; const auto s = nearby_snap(2);
    CHECK(open_nearby(f, s));
    f.m.on_gesture(Gesture::double_press, s);
    f.m.on_gesture(Gesture::short_press, s);
    f.m.on_gesture(Gesture::double_press, s);                    // -> JOIN, with nothing behind it
    CHECK(f.m.state().provisioning == Provision::create_result);
    CHECK(f.m.state().prov_answer.outcome == UiProvOutcome::join_refused);
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().provisioning == Provision::menu);          // ⛔ a refusal opens no offer
    CHECK(f.m.state().saved_key_team == 0u);
    CHECK(f.store.writes == 0);
    // ⛔⛔ **AND THE TWO FLOORS INSIDE `run_use_saved_key` ARE UNREACHABLE FROM THIS LAYER TODAY — SAID HERE RATHER
    //     THAN LEFT AS A SILENT GAP** ([[meshroute-mark-done-vs-missing-in-code]]): the offer opens ONLY on
    //     `create_result_gesture`, which requires a NON-NULL seam (it produced the `team_joined` answer) and a
    //     NON-ZERO id (it is a term of the condition). ⇒ neither `!_prov` nor `saved_key_team == 0` can be produced
    //     by any gesture sequence, so ⛔ no case below drives them and ⛔ no mutation counts them. They are C2
    //     floors for the day another caller reaches the act — the shape `run_join_team`'s own zero clause carries.
    // ★ WHERE THE ZERO FLOOR **IS** MEASURED: `test/test_firmware_ui_prov.cpp`'s `§UI16-K5 … FAILED activation`
    //   case (d), against the ADAPTER, where a 0 costs zero device calls and zero writes.
    CHECK(mrui::prov_result_head(mrui::UiProvAnswer{}) != nullptr);   // (the block above is the assertion's subject)
}

TEST_CASE("ui16-k5: the UNFINISHED offer does not survive BLANKING, an alarm or leaving (OQ-3, P-14)") {
    {   // ★★★★ OQ-3: an unfinished CONFIRMATION does not survive the blank — and this one is one `double` from
        //      installing a stored SECRET, so waking onto it is exactly what that rule forbids.
        CreateFix f; auto s = nearby_snap(3);
        CHECK(join_to_result(f, s, 0xBEEF0001u, joined_with_saved_key(0xBEEF0001u)));
        f.m.on_gesture(Gesture::double_press, s);
        CHECK(f.m.state().provisioning == Provision::saved_key);
        f.m.on_gesture(Gesture::short_press, s);                 // ...standing on USE SAVED KEY when it goes dark
        CHECK(f.m.state().prov_confirm == ProvConfirm::confirm);
        const auto s2 = nearby_snap(3, 0, s.now_ms + kBlankMs + 1);
        f.m.on_tick(s2);
        CHECK(f.m.state().blanked == true);
        CHECK(f.m.state().provisioning == Provision::menu);      // ⛔ NOT the offer, ⛔ not on the act
        CHECK(f.m.state().prov_confirm == ProvConfirm::back);
        CHECK(f.m.state().saved_key_team == 0u);                 // ...and the target went with it
        CHECK(f.prov.calls == 1);                                // ⛔ the blank performed nothing on its way past
    }
    {   // ★ P-14: the alarm pre-empts everything, and an unconfirmed act never stands.
        CreateFix f; const auto s = nearby_snap(3);
        CHECK(join_to_result(f, s, 0xBEEF0001u, joined_with_saved_key(0xBEEF0001u)));
        f.m.on_gesture(Gesture::double_press, s);
        f.m.on_gesture(Gesture::short_press, s);
        f.m.on_gesture(Gesture::long_arm, s);
        CHECK(f.m.state().provisioning == Provision::closed);
        CHECK(f.m.state().prov_confirm == ProvConfirm::back);
        CHECK(f.prov.calls == 1);
        CHECK(f.store.writes == 0);
    }
}

TEST_CASE("ui16-k5: the two ruled lexemes are VERBATIM, ⛔ `FORGET KEY` is absent, and the endings say only truths") {
    // ★ S-28 / S-29, owner-ruled, declared ONCE and pinned here so a re-ruling changes them in one place.
    CHECK(strcmp(mrui::kSavedKeyTitle, "SAVED KEY FOUND") == 0);
    CHECK(strlen(mrui::kSavedKeyTitle) == 15u);                              // 15 of the rail's 19 columns
    CHECK(strcmp(saved_key_label(ProvConfirm::confirm), "USE SAVED KEY") == 0);
    CHECK(strlen(saved_key_label(ProvConfirm::confirm)) == 13u);             // +1 marker = 14 of 19
    // ★ AND `BACK` IS THE ONE SHIPPED SPELLING, CALLED rather than re-spelled (the S-9 treatment).
    CHECK(strcmp(saved_key_label(ProvConfirm::back), "BACK") == 0);
    CHECK(strcmp(saved_key_label(ProvConfirm::back), prov_confirm_label(ProvConfirm::back)) == 0);
    // ⛔⛔ `FORGET KEY` (S-31) IS A RESERVED FUTURE VERB AND IS ⛔ NOT IN THIS SPEC — its absence is a test.
    CHECK(strstr(mrui::kSavedKeyTitle, "FORGET") == nullptr);
    CHECK(strstr(saved_key_label(ProvConfirm::confirm), "FORGET") == nullptr);
    UiProvAnswer used{};   used.outcome = UiProvOutcome::saved_key_used;
    UiProvAnswer failed{}; failed.outcome = UiProvOutcome::saved_key_failed; failed.reason = "rejected";
    for (const UiProvAnswer& a : { used, failed }) {
        CHECK(strstr(prov_result_head(a), "FORGET") == nullptr);
        CHECK(strstr(prov_result_head(a), "KEYLESS") == nullptr);            // S-33, forbidden
        CHECK(strstr(prov_result_head(a), "JOIN COMPLETE") == nullptr);      // S-32, forbidden
        CHECK(strstr(prov_result_head(a), "WAITING FOR KEY") == nullptr);    // S-34, forbidden
        CHECK(strlen(prov_result_head(a)) <= 19u);
        CHECK(strlen(prov_result_detail(a)) <= 19u);
        CHECK(strlen(prov_result_detail2(a)) <= 19u);
    }
    // ★★★ THE SUCCESS SCREEN SAYS THE KEY IS ACTIVE AND ⛔ NOTHING MORE — S-26's ruled lexeme REUSED (⛔ no new
    //     lexeme was invented, because §8 rules none for this ending), with ⛔ no durability WARNING under it.
    CHECK(strcmp(prov_result_head(used), "TEAM KEY ACTIVE") == 0);
    CHECK(strcmp(prov_result_detail(used), "") == 0);
    CHECK(strcmp(prov_result_detail2(used), "") == 0);
    CHECK(strcmp(prov_result_head(used), "TEAM KEY RECEIVED") != 0);         // ⛔ nothing was RECEIVED (S-25 is K3's)
    // ★★★ AND THE TWO `TEAM KEY ACTIVE` SCREENS ARE TOLD APART BY THE ROWS BELOW — the RAM-only one carries the
    //     warning, this one carries none. ⛔ The dangerous direction (the short screen for a RAM-only key) is
    //     impossible by construction: only a COMMITTED activation produces `saved_key_used`.
    UiProvAnswer unsaved{}; unsaved.outcome = UiProvOutcome::team_key_unsaved;
    CHECK(strcmp(prov_result_head(unsaved), prov_result_head(used)) == 0);
    CHECK(strcmp(prov_result_detail(unsaved), "NOT SAVED") == 0);
    CHECK(strcmp(prov_result_detail2(unsaved), "LOST ON REBOOT") == 0);
    // ★★★ THE FAILURE SCREEN NAMES **THIS ACT'S OUTCOME** — spec §8 S-39, owner-ruled — with the SERVICE's token
    //     beside it, and ⛔ it names the node's key INVENTORY nowhere: a refusal installs nothing and PRESERVES
    //     whatever unrelated key state the node had. ⛔ Never `SAVE FAILED` (only one arm is a failed write) and
    //     ⛔ never `JOIN REFUSED` (the join succeeded — that is why the offer existed).
    CHECK(strcmp(prov_result_head(failed), "KEY NOT INSTALLED") == 0);
    CHECK(strcmp(prov_result_head(failed), mrui::kSavedKeyFailedText) == 0);   // S-39, declared ONCE
    CHECK(strlen(mrui::kSavedKeyFailedText) == 17u);                           // 17 of the rail's 19 columns
    // ⛔ AND IT STATES THE **ACT'S** OUTCOME, ⛔ never the node's key inventory: `NO TEAM KEY` (S-24) was WITHDRAWN
    //    here on 2026-08-25 (QG blocker 1) because a SURGICAL refusal may leave another team's key live, which
    //    would make that sentence FALSE.
    CHECK(strcmp(prov_result_head(failed), "NO TEAM KEY") != 0);
    CHECK(strcmp(prov_result_detail(failed), "rejected") == 0);
    CHECK(strcmp(prov_result_detail2(failed), "") == 0);
    CHECK(strcmp(prov_result_head(failed), "SAVE FAILED") != 0);
    CHECK(strcmp(prov_result_head(failed), "JOIN REFUSED") != 0);
    // ⛔ AND THE TWO ENDINGS ARE TWO DIFFERENT HEADLINES: a success the operator cannot tell from a failure is the
    //    "success that isn't" this project registers.
    CHECK(strcmp(prov_result_head(used), prov_result_head(failed)) != 0);
}

// =====================================================================================================================
// §UI-16 N4 — THE `INVITE MEMBER` WINDOW: ITS ROW, ITS SNAPSHOT-AT-OPEN, ITS DEADLINE AND ITS HANDLED SET (spec §4-N4)
// =====================================================================================================================
// ★★★ WHAT THIS BLOCK MEASURES AND WHAT IT DOES NOT. The PURE decisions — the two authorities, the diff, the
//     handled set's own rules, the candidate row and every lexeme — belong to `test/test_firmware_ui_invite.cpp`
//     and to the `uiinvite` battery. What lives HERE is the MODEL's half: the FOURTH child and its RUNTIME
//     predicate, the snapshot taken ON THE TRANSITION, the window's own deadline (which ⛔ never writes
//     `_last_input_ms`), the expiry that closes the UI AND NOTHING ELSE, the FREEZE at the confirmation, what
//     `REJECT` reaches, and the discard that makes the handled set volatile.
// ⛔ THE RENDERER IS MEASURED IN NEITHER: `src/firmware_ui.cpp` is compiled by no automated gate (§B115), and its
//    cover is `tools/probe_firmware_ui`'s INVITE phase.
namespace {
// A snapshot carrying N team members, ids 81.., all AUTHORITATIVELY bound. ⚠ The window's own predicate is on by
// default here — a fixture for the INVITE cases would otherwise publish a row that is hidden.
UiSnapshot invite_snap(uint8_t n, uint32_t now_ms = 1000, bool invite_row = true) {
    UiSnapshot s = prov_snap(true, true, true, now_ms, invite_row);
    s.team_id    = 0x66C0FFEEu;
    s.team_total = n;
    s.team_shown = n;
    for (uint8_t i = 0; i < n && i < mrui::kMaxTeamRows; ++i) {
        s.team[i].id             = uint8_t(81 + i);
        s.member[i].id           = uint8_t(81 + i);
        s.member[i].key_hash32   = 0x11110000u + i;
    }
    return s;
}
// Add ONE member to a snapshot, at the end of the published list.
void add_member(UiSnapshot& s, uint8_t id, uint32_t hash, const char* name = "") {
    if (s.team_shown >= mrui::kMaxTeamRows) return;
    const uint8_t i = s.team_shown++;
    s.team_total = s.team_shown;
    s.team[i].id           = id;
    s.member[i].id         = id;
    s.member[i].key_hash32 = hash;
    for (uint8_t b = 0; name[b] && b + 1 < mrui::kInviteNameCap; ++b) s.member[i].name[b] = name[b];
}
// Land on the INVITE window FROM THE PROVISION MENU. ⚠ SEPARATE from `open_invite` below, deliberately: a
// re-open happens with the sub-view ALREADY open, and `open_provision`'s walk cannot run there — the sub-view
// owns the press, so its `short`es would be spent inside the child menu. (Measured: that is exactly what the
// first cut of these cases did, and it walked the cursor onto whatever row twelve presses landed on.)
bool enter_invite_from_menu(UiModel& m, const UiSnapshot& s) {
    if (!prov_cursor_to(m, s, ProvRow::invite)) return false;
    m.on_gesture(Gesture::double_press, s);
    return m.state().provisioning == Provision::invite;
}
// Open PROVISION and land on the INVITE window. The caller ASSERTS the landing.
bool open_invite(CfgFix& f, const UiSnapshot& s) {
    if (!open_provision(f.m, s)) return false;
    return enter_invite_from_menu(f.m, s);
}
// Walk the window's cursor to BACK and leave it, landing on the PROVISION menu. ⛔ By IDENTITY, never by index.
void leave_invite(UiModel& m, const UiSnapshot& s) {
    for (int i = 0; i < mrui::kMaxInviteRows + 2; ++i) {
        mrui::InviteSelRow r{};
        const mrui::InviteSelList l = mrui::invite_sel_rows(m.state().invite, s.member, s.team_shown);
        if (l.at(m.state().cursor, r) && r.back) break;
        m.on_gesture(Gesture::short_press, s);
    }
    m.on_gesture(Gesture::double_press, s);
}
// The candidate rows the model would bound its cursor with, for a given live snapshot.
uint8_t invite_cands(const UiModel& m, const UiSnapshot& s) {
    return uint8_t(mrui::invite_sel_rows(m.state().invite, s.member, s.team_shown).n - 1);
}
// Walk the window's cursor onto the candidate with `hash`. ⛔ Never a hardcoded index: the list is the DIFFED one
// and a REJECT re-indexes it, which is exactly what §B66 forbids reasoning about positionally.
bool invite_cursor_to(UiModel& m, const UiSnapshot& s, uint32_t hash) {
    for (int i = 0; i < mrui::kMaxInviteRows + 2; ++i) {
        mrui::InviteSelRow r{};
        const mrui::InviteSelList l = mrui::invite_sel_rows(m.state().invite, s.member, s.team_shown);
        if (l.at(m.state().cursor, r) && !r.back && r.cand.key_hash32 == hash) return true;
        m.on_gesture(Gesture::short_press, s);
    }
    return false;
}
}  // namespace

TEST_CASE("ui16-inviterow: pin 12 — the row is the FOURTH child, and it is HIDDEN off-team and off-build") {
    // ★★★ THE RUNTIME HALF, which no other child has: `prov_invite` carries `config().team_id != 0`, so the row
    //     is absent on a TEAMLESS node even on a build that could show it. A node with no membership has nobody
    //     to invite into it and no key it could ever grant — [[B209]]: hide it, ⛔ never a refusing stub.
    {
        CreateFix f; const auto s = invite_snap(1, 1000, /*invite_row=*/false);
        CHECK(open_provision(f.m, s));
        CHECK(f.m.provision_row_list(s).n == 4);                  // CREATE / JOIN NETWORK / JOIN TEAM / BACK
        for (int i = 0; i < 6; ++i) {
            ProvRow r{};
            CHECK(prov_row_under_cursor(f.m, s, r));
            CHECK(r != ProvRow::invite);                          // ⛔ on no row: unreachable, not merely undrawn
            f.m.on_gesture(Gesture::short_press, s);
        }
        CHECK(prov_cursor_to(f.m, s, ProvRow::invite) == false);
        CHECK(f.invite_dev.announcement_requests == 0);           // [[B249]] hidden means no request seam
    }
    // ⓘ THE `gateway_heltec` SHAPE (OLED=1, MR_N_LAYERS=2, MR_FEAT_TEAM=0): every child is off, so the PARENT row
    //   is hidden too — the predicate derived from the child list, which needed no change for a fourth child.
    CHECK(provision_has_child(false, false, false, false, false) == false);
    CHECK(provision_rows(false, false, false, false, false).n == 1);
    // ★ AND WITH IT PUBLISHED, IT IS THE FOURTH ROW AND IT OPENS THE WINDOW.
    {
        CreateFix f; const auto s = invite_snap(1);
        CHECK(open_provision(f.m, s));
        const ProvRowList l = f.m.provision_row_list(s);
        CHECK(l.n == 5);
        ProvRow r{};
        CHECK(l.at(0, r)); CHECK(r == ProvRow::create_team);
        CHECK(l.at(1, r)); CHECK(r == ProvRow::join_static);
        CHECK(l.at(2, r)); CHECK(r == ProvRow::join_team);
        CHECK(l.at(3, r)); CHECK(r == ProvRow::invite);           // ★ APPENDED — no landed row moved
        CHECK(l.at(4, r)); CHECK(r == ProvRow::back);
        CHECK(enter_invite_from_menu(f.m, s));
        CHECK(f.m.state().settings == Settings::provisioning);
        CHECK(f.m.state().cursor == 0);
        CHECK(f.invite_dev.announcement_requests == 1);
    }
}

TEST_CASE("ui16-invopen: B249 — snapshot then window then exactly one announcement request per fresh open") {
    CreateFix f;
    auto s = invite_snap(2);                                       // two members present BEFORE the window opens
    const int calls = f.prov.calls, writes = f.store.writes, applies = f.live.applies;
    CHECK(f.invite_dev.announcement_requests == 0);
    CHECK(open_invite(f, s));
    // ★ THE SNAPSHOT EXISTS AND IT IS THE OPENING's: both members are in it, so ⛔ neither is a candidate.
    CHECK(f.m.state().invite.taken == true);
    CHECK(f.m.state().invite.n == 2);
    CHECK(invite_cands(f.m, s) == 0);
    // ★★★★ [[B249]] THE AUTHORITY BINDING AND ORDER: the fresh-open transition reaches the attached seam once,
    //      and the fake observes both the completed snapshot and the established invitation arm AT THE CALL.
    CHECK(f.invite_dev.announcement_requests == 1);
    CHECK(f.invite_dev.snapshot_taken_at_announcement == true);
    CHECK(f.invite_dev.snapshot_n_at_announcement == 2);
    CHECK(f.invite_dev.provisioning_at_announcement == Provision::invite);
    // ...and it stays that way across an arbitrary number of refreshes (the list is rebuilt, the snapshot is not).
    for (int i = 0; i < 5; ++i) { f.m.on_tick(s); CHECK(invite_cands(f.m, s) == 0); }
    // ★ A MEMBER ARRIVING **AFTER** THE OPEN IS A CANDIDATE, and exactly one.
    add_member(s, 200, 0xAABBCCDDu);
    CHECK(invite_cands(f.m, s) == 1);
    f.m.on_tick(s);
    CHECK(invite_cands(f.m, s) == 1);
    CHECK(f.invite_dev.announcement_requests == 1);                // ⛔ redraw/tick/arrival do not repeat it
    // ⛔⛔ ASIDE FROM THE ONE SCHEDULER REQUEST, NOTHING WAS PERFORMED (spec §4-N4 pin 10): the window opens, holds
    //     and refreshes with ⛔ zero transactions, zero durable writes and zero live applies. ★ The authority is the COUNTERS, ⛔ not
    //     the screen — a model that quietly acted would look identical if it also failed to navigate.
    CHECK(f.prov.calls   == calls);
    CHECK(f.store.writes == writes);
    CHECK(f.live.applies == applies);
    // ⛔ AND RE-ENTERING FROM THE MENU RE-TAKES THE SNAPSHOT — which is what makes the window BOUNDED rather than
    //    a running diff: the candidate of the last window is an ordinary member of the next one.
    leave_invite(f.m, s);
    CHECK(f.m.state().provisioning == Provision::menu);
    CHECK(f.m.state().invite.taken == false);                      // ⛔ the window's state died with the window
    CHECK(f.invite_dev.announcement_requests == 1);                // ⛔ close does not request
    CHECK(enter_invite_from_menu(f.m, s));
    CHECK(f.invite_dev.announcement_requests == 2);                // ★ a genuinely fresh open requests afresh
    CHECK(f.invite_dev.snapshot_taken_at_announcement == true);
    CHECK(f.invite_dev.snapshot_n_at_announcement == 3);
    CHECK(f.invite_dev.provisioning_at_announcement == Provision::invite);
    CHECK(f.m.state().invite.n == 3);                              // ★ the arrival is now part of the SNAPSHOT...
    CHECK(invite_cands(f.m, s) == 0);                              // ...so it is nobody's candidate any more
}

TEST_CASE("ui16-invexpire: pin 1 — the window expires BY ITSELF at five minutes, and expiry changes NOTHING") {
    CreateFix f;
    auto s = invite_snap(1);
    CHECK(open_invite(f, s));
    CHECK(f.invite_dev.announcement_requests == 1);
    add_member(s, 200, 0xAABBCCDDu);
    CHECK(invite_cands(f.m, s) == 1);
    const int calls = f.prov.calls, writes = f.store.writes, applies = f.live.applies;
    // ⛔ ONE MILLISECOND BEFORE THE DEADLINE THE WINDOW IS STILL OPEN — the edge is asserted, not approximated.
    auto at = [&](uint32_t ms) { UiSnapshot t = s; t.now_ms = 1000 + ms; return t; };
    f.m.on_tick(at(mrui::kInviteWindowMs - 1));
    CHECK(f.m.state().provisioning == Provision::invite);
    // ★★ AND **AT** THE DEADLINE IT IS CLOSED — the edge is `wake_active`'s (`left != 0`), ⛔ not a bare
    //    `now < deadline`, because `elapsed(deadline, now)` is ZERO at the deadline and a bare comparison would run
    //    the ruled FIVE MINUTES for five minutes and one millisecond. The window needs STRICTLY POSITIVE time left,
    //    so it closes on exactly the tick the deadline arrives — the same edge the blank clock fires on.
    f.m.on_tick(at(mrui::kInviteWindowMs));
    CHECK(f.m.state().provisioning == Provision::invite_closed);
    CHECK(f.invite_dev.announcement_requests == 1);                // ⛔ expiry is not another open
    CHECK(strcmp(mrui::kInviteClosed, "WINDOW CLOSED") == 0);       // ...with the ruled word
    // ⛔⛔ P-11: THE EXPIRY GRANTED, REVOKED AND REWROTE NOTHING — no transaction, no durable write, no live
    //     apply — and the WINDOW'S OWN STATE IS GONE rather than left standing behind a screen.
    CHECK(f.prov.calls   == calls);
    CHECK(f.store.writes == writes);
    CHECK(f.live.applies == applies);
    CHECK(f.m.state().invite.taken == false);
    CHECK(invite_cands(f.m, at(mrui::kInviteWindowMs + 1)) == 0);  // ⛔ no candidate list exists past the window
    // ★ TERMINAL, ACKNOWLEDGED BY EITHER PRESS (§UI-17 §9 R-5), landing on the PROVISION menu.
    // ⓘ THE PANEL IS DARK BY NOW and the FIRST press is the wake, CONSUMED — five minutes without a gesture is
    //   twenty blank windows. That is the landed contract, not a quirk of this screen, and it is asserted here
    //   rather than dodged with an extra press nobody explains.
    CHECK(f.m.state().blanked == true);
    f.m.on_gesture(Gesture::short_press, at(mrui::kInviteWindowMs + 5));
    CHECK(f.m.state().blanked == false);
    CHECK(f.m.state().provisioning == Provision::invite_closed);
    f.m.on_gesture(Gesture::short_press, at(mrui::kInviteWindowMs + 10));
    CHECK(f.m.state().provisioning == Provision::menu);
    CHECK(f.invite_dev.announcement_requests == 1);                // ⛔ wake/ack/close do not repeat it
    // ...and the OTHER press does the same, because the screen is terminal and has no selectable row.
    {
        CreateFix g;
        auto t = invite_snap(1);
        CHECK(open_invite(g, t));
        UiSnapshot u = t; u.now_ms = t.now_ms + mrui::kInviteWindowMs + 1;
        g.m.on_tick(u);
        CHECK(g.m.state().provisioning == Provision::invite_closed);
        g.m.on_gesture(Gesture::double_press, u);                  // the wake, consumed
        g.m.on_gesture(Gesture::double_press, u);
        CHECK(g.m.state().provisioning == Provision::menu);
    }
    // ★ AND RE-OPENING WORKS AND RE-ARMS: the member set the second window sees is the SAME one the first did.
    auto later = at(mrui::kInviteWindowMs + 20);
    CHECK(enter_invite_from_menu(f.m, later));
    CHECK(f.invite_dev.announcement_requests == 2);                // ★ fresh open, fresh bounded request
    CHECK(f.m.state().invite.taken == true);
    CHECK(f.m.state().invite.n == 2);                              // both members, byte-identical across the expiry
    f.m.on_tick(at(mrui::kInviteWindowMs + 21));
    CHECK(f.m.state().provisioning == Provision::invite);          // ...and the new deadline is a fresh five minutes
}

TEST_CASE("ui16-invblank: pin 13 — the WINDOW survives blank/wake; an UNFINISHED CONFIRMATION does not") {
    // ✅ OQ-3's clarification, and the two halves are driven separately because they are two rules.
    CreateFix f;
    auto s = invite_snap(1);
    CHECK(open_invite(f, s));
    CHECK(f.invite_dev.announcement_requests == 1);
    add_member(s, 200, 0xAABBCCDDu);
    CHECK(invite_cursor_to(f.m, s, 0xAABBCCDDu));
    // ★★ THE WINDOW DOES **NOT** HOLD THE PANEL LIT (✅ OQ-3): with no press for `kBlankMs` the panel goes dark
    //    exactly as it would on any other screen — which is the observable consequence of ⛔ never writing
    //    `_last_input_ms`. A window that stamped it would keep a safety device's panel lit for five minutes and,
    //    through `ui_allows_sleep`, stop it light-sleeping at all.
    auto at = [&](uint32_t ms) { UiSnapshot t = s; t.now_ms = 1000 + ms; return t; };
    f.m.on_tick(at(kBlankMs + 1));
    CHECK(f.m.state().blanked == true);
    // ★ ...AND THE WINDOW IS STILL OPEN UNDER THE DARK PANEL: the arm, the snapshot and the cursor are intact.
    CHECK(f.m.state().provisioning == Provision::invite);
    CHECK(f.m.state().invite.taken == true);
    const uint8_t was_cursor = f.m.state().cursor;
    f.m.on_gesture(Gesture::short_press, at(kBlankMs + 50));        // the waking press is CONSUMED
    CHECK(f.m.state().blanked == false);
    CHECK(f.m.state().cursor == was_cursor);
    CHECK(f.m.state().provisioning == Provision::invite);
    CHECK(invite_cands(f.m, s) == 1);
    CHECK(f.invite_dev.announcement_requests == 1);                // ⛔ blank/wake did not re-open
    // ★★★ THE OTHER HALF: an UNFINISHED CONFIRMATION DOES NOT SURVIVE. The operator may not wake onto a screen
    //     whose act is one press away and which they may not remember opening (§3.6.5 rule 1).
    f.m.on_gesture(Gesture::double_press, at(kBlankMs + 60));
    CHECK(f.m.state().provisioning == Provision::invite_confirm);
    f.m.on_tick(at(2 * kBlankMs + 200));
    CHECK(f.m.state().blanked == true);
    CHECK(f.m.state().provisioning == Provision::invite);           // ★ back to the LIST, ⛔ not the confirmation
    CHECK(f.m.state().prov_confirm == ProvConfirm::back);
    CHECK(f.m.state().invite.taken == true);                        // ...and the WINDOW is still open
    // ⛔ AND THE DEADLINE KEPT RUNNING THROUGH THE DARK — the blank did not re-arm it, which is what "the window
    //    survives the blank" means. It still expires five minutes after the OPEN, not after the wake.
    f.m.on_tick(at(mrui::kInviteWindowMs - 1));
    CHECK(f.m.state().provisioning == Provision::invite);
    f.m.on_tick(at(mrui::kInviteWindowMs + 1));
    CHECK(f.m.state().provisioning == Provision::invite_closed);
    CHECK(f.invite_dev.announcement_requests == 1);                // ⛔ fallback/expiry did not repeat it
}

TEST_CASE("ui16-invreject: the ready confirmation freezes the hash, defaults REJECT and grants nothing in N5") {
    CreateFix f;
    auto s = invite_snap(1);
    CHECK(open_invite(f, s));
    add_member(s, 200, 0xAABBCCDDu, "Ann");
    add_member(s, 201, 0x99887766u, "Ann");                         // ★ the SAME name, a different hash (P-7d)
    CHECK(invite_cands(f.m, s) == 2);
    const int calls = f.prov.calls, writes = f.store.writes, applies = f.live.applies;
    CHECK(invite_cursor_to(f.m, s, 0xAABBCCDDu));
    f.m.on_gesture(Gesture::double_press, s);
    // ★★★ THE FREEZE: the confirmation carries the ROW's own identity — the full 32-bit hash AND its id.
    CHECK(f.m.state().provisioning == Provision::invite_confirm);
    CHECK(f.m.state().invite.sel_hash == 0xAABBCCDDu);
    CHECK(f.m.state().invite.sel_id == 200);
    CHECK(f.m.state().prov_confirm == ProvConfirm::invite_reject);  // N6's ruled safe default
    {   // pin 17: what the confirmation draws is the FULL hash, ⛔ never the six-character selection aid
        char full[mrui::kMemberHashCap];
        mrui::ui_fmt_member_hash_full(full, sizeof full, f.m.state().invite.sel_hash);
        CHECK(strcmp(full, "0xAABBCCDD") == 0);
    }
    // ★★ PIN 1 — REACHING `GRANT KEY` COSTS `short` **THEN** `double`: the `short` alone selects it and performs
    //    NOTHING (⛔ no grant, no handled-set entry, no command). The act itself is `ui16-grantact`'s.
    f.m.on_gesture(Gesture::short_press, s);
    CHECK(f.m.state().prov_confirm == ProvConfirm::invite_grant);
    CHECK(f.m.state().provisioning == Provision::invite_confirm);
    CHECK(f.m.state().invite.handled_n == 0);
    CHECK(f.invite_dev.grants == 0);
    CHECK(f.invite_dev.commands == 0);
    CHECK(strcmp(mrui::invite_confirm_label(true), "GRANT KEY") == 0);
    // REJECT is the safe default and stays the local act (F-13).
    f.m.on_gesture(Gesture::short_press, s);
    CHECK(f.m.state().prov_confirm == ProvConfirm::invite_reject);
    f.m.on_gesture(Gesture::double_press, s);
    // ★★ THE REJECT LANDED: the candidate is gone from the refreshed list, and the OTHER `Ann` is not.
    CHECK(f.m.state().provisioning == Provision::invite);
    // ⛔⛔ PIN 2 — REJECT SENDS **NOTHING**: it reaches no seam at all, which is asserted on the seam's own call
    //     count rather than on a screen (§UI-16 N6). ★ And the OTHER `Ann` below is what makes the handled set's
    //     KEY measurable: it is the hash, so one member's rejection can never answer for another's (P-7d).
    CHECK(f.invite_dev.grants == 0);
    CHECK(f.m.state().grant.st == mrui::InviteGrantState::none);
    CHECK(f.m.state().invite.handled_n == 1);
    CHECK(mrui::invite_handled_has(f.m.state().invite, 0xAABBCCDDu));
    CHECK(invite_cands(f.m, s) == 1);
    {
        mrui::InviteSelRow r{};
        CHECK(mrui::invite_sel_rows(f.m.state().invite, s.member, s.team_shown).at(0, r));
        CHECK(r.cand.key_hash32 == 0x99887766u);
    }
    for (int i = 0; i < 5; ++i) { f.m.on_tick(s); CHECK(invite_cands(f.m, s) == 1); }
    // ⛔⛔ AND IT CHANGED NO CORE, MEMBERSHIP, KEY OR NV STATE: no transaction, no durable write, no live apply.
    CHECK(f.prov.calls   == calls);
    CHECK(f.store.writes == writes);
    CHECK(f.live.applies == applies);
    // ★★★ THE SET IS VOLATILE (F-13 / P-11b): close the window and re-open it, and the candidate RETURNS.
    leave_invite(f.m, s);
    CHECK(f.m.state().provisioning == Provision::menu);
    CHECK(f.m.state().invite.handled_n == 0);                       // ⛔ discarded WITH the window, not kept
    CHECK(f.m.state().invite.sel_hash == 0);
    // ★★ ...AND RE-OPENING BRINGS THE **SUPPRESSION** DOWN: the set was volatile, so the operator's rejection did
    //    not outlive the window it was made in (P-11b, the ruling's own pin, from the other side).
    // ⚠ STATED PRECISELY, because the naive version of this case would prove the wrong thing: a member that is
    //   STILL PRESENT at the re-open is in the NEW snapshot and is therefore nobody's candidate — that is the
    //   TWO-AUTHORITY rule doing its job, ⛔ not the handled set. What the volatility means is that the HASH is no
    //   longer suppressed, so the same peer ARRIVING INTO a window that did not open with it is a candidate again.
    UiSnapshot s2 = invite_snap(1);                                 // the rejected peer is not published here...
    CHECK(enter_invite_from_menu(f.m, s2));
    CHECK(f.m.state().invite.handled_n == 0);
    CHECK(mrui::invite_handled_has(f.m.state().invite, 0xAABBCCDDu) == false);
    CHECK(invite_cands(f.m, s2) == 0);
    add_member(s2, 200, 0xAABBCCDDu, "Ann");                        // ...and now it arrives again
    CHECK(invite_cands(f.m, s2) == 1);                              // ★ IT RETURNS
}

TEST_CASE("ui16-invfreeze: a refresh between the two presses does ⛔ NOT move what the operator is acting on") {
    // ★★★★ F-14's closing rule: *"opening a confirmation FREEZES the selected hash/id"*. The window refreshes
    //      LOCALLY while it is open, so between the `double` that opens the confirmation and the `double` that
    //      acts, the LIST CAN RE-INDEX — a new member arriving ahead of the selected one shifts every later row.
    //      ⛔ If the act re-read the row under the cursor it would reject somebody the operator never chose.
    CreateFix f;
    auto s = invite_snap(1);
    CHECK(open_invite(f, s));
    add_member(s, 200, 0xAABBCCDDu);
    CHECK(invite_cursor_to(f.m, s, 0xAABBCCDDu));
    const uint8_t at_open = f.m.state().cursor;
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().provisioning == Provision::invite_confirm);
    CHECK(f.m.state().invite.sel_hash == 0xAABBCCDDu);
    // ★ THE REFRESH: two more members arrive while the confirmation is up, and one of them is published BEFORE
    //   the selected one is walked to again — the cursor's index now names a different candidate.
    UiSnapshot moved = invite_snap(1);
    add_member(moved, 150, 0x0BADF00Du);
    add_member(moved, 200, 0xAABBCCDDu);
    moved.now_ms = s.now_ms + 100;
    f.m.on_tick(moved);
    CHECK(f.m.state().provisioning == Provision::invite_confirm);   // ⛔ a refresh does not close a confirmation
    CHECK(f.m.state().invite.sel_hash == 0xAABBCCDDu);              // ★★ THE FROZEN TARGET DID NOT MOVE
    CHECK(f.m.state().invite.sel_id == 200);
    {
        mrui::InviteSelRow r{};
        CHECK(mrui::invite_sel_rows(f.m.state().invite, moved.member, moved.team_shown).at(at_open, r));
        CHECK(r.cand.key_hash32 == 0x0BADF00Du);                    // ...although the CURSOR's row now names another
    }
    // ...and the act rejects the FROZEN one, ⛔ never the row that slid under the cursor.
    f.m.on_gesture(Gesture::double_press, moved);
    CHECK(mrui::invite_handled_has(f.m.state().invite, 0xAABBCCDDu));
    CHECK(mrui::invite_handled_has(f.m.state().invite, 0x0BADF00Du) == false);
}

TEST_CASE("ui16-invquiet: pin 11 — with the window CLOSED a new member changes no screen, cursor or note") {
    // ⛔ P-12: there is no unsolicited one-button grant prompt outside a window. The diff runs only while the arm
    //    is live, and ⛔ nothing about a member arriving navigates, selects or annotates anything.
    CfgFix f;
    auto s = invite_snap(1);
    to_settings_menu(f.m, s);
    const Screen scr = f.m.state().screen;
    const uint8_t cur = f.m.state().cursor;
    const Settings set = f.m.state().settings;
    for (int i = 0; i < 4; ++i) {
        add_member(s, uint8_t(200 + i), 0xAABB0000u + uint32_t(i));
        s.now_ms += 1000;
        f.m.on_tick(s);
        CHECK(f.m.state().screen == scr);
        CHECK(f.m.state().cursor == cur);
        CHECK(f.m.state().settings == set);
        CHECK(f.m.state().provisioning == Provision::closed);
        CHECK(f.m.state().invite.taken == false);                   // ⛔ no snapshot, so no diff can even run
        CHECK(strcmp(settings_note(f.m.state()), "") == 0);         // ⛔ and no note appeared
    }
    CHECK(f.store.writes == 0);
}

// ===============================================================================================================
// §UI-16 N5 — SIDE-EFFECT-FREE PREFLIGHT, EXPLICIT REQUEST, MATCHED CACHE ARRIVAL AND NAME REFRESH
// ===============================================================================================================
TEST_CASE("ui16-reqpubkey: BACK is default and zero commands precede short + double on REQUEST PUBKEY") {
    CreateFix f;
    f.invite_dev.present = false;
    auto s = invite_snap(1);
    CHECK(open_invite(f, s));
    add_member(s, 200, 0xAABBCCDDu);
    CHECK(invite_cursor_to(f.m, s, 0xAABBCCDDu));
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().provisioning == Provision::invite_need_pubkey);
    CHECK(f.m.state().prov_confirm == ProvConfirm::back);
    CHECK(f.m.state().invite.sel_hash == 0xAABBCCDDu);
    CHECK(f.invite_dev.commands == 0);                             // candidate entry/preflight aired nothing
    CHECK(f.invite_dev.read_floor == MESHROUTE_NS::Node::PeerKeyConf::authoritative);
    char full[mrui::kMemberHashCap];
    mrui::ui_fmt_member_hash_full(full, sizeof full, f.m.state().invite.sel_hash);
    CHECK(strcmp(full, "0xAABBCCDD") == 0);                       // full identity survives into the request screen

    f.m.on_gesture(Gesture::double_press, s);                      // BACK
    CHECK(f.m.state().provisioning == Provision::invite);
    CHECK(f.invite_dev.commands == 0);

    CHECK(invite_cursor_to(f.m, s, 0xAABBCCDDu));
    f.m.on_gesture(Gesture::double_press, s);
    f.m.on_gesture(Gesture::short_press, s);                       // select REQUEST PUBKEY
    CHECK(f.m.state().prov_confirm == ProvConfirm::confirm);
    CHECK(f.invite_dev.commands == 0);                             // the short alone is not authorisation
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().provisioning == Provision::invite_wait_pubkey);
    CHECK(f.invite_dev.commands == 1);
    CHECK(f.invite_dev.last.kind == MESHROUTE_NS::CmdKind::reqpubkey);
    CHECK(f.invite_dev.last.u.resolve.dst_hash == 0xAABBCCDDu);
    CHECK(f.invite_dev.last.u.resolve.dst_id == 0);
    CHECK(f.invite_dev.last.u.resolve.plane == static_cast<uint8_t>(MESHROUTE_NS::Plane::TEAM));

    UiSnapshot later = s;
    later.now_ms += MESHROUTE_NS::protocol::hash_locate_giveup_ms + 1u; // the REAL locate timeout is not a grant
    f.m.on_tick(later);
    CHECK(f.m.state().provisioning == Provision::invite_wait_pubkey);
    CHECK(f.invite_dev.commands == 1);                             // no retry and no grant command
}

TEST_CASE("ui16-reqpubkey-floor: an overheard key and a name do not enable GRANT KEY; authoritative does") {
    CreateFix f;
    f.invite_dev.present = true;
    f.invite_dev.conf = MESHROUTE_NS::Node::PeerKeyConf::overheard;
    auto s = invite_snap(1);
    CHECK(open_invite(f, s));
    add_member(s, 200, 0xAABBCCDDu, "Spoofed name");
    CHECK(invite_cursor_to(f.m, s, 0xAABBCCDDu));
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().provisioning == Provision::invite_need_pubkey); // name cannot raise confidence
    CHECK(f.invite_dev.read_floor == MESHROUTE_NS::Node::PeerKeyConf::authoritative);
    CHECK(f.invite_dev.commands == 0);
    f.m.on_gesture(Gesture::double_press, s);                         // BACK

    f.invite_dev.conf = MESHROUTE_NS::Node::PeerKeyConf::authoritative;
    CHECK(invite_cursor_to(f.m, s, 0xAABBCCDDu));
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().provisioning == Provision::invite_confirm);
    CHECK(f.m.state().prov_confirm == ProvConfirm::invite_reject);    // REJECT is selected; GRANT is enabled
    CHECK(f.invite_dev.commands == 0);                               // preflight is still side-effect-free
}

TEST_CASE("ui16-reqpubkey-push: wrong hash stays waiting; right hash refreshes the cached name and enables grant") {
    CreateFix f;
    f.invite_dev.present = false;
    auto s = invite_snap(1);
    CHECK(open_invite(f, s));
    add_member(s, 200, 0xAABBCCDDu);
    CHECK(invite_cursor_to(f.m, s, 0xAABBCCDDu));
    f.m.on_gesture(Gesture::double_press, s);
    f.m.on_gesture(Gesture::short_press, s);
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().provisioning == Provision::invite_wait_pubkey);
    CHECK(f.invite_dev.commands == 1);

    f.invite_dev.present = true;
    f.invite_dev.conf = MESHROUTE_NS::Node::PeerKeyConf::authoritative;
    MESHROUTE_NS::Push wrong{};
    wrong.kind = MESHROUTE_NS::PushKind::peer_key_cached;
    wrong.sender_hash = 0x99887766u;
    wrong.body_len = 5;
    memcpy(wrong.body, "Wrong", 5);
    const int reads_before = f.invite_dev.reads;
    f.m.on_invite_push(wrong);
    CHECK(f.m.state().provisioning == Provision::invite_wait_pubkey);
    CHECK(f.invite_dev.reads == reads_before);                       // wrong identity does not even preflight
    CHECK(s.member[1].name[0] == '\0');                              // no UI-side name field was populated

    MESHROUTE_NS::Push right = wrong;
    right.sender_hash = 0xAABBCCDDu;
    f.m.on_invite_push(right);
    CHECK(f.m.state().provisioning == Provision::invite);           // back to the locally refreshed candidate row
    CHECK(f.invite_dev.read_hash == 0xAABBCCDDu);
    CHECK(f.invite_dev.read_floor == MESHROUTE_NS::Node::PeerKeyConf::authoritative);
    CHECK(f.invite_dev.commands == 1);                               // no second request

    // `build_snapshot` now publishes the name from the EXISTING cache under this same hash; simulate that one read.
    memcpy(s.member[1].name, "Wolfgangetta", 12);
    char row[mrui::kInviteRowCap];
    mrui::ui_fmt_invite_row(row, sizeof row, '>', s.member[1]);
    CHECK(strcmp(row, ">Wolfga T200 BBCCDD") == 0);                 // name added; fingerprint unchanged
    CHECK(invite_cursor_to(f.m, s, 0xAABBCCDDu));
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().provisioning == Provision::invite_confirm);   // live key, never the name, enables grant
    CHECK(f.m.state().invite.sel_hash == 0xAABBCCDDu);
}

// ★★★★ THE QG BLOCKER, AT THE SCREEN (2026-08-24): `WAITING FOR PUBKEY` says an identity request is outstanding.
//      The first cut entered it on the operator's `double` ALONE — with no seam attached, and against a
//      synchronous refusal — so the panel waited for the answer to a question nobody asked, and pin 5 then leaves
//      that screen up for ever. ⓘ Driven on the COMMAND COUNT and the state, ⛔ never on a word.
TEST_CASE("ui16-reqpubkey-refused: no seam and a refusal both STAY at NEED PUBKEY — the wait is never claimed") {
    // ---- (a) NO SEAM AT ALL. `CfgFix` attaches none, which is the `!MR_FEAT_OLED`-shaped build and the
    //          partially-wired probe — and the arm that fails CLOSED, exactly as the preflight above it does.
    {
        CfgFix f;
        auto s = invite_snap(1);
        CHECK(open_invite(f, s));
        add_member(s, 200, 0xAABBCCDDu);
        CHECK(invite_cursor_to(f.m, s, 0xAABBCCDDu));
        f.m.on_gesture(Gesture::double_press, s);
        CHECK(f.m.state().provisioning == Provision::invite_need_pubkey);   // the unattached preflight refuses
        f.m.on_gesture(Gesture::short_press, s);
        CHECK(f.m.state().prov_confirm == ProvConfirm::confirm);            // REQUEST PUBKEY selected
        f.m.on_gesture(Gesture::double_press, s);
        CHECK(f.m.state().provisioning == Provision::invite_need_pubkey);   // ⛔ NOT invite_wait_pubkey
        CHECK(f.m.state().prov_confirm == ProvConfirm::confirm);            // the action stays put, so a retry costs
        UiSnapshot later = s;                                               // one press rather than a re-entry
        // ⓘ THE STEP IS DELIBERATELY INSIDE `kBlankMs`: past it, pin 13's OWN ruling drops an unfinished
        //   confirmation back to the window, which would mask what this line is asking (that ticking alone
        //   promotes nothing). The blank behaviour has its own case.
        later.now_ms += 5000;
        f.m.on_tick(later);
        CHECK(f.m.state().provisioning == Provision::invite_need_pubkey);   // ⛔ and no wait screen appears
    }
    // ---- (b) A SYNCHRONOUS REFUSAL. The command WAS attempted — the operator authorised it — and the request
    //          still did not start, which is the whole distinction the first cut collapsed.
    {
        CreateFix f;
        f.invite_dev.present = false;
        f.invite_dev.answer = mrui::UiInviteIssue{ true, MESHROUTE_NS::CmdCode::err_no_identity, false };
        auto s = invite_snap(1);
        CHECK(open_invite(f, s));
        add_member(s, 200, 0xAABBCCDDu);
        CHECK(invite_cursor_to(f.m, s, 0xAABBCCDDu));
        f.m.on_gesture(Gesture::double_press, s);
        f.m.on_gesture(Gesture::short_press, s);
        f.m.on_gesture(Gesture::double_press, s);
        CHECK(f.invite_dev.commands == 1);                                  // it was asked...
        CHECK(f.m.state().provisioning == Provision::invite_need_pubkey);   // ...and ⛔ refused, so no wait
        // The retry is one press, and it is still not a wait while the refusal stands.
        f.m.on_gesture(Gesture::double_press, s);
        CHECK(f.invite_dev.commands == 2);
        CHECK(f.m.state().provisioning == Provision::invite_need_pubkey);
        // A parse/format failure has the SAME shape from here — and `code` alone cannot tell it from a success,
        // because `CmdResult::code` defaults to `queued` (see the invite unit's own case).
        f.invite_dev.answer = mrui::UiInviteIssue{};
        f.m.on_gesture(Gesture::double_press, s);
        CHECK(f.invite_dev.commands == 3);
        CHECK(f.m.state().provisioning == Provision::invite_need_pubkey);
        // ★★★ AND THE RACE THAT **IS** A SUCCESS: `queued` with the TX path handed nothing (the local-cache
        //     branch, which answers through `peer_key_cached`). ⛔ It must enter the wait, or the operator whose
        //     request already succeeded is stranded on the confirmation.
        f.invite_dev.answer = mrui::UiInviteIssue{ true, MESHROUTE_NS::CmdCode::queued, false };
        f.m.on_gesture(Gesture::double_press, s);
        CHECK(f.invite_dev.commands == 4);
        CHECK(f.m.state().provisioning == Provision::invite_wait_pubkey);
        // ...and the arriving push completes it exactly as the accepted-on-air path's does.
        f.invite_dev.present = true;
        f.invite_dev.conf = MESHROUTE_NS::Node::PeerKeyConf::authoritative;
        MESHROUTE_NS::Push right{};
        right.kind = MESHROUTE_NS::PushKind::peer_key_cached;
        right.sender_hash = 0xAABBCCDDu;
        f.m.on_invite_push(right);
        CHECK(f.m.state().provisioning == Provision::invite);
        CHECK(f.invite_dev.commands == 4);
    }
}

TEST_CASE("ui16-reqpubkey-resources: N5 adds no frame/state carrier and preserves the packed invite layout") {
    CHECK(sizeof(mrui::InviteWindow) == 104u);
    CHECK(offsetof(mrui::InviteWindow, hash) == 0u);
    CHECK(offsetof(mrui::InviteWindow, handled) == 32u);
    CHECK(offsetof(mrui::InviteWindow, id_bits) == 64u);
    CHECK(offsetof(mrui::InviteWindow, sel_hash) == 96u);
    CHECK(offsetof(mrui::InviteWindow, n) == 100u);
    CHECK(offsetof(mrui::InviteWindow, handled_n) == 101u);
    CHECK(offsetof(mrui::InviteWindow, sel_id) == 102u);
    CHECK(offsetof(mrui::InviteWindow, taken) == 103u);
    // ★★ §UI-16 N6 — THE ONE CARRIER THIS SLICE ADDS, AND IT COSTS EXACTLY ITSELF: `UiState` 448 -> **456** and
    //    `UiModel` 872 -> **880** (+8 each — the 4-aligned verdict lands in the tail quantum the window's array
    //    already opened). ⛔ The window is UNMOVED: the offsets above are the same eight they were, which is what
    //    proves the verdict was ADDED beside it rather than folded into it.
    // ⚠ NATIVE ALIGNMENT HIDES THE BOARD FIGURE (D2's standing warning) — this pins the shape, not the flash cost.
    CHECK(sizeof(mrui::InviteGrantResult) == 8u);
    // ⚠ RE-MEASURED 2026-08-25 (§UI-16 K6): `UiState` 456 -> **496** and `UiModel` 880 -> **920**, because K6's two
    //   carriers (`forget_team` 4 B + the frozen `SavedKeyList` 36 B) land at the struct's tail. ⛔ NOTHING N6 owns
    //   moved — the eight `InviteWindow` offsets above and `InviteGrantResult`'s size are the ones it landed with,
    //   which is what proves K6 ADDED beside this slice rather than into it (the full K6 arithmetic is in
    //   `ui16-k6-resources`).
    // ⚠ RE-MEASURED 2026-08-25 (§UI-16 K7, [[B245]]): `UiState` 496 -> **504** and `UiModel` 920 -> **928**,
    //   because the roster grant's two frozen fields (`compose_grant_hash` 4 B + `compose_grant_row` 1 B) take one
    //   8-byte quantum at the head of the struct. ⛔ NOTHING N5/N6 OWNS MOVED — the eight `InviteWindow` offsets
    //   above and `InviteGrantResult`'s size are the ones those slices landed with, and `UiSnapshot` is untouched
    //   because K7's one published field lands in an existing pad (the full K7 arithmetic is in
    //   `ui16-k7-resources`).
    CHECK(sizeof(mrui::UiState) == 504u);
    // ⓘ ⚠ **RE-PINNED 2026-08-26 BY §UI-10/11 P3, AND THE SUPERSEDED FIGURE IS KEPT VISIBLE: `1008u`.** The struct
    //   grew by the compose-list projection — `uint32_t preset_generation` at the old 8-aligned END (1008, free) plus
    //   two alignof-1 `ComposeList`s (161 each) at 1012 and 1173 — so it measures **1336 (+328)**. ⛔ NOTHING BELOW
    //   MOVED: every offset this case pins is ahead of `member[]` and is byte-identical.
    CHECK(sizeof(mrui::UiSnapshot) == 1336u);          // ⛔ UNCHANGED BY N6 ITSELF — see the note above
    CHECK(sizeof(mrui::UiModel) == 928u);
}

// ================================================= §UI-16 N6 — THE GRANT ACT's MODEL HALF (the pure unit's own
//                                                   rulings are `--target=uiinvite`)
// ★★★ WHAT IS DRIVEN HERE IS THE **FLOW**: which gesture may reach the seam, what it is handed, what the verdict
//     screen holds, and what a push does to it. The eight arms, the words and the correlation RULE are the pure
//     unit's and are driven in `test_firmware_ui_invite.cpp`.
TEST_CASE("ui16-grantact: `short` then `double` performs ONE grant, on the FROZEN hash, and lands the verdict") {
    CreateFix f;
    auto s = invite_snap(1);
    CHECK(open_invite(f, s));
    add_member(s, 200, 0xAABBCCDDu, "Ann");
    add_member(s, 201, 0x99887766u, "Ann");                        // ★ the SAME name, a different hash (P-7d)
    CHECK(invite_cursor_to(f.m, s, 0xAABBCCDDu));
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().provisioning == Provision::invite_confirm);
    // ⛔ PIN 1 — REJECT IS SELECTED INITIALLY, so the FIRST double would REJECT: reaching the grant costs a short.
    CHECK(f.m.state().prov_confirm == ProvConfirm::invite_reject);
    CHECK(f.invite_dev.grants == 0);
    f.m.on_gesture(Gesture::short_press, s);
    CHECK(f.m.state().prov_confirm == ProvConfirm::invite_grant);
    CHECK(f.invite_dev.grants == 0);                               // the SHORT alone still performs nothing
    f.invite_dev.tx = MESHROUTE_NS::Node::TeamKeyGrantTx::queued;
    f.invite_dev.tx_ctr = 4242;
    // ★★★★ §UI-16 N6b — THE SEAM RESOLVES **77**, WHILE THE ROW THIS SCREEN FROZE SAYS **200**. That is a team-DAD
    //      inside the window, and it is the exact input that used to make the verdict uncorrelatable: the core
    //      airs the grant to the NEW id, and a panel holding the OLD one waits for a push that cannot come.
    f.invite_dev.tx_dst = 77;
    CHECK(f.m.state().invite.sel_id == 200);                       // the freeze itself is unchanged (F-14)...
    f.m.on_gesture(Gesture::double_press, s);
    // ★★★ EXACTLY ONE FORWARD, on the FROZEN hash, on the TEAM plane.
    CHECK(f.invite_dev.grants == 1);
    CHECK(f.invite_dev.grant_hash == 0xAABBCCDDu);
    CHECK(f.invite_dev.grant_plane == mrui::kInviteGrantPlane);
    CHECK(f.invite_dev.grant_plane == MESHROUTE_NS::Plane::TEAM);
    // ★★★★ AND THE PANEL SAYS `GRANT QUEUED` — ⛔ NOT `KEY SENT` (F-9, the headline): nothing has aired yet.
    CHECK(f.m.state().provisioning == Provision::invite_result);
    CHECK(f.m.state().grant.st == mrui::InviteGrantState::queued);
    CHECK(strcmp(mrui::invite_grant_word(f.m.state().grant.st), "GRANT QUEUED") == 0);
    CHECK(strcmp(mrui::invite_grant_word(f.m.state().grant.st), "KEY SENT") != 0);
    // ...the verdict carries its OWN identity, because the window is gone (P-7c).
    CHECK(f.m.state().grant.hash == 0xAABBCCDDu);
    // ★★★★ THE CORRELATION'S SECOND TERM IS THE **CORE'S** ANSWER (§UI-16 N6b), ⛔ not the frozen row's id.
    CHECK(f.m.state().grant.dst == 77);
    CHECK(f.m.state().grant.dst != 200);                           // ⛔ ...and provably not the selection's
    CHECK(f.m.state().grant.ctr == 4242);
    CHECK(f.m.state().invite.taken == false);                      // the act ENDED the window
    CHECK(f.m.state().invite.sel_hash == 0);
    // ⛔ NO DURABLE WRITE, NO TRANSACTION, NO LIVE APPLY: a grant is airtime, ⛔ never a provisioning act.
    CHECK(f.store.writes == 0);
    CHECK(f.prov.calls == 0);
    CHECK(f.live.applies == 0);
    // ★★ PIN 9 — TERMINAL, ACKNOWLEDGED BY EITHER PRESS, and ⛔ acknowledging RE-RUNS NOTHING.
    f.m.on_gesture(Gesture::short_press, s);
    CHECK(f.m.state().provisioning == Provision::menu);
    CHECK(f.invite_dev.grants == 1);
    CHECK(f.m.state().grant.st == mrui::InviteGrantState::none);   // ...and the verdict is retired with the screen
    {   // the OTHER press acknowledges it too
        CreateFix g;
        auto t = invite_snap(1);
        CHECK(open_invite(g, t));
        add_member(t, 200, 0xAABBCCDDu);
        CHECK(invite_cursor_to(g.m, t, 0xAABBCCDDu));
        g.m.on_gesture(Gesture::double_press, t);
        g.m.on_gesture(Gesture::short_press, t);
        g.m.on_gesture(Gesture::double_press, t);
        CHECK(g.m.state().provisioning == Provision::invite_result);
        g.m.on_gesture(Gesture::double_press, t);
        CHECK(g.m.state().provisioning == Provision::menu);
        CHECK(g.m.state().grant.st == mrui::InviteGrantState::none);
    }
}

TEST_CASE("ui16-grantpush: only a CORRELATED push promotes the verdict, and only on the verdict screen") {
    CreateFix f;
    auto s = invite_snap(1);
    CHECK(open_invite(f, s));
    add_member(s, 200, 0xAABBCCDDu);
    CHECK(invite_cursor_to(f.m, s, 0xAABBCCDDu));
    f.m.on_gesture(Gesture::double_press, s);
    f.m.on_gesture(Gesture::short_press, s);
    f.invite_dev.tx_ctr = 4242;
    f.invite_dev.tx_dst = 77;                                       // §UI-16 N6b: a re-DAD moved the member off 200
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().provisioning == Provision::invite_result);
    CHECK(f.m.state().grant.st == mrui::InviteGrantState::queued);
    auto aired = [](uint8_t dst, uint16_t ctr) {
        MESHROUTE_NS::Push pu{};
        pu.kind = MESHROUTE_NS::PushKind::send_aired; pu.dst = dst; pu.ctr = ctr; return pu;
    };
    // ⛔ BOTH TERMS: a different dst and a different ctr each leave `GRANT QUEUED` standing.
    CHECK(f.m.on_invite_grant_push(aired(201, 4242)) == false);
    CHECK(f.m.state().grant.st == mrui::InviteGrantState::queued);
    CHECK(f.m.on_invite_grant_push(aired(77, 4243)) == false);
    CHECK(f.m.state().grant.st == mrui::InviteGrantState::queued);
    // ⛔⛔ AND THE **FROZEN** ID DOES NOT PROMOTE IT EITHER (§UI-16 N6b, blocker 2): the row said 200, the core
    //     aired to 77, and a screen correlating on what it remembered would hang at `GRANT QUEUED` for ever.
    CHECK(f.m.on_invite_grant_push(aired(200, 4242)) == false);
    CHECK(f.m.state().grant.st == mrui::InviteGrantState::queued);
    // ★★★ THE CORRELATED EDGE PROMOTES — and it marks the frame dirty, or the truth would be invisible.
    f.m.on_tick(s);                                                 // consume any pending repaint
    const bool was_dirty = f.m.state().dirty;
    CHECK(f.m.on_invite_grant_push(aired(77, 4242)) == true);
    CHECK(f.m.state().grant.st == mrui::InviteGrantState::sent);
    CHECK(strcmp(mrui::invite_grant_word(f.m.state().grant.st), "KEY SENT") == 0);
    CHECK((f.m.state().dirty || was_dirty));
    CHECK(f.m.state().provisioning == Provision::invite_result);    // ⛔ a push NEVER navigates
    // ⛔ ...AND OFF THE VERDICT SCREEN NOTHING IS PROMOTED: once acknowledged there is nothing to upgrade.
    f.m.on_gesture(Gesture::short_press, s);
    CHECK(f.m.state().provisioning == Provision::menu);
    CHECK(f.m.on_invite_grant_push(aired(77, 4242)) == false);
    CHECK(f.m.state().provisioning == Provision::menu);
    // ★★★★ AND THE ALARM'S PATH IS THE ONE THAT LEAVES A LIVE VERDICT BEHIND: `long_arm` -> `close_provisioning`
    //      retires the ARM and the window through `provision_reset_on_leave`, which by design knows nothing about
    //      the verdict — so the queued `{dst, ctr}` is still in RAM. ⛔ A push may NOT promote it there: the panel
    //      is showing an EMERGENCY, and a screen nobody can see may not be told a private key aired.
    {
        CreateFix g;
        auto t = invite_snap(1);
        CHECK(open_invite(g, t));
        add_member(t, 200, 0xAABBCCDDu);
        CHECK(invite_cursor_to(g.m, t, 0xAABBCCDDu));
        g.m.on_gesture(Gesture::double_press, t);
        g.m.on_gesture(Gesture::short_press, t);
        g.invite_dev.tx_ctr = 4242;
        g.invite_dev.tx_dst = 77;
        g.m.on_gesture(Gesture::double_press, t);
        CHECK(g.m.state().provisioning == Provision::invite_result);
        g.m.on_gesture(Gesture::long_arm, t);
        CHECK(g.m.state().provisioning == Provision::closed);
        CHECK(g.m.state().grant.st == mrui::InviteGrantState::queued);   // ⚠ still in RAM, by construction
        CHECK(g.m.on_invite_grant_push(aired(77, 4242)) == false);       // ⛔ ...and unreachable to a push
        CHECK(g.m.state().grant.st == mrui::InviteGrantState::queued);
    }
}

TEST_CASE("ui16-grantwindow: pin 8 — a double that lands on an EXPIRED window grants NOTHING") {
    // ★★★★ THE TICK RUNS **AFTER** THE GESTURE, so this is the one ordering in which the ruled five minutes could
    //      fail to bound the act: without the guard the grant fires and the closing tick arrives afterwards.
    CreateFix f;
    auto s = invite_snap(1);
    CHECK(open_invite(f, s));
    add_member(s, 200, 0xAABBCCDDu);
    CHECK(invite_cursor_to(f.m, s, 0xAABBCCDDu));
    f.m.on_gesture(Gesture::double_press, s);
    f.m.on_gesture(Gesture::short_press, s);
    CHECK(f.m.state().prov_confirm == ProvConfirm::invite_grant);
    UiSnapshot late = s; late.now_ms = 1000 + mrui::kInviteWindowMs;   // EXACTLY the deadline: already closed
    f.m.on_gesture(Gesture::double_press, late);
    CHECK(f.invite_dev.grants == 0);
    CHECK(f.m.state().provisioning == Provision::invite_closed);
    CHECK(f.m.state().grant.st == mrui::InviteGrantState::none);
    // ...and one millisecond EARLIER it is still a live window, so the guard is an EDGE and not a blanket refusal.
    CreateFix g;
    auto t = invite_snap(1);
    CHECK(open_invite(g, t));
    add_member(t, 200, 0xAABBCCDDu);
    CHECK(invite_cursor_to(g.m, t, 0xAABBCCDDu));
    g.m.on_gesture(Gesture::double_press, t);
    g.m.on_gesture(Gesture::short_press, t);
    UiSnapshot just = t; just.now_ms = 1000 + mrui::kInviteWindowMs - 1;
    g.m.on_gesture(Gesture::double_press, just);
    CHECK(g.invite_dev.grants == 1);
    CHECK(g.m.state().provisioning == Provision::invite_result);
}

TEST_CASE("ui16-grantseam: with NO seam attached the grant performs nothing and claims nothing") {
    // ⛔ FAIL CLOSED (C2), the N5 QG blocker's shape one screen over: an unattached model must ⛔ not enter a
    //    verdict screen for an act that never ran.
    CfgFix f;                                                        // ⛔ no `attach_invite` — this is the point
    UiFakeProvision prov; prov.m = &f.m; f.m.attach_provision(prov);
    auto s = invite_snap(1);
    CHECK(open_invite(f, s));
    add_member(s, 200, 0xAABBCCDDu);
    CHECK(invite_cursor_to(f.m, s, 0xAABBCCDDu));
    f.m.on_gesture(Gesture::double_press, s);
    // ⓘ With no seam the preflight fails closed, so the candidate lands on NEED PUBKEY — and its own BACK-default
    //   request cannot run either. The grant-ready arm is reached here only by driving the state directly, which
    //   is what the second half of this case does.
    CHECK(f.m.state().provisioning == Provision::invite_need_pubkey);
    f.m.on_gesture(Gesture::short_press, s);
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().provisioning == Provision::invite_need_pubkey);   // ⛔ no WAITING claim, no act
    CHECK(f.m.state().grant.st == mrui::InviteGrantState::none);
    CHECK(f.store.writes == 0);
}

TEST_CASE("ui16-invalarm: `long_arm` PRE-EMPTS the window, and the handled set does not survive it (P-14)") {
    CreateFix f;
    auto s = invite_snap(1);
    CHECK(open_invite(f, s));
    add_member(s, 200, 0xAABBCCDDu);
    CHECK(invite_cursor_to(f.m, s, 0xAABBCCDDu));
    f.m.on_gesture(Gesture::double_press, s);
    f.m.on_gesture(Gesture::double_press, s);                       // REJECT — the set now holds one hash
    CHECK(f.m.state().invite.handled_n == 1);
    f.m.on_gesture(Gesture::long_arm, s);
    CHECK(f.m.state().provisioning == Provision::closed);
    CHECK(f.m.state().settings == Settings::browsing);
    CHECK(f.m.state().invite.taken == false);
    CHECK(f.m.state().invite.handled_n == 0);                       // ★ volatile, and the alarm is a close like any other
    CHECK(f.store.writes == 0);
    f.m.on_gesture(Gesture::long_cancel, s);
    const auto s2 = invite_snap(1, s.now_ms + kCancelledMs + 1);
    f.m.on_tick(s2);
    CHECK(f.m.emergency() == Emergency::idle);
}

// =====================================================================================================================
// §UI-16 K7 ([[B245]]) — THE ROSTER GRANT: the per-member act on the ENTERED TEAM screen (spec §K7, ruled 2026-08-25)
// =====================================================================================================================
// ★★★ WHAT THIS BLOCK MEASURES, AND WHAT IT DELIBERATELY DOES NOT. K7 is an **ENTRY POINT** and nothing else: every
//     screen, lexeme, send path and outcome word past the act belongs to N5/N6, is reached VERBATIM, and is already
//     driven by the cases above and by `test/test_firmware_ui_invite.cpp`. ⇒ what is driven HERE is exactly the
//     seam: WHERE the act hangs, WHEN it is offered, WHAT identity it freezes, and that reaching the landed chain
//     from this door changes NOTHING about the chain — including the invitation window it deliberately bypasses.
// ⛔ THE RENDERER IS MEASURED IN NEITHER SUITE (§B115): its cover is `tools/probe_firmware_ui`'s roster-grant phase,
//    which drives the whole B245 repro through the REAL services.
namespace {
// `invite_snap`'s fixture PLUS the two facts the roster grant asks about **US**: the content key we would ship, and
// our own stable identity — the value `Node::team_key_grant_send`'s `self` arm compares against.
UiSnapshot k7_snap(uint8_t n, uint32_t now_ms = 1000) {
    UiSnapshot s = invite_snap(n, now_ms);
    s.team_key_present = true;
    s.my_key_hash32    = 0x5E1F0000u;
    return s;
}
// Enter the TEAM roster (§UI-17 S1: `short` to the screen, `double` to enter) and open the act sub-view of the
// member at published row `idx`. ⚠ The caller ASSERTS the landing; this returns a verdict rather than claiming one.
bool open_member_acts(UiModel& m, const UiSnapshot& s, uint8_t idx) {
    to_team(m, s);
    for (uint8_t i = 0; i < idx; ++i) m.on_gesture(Gesture::short_press, s);
    m.on_gesture(Gesture::double_press, s);
    return m.state().compose == Compose::dm && m.state().compose_peer == s.team[idx].id;
}
// Walk the act sub-view's cursor onto the GRANT row. ⛔ Never a hardcoded index: the row is OPTIONAL, which is
// exactly the positional coupling §B66 forbids reasoning about. BOUNDED, so an ABSENT row fails the caller's check.
bool compose_cursor_to_grant(UiModel& m, const UiSnapshot& s) {
    // ⓘ §UI-10/11 P3: the list is the SNAPSHOT's projection now, and the bound is its own length + the two derived
    //   rows. ⛔ Still never a hardcoded index — the row is OPTIONAL, which is what §B66 forbids reasoning about.
    const mrui::ComposeList& l = (m.state().compose == Compose::dm) ? s.preset_dm : s.preset_ch;
    for (int i = 0; i < int(mrui::compose_row_count(l, true)) + 1; ++i) {
        if (mrui::compose_row_kind(m.state().cursor, l, m.state().compose_grant_row) == mrui::ComposeRow::grant)
            return true;
        m.on_gesture(Gesture::short_press, s);
    }
    return false;
}
// Leave the provisioning sub-view, then the SETTINGS menu, then the screen — landing on STATUS, from which
// `to_team` runs. ⛔ Every step is by ROW IDENTITY, never by a press count.
void leave_settings_to_status(UiModel& m, const UiSnapshot& s) {
    if (m.state().provisioning == Provision::invite) leave_invite(m, s);   // the window has a list of its OWN
    if (m.state().settings == Settings::provisioning) {
        (void)prov_cursor_to(m, s, ProvRow::back);
        m.on_gesture(Gesture::double_press, s);
    }
    if (m.state().settings == Settings::browsing) {
        (void)cursor_to(m, s, CfgRow::back);
        m.on_gesture(Gesture::double_press, s);
    }
    m.on_gesture(Gesture::short_press, s);   // [[B232]]'s closed entry view passes the screen in ONE press
}
}  // namespace

TEST_CASE("ui16-k7-act: pin 1 — the act hangs on an entered-TEAM member row and opens the chain with the ROW'S hash") {
    const auto s = k7_snap(2);
    {   // ⛔ THE PASSIVE PREVIEW OFFERS NOTHING (§UI-17 S1, P-12): the roster must be ENTERED first, and the act is
        //    three deliberate presses past that — enter, open the member, walk to the row, confirm.
        CreateFix p;
        p.m.on_gesture(Gesture::short_press, s);
        CHECK(p.m.state().screen == Screen::team);
        CHECK(p.m.state().list_view == ListView::passive);
        CHECK(p.m.state().compose == Compose::none);
        CHECK(p.invite_dev.grants == 0);
        CHECK(p.invite_dev.reads == 0);
    }
    CreateFix f;
    CHECK(open_member_acts(f.m, s, 1));                            // the SECOND roster row, not the first
    CHECK(f.m.state().compose_peer == s.team[1].id);
    // ★★★ THE ACT IS OFFERED, AND ITS TARGET IS THE ROW'S OWN `key_hash32` — resolved through the TEAM chain's one
    //     lookup per row (`UiSnapshot::member`), ⛔ never the display name and ⛔ never the row index.
    CHECK(f.m.state().compose_grant_row  == true);
    CHECK(f.m.state().compose_grant_hash == s.member[1].key_hash32);
    CHECK(f.m.state().compose_grant_hash != s.member[0].key_hash32);
    // ...and the sub-view is the ENABLED preset list PLUS one row, with `back` still LAST.
    // ⓘ §UI-10/11 P3 / R-1 — RE-EXPRESSED, ⛔ NOT WEAKENED: the bound was `kDmTextCount` (a table's `sizeof`) and is
    //   now the projection's own `n`. With the compiled catalog that is the SAME 2, so this case describes exactly
    //   the list it always did — and it now also proves the row sits at `n` for a catalog of ANY size.
    CHECK(s.preset_dm.n == 2);
    CHECK(mrui::compose_row_count(s.preset_dm, true) == uint8_t(s.preset_dm.n + 2));
    CHECK(mrui::compose_row_kind(s.preset_dm.n, s.preset_dm, true) == mrui::ComposeRow::grant);
    CHECK(mrui::compose_row_kind(uint8_t(s.preset_dm.n + 1), s.preset_dm, true) == mrui::ComposeRow::back);
    // ⛔ THE WORD IS S-17, DECLARED ONCE IN THE INVITE UNIT AND REUSED — §K7 adds ⛔ no lexeme.
    CHECK(mrui::compose_row_text(s.preset_dm.n, s.preset_dm, true) == mrui::kInviteGrantKey);
    CHECK(strcmp(mrui::compose_row_text(s.preset_dm.n, s.preset_dm, true), "GRANT KEY") == 0);

    CHECK(compose_cursor_to_grant(f.m, s));
    CHECK(f.invite_dev.grants == 0);                               // walking onto it performs NOTHING
    f.m.on_gesture(Gesture::double_press, s);
    // ★★★★ AND IT LANDS IN THE **LANDED** N5/N6 CHAIN. The preflight ran, at the GRANT'S OWN BAR, on the row's hash.
    CHECK(f.m.state().provisioning == Provision::invite_confirm);
    CHECK(f.m.state().settings == Settings::provisioning);
    CHECK(f.m.state().screen == Screen::settings);
    CHECK(f.invite_dev.read_hash  == s.member[1].key_hash32);
    CHECK(f.invite_dev.read_floor == MESHROUTE_NS::Node::PeerKeyConf::authoritative);
    // ★★★ THE FROZEN SELECTION IS THE CHAIN'S OWN CARRIER (U2), and ⛔ NO SNAPSHOT WAS TAKEN: this door contributes
    //     nothing to F-11's two authorities, so it announces nobody as `NEW MEMBER` (rule 1, fail-CLOSED).
    CHECK(f.m.state().invite.sel_hash == s.member[1].key_hash32);
    CHECK(f.m.state().invite.taken == false);
    CHECK(f.m.state().invite.n == 0);
    CHECK(f.m.state().invite.handled_n == 0);
    CHECK(mrui::invite_sel_rows(f.m.state().invite, s.member, s.team_shown).n == 1);   // BACK alone
    // ★★ REJECT is still selected initially — the safe default the chain owns (N6 pin 1), inherited, not restated.
    CHECK(f.m.state().prov_confirm == ProvConfirm::invite_reject);
    CHECK(f.invite_dev.grants == 0);
    // ...and the sub-view it was opened from is gone, with BOTH frozen fields retired.
    CHECK(f.m.state().compose == Compose::none);
    CHECK(f.m.state().compose_grant_hash == 0);
    CHECK(f.m.state().compose_grant_row == false);
}

TEST_CASE("ui16-k7-b245: pin 2 — THE REPRO, END TO END: a member present BEFORE any window is grantable HERE") {
    CreateFix f; const auto s = k7_snap(2);                        // both members joined BEFORE anything was opened
    // ---- THE DEAD END, DRIVEN RATHER THAN QUOTED: the invitation window sees neither of them, and is RIGHT to.
    CHECK(open_invite(f, s));
    CHECK(f.m.state().invite.taken == true);
    CHECK(invite_cands(f.m, s) == 0);                              // N4 pin 2 — present at snapshot ⇒ never a candidate
    CHECK(strcmp(mrui::invite_note(mrui::invite_sel_rows(f.m.state().invite, s.member, s.team_shown)),
                 mrui::kInviteEmpty) == 0);
    leave_settings_to_status(f.m, s);
    CHECK(f.m.state().screen == Screen::status);
    CHECK(f.invite_dev.grants == 0);                               // ⛔ the whole window round trip granted NOTHING

    // ---- ...AND THE ROSTER REACHES THE VERY SAME MEMBER. This is [[B245]] closed, in one model, in one case.
    CHECK(open_member_acts(f.m, s, 0));
    CHECK(f.m.state().compose_grant_row == true);
    CHECK(compose_cursor_to_grant(f.m, s));
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().provisioning == Provision::invite_confirm);
    f.invite_dev.tx     = MESHROUTE_NS::Node::TeamKeyGrantTx::queued;
    f.invite_dev.tx_ctr = 909;
    f.invite_dev.tx_dst = 41;                                      // the id the CORE resolved at send time (N6b)
    f.m.on_gesture(Gesture::short_press, s);                       // REJECT -> GRANT KEY
    CHECK(f.invite_dev.grants == 0);
    f.m.on_gesture(Gesture::double_press, s);
    // ★★★★ ONE forward, on the row's hash, on the TEAM plane — the LANDED act, reached from the new door.
    CHECK(f.invite_dev.grants == 1);
    CHECK(f.invite_dev.grant_hash  == s.member[0].key_hash32);
    CHECK(f.invite_dev.grant_plane == mrui::kInviteGrantPlane);
    CHECK(f.m.state().provisioning == Provision::invite_result);
    CHECK(f.m.state().grant.st == mrui::InviteGrantState::queued);
    CHECK(strcmp(mrui::invite_grant_word(f.m.state().grant.st), "GRANT QUEUED") == 0);
    CHECK(strcmp(mrui::invite_grant_word(f.m.state().grant.st), "KEY SENT") != 0);   // F-9, from this entry too
    // ★★★ AND THE `{dst, ctr}` CORRELATION PROMOTES IT — the SAME rule, on the SAME carrier, reached the same way.
    MESHROUTE_NS::Push wrong{};
    wrong.kind = MESHROUTE_NS::PushKind::send_aired; wrong.ctr = 909; wrong.dst = 42;
    CHECK(f.m.on_invite_grant_push(wrong) == false);
    CHECK(f.m.state().grant.st == mrui::InviteGrantState::queued);
    MESHROUTE_NS::Push aired{};
    aired.kind = MESHROUTE_NS::PushKind::send_aired; aired.ctr = 909; aired.dst = 41;
    CHECK(f.m.on_invite_grant_push(aired) == true);
    CHECK(f.m.state().grant.st == mrui::InviteGrantState::sent);
    CHECK(strcmp(mrui::invite_grant_word(f.m.state().grant.st), "KEY SENT") == 0);
    // ⛔ NO DURABLE WRITE, NO TRANSACTION, NO LIVE APPLY: a grant is airtime, ⛔ never a provisioning act.
    CHECK(f.store.writes == 0);
    CHECK(f.prov.calls == 0);
    CHECK(f.live.applies == 0);
}

TEST_CASE("ui16-k7-window: pin 3 — the invitation window is UNDISTURBED in both directions") {
    // ★★★ THE STRONGEST PROOF IS A DIFF (`src/firmware_ui_invite.h` is untouched by this slice); what is driven
    //     here is the STATE the two doors share, because that is the one thing a diff cannot answer.
    CreateFix f; const auto s = k7_snap(2);
    // ---- (a) THE ROSTER GRANT LEAVES NOTHING BEHIND for a window opened afterwards ------------------------------
    CHECK(open_member_acts(f.m, s, 0));
    CHECK(compose_cursor_to_grant(f.m, s));
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().provisioning == Provision::invite_confirm);
    f.m.on_gesture(Gesture::double_press, s);                      // REJECT — the handled set's only writer
    CHECK(f.m.state().provisioning == Provision::invite);
    CHECK(f.m.state().invite.handled_n == 1);                      // ...it works from this door too (F-13)
    CHECK(f.m.state().invite.taken == false);                      // ⛔ but there is still NO snapshot
    // The window proper, opened afterwards, takes its OWN snapshot and starts with an EMPTY handled set.
    leave_invite(f.m, s);                                          // ...this list's BACK lands on the PROVISION menu
    CHECK(f.m.state().provisioning == Provision::menu);
    CHECK(prov_cursor_to(f.m, s, ProvRow::invite));
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().provisioning == Provision::invite);
    CHECK(f.m.state().invite.taken == true);
    CHECK(f.m.state().invite.n == 2);                              // BOTH authoritative hashes, as always
    CHECK(f.m.state().invite.handled_n == 0);                      // ★ the roster entry's rejection did NOT survive
    CHECK(invite_cands(f.m, s) == 0);                              // and the diff answers exactly as it did before
    // ---- (b) ...AND A LIVE WINDOW IS NOT REACHABLE FROM THE ROSTER AT ALL --------------------------------------
    // ⓘ The sub-view owns the press, so leaving SETTINGS is what it takes to reach TEAM — which is precisely why
    //   the two flows can never be open at once and cannot share `_invite_until_ms`.
    CHECK(f.m.state().screen == Screen::settings);
    leave_settings_to_status(f.m, s);
    CHECK(f.m.state().settings == Settings::closed);
    CHECK(f.m.state().provisioning == Provision::closed);
    CHECK(f.m.state().invite.taken == false);                      // the window's whole state died with the leave
}

TEST_CASE("ui16-k7-silent: pin 4 — nothing transmits without the operator's EXPLICIT confirmations (counted)") {
    // ★★★ THE N5 IDIOM, RE-PROVEN FROM THIS ENTRY: the assertion is a COMMAND COUNT, ⛔ never a screen. A panel
    //     saying `WAITING FOR PUBKEY` proves nothing about the air; the executor's call count does.
    CreateFix f; const auto s = k7_snap(1);
    f.invite_dev.present = false;                                  // no cached pubkey ⇒ the ceremony, not the grant
    CHECK(open_member_acts(f.m, s, 0));
    CHECK(f.invite_dev.commands == 0);
    CHECK(f.invite_dev.grants == 0);
    CHECK(compose_cursor_to_grant(f.m, s));
    CHECK(f.invite_dev.commands == 0);                             // ⛔ walking onto the act asks for nothing
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().provisioning == Provision::invite_need_pubkey);
    // ★★★★ THE HEADLINE: entering the row issues ⛔ NO WANT_PUBKEY. §no-auto-reqpubkey is PRESERVED through the new
    //      door — a slice that auto-issued here would be reversing an owner ratification, not adding a shortcut.
    CHECK(f.invite_dev.commands == 0);
    CHECK(f.m.state().prov_confirm == ProvConfirm::back);          // ...and BACK is what is selected
    f.m.on_gesture(Gesture::short_press, s);                       // BACK -> REQUEST PUBKEY
    CHECK(f.invite_dev.commands == 0);                             // the SHORT alone asks for nothing either
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.invite_dev.commands == 1);                             // ★ EXACTLY ONE, and only now
    CHECK(f.invite_dev.last.kind == MESHROUTE_NS::CmdKind::reqpubkey);
    CHECK(f.invite_dev.last.u.resolve.dst_hash == s.member[0].key_hash32);
    CHECK(f.invite_dev.last.u.resolve.plane == uint8_t(MESHROUTE_NS::Plane::TEAM));
    CHECK(f.m.state().provisioning == Provision::invite_wait_pubkey);
    CHECK(f.invite_dev.grants == 0);                               // ⛔ and no key has been offered, let alone shipped
}

TEST_CASE("ui16-k7-self: pin 5 — the SELF row offers nothing, and the core's own `self` arm is driven from here") {
    // ---- (a) THE ROW IS ABSENT, at the identity the CORE refuses on (`target_hash == _key_hash32`) --------------
    CreateFix f; auto s = k7_snap(1);
    s.member[0].key_hash32 = s.my_key_hash32;                      // this roster row IS us
    CHECK(open_member_acts(f.m, s, 0));
    CHECK(f.m.state().compose_grant_hash == s.my_key_hash32);      // the identity is still frozen, honestly...
    CHECK(f.m.state().compose_grant_row == false);                 // ...⛔ and the act is not offered
    CHECK(mrui::compose_row_count(s.preset_dm, false) == uint8_t(s.preset_dm.n + 1));   // the list is EXACTLY today's
    CHECK(compose_cursor_to_grant(f.m, s) == false);
    CHECK(f.invite_dev.reads == 0);                                // ⛔ not even the preflight is spent
    // ...and the last row is still `back, don't send`, which sends nothing.
    SendReq req{};
    for (int i = 0; i < 8 && mrui::compose_row_kind(f.m.state().cursor, s.preset_dm, false) != mrui::ComposeRow::back; ++i)
        f.m.on_gesture(Gesture::short_press, s);
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().compose == Compose::none);
    CHECK(f.m.take_send_request(req) == false);
    CHECK(f.invite_dev.grants == 0);
    // ---- (b) AND THE CORE'S OWN REFUSAL IS DRIVEN THROUGH THIS ENTRY, in its own word --------------------------
    CreateFix g; const auto t = k7_snap(1);
    g.invite_dev.tx = MESHROUTE_NS::Node::TeamKeyGrantTx::self;
    CHECK(open_member_acts(g.m, t, 0));
    CHECK(compose_cursor_to_grant(g.m, t));
    g.m.on_gesture(Gesture::double_press, t);
    g.m.on_gesture(Gesture::short_press, t);
    g.m.on_gesture(Gesture::double_press, t);
    CHECK(g.m.state().grant.st == mrui::InviteGrantState::self);
    CHECK(strcmp(mrui::invite_grant_word(g.m.state().grant.st), "SELF") == 0);
}

TEST_CASE("ui16-k7-keyless: pin 6 — a KEYLESS node offers nothing, and neither does a build that cannot grant") {
    // ★★★ THE DESIGN DECISION, DRIVEN: the act is HIDDEN, ⛔ never drawn-and-refusing. Each of the four vetoes is
    //     asserted ON ITS OWN, because a single collapsed predicate is one mutation away from covering for another.
    {   // (a) ⛔ NO CONTENT KEY — there is nothing to ship, so there is no act (spec §K7 pin 6)
        CreateFix f; auto s = k7_snap(1); s.team_key_present = false;
        CHECK(open_member_acts(f.m, s, 0));
        CHECK(f.m.state().compose_grant_row == false);
        CHECK(compose_cursor_to_grant(f.m, s) == false);
        CHECK(f.invite_dev.reads == 0);
    }
    {   // (b) ⛔ NO GRANT PLANE ON THIS BUILD / NOT IN A TEAM — the INVITE row's own predicate, REUSED (U1)
        CreateFix f; auto s = k7_snap(1); s.prov_invite = false;
        CHECK(open_member_acts(f.m, s, 0));
        CHECK(f.m.state().compose_grant_row == false);
    }
    {   // (c) ⛔ A ROUTE-ONLY MEMBER — no authoritative binding, so no seal target (F-7's floor, the invite list's)
        CreateFix f; auto s = k7_snap(1); s.member[0].key_hash32 = 0;
        CHECK(open_member_acts(f.m, s, 0));
        CHECK(f.m.state().compose_grant_hash == 0);
        CHECK(f.m.state().compose_grant_row == false);
    }
    {   // (d) ⛔ AND THE CHANNEL COMPOSE NEVER OFFERS IT: it has no member at all (`compose_peer == 0`)
        CreateFix f; const auto s = k7_snap(1);
        f.m.on_gesture(Gesture::short_press, s); f.m.on_gesture(Gesture::short_press, s);
        f.m.on_gesture(Gesture::short_press, s);                   // STATUS -> TEAM -> INBOX -> SEND
        CHECK(f.m.state().screen == Screen::send);
        f.m.on_gesture(Gesture::double_press, s);
        CHECK(f.m.state().compose == Compose::channel);
        CHECK(f.m.state().compose_peer == 0);
        CHECK(f.m.state().compose_grant_row == false);
        CHECK(mrui::compose_row_count(s.preset_ch, false) == uint8_t(s.preset_ch.n + 1));
        CHECK(mrui::compose_grant_offered(/*dm=*/false, true, true, 0x1234u, 0x9999u) == false);
    }
    // ⓘ ...and the pure predicate itself, term by term, so each veto is attributable at match count 1.
    CHECK(mrui::compose_grant_offered(true,  true,  true,  0x1234u, 0x9999u) == true);
    CHECK(mrui::compose_grant_offered(false, true,  true,  0x1234u, 0x9999u) == false);
    CHECK(mrui::compose_grant_offered(true,  false, true,  0x1234u, 0x9999u) == false);
    CHECK(mrui::compose_grant_offered(true,  true,  false, 0x1234u, 0x9999u) == false);
    CHECK(mrui::compose_grant_offered(true,  true,  true,  0u,      0x9999u) == false);
    CHECK(mrui::compose_grant_offered(true,  true,  true,  0x9999u, 0x9999u) == false);
}

TEST_CASE("ui16-k7-identity: pin 7 — P-7c/P-7d through THIS entry: the FULL hash, and a mutable field never moves it") {
    CreateFix f; auto s = k7_snap(1);
    // A member WITH a cached name — rule 3's state, and the one in which P-7c is worth anything.
    const char* nm = "Wolfgangetta";
    for (uint8_t b = 0; nm[b] && b + 1 < mrui::kInviteNameCap; ++b) s.member[0].name[b] = nm[b];
    const uint32_t target = s.member[0].key_hash32;
    CHECK(open_member_acts(f.m, s, 0));
    CHECK(f.m.state().compose_grant_hash == target);               // FROZEN at entry, from the row's own resolution
    // ★★★★ P-7d — THE NAME AND THE TEAM-LOCAL ID BOTH MOVE **BEFORE THE ACT IS EVEN PRESSED**, and the target does
    //      not. ⓘ The two mutable fields are moved HERE, between the sub-view opening and the act, deliberately: a
    //      target re-resolved at press time from either of them would land on nobody (or on somebody else), and
    //      that is precisely the control this case exists to redden.
    UiSnapshot moved = s;
    for (uint8_t b = 0; b < mrui::kInviteNameCap; ++b) moved.member[0].name[b] = 0;
    const char* nm2 = "Someone else";
    for (uint8_t b = 0; nm2[b] && b + 1 < mrui::kInviteNameCap; ++b) moved.member[0].name[b] = nm2[b];
    moved.member[0].id = uint8_t(s.member[0].id + 100);            // a team-DAD re-run
    moved.team[0].id   = moved.member[0].id;
    CHECK(mrui::team_member_hash_of(moved.member, moved.team_shown, s.member[0].id) == 0);   // the id names nobody now
    CHECK(compose_cursor_to_grant(f.m, moved));
    f.m.on_gesture(Gesture::double_press, moved);
    CHECK(f.m.state().provisioning == Provision::invite_confirm);  // ...the act still ran, on the FROZEN identity
    CHECK(f.m.state().invite.sel_hash == target);
    {   // ★★★ P-7c — THE CONFIRMATION CARRIES THE FULL `0x%08lX`, ⛔ EVEN THOUGH A NAME IS SHOWN.
        const mrui::InviteIdRows r = mrui::invite_id_rows(moved.member, moved.team_shown,
                                                          f.m.state().invite.sel_hash);
        char want[mrui::kMemberHashCap]; mrui::ui_fmt_member_hash_full(want, sizeof want, target);
        CHECK(strcmp(r.hash, want) == 0);
        CHECK(strlen(r.hash) == 10u);
        CHECK(strcmp(r.name, nm2) == 0);                           // the name is an ADDED row, and it is the LIVE one
        CHECK(strcmp(r.name, nm) != 0);
    }
    f.invite_dev.tx_dst = moved.member[0].id;                      // the core resolves the NEW id at send time
    f.m.on_gesture(Gesture::short_press, moved);                   // REJECT -> GRANT KEY
    f.m.on_gesture(Gesture::double_press, moved);
    CHECK(f.invite_dev.grants == 1);
    CHECK(f.invite_dev.grant_hash == target);                      // ★ THE SAME KEY, whatever the labels now say
    CHECK(f.m.state().grant.hash == target);
    CHECK(f.m.state().grant.dst == moved.member[0].id);            // ...and the correlation is the CORE'S answer
}

TEST_CASE("ui16-k7-words: pin 8 — the outcome words are N6's EXACTLY, over the WHOLE enum, through this entry") {
    // ★★★ THE ANCHOR IS THE **REUSED CALL**: every expectation below is `invite_grant_word(invite_grant_state_of(tx))`
    //     — the pure unit's own mapping — so a SECOND mapping forked behind this door cannot agree with it by
    //     accident. ⛔ No literal is typed here for a state's word; the literals are `test_firmware_ui_invite.cpp`'s.
    using TX = MESHROUTE_NS::Node::TeamKeyGrantTx;
    const TX arms[] = { TX::queued, TX::parked, TX::queue_full, TX::send_failed, TX::no_team, TX::no_key,
                        TX::no_identity, TX::no_pubkey, TX::self, TX::delegated, TX::too_large };
    for (TX tx : arms) {
        CreateFix f; const auto s = k7_snap(1);
        f.invite_dev.tx = tx;
        CHECK(open_member_acts(f.m, s, 0));
        CHECK(compose_cursor_to_grant(f.m, s));
        f.m.on_gesture(Gesture::double_press, s);
        f.m.on_gesture(Gesture::short_press, s);
        f.m.on_gesture(Gesture::double_press, s);
        CHECK(f.m.state().provisioning == Provision::invite_result);
        CHECK(f.m.state().grant.st == mrui::invite_grant_state_of(tx));
        CHECK(strcmp(mrui::invite_grant_word(f.m.state().grant.st),
                     mrui::invite_grant_word(mrui::invite_grant_state_of(tx))) == 0);
        // ⛔ AND NO COMPLETION WORD, EVER: there is no e2e ack on a grant, so this node cannot know it arrived.
        for (const char* forbidden : { "JOIN COMPLETE", "KEY RECEIVED", "DELIVERED", "WAITING FOR KEY", "KEYLESS" })
            CHECK(strstr(mrui::invite_grant_word(f.m.state().grant.st), forbidden) == nullptr);
    }
}

TEST_CASE("ui16-k7-resources: the act's TWO frozen fields cost ONE quantum, and the published identity costs ZERO") {
    // ★★ MEASURED, ⛔ NOT REASONED (the padding-placement rule, a fifteenth time), and every placement is
    //    `offsetof`-proved rather than asserted in prose.
    // ⚠ NATIVE ALIGNMENT HIDES THE BOARD FIGURE (D2's standing warning) — this pins the SHAPE, not the flash cost.
    // ---- the SNAPSHOT's one new field is FREE **and moves nothing** ---------------------------------------------
    // ⓘ ⚠ **RE-PINNED 2026-08-26 BY §UI-10/11 P3, AND THE SUPERSEDED FIGURE IS KEPT VISIBLE: `1008u`.** The struct
    //   grew by the compose-list projection — `uint32_t preset_generation` at the old 8-aligned END (1008, free) plus
    //   two alignof-1 `ComposeList`s (161 each) at 1012 and 1173 — so it measures **1336 (+328)**. ⛔ NOTHING BELOW
    //   MOVED: every offset this case pins is ahead of `member[]` and is byte-identical.
    CHECK(sizeof(mrui::UiSnapshot) == 1336u);                      // ⛔ UNCHANGED BY K7 ITSELF
    CHECK(offsetof(mrui::UiSnapshot, my_key_hash32) == 700u);      // ★ the pad before the 8-aligned age below
    CHECK(offsetof(mrui::UiSnapshot, home_confirm_age_ms) == 704u);// ⛔ UNMOVED
    CHECK(offsetof(mrui::UiSnapshot, prov_invite) == 689u);        // ⛔ UNMOVED
    CHECK(offsetof(mrui::UiSnapshot, prov_saved_keys) == 690u);    // ⛔ UNMOVED
    CHECK(offsetof(mrui::UiSnapshot, team_key_present) == 694u);   // ⛔ UNMOVED
    // ---- ...and the STATE's two frozen fields cost exactly ONE 8-byte quantum, together, at the head -------------
    CHECK(offsetof(mrui::UiState, compose_peer) == 3u);            // ⛔ UNMOVED
    CHECK(offsetof(mrui::UiState, compose_grant_hash) == 4u);      // ★ 4-aligned, immediately after the pick
    // ⓘ ⚠ **RE-PINNED 2026-08-26 BY §UI-10/11 P3, SUPERSEDED FIGURES KEPT VISIBLE: `compose_grant_row == 8u`,
    //   `compose_result == 9u`.** P3 declared `uint32_t compose_gen` immediately after `compose_grant_hash`, which
    //   is the 4-aligned slot at 8 — so the two flags shift to 12/13. ★ K7's CLAIM IS UNTOUCHED and is re-proved
    //   below: the hash is still 4-aligned right after the pick, the flag still costs NOTHING on top of the field
    //   in front of it, and `sizeof(UiState)` / `sizeof(UiModel)` are **UNCHANGED at 504 / 928 ON THE HOST** — i.e.
    //   P3's own 4-byte field landed in padding that already existed here.
    //   ⚠ ⛔ CORRECTED 2026-08-26 (QG): the withdrawn clause read *"and cost ZERO"* full stop, which is a HOST
    //   statement wearing a general one's clothes. MEASURED on the board ABI it costs **+8** to `UiState` and `+8`
    //   to `UiModel` — see `ui10-p3-resources`, which carries the figures and the toolchain.
    CHECK(offsetof(mrui::UiState, compose_gen) == 8u);             // ★ §UI-10/11 P3 — 4-aligned, and FREE
    CHECK(offsetof(mrui::UiState, compose_grant_row) == 12u);      // ★ ...and the flag costs NOTHING on top
    CHECK(offsetof(mrui::UiState, compose_result) == 13u);         // pushed by the 4-alignment above
    CHECK(sizeof(mrui::UiState) == 504u);                          // 496 + 8, and ⛔ UNMOVED by P3's uint32
    CHECK(sizeof(mrui::UiModel) == 928u);                          // 920 + the same 8, likewise UNMOVED
    // ---- and K7 adds NO carrier to the chain it enters ------------------------------------------------------------
    CHECK(sizeof(mrui::InviteWindow) == 104u);                     // ⛔ UNCHANGED
    CHECK(sizeof(mrui::InviteGrantResult) == 8u);                  // ⛔ UNCHANGED
}

// ============================================ §UI-16 K4 — THE GRANT RECEIPT'S NOTE, IN THE MODEL'S OWN TERMS
// ⓘ The ROUTER half (which push reaches this, and that no other kind does) is `test/test_firmware_ui_send.cpp`'s;
//   the GATE that only a `saved` persist forwards is `test/test_firmware_team_keyring.cpp`'s. What is measured
//   here is the MODEL's contract: the two ruled sentences, the slot they occupy, and that they are TRANSIENT.
TEST_CASE("ui16-K4: the note occupies the panel's ONE transient team answer, and every entry retires it") {
    CreateFix f; const auto s = prov_snap();
    f.prov.answer = created_answer(0x12A1B2C3u);
    CHECK(open_create_confirm(f, s));
    f.m.on_gesture(Gesture::short_press, s);
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().provisioning == Provision::create_result);
    CHECK(strcmp(prov_result_head(f.m.state().prov_answer), "TEAM CREATED") == 0);

    const uint8_t  cur_before  = f.m.state().cursor;
    const Screen   scr_before  = f.m.state().screen;
    // ★★ THE NOTE REPLACES THE PREVIOUS VERDICT, and that is the ruling rather than an accident: `TEAM CREATED`
    //    followed by `TEAM KEY RECEIVED` is the true sequence of two facts about ONE team, newest last. ⛔ What it
    //    must NOT do is move the operator: same screen, same arm, same cursor.
    f.m.on_team_key_note(/*saved=*/true, /*keyring_full=*/false, 9000);
    CHECK(f.m.state().prov_answer.outcome == UiProvOutcome::team_key_received);
    CHECK(strcmp(prov_result_head(f.m.state().prov_answer), "TEAM KEY RECEIVED") == 0);
    // ★★ THE SLOT IS CLEARED, ⛔ NOT MERELY RE-LABELLED: the previous act's id would otherwise render UNDER this
    //    note's headline — one screen showing another operation's data, which is the "success that isn't" shape.
    CHECK(f.m.state().prov_answer.team_id == 0u);
    CHECK(f.m.state().prov_answer.node_id == 0u);
    CHECK(strcmp(f.m.state().prov_answer.reason, "") == 0);
    CHECK(f.m.state().provisioning == Provision::create_result);
    CHECK(f.m.state().screen == scr_before);
    CHECK(f.m.state().cursor == cur_before);
    CHECK(f.prov.calls == 1);                       // ⛔ and it ran no transaction — a push is not an act

    // ⛔ CORRECTED IN PLACE 2026-08-25 (§UI-17 keyrecv — OWNER-RULED, shape (a)), AND THE WITHDRAWN EXPECTATION IS
    //    KEPT VISIBLE, ⛔ never deleted. It read, under the heading *"TRANSIENT: the press that leaves the result
    //    retires it through `enter_provision`, exactly as it retires a create/join verdict — so a note can never sit
    //    under a screen that did not establish it"*:
    //        f.m.on_gesture(Gesture::double_press, s);
    //        CHECK(f.m.state().provisioning == Provision::menu);
    //        CHECK(f.m.state().prov_answer.outcome == UiProvOutcome::none);
    //        CHECK(strcmp(prov_result_head(f.m.state().prov_answer), "") == 0);
    // ★ WHAT MOVED, and ONLY this: the SUCCESS note's acknowledgement now LEAVES the provisioning flow for STATUS, so
    //   it does not run `enter_provision` at all and the slot is therefore not zeroed BY THAT PRESS.
    // ⛔ WHAT DID **NOT** MOVE is the property the withdrawn lines were really about: a note can never be RENDERED
    //   under a screen that did not establish it. The two result arms are `prov_answer`'s only renderers and both are
    //   gone here — and the retirement itself is re-proven below, on the next ENTRY, which is where it has always
    //   belonged.
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().screen == Screen::status);
    CHECK(f.m.state().settings == Settings::closed);
    CHECK(f.m.state().provisioning == Provision::closed);
    CHECK(f.m.state().list_view == ListView::passive);
    CHECK(f.m.state().cursor == 0);
    // ★ THE RETIREMENT, RE-PROVEN THROUGH THE NEW LANDING: the next ENTRY into PROVISION zeroes the slot, so the
    //   operator can never walk back into a result screen still carrying the acknowledged note.
    CHECK(open_provision(f.m, s));
    CHECK(f.m.state().prov_answer.outcome == UiProvOutcome::none);
    CHECK(strcmp(prov_result_head(f.m.state().prov_answer), "") == 0);
}

// ================== §UI-17 keyrecv (OWNER-RULED 2026-08-25, shape (a)) — WHERE THE `TEAM KEY RECEIVED` ACK LANDS
// ⓘ ONE landing moved. These cases measure the moved one, the two that were explicitly ruled NOT to move (the
//   failure pair), the neighbours that were never in scope, and — through the new path — the landed §4-K4 control
//   that an ARRIVAL navigates nothing.
namespace {
// Put a result screen up with the grant receipt's note on it, on the arm the caller names. Returns the arm reached,
// so the caller ASSERTS the precondition rather than trusting this helper (the `open_create_confirm` idiom).
// ⛔ THE NOTE IS ALWAYS WRITTEN BY THE REAL ENTRY POINT `on_team_key_note`, ⛔ never by assigning `prov_answer`: the
//    two-doors convergence is the whole reason the failure arm's negatives are the success arm's.
bool put_note_on_create_result(CreateFix& f, const UiSnapshot& s, bool saved) {
    f.prov.answer = created_answer(0x12A1B2C3u);
    if (!open_create_confirm(f, s)) return false;
    f.m.on_gesture(Gesture::short_press, s);          // BACK -> CREATE
    f.m.on_gesture(Gesture::double_press, s);
    if (f.m.state().provisioning != Provision::create_result) return false;
    f.m.on_team_key_note(saved, /*keyring_full=*/false, s.now_ms);
    return true;
}
bool put_note_on_join_result(CreateFix& f, const UiSnapshot& s, bool saved) {
    f.prov.list = ok_join_list(0b0001);
    if (!start_join(f, s)) return false;
    f.m.on_join_push(adopt_push(4, 42), 4, 42);
    if (f.m.state().provisioning != Provision::join_result) return false;
    f.m.on_team_key_note(saved, /*keyring_full=*/false, s.now_ms);
    return true;
}
// The landing the ruling names, in the model's own fields. STATUS has no interactive form, so "passive" is exactly
// this: the top-level screen, both sub-views retired, the cursor re-anchored.
void check_passive_status(const UiModel& m) {
    CHECK(m.state().screen      == Screen::status);
    CHECK(m.state().settings    == Settings::closed);
    CHECK(m.state().provisioning == Provision::closed);
    CHECK(m.state().list_view   == ListView::passive);
    CHECK(m.state().cursor      == 0);
    CHECK(m.state().compose     == Compose::none);
    CHECK(m.state().detail      == InboxModal::closed);
}
}  // namespace

TEST_CASE("ui17-keyrecv: EITHER press on TEAM KEY RECEIVED lands on the PASSIVE STATUS screen, from EITHER result") {
    // ★★★★ PIN 1, and it is driven over the FULL cross-product rather than a sample: the two presses a terminal
    //      accepts (`short` and `double` — the panel says `press = back` and means both) × the two result arms the
    //      note can be rendered on (`create_result` for a create/nearby flow, `join_result` for a static join).
    //      ⛔ The landing is the NOTE's, so all four must reach the same place.
    for (Gesture g : { Gesture::short_press, Gesture::double_press }) {
        for (int arm = 0; arm < 2; ++arm) {
            CreateFix f; const auto s = prov_snap();
            const bool ok = arm == 0 ? put_note_on_create_result(f, s, /*saved=*/true)
                                     : put_note_on_join_result(f, s, /*saved=*/true);
            CHECK(ok);
            CHECK(strcmp(prov_result_head(f.m.state().prov_answer), "TEAM KEY RECEIVED") == 0);
            // ★★ PIN 2, RE-PROVEN THROUGH THE NEW PATH: the ARRIVAL moved nothing. It is asserted HERE, one statement
            //    before the press, so the landing below cannot be credited to the push.
            CHECK(f.m.state().screen == Screen::settings);
            CHECK(f.m.state().settings == Settings::provisioning);
            CHECK(f.m.state().provisioning == (arm == 0 ? Provision::create_result : Provision::join_result));
            const int calls_before = f.prov.calls, writes_before = f.store.writes;
            f.m.on_gesture(g, s);
            check_passive_status(f.m);
            // ⛔ AND ACKNOWLEDGING RE-RUNS NOTHING — the terminal's own standing rule, which a new landing may not
            //    quietly drop: no transaction, no durable write.
            CHECK(f.prov.calls  == calls_before);
            CHECK(f.store.writes == writes_before);
        }
    }
}

TEST_CASE("ui17-keyrecv: the note's ARRIVAL still navigates nothing — on the new landing's own screen too") {
    // ★★★★ PIN 2's other half (spec §4-K4 pin 3, `a push never navigates`). The landed K4 case proves it on the
    //      result screen; this proves the ruling did not smuggle a navigation in through the STATUS door — a receipt
    //      that arrives while the operator is on STATUS, or has just acknowledged one, moves nothing at all.
    CreateFix f; const auto s = prov_snap();
    CHECK(put_note_on_create_result(f, s, /*saved=*/true));
    f.m.on_gesture(Gesture::double_press, s);
    check_passive_status(f.m);
    // A SECOND receipt, now that the panel is on STATUS: it writes the transient slot and asks for a repaint, and
    // ⛔ that is all. It may ⛔ not open the result screen it would be rendered on.
    f.m.clear_dirty();
    const uint8_t cur_before = f.m.state().cursor;
    f.m.on_team_key_note(/*saved=*/true, /*keyring_full=*/false, s.now_ms + 1000);
    CHECK(f.m.state().dirty == true);                    // the repaint IS owed
    CHECK(f.m.state().cursor == cur_before);
    check_passive_status(f.m);                           // ⛔ ...and NOTHING else moved
    // ⛔ AND THE FAILURE DOOR IS THE SAME DOOR (U1): it may not navigate from STATUS either.
    f.m.on_team_key_note(/*saved=*/false, /*keyring_full=*/false, s.now_ms + 2000);
    check_passive_status(f.m);
}

TEST_CASE("ui17-keyrecv: the FAILURE pair's acknowledgement stays where the REMEDIES are — the LANDED landing") {
    // ★★★★ PIN 3, and it is the scope boundary made measurable: `TEAM KEY ACTIVE` / `NOT SAVED` / `LOST ON REBOOT`
    //      says the key is live but will not survive a reboot, and the operator's next act is a remedy inside the
    //      provisioning flow. ⛔ Walking him out to STATUS is exactly what the ruling declined to do.
    for (Gesture g : { Gesture::short_press, Gesture::double_press }) {
        for (int arm = 0; arm < 2; ++arm) {
            CreateFix f; const auto s = prov_snap();
            const bool ok = arm == 0 ? put_note_on_create_result(f, s, /*saved=*/false)
                                     : put_note_on_join_result(f, s, /*saved=*/false);
            CHECK(ok);
            CHECK(f.m.state().prov_answer.outcome == UiProvOutcome::team_key_unsaved);
            CHECK(strcmp(prov_result_head(f.m.state().prov_answer), "TEAM KEY ACTIVE") == 0);
            f.m.on_gesture(g, s);
            // ⛔ THE LANDED LANDING, UNCHANGED — still inside the flow, on its menu, with the slot retired by the
            //    entry exactly as it always was.
            CHECK(f.m.state().screen == Screen::settings);
            CHECK(f.m.state().settings == Settings::provisioning);
            CHECK(f.m.state().provisioning == Provision::menu);
            CHECK(f.m.state().prov_answer.outcome == UiProvOutcome::none);
        }
    }
}

TEST_CASE("ui17-keyrecv: every NEIGHBOURING terminal's acknowledgement is untouched (driven, ⛔ not assumed)") {
    // ★★★★ PIN 4. The rule is keyed on the ANSWER, so the only defence against it capturing a neighbour is to DRIVE
    //      the neighbours. Each of these is a terminal that renders on one of the two result arms.
    const auto s = prov_snap();
    {   // TEAM CREATED -> the menu
        CreateFix f;
        f.prov.answer = created_answer(0x12A1B2C3u);
        CHECK(open_create_confirm(f, s));
        f.m.on_gesture(Gesture::short_press, s);
        f.m.on_gesture(Gesture::double_press, s);
        CHECK(strcmp(prov_result_head(f.m.state().prov_answer), "TEAM CREATED") == 0);
        f.m.on_gesture(Gesture::double_press, s);
        CHECK(f.m.state().provisioning == Provision::menu);
        CHECK(f.m.state().screen == Screen::settings);
    }
    {   // ADOPTED (the static join's asynchronous verdict, on `join_result`) -> the menu
        CreateFix f;
        f.prov.list = ok_join_list(0b0001);
        CHECK(start_join(f, s));
        f.m.on_join_push(adopt_push(4, 42), 4, 42);
        CHECK(strcmp(prov_result_head(f.m.state().prov_answer), "ADOPTED") == 0);
        f.m.on_gesture(Gesture::double_press, s);
        CHECK(f.m.state().provisioning == Provision::menu);
        CHECK(f.m.state().screen == Screen::settings);
    }
    {   // A REFUSAL -> the menu (the remedy is in the flow, exactly as the failed save's is)
        CreateFix f;
        UiProvAnswer refused{}; refused.outcome = UiProvOutcome::refused; refused.reason = "nv_load_failed";
        f.prov.answer = refused;
        CHECK(open_create_confirm(f, s));
        f.m.on_gesture(Gesture::short_press, s);
        f.m.on_gesture(Gesture::double_press, s);
        CHECK(f.m.state().prov_answer.outcome == UiProvOutcome::refused);
        f.m.on_gesture(Gesture::double_press, s);
        CHECK(f.m.state().provisioning == Provision::menu);
        CHECK(f.m.state().screen == Screen::settings);
    }
    {   // ★ K5's OWN TWO LANDINGS, which share this arm and are the closest neighbours of all: `TEAM JOINED` WITHOUT
        //   a retained record lands on the menu, and WITH one it opens the SAVED KEY offer. ⛔ Neither becomes STATUS,
        //   and both are driven through the REAL nearby-join flow (`join_to_result`), ⛔ not a forged answer.
        for (int retained = 0; retained < 2; ++retained) {
            CreateFix f; const auto ns = nearby_snap(3);
            CHECK(join_to_result(f, ns, 0xBEEF0001u,
                                 retained ? joined_with_saved_key(0xBEEF0001u) : joined_answer(0xBEEF0001u)));
            CHECK(strcmp(prov_result_head(f.m.state().prov_answer), "TEAM JOINED") == 0);
            f.m.on_gesture(Gesture::double_press, ns);
            CHECK(f.m.state().screen == Screen::settings);
            CHECK(f.m.state().provisioning == (retained ? Provision::saved_key : Provision::menu));
        }
    }
}

TEST_CASE("ui17-keyrecv: the new landing keeps the LANDED blank/wake rules — nothing about STATUS is special-cased") {
    // ★★★★ PIN 6. The ack stamps `_last_input_ms` at the top of `on_gesture` like any press, so the panel blanks
    //      `kBlankMs` later and the waking press is CONSUMED, putting the SAME screen back (§UI-17 S2's ruling).
    //      ⛔ The landing neither holds the panel lit nor wakes it, and it is measured rather than reasoned.
    CreateFix f; const auto s = prov_snap();
    CHECK(put_note_on_create_result(f, s, /*saved=*/true));
    f.m.on_gesture(Gesture::double_press, s);
    check_passive_status(f.m);
    CHECK(f.m.state().blanked == false);
    // ...one millisecond short of the deadline it is still lit.
    f.m.on_tick(prov_snap(true, true, true, s.now_ms + kBlankMs - 1));
    CHECK(f.m.state().blanked == false);
    f.m.on_tick(prov_snap(true, true, true, s.now_ms + kBlankMs + 1));
    CHECK(f.m.state().blanked == true);
    // ★ THE WAKING PRESS IS CONSUMED AND THE SAME SCREEN COMES BACK — ⛔ it does not advance the cycle.
    f.m.on_gesture(Gesture::short_press, prov_snap(true, true, true, s.now_ms + kBlankMs + 10));
    CHECK(f.m.state().blanked == false);
    check_passive_status(f.m);
}

TEST_CASE("ui16-K4: the three result rows are TOTAL over the whole UiProvOutcome enum, and every row fits") {
    // ★ THE `-Werror=switch` DISCIPLINE'S OTHER HALF, applied to the two enumerators K4 adds: drive the FULL enum,
    //   ⛔ not a sample, and require every answer to be a real string that fits the 19-column body.
    for (uint8_t i = 0; i <= uint8_t(UiProvOutcome::team_key_unsaved); ++i) {
        UiProvAnswer a{}; a.outcome = UiProvOutcome(i);
        a.reason = "";                              // `refused`/`join_refused` carry the SERVICE's token, "" here
        CHECK(prov_result_head(a)    != nullptr);
        CHECK(prov_result_detail(a)  != nullptr);
        CHECK(prov_result_detail2(a) != nullptr);
        CHECK(strlen(prov_result_head(a))    <= 19u);
        CHECK(strlen(prov_result_detail(a))  <= 19u);
        CHECK(strlen(prov_result_detail2(a)) <= 19u);
    }
    // ⛔ THE THIRD ROW IS NOT A GENERAL SLOT: it exists for exactly ONE ruled sentence (S-27's second half) and
    //    every other outcome answers `""`. A future outcome that grows one must say so here, deliberately.
    int with_detail2 = 0;
    for (uint8_t i = 0; i <= uint8_t(UiProvOutcome::team_key_unsaved); ++i) {
        UiProvAnswer a{}; a.outcome = UiProvOutcome(i); a.reason = "";
        if (prov_result_detail2(a)[0]) { ++with_detail2; CHECK(UiProvOutcome(i) == UiProvOutcome::team_key_unsaved); }
    }
    CHECK(with_detail2 == 1);
    // ⛔ THE THREE FORBIDDEN LEXEMES STAY ABSENT FROM EVERY ARM (spec §8 S-32/S-33/S-34): absence is a test.
    for (uint8_t i = 0; i <= uint8_t(UiProvOutcome::team_key_unsaved); ++i) {
        UiProvAnswer a{}; a.outcome = UiProvOutcome(i); a.reason = "";
        for (const char* forbidden : { "JOIN COMPLETE", "KEYLESS", "WAITING FOR KEY" }) {
            CHECK(strstr(prov_result_head(a),    forbidden) == nullptr);
            CHECK(strstr(prov_result_detail(a),  forbidden) == nullptr);
            CHECK(strstr(prov_result_detail2(a), forbidden) == nullptr);
        }
    }
}

TEST_CASE("ui16-k5-resources: the offer's TWO fields cost ZERO bytes — both land in EXISTING padding") {
    // ★★ MEASURED, ⛔ NOT REASONED (the `nearby`/`invite` placement rule, a third time), and the placement is proved
    //    by `offsetof` rather than asserted in prose: this slice adds `UiProvAnswer::saved_key` (1 B) and
    //    `UiState::saved_key_team` (4 B), and BOTH land in padding the structs already carried.
    // ⚠ NATIVE ALIGNMENT HIDES THE BOARD FIGURE (D2's standing warning) — this pins the SHAPE, not the flash cost.
    CHECK(sizeof(mrui::UiProvAnswer) == 16u);                 // ⛔ UNCHANGED by the new flag
    CHECK(offsetof(mrui::UiProvAnswer, outcome) == 0u);
    CHECK(offsetof(mrui::UiProvAnswer, node_id) == 1u);
    CHECK(offsetof(mrui::UiProvAnswer, saved_key) == 2u);     // ★ the hole between `node_id` and the 4-aligned id
    CHECK(offsetof(mrui::UiProvAnswer, team_id) == 4u);       // ⛔ UNMOVED — no landed field shifted
    // ★ K5's own field was FREE: it landed in the tail quantum the grant verdict had already opened, and the two
    //   offsets below are what prove it was ADDED beside the other frozen selections rather than folded into one.
    // ⚠ RE-MEASURED 2026-08-25 (§UI-16 K6): the STRUCT SIZES moved (456 -> 496 / 880 -> 920) because K6 appended
    //   two carriers of its own; ⛔ K5's field did NOT move (`saved_key_team` is still 340), and neither did
    //   `nearby_sel_id`. ⓘ `invite` shifted 344 -> 384 because K6's carriers were APPENDED before it — the K6 case
    //   writes out that arithmetic; this one keeps K5's own placement claim, which is unaffected.
    // ⚠ RE-MEASURED 2026-08-25 (§UI-16 K7): the STRUCT SIZES moved again (496 -> 504 / 920 -> 928) and both
    //   offsets below shifted by exactly **+8**, because K7's two frozen compose fields are inserted at the HEAD of
    //   `UiState` (beside `compose_peer`, where the act's target belongs) rather than appended. ⛔ K5's CLAIM is
    //   unaffected and is what this case is about: its field still sits in the 4 bytes immediately after
    //   `nearby_sel_id`, with ⛔ not one padding byte between them.
    CHECK(sizeof(mrui::UiState) == 504u);
    CHECK(sizeof(mrui::UiModel) == 928u);
    // ⓘ ⚠ **RE-PINNED 2026-08-26 BY §UI-10/11 P3, AND THE SUPERSEDED FIGURE IS KEPT VISIBLE: `1008u`.** The struct
    //   grew by the compose-list projection — `uint32_t preset_generation` at the old 8-aligned END (1008, free) plus
    //   two alignof-1 `ComposeList`s (161 each) at 1012 and 1173 — so it measures **1336 (+328)**. ⛔ NOTHING BELOW
    //   MOVED: every offset this case pins is ahead of `member[]` and is byte-identical.
    CHECK(sizeof(mrui::UiSnapshot) == 1336u);                 // ⛔ UNCHANGED BY K5 ITSELF
    CHECK(offsetof(mrui::UiState, nearby_sel_id) == 344u);    // 336 + 8 (K7's head insert)
    CHECK(offsetof(mrui::UiState, saved_key_team) == 348u);   // ★ K5's field, still in the 4 bytes after it
}

// ============================================== §UI-16 K6 — SAVED-KEY RETENTION MANAGEMENT, THE MODEL'S HALF
// ★★★★ WHAT **THIS** BLOCK MEASURES IS THE **FLOW AND THE LANDING**: which child row opens the list, when the
//      keyring is READ, which landing a row takes, what a BACK costs (nothing), how many times the removal runs and
//      what the screen says afterwards. ⛔ IT DOES NOT MEASURE THE SERVICE: the PROTECTION of the active record, the
//      compaction, the wipe and the at-most-one-save rule are `test/test_firmware_team_keyring.cpp`'s, driven
//      against counting fakes; the ADAPTER's mapping is `test/test_firmware_ui_prov.cpp`'s.
// ⛔ AND IT IS **RETENTION MANAGEMENT**, ⛔ NEVER "KEY ROTATION" — the ruling's own first sentence.
// ⛔ THE RENDERER IS MEASURED IN NEITHER: `src/firmware_ui.cpp` is compiled by no automated gate (§B115); its cover
//    is `tools/probe_firmware_ui`'s SAVED KEYS phase.
namespace {

// A snapshot with the FIFTH child available. ⓘ It is the only predicate this screen needs — ⛔ there is no runtime
// term, deliberately (a node that has LEFT every team may still hold four retained records to free).
UiSnapshot keys_snap(uint32_t now_ms = 1000) {
    return prov_snap(true, true, true, now_ms, /*invite=*/false, /*saved_keys=*/true);
}
// A metadata-only list, as the SERVICE would answer it. ⛔ It carries `{team_id, active}` and nothing else — there
// is no key field to fill, which is the carrier's own contract.
mrfw::SavedKeyList keys_list(uint8_t n, int active_row = -1) {
    mrfw::SavedKeyList l{};
    l.served = true; l.binding_read = true; l.st = mrnv::TeamKeyRead::ok;
    for (uint8_t i = 0; i < n && i < mrnv::kTeamKeyRecs; ++i) {
        l.rec[l.n].team_id = 0xAB000001u + i;
        l.rec[l.n].active  = (int(i) == active_row);
        ++l.n;
    }
    return l;
}
// Open PROVISION and land on the SAVED KEYS list. The caller ASSERTS the landing.
bool open_saved_keys(CreateFix& f, const UiSnapshot& s) {
    if (!open_provision(f.m, s)) return false;
    if (!prov_cursor_to(f.m, s, ProvRow::saved_keys)) return false;
    f.m.on_gesture(Gesture::double_press, s);
    return f.m.state().provisioning == Provision::saved_keys;
}
// Put the list cursor on the row naming `team_id`. ⛔ Never a hardcoded index (§B66): the service skips a corrupt
// zero-id record, so a position is not an identity. BOUNDED, so a missing row fails the caller's check.
bool keys_cursor_to(CreateFix& f, const UiSnapshot& s, uint32_t team_id) {
    for (int i = 0; i < 12; ++i) {
        SavedKeySelRow r{};
        const SavedKeySelList l = saved_keys_sel_rows(f.m.state().saved_keys);
        if (l.at(f.m.state().cursor, r) && !r.back && r.key.team_id == team_id) return true;
        f.m.on_gesture(Gesture::short_press, s);
    }
    return false;
}
UiProvAnswer forgotten_answer() { UiProvAnswer a{}; a.outcome = UiProvOutcome::key_forgotten; return a; }
UiProvAnswer forget_failed_answer(const char* why) {
    UiProvAnswer a{}; a.outcome = UiProvOutcome::key_forget_failed; a.reason = why; return a;
}
}  // namespace

TEST_CASE("ui16-k6-menu: SAVED KEYS is the FIFTH child, hidden when the keyring plane is absent, and BACK stays last") {
    // ★ FIVE SEPARATE PREDICATES, ⛔ never one flag: the row is offered on its own condition and on nothing else.
    CHECK(provision_rows(false, false, false, false, /*saved_keys=*/true).n == 2);
    { const ProvRowList l = provision_rows(false, false, false, false, true);
      CHECK(l.row[0] == ProvRow::saved_keys);
      CHECK(l.row[1] == ProvRow::back); }                      // ⛔ BACK is UNCONDITIONALLY last
    CHECK(provision_has_child(false, false, false, false, /*saved_keys=*/true) == true);
    // ⛔ HIDDEN, ⛔ never a refusing stub ([[B209]]): a build with no team plane offers no row at all.
    CHECK(provision_rows(false, false, false, false, false).n == 1);
    // ★ AND IT IS APPENDED, ⛔ not inserted: every landed row keeps the position the operator has learned.
    { const ProvRowList all = provision_rows(true, true, true, true, true);
      CHECK(all.n == 6);
      CHECK(all.row[0] == ProvRow::create_team);
      CHECK(all.row[1] == ProvRow::join_static);
      CHECK(all.row[2] == ProvRow::join_team);
      CHECK(all.row[3] == ProvRow::invite);
      CHECK(all.row[4] == ProvRow::saved_keys);
      CHECK(all.row[5] == ProvRow::back); }
    // ★ ONE SPELLING FOR ONE OPERATION (U1): the row label IS the screen's title, so the two cannot drift.
    CHECK(strcmp(provision_row_label(ProvRow::saved_keys), kSavedKeysTitle) == 0);
    CHECK(strcmp(provision_row_label(ProvRow::saved_keys), "SAVED KEYS") == 0);
    CHECK(strlen(provision_row_label(ProvRow::saved_keys)) + 1u <= 19u);   // the rail's body still holds the row
}

TEST_CASE("ui16-k6-open: the keyring is read ONCE on the transition, and OPENING the screen performs NOTHING") {
    CreateFix f; const auto s = keys_snap();
    f.prov.keys = keys_list(4, /*active_row=*/2);
    CHECK(f.prov.keys_calls == 0);
    CHECK(open_saved_keys(f, s));
    // ★ ONE READ, ON THE TRANSITION — ⛔ not per tick and ⛔ not per page: the enumeration reaches flash.
    CHECK(f.prov.keys_calls == 1);
    // ⛔ OPENING PERFORMS NOTHING: no removal, no write, no eviction (spec §4-K6 pin 1).
    CHECK(f.prov.calls == 0);
    CHECK(f.store.writes == 0);
    CHECK(f.prov.last_forget_id == 0);
    // ★ THE COPY IS FROZEN AND IS WHAT THE SCREEN WALKS: four rows plus the unconditional BACK.
    CHECK(f.m.state().saved_keys.n == 4);
    CHECK(saved_keys_sel_rows(f.m.state().saved_keys).n == 5);
    CHECK(f.m.state().cursor == 0);
    // ...and a `short` walk over the whole list still reads it ZERO more times (the freeze, measured).
    for (int i = 0; i < 8; ++i) f.m.on_gesture(Gesture::short_press, s);
    CHECK(f.prov.keys_calls == 1);
    CHECK(f.prov.calls == 0);
    // ★ BACK LEAVES TO THE PROVISION MENU and still performs nothing.
    { SavedKeySelRow r{};
      for (int i = 0; i < 8; ++i) {
          const SavedKeySelList l = saved_keys_sel_rows(f.m.state().saved_keys);
          if (l.at(f.m.state().cursor, r) && r.back) break;
          f.m.on_gesture(Gesture::short_press, s);
      }
      CHECK(r.back);
      f.m.on_gesture(Gesture::double_press, s);
      CHECK(f.m.state().provisioning == Provision::menu);
      CHECK(f.prov.calls == 0);
      CHECK(f.store.writes == 0); }
}

TEST_CASE("ui16-k6-confirm: an INACTIVE row opens the irreversible confirmation on BACK, carrying the FULL 32-bit id") {
    CreateFix f; const auto s = keys_snap();
    f.prov.keys = keys_list(4, /*active_row=*/2);
    f.prov.forget_answer = forgotten_answer();
    CHECK(open_saved_keys(f, s));
    CHECK(keys_cursor_to(f, s, 0xAB000004u));                 // an INACTIVE row
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().provisioning == Provision::saved_keys_confirm);
    // ★ §3.6.3's default, established by `enter_provision` so it cannot be forgotten by the entry that lands here.
    CHECK(f.m.state().prov_confirm == ProvConfirm::back);
    // ★★★ THE TARGET IS THE ROW'S OWN FULL 32-BIT ID — ⛔ never the cursor, ⛔ never the six-hex fingerprint.
    CHECK(f.m.state().forget_team == 0xAB000004u);
    CHECK((f.m.state().forget_team & 0xFFFFFFu) != f.m.state().forget_team);   // the top byte really matters
    // ⛔ OPENING IT PERFORMS NOTHING (pin 1): no seam call, no write.
    CHECK(f.prov.calls == 0);
    CHECK(f.store.writes == 0);

    // ⛔⛔ `BACK` PERFORMS NOTHING AT ALL AND RETURNS TO THE **LIST** — ⛔ not the menu, and ⛔ without re-reading it.
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().provisioning == Provision::saved_keys);
    CHECK(f.prov.calls == 0);
    CHECK(f.prov.keys_calls == 1);                            // ⛔ the frozen list survives a change of mind
    CHECK(f.store.writes == 0);
    CHECK(f.m.state().forget_team == 0);                      // ...and the target is RETIRED on the way out

    // ★ REACHING THE ACT COSTS `short` THEN `double` (P-13): one press can never mean the destructive one.
    CHECK(keys_cursor_to(f, s, 0xAB000004u));
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().provisioning == Provision::saved_keys_confirm);
    f.m.on_gesture(Gesture::short_press, s);
    CHECK(f.m.state().prov_confirm == ProvConfirm::confirm);
    CHECK(f.prov.calls == 0);                                  // ⛔ the toggle is not an act
    f.m.on_gesture(Gesture::double_press, s);
    // ★★★ EXACTLY ONE REMOVAL, KEYED ON THE FULL ID, AND THE SCREEN MOVED ONLY AFTER IT RETURNED.
    CHECK(f.prov.calls == 1);
    CHECK(f.prov.last_op == UiProvOp::forget_key);
    CHECK(f.prov.last_forget_id == 0xAB000004u);
    CHECK(f.prov.arm_at_call == Provision::saved_keys_confirm);   // §8 pin 2: it ran BEFORE the screen moved
    CHECK(strcmp(f.prov.head_at_call, "") == 0);                  // ...and no result text existed yet
    CHECK(f.m.state().provisioning == Provision::saved_keys_result);
    CHECK(strcmp(prov_result_head(f.m.state().prov_answer), kKeyForgottenText) == 0);
}

TEST_CASE("ui16-k6-active: an ACTIVE row lands on ACTIVE KEY / CANNOT FORGET, with NO destructive action anywhere") {
    CreateFix f; const auto s = keys_snap();
    f.prov.keys = keys_list(4, /*active_row=*/2);              // 0xAB000003 is the ACTIVE one
    CHECK(open_saved_keys(f, s));
    CHECK(keys_cursor_to(f, s, 0xAB000003u));
    f.m.on_gesture(Gesture::double_press, s);
    // ★★★★ ITS OWN ARM, ⛔ NOT THE CONFIRMATION WITH A ROW HIDDEN: there is no action to select here, which is what
    //      makes *"the active key cannot be forgotten from the panel"* STRUCTURAL rather than conditional.
    CHECK(f.m.state().provisioning == Provision::saved_keys_active);
    CHECK(f.m.state().provisioning != Provision::saved_keys_confirm);
    // ★ It still shows WHICH record was selected, whole.
    CHECK(f.m.state().forget_team == 0xAB000003u);
    // ⛔ NOTHING WAS PERFORMED, AND NO PRESS ON THIS SCREEN CAN PERFORM ANYTHING.
    CHECK(f.prov.calls == 0);
    for (int i = 0; i < 6; ++i) {
        f.m.on_gesture(Gesture::short_press, s);
        CHECK(f.prov.calls == 0);
        if (f.m.state().provisioning != Provision::saved_keys_active) break;
    }
    // ★ EITHER PRESS RETURNS TO THE LIST — the screen the operator was choosing on.
    CHECK(f.m.state().provisioning == Provision::saved_keys);
    CHECK(f.prov.calls == 0);
    CHECK(f.store.writes == 0);
    { CreateFix g; const auto s2 = keys_snap();
      g.prov.keys = keys_list(4, 2);
      CHECK(open_saved_keys(g, s2));
      CHECK(keys_cursor_to(g, s2, 0xAB000003u));
      // ★ THE OTHER PRESS TAKES THE SAME EXIT: `double` on this screen is not an act either.
      g.m.on_gesture(Gesture::double_press, s2);
      CHECK(g.m.state().provisioning == Provision::saved_keys_active);
      g.m.on_gesture(Gesture::double_press, s2);
      CHECK(g.m.state().provisioning == Provision::saved_keys);
      CHECK(g.prov.calls == 0); }
}

TEST_CASE("ui16-k6-result: KEY FORGOTTEN is written by the ACT, and its acknowledgement returns to the REFRESHED list") {
    CreateFix f; const auto s = keys_snap();
    f.prov.keys = keys_list(4, 2);
    f.prov.forget_answer = forgotten_answer();
    CHECK(open_saved_keys(f, s));
    CHECK(keys_cursor_to(f, s, 0xAB000001u));
    f.m.on_gesture(Gesture::double_press, s);
    f.m.on_gesture(Gesture::short_press, s);
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().provisioning == Provision::saved_keys_result);
    CHECK(strcmp(prov_result_head(f.m.state().prov_answer), "KEY FORGOTTEN") == 0);
    CHECK(strcmp(prov_result_detail(f.m.state().prov_answer), "") == 0);    // ⛔ no second row, no id re-printed
    CHECK(strcmp(prov_result_detail2(f.m.state().prov_answer), "") == 0);

    // ★★★ THE ACKNOWLEDGEMENT **RE-READS** THE KEYRING — that is the whole of *"returns to the refreshed list"*.
    //     ⛔ Returning to the frozen copy would show the record just removed still standing.
    f.prov.keys = keys_list(3, 1);
    const int reads_before = f.prov.keys_calls;
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().provisioning == Provision::saved_keys);
    CHECK(f.prov.keys_calls == reads_before + 1);
    CHECK(f.m.state().saved_keys.n == 3);
    // ⛔ AND NOTHING IS RE-RUN: acknowledging a removal may not remove a second key.
    CHECK(f.prov.calls == 1);
}

TEST_CASE("ui16-k6-failure: a failed removal says KEY NOT FORGOTTEN plus the SERVICE's token — ⛔ never a success") {
    // ★★★ THE SIX FAILING SERVICE ARMS ALL LAND ON ONE TRUE SENTENCE, and the SECOND row is the service's own word
    //     so the operator still learns WHICH way it refused. ⛔ `KEY FORGOTTEN` is unreachable from any of them.
    for (int i = 1; i < int(mrfw::KeyringForget::count); ++i) {
        CreateFix f; const auto s = keys_snap();
        f.prov.keys = keys_list(4, 2);
        const char* tok = mrfw::keyring_forget_name(mrfw::KeyringForget(i));
        f.prov.forget_answer = forget_failed_answer(tok);
        CHECK(open_saved_keys(f, s));
        CHECK(keys_cursor_to(f, s, 0xAB000002u));
        f.m.on_gesture(Gesture::double_press, s);
        f.m.on_gesture(Gesture::short_press, s);
        f.m.on_gesture(Gesture::double_press, s);
        CHECK(f.m.state().provisioning == Provision::saved_keys_result);
        CHECK(strcmp(prov_result_head(f.m.state().prov_answer), kKeyNotForgottenText) == 0);
        CHECK(strcmp(prov_result_head(f.m.state().prov_answer), kKeyForgottenText) != 0);
        CHECK(strcmp(prov_result_detail(f.m.state().prov_answer), tok) == 0);
        // ⛔ AND THE FAILURE STAYS VISIBLE UNTIL IT IS ACKNOWLEDGED: it is a screen, not a flash.
        CHECK(strcmp(prov_result_head(f.m.state().prov_answer), "") != 0);
    }
    // ⛔ A NULL SEAM REFUSES OUT LOUD (C2) rather than doing nothing — a `double` that changes no pixel is a dead
    //    button. ⓘ The refusal is the FAILING outcome, so the panel cannot claim a removal that never ran.
    {
        CfgFix f; const auto s = keys_snap();                  // ⛔ no provisioning seam attached at all
        CHECK(open_provision(f.m, s));
        CHECK(prov_cursor_to(f.m, s, ProvRow::saved_keys));
        f.m.on_gesture(Gesture::double_press, s);
        CHECK(f.m.state().provisioning == Provision::saved_keys);
        CHECK(f.m.state().saved_keys.served == false);         // ★ the list FAILS CLOSED: nothing was served
        CHECK(saved_keys_sel_rows(f.m.state().saved_keys).n == 1);   // ⛔ BACK only — no row may be selected
        CHECK(strcmp(saved_keys_head(f.m.state().saved_keys), "NO KEYRING") == 0);
    }
}

TEST_CASE("ui16-k6-pin8: KEYRING FULL's acknowledgement ENTERS the list — ⛔ it deletes nothing and ⛔ replays nothing") {
    CreateFix f; const auto s = keys_snap();
    f.prov.keys = keys_list(4, 2);
    // The create refuses with P-15's loud `KEYRING FULL`, reported as the TYPED flag (⛔ never as display text).
    UiProvAnswer full{};
    full.outcome      = UiProvOutcome::refused;
    full.reason       = "keyring_full";
    full.keyring_full = true;
    f.prov.answer = full;
    CHECK(open_create_confirm(f, s));
    f.m.on_gesture(Gesture::short_press, s);
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().provisioning == Provision::create_result);
    CHECK(f.prov.calls == 1);
    const int keys_before = f.prov.keys_calls;

    // ★★★★ THE ACKNOWLEDGEMENT LANDS ON THE MANAGEMENT SCREEN, where the dead end can be resolved.
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().provisioning == Provision::saved_keys);
    CHECK(f.prov.keys_calls == keys_before + 1);               // the list is READ, once, on the transition
    // ⛔⛔ AND IT CHOOSES NOTHING: the cursor is on the FIRST row, no target is armed, nothing was removed.
    CHECK(f.m.state().cursor == 0);
    CHECK(f.m.state().forget_team == 0);
    CHECK(f.prov.last_forget_id == 0);
    CHECK(f.m.state().saved_keys.n == 4);                      // ⛔ all four records still listed
    // ⛔⛔ AND THE CREATE IS **NOT REPLAYED**: `calls` is still the ONE create the operator asked for — two explicit
    //     transactions, never one disguised one.
    CHECK(f.prov.calls == 1);
    CHECK(f.prov.last_op == UiProvOp::create_team);
    CHECK(f.store.writes == 0);

    // ★★★★ AND THE DOOR IS THE **FLAG'S**, ⛔ NEVER THE TOKEN'S. Two arms, because a text-shaped decision passes a
    //      one-arm test by coincidence: the flag ALONE opens it (with a token the panel never matched on), and the
    //      token ALONE does not. ⛔ A navigation decision taken by comparing a display string is [[B48]]'s class at
    //      the navigation layer, and `reason` is explicitly the operator's to read — not code's to switch on.
    {
        CreateFix h; const auto s3 = keys_snap();
        h.prov.keys = keys_list(4, 2);
        UiProvAnswer flag_only{};
        flag_only.outcome      = UiProvOutcome::refused;
        flag_only.reason       = "";                       // ⛔ NOTHING for a text match to find
        flag_only.keyring_full = true;
        h.prov.answer = flag_only;
        CHECK(open_create_confirm(h, s3));
        h.m.on_gesture(Gesture::short_press, s3);
        h.m.on_gesture(Gesture::double_press, s3);
        h.m.on_gesture(Gesture::double_press, s3);
        CHECK(h.m.state().provisioning == Provision::saved_keys);   // ★ the FLAG alone opened it
        CHECK(h.prov.calls == 1);                                   // ⛔ and still no replay
    }
    {
        CreateFix h; const auto s3 = keys_snap();
        h.prov.keys = keys_list(4, 2);
        UiProvAnswer text_only{};
        text_only.outcome      = UiProvOutcome::refused;
        text_only.reason       = "keyring_full";           // ★ the token the SERVICE really produces...
        text_only.keyring_full = false;                    // ...but the transaction did NOT report that refusal
        h.prov.answer = text_only;
        CHECK(open_create_confirm(h, s3));
        h.m.on_gesture(Gesture::short_press, s3);
        h.m.on_gesture(Gesture::double_press, s3);
        const int kb = h.prov.keys_calls;
        h.m.on_gesture(Gesture::double_press, s3);
        CHECK(h.m.state().provisioning == Provision::menu);          // ⛔ the TEXT alone opens nothing
        CHECK(h.prov.keys_calls == kb);
    }
    // ⛔ AND EVERY OTHER REFUSAL STILL LANDS ON THE MENU, exactly as it always did: the flag is the only door.
    {
        CreateFix g; const auto s2 = keys_snap();
        g.prov.keys = keys_list(4, 2);
        UiProvAnswer other{};
        other.outcome = UiProvOutcome::refused;
        other.reason  = "keyring_invalid";
        other.keyring_full = false;
        g.prov.answer = other;
        CHECK(open_create_confirm(g, s2));
        g.m.on_gesture(Gesture::short_press, s2);
        g.m.on_gesture(Gesture::double_press, s2);
        const int kb = g.prov.keys_calls;
        g.m.on_gesture(Gesture::double_press, s2);
        CHECK(g.m.state().provisioning == Provision::menu);
        CHECK(g.prov.keys_calls == kb);                        // ⛔ and no keyring read was spent on it
    }
}

TEST_CASE("ui16-k6-rows: the list's rows are IDENTITIES, the ACTIVE marker is a status, and BACK is unconditional") {
    // ★ BACK IS THE LAST ROW WHATEVER THE STORE SAYS — leaving may never depend on a store or a build flag.
    { mrfw::SavedKeyList empty{}; empty.served = true; empty.binding_read = true; empty.st = mrnv::TeamKeyRead::ok;
      const SavedKeySelList l = saved_keys_sel_rows(empty);
      CHECK(l.n == 1);
      CHECK(l.row[0].back); }
    // ⛔ A LIST THAT WAS NOT ESTABLISHED OFFERS **NO ROW**: three different failures, one safe answer.
    { mrfw::SavedKeyList l0{};                       CHECK(saved_keys_sel_rows(l0).n == 1); }   // ⛔ not served
    { mrfw::SavedKeyList l1{}; l1.served = true; l1.st = mrnv::TeamKeyRead::ok; l1.n = 2;
      CHECK(saved_keys_sel_rows(l1).n == 1); }                                                   // ⛔ binding unread
    { mrfw::SavedKeyList l2 = keys_list(3); l2.st = mrnv::TeamKeyRead::io_failed;
      CHECK(saved_keys_sel_rows(l2).n == 1); }                                                   // ⛔ store unreadable
    // ★ THE ROW CARRIES THE WHOLE ENTRY (U2), so the highlight and the act name the same record.
    { const SavedKeySelList l = saved_keys_sel_rows(keys_list(4, 1));
      CHECK(l.n == 5);
      CHECK(l.row[0].key.team_id == 0xAB000001u);
      CHECK(l.row[1].key.team_id == 0xAB000002u);
      CHECK(l.row[1].key.active);
      CHECK(l.row[0].key.active == false);
      CHECK(l.row[4].back);
      // ⛔ FAILS CLOSED: an out-of-range index names NO row (here it would open an irreversible confirmation).
      SavedKeySelRow r{};
      CHECK(l.at(4, r));
      CHECK(l.at(5, r) == false); }
    // ★ THE `ACTIVE` MARKER IS A ROW SUFFIX AND A STATUS ONLY — the decision is the model's, ⛔ not the renderer's.
    { mrfw::SavedKeyEntry a{}; a.active = true;
      mrfw::SavedKeyEntry i{};
      CHECK(strcmp(saved_key_row_tag(a), " ACTIVE") == 0);
      CHECK(strcmp(saved_key_row_tag(i), "") == 0);
      CHECK(strstr(saved_key_row_tag(a), kSavedKeyActiveTag) != nullptr); }
}

TEST_CASE("ui16-k6-lexemes: the SEVEN K6 words are declared ONCE, fit the rail, and the failure word is not the success") {
    // ★ The owner-ruled spellings, carried VERBATIM (spec §8 S-31 / S-40..S-44), each pinned so a re-ruling changes
    //   it in exactly one place. ⚠ WIDTH IS A CONSTRAINT: the rail leaves a 19-column body.
    CHECK(strcmp(kSavedKeysTitle,    "SAVED KEYS")    == 0);      // S-40
    CHECK(strcmp(kSavedKeysEmpty,    "NO SAVED KEYS") == 0);      // S-41
    CHECK(strcmp(kKeyForgottenText,  "KEY FORGOTTEN") == 0);      // S-42
    CHECK(strcmp(kActiveKeyText,     "ACTIVE KEY")    == 0);      // S-43 row 1
    CHECK(strcmp(kCannotForgetText,  "CANNOT FORGET") == 0);      // S-43 row 2
    CHECK(strcmp(kSavedKeyActiveTag, "ACTIVE")        == 0);      // S-44
    CHECK(strcmp(kForgetKeyText,     "FORGET KEY")    == 0);      // S-31 — ACTIVATED, no longer reserved
    CHECK(strcmp(kKeyNotForgottenText, "KEY NOT FORGOTTEN") == 0);
    for (const char* w : { kSavedKeysTitle, kSavedKeysEmpty, kKeyForgottenText, kActiveKeyText,
                           kCannotForgetText, kSavedKeyActiveTag, kForgetKeyText, kKeyNotForgottenText })
        CHECK(strlen(w) <= 19u);
    // ⛔ THE FAILURE WORD IS NOT THE SUCCESS WORD, and it is not a SUBSTRING of it either — a `strstr`-shaped reader
    //    must not be able to see one in the other.
    CHECK(strcmp(kKeyNotForgottenText, kKeyForgottenText) != 0);
    CHECK(strstr(kKeyForgottenText, kKeyNotForgottenText) == nullptr);
    // ★ THE CONFIRMATION'S TWO ACTIONS, BY IDENTITY — and `BACK` is the ONE spelling CALLED, ⛔ never re-spelled.
    CHECK(strcmp(forget_key_label(ProvConfirm::back), prov_confirm_label(ProvConfirm::back)) == 0);
    CHECK(strcmp(forget_key_label(ProvConfirm::back), "BACK") == 0);
    CHECK(strcmp(forget_key_label(ProvConfirm::confirm), kForgetKeyText) == 0);
    // ⛔ AND THE FORBIDDEN NEIGHBOURS STAY ABSENT: this is retention management, ⛔ never "key rotation".
    for (const char* w : { kSavedKeysTitle, kSavedKeysEmpty, kKeyForgottenText, kActiveKeyText,
                           kCannotForgetText, kForgetKeyText, kKeyNotForgottenText }) {
        CHECK(strstr(w, "ROTATE") == nullptr);
        CHECK(strstr(w, "ROTATION") == nullptr);
    }
    // ★ THE LIST'S FOUR HEADS ARE FOUR DIFFERENT SENTENCES (four different operator actions).
    { mrfw::SavedKeyList l{};                       CHECK(strcmp(saved_keys_head(l), "NO KEYRING") == 0); }
    { mrfw::SavedKeyList l{}; l.served = true;      CHECK(strcmp(saved_keys_head(l), "CONFIG UNREADABLE") == 0); }
    { mrfw::SavedKeyList l = keys_list(0);          CHECK(strcmp(saved_keys_head(l), kSavedKeysEmpty) == 0); }
    { mrfw::SavedKeyList l = keys_list(0); l.st = mrnv::TeamKeyRead::absent;
      CHECK(strcmp(saved_keys_head(l), kSavedKeysEmpty) == 0); }                  // ⛔ absent is NEVER an error
    { mrfw::SavedKeyList l = keys_list(2); l.st = mrnv::TeamKeyRead::invalid;
      CHECK(strcmp(saved_keys_head(l), "KEY STORE INVALID") == 0); }
    { mrfw::SavedKeyList l = keys_list(2); l.st = mrnv::TeamKeyRead::io_failed;
      CHECK(strcmp(saved_keys_head(l), "STORAGE FAILURE") == 0); }
    { mrfw::SavedKeyList l = keys_list(2);          CHECK(strcmp(saved_keys_head(l), "") == 0); }
    for (const char* w : { "NO KEYRING", "CONFIG UNREADABLE", "KEY STORE INVALID", "STORAGE FAILURE" })
        CHECK(strlen(w) <= 19u);
}

TEST_CASE("ui16-k6-resources: the retention carriers cost exactly themselves, and NO landed field moved") {
    // ★★ MEASURED, ⛔ NOT REASONED, and every placement is `offsetof`-proved rather than asserted in prose.
    // ⚠ NATIVE ALIGNMENT HIDES THE BOARD FIGURE (D2's standing warning) — this pins the SHAPE, not the flash cost.
    // ---- the METADATA carrier: two facts per record, and ⛔ no room for a key ------------------------------------
    CHECK(sizeof(mrfw::SavedKeyEntry) == 8u);
    CHECK(offsetof(mrfw::SavedKeyEntry, team_id)  == 0u);
    CHECK(offsetof(mrfw::SavedKeyEntry, active)   == 4u);
    CHECK(offsetof(mrfw::SavedKeyEntry, reserved) == 5u);      // ★ NAMED padding — a whole-record compare is sound
    CHECK(sizeof(mrfw::SavedKeyList) == 36u);                  // 4 x 8 + n + st + served + binding_read
    // ---- the SNAPSHOT predicate is FREE: it lands in the bool run's existing padding ------------------------------
    // ⓘ ⚠ **RE-PINNED 2026-08-26 BY §UI-10/11 P3, AND THE SUPERSEDED FIGURE IS KEPT VISIBLE: `1008u`.** The struct
    //   grew by the compose-list projection — `uint32_t preset_generation` at the old 8-aligned END (1008, free) plus
    //   two alignof-1 `ComposeList`s (161 each) at 1012 and 1173 — so it measures **1336 (+328)**. ⛔ NOTHING BELOW
    //   MOVED: every offset this case pins is ahead of `member[]` and is byte-identical.
    CHECK(sizeof(mrui::UiSnapshot) == 1336u);                  // ⛔ UNCHANGED BY K6 ITSELF
    CHECK(offsetof(mrui::UiSnapshot, prov_invite)     == 689u);   // ⛔ UNMOVED
    CHECK(offsetof(mrui::UiSnapshot, prov_saved_keys) == 690u);   // ★ the new flag, in the pad beside it
    // ---- the ANSWER's typed flag is FREE: it lands in the hole `saved_key` already sits in ------------------------
    CHECK(sizeof(mrui::UiProvAnswer) == 16u);                  // ⛔ UNCHANGED
    CHECK(offsetof(mrui::UiProvAnswer, saved_key)    == 2u);   // ⛔ UNMOVED
    CHECK(offsetof(mrui::UiProvAnswer, keyring_full) == 3u);   // ★ the new flag
    CHECK(offsetof(mrui::UiProvAnswer, team_id)      == 4u);   // ⛔ UNMOVED — no landed field shifted
    CHECK(sizeof(mrui::UiProvIntent) == 32u);                  // ⛔ UNCHANGED: the fifth op carries no new field
    // ---- and the STATE's two carriers cost EXACTLY themselves, 40 bytes, with no padding wasted ------------------
    // ⚠ RE-MEASURED 2026-08-25 (§UI-16 K7): every offset below shifted by exactly **+8** and the two sizes with
    //   them, because K7 inserts its two frozen compose fields at the HEAD of `UiState`. ⛔ K6's CLAIM — its two
    //   carriers cost EXACTLY themselves, contiguously, with no padding wasted — is what these lines measure, and
    //   the arithmetic below still closes: 348 + 4 = 352, 352 + 4 = 356, 356 + 36 = 392.
    CHECK(offsetof(mrui::UiState, saved_key_team) == 348u);    // ⛔ K5's field (340 + 8)
    CHECK(offsetof(mrui::UiState, forget_team)    == 352u);    // ★ 4 B, immediately after it
    CHECK(offsetof(mrui::UiState, saved_keys)     == 356u);    // ★ 36 B, immediately after THAT
    CHECK(offsetof(mrui::UiState, invite)         == 392u);    // = 356 + 36, i.e. ⛔ not one padding byte between
    CHECK(sizeof(mrui::UiState) == 504u);                      // 456 + 4 + 36 + K7's 8 = 504 ✓
    CHECK(sizeof(mrui::UiModel) == 928u);                      // 880 + the same 40 + K7's 8
}

// ============== §UI-16 K6 (QG blocker, 2026-08-25) — THE **RECEIVED** GRANT'S FULL-KEYRING ACKNOWLEDGEMENT
// ★★★★ THE BLOCKER: a `KEYRING FULL` refusal of a RECEIVED grant showed the three correct rows and then
//      acknowledged into the MENU, while the `team new` refusal of the SAME store state reached `SAVED KEYS`.
//      Spec §K6 (`:987`) rules the direction for a `KEYRING FULL` result of **either origin**.
// ⛔ WHAT THIS BLOCK ALSO MEASURES IS THE **ABSENCE** OF CHANGE: the three ruled rows (S-26/S-27) are byte-identical,
//    the note still navigates nothing on ARRIVAL, and a NON-full unsaved note still lands on the MENU.
TEST_CASE("ui16-k6-grantack: a FULL-keyring receipt keeps its three ruled rows and lands the ACK in SAVED KEYS") {
    CreateFix f; const auto s = keys_snap();
    f.prov.keys = keys_list(4, /*active_row=*/2);
    f.prov.answer = created_answer(0x12A1B2C3u);
    CHECK(open_create_confirm(f, s));
    f.m.on_gesture(Gesture::short_press, s);
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().provisioning == Provision::create_result);
    const uint8_t cur_before = f.m.state().cursor;
    const Screen  scr_before = f.m.state().screen;
    const int     acts_before = f.prov.calls, keys_before = f.prov.keys_calls;

    // ★ THE RECEIPT ARRIVES: a grant whose durable half was refused by a FULL keyring.
    f.m.on_team_key_note(/*saved=*/false, /*keyring_full=*/true, 9000);
    // ★★★ THE THREE RULED ROWS ARE **UNCHANGED** — S-26/S-27, byte for byte. They are three TRUE sentences: the key
    //     IS live in RAM and it WILL be gone after a reboot. ⛔ The new fact changes ⛔ NO word.
    CHECK(f.m.state().prov_answer.outcome == UiProvOutcome::team_key_unsaved);
    CHECK(strcmp(prov_result_head(f.m.state().prov_answer),    "TEAM KEY ACTIVE")  == 0);
    CHECK(strcmp(prov_result_detail(f.m.state().prov_answer),  "NOT SAVED")        == 0);
    CHECK(strcmp(prov_result_detail2(f.m.state().prov_answer), "LOST ON REBOOT")   == 0);
    CHECK(f.m.state().prov_answer.keyring_full == true);
    // ⛔ AND THE ARRIVAL STILL NAVIGATES NOTHING (spec §4-K4 pin 3): same screen, same arm, same cursor, no act.
    CHECK(f.m.state().provisioning == Provision::create_result);
    CHECK(f.m.state().screen == scr_before);
    CHECK(f.m.state().cursor == cur_before);
    CHECK(f.prov.calls == acts_before);
    CHECK(f.prov.keys_calls == keys_before);        // ⛔ and it read no flash

    // ★★★★ THE PRESS — and ONLY the press — LANDS IN `SAVED KEYS`, where the dead end can be resolved.
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().provisioning == Provision::saved_keys);
    CHECK(f.prov.keys_calls == keys_before + 1);    // the list is READ, once, on the transition
    // ⛔⛔ IT CHOSE NO VICTIM, DELETED NOTHING AND RETRIED NOTHING — the create-side arm's rules, verbatim.
    CHECK(f.m.state().cursor == 0);
    CHECK(f.m.state().forget_team == 0);
    CHECK(f.prov.last_forget_id == 0);
    CHECK(f.m.state().saved_keys.n == 4);
    CHECK(f.prov.calls == acts_before);             // ⛔ no transaction was re-run — a grant is the granter's
    CHECK(f.store.writes == 0);
}

TEST_CASE("ui16-k6-grantack-menu: a NON-full unsaved receipt still acknowledges to the MENU, and a SAVED one to STATUS") {
    // ⛔ THE LANDING IS THE **FLAG'S**, not the note's: every other durable failure (`record_unreadable`,
    //    `record_mismatch`, `binding_failed`, a corrupt or unopenable store) has no removal list to offer, and
    //    walking the operator into one would be a false remedy.
    {
        CreateFix f; const auto s = keys_snap();
        f.prov.keys = keys_list(4, 2);
        f.prov.answer = created_answer(0x12A1B2C3u);
        CHECK(open_create_confirm(f, s));
        f.m.on_gesture(Gesture::short_press, s);
        f.m.on_gesture(Gesture::double_press, s);
        const int keys_before = f.prov.keys_calls;
        f.m.on_team_key_note(/*saved=*/false, /*keyring_full=*/false, 9000);
        CHECK(f.m.state().prov_answer.keyring_full == false);
        // ★ the SAME three rows — which is the point: the words never depended on the flag.
        CHECK(strcmp(prov_result_head(f.m.state().prov_answer),    "TEAM KEY ACTIVE") == 0);
        CHECK(strcmp(prov_result_detail(f.m.state().prov_answer),  "NOT SAVED")       == 0);
        CHECK(strcmp(prov_result_detail2(f.m.state().prov_answer), "LOST ON REBOOT")  == 0);
        f.m.on_gesture(Gesture::double_press, s);
        CHECK(f.m.state().provisioning == Provision::menu);
        CHECK(f.prov.keys_calls == keys_before);    // ⛔ and no keyring read was spent on it
    }
    // ⛔⛔ AND A **SAVED** RECEIPT CAN NEVER CARRY THE FLAG — refused by construction in `on_team_key_note`, ⛔ not by
    //     a discipline at the call sites: a receipt that PERSISTED has no dead end, and a `TEAM KEY RECEIVED` screen
    //     whose acknowledgement opened a removal list would be the "success that isn't" from the other side.
    {
        CreateFix f; const auto s = keys_snap();
        f.prov.keys = keys_list(4, 2);
        f.prov.answer = created_answer(0x12A1B2C3u);
        CHECK(open_create_confirm(f, s));
        f.m.on_gesture(Gesture::short_press, s);
        f.m.on_gesture(Gesture::double_press, s);
        const int keys_before = f.prov.keys_calls;
        f.m.on_team_key_note(/*saved=*/true, /*keyring_full=*/true, 9000);   // ⚠ a caller that got it wrong
        CHECK(f.m.state().prov_answer.outcome == UiProvOutcome::team_key_received);
        CHECK(f.m.state().prov_answer.keyring_full == false);                // ★ REFUSED at the one entry point
        f.m.on_gesture(Gesture::double_press, s);
        // ★ §UI-17 keyrecv's landed ruling is untouched: the SUCCESS note acknowledges to PASSIVE STATUS.
        CHECK(f.m.state().screen == Screen::status);
        CHECK(f.m.state().provisioning == Provision::closed);
        CHECK(f.prov.keys_calls == keys_before);
    }
}

TEST_CASE("ui16-k6-grantack-join: the FULL-keyring landing works from the STATIC-JOIN result screen too") {
    // ⛔ ONE NOTE, ONE LANDING (the answer-keyed rule): the renderer draws the note on WHICHEVER result screen is up,
    //    so a landing wired to only one of them would give the same receipt two different endings.
    CreateFix f; const auto s = keys_snap();
    f.prov.keys = keys_list(4, 2);
    f.prov.list = ok_join_list(0b0001);
    f.prov.join_answer = UiProvAnswer{};
    f.prov.join_answer.outcome = UiProvOutcome::join_refused;
    f.prov.join_answer.reason  = "invalid_bw";
    // ⓘ Reached the way the operator reaches it — a real slot, a real confirmation, a scripted REFUSAL — so the
    //   screen under test is the one the flow really produces. ⛔ No test-only entry point exists or may be added.
    CHECK(open_join_confirm(f, s, 1));
    f.m.on_gesture(Gesture::short_press, s);
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().provisioning == Provision::join_result);
    const int keys_before = f.prov.keys_calls;
    f.m.on_team_key_note(/*saved=*/false, /*keyring_full=*/true, 9000);
    CHECK(strcmp(prov_result_head(f.m.state().prov_answer), "TEAM KEY ACTIVE") == 0);
    f.m.on_gesture(Gesture::double_press, s);
    CHECK(f.m.state().provisioning == Provision::saved_keys);
    CHECK(f.prov.keys_calls == keys_before + 1);
    CHECK(f.m.state().saved_keys.n == 4);
    CHECK(f.prov.calls == 1);          // ⛔ only the JOIN the operator asked for — ⛔ nothing was re-run
    CHECK(f.m.state().forget_team == 0);
}

// ==================================================================================================================
// §UI-10/11 slice P3 — THE CATALOG REACHES THE PANEL
// ------------------------------------------------------------------------------------------------------------------
// Authorities: the arc spec `docs/superpowers/specs/2026-08-25-ui10-11-preset-catalog-spec.md` §2 (OQ-A, the
// mutation/modal table, `PRESET CHANGED`, R-1/R-2/R-3) and §4's pins, over the parent design
// `2026-07-31-onboard-oled-ui-design.md` §3.2.2 (list semantics) + §3.2.3 (the per-kind sends) + §3.3's
// generation-freeze paragraph.
// ⓘ THE SEND COMPOSITIONS AND THE EXECUTION-TIME FREEZE LIVE IN `test/test_firmware_ui_send.cpp` (`ui10-p3-freeze`
//   / `ui10-p3-loc` / `ui10-p3-emergency`), where the composer and the driver are. What is here is the LIST and the
//   MODAL: row identity, the `L`/`-` column, the empty state, the ruled modal close, and R-1's K7 row.
namespace {
// A catalog with an arbitrary set of DM slots enabled, each carrying a text that NAMES its own slot — so a row that
// resolved through the row INDEX instead of the stable slot shows the wrong word and the case can say which.
mrnv::UiPresetBlob gapped_cat(std::initializer_list<uint8_t> dm_on,
                              std::initializer_list<uint8_t> ch_on = {}) {
    mrnv::UiPresetBlob b{};
    mrfw::preset_defaults(b);
    for (uint8_t i = 0; i < mrfw::kPresetPerKind; ++i) {
        mrfw::preset_slot_put(b.slot[mrfw::kPresetDmFirst + i],      false, false, nullptr, 0);
        mrfw::preset_slot_put(b.slot[mrfw::kPresetChannelFirst + i], false, false, nullptr, 0);
    }
    for (uint8_t o : dm_on) {
        char t[8] = { 'D', 'M', char('0' + o), 0 };
        mrfw::preset_slot_put(b.slot[mrfw::kPresetDmFirst + o - 1], true, (o % 2) == 0, t, 3);
    }
    for (uint8_t o : ch_on) {
        char t[8] = { 'C', 'H', char('0' + o), 0 };
        mrfw::preset_slot_put(b.slot[mrfw::kPresetChannelFirst + o - 1], true, (o % 2) == 0, t, 3);
    }
    return b;
}
UiSnapshot snap_with(const mrnv::UiPresetBlob& cat, uint32_t now_ms = 1000) {
    UiSnapshot s = snap(now_ms);
    ui_snapshot_publish_presets(s, cat);
    return s;
}
}  // namespace

// ★★★★ PIN 1 — **A VISIBLE ROW'S IDENTITY IS ITS STABLE SLOT** (§B66's cure, design §3.2.2's *"code must never
//      derive `dmN` from the current row index"*). The design's own example is used verbatim: `dm1`, `dm4`, `dm8`.
TEST_CASE("ui10-p3-slot: a GAPPED catalog projects dm1/dm4/dm8 to rows 0/1/2 carrying slots 1/4/8") {
    const auto cat = gapped_cat({1, 4, 8});
    ComposeList l{};
    compose_project(cat, mrfw::PresetKind::dm, l);
    CHECK(l.n == 3);
    CHECK(l.row[0].slot == uint8_t(mrfw::kPresetDmFirst + 0));
    CHECK(l.row[1].slot == uint8_t(mrfw::kPresetDmFirst + 3));
    CHECK(l.row[2].slot == uint8_t(mrfw::kPresetDmFirst + 7));
    CHECK(std::strcmp(l.row[0].text, "DM1") == 0);
    CHECK(std::strcmp(l.row[1].text, "DM4") == 0);
    CHECK(std::strcmp(l.row[2].text, "DM8") == 0);
    // ⛔ AND THE ROW INDEX IS NOT THE SLOT: rows 1 and 2 would resolve to `dm2`/`dm3` under the withdrawn identity.
    CHECK(l.row[1].slot != uint8_t(mrfw::kPresetDmFirst + 1));
    CHECK(l.row[2].slot != uint8_t(mrfw::kPresetDmFirst + 2));
    CHECK(compose_row_slot(1, l) == uint8_t(mrfw::kPresetDmFirst + 3));
    CHECK(compose_row_slot(2, l) == uint8_t(mrfw::kPresetDmFirst + 7));
    // ⛔ ...and the DERIVED rows answer a slot that is never a compose row, so nothing can send `back`.
    CHECK(compose_row_slot(3, l) == mrfw::kPresetEmergency);
    CHECK(compose_row_slot(99, l) == mrfw::kPresetEmergency);
}

TEST_CASE("ui10-p3-slot: the PRESS seals the row's stable slot — a gapped list sends dm4, never dm2") {
    const auto cat = gapped_cat({1, 4, 8});
    const auto s = snap_with(cat);
    UiModel m; SendReq req{};
    to_team(m, s);
    m.on_gesture(Gesture::double_press, s);                // open the DM compose on the first roster row
    CHECK(m.state().compose == Compose::dm);
    m.on_gesture(Gesture::short_press, s);                 // walk to ROW 1, which is `dm4`
    CHECK(m.state().cursor == 1);
    m.on_gesture(Gesture::double_press, s);
    const bool got = m.take_send_request(req);
    CHECK(got == true);
    if (!got) return;
    CHECK(req.slot == uint8_t(mrfw::kPresetDmFirst + 3));   // ★ dm4 — the STABLE slot the row carried
    CHECK(req.slot != uint8_t(mrfw::kPresetDmFirst + 1));   // ⛔ not dm2, which the row INDEX would have named
    CHECK(req.generation == cat.generation);               // ★ ...and the generation the wearer SAW
}

// ★★ DM AND CHANNEL ARE NEVER CROSSED (§3.2.2), and the emergency slot is on neither list.
TEST_CASE("ui10-p3-slot: the two lists are kind-pure, and the emergency slot appears on neither") {
    const auto cat = gapped_cat({2, 5}, {1, 3, 7});
    ComposeList dm{}, ch{};
    compose_project(cat, mrfw::PresetKind::dm,      dm);
    compose_project(cat, mrfw::PresetKind::channel, ch);
    CHECK(dm.n == 2);
    CHECK(ch.n == 3);
    for (uint8_t i = 0; i < dm.n; ++i) {
        CHECK(mrfw::preset_kind_of(dm.row[i].slot) == mrfw::PresetKind::dm);
        CHECK(std::strncmp(dm.row[i].text, "DM", 2) == 0);
    }
    for (uint8_t i = 0; i < ch.n; ++i) {
        CHECK(mrfw::preset_kind_of(ch.row[i].slot) == mrfw::PresetKind::channel);
        CHECK(std::strncmp(ch.row[i].text, "CH", 2) == 0);
    }
    // ⛔ The emergency slot is enabled in every catalog and is on NEITHER list — it is long-press only.
    CHECK(cat.slot[mrfw::kPresetEmergency].enabled == 1);
    for (uint8_t i = 0; i < dm.n; ++i) CHECK(dm.row[i].slot != mrfw::kPresetEmergency);
    for (uint8_t i = 0; i < ch.n; ++i) CHECK(ch.row[i].slot != mrfw::kPresetEmergency);
}

// ★★ A DISABLED SLOT HAS NO ROW AT ALL, at every list length the record allows — and the walk is CONTAINED.
TEST_CASE("ui10-p3-slot: a disabled slot is never rendered, and eight enabled slots all scroll into the list") {
    const auto full = gapped_cat({1, 2, 3, 4, 5, 6, 7, 8});
    ComposeList l{};
    compose_project(full, mrfw::PresetKind::dm, l);
    CHECK(l.n == mrfw::kPresetPerKind);
    CHECK(compose_row_count(l, /*grant=*/false) == uint8_t(mrfw::kPresetPerKind + 1));
    // ...and a walk over the whole list returns to row 0 having visited every row exactly once.
    const auto s = snap_with(full);
    UiModel m;
    to_team(m, s);
    m.on_gesture(Gesture::double_press, s);
    const uint8_t n = compose_row_count(s.preset_dm, m.state().compose_grant_row);
    for (uint8_t i = 0; i < n; ++i) {
        CHECK(m.state().cursor == i);
        m.on_gesture(Gesture::short_press, s);
    }
    CHECK(m.state().cursor == 0);                          // wrapped, exactly once round
}

// ★★★★ PIN — **THE `L` / `-` COLUMN, ALWAYS EXACTLY ONE OF THE TWO** (OQ-A's premise). It is what the wearer
//      confirms as part of the double press, so a blank column would silently read as `-`.
TEST_CASE("ui10-p3-row: every PRESET row shows `L` or `-`, and the action rows show neither") {
    const auto cat = gapped_cat({1, 2, 3, 4});             // gapped_cat sets loc on the EVEN ordinals
    ComposeList l{};
    compose_project(cat, mrfw::PresetKind::dm, l);
    CHECK(l.n == 4);
    CHECK(l.row[0].loc == false);   // dm1
    CHECK(l.row[1].loc == true);    // dm2
    CHECK(l.row[2].loc == false);   // dm3
    CHECK(l.row[3].loc == true);    // dm4
    for (bool grant : { false, true }) {
        for (uint8_t i = 0; i < l.n; ++i) {
            const char mk = compose_row_loc_marker(i, l, grant);
            CHECK((mk == 'L' || mk == '-'));               // ★ ALWAYS one of the two, never blank
            CHECK(mk == (l.row[i].loc ? 'L' : '-'));
        }
        // ⛔ R-1: an ACTION row carries no location column at all — see `compose_row_loc_marker`'s own block.
        const uint8_t n = compose_row_count(l, grant);
        if (grant) CHECK(compose_row_loc_marker(l.n, l, grant) == '\0');
        CHECK(compose_row_loc_marker(uint8_t(n - 1), l, grant) == '\0');
    }
    // ---- the LINE, byte for byte: selection marker · L/- · text
    char b[48];
    compose_row_line(b, sizeof b, 0, l, /*grant=*/false, /*selected=*/true);
    CHECK(std::strcmp(b, ">-DM1") == 0);
    compose_row_line(b, sizeof b, 1, l, /*grant=*/false, /*selected=*/false);
    CHECK(std::strcmp(b, " LDM2") == 0);
    // ...and the two derived rows keep EXACTLY the one marker column they have always had (R-1).
    compose_row_line(b, sizeof b, l.n, l, /*grant=*/true, /*selected=*/false);
    CHECK(std::strcmp(b, " GRANT KEY") == 0);
    compose_row_line(b, sizeof b, l.n, l, /*grant=*/true, /*selected=*/true);
    CHECK(std::strcmp(b, ">GRANT KEY") == 0);
    compose_row_line(b, sizeof b, uint8_t(l.n + 1), l, /*grant=*/true, /*selected=*/false);
    CHECK(std::strcmp(b, " back, don't send") == 0);
}

// ★★★★ PIN 6 — §3.2.1's ZERO-ENABLED EMPTY STATE: the note, the back row only, and the cursor ON it.
TEST_CASE("ui10-p3-empty: a catalog with no enabled slots shows the note and offers only `back`") {
    const auto cat = gapped_cat({});                       // ⛔ every DM and channel slot disabled
    ComposeList l{};
    compose_project(cat, mrfw::PresetKind::dm, l);
    CHECK(l.n == 0);
    CHECK(compose_empty_note(l) != nullptr);
    CHECK(std::strcmp(compose_empty_note(l), kNoPresetsText) == 0);
    CHECK(compose_row_count(l, /*grant=*/false) == 1);      // ★ the back row, and nothing else
    CHECK(compose_row_kind(0, l, /*grant=*/false) == ComposeRow::back);
    // ⛔ AND A NON-EMPTY LIST HAS **NO** NOTE — the answer is `nullptr`, never an empty string a caller would draw.
    ComposeList some{};
    compose_project(gapped_cat({3}), mrfw::PresetKind::dm, some);
    CHECK(some.n == 1);
    CHECK(compose_empty_note(some) == nullptr);
    // ---- and on the real model: the sub-view opens, the cursor lands on `back`, and a `double` SENDS NOTHING
    const auto s = snap_with(cat);
    UiModel m; SendReq req{};
    to_team(m, s);
    m.on_gesture(Gesture::double_press, s);
    CHECK(m.state().compose == Compose::dm);
    CHECK(m.state().cursor == 0);
    CHECK(compose_row_kind(m.state().cursor, s.preset_dm, m.state().compose_grant_row) == ComposeRow::back);
    m.on_gesture(Gesture::double_press, s);
    CHECK(m.state().compose == Compose::none);              // it LEFT
    CHECK(m.take_send_request(req) == false);               // ⛔ and queued nothing
}

// ★★★★ PIN — **THE RULED MODAL TABLE (§2), DRIVEN BY THE GENERATION MOVE AND BY NOTHING ELSE.**
TEST_CASE("ui10-p3-modal: a successful CHANGED mutation closes a selection-phase compose WITHOUT sending") {
    const auto cat = gapped_cat({1, 4, 8});
    auto s = snap_with(cat);
    UiModel m; SendReq req{};
    to_team(m, s);
    m.on_gesture(Gesture::double_press, s);
    m.on_gesture(Gesture::short_press, s);                  // a selection is standing on row 1
    CHECK(m.state().compose == Compose::dm);
    CHECK(m.state().compose_gen == cat.generation);
    // A `ui preset set` lands over BLE: P1 stamps the next generation into the record and publishes it.
    auto changed = cat;
    mrfw::preset_slot_put(changed.slot[mrfw::kPresetDmFirst + 1], true, false, "new", 3);
    changed.generation = mrfw::preset_generation_next(changed.generation);
    s = snap_with(changed);
    m.on_tick(s);
    CHECK(m.state().compose == Compose::none);              // ★ CLOSED
    CHECK(m.take_send_request(req) == false);               // ⛔ ...WITHOUT SENDING
    CHECK(m.state().compose_gen == 0u);                     // ⛔ and the seal is retired with the sub-view
}

TEST_CASE("ui10-p3-modal: a NO-OP and a FAILURE leave the compose open — the trigger is the generation, not the verb") {
    const auto cat = gapped_cat({1, 4, 8});
    auto s = snap_with(cat);
    UiModel m;
    to_team(m, s);
    m.on_gesture(Gesture::double_press, s);
    m.on_gesture(Gesture::short_press, s);
    CHECK(m.state().compose == Compose::dm);
    CHECK(m.state().cursor == 1);
    // (a) an IDENTICAL `set`: P1 answers `unchanged`, writes nothing and moves NO generation.
    m.on_tick(snap_with(cat, 1100));
    CHECK(m.state().compose == Compose::dm);                // ⛔ still open...
    CHECK(m.state().cursor == 1);                           // ⛔ ...with the selection intact
    // (b) a VALIDATION or STORAGE failure: nothing is published at all, so the projection and the generation stand.
    m.on_tick(snap_with(cat, 1200));
    CHECK(m.state().compose == Compose::dm);
    CHECK(m.state().cursor == 1);
    // (c) and a press still SENDS the row it was standing on.
    SendReq req{};
    m.on_gesture(Gesture::double_press, s);
    const bool got = m.take_send_request(req);
    CHECK(got == true);
    if (got) CHECK(req.slot == uint8_t(mrfw::kPresetDmFirst + 3));
}

TEST_CASE("ui10-p3-modal: an ALREADY-DISPLAYED OUTCOME may finish — a mutation does not discard a verdict") {
    const auto cat = gapped_cat({1, 4, 8});
    auto s = snap_with(cat);
    UiModel m; SendReq req{};
    to_team(m, s);
    m.on_gesture(Gesture::double_press, s);
    m.on_gesture(Gesture::double_press, s);                 // send row 0 -> the RESULT phase
    CHECK(m.state().compose == Compose::dm);
    CHECK(m.state().compose_result == true);
    CHECK(m.take_send_request(req) == true);
    auto changed = cat;
    changed.generation = mrfw::preset_generation_next(changed.generation);
    m.on_tick(snap_with(changed, 1100));
    CHECK(m.state().compose == Compose::dm);                // ★ the outcome is STILL on the panel
    CHECK(m.state().compose_result == true);
    // ...and the operator's acknowledgement is what retires it, exactly as before.
    m.on_gesture(Gesture::short_press, snap_with(changed, 1200));
    CHECK(m.state().compose == Compose::none);
}

// ★★★ THE PRESS-IN-THE-SAME-TICK ARM: `on_gesture` returns early for `Gesture::none`, so a `double` arriving in the
//     very tick a mutation landed reaches the model through `compose_gesture` and must be CONSUMED, not honoured.
TEST_CASE("ui10-p3-modal: a press arriving in the SAME tick as the mutation closes the modal and sends nothing") {
    const auto cat = gapped_cat({1, 4, 8});
    UiModel m; SendReq req{};
    to_team(m, snap_with(cat));
    m.on_gesture(Gesture::double_press, snap_with(cat));
    CHECK(m.state().compose == Compose::dm);
    auto changed = cat;
    mrfw::preset_slot_put(changed.slot[mrfw::kPresetDmFirst + 0], true, false, "other", 5);
    changed.generation = mrfw::preset_generation_next(changed.generation);
    m.on_gesture(Gesture::double_press, snap_with(changed, 1100));   // the press and the change in one tick
    CHECK(m.state().compose == Compose::none);
    CHECK(m.take_send_request(req) == false);               // ⛔ NOTHING was queued
}

// ★★★★ PIN 11 — the ruled visible word, and the states that carry it.
TEST_CASE("ui10-p3-changed: `PRESET CHANGED` is the exact word, on its own terminal state, for both kinds") {
    CHECK(std::strcmp(kPresetChangedText, "PRESET CHANGED") == 0);
    UiModel a;
    a.on_preset_changed(SendKind::dm, 1000);
    CHECK(a.dm_state() == DmState::preset_changed);
    CHECK(a.dm_state() != DmState::failed);                 // ⛔ never a generic failure (§2's ruling)
    CHECK(a.chan_state() == ChanState::idle);
    UiModel b;
    b.on_preset_changed(SendKind::channel_canned, 1000);
    CHECK(b.chan_state() == ChanState::preset_changed);
    CHECK(b.chan_state() != ChanState::failed);
    CHECK(b.dm_state() == DmState::idle);
    // ⛔ AND THE EMERGENCY CANNOT REACH IT — R-3/§4.1. `send_gate_of` exempts the kind; the model refuses it too.
    UiModel c;
    c.on_preset_changed(SendKind::emergency, 1000);
    CHECK(c.emergency() == Emergency::idle);
    CHECK(c.dm_state() == DmState::idle);
    CHECK(c.chan_state() == ChanState::idle);
    // ★ IT IS TERMINAL: a later `send_aired` for some older flight cannot promote it into a claim of transmission.
    a.on_send_aired(SendKind::dm, 2000);
    CHECK(a.dm_state() == DmState::preset_changed);
    b.on_send_aired(SendKind::channel_canned, 2000);
    CHECK(b.chan_state() == ChanState::preset_changed);
}

// ★★★★ THE FRAME FREEZE — one generation per frame. The model reads the SNAPSHOT the frame froze, so a catalog that
//      moves between two page replays cannot tear the list: the frozen copy is a VALUE, not a view.
TEST_CASE("ui10-p3-freeze: a frozen snapshot keeps its whole list and generation when the catalog moves under it") {
    const auto before = gapped_cat({1, 4, 8});
    const UiSnapshot frozen = snap_with(before);            // ★ what a frame would freeze
    auto after = gapped_cat({2, 3});
    after.generation = mrfw::preset_generation_next(before.generation);
    const UiSnapshot live = snap_with(after, 1100);
    // The two projections disagree in every way that matters...
    CHECK(frozen.preset_dm.n == 3);
    CHECK(live.preset_dm.n == 2);
    CHECK(frozen.preset_generation != live.preset_generation);
    // ...and the frozen one is UNMOVED, row for row, because it holds COPIES rather than pointers.
    CHECK(frozen.preset_dm.row[0].slot == uint8_t(mrfw::kPresetDmFirst + 0));
    CHECK(frozen.preset_dm.row[1].slot == uint8_t(mrfw::kPresetDmFirst + 3));
    CHECK(frozen.preset_dm.row[2].slot == uint8_t(mrfw::kPresetDmFirst + 7));
    CHECK(std::strcmp(frozen.preset_dm.row[1].text, "DM4") == 0);
    // ⇒ every row a frame draws comes from ONE generation. Asserted as the renderer walks it, row by row.
    char b[48];
    for (uint8_t i = 0; i < compose_row_count(frozen.preset_dm, false); ++i) {
        compose_row_line(b, sizeof b, i, frozen.preset_dm, false, false);
        CHECK(std::strstr(b, "DM2") == nullptr);            // ⛔ nothing from the NEW catalog can appear
        CHECK(std::strstr(b, "DM3") == nullptr);
    }
}

// ★★★★ R-1 — **K7's `GRANT KEY` ROW IS BYTE-IDENTICAL THROUGH THE REWORK**: same position (between the enabled DM
//      slots and the back row), same gating (`compose_grant_offered`, untouched), same semantics (`double` opens
//      N5/N6's chain and transmits nothing). ⓘ The landed K7 cases above re-run unmodified; this is the explicit
//      equivalence the reconciliation asks for.
TEST_CASE("ui10-p3-r1: with the COMPILED catalog the DM list is index-for-index the one K7 landed against") {
    const auto s = k7_snap(2);
    CHECK(s.preset_dm.n == 2);                              // the two compiled DM presets
    CHECK(std::strcmp(s.preset_dm.row[0].text, "Are you OK?") == 0);
    CHECK(std::strcmp(s.preset_dm.row[1].text, "I'm OK") == 0);
    for (bool grant : { false, true }) {
        CHECK(compose_row_count(s.preset_dm, grant) == uint8_t(2 + (grant ? 1 : 0) + 1));
        CHECK(compose_row_kind(0, s.preset_dm, grant) == ComposeRow::text);
        CHECK(compose_row_kind(1, s.preset_dm, grant) == ComposeRow::text);
        CHECK(compose_row_kind(2, s.preset_dm, grant) == (grant ? ComposeRow::grant : ComposeRow::back));
    }
    CHECK(compose_row_kind(3, s.preset_dm, true) == ComposeRow::back);
    // ★ THE ROW'S POSITION IS THE LIST'S LENGTH, at EVERY catalog size — that is what "between the slots and the
    //   back row" means once the list is configurable, and it is R-1 stated for the general case.
    for (uint8_t k = 0; k <= mrfw::kPresetPerKind; ++k) {
        ComposeList l{};
        std::initializer_list<uint8_t> all = {1, 2, 3, 4, 5, 6, 7, 8};
        mrnv::UiPresetBlob c{};
        mrfw::preset_defaults(c);
        for (uint8_t i = 0; i < mrfw::kPresetPerKind; ++i)
            mrfw::preset_slot_put(c.slot[mrfw::kPresetDmFirst + i], i < k, false, "x", 1);
        (void)all;
        compose_project(c, mrfw::PresetKind::dm, l);
        CHECK(l.n == k);
        CHECK(compose_row_kind(k, l, /*grant=*/true) == ComposeRow::grant);
        CHECK(compose_row_kind(uint8_t(k + 1), l, /*grant=*/true) == ComposeRow::back);
        CHECK(compose_row_text(k, l, /*grant=*/true) == kInviteGrantKey);
        // ⛔ ...and it is NEVER offered on the channel list, at any size (term 1 of `compose_grant_offered`).
        CHECK(compose_grant_offered(/*dm=*/false, true, true, 0x1234u, 0x9999u) == false);
    }
}

// ★★ THE RESOURCE MEASUREMENT, `offsetof`-proved (spec §5). ⚠ Native alignment hides the BOARD figure (D2).
TEST_CASE("ui10-p3-resources: the list projection costs 328 B of UiSnapshot and ZERO of UiState") {
    CHECK(sizeof(mrui::ComposeSlot) == 20u);               // 18 char + slot + loc, alignof 1, no padding
    CHECK(sizeof(mrui::ComposeList) == 161u);              // 8 x 20 + the count
    CHECK(offsetof(mrui::ComposeSlot, text) == 0u);
    CHECK(offsetof(mrui::ComposeSlot, slot) == 18u);
    CHECK(offsetof(mrui::ComposeSlot, loc)  == 19u);
    // ★ THE PLACEMENT: `member[]` ended at the struct's old 8-aligned END (1008), so the `uint32_t` lands there for
    //   free and the two alignof-1 lists follow it. ⇒ 1008 -> 1336, i.e. exactly 4 + 161 + 161 + 2 bytes of tail pad.
    CHECK(offsetof(mrui::UiSnapshot, preset_generation) == 1008u);
    CHECK(offsetof(mrui::UiSnapshot, preset_dm) == 1012u);
    CHECK(offsetof(mrui::UiSnapshot, preset_ch) == 1173u);
    CHECK(sizeof(mrui::UiSnapshot) == 1336u);
    // ★ AND THE STATE'S SEALED GENERATION COSTS **NOTHING ON THE HOST**: it lands in padding that already existed.
    // ⚠⚠ ⛔ NOT ON THE BOARD, and the difference is D2's warning MEASURED rather than repeated (QG, 2026-08-26,
    //    `xtensa-esp-elf` GCC 13.2 / ILP32 / the heltec_mobile flag set): `sizeof(UiState)` **496 -> 504** and
    //    `sizeof(UiModel)` **904 -> 912** there — the host's 8-byte adapter pointers open a hole the board has not
    //    got. The panel TU carries a whole `UiState` TWICE in its statics (`s_frame_state`, and `s_model`'s own).
    // ⛔ CORRECTED IN PLACE (QG), AND THE WITHDRAWN CLAUSE IS KEPT VISIBLE BECAUSE IT WAS **WRONG**: it read *"the
    //    TU's measured board growth is +344, not this file's host-visible +328"*, which offers a SYMBOL-SIZE sum as
    //    the device's RAM. ★ THE THREE FIGURES ARE THREE DIFFERENT THINGS ([[B246]]):
    //      · SYMBOL-SIZE growth **+344 B** = `s_frame_snap` +328 · `s_frame_state` +8 · `s_model` +8;
    //      · LINKED `heltec_mobile` RAM **218 564 -> 218 900 = +336 B** — the image truth, the only device cost;
    //      · ⇒ **8 B absorbed by existing section/alignment padding** at link time.
    // ⇒ these three lines pin the HOST shape, which is all a native case can see; the SYMBOL figures need the board
    //   ABI compiler, and the RAM figure needs a LINK — i.e. the per-board `RAM_used` diff, which is the board gate's.
    CHECK(offsetof(mrui::UiState, compose_gen) == 8u);
    CHECK(sizeof(mrui::UiState) == 504u);
    CHECK(sizeof(mrui::UiModel) == 928u);
    // ★ `SendReq` gains 4 bytes over the withdrawn `{kind, peer, text_index}` — it is a by-value request, held in
    //   ONE model member and one tick local, so this is 4 bytes of `UiModel` that measured ZERO above.
    CHECK(sizeof(mrui::SendReq) == 8u);
}
