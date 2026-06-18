// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>

# Routing Liveness + Freshness Plane — phased Lua-parity port

**Status:** coder instruction (audit ratified 2026-06-17). The user does ALL commits — land each phase GREEN + uncommitted, report ready, I gate.

## Why

The C++ port has the route table, scoring skeleton, RREQ/RREP discovery, aging, and cascade-requeue — but the **peer-liveness + freshness plane is entirely unported** ("reroute around a dead relay"). The C++ admits it: `node_beacon.cpp:70` *"no id-bind/dest-seen/liveness plane"*, `:349` *"no liveness plane → suspect_level 0"*, `node_routing.cpp:69` *"suspect penalty deferred (0)"*. Consequence: a dead next-hop keeps full score and wins route selection until the 3 h remote-aging TTL — instead of losing to fresh discovery within a couple of failed flights. This is the anti-flood "reroute on failure" golden rule, currently missing.

Source of truth: `spec/dv_dual_sf.lua`. The plane is **5 parts** (the prior quick review covered the local (a)+(b) third):

| part | Lua | C++ today |
|---|---|---|
| (a) local detection — count RTS/ACK timeouts → suspect(2)/silent(3)/dead(6) tiers | `record_peer_rts_timeout@4519`, `mark/clear_peer_suspect@4476/4434`, `get_peer_suspect_level@3986` | ABSENT (constants only) |
| (b) score penalty — 12/40/80 dB in `effective_score` | `peer_suspect_penalty_db@4008`, `effective_score@4140` | `effective_score@69` has the **budget** penalty; liveness penalty **deferred (0)** |
| (c) freshness — dest-seen + 20 min `next_hop_live_ttl` → stale next-hop ineligible | `mark_dest_seen@1309`, `is_next_hop_fresh@4016` | ABSENT |
| (d) **distributed gossip** — advertise suspect/dead peers in beacon ext blocks | `build_suspect_nodes_ext@1373`, BCN ext types 1/2 (`@1241-1242`), parse ~`@1949`, apply ~`@9634/9665` | ABSENT (the mesh-scale half) |
| (e) **silent-next cascade** — primary silent → cascade to an alt immediately | `candidate_silent_counts@5558` + hooks in `rts_timeout_fire`/`ack_timeout_fire` | ABSENT |

Plus **②** the `id==0`/`dest==0`/`via==0` sentinel leak (cheap, independent). **③** (A↔B mutual-refresh) is **not** a separate bug — it is a *symptom* of the missing (c); the Lua never guards the merge refresh, it makes a stale next-hop ineligible. Build (c) → ③ resolved. **④** (rt_aging 3 h) is a band-aid; (b)+(c)+(e) is the real fix — evaluate ④ for deletion after, likely unneeded.

## ★ Keystone discipline SHIFT — read this first

The liveness plane is a new *behavior* the byte-identical s18 keystone (`306c3cf4`) lacks, and a faithful Lua port runs it ON by default (no config gate — gating it off would defeat the purpose + diverge from Lua). So activating it changes routing, and **the gate shifts from byte-identical md5 to RESULT COMPARISON via `tools/dm_delivery_breakdown.py`** — we gate the *delivery outcome* across a **baseline SUITE**, not the byte stream of one scenario.

**BASELINE SUITE (captured 2026-06-17, current HEAD — the reference to beat). Full manifest + per-scenario run commands + caveats: `simulation/BASELINE.md`.**
Per scenario: `lus -e meshroute simulation/<s>.json /tmp/<s>.ndjson` then `dm_delivery_breakdown.py simulation/<s>.json /tmp/<s>.ndjson --failures`:

| scenario | role | same-layer (arr/sent) | cross-layer (deliv/sent) | leaks |
|---|---|---|---|---|
| `s18_meshroute` | single-layer dense — **anchor** (single-hop) | **108/113 (96%)** | — | — |
| `s19_singlelayer_multihop_chain` | single-layer **MULTI-HOP** (redundant 3-hop chain; the liveness home) | **8/8** (2–3-hop) | — | — |
| `s09_two_layer_gateway` | 2-layer cross-layer | 1/1 | **2/2 (100%)** | — |
| `s10_two_layer_separation` | 2-layer cross-layer | 1/1 | **2/2 (100%)** | — |
| `s16_dense_gateway` | dense 2-layer **overload stress** | — | **12/80 (15%)** | — |
| `s15_three_layer` | **3-layer** cross-layer + channels | 47/48 | **14/21 (67%)** | **0** |
| `s17_metro` | **252-node scale** + channels | **30/30 (100%)** | n/a (inert) | **0** |

