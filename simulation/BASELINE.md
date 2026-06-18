<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# Delivery baseline suite — the result-comparison gate

Replaces byte-identical s18 md5 as the gate for **behaviour-changing** work (routing-liveness, etc.). Gate on the **delivery breakdown**, not the byte stream.

**Run (per scenario):**
```
lus -e meshroute simulation/<s>.json /tmp/<s>.ndjson
python3 tools/dm_delivery_breakdown.py simulation/<s>.json /tmp/<s>.ndjson --failures
```

**Reference numbers (captured 2026-06-17, current HEAD). Gate = no regression vs these + `leaks == 0`:**

| scenario | role | same-layer (arr/sent) | cross-layer (deliv/sent) | leaks |
|---|---|---|---|---|
| `s18_meshroute` | single-layer dense — **anchor** (mean_hops ~10, cascade-inclusive — see Hop-count note) | **108/113 (96%)** | — | — |
| `s19_singlelayer_multihop_chain` | single-layer **MULTI-HOP** (redundant 3-hop chain) | **8/8 (100%)** over 2–3-hop paths | — | — |
| `s09_two_layer_gateway` | 2-layer cross-layer | 1/1 | **2/2 (100%)** | — |
| `s10_two_layer_separation` | 2-layer cross-layer (separated) | 1/1 | **2/2 (100%)** | — |
| `s16_dense_gateway` | dense 2-layer **gateway-overload stress** (4×20 burst) | — | **12/80 (15%)** | — |
| `s15_three_layer` | **3-layer** cross-layer + channels | 47/48 (98%) | **14/21 (67%)** | **0** |
| `s17_metro` | **252-node scale** + channels | **30/30 (100%)** | n/a (inert) | **0** |

**Notes / how to read the gate:**
- `s16` 15% and `s15` 67% are NOT bugs — `s16` is a deliberate gateway-overload stress; `s15` is 3-layer with C++-derived anti-phase windows (semantic parity, not byte-exact). The gate is **don't regress below these**, and the liveness work should *improve* them.
- `s17_metro` cross-layer is **inert** on meshroute (untranslated source = single-layer gateways; *translating* it degrades the dense scenario — channels 101%→33%). So gate `s17` on **same-layer 30/30 + channels leaks 0 + scale**, not cross-layer. (Cross-layer coverage comes from s09/s10/s15/s16.) **Follow-up:** cross-layer-at-scale window tuning — separate from the baseline.
- `s19_singlelayer_multihop_chain` is the suite's **multi-hop** coverage (s18/s17 same-layer are single-hop). Redundant 3-hop chain `A-{L,L2}-{R,R2}-B`, no A↔B direct link ⇒ every A↔B DM relays ≥2 hops (verified: A→L2→R2→B = 3 hops; routes converge at hops 2–4). Two disjoint paths ⇒ also the **liveness-reroute base** (kill one relay → reroute via the parallel path; the `t96` Phase-2 gate builds on this). Gate on **delivery 8/8** + the **mean_hops** column (A↔B **3.0**, A↔R **2.0**) — deterministic (seed 42, lossless links). (`dm_delivery_breakdown` counts hops via distinct `data_rx` receivers, which carry `origin`, rather than the origin-less relay `data_tx` — fixed 2026-06-17 so same-layer multi-hop is measured. Cross-layer hop-count is still uncounted — its records key on the gateway wire-dst; cross-layer *delivery* is measured by the `cross-layer DMs` line.)
- **`mean_hops` in DENSE scenarios is cascade-inclusive, NOT the delivered-path length.** It counts *distinct nodes that data_rx'd the DM as a next-hop* — in a sparse chain (s19) that equals the path (3); in a dense mesh (**s18 ~10, s17 ~10.3**) it also counts every next-hop a cascading/contending DM touched, so it reads ~10 even for short deliveries. (The earlier "mean_hops 1.0" figures were the OLD carriers method — it undercounted because relay `data_tx` lacks `origin`; superseded.) **Gate on relative STABILITY vs these numbers** (e.g. Phase 2: s18 9.5→10.0, s17 10.3→10.3 = stable), not the absolute value. A big jump = routing churn to investigate.
- Cross-layer is measured via `tx_enqueue_xl` + the `(origin,ctr)`-matched `delivered` (the firmware preserves origin/ctr) — the `dm_delivery_breakdown.py` "cross-layer DMs: X/Y" line is the authoritative per-scenario cross-layer number.
- Gateway scenarios were translated from `~/lora-universal-simulator/scenarios/` via `tools/translate_gateways.py` (adds the C++ `n_layers:2 + layers[]` dual-layer schema). `s18`/`s17` are the untranslated sources.

**Removed as obsolete/non-working:** `s09_two_layer_gateway_debug` (1-DM debug), `s15_three_layer_dual` (engine-untagged Lua-comparison variant), `s20_crosslayer` / `s21_multihop_xl` (0 gateways → non-functional cross-layer), `s22_location` (2-node/1-DM, too thin).
