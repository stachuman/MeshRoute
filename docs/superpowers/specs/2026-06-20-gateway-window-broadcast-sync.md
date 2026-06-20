<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# Gateway-window broadcast sync — make a direct neighbour's BCN + flood reach the gateway

**Status:** ✅ **READY FOR CODER** (2026-06-20 — all decisions locked to defaults). Implement §4 + the §9 build order; gate per §6. **Core: align the beacons a node already sends to the gateway window — ZERO extra beacons.**
**Origin:** the s15 cross-layer investigation (2026-06-20). Not an R6 issue — a long-standing gateway-plane gap.

---

## 0. The gate (and a gate-methodology fix this work forces)

- **Functional gate:** s15 (3-layer) cross-layer delivery, measured as a **multi-seed MEAN** (NOT the single configured seed), must **rise and stabilise** vs current code; the per-seed floor (the seed-1522 0/21 outlier) must lift well off zero. Plus: **no regression** on s18/s19/s09/s10/s16/s17, `leaks==0`, native suite green, 4 boards build.
- **★ Methodology fix (do this regardless of the code change):** cross-layer scenarios are **seed-sensitive** and MUST be gated on a seed distribution, never one seed. Current evidence (CURRENT code, varying the s15 seed): seed 1522 = **0/21**, seeds 1/42/100/7/2024/999 = **43/29/57/57/43/43%** (mean ≈45%). The deterministic single-seed s18 anchor stays as-is; **cross-layer gets a multi-seed mean**. Capture the new s15-xl reference as `mean ± range over N seeds` in `simulation/BASELINE.md`.

---

## 1. Problem (evidence-backed)

A gateway **time-multiplexes** its layers: it is on layer L only during L's window (s15: period 15 s, duration 7.5 s). Three transmit paths exist from a node:

| path | what | gateway-window-aware today? |
|---|---|---|
| **unicast / RTS** (DM, E2E-ack, gw-relay leg) | `issue_send` → `gateway_schedule_defer_ms` | **YES** — held to land in the gw's window |
| **BCN** (beacon: DV route advertisement) | `emit_beacon` / `schedule_triggered_beacon` | **NO** — fires on own cadence |
| **FLOOD** (F/RREQ route-discovery, channel digests) | `emit_route_request` / flood path | **NO** — fires on own cadence |

So a node's **DM reaches the gateway, but its BCN/flood is lost** whenever the gateway is on its other layer at that instant. Consequences measured in s15:

- **Gateways never hear each other** (GW31 hears only L1+L2 *regulars*, never the other gateways; same all round) — two time-multiplexing gateways share a layer in non-overlapping windows, and unsynced beacons never fire when the other is listening.
- **Far-layer route population is a timing lottery** — a gateway learns a route to a far-layer dst only if that dst's beacon happens to land in the gw's window. Whether the *final* regular-node dst (s15: node_ids 16/4/26/13) is reachable on the far layer swings with seed → cross-layer delivery 0–57%.
- `drop_sf_mismatch` quantifies the loss: hundreds of frames dropped at each bridge because it's tuned to its other layer's SF.

The gateway already advertises ITSELF in-window (`maybe_emit_gateway_beacon`, Slice 3d). The missing half is the **neighbour side**: a direct neighbour must get its BCN/flood *into the gateway's window* so the gateway learns the neighbour's routes. This is exactly the unicast `gateway_schedule_defer_ms` discipline, extended to broadcasts.

---

## 2. Current state — the reusable pieces

- `gateway_schedule_base_defer_ms(gw, *jmax)` (`node.cpp:440`) — PURE: ms until the gw's window opens on OUR active leaf (0 = open now), + herd-jitter range. **Reuse verbatim** for broadcast timing.
- `gateway_schedule_defer_ms(gw)` (`node.cpp:480`) — the SEND wrapper (base + jitter draw). Used by `issue_send` for unicast.
- `find_gw_schedule(gw)` / `store_gateway_schedule` — a node stores a `GatewaySchedule` **only when it ingests that gateway's own `self_gateway` beacon** (`node_beacon.cpp:493`). ⇒ **holding a schedule ≈ being a 1-hop neighbour of that gateway.** The "DIRECT neighbour" scoping is therefore *automatic* — no new neighbour-detection needed.
- The active-leaf model: a node iterates its `_layers[]`; `_active` is the current leaf. A gateway schedule's records are per-leaf; the defer math already filters to the node's active leaf.

---

## 3. Scope

A node that holds a gateway `GatewaySchedule` (its direct gateway neighbour) aligns its **BCN** (and later **FLOOD**) transmissions to that gateway's window, so the gateway reliably hears them. Unicast is already done. **No wire change** (timing only) — same frames, scheduled differently.

