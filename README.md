# MeshRoute

C++ firmware port of the `dv_dual_sf` LoRa-mesh protocol.

**Status:** iteration 1 scaffold. Native build compiles and runs unit
tests; firmware build compiles to a heartbeat-print skeleton. Wire
codecs, routing, MAC, beacon, join, and gateway state machines arrive
in subsequent iterations, in the order they were built in the Lua
model — see `docs/PORT_NOTES.md` for the plan.

## Spec of record

The protocol's behaviour is defined by `spec/dv_dual_sf.lua`, which is
a symlink to the live Lua firmware model in the sibling
`lora-universal-simulator` repository. **Don't reverse-engineer the
protocol from this C++ code** — the Lua side is canonical, this side
is a port. Spec docs are also symlinked under `spec/docs/` so the
PROTOCOL / ROADMAP / SCENARIOS reference material is always current.

If you receive a copy of MeshRoute outside the development environment
(no sibling Lua repo), run `tools/sync_spec.sh` to hard-copy the spec
from a tarball URL or a local path.

## RF plan

The deployment-target RF plan is locked in `platformio.ini`'s
`[common]` section:

| Parameter | Value | Source |
|---|---|---|
| Frequency | 869.4625 MHz | CEPT g3 sub-band, clear of Meshtastic + MeshCore |
| Bandwidth | 125 kHz | LoRa BW; fits in 250 kHz g3 envelope |
| Spreading factor | SF8 | working default; per-deployment tunable |
| Coding rate | 4/5 | matches Meshtastic / MeshCore default |
| Duty cycle | 10% | g3 sub-band allows 10× the g1 budget |
| Region | EU868 (Poland) | CEPT/ERC 70-03 Annex 1, ECC/DEC (01)04 |

## Build

```bash
# Native unit tests + sim driver
pio test -e native
pio run  -e native

# Firmware on Seeed XIAO nRF52840 + Wio-SX1262
pio run -e xiao_sx1262
pio run -e xiao_sx1262 -t upload

# Backup target: Heltec WiFi LoRa 32 V3
pio run -e heltec_v3
```

## Layout

```
MeshRoute/
├── platformio.ini            multi-env build (native + xiao + heltec)
├── lib/core/                 the protocol port itself
│   ├── protocol_constants.h  PROTOCOL = {...} block, byte-for-byte from Lua
│   ├── airtime.{h,cpp}       LoRa airtime calculation
│   ├── frame_codec.{h,cpp}   wire-format encoders / decoders
│   └── library.json          PlatformIO library metadata
├── src/
│   ├── sim_main.cpp          native sim driver (loads scenarios)
│   └── fw_main.cpp           firmware entry (boards only)
├── test/                     doctest unit tests, run via `pio test -e native`
├── spec/                     symlinks to lora-universal-simulator
│                             — dv_dual_sf.lua, docs/, test/, scenarios/
├── tools/                    helper scripts
└── docs/PORT_NOTES.md        port-progress tracker, iteration plan
```

## License

BSD-3-Clause — see `LICENSE`. Third-party attributions in `NOTICE.md`.
