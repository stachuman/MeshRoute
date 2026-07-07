<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Gateway config integrity: canonical id in the lease-persist + disable create/join on the gateway build — DIRECTLY IMPLEMENTABLE

**Status:** coder instruction (2026-07-06, from metal: a gateway's `node_id` recurs to both-layers-same after every reprovision+run, and `create`/`join` corrupted a gateway into a managed leaf). The user commits + flashes; I quality-gate. **No wire change, no NV bump.** Two independent fixes in `src/fw_main.cpp`.

## Fix 1 — the recurring `node_id` corruption (the id-persist fix MISSED this site)
`persist_cfg_if_needed()` (the loop-tail channel-counter-lease persist) derives the id from the **active-layer mirror**:
```cpp
const uint8_t id = g_node.node_id(), ...   // fw_main:1919 — g_node.node_id() = the ACTIVE leaf's id (flips 7↔8 with the window)
...
b.node_id = id; ...                        // fw_main:1949 — persists the mirror into nv.node_id
```
On a gateway this is pathological: `join_changed = (id != g_persist_id)` (:1921) fires on **every window switch** (the mirror flips), so `nv.node_id` **thrashes** (a flash write every ~8s, alternating the two leaves' ids). On the next boot `cfg.layers[0].node_id = nv.node_id` (:1720) = whatever was last written → both layers show the same id. The `2026-07-05-gateway-per-layer-id-persist-fix` updated the two *snapshot* sites (`handle_cfg_set` :480, `seed_blob_from_live` :932) but this site assigns the mirror through a local `id`, so it slipped the fix.

**Edit:** fw_main:1919 — `g_node.node_id()` → `g_node.canonical_node_id()`:
```cpp
const uint8_t id = g_node.canonical_node_id(), ep = g_node.claim_epoch(), jn = g_node.joined() ? 1 : 0;
```
`canonical_node_id()` (node.h:475) returns `layers[0].node_id` for a gateway (stable across window switches), `_node_id` for a single-layer node (the DAD id — unchanged behavior). After this: `id` is stable → `join_changed` no longer fires on a window switch → **no thrash, no corruption, and far fewer flash writes.** Single-layer nodes: `canonical_node_id() == _node_id`, so DAD-adopt persists exactly as before.

⚠ **Audit note for the coder:** grep for **every** `g_node.node_id()` feeding a persisted `b.node_id` (direct OR via a local), not just the literal `b.node_id = g_node.node_id()`. Confirm the only persist paths are :480, :932 (already canonical), :887 (gateway cmd, explicit `g.l0.node_id`), :1949 (this fix via :1919). :1493 (`make_cfg_extras`) is DISPLAY — leave it. :511 (`cfg set node_id <v>`) is an explicit operator value — leave it.

## Fix 2 — disable `create`/`join` on the gateway build
`create`/`join` are normal-node provisioning; on the gateway firmware (`MR_N_LAYERS >= 2`) they must refuse — mirroring how `gateway` already errors on the normal build (fw_main:863-865 "not_gateway_build"). Today the dispatch (fw_main:1414-1415) is ungated, so `create` on a gateway silently re-provisions it as a managed leaf (the metal lineage-23832 corruption).

**Edit:** wrap the dispatch (fw_main:1414-1415) with the build guard + a gateway-build error:
```cpp
#if MR_N_LAYERS < 2
    if (len >  5 && !strncmp(line, "join ", 5))    { handle_join(line + 5);    return true; }   // R6.3 provisioning verbs (normal-node)
    if (len >  7 && !strncmp(line, "create ", 7))  { handle_create(line + 7);  return true; }
#else   // gateway build: create/join are normal-node only -> refuse (use `gateway l0=… l1=…`)
    if ((len > 5 && !strncmp(line, "join ", 5)) || (len > 7 && !strncmp(line, "create ", 7))) {
        mrcon.println(F("> err gateway_build (create/join are normal-node only; use `gateway l0=<layer>:<node>:<sf>:<sfs> l1=…`)"));
        return true;
    }
#endif
```
(Optionally also `#if MR_N_LAYERS < 2` around the `handle_create`/`handle_join` *definitions* :983/:1015 to drop the dead code from the gateway image — not required for correctness; the dispatch guard is the behavioral fix.)

## Tests
- **Native (normal build, `-e native`):** unchanged — `create`/`join` still dispatch (the `#if MR_N_LAYERS < 2` branch), `canonical_node_id() == _node_id` for single-layer so `persist_cfg_if_needed` behaves identically. Full suite green (618).
- **Build gates:** `pio run -e gateway` compiles (the `#else` branch + `mrcon` in scope); `-e xiao_sx1262`/`heltec_v3`/`xiao_esp32s3` compile (the `#if` branch).
- **★ Bench (metal — the real proofs, USER):**
  1. **Fix 1:** reprovision a gateway (`gateway l0=100:7:… l1=102:8:…`), reboot, let it run through **several window switches** (>1 min), reboot again → `cfg` still shows `[cfg.layer0] node_id=7` / `[cfg.layer1] node_id=8` (no thrash to both-8). This is the exact recurring bug.
  2. **Fix 2:** on the gateway, `create …` / `join …` → `> err gateway_build …`, and `cfg` is unchanged (no lineage adopted).

## Operational (USER, once)
The affected gateway still holds a corrupted `nv.node_id` in NV. After flashing this fix, **reprovision once** (`gateway l0=100:7:7:6,7 l1=102:8:8:7,9 bw0=62.5 bw1=125 freq0=869 freq1=869`) — this time it will STAY correct across reboots/updates (Fix 1 stops the thrash). `factory_reset` also works but isn't required.

## Sites
`src/fw_main.cpp` — :1919 (canonical id in the lease persist) · :1414-1415 (gate create/join dispatch). **No wire, no NV, no lib/core change.**
