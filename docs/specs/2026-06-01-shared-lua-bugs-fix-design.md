# Shared-Lua-bugs cleanup — DATA giveup strand (both engines) + tx_with_retry duty pre-check (C++)

**Status:** DESIGN. **Date:** 2026-06-01. **Scope:** the two faithful-port bugs the R4.5b review surfaced
([[project_meshroute_shared_lua_bugs]]). Both are gate-inert (fire only under lbt congestion / non-healthy duty).
Delicate firmware (`dv_dual_sf.lua` + node*.cpp) → INLINE. USER commits.

---

## Bug #1 — standard stash-giveup STRANDS the DATA flight (BOTH engines)
**Symptom:** when a DATA exhausts its `on_radio_busy` stash retries, the giveup branch emits `tx_giveup` + clears the
stash and returns — but `on_radio_busy(DATA)` already cleared `awaiting_ack` + cancelled the ack-timeout, so
`pending_tx` is left SET with no recovery timer, and `become_free` is blocked behind it → the whole TX queue stalls.
Lua dv:12190-12195 has the identical leak; the DATA-M giveup (dv:12151-12152) and `try_cascade_requeue` DO clean up.

**Fix (both):** on the giveup branch, for the **DATA** slot/label, release the flight — `pending_tx = nil` +
`become_free` — mirroring the DATA-M giveup. CTS/ACK/NACK giveups are NOT cleaned up this way (their "flight" is a
receiver-side response whose `pending_rx` is released by `pending_rx_expiry`, and `pending_tx` may be an unrelated
outbound flight). C++ guards on `_pending_tx && _pending_tx->ctr_lo == s.ctr_lo` (the stash-stored ctr_lo); Lua on
`info.label == "DATA" and self.pending_tx ~= nil` (matches its own DATA-M giveup, which has no ctr_lo).

**Determinism:** reset is draw-free; `become_free` may re-drain (→ RTS jitter draw) but ONLY when a DATA gives up
under lbt_enabled congestion → never in a gate → gate-inert.

## Bug #2 — C++ tx_with_retry OMITS the Lua duty pre-check (C++ ONLY — the Lua is correct)
**Symptom:** Lua `tx_with_retry` (dv:3615-3635) duty-pre-checks; if over budget it emits `duty_cycle_blocked` +
`self:after(wait_ms, retry tx_with_retry)` + returns WITHOUT handing to the radio. The C++ always `_hal.tx`'s, so at
CRITICAL duty the sim's duty hard-block bounces the frame via `on_radio_busy(duty_cycle_exceeded)`, which consumes a
stash retry + an LBT jitter draw per bounce → the C++ gives up after 3 bounces where the Lua re-defers indefinitely
with fresh retries (different telemetry + retry accounting).

**Fix (C++ only):** port the duty pre-check into `tx_with_retry`, **scoped to retry-eligible frames (slot ≥ 0:
CTS/DATA/ACK/NACK)** — the only frames with a stash to re-run from and the only ones the review's "consumes a stash
retry per bounce" concern touches. Over budget → emit `duty_cycle_blocked` + arm `kDutyDeferTimerId + slot` (NEW
timer range [23..26]) + return (no `_hal.tx`); the timer fires `duty_defer_fire(slot)` → re-runs `tx_with_retry`
from the stash (re-checks duty + re-stashes fresh retries, faithful to the Lua closure re-run). `check_duty_cycle`
math ported verbatim: disabled or `used+airtime ≤ budget` → pass; else `wait = (oldest>0 ? oldest+window-now :
window)`, floor 1. **slot < 0 (RTS/beacon) keeps current behaviour** — the RTS recovers via rts_timeout (no stash to
consume), the beacon via tx_flood's OWN duty pre-check (dv:7781) + the next-beacon retry; documented residual.

**Determinism:** `airtime_used_ms`/`oldest_tx_end_ms`/`now`/`after` are all draw-free; gate-inert at healthy duty
(the check passes → straight to `_hal.tx`, byte-identical). NEW timer range [23..26] dispatched in on_timer.

## Tests
- **#1:** a DATA blocked → exhaust the 3 stash retries → `tx_giveup` → assert `pending_tx` is released + a queued
  2nd message's RTS now issues (the queue drained). Contrast: pre-fix the 2nd RTS never fires.
