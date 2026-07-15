<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Gateway per-layer discovery — DIRECTLY IMPLEMENTABLE

**Status:** coder instruction (2026-07-05, from the metal gateway "layer 102 never learns the gateway" observation). The user commits + flashes; I quality-gate. **No wire change, no NV bump.** One struct move + 6 mechanical retargets. **Order:** independent of the membership + intra-layer-relay fixes; lands any time, but it *compounds* the membership fix (both are needed for the far leaf to learn the gateway promptly).

## Why
A dual-layer gateway time-multiplexes one radio and beacons **each leaf at its own window** (`window_switch_fire` → `maybe_emit_gateway_beacon`, node.cpp:465). The fast bootstrap that lets a leaf learn the gateway *immediately* is **discovery mode** (5 s cadence + full pages, `discovery_beacon_period_ms`). But discovery state is **node-global** (`_discovery_mode` etc., node.h:1073-1076), and it **exits on the boot leaf's bootstrap**: `maybe_exit_discovery` (node_beacon.cpp:809) trips when `_discovery_bcn_rx_count >= 3` (a **global** counter, node_beacon.cpp:568) **or** `_active->_rt_count >= 4`. A gateway boots on leaf 0 (node.cpp:250), hears leaf-0 neighbours, trips the global exit — *before leaf 1 ever gets a fast-cadence window*. Leaf 1 then falls to the **reactive-dirty** path (needs the gateway to hear a leaf-1 node first) or the **3-hour** `gw_announce_min_interval_ms` heartbeat → the far layer learns the gateway slowly or not at all. (Metal: layer 100 = boot leaf = instant; layer 102 = starved.)

## ★ Gateway-only BY CONSTRUCTION (the user's constraint — satisfied without an `is_gateway` gate)
The fix moves the 4 discovery fields from `Node` into `LayerRuntime` (per-leaf) and reads them through `_active`. Because **`is_gateway ≡ n_layers==2`**, a normal node has **exactly one leaf**, so `_active` is *always* `&_layers[0]` → per-leaf discovery is **identical** to the old node-global state. The per-leaf *divergence* (leaf 1 staying in discovery after leaf 0 exits) can only happen with ≥2 leaves — i.e. only on a gateway. **No `if (is_gateway)` branch is needed; the single-leaf equivalence is the scoping,** and the **s18 byte-identical gate proves it.** (This is exactly how `_last_beacon_ms` already lives in `LayerRuntime`, node.h:1293.)

## Edit 1 — move the 4 discovery fields `Node` → `LayerRuntime` (`lib/core/node.h`)
**DELETE** from `Node` (node.h:1073-1076): `_discovery_mode`, `_discovery_started_ms`, `_discovery_until_ms`, `_discovery_bcn_rx_count`.
**ADD** to `LayerRuntime`, next to `_last_beacon_ms` (~node.h:1293):
```cpp
// §per-layer discovery (2026-07-05): a GATEWAY bootstraps each leaf INDEPENDENTLY — the boot leaf must not trip the
// OTHER leaf out of fast-cadence discovery (node-global discovery starved leaf 1 → 3h heartbeat). A single-layer node
// has ONE leaf, so _active is always &_layers[0] => per-leaf ≡ the old node-global state (byte-identical). Gateway-only
// by construction (is_gateway ≡ n_layers==2), no is_gateway branch needed.
bool     _discovery_mode = false;
uint64_t _discovery_started_ms = 0;
uint64_t _discovery_until_ms = 0;
uint16_t _discovery_bcn_rx_count = 0;
```

## Edit 2 — `in_discovery()` accessor (node.h:575) → the active leaf
```cpp
bool in_discovery() const { return _active && _active->_discovery_mode; }   // per-active-leaf; _active-guard for pre-init safety
```
Every other `in_discovery()` reader (node.cpp:275/284/590/668/700, node_query.cpp:83/85, node_beacon.cpp:223) inherits per-active-leaf semantics through this accessor — **NO change at those sites.**

