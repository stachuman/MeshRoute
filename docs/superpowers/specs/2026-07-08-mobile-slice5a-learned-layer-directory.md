<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Mobile v1 — Slice 5a (REVISED): cross-layer re-register via a LEARNED layer directory — DIRECTLY IMPLEMENTABLE

**Status:** coder instruction (2026-07-08). **SUPERSEDES the static-scan-set 5a-v1** (`…mobile-slice5a-multiphy-reregister.md`). Design §11 (Cross-layer DISCOVERY). The user commits; I quality-gate. **A mobile LEARNS where to retune, it doesn't guess:** while connected it **pulls** a directory of neighbouring layers from a gateway (a new DM query/answer), then cycles that directory's PHYs on home-lost. The retune+cycle mechanics of 5a-v1 stay; **only the candidate list changes from a hand-configured `mobile_scan_set` to the learned directory.**

## What changes vs 5a-v1
- **REMOVE** `NodeConfig.mobile_scan_set[]`/`_n` (the static config) and its `cap_mobile_scan_set`.
- **KEEP** the cycle-and-adopt mechanics: `_mobile_scan_idx`, the per-PHY retune in `mobile_discover_fire`, `adopt_mobile_phy`, `mobile_claim_guard_fire` advance-on-no-offer (§5a-v1 Fix 2/3), and adopting the **host's** layer.
- **ADD** the pull: two DM TYPEs, a layer record, the mobile's learned directory, the query trigger, the gateway answerer.
- **The scan-set becomes:** `[the mobile's current/bootstrap PHY] ∪ [the learned directory]`. On boot (nothing learned) that's just its own configured PHY → **single-PHY behaviour is 2b-identical**; the neighbours appear only after a successful pull.

## ★★ Static-safety
- The query is sent only by a `_cfg.is_mobile` node; the answer is built only by a gateway (`n_layers==2`). The learned directory is mobile-only state. **No beacon/TLV change** — it's a **pull**, so zero per-beacon airtime and the existing cross-layer scenarios (s09/s15/s16) are untouched.
- s18 has no mobiles/gateways-with-mobiles. **Gate:** native + **s18 byte-identical** (`3ac88d40…`) + s09/s15 unchanged.