- **#2:** a TestHal with `_airtime_used` near budget → a CTS/DATA tx_with_retry over budget → assert
  `duty_cycle_blocked` emitted + NO `_hal.tx` for that frame + the `kDutyDeferTimerId+slot` re-fire re-issues it;
  and the rand-order golden (zero draws on the duty defer). Healthy-duty → byte-identical (no defer).

## Gates
- The 5 byte-identical gates + r7 stay band-0 (both fixes gate-inert).
- No new differential gate (CRITICAL-duty is not deterministically gateable cross-engine — r6 lesson).

---

## REVIEW OUTCOME (2026-06-01, workflow wgrrw8bj4: 16 candidates → 11 verified)
The first cut of FIX #2 introduced a HIGH determinism bug; folded in:

- **[HIGH, FIXED] do_data_tx armed the ACK wait on a duty-DEFERRED DATA.** FIX #2 made `tx_with_retry(DATA)` return
  without `_hal.tx`, but `do_data_tx` ran `pt.awaiting_ack=true; start_ack_timeout()` UNCONDITIONALLY after it. The
  short ack-timeout (DATA+ACK airtime) fires before the long duty wait (up to a full window) → `ack_timeout_fire`
  passes its `awaiting_ack` guard, DRAWS a rand (node_cascade.cpp:225, the determinism break) + re-RTSes, tearing
  the flight down while the duty-defer timer is still armed → later re-transmits a stale DATA. The Lua does NOT have
  this (it arms only inside DATA `on_handed` dv:10274, which fires only on real `self:tx`, and clears
  `awaiting_ack=false` on the not-handed path dv:10281-10283). **Fix:** `tx_with_retry` now returns `bool handed`;
  `do_data_tx` arms the ACK wait ONLY when handed, else `awaiting_ack=false`. The `data_tx` emit STAYS unconditional
  (Lua-faithful — dv:10251 emits it before tx_with_retry). Test strengthened: after the defer a stray ack-timeout
  must no-op (zero draws, no re-RTS); after the re-issue a matching ACK is accepted.
- **[MEDIUM, FIXED] duty_defer_fire had no staleness guard.** It re-ran `tx_with_retry` from the stash with only
  `s.valid`, so if the flight was replaced during the duty wait it re-txed a stale DATA + re-stashed a mismatched
  ctr_lo. Fix: DATA staleness guard (`_pending_tx->ctr_lo == s.ctr_lo`, mirror retry_stashed / Lua dv:12172) + it now
  re-arms `awaiting_ack`+start_ack_timeout on a successful DATA re-hand (anchored to the real send time, matching the
  Lua deferred re-run replaying on_handed).
- **[HIGH, FIXED] the Lua FIX #1 giveup lacked the ctr_lo guard the C++ had.** dv:12199 released `pending_tx` on only
  `info.label==DATA and pending_tx ~= nil` — so when the DATA stash outlived its flight (e.g. implicit_ack_from_forward
  installed flight B), the Lua DESTROYED flight B where the C++ skipped. Fix: store `ctr_lo` in the Lua `tx_stash`
  entry + guard the giveup on `self.pending_tx.ctr_lo == stash.ctr_lo` (every other pending_tx mutation in the Lua is
  already ctr_lo-guarded). Now both engines match.
- **[MEDIUM, DEFERRED] the slot>=0 RTS residual.** FIX #2 scopes the duty pre-check to retry-eligible frames, so an
  over-budget RTS (slot -1) is NOT duty-deferred — it is handed, the sim duty hard-block bounces it via
  on_radio_busy(rts) (no stash consumed), and rts_timeout recovers it. The RTS does NOT strand (recovers), but under
  SUSTAINED EXHAUSTED duty the C++ burns rts_max_retries + cascades/gives up where the Lua re-defers indefinitely with
  retries intact (different route/giveup timing). Gate-inert (CRITICAL-duty non-gateable). A full faithful port needs
  the duty pre-check in `lbt_complete`/`tx_initiating` (where the LbtKind + ctr_lo are available to defer the RTS via
  the LBT ring), NOT in tx_with_retry — deferred to a dedicated slice ([[project_meshroute_shared_lua_bugs]] #3).

**Re-validated:** 120/120 native (the strengthened #2 test would FAIL pre-fix) + 6/6 gates band-0 + 79/79 t* +
s01/s13/s16 Lua congestion 0 assertion failures.
