# R1 — beacon emit + route-table build (behaviour milestone) — design proposal

**Date:** 2026-05-29  **Status:** IMPLEMENTED + REVIEWED (Q1=convergence-gated [C5 line
re-pinned], Q2=K=3 fold, Q3–Q7 rec-yes). t84 gate PASS; S3 differential PASS (route sets
+ rt_full match the Lua reference, beacon_tx within jitter tol); full sim suite pending.
You commit. t83 removed (superseded — it tested the now-gone S2 placeholder path, as
t82→t83 before). Adversarial review (5 angles, 14→5 verified, focused on the K=3/multi-hop
paths the 2-node gate misses): rt_merge funnel / ordering / score-bucket math / determinism
all CONFIRMED faithful. Two fixes applied: (a) a **mobile node now beacons 0 entries**
(identity-only, dv_dual_sf.lua:1717) — was packing its full rt; this also closes the
rt_merge mobile-as-transit case for the beacon plane; (b) the out-of-range `routing_sf`
viability-floor fallback now matches the Lua literal −240 (was clamp-to-SF12). Deferred
(noted in code): the explicit `route_uses_mobile_as_transit` rt_merge guard → rides with
the peer-mobility plane (its R1 trigger is closed by fix (a)).
**Track:** behaviour/R track, the FIRST behaviour milestone (after codec track C0–C6
COMPLETE). Spans both repos: `meshroute::Node` (emit + ingest + route table) +
`FirmwareNode`/`SimController` (config plumbing + the gate). Grounded in a 6-angle
extract + reconcile of the Lua beacon-emit state machine, route-merge, the Node/Hal
surface, and the verification path.

---

## 0. Goal + the headline correction

**Goal:** the `meshroute::Node`, running in-loop as a `FirmwareNode`, **emits periodic
beacons** (C5 `pack_beacon` via `Hal::tx`) and **ingests them** (`parse_beacon` → a
minimal route table), so two-plus meshroute nodes converge route tables the way the Lua
firmware does — verified Lua-engine vs meshroute-engine on a small beacon-only scenario.

