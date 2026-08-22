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
// DONE here (UI-6, at the bottom of this file): the TWO-TRACKER GLUE — `ui_pump_trackers` and `ui_route_send_push`,
// and (2026-08-05, §B103) the RECEIVE half `ui_route_recv_push` — the unread counters, the recency stamps and the one
// decision with safety weight on it: whether an arriving channel post may be shown as an answer to our distress call.
//   ★★ The two bullets below used to say these were "NOT here, by unit boundary — UI-6 owns them". They MOVED, and the
//   reason is the whole point: as caller obligations in `firmware_ui.cpp` they were unreachable by every automated gate
//   this project has (neither the native suite nor the simulator compiles `src/*.cpp`), and the plan records that the
//   wiring was got WRONG TWICE. As pure functions here they are natively tested and turn red on a revert. See the
//   §UI-6 GLUE block for the two §B84 blockers.
// DONE here (UI-7, the §UI-7 SEND block at the bottom): the SEND ITSELF — line composition (including §4.1's
// conditional `-l`), the `CmdCode` -> panel-reason mapping, and the three-rule send driver that reads `ctr == 0` as
// "no local handle" rather than as failure. `src/firmware_ui.cpp` supplies only an EXECUTOR, the fix bit and the
// channel id; everything a wrong answer could hurt is here, under the native gate.
//   · CLOSING a `late_ack` slot — the caller obligation this header used to record — is DISCHARGED in
//     `ui_pump_trackers`: the compose sub-view's lifetime bounds the one normal slot, so an unconfirmed DM can no
//     longer disable every later send. Still deliberately NOT a timer: the bound is the real display window, not a
//     second number invented here to disagree with it.
//   · §B69's rendering is CARRIED, and its premise is CORRECTED. `channel_remote_mint` now lands in its own model
//     state (`ChanState::unconfirmed`) and its own alarm evidence (`EmgEvidence::no_handle`) — see
//     firmware_ui_model.h, which also measures why "render it as SENT" would have been a FALSE CONFIRMATION on the
//     `-t` line this UI actually sends.
// NOT here, by unit boundary ([[meshroute-mark-done-vs-missing-in-code]]):
//   · a DM that comes back `queued` with `ctr == 0`. `tick()` releases the slot and invents nothing (a
//     `channel_remote_mint` for a DM would be a type error), so the model is left on `DmState::submitting` until the
//     sub-view is closed. Never a false claim, but it answers nothing — register B111, with the measurement that
//     made it reachable at all.
//     ⓘ CORRECTED 2026-08-21 (§UI-17 S2, V1): this read *"until the sub-view's own kBlankMs auto-exit closes it.
//       Bounded and never a false claim"*. §9 R-1 DELETED that auto-exit, so the sub-view now closes on `BACK`, on
//       either press acknowledging the result, or on `long_fire` — i.e. the display is bounded by the OPERATOR, not
//       by a timer. The B111 gap itself is unchanged: `SENDING...` still never becomes a claim it cannot support.
#pragma once
#include <cstdint>
#include <cstddef>   // std::size_t
#include <cstdio>    // snprintf — UI-7 composes the console line HERE so the native suite can assert it byte-for-byte
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

    // ★★★ §T3 — `send_aired`, THE ONE NON-TERMINAL CORRELATION IN THIS CLASS, AND IT IS `const` FOR THAT REASON.
    // Every matcher above ENDS a transaction (`_state = State::idle`). This one must not: `send_aired` says the
    // frame physically left the radio, and the terminal outcome — `channel_sent`, `send_failed`, the E2E ack, the
    // ack timeout — still has to arrive and still has to be able to correlate. Consuming the slot here would
    // silently disarm the very outcome the panel (and, on the emergency slot, the ALARM) is waiting for.
    // ★ It is otherwise as strict as its terminal siblings, and the two planes are structurally DISJOINT:
    //     DM      -> `_k == dm`,  exact `ctr` AND `dst == _peer` (the DM's own origination handle);
    //     CHANNEL -> `_k != dm`,  exact `ctr` AND `dst == 0`.
    //   ⚠ `dst == 0` is used here as a CONFIRMATION of the channel form, never as a discriminator on its own —
    //     §B84's converse error. The core emits `dst = 0` for exactly the channel row, and requiring it is what
    //     stops a DM push whose ctr happens to equal a live channel handle from claiming the channel slot.
    //   ⚠ `_ctr` is the FULL 16-bit handle and is compared whole: truncating anywhere on this path re-creates §b40.
    //   ⓘ `accepted` only. `awaiting` holds no handle (ctr == 0) and `late_ack` is already terminal on the panel.
    bool match_aired(uint8_t dst, uint16_t ctr) const {
        if (_state != State::accepted) return false;
        if (ctr == 0 || ctr != _ctr) return false;     // `next_ctr` never yields 0, so 0 is an unambiguous non-handle
        return (_k == SendKind::dm) ? (dst == _peer) : (dst == 0);
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
    // ✔ SUPPLIED BY UI-7, and the ⛔ above still stands verbatim: the canned outcome goes to `on_channel_outcome`,
    //   the CANNED-ONLY entry point, never to `on_outcome`. `on_outcome` is emergency-capable — every channel kind it
    //   receives may move a LIVE alarm — so routing a canned post's expiry through it is precisely the §2.1 crossover
    //   that blocker 2 exists to prevent. Two entry points, one per slot; the tracker cannot tell them apart because
    //   both carry channel kinds and both are correctly correlated. ⛔ Still do not call `on_outcome` here.
    SendOutcome normal_out{};
    if (normal.tick(now_ms, normal_out)) m.on_channel_outcome(normal_out, now_ms);
    // ★★ UI-7 — THE SUB-VIEW'S LIFETIME BOUNDS THE ONE NORMAL SLOT, and this discharges the `close()` obligation this
    //    file has carried since UI-4 ("CLOSING a `late_ack` slot ... that lifetime lives in firmware_ui.cpp ⇒ it must
    //    call `close()`"). It is here, not there, for the reason the block above argues: a caller obligation is not a
    //    gate, and this one has a HARD failure mode. `match_dm` parks an `e2e_ack_timeout` DM in `late_ack`, which is
    //    never `idle`, and `mr_ui_tick` only drains a new request when the normal slot IS idle ⇒ without this, ONE
    //    unconfirmed DM disables every further canned post and DM on the device, permanently. (H7-06: "no slot
    //    remains leaked".)
    // ★ THE SUB-VIEW IS THE ONLY RENDERER of a normal outcome, so a slot outliving it can only leak: spec §3.4.1
    //   bounds the NO CONFIRM -> DELIVERED upgrade to "while the sub-view is still showing", and once it is gone there
    //   is nothing an arriving ack could update.
    // ⚠⚠ THE COST MOVED 2026-08-21 (§UI-17 S2, §9 R-1) AND IT MOVED IN BOTH DIRECTIONS — recorded, not discovered
    //   later. ⛔ WITHDRAWN WORDING, KEPT VISIBLE: *"HONEST COST … the modal auto-exits after kBlankMs while a team
    //   `channel_sent` can take ~36 s and an e2e ack up to 60 s, so a late verdict is ABANDONED rather than
    //   displayed. That is a display loss, never a false claim — and the alternative is a device that cannot send
    //   again for a minute."*
    //   ⇒ THERE IS NO 15 s AUTO-EXIT ANY MORE. **The gain:** the upgrade window is now the whole time the operator
    //   leaves the sub-view up — blanking preserves it — so the ~36 s / 60 s late verdicts this paragraph called
    //   abandoned are now the ones most likely to be SEEN. **The price, stated plainly:** the one normal slot is held
    //   for exactly as long, so until the operator acknowledges the result (`BACK`, either press on the RESULT
    //   phase, or `long_fire`) `mr_ui_tick` drains no further UI send. ⛔ The EMERGENCY slot is untouched by all of
    //   this — an alarm has no modal — so the safety path cannot be blocked by a forgotten compose.
    // ⓘ The EMERGENCY slot is deliberately untouched: an alarm has no modal and must never be closed by one.
    if (!m.compose_open() && !normal.idle()) normal.close();
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
            // ★ UI-7: a canned post's outcome now REACHES the sub-view, through the canned-only entry point. It is
            //   still never `on_outcome` — that would let a canned post move a live alarm (§2.1).
            if (normal.match_channel_sent(pu.ctr, pu.relayed, o)) { m.on_channel_outcome(o, now_ms); return true; }
            return false;
        case PK::send_blocked:
            if (emg.match_blocked(pu.blocked_channel, pu.next_ms, now_ms, o)) { m.on_outcome(o, now_ms); return true; }
            if (normal.match_blocked(pu.blocked_channel, pu.next_ms, now_ms, o)) { m.on_channel_outcome(o, now_ms); return true; }
            return false;
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
        // ★★★ §T3 — THE EXPLICIT `send_aired` ARM. ⛔ It MUST be spelled out here and must never be left to the
        //     `default:` below (or to `firmware_ui.cpp:mr_ui_on_push`'s own `default:`): both would silently ignore
        //     the new kind and the whole app half of [[B164]] would compile and pass while doing nothing.
        // ★ THE EMERGENCY SLOT IS OFFERED FIRST, exactly as it is for every other kind — and when it claims the
        //   push the model is NOT touched at all. That is the point, not an omission: an attempt-level fact must
        //   leave `Emergency`, `ChanState` and `EmgEvidence` UNCHANGED (an alarm's evidence is about what was
        //   HEARD), and the slot is RETAINED so the alarm's own `channel_sent` still reaches `on_outcome`.
        // ★ The normal slot promotes through `on_send_aired`, which applies the SCOPED rank in `UiModel`:
        //   queued -> aired, aired idempotent, every terminal state refuses.
        // ⛔ Neither arm closes its tracker — see `match_aired`, which is `const` for exactly this reason.
        case PK::send_aired:
            if (emg.match_aired(pu.dst, pu.ctr))    return true;   // correlated, and DELIBERATELY inert on the model
            if (normal.match_aired(pu.dst, pu.ctr)) { m.on_send_aired(normal.kind(), now_ms); return true; }
            return false;
        // ⓘ `default:` is correct here and is NOT §B72's -Wswitch hole: `PushKind` has 17 members on core's schedule
        //    and this unit is interested in exactly five. The kinds the UI renders rather than correlates
        //    (`msg_recv` / `channel_recv`) are handled by firmware_ui.cpp, which owns the counters they feed.
        default: return false;
    }
}