**The gate per behavior-changing phase (2–4)** — run the suite; vs the table require:
- **same- AND cross-layer delivery do NOT regress** (s18 ≥108/113 · **s19 8/8 multi-hop** · s09/s10 2/2 · s16 ≥12/80 · s15 ≥14/21 and 47/48 · s17 ≥30/30) — **ideally they IMPROVE** (liveness should convert `SL: next-hop silent`/lost failures into reroutes). A drop is a FAIL. **s19 is the multi-hop reroute home** — gate its delivery 8/8 + `mean_hops` (A↔B **3**, A↔R **2**; the tool counts hops via `data_rx` receivers).
- **`leaks == 0`** everywhere (Principle 11) — hard invariant (s15/s17 carry channels).
- **failure taxonomy no worse** — no NEW failure class; the `--failures` `SL: next-hop silent` bucket trends down, not up.
- **mean_hops sane + event-count within ~±3 %** of each scenario's baseline — the supplements that catch a *delivery-neutral* airtime/churn regression delivery-% can't see (byte-identical gave this for free).

**Per-scenario reading:** s16 15 % / s15 67 % are stress / 3-layer difficulty — the gate is *no-regress, not 100 %*. **s17's cross-layer is inert** (translating it degrades the dense scenario, channels 101 %→33 %) → gate s17 on **same-layer 30/30 + channels leaks 0 + scale**, NOT cross-layer (XL coverage = s09/s10/s15/s16). XL-at-scale window tuning is a separate follow-up. See `BASELINE.md`.

**The two phases that stay TIGHTER (byte-identical still applies — behavior is invariant there):**
- **Phase 0** — no `id==0` in s18 ⇒ the guards are inert ⇒ s18 stays **byte-identical `306c3cf4`**.
- **Phase 1** — state tracked but not applied ⇒ routing unchanged; only new telemetry. Prove with a **filtered** check: `grep -v` the new event types from both the fresh run and the `306c3cf4` baseline → the remainders' md5s MUST match. (Delivery-breakdown is also unchanged here.)

**Why the shift (the honest trade):** byte-identical caught *any* drift (timing, airtime, churn) for free; result-comparison gates the *outcome* and is blind to a delivery-neutral airtime/churn regression — hence the event-count guard + `mean_hops` supplements, and keeping byte-identical where behavior is invariant. The NEW scenarios `t96`/`t97` are gated the same way: delivery in a dead-relay scenario must **improve** vs the pre-liveness run.

Every phase also gates: `pio test -e native` (new units) + `pio run -e gateway -e xiao_sx1262 -e heltec_v3` (3 boards).

## Constants

Most tiers/TTLs are already mirrored in `protocol_constants.h` (`peer_suspect/silent/dead_rts_timeouts`, `_penalty_q4`, `_ttl_ms`, `peer_dead_evidence_window_ms`, `next_hop_live_ttl_ms`) — **confirm against Lua 944-959 and fill any gap**. Values: timeouts 2/3/6; penalties 192/640/1280 Q4 (12/40/80 dB); TTLs 5 min/15 min/1 h; evidence window 15 min; `next_hop_live_ttl_ms` 1 200 000 (20 min).

---

## Phase 0 — `id==0` / sentinel hardening  (TOP, standalone, byte-identical)

The user's explicit items + the broader ②. An unprovisioned node (`id==0`) and the `0` sentinel must never enter routing.

1. **A node with `id==0` must NOT emit a BCN.** Guard the common emit path: at the top of `emit_beacon` (`node_beacon.cpp:130`) add `if (_node_id == 0) return;`. This covers periodic + triggered + every caller (broader than the existing `join_required` gate at `periodic_beacon_fire:416`, which stays).
2. **A received BCN with `src==0` must be DROPPED.** At the top of `ingest_beacon` (`node_beacon.cpp:255`, before the `b.src==_node_id` self-guard at `:260` and before any `learn_*`) add `if (b.src == 0) return;`.
3. **Reject the `0` sentinel in the route learners** (mirror the existing `0xFF` guards):
   - `learn_route_via` (`node_beacon.cpp:56`): add `|| dest == 0 || via == 0`.
   - `learn_direct_neighbor` (`node_beacon.cpp:72`): add `|| sender == 0`.
   - The beacon entry-ingest loop (`node_beacon.cpp:~344`) and `rt_insert` (`node_routing.cpp:22`): reject `e.dest == 0` / a `0` next-hop.

**Gate 0:** s18 **byte-identical `306c3cf4`** (no `id==0`/`0` in s18 ⇒ inert) · native units (a BCN with `src==0` is dropped; an `id==0` node emits no beacon; a `dest==0`/`via==0` candidate is rejected) · 3 boards.

