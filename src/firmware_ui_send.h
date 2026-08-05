// MeshRoute — src/firmware_ui_send.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// ★ The ATTRIBUTION layer (UI-4). Pushes are NODE-WIDE: `channel_sent` / `send_blocked` / `send_failed` /
// `send_e2e_acked` fire for every origination on this node, including console, BLE and canned sends. Feeding them
// straight to the UI model lets an unrelated post complete an emergency that was never transmitted — a FALSE SAFETY
// CONFIRMATION. Nothing reaches `UiModel` until it has matched here, which is what makes a false `PICKED UP`
// structurally impossible rather than merely unlikely (spec §2.1).
//
// DONE here (UI-4): the typed correlation of all five outcome pushes; the `ctr == 0` "no local handle" state and its
// three producers; the bounded outcome window for the ctr-less pushes AND its expiry; the late-ack retention.
// DONE here (UI-6, at the bottom of this file): the TWO-TRACKER GLUE — `ui_pump_trackers` and `ui_route_send_push`.
//   ★★ The two bullets below used to say these were "NOT here, by unit boundary — UI-6 owns them". They MOVED, and the
//   reason is the whole point: as caller obligations in `firmware_ui.cpp` they were unreachable by every automated gate
//   this project has (neither the native suite nor the simulator compiles `src/*.cpp`), and the plan records that the
//   wiring was got WRONG TWICE. As pure functions here they are natively tested and turn red on a revert. See the
//   §UI-6 GLUE block for the two §B84 blockers.
// NOT here, by unit boundary (UI-7 owns it, [[meshroute-mark-done-vs-missing-in-code]]):
//   · CLOSING a `late_ack` slot. Spec §3.4.1 bounds the NO CONFIRM -> DELIVERED upgrade to "while the sub-view is
//     still showing", and that lifetime lives in firmware_ui.cpp ⇒ it must call `close()` when the sub-view closes.
//     Deliberately NOT a timer here: the tracker has no idea what is on the panel, and inventing a second window
//     would be a number that disagrees with the real one. `close()` is tested; the obligation is registered.
//   · rendering. `channel_remote_mint` must read as SENT and not as PICKED UP, and no model state carries that
//     distinction — register B69, still open, still UI-6/UI-7's.
#pragma once
#include <cstdint>
#include "firmware_ui_model.h"