// ★★★ THE RECEIVE HALF (§B103 / QA finding F4). `ui_route_send_push` above correlates what WE sent; this correlates
// what ARRIVED — the unread counters, the recency stamps, and the one decision with safety weight on it: whether an
// incoming channel post may be shown as an ANSWER TO OUR DISTRESS CALL.
//
// ⚠⚠ IT LIVES HERE, NOT IN `firmware_ui.cpp`, FOR THE REASON THE BLOCK ABOVE ALREADY ARGUES: as a caller obligation it
//    was unreachable by every automated gate this project has, and it shipped WRONG — the guard was
//    `pu.channel_id == MR_UI_TEAM_CHANNEL_ID` alone.
//
// ★★ WHY THAT WAS A LIVE SAFETY DEFECT, and the clause that fixes it is NOT the one it looks like:
//    `Node::ingest_channel_m` (node_channel.cpp:211-212) DROPS a foreign TEAM's M — so the channel-id equality is
//    already implied for team traffic and adds nothing. But its own comment records that **a normal leaf M
//    (`team_id == 0`) falls through and is ingested by EVERYONE**. With `MR_UI_TEAM_CHANNEL_ID == 0`, any node in
//    radio range posting plaintext on channel 0 — no team membership, no key, no crypto — rendered as
//    "someone answered my distress call". That is precisely the §2.1 false-confirmation class.
// ⇒ **THE CLAUSE CARRYING THE SAFETY WEIGHT IS `team_id != 0`**, i.e. "this post was scoped to a team AND that team is
//    OURS". Stated here because it looks redundant beside the equality and a later reader would delete it.
// ★ `same_team_post` is the caller's `g_node.same_team(pu.team_id)` (node.h:274 — `_cfg.team_id != 0 && their_team ==
//   _cfg.team_id`), reused rather than re-derived (U1). It is passed IN because `Node` is not board-free enough to
//   name here, and because the equivalence is worth stating: `same_team(t)` ⟺ `t != 0 && t == our_team` — forward,
//   `t == our_team != 0` gives `t != 0`; backward, `t != 0 && t == our_team` gives `our_team != 0`. So the two-clause
//   helper IS the three-clause guard, not an approximation of it.
// ⓘ Consequence, deliberate and ruled: on a node with NO team (`team_id == 0`, every static leaf and every lone
//   mobile) the REPLY indication is now unreachable. Without a team there is no key and no membership, so there is
//   nothing that could make a reply trustworthy — refusing to claim one is the point, not a regression.
//
// ================================================================================================= UI-7 — THE SEND
// ★★★ THE SEND ITSELF, AND IT IS PURE FOR THE §UI-6 GLUE REASON RESTATED ONE LAST TIME: `ui_perform_send` shipped in
// `src/firmware_ui.cpp` as a LOUD REFUSAL STUB precisely because that TU is compiled by neither the native suite nor
// the simulator, and every safety rule the plan writes about this function — the conditional `-l`, the typed result,
// the `ctr == 0` reading, `on_send_accepted`'s placement — would have been a caller obligation nobody could gate.
// ⇒ everything here is board-free. The device supplies exactly THREE facts through the seam below: the command
//   EXECUTOR, whether we hold a position fix, and the team channel id. A native test supplies a fake executor that
//   RECORDS THE LINE, which is how "the right command was issued" becomes an assertion about a SIDE EFFECT rather
//   than about a post-hoc enum (§B97/§B98/§B110: the shipped path closes its modal as it sends, so `compose == none`
//   is green against a real mis-send — only the issued request discriminates).

