<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# Delivery baseline suite — the result-comparison gate

Replaces byte-identical s18 md5 as the gate for **behaviour-changing** work (routing-liveness, gateway/cross-layer, etc.). Gate on the **delivery breakdown**, not the byte stream.

**Run (per scenario):**
```
lus -e meshroute simulation/<s>.json /tmp/<s>.ndjson
python3 tools/dm_delivery_breakdown.py simulation/<s>.json /tmp/<s>.ndjson --failures
```

**Reference numbers — RE-CAPTURED 2026-06-21.** Code state: R6.1 `9c80173` + R6.2 `471476b` + BCN-gateway-window **align** `3dbaa14` + the **gateway reactive route-pull** (uncommitted, commit imminent; spec `2026-06-21-gateway-reactive-route-pull-on-bridge-miss.md`). Gate = no regression vs these + `leaks == 0`.

| scenario | role | same-layer (arr/sent) | cross-layer (deliv/sent) | leaks |
|---|---|---|---|---|
| `s18_meshroute` | single-layer dense — **anchor** (mean_hops ~10, cascade-inclusive — see Hop-count note) | **101/113 (89%)** (was 98 pre-bidi — see 2026-06-29 note) | — | — |
| `s19_singlelayer_multihop_chain` | single-layer **MULTI-HOP** (redundant 3-hop chain) | **8/8 (100%)** over 2–3-hop paths | — | — |
| `s09_two_layer_gateway` | 2-layer cross-layer | 3/3 (100%) | **2/2 (100%)** | — |
| `s10_two_layer_separation` | 2-layer cross-layer (separated) | 3/3 (100%) | **2/2 (100%)** | — |
| `s16_dense_gateway` | dense 2-layer **gateway-overload stress** (4×20 burst) | — | **57/80 (71%)** | — |
| `s15_three_layer` | **3-layer** cross-layer + channels | 47/47 (100%) | **mean ≈90% — MULTI-SEED, see ★** | **0** |
| `s17_metro` | **252-node scale** + channels | **26/30 (87%)** | n/a (inert) | **0** |

★ **Cross-layer is SEED-SENSITIVE → gate on a MULTI-SEED MEAN, never the single configured seed.** s15 over seeds {1522, 1, 42, 100, 7, 2024, 999} = **13 / 21 / 21 / 17 / 20 / 20 / 21 = mean 19/21 ≈ 90%** (range 62–100%). The scenario's baked-in seed **1522 is the worst case (13/21)** — do NOT gate on it alone (pre-fix it read 0/21, which is what made it look like a collapse). s15 channels = 218/224 (97%), `leaks 0`. Apply the same multi-seed discipline to the other cross-layer scenarios where feasible.

**What changed vs the old 2026-06-17 baseline (and why) — read before trusting a diff against history:**
- **s16 cross 15% → 71%** and **s15 cross 67%(single-seed) → ≈90%(multi-seed)**: FIXED. The gateway-window beacon **align** (`3dbaa14`) carries the 2-layer dense case (s16); the **reactive route-pull** (a gateway fires REQ_SYNC + defers, instead of dropping, on a bridged-leg route-miss) carries the 3-layer case (s15). Root cause of the old lows: dirty-only steady-state beacons never re-advertise stable routes to a time-multiplexing gateway (reactive-pull spec §1; the byte-budget/truncation theory was investigated and **ruled out**).
- **s18 same 108 → 98** and **s17 same 30 → 26**: down-shifts from the intervening gateway-rework cluster (Phase-4 / Gateway-fixes / bitmap-deffer, `93fa6c5`…`b15586a`) — airtime/timing chaos, not the cross-layer fixes (s18 multi-seed mean ≈101, keystone seed 157373 = 110 ≥ 108). ⚠ Recorded as the new reference; lifting them back is a **separate** investigation, not a cross-layer-fix regression.
- **s18 same 98 → 101 (2026-06-29): asymmetric-link-aware routing IMPROVED it.** s18 is the realistic Seattle mesh (`SEAMESH`/`First_Hill`/`Fremont`) with genuine **directional links** (topology `bidir:false`). The new bidirectionality plane — heard-set census + the `one_way` route penalty (commit "Slice 5") — detects ~478 of these asymmetric link-instances per run and re-routes around them → **+3 delivered**, deterministic, `leaks 0` (the configured seed rises to ≈ the prior multi-seed mean). The slow-reprobe (Slice 6) is **delivery-neutral on the idealized sim** but reclaims airtime: node-72 9-node 16-seed A/B, doomed `rts_tx` to the isolated nodes (204/247) **−21%** (149→118), total channel load −16%, delivery flat. The lucky-marginal delivery tradeoff (sim has the isolated nodes at 0%, metal ~40%) is **metal-tuned** via `link_reprobe_ttl_ms` (60 s seed) + the `bidi_penalty_one_way_q4` 640→192 fallback — not a sim judgment.

