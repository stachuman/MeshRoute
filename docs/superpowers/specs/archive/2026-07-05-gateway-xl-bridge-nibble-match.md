<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Gateway cross-layer bridge: match target by leaf nibble — DIRECTLY IMPLEMENTABLE

**Status:** coder instruction (2026-07-05, from a metal cross-layer **E2E-ACK that never bridged home**). The user commits + flashes; I quality-gate. **No wire change, no NV bump.** One line in `bridge_cross_layer` + a native test. **The blocker for the cross-layer E2E-ACK** (forward DM already delivers; the reverse ack was silently refused).

## Why (root cause — diagnosed live, then confirmed statically)
A cross-layer DM carries a layer-path (e.g. `[source_layer, target_layer]`). The originator seeds `path[0] = active_layer_id()` (node_mac.cpp:281 / :265). But `active_layer_id()` (node.h:478) returns `layers[i].layer_id`, and for a **single-layer node** `layers[0].layer_id = leaf_id` = the **leaf NIBBLE** (node.cpp:208; `leaf_id = layer_id & 0x0F`). So an originator on "layer 100" reports **4** (the nibble), not **100**.

Result — with a metal gateway holding **full** ids (layer0=100, layer1=102):
- **Forward works**: the DM's target is the user's full id (102); `bridge_cross_layer` matches `102 == layers[1].layer_id` → bridges → delivered. ✅
- **Reverse fails**: the 4e ack reverses the path to `[102, 4]` and targets `rev[1] = 4`; `bridge_cross_layer` matches `4` against the full ids `100/102`, finds **nothing** → `target_leaf < 0` → `xl_bridge_refused` (an `MR_EMIT`, invisible on the console). The ack never bridges home; the origin never gets its E2E confirm.

Live `[XLDBG]` trace confirmed the ack reaches the gateway and enters the bridge (`deliv … xl=1 dh=1 dst=<origin-key> ≠ myk`), then returns before the resolve = the `target_leaf < 0` refuse.

**This is metal-only:** the sim uses layer ids `< 16`, so nibble == full and the mismatch never appears (why s18/s09-style scenarios never caught it).

## The fix — align the bridge with the already-nibble-based gateway plane
The rest of the gateway plane **already keys on the leaf nibble**: `select_gateway_for_leaf` takes a leaf (nibble), and the 4e itself computes `target_leaf = rev[1] & 0x0F` (node_mac.cpp:343) to pick the reverse gateway. Only `bridge_cross_layer`'s `target_leaf` lookup compares the **full** id — the lone inconsistency. Fix it to match by nibble.

`lib/core/node_mac_rx.cpp`, `bridge_cross_layer` (~:805):
```cpp
// §xl-nibble-match (2026-07-05, metal): match by the LEAF NIBBLE, not the full 8-bit id. A single-layer originator
// reports active_layer_id() == leaf_id (the NIBBLE, since layers[0].layer_id = leaf_id when n_layers==1), so a reversed
// 4e path can carry a nibble (e.g. 4) where the gateway holds the full id (100). The nibble is the canonical wire
// identity (byte-0 filter); validate_gateway_layers (node.cpp:72) guarantees the two leaves have DISTINCT nibbles, so
// this is unambiguous — and it aligns with select_gateway_for_leaf + the 4e's own `rev[1] & 0x0F`.
for (uint8_t i = 0; i < _n_layers; ++i) if ((_cfg.layers[i].layer_id & 0x0F) == (target_layer_id & 0x0F)) { target_leaf = i; break; }
```
(Change is ONLY the loop condition: `_cfg.layers[i].layer_id == target_layer_id` → `(_cfg.layers[i].layer_id & 0x0F) == (target_layer_id & 0x0F)`.)

**Why it's safe / inert where it should be:**
- **Forward unchanged**: a full-id target (102) → nibble 6 → still matches layer1 (102 → nibble 6). Same `target_leaf`.
- **Reverse fixed**: nibble target (4) → matches layer0 (100 → nibble 4). `target_leaf = 0`, bridges home.
- **Sim byte-identical**: `bridge_cross_layer` only runs on a gateway (`n_layers >= 2`; single-layer s18 never reaches it), and cross-layer sims use ids `< 16` (nibble == full) → identical result. Confirm s18 + s09/s10/s15 unchanged-or-better.
- **Unambiguous**: `validate_gateway_layers` forbids two leaves sharing a nibble (`leaf_nibble_clash`).

## Tests (`test/test_dual_layer.cpp`)
- **Nibble target (the regression guard):** a gateway (`layers[0].layer_id=100`, `layers[1].layer_id=102`) bridges a cross-layer DM whose reversed path targets the **nibble** `4` (i.e. `ui.layer_ids[ui.cur] == 4`, `dst_hash` = a resolvable node on leaf 0). Assert it **resolves `target_leaf == 0`** and queues/handoffs (no `xl_bridge_refused reason=1`). Before the fix this refuses.
- **Full-id target still works:** the same bridge with `target_layer_id == 102` still resolves `target_leaf == 1` (forward path unaffected).
- **Round-trip (if the harness supports it):** originate a cross-layer DM `leaf-A → leaf-B` with `e2e_ack_req`, deliver, and assert the 4e reverse ack **bridges back** to leaf A (the `[XLDBG]`-observed failure, now passing).
- Full native suite green; boards + **gateway env**.

## Note / deferred (don't fix here)
The deeper inconsistency is that a **single-layer node identifies its layer by the nibble** (`layers[0].layer_id = leaf_id`) while gateways use full 8-bit ids — so cross-layer paths mix nibble (source) + full-id (target) entries. Matching by nibble at the bridge is the correct, minimal fix (the nibble is the wire identity, and the whole gateway-selection plane already uses it). A future normalization (paths carry nibbles end-to-end, or single-layer nodes learn their full layer id) is a **separate** design item — not needed for correctness.

## Sites
`lib/core/node_mac_rx.cpp` (the `bridge_cross_layer` target_leaf loop, ~:805) · `test/test_dual_layer.cpp`. **No wire, no NV.** ⚠ Two temporary `[XLDBG]` `_hal.log` probes are currently in `node_mac_rx.cpp` (the `deliv` line after the inner-parse in `do_post_ack`, and the `bridge` line in `bridge_cross_layer`) + `#include <cstdio>` — **remove all three** when landing this (they were the diagnostic that found it).