// The synchronous result of running ONE composed line, in the UI's own terms. It is `mrfw::ExecResult` minus the
// things no UI decision reads: `mrfw` lives behind `<Arduino.h>`, so the seam is a 3-field POD and the device TU does
// the 3-field copy. `ok == false` means the line never became a `Command` at all (a parser reject).
struct SendExec {
    bool                  ok   = false;
    MESHROUTE_NS::CmdCode code = MESHROUTE_NS::CmdCode::queued;   // meaningful only when `ok`
    uint16_t              ctr  = 0;                               // the origination handle; 0 = NO LOCAL HANDLE (§B39)
};

// The device's command executor. A raw function pointer, not a `std::function`: no heap, no RTTI, and a captureless
// lambda decays to it — the same idiom `Inbox::pull`'s `PullCb` uses (U3).
using SendExecFn = SendExec (*)(const char* line, std::size_t len, void* ctx);

// ★ THE COMPACT PANEL REASON for a synchronous refusal. ⚠ IT IS DELIBERATELY ALMOST CONSTANT, and that is a MEASURED
//   property of the core rather than laziness: on the two lines this UI sends, five different walls — `no_key`,
//   `no_identity`, `no_fix`, `empty` and `unsealable` — all return `CmdCode::err_unsupported` (node.cpp:1530, :1543,
//   :1553, :1568) and differ only in an `MR_EMIT` string the UI never sees. The plan's rule is explicit: *"show the
//   generic refusal and the code; do not invent a specific reason"*, because `RefuseReason::unsealable` on a `no_fix`
//   refusal would send the user to fix the wrong thing. ⇒ the CODE rides beside it (`UiModel::refuse_code()`).
// ⓘ `unsealable` / `no_location` / `queue_full` stay reachable through §B73's ASYNC path (`note_failure`, which reads
//   a real `SendFailReason`); they are simply not derivable from a `CmdCode`.
// ⓘ NO `default:` — `CmdCode` is documented APPEND-ONLY in command.h, and a new code must fail the build here rather
//   than land silently (§B72's lesson; -Wswitch is gate-blocking in this project).
inline RefuseReason refuse_reason_of(const SendExec& r) {
    using C = MESHROUTE_NS::CmdCode;
    if (!r.ok) return RefuseReason::parser;
    switch (r.code) {
        case C::queued:                   return RefuseReason::other;   // not a refusal; callers never ask
        case C::err_unknown_dst:          case C::err_too_large:
        case C::err_no_gateway:           case C::err_priority_capped:
        case C::err_no_binding:           case C::err_unsupported:
        case C::err_unprovisioned:        case C::err_no_data_sf:
        case C::err_ack_ring_full:        case C::err_ambiguous_plane:
        case C::err_no_identity:          case C::err_tx_queue_full:
        case C::err_resolve_pending_full: return RefuseReason::other;
    }
    return RefuseReason::other;   // -Wswitch covers the enum; this satisfies -Wreturn-type
}