namespace mrui {

// How long an accepted send may still claim a ctr-LESS outcome. ⚠ §B84: only ONE correlator needs it now —
// `send_blocked`. The channel-failure correlator is DELETED (it turned an unattributable push into a TERMINAL
// outcome). Historical wording follows. Both correlators that needed it (`send_blocked` and a
// channel `send_failed`) are emitted SYNCHRONOUSLY inside `Node::on_command`, so they are already in the push ring
// when the UI drains it — 8 s is slack, not a budget.
// ⚠⚠ IT MUST NOT BE APPLIED TO AN EXACTLY-CORRELATED OUTCOME, and that is measured, not stylistic: a team post's
// `channel_sent` arrives after up to `channel_reoffer_team_max_retries`(3) x (`channel_reoffer_delay_ms` 10000 +
// `channel_reoffer_jitter_ms` 2000) ≈ 36 s (protocol_constants.h:449-464), and an e2e ack's own deadline is 60 s
// same-layer / 300 s cross-layer (protocol::e2e_ack_deadline_ms). Windowing those would silently discard the very
// outcome the alarm is waiting for. ⇒ `accepted` NEVER expires; only `awaiting` does.
inline constexpr uint32_t kOutcomeWindowMs = 8000;

// ONE SLOT. The UI owns TWO of these (UI-6): an emergency slot and a normal slot, so an alarm never waits on a DM
// that is waiting on its e2e ack. Their pushes are told apart by kind plus ctr/peer.
class SendTracker {
public:
    // `now_ms` is retained for diagnostics only — see `_submit_ms`.
    void submit(SendKind k, uint8_t peer_id, uint8_t channel_id, uint32_t now_ms) {
        _k = k; _peer = peer_id; _chan = channel_id; _state = State::submitted; _submit_ms = now_ms; _ctr = 0;
    }
    // ★★ §B39: `queued` with `ctr == 0` does NOT mean failure and does NOT mean success — it means THIS NODE MINTED
    // NO CHANNEL CTR, so no local handle exists and whether anything flew is not answerable synchronously
    // (node.cpp:1608-1651). `next_ctr` never yields 0 (it wraps 65535 -> 1, node_mac.cpp:20), so zero is an
    // unambiguous sentinel.
    // ⚠ THEREFORE `accept(0, t)` IS `awaiting_outcome(t)` — the same fact, stated twice. The plan wrote "caller must
    // not call this with 0" and left it unenforced, which made a caller slip DANGEROUS rather than merely wrong: with
    // `_ctr == 0` live in `accepted`, an unrelated ctr-0 `channel_sent` would match exactly and manufacture
    // `PICKED UP`. Normalising here makes that unrepresentable. It is not a silent fallback (C2): there is only one
    // state a zero handle can describe.
    void accept(uint16_t ctr, uint32_t now_ms) {
        if (ctr == 0) { awaiting_outcome(now_ms); return; }
        _ctr = ctr; _accept_ms = now_ms; _state = State::accepted;
    }
    // Accepted-shaped result with ctr == 0: no local handle. One of B39's three producers is a SUCCESS (a registered
    // mobile's delegated GLOBAL post), one is a pre-TX BLOCK and one is a post-mint SEAL FAILURE, and nothing
    // distinguishes them synchronously — so wait for the push that does, or for the window to expire.
    void awaiting_outcome(uint32_t now_ms)     { _ctr = 0; _accept_ms = now_ms; _state = State::awaiting; }
    // A synchronous refusal (a parser reject or an immediate `err_*`): there is no core send, so there is nothing to
    // correlate. `close()` is the caller ABANDONING a live transaction (the sub-view shut, or the emergency took the
    // slot). Same transition, deliberately two names — the call sites mean different things and read as such.
    void refuse()                              { _state = State::idle; }
    void close()                               { _state = State::idle; }
    bool idle() const                          { return _state == State::idle; }
    SendKind kind() const                      { return _k; }

