# R2 — route-plane hardening (behaviour milestone) — design proposal

**Date:** 2026-05-30  **Status:** IMPLEMENTED + REVIEWED. t85 gate PASS; prune unit test
PASS; S3 differential EXACT-match vs Lua t29 (beacon_tx/rt_aged/discovery/rt_full/dirty/
stable all identical); full sim suite 103/103. Review (5 angles, 10→6): found + fixed a
real determinism bug — the direct-route `primary_refresh` arm was missing, so a known
neighbour re-heard at improved SNR skipped the triggered-beacon rand draw and desynced the
mt19937 vs the Lua on fluctuating links (invisible to the static-SNR gate); plus 2 low
`_beacon_offset` edge-fixes (Phase-2 `remaining>0` guard + empty-table reset). You commit.

**Pinned (original proposal below):** Q1=prune unit test; **Q3=single-draw
triggered beacon — the min-interval rate-limit defer branch moves to R4 with the throttle
plane, which ELIMINATES the conditional-2nd-draw determinism hazard entirely**; Q4=keep
discovery on + widen tol + bcn_discovery_exit parity; Q2/Q5/Q6/Q7 rec-yes). With Q3=b, §2's
"HAZARD — conditional 2nd draw" is moot: R2 adds exactly one unconditional new rand site
(triggered jitter), gated by `_triggered_beacon_pending` before the draw.
**Track:** behaviour/R track, after R1 (beacon emit + K=3 DV route table). EXTENDS the
R1 `meshroute::Node` — no new wire (C5 BCN unchanged). Grounded in a 6-angle extract +
reconcile of the Lua aging/eviction, 3-cycle prune, dirty-only paging, triggered beacon,
and discovery state machine.

---

## 0. Goal + scope

**Goal:** harden the R1 route/beacon plane to full Lua fidelity by adding the five
mechanisms R1 deferred, so route tables stay correct on **longer / looping / link-loss**
runs (not just the 2-node happy path). All five interlock (dirty-only depends on
discovery; aging + prune both fire triggered beacons), so they land together.

**The five in-scope mechanisms:**
1. **Route aging / TTL eviction** — periodic walk evicting candidates past a hop-class TTL
   (neighbour `hops≤1` = 45 min, remote = 3 h), removing empty entries, firing a triggered
   re-beacon on eviction (`dv_dual_sf.lua:5249-5302`).
2. **3-cycle prune** — on a beacon entry whose `next == self`, drop cached candidates whose
   `n2_hop == sender` (the `me→X→N→me` loop guard) (`rt_prune_cycle :5193-5227`).
3. **Dirty-only differential beacons + paging** — steady-state beacons carry only changed
   (`dirty`) routes + a sliding `beacon_offset` stable page; discovery sends full pages
   (`pack_beacon :1714-1848`).
4. **Triggered beacon kind** — a coalesced one-shot re-beacon on any route change
   (`schedule_triggered_beacon :7877-7907`).
5. **Discovery state machine** — boot fast-cadence (5 s) + full beacons until `heard≥3 ||
   routes≥8 || timeout`, then steady period + dirty-only (`in_discovery :7513`,
   `maybe_exit_discovery :7517-7539`).

**Gate stays convergence + beacon-event parity vs the Lua, NOT `dm_delivery`** (still R3).
No wire change: dirty-only changes *which* `rt` entries fill the beacon span, not the codec
(C5 already packs arbitrary spans ≤63 entries).

---

## 1. What the Node adds (extends R1 node.cpp)

**Timer ids** (the Node owns the id namespace; `kBeaconTimerId=1` is taken):
`kAgingTimerId=2`, `kTriggeredBeaconTimerId=3`. (Reserve 4+ for R3's RTS/CTS/ACK timers —
document the allocation table in `node.h`.)

