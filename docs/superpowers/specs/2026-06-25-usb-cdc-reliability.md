<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# USB-CDC reliability — stop self-inflicted serial flooding (the MeshCore lesson) + never block the loop

**Status:** coder instruction. The user commits + flashes + benches; I gate. nRF52840 (the Adafruit TinyUSB CDC); ESP32 mostly inert (different USB) but the gating + flush guards are platform-neutral. No wire change.

## Why (the MeshCore comparison, 2026-06-24/25)

The node "wedge" (serial dies, mesh keeps running, power-cycle to recover) is **self-inflicted USB flooding**. Compared head-to-head with `~/MeshCore` (same nRF52840 / Adafruit-TinyUSB stack, runs reliably): MeshCore is **not** more defensively coded — it has the SAME unguarded `Serial.print`, SAME ~256 B default CDC buffers, NO write guards, NO USB watchdog, and a blocking `Serial.flush()` just like ours. The **one** difference that matters: **MeshCore ships with all radio-hot-path logging compiled out** — `MESH_PACKET_LOGGING` is `#if`-gated and **commented out in every nRF52 variant** (one even annotated `NOTE: DO NOT ENABLE`). Its Serial only ever carries low-frequency interactive console, so it never floods, never blocks, never wedges.

We do the opposite: **always-on `[rxbad]`/`[txerr]`/`[txabort]`/`[↻ rx-freq]` prints in the radio hot path** (`lib/hal/device_radio.h`), so during the CRC storm (4–11 bad-RX/node) we blast Serial exactly when the radio is busiest — the loop blocks on a full TX FIFO, `service_console` stops running, and both directions wedge. Fix = stop flooding (match MeshCore) **and** add the guards MeshCore lacks so we never block even if something does flood (we're console-heavier than MeshCore — `rcmd`, richer diagnostics — so discipline alone isn't enough).

**User decisions (2026-06-25): all four + a production compile-out — (1) gate the hot path + a bad-RX counter; (2) arm-mode (done); (3) route ALL output through one guarded sink (`mrcon`); (4) bump the FIFO; (5) an `MR_CONSOLE` compile flag that EXCLUDES the entire USB serial from the production build.**

## Part 1 — silence the radio hot path + a bad-RX COUNTER (the biggest lever)

In `lib/hal/device_radio.h`, the unconditional prints fire on radio events: `[txerr]` (~79), `[txabort]` (~109), `[↻ rx-freq]` (~132), `[rxbad st=]` (~205). (`[↻ rx-sf]` ~120 is already commented out.)
- **Gate all four behind `debug on`** — wrap each in `if (meshroute::g_mr_trace_on) { … }` (the existing `debug on` flag from `frame_trace.h`; include it / use the extern). Default OFF = a silent radio path, exactly like MeshCore's gated `MESH_PACKET_LOGGING`. Keep the prints (don't delete) — `debug on` brings them back for bench debugging.
- **Add a bad-RX counter** so the CRC-storm metric survives the silencing: a file-scope `static volatile uint32_t g_rxbad_count` (next to `g_isr_count` ~47), `++`'d **unconditionally** at ~205 on `st != RADIOLIB_ERR_NONE` (the per-event PRINT is now gated; the COUNT always runs), + a `uint32_t rxbad_count() const` getter (next to `isr_count()` ~214).
- **Expose it in `status`** — add ` rxbad=` to the console `[status]` line (fw_main ~307, beside `isr=`/`txto=`) AND to the **rcmd status response** (fw_main ~1155, the `up=… txto=… duty_ms=…` string) so `rcmd <node> status` reports it over the air. Now the harness reads the bad-RX count as a clean counter delta (baseline→end), not a flood of per-event lines.

## Part 2 — stop the continuous live stream (DONE — usage side, no coder work)

The other flood source was the oracle's live serial stream. Already fixed by the **arm-and-read mode** (`tools/lab/armrun.py` + the firmware scheduled-send): USB is touched only at arm + read. Noted here for completeness; nothing to implement.

## Part 3 — route ALL console output through one sink (`mrcon`): the guards live here AND it's the seam for the compile-out

Introduce a single console-output object **`mrcon`** that every firmware print goes through, and migrate the ~310 `Serial.print/println/write/flush` OUTPUT sites to it. This is mechanical (`Serial.` → `mrcon.`); the ONLY direct `Serial` uses left are the INPUT path (`Serial.read/available` in `service_console`) and the structural `Serial.begin`/DTR-wait — those are handled by Part 5's `#if`. `mrcon` is a thin `Print`-derived wrapper whose `write()`:
- **drops, never blocks**, when `!Serial` (disconnected) or `Serial.availableForWrite() < n` (FIFO full) — so the loop NEVER stalls on a stalled host (the anti-wedge). No `setWriteTimeout` needed — `mrcon` simply never enters a blocking write.
- Dropping console output under extreme load is fine — the durable inbox is the truth, not the live stream.

Also guard the few remaining direct `Serial.flush()` (reboot/crashtest ~616/646/1105+) with `if (Serial)`, and **drop the in-`loop()` flushes** (~1868/1998) — the Adafruit background USB task drains the FIFO anyway, so a loop-body flush only risks a stall.

