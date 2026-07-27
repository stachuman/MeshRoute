# Team-plane routing: full static parity under co-channel isolation — design spec

*2026-07-27. Follow-on to `2026-07-15-team-plane-routing-parity-design.md` (which delivered team hash-resolution + team F discovery + team liveness, and shipped s24/s25). That spec built the team routing **machinery**. This one fixes the fact that almost nothing **feeds** it.*

*Status: DRAFT for dedicated review. Not implemented. All line references verified against the working tree at the time of writing (branch `main`, tip `aac7e61` + uncommitted CR work).*

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

Plus the case the bench exposed directly: **learn the DATA *origin*, not only the prev-hop.** A team DM delivered over N hops proves the origin is reachable at that cost; today nothing records it. Install it as a team route with `hops` taken from the DM's own hop accounting, at a score derived from the *last* hop's SNR only — see the open question in §9.

**Isolation constraint (R2), non-negotiable:** every one of these writes must land in `_rt_team` / `_team_peer` / `_team_liveness` and must never touch `_rt`, `_id_bind`, `_link_bidi`, `_dest_seen_ms` or `_peer_liveness`. The existing `next_is_local_id` guards exist precisely to enforce that; T2 must add a team destination for the excluded traffic, **not** relax the guard that keeps it out of static state. A reviewer should read every T2 hunk with that single question.

### T3 — team DV census

`node_beacon.cpp:423` gates the 1-hop census force-inject on `dirty_only && _cfg.n_layers != 2 && !_cfg.is_mobile`. Allow it when `team_emit` is true (`:388`), packing from `_rt_team` (`src_rt` already selects the plane at `:389`). This is what makes a stable team route re-advertise instead of being announced once and cleared at `:467`.

Bounded by the existing `max_entries` (`:379`) and `heard_set_census_min_headroom` (`:434`). At 3–10 members the cost is ~4 bytes per teammate on a beacon that already flies every 5 minutes.

`bidi_census_full` / `heard_set_complete` must be set on team beacons too — T5 depends on it (absence is only authoritative on a beacon that actually ran the census, per the M1 note at `:420-422`).

### T4 — team REQ_SYNC (on-demand full-table pull)

- **`node_query.cpp:52`** — `if (_cfg.is_mobile) return;` currently blocks all mobiles. The comment's reasoning is sound *for the static plane* (a mobile's local id must not leak into every static `_rt`) but does not apply to a team-scoped pull. Replace with: static-plane REQ_SYNC stays forbidden for mobiles; team-scoped REQ_SYNC is allowed for a team member with an adopted `team_local_id`.
- **Wire:** add a `q_opcode` value (`team_sync`) carrying a 4-byte `team_id` tail. `pack_q` is already variable-length by opcode ("4, or 5+4N for pull", `frame_codec.h:349`), so this adds a shape rather than changing one. A node that does not know the opcode ignores the frame. `q_in`/`q_out` (`:337-348`) gain `uint32_t team_id`.
- **Response:** `schedule_sync_response` (`node_query.cpp:217`) answers a `team_sync` only if `same_team(q.team_id)`, and the resulting beacon is the existing team-tagged `"sync"` beacon — full `_rt_team` table via the Phase-2 rotation.
- **Trigger:** `node_mac.cpp:731`'s originator antidote becomes plane-aware, so a team originator with no route fires a team REQ_SYNC alongside the RREQ.

**No `wire_version` bump** (C4). The static Q wire is untouched; the new opcode is additive on a frame that is already opcode-variable.

### T5 — team bidi plane

The static bidi plane is `_link_bidi[256]` + `_link_bidi_confirmed_ms[256]` = **2304 B** (`node.h:1517-1518`). A mirror at that size is not acceptable on the nRF52840. Add a right-sized, self-slotted table in the style of `_team_liveness[cap_team_liveness=16]` (`protocol_constants.h:204`): 16 entries of `{uint8_t team_local_id; uint8_t state; uint64_t confirmed_ms;}` ≈ **160 B**.

- Fed by team CTS/ACK confirmations (T2) and by the heard-set scan currently skipped for team beacons at `node_beacon.cpp:868`.
- Enable the penalties currently zeroed for team: `bidi_penalty_q4` (`node_routing.cpp:161`), the freshness-viability bypass (`:192-193`), and `candidate_degraded`'s wire-only shortcut (`:361`).

This is what would have let 213 and 174 recognise that their direct link is one-way and commit to the 234 path deliberately rather than by accident.

**D2 obligation:** `node.h:1676` carries `static_assert(sizeof(Node) == 220592, …)`. T5 changes it. The assert must be updated **with the arithmetic spelled out in the comment** (matching the existing style), plus a per-board RAM diff — native alignment hides board padding.

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

**★ s18 inertness is runtime-gated, not compiled out.** `MR_FEAT_TEAM` compiles out only on the gateway build; native runs s18 with it ON. Every team branch must be runtime-inert when `team_id == 0`. This is the standing rule from the 2026-07-15 spec §4 and it applies unchanged.

---

## 5. Test — `s35_cochannel_isolation_meshroute.json` (R7)

s24 already places statics and team members on one PHY (`routing_sf: 8`, `allowed_data_sfs: [7,9,10]`, one global `radio` block) and asserts separation. `s35` makes co-channel isolation the *subject* rather than the setting, and adds the measurement s24 lacks: a **control run**.

