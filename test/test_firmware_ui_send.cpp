// MeshRoute — test_firmware_ui_send.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN — test_airtime.cpp provides main().
// NB: the native build is -fno-exceptions, so doctest's REQUIRE is a HARD COMPILE ERROR (measured: doctest.h:2824
//     "static assertion failed: Exceptions are disabled!"). Every "REQUIRE" the plan wrote is a CHECK here (§B67).
//
// UI-4 (plan Task 4, spec §2.1): the ATTRIBUTION layer. Pushes are node-wide — a console post, a BLE post and a
// canned message all raise `channel_sent` — so an uncorrelated push completes an emergency that was never
// transmitted. That is a FALSE SAFETY CONFIRMATION, and these cases are what prove it cannot happen.
//
// ⚠⚠ EVERY MATCHER HERE CONSUMES (register B70). Never write `CHECK(t.match_x(..) == true); if (!t.match_x(..)) …` —
// that is two calls, the second is false, and the guard silently aborts the case with a green tick (measured on the
// UI-3 cases: 2 assertions instead of 11). ONE call, into a local with a name UNIQUE IN ITS TEST_CASE, then CHECK
// the local and guard on the local. The duplicate-name half of that rule is register B76's first error.
#include "doctest.h"
#include "firmware_ui_send.h"
#include <cstdint>
// ⚠ §B76 ADDENDUM (measured 2026-08-04): the plan's B40 case iterates a BRACED LIST (`for (uint16_t c : {…})`), which
// is a 7th compile error its scratch-TU probe did not report — `error: deducing from brace-enclosed initializer list
// requires '#include <initializer_list>'`. Neither doctest.h nor firmware_ui_model.h drags it in on this toolchain.
#include <initializer_list>
#include <cstring>   // strlen/strncmp — the §B103 recv cases check the exact clamped reply text

using namespace mrui;

// ---------------------------------------------------------------- the plan's ten cases (B76-corrected)

TEST_CASE("ui-send: an unrelated channel_sent cannot complete the emergency") {
    SendTracker t; SendOutcome o{};
    t.submit(SendKind::emergency, 0, 0, 1000); t.accept(/*ctr=*/77, 1010);
    CHECK(t.match_channel_sent(/*ctr=*/12, /*relayed=*/true, o) == false);   // someone else's post
    const bool mine = t.match_channel_sent(/*ctr=*/77, /*relayed=*/true, o);
    CHECK(mine == true);
    if (!mine) return;
    CHECK(o.kind == SendOutcome::Kind::channel_relayed);
}

TEST_CASE("ui-send: a blocked DM cannot block the emergency") {
    SendTracker t; SendOutcome o{};
    t.submit(SendKind::emergency, 0, 0, 1000); t.accept(77, 1010);
    CHECK(t.match_blocked(/*blocked_channel=*/false, 5000, 1020, o) == false);
    CHECK(t.match_blocked(/*blocked_channel=*/true,  5000, 1020, o) == true);
}

TEST_CASE("ui-send: a blocked event outside the outcome window is ignored") {
    SendTracker t; SendOutcome o{};
    t.submit(SendKind::emergency, 0, 0, 1000); t.accept(77, 1010);
    CHECK(t.match_blocked(true, 5000, 1010 + kOutcomeWindowMs + 1, o) == false);
}

TEST_CASE("ui-send: a DM outcome must match ctr AND peer") {
    SendTracker t; SendOutcome o{};
    t.submit(SendKind::dm, /*peer=*/174, 0, 1000); t.accept(/*ctr=*/900, 1010);
    // §B76: the 4th argument is a REASON, not a bool — `bool -> enum class` is not implicit, and the plan's older
    // `/*no_pubkey=*/true` spelling is what made this block uncompilable.
    CHECK(t.match_dm(900, /*dst=*/99,  true, FailReason::none, o) == false);   // right ctr, wrong peer
    CHECK(t.match_dm(901, /*dst=*/174, true, FailReason::none, o) == false);   // right peer, wrong ctr
    const bool matched = t.match_dm(900, /*dst=*/174, true, FailReason::none, o);   // acked => the reason is `none`
    CHECK(matched == true);
    if (!matched) return;
    CHECK(o.kind == SendOutcome::Kind::dm_acked);
}

TEST_CASE("ui-send: no_pubkey maps to dm_no_key") {
    SendTracker t; SendOutcome o{};
    t.submit(SendKind::dm, 174, 0, 1000); t.accept(900, 1010);
    const bool ok = t.match_dm(900, 174, false, FailReason::no_pubkey, o);   // §B70: ONE call — the matcher CONSUMES
    CHECK(ok == true);
    if (!ok) return;
    CHECK(o.kind == SendOutcome::Kind::dm_no_key);
    CHECK(t.idle() == true);   // no_key is terminal: nothing may follow it
}

TEST_CASE("ui-send: a refused submit leaves nothing to match") {
    SendTracker t; SendOutcome o{};
    t.submit(SendKind::emergency, 0, 0, 1000); t.refuse();
    CHECK(t.idle() == true);
    CHECK(t.match_channel_sent(77, true, o) == false);
}

TEST_CASE("ui-send: B39 — a ctr==0 result awaits its outcome and never claims a channel_sent") {
    SendTracker t; SendOutcome o{};
    t.submit(SendKind::emergency, 0, 0, 1000); t.awaiting_outcome(1010);
    CHECK(t.match_channel_sent(0, true, o) == false);              // ctr 0 is a sentinel, not a handle
    CHECK(t.match_blocked(true, 5000, 1020, o) == true);
}

// ★★ REPLACES the plan's "a channel seal failure is terminal" case, which tested `match_channel_failed` — DELETED by
// owner ruling (B80). The post-mint seal failure is NOT attributable (the core: "the reason arrives asynchronously and
// correlates with nothing"), so the requirement it really carries is *not a stuck `SENDING...`*, and that is now met by
// the bounded expiry rather than by a matcher that guesses.
TEST_CASE("ui-send: a channel seal failure still escapes SENDING..., via the bounded expiry") {
    SendTracker t; SendOutcome o{};
    t.submit(SendKind::emergency, 0, 0, 1000); t.awaiting_outcome(1010);   // on_command answered {queued, ctr = 0}
    CHECK(t.idle() == false);                                             // still tracked, nothing decided yet
    const bool ok = t.tick(1010 + kOutcomeWindowMs + 1, o);
    CHECK(ok == true);
    if (!ok) return;
    CHECK(o.kind == SendOutcome::Kind::channel_remote_mint);
    CHECK(t.idle() == true);
}

TEST_CASE("ui-send: B40 — full-width counters correlate across the 8-bit boundary") {
    for (uint16_t c : {uint16_t(255), uint16_t(256), uint16_t(257), uint16_t(65535)}) {
        SendTracker t; SendOutcome o{};
        t.submit(SendKind::emergency, 0, 0, 1000); t.accept(c, 1010);
        CHECK(t.match_channel_sent(uint16_t(c & 0xff), true, o) == (c < 256));   // low-byte collider must NOT match
        SendTracker t2; SendOutcome o2{};
        t2.submit(SendKind::emergency, 0, 0, 1000); t2.accept(c, 1010);
        CHECK(t2.match_channel_sent(c, true, o2) == true);
    }
}

TEST_CASE("ui-send: e2e_ack_timeout yields NO CONFIRM and a late ack upgrades it") {
    SendTracker t; SendOutcome o{};
    t.submit(SendKind::dm, 174, 0, 1000); t.accept(900, 1010);
    // §B76: two `const bool ok` in ONE TEST_CASE is a redeclaration, not a shadow — distinct names per block.
    const bool ok_timeout = t.match_dm(900, 174, false, FailReason::e2e_ack_timeout, o);   // §B70: ONE call
    CHECK(ok_timeout == true);
    if (!ok_timeout) return;
    CHECK(o.kind == SendOutcome::Kind::dm_timeout);
    const bool ok_lateack = t.match_dm(900, 174, true, FailReason::none, o);               // §B70: ONE call
    CHECK(ok_lateack == true);
    if (!ok_lateack) return;
    CHECK(o.kind == SendOutcome::Kind::dm_acked);
    CHECK(t.idle() == true);   // the upgrade closes the transaction; nothing further may match
}

// ---------------------------------------------------------------- UI-4 QA additions
// Each of these pins a behaviour the plan's ten cases do not reach, and each was chosen because reverting THAT
// behaviour alone turns it red (the revert-probe table is in simulation/BASELINE.md's UI-4 note).

// ★★ THE MISSING PRODUCER. `SendOutcome::channel_remote_mint()` (§B68, the EIGHTH kind, added by UI-3) had NO caller
// anywhere in the tree: the plan's tracker emits eight of the nine kinds and never this one. Its producer is the
// window EXPIRING with no failure push — which is exactly what B39's producer (3) does, MEASURED at node.cpp:1631-1634:
// a registered mobile's delegated GLOBAL post "emits no CHANNEL-level push at all". Without this the alarm sits on
// `SENDING...` for ever, which is §B72's defect one level up.
TEST_CASE("ui-send: UI-4 — an awaiting send with no push at all closes as channel_remote_mint, not a stuck SENDING") {
    SendTracker t; SendOutcome o{};
    t.submit(SendKind::channel_canned, 0, 0, 1000); t.awaiting_outcome(1010);
    CHECK(t.tick(1010 + kOutcomeWindowMs, o) == false);          // still inside the window: nothing is decided yet
    CHECK(t.idle() == false);
    const bool expired = t.tick(1010 + kOutcomeWindowMs + 1, o);
    CHECK(expired == true);
    if (!expired) return;
    CHECK(o.kind == SendOutcome::Kind::channel_remote_mint);     // a SUCCESS shape — never a failure (§B68)
    CHECK(t.idle() == true);                                     // and the slot is released, so the UI can send again
    CHECK(t.tick(1010 + kOutcomeWindowMs + 2, o) == false);       // exactly once
}

// ★ THE OTHER HALF OF THE SAME RULE, and it is the one a naive "apply the window everywhere" fix breaks. A team
// post's channel_sent legitimately arrives up to channel_reoffer_team_max_retries(3) x
// (channel_reoffer_delay_ms 10000 + jitter 2000) ~= 36 s after acceptance, so the 8 s window MUST NOT bound an
// exactly-correlated outcome. `accepted` never expires; only `awaiting` does.
TEST_CASE("ui-send: UI-4 — an ACCEPTED send never expires, because a team channel_sent arrives ~36s later") {
    SendTracker t; SendOutcome o{};
    t.submit(SendKind::emergency, 0, 0, 1000); t.accept(77, 1010);
    CHECK(t.tick(1010 + 40000, o) == false);
    CHECK(t.idle() == false);
    const bool late = t.match_channel_sent(77, /*relayed=*/true, o);   // 40 s later, and still exactly ours
    CHECK(late == true);
    if (!late) return;
    CHECK(o.kind == SendOutcome::Kind::channel_relayed);
}

// ★★ `accept(0, …)` and `awaiting_outcome()` STATE THE SAME FACT — ctr == 0 means no local handle exists. The plan
// documented "caller must not call accept with 0" and left it unenforced, so a caller slip made `_ctr == 0` a live
// handle: an unrelated ctr-0 channel_sent would then match and manufacture PICKED UP. Normalising costs nothing and
// makes the hazard unrepresentable rather than forbidden.
TEST_CASE("ui-send: UI-4 — accept(ctr=0) normalises to awaiting, so a ctr-0 push can never be a handle") {
    SendTracker t; SendOutcome o{};
    t.submit(SendKind::emergency, 0, 0, 1000); t.accept(/*ctr=*/0, 1010);
    CHECK(t.match_channel_sent(0, /*relayed=*/true, o) == false);   // would have been a FALSE PICKED UP
    CHECK(t.idle() == false);                                       // still tracked — the outcome is simply not known yet
    const bool blocked0 = t.match_blocked(true, 5000, 1020, o);
    CHECK(blocked0 == true);                                        // and the awaiting-state matchers still work
    if (!blocked0) return;
    CHECK(o.kind == SendOutcome::Kind::blocked);
}

// ★ The real outcome of an `awaiting` alarm still lands. Kept from the B80 case, minus the deleted matcher: there is
// now NO tracker entry point through which an async `send_failed` of any shape can reach the model.
TEST_CASE("ui-send: UI-4 — an awaiting emergency still receives its real blocked outcome") {
    SendTracker t; SendOutcome o{};
    t.submit(SendKind::emergency, 0, 0, 1000); t.awaiting_outcome(1010);
    CHECK(t.idle() == false);
    const bool blocked = t.match_blocked(true, 5000, 1030, o);
    CHECK(blocked == true);
    if (!blocked) return;
    CHECK(o.kind == SendOutcome::Kind::blocked);
    CHECK(o.next_ms == 5000);
}

// ★ `late_ack` exists ONLY to upgrade NO CONFIRM -> DELIVERED (spec §3.4.1). Letting any other outcome fire from it
// lets a second, later send_failed DOWNGRADE an already-reported `not_confirmed` to a generic `failed` — losing the
// one distinction command.h:254 insists on ("delivery was never CONFIRMED, NOT that it failed").
TEST_CASE("ui-send: UI-4 — in late_ack only an ACK matches; a stray failure cannot downgrade NO CONFIRM") {
    SendTracker t; SendOutcome o{};
    t.submit(SendKind::dm, 174, 0, 1000); t.accept(900, 1010);
    const bool timed_out = t.match_dm(900, 174, false, FailReason::e2e_ack_timeout, o);
    CHECK(timed_out == true);
    if (!timed_out) return;
    CHECK(o.kind == SendOutcome::Kind::dm_timeout);
    CHECK(t.match_dm(900, 174, false, FailReason::no_ack, o) == false);              // no downgrade
    CHECK(t.match_dm(900, 174, false, FailReason::e2e_ack_timeout, o) == false);     // and no duplicate report
    const bool upgraded = t.match_dm(900, 174, true, FailReason::none, o);           // the ONE thing that may follow
    CHECK(upgraded == true);
    if (!upgraded) return;
    CHECK(o.kind == SendOutcome::Kind::dm_acked);
}

// ★ The late-ack window is bounded by the CALLER, and that obligation needs a named mechanism rather than a comment.
// `close()` is it: spec §3.4.1 scopes the upgrade to "while the sub-view is still showing", and UI-6/UI-7 owns that
// lifetime. Without a close the slot never returns to idle and the UI can never send another DM.
TEST_CASE("ui-send: UI-4 — close() releases a late_ack slot, which is how the sub-view bounds the upgrade") {
    SendTracker t; SendOutcome o{};
    t.submit(SendKind::dm, 174, 0, 1000); t.accept(900, 1010);
    const bool timed_out = t.match_dm(900, 174, false, FailReason::e2e_ack_timeout, o);
    CHECK(timed_out == true);
    if (!timed_out) return;
    CHECK(t.idle() == false);            // retained: a late ack may still upgrade it
    t.close();
    CHECK(t.idle() == true);
    CHECK(t.match_dm(900, 174, true, FailReason::none, o) == false);   // after the close, nothing may match
}

// ★ The two slots are told apart by KIND before anything else (spec §2.1's two-tracker rule). A DM slot must be deaf
// to every channel push and a channel slot deaf to every DM push, or the emergency and the normal slot both claim
// the same event.
TEST_CASE("ui-send: UI-4 — a DM slot is deaf to channel pushes and a channel slot to DM pushes") {
    SendTracker dm; SendOutcome o{};
    dm.submit(SendKind::dm, 174, 0, 1000); dm.accept(900, 1010);
    CHECK(dm.match_channel_sent(900, true, o) == false);
    CHECK(dm.match_blocked(true, 5000, 1020, o) == false);
    CHECK(dm.tick(1010 + kOutcomeWindowMs + 1, o) == false);   // an accepted DM is not a remote mint either
    CHECK(dm.idle() == false);

    SendTracker ch; SendOutcome o2{};
    ch.submit(SendKind::channel_canned, 0, 7, 1000); ch.accept(900, 1010);
    CHECK(ch.match_dm(900, 0, true, FailReason::none, o2) == false);
    CHECK(ch.idle() == false);
}

// ★ A DM can reach `awaiting` only by a route the UI never takes (a HASH-addressed send parked behind an H resolve,
// node_hashlocate.cpp; spec §3.4 sends by team_local_id). Guarded anyway, because `channel_remote_mint` for a DM
// would be a type error: the slot is released and NO outcome is invented.
TEST_CASE("ui-send: UI-4 — an awaiting DM releases its slot without inventing a channel outcome") {
    SendTracker t; SendOutcome o{};
    t.submit(SendKind::dm, 174, 0, 1000); t.awaiting_outcome(1010);
    CHECK(t.tick(1010 + kOutcomeWindowMs + 1, o) == false);   // nothing to report...
    CHECK(t.idle() == true);                                  // ...but the slot must not leak
}

// ★ The outcome window is an unsigned difference, so it survives the millis() wrap the rest of the model is careful
// about. `now < _accept_ms` after a wrap must still read as "50 ms later", not "4.29e9 ms later".
TEST_CASE("ui-send: UI-4 — the outcome window is wrap-safe across millis()") {
    SendTracker t; SendOutcome o{};
    t.submit(SendKind::emergency, 0, 0, 0xFFFFF000u); t.awaiting_outcome(0xFFFFF000u);
    const bool blocked = t.match_blocked(true, 5000, /*now=*/0x00000100u, o);   // 0x1100 ms later, inside the window
    CHECK(blocked == true);
    if (!blocked) return;
    CHECK(o.kind == SendOutcome::Kind::blocked);

    SendTracker t2; SendOutcome o2{};
    t2.submit(SendKind::emergency, 0, 0, 0xFFFFF000u); t2.awaiting_outcome(0xFFFFF000u);
    const bool expired = t2.tick(0xFFFFF000u + kOutcomeWindowMs + 1, o2);       // and the expiry wraps too
    CHECK(expired == true);
    if (!expired) return;
    CHECK(o2.kind == SendOutcome::Kind::channel_remote_mint);
}

