<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Channel flood — honest seeding (port-fix) + origin re-offer (single/contended-link reliability)

**Status:** coder instruction. The user commits + flashes; I gate. No wire change; TX-scheduling/coverage logic only.

> ## ★ FINAL DESIGN (2026-06-26, post-experiment) — supersedes Parts 1+2 below
> The Lua-sim experiment (24-seed A/B sweep on the asymmetric `topo_3d7377`, meshroute engine) **reversed Part 1** and refined Part 2's confirmation signal. The shipped design is:
> - **Part 1 (honest empty seed): DROPPED.** Keep the existing **`{self + hops==1 neighbours}` frugal seed** — now a *documented deliberate divergence* from the Lua's empty seed (frugality: a relay skips the origin's own neighbours, which got the post directly). The "honest" seed REGRESSED coverage in the sweep (it removes the neighbour-skip → more rebroadcast contention → collisions drop deliveries: 247 mean reach **4.04 → 3.17**, *coverage* not just airtime) with **no** orphan benefit. (It also can't be the literal empty `{}` anyway — `tx_m_broadcast_rts`'s fail-loud zero-bitmap guard refuses an all-zero flood.) The parked **bidirectional-seed** spec drops to a *deferred latency optimisation* (it would deliver asymmetric nodes via the fast flood instead of the slower pull) — revisit only if pull-latency proves to matter.
> - **Part 2 (origin re-offer): KEPT, and is the WHOLE fix** — with one adjustment: its confirmation is a **dedicated "did I overhear a RELAY of my message?" boolean** (`channel_reoffer_confirm`, set when the origin overhears another node's flood RTS-M / DATA-M / M-frame of its post), **NOT** the `seen_by` set. This decouples it from the seed and from digest/pull marks, so the re-offer stops *only* on real relay activity (and keeps trying until then, up to the cap). The re-flood reuses the same frugal seed.
> - **K revert (`channel_dirty_max_advertisements` 16→3): KEPT** (Lua-parity + frugal; the re-offer supersedes the inflated horizon backstop).
> - **Result:** asymmetric 247-orphan **8/24 → 0/24 seeds**, mean reach **4.04 → 5.29**; nearly every origin up (79: 4.08→5.93). s15 symmetric no-regression: **223 vs 224 deliveries** (delivery-neutral), re-offers bounded. native **524/524**. *The boolean confirmation is what closed the orphan — with `seen_by`-confirm it only reached 6/24.*
> - **Status: IMPLEMENTED + sim-gated, uncommitted.** Sites: `node_channel.cpp` (frugal seed kept · `channel_reoffer_register/fire/confirm` · confirm hooks at the two overhear sites) · `node.h` (`ChannelReofferPending`, ring `[70..73]`, decls) · `node.cpp` (on_timer dispatch) · `protocol_constants.h` (constants + K=3) · `test_node_channel.cpp` (+3 re-offer tests).
>
> The original two-part text below is kept for the record (Part 1 is historical).

Two parts (ORIGINAL — Part 1 superseded): **(1) a PORT-CORRECTION** (the C++ diverged from the Lua's flood-origination seed) and **(2) a deliberate DIVERGENCE** (an origin re-offer the Lua doesn't have).

## Why
Oracle run 3d7377: channel origin **247 reaches 0/7**, and the sim reproduces it **with perfect RF** → it's logic, not RF. The full sim trace (`/tmp/sim_3d7377.ndjson`) shows: 247 floods (RTS @t=91071, broadcast DATA @t=91256), but **54 — the only node that hears 247 — was mid-DM-exchange and missed it.** The flood is fire-once (no retry); the repair-pull can't help because 54 is *also* too contended to hear 247's beacon digest. A node whose only link is the mesh's busiest is cut off at one chokepoint.

## Part 1 — Honest seeding (PORT-CORRECTION, do this regardless)
**The bug:** `node_channel.cpp:283` seeds the flood coverage bitmap with `{me + my hops==1 neighbours}` ("nodes I hear"). **The Lua seeds `seen_by = {}`** (empty — nobody is covered until confirmed; `dv_dual_sf.lua` channel-origination block, ~L12134). The C++ seed is the asymmetric-coverage lie: it marks neighbours covered who never received it (and, for asymmetric-RX, *can't*). Restore the Lua behaviour:
- At flood origination, seed `seen_by = {}` (empty). Drop the `hops==1 neighbours` loop; drop the self-bit too unless the Lua sets it (it doesn't — match `{}`).
- Coverage then accrues **only** via `channel_mark_seen_by` as actual relays are overheard (unchanged).
- Effect: relays no longer skip "already covered" neighbours that were never reached → general coverage improves, and the origin's view of its own propagation becomes **truthful** (the prerequisite for Part 2). 
- This is a code-vs-code Lua divergence — fix it directly (Lua is source of truth). Gate via the **Lua-parity harness**: the fixed C++ s18 should move toward the lua-engine s18, not away.

## Part 2 — Origin re-offer (DIVERGENCE — the Lua relies on the pull, which fails here)
The origin owns its message's propagation until it sees proof it got out.

**Confirmation signal:** with the honest seed (Part 1), `seen_by` starts empty; the **first overheard relay** of the message sets a bit → non-empty `seen_by` = "it propagated" = confirmed. (No new signal needed — Part 1 makes the existing `seen_by` the confirmation.)

**Mechanism** (a small per-origin-message re-offer table, sized like `channel_pull_pending`):
- On flood origination, register the message in a `channel_reoffer_pending[]` slot with `retries_left = channel_reoffer_max_retries` and arm a timer at `delay + jitter`.
- On timer fire: if `seen_by` is still **empty** (no relay overheard) AND `retries_left > 0` → **re-flood** the cached body (`node.h:735` already caches it "for the re-flood DATA-M"), LBT-gated; `--retries_left`; re-arm at `delay + jitter`. Else (confirmed OR exhausted) → free the slot, cancel the timer.
- Re-floods are deduped by the existing `originator_retry_dedup_ms` (anti-spam) so receivers don't double-inbox; they DO re-broadcast for coverage.
- Free the slot early the moment `seen_by` goes non-empty (a relay was heard) — the common, well-connected case re-offers **zero** times.

**Timer id:** allocate a `kChannelReofferTimerId` ring (slot = id − base), sized to the re-offer table. The 1..63 dense block is full and 64..79 is the gateway band — either extend `TimerWheel::kCap` + reserve a new band, or reuse the flood-rebcast ring's pattern. Coder picks; keep `after()`'s `id < kCap` bound intact (the canary era taught us the wheel is exonerated only because it bounds).

## The constants (per the user — named, tunable)
In `protocol_constants.h`:
```
inline constexpr uint8_t  channel_reoffer_max_retries = 3;      // cap — bounds the airtime cost
inline constexpr uint32_t channel_reoffer_delay_ms    = 10000;  // base cadence (>= originator_retry_dedup_ms so re-floods dedup)
inline constexpr uint32_t channel_reoffer_jitter_ms   = 2000;   // +/- spread so multiple origins don't re-offer in lockstep
```
Jitter applied as `delay + rand(0, jitter)` (or `delay ± jitter/2`) via the existing retry-jitter PRNG path (NOT `Math.random` — the deterministic `std::mt19937`, for sim parity).

## Also revert: `channel_dirty_max_advertisements` 16 → 3 (the Lua value)
The C++ raised K 3→16 as the holder-aware-retire **backstop** (advertise an orphan longer before giving up). Metal (run 3b9abc) proved it insufficient — the permanent-orphan case is "**no holder exists at all**" (247's flood reached 0 nodes), so K is irrelevant. The origin re-offer is the correct lever (it re-injects the message so a holder *forms*) and **supersedes** the inflated K. Revert to the Lua's 3; it also isolates the re-offer's effect in the seed sweep below.

## Experimental design (READ THIS — it splits the gate)
**The existing Lua scenarios are SYMMETRIC — no asymmetric links — so they CANNOT reproduce the orphaning.** Two consequences:
- On symmetric links the honest seed still *changes behaviour*: the empty seed means relays no longer skip the origin's direct neighbours (which got it from the origin directly), so expect **more redundant re-broadcasting = more airtime for the same coverage**. This is exactly the Lua's behaviour (it seeds empty) — parity, not a bug — **but it must be bounded.**
- So the existing scenarios' only job is **NO-REGRESSION**, and the orphan-recovery must come from a **new asymmetric scenario** we build.

## Tests / gate
- **Native:** the re-offer table + the seed change are pure logic — unit-test: empty seed → `seen_by` starts 0; a re-offer fires when `seen_by` empty + retries remain; stops on first mark or at the cap; jitter is deterministic.
- **★ NO-REGRESSION SEED SWEEP (the gate on existing symmetric scenarios):** run a **≥24-seed sweep** (the s15 precedent) on the baseline scenarios (s15/s12, lua + meshroute engines). Pass = **channel coverage ≥ baseline AND flood/digest airtime ≤ baseline + a ceiling, ACROSS THE DISTRIBUTION** (worst-seed, not just mean — a single seed hides regressions). Part 2 (re-offer) must fire **~0 times** here (everything confirms fast) — confirm no spurious re-offers inflate airtime. Gate vs the **lua-engine** (the seed is now the Lua's), NOT the old C++ keystone md5 (which WILL change — expected).
- **★ ASYMMETRIC ORPHAN-RECOVERY (a NEW scenario — the actual purpose):** the existing scenarios can't test this. Build an asymmetric scenario — **a node heard by only one busy node** (mirror 3d7377), as a synthetic Lua scenario (with one-way links) OR the `topo_3d7377` topology. **Metric, lexicographic:** (1) zero permanent orphans — the orphan's final coverage goes 0 → reached within the cap; (2) lowest airtime among passers; (3) time-to-coverage tiebreak. This is where re-offer variants (retries/delay/jitter/trigger) get ranked apples-to-apples.
- **Final cross-check (meshroute engine):** `topo_to_sim.py /tmp/topo_3d7377.json --ledger runs/3d7377/send_ledger.jsonl` → **247 recovers** (~3 retries into a 54-idle window); 79/48 re-offer ~0×.
- **Boards:** all 4 build (it's lib/core).

## Caveats (known, accepted)
- The re-offer adds airtime near the contended node — bounded by the cap + LBT, paid only by fragile messages. If a link is *permanently* saturated, retries still can't help (probability boost, not a guarantee).
- Part 1 is the safe, always-correct half; Part 2 is the targeted reliability add. They can ship together or Part 1 first (it stands alone).
