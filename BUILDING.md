## Build preparation for MeshRoute

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

## Flashing a pre-built image on a low-power host

When the device is wired to a host too weak to compile (e.g. a Raspberry Pi),
build on a capable machine and flash the artifact on the host. Two verified
gotchas:

- **`pio run … -t upload` auto-cleans a cross-machine build dir.** When
  PlatformIO sees a `.pio/build/<env>/` produced under different conditions
  (different core/platform version, or a `platformio.ini` touched by the copy)
  it *wipes* it first — so `firmware.*` vanishes and `nobuild` reports
  "not found". Pass **`--disable-auto-clean`** to stop the wipe.
- **The artifact differs by MCU.** ESP32 flashes a directly-usable `.bin`;
  nRF52 flashes a `.zip` DFU package PlatformIO builds from the `.hex` (a step
  `-t nobuild` skips). So `nobuild` works for ESP32 but **cannot** for nRF52.

The robust path for both is to skip the host's build system and flash raw bytes
with the platform's own tool. Copy only the artifact(s) below to the host — not
the whole `.pio/build` tree.

### ESP32-S3 (esptool) — e.g. `xiao_esp32s3`

Keep PlatformIO, just disable the clean:

```bash
pio run --disable-auto-clean -e xiao_esp32s3 -t nobuild -t upload --upload-port /dev/ttyACM0
```

…or (recommended) merge a single factory image on the **build machine** and
dumb-flash it on the host. From `.pio/build/xiao_esp32s3/` (esptool ≥ 4.5):

```bash
python -m esptool --chip esp32s3 merge_bin -o factory.bin \
  --flash_mode qio --flash_size 8MB \
  0x0     bootloader.bin \
  0x8000  partitions.bin \
  0xe000  ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin \
  0x10000 firmware.bin
```

Copy only `factory.bin` to the host, then:

```bash
python -m esptool --chip esp32s3 --port /dev/ttyACM0 write_flash 0x0 factory.bin
```

`boot_app0.bin` resets the OTA-data partition so the bootloader boots the
freshly-flashed slot — required because this firmware ships OTA; the merged
image bakes it in. Offsets `0x0 / 0x8000 / 0xe000 / 0x10000` are the standard
ESP32-S3 Arduino layout; `qio` / `8MB` come from the `seeed_xiao_esp32s3` board.

**Factory reset vs. config-preserving update.** Flashing `factory.bin` at `0x0`
is a **factory reset**: `merge_bin` makes a contiguous image and fills gaps with
`0xFF`, and the gap between the partition table (`0x8000`) and `boot_app0`
(`0xe000`) is the **`nvs`** partition (`0x9000`, size `0x5000`) — where the
node's provisioning/config lives — so the device comes up blank. To **update the
firmware but keep the config** (the nRF52 DFU behaviour), flash only the app:
copy `firmware.bin` (+ `boot_app0.bin`) to the host instead of `factory.bin`,
then

```bash
esptool --chip esp32s3 --port /dev/ttyACM0 --no-stub write_flash \
  0xe000  boot_app0.bin \
  0x10000 firmware.bin
```

`0x10000` is `app0`; nothing below `0xe000` is touched, so `nvs` (config), the
partition table, and the bootloader survive. `0xe000 boot_app0.bin` resets
otadata so the bootloader boots the freshly-flashed slot — drop it only if the
device wasn't left on the OTA `app1` slot.

> **Host esptool gotcha.** Run esptool via PlatformIO's bundled copy —
> `python ~/.platformio/packages/tool-esptoolpy/esptool.py …` (v4.5.x, stubs
> embedded) — **not** the host's bare `esptool`. A distro `python3-esptool`
> 4.7.x connects to the chip but then dies with `FileNotFoundError:
> …stub_flasher_32s3.json`: that release moved the flasher stubs to external
> JSON data files the package often omits. Workarounds, in order: use
> PlatformIO's esptool; or add **`--no-stub`** (talks to the ROM loader,
> skipping the stub — slower but reliable); or upgrade the host to esptool
> ≥ 4.8 (`pipx install esptool`).

### nRF52840 (adafruit-nrfutil DFU) — e.g. `xiao_sx1262`

`-t nobuild` **cannot** flash this board — it skips the `.hex`→`.zip`
packaging, so PlatformIO hands the raw `.hex` to nrfutil (`-pkg firmware.hex`)
→ `BadZipFile`. Flash the **pre-built `firmware.zip`** directly — it sits in the
build output next to the `.hex`; copy *it* (not just the `.hex`) to the host:

```bash
cd .pio/build/xiao_sx1262
python ~/.platformio/packages/tool-adafruit-nrfutil/adafruit-nrfutil.py --verbose dfu serial \
  -pkg firmware.zip -p /dev/ttyACM1 -b 115200 --singlebank --touch 1200
```

`--touch 1200` performs the 1200 bps reset into the bootloader (PlatformIO does
this as a separate step). After the touch the board re-enumerates into the
bootloader — usually the same `ttyACM*`; adjust `-p` if it moves. `-b 115200` /
`--singlebank` match the platform's own DFU invocation. DFU writes only the
application region, so the node's config/NV survives the update — unlike the
ESP32 `factory.bin` reset above (use the app-only ESP32 command there to match
this behaviour).

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
