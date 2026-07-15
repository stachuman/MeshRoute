<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Gateway per-layer bandwidth (`bw_hz` per `LayerConfig`)

**Status:** coder instruction (2026-07-04). The user commits + flashes; I quality-gate. **Gateway build only** (`MR_N_LAYERS >= 2`) — a normal single-layer node is unaffected. **NV blob grows → `kVersion` bump → re-provision** (see Gotchas).

## Why
A dual-layer gateway bridges two networks that may run **different bandwidths**, but `bw` is node-level (`NodeConfig.radio_bw_hz`) while the *other* per-layer RF params are already per-layer. The design went per-layer for **freq** (`LayerConfig.freq_mhz`, v12), **routing_sf**, and **sf_list** (`allowed_sf_bitmap`) — but **bw was missed**. Consequence: the window-switch retune changes the carrier **freq** per layer (`node.cpp:397` `set_rx_freq`) but keeps **one bw**, so a gateway bridging two different-BW networks can only decode one of them (a BW mismatch = no reception).

**This is a clean mirror of the existing `l1_freq_mhz` / `LayerConfig.freq_mhz` machinery** — add the `bw` sibling everywhere `freq` is per-layer. Two spots are more than mechanical (flagged ★): the **runtime retune** (the actual fix) and the **window-derive** (uses bw for airtime).

## The `l1_freq` template → add the `bw` sibling at each site

**Semantics (mirror freq exactly):** `LayerConfig.bw_hz == 0` ⇒ **inherit the node bw** (`radio_bw_hz`). Layer 0 uses the node bw; layer 1 uses `l1_bw_hz` or inherits. The gateway command's new `bw0=`/`bw1=` take **kHz** (fractional ok, like `create`/`join` `bw=`) and store Hz (`×1000` rounded).

1. **`lib/core/node.h` — `LayerConfig`:** add `uint32_t bw_hz = 0;` immediately after `freq_mhz` (~:50). Comment: `per-layer BW (Hz); 0 = inherit the node radio_bw_hz. Layer 0 reuses radio_bw_hz.` — mirror the `freq_mhz` comment.

2. **`src/device_nv.h` — Blob + version:** add `uint32_t l1_bw_hz;` immediately after `l1_freq_mhz` (:69), comment `v17: layer-1 BW (per-layer). 0 = inherit bw_hz (layer 0's).`. **Bump `kVersion 16 → 17`** (:83) with a `v17: per-layer gateway bandwidth (l1_bw_hz)` note prepended to the version history. (Blob grows +4 B → the exact-size `load()` check rejects a v16 blob → clean re-provision, the established pattern.)

3. **`lib/core/node.cpp` — `parse_gateway_cmd`:** add `bw0=` / `bw1=` tokens mirroring `freq0=` / `freq1=` (:145-146). Parse kHz (`atof`), range-check 7..500, store Hz (`(uint32_t)(khz*1000.0+0.5)`) into `out.l0.bw_hz` / `out.l1.bw_hz`. Add a `GwParseErr::bad_bw` (mirror `bad_freq`) + its `gw_parse_err_str` case in `fw_main.cpp`.

4. **★ `lib/core/node.cpp` — `validate_gateway_layers` window-derive:** the `per_byte_air(sf)` lambda (~:65-72) currently uses the single `radio_bw_hz` param for BOTH layers' airtime. The SF-weighted anti-phase window split is airtime-proportioned, and airtime depends on **bw as well as SF** — so with per-layer bw it is mis-proportioned. FIX: make `per_byte_air(sf, bw)` and call it with each layer's effective bw: `per_byte_air(a.routing_sf, a.bw_hz ? a.bw_hz : radio_bw_hz)` and `per_byte_air(b.routing_sf, b.bw_hz ? b.bw_hz : radio_bw_hz)`. Keep the `radio_bw_hz` param as the inherit fallback.

5. **`src/fw_main.cpp` — `handle_gateway` apply:** mirror the freq apply (:871-872). `if (g.l0.bw_hz) b.bw_hz = g.l0.bw_hz;` (bw0 sets the node/layer-0 bw, else keep); `b.l1_bw_hz = g.l1.bw_hz;` (0 = inherit at boot). NB the existing `const uint32_t bw = b.bw_hz ? … : LORA_BW` at :867 already seeds layer-0 bw; `bw0=` now overrides it.

6. **`src/fw_main.cpp` — `cfg set l1_bw` key:** mirror `l1_freq` (:656-659). kHz in → Hz. `b.l1_bw_hz = (uint32_t)(atof(val)*1000.0+0.5);` with a `0 = inherit` note; range-check 7..500 (nonzero). `live = false` (reboot-to-apply, like l1_freq).

