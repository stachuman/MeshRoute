# MeshRoute — Architecture (source-module map)

*Where the code lives and how the pieces fit.* This is the **navigation map**, not the protocol story:
- the *protocol* narrative → [docs/how-it-works.md](docs/how-it-works.md)
- the *wire* format → [docs/frames.md](docs/frames.md) · [docs/protocol.md](docs/protocol.md)
- *build / debug / upload* → [docs/firmware-dev-guide.md](docs/firmware-dev-guide.md)

---

## The one big idea: one engine, two backends

The protocol engine (`lib/core`) depends **only** on the HAL interface (`lib/core/hal.h`) — no Arduino, no RadioLib, no JSON. So the *identical* engine runs on:

- **real hardware** — the device HAL (`lib/hal`) + the Arduino firmware (`src/`), on nRF52 and ESP32.
- **the simulator** — a host HAL in the external `lora-universal-simulator` repo, whose `lus` binary compiles `lib/core` directly and drives it as `engine:meshroute`.

This is why the **`s18` simulator scenario is a real behavioral test of any `lib/core` change** — a byte-identical event stream proves the engine's behavior didn't move. (Firmware-only changes in `src/` are `s18`-inert by construction, since the sim never compiles `src/`.)

```
        src/ (Arduino firmware)          lora-universal-simulator (external repo)
                 │                                    │
          lib/hal (device HAL)                  host HAL (sim)
                 └──────────────┬───────────────────┘
                          lib/core/hal.h  (the seam)
                                │
                     lib/core  — the protocol engine
```

---

## `lib/core` — the protocol engine (platform-neutral, fixed-size, no heap in hot paths)

The heart of the system. Talks only to `hal.h`.

- **The Node** — `node.h` (the class + its private state, carved into per-concern sections) + `node_carriers.h` (the shared value types: `NodeConfig`/`RtEntry`/`TxItem`/`PendingTx`/…). The class *implementation* is split across **13 partial-class TUs** by concern:
  - `node.cpp` (spine/lifecycle) · `node_beacon` · `node_routing` + `node_route_discovery` (DV routing + F discovery) · `node_mac` + `node_mac_rx` (the RTS/CTS/DATA/ACK/NACK data plane) · `node_channel` + `node_cascade` (channel gossip + flood) · `node_hashlocate` (H hash-locate + E2E key exchange) · `node_join` (node-id DAD/heal) · `node_mobile` + `node_query` (mobile roaming + REQ_SYNC) · `node_budget` (airtime tiers).
- **Wire codec** — `frame_codec.cpp` + `wire.h` (pack/unpack every frame) · `frame_trace.h` (one-line decoded RX/TX trace).
- **Crypto** — `dm_crypto` (X25519 + XChaCha20-Poly1305 sealed-sender DM) · `admin_auth` (remote-management signing) · `identity` (Ed25519/X25519 derived from a seed).
- **Airtime & limits** — `airtime` (SX126x-exact airtime) · `protocol_constants.h` (every cap, TTL, and timing) · `leaf_config` (R6 lineage/epoch membership).
- **Inbox** — `inbox` + the persistence backends `fixed_inbox_store.h` (RAM ring) / `segmented_inbox_store.h`.
- **Fault log** — `fault_log` (platform-neutral crash ring + decode).
- **Feature split** — `mr_features.h` (compile-time `MR_FEAT_*` by role → a gateway/static build compiles the mobile/team planes *out*).

## The HAL seam — `lib/core/hal.h`

The interface the engine calls (radio TX/RX + config, clock, timers). Two implementations:

- **`lib/hal`** — the **device** backend: `device_hal` + `device_radio` / `iradio` (RadioLib SX1262 via the vendored `lib/meshcore` `CustomSX1262`) + `iclock` + `airtime_ledger` + `timer_wheel` + `radio_canary` + `mr_ui`.
- the **simulator's** host HAL lives in the sim repo (not here).

## `src/` — the device firmware (Arduino; nRF52 / ESP32)

Glue and drivers around the engine. `fw_main.cpp` = `setup`/`loop`/mesh-service/console. The rest is the post-review split **by responsibility** (`namespace mrfw`, reached from `fw_main` via `using`):

- **firmware clusters** — `firmware_commands` (the `dispatch` verb-router + all diagnostics) · `firmware_config` (+ `firmware_config_parse.h`; provisioning + gateway config) · `firmware_remote` (remote-management) · `firmware_inbox` (companion inbox sync). `fw_context.h` = the shared-globals seam (`extern` device stack + runtime state).
- **device drivers** — `device_ble` (BLE-NUS companion) · `device_nv` (NV blobs) · `device_inbox_store` (live QSPI/LittleFS inbox on nRF52) · `device_ota` · `device_fault` (ISR fault vectors — single-TU) · `device_rng` · `board_ui` (OLED).
- **transports / sinks** — `console_sink` (the one guarded USB-CDC sink) · `dispatch_sink` (`BufferSink`/`LineSink`).
- `sim_main.cpp` = the native/sim entry (excluded from the device build) · `sched_send.h` = the on-node scheduled-send test workload.

## `lib/console` — transport-neutral console encoders

`console_json` (companion JSON) · `console_binary` (remote TLV response encoders) · `console_parse` (the shared command grammar). Both USB and BLE route through the same `dispatch`, so the wire grammar can't drift between transports.

## Vendored (do not edit) — `lib/meshcore`, `lib/monocypher`

`lib/meshcore` = the RadioLib-only SX1262 PHY headers (`CustomSX1262`), vendored **PHY-not-MAC**. `lib/monocypher` = the crypto primitives.

---

## Build & test

- **11 PlatformIO envs**: `native` (doctest host build) + nRF52 (`xiao_sx1262`, `gateway`, `production`, `xiao_mobile`) + ESP32 (`heltec_v3`, `xiao_esp32s3`, `gateway_heltec`, `gateway_esp32s3`, `heltec_mobile`, `xiao_esp32s3_mobile`). Role/board selected by `MR_FEAT_*` + `MR_N_LAYERS`.
- **`test/`** — 27 doctest suites (run under `native`). **`simulation/`** — 21 scenarios driven by the external `lus`; **`s18` is the keystone** parity/regression scenario.

## Design specs

`docs/specs/` and `docs/superpowers/specs/` are the per-feature **design records**. Once a feature ships, **the code is the source of truth** (the specs are historical); implemented specs live under each dir's `archive/`, and the still-living/in-progress specs stay at the top level. Start at [docs/superpowers/specs/README.md](docs/superpowers/specs/README.md) for the convention + the current living specs.
