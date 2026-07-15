# R4.0 + R4.1 — Duty-cycle budget tier + BUDGET NACK (reason 1) — design spec

**Status:** IMPLEMENTED + verified (98/98 native, all sim gates green). **Gate reality in §8 (P3 revised).**
Originally DESIGN (pins settled from the r4-grounding workflow + direct Lua/C++ verification). **Date:** 2026-05-31.
**Scope (user pick):** R4.0 (config + `compute_budget_tier`) **+** R4.1 (BUDGET-NACK reason-1 end-to-end). This
**completes the NACK plane** — it lights the last dead stub (`node_mac.cpp:603-607`, the reason-1 "DEFERRED"
comment). **Entirely draw-free** → gates at `--band 0` exactly like the prior NACK work. **EXCLUDES** R4.2
(persistent tier mark + route penalty + ACK budget_hint) → `mark_neighbor_budget_tier` stays a no-op returning
`reranked=0`. **Source of truth:** `dv_dual_sf.lua` (behaviour). Delicate firmware → implement INLINE. USER commits.

**Predecessors:** R1–R3, R3.x, cascade-to-alt, NACK plane (BUSY_RX+LOOP_DUP), HOP_BUDGET. All constants this
slice needs ALREADY exist in `protocol_constants.h` (verified): `budget_strained_pct=50` / `budget_critical_pct=80`
/ `budget_exhausted_pct=95`; `budget_blind_strained_ms=60000` / `_critical_ms=180000` / `_exhausted_ms=300000`;
`nack_reason_budget=1`. `_blind_until` map + `is_blind` + `try_cascade_requeue` all shipped.

---

## 1. Goal / non-goal
**IN** — (a) per-node duty-cycle config + the route-free tier classifier; (b) the RECEIVER's budget-aware NACK
(refuse an RTS when our own duty budget is ≥CRITICAL, so the sender reroutes instead of stalling mid-cycle); (c)
the SENDER's reaction (blind the peer + requeue, terminal-drop on caps).
**OUT (R4.2+)** — `mark_neighbor_budget_tier` STORING + the `effective_score` route penalty + the ACK
`budget_hint` piggyback; the adaptive beacon throttle / silence-jitter (R4.3, the first new draw); LBT /
`on_radio_busy` (R4.5); the originator anti-spam ledger (R4.4); the load-adaptive requeue shrink (no-op today).

## 2. The tier classifier (R4.0) — `compute_budget_tier()`
Faithful to Lua dv:3560-3571. `BudgetTier { HEALTHY=0, STRAINED=1, CRITICAL=2, EXHAUSTED=3 }` (Lua dv:3555-3558).
```
BudgetTier compute_budget_tier() const:
  if duty_cycle <= 0 || duty_cycle_budget_ms == 0: return HEALTHY      // disabled (dv:3561-3564)
  used    = _hal.airtime_used_ms(duty_cycle_window_ms)                 // the rolling-window primitive (exists)
  pct     = 100 * used / duty_cycle_budget_ms                          // integer; Lua uses float but the >= are
  if pct >= budget_exhausted_pct: return EXHAUSTED                     //   on integer pct boundaries (50/80/95)
  if pct >= budget_critical_pct:  return CRITICAL                      //   so integer math is identical at the
  if pct >= budget_strained_pct:  return STRAINED                      //   thresholds (no fractional tier edge)
  return HEALTHY
```
**Determinism note:** Lua computes `pct_used` as float `100.0*used/budget`; the C++ uses integer `100*used/budget`.
The tier only ever compares `pct >= {50,80,95}`; integer floor vs float differ ONLY for a fractional pct, and a
fractional pct never sits exactly on an integer threshold unless `used` is an exact multiple — at which point both
agree. So integer math is tier-identical. (Keep `used`/`budget` as `uint64_t`; `100*used` can't overflow at duty
window scale.) **No rand.**

### R4.0 config (Pin P0 = mirror the Lua)
`NodeConfig` gains: `float duty_cycle = 0.0f` (default OFF so every existing gate stays HEALTHY/unthrottled),
`uint32_t duty_cycle_window_ms = 3600000`. `on_init` derives `_duty_cycle_budget_ms = floor(duty_cycle * window)`
(Lua dv:8497) into a Node field (NOT config) so the gate scenarios shrink `duty_cycle` per-node to force a
deterministic CRITICAL. FirmwareNode plumbs the two JSON keys (same pattern as `quiet_threshold_ms`).
*(NB: the Lua default is 0.01; we default the C++ to 0.0 so the SLICE is inert on all prior gates — a scenario
that wants budget pressure sets it explicitly. Documented divergence from the Lua default, behaviourally safe.)*