## Fix 1 — two DM TYPEs + the layer record (`frame_codec.h`)
Codes 8/9 taken → add **`DATA_TYPE_MOBILE_LAYER_QUERY = 10`** and **`DATA_TYPE_MOBILE_LAYER_ANSWER = 11`**.
- **QUERY body:** empty (or 1 flag byte, reserved) — "list the layers you bridge." It rides a normal DM with `SOURCE_HASH = M` (so the gateway can reply to the mobile) and `origin = home_id` (the mobile's stamp).
- **ANSWER body:** `[count u8]` then `count` × **layer record**:
  ```
  [layer_id u8][freq_khz u32 LE][sf u8][bw_hz u32 LE][name_len u8][name … up to leaf_name_max]
  ```
  `freq_khz` = MHz×1000 (868.1 MHz → 868100; u32 fits any LoRa band). This is the **composite network identity** (`layer_id + name + freq + SF + BW`) — see §11 (`layer_id` alone isn't unique across areas).

## Fix 2 — the learned directory (`node.h`)
```cpp
struct LayerRecord { uint8_t layer_id; uint32_t freq_khz; uint8_t sf; uint32_t bw_hz; uint8_t name_len; char name[protocol::leaf_name_max]; };
LayerRecord _learned_layers[protocol::cap_learned_layers];   // e.g. 4
uint8_t     _learned_layers_n = 0;
uint64_t    _learned_layers_ms = 0;                          // last refresh (TTL — the directory is long-lived but not forever)
```
`protocol_constants.h`: `cap_learned_layers = 4;`, `learned_layer_ttl_ms = 3600000;` (1 h — records are static). `mobile_layer_query_period_ms = 600000;` (10-min refresh while connected).

## Fix 3 — the mobile PULLs while connected (`node_mobile.cpp` + a timer)
When registered AND it knows a gateway (the type-4 TLV gave `gw_id → dest_leaf`), query it. Add a timer `kMobileLayerQueryTimerId` (armed while `_my_mobile_reg.active`):
```cpp
void Node::mobile_layer_query_fire() {
    if (!_cfg.is_mobile || !_my_mobile_reg.active) return;
    const int gw = nearest_bridging_gateway();               // from the learned gw-layer TLV entries (a gw with a route)
    if (gw >= 0) {
        uint8_t q = 0;                                        // empty/reserved body
        enqueue_data(static_cast<uint8_t>(gw), &q, 0, DATA_FLAG_SOURCE_HASH, "mobile_layer_query",
                     /*app_dm=*/false, DATA_TYPE_MOBILE_LAYER_QUERY, CryptIntent::off);   // SOURCE_HASH=M
    }
    (void)_hal.after(protocol::mobile_layer_query_period_ms, kMobileLayerQueryTimerId);   // refresh
}
```
Arm it on adopt (`mobile_claim_guard_fire`) alongside the existing re-CLAIM arm.

## Fix 4 — the gateway ANSWERS (`node_mac_rx.cpp` do_post_ack)
On `DATA_TYPE_MOBILE_LAYER_QUERY`, a **gateway** (`n_layers==2`) replies with its bridged layers. The reply routes to the mobile via the request's `origin` (=home_id) + `source_hash` (=M) — the same reply-addressing as the E2E-ack — so the home last-mile-forwards it:
```cpp
if (pa.type == DATA_TYPE_MOBILE_LAYER_QUERY && _cfg.n_layers == 2 && ui && ui->has_source_hash) {
    uint8_t body[ /* 1 + N*(11+name) */ ];  uint8_t off = 0;  body[off++] = 0;   // count, filled below
    uint8_t cnt = 0;
    for (uint8_t i = 0; i < _cfg.n_layers; ++i) {
        const LayerConfig& L = _cfg.layers[i];
        if (L.layer_id == /* the requester's current leaf, if known */) continue;   // skip the layer the mobile is already on (optional)
        off += pack_layer_record(body + off, L);  ++cnt;                            // [layer_id][freq_khz][sf][bw_hz][name]
    }
    body[0] = cnt;
    // reply: dst = origin (home_id), dst_hash = source_hash (M) → home last-miles to M (reuse the mobile-delivery path)
    enqueue_data(pa.origin, body, off, DATA_FLAG_DST_HASH, "mobile_layer_answer",
                 /*app_dm=*/false, DATA_TYPE_MOBILE_LAYER_ANSWER, CryptIntent::off, /*override_dst_hash=*/ui->source_hash);
    become_free(); return;
}
```
(A non-gateway ignores the query — drop. Only gateways know >1 layer.)

## Fix 5 — the mobile INGESTS the answer (`node_mac_rx.cpp` do_post_ack, at the mobile)
On `DATA_TYPE_MOBILE_LAYER_ANSWER` delivered to us (a mobile): parse the records → **union** into `_learned_layers` (dedup by the composite id; refresh `_learned_layers_ms`; evict oldest if full; skip our own current layer):
```cpp
if (pa.type == DATA_TYPE_MOBILE_LAYER_ANSWER && _cfg.is_mobile) {
    learned_layers_ingest(ui->body.data(), ui->body.size());   // parse [count][record…], upsert by (layer_id,name,freq,sf,bw)
    become_free(); return;
}
```

## Fix 6 — the scan cycles the directory + the mobile stores its home's FULL record (`node_mobile.cpp` + `node.h`)
- `scan_phy(idx)` / `scan_set_count()` now source from **`[current PHY] ∪ _learned_layers`** instead of `_cfg.mobile_scan_set` (index 0 = the mobile's current/bootstrap layer `_cfg.layers[0]`; 1..n = `_learned_layers`). Everything else in `mobile_discover_fire`/`mobile_claim_guard_fire` is unchanged from 5a-v1.
- **`_my_mobile_reg`** records the home's **full layer record** (add the composite fields, not just `home_leaf_id`) so a repeated `layer_id` across areas can't confuse it; `home_leaf_id` (the nibble) stays for intra-mesh routing (5b).

## Tests
- **Codec:** `pack_layer_record`/parse round-trips a `LayerRecord`; the ANSWER `[count][record…]` round-trips N records.
- **Gateway answer:** a gateway fed a `MOBILE_LAYER_QUERY` (source_hash=M) → emits a `MOBILE_LAYER_ANSWER` (dst=origin, dst_hash=M) listing its layers with correct freq/SF/BW/name. A non-gateway → ignores.
- **Mobile ingest:** feeding a mobile an ANSWER with 2 records → `_learned_layers_n==2`, deduped; a repeated record refreshes not duplicates.
- **Scan from directory:** with `_learned_layers=[layer B]`, a home-lost mobile cycles its current PHY (no host) → retunes to B (from the directory) → DISCOVERs on B's SF. Empty directory → single-PHY (its own layer only) = 2b behaviour.
- **★ Static regression:** s18 (no mobiles) + s09/s15 (gateways, no mobiles) — no query/answer, no directory, byte-identical/0-failures.

## Gate
- `pio test -e native` green (codec + gateway-answer + mobile-ingest + scan-from-directory + static regression).
- **s18 byte-identical** (`3ac88d40…`); **s09/s15/s16 0-failures**.
- **Sim (the cross-layer-mobile e2e, still needed):** a 2-layer + gateway scenario where a mobile pulls the directory, then re-registers on the far layer and is reachable (pairs with 5b). *(The e2e scenario is the outstanding integration item flagged at 5a/5b gating.)*
- 4 boards compile.

## Sites
`frame_codec.h/.cpp`(`DATA_TYPE_MOBILE_LAYER_QUERY=10`/`_ANSWER=11`; `pack/parse_layer_record`) · `node.h`(`LayerRecord`, `_learned_layers[]`/`_n`/`_ms`; timer `kMobileLayerQueryTimerId`; drop `mobile_scan_set`) · `protocol_constants.h`(`cap_learned_layers`, TTLs; drop `cap_mobile_scan_set`) · `node_mobile.cpp`(`mobile_layer_query_fire`; `scan_phy`/`scan_set_count` from directory; `_my_mobile_reg` full record) · `node_mac_rx.cpp`(do_post_ack QUERY answer + ANSWER ingest) · `node.h`(`nearest_bridging_gateway`, `learned_layers_ingest`) · tests. **Removes the 5a-v1 static scan-set. 5b (delivery) unchanged.**