```
on_init:                                    // + discovery window + aging timer
  init discovery: _discovery_mode = (discovery_ms>0); _discovery_started_ms=now;
                  _discovery_until_ms = now + discovery_ms; _discovery_bcn_rx_count=0
  first beacon period = in_discovery() ? discovery_beacon_period_ms(5000) : beacon_period_ms
  after(rand_range(0, first_period), kBeaconTimerId)            // SAME call site as R1, P now phase-dependent
  after(rt_aging_check_period_ms, kAgingTimerId)

on_timer(id):                               // R1 early-returned for id!=beacon — now DISPATCH
  kBeaconTimerId:        maybe_exit_discovery("timer"); P = in_discovery()?5000:beacon_period_ms;
                         emit_beacon("periodic"); re-arm rand_range(P*4/5, P*6/5+1)
  kAgingTimerId:         age_out_stale_routes(); re-arm rt_aging_check_period_ms
  kTriggeredBeaconTimerId: _triggered_beacon_pending=false; emit_beacon("triggered")

emit_beacon(kind):                          // R1 packed a FULL page every time
  maybe_exit_discovery("before_bcn"); dirty_only = !in_discovery()
  Phase 1: collect dirty _rt indices (dirty_n = min(count, cap))
  Phase 2: if !dirty_only && room: walk from _beacon_offset over _rt, skip dirty, fill;
           new_offset = idx % _rt_count
  pack dirty-first then stable; CLEAR dirty on the first dirty_n landed dests ONLY
  advance _beacon_offset to new_offset ONLY if Phase 2 ran
  tx; _last_beacon_tx_ms = now; emit beacon_tx{...,kind} + beacon_diff_breakdown{dirty_n,stable_n,total_dirty}

ingest_beacon:                              // + discovery count, prune, triggered
  ... beacon_rx; if in_discovery() _discovery_bcn_rx_count++
  direct merge (rt_changed |= new/promote/primary_refresh) ; schedule_triggered_beacon on new/promote
  DV merge: REPLACE 'if (e.next==self) continue;' WITH
            'if (e.next==self) { rt_prune_cycle(e.dest, b.src); continue; }'
            (rt_changed |= ...)
  maybe_exit_discovery(rt_changed?"rt_update":"beacon_rx")
  if (rt_changed) schedule_triggered_beacon()
  maybe_emit_rt_full()
```

