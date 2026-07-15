# H0 — MeshCore vendoring + device HAL backend (design proposal)

**Date:** 2026-05-29  **Status:** PROPOSAL — awaiting review, no code written.
**Builds on:** the `meshroute::Hal` contract from S2
(`~/lora-universal-simulator/docs/superpowers/specs/2026-05-29-s2-hal-and-sim-backend-design.md`
§3) — this spec is the **device** implementation of that *same* interface (the
sim backend is S2/S3). See
`~/MeshRoute/docs/PORT_PLAN.md` §3 (MeshCore reuse), §4 (HAL track H0–H3), D5/D6
(vendoring & license).

**Touches MeshRoute only** (`lib/`, `platformio.ini`, `NOTICE.md`, `tools/`). No
simulator changes.

> **RESOLUTION — include-graph verified 2026-05-29 (supersedes §0/§2/§3/§4 below).**
> The "vendor the radio glue" plan is dropped. `RadioLibWrappers.h` and
> `ArduinoHelpers.h` both `#include <Mesh.h>` → `Dispatcher.h` →
> `Packet.h`+`Identity.h`+`Utils.h` — the whole MAC/packet/identity/crypto layer —
> and `RadioLibWrapper` references `mesh::Packet`/`Identity`/`mesh::RNG`, not just
> `mesh::Radio`. The wrapper is **not** separable from the mesh stack. The SX1262
> driver, however, is clean: **`CustomSX1262.h` + `SX126xReset.h` include only
> `<RadioLib.h>`**, and `std_init()` is verified self-contained (RadioLib calls +
> compile-time pin/RF macros; no board class, no `mesh::` type; RXEN/TXEN handled
> in-driver via `setRfSwitchPins`; `isReceiving()` poll included).
>
> **Corrected vendor set = those 2 headers, nothing else.** Construct the RadioLib
> `Module(NSS,DIO1,RST,BUSY)` ourselves and drive `CustomSX1262` directly from
> `lib/hal/device_hal`; supply pins/RF via build-flag macros in MeshCore's names
> (`P_LORA_*`, `LORA_FREQ/BW/SF/CR`, `LORA_TX_POWER`, `SX126X_*`). **Skip
> `RadioLibWrapper`, `mesh::Radio`, `ArduinoHelpers`, `Dispatcher.h`, `MeshCore.h`,
> the board classes, and the variant files** — write our own trivial `ArduinoClock`
> + RNG, take `channel_busy_until` from `radio.getRSSI(false)` /
> `CustomSX1262::isReceiving()` (RadioLib primitives), and own duty/LBT/scheduling
> (§0). This is §0's PHY/MAC cut at its clean limit; it resolves Q3 + risk #4 and
> dissolves the GODMODE-via-wrapper **and** `startReceive`-re-arm concerns (we own
> `startReceive`). §0/§2/§3/§4/§7 get reworked to this when the H-track starts —
> S2 is next.

---

## 0. The decision that shapes everything: PHY from MeshCore, MAC from us

What we learned writing `dv_dual_sf.lua`: **our protocol owns the MAC.**
Duty-cycle budgeting (`airtime_used_ms`/`oldest_tx_end_ms`), listen-before-talk /
channel sensing (`channel_busy_until`), and all scheduling (`after`) are
implemented *by the protocol* and are part of the behaviour the differential test
pins against the Lua. MeshCore *also* implements a MAC — its `Dispatcher` has a
duty-cycle token-bucket, an RSSI-LBT/CAD policy, and packet scheduling. **We must
not adopt MeshCore's MAC**: it would (a) duplicate/fight our protocol's own logic
and (b) diverge from the Lua semantics the sim backend reproduces — device and
sim would then disagree.

So the reuse line is drawn at the **PHY**:

| Layer | Source | Why |
|---|---|---|
| SX1262 driver, RadioLib glue, pin maps, board bring-up | **MeshCore, vendored 1:1** | transmit/receive/CAD/SNR/RSSI + correct pins — hardware-specific, well-tested, worth tracking upstream |
| Duty-cycle ledger, LBT/CAD *policy*, scheduling, framing | **MeshRoute (ours)** | our protocol owns these; must match the Lua (and the sim backend) bit-for-bit |

