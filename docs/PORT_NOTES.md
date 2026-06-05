# Port progress + iteration plan

> ⚠️ **SUPERSEDED 2026-05-28 by `docs/PORT_PLAN.md`.** This file was
> written at scaffold time; its "byte-for-byte against Lua" codec
> criteria and from-scratch HAL plan are out of date (the C++ port
> targets the ROADMAP §10 cmd-nibble wire, and the HAL is reused from
> MeshCore). Kept for history. **Read `PORT_PLAN.md` for the current
> plan.**




## How to read this doc

`spec/dv_dual_sf.lua` is the firmware model — the authoritative
description of what the protocol does. This C++ port reproduces its
behaviour one module at a time, in the order the Lua model was
itself built. Each iteration below has a concrete success criterion;
once met, commit on `port-init`, then move on.

Cross-implementation differential testing is the primary verification
strategy. For each scenario JSON in `spec/test/` and `spec/scenarios/`,
both the Lua model and the C++ port should produce the same NDJSON
event sequence given the same seed. The RNG contract
(`spec/dv_dual_sf.lua` top-of-file comment block) and the iteration-
order caveat (use `std::map`, not `std::unordered_map`, where order is
observable) are what make this possible.

## Status

| Iteration | Scope | Success criterion | Status |
|---|---|---|---|
| 1 | Scaffold | `pio test -e native` passes; `pio run -e native` prints PROTOCOL summary | **CURRENT** |
| 2 | BCN codec | `pack_beacon` round-trips byte-for-byte against Lua | pending |
| 3 | RTS/CTS/ACK codec | All five frame types round-trip | pending |
| 4 | DATA / NACK / Q / J codec | Same, full §3 wire-format coverage | pending |
| 5 | Single-node beacon emit | Native sim emits BCN frames at the right cadence; matches Lua's beacon_tx events | pending |
| 6 | Routing table + DV merge | rt_merge, K=3 alt slots, 3-cycle prune; t10/t12 equivalents pass | pending |
| 7 | MAC: RTS-CTS-DATA-ACK | t01-equivalent passes via native sim | pending |
| 8 | Beacon throttle + triggered | t29/t42 equivalents pass | pending |
| 9 | F1 blind window | t14/t20 equivalents pass | pending |
| 10 | Cascade requeue | t26-style cascade walks alternatives | pending |
| 11 | Q frames | ROUTE_QUERY + REQ_SYNC + HASH_QUERY | pending |
| 12 | Join state machine | t46-t60 equivalents pass | pending |
| 13 | Gateway + cross-layer | s09/s10 equivalents pass | pending |
| 14 | Mobile + asymmetric | s07/s08 equivalents pass | pending |
| 15 | First on-device flash | XIAO nRF52840 sends + receives one BCN over the air | pending |
| 16 | Two-board over-the-air | RTS-CTS-DATA-ACK round-trips between two XIAOs | pending |

## Iteration 1 deliverables (current)

- `platformio.ini` with `native`, `xiao_sx1262`, `heltec_v3` envs.
- `lib/core/protocol_constants.h` — port of the Lua PROTOCOL block.
- `lib/core/airtime.{h,cpp}` — `airtime_ms()` port with doctest cases
  pinning the output against Lua-captured baselines.
- `lib/core/frame_codec.{h,cpp}` — function signatures only; iteration 2
  fills them in.
- `src/sim_main.cpp` — prints the protocol summary (verifies the build
  wires together).
- `src/fw_main.cpp` — heartbeat-print skeleton, compiles for xiao + heltec.
- `test/test_airtime.cpp`, `test/test_protocol_constants.cpp` — unit
  tests for the things we ported.
- `spec/` — symlinks to `lora-universal-simulator` artifacts.
- `docs/PORT_NOTES.md` — this file.
- `LICENSE` (BSD-3) and `NOTICE.md` (third-party).

## Coding conventions

| Rule | Why |
|---|---|
| C++20 | nRF52 (arm-none-eabi gcc 11+) and ESP32 (xtensa-esp32-elf 11+) both ship modern compilers; we use `std::span`, `std::optional`, designated initializers |
| `snake_case` everywhere (functions, vars, types) | One-to-one mapping to the Lua spec, eases diff review |
| No exceptions (`-fno-exceptions`), no RTTI (`-fno-rtti`) | Embedded discipline; spares ~10-20 KB flash |
| No heap allocations in hot paths | Same. Use fixed-size containers; the PROTOCOL caps drive sizing |
| `std::span` / `std::optional` over raw pointers + sentinel | Safer + zero-cost in C++17 |
| `std::map` or sort-before-iterate where iteration order is observable | Cross-implementation determinism with the Lua model |
| Public protocol functions go in `namespace meshroute::` | Avoid name collisions with RadioLib / Arduino core |
| One-to-one with Lua line numbers in comments where useful | Eases the diff-debug loop |

## When you commit a port iteration

1. `pio test -e native` — all unit tests pass
2. `pio run -e native` — sim driver compiles + runs
3. `pio run -e xiao_sx1262` and `pio run -e heltec_v3` — both firmware
   targets compile (don't need to flash every time; just verify no
   regressions on either platform)
4. For iteration 2+: regenerate the differential test baselines from
   the Lua side and pin them into the C++ tests
5. Commit with `Iteration N: <scope>` subject + the success criterion
   from the table above as the trailing line

## Open questions deferred to later iterations

- **Time source.** `millis()` on hardware, but native sim needs a
  virtual clock. Plan: an `IClock` interface that `arduino-millis`
  and `virtual-clock` both implement. Iteration 2.
- **Event emit format on hardware.** Same NDJSON as Lua, over USB CDC.
  Behind `#ifdef VERBOSE_EVENTS` so production builds can compile them
  out. Iteration 5 (first time we have events to emit).
- **NV (flash) layer.** nRF52 has internal flash; need a small
  key-value store for `claim_epoch` etc. Possibly LittleFS, possibly
  raw flash + CRC. Iteration 12 (when join lands and needs NV).
- **OTA update.** nRF52 + Adafruit nRF52 bootloader supports DFU via
  USB. Multi-board OTA over LoRa is a much bigger problem; parked.
