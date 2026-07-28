# Team-plane routing: full static parity under co-channel isolation — design spec

*2026-07-27. Follow-on to `2026-07-15-team-plane-routing-parity-design.md` (which delivered team hash-resolution + team F discovery + team liveness, and shipped s24/s25). That spec built the team routing **machinery**. This one fixes the fact that almost nothing **feeds** it.*

*Original status line (2026-07-27): "DRAFT for dedicated review. Not implemented. All line references verified against the working tree at the time of writing (branch `main`, tip `aac7e61` + uncommitted CR work)."*

> ## ★ STATUS 2026-07-28 — REVIEWED (§10) AND PARTLY IMPLEMENTED
>
> ## ★★★ THE §0 BENCH FAILURE IS CLOSED (T7, 2026-07-28)
>
> **`T0 ✅ → T1 ✅ → (T2 ✅ ∥ T4 ✅) → T6 ✅ → T7 ✅ → T3 ❌ → T5 ❌`** — see the position note in §8.
> R1's functional claim now holds end to end: a member can address **any** teammate by id, heard or not, and the
> reverse ack routes on the team plane. **T6 is new**
> (§3/T6), from the owner ruling in §11, and was **pulled ahead of T3 by the owner on 2026-07-28** once the
> measurement showed it fixes a live defect in a configuration already run on metal, not latent hardening.
> ✅ **`s35` (§5) shipped 2026-07-28 as a pair — `s35a` (74 asserts) + `s35b` control (5)** — after the original
> was lost with an agent scratchpad. Discrimination is proven by poison probe, and its **declared gaps** (it does
> NOT cover T2; A2/A4 are algebra-only; A8 as spec'd is vacuous) are recorded in-file rather than left silent.
>
> **Read this document as a design record, not a work order.** The body below is the 2026-07-27 draft; every
> place implementation contradicted it now carries an inline correction marked ⚠ or ★. **Four of those
> corrections matter beyond this spec** and are the real yield of the build:
>
> 1. **§3/T4 + §6 — `q_opcode` is a 2-BIT field**, not an open enum. `team_sync` took the last codepoint; the
>    space is now full. An out-of-range enumerator truncates on the wire while comparing at full width — a
>    dead feature *and* an isolation leak, with no build error.
> 2. **§7/4 — `-Werror=switch` did not and could not cover it.** There are no switches over `q_opcode`; the one
>    switch that touches it takes a `uint8_t`. The gate protects an enum only where it reaches a `switch` **as
>    the enum type**.
> 3. **§3/T2 — the DATA frame *does* carry a hop count** (`committed_hops`). The exclusion argued from its
>    absence; §10.1 overruled it and T2 shipped origin-learning with an exact metric and no wire change.
> 4. **§3/T0 — three of the four hop-cap consumers, not four**, leaving team RREQ at cap 8 while team DV
>    accepts 16. **T3 owns resolving that.**
>
> ⚠ **§11 (new) carries the owner ruling that created T6** — RULED 2026-07-28, and the measurement behind it
> found an **R3 violation** (a homed member's team `-a` provokes four *static*-plane RREQ floods and returns its
> ack only when static infrastructure is reachable). §9 now tracks which of the six reviewer questions are closed. Line references in the body are as-of 2026-07-27 and have drifted — re-verify
> before relying on one (V1/V2).

---

## 0. Origin — the bench evidence

A three-member off-grid team (`team_id=0x06EF37AE`), all `is_mobile`, all off-grid (`node_id == team_local_id`), on one shared PHY (870 MHz / BW125 / SF6 / `sf_list=6`):

| node | leaf/layer | team routes held |
|---|---|---|
| 174 | 7 / 23 | `dest=213 next=234 hops=2`, `dest=234 next=234 hops=1` |
| 234 | 7 / 23 | (relay for both) |
| 213 | 4 / 20 | `dest=234 next=234 hops=1` **only** |

174 ↔ 213 have no direct link (three direct `RTS to=213` at t=18137371/563/824 drew no CTS; 234 overheard all three at rssi −121). 174 reached 213 anyway: its send deferred, fired `F … RREQ dst=213`, 234 answered `RREP dst=213 next_hop=174 hops=1`, and the DM was relayed. 213 could not reach 174 at all — `send 174 "…" -t` returned `err ctr=0 depth=0`, i.e. it was refused in `on_command` before a counter was minted.

**The smoking gun:** 213 *received* a message from 174 (`RECV from=174: Test next`) and the immediately following `routes` still showed `n=1`. Receiving a multi-hop team DM teaches the receiver nothing about its originator.

Two root causes, one structural:

1. **Discovery is unreachable from the one place that needs it.** `node.cpp:1082-1083` refuses a `-t` send to any id not already in `_team_peer`. Every team-RREQ entry point (`node_cascade.cpp:139/261/297`) sits *downstream* of `do_send`, so the guard forecloses the discovery that would satisfy it. A team id can only be discovered if it has already been discovered.
2. **The plane is starved of input.** Team routes install from exactly two events (a same-team beacon, `node_beacon.cpp:777`; an RTS *addressed to us*, `node_mac_rx.cpp:86-89`), against seven on the static plane. Steady-state beacons are dirty-only, and the 1-hop census that rescues the static plane from that is explicitly disabled for mobiles (`node_beacon.cpp:423`).

Aggravating: `node_routing.cpp:489` clears the `_team_peer` bit when the last route to a dest ages out, so every team route is a one-way ratchet toward permanently-unsendable.

---

## 1. Requirements (agreed with the owner, 2026-07-27)

| # | Requirement |
|---|---|
| R1 | Team-plane routing reaches the same functional parity as the static plane: a member can address **any** teammate by id, whether or not it has been heard, including 8 hops away. |
| R2 | **Isolation is unchanged.** Today's model stands: team frames are relayed **only** by same-team members; a static node drops them unconditionally and learns nothing from them. No static node spends state on team traffic. |
| R3 | Team-internal routing only — a hiking-group mesh, no static infrastructure in the path. |
| R4 | Target 3–10 members; 1–3 hops typical, stragglers to 8. Team hop ceiling **8**; static stays 16. |
| R5 | Convergence uses **both** proactive DV and reactive discovery (full static parity, not one or the other). |
| R6 | Link-quality parity included: one-way link sensing on the team plane, right-sized. |
| R7 | A test that **measures** isolation with team and static deliberately sharing freq, bw, sf and sf_list. |

**Non-goals.** Changing the isolation model (R2 is explicitly "keep what exists"). Static-plane behaviour of any kind. Team channel/M-frame work (`2026-07-26-team-encrypted-channel-design.md` owns that). Hash-locate resolution (the 2026-07-15 spec owns it, and it works). Static nodes tunnelling team traffic — considered and rejected by the owner.

---

## 2. Architecture

The static plane reaches an unknown, distant node through four cooperating mechanisms plus link sensing. The team plane has one and a half.

| # | Mechanism | Static | Team today | Slice |
|---|---|---|---|---|
| 1 | Learn a 1-hop neighbour from any RX event | 7 sites | 2 sites | T2 |
| 2 | DV gossip + 1-hop census in every steady beacon | `node_beacon.cpp:423` | disabled for mobiles | T3 |
| 3 | On-demand full-table pull (`REQ_SYNC` → `"sync"` beacon) | `node_mac.cpp:731` | dead (`node_query.cpp:52`) | T4 |
| 4 | On-demand route discovery (RREQ/RREP) | works | built, correct, **unreachable** | T1 |
| 5 | Link bidirectionality sensing | yes (2.3 KB) | none | T5 |

**Mechanism 3 deserves emphasis.** It is the cheapest and strongest of the four for this use case: zero idle airtime, and a single round trip transfers a teammate's *entire* table rather than one hop's worth per beacon period. It already exists in full — `send_req_sync_q` (`node_query.cpp:42`) fires a Q, `schedule_sync_response` (`:217`) answers with a `kind == "sync"` beacon, and `"sync"` is the one kind that bypasses `dirty_only` at `node_beacon.cpp:262`, so the reply carries the responder's whole table via the Phase-2 rotation at `:406`. `node_query.cpp:52` (`if (_cfg.is_mobile) return;`) makes it dead code for every team member, which in turn makes the Wave-4 antidote at `node_mac.cpp:724-732` a no-op on the team plane.

**Why DV alone is insufficient (R4/R5 interaction).** DV propagates roughly one hop per beacon period. At `team_beacon_period_ms = 300000` (`node_carriers.h:107`) an 8-hop path needs 20–40 minutes to converge — useless for a group still walking. Discovery resolves the same path in ~1 s. So T1/T4 are load-bearing and T3 is a warm cache that keeps `routes` populated for operator visibility.

### 2.1 Structural approach — full plane-parameterization

**Owner's decision** (I recommended the lower-risk alternative; the owner chose this, and the choice is recorded here deliberately). Guard expressions are rewritten to take an explicit plane rather than adding sibling `else if` branches beside untouched static conditions.

The upside is one unified code path and no accumulation of paired branches. The cost is that **s18 byte-identity becomes an empirical result rather than a property of the diff**: every rewritten expression sits on the hot static RX path, so "the static behaviour cannot have changed" must be *demonstrated* per slice instead of being obvious by inspection. §7 defines the discipline that compensates. A reviewer should treat §7 as load-bearing, not boilerplate.

---

## 3. Slices

Each slice is independently gateable. **T0 is a pure refactor and must land and gate alone** (C1: refactor XOR feature — never both in one slice).

### T0 — parameterization groundwork (refactor only, zero behaviour change)

- Add `uint8_t hop_cap_for(bool team_plane) const` returning `team_plane ? _cfg.team_hop_cap : _cfg.dv_hop_cap`, and route the four `dv_hop_cap` consumers through it: `node_route_discovery.cpp:225` (RREQ hop-cap guard), `:275` (the `2 * dv_hop_cap` RREP backstop), `node_beacon.cpp:832` (DV combined-hops cap), `node_cascade.cpp:139/297` (requery TTL).
- Thread an explicit plane parameter through the six learn call sites (`node_mac_rx.cpp:77/467/572/1251/1300`, `node_query.cpp:88`), the beacon packer entry-selection block (`node_beacon.cpp:396-435`) and the query path — **with every team branch still inert**.
- Introduce `team_hop_cap` in `NodeConfig` (`node_carriers.h`, beside `dv_hop_cap` at `:138`) seeded from a new `protocol::team_hop_cap = 8` (`protocol_constants.h`, beside `dv_hop_cap = 16` at `:174`).

At the end of T0 nothing behaves differently on either plane. **Gate: s18 md5 EXACT + native + every board env.** If T0 does not reproduce the keystone, stop — the parameterization is wrong, and no feature slice may proceed.

> ✅ **LANDED `2d0366d` (2026-07-28), 32/32 byte-identical** — the keystone reproduced exactly, so the parameterization is sound.
> ⚠ **CORRECTION — three of the four consumers, not four.** The **DV combined-hops cap (`node_beacon.cpp:846`)
> deliberately still reads `dv_hop_cap`**. Routing it through `hop_cap_for(same_team_beacon)` immediately halves
> the team DV radius and breaks s24/s25/s26/s28/s29/s30/s34. Recorded in-source at `node_beacon.cpp:853-856`
> and in the `node_carriers.h:146` DONE/MISSING note.
> ★ **This leaves a live asymmetry in the team plane's radius: team RREQ floods at `team_hop_cap` 8, but team
> DV accepts combined hops up to `dv_hop_cap` 16.** That is not what R4 intends and it is unresolved. **T3 owns
> it** — T3 is the DV slice, so it is the only honest place to either raise `team_hop_cap`, lower the DV cap
> with the scenario retunes that implies, or state in-source why the two radii legitimately differ.

### T1 — on-demand team discovery

- **`node.cpp:1082-1083`** — remove the `!is_team_peer(dst)` precondition for `Plane::TEAM`. Replace with a genuine configuration check only: refuse when `_cfg.team_id == 0` (not in a team) or `team_local_id() == 0` (team-DAD not yet complete). An unknown *teammate id* is no longer an error; it is the normal input to discovery.
- **`node_mac.cpp:68-73`** — the E2E-ACK gate refuses `-a` when `!is_team_peer(dst)`. Make it plane-aware: `!(plane == Plane::TEAM || is_team_peer(dst))`. `do_send` already receives `plane` (it is stored at `:80`), so no signature change.
- Team RREQ TTL escalation uses `team_hop_cap` (8) via T0's `hop_cap_for`.

**Resulting semantics for `send -t <unknown-id>`**, identical in shape to a static send to an unknown id:

| t | event |
|---|---|
| 0 | `do_send` → no `_rt_team` route → `node_mac.cpp:732` `defer_send` → console prints `queued ctr=N depth=1` |
| 0 | `node_cascade.cpp:261` emits a team-scoped RREQ at ttl=1 |
| +1 s | `try_drain_deferred` (`:297`) requeries at ttl=`team_hop_cap`; TTL escalation bypasses the `rreq_rate_ok` window (`node_route_discovery.cpp:66-70`) |
| +11 s, +21 s | further requeries, each gated by `route_request_seen_ttl_ms = 10000` (`protocol_constants.h:180`) |
| +30 s | `send_defer_ttl_ms` (`:258`) → `send_failed{no_route}` pushed to the app/console |

So an unknown id costs about three team-scoped floods over 30 s and then fails loud — bounded, and strictly better than today's instant refusal, which is loud but *wrong* for a teammate that genuinely exists.

**Note for the reviewer:** this deliberately removes the storm protection that the `:1082` comment cites ("don't storm the static plane"). That rationale does not survive scrutiny — a team RREQ is fully scoped (team-private rate ledger and dedup ring, `node_route_discovery.cpp:118/248-249`; `team_scoped=true` at `:122`; unconditional static drop at `:183-188`; same-team-only admission at `:314`) and therefore cannot reach the static plane at all. Please confirm this reading independently; it is the single assumption T1 rests on.

> ✅ **LANDED `36b19f3` (2026-07-28), with T1b folded in. The isolation assumption was confirmed independently
> and 32/32 came back byte-identical** — the change is reachable only from a team-plane send.
> ✅ The §5 "Also required" native bench-case test **was delivered** (`test/test_node_r3.cpp:2016+`).
> ⚠ **T1b was needed on top:** T1 opened a rate-limit wedge, closed by `age_out_rreq_last()`
> (`node_route_discovery.cpp`), which sweeps `_rreq_last` **and** `_rreq_last_team` on
> `route_request_seen_ttl_ms`. Note the TTL — my brief said `send_defer_ttl_ms` and the coder was right to
> reject it.
> ★★ **A FALSE INVARIANT WAS INTRODUCED HERE AND IS STILL IN THE SOURCE.** `node_mac.cpp:70` justifies the
> plane-aware E2E-ACK gate with *"the origin it stamps is the team_local_id, which every teammate CAN route."*
> **That is not true of a homed teammate:** `stamp_origin` (`node.h:819-822`) has no team-plane exception, so
> `_cfg.is_mobile && _my_mobile_reg.active` ⇒ `origin = _my_mobile_reg.home_id`, a **static** id. An off-grid
> member stamps its team id only because `node_id == team_local_id` there. ⇒ the TEAM plane currently carries
> **two different origin namespaces depending on whether the sender is homed**, and the argument that permits
> `-a` on the team plane rests on the half that is false. **This is the open owner ruling in §11.**

### T2 — neighbour-learning parity

Give the team plane the same RX-event coverage the static plane has. Each site currently excludes team traffic via `next_is_local_id()` (`node.h:141`) or `mobile_from`/`q.mobile`:

| site | frame | today | after |
|---|---|---|---|
| `node_mac_rx.cpp:77` | RTS (any, overheard) | static only (`!r.mobile_src`) | + team learn when the src is a same-team local id |
| `node_mac_rx.cpp:86-89` | RTS addressed to us | team learn (only case today) | unchanged |
| `node_mac_rx.cpp:467-468` | CTS from our next-hop | excluded by `next_is_local_id` | + team learn + team `note_link_confirmed` (feeds T5) |
| `node_mac_rx.cpp:572` | DATA prev-hop | excluded by `mobile_from` (set at `:381`) | + team learn |
| `node_mac_rx.cpp:1251` | ACK from our next-hop | excluded by `next_is_local_id` | + team learn + team confirm |
| `node_mac_rx.cpp:1300` | NACK from our next-hop | excluded by `next_is_local_id` | + team learn |
| `node_query.cpp:88` | Q sender | excluded by `!q.mobile` | + team learn (pairs with T4) |

> ✅ **LANDED `0041ed2` (2026-07-28)** — all seven sites, **plus DATA-origin learning**, which the exclusion
> note immediately below argued against and which **§10.1 overruled**. The note is retained for the record but
> ⚠ **its first bullet is factually wrong** (see the strikethrough) and its conclusion no longer describes the
> code. Implemented at `node_mac_rx.cpp:666`: `learn_route_via(origin, from, committed_hops + 1, …)`.
> Re-anchored s23/s24/s25/s26/s34; s18 keystone unmoved; no static scenario moved.

**~~Deliberately excluded~~ (OVERRULED by §10.1 — now implemented): learning from the DATA *origin*.** The bench smoking gun (213 received a DM from 174 and learned nothing about it) invites a rule like "install a route to the origin of any received team DM". Rejected, for two verified reasons:

- ~~**The DATA frame carries no hop count** — `d_in`/`d_out` (`frame_codec.h`) have no `hops` field. The receiver knows the origin exists but has no basis for a `hops` value, and `hops` is the primary sort key in `route_strictly_better`. Any value invented here is a fabricated route metric.~~
  ★ **FALSE — verified at `frame_codec.h:591`.** The DATA frame **does** carry `committed_hops`, a from-origin
  count at byte 4 (bits 2..0), incremented at every forward. `hops = committed_hops + 1` is an **exact** metric
  for every legal team path, not a fabricated one, so this bullet's conclusion inverts. It is also a 3-bit field,
  so `+1` can neither wrap nor exceed 8. **No wire change was needed** — which retires the companion decision
  §9/Q1 asks for.
- **Marking `_team_peer` without a route breaks a load-bearing invariant.** `node_beacon.cpp:73` documents "an `_rt_team` route ⟹ the `_team_peer` dispatch bit is set", and `node_routing.cpp:489` maintains it by clearing the bit on age-out. Setting the bit from a DM would decouple the two.

T1 makes this unnecessary: once an unknown teammate id is a valid send target, discovery finds the real path with a real metric in ~1 s. The correct fix for the symptom is T1, not origin-learning.

**Isolation constraint (R2), non-negotiable:** every one of these writes must land in `_rt_team` / `_team_peer` / `_team_liveness` and must never touch `_rt`, `_id_bind`, `_link_bidi`, `_dest_seen_ms` or `_peer_liveness`. The existing `next_is_local_id` guards exist precisely to enforce that; T2 must add a team destination for the excluded traffic, **not** relax the guard that keeps it out of static state. A reviewer should read every T2 hunk with that single question.

### T3 — team DV census

`node_beacon.cpp:423` gates the 1-hop census force-inject on `dirty_only && _cfg.n_layers != 2 && !_cfg.is_mobile`. Allow it when `team_emit` is true (`:388`), packing from `_rt_team` (`src_rt` already selects the plane at `:389`). This is what makes a stable team route re-advertise instead of being announced once and cleared at `:467`.

Bounded by the existing `max_entries` (`:379`) and `heard_set_census_min_headroom` (`:434`). At 3–10 members the cost is ~4 bytes per teammate on a beacon that already flies every 5 minutes.

`bidi_census_full` / `heard_set_complete` must be set on team beacons too — T5 depends on it (absence is only authoritative on a beacon that actually ran the census, per the M1 note at `:420-422`).

### T4 — team REQ_SYNC (on-demand full-table pull)

- **`node_query.cpp:52`** — `if (_cfg.is_mobile) return;` currently blocks all mobiles. The comment's reasoning is sound *for the static plane* (a mobile's local id must not leak into every static `_rt`) but does not apply to a team-scoped pull. Replace with: static-plane REQ_SYNC stays forbidden for mobiles; team-scoped REQ_SYNC is allowed for a team member with an adopted `team_local_id`.
  ⚠ **CORRECTION — two guards blocked this path, not one.** The adjacent **`_node_id == 0`** guard also
  refuses every **off-grid** member (s29's T3, s23's chain), which is precisely the bench configuration. Both
  needed the carve-out; naming only `is_mobile` would have left the feature dead for the users it was for.
