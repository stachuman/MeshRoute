<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Gateway ⇄ leaf-config membership exemption (both directions)

**Status:** coder instruction (2026-07-05, from a metal gateway bug). The user commits + flashes; I quality-gate. **No wire change** — the gateway bit (`self_gateway`) is already on the beacon (frame_codec.h byte-2 b6). One file: `lib/core/node_beacon.cpp` (the R6.1 membership filter) + a dual-layer test.

## Why (the metal bug)
A gateway is **unmanaged** (`lineage_id == 0`) and bridges multiple leaves — it must **not** participate in the R6.1 leaf-config membership plane. Today it does, in **both** directions (`node_beacon.cpp` ingest, the `if (b.config_hash != 0)` filter ~:444-485):

- **Gateway side (:455-459):** the gateway hears a MANAGED neighbour (`my_lineage==0 && b.lineage_id!=0`) → **adopts the leaf's lineage** (`_cfg.lineage_id = b.lineage_id`) + **fires a config-pull** (`send_config_pull` = the stray `Q op=REQ_SYNC` seen on metal). This corrupts the gateway's own identity/config (very plausibly cascading into the wrong per-leaf `node_id`).
- **Leaf side (:462-463):** a MANAGED member hears the gateway (`b.lineage_id != my_lineage`) → `return` → **refuses to peer / learn a route** to it. So even after the gateway-side fix, managed leaves won't route to/from the gateway → it goes quiet-but-non-bridging.

Both stem from ONE gap: the gateway isn't exempt from the membership plane, in either direction. **Fix both in one change** — a gateway-side gate + a leaf-side gateway-neighbour exemption. The wire already carries what the leaf side needs (`b.self_gateway`).

## The fix — two carve-outs in the one filter block

In `node_beacon.cpp`, the membership filter opens `if (b.config_hash != 0) {`. Change it to:

```cpp
// R6.1 leaf-config membership filter (§3.3). §GW (metal 2026-07-05): a GATEWAY is exempt in BOTH directions —
//   (A) a gateway (is_gateway ≡ n_layers==2) is not a member of ANY leaf-config plane (it bridges multiple
//       leaves/lineages) -> skip the whole filter; peer by nibble, never adopt a lineage or fire a config-pull.
//   (B) a NON-gateway hearing a GATEWAY neighbour (b.self_gateway) must NOT refuse it: a gateway's lineage/config
//       never matches ours, so peer by nibble (fall through to route-learning) or managed members can't route to/from it.
if (b.config_hash != 0 && !_cfg.is_gateway) {          // (A) gateway-side: a gateway skips the filter entirely
    const uint16_t my_lineage = _cfg.lineage_id;
    const uint16_t my_hash    = cfg_config_hash();
    if (b.self_gateway) {
        // (B) leaf-side: peer with a gateway neighbour by nibble — empty body, fall through to route-learning.
    } else if (my_lineage == 0 && b.lineage_id == 0) {
        ... (unchanged: both-unmanaged config-match gate) ...
    } else if (my_lineage == 0 && b.lineage_id != 0) {
        ... (unchanged: unmanaged joins managed — adopt + pull) ...
    } else if (b.lineage_id != my_lineage) {
        ... (unchanged: foreign lineage -> return) ...
    } else {
        ... (unchanged: same-lineage epoch/hash reconcile) ...
    }
}
```

Exactly two edits:
1. **(A) `&& !_cfg.is_gateway`** on the outer `if`. A gateway falls straight through to route-learning (peer-by-nibble, legacy), never touching adopt/pull/refuse.
2. **(B) a new FIRST inner branch `if (b.self_gateway) { }`** (empty → fall through). A non-gateway node never refuses/adopts-from a gateway neighbour; it learns a route to it like any nibble-peer. Placed first so it short-circuits the lineage/config branches.

Together they're symmetric: neither the gateway (A) nor the members hearing it (B) run any membership logic against a gateway.

## Notes / caveats
- **⚠ Blanket exemption caveat (comment it):** (A) skips the WHOLE block, including the same-lineage epoch reconcile (:464+). Correct for **today's explicit-config gateway** (lineage 0, bridges by nibble). If a future `join_as_gateway` ever makes a gateway a *managed member* on one of its layers, (A) would wrongly skip that layer's config sync — revisit then. Add a one-line comment at (A) flagging this.
- **`self_gateway` availability:** decoded into `b.self_gateway` (frame_codec.h:84, byte-2 b6) and set on every gateway beacon (`node_beacon.cpp` `in.self_gateway = _cfg.is_gateway`), so it's valid at the filter. A gateway beacons `self_gateway=true` on whichever layer is active, so members on either leaf see it. No wire change.
- **Secondary symptoms (record, don't chase here):** the wrong-per-layer-id in the `Q` is the gateway firing `send_config_pull` with a single `_node_id` while it has per-layer ids — it **disappears** once (A) stops the gateway firing the pull. The metal `node_id=3` is plausibly the adopt/re-provision path re-writing per-leaf state; it should also clear with (A). **⚠ If `node_id=3` PERSISTS after this fix, it is a SEPARATE per-layer-id bug** (not this membership fix) — file it separately.

## Tests (`test/test_dual_layer.cpp`)
- **(A) gateway-side:** a gateway (n_layers==2, lineage 0) ingests a beacon from a MANAGED neighbour (`b.lineage_id != 0`, `b.config_hash != 0`). Assert: `_cfg.lineage_id` **stays 0** (no adopt), **no `leaf_join_pull` / config-pull** fired (spy the emit / `send_config_pull`), and the gateway **learns a route** to the neighbour (peer-by-nibble). Kills the self-corruption.
- **(B) leaf-side:** a MANAGED member (lineage != 0) ingests a beacon from a GATEWAY neighbour (`b.self_gateway = true`, divergent `b.lineage_id`/`config_hash`). Assert it **DOES peer** — `rt_find(gateway_src) != nullptr` (a route is learned), no early `return`. Kills the quiet-but-non-bridging half.
- Full native suite green; boards + **gateway env** build.

## Sites
`lib/core/node_beacon.cpp` (the two edits at the `if (b.config_hash != 0)` filter) · `test/test_dual_layer.cpp` (the two membership-exemption tests).
