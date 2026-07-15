<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# lus metal-fidelity: close the two blind spots that let recent metal bugs through — DIRECTLY IMPLEMENTABLE

**Status:** coder instruction (2026-07-07). Cross-repo: `~/lora-universal-simulator` (the slop knob) + `~/MeshRoute/simulation` (new scenario variants). The user commits + I quality-gate. **Step 1 of the "prepare lus for mobile" plan.**

## Why
Two recent metal-only bugs were **structurally invisible** in lus, and the diagnosis is verified:
- **CTS-wait bug** (cross-layer DM RTS↔CTS-looped): `FirmwareNode::simRxWindowSlopMs()` (orchestrator/runtime/FirmwareNode.cpp:205) **returns 0** by design ("idealized sim") — but the bug needs the metal turnaround slop > 0, so the too-short window always fit on the sim.
- **Nibble-match bug** (cross-layer E2E-ACK never bridged home): every sim scenario uses `layer_id` **< 16** (s09 = 4/5, s15 = 1) → **nibble == full**, so the gateway's full-id match worked. Metal used 100/102 (nibble 4/6 ≠ full).

lus is faithful to lib/core *logic* (real frame_codec, distance-RF, airtime) but **idealizes the metal**. This spec adds a **metal-fidelity mode** that surfaces both bug classes — **without disturbing any existing baseline** (the idealized path stays the default; new coverage is opt-in).

## ★ Non-negotiable invariant
**No existing scenario changes behavior.** s18 stays byte-identical (240119/1149/0); s09/s10/s15/s16 delivery baselines unchanged. Both fixes are **additive + default-off**: a flag that defaults to the current value, and *new* scenario files (never edits to the existing ones).

## Fix 1a — configurable RX-window slop (`~/lora-universal-simulator`) — ALREADY PARTLY DONE, but bw is FROZEN
A metal-slop mode already exists (FirmwareNode.cpp:216: `if (_rx_window_slop_metal && _node_bw_hz > 0) return ((1<<sf)*1000)/_node_bw_hz + 1 + 50;`, default `return 0`). ✅ the flag + the device formula are correct. **The bug: `_node_bw_hz` is set ONCE from `_sim_bw_hz` (FirmwareNode.cpp:52) and NEVER updated** — so on a GATEWAY it does not follow the per-layer 62.5↔125 kHz switching, and the slop is wrong on one layer. The device's `_def_bw` is updated by `set_rx_bw` on every window switch; the sim treats `set_rx_bw` as a **no-op** (iradio.h:55 default, not overridden). **NOT `#53` hardcoded, but effectively frozen-per-node — same defect for a gateway.**

**Edit — make the sim track the ACTIVE-layer bw, mirroring the device `_def_bw`:**
- **Override `set_rx_bw(bw_hz)`** in the sim's HAL/radio so it **updates `_node_bw_hz`** (exactly as `device_radio.h:216` updates `_def_bw`). Then `simRxWindowSlopMs` reads the current per-layer bw, not the frozen config value. Single-layer nodes are unaffected (one `set_rx_bw`, or none → the `_sim_bw_hz` seed stands).
- **Seed `_node_bw_hz` = the node's layer-0 bw** at init (already via `_sim_bw_hz`) so it's correct before the first switch.
- Keep the default path (`return 0`) so idealized scenarios stay byte-identical.
- Result: a metal gateway scenario computes the slop from whichever layer's bw is active — so `start_rts_timeout`'s window is checked against the *correct* per-layer CTS latency (the narrow-BW layer being the one that exposed the bug).

✅ **Path verified — the fix is a clean 1-liner.** The Node already calls `_hal.set_rx_bw(active_bw_hz())` on every window switch (node.cpp:424), so the per-layer bw signal *arrives* at the sim; `FirmwareNode` simply doesn't override `set_rx_bw`, so it hits the `iradio.h:55` no-op default and is dropped. The edit is just: add `void set_rx_bw(uint32_t bw_hz) override { _node_bw_hz = static_cast<int>(bw_hz); }` to `FirmwareNode` (mirror `device_radio.h:216`). No deeper wiring.

## Fix 1b — realistic-id cross-layer scenario variants (`~/MeshRoute/simulation`)
**New files** (do NOT edit s09/s15): `s09_two_layer_gateway_metal.json`, `s15_three_layer_metal.json` (+ s16 if cheap). Clone the existing scenario, then:
- **`layer_id` ≥ 16, nibbles distinct** — mirror metal: layer A = **100** (leaf 4), layer B = **102** (leaf 6). So `nibble (4/6) ≠ full (100/102)`.
- **Node ids mirror the reservation** — gateway node_id in **1..16** (e.g. 7/8), normal nodes **17..254**. (This also exercises the `is_gateway_dest` recognition, not the id-range.)
- **Set `rx_window_slop: metal`** in the `simulation` block (Fix 1a) — so these variants have both the id-width AND the metal-timing fidelity.
- **Assert the E2E-ACK round-trip**, not just forward delivery: an originator sends a cross-layer DM with `e2e_ack_req`, and the check requires the **reversed 4e ack to bridge home** (the originator records `send_e2e_acked` / an `E2E-ACKED` receipt). This is the reverse hop that the nibble bug broke and no existing scenario asserted.

## ★ The proof it actually catches the bugs (the gate)
This is the whole point — verify the new scenarios FAIL without the fixes:
1. **Nibble:** with the bridge nibble-match REVERTED (node_mac_rx.cpp:801 back to `layer_id == target_layer_id`), `s09_two_layer_gateway_metal` must **fail the E2E-ACK-round-trip assertion** (bridge refuses `target_leaf<0`). With the fix in → passes.
2. **CTS-wait:** with `start_rts_timeout`'s slop REVERTED (drop the `+2*slop`) AND `rx_window_slop: metal`, `s09_..._metal` must show the **RTS↔CTS loop / no cross-layer DATA**. With the fix in → delivers.
   (Do these two reverts locally to CONFIRM the scenarios bite, then restore — don't commit the reverts. Report the before/after.)
3. **No regression:** `lus -e meshroute simulation/s18_meshroute.json` still **byte-identical** (240119/1149/0); existing s09/s10/s15/s16 delivery unchanged (they don't set the flag).

## Sites
`~/lora-universal-simulator`: `orchestrator/runtime/FirmwareNode.cpp:205` (+ SimConfig/SimController config plumbing for the `rx_window_slop` field) · `~/MeshRoute/simulation/`: new `s09_two_layer_gateway_metal.json` + `s15_three_layer_metal.json`. **No firmware (lib/core / src) change — this is test-harness fidelity only.**

## Scope note (not this spec)
The **id-persist / node_id-thrash** bug class lives in `fw_main.cpp` (device NV persist) — **out of lus scope** (lus drives lib/core `Node`, not the device layer). That class needs a native-fw or bench harness; track separately, don't force it into lus.