// ★ A CONFIRMED premise, pinned so nobody "tightens" it: `accepted` + a genuine `send_blocked` is REACHABLE, so
// match_blocked must accept both states. On a `-t -g` post the team copy mints a ctr and stamps
// _last_channel_origin_ms (node_channel.cpp:752), which then blocks the global copy on channel_min_interval_ms
// (node_channel.cpp:633) — a real send_blocked while ctr != 0. Restricting match_blocked to `awaiting` would drop it.
TEST_CASE("ui-send: UI-4 — match_blocked accepts an ACCEPTED slot, because -t -g really blocks its second copy") {
    SendTracker t; SendOutcome o{};
    t.submit(SendKind::channel_canned, 0, 0, 1000); t.accept(/*team copy's ctr=*/77, 1010);
    const bool blocked = t.match_blocked(true, /*next_ms=*/9500, 1020, o);
    CHECK(blocked == true);
    if (!blocked) return;
    CHECK(o.kind == SendOutcome::Kind::blocked);
    CHECK(o.next_ms == 9500);
}

// ★ Every failure kind carries its reason verbatim (§B73, spec :147). A table, so a new reason cannot be added to
// the pass-through without someone deciding what the panel says.
TEST_CASE("ui-send: UI-4 — a DM failure threads its exact reason, and only the two special ones get their own kind") {
    struct Row { FailReason in; SendOutcome::Kind kind; } rows[] = {
        { FailReason::no_pubkey,       SendOutcome::Kind::dm_no_key  },
        { FailReason::e2e_ack_timeout, SendOutcome::Kind::dm_timeout },
        { FailReason::no_route,        SendOutcome::Kind::dm_failed  },
        { FailReason::queue_full,      SendOutcome::Kind::dm_failed  },
        { FailReason::unsealable,      SendOutcome::Kind::dm_failed  },
        { FailReason::mobile_no_home,  SendOutcome::Kind::dm_failed  },
        { FailReason::no_ack,          SendOutcome::Kind::dm_failed  },
        { FailReason::reprovisioned,   SendOutcome::Kind::dm_failed  },
    };
    for (const Row& r : rows) {
        SendTracker t; SendOutcome o{};
        t.submit(SendKind::dm, 174, 0, 1000); t.accept(900, 1010);
        const bool matched = t.match_dm(900, 174, false, r.in, o);
        CHECK(matched == true);
        if (!matched) continue;
        CHECK(o.kind == r.kind);
        CHECK(o.reason == r.in);   // ★ verbatim on EVERY row, including the two with their own kind
    }
}

// ★ The tracker is the only thing between a node-wide push and the emergency machine, so drive the two together and
// prove the model cannot move on someone else's traffic. Spec §12: "native tests must interleave unrelated channel
// and DM outcomes and prove the emergency state cannot move".
TEST_CASE("ui-send: UI-4 — interleaved unrelated traffic cannot move the emergency through the tracker") {
    UiModel m; SendTracker emg; SendTracker normal; SendReq req{}; SendOutcome o{};
    UiSnapshot s{}; s.now_ms = 1000; s.team_shown = 1; s.team[0].id = 174;

    m.on_gesture(Gesture::long_arm, s);
    s.now_ms = 4500; m.on_gesture(Gesture::long_fire, s);
    const bool got = m.take_send_request(req);   // §B70: ONE call — take_send_request DRAINS
    CHECK(got == true);
    if (!got) return;
    CHECK(req.kind == SendKind::emergency);
    emg.submit(SendKind::emergency, 0, 0, 4500);
    emg.accept(/*ctr=*/1234, 5000);
    m.on_send_accepted(SendKind::emergency, 5000);
    CHECK(m.emergency() == Emergency::firing);

    // A console post completes, a BLE post completes, a DM ack lands, a DM is blocked — none of them ours.
    CHECK(emg.match_channel_sent(/*ctr=*/1, true, o) == false);
    CHECK(emg.match_channel_sent(/*ctr=*/0xD2, true, o) == false);        // 1234 & 0xff — the B40 collider
    CHECK(emg.match_dm(1234, 174, true, FailReason::none, o) == false);   // right ctr, wrong PLANE of push
    CHECK(emg.match_blocked(/*blocked_channel=*/false, 3000, 5100, o) == false);
    CHECK(m.emergency() == Emergency::firing);                            // ★ nothing reached the model

    // Ours, and only now does the model move.
    const bool ours = emg.match_channel_sent(1234, /*relayed=*/true, o);
    CHECK(ours == true);
    if (!ours) return;
    m.on_outcome(o, 5200);
    CHECK(m.emergency() == Emergency::picked_up);
    CHECK(m.attempts() == 1);
}

// ---------------------------------------------------------------- UI-4 integration regressions (owner-ruled 2026-08-04)
// ★★ These four span TRACKER + MODEL, so no unit test of either alone can reach them — and they exist because the
// first UI-4 fix traded a permanent `SENDING...` for an INFINITE RETRY LOOP. `UiModel::_tries` increments ONLY in
// `on_send_accepted` ("ACCEPTED transmissions, never requests"), and a `ctr == 0` send never gets there, so an
// unattributable expiry that merely re-queues can never satisfy `_tries >= kEmgMaxTries`. ⇒ THE RULE: an expired
// unattributable EMERGENCY consumes ONE bounded attempt BEFORE its `channel_remote_mint` is processed.

// Drive `n` ctr-0 emergency expiries. `via_glue` selects the SHIPPED wiring vs the defect, so the requirement and its
// negative control share one body and cannot drift apart.
//
// ⚠⚠ §UI-6 CHANGED THIS HELPER, AND THE REASON IS THE POINT. v1 HAND-REPLICATED the wiring
// (`emg.tick(); on_send_accepted(); on_outcome();`) — so it pinned the RULE while being STRUCTURALLY BLIND to the
// shipped `mr_ui_tick`. A coder who wired the tick in the wrong order, which the plan records happening TWICE, would
// have seen these four cases stay green. That is the "could this check have failed?" test, and v1 failed it.
// ⇒ the ruled path now calls `mrui::ui_pump_trackers` — the ACTUAL function firmware_ui.cpp calls, and its only caller.
static void run_ctr0_expiries(UiModel& m, int n, bool via_glue) {
    SendReq req{}; SendOutcome o{};
    SendTracker normal;                                  // idle throughout — the glue must not need it to be live
    for (int i = 1; i <= n; ++i) {
        const bool got = m.take_send_request(req);      // §B70: ONE call — take_send_request DRAINS
        CHECK(got == true);
        if (!got) return;
        CHECK(req.kind == SendKind::emergency);
        SendTracker emg;                                 // UI-6 re-submits the slot for every retry
        const uint32_t t_sub = 5000u * uint32_t(i);
        emg.submit(SendKind::emergency, 0, 0, t_sub);
        emg.awaiting_outcome(t_sub);                     // on_command answered {queued, ctr = 0} — B39 producer (2)/(3)
        const uint32_t t_exp = t_sub + kOutcomeWindowMs + 1;
        if (via_glue) {
            ui_pump_trackers(emg, normal, m, t_exp);     // ★ THE SHIPPED GLUE, verbatim
            CHECK(emg.idle() == true);                   // the slot was released, not leaked
        } else {
            const bool expired = emg.tick(t_exp, o);     // the DEFECT: the expiry consumes no attempt
            CHECK(expired == true);
            if (!expired) return;
            CHECK(o.kind == SendOutcome::Kind::channel_remote_mint);
            m.on_outcome(o, t_exp);
        }
    }
}

static UiModel armed_and_fired() {
    UiModel m; UiSnapshot s{}; s.now_ms = 1000;
    m.on_gesture(Gesture::long_arm, s);
    s.now_ms = 4500; m.on_gesture(Gesture::long_fire, s);
    CHECK(m.emergency() == Emergency::firing);
    return m;
}

// (2) + (3) + (4) — one sequence, so one case.
TEST_CASE("ui-send: UI-4 int — each ctr==0 expiry consumes ONE attempt, three end in sticky NOT HEARD, no fourth") {
    UiModel m = armed_and_fired();
    SendReq req{};
    run_ctr0_expiries(m, 3, /*via_glue=*/true);
    CHECK(m.attempts() == 3);                        // (2) 1, 2, 3 — NOT 0
    CHECK(m.emergency() == Emergency::not_heard);    // (3) terminal, and sticky
    CHECK(m.take_send_request(req) == false);        // (4) no fourth request is queued
    // sticky: a later tick must not resurrect it, and the budget stays spent
    UiSnapshot s{}; s.now_ms = 5000u * 4;
    m.on_tick(s);
    CHECK(m.emergency() == Emergency::not_heard);
    CHECK(m.take_send_request(req) == false);
}

// ★ NEGATIVE CONTROL — it asserts the DEFECT, deliberately, so that "the suite is green" and "the retry path is
// bounded" cannot be confused for one fact again. If a future change makes `_tries` move on some other path, THIS case
// turns red and forces the rule above to be re-read rather than silently satisfied.
TEST_CASE("ui-send: UI-4 int — NEGATIVE CONTROL: without the consumption the retry never terminates") {
    UiModel m = armed_and_fired();
    SendReq req{};
    run_ctr0_expiries(m, 3, /*via_glue=*/false);
    CHECK(m.attempts() == 0);                       // the three-alarm budget was never touched
    CHECK(m.emergency() == Emergency::firing);      // still firing after 3 expiries...
    CHECK(m.take_send_request(req) == true);        // ...and a FOURTH request is queued: unbounded
}

// (1) The colliding `{dst = 0, unsealable}` shape. ★ `dst == 0` does NOT imply "channel" — that converse error was the
// first fix's, and six unrelated operations emit this exact shape (`send_layer`'s unsealable arms at
// node_mac.cpp:220/452/473/561/579, plus node_mac.cpp:59/111). The guarantee is now STRUCTURAL: the tracker has no
// entry point that accepts an async `send_failed`, so it can never build a `SendOutcome::channel_failed` and the model
// can never be driven to `Emergency::failed` by one. Every remaining matcher is offered the push and refuses it.
TEST_CASE("ui-send: UI-4 int — a colliding send_layer {dst=0, unsealable} cannot make the emergency FAILED") {
    UiModel m = armed_and_fired();
    SendTracker emg; SendReq req{}; SendOutcome o{};
    const bool got = m.take_send_request(req);
    CHECK(got == true);
    if (!got) return;
    emg.submit(SendKind::emergency, 0, 0, 4500);
    emg.awaiting_outcome(5000);                     // the alarm is awaiting, i.e. maximally exposed

    // The push is `send_failed{dst = 0, ctr = 0, reason = unsealable}`. No matcher accepts it.
    CHECK(emg.match_channel_sent(/*ctr=*/0, /*relayed=*/false, o) == false);          // ctr 0 is a sentinel
    CHECK(emg.match_dm(/*ctr=*/0, /*dst=*/0, false, FailReason::unsealable, o) == false);   // wrong slot kind
    CHECK(emg.match_blocked(/*blocked_channel=*/false, 0, 5010, o) == false);         // not a block
    CHECK(emg.idle() == false);                                                      // nothing consumed the slot
    CHECK(m.emergency() == Emergency::firing);                                       // ★ and the model never moved
    CHECK(m.emergency() != Emergency::failed);

    // The alarm's own bounded expiry is still what closes it, with an attempt spent.
    const bool expired = emg.tick(5000 + kOutcomeWindowMs + 1, o);
    CHECK(expired == true);
    if (!expired) return;
    m.on_send_accepted(SendKind::emergency, 5000 + kOutcomeWindowMs + 1);
    m.on_outcome(o, 5000 + kOutcomeWindowMs + 1);
    CHECK(m.attempts() == 1);
    CHECK(m.emergency() == Emergency::firing);      // one of three spent, still trying — never `failed`
}

// ============================================================ §UI-6 GLUE — the wiring itself, not a replica of it
// ★★★ These drive `mrui::ui_pump_trackers` / `mrui::ui_route_send_push` — the ONLY two functions
// `src/firmware_ui.cpp`'s `mr_ui_tick` / `mr_ui_on_push` call. Before UI-6 this wiring lived inline in a `.cpp` that
// NEITHER the native suite nor the simulator compiles, so the three caller obligations the tracker documents at length
// were prose. The plan records the wiring being got wrong TWICE. Now a revert turns this suite red.

static MESHROUTE_NS::Push push_of(MESHROUTE_NS::PushKind k) {
    MESHROUTE_NS::Push pu{}; pu.kind = k; return pu;
}
// A live alarm with one accepted transmission, its handle held by `emg`.
static UiModel alarm_accepted(SendTracker& emg, uint16_t ctr) {
    UiModel m; SendReq req{};
    UiSnapshot s{}; s.now_ms = 1000; m.on_gesture(Gesture::long_arm, s);
    s.now_ms = 4500; m.on_gesture(Gesture::long_fire, s);
    const bool got = m.take_send_request(req);
    CHECK(got == true);
    emg.submit(SendKind::emergency, 0, 0, 4500);
    emg.accept(ctr, 5000);
    m.on_send_accepted(SendKind::emergency, 5000);
    CHECK(m.emergency() == Emergency::firing);
    return m;
}

// ★★★ §B84 BLOCKER 2, and it had NO test before UI-6. `SendTracker::tick()` yields a CHANNEL outcome, and
// `on_outcome` lets a channel outcome move any LIVE alarm — so routing the NORMAL tracker's expiry into `on_outcome`
// lets an abandoned canned post alter a live emergency. The glue must DRAIN that slot and route NOTHING.
// ⚠⚠ THIS CASE FAILED ITS OWN NEGATIVE CONTROL ON FIRST WRITING, and the record matters more than the fix. v1 asserted
//    only `emergency() == firing` and `attempts() == 1` — and routing the normal expiry into `on_outcome` (the exact
//    defect B84 names) left BOTH of those true: with `_tries` 1 of 3, `on_outcome`'s channel arm re-enters `firing` and
//    QUEUES ANOTHER ALARM. So the visible harm is a PHANTOM RE-TRANSMISSION of the distress call, triggered by an
//    unrelated canned post — and v1 was blind to precisely that. ⇒ assert the QUEUE, and add the budget-spent variant
//    where the same revert also fabricates a terminal NOT HEARD.
TEST_CASE("ui-glue: the NORMAL tracker's expiry drains its slot and CANNOT touch a live alarm") {
    SendTracker emg, normal;
    UiModel m = alarm_accepted(emg, /*ctr=*/77);          // `accepted` NEVER expires — only `awaiting` does
    normal.submit(SendKind::channel_canned, 0, 0, 5100);
    normal.awaiting_outcome(5100);                        // a canned post with no local handle
    const uint32_t t_exp = 5100 + kOutcomeWindowMs + 1;
    ui_pump_trackers(emg, normal, m, t_exp);
    CHECK(normal.idle()          == true);                // ★ drained: the slot does not leak (§B79)
    CHECK(m.emergency()          == Emergency::firing);   // ★ and the alarm did NOT move
    CHECK(m.attempts()           == 1);                   // ...nor did it spend one of its three transmissions
    CHECK(emg.idle()             == false);               // the alarm still owns its handle, still waiting
    // ★★ THE ASSERTION THAT ACTUALLY DISCRIMINATES: no phantom alarm was queued by somebody else's expiry.
    CHECK(m.emergency_pending()  == false);
    SendReq spurious{};
    const bool queued = m.take_send_request(spurious);
    CHECK(queued == false);
}

// The same revert, against an alarm whose three-transmission budget is ALREADY SPENT: `on_outcome` would then take the
// `_tries >= kEmgMaxTries` arm and manufacture a TERMINAL `NOT HEARD` — telling the hiker the alarm was not heard on the
// strength of an unrelated canned post's missing push.
TEST_CASE("ui-glue: a canned expiry cannot fabricate a terminal NOT HEARD on a budget-spent alarm") {
    SendTracker emg, normal;
    UiModel m = alarm_accepted(emg, /*ctr=*/77);
    m.on_send_accepted(SendKind::emergency, 5100);
    m.on_send_accepted(SendKind::emergency, 5200);        // three accepted transmissions, still awaiting evidence
    CHECK(m.attempts()  == kEmgMaxTries);
    CHECK(m.emergency() == Emergency::firing);
    normal.submit(SendKind::channel_canned, 0, 0, 5300);
    normal.awaiting_outcome(5300);
    ui_pump_trackers(emg, normal, m, 5300 + kOutcomeWindowMs + 1);
    CHECK(normal.idle() == true);
    CHECK(m.emergency() == Emergency::firing);            // ★ NOT not_heard: the alarm's own outcome has not arrived
    CHECK(m.attempts()  == kEmgMaxTries);
}

// The other half: the EMERGENCY slot's own expiry DOES move the model, and consumes an attempt first.
TEST_CASE("ui-glue: the EMERGENCY tracker's expiry consumes one attempt BEFORE its outcome lands") {
    SendTracker emg, normal;
    UiModel m; SendReq req{};
    UiSnapshot s{}; s.now_ms = 1000; m.on_gesture(Gesture::long_arm, s);
    s.now_ms = 4500; m.on_gesture(Gesture::long_fire, s);
    const bool got = m.take_send_request(req);
    CHECK(got == true);
    emg.submit(SendKind::emergency, 0, 0, 4500);
    emg.awaiting_outcome(5000);                           // {queued, ctr = 0}: no local handle
    CHECK(m.attempts() == 0);
    ui_pump_trackers(emg, normal, m, 5000 + kOutcomeWindowMs + 1);
    CHECK(m.attempts()  == 1);                            // ★ the ordering: consumed, then the outcome re-queued
    CHECK(emg.idle()    == true);
    CHECK(m.emergency() == Emergency::firing);            // one of three spent, still trying — never `failed`
    const bool requeued = m.take_send_request(req);
    CHECK(requeued == true);
}

TEST_CASE("ui-glue: pumping idle trackers is inert") {
    SendTracker emg, normal;
    // ⓘ 2026-08-05: a dead `UiSnapshot s{}; s.now_ms = 1000;` lived here and drew `-Wunused-but-set-variable`. It was
    //   invisible until the §R1 slice touched this TU and PlatformIO recompiled it — object caching is why a
    //   pre-existing warning can surface on an unrelated change. `ui_pump_trackers` takes a raw `now_ms`, not a snapshot.
    UiModel m;
    for (uint32_t t = 1000; t < 200000; t += 5000) ui_pump_trackers(emg, normal, m, t);
    CHECK(m.emergency() == Emergency::idle);
    CHECK(m.attempts()  == 0);
}