    // `channel_sent` — the ONE exactly-correlated channel outcome. §B40: `ctr` is the full 16-bit origination handle,
    // and it is LOCAL ONLY — the wire carries `ctr & 0xff` inside the channel msg-id, so this must never be matched
    // against a RECEIVED message id.
    // ⓘ `_chan` is deliberately NOT checked, and that is a push-schema limit rather than an omission: spec §2.1's table ONCE promised a `channel_id`
    // scope check — WITHDRAWN 2026-08-04 as unsatisfiable (§B81) — but `Node::emit_channel_sent` sets only `relayed` + `ctr`
    // (node_channel.cpp:850-852) and `emit_send_blocked` only `blocked_channel`/`reason`/`next_ms` (:822-826) —
    // neither push carries a channel id at all. The exact ctr match is strictly stronger here anyway.
    bool match_channel_sent(uint16_t ctr, bool relayed, SendOutcome& out) {
        if (_state != State::accepted) return false;      // `awaiting` has no handle -> nothing to match against
        if (_k == SendKind::dm) return false;
        if (ctr != _ctr) return false;                    // ★ the only reliable correlator
        out = relayed ? SendOutcome::channel_relayed() : SendOutcome::channel_no_relay();
        _state = State::idle; return true;
    }
    // `send_blocked` carries NO ctr at all (`emit_send_blocked`, node_channel.cpp:822). Scope by channel-ness + a
    // bounded window. ⚠ This is WEAKER than exact matching and must not be described as exact attribution — it is
    // the best the current push schema allows.
    // ⚠ BOTH `accepted` and `awaiting` are accepted, and that is a CONFIRMED premise rather than laxity: on a
    // `-t -g` post the team copy mints a ctr and stamps `_last_channel_origin_ms` (node_channel.cpp:752), which then
    // blocks the GLOBAL copy on `channel_min_interval_ms` (:633) — a genuine `send_blocked` while `ctr != 0`.
    bool match_blocked(bool blocked_channel, uint32_t next_ms, uint32_t now_ms, SendOutcome& out) {
        if (_state != State::accepted && _state != State::awaiting) return false;
        if (_k == SendKind::dm) return false;
        if (!blocked_channel) return false;
        if (uint32_t(now_ms - _accept_ms) > kOutcomeWindowMs) return false;   // wrap-safe unsigned difference
        out = SendOutcome::blocked(next_ms);
        _state = State::idle; return true;
    }
    // ⛔⛔ THERE IS DELIBERATELY NO `match_channel_failed`, AND IT MUST NOT BE REINTRODUCED (owner-ruled 2026-08-04,
    // register B80). A ctr-less async `send_failed` is NOT ATTRIBUTABLE — the core says so outright: the post-mint
    // seal failure "burns" the counter and "the reason arrives asynchronously and correlates with nothing"
    // (node_channel.cpp:~723-744, restated at node.cpp:1626-1628).
    // ★★ AND `dst == 0` IS NOT A CHANNEL DISCRIMINATOR — that was a CONVERSE ERROR in the first fix, mine. Verifying
    // that *every channel producer passes dst = 0* establishes channel ⇒ dst 0; it does NOT establish dst 0 ⇒ channel,
    // and six unrelated operations emit exactly that shape (`send_layer`'s `unsealable` arms at node_mac.cpp:220/452/
    // 473/561/579 and node_mac.cpp:59/111 among them, all with `/*ctr=*/0`). A matcher built on it would attribute a
    // colliding `send_layer` refusal to the alarm and land it in TERMINAL `Emergency::failed` — the same false
    // negative, reached by a different route. ★ The sound check enumerates everything that EMITS the value, not
    // everything the feature touches.
    // ⇒ The channel failure path is covered from BOTH ends instead: the PREFLIGHT refusals are SYNCHRONOUS
    // (`exec_command` -> `refuse_reason_of` -> `on_send_refused` -> `Emergency::failed`, with the exact reason), and
    // the post-mint seal failure is handled by `tick()`'s bounded expiry below. `SendOutcome::channel_failed()`
    // therefore has NO producer here; that is the owner's accepted cost — this rare path loses its precise terminal
    // REASON in exchange for being attributable and bounded at all.
    // `send_e2e_acked` / `send_failed` for a DM — matched on ctr AND peer (spec §2.1 rule 5).
    // ★ The FULL reason reaches the model (§B73, spec :147): an `acked`/`no_pubkey` bool pair makes NO CONFIRM
    // unreachable and collapses every other failure into something the user cannot act on.
    bool match_dm(uint16_t ctr, uint8_t dst, bool acked, FailReason r, SendOutcome& out) {
        if ((_state != State::accepted && _state != State::late_ack) || _k != SendKind::dm) return false;
        if (ctr != _ctr || dst != _peer) return false;    // ★ ctr AND peer
        // ★ In `late_ack` the ONLY thing that may still fire is the ack itself. The core deliberately permits
        // `send_e2e_acked` after `e2e_ack_timeout` (command.h:254), so we retain identity to UPGRADE NO CONFIRM to
        // DELIVERED (spec §3.4.1) — but letting a second, later `send_failed` through would DOWNGRADE an already
        // reported `not_confirmed` to a generic `failed`, discarding exactly the distinction command.h insists on
        // ("delivery was never CONFIRMED, NOT that it failed"). A repeat timeout is likewise not news.
        if (_state == State::late_ack && !acked) return false;
        if (acked) { out = SendOutcome::dm_acked(); _state = State::idle; return true; }
        out = (r == FailReason::no_pubkey)       ? SendOutcome::dm_no_key()
            : (r == FailReason::e2e_ack_timeout) ? SendOutcome::dm_timeout()
                                                 : SendOutcome::dm_failed(r);   // §B73: thread the reason
        // ⓘ `dm_no_key` / `dm_timeout` carry the reason too (SendOutcome::reason defaults to `none`, so the two
        // dedicated kinds would otherwise report `none` beside a state that IS its reason). Set it uniformly here so
        // a renderer never has to know which kinds happen to carry it.
        out.reason = r;
        _state = (r == FailReason::e2e_ack_timeout) ? State::late_ack : State::idle;
        return true;
    }