7. **`src/fw_main.cpp` — on_init restore** (the freq restore at :1701 / :1711): add `cfg.layers[0].bw_hz = nv.bw_hz;` and `cfg.layers[1].bw_hz = (nv.l1_bw_hz > 0) ? nv.l1_bw_hz : nv.bw_hz;` (mirror the `freq_mhz` inherit).

8. **`src/fw_main.cpp` — the 4 snapshot/seed sites** that copy `l1_freq_mhz` (:481, :872 [done in #5], :913, :1911): add the parallel `b.l1_bw_hz = nc.layers[1].bw_hz;` so a `cfg set`/provision accumulation doesn't zero it.

9. **★ `lib/core/node.cpp:397` — the runtime retune (THE fix):** immediately after `if (L.freq_mhz > 0.0) _hal.set_rx_freq(L.freq_mhz);` add `if (L.bw_hz > 0) _hal.set_rx_bw(L.bw_hz);`. `L` is the just-activated layer; without this the per-layer bw config is stored but **never applied** on the window switch (the whole bug). *(Both RX and the subsequent TX inherit the radio's active bw, exactly like freq — so a window-switch that sets bw fixes reception AND transmission on that layer.)*

10. **★ `lib/hal/` — the `set_rx_bw` HAL primitive:** add `set_rx_bw(uint32_t bw_hz)` mirroring `set_rx_freq` at all three sites: the `iradio.h` virtual default `{}` (:51), the `device_hal.h` decl (:30), and the `device_radio.h` override (:199). The override mirrors `set_rx_freq`'s discipline exactly: **standby → `setBandwidth(bw_hz/1000.0f)` → re-arm RX** (a bandwidth change mid-RX is dropped, same as freq), and it must latch so the next TX carries it (see the `set_rx_freq` comment at device_radio.h:197). Guard `bw_hz > 0`.

11. **`src/fw_main.cpp` — help + `dump_cfg`:** add `bw0=`/`bw1=` to the `gateway` usage/help line (:1088 area) and `l1_bw` to the dual-layer-gw `cfg keys` help line (:1093); show each layer's effective bw in the `[cfg.layer0]`/`[cfg.layer1]` dump lines (next to `routing_sf`).

## Gotchas
- **★ NV v16→v17 = re-provision.** The whole gateway must be re-provisioned after the flash (blob grew). The `0 ⇒ inherit` guard means a fresh/zeroed field behaves as "same bw as layer 0" — sane default. Confirm the exact-size `load()` rejects v16 and reprovisions (no misread).
- **★ The window-derive (site 4) is not optional** — if omitted, two different-BW layers get a time-split proportioned as if both ran the node bw, so the slower-airtime layer is starved. Airtime = f(SF, **BW**).
- **★ Site 9 is the load-bearing fix** — sites 1-8/10 only *store/derive* the per-layer bw; site 9 is what actually applies it at the window switch. A review that sees the config plumbing but not the retune would pass a still-broken gateway.
- **Reserve/NAV airtime on a gateway** already reads `_cfg.radio_bw_hz` in a few spots — for the gateway's own airtime accounting this is an approximation; out of scope here (the reception fix is the priority). Note it, don't chase it.
- **Gateway build only** — `l1_bw_hz`/`bw0=`/`bw1=`/`l1_bw` live behind `MR_N_LAYERS >= 2` like the l1_freq machinery; a normal build is untouched.

## Tests / gate
- **Native:** `validate_gateway_layers` with **different per-layer bw** (e.g. l0=SF7@125k, l1=SF9@250k) — assert the derived `window_ms` split reflects both SF and BW (differs from the same-bw case); the `0 ⇒ inherit radio_bw_hz` fallback. NV **v16→v17** round-trip (`l1_bw_hz` persists; a zero field inherits; an old v16 blob is rejected → reprovision). The full suite stays green.
- **Boards:** all 4 build — **especially the `gateway` env** (the new fields are behind `MR_N_LAYERS>=2`).
- **★ Metal (the real gate):** flash a gateway bridging **two networks on different BW** (e.g. leaf A @125 kHz, leaf B @250 kHz); confirm it **receives DM/channel traffic from BOTH** across the window switches (before this fix it only decoded the one matching the node bw). Confirm `cfg`/`status` shows the per-layer bw.

## Sites (quick index)
`node.h` (LayerConfig.bw_hz) · `device_nv.h` (l1_bw_hz + kVersion 17) · `node.cpp` (parse_gateway_cmd bw0=/bw1=, validate_gateway_layers per-layer per_byte_air, **:397 set_rx_bw call**) · `iradio.h`/`device_hal.h`/`device_radio.h` (**set_rx_bw** primitive) · `fw_main.cpp` (handle_gateway apply, cfg set l1_bw, on_init restore, 4 snapshot sites, help/dump_cfg + bad_bw string).