## Phase 1 — local liveness STATE  (detection only, behaviorally inert)

Port the detection + tracking; do **not** yet apply it to scoring/selection/cascade.

1. **Per-next-hop timeout counter + tiers.** Port `record_peer_rts_timeout` (`@4519`): a map `node_id → {count, first_timeout_ms}`; on the configured thresholds promote to suspect(2)/silent(3)/dead(6) with the per-tier expiry TTLs + the 15 min evidence window for dead (`mark_peer_suspect@4476`, `get_peer_suspect_level@3986`). A successful CTS/ACK from the node resets it (`clear_peer_suspect@4434`). Emit `peer_rts_timeout_count` / a tier-change event (match the Lua).
2. **Hooks:** call the counter from `rts_timeout_fire` (`node_cascade.cpp:228`) and `ack_timeout_fire` (`:239`); call the reset from the CTS/ACK success paths.
3. **dest-seen stamping.** Port `mark_dest_seen` (`@1309`): stamp `dest_seen_ms[node_id] = now` on every frame RX from a node (the RX learn sites in `node_mac_rx.cpp` / `node_beacon.cpp`). Provide `is_next_hop_fresh(node_id)` (`@4016`) computing `now - dest_seen_ms[node_id] <= next_hop_live_ttl_ms` — **defined but not yet consulted** in Phase 1.

**Gate 1:** native units (2 timeouts → suspect; 3 → silent; 6 over ≥15 min → dead; <15 min → not-yet-dead; CTS/ACK success → clear; `is_next_hop_fresh` true/false by age) · s18 re-recorded but **filtered-byte-identical**: `grep -v -E '"peer_rts_timeout_count"|"peer_tier_change"|"dest_seen"…'` the new events out of both the fresh run and the `306c3cf4` baseline → the remainder md5s MUST match (proves routing decisions + delivery unchanged) · 3 boards.

## Phase 2 — APPLY the penalty + freshness gate  (behavioral activation; resolves ③)

This is where the discipline shifts to semantic parity.

1. **(b) Liveness penalty in `effective_score`** (`node_routing.cpp:68-69`): replace the deferred-0 with `peer_suspect_penalty_q4` for the candidate's next-hop (suspect 192 / silent 640 / dead 1280 Q4), **added to** the existing `budget_penalty_q4` — i.e. `score - budget_penalty - liveness_penalty`. (Lua `effective_score@4140` subtracts both.)
2. **(c) Freshness eligibility:** a candidate whose next-hop is `!is_next_hop_fresh` is **non-viable** in `route_strictly_better` (`node_routing.cpp:72`) — it loses to any fresh candidate and to fresh discovery. This is the Lua-faithful fix and it **subsumes ③**: the unconditional `last_seen` refresh at `node_routing.cpp:215` is now harmless because a stale-next-hop route can't be *selected*.
3. Re-rank on a tier change (the existing `resort_routes_for_neighbor_penalty@120` already re-sorts under a penalty change — wire the liveness tier into its trigger, alongside the budget tier).

**Gate 2:** native units (a dead-tier next-hop loses to a fresh alt in `route_strictly_better`; a stale next-hop is non-viable; the penalty stacks with the budget penalty) · **the `simulation/BASELINE.md` suite** (s18/s09/s10/s16/s15/s17) — no same/cross-layer delivery regression vs the table, **`leaks == 0`**, taxonomy no-worse, mean_hops + event-count (±3%) sane · **NEW sim gate `t96_liveness_reroute`**: a relay on the best path stops responding → the originator's traffic reroutes to an alt within a couple of failed flights (assert delivery continues; assert the dead relay's route is demoted), vs stalling ~3 h before · 3 boards.

## Phase 3 — silent-next CASCADE  (the reroute reaction at the sender) — DONE · GATED-PASS 2026-06-18

**As-built (drift from the literal spec — ACCEPTED, gated):** the literal "per-timeout counting → cascade on the first failure" was tried and **regressed the suite** (s18 108→107, events **+17.8%**) — per-timeout evidence is too churny under dense transient-collision traffic, and the with-alt early-cascade is largely subsumed by Phase 2's pick-time resort. Rebuilt on the **persisted tier** (no per-timeout counting):
1. **Early-cascade on an already-silent primary** — `rts_timeout_fire`/`ack_timeout_fire` (`node_cascade.cpp:242/258`): if `liveness_penalty_q4(next) >= peer_silent_penalty_q4` → `cascade_to_alt` immediately instead of burning same-hop retries. Reads prior-flight evidence; gated on **silent** (not suspect) to avoid churn (2nd drift, same justification).
2. **No-alt RREQ-on-silent** — `cascade_to_alt` no-alt branch (`:106-107`): when the failed primary is silent/dead, `emit_route_request(dst, dv_hop_cap)` (full-radius, rate-limited) before requeue. **Closes the actual bug** — a dest reachable only via a departed relay floods an RREQ instead of stalling on the 3 h aging.

