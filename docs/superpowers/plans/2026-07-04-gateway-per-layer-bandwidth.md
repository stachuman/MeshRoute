// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>

# Gateway per-layer bandwidth (+ CR) — implementation plan

> **For agentic workers:** implement task-by-task; steps use `- [ ]` checkboxes. Native uses `CHECK` only (`-fno-exceptions`, no `REQUIRE`); read the REAL result from `.pio/build/native/program 2>&1 | grep "Status:"`. The USER commits each slice.

**Goal:** Make each gateway layer an independent `(freq, SF, BW, CR)` channel — bandwidth + coding-rate become per-layer, read via an `active_bw_hz()`/`active_cr()` accessor.

**Architecture:** `LayerConfig` gains `bw_hz`/`cr` (0=inherit). The ~13 airtime reads of `_cfg.radio_bw_hz`/`_cfg.radio_cr` move to `active_bw_hz()`/`active_cr()` (config-index read of the active layer, global fallback). The window switch retunes BW/CR on the radio AND in the HAL's `_def_bw`/`_def_cr` (which is what TX already uses — so TX needs no per-site change). NV v16→v17 + the `l0=/l1=` config wire carry it.

**Audit basis (2026-07-04, in the spec §7):** all 13 airtime sites are `active-correct`; `max_data_sf()` is already active-coherent via the swap-mirrored `_cfg.allowed_sf_bitmap` scalar (NO change); the bridge path costs nothing at bridge time; TX is HAL-`_def_bw`-driven (case a). So this plan is a clean mechanical migration + the retune, with no `max_data_sf` or tx-site work.

**Tech stack:** C++20, PlatformIO. Native tests (`pio test -e native`) + board builds (`pio run -e xiao_sx1262`, `-e gateway`).

---

## SLICE 1 — `LayerConfig.bw_hz/cr` + the accessor + migrate the 13 airtime reads

Single-layer parity is the gate: on a 1-layer node the accessor returns the global, so behavior is byte-identical (verify on s18).

### Task 1: `LayerConfig` gains `bw_hz` + `cr`
**Files:** Modify `lib/core/node.h` (`struct LayerConfig`, ~:43-56, next to `freq_mhz` @:50)
- [ ] **Step 1: failing test** — in `test/test_dual_layer.cpp`, assert a fresh `LayerConfig` has `bw_hz == 0 && cr == 0` (0 = inherit):
```cpp
TEST_CASE("LayerConfig carries per-layer bw_hz/cr defaulting to 0 (inherit)") {
    meshroute::LayerConfig L{};
    CHECK(L.bw_hz == 0u);
    CHECK(L.cr == 0);
}
```
- [ ] **Step 2: run, expect FAIL** — `pio test -e native`: compile error (no member `bw_hz`).
- [ ] **Step 3: implement** — add after `freq_mhz` (node.h:50):
```cpp
    uint32_t bw_hz = 0;   // per-layer bandwidth (Hz); 0 = inherit the node's global radio_bw_hz
    uint8_t  cr    = 0;   // per-layer coding-rate (5..8); 0 = inherit the node's global radio_cr
```
- [ ] **Step 4: run, expect PASS** — `pio test -e native` → `Status: SUCCESS!`.
- [ ] **Step 5: commit** — `git add lib/core/node.h test/test_dual_layer.cpp && git commit -m "per-layer-bw: LayerConfig gains bw_hz/cr (0=inherit)"`