- **Wire:** add a `q_opcode` value (`team_sync`) carrying a 4-byte `team_id` tail. `pack_q` is already variable-length by opcode ("4, or 5+4N for pull", `frame_codec.h:349`), so this adds a shape rather than changing one. A node that does not know the opcode ignores the frame. `q_in`/`q_out` (`:337-348`) gain `uint32_t team_id`.
  ★ **CORRECTION — "add a value" was not free. `q_opcode` is a 2-BIT field** (`(b3 >> 6) & 0x03`,
  `frame_codec.cpp:531/556`), and 1/2/3 were already taken. `team_sync` had to take **0, the last free
  codepoint, and the opcode space is now FULL** — a fifth Q kind requires a wire change. The tail is additive;
  the opcode was not. ⚠ **The out-of-range value this section implies (`= 4`) was probed rather than assumed,
  and it fails silently in the worst way:** it truncates to 0 on the wire while comparing at full width, so the
  feature never dispatches **and** the I7 gate stops firing (a static node gains a `q_rx`). Dead feature plus
  isolation leak, no build error, and telemetry that reads plausibly. Taking the zero enumerator is made safe
  by `pack_q` refusing a `team_sync` with `team_id == 0`, so a value-initialised `q_in{}` cannot air a
  scope-less team frame.