**Gate 3 (PASS):** native **22643/0** + 3 §P3 units (RED-verified): early-cascade-to-alt-on-first-timeout · silent-single-candidate fires RREQ · **healthy single-candidate does NOT fire RREQ** (the anti-churn proof) · BASELINE suite **0-regression** (s18 +8 events = +0.005%, confirming early-cascade rarely fires in healthy traffic) · **t96** golden✓ · new **t97_liveness_rreq_rediscovery** golden✓ · 3 boards. Benefit proven by t96 (reroute) + t97 (RREQ fires) + units; a full multi-node RREQ→fresh-path→deliver sim is blocked by the **F-frame mislabel** (`label_of_frame` defaults F/Q/H/J/M → "BCN") — small follow-up to unlock it.

## Phase 4 — distributed liveness GOSSIP  (mesh-scale; BCN WIRE change)

Port the beacon extension blocks so the mesh converges, not just the failing node.

1. **Encode** (`build_suspect_nodes_ext@1373`): on `emit_beacon` (`node_beacon.cpp:130`/`pack_beacon@238`), append `BCN_EXT_TYPE_SUSPECT_NODES` (type 1) + `BCN_EXT_TYPE_LIVENESS_STATE` (type 2) TLVs carrying the node's locally-observed suspect/dead peers (with an advertise-until TTL).
2. **Parse + apply** in `ingest_beacon` (`node_beacon.cpp:255`): read the ext TLVs (~Lua `@1949`), and `mark_peer_suspect(node, level, remote_src=true)` (~Lua `@9634/9665`) so a remote observation demotes our routes via that peer.
3. ⚠ **Wire change** — the beacon grows an ext block. Honor the **beacon-truncation cut-order** (the ext block must fit `beacon_max_bytes` and never displace the routing entries / leaf header — same watch-item flagged for the Phase-2 leaf-membership work). Confirm the size budget.

**Gate 4:** native units (encode → parse round-trips; a received suspect list demotes the local route) · the **`BASELINE.md` suite** — no-regress + `leaks == 0` + confirm the beacon still fits (cut-order intact; watch s15/s17 channel reach) · **NEW sim gate `t97_liveness_gossip`** (≥3 nodes): node A detects relay R dead and advertises it; node C — which has NOT itself failed via R — avoids R based on A's gossip (assert C reroutes without its own timeout evidence) · 3 boards.

## Phase 5 (optional) — ④ aging relief

After Phases 2–4, re-evaluate `rt_aging_ttl_remote_ms` (3 h, `node_routing.cpp:261`). The liveness plane should make a dead relay lose within a couple of flights, so the 3 h aging is no longer the limiter — **likely leave as-is / drop this phase**. Only shorten if a gate scenario still shows a stale-route tail.

---

## Gate checklist (every phase)
- [ ] `pio test -e native` all pass (`.pio/build/native/program | tail -3` for the true count)
- [ ] delivery gate: **s18 byte-identical `306c3cf4`** (Phase 0) / **s18 filtered-byte-identical** (Phase 1) / **the `simulation/BASELINE.md` suite via `dm_delivery_breakdown`** (Phases 2–4) — no same/cross-layer regression vs the table (s18 ≥108/113 · **s19 8/8 multi-hop** · s09/s10 2/2 · s16 ≥12/80 · s15 ≥14/21+47/48 · s17 ≥30/30), `leaks==0`, taxonomy no-worse, event-count (±3%) sane
- [ ] new sim gate green (`t96` Phase 2/3, `t97` Phase 4) — delivery **improves** vs the pre-liveness run; prefer convergence toward `-e lua` s18
- [ ] 3 boards green
- [ ] leave GREEN + uncommitted — the user commits

## Phase order + dependencies
**0 → 1 → 2 → 3 → 4 → (5)**. 0 is standalone (byte-identical warm-up). 1 is the inert state foundation (de-risks the state machine against the baseline before activation). 2 activates (penalty + freshness, resolves ③) — the discipline shift. 3 + 4 build on the active plane (3 = local cascade, 4 = mesh gossip + the wire change). 5 optional. Each phase is independently gateable.
