# H0 (reworked) — device HAL backend over the SX1262 — design proposal

**Date:** 2026-05-30  **Status:** PROPOSAL — awaiting review, no code written.
**Supersedes** `2026-05-29-h0-meshcore-vendor-and-device-hal-design.md` (whose own
top RESOLUTION already invalidated its §0/§2/§3/§4 body). This is the corrected,
verified plan: the H-track stands up the **device backend** of `meshroute::Hal` on
real SX1262 hardware (XIAO nRF52840 primary), reusing MeshCore's PHY at its **clean
minimal limit**.

**Reality split (no hardware in the dev loop):** I can author the design + scaffold
the code + **native-verify** the MeshRoute-owned HAL logic (timer wheel, airtime
ledger, the Hal facade against a mock radio). The **on-device flash + bench bring-up
is yours** (the XIAO + the nordic toolchain). The nordic platform is not installed
here and the env's `board = xiao_ble_sense` is a wrong/unknown id (see §5).

---

## 0. The cut: PHY from MeshCore, MAC from us (unchanged)

Our protocol owns the MAC — duty-cycle budgeting (`airtime_used_ms`/`oldest_tx_end_ms`),
LBT/channel-sensing (`channel_busy_until`), and all scheduling (`after`) are MeshRoute's
and must match the Lua (and the sim backend) bit-for-bit. We must **not** adopt MeshCore's
`Dispatcher` MAC (it would fight our logic + diverge from the Lua). The reuse line is the
**PHY only**: the SX1262 driver. Everything above it is ours.

## 1. Verified vendor cut — TWO headers, nothing else

The old "vendor the radio glue" plan is dropped: `RadioLibWrappers.h`/`ArduinoHelpers.h`
`#include <Mesh.h>` → the whole `Dispatcher`/`Packet`/`Identity`/crypto stack. **Verified
2026-05-30:** `CustomSX1262.h` and `SX126xReset.h` include **only `<RadioLib.h>`** — zero
`mesh::`/`Dispatcher`/`Packet`/`Identity` leakage (grep-confirmed). So:

> **Vendor exactly `CustomSX1262.h` + `SX126xReset.h`, byte-identical.** Construct the
> RadioLib `Module(NSS, DIO1, RST, BUSY)` ourselves and drive `CustomSX1262` directly from
> `lib/hal/`. Skip `CustomSX1262Wrapper.h` (it pulls `RadioLibWrappers.h` → the mesh stack),
> `RadioLibWrappers`, `mesh::Radio`, `ArduinoHelpers`, `Dispatcher.*`, `MeshCore.h`, the board
> classes, and the variants — write our **own** trivial clock + RNG + LBT + airtime + timer
> wheel. `CustomSX1262::std_init()` is a self-contained one-call bring-up (begin / CRC / TCXO /
> DIO2-as-rf-switch / RXEN / RX-boost) reading compile-time macros for its **initial** bring-up:
> `LORA_FREQ` (MHz) / `LORA_BW` (kHz) / `LORA_SF` / `LORA_CR` / `LORA_TX_POWER` (dBm), the
> `SX126X_*` feature flags, and the SPI-**bus** pins `P_LORA_SCLK/MISO/MOSI`. (The *operating*
> freq/BW/SF are RUNTIME node config, not these macros — §5; the sync word is RadioLib's built-in
> `RADIOLIB_SX126X_SYNC_WORD_PRIVATE`, hard-coded, *not* a user macro; the NSS/DIO1/RST/BUSY
> control pins go to the `Module(...)` ctor above, not std_init.)

This resolves the GODMODE-via-wrapper + the `Dispatcher.h`-transitive-include risks entirely
(we never touch them).

