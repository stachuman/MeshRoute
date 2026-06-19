// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>

# seen_bitmap cost reduction + benefit validation — problem statement, decisions & implementation guide

**Status:** REVIEWED 2026-06-19 → decisions locked, READY TO IMPLEMENT. The quality review confirmed §2 (problem framing + cost mechanism accurate) and resolved all four open questions (§5, now answered). Two findings are folded in:
- **F1 (Q4):** the cost is a *necessary* divergence from an **unbudgeted** Lua, not an unnecessary one — so the job is "spend the 151-B budget well," not "match the Lua" (§2.5).
- **F2:** there is a **pre-existing latent SILENT beacon-drop on byte-overflow** (`node_beacon.cpp:316`) that the conditional-cap change must close in the same pass (§2.6).

**Coder, do this:** build Proposal 1 (§3) as a **true byte-budget cap** (NOT a 35/27 toggle), reordering the beacon build so the schedule/bitmap/ext sizes are known before entry selection; then build Proposal 2's scenario. §4 gates are load-bearing — start with the §4 pre-step (apportion the −7) before changing code.

**Scope:** the routing-liveness `seen_bitmap` plane (`docs/superpowers/specs/2026-06-17-routing-liveness-plane-port.md`, Phase-4-adjacent). This doc covers ONLY the seen_bitmap cost/benefit, post-re-port.

---

## 1. Background (what is seen_bitmap)

A node's beacon carries a 32-byte (256-bit) bitmap of node_ids it has recently "seen" (`dest_seen_ms` within `seen_bitmap_ttl_ms` = 30 min). On receipt, a node stamps those ids fresh in its own `dest_seen_ms` map and refreshes `last_seen_ms` on any route candidate whose `next_hop` is the beacon sender. Purpose (Lua `build_seen_bitmap@1283` / `apply_seen_bitmap@1357`): **keep routes-via-an-alive-but-quiet next-hop FRESH via gossip**, so the Phase-2 freshness gate (`is_next_hop_fresh` in `route_strictly_better` viability) doesn't wrongly demote a working path you don't directly+frequently hear from. The user reports it worked well in the Lua sim, *especially for unstable/disappearing nodes*.

## 2. Problem statement (VERIFIED 2026-06-18)

The C++ port was **doubly broken**, now fixed; but a **structural cost remains unjustified by any current benefit measurement.**

**2.1 Two bugs found + fixed (this is settled, not under review):**
- **Dead apply:** `apply_seen_bitmap` was mis-nested inside `if (b.has_ext)` → ran only on the rare ext-carrying beacon → `seen_bitmap_rx == 0` across all of s18 (apply never executed) while still paying the wire cost. **Fixed** (un-nested).
- **Bounded-map starvation:** `dest_seen_ms` was piggybacked on the bounded 64-entry `PeerLiveness` LRU table with `create=false` on apply → gossip-only peers got no freshness, and >64 distinct dests evicted each other. **Fixed**: re-ported to a dedicated per-layer `uint64_t _dest_seen_ms[256]` (full 0–254 range, no eviction; mirrors the Lua unbounded map). TDD test added ("dest_seen survives >cap_peer_liveness distinct dests").

**2.2 The remaining structural cost (the subject of this proposal):**
Even with the mechanism working *perfectly*, the suite still regresses vs the no-bitmap baseline:

| scenario | no bitmap (baseline) | bitmap re-ported (correct) | delta |
|---|---|---|---|
| s18_meshroute (dense static) | 108/113 | **101/113** | **−7** |
| s15_three_layer | 47/48 · xl 14/21 | 42/42 · xl 8/21 | ~−5/−6 |
| s17_metro (252-node) | 30/30 | 23/30 | −7 |
| s06_seattle_lifecycle (5 die) | 112/148 | 113/148 | **+1 (noise)** |

The faithful re-port made **zero delivery difference** vs the nesting-fix-only version → **the regression is purely the 32-byte beacon-capacity cost**, not the apply quality.

