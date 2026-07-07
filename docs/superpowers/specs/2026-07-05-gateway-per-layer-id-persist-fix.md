<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Gateway per-layer node_id persist fix — DIRECTLY IMPLEMENTABLE

**Status:** coder instruction (2026-07-05, from a metal gateway whose layer0 node_id got clobbered 4→5 after reconfig). The user commits + flashes; I quality-gate. **No wire change, no NV bump.** A tiny accessor + two call-site swaps + a native test. **Independent** of the four prior gateway/MAC specs; this is the "separate per-layer-id bug" the handover flagged.

## Why (the metal bug — confirmed root cause)
A dual-layer gateway has **distinct per-layer ids** (layer0=4 on layer 100, layer1=5 on layer 102). The persisted `nv.node_id` is meant to hold **layer0's canonical id (4)** — the restore does `cfg.layers[0].node_id = nv.node_id` (fw_main.cpp:1720). But the NV snapshots write the **active-mirrored** id instead:
- `activate_layer` (node.cpp:411): `_node_id = L.node_id` — so `_node_id` is **5 while the window is on layer1**, 4 on layer0.
- `Node::node_id()` returns that active `_node_id`.
- **Persist sites** `b.node_id = g_node.node_id()` fire while the gateway may be on **layer1** → `nv.node_id = 5`, clobbering layer0's 4.
- Next boot: `layers[0].node_id = nv.node_id = 5` → **both layers show id 5.**

**The trigger:** `handle_cfg_set` (fw_main.cpp:441) snapshots the whole blob — including `b.node_id = g_node.node_id()` — on **every `cfg set`**. The window switches every ~7-8s, so a `cfg set` during provisioning lands on layer1 with high probability → corruption. (This is why it appeared right after reflash + reconfig.)

**Why single-layer must stay on `node_id()`:** `set_identity` (DAD, node.cpp:38) updates only `_node_id`; `layers[0].node_id` is written in just 3 places, none on DAD. So a single-layer node that DAD-adopts a new id has a **stale** `layers[0].node_id` — it genuinely needs `_node_id` for persistence. A gateway uses **explicit** ids (no DAD) and `activate_layer` never writes back into `layers[i].node_id`, so `layers[0].node_id` is the stable canonical value. The fix must therefore be **role-aware**.

## Edit 1 — add a canonical-id accessor (`lib/core/node.h`, next to the other public accessors)
```cpp
// The id to PERSIST as nv.node_id (restore maps it to layers[0].node_id). A GATEWAY's node_id() is the ACTIVE-leaf
// mirror (activate_layer stamps _node_id = _active->node_id), which flips 4↔5 with the window — persisting it clobbers
// layer0's canonical id. layers[0].node_id is the stable, explicit gateway id (no per-leaf DAD writes it back). A
// single-layer node has NO per-leaf id and DAD updates only _node_id, so it must persist _node_id. §per-layer-id 2026-07-05.
uint8_t canonical_node_id() const { return _cfg.is_gateway ? _cfg.layers[0].node_id : _node_id; }
```

## Edit 2 — persist the canonical id at the two NV snapshot sites (`src/fw_main.cpp`)
Both sites currently read `b.node_id = g_node.node_id();`. Change **both** to:
```cpp
b.node_id = g_node.canonical_node_id();
```
- **fw_main.cpp:480** — `handle_cfg_set` (the `cfg set` blob snapshot; the primary corruption trigger).
- **fw_main.cpp:932** — `seed_blob_from_live` (fresh-blob seed; belt-and-suspenders — normally a single-layer path, but must be correct if a gateway ever hits it).

**Do NOT change fw_main.cpp:1493** (`make_cfg_extras` → `x.node_id = g_node.node_id()`): that feeds the **`cfg` display**, where showing the active-leaf id is correct (the per-layer ids are printed separately as `[cfg.layer0]`/`[cfg.layer1]`). It is not persisted. Leave it.

The `gateway` command's own persist (fw_main.cpp:887, `b.node_id = g.l0.node_id`) is already correct — it writes layer0's id directly. `b.l1_node_id` is untouched by this bug (persisted from the gateway command; the snapshots don't overwrite it).

## Tests (`test/test_dual_layer.cpp`)
- **Gateway invariance (the core guard):** build a gateway (`n_layers==2`, `layers[0].node_id=4`, `layers[1].node_id=5`). Assert `canonical_node_id() == 4`. Then `activate_layer(1)` (so `node_id() == 5`, active on layer1) and assert **`canonical_node_id()` is STILL 4** (independent of the active window — this is exactly the corruption condition). `activate_layer(0)` → still 4. This pins that a persist-while-on-layer1 records layer0's id, not the mirror.
- **Single-layer regression (`test/test_node_r3.cpp`):** a single-layer node with `node_id()==X` (incl. after a DAD `set_identity(Y)`), assert `canonical_node_id() == node_id()` (== the current `_node_id`, i.e. the DAD-adopted id) — the single-layer persist is unchanged.
- Full native suite green; boards + **gateway env** build.

## Operational note (call out to the user)
This stops **future** corruption. The already-persisted `nv.node_id=5` means the affected gateway must be **re-provisioned once** (re-run the `gateway` command with `l0=` id **4**) to restore `layers[0].node_id=4`; after that the canonical persist keeps it stable across `cfg set`/reboot.

## Sites
`lib/core/node.h` (Edit 1, `canonical_node_id()`) · `src/fw_main.cpp` (Edit 2, sites 480 + 932 only; NOT 1493) · `test/test_dual_layer.cpp` + `test/test_node_r3.cpp`. **No wire, no NV, display untouched.**