**Layout:**
```
MeshRoute/
  lib/meshcore/              # vendored 1:1 — DO NOT EDIT
    src/helpers/radiolib/
      CustomSX1262.h         # CustomSX1262 : RadioLib::SX1262 + std_init()
      SX126xReset.h          # the SX126x reset helper
    NOTICE  license.txt  library.json   # attribution (§7)
  boards/                    # vendored 1:1 — DO NOT EDIT (PlatformIO auto-discovers custom boards)
    seeed-xiao-afruitnrf52-nrf52840.json   # MeshCore's XIAO board def (device build only — §5)
  lib/hal/                   # MeshRoute-OWNED device HAL — all customization HERE
    iclock.h                 # IClock seam (device ArduinoClock / native FakeClock)
    timer_wheel.{h,cpp}      # after()/cancel() over IClock (H1)
    airtime_ledger.{h,cpp}   # the sliding-window airtime log (shared algorithm w/ the sim)
    meshroute_sx1262.h       # CustomSX1262 subclass: preamble-detect DIO1 IRQ (H2)
    device_hal.{h,cpp}       # the meshroute::Hal device impl (H3)
  tools/vendor_meshcore.sh   # reproducible re-sync from a pinned MeshCore commit
```
PlatformIO auto-adds `lib/meshcore/src` to the include path so `CustomSX1262.h`'s
`#include <RadioLib.h>` resolves unmodified, and auto-discovers `boards/*.json`. Re-sync =
`vendor_meshcore.sh` copies the 2 headers (→ `lib/meshcore/`) + the board JSON (→ `boards/`)
from a pinned MeshCore checkout; `git diff` shows exactly what upstream changed.

## 2. H0–H3 sequence

| It | Scope | Success criterion |
|---|---|---|
| **H0** | Vendor the 2 PHY headers + attribute; the `IClock` seam; wire the device build | the 2 vendored headers + `lib/hal` compile under `xiao_sx1262`; the HAL-logic (`iclock`/`timer_wheel`/`airtime_ledger`/the facade shape) compiles + unit-tests under `native` against a `FakeClock` + `MockRadio` |
| **H1** | `after()`/`cancel()` timer wheel over `IClock` | native-tested: fire ordering + cancel + re-arm-by-id + the bounded-id allocator (the FirmwareNode contract) |
| **H2** | preamble-detect DIO1 IRQ via a `CustomSX1262` subclass | bench-verified on the XIAO; `on_preamble_detected` witness fires |
| **H3** | the `device_hal` facade: `tx`/`set_rx_sf`/`channel_busy_until`/airtime ledger driving `CustomSX1262` | drives a raw frame out the radio on-device; native mock proves the facade logic; then a 2-device beacon+DM exchange |

**Non-goals:** no protocol/codec/MAC logic (that is `lib/core`, already done); no NV/flash/OTA;
no `Dispatcher`; no edits to any vendored file.

## 3. Device `Hal` mapping (each method → its device source)