**Aging** (`age_out_stale_routes`): walk `_rt`; per entry compact `candidates[]` dropping
`(now − last_seen_ms) ≥ ttl_for_hops(c.hops)` (`hops≤1`→neighbour TTL else remote; `ttl≤0`
disables); on slot-0 eviction set `dirty`; emit `rt_aged{dest,slot}`; when `n==0` `rt_remove(i)`
(left-shift `_rt`, **don't advance `i`**); one `schedule_triggered_beacon()` if any evicted.
`last_seen_ms` refresh is already done in R1's `rt_merge`.

**Prune** (`rt_prune_cycle(dest, sender)`): `rt_find(dest)`; in-place drop candidates with
`n2_hop == sender` (no re-sort — survivor order preserved, matches Lua); emit `rt_prune{dest}`;
if emptied `rt_remove`; else if primary dropped set `dirty`; `schedule_triggered_beacon()` if
mutated. (Direct candidates have `n2_hop==0`, never falsely pruned.)

**Triggered** (`schedule_triggered_beacon`): `if(is_mobile) return; if(_triggered_beacon_pending)
return;` — **the pending check gates BEFORE the rand draw** (coalesce invariant) — set pending;
`delay = rand_range(2000, 10001)`; arm `kTriggeredBeaconTimerId`. The steady-state min-interval
defer branch (a conditional 2nd draw) is included for fidelity but stays dormant on R2 gates
(see §2).

**`rt_remove(idx)`**: `for(k=idx; k+1<_rt_count; ++k) _rt[k]=_rt[k+1]; --_rt_count;` — the
reverse of R1's `rt_insert` shift, preserving the ascending-dest sort.

---

## 2. Determinism contract additions

R1 pinned 3 beacon rand sites. R2 adds **exactly one new site**, plus one hazard:

- **NEW site — triggered jitter:** `rand_range(2000, 10001)` (Lua `rand(2000, 10000+1)`,
  `:7885`), once per coalesced trigger. **Coalesce invariant:** check `_triggered_beacon_pending`
  *before* the draw — multiple prune+rt_changed triggers in one `on_recv` must draw **nothing**
  after the first (Lua draws after setting pending).
- **HAZARD — conditional 2nd draw:** the rate-limit defer branch (`:7894`) draws a second
  `rand` only when `steady_state && min_interval && now+delay < earliest`. If the two engines
  disagree on taking it, the shared `mt19937` desyncs *all* downstream draws. **Mitigation
  (verified safe):** `steady_state` requires `now − discovery_started ≥ beacon_boot_grace_ms
  (120 s)`; **every R2 gate runs < 120 s**, so the branch never arms — confirmed by t29's run
  (`0 beacon_trigger_deferred`). Constraint documented in `s3_diff`. (A future >120 s gate must
  bit-replicate the branch.)
- **Argument change, not new draw:** `on_init`/`on_timer` now pick `P` from
  `in_discovery() ? 5000 : beacon_period_ms` — same call site/order as R1, but a different
  number of subsequent re-arms once discovery exits. Both engines must call
  `maybe_exit_discovery` at the **same point before the arm** so the period choice matches.
- Sorted `_rt` iteration (R1) preserved through `rt_remove`/aging/prune.

---

## 3. Acceptance / gate (three pieces)

**(1) PRIMARY — `t85_meshroute_aging.json`** = clone of the existing `test/t29_route_aging.json`
with `engine:"meshroute"` on all 3 nodes, **keeping its T-class config** (`rt_aging_ttl_*=15000`,
`rt_aging_check_period_ms=5000`, `beacon_period_ms=5000`, `quiet_threshold_ms=0`,
`seen_bitmap_enabled=false`, carol `dies_at_ms=20000`, `duration=60000`, `warmup=5000`,
`seed=42`). `dies_at_ms` is enforced **engine-agnostically in `SimController`** (node stops
being stepped + its frames drop), so meshroute nodes also see carol go silent — **no C++
death mechanism needed**. The 15 s TTL makes aging fire in ~15-20 s → no 45-min run. The
captured Lua run (`t29_route_aging_events.ndjson`) shows the target: **4 `rt_aged`, 6
`bcn_discovery_exit`, 36 `beacon_diff_breakdown`, 0 deferred, 0 prune**. Assertions mirror
t29 + t84: `beacon_tx`/`beacon_rx`/`rt_update`/`rt_full`, `rt_aged` for carol at alice+bob,
`bcn_discovery_exit` at all three.

**(2) PRUNE — a dedicated C++ unit test** against a hand-built `_rt` (a `me→X→N→me` triangle).
The t29 line topology gives prune **zero** coverage (`0 rt_prune`), and a triangle scenario's
back-edge would shift the aging timing t29 relies on. A unit test matches the project's
"dedicated test per implementation" rule with no scenario-timing coupling. (§7 Q1.)

**(3) S3 differential — extend `tools/s3_diff.py`:** add per-node `rt_aged{dest}` set+count,
`rt_prune{dest}` set+count, `bcn_discovery_exit` count+reason, and `beacon_diff_breakdown`
aggregate `Σdirty_n / Σstable_n`. **Verdict policy:** MUST-match `rt_aged`/`rt_prune` dest-sets
+ `bcn_discovery_exit` count (deterministic events); within-`--tol` on `dirty_n`/`stable_n`
sums (jitter-sensitive); **do NOT** assert per-beacon `n_entries` (dirty-only changes that
stream by design — the converged route SET stays the MUST-match signal). `beacon_tx` gains a
`kind` field for periodic-vs-triggered counting; widen the `beacon_tx` tol for the discovery
fast-cadence first 60 s and add the `bcn_discovery_exit` parity check so discovery is *gated*,
not neutralized.

**Suite gate:** default `run_tests.sh` green (+`t85`), `t84` still passes.

---

## 4. Non-goals (deferred, with owner)

| Deferred | Owner |
|---|---|
| Data plane RTS/CTS/DATA/ACK (where `dm_delivery` becomes the gate) | **R3** |
| Adaptive channel-busy throttle + silence-jitter + `beacon_max_idle` override (R2's `on_timer` mirrors only the `quiet_threshold≤0` fast path) | **R4** |
| sync / gateway_sweep beacon kinds; the `req_sync` Q-frame discovery loop (`:9036-9044`); the `\|\| kind=="sync"` term in `dirty_only` (leave a TODO); seen-bitmap | **R5** |
| Gateway schedule plane | **R7** |
| ext-TLV block (stays OPAQUE) | R4/R5/R7 |
| budget/suspect penalties — stay stubbed 0 (`effective_score == score`) | R3+ |
| `route_mobile_touched` (informational; doesn't gate eviction) + the explicit mobile-as-transit guard | peer-mobility plane |
| the 6 non-route-mutation `schedule_triggered_beacon` callers (penalty/suspect/liveness driven, `:4316,4414,4466,9514,9547`) | R3+ |

---

## 5. New constants / state / methods

- **`protocol_constants.h`:** add `rt_aging_ttl_neighbor_ms = 2700000` (45 min),
  `rt_aging_ttl_remote_ms = 10800000` (3 h) (confirmed `dv_dual_sf.lua:8783-8784`). All
  others (`rt_aging_check_period_ms`, `discovery_*`, `beacon_trigger_*`, `beacon_boot_grace_ms`,
  `discovery_beacon_period_ms`) already exist — confirm-and-reuse.
- **`NodeConfig`:** add config-overridable `rt_aging_ttl_neighbor_ms`, `rt_aging_ttl_remote_ms`,
  `rt_aging_check_period_ms` (default to the constants) so the gate injects 15000/15000/5000
  (Lua reads `config.X or default`). **Confirm `FirmwareNode::onInit` maps these 3 keys**, else
  the override is silently dropped and aging never fires in 60 s (a vacuous pass).
- **`Node` state:** `_beacon_offset`, `_discovery_mode`, `_discovery_started_ms`,
  `_discovery_until_ms`, `_discovery_bcn_rx_count`, `_triggered_beacon_pending`,
  `_last_beacon_tx_ms`. (`RtEntry.dirty` + `RtCandidate.n2_hop`/`last_seen_ms` already exist.)
- **`Node` methods:** `age_out_stale_routes`, `ttl_for_hops`, `rt_remove`, `rt_prune_cycle`,
  `schedule_triggered_beacon`, `in_discovery`, `maybe_exit_discovery`; `emit_beacon` →
  `emit_beacon(kind)`.

---

## 6. Open questions (recommendations; confirm or override)

1. **Prune coverage** = a dedicated **C++ unit test** (hand-built `_rt` triangle) — **rec yes**
   (no scenario-timing coupling; a triangle scenario's back-edge perturbs the aging gate).
2. **TTLs as config-overridable `NodeConfig` fields** (so the gate uses 15 s) — **rec yes**
   (the Lua is config-overridable; without it the gate needs a 3 h run).
3. **Triggered beacon = ship the full min-interval defer branch** but keep every R2 gate < 120 s
   so the 2nd draw stays dormant — **rec yes** (faithful; the hazard is contained, verified by
   t29's `0 deferred`). *(Alternative: a single-draw version, treating the rate-limit as an R4
   throttle concern.)*
4. **Discovery in the gate = keep it on + widen `beacon_tx` tol + add `bcn_discovery_exit`
   parity** — **rec yes** (actually tests the discovery FSM). *(Alternative: pin `discovery_ms=0`
   to neutralize the cadence shift, but then discovery ships untested.)*
5. **`s3_diff` verdict policy** (MUST-match `rt_aged`/`rt_prune`/`discovery_exit`; within-tol on
   `dirty_n`/`stable_n`; no per-beacon `n_entries`) + `beacon_tx.kind` field name `"periodic"`/
   `"triggered"` matching the Lua emit — **rec yes**.
6. **`req_sync` boundary** = R2 implements only the discovery cadence + dirty-only + exit
   conditions and **stubs the `req_sync` Q-frame emission to R5** (TODO for the `kind=="sync"`
   term) — **rec yes** (don't accidentally grow a Q-frame family).
7. **Iteration-during-removal** = the C++ no-advance-on-remove index (vs Lua's collect-first
   two-pass, which is a Lua `pairs`-mutation quirk, not an algorithmic need) — **rec yes**.

(Also confirm the `warmup_ms=5000` boundary doesn't reset/re-arm the meshroute beacon/aging
timers differently than Lua — a risk noted for the gate.)

## 7. Files

`MeshRoute/lib/core/node.{h,cpp}` (the 5 mechanisms), `protocol_constants.h` (2 TTL consts);
`lora-universal-simulator/orchestrator/runtime/FirmwareNode.cpp` (3 new config keys);
`MeshRoute/test/test_node_r2.cpp` or similar (the prune unit test — confirm the meshroute_core
test target); `test/t85_meshroute_aging.json` (clone of t29); `tools/s3_diff.py` (new metrics
+ the <120 s note). Uncommitted working artifact — you commit.