Concretely: vendor the radio wrapper + `CustomSX1262` + variants, and **do not
vendor/instantiate `Dispatcher`** as a MAC. Drive the radio wrapper directly from
our device `Hal`, feeding our *own* airtime log and computing `channel_busy_until`
from the radio's RSSI/CAD **primitive** (not MeshCore's policy). This is PORT_PLAN
§3's reuse list **minus** the "Dispatcher duty-cycle/LBT bonus" — that bonus is
exactly the part that would conflict with our protocol. (We still vendor
`Dispatcher.h` for the `mesh::Radio` / `mesh::MillisecondClock` *base interfaces*
the wrapper inherits — but not `Dispatcher.cpp`'s engine. See §3.)

---

## 1. Goal & non-goals

**Goal:** stand up the **device backend** of `meshroute::Hal` on real SX1262
hardware (XIAO nRF52840 primary, Heltec V3 backup) by reusing MeshCore's PHY 1:1,
vendored structure-preserving so upstream fixes re-sync mechanically. Sequenced
(PORT_PLAN §4):

| It | Scope | Success criterion |
|---|---|---|
| **H0** | Vendor + attribute the MeshCore PHY files (structure-preserving, byte-identical); add `IClock`; wire the device build | vendored radio glue compiles under `xiao_sx1262` (+ `heltec_v3`); the MeshRoute-owned device-HAL scaffolding + `IClock` compile under `native` against a mock radio (§6) |
| **H1** | `after()`/`cancel()` timer wheel over `IClock` | native unit-tested: fire ordering + cancel + re-arm + the bounded id allocator (S2 §3) |
| **H2** | PreambleDetected DIO1 IRQ via a `CustomSX1262` subclass | bench-verified on XIAO; `on_preamble_detected` witness fires |
| **H3** | Device `Hal` facade: `tx`/`set_rx_sf`/`channel_busy_until`/duty ledger driving the radio wrapper | drives a raw frame out the radio on-device; native mock proves the facade logic |

**Non-goals:** no protocol/codec logic (codec + R tracks); no NV/flash; no OTA;
**no edits to any vendored MeshCore file** (the entire point — §2); no sim backend
(S2/S3); no `Dispatcher` MAC (§0).

---

## 2. Vendoring strategy (the core of H0)

**Layout — mirror MeshCore's tree so internal `#include`s resolve unmodified.** A
new PlatformIO library `lib/meshcore/` whose `src/` root maps onto MeshCore's
`src/`:

```
MeshRoute/
  lib/meshcore/              # vendored 1:1 from MeshCore v1.10 — DO NOT EDIT
    src/
      Dispatcher.h           # for mesh::Radio + mesh::MillisecondClock bases ONLY (not the MAC)
      MeshCore.h             # MainBoard / RTCClock / RNG base classes
      helpers/
        ArduinoHelpers.h     # ArduinoMillis, StdRNG
        radiolib/
          RadioLibWrappers.h/.cpp
          CustomSX1262.h
          CustomSX1262Wrapper.h
          SX126xReset.h
        NRF52Board.h/.cpp    # XIAO board base
        ESP32Board.h/.cpp    # Heltec board base (backup)
    variants/
      xiao_nrf52/...         # board class + target.cpp
      heltec_v3/...
    NOTICE                   # origin, version, file list, MIT (§7)
    license.txt              # MeshCore's MIT license, verbatim
  lib/hal/                   # MeshRoute-OWNED device HAL — ALL customization lives HERE
    iclock.h                 # IClock seam (H0)
    timer_wheel.h/.cpp       # after()/cancel() (H1)
    meshroute_sx1262.h       # CustomSX1262 subclass: preamble IRQ (H2)
    device_hal.h/.cpp        # the meshroute::Hal device impl (H3)
```

PlatformIO auto-adds `lib/meshcore/src` to the include path, so MeshCore's
relative includes (`#include "helpers/radiolib/CustomSX1262.h"`) resolve with
**zero edits**.

**1:1 / no edits.** Every file under `lib/meshcore/` is byte-identical to upstream
MeshCore v1.10. All MeshRoute behaviour — the preamble subclass, the timer wheel,
the device Hal — lives under `lib/hal/`, *composing/subclassing* the vendored
types. The survey confirmed this suffices (preamble IRQ reachable by subclassing
`CustomSX1262`; timer wheel standalone over `getMillis()`).

**Re-sync.** `tools/vendor_meshcore.sh <meshcore-checkout>` copies exactly the
listed files from a **pinned** MeshCore commit into `lib/meshcore/`; a subsequent
`git diff` then shows precisely what upstream changed. Pin the MeshCore version
the way the Lua baseline is pinned — record `MESHCORE_VERSION` (v1.10) + the source
commit in `NOTICE`. Updating = re-run the script against a newer MeshCore, review
the diff, re-test. Flattening/renaming would break MeshCore's internal includes
(forcing edits) and turn the re-sync diff into noise — hence structure-preserving.

---

## 3. Exactly which files, and the PHY/MAC cut

From the survey (paths verified in `/home/staszek/MeshCore`):

**Vendor + use:**
- `src/helpers/radiolib/RadioLibWrappers.{h,cpp}` — `RadioLibWrapper : mesh::Radio`
  (`startSendRaw`, `recvRaw`, `startReceive`, `isChannelActive` (RSSI-LBT
  *primitive*), `getLastSNR/RSSI`, `idle`/standby, ISR registration).
- `src/helpers/radiolib/CustomSX1262.h` (+ `CustomSX1262Wrapper.h`,
  `SX126xReset.h`) — `CustomSX1262 : RadioLib::SX1262`, `std_init()` one-call
  bring-up (begin, CRC, TCXO, DIO2-as-rf-switch, RXEN, RX-boost).
- `src/helpers/ArduinoHelpers.h` — `ArduinoMillis` (→ `IClock`), `StdRNG`.
- `src/MeshCore.h` — `MainBoard`/`RTCClock`/`RNG` bases the board classes need.
- `variants/xiao_nrf52/*`, `src/helpers/NRF52Board.{h,cpp}` (primary);
  `variants/heltec_v3/*`, `src/helpers/ESP32Board.{h,cpp}` (backup).

**Vendor for the interface only (NOT the engine):**
- `src/Dispatcher.h` — declares `mesh::Radio` (`:22`) + `mesh::MillisecondClock`
  (`:14`), which the wrapper inherits. We do **not** vendor `Dispatcher.cpp` and do
  **not** instantiate `Dispatcher` (its duty-cycle/LBT/scheduling MAC — §0).
  *Verify (Q3): `RadioLibWrappers.cpp` compiles/links without `Dispatcher.cpp`; if
  `Dispatcher.h` drags inline deps (PacketManager/Packet), vendor those headers too
  (still 1:1) rather than extracting (extraction = an edit).*

**Do NOT vendor:** `Dispatcher.cpp`, routing/packet/mesh-table layer, BLE/serial
transports, the MeshCore app — none of it is PHY.

---

## 4. Device `Hal` mapping (H3) — each S2 §3 method → its device source

| `Hal` method | Device implementation | Parity note |
|---|---|---|
| `tx(bytes,len,TxParams)` | apply `TxParams` (`setSpreadingFactor/Bandwidth/CodingRate/OutputPower/preamble` on the RadioLib object) → `RadioLibWrapper::startSendRaw`; map result → `TxResult`; record airtime into **our** log | duty-cycle *decision* is the protocol's (it calls `airtime_used_ms` first); Hal just transmits + logs |
| `set_rx_sf(sf)` | `setSpreadingFactor(sf)` + re-arm `startReceive`; arm our blind-window | clamp 5..12, ignore OOR (matches `api_set_rx_sf`) |
| `channel_busy_until()` | from the radio **primitive** `isChannelActive()`/`isReceiving()` (RSSI + in-progress), wrapped into a "busy-until" hold | **our** LBT policy, not MeshCore's `Dispatcher` — must match the Lua's `channel_busy_until` |
| `airtime_used_ms(window)` / `oldest_tx_end_ms()` | **our** sliding-window airtime log, fed on tx completion | identical algorithm to `ScriptedNode`'s log → device == sim == Lua |
| `now()` | `IClock::now_ms()` → `ArduinoMillis` on device | §5 |
| `after`/`cancel` | **our** timer wheel (H1) over `IClock` | bounded id allocator (S2 §3) |
| `rand_range(lo,hi)` | device RNG (`StdRNG`/HW), `[lo,hi)` | determinism only matters in sim; device seeds a real RNG at boot |
| `emit(type,fields[])` / `log` | USB-CDC behind `#ifdef MESHROUTE_VERBOSE_EVENTS`; else no-op | the *sim* backend is where `emit` must be NDJSON-faithful, not the device |
| `set_protocol_id(id)` | store (→ NV later, deferred) | join/lease |
| `panic(why)` | log + safe halt/reset | exception-free fatal hook |

---

## 5. `IClock` — the testability seam (H0)

```cpp
// MeshRoute — lib/hal/iclock.h
namespace meshroute {
struct IClock { virtual ~IClock() = default; virtual uint64_t now_ms() = 0; };
}
```
- **Device:** `ArduinoClock : IClock` → MeshCore's `ArduinoMillis`/`millis()`.
- **Native test:** `FakeClock : IClock` with a settable `now` — lets H1's
  timer-wheel ordering/cancel tests and H3's duty-ledger tests run deterministically
  on the host **without** real `millis()` or RadioLib.

The device `Hal`'s `now()` delegates to its `IClock`. (The *sim* backend does not
use `IClock` — `FirmwareNode` drives `now()` from the simulator's VirtualClock;
`IClock` is device/native-test only.)