**Topology.** Statics `S1–S4` in a chain; team members `T1–T4` interleaved so that (a) no two teammates are in direct range of each other beyond 1 hop, forcing multi-hop, and (b) a static node sits physically *between* two teammates, so a leaky implementation would visibly route through it. PHY deliberately identical for all eight: same freq, bw, `routing_sf`, `sf_list`. At least one teammate on a **different leaf nibble** (mirroring the bench's leaf 4 vs 7), since mixed-leaf teams are supported by design (`node_beacon.cpp:491-515`) and that is the configuration that actually shipped to metal.

**Assertions.**

| A# | Assertion |
|---|---|
| A1 | Every static node's `_rt` contains **zero** team local ids for the whole run |
| A2 | Every team member's `_rt_team` contains **zero** static node ids |
| A3 | No static node ever emits `rreq_forward` / `q_rx`-response / `h` forward for a team-scoped frame |
| A4 | Every hop of every delivered team DM has a team member as relay (walk the RTS `src` chain) |
| A5 | T1→T4 (never-heard, ≥3 hops) delivers, having started from `_team_peer` empty — the exact bench failure |
| A6 | Static delivery in `s35` is **identical** to a control run with all team nodes removed — isolation costs the static plane nothing beyond airtime |
| A7 | Team delivery is unaffected by static traffic volume (second control: statics idle vs statics loaded) |
| A8 | `leaks == 0` |

A6/A7 are the point of the scenario: they turn "isolated" from a code-reading claim into a measured one. A6 in particular is the strongest available proof of R2 and is worth the extra control run.

**Also required:** a native test reproducing the *bench* case directly — three nodes, the middle one the only mutual neighbour, `send -t` from the leaf that has never heard the far leaf. That is a unit-level regression pin for T1 and should fail before T1 and pass after.

---

## 6. Wire and RAM impact

| | change |
|---|---|
| `wire_version` | **no bump** |
| Q frame | new `team_sync` opcode + 4-byte `team_id` tail (additive shape on an already opcode-variable frame) |
| F / beacon / DATA | unchanged |
| RAM | +~160 B (T5 team bidi table); `sizeof(Node)` assert at `node.h:1676` updated; per-board RAM diff required |
| `NodeConfig` | +1 byte `team_hop_cap` — place it to fill existing padding, not to open a new hole (see the `radio_freq_mhz` precedent in the `node.h:1676` comment) |

---

## 7. Gate strategy and byte-identity discipline

The standing gate (D1) applies in full to every slice: native (`pio test -e native`, then **run** `./.pio/build/native/program` — the wrapper misreports "0 test cases"), s18 md5 **exact** against the current `BASELINE.md` keystone, and every board env, sequentially. The keystone value is read from `simulation/BASELINE.md` at gate time and never hardcoded here — it re-anchors, and this document will rot.

Because §2.1 chose full parameterization, add the following per-slice discipline:

1. **T0 gates alone, on byte-identity, before any feature slice.** A refactor that cannot reproduce the keystone is a failed refactor, not a tolerable diff.
2. **Each rewritten guard carries a written equivalence argument** in the commit-ready diff: for the static plane, the new expression must reduce to the old one when `team_id == 0`. State it per expression, not per file.
3. **The mandatory mobile/team scenario set is not optional** — `s21`–`s30` (and now `s34`, `s35`) at 0 assertion failures, per `BASELINE.md` §2.
4. **Warnings are gate-blocking** — `-Wswitch` zero and no new warnings vs the pio baseline. The new `q_opcode` value makes this concrete: every `switch` over `q_opcode` must handle it or the build fails, which is the desired outcome.

---

## 8. Build order

T0 → T1 → (T2 ∥ T4) → T3 → T5, with `s35` authored alongside T1 (it must fail before T1 lands, proving it measures the right thing).

T1 alone fixes the reported bench failure. T4 is the highest value-per-byte of the remainder. T3 is comfort and operator visibility. T5 is the roaming-quality slice and the only one that touches `sizeof(Node)`.

---

## 9. Open questions for the reviewer

1. **§3/T2 — DATA-origin route scoring.** Installing a route to the *origin* of a received multi-hop DM is the direct fix for the bench smoking gun, but the score is not well defined: we observe only the final hop's SNR, not the path's worst link. Options: (a) install at `hops = N` with the last hop's SNR, accepting an optimistic score that DV/RREP will correct; (b) install at a deliberately pessimistic floor score so it is used only when nothing better exists; (c) mark `_team_peer` but install no route, letting T1's discovery find the real path. My inclination is (c) — it fixes reachability without inventing a quality number — but it is the weakest of the three for convergence speed. **Needs a decision before T2.**
2. **§3/T1 — storm bound.** ~3 floods per unknown id per 30 s is the static profile. With 10 members each addressing a departed teammate, the worst case is ~30 floods/30 s at SF6. Is a team-specific `rreq_rate_ok` window warranted, or is the existing 16-slot team ledger (`node.h:1439`) sufficient back-pressure?
3. **§3/T3 — `_team_peer` age-out.** `node_routing.cpp:489` clears the dispatch bit when the last team route expires. With T1 landed this is no longer fatal (discovery can re-find the peer), but should the bit instead persist for a grace window so `is_team_peer`-driven AUTO dispatch stays stable across a brief route gap?
4. **§4/I2 — is the invariant list complete?** It was assembled by grepping the plane-divergent guards. A second pair of eyes on whether any static array is still reachable from a team code path would be valuable; this is exactly the class the 2026-07-10 plane-separation audit was created to catch, and its re-audit-by-plane item is still open per `MEMORY.md`.
5. **§5 — `s35` control runs.** A6/A7 require running the scenario twice with different node sets. Confirm the harness supports that as one scenario file, or whether `s35` must ship as a pair (`s35a`/`s35b`) with the comparison done in the assertion layer.
6. **§2.1 — approach.** Recorded for completeness: I recommended sibling branches over full parameterization on blast-radius grounds and was overruled. If the reviewer shares that concern, T0 is the natural place to revisit it, since T0 is exactly the slice that commits to the choice.