// ★★ §B84's colliding shape, now through the REAL router rather than by calling each matcher by hand. Six unrelated
// operations emit `send_failed{dst = 0, ctr = 0}`, and `dst == 0` does NOT mean "channel" — so the router must have no
// path from this push to `Emergency::failed`. That guarantee is STRUCTURAL: no matcher accepts it.
TEST_CASE("ui-glue: an unattributable send_failed{dst=0, unsealable} cannot end the alarm") {
    SendTracker emg, normal;
    UiModel m = alarm_accepted(emg, /*ctr=*/77);
    MESHROUTE_NS::Push pu = push_of(MESHROUTE_NS::PushKind::send_failed);
    pu.dst = 0; pu.ctr = 0; pu.reason = FailReason::unsealable;
    const bool claimed = ui_route_send_push(emg, normal, m, pu, 5200);
    CHECK(claimed       == false);                        // ignored, which is the whole point of the tracker
    CHECK(m.emergency() == Emergency::firing);
    CHECK(m.emergency() != Emergency::failed);
    CHECK(emg.idle()    == false);                        // nothing consumed the alarm's slot
}

TEST_CASE("ui-glue: channel_sent routes by ctr — ours becomes PICKED UP, a stranger's is ignored") {
    SendTracker emg_a, normal_a;
    UiModel a = alarm_accepted(emg_a, /*ctr=*/77);
    MESHROUTE_NS::Push other = push_of(MESHROUTE_NS::PushKind::channel_sent);
    other.ctr = 12; other.relayed = true;                 // somebody else's post, and it claims a relay
    CHECK(ui_route_send_push(emg_a, normal_a, a, other, 5200) == false);
    CHECK(a.emergency() == Emergency::firing);            // ★ no FALSE PICKED UP
    MESHROUTE_NS::Push mine = push_of(MESHROUTE_NS::PushKind::channel_sent);
    mine.ctr = 77; mine.relayed = true;
    CHECK(ui_route_send_push(emg_a, normal_a, a, mine, 5300) == true);
    CHECK(a.emergency() == Emergency::picked_up);

    // relayed=false is the §B38 case: no relay evidence ⇒ never PICKED UP, and the bounded retry re-queues.
    SendTracker emg_b, normal_b;
    UiModel b = alarm_accepted(emg_b, /*ctr=*/88);
    MESHROUTE_NS::Push quiet = push_of(MESHROUTE_NS::PushKind::channel_sent);
    quiet.ctr = 88; quiet.relayed = false;
    CHECK(ui_route_send_push(emg_b, normal_b, b, quiet, 5300) == true);
    CHECK(b.emergency() == Emergency::firing);            // attempt 1 of 3 answered; re-queued, not concluded
}

// ★ The abandoned canned post: its outcome must be CLAIMED by the normal slot (so it can never be offered to the alarm)
//   and must NOT reach the model — there is no canned-only entry point yet, and `on_outcome` would move the alarm.
TEST_CASE("ui-glue: a canned post's outcome is claimed by the normal slot and never routed to the model") {
    SendTracker emg, normal;
    UiModel m = alarm_accepted(emg, /*ctr=*/77);
    normal.submit(SendKind::channel_canned, 0, 0, 5100);
    normal.accept(/*ctr=*/500, 5100);
    MESHROUTE_NS::Push canned = push_of(MESHROUTE_NS::PushKind::channel_sent);
    canned.ctr = 500; canned.relayed = true;              // a REAL relay — of the canned post, not the alarm
    CHECK(ui_route_send_push(emg, normal, m, canned, 5200) == true);
    CHECK(normal.idle() == true);                         // claimed and closed
    CHECK(m.emergency() == Emergency::firing);            // ★ and the alarm is untouched: no borrowed PICKED UP
    CHECK(emg.idle()    == false);
}

// ★ OFFER ORDER. `match_blocked` correlates by WINDOW, not by ctr, so with both slots live the push would match EITHER.
//   Offering the emergency slot first is the only thing that stops a canned post from stealing the alarm's outcome —
//   and, conversely, the alarm's outcome from being spent on the canned post.
TEST_CASE("ui-glue: with BOTH slots live, a send_blocked goes to the EMERGENCY slot") {
    SendTracker emg, normal;
    UiModel m = alarm_accepted(emg, /*ctr=*/77);
    normal.submit(SendKind::channel_canned, 0, 0, 5100);
    normal.accept(/*ctr=*/500, 5100);
    MESHROUTE_NS::Push blk = push_of(MESHROUTE_NS::PushKind::send_blocked);
    blk.blocked_channel = true; blk.next_ms = 7000;
    CHECK(ui_route_send_push(emg, normal, m, blk, 5200) == true);
    CHECK(m.emergency()   == Emergency::blocked);         // the ALARM took it...
    CHECK(m.retry_at_ms() == 5200 + 7000u);               // ...with the deadline anchored on the OUTCOME's arrival
    CHECK(emg.idle()      == true);
    CHECK(normal.idle()   == false);                      // ...and the canned post kept its own transaction
}

// A DM ack is routed to the NORMAL slot; offering the emergency slot first is provably inert (`match_dm` refuses a
// non-DM slot), which is why the glue offers it unconditionally rather than carrying a per-kind exception.
TEST_CASE("ui-glue: a DM ack reaches the DM state and the emergency offer is inert") {
    SendTracker emg, normal;
    UiModel m = alarm_accepted(emg, /*ctr=*/77);
    normal.submit(SendKind::dm, /*peer=*/174, 0, 5100);
    normal.accept(/*ctr=*/900, 5100);
    MESHROUTE_NS::Push ack = push_of(MESHROUTE_NS::PushKind::send_e2e_acked);
    ack.ctr = 900; ack.dst = 174;
    CHECK(ui_route_send_push(emg, normal, m, ack, 5200) == true);
    CHECK(m.dm_state()  == DmState::delivered);
    CHECK(m.emergency() == Emergency::firing);            // the alarm is a channel transaction; the ack is not its
    CHECK(emg.idle()    == false);
}

// The kinds the router deliberately does not handle: firmware_ui.cpp owns those (counters + the §4.4 reply scope).
TEST_CASE("ui-glue: the router claims nothing for the RX kinds") {
    SendTracker emg, normal;
    UiModel m = alarm_accepted(emg, /*ctr=*/77);
    CHECK(ui_route_send_push(emg, normal, m, push_of(MESHROUTE_NS::PushKind::msg_recv),     5200) == false);
    CHECK(ui_route_send_push(emg, normal, m, push_of(MESHROUTE_NS::PushKind::channel_recv), 5200) == false);
    CHECK(ui_route_send_push(emg, normal, m, push_of(MESHROUTE_NS::PushKind::send_acked),   5200) == false);
    CHECK(m.emergency() == Emergency::firing);
}

// ================================================ §B103 / QA finding F4 — the RECEIVE half and the FALSE distress REPLY
// ★★★ THE DEFECT THESE MEASURE, and it was LIVE on bench hardware: `mr_ui_on_push` scoped the distress-reply
// correlation with `pu.channel_id == MR_UI_TEAM_CHANNEL_ID` and nothing else. `Node::ingest_channel_m`
// (node_channel.cpp:211-212) drops a foreign TEAM's post, so that equality looked sufficient — but its own comment
// records that a normal leaf M (`team_id == 0`) "falls through -> ingested by everyone". With
// `MR_UI_TEAM_CHANNEL_ID == 0`, ANY node in radio range posting plaintext on channel 0 — no team, no key, no crypto —
// rendered as "someone answered my distress call".
// ⚠ ASSERT THE SIDE EFFECT, not an end state reached some other way: the harm is the model MOVING to `reply` and
//   naming a stranger, so these check the emergency state, `reply_who` and `reply_text` — the three things the panel
//   actually shows.

// A channel post as it arrives from `ingest_channel_m`. `team` is `Push::team_id` (node_channel.cpp:413).
static MESHROUTE_NS::Push chan_post(uint8_t channel_id, uint32_t team, const char* text) {
    MESHROUTE_NS::Push pu{};
    pu.kind = MESHROUTE_NS::PushKind::channel_recv;
    pu.channel_id = channel_id; pu.team_id = team; pu.origin = 42;
    uint8_t n = 0; while (text[n] && n < 20) { pu.body[n] = uint8_t(text[n]); ++n; }
    pu.body_len = n;
    return pu;
}

TEST_CASE("ui-recv: F4 — a NON-team plaintext post on channel 0 CANNOT become a distress REPLY") {
    SendTracker emg, normal; (void)normal;
    UiModel m = alarm_accepted(emg, /*ctr=*/77);          // an alarm really did go out: _tries == 1
    UiInboxCounters c{};
    // `team_id == 0` is a plain LEAF post — the shape ingest lets through to everyone. `same_team(0)` is false.
    MESHROUTE_NS::Push passerby = chan_post(/*channel_id=*/0, /*team=*/0, "hello everyone");
    const bool owned = ui_route_recv_push(c, m, passerby, /*ui_team_channel_id=*/0,
                                          /*same_team_post=*/false, "stranger", 6000);
    CHECK(owned == true);                                 // the RECV half still owns it: the counter must move...
    CHECK(c.unread_ch() == 1);
    CHECK(c.have_ch   == true);
    // ★★ ...but NOTHING about the alarm may move. These three are the panel's actual claim.
    CHECK(m.emergency() != Emergency::reply);             // ← RED against the shipped guard
    CHECK(m.emergency() == Emergency::firing);
    CHECK(m.reply_who()[0]  == '\0');
    CHECK(m.reply_text()[0] == '\0');
}

// The same push, from a node in a DIFFERENT team. Ingest drops this one today, so it is defence in depth — but the UI
// must not be the layer that relies on that, and `same_team` is what makes it not rely on it.
TEST_CASE("ui-recv: F4 — a FOREIGN team's post on our channel cannot become a REPLY either") {
    SendTracker emg;
    UiModel m = alarm_accepted(emg, /*ctr=*/77);
    UiInboxCounters c{};
    MESHROUTE_NS::Push foreign = chan_post(0, /*team=*/0xDEADBEEF, "we found him");
    (void)ui_route_recv_push(c, m, foreign, 0, /*same_team_post=*/false, "other-team", 6000);
    CHECK(m.emergency()     == Emergency::firing);        // ← RED against the shipped guard
    CHECK(m.reply_text()[0] == '\0');
}

// The POSITIVE half, so the fix cannot be "never show a reply": our own team's post on our own channel still lands.
TEST_CASE("ui-recv: F4 — OUR team's post on OUR channel is still a REPLY, with who and text") {
    SendTracker emg;
    UiModel m = alarm_accepted(emg, /*ctr=*/77);
    UiInboxCounters c{};
    MESHROUTE_NS::Push mate = chan_post(0, /*team=*/0x1234ABCD, "on my way");
    (void)ui_route_recv_push(c, m, mate, 0, /*same_team_post=*/true, "Ana", 6000);
    CHECK(m.emergency() == Emergency::reply);
    CHECK(strncmp(m.reply_who(),  "Ana",       3)  == 0);
    CHECK(strncmp(m.reply_text(), "on my way", 9)  == 0);
    CHECK(c.unread_ch() == 1);
}

// The channel scope is still enforced independently: a teammate posting on a DIFFERENT channel is chatter, not a reply.
TEST_CASE("ui-recv: F4 — a teammate's post on ANOTHER channel is counted but is not a REPLY") {
    SendTracker emg;
    UiModel m = alarm_accepted(emg, /*ctr=*/77);
    UiInboxCounters c{};
    MESHROUTE_NS::Push elsewhere = chan_post(/*channel_id=*/7, 0x1234ABCD, "chatter");
    (void)ui_route_recv_push(c, m, elsewhere, /*ui_team_channel_id=*/0, /*same_team_post=*/true, "Ana", 6000);
    CHECK(c.unread_ch()   == 1);                            // it IS a message...
    CHECK(m.emergency() == Emergency::firing);            // ...but not an answer to the alarm
}

// ★★ REWRITTEN, NOT EXTENDED (§B108 round 2). The old case asserted `c.unread_dm == kUnreadCap` after 1200 arrivals
// and called it "saturates, never wraps" — it PINNED the defect: a counter that stops counting cannot tell a frame
// which arrivals it showed. The contract now under test is the split: the SERIAL keeps counting, and `kUnreadCap`
// binds only the three digits `publish` hands to the bar.
TEST_CASE("ui-recv: the counters, the stamps, and the cap that is DISPLAY-ONLY") {
    UiModel m; UiInboxCounters c{};
    CHECK(c.have_dm == false); CHECK(c.have_ch == false);
    MESHROUTE_NS::Push dm = push_of(MESHROUTE_NS::PushKind::msg_recv);
    CHECK(ui_route_recv_push(c, m, dm, 0, false, "x", 1000) == true);
    CHECK(c.unread_dm() == 1); CHECK(c.have_dm == true); CHECK(c.last_dm_ms == 1000u);
    CHECK(c.unread_ch() == 0); CHECK(c.have_ch == false);   // a DM never touches the channel counter
    (void)ui_route_recv_push(c, m, chan_post(0, 0, "hi"), 0, false, "x", 2000);
    CHECK(c.unread_ch() == 1); CHECK(c.last_ch_ms == 2000u);
    CHECK(c.unread_dm() == 1);
    for (uint16_t i = 0; i < 1200; ++i) (void)ui_route_recv_push(c, m, dm, 0, false, "x", 3000);
    CHECK(c.unread_dm() == 1201u);                         // ★ the SERIAL kept counting straight through the cap...
    UiSnapshot pub{}; c.publish(pub);
    CHECK(pub.unread_dm == kUnreadCap);                    // ...and only the PUBLISHED digits saturate
    CHECK(pub.arr_dm    == 1201u);                         // the serial rides along, uncapped, for the freeze
    CHECK(pub.unread_ch == 1);                             // the other kind is untouched by the DM flood
    // A push kind this unit does not own is refused outright, so the caller's `default:` arm keeps its work.
    CHECK(ui_route_recv_push(c, m, push_of(MESHROUTE_NS::PushKind::channel_sent), 0, true, "x", 4000) == false);
}

// ★ THE WRAPAROUND IS A DECISION, so it gets a test rather than a comment. `uint32_t` + unsigned modular subtraction:
// the serials wrapping is harmless, and the invariant is only that the TRUE unread count stays below 2^32 between two
// reads. Driven at the boundary directly — 2^32 arrivals is not a thing a test can push through `ui_route_recv_push`.
TEST_CASE("ui-recv: the arrival serial WRAPS without losing the unread count") {
    UiInboxCounters c{};
    c.arr_dm = 0xFFFFFFFEu; c.read_dm = 0xFFFFFFFEu;
    CHECK(c.unread_dm() == 0);
    ++c.arr_dm; ++c.arr_dm; ++c.arr_dm;                    // ...FE -> ...FF -> 0 -> 1: the serial wrapped
    CHECK(c.arr_dm      == 1u);
    CHECK(c.unread_dm() == 3u);                            // modular subtraction still yields the true count
    UiSnapshot s{}; c.publish(s);
    CHECK(s.unread_dm == 3);
    FrameGate g; UiModel m; UiSnapshot fs{}; fs.now_ms = 10000; c.publish(fs);
    m.on_gesture(Gesture::short_press, fs); m.on_gesture(Gesture::short_press, fs);
    CHECK(m.state().screen == Screen::inbox);
    CHECK(g.step(m, fs, true) == FrameStep::open);
    g.on_page(false, m, c);
    CHECK(c.read_dm     == 1u);                            // the watermark followed the serial across the wrap...
    CHECK(c.unread_dm() == 0);                             // ...and the count is clean, not 4294967295
}

// `Push::body` is LENGTH-COUNTED, not NUL-terminated. A reply built by casting it to `const char*` reads past
// `body_len` into whatever the ring left there — so the bytes after `body_len` must never reach the panel.
TEST_CASE("ui-recv: the reply text is bounded by body_len, not by a NUL") {
    SendTracker emg;
    UiModel m = alarm_accepted(emg, /*ctr=*/77);
    UiInboxCounters c{};
    MESHROUTE_NS::Push pu = chan_post(0, 0x1234ABCD, "OK");
    for (uint8_t i = pu.body_len; i < 40; ++i) pu.body[i] = uint8_t('X');   // stale ring bytes past the length
    (void)ui_route_recv_push(c, m, pu, 0, true, "Ana", 6000);
    CHECK(m.emergency() == Emergency::reply);
    CHECK(strlen(m.reply_text()) == 2u);
    CHECK(strncmp(m.reply_text(), "OK", 2) == 0);
}

// ============================================ §B108 / QA finding F2 — unread handling DISCARDS UNSEEN messages
// ★★★ TWO DEFECTS, one lifecycle. ① `mr_ui_on_push` moved the unread counters and asked for NO repaint, so a new
// message sat unshown until an unrelated gesture happened to invalidate the panel. ② the tick zeroed them on EVERY
// pass while `screen == inbox` — ahead of the blanked check, before a single page had reached the panel, and
// regardless of whether the emergency overlay or the compose modal was covering the body.
// ⚠⚠ ASSERT FROZEN-VS-LIVE COUNTS. "The count is 0 afterwards" passes against BOTH the shipped bug and the tempting
//    wrong fix (`= 0` once the frame completes). Only "the mid-frame arrival SURVIVED" separates the three.

// The tick's own order: `firmware_ui.cpp` builds the snapshot FROM the counters, so the frame freezes what it renders.
static UiSnapshot snap_from(const UiInboxCounters& c, uint32_t now_ms) {
    UiSnapshot s{}; s.now_ms = now_ms; c.publish(s); return s;   // ★ §B108 round 2: ONE call, display + serials
}
// Two short presses walk status -> team -> inbox on a snapshot with no team rows (list_len 1 on both).
static void goto_inbox(UiModel& m, UiSnapshot& s) {
    m.on_gesture(Gesture::short_press, s);
    m.on_gesture(Gesture::short_press, s);
    CHECK(m.state().screen == Screen::inbox);
}

