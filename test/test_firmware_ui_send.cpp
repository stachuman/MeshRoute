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

// Drive `n` ctr-0 emergency expiries. `consume` selects the ruled order vs the defect, so the requirement and its
// negative control share one body and cannot drift apart.
static void run_ctr0_expiries(UiModel& m, int n, bool consume) {
    SendReq req{}; SendOutcome o{};
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
        const bool expired = emg.tick(t_exp, o);
        CHECK(expired == true);
        if (!expired) return;
        CHECK(o.kind == SendOutcome::Kind::channel_remote_mint);
        if (consume) m.on_send_accepted(SendKind::emergency, t_exp);   // ★ THE RULE, and it must precede the outcome
        m.on_outcome(o, t_exp);
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
    run_ctr0_expiries(m, 3, /*consume=*/true);
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
    run_ctr0_expiries(m, 3, /*consume=*/false);
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