> **INVARIANT (author 2026-06-20): no node has more than ONE gateway as a direct neighbour.** Verified in s15 (0 of 12 gateway-hearing nodes hear >1 gateway, even with two L1-home gateways). The design **assumes single-gateway alignment** — no multi-gateway dedup, no disjoint-window coverage logic. Enforcement is **topology/provisioning**; firmware fallback if ever breached = align to ONE gateway (first-heard / strongest-SNR schedule), never split, and warn via telemetry. This invariant is what makes the alignment deterministic.

---

## 4. Design — "retime, don't add": window-ALIGNED beacons, dirty-gated

**Core principle (airtime-first, author 2026-06-20):** do NOT add beacons — **a beacon every window is waste.** Beacon FREQUENCY stays governed by route-freshness (`beacon_period_ms` + dirty-tracking + `beacon_max_idle_skip_clean`), exactly as today. The ONLY change is *when* a node's already-scheduled beacons fire: **align them to the gateway-neighbour window** so the gateway actually hears the beacons the node was going to send anyway.

### 4.1 BCN — align the PERIODIC refresh to the window (the core fix)
For a node with a gateway neighbour (≤1 by the §3 invariant), when arming its **periodic** beacon, bias the fire time to land in that gateway's window-open (`gateway_schedule_base_defer_ms` + herd-jitter, Q5):
- The periodic cadence (`beacon_period_ms`, 30 s steady / 4 s discovery) is unchanged and ≫ the window-period, so the gateway is refreshed **every few windows** — enough to beat route-aging, **not every window**.
- `beacon_max_idle_skip_clean` still applies → clean + idle = no beacon = no waste. Stable networks don't beacon into windows pointlessly.
- **Zero extra beacons** — same count, retimed to be heard. This is the airtime-optimal core.

**Triggered beacons (a dirty route changed):** fire NORMALLY/immediately — don't delay them (same-layer latency matters). The gateway picks up the new route at the next window-aligned **periodic** (≤ one `beacon_period`). *Optional knob (Q6):* if cross-layer discovery lag proves too slow, also window-align the triggered beacon (bounded ≤ one window-period delay) — measure first, add only if needed.

Reuses the existing `_next_open_ms`/timer machinery + `gateway_schedule_base_defer_ms`; no new per-window state, no per-gateway re-beacon counter.

### 4.2 FLOOD (F/RREQ, channel digest)
A flood is reactive/latency-sensitive, so do **not** delay the primary flood. Instead, if a gateway neighbour is out-of-window when the flood fires, schedule **one supplemental re-flood at the gw's window-open** (same rate-limit, same dedup). For route-discovery this guarantees the gateway sees the RREQ within one window; for the general mesh the primary flood already went out immediately.
- *Note:* much of the s15 win likely comes from BCN alone (routes propagate via the DV/beacon plane). The flood half is for completeness per the scope ("any direct and flood communication") and to cover reactive discovery toward/through gateways. §5-Q2 lets you stage it.

### 4.3 Why align-not-supplement, and the residual tradeoff
A per-window supplemental copy would send a full ~150 B DV beacon every window-period regardless of whether anything changed — pure waste, scaling with gateway-count (rejected by the author). Aligning the beacons the node sends anyway costs nothing extra. The one accepted tradeoff: a periodic beacon biased to a window is slightly delayed for NON-gateway neighbours too (≤ one window-period; fine for the refresh plane). (The multi-gateway-per-node disjoint-window problem is **ruled out by the §3 invariant** — each node aligns to its single gateway.)

---

## 5. Decisions — ALL LOCKED (author 2026-06-20: "defaults are fine")

> **Locked summary:** Q1 align/retime (not supplements) · Q2 BCN-first, flood staged · Q3 no extra rate-limit (existing `beacon_period` + dirty + `skip_clean` already bound frequency) · Q4 duty-gated like any beacon (degrade-not-starve) · Q5 reuse the existing herd-jitter for in-window placement · Q6 periodic-align ONLY (triggered fires immediately; revisit only if the gate shows discovery lag). Each item below is resolved to its stated recommendation.