// Wide enough for the longest line either verb can produce, with the widest `%u` GCC must assume for a promoted
// `uint8_t` (10 digits) and the longest canned text. Measured worst case is `send_channel` at ~52 B; the excess is
// deliberate slack against `-Wformat-truncation=`, exactly as `firmware_ui.cpp`'s kLineCap documents.
inline constexpr std::size_t kSendLineCap = 96;

// ★★ COMPOSE THE CONSOLE LINE. Returns its length, or 0 = REFUSE (C2 — never a truncated or partly-formed command).
// ★ §3.4 — a DM is `send <team_local_id> "<text>" -t -a`. `-t` selects the TEAM plane; `-a` buys the ONE thing a
//   channel post can never offer, a per-destination end-to-end ack. ⛔ NO `-e`: the parser gates it `allow_e=by_hash`
//   and rejects it on an id target, so `crypt` stays `def` and follows the node's own `e2e_dm` setting. The UI must
//   not force plaintext either — `CryptIntent::off` was deliberately removed from the console.
// ★★★ §4.1 — `-l` IS CONDITIONAL, AND UNCONDITIONALLY SENDING IT WOULD TURN "NO FIX" INTO NO ALARM AT ALL.
//   `node.cpp:1553` refuses `want_loc && lat_e7 == 0 && lon_e7 == 0` with `err_unsupported` BEFORE anything is
//   enqueued. A distress call is worth more than the coordinates attached to it, so a node without a fix sends the
//   alarm WITHOUT `-l` rather than not at all.
// ⓘ The canned channel post carries no `-l`: it is not a distress message and §4.1's location ruling is about the
//   alarm. It does carry `-e`, like the alarm, so both take the identical team-crypt path.
inline int ui_compose_send_line(char* out, std::size_t cap, const SendReq& req,
                                uint8_t team_channel_id, bool have_fix) {
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    int n = 0;
    if (req.kind == SendKind::dm) {
        if (req.text_index >= kDmSendableTexts) return 0;   // `back` (or past the table) is not a message — REFUSE
        n = snprintf(out, cap, "send %u \"%s\" -t -a",
                     unsigned(req.peer_id), kDmTexts[req.text_index]);
    } else if (req.kind == SendKind::channel_canned) {
        if (req.text_index >= kChannelSendableTexts) return 0;
        n = snprintf(out, cap, "send_channel %u \"%s\" -t -e",
                     unsigned(team_channel_id), kChannelTexts[req.text_index]);
    } else {   // SendKind::emergency — one fixed body, no list, no cursor
        n = have_fix ? snprintf(out, cap, "send_channel %u \"%s\" -t -l -e",
                                unsigned(team_channel_id), kEmergencyText)
                     : snprintf(out, cap, "send_channel %u \"%s\" -t -e",
                                unsigned(team_channel_id), kEmergencyText);
    }
    if (n <= 0 || std::size_t(n) >= cap) { out[0] = '\0'; return 0; }   // truncation is a refusal, never a short send
    return n;
}