| `Hal` method | Device implementation | Parity note |
|---|---|---|
| `tx(bytes,len,TxParams)` | apply `TxParams` (`setSpreadingFactor/Bandwidth/CodingRate/OutputPower/preamble`) on the `CustomSX1262` → `transmit`/`startTransmit`; map → `TxResult`; record airtime into **our** ledger | the duty-cycle DECISION is the protocol's (it calls `airtime_used_ms` first); Hal just TXes + logs |
| `set_rx_sf(sf)` | `setSpreadingFactor(sf)` + re-arm `startReceive`; arm our blind-window | clamp 5..12 (matches the sim's `set_rx_sf`) |
| `channel_busy_until()` | the radio **primitive** — `scanChannel()`/CAD or `getRSSI(false)` threshold, wrapped into a busy-until hold | **our** LBT policy, not MeshCore's Dispatcher — must match the Lua |
| `airtime_used_ms` / `oldest_tx_end_ms` | **our** sliding-window ledger (`lib/hal/airtime_ledger`) — the SAME algorithm as the sim's `FirmwareNode` log → device == sim == Lua | the one place device + sim must agree exactly |
| `now()` | `IClock::now_ms()` → `ArduinoClock`/`millis()` on device | §4 |
| `after`/`cancel` | **our** timer wheel (H1) over `IClock`, bounded-id allocator | matches the FirmwareNode timer-id contract |
| `rand_range(lo,hi)` | a device RNG (HW RNG / seeded `std`), `[lo,hi)` | determinism only matters in the sim; the device seeds a real RNG at boot |
| `emit` / `log` | USB-CDC behind `#ifdef MESHROUTE_VERBOSE_EVENTS`, else no-op | NDJSON faithfulness is the SIM backend's job, not the device |
| `set_protocol_id` | store (→ NV later, deferred) | join/lease |
| `panic` | log + safe halt/reset | exception-free fatal hook |

## 4. `IClock` — the testability seam

```cpp
// lib/hal/iclock.h
namespace meshroute { struct IClock { virtual ~IClock() = default; virtual uint64_t now_ms() = 0; }; }
```
Device: `ArduinoClock : IClock` → `millis()`. Native test: `FakeClock` with a settable `now`
so the H1 timer-wheel + the airtime-ledger + the H3 facade run deterministically on the host
**without** real `millis()` or RadioLib. (The SIM backend does NOT use `IClock` — `FirmwareNode`
drives `now()` from the VirtualClock; `IClock` is device/native-test only.)

The device facade calls the radio behind a thin internal `IRadio` seam (`tx_raw`/`set_sf`/
`start_rx`/`scan_channel`/`last_snr`…) so the **logic** (`device_hal.cpp` minus the RadioLib
calls) links + unit-tests on `native` against a `MockRadio`; the real `CustomSX1262`-backed
`IRadio` is a device-only TU. This is how "native compiles" without the Arduino/RadioLib deps.

## 5. Build wiring

- **`xiao_sx1262`:** `platform = nordicnrf52`, **`board = seeed-xiao-afruitnrf52-nrf52840`**
  (FIX — the current `xiao_ble_sense` is an unknown id; this is the id MeshCore's canonical XIAO
  target uses, `variants/xiao_nrf52/platformio.ini:3`). **That id is a MeshCore-*custom* board, not
  a stock `nordicnrf52` one** — its def is `MeshCore/boards/seeed-xiao-afruitnrf52-nrf52840.json`
  and **must be vendored into `MeshRoute/boards/`** (§1) or `board =` won't resolve. It selects the
  **Adafruit** nRF52 core (`core:nRF5`, `bsp:adafruit`, S140 v7.3.0, ldscript `nrf52840_s140_v7.ld`);
  the variant/ldscript/softdevice come from the Adafruit BSP the platform auto-installs, so the JSON
  is the only extra file. `framework = arduino`, `build_src_filter = +<fw_main.cpp>`. Add
  `-DRADIOLIB_GODMODE=1 -DRADIOLIB_STATIC_ONLY=1`. Deps present: `RadioLib`, `Crypto`.
  **Board/analog macros (genuinely compile-time — board wiring):** `SX126X_DIO2_AS_RF_SWITCH=1`,
  `SX126X_DIO3_TCXO_VOLTAGE=1.8`, `SX126X_RXEN`, `SX126X_CURRENT_LIMIT`, `SX126X_RX_BOOSTED_GAIN`,
  and — only to override the board's default SPI bus — `P_LORA_SCLK/MISO/MOSI`. The **control pins**
  NSS/DIO1/RST/BUSY are NOT macros `std_init` reads; they go to the `Module(NSS,DIO1,RST,BUSY)` ctor
  we build (our existing `LORA_PIN_*` = NSS 4 / DIO1 1 / RST 2 / BUSY 3). **Do NOT define
  `SX126X_SYNC_WORD_PRIVATE`** — `std_init` hard-codes RadioLib's built-in
  `RADIOLIB_SX126X_SYNC_WORD_PRIVATE`, so a user macro of that name does nothing.
  **Freq / BW / SF / power are RUNTIME node configuration, not authoritative compile constants.**
  Parity with the sim: `routing_sf`/`data_sf` are config keys (`FirmwareNode.cpp:44,56`), `bw_hz`
  rides per-TX in `TxParams` (`FirmwareNode.cpp:196`), the protocol retunes SF at runtime, and the
  carrier freq sits *below* the `Hal` interface entirely — the device backend applies it from node
  config (`setFrequency`). So the build macros are only **plan defaults / `std_init` bootstrap**, not
  the operating point. Mechanically: `std_init` references `LORA_FREQ` (MHz) / `LORA_BW` (kHz) /
  `LORA_TX_POWER` (dBm) in its initial `begin()` (`CustomSX1262.h:45`), so those must be DEFINED to
  compile — e.g. `LORA_FREQ=869.4625`, `LORA_BW=125.0`, `LORA_TX_POWER=22` (cap to the g3 ≤500 mW
  ERP limit). The current `[common]` `LORA_FREQ_HZ`/`LORA_BW_HZ` (Hz) are the wrong name+unit for
  `std_init` and are read only by two banner prints (`src/fw_main.cpp:20,22`, `src/sim_main.cpp:20-21`);
  fold them into the MeshCore-unit macros so one definition feeds both `std_init` and the banners.
  `LORA_SF`/`LORA_CR` names already match. XIAO-Wio-SX1262 pins: NSS=D4, DIO1=D1, RST=D2, BUSY=D3,
  RXEN=D5 (the [[project_band_choice]] plan).
- **`native`:** compile `lib/hal`'s LOGIC (`timer_wheel`/`airtime_ledger`/the facade over
  `MockRadio`+`FakeClock`) — **exclude** the vendored RadioLib header + the device-only radio
  TU via a lib filter. So `pio test -e native` keeps proving the HAL logic on the host.