## 3. RECEIVER emit (R4.1) — `handle_rts`, the budget-aware NACK
Faithful to Lua dv:10016-10044. Insertion point: `node_mac.cpp:220`, AFTER the two busy guards (`_pending_rx`
BUSY_RX NACK at 198-214, `_pending_tx` silent at 215-219) and BEFORE the fresh CTS block (`select_data_sf` at 221).
```
const BudgetTier my_tier = compute_budget_tier();
if (my_tier >= BudgetTier::CRITICAL) {                       // ≥CRITICAL only (STRAINED still CTSes)
    nack_in nin{}; nin.reason = nack_reason_budget; nin.ctr_lo = r.ctr_lo;
    nin.payload = (uint8_t(my_tier) & 0x0f) << 4;            // tier in the HIGH nibble (dv:10029)
    nin.to = r.src;
    pack_nack + tx on routing_sf, label "NACK";
    emit nack_tx{ to=r.src, reason=nack_reason_budget, tier=my_tier };   // ctr=r.ctr_lo for the event
    return;                                                  // NO CTS, NO pending_rx
}
```
**Why ≥CRITICAL not ≥STRAINED:** CTS+DATA-RX are free but the ACK (and any forward) cost budget; at CRITICAL we
likely can't finish the cycle, so refuse early and save the CTS+ACK round-trip (dv:10016-10026).

## 4. SENDER react (R4.1) — `handle_nack` reason-1, replacing the stub at 603-607
Faithful to Lua dv:10406-10453. Drop-in where the defensive `awaiting_cts=true; start_rts_timeout()` stub is now.
The timeouts were already cancelled at the top of `handle_nack` (575-577-equiv); `pt` is bound.
```
if (n.reason == nack_reason_budget) {
    const uint8_t tier = (n.payload >> 4) & 0x0f;                       // inline decode (Pin: P-CODEC inline, as HOP_BUDGET)
    uint32_t blind_ms = budget_blind_critical_ms;                       // tier==CRITICAL default
    if      (tier >= uint8_t(BudgetTier::EXHAUSTED)) blind_ms = budget_blind_exhausted_ms;
    else if (tier <= uint8_t(BudgetTier::STRAINED))  blind_ms = budget_blind_strained_ms;
    const uint64_t until = _hal.now() + blind_ms;                       // max-merge into _blind_until[pt.next]
    auto bit = _blind_until.find(pt.next);
    if (bit == _blind_until.end() || until > bit->second) {
        _blind_until[pt.next] = until;
        emit blind_observed{ next=pt.next };                            // (reason "nack_budget", tier — event-only)
    }
    // R4.2 will store the persistent tier here; R4.1 keeps it a no-op:
    const int reranked = 0;                                             // mark_neighbor_budget_tier no-op (Pin P2)
    emit nack_rx{ from=pt.next, reason=nack_reason_budget, tier, /*blind_ms, reranked=0*/ };
    try_cascade_requeue(pt, "budget_low");   // requeues w/ backoff (skips the now-blind hop on re-issue) OR,
                                             // on caps, emits path_cascade_exhausted+rts_giveup+send_failed and
                                             // drops — the helper ALREADY does both legs + _pending_tx.reset()
                                             // + become_free()/timer. (dv:10449-10467)
    return;
}
```
**Reuse:** identical machinery to the BUSY_RX-long-busy + HOP_BUDGET branches. `try_cascade_requeue` (node_cascade.cpp:103)
already: caps→giveup+drop, else requeue@backoff + `_pending_tx.reset()` + arm `kCascadeRequeueTimerId`. On the
re-issue `pick_next_cascade_hop` skips `pt.next` because `is_blind(pt.next)` is now true → alt, or originator-defer /
forwarder-drop. **Draw-free** (blind windows are constants, requeue backoff is `base*2^(n-1)`).

## 5. Determinism — ZERO new rand draws
compute_budget_tier = pure airtime arithmetic; the emit is a deterministic NACK tx; the react is constant blind
windows + the existing deterministic `try_cascade_requeue`. The mt19937 stream is UNCHANGED — so every prior gate
(r3/r4_forced/r5_cascade/hop_budget_chain) must still hold at `--band 0` byte-for-byte in draw count. Lock with a
`rand_calls`-delta==0 assertion on the new unit tests.

