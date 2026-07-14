# T1000-E (mobile-only) firmware variant — feasibility note

*2026-07-14. Assessment only — not scheduled.*

**Verdict: feasible and well-bounded.** Mobile-only is the right scope.

## MCU — free
The SenseCAP T1000-E is an **nRF52840** (811 KB flash / 235 KB RAM, variant `Seeed_T1000-E`, SoftDevice s140 7.3.0) — the same MCU MeshRoute already ships on `xiao_sx1262` and `gateway`. So BLE, LittleFS/QSPI inbox, fault-log, USB-CDC, and light-sleep already run on this silicon, and the mobile role is just a build flag (`-DMR_PROFILE_MOBILE`, the `xiao_mobile` pattern).

## Radio — the one real piece of work
The T1000-E uses the **Semtech LR1110**, not the SX1262 (MeshRoute's only PHY today). The architecture keeps this bounded rather than a rewrite:

- `lib/hal/iradio.h` is a clean ~12-method seam; `DeviceHal` uses the radio only through it (native tests run on a `MockRadio`). The chip-specific code is a single TU — `Sx1262Radio : IRadio` (`lib/hal/device_radio.h`, 340 lines).
- MeshRoute already vendors MeshCore's `CustomSX1262.h`. MeshCore has the exact parallel to vendor the same way: `CustomLR1110.h` + `CustomLR1110Wrapper.h` + `LR11x0Reset.h`, plus a **working** T1000-E config (pin map, RF-switch table, DIO3-TCXO @ 1.6 V, RX-boosted-gain).
- SF/BW/CR/freq are standard-LoRa RadioLib calls that the LR1110 class supports with the same shape.

### Scope (4 steps)
1. Vendor the 3 LR1110 headers (mirror the SX1262 vendoring). *small*
2. Write `Lr1110Radio : IRadio` — parallel to `Sx1262Radio`: LR1110 init/reset, RF-switch table, TCXO, RX-boosted-gain, LR11x0 IRQ constants, LR1110 CAD/RSSI for `channel_busy`. *medium — the bulk; needs the physical board to nail RF-switch/TCXO/CAD.*
3. Add the `t1000e_mobile` env + the `Seeed_T1000-E` nRF52 variant + pin map (all present in MeshCore's `variants/t1000-e/`) + `-DMR_PROFILE_MOBILE`. *small*
4. Branch `device_hal.cpp` to instantiate `Lr1110Radio` under the T1000-E env. *small*

## Interop
LR1110 and SX1262 both use standard LoRa modulation, so a T1000-E talks to the existing SX1262 fleet on the same freq/SF/BW/CR — no frame or protocol change; the airtime model is chip-agnostic. (Bench-verify sync-word/preamble; MeshCore uses the same defaults.)

## Effort / risks
Scaffolding (env/variant/vendor/wire-up) is a few hours of known patterns. The LR1110 driver plus on-hardware bring-up is ~1–3 days, de-risked by MeshCore's working `CustomLR1110` and proven T1000-E config. Watch: RadioLib 7.6 LR1110 LoRa maturity; `CustomLR1110` possibly pulling GNSS/WiFi deps (ignore for LoRa-only — confirm it compiles standalone); the vendored-file Serial-in-error-path caveat (same as `CustomSX1262`).