- **Q1 — Mechanism: RESOLVED (author 2026-06-20) → align/retime existing beacons, NOT per-window supplemental copies** ("BCN must not be sent every window — waste"). §4 reflects this.
- **Q2 — Flood scope:** BCN-only first (likely captures most of the s15 gain via the DV plane), **vs** BCN+flood together (full scope). *Recommend staging:* BCN first, measure, add flood if the seed-mean still has a gap.
- **Q3 — Rate-limit:** once per gateway per window-period (*recommended*) vs a coarser cap. Confirm the per-gw stamp + dedup-across-coincident-windows policy.
- **Q4 — Duty interaction:** supplemental beacons count against the node's duty budget like any beacon (*recommended*); confirm they're subject to the same airtime backstop and may be skipped under duty pressure (degrade, don't starve).
- **Q5 — In-window placement:** land at window-open + a small offset, or use the existing herd-jitter (`*jmax`) so multiple neighbours don't collide at open (*recommend reuse the herd-jitter*).
- **Q6 — Triggered-beacon alignment:** start with **periodic-align only** (triggered fires immediately — protects same-layer latency); add triggered-window-align ONLY if the gate shows cross-layer discovery lag. *Recommend: measure periodic-only first.*

---

## 6. Verification / gate

1. **Native:** new unit(s) — a node with a mocked gateway schedule emits a supplemental beacon timed into the window; rate-limit holds (no >1 per window); no supplemental when the gw is in-window or when no gateway neighbour exists. Full suite stays green.
2. **Sim — the payoff:** s15 cross-layer **multi-seed mean rises** (floor off zero, up from ~45%) with `leaks==0`; the bridges now hold far-layer routes to the **final regular-node dsts** (the ex-`send_no_route` targets — s15 node_ids 16/4/26/13 et al.), so **bridge `send_no_route` drops sharply**. Also assert the §3 invariant holds across the suite (no node hears >1 gateway).
3. **No regression:** s18/s19/s09/s10/s16/s17 multi-seed — no delivery loss; **airtime MUST be ≈flat** — the align approach adds zero beacons, so flag ANY beacon-count/airtime rise as a bug (report the delta, expect ≈0). Confirm biasing didn't defeat `skip_clean`, and no same-layer latency regression from the periodic delay.
4. **Boards:** 4 builds.
5. **Re-baseline:** capture s15-xl as a multi-seed mean±range in `BASELINE.md` (§0 methodology fix).

---

## 7. Risks

- **Airtime/duty:** the align approach adds ZERO beacons (frequency unchanged; only timing shifts) — the whole point. Residual: a periodic beacon biased to a window is delayed ≤ one window-period for all neighbours; confirm no same-layer regression (§6). Duty-gating (Q4) applies to the beacon as today.
- **Herd collision at window-open:** many neighbours targeting the same window can collide → reuse herd-jitter (Q5).
- **Multi-gateway / overlapping windows:** dedup by distinct target window-open so one supplement covers coincident gateways.
- **Mobile gateways / stale schedules:** a stale `heard_ms` anchor mis-times the supplement; the existing schedule-validity/aging applies — no new exposure beyond what unicast already tolerates.

## 8. Out of scope
- Gateway-side pull (a gw soliciting neighbour state on window-open) — a different lever; this spec is the sender side the author chose.
- Window co-scheduling between gateways (making gw windows overlap) — orthogonal anti-phase tuning.
- The R6 membership/join plane — unrelated (s15 is unmanaged; R6 gates are inert there).

---

## 9. Build order (coder)

1. **Locate** the periodic-beacon arming (the `kBeaconTimerId` re-arm against `beacon_period_ms`). Leave `schedule_triggered_beacon` and the dirty/`skip_clean` logic untouched.
2. **Add the bias (the whole feature):** when arming the *periodic* beacon, if the node holds ≥1 gateway `GatewaySchedule` (`find_gw_schedule`), compute `defer = gateway_schedule_base_defer_ms(soonest_gw_on_active_leaf, &jmax)` and arm at `now + defer + rand[0,jmax)` instead of the plain cadence instant — **bounded so it never delays > one window-period**; skip the bias if `defer==0` (gw already in-window) or there's no gateway neighbour. Cadence, frequency, `skip_clean`, dirty-tracking all UNCHANGED.
3. **Single gateway (invariant §3):** align to the node's one gateway. Defensive guard: if a node ever holds >1 gateway schedule (invariant breach), align to ONE (first-heard / strongest-SNR) — never split — and emit a telemetry warn (`multi_gateway_neighbour`).
4. **Native tests:** (a) node with a mocked gw schedule → its periodic beacon lands inside the window; (b) no gw neighbour → timing identical to today; (c) `skip_clean` still suppresses idle beacons; (d) **beacon COUNT over a fixed interval is unchanged** vs baseline (the airtime invariant).
5. **Sim gate (§6):** s15 multi-seed mean cross-layer UP (floor off zero), **airtime ≈flat**, no regression on s18/s19/s09/s10/s16/s17; confirm gw↔gw `beacon_rx` now appears and bridge `send_no_route` drops.
6. **Re-baseline** s15-xl as a multi-seed mean±range in `simulation/BASELINE.md`.
7. Hand back **green-shaped + uncommitted**; the quality-gate verifies against §6 before the author commits.