**2.3 Root cause of the cost (mechanism, verified):**
`kMaxBeaconEntries = (beacon_max_bytes − 8 − 32 − 1) / 4 = 27` (`node_beacon.cpp:21`) — a **compile-time constant** that assumes the 32-byte bitmap is always present, dropping the per-beacon route-entry budget from **35 → 27**. The bitmap is built on **every** beacon (`node_beacon.cpp:283`, unconditional when `seen_bitmap_enabled && !is_mobile`). The truncation bites on **full-page beacons** — `dirty_only == false`, i.e. discovery + `kind=="sync"` (`node_beacon.cpp:202`, the Phase-2 stable rotation `:221`) — so route-table propagation during **cold-start discovery** carries 27 entries/page instead of 35 → slower convergence → DMs sent before their route converged → ≈−7 on dense meshes. Steady-state `dirty_only` beacons carry only dirty entries (usually < 27) so they rarely truncate.

**2.4 Why no benefit shows:** seen_bitmap helps only when a route's next-hop is *alive but rarely directly heard* AND freshness would otherwise false-demote it AND a worse alt exists to wrongly switch to. The suite has no such topology: s18/s17 are dense (every node heard directly → freshness never false-demotes); s06's `dies_at_ms` deaths are *permanent* (the liveness penalty's job, not seen_bitmap's). So the benefit is real (per Lua) but **unmeasured in C++**.

**Net:** as-shipped, seen_bitmap costs ~−7 on dense convergence and returns nothing measurable. We want to (1) make the cost near-zero and (2) build the scenario that proves the benefit, so the keep/drop decision is evidence-based.

**2.5 Reviewer finding F1 (Q4 resolved) — the cost is a NECESSARY divergence, not an unnecessary one.**
Verified against the frozen Lua: the Lua emits the bitmap on **every** beacon (no throttle — `build_seen_bitmap` is called unconditionally in `pack_beacon`, `dv:1747`; the `dv:1314` `emit_interval` only throttles the `dest_seen_update` *telemetry*, not the bitmap). Crucially, the Lua's entry budget does **not** reserve the bitmap: `beacon_max_entries = floor((beacon_max_bytes − 8) / 4) = 35` (`dv:8602-8603`) subtracts **only the 8-B header**. And Lua `pack_beacon` has **no total-byte check** — it packs up to 35 entries, appends the full 32-B bitmap (`dv:1850`) + ext, returns; nothing truncates to fit (the only "too large" guards, `dv:12091/12138/12208`, are DATA/channel, not beacons). So a full-page Lua beacon with the bitmap is `8 + 35×4 + 32 + ext ≈ 181 B` — it **silently overflows its own `beacon_max_bytes=151`** and never pays an entry cost.

> **Conclusion:** the Lua "didn't regress" because it is **unbudgeted** — it overflows the frame instead of truncating. The C++ honoring a hard 151-B budget with a real drop-on-overflow is *more correct*, not a bug to undo — same class as our other deliberate divergences (DAD `lease_age`, RTS duty-defer). **So this work is not "stop diverging"; it is "spend the 151-B budget well."**