## 6. Minimal change set
1. `protocol_constants.h` — nothing (constants exist).
2. `node.h` — `NodeConfig`: `+float duty_cycle=0.0f; +uint32_t duty_cycle_window_ms=3600000;`. Node: `+uint64_t
   _duty_cycle_budget_ms=0;`; `+enum class BudgetTier{...};`; `+BudgetTier compute_budget_tier() const;`.
3. `node.cpp on_init` — derive `_duty_cycle_budget_ms = floor(duty_cycle * window)`.
4. `node_beacon.cpp` (or wherever helpers live) — define `compute_budget_tier()`. *(Pick the file that already has
   `_hal.airtime_used_ms` access; it's a Node method so any node_*.cpp works — keep it near the MAC, node_mac.cpp.)*
5. `node_mac.cpp handle_rts` — the ≥CRITICAL budget NACK at line 220.
6. `node_mac.cpp handle_nack` — replace the reason-1 stub (603-607) with the blind+requeue react.
7. `FirmwareNode.cpp` — plumb `duty_cycle` + `duty_cycle_window_ms` JSON keys.

## 7. Tests
- **Unit (R4.0):** `compute_budget_tier` table — a FakeHal whose `airtime_used_ms` returns scripted values; assert
  HEALTHY/STRAINED/CRITICAL/EXHAUSTED at 49/50/79/80/94/95/100 pct, and HEALTHY when `duty_cycle<=0`. `rand`-delta==0.
- **Unit (R4.1 emit):** a node at CRITICAL → an RTS gets a `nack_tx` reason=1, tier in the high nibble (parse the
  REAL bytes), NO `cts_tx`, NO `_pending_rx`. A HEALTHY node → CTS as today (no NACK).
- **Unit (R4.1 react):** feed a reason-1 NACK with tier=CRITICAL/EXHAUSTED/STRAINED → assert `_blind_until[next]`
  set to the tier-correct window (via `is_blind` + a clock probe), `blind_observed`+`nack_rx` fire, the flight
  requeues (`cascade_requeue`) or terminal-drops on caps; `rand`-delta==0.
- **Differential gate (wired):** `scenarios/r6_nack_budget_diff.json` — see §8. `dm_diff_band --band 0`.

## 8. The gate — REALITY (P3's forced-CRITICAL idea is not constructible; the BUDGET NACK is UNIT-tested)
The grounding's Pin P3 ("drive a node to CRITICAL with deterministic warm-up airtime, reroute, `--band 0`) turned
out NOT to be constructible, for two structural reasons discovered during implementation:

1. **The BUDGET NACK changes the PATH, not delivery success.** A budget-refused RTS reroutes via the SAME
   cascade-to-alt that the `rts_timeout` would have taken anyway, so delivery is 1/1 with OR without the NACK.
   A delivery-band gate is therefore blind to it — exactly the family as the BUSY_RX / LOOP_DUP NACK gates that
   were declared intractable and unit-tested (see [[project_meshroute_sim_strategy]] NACK-plane entry).
2. **The sim hard-enforces duty-cycle at the SAME per-node `duty_cycle`** the firmware reads
   (`SimController.cpp:1347-1379`: `used+airtime > budget_ms` → the TX is deferred). So a budget tiny enough that
   `compute_budget_tier` reaches CRITICAL is ALSO tiny enough that the sim blocks the node's OWN beacons → peers
   never learn its route (verified: alice heard 0 of r1's beacons with `duty_cycle=1e-6`). And the firmware-CRITICAL
   band (80–100% of budget, below the sim's 100% hard-block) can't be hit robustly across engines because beacon
   jitter makes each engine's cumulative airtime differ.

**Decision:** the BUDGET NACK emit + the blind+requeue react are **UNIT-tested** (`test_node_r3.cpp` R4.0/R4.1:
tier table at 49/50/79/80/94/95/100 pct + disabled; emit ≥CRITICAL with the real NACK bytes' tier nibble; the
blind+requeue react with the tier-scaled window; all `rand`-delta==0). The wired differential
`scenarios/r6_nack_budget_diff.json` is the DELIVERY-NEUTRAL integration gate: a relay with a GENEROUS
`duty_cycle=0.1` stays HEALTHY all run, so `compute_budget_tier` runs on REAL sim airtime and returns HEALTHY →
normal CTS, NO spurious NACK, delivery 1/1 identical (`--band 0`). This exercises the enabled-but-healthy tier
path the FakeHal unit tests can't, and proves the slice is inert when budget is healthy. HEALTHY is stable across
engines (airtime << budget), so the gate is robust.

## 8b. Post-review hardening (R4 budget-NACK adversarial review, 2026-05-31)
Review: 18 candidates → 14 surviving. Confirmed fixes applied (98/98 native; all 5 differential gates band-0):
- **[HIGH] `_sim_duty_cycle` fallback (review #00/#01/#04).** FirmwareNode read ONLY the per-node `duty_cycle`
  JSON key (default 0.0/disabled), but the Lua reads `config.duty_cycle or config._sim_duty_cycle or 0.01`
  (dv:8495) and SimController injects `_sim_duty_cycle = simulation.radio.duty_cycle` (default 0.01) into every
  node. So on any scenario that sets only the GLOBAL `radio.duty_cycle`, Lua nodes were budget-ENABLED while
  meshroute nodes were DISABLED — a latent band-0 divergence the moment a node crossed CRITICAL. FIXED:
  FirmwareNode now mirrors the Lua precedence (`duty_cycle ?? _sim_duty_cycle ?? 0.01`). This makes meshroute
  nodes default-enabled (0.01) like Lua; re-verified ALL 5 gates stay band-0 (no node reaches CRITICAL in the
  short scenarios, so the enabled-but-HEALTHY path is inert — exactly what r6 proves). **This SUPERSEDES the
  spec §2 "C++ default 0.0 OFF" decision** — the struct default stays 0.0 (for the device + unit tests, which
  opt in explicitly), but the SIM backend inherits the Lua default.
- **[LOW] float→double budget (review #05/#11).** `NodeConfig::duty_cycle` was `float`; `0.01f*3.6e6` floors to
  35999 where the Lua double floors to 36000 — a 1ms budget divergence near a tier boundary. FIXED: `duty_cycle`
  is now `double` end-to-end (struct + FirmwareNode parse + the test helper).
- **[LOW] cap-giveup event name (review #09).** The budget react passed `"budget_low"` as the giveup event NAME
  to `try_cascade_requeue`; the Lua names it `"rts_giveup"` (dv:10462) with `budget_low` as the *trigger*. FIXED
  to `"rts_giveup"` (consistent with the cascade-to-alt callers).
- **[LOW] plumb-proof test (review #12).** Added a unit assertion that the r6 values (`0.1`, 1h) derive a
  NON-ZERO budget (360000ms) crossing HEALTHY→STRAINED at 50% — proving the plumb isn't a silent disabled no-op.

**DEFERRED — telemetry-schema (review #02/#03/#07/#08/#10/#13):** the meshroute budget events emit `reason` as an
INTEGER and a simplified `blind_observed{next}`/`nack_rx{from,reason,tier,reranked}`, where the Lua emits string
`reason="budget_low"` + richer fields, so `analyze.py`/`dm_delivery` (which key on the `"budget_low"` string)
mis-bucket meshroute budget telemetry. This is **consistent with EVERY existing C++ NACK emit** (BUSY_RX/LOOP_DUP/
HOP_BUDGET all emit integer `reason` + simplified blind events) and is **permitted by the verification model**
(events are NOT byte-for-byte; the differential `dm_diff_band` keys on event NAMES, not the reason string, so it
is unaffected). Special-casing only the budget events would create an inconsistency. → tracked as a GLOBAL
"C++ NACK telemetry schema vs Lua + analyzer compat" item (fix all NACK emits together, or not at all), NOT an
R4.1 fix.

## 9. Pins (settled)
| # | Decision | Pick |
|---|----------|------|
| **P0** | duty_cycle config entry | config `duty_cycle`(float)+`duty_cycle_window_ms`; DERIVE `budget=floor(dc*win)` in on_init (Lua-faithful, per-node). C++ default 0.0 (OFF) so prior gates stay inert. |
| **P1** | slice scope | R4.0+R4.1 only; R4.2 isolated. |
| **P2** | `mark_neighbor_budget_tier` in R4.1 | **no-op returning 0** (emit `reranked=0`); R4.2 wires it. No dead state. |
| **P3** | reach CRITICAL in the gate | NOT constructible (see §8): BUDGET NACK is path-not-delivery + sim hard-enforces the same duty → **UNIT-test** the NACK; wire a delivery-neutral HEALTHY-budget integration gate. |
| **P-CODEC** | decode tier | inline `(payload>>4)&0xf` in handle_nack (consistent w/ HOP_BUDGET; `nack_out` stays a raw POD). |
| **P-EMIT-THRESH** | NACK at which tier | **≥CRITICAL** (Lua dv:10028); STRAINED still CTSes. |
