// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>

# Gateway per-layer bandwidth (+ coding-rate) — design

**Date:** 2026-07-04
**Status:** DESIGN (for review, then a plan → implementation)

## Goal
A dual-layer gateway time-multiplexes one radio across two layers. A "layer" is already a **`(freq, SF, leaf)` channel** — `freq_mhz` and `routing_sf`/`allowed_sf_bitmap` are per-layer, and the gateway retunes them on each window switch. **Bandwidth and coding-rate are the last shared PHY params.** This makes a layer a fully independent **`(freq, SF, BW, CR)` channel**, so the two layers can run on different bandwidths (e.g. a wide-BW fast layer + a narrow-BW long-range layer).

## Current state (verified 2026-07-04)
- **Per-layer already:** `LayerConfig` carries `routing_sf`, `allowed_sf_bitmap`, `freq_mhz` (`node.h:48-51`; `0 = inherit` the global freq). The window switch retunes per-layer: `set_rx_sf(L.routing_sf)` + `if (L.freq_mhz>0) set_rx_freq(L.freq_mhz)` (`node.cpp:396-397`). NV persists `l1_freq_mhz` (v12).
- **Global (shared) today:** `NodeConfig.radio_bw_hz = 250000` and `radio_cr` (`node.h:136`). There is **no** `LayerConfig.bw_hz`/`cr`, no `set_rx_bw`/`set_rx_cr`, no BW/CR retune on the window switch. `validate_gateway_layers(l0, l1, radio_bw_hz, radio_cr)` takes the single global BW/CR for its airtime checks.
- **The airtime model reads the global scalar in ~13 sites** — every `airtime_ms(sf, _cfg.radio_bw_hz, _cfg.radio_cr, …)` call (node_mac.cpp ×7, node_mac_rx.cpp ×3, node.cpp ×2, node_routing.cpp ×1 = `channel_capacity_C`'s `T_ch`). BW/CR feed duty, the anti-spam channel cap, LBT, NAV, and every frame's airtime.

## Decisions (2026-07-04, user)
1. **BW + CR both become per-layer** (a full per-layer PHY, not BW-only) — each layer an independent `(freq, SF, BW, CR)` channel.
2. **A single `active_bw_hz()` / `active_cr()` accessor** is the source of truth (NOT mutating `_cfg.radio_bw_hz` on the window switch). Every airtime site reads the accessor; mirrors how the active SF/freq already flow. One place to be right, no "current value" mutation muddying the config/live boundary.

## Design

### 1. `LayerConfig` gains BW + CR (mirror `freq_mhz`)
```cpp
uint32_t bw_hz = 0;   // per-layer bandwidth; 0 = inherit the node's global radio_bw_hz
uint8_t  cr    = 0;   // per-layer coding-rate (5..8); 0 = inherit the node's global radio_cr
```
Layer 0 keeps using the global `radio_bw_hz`/`radio_cr` (0 = inherit, exactly like layer-0 freq = `freq_mhz`). A single-layer node is unaffected (its sole layer inherits).

### 2. `active_bw_hz()` / `active_cr()` — the accessor (the load-bearing piece)
★ **CORRECTED (review 2026-07-04):** `bw_hz`/`cr` live in `_cfg.layers[]` (CONFIG), but `_active` is a `LayerRuntime*` into the SEPARATE `_layers[]` (RUNTIME) array — so `_active->bw_cfg` doesn't exist. The correct idiom is the one `active_layer_id()` already uses (`node.h:466`): map the runtime pointer back to the config index. **Config-read, no config→runtime duplication (chosen; the alternative — copy into the runtime at the swap — is rejected to avoid a second source of truth):**
```cpp
uint32_t active_bw_hz() const {
    const uint32_t b = _cfg.layers[static_cast<size_t>(_active - &_layers[0])].bw_hz;   // runtime ptr -> config index
    return b > 0 ? b : _cfg.radio_bw_hz;                                                 // 0 = inherit the global
}
uint8_t active_cr() const {
    const uint8_t c = _cfg.layers[static_cast<size_t>(_active - &_layers[0])].cr;
    return c > 0 ? c : _cfg.radio_cr;
}
```
- Returns the **active** layer's BW/CR (falling back to the global for `0`/single-layer).
- **Replace all ~13 `_cfg.radio_bw_hz`/`_cfg.radio_cr` airtime reads with `active_bw_hz()`/`active_cr()`.** ⚠ Grep to catch every one — a missed site debits the wrong layer's airtime into the duty/anti-spam ledger. (Config-plumbing reads — `set_radio_cfg`, `validate_gateway_layers`, the NV load — stay on the scalar/per-layer field; only the *airtime* reads move to the accessor.)

### 3. HAL retune on the window switch
Add `set_rx_bw(uint32_t)` + `set_rx_cr(uint8_t)` to `IRadio`/`DeviceHal` — standby-latch, mirroring `set_rx_freq` (SX1262 `SetModulationParams` latches SF **and** BW **and** CR only in STANDBY; the L9/M11 lesson). Wire into the window switch after the SF/freq retune (`node.cpp:396-397`):
```cpp
_hal.set_rx_sf(L.routing_sf);
if (L.freq_mhz > 0) _hal.set_rx_freq(L.freq_mhz);
if (L.bw_hz    > 0) _hal.set_rx_bw(L.bw_hz);      // per-layer BW retune
if (L.cr       > 0) _hal.set_rx_cr(L.cr);         // per-layer CR retune
```
(Consider a single `set_rx_modparams(sf, bw, cr)` to latch all three in one standby cycle — fewer standby round-trips — as a plan-time refinement.)

### 4. TX side — ★ pin the BW/CR source, then migrate it (the plan MUST list these sites explicitly)
The `_hal.tx()` call sites (node_mac.cpp:739/774/909/943, …) pass a `TxParams` with only `.sf` set — **BW/CR do NOT currently ride the per-TX path.** So the plan must first DETERMINE how BW/CR reach the on-air frame:
- **(a) via the HAL's stored `_def_bw`/`_def_cr`** (set by `configure()` at boot and — if we add it — by the window-switch `set_rx_bw`/`set_rx_cr` retune). If so, the TX side is handled *entirely* by the window-switch retune (the HAL already holds the active layer's BW/CR), and there's NOTHING to change at the `_hal.tx` sites. This is the likely + cleanest case and it makes the consistency invariant hold by construction.
- **(b) via a per-TX param** (`TxParams` grows `bw`/`cr`, filled from the accessor at each `_hal.tx` site). Only if (a) doesn't hold.
The plan MUST verify which, and **list every affected site explicitly** — these may NOT overlap the ~13 airtime-*read* sites. ★ **Consistency invariant (the keystone):** the airtime a frame is *charged* (`active_bw_hz()`) MUST equal the BW it *transmits* on. Whichever of (a)/(b), the plan proves this holds.

