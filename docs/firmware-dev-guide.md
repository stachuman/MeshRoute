# MeshRoute Firmware — Build, Debug & Upload Guide

Practical instructions for building the firmware, debugging it (host + on-metal, with hardware suggestions), and flashing it to a device.

**Toolchain:** [PlatformIO](https://platformio.org/). Pin to **PlatformIO Core 6.1.19** (`pio --version`) so every machine resolves identical platform/toolchain packages. Either the **VS Code PlatformIO IDE** extension (bundles everything) or the standalone CLI (`pipx install platformio==6.1.19`).

**Three build environments** (`platformio.ini`):

| Env | Target | Purpose | Radio |
|---|---|---|---|
| `native` | host (your dev machine) | doctest suite + the simulator driver | none (logic only) |
| `xiao_sx1262` | Seeed XIAO nRF52840 + Wio-SX1262 | **primary** device | SX1262 |
| `heltec_v3` | Heltec WiFi LoRa 32 V3 (ESP32-S3) | backup device | SX1262 |

> The protocol core (`lib/core`) is platform-neutral and depends only on the abstract `Hal`. The same code runs on the host (under the simulator) and on the device (over the MeshCore-vendored SX1262 PHY) — so **most development and debugging happens on `native`**, and the device builds prove it on metal.

---

## 1. Building

### Prerequisites
- PlatformIO Core 6.1.19 + `git`.
- A clone of this repo. The device build needs files **vendored from MeshCore** that are already committed here: `lib/meshcore/` (the 2 SX1262 PHY headers), `boards/seeed-xiao-afruitnrf52-nrf52840.json` + `boards/nrf52840_s140_v7.ld` (custom board + linker script), `variants/Seeed_XIAO_nRF52840/` (the board variant). Re-sync them from a pinned MeshCore checkout with `tools/vendor_meshcore.sh` (needs `bash`; Git Bash on Windows).

### Native (tests + simulator driver)
```bash
pio test -e native          # build + run the doctest unit suite
pio run  -e native          # build the simulator driver (src/sim_main.cpp)
```

> **Gotcha — PlatformIO prints `0 test cases`.** The suite provides its own doctest `main()` (in `test/test_airtime.cpp`), which PlatformIO's reporter doesn't parse — so `pio test` shows `native:* [PASSED]` but `0 test cases: 0 succeeded`. The tests **did** run. To see the real pass/fail counts, run the built binary directly:
> ```bash
> .pio/build/native/program                       # -> "test cases: N | N passed | 0 failed"
> .pio/build/native/program -tc="parse_command*"  # run a subset by name (doctest -tc glob)
> ```

### Device — XIAO nRF52840 (primary)
```bash
pio run -e xiao_sx1262
```
First run downloads the **Adafruit nRF52 BSP** (`framework-arduinoadafruitnrf52 @ 1.10700.0`) and a **GCC-12 ARM toolchain** (`toolchain-gccarmnoneeabi @ 1.120301.0`, bumped from the stock GCC-7 because `lib/core` needs C++20 `<span>`) — several hundred MB, one-time. The board id `seeed-xiao-afruitnrf52-nrf52840` resolves from the vendored `boards/` JSON; the S140 softdevice + variant come from the Adafruit BSP + `variants/`.

- **RF / pins are `-D` macros** (see `platformio.ini [common]` + `[env:xiao_sx1262]`). Note `LORA_FREQ`/`LORA_BW`/`LORA_SF` are only the `std_init()` bring-up defaults + banner values — the **operating** freq/BW/SF are runtime node config (the protocol retunes SF live). Control pins (`LORA_PIN_NSS=D4`/`DIO1=D1`/`RST=D2`/`BUSY=D3`) go to the RadioLib `Module(...)` ctor; `SX126X_*` (RXEN=D5, TCXO 1.8 V, DIO2-as-rf-switch, …) are read inside `std_init`.
- **What H0 builds today:** `src/fw_main.cpp` — boot banner + `std_init()` the radio + a 5 s "heartbeat". The Node/MAC/console wiring lands in H1–H3.

### Device — Heltec V3 (backup)
```bash
pio run -e heltec_v3        # ESP32-S3; dedicated SPI bus (P_LORA_SCLK/MISO/MOSI)
```

### Artifacts
`.pio/build/<env>/firmware.elf` (+ `.hex`, and `.uf2` for the XIAO). Use `firmware.elf` for debugging, `firmware.uf2` for drag-drop flashing.

### Cross-platform note (Windows)
The device build is OS-independent (PlatformIO downloads the same toolchain). Windows specifics: pin pio 6.1.19; set `git config core.autocrlf false` (or a `.gitattributes` with `lib/meshcore/** -text`) to keep vendored files byte-identical; keep the repo near the drive root (deep `.pio` paths vs. the 260-char limit); the `native` env needs a host g++ (MinGW-w64) — easiest to run the test suite on Linux and use Windows for device build + flash. UF2 drag-drop (below) sidesteps COM/driver setup.

---

## 2. Debugging

Three tiers, in order of leverage. **Reach for the host first** — the protocol is fully reproducible there; save on-metal debugging for PHY/timing/bring-up.

### Tier 1 — Native (host): your primary tool
The whole point of the platform-neutral core: debug protocol logic on your dev machine, deterministically, no hardware.
- **Unit suite:** `pio test -e native` (+ run the binary directly for real output, above). Filter to the area you're poking: `.pio/build/native/program -tc="<TEST_CASE glob>"`.
- **Source-level gdb:**
  ```bash
  pio debug -e native        # gdb on lib/core + the sim driver; breakpoints in node_mac.cpp etc.
  ```
- **The simulator** (`lora-universal-simulator`) drives many-node scenarios with a fixed RNG — the richest, reproducible environment for routing/MAC bugs. A failing scenario is a deterministic repro you can step through.
- Reach for Tiers 2–3 only when the bug is physical (radio init, IRQ timing, hardfault) — not for protocol logic.

### Tier 2 — On-device serial (USB-CDC / printf)
The device logs over USB-CDC at **115200 baud**.
```bash
pio device list                    # find the port
pio device monitor -e xiao_sx1262  # 115200; Ctrl-C to exit
```
- **H0 today:** the boot banner (freq/sf/bw/cr/pins/board) → `radio = OK` (or `INIT FAILED`) → `heartbeat` every 5 s. Seeing the banner + `OK` confirms flash + radio bring-up on metal.
- **Later (H1–H3):** the device console (line commands in, NDJSON events out — see `docs/specs/2026-05-30-device-console-design.md`) plus `emit`/`log` telemetry behind `-DMESHROUTE_VERBOSE_EVENTS`.

### Tier 3 — On-device hardware (SWD)
For stepping firmware on the chip — radio register issues, IRQ timing, hardfaults. `platformio.ini` is preconfigured (`debug_tool = cmsis-dap`, `debug_init_break = tbreak setup`):
```bash
pio debug -e xiao_sx1262     # halts at setup(); step the real silicon
```

**Suggested debug hardware (pick one probe):**

| Probe | ~Cost | `debug_tool` | Notes |
|---|---|---|---|
| **Raspberry Pi Pico** as a CMSIS-DAP probe (flash "debugprobe"/picoprobe firmware) | ~$4 | `cmsis-dap` | Cheapest; works out-of-the-box with the current config. **Recommended budget pick.** |
| **Raspberry Pi Debug Probe** (packaged picoprobe) | ~$12 | `cmsis-dap` | Same, tidier; JST-SH cables included. |
| **SEGGER J-Link** (EDU mini ~$20 / full) | $20+ | `jlink` | Nordic's first-class nRF52 debugger; unlocks **RTT** logging + nRF Connect tooling. Best if you'll do a lot of nRF work. |

**Wiring the XIAO nRF52840:** SWDIO + SWCLK are on **small test pads on the *underside*** of the board (not the castellated edge), plus GND (and optionally 3V3/RESET). They are *not* broken out by the Seeed expansion board — budget for soldering fine wires to the pads or a pogo-pin jig. With a J-Link you also get **SEGGER RTT**: high-throughput trace that doesn't compete with the USB-CDC console — handy for timing-sensitive logging.

**PHY / over-the-air debugging:** the real protocol check is a **2-device exchange** (two XIAOs running the H3 beacon+DM, watching both serial consoles). To inspect the actual RF — confirm a TX happened, eyeball timing/duty — a **second SX1262 node in RX-sniff mode** or an **RTL-SDR/HackRF on 869.4625 MHz** works; optional, since the 2-node console exchange covers most cases.

---

## 3. Uploading to a device

### XIAO nRF52840 (primary)
```bash
pio run -e xiao_sx1262 -t upload
```
`upload_protocol = nrfutil` needs the board in **bootloader mode**: **double-tap the reset button** → the board enumerates as a serial port *and* a UF2 mass-storage drive. Find the port with `pio device list`.

**UF2 drag-drop fallback (no tools, most robust — esp. on Windows):**
1. Double-tap reset → a USB drive appears (e.g. `XIAO-SENSE`).
2. Copy `.pio/build/xiao_sx1262/firmware.uf2` onto it.
3. The board flashes and reboots automatically.

Then watch it come up:
```bash
pio device monitor          # expect: boot banner -> radio = OK -> heartbeat
```
Or do it all at once: `pio run -e xiao_sx1262 -t upload && pio device monitor`.

### Heltec V3 (backup)
```bash
pio run -e heltec_v3 -t upload     # esptool over USB; auto-enters bootloader
```

### Troubleshooting
- **`No device found` / upload hangs (XIAO):** enter the bootloader (double-tap reset) and re-run; verify `pio device list` shows the port; use a *data* USB cable (not charge-only). When in doubt, use the UF2 drag-drop path.
- **`radio = INIT FAILED` on boot:** check the Wio-SX1262 shield seating, the pin macros (`LORA_PIN_*`/`SX126X_RXEN`), and `SX126X_DIO3_TCXO_VOLTAGE` (1.8 V for the Wio module). `std_init` retries once at TCXO 0 V on `-707/-706`.
- **Windows COM/driver issues:** Win10/11 has the CDC driver built in for the Adafruit bootloader; if the port doesn't appear, the UF2 drag-drop path needs no driver at all.

---

### Quick reference
```bash
# build
pio test -e native              &&  .pio/build/native/program   # host tests (real counts)
pio run  -e xiao_sx1262                                          # device firmware
# debug
pio debug -e native                                              # host gdb (logic)
pio device monitor -e xiao_sx1262                                # serial (115200)
pio debug -e xiao_sx1262                                         # SWD (needs a CMSIS-DAP probe)
# upload
pio run -e xiao_sx1262 -t upload                                 # nrfutil (double-tap reset)
#   or drag .pio/build/xiao_sx1262/firmware.uf2 onto the XIAO drive
```