TEST_CASE("ui-recv: F2 — an ARRIVING message asks for a repaint") {
    UiModel m; FrameGate g; UiInboxCounters c{};
    UiSnapshot s = snap_from(c, 10000);
    CHECK(g.step(m, s, true) == FrameStep::open);      // the boot frame
    g.on_page(false, m, c);
    CHECK(g.step(m, s, true) == FrameStep::idle);      // clean: nothing pending
    (void)ui_route_recv_push(c, m, push_of(MESHROUTE_NS::PushKind::msg_recv), 0, false, "x", 10100);
    CHECK(c.unread_dm() == 1);
    s = snap_from(c, 10600);                           // past the 2 Hz throttle
    // ★ Against the shipped code this is `idle`: the DM count changed and NOTHING ever asked the panel to say so.
    CHECK(g.step(m, s, true) == FrameStep::open);
}

TEST_CASE("ui-frame: F2 — a COMPLETE Inbox frame reads only the counts it FROZE") {
    UiModel m; FrameGate g; UiInboxCounters c{};
    UiSnapshot s = snap_from(c, 10000);
    (void)ui_route_recv_push(c, m, chan_post(0, 0, "one"), 0, false, "x", 10000);
    (void)ui_route_recv_push(c, m, chan_post(0, 0, "two"), 0, false, "x", 10000);
    CHECK(c.unread_ch() == 2);
    goto_inbox(m, s);
    s = snap_from(c, 10600);
    CHECK(g.step(m, s, true) == FrameStep::open);
    g.on_page(true, m, c);
    CHECK(c.unread_ch() == 2);                           // ★ opening a frame reads NOTHING — the panel is still blank
    // ...and NOW, while the frame pages out, a third message arrives. It is never on this frame.
    (void)ui_route_recv_push(c, m, chan_post(0, 0, "three"), 0, false, "x", 10650);
    CHECK(c.unread_ch() == 3);
    for (uint8_t i = 0; i < 6; ++i) { CHECK(g.step(m, s, true) == FrameStep::next_page); g.on_page(true, m, c); }
    CHECK(g.step(m, s, true) == FrameStep::next_page);
    g.on_page(false, m, c);                               // the LAST page: the frame is now in front of the user
    // ★★★ THE ASSERTION THAT SEPARATES ALL THREE BEHAVIOURS: shipped code -> 0 (zeroed every tick), a bare `= 0`
    //     after the frame -> 0, correct -> 1. The message that arrived mid-frame was never shown, so it is unread.
    CHECK(c.unread_ch() == 1);
}

// ★★★ §B108 ROUND 2 — THE CASE THE SUITE ABOVE STRUCTURALLY CANNOT REACH: the SAME mid-frame arrival, but with the
// counter ALREADY SATURATED. The first fix survived above the cap in exactly the shape B108 exists to prevent —
// `if (unread < kUnreadCap) ++unread` made the arrival a no-op, and the completion then subtracted the frozen 999 to
// 0, marking a message read that had never been on the panel. The old `on_page` comment NAMED this case and clamped
// it; a clamp changes 65535 into 0, which is the wrong answer rendered tidily.
// ⚠ MEASURED RED against the pre-fix tree, in the OLD field API, before the fix existed:
//     test_firmware_ui_send.cpp: CHECK( 999 == 1000 )   — the cap swallowed the arrival
//     test_firmware_ui_send.cpp: CHECK( 0 == 1 )        — the completion marked it read
//   1 case / 2 assertions failed out of 1311 / 73589.
TEST_CASE("ui-frame: B108 round 2 — a mid-frame arrival survives AT the unread cap") {
    UiModel m; FrameGate g; UiInboxCounters c{};
    UiSnapshot s = snap_from(c, 10000);
    for (uint16_t i = 0; i < kUnreadCap; ++i)
        (void)ui_route_recv_push(c, m, chan_post(0, 0, "x"), 0, false, "x", 10000);
    CHECK(c.unread_ch() == kUnreadCap);
    goto_inbox(m, s);
    s = snap_from(c, 10600);
    CHECK(s.unread_ch == kUnreadCap);                     // the bar draws its three digits...
    CHECK(s.arr_ch    == uint32_t(kUnreadCap));           // ...over a serial that is free to keep going
    CHECK(g.step(m, s, true) == FrameStep::open);
    g.on_page(true, m, c);
    // ...and NOW the arrival the cap used to swallow whole.
    (void)ui_route_recv_push(c, m, chan_post(0, 0, "the one that matters"), 0, false, "x", 10650);
    CHECK(c.unread_ch() == kUnreadCap + 1u);              // ← 999 pre-fix: the increment was a silent no-op
    for (uint8_t i = 0; i < 6; ++i) { CHECK(g.step(m, s, true) == FrameStep::next_page); g.on_page(true, m, c); }
    CHECK(g.step(m, s, true) == FrameStep::next_page);
    g.on_page(false, m, c);
    CHECK(c.unread_ch() == 1);                            // ← 0 pre-fix: read without ever having been displayed
    CHECK(c.read_ch     == uint32_t(kUnreadCap));         // the watermark stopped exactly at what the frame froze
}

TEST_CASE("ui-frame: F2 — an Inbox frame COVERED by the emergency overlay reads nothing") {
    UiModel m; FrameGate g; UiInboxCounters c{};
    UiSnapshot s = snap_from(c, 10000);
    (void)ui_route_recv_push(c, m, chan_post(0, 0, "help?"), 0, false, "x", 10000);
    goto_inbox(m, s);
    s = snap_from(c, 10600);
    m.on_gesture(Gesture::long_fire, s);               // the alarm takes the body from any screen (spec §4.2)
    CHECK(m.emergency()    == Emergency::firing);
    CHECK(m.state().screen == Screen::inbox);          // ...the screen UNDERNEATH is still Inbox
    CHECK(g.step(m, s, true) == FrameStep::open);
    g.on_page(false, m, c);
    CHECK(c.unread_ch() == 1);                           // ← 0 against the shipped code: the user saw an ALARM, not mail
}

TEST_CASE("ui-frame: F2 — an Inbox frame under an open COMPOSE modal reads nothing") {
    UiModel m; FrameGate g; UiInboxCounters c{};
    UiSnapshot s = snap_from(c, 10000);
    (void)ui_route_recv_push(c, m, chan_post(0, 0, "hi"), 0, false, "x", 10000);
    m.on_gesture(Gesture::short_press, s);
    m.on_gesture(Gesture::short_press, s);
    m.on_gesture(Gesture::short_press, s);             // status -> team -> inbox -> send
    CHECK(m.state().screen == Screen::send);
    m.on_gesture(Gesture::double_press, s);            // open the canned-channel modal
    CHECK(m.state().compose == Compose::channel);
    s = snap_from(c, 10600);
    CHECK(g.step(m, s, true) == FrameStep::open);
    g.on_page(false, m, c);
    CHECK(c.unread_ch() == 1);
}

TEST_CASE("ui-frame: F2 — a BLANKED panel on the Inbox screen reads nothing") {
    UiModel m; FrameGate g; UiInboxCounters c{};
    UiSnapshot s = snap_from(c, 10000);
    (void)ui_route_recv_push(c, m, chan_post(0, 0, "hi"), 0, false, "x", 10000);
    goto_inbox(m, s);
    m.on_tick(s);                                      // §B65: the first tick only SEEDS the blank timer
    s = snap_from(c, 10000 + kBlankMs); m.on_tick(s);
    CHECK(m.state().blanked  == true);
    CHECK(g.step(m, s, true) == FrameStep::blank);
    CHECK(c.unread_ch() == 1);                           // ← 0 against the shipped code: cleared into a DARK panel
}

TEST_CASE("ui-frame: F2 — a MAC-busy pass on the Inbox screen reads nothing") {
    UiModel m; FrameGate g; UiInboxCounters c{};
    UiSnapshot s = snap_from(c, 10000);
    (void)ui_route_recv_push(c, m, chan_post(0, 0, "hi"), 0, false, "x", 10000);
    goto_inbox(m, s);
    s = snap_from(c, 10600);
    CHECK(g.step(m, s, /*mac_idle=*/false) == FrameStep::mac_busy);
    CHECK(c.unread_ch() == 1);                           // ← 0 against the shipped code: cleared before the §5 gate
}

TEST_CASE("ui-frame: F2 — a NON-Inbox frame never reads the counters") {
    UiModel m; FrameGate g; UiInboxCounters c{};
    (void)ui_route_recv_push(c, m, chan_post(0, 0, "hi"), 0, false, "x", 10000);
    (void)ui_route_recv_push(c, m, push_of(MESHROUTE_NS::PushKind::msg_recv), 0, false, "x", 10000);
    UiSnapshot s = snap_from(c, 10600);
    CHECK(m.state().screen == Screen::status);
    CHECK(g.step(m, s, true) == FrameStep::open);
    g.on_page(false, m, c);
    CHECK(c.unread_ch() == 1);
    CHECK(c.unread_dm() == 1);
}

// ============================== §B102 / QA finding F3 — a queued press acknowledging a result that was never SHOWN
// ★★★ THE DEFECT: B71's exit tested `emg_outcome_retained()`, which answered "is this a terminal state" — and the
// safety argument for allowing a bare SHORT press rested on "a retained outcome is ALWAYS displayed before any press
// can dismiss it". That was an ASSUMPTION about timing. A frame is eight ticks; `InputFsm` delivers a gesture that
// was already in progress; the MAC-idle gate can hold every one of those ticks. So the alarm's answer could be
// dismissed before its FIRST page reached the panel, and the user would never learn it.
// ⚠⚠ ASSERT THE SEQUENCE, not the state: `emergency() == idle` afterwards is what the SHIPPED code produces too.
//    What discriminates is WHEN it becomes idle relative to the frame completing.

static UiModel alarm_with_outcome(SendTracker& emg, SendOutcome o, uint32_t t) {
    UiModel m = alarm_accepted(emg, /*ctr=*/77);
    m.on_outcome(o, t);
    return m;
}
// Page a whole frame out, answering next_page() as a real 8-page panel does.
static void complete_frame(FrameGate& g, UiModel& m, UiInboxCounters& c, UiSnapshot& s) {
    CHECK(g.step(m, s, true) == FrameStep::open);
    g.on_page(true, m, c);
    for (uint8_t i = 0; i < 7; ++i) {
        CHECK(g.step(m, s, true) == FrameStep::next_page);
        g.on_page(/*more=*/i < 6, m, c);
    }
    CHECK(g.frame_open() == false);
}

TEST_CASE("ui-frame: F3 — a press BEFORE the outcome's first page is consumed, and does NOT dismiss") {
    SendTracker emg; UiInboxCounters c{}; FrameGate g;
    UiModel m = alarm_with_outcome(emg, SendOutcome::channel_relayed(), 5100);
    CHECK(m.emergency() == Emergency::picked_up);
    UiSnapshot s{}; s.now_ms = 5200;
    // The gesture arrives first — nothing has been painted at all yet.
    m.on_gesture(Gesture::short_press, s);
    CHECK(m.emergency() == Emergency::picked_up);      // ← `idle` against the shipped code: the answer is GONE unseen
    // ...and it must not have operated the screen underneath the overlay either.
    CHECK(m.state().screen  == Screen::status);
    CHECK(m.state().compose == Compose::none);
    // Now the frame actually completes, and only then does the press work.
    complete_frame(g, m, c, s);
    CHECK(m.emergency() == Emergency::picked_up);      // still shown: a completed frame does not dismiss anything
    s.now_ms = 6000;
    m.on_gesture(Gesture::short_press, s);
    CHECK(m.emergency() == Emergency::idle);
}

TEST_CASE("ui-frame: F3 — a press DURING the outcome's frame is consumed; the frame must COMPLETE") {
    SendTracker emg; UiInboxCounters c{}; FrameGate g;
    UiModel m = alarm_with_outcome(emg, SendOutcome::channel_relayed(), 5100);
    UiSnapshot s{}; s.now_ms = 5200;
    CHECK(g.step(m, s, true) == FrameStep::open);
    g.on_page(true, m, c);                             // page 0 of 8 only
    m.on_gesture(Gesture::short_press, s);
    CHECK(m.emergency() == Emergency::picked_up);      // ← `idle` against the shipped code, one page in
    for (uint8_t i = 0; i < 7; ++i) { CHECK(g.step(m, s, true) == FrameStep::next_page); g.on_page(i < 6, m, c); }
    s.now_ms = 6000;
    m.on_gesture(Gesture::short_press, s);
    CHECK(m.emergency() == Emergency::idle);
}

TEST_CASE("ui-frame: F3 — a frame that BLANKED before completing presents nothing") {
    SendTracker emg; UiInboxCounters c{}; FrameGate g;
    UiModel m = alarm_with_outcome(emg, SendOutcome::channel_relayed(), 5100);
    UiSnapshot s{}; s.now_ms = 5200;
    CHECK(g.step(m, s, true) == FrameStep::open);
    g.on_page(true, m, c);
    m.on_tick(s);                                      // §B65 seed
    s.now_ms = 5200 + kEmgHoldMs + kBlankMs; m.on_tick(s);
    CHECK(m.state().blanked  == true);
    CHECK(g.step(m, s, true) == FrameStep::blank);     // the page loop is abandoned mid-frame
    m.on_gesture(Gesture::short_press, s);             // the WAKING press is consumed by the blank rule
    CHECK(m.state().blanked == false);
    m.on_gesture(Gesture::short_press, s);
    CHECK(m.emergency() == Emergency::picked_up);      // ★ still not dismissed: no frame ever completed
}

// A NEWER outcome is NEW news: the frame that presented the older one must not license dismissing it.
TEST_CASE("ui-frame: F3 — a second REPLY needs its own completed frame") {
    SendTracker emg; UiInboxCounters c{}; FrameGate g;
    UiModel m = alarm_with_outcome(emg, SendOutcome::channel_relayed(), 5100);
    UiSnapshot s{}; s.now_ms = 5200;
    complete_frame(g, m, c, s);
    m.on_reply("Ana", "on my way", 5300);              // ...and now a real answer arrives
    CHECK(m.emergency() == Emergency::reply);
    s.now_ms = 5400;
    m.on_gesture(Gesture::short_press, s);
    CHECK(m.emergency() == Emergency::reply);          // ★ the PICKED UP frame does not pay for the REPLY
    complete_frame(g, m, c, s);
    s.now_ms = 6000;
    m.on_gesture(Gesture::short_press, s);
    CHECK(m.emergency() == Emergency::idle);
}

// The other half of F3: while the overlay is up, a consumed press must not drive the screen beneath it.
TEST_CASE("ui-frame: F3 — a consumed press never operates the screen under the overlay") {
    SendTracker emg; UiInboxCounters c{}; FrameGate g; (void)g; (void)c;
    UiModel m; UiSnapshot s{}; s.now_ms = 1000;
    m.on_gesture(Gesture::short_press, s);                  // status -> team
    CHECK(m.state().screen == Screen::team);
    m.on_gesture(Gesture::long_arm,  s);
    s.now_ms = 4600; m.on_gesture(Gesture::long_fire, s);
    CHECK(m.emergency() == Emergency::firing);              // SENDING... owns the body
    const Screen under = m.state().screen;
    for (uint8_t i = 0; i < 5; ++i) { s.now_ms += 100; m.on_gesture(Gesture::short_press, s); }
    CHECK(m.emergency()    == Emergency::firing);           // sticky, as B71 requires
    CHECK(m.state().screen == under);                       // ← the screen CYCLED against the shipped code
    CHECK(m.state().compose == Compose::none);
}

// ================================ §R1 (OWNER-RULED 2026-08-05) — AN INCOMING REPLY UN-BLANKS A DARK PANEL
// ★★★ THE DEFECT, found while disproving §B107's "the blanked branch has the same defect" premise, and REPORTED rather
// than invented: NOTHING un-blanked on an incoming push. `on_reply` moved the model to `Emergency::reply` and set
// `dirty`, but `UiState::blanked` stayed true — so `FrameGate::step` kept answering `blank` and the answer to a
// distress call sat behind a panel that is OFF until the hiker happens to press the button. On a safety device that is
// the one message the feature exists to deliver.
// ⇒ OWNER RULING: an incoming REPLY un-blanks. ⚠ Blanking stays EDGE-TRIGGERED (spec §5): the un-blank is a
//   TRANSITION — one `set_power_save(false)`, never a per-tick write — and it must NOT become wake-on-any-push.
//
// ★★ ASSERT THE SIDE EFFECT, WHICH IS "A PANEL THAT STAYS DARK" — never a post-hoc `blanked` enum, which is exactly the
//    shape §B97/§B98 shipped green against. These cases therefore drive the same `FrameStep` -> `set_power_save`
//    mapping `mr_ui_tick` uses and record ONLY what actually reaches the SSD1306, through the board's own latch.
// ⓘ HONEST SCOPE, stated rather than implied: `PanelLog::apply` HAND-REPLICATES `firmware_ui.cpp`'s switch, and a
//   hand-replicated wiring cannot fail for a mis-wired tick (§B97). What it measures is `FrameGate`'s DECISIONS. That
//   the tick really maps those onto the two panel commands is pinned separately and structurally, by
//   `tools/probe_board_ui/run.sh`'s W6 — which carries its own negative control.
struct PanelLog {
    bool    asleep  = false;    // the board's LATCH (variants/heltec_v3/board_ui.cpp:143 `if (on == s_asleep) return;`)
    uint8_t cmds[8] = {};       // ONLY what reached the bus: 1 = SSD1306 DISPLAYOFF, 0 = DISPLAYON
    uint8_t n       = 0;
    void power_save(bool on) { if (on == asleep) return; asleep = on; if (n < 8) cmds[n++] = on ? 1 : 0; }
    void apply(FrameStep st) {
        switch (st) {
            case FrameStep::mac_busy:  return;                  // touch NOTHING mid-exchange, not even the latch
            case FrameStep::blank:     power_save(true);  return;
            case FrameStep::idle:      power_save(false); return;
            case FrameStep::open:      power_save(false); return;
            case FrameStep::next_page: power_save(false); return;
        }
    }
    // ⚠ NOT a bare `cmds[i]`: the array is zero-initialised and 0 IS "DISPLAYON", so an ABSENT wake would read as a
    //   successful one — a test green against the defect it names (§B98). -1 makes absence distinguishable.
    int cmd(uint8_t i) const { return (i < n) ? int(cmds[i]) : -1; }
};