**Headline correction (needs your sign-off — §7 Q1):** the verification model is
"compare delivery via `dm_delivery_breakdown`, Lua vs meshroute." That tool keys every
message on `rts_tx`/`data_tx`/`data_rx`/`ack_rx`/`delivered`
(`tools/dm_delivery_breakdown.py:172-189`) — **the full RTS/CTS/DATA/ACK data plane,
which does not exist until R3.** A beacon-only run emits **zero** of those events, so
`dm_delivery` reads 0% for *both* engines (degenerate parity = no signal). Therefore
**R1's acceptance metric is route-table CONVERGENCE + beacon-event parity, NOT
`dm_delivery`.** The C5 spec line calling BCN "the first `dm_delivery`-measured
behaviour milestone" (`2026-05-29-c5-bcn-design.md:6`) is wrong for a beacon-only
milestone and should be re-pinned; PORT_PLAN R1 already says the right thing ("matches
Lua beacon_tx cadence/events"). `dm_delivery` becomes the gate at **R3**.

**Scope note (R1↔R2 fold — §7 Q2):** PORT_PLAN splits beacon-emit (R1) from the
route-table DV-merge (R2). This proposal **folds the minimal K=3 route table into R1**,
because beacon-emit alone produces nothing observable on the receiver beyond `beacon_rx`
and there is no convergence to measure. The merge is self-contained and R2 needs it
regardless; only the 3-cycle prune lags to R2.

---

## 1. What the Node does (the minimal emit/ingest loop)

```
on_init(cfg):
  read NodeConfig (routing_sf, beacon_period_ms, quiet_threshold_ms, peer_count, is_mobile)
  _hal.set_rx_sf(routing_sf)                       // else nodes can't hear each other (RISK §6)
  _hal.after(rand_range(0, first_period), kBeaconTimerId)   // first_period = beacon_period_ms

on_timer(kBeaconTimerId):                          // the periodic loop
  build beacon_in { leaf_id, src=node_id, key_hash32, is_mobile,
                    entries = each rt[dest].candidates[0] (FULL page, ascending dest),
                    schedule/seen_bitmap/ext EMPTY }    // codec derives has_*=0
  pack_beacon -> _hal.tx(buf, len, {sf=routing_sf, label="BCN"})
  _hal.emit("beacon_tx", {n_entries, rt_total, kind="periodic", key_hash32, routing_sf})
  _hal.after(rand_range(period*4/5, period*6/5 + 1), kBeaconTimerId)   // ±20% re-arm, SAME id

on_recv(frame, meta):  if cmd_of(frame[0]) == 0x0:        // BCN
  parse_beacon(frame)
  rt_merge DIRECT { dest=b.src, next=b.src, hops=1, score=score_from_snr(meta.snr_q4) }
  _hal.emit("beacon_rx", {src=b.src})
  for each parse_beacon_entry:                            // DV merge
     skip if e.dest == self (split-horizon)
     score = snr_of_bucket_4b(e.score_bucket)             // bucket round-trip, NOT raw Q4 (RISK §6)
     rt_merge { dest=e.dest, next=b.src, n2_hop=e.next, hops=e.hops+1,
                score=min(score, snr_of_bucket_4b(rx)) }  gated combined_hops <= dv_hop_cap(16)
     _hal.emit("rt_update", {dest, next, score, hops, slot}) on new/promote/alt
  maybe_emit_rt_full()    // emit rt_full{peers} once rt_count >= peer_count
```

The periodic re-arm is **unconditional** (heartbeat never dies), re-using the same
timer id (`FirmwareNode::after` re-arms-by-id). `kBeaconTimerId` generalizes the
existing `kTxTimerId` one-shot at `node.cpp:18-25`.

---

## 2. Route table (the folded-in minimal R2)

```cpp
struct RtCandidate { uint8_t next_hop; int16_t score /*Q4*/; uint8_t hops;
                     uint64_t last_seen_ms; uint8_t n2_hop; bool is_gateway;
                     uint8_t learned_layer_id; };          // mirrors dv_dual_sf.lua:9646-9654
struct RtEntry     { std::array<RtCandidate, 3> candidates; uint8_t n; bool dirty; };
std::map<uint8_t, RtEntry> _rt;   // std::map = sorted iteration = the #1 determinism rule
```
- **K = MAX_RT_CANDIDATES = 3**, candidates sorted by the comparator below.
- `rt_merge` funnel (Lua `dv_dual_sf.lua:4484-4557`, `9580-9678`): DIRECT install
  (`hops=1`), then DV-merge each carried entry (`hops=entry.hops+1`,
  `score=min(rx,entry.score)`, split-horizon `e.dest!=self`, gated
  `combined_hops <= dv_hop_cap=16`).
- `route_strictly_better` (`dv_dual_sf.lua:4227-4245`): viability floor
  (`SF_DEMOD_THRESHOLD[sf] + sf_margin_q4`), then **fewer-hops-wins**, score breaks
  ties. **Budget + suspect penalties STUBBED to 0** (they are 0 until R4 anyway).
- Constants to add to `protocol_constants.h`: `MAX_RT_CANDIDATES=3`, `dv_hop_cap=16`.
  Store `n2_hop` + `last_seen_ms` now (cheap) so R2's 3-cycle prune + R2/R4 TTL drop
  in with no data-shape change.

**Score-bucket fidelity (load-bearing):** a carried entry's score is the 4-bit bucket
round-trip `snr_of_bucket_4b` (`dv_dual_sf.lua:836-838`), NOT raw Q4. Carrying full
precision instead would diverge route SELECTION (which next-hop is primary) from the Lua
even with identical beacons heard. Replicate the bucket centers exactly.

---

## 3. Determinism contract (the hard requirement)

The meshroute Node must draw from the host shared `mt19937` in **identical call-site
order** to the Lua. `FirmwareNode::rand_range` and `ScriptedNode::api_rand` are both
`uniform_int_distribution(lo, hi-1)` i.e. `[lo,hi)` — so the Node must pass Lua's
arguments **verbatim, including the `+1`**:
- initial arm: `rand_range(0, first_period)`
- periodic re-arm: `rand_range(period*4/5, period*6/5 + 1)` — **integer floor division**
  (`*4/5`, not `*0.8`); the `+1` makes `hi` inclusive.
- R1 runs **unthrottled** (`quiet_threshold_ms <= 0` fast path,
  `dv_dual_sf.lua:7704-7708`): immediate send, **no silence-jitter draw** — keeping the
  Lua and meshroute RNG streams aligned. The gate scenario MUST set
  `quiet_threshold_ms=0` on the Lua side too (t10 does; s01 does NOT — §6).
- every `pairs(self.rt)` site (DV-merge, `pack_beacon` entry collection) is `std::map`
  ascending-`dest` iteration — never `unordered_map`.

---

## 4. Acceptance / gate

**PRIMARY gate — a new t-test (no new infra; the t83/S2 pattern):**
clone `test/t10_dv_beacons.json` (2 nodes, 12 s, seed 42, `beacon_period_ms=5000`,
`quiet_threshold_ms=0`; asserts `beacon_tx` + `beacon_rx{src}` + `rt_update{dest}`) into
`test/t84_meshroute_beacons.json` with `engine:"meshroute"` on both nodes. **PASS = the
same `beacon_tx`/`beacon_rx{src}`/`rt_update{dest}` assertions pass under
`engine:meshroute`.** PHY tx/rx events come free from `FirmwareNode`+`SimController`
given `TxParams.label="BCN"`.

**SECONDARY gate — the S3 differential (richer, recommended):** run the *same* scenario
JSON twice flipping each node's `engine` (lua = REFERENCE vs meshroute), feed both NDJSON
streams to `analyze.py`, and compare (a) per-node converged route count (`rt_full peers`
/ `node_state_snapshot.rt_dst_count`), (b) `beacon_tx` count + cadence within a tolerance
band (NOT timestamp-exact — jitter makes exact timing fragile), (c) the
`rt_update`/`beacon_rx` effectiveness ratio (`analyze.py:862-893`). A measured regression
vs the Lua reference triggers investigation.

**Suite gate:** default `test/run_tests.sh` stays green (102/102 + t84); `t83` still
passes. `s01` is NOT an R1 gate (it uses the production 30 s throttle + asserts
data-plane events R1 cannot produce — keep it for R3+).

---

## 5. Non-goals (explicitly deferred, with the R-iteration that owns each)

| Deferred | Why safe to defer | Owner |
|---|---|---|
| Adaptive channel-busy throttle (`quiet_threshold_ms` gate) | R1 runs the `<=0` fast path; no duty pressure on the gate | R4 |
| Silence-jitter + post-jitter re-check | coupled to throttle; never entered on the fast path (and it draws a determinism-sensitive rand) | R4 |
| Max-idle B+C override | only matters when the throttle can suppress beacons | R4 |
| Triggered / sync / gateway_sweep beacon kinds | R1 emits only `periodic`; 5 s beacons converge a small mesh in time | R4 / R5 / R7 |
| dirty-only differential emit + `beacon_offset` paging | R1 ships FULL pages (≤15 nodes fit one page); keep the `dirty` bool, ignore it | R2/R4 |
| Per-candidate TTL eviction (45 min / 3 h) | short gate scenarios never hit the TTL; refresh `last_seen_ms` now | R2/R4 |
| 3-cycle prune (`rt_prune_cycle`, n2_hop) | acyclic small R1 topo converges without it; store `n2_hop` now | R2 |
| Budget-tier (duty-cycle) skip + `tx_flood` LBT/duty defer | no duty pressure on the gate; Hal already exposes the airtime hooks | R4 |
| Gateway schedule block | R1 nodes aren't gateways; C5 codec already supports it (leave `schedule` empty) | R7 |
| Seen-bitmap (32 B reachability) | redundant under FULL-page beaconing; `seen_bitmap_enabled=false` on the gate | R5+ |
| ext-TLV block | C5 keeps it OPAQUE by design; leave `ext` empty, ignore the parse span | R4/R5/R7 |
| Discovery fast-cadence state machine | the gate sets a single `beacon_period_ms` directly; revisit only if convergence timing diverges | later |
| Half-duplex `pending_tx`/`pending_rx` skip | no data plane in R1 → always nil → no-op stub | R3 |
| `on_preamble_detected` / `on_radio_busy` hooks | no throttle, no LBT defer in R1 → no-ops | R4 |

---

## 6. Porting steps

0. **(prereq, flag it)** No dated S3 spec exists; PORT_PLAN orders S3 before R1. But the
   PRIMARY gate (t84) is a self-contained t-test needing **no diff tool** (the t83
   pattern), so R1 is not blocked. The S3 differential (§4 secondary) is a thin diff
   script (parse both NDJSON, compare converged route sets + `beacon_tx` counts) — author
   it alongside R1, not as a blocker.
1. **NodeConfig plumbing:** `FirmwareNode::onInit` currently `(void)config`s
   (`FirmwareNode.cpp:42`). Map per-node scenario JSON → `meshroute::NodeConfig`
   (`routing_sf`, `beacon_period_ms`, `quiet_threshold_ms`, `seen_bitmap_enabled`,
   `is_gateway`, `is_mobile`) and plumb **`peer_count` (= N−1)** via a host-set seam
   (Lua derives it from `sim:nodes()` `dv_dual_sf.lua:8977`; the device has no
   `sim:nodes()`, so it's a config/setter, not a protocol constant — §7 Q4).
2. **Route-table state** in `meshroute::Node` (§2): the `RtCandidate`/`RtEntry` structs +
   `std::map` + the new constants.
3. **on_init:** read cfg → `set_rx_sf(routing_sf)` → arm first beacon.
4. **Periodic beacon body** in `on_timer(kBeaconTimerId)` (§1): build `beacon_in`,
   `pack_beacon`, `tx`, `emit beacon_tx`, re-arm same id.
5. **on_recv beacon ingest** (§1/§2): DIRECT install + DV-merge + `emit beacon_rx`/
   `rt_update`; stub budget/suspect penalties to 0.
6. **Convergence telemetry:** `maybe_emit_rt_full` after each merge (`rt_full{peers}`),
   + a `node_state_snapshot{rt_dst_count,rt_total_candidates}` on a small snapshot timer
   so convergence is sampled over time (`analyze.py` §16).
7. **Gate:** author `t84_meshroute_beacons.json`; confirm `run_tests.sh` green + `t83`
   passes; run the S3 differential.

---

## 7. Open questions (recommendations; confirm or override)

1. **Acceptance metric = route-table convergence + beacon-event parity, NOT `dm_delivery`**
   (and re-pin the C5 spec line) — **rec yes** (`dm_delivery` needs the R3 data plane;
   beacon-only emits none of its events).
2. **Fold the full K=3 route merge into R1** (the R1↔R2 boundary) — **rec yes**
   (self-contained, nothing to measure otherwise; 3-cycle prune lags to R2).
3. **Gate = a new `t84` clone of `t10`** with `engine:meshroute` (keep Lua `t10` as the
   reference) — **rec yes**. Optional: a bespoke 3–5 node convergence scenario for fast
   iteration (à la the s13 pull-storm repro).
4. **`peer_count` via a host-set `NodeConfig` seam** — **rec yes**.
5. **FULL-page beacons for R1** (defer dirty-only + `beacon_offset`) — **rec yes**;
   the differential then compares a stable subset of `beacon_tx` fields, not the
   `beacon_diff_breakdown`.
6. **S3 differential = a thin metric-level diff script** (converged route set +
   `beacon_tx` count within tolerance, NOT timestamp-exact); the PRIMARY gate stays the
   self-contained `t84` so R1 isn't blocked on harness infra — **rec yes**.
7. **Skip the discovery state machine in R1** (single configured period) — **rec yes**;
   add it only if convergence timing diverges from the Lua reference.

## 8. Files

`MeshRoute/lib/core/node.{h,cpp}` (route table + beacon emit/ingest), `protocol_constants.h`
(`MAX_RT_CANDIDATES`, `dv_hop_cap`); `lora-universal-simulator/orchestrator/runtime/FirmwareNode.cpp`
(NodeConfig JSON plumbing + `peer_count` seam); `test/t84_meshroute_beacons.json`
(+ optional S3 diff script under `tools/`); re-pin the C5 spec acceptance line (pending Q1).
Uncommitted working artifact — you commit.