- **Response:** `schedule_sync_response` (`node_query.cpp:217`) answers a `team_sync` only if `same_team(q.team_id)`, and the resulting beacon is the existing team-tagged `"sync"` beacon — full `_rt_team` table via the Phase-2 rotation.
- **Trigger:** `node_mac.cpp:731`'s originator antidote becomes plane-aware, so a team originator with no route fires a team REQ_SYNC alongside the RREQ.

**No `wire_version` bump** (C4). The static Q wire is untouched; the new opcode is additive on a frame that is already opcode-variable.

> ✅ **LANDED `fe1c2fd` (2026-07-28).** All four bullets, with the two corrections above. **s28 is the only
> scenario of 32 that moved** (`e85ae061`/3534 → `27f486fa`/3552), attributed to one causal team_sync chain
> with `dm_delivery_breakdown` diff-identical; s18 keystone unmoved; no static scenario moved.
> ★ **I7 was measured BOTH WAYS on a single wire frame** — teammate answers, static node receiving the same
> frame emits nothing, and with the gate removed that static node airs its **static** table in reply. The gate
> sits at the `handle_q` dispatch site so the `return` is outside every `#if` and lands before the dedup ring.
> ⚠ **Left open, reported not fixed:** `schedule_sync_response`'s route-starved skip and its `rt_total` both
> read the **static** `_rt_count` on either plane — inert only because `sync_response_min_routes` defaults 0,
> and marked in-source as MISSING with its trigger condition. Also `channel_pull` carries no `team_id` (so it
> cannot get the leaf exemption `team_sync` gets) and airs `src = _node_id`.