## Edit 3 — boot: enter discovery on EVERY leaf (`lib/core/node.cpp`, replace the block at ~:257-260)
```cpp
const uint64_t now_disc = _hal.now();
for (uint8_t i = 0; i < _n_layers; ++i) {                 // per-leaf: n_layers==1 (normal node) => leaf 0 only = UNCHANGED
    _layers[i]._discovery_started_ms   = now_disc;
    _layers[i]._discovery_mode         = (protocol::discovery_ms > 0);
    _layers[i]._discovery_until_ms     = now_disc + protocol::discovery_ms;
    _layers[i]._discovery_bcn_rx_count = 0;
}
```
⚠ Keep the SAME position (after `_n_layers` is set at :220 + the layers are init'd, before the `in_discovery()` reads at :275/:284). For a gateway this arms BOTH leaves' discovery at boot; leaf 1 then runs its own fast cadence in its own windows.

## Edit 4 — `restart_discovery()` (node.cpp, ~:348-351) → the active leaf
```cpp
_active->_discovery_started_ms   = now;
_active->_discovery_mode         = (protocol::discovery_ms > 0);
_active->_discovery_until_ms     = now + protocol::discovery_ms;
_active->_discovery_bcn_rx_count = 0;
```
(`restart_discovery` is the id-adopt/join rebuild, node_join.cpp:179 — restarts the active leaf. A gateway uses explicit ids so rarely hits it; a normal node has one leaf. Correct either way.)

## Edit 5 — `maybe_exit_discovery` (node_beacon.cpp:805-818) → the active leaf
Retarget every `_discovery_*` to `_active->_discovery_*`:
```cpp
void Node::maybe_exit_discovery([[maybe_unused]] const char* reason) {
    if (!_active->_discovery_mode) return;
    const uint64_t now = _hal.now();
    const bool timed_out = (_active->_discovery_until_ms > 0) && (now >= _active->_discovery_until_ms);
    if (_active->_discovery_bcn_rx_count >= protocol::discovery_min_bcn_rx ||
        _active->_rt_count >= protocol::discovery_min_routes || timed_out) {
        _active->_discovery_mode = false;
        // telemetry: heard_bcn = _active->_discovery_bcn_rx_count, elapsed_ms = now - _active->_discovery_started_ms
        ...
    }
}
```

## Edit 6 — beacon-RX count (node_beacon.cpp:568) → the active leaf
```cpp
if (in_discovery()) ++_active->_discovery_bcn_rx_count;   // per-leaf: a leaf-1 beacon counts toward leaf 1's bootstrap, not leaf 0's
```

## Edit 7 — steady-state reader (node_beacon.cpp:787-788) → the active leaf
```cpp
const bool steady_state = !in_discovery()
    && (now - _active->_discovery_started_ms >= protocol::beacon_boot_grace_ms);
```

## Tests
**Native (`test/test_dual_layer.cpp` — per-leaf independence):**
- A gateway (`n_layers==2`) after `on_init`: **both** leaves have `_discovery_mode==true`. Drive leaf 0 to its exit threshold (feed `discovery_min_bcn_rx` beacons while active on leaf 0, or seed `discovery_min_routes` leaf-0 routes) → `maybe_exit_discovery` → assert **leaf0 `_discovery_mode==false` but leaf1 `_discovery_mode==true`** (the boot leaf no longer starves the far leaf). Then `activate_layer(1)` → `in_discovery()==true`; seed leaf 1 → it exits independently. This is the core regression guard.
- Add a `LayerRuntime`-field accessor to the test harness (like `set_intra_relay`) to read/seed per-leaf `_discovery_mode`/`_discovery_bcn_rx_count`.

**Native (`test/test_node_r3.cpp` — single-layer UNCHANGED):**
- The existing discovery tests (boot enters discovery, exits on `discovery_min_bcn_rx`/`discovery_min_routes`/timeout, `req_sync_on_boot` gate, fast-cadence jitter) must pass **byte-for-byte unchanged** — a single-layer node has one leaf, so `_active->_discovery_*` ≡ the old node-global fields. If any single-layer discovery test changes behavior, the move is wrong.

**★ lus gate (BASELINE.md delivery breakdown):**
- **s18** (single-layer): **byte-identical / delivery-neutral** — the proof of "gateway-only." Anchor 101/113 (89%), `leaks 0`. (One leaf ⇒ the move is inert.)
- **s09 / s10** (2-layer): cross-layer 2/2 (100%) held; expect the **far leaf to learn the gateway as fast as the boot leaf** (both bootstrap in discovery). `bcn_discovery_exit` should now fire **per-leaf** (two events, one per leaf), not once.
- **s15 / s16** (3-layer / dense): cross-layer **≥ baseline** (s16 57/80, s15 multi-seed ≈90% over the documented seed set — gate on the MEAN, not seed 1522), `leaks 0`. Faster far-leaf bootstrap should hold-or-improve convergence, never regress.
- Boards + **gateway env** build.

## Sites
`lib/core/node.h` (Edit 1 field move, Edit 2 accessor) · `lib/core/node.cpp` (Edit 3 boot loop, Edit 4 restart) · `lib/core/node_beacon.cpp` (Edit 5 exit, Edit 6 count, Edit 7 steady-state) · `test/test_dual_layer.cpp` + `test/test_node_r3.cpp`. **No wire, no NV, no `is_gateway` branch.**
