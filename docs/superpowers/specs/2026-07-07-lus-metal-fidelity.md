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

## Fix 1a — configurable RX-window slop (`~/lora-universal-simulator`)
Make `simRxWindowSlopMs` return the **bench-measured metal slop** when a scenario opts in; **default 0** (unchanged).
- **Config:** add a `simulation` block field, e.g. `"rx_window_slop": "metal"` (default `"idealized"` / absent → 0). Plumb it into `FirmwareNode` (via `SimController`/`SimConfig`, the same path `simAirtimeUsedMs`/positions use).
- **FirmwareNode.cpp:205** — when the flag is `metal`, return the **device formula** (mirror `lib/hal/device_hal.h:39`): `((1u << sf) * 1000u) / bw + 1 + 50`, using the node's configured bandwidth. If wiring `bw` into `FirmwareNode` is awkward, a **fixed `53`** is acceptable (the `+50` reconfig/safety term dominates and is what the CTS-wait needs) — but the formula is preferred for fidelity. Default path unchanged (`return 0`).
- Result: with `rx_window_slop: metal`, `start_rts_timeout`'s window must clear the real CTS latency — exactly the margin the CTS-wait fix added.

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