---

## 6. Build & the "native compiles" subtlety

PORT_PLAN H0 says "compile under `xiao_sx1262` + `native`." The honest refinement:
**the vendored RadioLib/SX1262 glue cannot compile on `native`** — it needs
RadioLib + Arduino, and PORT_PLAN §2.1 explicitly rejects building Arduino shims
(the `meshcore_real_sim` approach we're avoiding). So:

- **`xiao_sx1262` / `heltec_v3`:** compile `lib/meshcore` (vendored PHY) +
  `lib/hal`. Add `-DRADIOLIB_GODMODE=1 -DRADIOLIB_STATIC_ONLY=1` to match MeshCore
  (GODMODE exposes `setDioIrqParams` for the H2 subclass; STATIC_ONLY suits
  embedded). Deps already present: `RadioLib`, `Crypto`.
  **Pin macros — use MeshCore's names, not MeshRoute's.** The vendored
  `std_init()` reads `P_LORA_NSS`/`P_LORA_DIO_1`/`P_LORA_RESET`/`P_LORA_BUSY`,
  `SX126X_RXEN`, `SX126X_DIO2_AS_RF_SWITCH`, `SX126X_DIO3_TCXO_VOLTAGE`,
  `LORA_TX_POWER`, `SX126X_CURRENT_LIMIT`, `SX126X_RX_BOOSTED_GAIN`. Our device
  `-D`s must define **those** names (XIAO values NSS=D4/DIO1=D1/RST=D2/BUSY=D3,
  RXEN=D5, DIO2-rf-switch=1, TCXO=1.8, TX=22 — identical to MeshCore's XIAO map).
  The current `platformio.ini`'s `LORA_PIN_NSS=4`… are MeshRoute's own names;
  reconcile (add MeshCore's macro names for the vendored code; keep `LORA_PIN_*`
  only if MeshRoute-owned code uses them).
- **`native`:** compile `lib/hal`'s **logic** (`timer_wheel`, the duty-ledger, the
  `Hal` facade shape) against a `FakeClock` + a `MockRadio` seam — **not** the
  vendored RadioLib files (exclude them from the native env via lib filter). So
  "native compiles" = the MeshRoute-owned device-HAL logic is host-testable; the
  vendored PHY is device-only. Keep `device_hal.cpp`'s RadioLib calls in a
  device-only TU behind a thin internal seam so the testable logic links on native.

---

## 7. Attribution & license (refines PORT_PLAN D6)

PORT_PLAN D6 proposed a per-file origin header on each vendored file. **That
conflicts with the 1:1 byte-identical constraint** (a header edit makes the
re-sync diff noisy and the file non-identical). MIT doesn't require per-file
headers — only that the copyright + permission notice ship with copies. MeshCore
ships its notice in `license.txt` (© 2025 Scott Powell / rippleradios.com), no
per-file headers. So:

- **Do NOT add per-file headers** — keep vendored files byte-identical.
- `lib/meshcore/license.txt` = MeshCore's MIT license, verbatim.
- `lib/meshcore/NOTICE` = origin, pinned `MESHCORE_VERSION` + commit, the exact
  vendored file list, "vendored unmodified; do not edit — see
  `tools/vendor_meshcore.sh`."
- Add a MeshCore entry to MeshRoute's top-level `NOTICE.md`.
- MeshRoute stays BSD-3; incorporating MIT files under a marked dir is fine (D6).

---

## 8. Files (H0 only)

| File | Change |
|---|---|
| `lib/meshcore/src/...`, `variants/...` | NEW — vendored MeshCore PHY (byte-identical, §3 list) |
| `lib/meshcore/{NOTICE,license.txt,library.json}` | NEW — attribution (§7) + PlatformIO lib manifest |
| `lib/hal/iclock.h` | NEW — `IClock` + `ArduinoClock` + `FakeClock` |
| `tools/vendor_meshcore.sh` | NEW — reproducible re-sync from a pinned MeshCore checkout |
| `platformio.ini` | device envs gain `lib/meshcore`+`lib/hal`, GODMODE/STATIC_ONLY, MeshCore pin-macro `-D`s; native lib filter excludes vendored RadioLib |
| `NOTICE.md` | MeshCore MIT entry |

(H1 adds `lib/hal/timer_wheel.*` + tests; H2 adds `meshroute_sx1262.h`; H3 adds
`device_hal.*` + a native mock-radio test.)

---

## 9. Risks

- **GODMODE dependency** for the H2 subclass. Mitigation: the subclass route works
  even *without* GODMODE (`setDioIrqParams` is `protected` → reachable from a
  subclass; only direct *external* calls need GODMODE), and the chip's IRQ-status
  register self-latches preamble (poll fallback). Prefer the subclass over calling
  protected methods directly.
- **`startReceive` re-arm mask** (survey): MeshCore's `recvRaw()` re-arms via base
  `SX126x::startReceive`, which rewrites the DIO1 mask to RX_DONE-only each cycle.
  Our `MeshRouteSX1262::startReceive()` override re-applies the preamble mask every
  time — correct via the vtable, since MeshCore calls through `PhysicalLayer*` into
  our subclass. Document, don't fight it.
- **Upstream MeshCore API drift** (the `mesh::Radio` virtual set, `RadioLibWrapper`
  state machine, dropping GODMODE). Mitigation: pinned version + diffable vendor
  tree make drift explicit on re-sync; we adapt our thin `lib/hal`, never the
  vendored files.
- **`Dispatcher.h` transitive includes** (Q3): may drag PacketManager/Packet.
  Mitigation: vendor those headers too (still 1:1), or accept a larger PHY set; do
  NOT extract.
- **native vs RadioLib** (§6): handled by the device-only TU seam + mock radio.

---

## 10. Open questions / decisions for sign-off

1. **PHY-not-MAC cut (§0)** — vendor the radio PHY but implement duty-cycle / LBT /
   scheduling ourselves (our protocol owns them; parity with Lua), i.e. drop
   PORT_PLAN §3's "Dispatcher duty-cycle bonus"? **Rec: yes** (this is the
   "slightly different approach" the Lua taught us).
2. **Vendor dir** — `lib/meshcore/` as a PlatformIO library (rec) vs `vendor/`
   raw + manual include path?
3. **`Dispatcher.h` cut** — vendor it whole for the `mesh::Radio`/`MillisecondClock`
   bases and not use the engine (rec); verify `RadioLibWrapper` links without
   `Dispatcher.cpp`.
4. **Attribution (§7)** — dir-level `NOTICE` + bundled `license.txt`, **no per-file
   headers**, overriding PORT_PLAN D6? **Rec: yes** (keeps byte-identical re-sync).
5. **MeshCore version to pin** — current `~/MeshCore` (v1.10)? Record the commit.
6. **`heltec_v3` now or later** — vendor both boards in H0 (rec — PHY siblings,
   trivial extra) or XIAO-only first?
7. **`IClock` vs `Hal.now()`** — keep `IClock` as the device/native-test seam under
   `Hal.now()` (rec), or fold the clock into the Hal impl directly?