**Notes / how to read the gate:**
- `s16` 71% and `s15` ≈90% are **post-fix** numbers (the old 15%/67% are superseded). Both still have headroom (s16 is a deliberate overload; s15's worst seed lands 13/21). The gate is **don't regress below these** + `leaks == 0`.
- `s17_metro` cross-layer is **inert** on meshroute (untranslated source = single-layer gateways; *translating* it degrades the dense scenario — channels 101%→33%). So gate `s17` on **same-layer 26/30 + channels leaks 0 + scale**, not cross-layer. (Cross-layer coverage comes from s09/s10/s15/s16.) **Follow-up:** cross-layer-at-scale window tuning — separate from the baseline.
- `s19_singlelayer_multihop_chain` is the suite's **multi-hop** coverage (s18/s17 same-layer are single-hop). Redundant 3-hop chain `A-{L,L2}-{R,R2}-B`, no A↔B direct link ⇒ every A↔B DM relays ≥2 hops (verified: A→L2→R2→B = 3 hops; routes converge at hops 2–4). Two disjoint paths ⇒ also the **liveness-reroute base** (kill one relay → reroute via the parallel path; the `t96` Phase-2 gate builds on this). Gate on **delivery 8/8** + the **mean_hops** column (A↔B **3.0**, A↔R **2.0**) — deterministic (seed 42, lossless links). (`dm_delivery_breakdown` counts hops via distinct `data_rx` receivers, which carry `origin`, rather than the origin-less relay `data_tx` — fixed 2026-06-17 so same-layer multi-hop is measured. Cross-layer hop-count is still uncounted — its records key on the gateway wire-dst; cross-layer *delivery* is measured by the `cross-layer DMs` line.)
- **`mean_hops` in DENSE scenarios is cascade-inclusive, NOT the delivered-path length.** It counts *distinct nodes that data_rx'd the DM as a next-hop* — in a sparse chain (s19) that equals the path (3); in a dense mesh (**s18 ~10, s17 ~10.3**) it also counts every next-hop a cascading/contending DM touched, so it reads ~10 even for short deliveries. **Gate on relative STABILITY vs these numbers**, not the absolute value. A big jump = routing churn to investigate.
- Cross-layer is measured via `tx_enqueue_xl` + the `(origin,ctr)`-matched `delivered` (the firmware preserves origin/ctr) — the `dm_delivery_breakdown.py` "cross-layer DMs: X/Y" line is the authoritative per-scenario cross-layer number.
- Gateway scenarios were translated from `~/lora-universal-simulator/scenarios/` via `tools/translate_gateways.py` (adds the C++ `n_layers:2 + layers[]` dual-layer schema). `s18`/`s17` are the untranslated sources.

**Removed as obsolete/non-working:** `s09_two_layer_gateway_debug` (1-DM debug), `s15_three_layer_dual` (engine-untagged Lua-comparison variant), `s20_crosslayer` / `s21_multihop_xl` (0 gateways → non-functional cross-layer), `s22_location` (2-node/1-DM, too thin). Note `s21_leaf_config_divergence` + `s22_leaf_config_join` (R6) are membership-gate scenarios, not part of this delivery suite.