### Task 2: `active_bw_hz()` / `active_cr()` accessors
**Files:** Modify `lib/core/node.h` (near `active_layer_id()` @:466)
- [ ] **Step 1: failing test** — in `test/test_dual_layer.cpp`, a 2-layer node with `layers[1].bw_hz=125000, cr=8`, `layers[0]` left 0: on layer 0, `active_bw_hz()==_cfg.radio_bw_hz`; after `set_active(node,1)`, `active_bw_hz()==125000 && active_cr()==8`. Single-layer node → global.
```cpp
TEST_CASE("active_bw_hz/active_cr return the ACTIVE layer's value, global fallback on 0") {
    StubHal hal; Node node(hal, 1, 0x1);
    NodeConfig cfg; cfg.n_layers = 2; cfg.radio_bw_hz = 250000; cfg.radio_cr = 5;
    cfg.layers[0] = good_layer(1, 8);                     // bw_hz/cr left 0 -> inherit
    cfg.layers[1] = good_layer(2, 9); cfg.layers[1].bw_hz = 125000; cfg.layers[1].cr = 8;
    CHECK(node.on_init(cfg));
    CHECK(node.active_bw_hz() == 250000u); CHECK(node.active_cr() == 5);   // layer 0 active, inherit
    DualLayerTestAccess::set_active(node, 1);
    CHECK(node.active_bw_hz() == 125000u); CHECK(node.active_cr() == 8);   // layer 1 override
}
```
- [ ] **Step 2: run, expect FAIL** — no `active_bw_hz`.
- [ ] **Step 3: implement** — add after `active_layer_id()` (node.h:466), using the SAME runtime→config index idiom:
```cpp
    uint32_t active_bw_hz() const {
        const uint32_t b = _cfg.layers[static_cast<size_t>(_active - &_layers[0])].bw_hz;
        return b > 0 ? b : _cfg.radio_bw_hz;                 // 0 = inherit the global
    }
    uint8_t active_cr() const {
        const uint8_t c = _cfg.layers[static_cast<size_t>(_active - &_layers[0])].cr;
        return c > 0 ? c : _cfg.radio_cr;
    }
```
- [ ] **Step 4: run, expect PASS**.
- [ ] **Step 5: commit** — `git add lib/core/node.h test/test_dual_layer.cpp && git commit -m "per-layer-bw: active_bw_hz()/active_cr() accessors (config-index idiom)"`

### Task 3: migrate the 13 airtime reads to the accessor
**Files:** Modify `lib/core/node_mac.cpp` (7: :44, :630, :640, :867, :979, :1062, :1082), `lib/core/node_mac_rx.cpp` (3: :100, :124, :411), `lib/core/node.cpp` (`exchange_airtime_ms` @:602), `lib/core/node_routing.cpp` (`channel_capacity_C` @:105). ⚠ Leave `validate_gateway_layers`'s scalar arg (node.cpp:198) — that's config-plumbing (Slice 3).
- [ ] **Step 1: guard test (parity)** — add a native test asserting an `airtime_ms`-derived value on a 1-layer node is UNCHANGED (the accessor == global). The real gate is s18 sim parity in Step 4b; add a direct assert too:
```cpp
TEST_CASE("single-layer node: active_bw_hz() == the global radio_bw_hz (migration parity)") {
    TestHal hal; Node node(hal, 2, 0xBEEF); NodeConfig cfg = basic_cfg(); cfg.radio_bw_hz = 250000; node.on_init(cfg);
    CHECK(node.active_bw_hz() == 250000u);   // sole layer inherits -> reads identical to _cfg.radio_bw_hz
}
```
- [ ] **Step 2: implement** — at EACH of the 13 sites, replace `_cfg.radio_bw_hz` → `active_bw_hz()` and `_cfg.radio_cr` → `active_cr()`. `grep -rn "_cfg.radio_bw_hz\|_cfg.radio_cr" lib/core/*.cpp` must show ONLY `set_radio_cfg` (node.h — the writer), the accessor bodies (the fallback), and `validate_gateway_layers`'s call site remaining. Example (node_mac.cpp:44):
```cpp
    return airtime_ms(_cfg.routing_sf, active_bw_hz(), active_cr(), protocol::preamble_sym, len);
```
Add a one-line comment at `max_data_sf()` (node_mac.cpp:418) documenting the load-bearing dependency the audit found:
```cpp
    // NOTE (per-layer-bw): reads the SWAP-MIRRORED _cfg.allowed_sf_bitmap (activate_layer stamps the active layer's
    // bitmap here), so it is active-layer-coherent WITH active_bw_hz(). If a refactor ever freezes this scalar at
    // layer-0, that coherence breaks — keep the mirror.
```
- [ ] **Step 3: run native, expect PASS** — `pio test -e native` → SUCCESS (count unchanged + the 2 new).
- [ ] **Step 4a: build** — `pio run -e xiao_sx1262` SUCCESS (device compiles).
- [ ] **Step 4b: ★ s18 sim-parity gate (BYTE-IDENTICAL)** — the migration must be a no-op on single-layer s18. Rebuild lus (`cd ~/lora-universal-simulator/build && cmake --build . --target lus -j8`), run `lus -e meshroute simulation/s18_meshroute.json /tmp/s18_new.ndjson`, and confirm `events emitted` + `data_rx` counts EQUAL the pre-change baseline (stash Slice 1, rebuild lus, run, compare — same A/B method as the L9 gate). ⚠ If lus fails to build on a stale `channel_origin_max_per_window`-style ref, that's the known cross-repo drift — fix `NodeRuntimeWrapper.cpp` first.
- [ ] **Step 5: commit** — `git add lib/core/node_mac.cpp lib/core/node_mac_rx.cpp lib/core/node.cpp lib/core/node_routing.cpp test/... && git commit -m "per-layer-bw: migrate the 13 airtime reads to active_bw_hz()/active_cr() (single-layer s18 byte-identical)"`