## 6. Test split (what I do here vs what you do on the XIAO)

- **Me, here:** scaffold all the code; native-verify the HAL logic — `pio test -e native` (the
  timer wheel ordering/cancel, the airtime ledger == the sim's, the facade against `MockRadio`).
  Confirm the design compiles where the toolchain allows.
- **You, on the XIAO:** `pio run -e xiao_sx1262 -t upload`; bench-verify H2 (the preamble IRQ
  fires) + H3 (a raw frame goes out the radio), then a **2-device beacon + DM exchange** (the
  on-metal analogue of t84/t86) — the real validation of the "one `lib/core`, two HAL backends"
  thesis. (If you want, I can install the nordic platform here to at least confirm the device
  build COMPILES — a sizable download — say the word.)

## 7. Attribution (keep byte-identical re-sync)

No per-file headers (MIT doesn't require them; a header edit breaks byte-identity).
`lib/meshcore/license.txt` = MeshCore's MIT license verbatim; `lib/meshcore/NOTICE` = origin +
pinned `MESHCORE_VERSION` + the source commit + the vendored-file list (2 headers + the board JSON) + "vendored unmodified — see
`tools/vendor_meshcore.sh`"; a MeshCore entry in the top-level `NOTICE.md`. MeshRoute stays BSD-3.

## 8. Files (H0)

`lib/meshcore/src/helpers/radiolib/{CustomSX1262.h,SX126xReset.h}` (vendored), `lib/meshcore/
{NOTICE,license.txt,library.json}`, `boards/seeed-xiao-afruitnrf52-nrf52840.json` (vendored custom
board def, §5), `lib/hal/iclock.h`, `lib/hal/airtime_ledger.{h,cpp}` (+ its native test),
`tools/vendor_meshcore.sh`, `platformio.ini` (board-id fix + GODMODE/STATIC_ONLY + the `SX126X_*`
board/analog `-D`s + MeshCore-unit `LORA_FREQ`/`LORA_BW`/`LORA_TX_POWER` bootstrap defaults folding
in the `*_HZ` ones + the native lib-filter), `NOTICE.md`, `src/{fw_main,sim_main}.cpp` (the banner
prints retargeted to the MeshCore-unit macros; `fw_main` setup() also constructs the radio + device
Hal + Node and loop() pumps timers + RX). (H1 adds `timer_wheel.*`; H2 `meshroute_sx1262.h`; H3
`device_hal.*` + the mock test.)

## 9. Open questions

1. **Boards:** XIAO nRF52840 only for H0 (rec), or vendor `heltec_v3` too (PHY sibling, cheap)?
2. **MeshCore version pin:** pin the current `~/MeshCore` checkout — record the commit in
   `NOTICE`? (rec yes — like the Lua baseline pin.)
3. **Native vs device build verification:** native-only here (rec — the HAL logic is what's
   testable without hardware), or should I install the nordic platform to confirm the device
   build COMPILES too (big download, no flash)?
4. **RNG source on device:** the nRF52840 HW RNG (rec) vs a seeded `std` — determinism only
   matters in the sim, so a real entropy source is fine on-device.
5. **`fw_main.cpp` scope for H0:** a bare radio+Hal+Node bring-up that just beacons (rec — the
   smallest thing that proves the stack runs on metal), or wait until H3 for any device main?
6. **Confirm the §0 PHY-not-MAC cut + the 2-header vendor set** (vs a larger PHY vendor) — rec
   the minimal cut (verified clean); it is the whole point.
