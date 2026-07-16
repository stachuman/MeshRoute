# Team-plane liveness parity (Plane 2 — increment 2c) — design spec **v2** (post-QA)

*2026-07-16 v2. v1 was quality-assessed (2 rounds) → REVISE; this folds in every finding. Scope decided: **LIGHTER-PLUS** (dead-relay proactive demotion + the read-alias fix; defer the bidi/one-way plane). Sensitive: team-id ↔ static-id aliasing is THE hazard — and the QA found it is **already happening on committed code (`7053970`)**, so 2c is partly a separation *fix*, not only a feature. Grounded against the tree; the coder re-verifies each ref.*

## 0. Context — and the correction to v1's premise

- **2b** gave the team plane reactive discovery + a plane-aware reroute; `s26_team_reroute` proves it works. **But the reroute is RTS-timeout-driven**: in s26 the sender wastes **3 RTS on the dead relay** (initial + `rts_max_retries=2`) before cascading. 2c's job = demote a dead team next-hop **proactively** so the cascade skips it.
- **★ v1 was WRONG that "the team plane has NO liveness / is guarded-out."** The QA verified a **pre-existing team-id → static-array leak** on committed `7053970`:
  - **Write leak:** `learn_direct_neighbor(…, team_plane=true)` calls `mark_dest_seen(sender)` + `clear_peer_suspect(sender)` **unconditionally** (node_beacon.cpp:95-96), writing the **static** `_dest_seen_ms[team_local_id]` + `_peer_liveness`. Reached from the same-team-beacon learn (node_beacon.cpp:731) and the team-RTS reverse-learn (node_mac_rx.cpp:74).
  - **Write leak:** `update_link_bidi_from_beacon(b.src, …)` writes static `_link_bidi[team_local_id]` for a team beacon (node_beacon.cpp:819).
  - **Read alias:** `effective_score` subtracts `liveness_penalty_q4(c.next_hop)` (node_routing.cpp:148) and `candidate_degraded` reads `_link_bidi[c.next_hop]` (node_routing.cpp:328); the viability gate reads `is_next_hop_fresh(a.next_hop)` (node_routing.cpp:177-178). For a **team** candidate `c.next_hop` is a `team_local_id` → team route sorting reads the **static** liveness/bidi/freshness arrays at a team index. Reached via `sort_candidates ← refresh_route_order(…, Plane::TEAM) ← pick_next_cascade_hop` (node_cascade.cpp:72). (Read-only = wrong-answer, not corruption — but it's the same §18 class, and today the static `_dest_seen_ms[team_id]` is even *populated* by the write leak, so team routes pass the freshness gate *because of* the bug.)
- So **2c both adds team liveness AND routes these leaks to team-private state** — the separation §3 is violated today; 2c is where it's fixed.

## 1. Grounded facts (corrected)

- `_peer_liveness[cap_peer_liveness=64]` (node.h:1204; `cap_peer_liveness=64` protocol_constants.h:156), **self-slotted** by `peer_liveness_slot(node_id, create)` (node.h:774) — an on-demand LRU that stores its own `node_id` key (self-keyed → a stale slot is self-detecting). `PeerLiveness` = `uint8_t node_id; uint16_t rts_timeouts; 8×uint64_t` ≈ **72 B** (node.h:1018-1021).
- Tiers: **suspect@1** / silent@3 / dead@6 (`peer_suspect_rts_timeouts=1` [not 2 — the constant + the node.h:425 comment both drifted], `peer_silent_rts_timeouts=3`, dead@6; protocol_constants.h:137-138). Penalties suspect **192** / silent **640** / dead **1280** Q4 (protocol_constants.h:144-146).
- `liveness_penalty_q4(next_hop)` (node.h:757 / node_routing.cpp:86) — READ. `record_peer_rts_timeout` (node_routing.cpp:594) — WRITE (timeout→tier). `clear_peer_suspect(node_id, src)` (node_routing.cpp:613) — WRITE (heard→alive, clears tiers). `candidate_degraded(c)` reads `_link_bidi[c.next_hop]` (node_routing.cpp:326). `is_next_hop_fresh` (node_routing.cpp) IS consulted (viability gate node_routing.cpp:177-178) — v1's "unused" was refuted.
- `refresh_route_order(dst, reason, Plane)` (node.h:763) is plane-aware **for table selection only** (`rt_find(dst, plane)`, node_routing.cpp:257); its sort (`sort_candidates → route_strictly_better → effective_score`) reads liveness/bidi/freshness on **static** arrays with **no** plane dispatch. That dispatch is the work.
- `cascade_to_alt` team branch (node_cascade.cpp:127) returns at :136 **before** the §P3 liveness gate (`liveness_penalty_q4(from_next) >= peer_silent_penalty_q4=640`, :143) and the one-way slow-reprobe (:150-175). `record_peer_rts_timeout`/`note_link_confirmed` team writes ARE guarded (node_cascade.cpp:350/377; node_mac_rx.cpp:419-422) — but the **beacon/RTS-*learn* path is not** (the leak above).
- `age_out_stale_routes` already ages `_rt_team` (node_routing.cpp:417-423) → a demoted team route needs **no explicit eviction** (liveness demotes via `effective_score`; aging removes it).
- There is **no `cap_team` constant** — the 2b `_rreq_*_team[16]` use a bare literal / local `sizeof(arr)/sizeof(arr[0])`.

## 2. Design — LIGHTER-PLUS (the minimum that delivers proactive demotion + closes the alias)

**Why not the pure "lighter subset" (record + penalty only):** the reroute skip fires at penalty ≥ **640** (SILENT, 3 timeouts); one cascade giveup records **one** timeout → SUSPECT (**192** < 640) → still full-budget retry. SUSPECT only *bites* through the **sort** (`effective_score` demotes the SUSPECT candidate so an alt outranks it). So the read-fork is mandatory, not optional.
**Why not the full fork:** the one-way/slow-reprobe/bidi-heard-set targets a *silently-degraded-but-liveness-healthy* link (needs team heard-set gossip) — not the hiking team's cleanly-dead relay. Defer it; that deferral is what lets `candidate_degraded` return wire-only for team.

**(a) State — ONE self-contained team table (`#if MR_FEAT_TEAM`):**
- `PeerLiveness _team_liveness[cap_team_liveness]` + `uint8_t _team_liveness_n` — a **mirror of `_peer_liveness`**, keyed by `team_local_id`, with its **own** on-demand LRU. `constexpr uint8_t cap_team_liveness = 16` in protocol_constants.h (named like `cap_peer_liveness=64`).
- **No** `_dest_seen_ms_team` / `_link_bidi_team` / `_link_reprobe_last_ms_team` (freshness + bidi are deferred/omitted — see the read-fork below).

**(b) Slot finder — self-slotted, NEVER `_team_keys`:**
- `PeerLiveness* team_liveness_slot(uint8_t team_local_id, bool create)` — a copy of `peer_liveness_slot`'s find-or-LRU-create over `_team_liveness`, keyed by `team_local_id`. **Rationale (QA):** `_team_keys` is an LRU ring keyed by crypto-key *recency* (`team_key_set`, node_routing.cpp:655-667, evicts oldest `last_seen_ms` at n==16); liveness wants eviction by *liveness* relevance. Sharing the index forces two lifetimes onto one eviction clock — a key-cache eviction would silently rebind a peer's suspect/silent/dead onto whoever next takes the slot = intra-team corruption (exactly what §3 guards against). Self-slotting is what parity means: one self-describing map that never reads `_team_keys` or any static array.

**(c) Fork the WRITE path (team liveness accrues) — the three functions, gated on the team next-hop:**
- `record_peer_rts_timeout(id, ctr, team_plane)` → `_team_liveness` when team.
- `clear_peer_suspect(id, src, team_plane)` → `_team_liveness` when team. **★ Mandatory even in the lightest scope (QA #4):** without recovery-on-heard a transiently-missed team relay stays demoted forever. Clear on any team frame heard FROM the peer (team RTS/CTS/ACK/beacon).
- Wire: the cascade giveup on a team flight records the team timeout; a heard team frame clears it. (These REPLACE the guarded-out no-ops with real team-variant calls.)

**(d) Fork the READ path (the sort demotes + the alias is closed) — thread a `team_plane`/candidate-plane through:**
- `effective_score` → subtract `liveness_penalty_q4(c.next_hop, team_plane=true)` reading `_team_liveness` for a team candidate.
- `candidate_degraded(c, team_plane)` → for a team candidate return **`c.degraded_from_wire` only** (no `_link_bidi[team_id]` read) — this **eliminates** the committed static-`_link_bidi` read alias.
- The viability gate (`route_strictly_better`, node_routing.cpp:177-178) → for a team candidate **skip `is_next_hop_fresh`** (treat as fresh-viable): the team plane keeps no `_dest_seen_ms` (omitted, decision #4), the cleanly-dead relay is handled by the liveness *penalty*, and this **eliminates** the static-`_dest_seen_ms[team_id]` read alias. The plane is threaded from `refresh_route_order(…, Plane::TEAM)` → `sort_candidates` → `route_strictly_better`/`effective_score`.

**(e) Wire the cascade demotion:** in the `cascade_to_alt` team branch (node_cascade.cpp:127-137), **before** the team RREQ, consult `liveness_penalty_q4(from_next, team_plane=true)` — the just-recorded SUSPECT (via the sort in the next `pick_next_cascade_hop`) makes the sender prefer the alt, skipping the dead relay. (No static §P3 gate; no team slow-reprobe.)

**(f) Fix the pre-existing WRITE leaks (bundled separation fix):** `learn_direct_neighbor(team_plane=true)` must NOT call the static `mark_dest_seen`/`clear_peer_suspect` (route it to the team path or drop the freshness stamp, since team freshness is omitted); `update_link_bidi_from_beacon` must not write static `_link_bidi[team_src]` for a team beacon. **⚠ Sequencing:** removing the static `_dest_seen_ms[team_id]` write **without** the read-fork (d) would break team routing (the viability gate would read a now-0 static freshness → team candidate non-viable). So (d) and (f) land together.

## 3. Separation — the invariant (the whole point)

Every team-liveness read/write lands on `_team_liveness` via `team_liveness_slot` — **never** a static `_peer_liveness`/`_dest_seen_ms`/`_link_bidi`/`_link_bidi_confirmed_ms`/`_link_reprobe_last_ms`. The slot map is **total** (every team next-hop that can appear in a sort/cascade gets a slot on demand) and **collision-free within a team** (self-keyed by `team_local_id`). **s18 inert** (empty for `team_id==0`; `MR_FEAT_TEAM`-gated → md5 `3ac88d40`). The QA acceptance = "the slot map is total + collision-free, and no team read/write touches a static array."

## 4. RAM

One `PeerLiveness _team_liveness[16]` ≈ 72 B × 16 ≈ **~1.15 KB × n_layers** (≈ 2.3 KB across 2 layers). No `_dest_seen_ms_team`/`_link_bidi_team` (deferred). `sizeof(Node)` tripwire **will move** (currently 215784) — update the `static_assert` consciously + capture the **per-board `.bss`/`.data` delta** (native's 8-B alignment hides a 4-B-align board padding shift — the trap that caught the +8 B team block before).

## 5. Gate (the mobile recipe)

- **s18 md5 `3ac88d40` EXACT** (team liveness inert on static).
- **s22–s26 all 0-fail** (functional; s26 stays the delivery proof — but its `expect` can't see the latency win).
- **★ NEW native team-liveness unit test = the acceptance signal (QA #3):** deterministically demote a team next-hop (record N timeouts → SUSPECT/SILENT) → assert the cascade/sort **skips** it (picks the alt, **no RTS burst on the dead relay**) → then a team frame heard from it **clears** the demotion (recovery); AND a static node's `_peer_liveness` is **untouched** (the separation assertion, GLOBAL-forced like `rt_has_global`). A count is stable in native; sim RTS counts are timing-fuzzy — keep any s26 count check *soft*/telemetry-only.
- **native + 10 boards sequential + per-board RAM delta** = the budgeted `_team_liveness` add.
- **separation audit** (a workflow, as for 2b): the slot map is total + collision-free; no team read/write touches a static array; the three v1-leak sites (§0) are closed.

## 6. Decisions (resolved by the QA)

1. **Slot map — self-contained `team_liveness_slot` + `_team_liveness[16]`** (reject the `_team_keys` reuse). §2(b).
2. **Fork scope — LIGHTER-PLUS:** `record_peer_rts_timeout_team` + `clear_peer_suspect_team` + `liveness_penalty_q4(team)` + the `effective_score`/`candidate_degraded`/viability-gate read-fork. **Defer** the bidi/one-way/slow-reprobe plane (`_link_bidi_team` + `update_link_bidi_from_beacon` — needs team heard-set gossip). **Omit** `is_next_hop_fresh`/`_dest_seen_ms_team` for team (skip the freshness gate for a team candidate).
3. **`cap_team_liveness = 16`** constexpr (protocol_constants.h), named like `cap_peer_liveness`.
4. **`clear_peer_suspect_team` is NOT optional** — recovery-on-heard is part of the minimum.
