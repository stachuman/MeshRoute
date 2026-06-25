<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Channel flood — honest seeding (port-fix) + origin re-offer (single/contended-link reliability)

**Status:** coder instruction. The user commits + flashes; I gate. Two parts: **(1) a PORT-CORRECTION** (the C++ diverged from the Lua's flood-origination seed) and **(2) a deliberate DIVERGENCE** (an origin re-offer the Lua doesn't have). No wire change; both are TX-scheduling/coverage logic.

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

## Tests / gate
- **Native:** the re-offer table + the seed change are pure logic — unit-test: empty seed → `seen_by` starts 0; a re-offer fires when `seen_by` empty + retries remain; stops on first mark or at the cap; jitter is deterministic.
- **★ Lua-parity (the key gate for Part 1):** run the s18-family scenarios under `lus --engine lua` vs `--engine meshroute`; the honest seed should bring channel coverage to **parity-or-better** (mr ≥ lua), not regress. The current keystone md5 WILL change (the seed changes the flood) — that's expected; gate on the **lua-parity delta**, not the old md5.
- **Sim repro (the payoff):** re-run `topo_to_sim.py /tmp/topo_3d7377.json --ledger runs/3d7377/send_ledger.jsonl` → **247 should recover** (the re-offer hits a 54-idle window within ~3 retries). Confirm the well-connected origins (79, 48) still re-offer ~0 times (no airtime regression).
- **Boards:** all 4 build (it's lib/core).

## Caveats (known, accepted)
- The re-offer adds airtime near the contended node — bounded by the cap + LBT, paid only by fragile messages. If a link is *permanently* saturated, retries still can't help (probability boost, not a guarantee).
- Part 1 is the safe, always-correct half; Part 2 is the targeted reliability add. They can ship together or Part 1 first (it stands alone).