---

## SLICE 2 — HAL `set_rx_bw`/`set_rx_cr` + the window-switch retune (TX handled here)

The audit's case (a): TX flies on the HAL's `_def_bw`/`_def_cr`. So the retune MUST update those, not just the radio — then TX is correct with zero tx-site changes.

### Task 1: `IRadio`/`DeviceHal` gain `set_rx_bw`/`set_rx_cr` (standby-latch, update `_def_bw`/`_def_cr`)
**Files:** Modify `lib/core/hal.h` (`IRadio`), `lib/hal/device_hal.{h,cpp}`, `lib/hal/device_radio.h`. Test: the dual-layer HAL spy (`StubHal` in `test/test_dual_layer.cpp`).
- [ ] **Step 1: failing test** — extend the test HAL to record `set_rx_bw`/`set_rx_cr` calls; assert the window switch (Task 2) drives them. (Write the spy method now; the assertion lands in Task 2's test.)
- [ ] **Step 2: implement** — add to `IRadio`: `virtual void set_rx_bw(uint32_t) {}` `virtual void set_rx_cr(uint8_t) {}` (default no-op so non-gateway HALs need nothing). In `device_radio.h`, mirror `set_rx_freq` (standby → set → re-arm; SX1262 SetModulationParams latches BW/CR only in STANDBY — the L9/M11 lesson):
```cpp
void set_rx_bw(uint32_t bw_hz) override { _radio.standby(); _radio.setBandwidth((float)bw_hz/1000.0f); g_dio1_fired=false; arm_rx(); }
void set_rx_cr(uint8_t cr)     override { _radio.standby(); _radio.setCodingRate(cr);                  g_dio1_fired=false; arm_rx(); }
```
★ In `DeviceHal` (device_hal.{h,cpp}): `set_rx_bw` MUST also set `_def_bw = bw_hz` (and `set_rx_cr` → `_def_cr = cr`) so the TX path (which resolves the `-1` TxParams sentinel to `_def_bw`) transmits on the active layer's BW. Verify `_def_bw`/`_def_cr` are the fields `tx()`/`pump_tx()` read (device_hal.cpp:21-22,38-41). (Consider a combined `set_rx_modparams(sf,bw,cr)` — one standby cycle — as a refinement.)
- [ ] **Step 3-4: build** — `pio run -e xiao_sx1262` SUCCESS.
- [ ] **Step 5: commit**.

### Task 2: window switch retunes BW/CR
**Files:** Modify `lib/core/node.cpp` (`activate_layer`, the retune block ~:395-397). Test: `test/test_dual_layer.cpp` (HAL spy).
- [ ] **Step 1: failing test** — a 2-layer node with `layers[1].bw_hz=125000, cr=8`; after `set_active`/window-switch to layer 1, the HAL spy recorded `set_rx_bw(125000)` + `set_rx_cr(8)`; a layer with 0-override records no BW/CR retune (or the global).
- [ ] **Step 2: implement** — after the `set_rx_sf`/`set_rx_freq` lines (node.cpp:396-397), add:
```cpp
    { const uint32_t b = L.bw_hz; if (b > 0) _hal.set_rx_bw(b); }   // per-layer BW retune (+ updates HAL _def_bw -> TX flies on it)
    { const uint8_t  c = L.cr;    if (c > 0) _hal.set_rx_cr(c); }
```
- [ ] **Step 3-4: run native + build** SUCCESS.
- [ ] **Step 5: commit**.

---

## SLICE 3 — NV v17 + config wire (`bw0/bw1/cr0/cr1`) + validate

### Task 1: NV `Blob` v16→v17 (`l1_bw_hz` + `l1_cr`)
**Files:** Modify `src/device_nv.h` (append to `Blob` after `l1_freq_mhz` @:69; bump `kVersion` 16→17), `src/fw_main.cpp` (the boot-load + the b.snapshot save sites — thread `l1_bw_hz`/`l1_cr` ↔ `layers[1].bw_hz`/`cr`, mirroring `l1_freq_mhz`).
- [ ] **Step 1-2:** append `uint32_t l1_bw_hz;` + `uint8_t l1_cr;` (0=inherit), bump `kVersion=17` + the version comment. Old blobs size-reject → defaults (dev convention).
- [ ] **Step 3:** wire load (blob→`layers[1]`) + EVERY `b.` snapshot save site (grep, as in the v16 work) + the boot-defaults. `pio run -e xiao_sx1262` SUCCESS.
- [ ] **Step 5: commit**.

### Task 2: `parse_gateway_cmd` `bw0/bw1/cr0/cr1` + `validate_gateway_layers` per-layer
**Files:** Modify `lib/core/node.cpp` (`parse_gateway_cmd` @:95, freq parse @:145-146; `validate_gateway_layers` @:53), `lib/core/node.h` (decl @:68 if the signature changes), `src/fw_main.cpp` (help string @:1096).
- [ ] **Step 1: failing test** — `parse_gateway_cmd("l0=… l1=… bw0=250000 bw1=125000 cr0=5 cr1=8 …")` round-trips into `out.l0/l1.bw_hz/cr`; `validate_gateway_layers` REJECTS an illegal per-layer BW (e.g. bw=0-but-not-inherit path / out of the SX1262 set) or CR ∉ 5..8, and accepts legal per-layer values with each layer's duty feasible at its own BW.
- [ ] **Step 2-3: implement** — parse `bw0/bw1/cr0/cr1` → `out.l0/l1.bw_hz/cr` alongside `freq0/freq1`. `validate_gateway_layers` uses each `LayerConfig`'s `bw_hz`/`cr` (falling back to the passed global args for 0) for the airtime/duty feasibility check — ⚠ a narrow BW = longer airtime = tighter duty, so validate each layer at ITS bw. Update the help string.
- [ ] **Step 4: run native + build** SUCCESS.
- [ ] **Step 5: commit**.

---

## SLICE 4 — dual-layer per-layer-airtime verification

### Task 1: a 2-layer config with distinct BWs charges each layer its own airtime
**Files:** Test `test/test_dual_layer.cpp`.
- [ ] **Step 1: test** — a gateway with `layers[0].bw_hz=250000`, `layers[1].bw_hz=125000` (same SF): a duty/airtime debit computed while `_active`=layer 0 uses 250k; after the window switch to layer 1, the SAME-size frame's debit uses 125k (≈2× airtime). Assert via `active_bw_hz()` returning each + an `airtime_ms` spot-check (125k airtime > 250k airtime for the same SF/len). Confirms the accessor + retune agree = the charge==transmit invariant.
- [ ] **Step 2-4: run** — `pio test -e native` SUCCESS.
- [ ] **Step 5: full gate** — `pio test -e native` (full suite green) + `pio run -e xiao_sx1262 && pio run -e gateway` (+ `heltec_v3`/`gateway_heltec`/`xiao_esp32s3`/`gateway_esp32s3` as time allows — one build at a time, NOT parallel, per the nRF `.pio`-race lesson). Commit.
- [ ] **Bench (USER):** two physical gateway layers on different BWs — traffic on each delivers, and per-layer duty accounting matches on-air airtime.

---

## Notes carried from the audit (do not re-derive)
- `max_data_sf()` needs NO change (swap-mirrored scalar). Documented at node_mac.cpp:418 in Slice 1 Task 3.
- TX is HAL-`_def_bw`-driven — the ONLY TX work is `set_rx_bw` updating `_def_bw` (Slice 2 Task 1). No `_hal.tx` site changes.
- The bridge/handoff path costs no airtime at bridge time — no per-site fix.
- `channel_capacity_C` is coherent on a gateway (SF + BW both resolve to the active leaf) — no special-case.
- The cross-repo `NodeRuntimeWrapper.cpp` sim wrapper must build for the Slice-1 s18 parity gate (fix any stale removed-field ref first).