### 5. NV persistence (v16 → v17)
Append `uint32_t l1_bw_hz` + `uint8_t l1_cr` to `Blob` (0 = inherit; mirrors `l1_freq_mhz`). Bump `kVersion` 16 → 17. Layer-0 BW/CR remain the existing global `bw_hz`/`cr` NV fields. (Old-blob size-reject → defaults → re-provision, the dev convention.)

### 6. Config wire (the `l0=/l1=` gateway command)
Extend `parse_gateway_cmd` with `[bw0=kHz] [bw1=kHz] [cr0=] [cr1=]` (alongside `freq0`/`freq1`). ★ bw is **kHz (fractional, e.g. 62.5)** to match `create`/`join` — converted to Hz internally (`kHz*1000`). (`cfg set bw`/`cfg set l1_bw` deliberately stay Hz — the low-level cfg interface, per its existing convention.) `validate_gateway_layers` takes the per-layer BW/CR from the `LayerConfig`s (not the single global args) for its airtime/duty feasibility checks — each layer validated against its own PHY.

### 7. ★ Per-site verification — REQUIRED (walk every airtime site at plan time)
`active_bw_hz()` returns the **currently-active** layer's BW. That is correct ONLY IF the site costs a frame flying on the active layer. Most airtime sites are TX/RX on `_active` and are fine — but the plan MUST walk all ~13 (+ the paired-SF reads) and confirm each computes at a swap-time-correct `_active`. Two concrete hazards found in review:

