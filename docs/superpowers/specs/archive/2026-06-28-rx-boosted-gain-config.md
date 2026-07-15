<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# SX1262 RX boosted-gain — lift from compile-time macro to a runtime `cfg` knob

**Status:** coder instruction. The user commits + flashes; I gate. Device-side feature (radio + NV + console). No wire/protocol change.

## Why
SX1262 RX boosted gain (reg `0x08AC`) trades ~2–3 dB sensitivity (more range, better weak/asymmetric inbound-link decode) for a small RX-current increase. Today it's **hard-wired ON** by the build flag `-DSX126X_RX_BOOSTED_GAIN=1` (platformio.ini, all 3 SX1262 board envs); the vendored `CustomSX1262::begin()` applies the macro. MeshCore exposes it as a runtime pref (`NodePrefs.rx_boosted_gain`, default on, `setRxBoostedGainMode()` + persisted). We do the same — a battery leaf turns it **off** for RX power; a range node keeps it **on**. Mirror the existing `tx_power` cfg knob exactly.

## The change (mirror `tx_power`)
1. **Blob** (`src/device_nv.h`): append `uint8_t rx_boost;` after `leaf_name[16]` (the v8–v15 append pattern). Bump `kVersion` 15 → 16 (+ comment line). The vendored `CustomSX1262` already has `setRxBoostedGainMode(bool)` / `getRxBoostedGainMode()` — no vendored edit.
2. **Default** (no / old / rejected blob → compile-time defaults): set `rx_boost` to the macro, so today's behaviour is preserved:
   ```cpp
   #ifdef SX126X_RX_BOOSTED_GAIN
     b.rx_boost = SX126X_RX_BOOSTED_GAIN;   // = 1 on all board envs -> boosted stays ON
   #else
     b.rx_boost = 1;
   #endif
   ```
3. **`cfg set rx_boost <0|1>`** (`src/fw_main.cpp` `handle_cfg_set`): mirror the `tx_power` branch — validate 0/1 (`bad_value` otherwise), set `b.rx_boost`, apply live, persist via `mrnv::save`, echo `cfg ok`.
4. **`apply_config`** (fw_main.cpp:383, where the live radio knobs land): add
   ```cpp
   g_radio.setRxBoostedGainMode(b.rx_boost != 0);   // AFTER the vendored begin() -> overrides the macro; also fires live on `cfg set`
   ```
5. **status / cfg dump**: add `rx_boost=` — print **`g_radio.getRxBoostedGainMode()`** (the actual register read-back), NOT the cached config, so the re-apply gotcha below is visible.

## ★ Re-apply gotcha (the correctness risk — audit these paths)
The RX_GAIN register does NOT stay set across radio re-inits:
- A **chip reset** re-applies the **macro**: the vendored `SX126xReset.h:29` calls `setRxBoostedGainMode(SX126X_RX_BOOSTED_GAIN)` — the *macro*, not our config. So any MeshRoute path that resets the radio (error/fault recovery) reverts boosted-gain to the build-flag value.
- A **cold sleep-wake** may lose the register (verify against the light-sleep path — [[meshroute-metal-rx-redesign]]; warm sleep retains, cold loses).

**Fix:** wrap the apply in one helper (e.g. `apply_rx_boost()` reading the live config) and call it after **every** radio reset / sleep-wake, not just boot. Confirm with `getRxBoostedGainMode()` after a sleep cycle and after a forced recovery reset.

## Default / no silent change
`SX126X_RX_BOOSTED_GAIN=1` today ⇒ `rx_boost` defaults to 1 ⇒ boosted stays ON fleet-wide. The cfg only *adds* the ability to turn it OFF.

## Migration (flag to the user)
No spare pad in the Blob (packed v15), so appending grows the struct → old blobs fail `n == sizeof(out)` in `load()` → rejected → **the node re-provisions from defaults** (the documented v8–v15 behaviour; shared with the ESP32/Preferences backend). ⚠ **The whole config resets — the fleet must re-`cfg set`/provision after flashing.** Same cost as every prior `kVersion` bump.

## Vendoring
No edits to `lib/meshcore/.../CustomSX1262.h` or `SX126xReset.h`. MeshRoute overrides post-`begin()` in `apply_config` (and the re-apply helper).

## Tests / gate
- **Native:** the feature is device-side (Blob = InternalFS/Preferences, `setRxBoostedGainMode` = SX1262) — not exercised by the lib/core native suite. Gate = native suite still **passes (no regression)**.
- **Boards:** all 3 SX1262 envs build (the macro is defined there; the `#ifdef` fallback covers any env without it).
- **★ Metal (the real gate):**
  - `cfg set rx_boost 0` → `status` shows `rx_boost=0` AND `getRxBoostedGainMode()` reads **false** (register actually changed); `cfg set rx_boost 1` → **true**.
  - reboot → persists (survives the NV round-trip).
  - **sleep-wake + a recovery reset** → `getRxBoostedGainMode()` still matches the config (proves the re-apply helper covers the gotcha).
  - optional: RSSI/decode A/B on a marginal link (boosted vs off) to confirm the ~2–3 dB sensitivity delta.