    // ★★ THE WINDOW'S EXPIRY, and the ONLY producer of `channel_remote_mint` anywhere in the tree.
    // B39's producer (3) — a registered mobile's plain/`-g` GLOBAL post — "emits no CHANNEL-level push at all, only
    // the wrapper DM's own send_acked/send_failed, under a ctr this caller never saw" (node.cpp:1631-1634, MEASURED).
    // ⇒ nothing can ever close an `awaiting` slot on that path, so without this the alarm/compose sits on
    // `SENDING...` for ever — §B72's defect one level up — and UI-3's eighth kind has no caller at all.
    // ★ The expiry is reported as a SUCCESS shape on purpose (§B68): "a delivered message called failed" is the exact
    // error the kind was added to prevent. It matches the approved "accepted by the transmitter is what we can
    // establish" policy, and it also covers a DROPPED push (the ring is bounded at `cap_push_ring`).
    //
    // ★★★ THE CALLER OBLIGATION, AND IT IS SAFETY-CRITICAL — NOT A STYLE NOTE (owner-ruled 2026-08-04, register B84):
    // ON THE EMERGENCY SLOT, `on_send_accepted(SendKind::emergency, now_ms)` MUST BE CALLED **BEFORE** THE
    // `channel_remote_mint` IS PASSED TO `on_outcome`, SO THE EXPIRY CONSUMES ONE BOUNDED ATTEMPT.
    // ⚠ The earlier claim that this path "fails safe to NOT HEARD" was FALSE, and the measurement is arithmetic:
    // `UiModel::_tries` increments ONLY in `on_send_accepted` ("ACCEPTED transmissions, never requests"), and a
    // `ctr == 0` send never reaches it. Without the consumption the cycle is
    //     seal failure -> awaiting -> 8 s -> channel_remote_mint -> on_outcome -> re-queue -> awaiting -> 8 s -> …
    // FOR EVER with `attempts() == 0`, because `_tries >= kEmgMaxTries` can never become true. That is UNBOUNDED
    // AIRTIME on the distress path — in that one dimension worse than the permanent `SENDING...` it replaced.
    // ⇒ With the consumption: three expiries spend the three-alarm budget and the third terminates in sticky
    // `NOT HEARD` with no fourth request queued. If the missing push really was a failure, an attempt has been
    // honestly spent. Pinned by the four integration cases in test_firmware_ui_send.cpp, which are the tripwire for
    // the UI-6 glue that owes this call.
    bool tick(uint32_t now_ms, SendOutcome& out) {
        if (_state != State::awaiting) return false;                          // `accepted` never expires — see above
        if (uint32_t(now_ms - _accept_ms) <= kOutcomeWindowMs) return false;
        _state = State::idle;
        // A DM reaches `awaiting` only via a HASH-addressed send parked behind an H resolve
        // (node_hashlocate.cpp; "the ctr if sent immediately, else 0"), which the UI never issues — spec §3.4 sends
        // by team_local_id. Guarded anyway: `channel_remote_mint` for a DM would be a type error, so release the slot
        // and invent NOTHING. ⚠ The model is then left in `DmState::submitting`; that display residue is UI-7's, and
        // it is registered rather than papered over with a fabricated reason.
        if (_k == SendKind::dm) return false;
        out = SendOutcome::channel_remote_mint();
        return true;
    }