// One service pass, in `mr_ui_tick`'s own order: decide, drive the panel, feed the page verdict back.
static void ui_pass(FrameGate& g, UiModel& m, UiInboxCounters& c, UiSnapshot& s, PanelLog& p, bool more_pages) {
    const FrameStep st = g.step(m, s, /*mac_idle=*/true);
    p.apply(st);
    if (st == FrameStep::open || st == FrameStep::next_page) g.on_page(more_pages, m, c);
}

// Carry a live alarm past its kEmgHoldMs window until the panel is DARK — the exact bench state R1 is about.
// Returns the instant it went dark. ★ It also asserts the EDGE on the way in: four dark passes, ONE DISPLAYOFF.
static uint32_t blank_the_panel(UiModel& m, FrameGate& g, UiInboxCounters& c, UiSnapshot& s, PanelLog& p) {
    s.now_ms = 5000; m.on_tick(s);                                   // §B65: the first tick only SEEDS the timer
    s.now_ms = 5000 + kEmgHoldMs + kBlankMs; m.on_tick(s);
    CHECK(m.state().blanked == true);
    for (uint8_t i = 0; i < 4; ++i) ui_pass(g, m, c, s, p, false);
    CHECK(p.n      == 1);                                            // spec §5: the EDGE, never a per-tick write
    CHECK(p.cmd(0) == 1);
    return s.now_ms;
}

TEST_CASE("ui-recv: R1 — OUR team's REPLY wakes a dark panel, exactly once") {
    SendTracker emg; UiInboxCounters c{}; FrameGate g; PanelLog p;
    UiModel m = alarm_accepted(emg, /*ctr=*/77);           // an alarm really did go out: _tries == 1
    UiSnapshot s{};
    const uint32_t dark = blank_the_panel(m, g, c, s, p);
    CHECK(m.emergency() == Emergency::firing);             // ...and it is still live behind the dark panel

    const uint32_t t_reply = dark + 1000;
    (void)ui_route_recv_push(c, m, chan_post(0, 0x1234ABCD, "on my way"), /*ui_team_channel_id=*/0,
                             /*same_team_post=*/true, "Ana", t_reply);
    CHECK(m.emergency() == Emergency::reply);              // the MODEL knows the answer arrived...
    s.now_ms = t_reply; m.on_tick(s);
    CHECK(m.state().blanked == false);                     // ← true against the shipped code
    for (uint8_t i = 0; i < 4; ++i) ui_pass(g, m, c, s, p, false);
    // ★★ THE SIDE EFFECT, and it is the whole case: against the shipped code `step` keeps answering `blank`, the latch
    //    never flips, and the hiker's answer is on a panel that is OFF.
    CHECK(p.n      == 2);
    CHECK(p.cmd(1) == 0);                                  // DISPLAYON — the wake TRANSITION
    CHECK(p.asleep == false);
    // ...and EXACTLY once: four more awake passes must add no command at all (spec §5's edge trigger).
    for (uint8_t i = 0; i < 4; ++i) ui_pass(g, m, c, s, p, false);
    CHECK(p.n == 2);
}

// ★★★ THE NEGATIVE CONTROLS R1 CANNOT BE TRUSTED WITHOUT. Without them the case above passes equally against the
// correct fix and against WAKE-ON-ANY-PUSH — the §2.1 false-confirmation class in POWER form, and a battery-drain
// vector on a device whose whole job is to still be alive when it is needed.
//
// ⚠⚠ THE FIRST WRITING OF THESE CONTROLS WAS VACUOUS, AND IT WAS CAUGHT BY MEASUREMENT, NOT BY REVIEW. They asserted
//    only the PANEL COMMANDS after an `on_tick`, and a mutation that un-blanked on EVERY arrival still passed them:
//    with no live hold, that very next `on_tick` re-blanks the model before any paint pass can run, so the wrong fix
//    is papered over by an unrelated rule. ⇒ two changes, and both are the point:
//      ① assert `blanked` AT THE INSTANT OF DIVERGENCE — immediately after `ui_route_recv_push` returns, before any
//         tick can hide it. That is the routing call's own side effect, not a post-hoc end state; and
//      ② the third case builds the state where the harm really does reach the bus — a live alarm HOLDING the panel
//         (§4.3), so `on_tick` has no re-blank to hide behind. That one measures pixels, not a flag.
//    ⓘ Recorded rather than quietly fixed: "the wrong fix is neutralised by on_tick" is an ACCIDENT of rule order, not
//      a safety property, and nothing may be built on it.
TEST_CASE("ui-recv: R1 NEGATIVE CONTROL — a stranger's channel-0 post must NOT un-blank") {
    SendTracker emg; UiInboxCounters c{}; FrameGate g; PanelLog p;
    UiModel m = alarm_accepted(emg, /*ctr=*/77);
    UiSnapshot s{};
    const uint32_t dark = blank_the_panel(m, g, c, s, p);
    // The §B103/F4 shape: a plain LEAF post (`team_id == 0`) on channel 0 from a node with no team and no key.
    // `ingest_channel_m` lets it through to everyone, so it really does reach the UI.
    const uint32_t t = dark + 1000;
    const bool owned = ui_route_recv_push(c, m, chan_post(0, /*team=*/0, "hello everyone"), 0,
                                          /*same_team_post=*/false, "stranger", t);
    CHECK(owned == true);                                  // it IS a message: the counter moves...
    CHECK(c.unread_ch() == 1);
    // ★★ THE INSTANT OF DIVERGENCE. ← false under wake-on-any-push, and nothing has yet had a chance to hide it.
    CHECK(m.state().blanked == true);
    CHECK(m.emergency()     == Emergency::firing);         // and nothing became a REPLY either (§B103)
    s.now_ms = t; m.on_tick(s);
    for (uint8_t i = 0; i < 4; ++i) ui_pass(g, m, c, s, p, false);
    CHECK(p.n      == 1);
    CHECK(p.cmd(1) == -1);                                 // no second command reached the panel at all
    CHECK(p.asleep == true);
}

// The SECOND control pins WHICH reading of the ruling was implemented. The ruling names F4's team-scoped predicate as
// what qualifies a reply; §4.4 then adds a state whitelist plus "an alarm was actually transmitted". What wakes is a
// REPLY — a post those together ACCEPT — not every post that merely clears the team scope. Ordinary team chatter must
// not spend battery lighting a panel nobody is looking at.
TEST_CASE("ui-recv: R1 CONTROL — an OUR-TEAM post with no alarm behind it is chatter, and chatter stays dark") {
    UiModel m; UiInboxCounters c{}; FrameGate g; PanelLog p;
    UiSnapshot s{}; s.now_ms = 1000; m.on_tick(s);
    s.now_ms = 1000 + kBlankMs; m.on_tick(s);
    CHECK(m.state().blanked == true);
    for (uint8_t i = 0; i < 3; ++i) ui_pass(g, m, c, s, p, false);
    CHECK(p.n == 1); CHECK(p.cmd(0) == 1);
    // Our own team, our own channel — F4's predicate PASSES. But nothing was ever transmitted, so §4.4 refuses it and
    // there is no REPLY to show.
    const uint32_t t = s.now_ms + 1000;
    (void)ui_route_recv_push(c, m, chan_post(0, 0x1234ABCD, "anyone about?"), 0, /*same_team_post=*/true, "Ana", t);
    CHECK(m.emergency()     == Emergency::idle);
    CHECK(m.state().blanked == true);                      // ★ at the instant of divergence
    s.now_ms = t; m.on_tick(s);
    for (uint8_t i = 0; i < 3; ++i) ui_pass(g, m, c, s, p, false);
    CHECK(p.n      == 1);
    CHECK(p.asleep == true);
}

// ★★★ THE CONTROL THAT REACHES THE PANEL. A blanked panel with a LIVE, HOLDING alarm behind it is an ordinary state:
// the alarm blocks, `retain()` re-arms §4.3's deadline, and `on_tick` therefore has no re-blank left to hide a wrong
// wake behind. Under wake-on-any-push a passer-by's plaintext post LIGHTS a rescue device's screen here — measured as
// a DISPLAYON on the bus, not as a flag.
// ⓘ OBSERVED AND REPORTED, NOT FIXED: nothing un-blanks for a `blocked` / `picked_up` / `not_heard` outcome either —
//   R1 rules on the REPLY, and widening it is a ruling I do not have. The state below is exactly that gap, used here
//   as a fixture.
TEST_CASE("ui-recv: R1 NEGATIVE CONTROL — a stranger cannot LIGHT a panel a live alarm is holding dark") {
    SendTracker emg; UiInboxCounters c{}; FrameGate g; PanelLog p;
    UiModel m = alarm_accepted(emg, /*ctr=*/77);
    UiSnapshot s{};
    const uint32_t dark  = blank_the_panel(m, g, c, s, p);
    const uint32_t t_blk = dark + 500;
    m.on_outcome(SendOutcome::blocked(60000), t_blk);      // §4.3: the hold deadline is LIVE again...
    CHECK(m.emergency() == Emergency::blocked);
    s.now_ms = t_blk; m.on_tick(s);
    CHECK(m.state().blanked == true);                      // ...but a blocked outcome is not a reply, so it stays dark
    for (uint8_t i = 0; i < 3; ++i) ui_pass(g, m, c, s, p, false);
    CHECK(p.n == 1);
    const uint32_t t = t_blk + 100;
    (void)ui_route_recv_push(c, m, chan_post(0, /*team=*/0, "hello everyone"), 0,
                             /*same_team_post=*/false, "stranger", t);
    CHECK(m.state().blanked == true);                      // ← false under wake-on-any-push
    s.now_ms = t; m.on_tick(s);
    CHECK(m.state().blanked == true);                      // ...and the live hold means on_tick cannot re-blank it
    for (uint8_t i = 0; i < 3; ++i) ui_pass(g, m, c, s, p, false);
    // ★★ THE PANEL ITSELF: one command in this test's whole life, and it was the DISPLAYOFF.
    CHECK(p.n      == 1);                                  // ← 2 under wake-on-any-push: the screen LIT for a stranger
    CHECK(p.asleep == true);
}

TEST_CASE("ui-recv: R1 — the wake inherits the retained-outcome hold; it invents no second window") {
    SendTracker emg; UiInboxCounters c{}; FrameGate g; PanelLog p;
    UiModel m = alarm_accepted(emg, /*ctr=*/77);
    UiSnapshot s{};
    const uint32_t dark    = blank_the_panel(m, g, c, s, p);
    const uint32_t t_reply = dark + 1000;
    (void)ui_route_recv_push(c, m, chan_post(0, 0x1234ABCD, "on my way"), 0, /*same_team_post=*/true, "Ana", t_reply);
    s.now_ms = t_reply; m.on_tick(s);
    CHECK(m.state().blanked == false);                     // ← true against the shipped code
    // ★ NO SECOND TIMER (U1/C2): `on_reply` already calls `retain()`, so §4.3's kEmgHoldMs deadline — measured from the
    //   REPLY's OWN arrival, which is exactly what §4.3 exists to guarantee — is what keeps the panel lit.
    s.now_ms = t_reply + kEmgHoldMs - 1; m.on_tick(s);
    CHECK(m.state().blanked == false);
    s.now_ms = t_reply + kEmgHoldMs + 1; m.on_tick(s);
    CHECK(m.state().blanked == true);                      // ...then dark again, with the REPLY state retained
    CHECK(m.emergency()     == Emergency::reply);
    // ⓘ THE DECISION THE RULING ASKED ME TO STATE — and it needed no ruling of its own, because it is INERT: the wake
    //   deliberately does NOT touch `_last_input_ms`, and it could not matter if it did. kEmgHoldMs outranks kBlankMs,
    //   so the hold is the binding constraint and BOTH readings blank at exactly this instant. ASSERTED rather than
    //   argued in prose, so a later reader cannot "fix" it into a second window that then disagrees with the real one.
    static_assert(kEmgHoldMs > kBlankMs, "R1: the retained hold must outrank the blank timer, or a wake needs its own window");
}

// ====================================================================================================== UI-7 — THE SEND
// ★★★ THESE ARE THE CASES UI-6 COULD NOT HAVE WRITTEN, AND THE REASON IS THE WHOLE ARGUMENT FOR THE SEAM.
// `ui_perform_send` shipped in `src/firmware_ui.cpp` as a LOUD REFUSAL STUB — a TU neither the native suite nor the
// simulator compiles — so every safety rule the plan states about it (the conditional `-l`, the typed result, the
// `ctr == 0` reading, where `on_send_accepted` goes) was a caller obligation no gate could reach. It is now a pure
// function taking an EXECUTOR, and the executor a test supplies RECORDS THE LINE.
// ⇒ ★★ EVERY ASSERTION BELOW IS ABOUT A SIDE EFFECT — the command actually issued, or the call that was or was not
//   made — never a post-hoc enum. §B110 measured why that matters: the shipped compose path CLOSES its modal as it
//   sends, so `compose == none` is green against a real mis-send. `f.calls` and `f.line` cannot be.

namespace {
struct FakeExec {
    char     line[160] = {};
    int      calls     = 0;
    SendExec reply{};                      // what `on_command` will answer
};
SendExec fake_exec(const char* line, std::size_t len, void* ctx) {
    FakeExec* f = static_cast<FakeExec*>(ctx);
    ++f->calls;
    const std::size_t n = (len < sizeof f->line - 1) ? len : sizeof f->line - 1;
    for (std::size_t i = 0; i < n; ++i) f->line[i] = line[i];
    f->line[n] = '\0';
    return f->reply;
}
UiSnapshot snap_at(uint32_t now_ms) { UiSnapshot s{}; s.now_ms = now_ms; return s; }
SendExec ok_ctr(uint16_t c)  { return SendExec{ true, MESHROUTE_NS::CmdCode::queued, c }; }
SendExec refused(MESHROUTE_NS::CmdCode c) { return SendExec{ true, c, 0 }; }
}  // namespace

// ---- the composed line: the one artefact the radio actually sees -----------------------------------------------

TEST_CASE("ui7-line: a DM is `send <id> \"<text>\" -t -a` — the id, the plane and the ack, exactly") {
    char b[kSendLineCap];
    const int n = ui_compose_send_line(b, sizeof b, SendReq{SendKind::dm, /*peer=*/7, /*idx=*/0}, 0, false);
    CHECK(n > 0);
    // ⛔ NO `-e`: the parser gates it `allow_e=by_hash` and REJECTS it on an id target, so the line would not parse
    //    at all. `crypt` stays `def` and follows the node's own e2e_dm setting (spec §3.4).
    CHECK(std::strcmp(b, "send 7 \"Are you OK?\" -t -a") == 0);
}

TEST_CASE("ui7-line: the second DM text is the second table row, not an off-by-one") {
    char b[kSendLineCap];
    CHECK(ui_compose_send_line(b, sizeof b, SendReq{SendKind::dm, 200, 1}, 0, false) > 0);
    CHECK(std::strcmp(b, "send 200 \"I'm OK\" -t -a") == 0);
}

TEST_CASE("ui7-line: a canned channel post is `send_channel <ch> \"<text>\" -t -e`") {
    char b[kSendLineCap];
    CHECK(ui_compose_send_line(b, sizeof b, SendReq{SendKind::channel_canned, 0, 1}, /*ch=*/3, false) > 0);
    CHECK(std::strcmp(b, "send_channel 3 \"All good\" -t -e") == 0);
}

// ★★★ THE SAFETY CASE OF THIS WHOLE STEP (spec §4.1). `Node::on_command` REFUSES a located post outright when both
//     coordinates are zero (node.cpp:1553 -> `err_unsupported`, BEFORE anything is enqueued), so an unconditional
//     `-l` turns "this node has no fix" into NO ALARM AT ALL. A distress call is worth more than the coordinates.
TEST_CASE("ui7-line: the EMERGENCY carries -l only WITH a fix; without one it still goes out") {
    char with_fix[kSendLineCap], no_fix[kSendLineCap];
    CHECK(ui_compose_send_line(with_fix, sizeof with_fix, SendReq{SendKind::emergency, 0, 0}, 0, /*have_fix=*/true) > 0);
    CHECK(std::strcmp(with_fix, "send_channel 0 \"I'm in danger\" -t -l -e") == 0);
    CHECK(ui_compose_send_line(no_fix, sizeof no_fix, SendReq{SendKind::emergency, 0, 0}, 0, /*have_fix=*/false) > 0);
    CHECK(std::strcmp(no_fix, "send_channel 0 \"I'm in danger\" -t -e") == 0);
    // The alarm is not silently downgraded to nothing: the body is identical and only `-l` differs.
    CHECK(std::strstr(no_fix, "\"I'm in danger\"") != nullptr);
    CHECK(std::strstr(no_fix, " -l") == nullptr);
}

// ★★ §B66's positional `back` row, guarded one level DOWN. The model already refuses to queue it, but a composer that
//    CLAMPED instead of refusing would turn "back, don't send" into a send the moment the two ever disagree.
TEST_CASE("ui7-line: the `back` row index REFUSES to compose, for both tables, and leaves the buffer empty") {
    char b[kSendLineCap];
    b[0] = 'x';
    CHECK(ui_compose_send_line(b, sizeof b, SendReq{SendKind::dm, 7, uint8_t(kDmTextCount - 1)}, 0, false) == 0);
    CHECK(b[0] == '\0');                                   // never a partly-formed command
    b[0] = 'x';
    CHECK(ui_compose_send_line(b, sizeof b, SendReq{SendKind::channel_canned, 0,
                                                    uint8_t(kChannelTextCount - 1)}, 0, false) == 0);
    CHECK(b[0] == '\0');
    // ...and anything past the table too.
    CHECK(ui_compose_send_line(b, sizeof b, SendReq{SendKind::dm, 7, 99}, 0, false) == 0);
}

TEST_CASE("ui7-line: truncation is a REFUSAL, never a short send (C2)") {
    char tiny[12];
    tiny[0] = 'x';
    CHECK(ui_compose_send_line(tiny, sizeof tiny, SendReq{SendKind::dm, 7, 0}, 0, false) == 0);
    CHECK(tiny[0] == '\0');
}