1. **★ `max_data_sf()` / `max_data_sf_index()` read the GLOBAL `_cfg.allowed_sf_bitmap` (node_mac.cpp:419/426), NOT the active layer's `_cfg.layers[…].allowed_sf_bitmap`.** So any site that pairs `active_bw_hz()` with `max_data_sf()` — notably **`channel_capacity_C`'s `T_ch`** — would mix the active layer's BW with **layer-0's** SF → an inconsistent airtime. The plan MUST resolve this per site: either (i) make `max_data_sf()` active-layer-aware too (read `_cfg.layers[_active-&_layers[0]].allowed_sf_bitmap`), or (ii) prove the site is not reached per-layer on a gateway — e.g. `channel_capacity_C` is the CHANNEL cap and a dual-layer gateway is OUT of the channel plane (`channel_origin_admit` early-returns at `n_layers==2`), so it may be inert on a gateway. Decide + document per site; do NOT assume.
2. **Cross-layer bridge / handoff cost-at-the-wrong-time.** If a DM destined for layer 1 is costed (duty/airtime debit) while `_active` is still layer 0, the accessor returns layer-0's BW. Scrutinize the bridge/handoff path (`bridge_cross_layer`, `drain_xl_handoffs_for_leaf`, the deferred-handoff airtime) — confirm the debit happens when `_active` is the frame's target layer, or thread the target layer's BW explicitly at those sites.

This audit is the crux of correctness; the plan produces a per-site table (site · frame's layer · is `_active` correct there · fix if not).

## Edge cases / invariants
- **Single-layer node:** `_active->bw_cfg == 0` → `active_bw_hz()` returns `_cfg.radio_bw_hz` — byte-identical behavior. No regression.
- **Layer 0 of a gateway:** inherits the global (0) unless explicitly set — so an existing dual-layer gateway with no BW/CR override behaves exactly as today.
- **The airtime consistency invariant** (§4) is the correctness keystone: charge == transmit BW.
- **Validation:** a per-layer BW/CR must be a legal SX1262 value (BW ∈ the valid set, CR 5..8) AND the layer's duty must still be feasible at its BW (a narrow BW = longer airtime = tighter duty).

## Test plan
- `active_bw_hz()`/`active_cr()` return the active layer's value, global fallback on 0, single-layer == global.
- Airtime: a frame on a layer with a distinct BW is charged that layer's airtime (not the global) — assert via the duty ledger / `airtime_ms` on a 2-layer config.
- NV v17 round-trip (l1_bw_hz/l1_cr survive) + old-blob reject.
- `parse_gateway_cmd` round-trip with `bw0/bw1/cr0/cr1`; `validate_gateway_layers` rejects an illegal per-layer BW/CR.
- The window-switch retune calls `set_rx_bw`/`set_rx_cr` on a layer with an override (via a HAL spy in the dual-layer test).
- **Bench (gateway):** two layers on different BWs — traffic on each delivers, and the duty accounting per layer matches the on-air airtime.
- Native suite green; boards + gateway envs build.

## Proposed slices (for the plan)
1. `LayerConfig.bw_hz/cr` + `active_bw_hz()/active_cr()` accessors (config-index idiom) + **the §7 per-site audit** (produce the per-site table; resolve `max_data_sf()` active-ness + the bridge/handoff cost-timing) + migrate the ~13 airtime reads. Native-testable; **single-layer parity is the gate** (accessor returns the global → byte-identical, verify on s18).
2. HAL `set_rx_bw/set_rx_cr` (or `set_rx_modparams`) + the window-switch retune + the TX-side active BW/CR (board-build; dual-layer HAL-spy test).
3. NV v17 (l1_bw_hz/l1_cr) + `parse_gateway_cmd` (bw0/bw1/cr0/cr1) + `validate_gateway_layers` per-layer (native + board).
4. Verification: dual-layer airtime-per-layer test + full-suite + gateway-env builds.
