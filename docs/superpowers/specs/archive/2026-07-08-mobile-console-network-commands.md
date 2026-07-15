<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Mobile v1 — console network-control commands + the auto-register toggle — DIRECTLY IMPLEMENTABLE

**Status:** coder instruction (2026-07-08). Makes a mobile **user/app-drivable** for network discovery + registration (the phone companion orchestrates it), with a toggle so the sim + basic bench stay hands-off. The user commits; I quality-gate. **Depends on revised-5a** (the `MOBILE_LAYER_QUERY`/`_ANSWER` DM TYPEs + `_learned_layers` directory + the retune/scan mechanics) — land that first.

## Model
- **`mobile_autoregister` (config, default ON)** gates ALL autonomous behaviour: the boot-arm of the registration FSM, the home-lost re-scan, the periodic re-CLAIM, AND the revised-5a auto-layer-pull. **ON** = today's behaviour (sim/bench unchanged). **OFF** = the mobile does nothing on its own — the app drives every step via the commands below.
- **The 4 commands work regardless of the toggle** — they are explicit triggers into the existing FSM / pull path.

## Fix 1 — `mobile_autoregister` config (`node.h` + NV + cfg-key)
- `NodeConfig`: `bool mobile_autoregister = true;`.
- NV: add to the Blob (coordinate the version bump with 6.1's team_id — one v18 that adds both `team_id` + `mobile_autoregister`, so a single reprovision covers both).
- cfg-key (fw_main.cpp ~:585, beside `mobile`): `else if (!strcmp(key, "mobile_autoregister")) { lc.mobile_autoregister = (atoi(val)!=0 || !strcmp(val,"true")); b.mobile_autoregister = lc.mobile_autoregister?1:0; }`. Preserved across create/join (the blob-preserve sites).
- Gate the autonomy: `node.cpp:282` boot-arm → `if (_cfg.is_mobile && _cfg.mobile_autoregister) _hal.after(0, kMobileDiscoverTimerId);`. Same `&& _cfg.mobile_autoregister` guard on the home-lost re-arm, the re-CLAIM re-arm (`node_mobile.cpp`), and the revised-5a layer-query timer.

## Fix 2 — `mobile register [freq=<MHz> sf=<5-12> bw=<kHz> | scan]` (`fw_main.cpp`)
A `mobile` command group (`handle_mobile(args)`), `register` verb — arm the FSM explicitly (works even with autoregister OFF):
- **no args:** DISCOVER on the CURRENT PHY now — arm `mobile_discover_fire` immediately (`_hal.after(0, kMobileDiscoverTimerId)`), scan mode = current.
- **`freq=<f> sf=<s> bw=<b>`:** `adopt_mobile_phy({that PHY})` (retune), then arm → register on that PHY (the initial/target layer).
- **`scan`:** set the scan cursor to cycle `[current] ∪ _learned_layers` (the learned directory) and arm → cross-layer scan across known networks.
- Prints e.g. `> mobile register: scanning on freq=868.1 sf=7 bw=125` / `scanning 3 known networks`.

## Fix 3 — `mobile gateways` (`fw_main.cpp`)
List what the mobile has learned:
- **Gateways** (from the type-4 gateway-layer TLV / the bridged-layers table): `gw <gw_id> -> leaf <L>` per known entry.
- **Networks** (the `_learned_layers` directory, from `mobile query`): `net layer=<layer_id> "<name>" freq=<MHz> sf=<sf> bw=<kHz>` per record.
- Empty → `no gateways learned` / `no networks learned (use 'mobile query <gw>')`.

## Fix 4 — `mobile query <gw_id>` (`fw_main.cpp`) — the manual pull (replaces the auto-pull)
Send a `DATA_TYPE_MOBILE_LAYER_QUERY` (revised-5a) to `<gw_id>` → its `MOBILE_LAYER_ANSWER` populates `_learned_layers`. Prints `> mobile query gw=<id> sent` (the answer arrives async → then visible in `mobile gateways` / `mobile status`). With autoregister OFF this is the ONLY way the directory fills.

## Fix 5 — `mobile status` (`fw_main.cpp`) — the detailed mobile view
- **Registration:** `REGISTERED home=<home_id> local=<local_id> epoch=<e>` or `UNREGISTERED`.
- **Current network (the full layer record):** `layer=<layer_id> "<name>" freq=<MHz> sf=<sf> bw=<kHz>` (from `_my_mobile_reg`'s full record, 5a-revised).
- **autoregister=<0/1>**.
- **Known networks:** the `_learned_layers` count + list (or a hint to `mobile query`).
*(The brief `mobile-reg:` line already in `status` stays; this is the full view.)*

## Tests
- **Toggle default:** a fresh mobile → `mobile_autoregister==true` → the FSM auto-arms (as today). `cfg set mobile_autoregister 0` → the boot-arm/home-lost/re-CLAIM/layer-pull do NOT fire; only a `mobile register` arms the FSM.
- **register modes:** `mobile register freq=… sf=… bw=…` → `adopt_mobile_phy` called with that PHY + FSM armed; `mobile register scan` → scan cursor spans `_learned_layers`; no-args → arm on current PHY.
- **query→gateways/status:** feeding a mobile a `MOBILE_LAYER_ANSWER` after `mobile query` → `mobile gateways`/`mobile status` list the learned network(s).
- **★ Sim regression:** autoregister default-ON → **s07/s21 unchanged** (mobiles auto-register exactly as gated); native + s18 byte-identical.

## Gate
- `pio test -e native` green (toggle gating + the command triggers + register-modes).
- **s18 byte-identical** (`3ac88d40…`); **s07/s21 unchanged** (autoregister default-ON).
- 4 boards compile (the console additions — verify after revised-5a's `LayerRecord` unblocks the build).

## Sites
`node.h`(`NodeConfig.mobile_autoregister`) · NV Blob + `kVersion` (with 6.1) · `node.cpp:282` + `node_mobile.cpp`(guard the auto-arms on `mobile_autoregister`) · `src/fw_main.cpp`(`handle_mobile` = register/gateways/query/status; cfg-key `mobile_autoregister`; dispatch `mobile`) · tests. **Depends on revised-5a (DM TYPEs 10/11 + `_learned_layers` + `adopt_mobile_phy`). Commands reuse the FSM + the pull — no new wire.**