TEST_CASE("ui7-line: kSendLineCap fits every line either verb can produce, at the widest id and channel") {
    char b[kSendLineCap];
    CHECK(ui_compose_send_line(b, sizeof b, SendReq{SendKind::dm, 255, 0}, 0, false) > 0);
    CHECK(ui_compose_send_line(b, sizeof b, SendReq{SendKind::channel_canned, 0, 0}, 255, false) > 0);
    CHECK(ui_compose_send_line(b, sizeof b, SendReq{SendKind::emergency, 0, 0}, 255, true) > 0);
}

// ---- the send driver -------------------------------------------------------------------------------------------

TEST_CASE("ui7-send: an ACCEPTED emergency spends exactly one attempt and holds its handle") {
    UiModel m = armed_and_fired(); SendReq req{}; SendTracker emg, normal; FakeExec f; f.reply = ok_ctr(77);
    const bool got = m.take_send_request(req); CHECK(got == true); if (!got) return;
    ui_perform_send(emg, normal, m, req, 0, true, fake_exec, &f, 6000);
    CHECK(f.calls == 1);
    CHECK(std::strcmp(f.line, "send_channel 0 \"I'm in danger\" -t -l -e") == 0);
    CHECK(m.attempts() == 1);
    CHECK(emg.idle() == false);
    CHECK(normal.idle() == true);                          // ★ the alarm never touches the normal slot
    SendOutcome o{};
    CHECK(emg.match_channel_sent(77, /*relayed=*/true, o) == true);   // the handle really is held
}

// ★★★ §B39/§B84 — THE RULE THAT WAS A DEFECT TWICE. `ctr == 0` is NOT acceptance: `_tries` moves only in
//     `on_send_accepted`, and the BOUNDED EXPIRY is what spends the attempt. Counting it here as well would burn two
//     of the three alarms on one transmission.
TEST_CASE("ui7-send: `queued` with ctr==0 spends NO attempt here — the bounded expiry does") {
    UiModel m = armed_and_fired(); SendReq req{}; SendTracker emg, normal; FakeExec f; f.reply = ok_ctr(0);
    const bool got = m.take_send_request(req); CHECK(got == true); if (!got) return;
    ui_perform_send(emg, normal, m, req, 0, false, fake_exec, &f, 6000);
    CHECK(f.calls == 1);
    CHECK(m.attempts() == 0);                              // ★ NOT 1 — this is the whole assertion
    CHECK(emg.awaiting() == true);                         // parked, not accepted and not refused
    CHECK(m.emergency() == Emergency::firing);             // and NOT reported as a failure either (§B68)
    // ...and the expiry then spends exactly ONE, through the shipped glue.
    ui_pump_trackers(emg, normal, m, 6000 + kOutcomeWindowMs + 1);
    CHECK(m.attempts() == 1);
    CHECK(emg.idle() == true);
}

// ★ Spec §2.1 rule 1: a synchronous refusal is a TERMINAL, NAMED outcome — never an indefinite `SENDING...`.
TEST_CASE("ui7-send: an err_* result lands the alarm in FAILED and carries the CmdCode verbatim") {
    UiModel m = armed_and_fired(); SendReq req{}; SendTracker emg, normal; FakeExec f;
    f.reply = refused(MESHROUTE_NS::CmdCode::err_unsupported);
    const bool got = m.take_send_request(req); CHECK(got == true); if (!got) return;
    ui_perform_send(emg, normal, m, req, 0, true, fake_exec, &f, 6000);
    CHECK(m.emergency() == Emergency::failed);
    CHECK(emg.idle() == true);                             // the slot is released, never leaked on a refusal
    // ★★ THE CODE IS THE POINT. `no_key`, `no_identity`, `no_fix`, `empty` and `unsealable` ALL return
    //    `err_unsupported`, so the compact reason cannot name the wall and the code is the only thing that can.
    CHECK(m.refuse_reason() == RefuseReason::other);
    CHECK(m.refuse_code() == MESHROUTE_NS::CmdCode::err_unsupported);
}

TEST_CASE("ui7-send: a line that never PARSED is `parser`, and no code is claimed for it") {
    UiModel m = armed_and_fired(); SendReq req{}; SendTracker emg, normal; FakeExec f;
    f.reply = SendExec{ /*ok=*/false, MESHROUTE_NS::CmdCode::queued, 0 };
    const bool got = m.take_send_request(req); CHECK(got == true); if (!got) return;
    ui_perform_send(emg, normal, m, req, 0, true, fake_exec, &f, 6000);
    CHECK(m.emergency() == Emergency::failed);
    CHECK(m.refuse_reason() == RefuseReason::parser);       // the predicate that says "read no code"
}

// ★★★ THE MIS-SEND CONTROL, and it asserts the ONE thing that discriminates: whether the executor RAN. A composer
//     that clamped a bad index instead of refusing would put a real command on the wire, and every state assertion
//     downstream would still look correct.
TEST_CASE("ui7-send: a request the composer refuses NEVER reaches the executor") {
    UiModel m; SendTracker emg, normal; FakeExec f; f.reply = ok_ctr(9);
    ui_perform_send(emg, normal, m, SendReq{SendKind::dm, 7, uint8_t(kDmTextCount - 1)}, 0, false,
                    fake_exec, &f, 6000);
    CHECK(f.calls == 0);                                   // ★ nothing was transmitted
    CHECK(f.line[0] == '\0');
    CHECK(m.dm_state() == DmState::failed);                // and it fails LOUD, not silently (C2)
    CHECK(normal.idle() == true);
}

TEST_CASE("ui7-send: a DM goes to the NORMAL slot and reaches waiting_ack; the alarm slot stays untouched") {
    UiModel m; SendTracker emg, normal; FakeExec f; f.reply = ok_ctr(42);
    ui_perform_send(emg, normal, m, SendReq{SendKind::dm, 11, 0}, 0, false, fake_exec, &f, 6000);
    CHECK(std::strcmp(f.line, "send 11 \"Are you OK?\" -t -a") == 0);
    CHECK(normal.idle() == false);
    CHECK(emg.idle() == true);                             // ★ a DM can never occupy the alarm's slot
    CHECK(m.dm_state() == DmState::waiting_ack);
    CHECK(m.attempts() == 0);                              // ...and never spends an alarm attempt
}

// ---- §B113: the CANNED-CHANNEL twin of the case above, and it did not exist ---------------------------------------
// ★★★ B113 (found by independent QA on UI-7). `ChanState::waiting` was assigned ZERO times in the whole tree and
//     referenced exactly ONCE, by `firmware_ui.cpp`'s `"SENT, waiting"` arm — a DEAD STATE. `on_send_accepted` had
//     arms for `emergency` and `dm` only, so an ACCEPTED canned post stayed on `submitting` and the panel read
//     `SENDING...` until either the ~36 s `channel_sent` verdict or the sub-view's own 15 s auto-exit — which on the
//     common path arrives FIRST. ⇒ a successful send whose only feedback was a spinner that never resolved. The bench
//     guide (H7-01) states the required behaviour verbatim.
// ⓘ **§T3 2026-08-14:** that sequence is now `SENDING...` -> `QUEUED` -> `SENT, waiting`, and this case's subject —
//     the ACCEPTANCE arm — is the `QUEUED` step. `SENT, waiting` belongs to `ChanState::aired` and is reached only by
//     a correlated `send_aired`; the ui-T3 cases at the bottom of this file own that half.
// ★ THREE THINGS ARE ASSERTED, and ② and ③ are what stop the one-line fix from breaking what already worked:
//     ① the state MOVES on acceptance;
//     ② the normal tracker STILL HOLDS ITS HANDLE — UI-4's slot discipline: the acceptance must not disturb the
//        correlation, or the verdict this state is waiting FOR can never be matched;
//     ③ neither the DM state, the alarm state nor the attempt budget moves — §B84's ordering, from the other side.
// ⓘ Walked through the REAL path rather than calling `ui_perform_send` on a hand-built request: the claim is about an
//   *accepted canned post*, so the request must come from the compose modal that produces one, and `take_send_request`
//   must be the thing that leaves it on `submitting`. That is also the sequence `mr_ui_tick` runs.
TEST_CASE("ui7-B113: an ACCEPTED canned post enters `waiting`, KEEPS its handle, and moves neither DM nor alarm") {
    UiModel m; SendTracker emg, normal; FakeExec f; f.reply = ok_ctr(31);
    const UiSnapshot s = snap_at(6000);
    for (int i = 0; i < 3; ++i) m.on_gesture(Gesture::short_press, s);   // status -> team -> inbox -> SEND
    CHECK(m.state().screen == Screen::send);
    m.on_gesture(Gesture::double_press, s);                              // open the canned CHANNEL list
    m.on_gesture(Gesture::double_press, s);                              // ...and send its first text
    SendReq req{};
    const bool got = m.take_send_request(req); CHECK(got == true); if (!got) return;   // ⚠ §B70: ONE call
    CHECK(req.kind == SendKind::channel_canned);
    CHECK(m.chan_state() == ChanState::submitting);                      // the hand-off, before the core answers
    ui_perform_send(emg, normal, m, req, /*ch=*/0, false, fake_exec, &f, 6000);
    // The SIDE EFFECT first (§B110): the command actually issued, not a post-hoc enum.
    CHECK(f.calls == 1);
    CHECK(std::strcmp(f.line, "send_channel 0 \"Got your message\" -t -e") == 0);
    // ① — the whole point of the entry: `submitting` -> `waiting` on ACCEPTANCE.
    CHECK(m.chan_state() == ChanState::waiting);
    CHECK(m.chan_state() != ChanState::submitting);                      // ...it really MOVED, it did not merely differ
    // ② THE HANDLE IS RETAINED. ⚠ `match_channel_sent` CONSUMES (§B70) — ONE call, into a local.
    SendOutcome o{};
    const bool matched = normal.match_channel_sent(31, /*relayed=*/true, o);
    CHECK(matched == true);
    if (matched) CHECK(o.kind == SendOutcome::Kind::channel_relayed);
    // ③ — the other two machines are untouched, in BOTH directions (see the control below for the converse).
    CHECK(m.dm_state()   == DmState::idle);
    CHECK(m.emergency()  == Emergency::idle);
    CHECK(m.attempts()   == 0);
    CHECK(emg.idle()     == true);
    // ...and `waiting` is not terminal: the verdict it was waiting for still lands.
    if (matched) m.on_channel_outcome(o, 7000);
    CHECK(m.chan_state() == ChanState::relayed);
}

// ★★★ THE NEGATIVE CONTROL FOR THE TEMPTING WRONG FIX — writing `_chan` UNCONDITIONALLY in `on_send_accepted` instead
//     of on the `channel_canned` arm. That passes the case above completely, and it is exactly the §2.1 crossover the
//     two slots exist to prevent: an ALARM's acceptance would then relabel a coincident canned post as `SENT, waiting`,
//     and a DM's acceptance would do the same for a channel transaction that was never even submitted.
// ⚠ Two independent models, so neither can hide the other's write behind a state the first one already moved (the §M6
//   vacuity lesson: a control whose scenario has already set the field cannot measure who set it).
TEST_CASE("ui7-B113 CONTROL: accepting a DM or an ALARM must never move the canned-channel state") {
    UiModel dm_only; SendTracker emg_a, normal_a; FakeExec fa; fa.reply = ok_ctr(42);
    ui_perform_send(emg_a, normal_a, dm_only, SendReq{SendKind::dm, 11, 0}, 0, false, fake_exec, &fa, 6000);
    CHECK(dm_only.dm_state()   == DmState::waiting_ack);   // the DM really was accepted...
    CHECK(dm_only.chan_state() == ChanState::idle);        // ★ ...and the channel state did NOT move

    UiModel alarm = armed_and_fired(); SendReq req{}; SendTracker emg_b, normal_b; FakeExec fb; fb.reply = ok_ctr(77);
    const bool got = alarm.take_send_request(req); CHECK(got == true); if (!got) return;
    ui_perform_send(emg_b, normal_b, alarm, req, 0, true, fake_exec, &fb, 6000);
    CHECK(alarm.attempts()   == 1);                        // the alarm really was accepted...
    CHECK(alarm.chan_state() == ChanState::idle);          // ★ ...and it does not own `_chan`. The alarm's own
                                                           //   evidence is `EmgEvidence`; `_chan` is the sub-view's.
}

// ★★ AND `waiting` MUST MEAN "WE HOLD A HANDLE" — otherwise B113's fix would only have replaced one wrong reading with
//    another. §B39: `queued` with `ctr == 0` is NOT acceptance, so a handle-less canned post must NOT reach `waiting`;
//    the bounded expiry owns it and it lands in §B69's `unconfirmed`. This is the discrimination the dead state made
//    impossible: before the fix BOTH readings were `submitting`, so the panel could not tell a real send from a
//    handle-less one at all.
TEST_CASE("ui7-B113: `waiting` means WE HOLD A HANDLE — a ctr-less canned post must not reach it") {
    UiModel held, handleless;
    SendTracker emg_a, normal_a; FakeExec fa; fa.reply = ok_ctr(31);
    ui_perform_send(emg_a, normal_a, held, SendReq{SendKind::channel_canned, 0, 0}, 0, false, fake_exec, &fa, 6000);
    SendTracker emg_b, normal_b; FakeExec fb; fb.reply = ok_ctr(0);       // §B39: accepted-shaped, NO local handle
    ui_perform_send(emg_b, normal_b, handleless, SendReq{SendKind::channel_canned, 0, 0}, 0, false,
                    fake_exec, &fb, 6000);
    CHECK(held.chan_state()       == ChanState::waiting);
    CHECK(handleless.chan_state() != ChanState::waiting);      // nothing was ACCEPTED, so nothing may say SENT
    CHECK(held.chan_state() != handleless.chan_state());       // ★ the two readings are finally distinguishable
    CHECK(normal_b.awaiting() == true);                        // ...and the ctr-less one belongs to the bounded expiry
    // ⇒ and it resolves to §B69's UNCONFIRMED, never to `waiting` and never to SENT.
    ui_pump_trackers(emg_b, normal_b, handleless, 6000 + kOutcomeWindowMs + 1);
    CHECK(handleless.chan_state() == ChanState::unconfirmed);
}

// ---- §B69: the canned-channel outcome REACHES the model, and the two collapsed kinds land apart ------------------

// ★★★ B69 CLOSES HERE. `channel_no_relay` and `channel_remote_mint` were indistinguishable to every renderer because
//     `on_outcome` maps them down one path. `on_channel_outcome` is the canned-only entry point and it separates them.
TEST_CASE("ui7-B69: no_relay and remote_mint land in DIFFERENT ChanStates — the carrier B69 asked for") {
    UiModel a, b;
    a.on_channel_outcome(SendOutcome::channel_no_relay(), 1000);
    b.on_channel_outcome(SendOutcome::channel_remote_mint(), 1000);
    CHECK(a.chan_state() == ChanState::no_relay);
    CHECK(b.chan_state() == ChanState::unconfirmed);
    CHECK(a.chan_state() != b.chan_state());               // ★ the whole point: they are no longer the same state
    // ...and neither of them may claim the relay evidence that only `channel_relayed` carries.
    UiModel c; c.on_channel_outcome(SendOutcome::channel_relayed(), 1000);
    CHECK(c.chan_state() == ChanState::relayed);
    CHECK(c.chan_state() != a.chan_state());
    CHECK(c.chan_state() != b.chan_state());
}

TEST_CASE("ui7-B69: a canned post's outcome NEVER moves a live alarm (§2.1), by either route") {
    SendTracker emg;
    UiModel m = alarm_accepted(emg, /*ctr=*/55);
    SendTracker normal;
    normal.submit(SendKind::channel_canned, 0, 0, 5000);
    normal.accept(/*ctr=*/56, 5000);                       // a DIFFERENT handle — a coincident canned post
    MESHROUTE_NS::Push pu = push_of(MESHROUTE_NS::PushKind::channel_sent);
    pu.ctr = 56; pu.relayed = true;                        // it WAS relayed...
    CHECK(ui_route_send_push(emg, normal, m, pu, 6000) == true);
    CHECK(m.chan_state() == ChanState::relayed);           // ...so the SUB-VIEW says so...
    CHECK(m.emergency() == Emergency::firing);             // ...and the ALARM is untouched. Never PICKED UP.
    CHECK(m.attempts() == 1);
}

TEST_CASE("ui7-B69: the NORMAL tracker's expiry reaches the sub-view and still cannot touch the alarm (§B84 blk 2)") {
    SendTracker emg;
    UiModel m = alarm_accepted(emg, /*ctr=*/55);
    SendTracker normal;
    normal.submit(SendKind::channel_canned, 0, 0, 5000);
    normal.awaiting_outcome(5000);                         // a canned post with NO local handle
    ui_pump_trackers(emg, normal, m, 5000 + kOutcomeWindowMs + 1);
    CHECK(m.chan_state() == ChanState::unconfirmed);       // ★ UI-7 supplies what the glue had to DISCARD
    CHECK(m.emergency() == Emergency::firing);             // ★ §B84 blocker 2 still holds: the alarm did NOT move
    CHECK(m.attempts() == 1);                              // ...and no attempt was spent on it
    CHECK(normal.idle() == true);
}

TEST_CASE("ui7-B69: a canned send_blocked reaches the sub-view as BLOCKED, not the alarm") {
    SendTracker emg;
    UiModel m = alarm_accepted(emg, /*ctr=*/55);
    SendTracker normal;
    normal.submit(SendKind::channel_canned, 0, 0, 5000);
    normal.accept(/*ctr=*/56, 5000);
    MESHROUTE_NS::Push pu = push_of(MESHROUTE_NS::PushKind::send_blocked);
    pu.blocked_channel = true; pu.next_ms = 4000;
    // The EMERGENCY slot is offered first and is `accepted`, so it claims it — that is the shipped offer order and
    // it is deliberate (`match_blocked` correlates by WINDOW, not by ctr). Drain the alarm first to reach the
    // canned arm, which is what this case is about.
    emg.close();
    CHECK(ui_route_send_push(emg, normal, m, pu, 6000) == true);
    CHECK(m.chan_state() == ChanState::blocked);
    CHECK(m.emergency() == Emergency::firing);
}

// ---- the sub-view's lifetime bounds the ONE normal slot ---------------------------------------------------------

