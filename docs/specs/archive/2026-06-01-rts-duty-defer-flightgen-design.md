# RTS duty-defer done right — flight-generation staleness + dedicated defer (redo of cleanup #A)

**Status:** DESIGN (pinned). **Date:** 2026-06-01. **Supersedes:** the reverted cleanup #A (LBT-ring reuse, review
wgvbtirmu — net-worse than the residual). Closes [[project_meshroute_shared_lua_bugs]] #3 (for the RTS) + R4.5a #09
(ctr_lo staleness proxy, RTS path). Delicate firmware → INLINE. USER commits.

## The problem (recap)
An over-budget RTS is NOT duty-deferred (the #2 duty pre-check is scoped slot>=0), so it is handed, the sim duty
hard-block bounces it via on_radio_busy(rts), and the retry-CONSUMING rts_timeout recovers it — under sustained
EXHAUSTED duty the C++ gives up where the Lua re-defers indefinitely. The first #A attempt duty-deferred the RTS via
the shared LBT ring; the review found it HIGH-worse: the ring's 4-bit ctr_lo staleness + a ~1h duty wait = a
wrong-dst false-match; ring-full silent stall; start_rts_timeout divergence.

## Design (3 parts)

### 1. Flight-generation staleness (replaces the 4-bit ctr_lo proxy for the RTS defers)
- `Node._flight_gen` (uint32_t counter) + `PendingTx.flight_gen`. Bump + stamp at the SINGLE pending_tx install site
  (node_mac.cpp:119 issue_send `_pending_tx = pt`): `pt.flight_gen = ++_flight_gen;`. cascade_to_alt mutates
  _pending_tx IN PLACE (same flight → same gen, correct); a requeue re-installs via issue_send → new gen (a restart
  is correctly "stale" for an old deferred RTS).
- This is the C++ equivalent of the Lua object-identity guard `__pending_tx_ref` (dv:3712) — exact, no 16-send wrap.
- **Scope (user pin "RTS defers"):** replace `rts_ctr_lo` with `rts_flight_gen` in the LBT-defer path too —
  `DeferredLbt.rts_ctr_lo`→`rts_flight_gen`, the tx_initiating/lbt_complete/schedule_lbt_defer param, and the
  lbt_complete staleness check `_pending_tx->ctr_lo != rts_ctr_lo` → `... flight_gen != rts_flight_gen`. tx_rts_retry
  passes `pt.flight_gen`; NACK/flood pass 0 (their staleness isn't checked). nack_wait keeps its short-window ctr_lo
  (sub-second, low risk — R4.5a #09 stays open only there).

### 2. Dedicated RTS-duty-defer slot + timer (NOT the shared LBT ring)
- `struct RtsDutyDefer { bool pending; uint16_t len; int16_t sf; uint32_t flight_gen; uint8_t buf[16]; } _rts_duty_defer;`
  (one slot — there is only ever one pending_tx/flight). `kRtsDutyDeferTimerId = 31` (single id; [27..30] is the #D
  beacon ring).
- **lbt_complete RTS branch** (after the flight_gen staleness check): `if (duty_over_budget(len, sf, &wait))` → emit
  duty_cycle_blocked{source=lbt_complete} + store (buf/len/sf/flight_gen) + `after(wait, kRtsDutyDeferTimerId)` +
  return (NOT handed; NO start_rts_timeout yet). The ~1h wait is now SAFE (flight_gen staleness is exact).
- **rts_duty_defer_fire** (on_timer dispatch of kRtsDutyDeferTimerId): bail if not pending; staleness
  (`!_pending_tx || flight_gen mismatch` → emit rts_tx_cancelled_stale + clear); re-check `duty_over_budget` → still
  over → re-defer (re-arm); else clear + `_hal.tx(buf, len, RTS-tag)` + **start_rts_timeout()** (the drift, below).

### 3. start_rts_timeout on the duty-deferred-then-sent RTS — DELIBERATE DRIFT (user pin "Arm it — C++ only")
The Lua is asymmetric: an LBT-deferred RTS re-runs tx_initiating(after_tx) → arms start_rts_timeout (dv:3707); a
DUTY-deferred RTS re-runs tx_with_retry (dv:3633, no after_tx) → DROPS it → the RTS stalls with no CTS-wait (a latent
Lua oversight). **The C++ ARMS start_rts_timeout** on the eventual send so the duty-deferred RTS gets a CTS-wait like
every other RTS (robust + consistent with the LBT-defer path). The Lua keeps its stall (no Lua change this slice).
Gate-inert (CRITICAL duty never in a gate) so NO differential impact; the extra rts_timeout retry-jitter draw in the
exhausted-duty regime is a documented C++>Lua divergence (the activated-duty plane is unit-tested, not gate-tested).

## Determinism / gate-inertness
All gate-inert: the RTS duty defer fires only when `_duty_cycle_budget_ms>0 && over budget` (CRITICAL/EXHAUSTED) — no
gate (healthy duty). flight_gen is a pure counter (no rand). duty_over_budget is draw-free. The lbt_complete staleness
swap (ctr_lo→flight_gen) is byte-identical at healthy duty (the immediate path: flight_gen matches the current flight,
same as ctr_lo matching). Verify: 6 gates band-0 + 79/79 t* + native.

## Tests
- RTS over budget → deferred (not handed) + duty_cycle_blocked; budget frees → kRtsDutyDeferTimerId re-fire → RTS
  handed + start_rts_timeout armed (the drift: a subsequent no-CTS rts_timeout fires a retry). Draw-free defer.
- Staleness: defer an RTS → install a NEW flight (flight_gen bumps) → the re-fire DROPS the stale RTS (no tx,
  rts_tx_cancelled_stale).
- flight_gen golden: a new send bumps the gen; cascade_to_alt does NOT (same flight).

## Non-goals
nack_wait ctr_lo→flight_gen (short window, deferred); fixing the Lua's asymmetric after_tx drop (would re-align but is
an awkward Lua restructure — deferred). A non-default-bw gate (separate, [[project_firmwarenode_sim_config_seam]]).