**Rejected alternative (recorded so it isn't re-litigated):** raise `beacon_max_bytes` to ~181 to match the Lua on-air. Rejected because (a) longer beacons widen the collision cross-section ~22% on **every full page**, which in the dense scenarios that regressed (`s17_metro`, 252-node) likely trades truncation-loss for collision-loss — net-neutral-or-worse; (b) `beacon_max_bytes` is woven into the LBT max-defer derivation (`flood_lbt_max_defer_ms = airtime(beacon_max_bytes)`) and the `buf[]`/`_deferred_lbt[].buf` sizing — wider blast radius. Proposal 1 keeps full pages at ~148 B (no extra airtime) and parks the 32 B on small steady-state beacons — strictly cheaper.

**2.6 Reviewer finding F2 — a latent SILENT beacon-drop on byte-overflow (pre-existing; close it here).**
Neither `pack_beacon` enforces a total-byte budget; the C++ relies on the `out` span (`buf[beacon_max_bytes=151]`, `node_beacon.cpp:314-315`) and **drops the entire beacon on overflow** — `if (len == 0) { log; return; }` (`node_beacon.cpp:316`), log-only, no telemetry. The naive `kMaxBeaconEntries=27` accounts for header+bitmap+ext_len but **not** for: (i) the **ext payload** (digest ≤14 + gateway ≤15 + suspect ≤16 — `node_beacon.cpp:302`), nor (ii) the **gateway schedule block** (`1 + 4×n_sched` B; `frame_codec.cpp` pack). Arithmetic: a 27-entry full page + bitmap leaves only `151 − 8 − 27×4 − 32 = 3 B` → after the 1-B `ext_len`, **any ext TLV with payload > 2 B overflows → the beacon is dropped today.** In the dense+churning scenarios that regressed, suspect-gossip + channel-digest TLVs co-occur with full pages, so part of the −7 is plausibly **dropped beacons**, not only truncation.

> Note the symmetry: `27 entries + 32 bitmap` and `35 entries + 0 bitmap` are byte-identical at **140 entry+bitmap bytes**. So omitting the bitmap **alone** is byte-neutral and does NOT close this drop — only making the cap a real byte-budget (§3 P1, change 2) does.

## 3. Proposed solution

### Proposal 1 — eliminate the convergence cost (placement + a TRUE byte-budget cap)

**Decision (Q1 resolved): include the bitmap on steady-state (`dirty_only`) beacons; omit it on full-page (discovery/sync) beacons.** Discovery/sync pages get the full route-entry budget back (fast convergence); the bitmap rides steady-state beacons. ⚠ **Cross-review caveat (2026-06-19, verified):** gossip does NOT ride *every* period — `beacon_max_idle_force` (`node_beacon.cpp:529`) has a `skip_clean` branch (`:537`): a clean node (`dirty_n==0`) that *recently heard a neighbour* (`since_bcn_rx < beacon_max_idle_ms/3`) suppresses its own beacon. So in a quiet/stable mesh, the bitmap gossips only on **dirty + forced-idle** beacons, not on every beacon period. Irrelevant to the cost fix (discovery pages are full regardless), **but it is a confound for P2's benefit measurement** (see §3 P2 / §4). Freshness gossip is a steady-state keepalive, not a cold-start need. (`every-Nth-beacon` was considered as the perpetual-churn hedge and **not** chosen — `dirty_only` is simpler; **revisit if P2 shows no benefit AND the cause is gossip-starvation** rather than a worthless mechanism.)

**Two coupled changes:**

1. **Gate bitmap inclusion on `!full_page`.** Beside `dirty_only` (`node_beacon.cpp:202`) compute
   `const bool include_bitmap = _cfg.seen_bitmap_enabled && !_cfg.is_mobile && dirty_only;`
   and gate the build block (`node_beacon.cpp:283`) on `include_bitmap`.

2. **Replace `kMaxBeaconEntries` (the compile constant, `node_beacon.cpp:21`) with a per-beacon TRUE byte-budget** — this is the part that also closes F2 (§2.6). Compute the cap from the bytes this beacon's non-entry sections actually consume, then divide the remainder by 4:
   ```
   budget = beacon_max_bytes − 8                     // fixed header
          − (has_schedule  ? 1 + 4*n_sched : 0)      // gateway schedule block
          − (include_bitmap ? 32 : 0)                // this beacon's bitmap
          − (ext_n > 0      ? 1 + ext_n : 0)         // ext_len + TLV payload
   max_entries = clamp(budget / 4, 0, 63)            // 63 = 6-bit n_entries field max
   ```
   Size `entries[]` / `pack_idx[]` at the theoretical max **35** (`= (151−8)/4`, the no-schedule/no-bitmap/no-ext case). With no ext/schedule: bitmap-off → 35, bitmap-on → 27 (the old numbers, now *derived*, not hard-coded). With ext/schedule present the cap shrinks, so a full page + a populated TLV **no longer overflows → no silent drop.**

**Build-order change this requires:** the schedule, bitmap, and ext blocks must be **built before** entry selection so their sizes are known. Today: entry selection `node_beacon.cpp:206-230`, schedule `:253-278`, bitmap `:283-299`, ext `:300-312`. Reorder to: decide `dirty_only`/`include_bitmap` → build schedule + (conditional) bitmap + ext → compute `max_entries` → select + pack entries → `pack_beacon`. The ext-builders' side effects (`ad_count`++/retire, `dv:1453`) are independent of entry selection, so the reorder is behavior-safe — keep them firing exactly once per beacon, and keep the dirty-clear (`node_beacon.cpp:326`) keyed to the entries that actually landed.

**Expected effect:** s18/s15/s17 recover to ~baseline (discovery pages full again); the freshness bitmap still gossips every steady-state beacon; the latent ext/schedule overflow-drop is closed. The −7 should largely vanish.

### Proposal 2 — build the benefit scenario (quiet-but-alive next-hop)

**Idea:** construct `simulation/sNN_quiet_link_freshness.json` that *exercises* seen_bitmap's purpose, which the current suite cannot:
- A multi-hop path O → R → D where **R is alive but the originator O rarely hears R directly** (asymmetric link / R beacons infrequently), so without gossip `is_next_hop_fresh(R)` at O goes stale (>20 min) and the Phase-2 viability gate demotes the O→R→D route.
- A **neighbour N that DOES hear R** and gossips R in its seen_bitmap → O refreshes `dest_seen_ms[R]` → the route stays viable → delivery continues.
- A worse/longer alternative path so the demotion would visibly hurt delivery if it fired.

Mechanism knobs available in the scenario schema: explicit `topology.links` (asymmetric — `bidir:false`, one-directional or weak O↔R), per-node `beacon_period_ms` (make R quiet), `start_at_ms`/`dies_at_ms` (R stays alive). Single-layer, `engine:meshroute`, `allowed_data_sfs` set.

**Reviewer notes (verified):**
- **The consumer IS wired** — `is_next_hop_fresh` is consulted in `route_strictly_better` viability at `node_routing.cpp:98-99`. (The `node.h:430` comment *"DEFINED, not consulted in P1"* is **stale** — fix it; see §4.) So this scenario *can* fire the demotion; nothing else blocks the benefit from showing.
- **Thread the two TTLs precisely:** the freshness **gate** is `next_hop_live_ttl_ms = 20 min`; the gossip **window** is `seen_bitmap_ttl_ms = 30 min` (`protocol_constants.h:97,83`). So O's *direct* link to R must be quiet **> 20 min** (without gossip `dest_seen_ms[R]@O` ages out → demotion), while the gossiper N must beacon **< 20 min** (so O re-stamps in time) and N must itself hear R within its 30-min window. ⇒ the scenario needs **> 20 min of sim time** + tuned `beacon_period_ms` and asymmetric `topology.links`. Deterministic (seed-fixed), but not a 60-second run.

- **Gossip-cadence confound (from the §3 P1 `skip_clean` caveat):** the gossiper `N` only emits its bitmap on a **dirty or forced-idle** beacon — if `N` is clean and hearing neighbours it `skip_clean`s. So the scenario must keep `N` emitting (e.g. `N` carries some route churn, or set `beacon_max_idle_ms` low enough that `N`'s forced-idle beacons fire within O's 20-min freshness gate). **Confirm `seen_bitmap_rx` fires at O for R's bit before trusting a WITH/WITHOUT delta** — a null result with `applied==0` is gossip-starvation, not a worthless mechanism.

**Gate / success criterion:** delivery on this scenario is **materially higher WITH seen_bitmap than WITHOUT** (re-run both ways, like the s06 A/B). That is the evidence the keep-decision needs. If it shows no benefit even here AND gossip demonstrably reached O (the cadence check above), seen_bitmap should be dropped/off-by-default — the proposal explicitly allows that conclusion.

## 4. Gates (load-bearing for the implementation)

- **Pre-step (diagnose the −7, before changing code):** instrument/count the `"beacon pack failed (entries overflow)"` log (`node_beacon.cpp:316`) across the current `s18`/`s17` bitmap runs. Apportions the −7 between truncation (convergence) and outright drop (F2). If drops are a meaningful slice, the byte-budget cap alone recovers much of it.
- `pio test -e native` green (the existing dest_seen TDD test stays). **Add a unit for the byte-budget cap with THREE cases, not two:** (a) no-bitmap/no-ext beacon packs up to 35 entries; (b) bitmap beacon ≤27; (c) **a full page + a populated ext TLV does NOT overflow** (`pack_beacon` returns non-zero, total ≤151) — the F2 regression guard.
- **`simulation/BASELINE.md` suite** via `dm_delivery_breakdown.py` — **s18/s15/s17 recover to ≥ their no-bitmap baselines** (108 / 47+14 / 30), `leaks==0`, s19/s09/s16/s06 no-regress. The load-bearing gate for Proposal 1.
- **The new quiet-link scenario** shows WITH-bitmap > WITHOUT-bitmap delivery (Proposal 2's evidence gate).
- 3 boards (`gateway -e xiao_sx1262 -e heltec_v3`); gateway RAM already at 72.8% with the +2KB/layer `_dest_seen_ms` array (was 70.2%) — no new RAM in these proposals, but confirm.
- **Housekeeping:** update the stale `node.h:430` comment (`is_next_hop_fresh` IS consulted now — `node_routing.cpp:98`).

## 5. Resolved decisions (review 2026-06-19)

- **Q1 (placement policy): RESOLVED → bitmap on `dirty_only` (steady-state) beacons.** The Lua is always-on, so both options diverge from it; `dirty_only` is the cheaper faithful-enough choice (full discovery pages, gossip every steady-state period). `every-Nth` kept on the shelf as the perpetual-churn hedge only.
- **Q2 (byte budget composes): RESOLVED — and the exposure is bigger than the bitmap.** The real risk is the **ext block (and gateway schedule)**, not the bitmap: there is no total-byte guard, overflow → silent drop (§2.6). Fix = the **true byte-budget cap** in §3 P1 (fold in schedule + bitmap + ext). `27+bitmap` and `35−bitmap` are byte-equal (140 B), so omitting the bitmap alone does NOT close the overflow — the budget cap does.
- **Q3 (benefit vs residual cost): LIKELY DISSOLVES.** With the byte-budget cap, full pages return to 35 entries with **zero** truncation, so there should be no residual to trade. Re-confirm after the suite re-run; if a residual remains, author's call against P2's evidence.
- **Q4 (Lua parity): RESOLVED — necessary divergence (see §2.5).** Lua = unbudgeted (35-entry budget that ignores the bitmap + no byte cap → overflows 151, never truncates). We are not diverging unnecessarily; we are spending a real budget the Lua never had to. `raise beacon_max_bytes` rejected (§2.5).

## 6. Verified baselines (reference)

- s18 no-bitmap = **108/113**; bitmap (broken or re-ported) = **98–101/113**.
- Re-port = delivery-neutral vs the nesting-fix-only version (∴ cost is structural, §2.2).
- `kMaxBeaconEntries`: 27 (with bitmap) vs 35 (without); `beacon_max_bytes=151`.
- Gateway RAM 72.8% after the `_dest_seen_ms[256]` (uint64, 2 KB/layer) add.
- Current tree (uncommitted, on `main`, native 22892/0, 3 boards green): pick-gate reverted · seen_bitmap RE-PORTED (correct, full Lua apply + unbounded map) · `s06_seattle_lifecycle.json` added · device OTA.

**Review-verified (2026-06-19):**
- Lua entry budget `= floor((151−8)/4) = 35`, header-only (`dv:8602-8603`); Lua `pack_beacon` builds the bitmap every page (`dv:1747`) with **no byte cap** → ~181-B full pages, never truncated (Q4 / §2.5).
- C++ has no total-byte guard either; overflow → **silent beacon drop** (`node_beacon.cpp:316`). `27+bitmap ≡ 35−bitmap = 140 entry+bitmap B`; a full page + ext > 2 B overflows today (F2 / §2.6).
- Freshness consumer **is** wired: `is_next_hop_fresh` @ `node_routing.cpp:98-99`; TTLs `next_hop_live_ttl_ms = 20 min` / `seen_bitmap_ttl_ms = 30 min` (`protocol_constants.h:97,83`). `node.h:430` comment is stale.