// ★★★ WITHOUT THIS, ONE UNCONFIRMED DM DISABLES EVERY FURTHER SEND ON THE DEVICE, PERMANENTLY. `match_dm` parks an
//     `e2e_ack_timeout` DM in `late_ack`, which is never `idle`, and `mr_ui_tick` only drains a new request while the
//     normal slot IS idle. H7-06: "no slot remains leaked".
TEST_CASE("ui7-slot: a late_ack slot is released once the sub-view has closed") {
    UiModel m; SendTracker emg, normal; FakeExec f; f.reply = ok_ctr(42);
    UiSnapshot s{}; s.now_ms = 1000; s.team_shown = 1; s.team[0].id = 11;
    m.on_gesture(Gesture::short_press, s);                 // -> TEAM
    m.on_gesture(Gesture::double_press, s);                // -> DM compose
    m.on_gesture(Gesture::double_press, s);                // -> send "Are you OK?"
    SendReq req{}; const bool got = m.take_send_request(req); CHECK(got == true); if (!got) return;
    ui_perform_send(emg, normal, m, req, 0, false, fake_exec, &f, 1000);
    SendOutcome o{};
    CHECK(normal.match_dm(42, 11, /*acked=*/false, FailReason::e2e_ack_timeout, o) == true);
    m.on_outcome(o, 2000);
    CHECK(m.dm_state() == DmState::not_confirmed);
    // While the sub-view is STILL SHOWING the slot is retained, so spec §3.4.1's late-ack UPGRADE still works.
    CHECK(m.compose_open() == true);
    ui_pump_trackers(emg, normal, m, 3000);
    CHECK(normal.idle() == false);                         // ★ the negative half — do NOT close it early
    SendOutcome late{};
    CHECK(normal.match_dm(42, 11, /*acked=*/true, FailReason::none, late) == true);
    m.on_outcome(late, 3100);
    CHECK(m.dm_state() == DmState::delivered);             // NO CONFIRM -> DELIVERED, the upgrade the spec wants
    // ⓘ NOT asserted here that the slot is then released: the successful late ack ALREADY idles it inside `match_dm`
    //   (`if (acked) { _state = State::idle; }`), so a `normal.idle()` check at this point could not fail whatever
    //   `ui_pump_trackers` does. MEASURED — a mutation that removed the close entirely left this case fully green.
    //   The release is proved by the case below, which never receives its late ack.
    m.on_tick(snap_at(3100 + kBlankMs + 1));
    CHECK(m.compose_open() == false);
}

// ★★★ THE LEAK ITSELF, in the ONLY state that can exhibit it: a `late_ack` slot whose late ack NEVER ARRIVES — which
//     is the normal case, since `e2e_ack_timeout` means the peer did not answer. Nothing else releases it, so without
//     the compose-lifetime close the slot stays non-idle FOR EVER and `mr_ui_tick`'s
//     `else if (s_tracker_normal.idle())` gate never opens again: one unconfirmed DM disables every further send on
//     the device. (H7-06: "no slot remains leaked".)
TEST_CASE("ui7-slot: an UNANSWERED late_ack slot is released once the sub-view has gone, or the device is bricked") {
    UiModel m; SendTracker emg, normal; FakeExec f; f.reply = ok_ctr(42);
    UiSnapshot s{}; s.now_ms = 1000; s.team_shown = 1; s.team[0].id = 11;
    m.on_gesture(Gesture::short_press, s);
    m.on_gesture(Gesture::double_press, s);
    m.on_gesture(Gesture::double_press, s);
    SendReq req{}; const bool got = m.take_send_request(req); CHECK(got == true); if (!got) return;
    ui_perform_send(emg, normal, m, req, 0, false, fake_exec, &f, 1000);
    SendOutcome o{};
    CHECK(normal.match_dm(42, 11, /*acked=*/false, FailReason::e2e_ack_timeout, o) == true);
    m.on_outcome(o, 2000);
    CHECK(normal.idle() == false);                         // parked in late_ack, and NOTHING else will free it
    // While the sub-view still shows, it is RETAINED — spec §3.4.1's upgrade window (the negative half).
    ui_pump_trackers(emg, normal, m, 2100);
    CHECK(normal.idle() == false);
    // Once the sub-view has auto-exited, the slot is released and the device can send again.
    m.on_tick(snap_at(1000 + kBlankMs + 1));
    CHECK(m.compose_open() == false);
    ui_pump_trackers(emg, normal, m, 1000 + kBlankMs + 2);
    CHECK(normal.idle() == true);                          // ★ the gate `mr_ui_tick` reads before draining a request
    // ⓘ HONEST SCOPE, stated rather than implied: the assertion above IS the discriminating one, because the gate it
    //   feeds (`else if (s_tracker_normal.idle())`) lives in `firmware_ui.cpp`, which no native test compiles. The
    //   second send below therefore DOCUMENTS reusability; it is not a second control — `ui_perform_send` does not
    //   consult `idle()` itself, so it would succeed either way. That the tick really reads the gate is pinned by
    //   the probe's wiring checks, the same division of labour §R1 used for W6.
    FakeExec f2; f2.reply = ok_ctr(43);
    ui_perform_send(emg, normal, m, SendReq{SendKind::channel_canned, 0, 0}, 0, false, fake_exec, &f2,
                    1000 + kBlankMs + 3);
    CHECK(f2.calls == 1);
    CHECK(std::strcmp(f2.line, "send_channel 0 \"Got your message\" -t -e") == 0);
}

TEST_CASE("ui7-slot: the EMERGENCY slot is never closed by a compose modal") {
    SendTracker emg;
    UiModel m = alarm_accepted(emg, /*ctr=*/55);
    SendTracker normal;
    CHECK(m.compose_open() == false);                      // an alarm has no modal, and must not be bound to one
    ui_pump_trackers(emg, normal, m, 6000);
    CHECK(emg.idle() == false);                            // ★ still holding its handle
    SendOutcome o{};
    CHECK(emg.match_channel_sent(55, true, o) == true);
}

// ================================================================================================== §B115
// ★★★ THE ATTEMPT COUNTER THE PANEL SHOWS — MEASURED WRONG ON METAL, AND THE FIRST READING IS THE DIAGNOSTIC ONE.
// Three emergency posts went out (`45F66601/02/03`, ctr 769-771) and the panel read `2 of 3` -> `3 of 3` -> `4 of 3`.
// The owner confirmed **`1 of 3` was NEVER displayed**, which is what makes the defect a UNIFORM `+1` present from the
// first attempt rather than one extra increment at the end: the renderer emitted `attempts() + 1` unconditionally.
// ⚠⚠ THE FIRST TWO READINGS ARE INDIVIDUALLY PLAUSIBLE. A check asking "does it say `N of 3`?" passes on the bug, and a
//    check keyed on the final `4 of 3` is satisfied by a CLAMP that would leave `2 -> 3 -> 3` — still wrong on every
//    attempt, now permanently invisible ([[B108]]'s rejected pattern). ⇒ these cases assert the EXACT BYTES of the
//    FIRST line, through the two shipped units: the model's ordinal and the one formatter.
// ★ THE AIRTIME BOUND IS NOT UNDER TEST HERE AND MUST NOT MOVE: it held on metal (exactly three `M` ids), `_tries` is
//   its single source of truth, and §B84's whole argument is that `_tries` moves ONLY in `on_send_accepted`. What was
//   wrong is the DISPLAY, so the display gets its own number — see the two-numbers block in firmware_ui_model.h.

namespace {
// The exact panel text for the alarm's CURRENT attempt, built the way `firmware_ui.cpp` builds it — the model's
// ordinal into the one formatter. Nothing here re-derives the number, so a test cannot agree with itself.
void emg_line_now(const UiModel& m, char* out, std::size_t cap) {
    emg_attempt_line(out, cap, m.emg_attempt_ordinal());
}
}  // namespace

// ★★★ THE BUG, END TO END, ON THE ACCEPTED PATH. Every one of the three lines is asserted, and the FIRST one is the
//     assertion that fails on the shipped code.
TEST_CASE("ui7-b115: the accepted alarm's VISIBLE line steps `1 of 3` -> `2 of 3` -> `3 of 3`") {
    UiModel m = armed_and_fired(); SendTracker emg, normal;
    char l[48];

    // ---- attempt 1. Queued but not yet handed to the core: the attempt IS in flight, `_tries` has not counted it.
    emg_line_now(m, l, sizeof l);
    CHECK(std::strcmp(l, "attempt 1 of 3") == 0);           // ★★★ THE READING METAL NEVER SHOWED
    SendReq r1{}; const bool got1 = m.take_send_request(r1); CHECK(got1 == true); if (!got1) return;
    CHECK(r1.kind == SendKind::emergency);
    FakeExec f1; f1.reply = ok_ctr(769);
    ui_perform_send(emg, normal, m, r1, 0, /*have_fix=*/true, fake_exec, &f1, 6000);
    CHECK(f1.calls == 1);
    CHECK(m.attempts() == 1);
    emg_line_now(m, l, sizeof l);
    CHECK(std::strcmp(l, "attempt 1 of 3") == 0);           // ★★★ ACCEPTANCE DOES NOT ADVANCE THE ORDINAL — same attempt
    SendOutcome o1{}; const bool s1 = emg.match_channel_sent(769, /*relayed=*/false, o1);
    CHECK(s1 == true); if (!s1) return;
    m.on_outcome(o1, 7000);
    CHECK(m.emergency() == Emergency::firing);              // bounded retry, not terminal yet

    // ---- attempt 2
    emg_line_now(m, l, sizeof l);
    CHECK(std::strcmp(l, "attempt 2 of 3") == 0);
    SendReq r2{}; const bool got2 = m.take_send_request(r2); CHECK(got2 == true); if (!got2) return;
    FakeExec f2; f2.reply = ok_ctr(770);
    ui_perform_send(emg, normal, m, r2, 0, true, fake_exec, &f2, 8000);
    CHECK(m.attempts() == 2);
    emg_line_now(m, l, sizeof l);
    CHECK(std::strcmp(l, "attempt 2 of 3") == 0);
    SendOutcome o2{}; const bool s2 = emg.match_channel_sent(770, false, o2);
    CHECK(s2 == true); if (!s2) return;
    m.on_outcome(o2, 9000);

    // ---- attempt 3, the last one the budget allows
    emg_line_now(m, l, sizeof l);
    CHECK(std::strcmp(l, "attempt 3 of 3") == 0);
    SendReq r3{}; const bool got3 = m.take_send_request(r3); CHECK(got3 == true); if (!got3) return;
    FakeExec f3; f3.reply = ok_ctr(771);
    ui_perform_send(emg, normal, m, r3, 0, true, fake_exec, &f3, 10000);
    CHECK(m.attempts() == 3);
    emg_line_now(m, l, sizeof l);
    CHECK(std::strcmp(l, "attempt 3 of 3") == 0);           // ⛔ NEVER `4 of 3`
    SendOutcome o3{}; const bool s3 = emg.match_channel_sent(771, false, o3);
    CHECK(s3 == true); if (!s3) return;
    m.on_outcome(o3, 11000);

    // ★ AND THE BOUND IS UNTOUCHED — the reassuring fact from the bench run, restated as a test so a display fix can
    //   never be mistaken for a send-path change: three transmissions, terminal, and NO fourth request.
    CHECK(m.emergency() == Emergency::not_heard);
    CHECK(m.attempts() == kEmgMaxTries);
    SendReq r4{};
    CHECK(m.take_send_request(r4) == false);
}

// ★★★ THE `ctr == 0` HALF, AND IT IS WHY THE FIX IS NOT "DELETE THE `+1`". Such an attempt is IN FLIGHT and
//     deliberately UNCOUNTED (spec §2.1 rule 2 — the §B84 expiry spends it later), so `_tries` is still 0 while the
//     first alarm is on the air. An unconditional `+0` would print `attempt 0 of 3` there: a distress panel claiming
//     nothing has been tried while the radio is transmitting.
TEST_CASE("ui7-b115: a ctr==0 attempt still reads `1 of 3`, never `0 of 3`") {
    UiModel m = armed_and_fired(); SendTracker emg, normal;
    char l[48];
    SendReq req{}; const bool got = m.take_send_request(req); CHECK(got == true); if (!got) return;
    FakeExec f; f.reply = ok_ctr(0);
    ui_perform_send(emg, normal, m, req, 0, /*have_fix=*/false, fake_exec, &f, 6000);
    CHECK(f.calls == 1);
    CHECK(emg.awaiting() == true);                          // parked: no handle, status unknown
    CHECK(m.attempts() == 0);                               // ★ and the LIMIT's counter has genuinely not moved
    emg_line_now(m, l, sizeof l);
    CHECK(std::strcmp(l, "attempt 1 of 3") == 0);           // ★★★ the display still names the attempt that IS flying
    // The bounded expiry then counts it — the SAME attempt, so the SAME line.
    ui_pump_trackers(emg, normal, m, 6000 + kOutcomeWindowMs + 1);
    CHECK(m.attempts() == 1);
    emg_line_now(m, l, sizeof l);
    CHECK(std::strcmp(l, "attempt 2 of 3") == 0);           // ...and `on_outcome` has already opened attempt 2
    CHECK(m.emergency() == Emergency::firing);
}

// ★★★ THE MUTATION CONTROL INDEPENDENT QA NAMED, AS TWO RELATIONS RATHER THAN TWO STRINGS — this is the pair that makes
//     BOTH tempting wrong fixes red, and neither relation can be satisfied by the other's mutation:
//       `ordinal == attempts()`     once the attempt is ACCEPTED   -> RED under an unconditional `+1` (the shipped bug)
//       `ordinal == attempts() + 1` while the attempt is UNCOUNTED  -> RED under an unconditional `+0`
// ⚠ Deliberately expressed against `attempts()` and not against literals: a literal pair would still pass if BOTH the
//   counter and the ordinal drifted together, which is precisely the "display and bound read different state" lead the
//   register recorded. This pins the RELATION between them.
TEST_CASE("ui7-b115: the ordinal is a DIFFERENT number from `attempts()`, and by exactly one attempt") {
    UiModel m = armed_and_fired(); SendTracker emg, normal;
    // Queued, nothing accepted: uncounted.
    CHECK(m.emg_attempt_ordinal() == uint8_t(m.attempts() + 1));
    SendReq r1{}; const bool got1 = m.take_send_request(r1); CHECK(got1 == true); if (!got1) return;
    FakeExec f1; f1.reply = ok_ctr(769);
    ui_perform_send(emg, normal, m, r1, 0, true, fake_exec, &f1, 6000);
    CHECK(m.emg_attempt_ordinal() == m.attempts());          // ★ accepted: counted, so NO `+1`
    SendOutcome o1{}; const bool s1 = emg.match_channel_sent(769, false, o1);
    CHECK(s1 == true); if (!s1) return;
    m.on_outcome(o1, 7000);
    CHECK(m.emg_attempt_ordinal() == uint8_t(m.attempts() + 1));   // the retry re-opens an uncounted attempt
    // ...and the `ctr == 0` arm reaches the uncounted relation from the other direction.
    SendReq r2{}; const bool got2 = m.take_send_request(r2); CHECK(got2 == true); if (!got2) return;
    FakeExec f2; f2.reply = ok_ctr(0);
    ui_perform_send(emg, normal, m, r2, 0, false, fake_exec, &f2, 8000);
    CHECK(m.emg_attempt_ordinal() == uint8_t(m.attempts() + 1));
}

// ★★ A PRE-TX BLOCK CONSUMES NO ATTEMPT (spec §4), SO IT MUST NOT ADVANCE THE DISPLAY EITHER. The panel would
//    otherwise count down the hiker's three alarms while nothing had been transmitted at all — the same class of false
//    statement as B115 itself, arriving from the retry path.
TEST_CASE("ui7-b115: a blocked-then-retried alarm still reads `1 of 3` — a block spends nothing") {
    UiModel m = armed_and_fired(); SendTracker emg, normal;
    char l[48];
    SendReq r1{}; const bool got1 = m.take_send_request(r1); CHECK(got1 == true); if (!got1) return;
    FakeExec f1; f1.reply = ok_ctr(0);                       // a pre-TX self-gate returns queued with no handle
    ui_perform_send(emg, normal, m, r1, 0, false, fake_exec, &f1, 6000);
    SendOutcome ob{}; const bool blocked = emg.match_blocked(/*blocked_channel=*/true, /*next_ms=*/3000, 6500, ob);
    CHECK(blocked == true); if (!blocked) return;
    m.on_outcome(ob, 6500);
    CHECK(m.emergency() == Emergency::blocked);
    CHECK(m.attempts() == 0);
    // The retry deadline fires and re-queues the SAME first attempt.
    m.on_tick(snap_at(6500 + 3000 + 1));
    CHECK(m.emergency() == Emergency::firing);
    emg_line_now(m, l, sizeof l);
    CHECK(std::strcmp(l, "attempt 1 of 3") == 0);            // ⛔ not `2 of 3` — nothing was ever transmitted
    SendReq r2{}; const bool got2 = m.take_send_request(r2); CHECK(got2 == true); if (!got2) return;
    FakeExec f2; f2.reply = ok_ctr(769);
    ui_perform_send(emg, normal, m, r2, 0, true, fake_exec, &f2, 10000);
    CHECK(m.attempts() == 1);
    emg_line_now(m, l, sizeof l);
    CHECK(std::strcmp(l, "attempt 1 of 3") == 0);
}

// ★ A NEW ALARM RESTARTS THE DISPLAY, because it restarts the budget (`long_fire` resets `_tries`). A sticky NOT HEARD
//   re-fired must not open at `4 of 3`.
TEST_CASE("ui7-b115: re-firing a sticky NOT HEARD opens at `1 of 3` again") {
    UiModel m = armed_and_fired();
    char l[48];
    run_ctr0_expiries(m, kEmgMaxTries, /*via_glue=*/true);
    CHECK(m.emergency() == Emergency::not_heard);
    CHECK(m.attempts() == kEmgMaxTries);
    UiSnapshot s{}; s.now_ms = 200000;
    m.on_gesture(Gesture::long_fire, s);
    CHECK(m.emergency() == Emergency::firing);
    CHECK(m.attempts() == 0);
    emg_line_now(m, l, sizeof l);
    CHECK(std::strcmp(l, "attempt 1 of 3") == 0);
}