Centralizing here puts the guards in ONE place (not 310), AND gives Part 5 a single seam to flip. ⚠ Effort: the ~310-site migration is mechanical but real — it's the shared foundation for the guards (this part) and the compile-out (Part 5), so it's not throwaway. (Do NOT macro-`#define Serial` — that also rewrites the Adafruit core's own `Serial` object definition and breaks the build; the indirection must be our own object.)

## Part 4 — bump the CDC buffers + the console line buffer

- nRF52 `build_flags`: add **`-DCFG_TUD_CDC_TX_BUFSIZE=1024`** (absorb output bursts — the TX side that overflows) and **`-DCFG_TUD_CDC_RX_BUFSIZE=1024`** (the input side — a long `testsend … -t …` line must buffer without the host outrunning our `Serial.read`). ~1.5 KB extra RAM, trivially affordable on the nRF52840.
- **Bump the console line buffer** `line[512]`→`line[1024]` (fw_main `service_console` ~1717) + update the `>511`→`>1023` reject message. This lets the harness send longer `testsend` lines (fewer arm commands) and keeps the RX FIFO and the line buffer matched. (All `#if MR_CONSOLE` — moot in the production build, see Part 5.)

## Part 5 — `MR_CONSOLE` compile flag: exclude the ENTIRE USB serial from the production build

Build flag **`MR_CONSOLE`** (default **1** = dev/bench — the harness drives the node over USB). A production node sets **`-DMR_CONSOLE=0`** and gets NO USB serial at all:
- **`mrcon` becomes a `NullPrint`** (empty `write()`) → the ~310 output calls dead-code-eliminate; nothing ever reaches USB.
- **`service_console`, `Serial.begin`/the DTR-wait, and the direct `Serial.flush()` sites are `#if MR_CONSOLE`-compiled out** → no input console, and NO `Serial` symbol is referenced anywhere in our code.
- **`-DCFG_TUD_CDC=0`** (the production env) → the TinyUSB CDC class is excluded entirely: freed RAM/flash, no CDC enumeration, and **the USB-CDC failure surface is GONE — you can't wedge what isn't compiled in.**
- **Production diagnostics stay over-the-air:** the BLE-NUS console sink + `rcmd` (status/faults/version) + the persistent fault-log are untouched — exactly the path the user described ("production connects via BLE/WiFi"). A crashed production node still records to `/mrfault` and answers `rcmd faults` later.

Ship it via a dedicated `production` env (or `-DMR_CONSOLE=0 -DCFG_TUD_CDC=0` added to the gateway/leaf envs). The bench fleet keeps `MR_CONSOLE=1` (the harness needs the console). ⚠ The proof: with `MR_CONSOLE=0` a build-wide grep finds **zero** un-`#if`'d `Serial.` references, and it links with no `Adafruit_USBD_CDC`. Pairs with the BLE-companion initiative ([[companion-ota-inbox-initiative]]) — production is BLE-managed, console-free.

## Harness coordination (MINE — follow-on, not coder work)

- **Fix `schedule.py rx_budget`** (currently 800 — it EXCEEDS the present 512 line buffer, so arm-mode lines would be rejected): set it to ~460 to fit the current firmware NOW; raise toward ~960 after the Part-4 line-buffer bump lands. (I'll do this immediately so arm-mode works against the just-flashed firmware.)
- **Switch the oracle's CRC-storm metric** from the `events_<node>.log` `[rxbad st=-7]` scan (which Part 1 gates behind `debug on`) to the **`status rxbad=` counter delta** (baseline→end) — cleaner and always-on.

## Tests / gate

- **Native:** the bad-RX counter logic if any lands in `lib/core` (likely just the device HAL — device-only). The gating + buffers are device/build-only → no native surface beyond a compile.
- **Build (dev, `MR_CONSOLE=1`):** all 4 boards; confirm the `CFG_TUD_CDC_*` bumps compile and the line buffer fits RAM.
- **Build (production, `MR_CONSOLE=0 CFG_TUD_CDC=0`):** a new `production` env (or the flag on an existing one) **links clean** — a build-wide grep shows **zero un-`#if`'d `Serial.` references**, no `Adafruit_USBD_CDC` in the map, and a **smaller binary / freed RAM** vs the dev build. This is the new must-pass: the whole serial subsystem is genuinely excluded, not just disabled.
- **No sim impact:** device-HAL + build flags only; lib/core untouched (verify s18 unchanged).
- **Metal (the proof):** (a) default build (`debug` OFF) → trigger a CRC storm / heavy RX and confirm the console **stays responsive** (no lag→silence) and `status rxbad=` climbs; (b) `debug on` → the `[rxbad]`/`[txerr]` lines return; (c) **the wedge test** — run a heavy workload (or the old live-stream oracle) and confirm the node no longer wedges / needs a power-cycle; (d) a reboot/OTA with **no host attached** completes (the flush guard) rather than hanging; (e) a **`MR_CONSOLE=0` production node** boots, joins the mesh, and answers `rcmd status`/`faults` **over the air** while `meshroute_client` over USB sees nothing (no CDC) — production runs console-free, diagnostics via BLE/`rcmd`, fault-log intact.

## Sequencing

Part 1 is the highest-leverage and smallest — do it first; it alone likely matches MeshCore's working behavior. Parts 3+4 make us robust rather than merely disciplined. Independent of the fault-log-v3 LR + scheduled-send work; all in the hardware-hardening thread.