### T5 — team bidi plane

The static bidi plane is `_link_bidi[256]` + `_link_bidi_confirmed_ms[256]` = **2304 B** (`node.h:1517-1518`). A mirror at that size is not acceptable on the nRF52840. Add a right-sized, self-slotted table in the style of `_team_liveness[cap_team_liveness=16]` (`protocol_constants.h:204`): 16 entries of `{uint8_t team_local_id; uint8_t state; uint64_t confirmed_ms;}`. With natural alignment that struct is 16 B, so **256 B** total. If that is judged too much, storing the timestamp as `uint32_t` seconds halves it to **128 B** at the cost of second-granularity decay — `bidi_confirm_ttl_ms` is 1200000 ms (`protocol_constants.h:198`), so seconds are ample. **Recommend the u32 form (128 B).**

- Fed by team CTS/ACK confirmations (T2) and by the heard-set scan currently skipped for team beacons at `node_beacon.cpp:868`.
- Enable the penalties currently zeroed for team: `bidi_penalty_q4` (`node_routing.cpp:161`), the freshness-viability bypass (`:192-193`), and `candidate_degraded`'s wire-only shortcut (`:361`).

This is what would have let 213 and 174 recognise that their direct link is one-way and commit to the 234 path deliberately rather than by accident.

**D2 obligation:** `node.h:1676` carries `static_assert(sizeof(Node) == 220592, …)`. T5 changes it. The assert must be updated **with the arithmetic spelled out in the comment** (matching the existing style), plus a per-board RAM diff — native alignment hides board padding.

### T6 — one origin namespace per plane + plane-keyed ledgers *(added 2026-07-28 by owner ruling — see §11)*