    // Diagnostic read of the live slot, for the UI-6 glue's own tests. Never a decision input.
    bool awaiting() const { return _state == State::awaiting; }

private:
    enum class State : uint8_t { idle = 0, submitted, accepted, awaiting, late_ack };
    State    _state = State::idle;
    SendKind _k = SendKind::emergency;
    uint8_t  _peer = 0;
    // ⚠ WRITTEN, NEVER READ — and by CONSTRUCTION, not by omission (the same in-source honesty UiModel's
    // `_last_try_ms` gets). Spec §2.1's table asks for a `channel_id` scope check, but no outcome push carries a
    // channel id: `emit_channel_sent` sets `relayed` + `ctr` only and `emit_send_blocked` sets no id at all. Kept
    // because `submit()` is the natural place to record what was sent and UI-6 already has the value; the day a push
    // grows a `channel_id`, this is where the check goes.
    uint8_t  _chan = 0;
    uint16_t _ctr = 0;
    // ⚠ Also written-never-read: the outcome window runs from ACCEPTANCE (spec §2.1's `accepted_ms`), and
    // `submitted` is a synchronous instant — `mrfw::exec_command` returns before the next statement, so no window can
    // usefully bound it. Retained so `submit()`'s `now_ms` is not a `(void)`-cast parameter, and as the timestamp any
    // future submit-side diagnostic would need.
    uint32_t _submit_ms = 0;
    uint32_t _accept_ms = 0;
};

// ====================================================================================================== UI-6 GLUE
// ★★★ THE TWO-TRACKER WIRING, AND IT LIVES HERE FOR ONE REASON: EVERYTHING ABOVE CALLED IT A "CALLER OBLIGATION", AND
// A CALLER OBLIGATION IS NOT A GATE.
//
// The block above documents three obligations at length (offer the emergency slot FIRST; call `on_send_accepted`
// BEFORE the expiry's outcome; never route the NORMAL tracker's expiry into the emergency-capable entry point) and the
// plan says of them: *"I got this wrong twice, so copy it, don't improvise."* Twice-wrong, load-bearing on the distress
// path, and — until now — living as prose in a `.cpp` that NEITHER the native suite NOR the simulator compiles.
//
// ⚠⚠ AND THE FOUR "REQUIRED INTEGRATION REGRESSIONS" DID NOT COVER IT. They exist (test_firmware_ui_send.cpp), they are
//    green, and they pin the RULE — but they HAND-REPLICATE the wiring, so they could not have failed for a
//    mis-wired `mr_ui_tick`. That is exactly the "ask whether the check COULD have failed" test, and it fails it.
//    ⇒ these two functions ARE the shipped glue; `firmware_ui.cpp` does nothing but call them, and the tests now drive
//    THEM. A revert of either rule turns the suite red.
// ⓘ Pure by construction — `SendTracker`, `UiModel` and `MESHROUTE_NS::Push` are all board-free, so this compiles into
//    the native suite unchanged. No Arduino, no g_node, no display: the same boundary rule as the rest of this header.

// Drive BOTH trackers' bounded outcome window. Call it FIRST in the tick, before any paint decision.
// ★★ §B79: without this an `awaiting` slot is never closed — the alarm sits on `SENDING...` for ever and the send slot
//    leaks permanently. `tick()` is also `channel_remote_mint`'s ONLY producer anywhere in the tree.
inline void ui_pump_trackers(SendTracker& emg, SendTracker& normal, UiModel& m, uint32_t now_ms) {
    // ★★★ §B84 BLOCKER 1 — `on_send_accepted` MUST COME FIRST. `UiModel::_tries` moves ONLY there, and a `ctr == 0`
    // send never reaches it, so an expiry that merely re-queues can NEVER satisfy `_tries >= kEmgMaxTries`:
    //     seal failure -> awaiting -> window -> channel_remote_mint -> re-queue -> awaiting -> ... FOR EVER,
    // with `attempts() == 0`. That is UNBOUNDED AIRTIME on the distress path — worse in that dimension than the
    // permanent `SENDING...` it replaced. With the consumption, three expiries spend the budget and terminate in
    // sticky NOT HEARD. The ordering is safety-critical, not stylistic.
    SendOutcome emg_out{};
    if (emg.tick(now_ms, emg_out)) {
        m.on_send_accepted(SendKind::emergency, now_ms);   // consume ONE bounded attempt
        m.on_outcome(emg_out, now_ms);
    }
    // ★★★ §B84 BLOCKER 2 — the NORMAL tracker's expiry must NEVER reach the emergency model. `tick()` yields a CHANNEL
    // kind, and `on_outcome` lets a channel outcome move any LIVE alarm, so a canned post's expiry could alter a live
    // emergency. DRAINING the slot is this call's whole job here (that is the leak fix); routing its outcome into the
    // emergency-capable entry point is what was unsafe.
    // ✖ MISSING, stated so it is not mistaken for done: the canned sub-view's own presentation update. There is NO
    //   canned-only entry point on `UiModel` today — `on_outcome` is the only one — so TASK 7 owns adding it. Until
    //   then the expiry is consumed and NOT routed. ⛔ Do not "fix" this by calling `on_outcome`.
    SendOutcome normal_out{};
    (void)normal.tick(now_ms, normal_out);
}

// Correlate ONE node-wide outcome push into the UI. Returns true if some slot claimed it (diagnostic; an unmatched push
// being IGNORED is the entire point of the tracker).
// ★ The emergency slot is offered EVERY push FIRST — `match_blocked` correlates by WINDOW rather than by ctr, so
//   offer-order is the only thing that stops an abandoned canned post from claiming the alarm's outcome. On the two DM
//   kinds the emergency offer is provably inert (`match_dm` requires `_k == dm`, and the emergency slot is only ever
//   submitted as `SendKind::emergency`) — it is made anyway so there is no per-kind exception to remember.
inline bool ui_route_send_push(SendTracker& emg, SendTracker& normal, UiModel& m,
                              const MESHROUTE_NS::Push& pu, uint32_t now_ms) {
    using PK = MESHROUTE_NS::PushKind;
    SendOutcome o{};
    switch (pu.kind) {
        case PK::channel_sent:
            if (emg.match_channel_sent(pu.ctr, pu.relayed, o))    { m.on_outcome(o, now_ms); return true; }
            // A canned post's outcome: claimed, so it cannot then be offered to the alarm, but NOT routed — the model
            // has no canned-only entry point (Task 7). `on_outcome` here would move a live alarm.
            return normal.match_channel_sent(pu.ctr, pu.relayed, o);
        case PK::send_blocked:
            if (emg.match_blocked(pu.blocked_channel, pu.next_ms, now_ms, o)) { m.on_outcome(o, now_ms); return true; }
            return normal.match_blocked(pu.blocked_channel, pu.next_ms, now_ms, o);
        case PK::send_e2e_acked:
            if (emg.match_dm(pu.ctr, pu.dst, /*acked=*/true, FailReason::none, o)) { m.on_outcome(o, now_ms); return true; }
            if (normal.match_dm(pu.ctr, pu.dst, /*acked=*/true, FailReason::none, o)) { m.on_outcome(o, now_ms); return true; }
            return false;
        case PK::send_failed:
            // ★★ §B84: THERE IS NO EMERGENCY ARM, and `match_channel_failed` must not be reintroduced. `dst == 0` does
            // NOT mean "channel" — six unrelated operations emit exactly that shape — so an unattributable async
            // failure is IGNORED and the bounded retry reaches NOT HEARD. Fails safe. The emergency's own preflight
            // refusals arrive SYNCHRONOUSLY instead (exec_command -> on_send_refused -> Emergency::failed).
            // ⓘ The emergency slot is still OFFERED here, and is still provably inert, for the no-exceptions reason
            //   above: `match_dm` refuses a non-DM slot.
            if (emg.match_dm(pu.ctr, pu.dst, /*acked=*/false, pu.reason, o)) { m.on_outcome(o, now_ms); return true; }
            if (normal.match_dm(pu.ctr, pu.dst, /*acked=*/false, pu.reason, o)) { m.on_outcome(o, now_ms); return true; }
            return false;
        // ⓘ `default:` is correct here and is NOT §B72's -Wswitch hole: `PushKind` has 15 members on core's schedule
        //    and this unit is interested in exactly four. The kinds the UI renders rather than correlates
        //    (`msg_recv` / `channel_recv`) are handled by firmware_ui.cpp, which owns the counters they feed.
        default: return false;
    }
}

}  // namespace mrui