// ★★★ THE WHOLE SEND DECISION, IN ONE PLACE THE NATIVE SUITE DRIVES.
// ⚠⚠ THE THREE RULES THAT MAKE IT SAFE, ALL OF WHICH HAVE BEEN GOT WRONG IN THIS ARC:
//  1. **The result reaches the model TYPED.** A discarded `BufferSink` (the rejected alternative) leaves a parser
//     refusal or an immediate `err_*` on the panel as `SENDING...` FOR EVER — spec §2.1 rule 1.
//  2. **`ctr == 0` IS NOT FAILURE** (§B39/§B84). It means NO LOCAL HANDLE EXISTS and the transmission status is not
//     answerable synchronously. It is NOT accepted either ⇒ `on_send_accepted` is NOT called here, because `_tries`
//     moves only there and the BOUNDED EXPIRY is what spends the attempt (`ui_pump_trackers`, §B84). Calling it in
//     both places would spend two of the three alarms on one transmission.
//  3. **`submit()` precedes the executor**, because `send_blocked` is emitted SYNCHRONOUSLY inside `Node::on_command`
//     and a slot that is not open yet cannot claim it. (The push ring is drained on a later service pass, so this is
//     ordering discipline rather than a reentrancy hazard — but the discipline is free and the hazard would not be.)
// ⓘ It takes BOTH trackers and picks by kind, so a caller cannot hand an alarm to the normal slot. That choice was a
//   caller obligation in the plan's listing; here it is one line inside the tested unit.
inline void ui_perform_send(SendTracker& emg, SendTracker& normal, UiModel& m, const SendReq& req,
                            uint8_t team_channel_id, bool have_fix,
                            SendExecFn exec, void* ctx, uint32_t now_ms) {
    SendTracker& tr = (req.kind == SendKind::emergency) ? emg : normal;
    char line[kSendLineCap];
    const int n = ui_compose_send_line(line, sizeof line, req, team_channel_id, have_fix);
    // A line we refuse to compose never reaches the core, so there is no `CmdCode` for it — same shape as a parser
    // reject, and `RefuseReason::parser` is the predicate that says "read no code" (see UiModel::refuse_code).
    if (n == 0 || !exec) {
        tr.refuse();
        m.on_send_refused(req.kind, RefuseReason::parser, MESHROUTE_NS::CmdCode::queued, now_ms);
        return;
    }
    tr.submit(req.kind, req.peer_id, (req.kind == SendKind::dm) ? uint8_t(0) : team_channel_id, now_ms);
    const SendExec r = exec(line, std::size_t(n), ctx);
    if (!r.ok || r.code != MESHROUTE_NS::CmdCode::queued) {
        tr.refuse();
        // §B78: `now_ms` is the REFUSAL's own time — a gesture-anchored deadline is already partly spent by the time
        // a refusal lands, which is the defect spec §4.3 exists to kill.
        m.on_send_refused(req.kind, refuse_reason_of(r), r.ok ? r.code : MESHROUTE_NS::CmdCode::queued, now_ms);
        return;
    }
    if (r.ctr == 0) { tr.awaiting_outcome(now_ms); return; }   // ★ rule 2 — no handle, status UNKNOWN, no attempt spent
    tr.accept(r.ctr, now_ms);
    m.on_send_accepted(req.kind, now_ms);
}