> ✅ **LANDED `9c7b40a` (2026-07-28).** Part A shipped with a **WIDER predicate than this section specifies, and
> correctly so** — `flight_is_team_plane()` is `rt_find`'s dispatch expression verbatim, so identity-claimed and
> route-taken cannot diverge; the literal `plane == Plane::TEAM` would have missed every **AUTO** team DM, which
> is what the corpus and the companion default actually use. **`_mediated_recent` was REFUSED with a
> disjointness proof** (its key is a global key-hash; the two writers' loser sets are disjoint on `b.is_mobile`),
> saving 256 B and keeping `sizeof(Node)` at **220592** — ⇒ ★ **this section's D2 warning was wrong: the layout
> does not move.** `s37_team_homed_origin` captures the R3 breach as an assertion. Full record in the
> `BASELINE.md` **T6** note.
> ✅ **T7 (`§team-parity T7`) closed the remainder** — one line, the removal of `&& is_team_peer(origin)`. Its
> §0 evidence: `s35a` **lost 69 events** because node 174 now learns 213 from the DM it receives and needs no
> discovery at all, and the reverse DM flies **2 s sooner**. ★ Its before-arm also exposed an **I2 breach no
> learn-site audit could have found** — see §10.3's amendment.

★ **Ordering: FIRST of the remainder — owner ruling 2026-07-28, pulled ahead of T3.** *"yes, do T6 first."*
Rationale: the §11 measurement showed this is a **live delivery defect in a configuration already run on metal**
(a homed teammate's team `-a`), whereas T3 is operator visibility plus the hop-cap asymmetry — neither a delivery
bug. It also had to precede T5 in any case, since T5 keys link state by id and must not be built on an ambiguous
id space. **Revised order: `T6 → T3 → T5`.**

**Part A — `stamp_origin` gains a plane.** `node.h:818`:

```cpp
item.origin = (plane == Plane::TEAM) ? team_local_id()
                                     : (mob ? _my_mobile_reg.home_id : _node_id);
```

⚠ **`stamp_origin(TxItem&)` does not currently receive `plane` — five callers must be checked, not one:**
`node_mac.cpp:88` (`enqueue_data`), `:343` (gateway envelope), `:518` (delegated/XL), and **`node_channel.cpp:562`
and `:1047`** — the channel-M paths, which have their own `-t` plane select (§S7 T-B) and must therefore be
decided deliberately rather than swept along. Prefer threading the existing `Plane` value over adding a bool
(U1/U2 — one conversion path for the carriers).

**Part B — the prerequisite, and it is not optional.** Plane-key the four ledgers of §10.3 / §9-Q4:
`_seen_origins` (`node.h:1564`), `_per_origin_channel` (`:1552`), `_hash_query_seen` (`:1499`),
`_mediated_recent` (`:1390`).

★★ **Why B cannot be deferred, and the strongest argument in this section: the arc is actively destroying the
safety arguments these ledgers rest on.** `node.h:1503-1523` documents each one as *"safe today"* — and each
reason is a statement about the team plane being quiet:

| ledger | its stated safety reason | what this arc does to that reason |
|---|---|---|
| `_per_origin_channel` | *"the planes rarely co-relay the same origin id"* | R1 makes the team plane a full peer of the static one |
| `_seen_origins` | aliases only if origin+dst+ctr **all** collide | Part A changes which ids collide, and T1 multiplies team flights |
| `_hash_query_seen` | *"safe ONLY by an UNWRITTEN role-exclusion invariant: no node today processes BOTH the static and the team H-flood plane"* | T2/T4 give one node live processing on both planes |

★ **And `s35` is precisely the configuration that falsifies them by construction** — §5 puts statics and team
members on one PHY with *a static node physically between two teammates*. "Planes rarely co-active on one link"
is not merely weakened there; it is the scenario's subject. ⇒ **T6/B is not speculative hardening. It is
required by the test this spec already commits to adding.**

**Shape.** Key by `(plane, id)`, not `id`. For the two `std::map` ledgers a plane bit folded into the existing
key is enough (`_seen_origins`' key is already a composed `uint64_t`). For the two fixed arrays, add a plane
field to the entry struct — ⚠ **that moves `sizeof(Node)`**, so D2 applies (assert updated with the arithmetic
spelled out, per-board RAM diff, and the full ten-env build since padding is the thing under test).

**Gate note.** Part A is a **behaviour change on the team plane only** and should re-anchor team scenarios while
leaving s18 and every static scenario byte-identical. Part B should be **fully inert** — it changes keys that do
not collide in any current scenario, so expect byte-identity and prove reachability by poison probe rather than
by a moved stream. ⚠ **A 0/N corpus result for Part B means "no scenario collides", NOT "the code is dead"** —
demand a same-site control that does move.

★ **Add to T6's acceptance, from the 2026-07-28 measurement (see §11):** a team-plane message must not
generate a **static-plane** frame. Today a homed member's team `-a` provokes **four static RREQ floods for the
home's id** from the acking teammate, and returns the ack only when static infrastructure happens to be
reachable — an R3 violation, not merely an untidy namespace. **Gate T6 on that**: assert zero static-plane
`r_tx`/`rreq_forward` attributable to a team-plane send, in both the homed and off-grid configurations. `s35`
covers only off-grid today (A2/A4 are algebra-only for exactly this reason), so **T6 owes a homed-member
scenario or an extension of `s35a`.**

**Closes:** §9-Q4, and the `[[meshroute-plane-separation]]` re-audit-by-plane item that has been open since
2026-07-10.

---

## 4. Isolation invariants (the contract T2–T5 must not break)

These hold today and must still hold after every slice. They are the acceptance criteria for `s35` (§5).

| I# | Invariant | Enforced at |
|---|---|---|
| I1 | A `team_scoped` F is never handled, learned from, or re-flooded by a non-member — including on a `!MR_FEAT_TEAM` build | `node_route_discovery.cpp:183-188`, `:314` |
| I2 | A team local id never enters `_rt`, `_id_bind`, `_link_bidi`, `_dest_seen_ms`, `_peer_liveness` | `next_is_local_id` (`node.h:141`) + the per-site guards T2 must preserve |
| I3 | A same-team beacon feeds only the team plane; every static-plane branch is `leaf_match`-gated | `node_beacon.cpp:602/624/676/684/719` |
| I4 | A team-scoped H is answered/relayed only by same-team members | 2026-07-15 spec §2 |
| I5 | Team RREQ dedup/rate ledgers are plane-private (no aliasing with static) | `node_route_discovery.cpp:28/38/54-56` |
| I6 | Team liveness/suspect state is plane-private | `node_routing.cpp:94/660/692` |
| I7 | **(new, T4)** A `team_sync` Q is answered only by same-team members; a static node ignores the opcode | T4 |
| I8 | **(new, T5)** Team bidi state is plane-private and never indexes `_link_bidi` | T5 |
| **I9** | ★ **(new, ACCEPTED 2026-07-28 — a BOUND, not a guarantee)** Team-plane admission is bounded by **team-LOCAL-ID match, NOT by team identity.** Neither RTS nor plain DATA carries a `team_id` (`frame_codec.h:289`/`:591`), so `for_team_data` (= `team_addr_for_us`) proves only *"this frame was addressed to our team-plane id"*. ⇒ on a **§18 numeric collision** — a foreign team whose local id equals ours, on the same PHY — we **CTS, ACK and deliver-or-relay** their frame. **Pre-existing**, not introduced by T6/T7: the shipped addressed-RTS arm (`node_mac_rx.cpp:91`) already admits ids on the identical predicate, and it relays (`:568`) on the same test. **Owner ruled ACCEPT + DOCUMENT** rather than close; closing requires a **wire discriminator**, and since the DATA flags byte is full (`0xFF`) that means a `wire_version` bump and its own slice.
★ **OWNER RATIONALE (2026-07-28), and it is the right frame:** *"we can't protect two teams using same PHY from leaking to each other — this is the same as if we'd have two static networks with same PHY, so we accept the risk."* The protocol carries **no network identity** on the MAC/data leg for anyone: two independent **static** meshes sharing a PHY and a `leaf_id` collide on `node_id` exactly the same way. ⇒ this is **not a team-plane weakness**; it is a property of the wire, and the team plane is held to the standard the static plane already sets.
★★ **REFINEMENT — teams are in fact STRICTLY BETTER protected than two static networks, and the boundary is exact** (verified against `frame_codec.h`): the **control / discovery plane IS team-identity-authenticated** — beacon (type-5 TLV), **H** (`H_FLAG_TEAM` + 4 B), **F** (byte-2 b6 + 4 B at offset 9), **J_DENY** (19-B form, bytes 15..18), **Q:TEAM_SYNC** (bytes 4..7), **M** (4 B BE iff `channel_flavor_team`) — so route discovery, hash-locate, DAD mediation, the full-table pull and channel posts all **verify team identity and drop a foreign team's frame**. The **MAC / data leg is NOT**: `rts_in` has no team field, DATA has none, and CTS/ACK ride the pending-RTS context. ⇒ **I9's residual is precisely the frames that carry no id — and those are exactly the frames where a static network has no protection either.** | accepted bound |

**★ s18 inertness is runtime-gated, not compiled out.** `MR_FEAT_TEAM` compiles out only on the gateway build; native runs s18 with it ON. Every team branch must be runtime-inert when `team_id == 0`. This is the standing rule from the 2026-07-15 spec §4 and it applies unchanged.

---

## 5. Test — `s35_cochannel_isolation_meshroute.json` (R7)

⚠ **READ I9 (§4) FIRST.** Every assertion below measures isolation **between the team plane and the STATIC plane**. That is a real and measured property — but it is **not** "the team plane is isolated", full stop: admission is by team-local-id match, so a foreign team colliding numerically is admitted. Owner-accepted 2026-07-28; do not let A2/A6 be quoted as more than they are.

s24 already places statics and team members on one PHY (`routing_sf: 8`, `allowed_data_sfs: [7,9,10]`, one global `radio` block) and asserts separation. `s35` makes co-channel isolation the *subject* rather than the setting, and adds the measurement s24 lacks: a **control run**.

**Topology.** Statics `S1–S4` in a chain; team members `T1–T4` interleaved so that (a) no two teammates are in direct range of each other beyond 1 hop, forcing multi-hop, and (b) a static node sits physically *between* two teammates, so a leaky implementation would visibly route through it. PHY deliberately identical for all eight: same freq, bw, `routing_sf`, `sf_list`. At least one teammate on a **different leaf nibble** (mirroring the bench's leaf 4 vs 7), since mixed-leaf teams are supported by design (`node_beacon.cpp:491-515`) and that is the configuration that actually shipped to metal.

**Assertions.**

| A# | Assertion |
|---|---|
| A1 | Every static node's `_rt` contains **zero** team local ids for the whole run |
| A2 | Every team member's `_rt_team` contains **zero** static node ids. ⚠ **Scope, per I9:** this measures isolation from the **STATIC** plane, which is what it asserts and all it asserts. Isolation from a **foreign team** is *not* measured here and is bounded by I9, not proven |
| A3 | No static node ever emits `rreq_forward` / `q_rx`-response / `h` forward for a team-scoped frame |
| A4 | Every hop of every delivered team DM has a team member as relay (walk the RTS `src` chain) |
| A5 | T1→T4 (never-heard, ≥3 hops) delivers, having started from `_team_peer` empty — the exact bench failure |
| A6 | Static delivery in `s35` is **identical** to a control run with all team nodes removed — isolation costs the static plane nothing beyond airtime. ⚠ **Scope, per I9:** "isolation" here means *from static ids*; A6 says nothing about a foreign team sharing our local-id space |
| A7 | Team delivery is unaffected by static traffic volume (second control: statics idle vs statics loaded) |
| A8 | `leaks == 0` |

A6/A7 are the point of the scenario: they turn "isolated" from a code-reading claim into a measured one. A6 in particular is the strongest available proof of R2 and is worth the extra control run.

**Also required:** a native test reproducing the *bench* case directly — three nodes, the middle one the only mutual neighbour, `send -t` from the leaf that has never heard the far leaf. That is a unit-level regression pin for T1 and should fail before T1 and pass after.

---

## 6. Wire and RAM impact

| | change |
|---|---|
| `wire_version` | **no bump** — held, T4 shipped without one |
| Q frame | new `team_sync` opcode + 4-byte `team_id` tail. ⚠ **The TAIL is additive; the OPCODE was not.** The field is **2 bits**, `team_sync` took the last free codepoint (`0`), and **the opcode space is now exhausted** — see the correction in §3/T4 |
| F / beacon / DATA | unchanged |
| RAM | +128 B (T5 team bidi table, u32-seconds form; 256 B if u64 ms); `sizeof(Node)` assert at `node.h:1676` updated with the arithmetic spelled out; per-board RAM diff required |
| `NodeConfig` | +1 byte `team_hop_cap` — place it to fill existing padding, not to open a new hole (see the `radio_freq_mhz` precedent in the `node.h:1676` comment) |

---

## 7. Gate strategy and byte-identity discipline

The standing gate (D1) applies in full to every slice: native (`pio test -e native`, then **run** `./.pio/build/native/program` — the wrapper misreports "0 test cases"), s18 md5 **exact** against the current `BASELINE.md` keystone, and every board env, sequentially. The keystone value is read from `simulation/BASELINE.md` at gate time and never hardcoded here — it re-anchors, and this document will rot.

Because §2.1 chose full parameterization, add the following per-slice discipline:

1. **T0 gates alone, on byte-identity, before any feature slice.** A refactor that cannot reproduce the keystone is a failed refactor, not a tolerable diff.
2. **Each rewritten guard carries a written equivalence argument** in the commit-ready diff: for the static plane, the new expression must reduce to the old one when `team_id == 0`. State it per expression, not per file.
3. **The mandatory mobile/team scenario set is not optional** — `s21`–`s30` (and now `s34`, `s35`) at 0 assertion failures, per `BASELINE.md` §2.
4. **Warnings are gate-blocking** — `-Wswitch` zero and no new warnings vs the pio baseline. ~~The new `q_opcode` value makes this concrete: every `switch` over `q_opcode` must handle it or the build fails, which is the desired outcome.~~
   ★★ **THAT SECOND SENTENCE IS FALSE, and it was this section's safety argument. Verified during T4:
   there are ZERO switches over `q_opcode` anywhere in the tree** — every dispatch is an `if`-chain, and the
   only switch that touches a Q opcode (`frame_trace.h:120`) takes a **`uint8_t`**, so `-Wswitch` is
   *structurally* blind to it. The gate would have caught nothing; the manual audit found it, and the
   `TEAM_SYNC` trace name had to be added by hand.
   ⇒ **The durable lesson, which generalises well beyond this spec: `-Werror=switch` protects an enum only
   where that enum reaches a `switch` AS THE ENUM TYPE.** An enum that is dispatched by `if`-chains, or that
   crosses an interface as an integer, is outside the gate's reach entirely. When a slice adds an enumerator,
   *grep for the switches first* and state whether the gate actually covers it — do not assume it does. This is
   the same sweep-scope failure the arc hit seven other ways (see the handover §5).

---

## 8. Build order

T0 → T1 → (T2 ∥ T4) → T3 → T5, with `s35` authored alongside T1 (it must fail before T1 lands, proving it measures the right thing). ⚠ **Superseded — the shipped order is `T0 → T1 → (T2 ∥ T4) → T6 → T3 → T5`; see the position note below.**

T1 alone fixes the reported bench failure. T4 is the highest value-per-byte of the remainder. T3 is comfort and operator visibility. T5 is the roaming-quality slice and the only one that touches `sizeof(Node)`.

> **★ POSITION AND REVISED ORDER AS OF 2026-07-28:**
> `T0 ✅ → T1 ✅ → (T2 ✅ ∥ T4 ✅) → T3 ❌ → T6 ❌ → T5 ❌`
>
> **T6 is new** (§3, owner ruling §11) and is inserted **before T5 deliberately**: T5 keys link state by id, so
> it must not be built on an ambiguous id space. `sizeof(Node)` is **220592** and unmoved so far; **both T6/B
> and T5 will change it**, so each owes the D2 treatment (assert arithmetic spelled out + per-board RAM diff +
> the full ten-env build, since per-board padding is the thing under test there).
>
> ⚠ **Order 2026-07-28: `T6 ✅ → T7 ✅ → T3 → T5`.** **T7 CLOSED the §0 bench failure** — it relaxed T2's `is_team_peer(origin)` learn
> fence, which T6 made decidable and which is **the last unclosed piece of the §0 bench failure**. Recommended
> next by the same logic that pulled T6 ahead of T3: a live defect outranks operator visibility. T3 still owns the hop-cap
> asymmetry T0 left open; it is simply no longer the head of the queue.
>
> ⚠ **`s35` did NOT ship alongside T1 as this section requires.** It was authored and proven to discriminate,
> then **lost** with an agent scratchpad before it was committed — the durable lesson being that agent scratch
> does not survive and repo files do. It is being re-authored now.
> ★ **Its discrimination proof must change shape.** "Must fail before T1 lands" is no longer executable, since
> T1 has landed. Faking a BEFORE tree would prove nothing. The replacement is a **poison probe**: author s35
> green on HEAD, then revert each slice's guard in turn and show s35 goes red with the matching signature.
> That is strictly stronger than the original recipe — it proves coverage of T1, T2 **and** T4 rather than T1
> alone.

---

## 9. Open questions for the reviewer

> **★ STATUS 2026-07-28 — four closed, two still open.**
>
> | Q | Subject | Status |
> |---|---|---|
> | Q1 | DATA-origin learning | ✅ **OVERRULED by §10.1** — the "no hop count" premise was false (`committed_hops`); T2 implemented it, no wire change, so the companion decision this question asks for is moot |
> | Q2 | team RREQ storm bound | ⚠ **OPEN — blocked on `s35`.** §10.4 ruled: add no team rate window, *measure* it first. That measurement was in the lost scenario |
> | Q3 | `_team_peer` age-out grace | ✅ **CLOSED by §10.2** — no grace window; the spec already contained the argument against it |
> | Q4 | is the I2 invariant list complete? | ⚠ **OPEN — §10.3 answered "no"**: `_seen_origins`, `_per_origin_channel`, `_hash_query_seen`, `_mediated_recent` are plane-blind. T1b closed the one wedge T1 opened; **the rest of that sweep is unaudited**, and it is a prerequisite for §11 |
> | Q5 | `s35` control runs | ✅ **CLOSED by §10.5** — ships as a pair |
> | Q6 | parameterization vs sibling branches | ✅ **CLOSED by §10.6**, and T0/T1/T2/T4 all landing byte-identical-or-attributable is the evidence the chosen approach held |

1. **§3/T2 — DATA-origin learning was considered and dropped** (see the exclusion note in T2). Raised here so the reviewer can overrule: the counter-argument is convergence speed, since a received DM is free evidence of reachability that T1 then re-derives with a flood. If the reviewer wants it, it needs a companion decision on how `hops` is obtained — most likely by adding a hop counter to the DATA frame, which **would** be a wire change and therefore its own slice.
2. **§3/T1 — storm bound.** ~3 floods per unknown id per 30 s is the static profile. With 10 members each addressing a departed teammate, the worst case is ~30 floods/30 s at SF6. Is a team-specific `rreq_rate_ok` window warranted, or is the existing 16-slot team ledger (`node.h:1439`) sufficient back-pressure?
3. **§3/T3 — `_team_peer` age-out.** `node_routing.cpp:489` clears the dispatch bit when the last team route expires. With T1 landed this is no longer fatal (discovery can re-find the peer), but should the bit instead persist for a grace window so `is_team_peer`-driven AUTO dispatch stays stable across a brief route gap?
4. **§4/I2 — is the invariant list complete?** It was assembled by grepping the plane-divergent guards. A second pair of eyes on whether any static array is still reachable from a team code path would be valuable; this is exactly the class the 2026-07-10 plane-separation audit was created to catch, and its re-audit-by-plane item is still open per `MEMORY.md`.
5. **§5 — `s35` control runs.** A6/A7 require running the scenario twice with different node sets. Confirm the harness supports that as one scenario file, or whether `s35` must ship as a pair (`s35a`/`s35b`) with the comparison done in the assertion layer.
6. **§2.1 — approach.** Recorded for completeness: I recommended sibling branches over full parameterization on blast-radius grounds and was overruled. If the reviewer shares that concern, T0 is the natural place to revisit it, since T0 is exactly the slice that commits to the choice.

---

## 10. Reviewer response (QA, 2026-07-27)

**Verdict: technically sound and buildable. T1 is cleared to proceed. Two substantive corrections (§10.1, §10.2), one gap in §4 (§10.3), and one concern with §2.1 that today's evidence sharpens (§10.6).** Every line reference in §0–§9 was re-verified against the tree; they are accurate, including the `sizeof(Node)` assert at `node.h:1676` (it shifted twice on 2026-07-27 and the spec is current). That is unusual and worth saying.

**T1's single assumption — which §3/T1 explicitly asks a reviewer to confirm independently — HOLDS, and is stronger than stated.** `node_route_discovery.cpp:183` drops a `team_scoped` F with the `return` placed **outside** the `#if MR_FEAT_TEAM`, so even a `MR_FEAT_TEAM 0` gateway build drops it rather than falling through into the static F body; `handle_f_team` (`:314`) then requires `_cfg.is_mobile && same_team(f.team_id) && me != 0`. Static, wrong-team and not-yet-DAD'd nodes all bail before touching any state. A team RREQ cannot reach the static plane. **T1 is safe to build.** The core diagnosis is likewise exact: `node.cpp:1082` returns `err_no_binding` with ctr 0, matching the bench's `err ctr=0 depth=0`.

### 10.1 ★ Q1 OVERRULED — the DATA frame DOES carry a usable hop count

§3/T2's exclusion note rests on *"The DATA frame carries no hop count — `d_in`/`d_out` (`frame_codec.h`) have no `hops` field."* **Both halves are wrong.** The structs are named **`data_in`/`data_out`**, and `frame_codec.h:595` reads:

```cpp
uint8_t  next, dst, hops_remaining, committed_hops, prev_fwd_rt_hops;
```

`committed_hops` is a **from-origin hop count**: `node_mac_rx.cpp:615-616` computes `hb_new_committed = (d.committed_hops >= 7) ? 7 : d.committed_hops + 1`, i.e. it is incremented at every hop. It is on the wire today, in every DATA frame, already parsed.

⇒ **the second rejection reason also falls.** With a real metric in hand a receiver can install a **real** route — `_rt_team[origin] = { next: <prev-hop>, hops: committed_hops + 1 }` — which sets the `_team_peer` bit *alongside* an `_rt_team` entry and therefore **preserves** the `node_beacon.cpp:73` invariant rather than decoupling it. The decoupling objection applied only to bit-without-route, which a real metric makes unnecessary.

**Recommendation: reinstate DATA-origin learning as a candidate, most naturally inside T2** (it is a learn site like the other seven). No wire change is required at all — and per the owner's standing ruling, **`wire_version` is not to be bumped and MeshRoute is not yet deployed**, so even a hypothetical wire change would not have been the barrier the note implies.

**Two honest caveats, neither disqualifying.** (a) `committed_hops` **saturates at 7** while R4 sets the team ceiling at **8** — so the deepest legal path yields a metric of 7, understating by one. Bounded and knowable; either accept it or treat 7 as "≥7" and let `route_strictly_better` prefer a discovered route. (b) It is an unauthenticated wire field, so a buggy or hostile node can understate its distance and attract traffic — but that is equally true of the beacon `hops` the static DV already trusts, so it is not a new exposure.

**T1 remains the primary fix** and this does not change the build order: T1 gives a *verified* path in ~1 s, origin-learning gives a *free* one with no flood. They compose.

### 10.2 Q3 — no grace window; the spec already contains the argument against it

§9/Q3 asks whether the `_team_peer` bit should persist past route age-out. **No** — and §3/T2 supplies the reason: it rejects DATA-origin learning precisely *because* setting the bit without a route decouples the `node_beacon.cpp:73` invariant. A grace window introduces exactly that decoupling from the other direction. Keep them coupled. With T1 landed, re-discovery is ~1 s, so the window buys very little; and with §10.1 landed a received DM re-installs the route for free.

### 10.3 ★ Q4 — §4's invariant list is INCOMPLETE: the plane-blind ledgers

I2 covers routing/liveness arrays only. It omits a class the code documents against itself at **`node.h:1495`**: *"`_seen_origins` … the PLAINTEXT flight key (`origin<<24|dst<<16|ctr`) has **NO plane bit**"* — so a **team local id and a static node id alias in that key**. The 2026-07-27 channel-purge audit (BASELINE 27zc) independently enumerated four such plane-blind ledgers: **`_seen_origins`, `_per_origin_channel`, `_hash_query_seen`, `_mediated_recent`**.

**Why this matters for THIS spec specifically.** The failure direction is *suppress*, not leak, so **R2 survives** — no static node spends state on team traffic. But **T2/T3/T4 multiply team-plane traffic** (2 → 7 learn sites, a census in every steady beacon, on-demand full-table pulls), which multiplies the aliasing rate. ⇒ **A6 — the spec's own strongest proof of R2 — can fail for a reason that is not an isolation bug.**

**Required:** add these four to §4 as a documented **non-leak cross-plane interference**, and give A6 either a stated tolerance or a diagnostic that distinguishes *dedup aliasing* from *state leakage*. A6 is the right assertion; it just needs to be able to tell the two apart, or a red A6 will be misread as a broken isolation model.

### 10.4 Q2 — do not add a team-specific rate window in T1; measure it in `s35`

The 16-slot team ledger is **dedup, not back-pressure**, so it is not sufficient back-pressure on its own — the honest answer to the question as posed. But the bound is self-limiting: 3 floods per unknown id over 30 s, then a loud `send_failed{no_route}`. Rather than adding speculative complexity to T1, **add an `rreq_tx` ceiling assertion to `s35` at the R4 worst case (10 members, SF6)** and let the measurement decide. If it breaches, the window becomes its own small slice with evidence behind it.

### 10.5 Q5 — `s35` must ship as a PAIR

The `expect` DSL has **no cross-run comparison**: the available types are `cmd_reply_contains`, `cmd_reply_not_contains`, `event_count`, `event_count_min`, `script_emit_contains`, `script_emit_not_contains`, `tx_airtime_between` (`orchestrator/test_runner/ExpectRunner.cpp`). None compares two runs. A6/A7 therefore cannot live in the assertion layer of one file. Ship `s35a`/`s35b`, or hand the comparison to QA as a scripted diff — that is exactly how the s31/s32/s33/s34 control comparisons were done on 2026-07-26/27.

### 10.6 ★ Q6 — I share the author's original concern, and 2026-07-27 gave it evidence

§2.1 records that the author recommended sibling branches and was overruled; §7's compensating discipline is *"demonstrate byte-identity per slice."*

**That discipline demonstrably failed twice on 2026-07-27, in this exact shape.** In the `§clean-team-channel` slice, `purge_team_channel_state()` was a `{}` stub under `#if MR_FEAT_TEAM`; a proposed change would have routed the **gateway** build's channel wipe through that stub, silently disabling it — while `leave` *is* dispatched on the gateway build. **Byte-identity held perfectly**, because the corpus cannot reach `src/`-only callers. The same slice's reprovision axis then measured **0/31** corpus coverage with a maximally-destructive probe.

⇒ **byte-identity is structurally blind to per-build-profile divergence**, which is precisely the risk full parameterization of hot static paths introduces. §7 should say so explicitly and add: *for each rewritten guard, state the reduction for `team_id == 0` **and** name the build profiles the expression compiles differently under* (`MR_FEAT_TEAM 0` on the three `gateway_*` envs; `MR_N_LAYERS >= 2` gating). A per-slice `-Werror=switch`-style structural check is worth more here than another byte-identity run.

The owner has ruled on §2.1 and this does not reopen it — but T0 is the commit point, and if it produces guards whose static reduction cannot be stated in one line per expression, that is the signal to revisit.

### 10.7 Smaller notes

- **§6 / `NodeConfig` +1 byte `team_hop_cap`:** the `radio_freq_mhz` precedent is exactly right — that member was placed between `dv_hop_cap` and the existing `double duty_cycle` to land in existing alignment padding, costing **+8** where a naive placement cost +16. Measure `sizeof(NodeConfig)` before and after, not just `sizeof(Node)`.
- **§3/T5 — take the u32-seconds form (128 B).** `bidi_confirm_ttl_ms` is 1 200 000 ms; second granularity is ample and the halving is real on the nRF52840.
- **§5 — add the mixed-leaf case as an assertion, not just topology.** The spec already notes at least one teammate on a different leaf nibble mirrors what shipped to metal; make that explicit in the assertion table so a future edit cannot quietly normalise it away.
- **§7/3 — the mandatory scenario set is now `s21`–`s34`** (s31–s34 landed 2026-07-26/27), and the corpus is **31** scenarios. Read the count and the anchors from `BASELINE.md` at gate time, as §7 already says.

---

## 11. ✅ OWNER RULING (2026-07-28) — a TEAM-plane send stamps `team_local_id()`

> ## ★ RULED: **"Team id, bundled with the ledger fix."**
>
> The owner took QA's recommendation in full, **including its condition** — the change ships as ONE slice
> together with plane-keying `_seen_origins`, `_per_origin_channel`, `_hash_query_seen` and `_mediated_recent`,
> not as the one-line edit it resembles. **This is now `T6` (§3), ordered after T3 and before T5.**
>
> Consequences to carry out there: `node_mac.cpp:70`'s comment **becomes true** rather than being corrected;
> anti-spam accountability on the team plane moves to the team id, deliberately; and §9-Q4 closes.
>
> ### ★★ MEASURED 2026-07-28 — the premise holds, the mechanism is worse than predicted
>
> QA predicted the reverse ack would fail to route **on the team plane**. It does not route on the team plane
> at all: the acking teammate addresses `dst = <home_id>` on the **STATIC** plane. Consequences, both measured:
>
> - **With** a static route to the home, the ack **RETURNS** (1.77 s — the home last-miles it to its hosted
>   mobile). ⇒ ★ **a `-a` team DM from a homed member requires static infrastructure for its ack. That is a
>   plane crossing, and it violates R3** ("team-internal routing only — a hiking-group mesh, no static
>   infrastructure in the path").
> - **Without** one — the bench's own leaf-4-vs-leaf-7 shape — it **FAILS**, and noisily: `r_tx {dst:<home>,
>   reason:"no_route"}` at ttl 1 then ttl 16 ×3, i.e. **four STATIC-plane RREQ floods for the home's id,
>   generated by a TEAM message's ack**, then `send_deferred_giveup`, then `send_failed{e2e_ack_timeout}` at
>   +60 s on the originator.
>
> ⇒ **The defect is real but configuration-dependent, and the failing configuration is the bench's.** The R3
> violation is a *stronger* argument for T6 than the routing argument the ruling was made on — so T6 should be
> gated on R3 compliance (no static-plane frame generated by a team-plane message), not only on namespace
> tidiness. Evidence in the `BASELINE.md` **SCEN** note.

The original write-up of the question follows, as the record of what was ruled on.

**Raised 2026-07-28 by QA, after T1. This blocks a clean T5 and it is not mine to decide.**

### The exact code state

`node.h:819-822` — there is **no team-plane exception**:

```cpp
void stamp_origin(TxItem& item) const {
    const bool mob = _cfg.is_mobile && _my_mobile_reg.active;
    item.origin = mob ? _my_mobile_reg.home_id : _node_id;
    item.mobile_src = mob;
```

⇒ on the **team plane** the origin namespace depends on how the sender happens to be attached:

| sender | `origin` it stamps | routable by a teammate on `_rt_team`? |
|---|---|---|
| **off-grid** member (`_my_mobile_reg.active == false`) | `_node_id`, which **is** its `team_local_id` | yes |
| **homed** member (registered to a static host) | `_my_mobile_reg.home_id` — a **static** id | **no** |

The original rationale is sound and predates the team plane: a registered mobile *bills its home* because a home id is an accountable **global** id, and self-marking keeps the mobile's local id out of the global `_rt`.

### Why it needs a ruling now

1. **A load-bearing comment asserts the opposite.** `node_mac.cpp:70` permits `-a` on the team plane *because* "the origin it stamps is the team_local_id, which every teammate CAN route." For a homed member that is false, so the justification for the T1 gate rests on the half that does not hold. **This comment must be fixed either way** (V1) — the ruling decides in which direction.
2. **It is a policy question, not a code question.** Stamping `team_local_id` on the team plane moves **anti-spam accountability** from an accountable global id to a team-scoped one, and changes a `_seen_origins` dedup key.
3. **It has a hard prerequisite.** `_seen_origins` is one of the four **plane-blind ledgers** in §10.3 / §9-Q4. Team local ids and static node ids share the `1..254` space, so making team origins be team ids **creates an aliasing surface in a ledger that cannot currently tell the planes apart.** Changing `stamp_origin` before plane-keying those ledgers would trade a routing bug for a silent-suppression bug.

### QA recommendation

**Stamp `team_local_id()` on `Plane::TEAM`, but only as one slice together with plane-keying the shared ledgers** — not as the one-line change it looks like. Rationale: the team plane should have exactly one origin namespace, and the homed/off-grid split above is an accident of attachment, not a design. ⚠ **Do not treat this recommendation as measured.** Whether a homed member's team `-a` ack actually fails to return has been *reasoned* from the routing tables, not observed. The honest next step is to **measure it in `s35`** (which already needs a homed and an off-grid teammate for §5) and rule on evidence.

**Alternative, if accountability outweighs it:** keep `home_id`, correct `node_mac.cpp:70` to state the real limitation, and document that a homed member's team-plane `-a` may not receive its ack.

---

## 12. ★★ AMENDMENT TO §10.3 — the plane-blind audit was a *learn-site* audit, and that is a blind spot

**Found 2026-07-28 by T7's before-arm, not by any audit.** §10.3 listed four plane-blind ledgers, assembled by
grepping the **learn sites** and the plane-divergent guards. T6 acted on that list. It was the right list for the
question asked — and the question was too narrow.

`s38`'s before-arm shows a **static witness node installing `rt_update{dest:213, hops:1, next:213}`** — a team
local id in a static node's `_rt`, which is **invariant I2 breached**. No learn site is at fault; every one of them
behaves correctly. The leak enters through the **plane fallback in routing**:

1. `rt_find(174, AUTO)` finds no `_team_peer` bit for the never-heard teammate, so it falls through to `_rt`;
2. the resulting RREQ is therefore a **static-plane** flood carrying `origin = 213` (a team local id);
3. a static witness **reverse-path-learns** it as an entirely legitimate static route.

⇒ **The generalisable lesson: an audit scoped to "where do we write?" cannot see a leak whose cause is "which
table did we read?".** A plane can be violated by a *fallback* as easily as by a write, and the fallback looks
correct at every individual site. This is the eighth instance of the sweep-scope meta-bug in this arc and the
first where the scope error was in the **question**, not the grep.

**Consequences:**
- `s38` assert 12 pins it. T7 removes the cause by making the `_team_peer` bit present, so the fallback is not
  taken — but the fallback itself is unchanged and remains a live mechanism.
- **Owed: a plane audit of the READ paths**, not just the writes — every `rt_find(..., AUTO)` and every place a
  plane-typed lookup can silently degrade to the static table. That is a candidate slice, and it is the natural
  companion to the `[[meshroute-plane-separation]]` re-audit item that §9-Q4 closed only for writes.
