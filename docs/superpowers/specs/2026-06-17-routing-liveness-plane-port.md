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

The s18 keystone has been **byte-identical `306c3cf4af65b56d6fc6415b964ad9f3`** because every prior slice was off-by-default-inert. **The liveness plane is NOT inert** — it is a new *behavior* the 306c3cf4 baseline lacks, and a faithful Lua port runs it ON by default (no config gate — gating it off would defeat the purpose and diverge from the Lua). So **activating it changes s18**, and the gate shifts from byte-identical to **semantic parity**:

- **Phase 0** is the one phase that STAYS byte-identical (no `id==0` appears in s18 → the guards are inert). Gate it the old way: s18 == `306c3cf4`.
- **Phase 1** is behaviorally inert but adds diagnostic telemetry → re-record s18, and prove inertness with a **filtered** check: `grep -v` the new event types, then the remainder is byte-identical to the 306c3cf4 baseline's same filter (routing decisions unchanged; only new events added).
- **Phases 2–4** change routing decisions → **re-baseline s18** to a new golden. The load-bearing gate is now **delivery-metric parity**: delivered count ≥ the current baseline, **leaks == 0**, no new stalls — ideally *improvement* (fewer dead-relay stalls), and the C++ behavior should move *toward* the `-e lua` s18 (we're porting a Lua mechanism). Plus a NEW targeted liveness gate scenario per phase (below).

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

**Gate 2:** native units (a dead-tier next-hop loses to a fresh alt in `route_strictly_better`; a stale next-hop is non-viable; the penalty stacks with the budget penalty) · s18 **RE-BASELINE** (new golden) — delivered ≥ prior baseline, **leaks == 0**, route-stall count not worse · **NEW sim gate `t96_liveness_reroute`**: a relay on the best path stops responding → the originator's traffic reroutes to an alt within a couple of failed flights (assert delivery continues; assert the dead relay's route is demoted), vs stalling ~3 h before · 3 boards.

## Phase 3 — silent-next CASCADE  (the reroute reaction at the sender)

Port `candidate_silent_counts` (`@5558`) + the cascade hooks: in `rts_timeout_fire`/`ack_timeout_fire` (`node_cascade.cpp:228/239`), when the primary next-hop is suspect/silent, **cascade to the next viable alt immediately** instead of retrying the dead path. (The cascade machinery exists; this adds the liveness-driven trigger.)

**Gate 3:** native units (primary silent → next attempt uses the alt, not a retry on the dead primary) · s18 re-baseline (delivery-metric parity, leaks 0) · `t96` extended: the reroute happens via cascade (assert the alt is used on the first failure, not after N retries) · 3 boards.

## Phase 4 — distributed liveness GOSSIP  (mesh-scale; BCN WIRE change)

Port the beacon extension blocks so the mesh converges, not just the failing node.

1. **Encode** (`build_suspect_nodes_ext@1373`): on `emit_beacon` (`node_beacon.cpp:130`/`pack_beacon@238`), append `BCN_EXT_TYPE_SUSPECT_NODES` (type 1) + `BCN_EXT_TYPE_LIVENESS_STATE` (type 2) TLVs carrying the node's locally-observed suspect/dead peers (with an advertise-until TTL).
2. **Parse + apply** in `ingest_beacon` (`node_beacon.cpp:255`): read the ext TLVs (~Lua `@1949`), and `mark_peer_suspect(node, level, remote_src=true)` (~Lua `@9634/9665`) so a remote observation demotes our routes via that peer.
3. ⚠ **Wire change** — the beacon grows an ext block. Honor the **beacon-truncation cut-order** (the ext block must fit `beacon_max_bytes` and never displace the routing entries / leaf header — same watch-item flagged for the Phase-2 leaf-membership work). Confirm the size budget.

**Gate 4:** native units (encode → parse round-trips; a received suspect list demotes the local route) · s18 re-baseline (delivery-metric parity; confirm the beacon still fits + cut-order intact) · **NEW sim gate `t97_liveness_gossip`** (≥3 nodes): node A detects relay R dead and advertises it; node C — which has NOT itself failed via R — avoids R based on A's gossip (assert C reroutes without its own timeout evidence) · 3 boards.

## Phase 5 (optional) — ④ aging relief

After Phases 2–4, re-evaluate `rt_aging_ttl_remote_ms` (3 h, `node_routing.cpp:261`). The liveness plane should make a dead relay lose within a couple of flights, so the 3 h aging is no longer the limiter — **likely leave as-is / drop this phase**. Only shorten if a gate scenario still shows a stale-route tail.

---

## Gate checklist (every phase)
- [ ] `pio test -e native` all pass (`.pio/build/native/program | tail -3` for the true count)
- [ ] s18: **byte-identical `306c3cf4`** (Phase 0) / **filtered-byte-identical** (Phase 1) / **re-baselined with delivery-metric parity + leaks==0** (Phases 2–4)
- [ ] new sim gate green (`t96` Phase 2/3, `t97` Phase 4) — and prefer the C++ behavior to converge toward `-e lua` s18
- [ ] 3 boards green
- [ ] leave GREEN + uncommitted — the user commits

## Phase order + dependencies
**0 → 1 → 2 → 3 → 4 → (5)**. 0 is standalone (byte-identical warm-up). 1 is the inert state foundation (de-risks the state machine against the baseline before activation). 2 activates (penalty + freshness, resolves ③) — the discipline shift. 3 + 4 build on the active plane (3 = local cascade, 4 = mesh gossip + the wire change). 5 optional. Each phase is independently gateable.