// Returns true if this push was a RECEIVE this unit owns (diagnostic; the caller routes everything else to
// `ui_route_send_push`). `who` is the caller's already-resolved display label — the ONE thing here that needs `Node`.
inline bool ui_route_recv_push(UiInboxCounters& c, UiModel& m, const MESHROUTE_NS::Push& pu,
                               uint8_t ui_team_channel_id, bool same_team_post,
                               const char* who, uint32_t now_ms) {
    using PK = MESHROUTE_NS::PushKind;
    // ★★★ A DM IS COUNTED AND NOTHING ELSE — IT IS **RULED** NOT TO BE EMERGENCY CONFIRMATION (owner, 2026-08-05,
    //     register B114). This arm returns before `on_reply` can be reached, so a teammate answering a distress call by
    //     DIRECT MESSAGE does NOT move the alarm off `NOT HEARD`. ⚠ THAT IS THE DESIGN, NOT AN OVERSIGHT — recorded
    //     here because it was measured on metal as a suspected defect (the team heard the call, replied by DM, and the
    //     panel still said NOT HEARD) and the owner ruled the shipped behaviour CORRECT.
    // ⛔ DO NOT "FIX" IT BY WIDENING THE ROUTER TO `msg_recv`. Two reasons, both structural:
    //     ① it re-opens exactly the surface §F4/§B103 deliberately narrowed — a reply must be provably from OUR TEAM;
    //     ② a DM's `pu.team_id` is NOT the channel-post team tag, so `same_team(pu.team_id)` cannot scope it safely.
    //        Any widening therefore needs its own scope guard, i.e. its own slice and its own ruling.
    // ⓘ What the panel MAY yet learn from is a teammate's reply on the TEAM CHANNEL (below, the `channel_recv` path) or
    //   — not implemented, its own protocol/UI slice — the channel `HAVE` digest as delivery evidence (register B116).
    if (pu.kind == PK::msg_recv) {
        c.last_dm_ms = now_ms; c.have_dm = true;
        // ★★ §B108 ROUND 2: THE SERIAL IS UNCAPPED. This used to read `if (c.unread_dm < kUnreadCap) ++c.unread_dm;`
        //    — and at the cap that increment silently did NOTHING, so an arrival during a paging frame left no trace
        //    at all and the frame's completion marked it read. `kUnreadCap` now applies only in `publish`, on the way
        //    to the three digits the bar can draw. See `UiInboxCounters` for the wraparound argument.
        ++c.arr_dm;
        m.mark_dirty();                       // ★ §B108: the counts moved -> the STATUS BAR is stale on every screen
        // ★★★★ §UI-17 S8 (owner-ruled 2026-08-20, §9 R-7) — **A DM DELIVERED TO US LIGHTS THE PANEL, SEALED OR NOT.**
        //      ⛔ THE CHANNEL ARM's `enc` GATE MUST NOT BE COPIED HERE: this message is ADDRESSED TO US, and gating it
        //      on `pu.enc` would silence an unsealed DM — the half-applied shape an `enc == true` fixture alone would
        //      never see (it has its own mutation). ⓘ `pu.enc` on this path is `crypted_ok` (node_mac_rx.cpp:1758).
        //      ⓘ This is a WAKE and nothing else: it navigates nothing and writes no emergency field (see
        //      `UiModel::on_msg_wake`), so §B114's ruling above — a DM is NOT emergency confirmation — is untouched.
        m.on_msg_wake(now_ms);
        return true;
    }
    if (pu.kind != PK::channel_recv) return false;
    c.last_ch_ms = now_ms; c.have_ch = true;
    ++c.arr_ch;                               // ★★ §B108 round 2: uncapped, exactly as above
    m.mark_dirty();
    // ★★★★ §UI-17 S8 — **ONLY A POST THAT ARRIVED *SEALED* WAKES THE PANEL**, and `pu.enc` IS THE WHOLE SAFETY
    //      ARGUMENT (spec §1.9 F-9, ruling §9 R-7). The three delivery cases, measured at `node_channel.cpp:405-419`
    //      rather than argued: an UNDECRYPTABLE/foreign post is not `readable`, so it is never inboxed and emits ⛔ NO
    //      PUSH AT ALL — nothing here could wake for it; a post WE OPENED with our channel key arrives `enc = true`
    //      (`:415`) ⇒ it wakes; and a CLEARTEXT post on a matching channel id — the one case that delivers from
    //      OUTSIDE our key — arrives `enc = false` ⇒ ⛔ it must NOT wake. ⇒ §R1/[[B109]]'s *"a stranger's post does
    //      not light a dark panel"* (bench §8.15) survives BY CONSTRUCTION, and nothing about it is withdrawn.
    // ⛔ THE GATE GOVERNS THE **WAKE** AND NOTHING ELSE: the counters and `mark_dirty()` above are unconditional, so a
    //   cleartext post still counts as unread and still repaints a LIT panel. Moving this line above them — or above
    //   the kind gate — is the tempting simplification and it is wake-on-any-push wearing a scope's clothes.
    // ⛔ It is deliberately NOT also gated on the team-channel scope below: `enc` already means "opened with a key we
    //   hold", which is the ruling's exposure — the operator's own team's sealed traffic. The scope below is
    //   `on_reply`'s (§4.4), and a wake is not a distress confirmation.
    if (pu.enc) m.on_msg_wake(now_ms);
    // Spec §4.4: ONLY a post on our own team's channel qualifies. The model applies the rest of the guard — a state
    // whitelist plus "at least one alarm was actually transmitted" (`on_reply`).
    // ★★ §R1 (OWNER-RULED 2026-08-05): an ACCEPTED reply also UN-BLANKS the panel, and that happens inside `on_reply`
    //    rather than here — deliberately. Waking beside `mark_dirty()` above would wake on ANY arrival, including the
    //    stranger's channel-0 post this very guard exists to reject; putting it past BOTH scopes is what makes the
    //    wake mean "an answer to our distress call arrived", not "a packet arrived". ⛔ Do not hoist it up here.
    if (pu.channel_id != ui_team_channel_id || !same_team_post) return true;
    // ⚠ NOT a C string on the wire: `Push::body` is a length-counted byte buffer, so copy-and-terminate. Casting it to
    //   `const char*` and handing it to `on_reply` (an earlier draft) reads past `body_len`.
    char body[21];
    const uint8_t n = (pu.body_len < uint8_t(sizeof body - 1)) ? pu.body_len : uint8_t(sizeof body - 1);
    for (uint8_t i = 0; i < n; ++i) body[i] = char(pu.body[i]);
    body[n] = '\0';
    m.on_reply(who, body, now_ms);
    return true;
}

}  // namespace mrui