// ==================================================================================================================
// §T3 — the UI half of `send_aired`: one explicit arm, a NON-CONSUMING correlation, and a SCOPED monotonic rank.
// ==================================================================================================================
// ★★★ THREE PROPERTIES, AND EACH ONE IS A DIFFERENT WAY THIS COULD GO WRONG:
//   (1) it must be CORRELATED — an uncorrelated attempt fact moving the panel is §2.1's false confirmation;
//   (2) it must NOT CONSUME the slot — it is not terminal, and the terminal outcome still has to arrive;
//   (3) it must be RANKED — a delayed `send_aired` must never overwrite a terminal state that already landed.
// ⓘ Helper: `aired_push(dst, ctr)` builds exactly what `Node::push_send_aired_if_owned` enqueues.
static MESHROUTE_NS::Push aired_push(uint8_t dst, uint16_t ctr) {
    MESHROUTE_NS::Push pu = push_of(MESHROUTE_NS::PushKind::send_aired);
    pu.dst = dst; pu.ctr = ctr; return pu;
}

TEST_CASE("ui-T3: a correlated DM send_aired upgrades QUEUED -> SENT and does NOT close the slot") {
    UiModel m; SendTracker emg, normal; FakeExec f; f.reply = ok_ctr(42);
    ui_perform_send(emg, normal, m, SendReq{SendKind::dm, /*peer=*/11, 0}, 0, false, fake_exec, &f, 6000);
    CHECK(m.dm_state() == DmState::waiting_ack);                       // PREMISE: core ACCEPTANCE only — renders `QUEUED`
    // ---- ① CORRELATED ⇒ the upgrade happens, through the ONE explicit arm.
    CHECK(ui_route_send_push(emg, normal, m, aired_push(/*dst=*/11, /*ctr=*/42), 6100) == true);
    CHECK(m.dm_state() == DmState::aired_waiting);                     // ★ the only DM state that may say `SENT, waiting`
    // ---- ② IDEMPOTENT. The core does not de-duplicate, so a second attempt of the same flight arrives here.
    CHECK(ui_route_send_push(emg, normal, m, aired_push(11, 42), 6200) == true);
    CHECK(m.dm_state() == DmState::aired_waiting);
    // ---- ③ ★★★ THE SLOT IS NOT CONSUMED — the terminal outcome still correlates and still lands.
    //      ⚠ `match_dm` CONSUMES (§B70), so this is the ONE call that proves it: had `send_aired` closed the slot,
    //      the ack below would be ignored and the panel would sit on `SENT, waiting` for ever.
    MESHROUTE_NS::Push ack = push_of(MESHROUTE_NS::PushKind::send_e2e_acked); ack.dst = 11; ack.ctr = 42;
    CHECK(ui_route_send_push(emg, normal, m, ack, 6300) == true);
    CHECK(m.dm_state() == DmState::delivered);
    CHECK(m.emergency() == Emergency::idle);                           // ⛔ and it never touched the alarm
}

TEST_CASE("ui-T3: an UNCORRELATED send_aired moves nothing (neither handle nor peer may be approximated)") {
    UiModel m; SendTracker emg, normal; FakeExec f; f.reply = ok_ctr(42);
    ui_perform_send(emg, normal, m, SendReq{SendKind::dm, /*peer=*/11, 0}, 0, false, fake_exec, &f, 6000);
    CHECK(ui_route_send_push(emg, normal, m, aired_push(/*dst=*/11, /*ctr=*/43), 6100) == false);  // wrong handle
    CHECK(m.dm_state() == DmState::waiting_ack);
    CHECK(ui_route_send_push(emg, normal, m, aired_push(/*dst=*/12, /*ctr=*/42), 6100) == false);  // wrong peer
    CHECK(m.dm_state() == DmState::waiting_ack);
    CHECK(ui_route_send_push(emg, normal, m, aired_push(/*dst=*/0,  /*ctr=*/42), 6100) == false);  // the CHANNEL form
    CHECK(m.dm_state() == DmState::waiting_ack);                       // ★ a DM slot never answers a channel push
    // ...and the correct one still works, so the three refusals above are the guard and not an inert consumer.
    CHECK(ui_route_send_push(emg, normal, m, aired_push(11, 42), 6100) == true);
    CHECK(m.dm_state() == DmState::aired_waiting);
}

TEST_CASE("ui-T3: a canned CHANNEL post correlates on the 16-bit handle ALONE, above 255 (§b40)") {
    UiModel m; SendTracker emg, normal; FakeExec f; f.reply = ok_ctr(300);
    ui_perform_send(emg, normal, m, SendReq{SendKind::channel_canned, 0, /*text=*/0}, /*ch=*/0, false, fake_exec, &f, 6000);
    CHECK(m.chan_state() == ChanState::waiting);                       // PREMISE: acceptance -> `QUEUED`
    // ⛔ TRUNCATION IS THE DEFECT THIS PINS: 300 & 0xff == 44, and the low byte must NOT match.
    CHECK(ui_route_send_push(emg, normal, m, aired_push(/*dst=*/0, /*ctr=*/44), 6100) == false);
    CHECK(m.chan_state() == ChanState::waiting);
    CHECK(ui_route_send_push(emg, normal, m, aired_push(/*dst=*/0, /*ctr=*/300), 6100) == true);
    CHECK(m.chan_state() == ChanState::aired);                         // ★ the channel twin of `aired_waiting`
    CHECK(m.dm_state() == DmState::idle);                              // ⛔ the DM machine did not move
    CHECK(m.emergency() == Emergency::idle);
    // ...and the terminal verdict still arrives, because the slot was not consumed.
    MESHROUTE_NS::Push sent = push_of(MESHROUTE_NS::PushKind::channel_sent); sent.ctr = 300; sent.relayed = true;
    CHECK(ui_route_send_push(emg, normal, m, sent, 6200) == true);
    CHECK(m.chan_state() == ChanState::relayed);
}

// ★★★★ §T3-c — THE EMERGENCY PATH. An attempt-level fact must not move a live alarm, AND must not disarm the alarm's
// own reporting. Both halves are asserted, because either one alone would pass a broken implementation:
//   · consume the emergency slot and the following `channel_sent` is ignored ⇒ the alarm never reports;
//   · write the model from the emergency arm and `ChanState`/`EmgEvidence` move for a frame nobody has heard.
TEST_CASE("ui-T3-c: an EMERGENCY send_aired changes NOTHING and RETAINS the slot; its channel_sent still lands") {
    UiModel m = armed_and_fired(); SendReq req{}; SendTracker emg, normal; FakeExec f; f.reply = ok_ctr(77);
    const bool got = m.take_send_request(req); CHECK(got == true); if (!got) return;
    ui_perform_send(emg, normal, m, req, /*ch=*/0, /*have_fix=*/true, fake_exec, &f, 6000);
    CHECK(m.emergency() == Emergency::firing);
    CHECK(m.attempts() == 1);
    const Emergency   emg_before  = m.emergency();
    const ChanState   chan_before = m.chan_state();
    const EmgEvidence ev_before   = m.emg_evidence();

    // ---- ① the push IS correlated (it returns true), so this is not a silent miss...
    CHECK(ui_route_send_push(emg, normal, m, aired_push(/*dst=*/0, /*ctr=*/77), 6100) == true);
    // ---- ② ...and NOTHING moved.
    CHECK(m.emergency()    == emg_before);
    CHECK(m.chan_state()   == chan_before);
    CHECK(m.emg_evidence() == ev_before);
    CHECK(m.attempts()     == 1);                                      // ⛔ no bounded attempt was spent
    CHECK(emg.idle()       == false);                                  // ★★★ the slot is RETAINED
    // ---- ③ the alarm's own terminal outcome still reaches the emergency path.
    MESHROUTE_NS::Push sent = push_of(MESHROUTE_NS::PushKind::channel_sent); sent.ctr = 77; sent.relayed = true;
    CHECK(ui_route_send_push(emg, normal, m, sent, 6200) == true);
    CHECK(m.emergency()    == Emergency::picked_up);                   // ★★★★ the alarm reported, as it must
    CHECK(m.emg_evidence() == EmgEvidence::local_tx);
}

// ★★★★ §T3-c, SECOND HALF — AND IT EXISTS BECAUSE THE FIRST HALF COULD NOT FAIL FOR TWO OF ITS OWN MUTATIONS.
// Measured, not foreseen: with `_chan` on `idle` (an alarm never sets it), BOTH "the emergency arm writes the model"
// and "the model's own `SendKind::emergency` refusal is dropped" leave every assertion above green — the promotion
// is absorbed by the rank's idle-refusal, one layer further in. ⇒ the state this needs is a canned post SITTING IN
// `waiting` while the alarm airs, which is also the real §2.1 crossover: an ALARM's airing must never relabel a
// coincident canned post as `SENT, waiting`.
TEST_CASE("ui-T3-c: an EMERGENCY airing must not relabel a coincident canned post") {
    UiModel m = armed_and_fired(); SendReq req{}; SendTracker emg, normal;
    // A canned post is accepted on the NORMAL slot first, and is left QUEUED.
    { FakeExec fc; fc.reply = ok_ctr(300);
      ui_perform_send(emg, normal, m, SendReq{SendKind::channel_canned, 0, 0}, /*ch=*/0, false, fake_exec, &fc, 5900); }
    CHECK(m.chan_state() == ChanState::waiting);                       // PREMISE: a live canned transaction stands
    // ...and the alarm is accepted on the EMERGENCY slot, with a DIFFERENT handle.
    { const bool got = m.take_send_request(req); CHECK(got == true); if (!got) return;
      FakeExec fe; fe.reply = ok_ctr(77);
      ui_perform_send(emg, normal, m, req, /*ch=*/0, /*have_fix=*/true, fake_exec, &fe, 6000); }
    CHECK(m.attempts() == 1);
    const Emergency emg_before = m.emergency();
    // ★★★★ THE ALARM'S OWN airing. It correlates (the emergency slot claims it) and it must move NOTHING —
    //      neither the alarm nor the canned post standing beside it.
    CHECK(ui_route_send_push(emg, normal, m, aired_push(/*dst=*/0, /*ctr=*/77), 6100) == true);
    CHECK(m.chan_state() == ChanState::waiting);                       // ★★★★ the canned post was NOT relabelled
    CHECK(m.emergency()  == emg_before);
    CHECK(m.attempts()   == 1);
    // ⚠ THE CONTROL: the CANNED post's own airing, on ITS handle, DOES promote — so the refusal above is the
    //   emergency scoping, not an inert panel.
    CHECK(ui_route_send_push(emg, normal, m, aired_push(/*dst=*/0, /*ctr=*/300), 6200) == true);
    CHECK(m.chan_state() == ChanState::aired);
    CHECK(m.emergency()  == emg_before);                               // ...and it still did not touch the alarm
}

// ★★ The MODEL-level twin of the case above: the same crossover, one layer in, so the `SendKind::emergency` refusal
// inside `on_send_aired` has a witness of its own rather than resting on the caller never calling it.
TEST_CASE("ui-T3: on_send_aired(emergency) never promotes a canned post standing in QUEUED") {
    UiModel m = armed_and_fired();
    m.on_send_accepted(SendKind::channel_canned, 5900);
    CHECK(m.chan_state() == ChanState::waiting);
    m.on_send_aired(SendKind::emergency, 6000);
    CHECK(m.chan_state() == ChanState::waiting);                       // ★★★ the alarm's kind may not move the post
    m.on_send_aired(SendKind::channel_canned, 6100);
    CHECK(m.chan_state() == ChanState::aired);                         // ...and the post's own kind still does
}

// ★★★★ §T3-d (design N16) — THE RANK, DRIVEN AT THE MODEL DIRECTLY, AND THAT IS THE WHOLE POINT.
// ⛔ Through `ui_route_send_push` every terminal outcome has ALREADY CLOSED the tracker, so the correlation fails
//    first and the rank mutation is NEVER REACHED — the test would pass without measuring the thing it names, i.e.
//    an instrument that cannot fail, on the very test written to prevent one. These call `on_send_aired` directly.
// ★ FIVE ARMS, not one: an enumerated-subset rank (`DELIVERED`/`relayed` only, the first draft) passes two of them
//   and fails the other three.
TEST_CASE("ui-T3-d: a LATE send_aired never overwrites a terminal state — all five, plus the queued controls") {
    // ---- the DM terminals.
    { UiModel m; m.on_send_accepted(SendKind::dm, 1000); m.on_outcome(SendOutcome::dm_acked(), 1100);
      CHECK(m.dm_state() == DmState::delivered);
      m.on_send_aired(SendKind::dm, 1200);
      CHECK(m.dm_state() == DmState::delivered); }                     // ★ DELIVERED
    { UiModel m; m.on_send_accepted(SendKind::dm, 1000); m.on_outcome(SendOutcome::dm_timeout(), 1100);
      CHECK(m.dm_state() == DmState::not_confirmed);
      m.on_send_aired(SendKind::dm, 1200);
      CHECK(m.dm_state() == DmState::not_confirmed); }                 // ★ NO CONFIRM — the arm a two-state rank misses
    { UiModel m; m.on_send_accepted(SendKind::dm, 1000); m.on_outcome(SendOutcome::dm_no_key(), 1100);
      CHECK(m.dm_state() == DmState::no_key);
      m.on_send_aired(SendKind::dm, 1200);
      CHECK(m.dm_state() == DmState::no_key); }
    { UiModel m; m.on_send_accepted(SendKind::dm, 1000);
      m.on_outcome(SendOutcome::dm_failed(FailReason::no_route), 1100);
      CHECK(m.dm_state() == DmState::failed);
      m.on_send_aired(SendKind::dm, 1200);
      CHECK(m.dm_state() == DmState::failed); }                        // ★ FAILED
    // ---- the CHANNEL terminals.
    { UiModel m; m.on_send_accepted(SendKind::channel_canned, 1000);
      m.on_channel_outcome(SendOutcome::channel_no_relay(), 1100);
      CHECK(m.chan_state() == ChanState::no_relay);
      m.on_send_aired(SendKind::channel_canned, 1200);
      CHECK(m.chan_state() == ChanState::no_relay); }                  // ★ NO RELAY HEARD
    { UiModel m; m.on_send_accepted(SendKind::channel_canned, 1000);
      m.on_channel_outcome(SendOutcome::blocked(500), 1100);
      CHECK(m.chan_state() == ChanState::blocked);
      m.on_send_aired(SendKind::channel_canned, 1200);
      CHECK(m.chan_state() == ChanState::blocked); }                   // ★ BLOCKED
    { UiModel m; m.on_send_accepted(SendKind::channel_canned, 1000);
      m.on_channel_outcome(SendOutcome::channel_relayed(), 1100);
      CHECK(m.chan_state() == ChanState::relayed);
      m.on_send_aired(SendKind::channel_canned, 1200);
      CHECK(m.chan_state() == ChanState::relayed); }
    { UiModel m; m.on_send_accepted(SendKind::channel_canned, 1000);
      m.on_channel_outcome(SendOutcome::channel_remote_mint(), 1100);
      CHECK(m.chan_state() == ChanState::unconfirmed);
      m.on_send_aired(SendKind::channel_canned, 1200);
      CHECK(m.chan_state() == ChanState::unconfirmed); }
    // ---- ★★ THE CONTROLS THAT MAKE THE EIGHT REFUSALS MEAN SOMETHING: from the QUEUED state the SAME call DOES
    //      promote, and the promotion is idempotent. Without these, an `on_send_aired` that did nothing at all would
    //      pass every arm above.
    { UiModel m; m.on_send_accepted(SendKind::dm, 1000);
      CHECK(m.dm_state() == DmState::waiting_ack);
      m.on_send_aired(SendKind::dm, 1100);
      CHECK(m.dm_state() == DmState::aired_waiting);
      m.on_send_aired(SendKind::dm, 1200);
      CHECK(m.dm_state() == DmState::aired_waiting); }
    { UiModel m; m.on_send_accepted(SendKind::channel_canned, 1000);
      CHECK(m.chan_state() == ChanState::waiting);
      m.on_send_aired(SendKind::channel_canned, 1100);
      CHECK(m.chan_state() == ChanState::aired);
      m.on_send_aired(SendKind::channel_canned, 1200);
      CHECK(m.chan_state() == ChanState::aired); }
    // ---- ⛔ AND A NEW TRANSACTION'S RESET STAYS AUTHORITATIVE: an `idle`/`submitting` model refuses the promotion
    //      rather than resurrecting the previous send's panel.
    { UiModel m;
      m.on_send_aired(SendKind::dm, 1000);            CHECK(m.dm_state()   == DmState::idle);
      m.on_send_aired(SendKind::channel_canned, 1000); CHECK(m.chan_state() == ChanState::idle); }
    // ---- ⛔ THE EMERGENCY KIND IS REFUSED AT THE MODEL TOO, so a second caller cannot re-open §T3-c's hole.
    { UiModel m = armed_and_fired();
      const Emergency before = m.emergency();
      m.on_send_aired(SendKind::emergency, 1000);
      CHECK(m.emergency() == before);
      CHECK(m.chan_state() == ChanState::idle); }
}

// ★★ §T3 (design N13, the UI half): `aired` is an UPGRADE and never a BARRIER. A terminal outcome arriving after it
// must still be applied — the failure this pins is a rank that treats `aired` as terminal-ish and swallows the ack.
TEST_CASE("ui-T3: after SENT the terminal verdict still applies, in every direction") {
    { UiModel m; m.on_send_accepted(SendKind::dm, 1000); m.on_send_aired(SendKind::dm, 1100);
      CHECK(m.dm_state() == DmState::aired_waiting);
      m.on_outcome(SendOutcome::dm_timeout(), 1200);
      CHECK(m.dm_state() == DmState::not_confirmed); }
    { UiModel m; m.on_send_accepted(SendKind::channel_canned, 1000); m.on_send_aired(SendKind::channel_canned, 1100);
      CHECK(m.chan_state() == ChanState::aired);
      m.on_channel_outcome(SendOutcome::channel_no_relay(), 1200);
      CHECK(m.chan_state() == ChanState::no_relay); }
}
