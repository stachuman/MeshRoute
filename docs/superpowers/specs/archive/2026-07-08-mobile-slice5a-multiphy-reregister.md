<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Mobile v1 — Slice 5a: multi-PHY re-register (cross to a new layer) — DIRECTLY IMPLEMENTABLE

**Status:** coder instruction (2026-07-08). **Slice 5a of mobile v1** (design §11; activates 2b's deferred multi-PHY scan, B1). The user commits; I quality-gate. **This is how a mobile CROSSES layers:** when its home is lost and its current PHY has no host, the mobile cycles a configured **scan-set** of PHYs (freq/SF/BW/CR), DISCOVERing on each, until a host on some layer offers — then it **adopts that host's layer** (fixing the `home_leaf_id` gap) and stays tuned there. Slice 5b makes a cross-layer sender able to reach it.

## ★★ Static-safety
- The scan cycle runs **only** when `_cfg.is_mobile` AND the mobile is home-lost/unregistered (a settled mobile and every static node never enter it — the existing `mobile_discover_fire` guards hold).
- **The scan-set defaults to ONE entry — the mobile's own PHY** — so a single-layer mobile (s07/s21) does exactly what 2b does today (retune to its one PHY, DISCOVER). Multi-PHY behaviour appears only when a scenario configures ≥2 scan-set entries.
- s18 has no mobiles. **Gate:** native + **s18 fresh-md5 byte-identical** (`3ac88d40…`) + s07 unchanged (single-entry scan-set).

## Fix 1 — the scan-set config (`node.h` `NodeConfig`)
Reuse the `LayerConfig` shape (each entry is a PHY):
```cpp
LayerConfig mobile_scan_set[protocol::cap_mobile_scan_set];   // e.g. 4 — candidate PHYs the mobile cycles when home-lost
uint8_t     mobile_scan_set_n = 0;                              // 0 ⇒ derive a single entry from the mobile's own layer (§default)
```
`protocol_constants.h`: `cap_mobile_scan_set = 4;`. **Default (`_n==0`):** treat it as one synthetic entry = `_cfg.layers[0]` (freq/SF/BW/CR/layer_id) → single-PHY, 2b-identical.

## Fix 2 — cycle the scan-set (`node_mobile.cpp`)
Add mobile state (`node.h`): `uint8_t _mobile_scan_idx = 0;`.
- **`mobile_discover_fire`** (~:21) — before the DISCOVER, **retune to the current scan-set entry** (only if the mobile has >1 candidate, else no-op = 2b):
  ```cpp
  const LayerConfig& phy = scan_phy(_mobile_scan_idx);          // helper: mobile_scan_set[idx], or layers[0] when _n==0
  if (scan_set_count() > 1) {                                    // single-entry → stay put (byte-identical to 2b)
      _hal.set_rx_sf(phy.routing_sf);
      if (phy.freq_mhz > 0) _hal.set_rx_freq(phy.freq_mhz);
      _hal.set_rx_bw(phy.bw_hz ? phy.bw_hz : _cfg.radio_bw_hz);
      _hal.set_rx_cr(phy.cr ? phy.cr : _cfg.radio_cr);
  }
  // DISCOVER on THIS PHY's control SF (was _cfg.routing_sf):
  tx_initiating(buf, n, static_cast<int16_t>(phy.routing_sf), LbtKind::flood, 0);
  ```
- **`mobile_claim_guard_fire`** (~:40), the **no-offer** branch (~:42) — advance to the next PHY before the backoff:
  ```cpp
  if (_mobile_offers_n == 0) {
      _mobile_scan_idx = static_cast<uint8_t>((_mobile_scan_idx + 1) % scan_set_count());
      if (_mobile_scan_idx == 0) { /* full cycle done → exp backoff (existing) */ }
      (void)_hal.after(/* backoff only after a full cycle, else a short inter-PHY gap */, kMobileDiscoverTimerId);
      return;
  }
  ```
  (A full cycle with no host → the existing exp-backoff; within a cycle, a short inter-PHY gap so the scan sweeps promptly.)

## Fix 3 — adopt the HOST's layer, not `_cfg.leaf_id` (`node_mobile.cpp` `mobile_claim_guard_fire` ~:63-66)
The mobile found the host on `scan_phy(_mobile_scan_idx)` — that entry's `layer_id` IS the host's layer. Record it and **stay on that PHY** (already tuned from Fix 2):
```cpp
    const LayerConfig& phy = scan_phy(_mobile_scan_idx);
    _my_mobile_reg = { true, o.responder_id, o.proposed_local_id, o.responder_hash,
                       phy.layer_id,                       // §5a: the HOST's layer (was _cfg.leaf_id — the E1 source)
                       _my_mobile_reg.epoch, _hal.now() };
    // adopt the operating PHY so ongoing home traffic uses it (leaf_id/routing_sf/freq/bw/cr):
    adopt_mobile_phy(phy);                                 // set _cfg.leaf_id/_active leaf scalars + keep the radio tuned (mirror activate_layer's scalar+retune, single-leaf)
```
`adopt_mobile_phy(phy)` sets the mobile's active leaf scalars (`leaf_id`, `routing_sf`, `allowed_sf_bitmap`) and confirms the radio tune to `phy` — the mobile now **operates on the host's layer**. (For the single-entry default this is the mobile's own PHY → no-op.)

## Tests
- **Single-entry (2b-identical):** a mobile with `mobile_scan_set_n==0` (or 1) → `mobile_discover_fire` retunes to its one PHY (no-op) and DISCOVERs exactly as 2b; adopts `home_leaf_id == layers[0].layer_id`.
- **Multi-PHY:** a mobile with a 2-entry scan-set, a host reachable only on **entry 1** → the mobile cycles idx 0 (no host, advance) → idx 1 (host offers) → CLAIMs, and `home_leaf_id == scan_set[1].layer_id`, radio tuned to entry 1.
- **Adopt-layer:** after registering with a host on layer B, `_my_mobile_reg.home_leaf_id == B` (NOT the mobile's start layer).
- **★ Static/settled:** a registered mobile does not scan (home-heard refreshes `last_heard_home_ms`); a static node never enters `mobile_discover_fire`.

## Gate
- `pio test -e native` green (single-entry-identical + multi-PHY cycle + adopt-layer).
- **s18 fresh-md5 byte-identical** (`3ac88d40…`); **s07 unchanged** (single-entry scan-set → 2b behaviour).
- **Sim (a 2-PHY cross-layer scenario):** a mobile started on PHY-A, host only on PHY-B → the mobile cycles to B, registers, `home_leaf_id==B`. (Focused scenario; pairs with 5b for delivery.)
- 4 boards compile.

## Sites
`node.h`(`NodeConfig.mobile_scan_set[]`/`_n`; `_mobile_scan_idx`; `scan_phy`/`scan_set_count`/`adopt_mobile_phy` decls) · `protocol_constants.h`(`cap_mobile_scan_set`) · `node_mobile.cpp`(`mobile_discover_fire` retune+per-PHY SF; `mobile_claim_guard_fire` advance-on-no-offer + adopt host's layer) · `node.cpp`(`adopt_mobile_phy` — scalar set + retune, single-leaf) · tests. **Layer-aware DELIVERY is 5b. NO change to static / gateway layer switching.**
