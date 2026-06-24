<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Persistent fault log + watchdog + on-demand `version` (nRF52840)

**Status:** coder instruction. The user does ALL commits — land GREEN + uncommitted, report ready, I gate. Firmware change; **no wire change**. **nRF52840 only** (XIAO + gateway); the ESP32-S3 envs must still **compile** (feature `#if`-guarded to a no-op) — their port is a later spec.

## Why

On real hardware a node occasionally **wedges** — the main loop stops servicing the console (4–5 s lag degrading to fully silent) and only a **power-cycle** recovers it. It is **pre-existing** (not the recent e2e-ack / channel-repair work) and **real-HW-only** (the sim has no radio/loop to hang). Today there is **no hardware watchdog** (only the *software* TX-watchdog `txto`) so a hang stays dark indefinitely, and **no fault record** survives, so a node that died at 3 a.m. tells you nothing when you reach it later. Since you can't connect to every node the moment it fails, the diagnosis must **persist in flash and be queryable on demand** — without resetting the node (a reset destroys the live degraded state you want to read).

**Decisions (user, 2026-06-24):** (1) **nRF52840 only** now; (2) **8 s hardware watchdog, runs through light-sleep, auto-resets a hang** (the enabler — a hang produces no reset reason unless the WDT forces one); (3) command surface = **`version`** (banner on demand, no reset) + **`faults`** (the flash history) + a **`reset=`** field in `status`.

## Verified current state

- **No fault/reset/WDT infra** (greenfield). `NRF_POWER->GPREGRET` is already used for the OTA reset (fw_main.cpp:582) — retained-register/RAM is available.
- **NV persistence** = Adafruit `InternalFS` (LittleFS) files (`/mrcfg`, `/mrid`, `/mrpeers`), packed-POD blobs (magic+version), `mrnv::load`/`save` = `remove`+`write` whole (device_nv.h:124-138). A new `/mrfault` ring follows this exactly.
- **Command dispatch** = a flat `if (len==N && !strncmp(line,"cmd",N))` table (fw_main.cpp:1011+) for the text console; a separate JSON console for the companion (~:1144).
- **Boot** (`setup`, fw_main.cpp:1198+): `Serial.begin`, a `while(!Serial && millis()<3000)` + `delay(2000)` settle, then the banner (`:1210` `"MeshRoute firmware v0.1 — boot"` + build defaults + board), radio `std_init`, NV load, BLE/SoftDevice init (later).
- **Loop** (fw_main.cpp:1509+): radio `poll_rx` (a `while`), `take_preamble`, due-timers, `service_tx`, `sample_noise`, inbox flush (30 s), push drain — all radio-heavy (the likely hang sites), then the sleep gate.

## The design

Flash can't be written from a fault handler (IRQs off, SoftDevice mid-state), so it's a **capture → reboot → persist** flow across four pieces.

### 1. Retained `.noinit` scratch (survives WDT/fault/pin/soft reset; lost only on true power-off)
```c
struct RetainedScratch {                 // ~28 B
    uint32_t magic;                      // kScratchMagic when valid (garbage after a power-on => "no context")
    uint32_t last_uptime_ms;             // updated every loop = the moment-of-death for a hang/fault
    uint8_t  had_fault;                  // a HardFault was captured this life
    uint32_t fault_pc, fault_lr, cfsr, fault_addr;   // the ARM fault frame (0 if no fault)
};
__attribute__((section(".noinit"))) static volatile RetainedScratch g_scratch;
```
⚠ Confirm the linker script has a non-initialised `.noinit` (NOLOAD) region (the Adafruit nRF52 cores do). If absent, reserve a fixed RAM region instead — do NOT fall back to GPREGRET (16 bits, too small for the frame). The loop sets `g_scratch.last_uptime_ms = millis()` each iteration (free) and keeps `magic = kScratchMagic`.

### 2. Hardware watchdog (8 s, through sleep)
nRF52 `NRF_WDT`, configured ONCE (it cannot be stopped/reconfigured until reset):
- `CRV = 8 * 32768 - 1` (8 s on the 32.768 kHz clock); `RREN = RR0` enabled.
- `CONFIG`: **`SLEEP=1`** (run while the CPU light-sleeps — catches a stuck-sleep) · `HALT=0` (pause under a debugger so it doesn't trip a JTAG session).
- `TASKS_START=1`. **Start it just after the `delay(2000)` settle** (so the deliberate boot settle isn't watched) — the 8 s window then covers radio init + NV + the whole runtime; each boot step is < 8 s, so a *boot* hang (e.g. a radio-init wedge) is also caught.
- **Feed** in `loop()` every iteration: `NRF_WDT->RR[0] = WDT_RR_RR_Reload (0x6E524635)`. A hang ⇒ no feed ⇒ reset in ≤ 8 s ⇒ `RESETREAS.DOG` ⇒ auto-recovery **and** a logged reason.

### 3. HardFault capture
Override the app fault handlers (naked → capture the active stack frame → reset):
```c
__attribute__((naked)) void HardFault_Handler(void) {
    __asm volatile("tst lr,#4 \n ite eq \n mrseq r0,msp \n mrsne r0,psp \n b fault_capture \n");
}
extern "C" void fault_capture(uint32_t* sp) {        // sp[6]=PC, sp[5]=LR, sp[7]=xPSR
    g_scratch.fault_pc=sp[6]; g_scratch.fault_lr=sp[5]; g_scratch.cfsr=SCB->CFSR;
    g_scratch.fault_addr=(SCB->CFSR&0x8000)?SCB->BFAR:((SCB->CFSR&0x80)?SCB->MMFAR:0);
    g_scratch.had_fault=1; g_scratch.magic=kScratchMagic; NVIC_SystemReset();
}
```
Also alias `MemManage_/BusFault_/UsageFault_Handler` to the same path. **Scope:** this captures **app** faults (our code). A **SoftDevice assert** vectors through the Adafruit/Bluefruit SoftDevice fault handler — hooking that is a flagged follow-on, not in this spec (the WDT still catches the resulting hang/lockup, logged as DOG/LOCKUP).

### 4. `/mrfault` flash ring (mrnv-style)
```c
struct FaultRecord {                      // ~24 B
    uint32_t boot_seq;                    // persistent, monotonic across power-cycles
    uint16_t reason_bits;                 // decoded RESETREAS (POR=0, PIN, DOG, SREQ, LOCKUP, OFF, …)
    uint8_t  had_fault, _pad;
    uint32_t ran_ms;                      // last_uptime from the scratch (0/—  if power-on => unknown)
    uint32_t fault_pc, cfsr, fault_addr;
};
struct FaultLog { uint32_t magic, boot_seq; uint16_t version, count, head; FaultRecord ring[kFaultRingN]; };  // kFaultRingN=16
```
`mrnv::load_faults`/`save_faults` mirror `load`/`save(Blob)` on `/mrfault`.

### 5. Boot capture (in `setup`, ordered)
1. **Top of `setup`, BEFORE BLE/SoftDevice** (so direct register access is safe): read `NRF_POWER->RESETREAS` into a local, then **clear it** (write-1-to-clear: `NRF_POWER->RESETREAS = 0xFFFFFFFF`).
2. Read `g_scratch` (retained). `magic == kScratchMagic` ⇒ context present (a reset, not a power-on): `ran_ms = last_uptime_ms`, plus the fault frame if `had_fault`.
3. After `InternalFS` is up: `load_faults` (init if absent), `boot_seq = header.boot_seq + 1`, compose a `FaultRecord` from `(RESETREAS, scratch)`, push into the ring (drop-oldest), `save_faults`.
4. Reset `g_scratch` fault fields, set `magic`, prime `last_uptime_ms = 0`.
5. Start the WDT (§2). Print the banner (now includes the last reset reason — §6).

### 6. Commands + banner
- **Refactor** the boot-banner prints (fw_main.cpp:1210-1222) into a reusable `print_banner()` and call it from both `setup` and the new command.
- **`version`** (text console + the JSON console): `print_banner()` → `MeshRoute fw v0.1 · built <__DATE__ __TIME__> · <GIT_REV> · board=<…>` then `last reset: <reason>[ · ran <…>][ · HARDFAULT pc=0x… cfsr=0x…]`. **On demand, no reset.**
- **`faults`** (text console): dump the `/mrfault` ring newest-first — `boot <seq> · <reason> · ran <h m s|—>[ · pc=0x… cfsr=0x… @0x…]` per record; a one-line summary (N records, M faults, P watchdog resets).
- **`status`**: add a compact ` reset=<reason>` field (the last record's reason).
- Wire all into the dispatch table (:1011+), `dump_help`, and the JSON console for `version`.

### 7. Build stamp (`__DATE__`/`__TIME__` + git rev)
- `built __DATE__ " " __TIME__` (compile-time, free).
- Git rev via a PlatformIO pre-build `extra_scripts = pre:tools/git_rev.py` (in `[common]` or the nRF52 envs): runs `git rev-parse --short HEAD` (+ `-dirty` if the tree is dirty), injects `-DGIT_REV='"<rev>"'`; defaults `GIT_REV` to `"nogit"` if unavailable. New file `tools/git_rev.py`.

### 8. ESP32 envs must still compile
All of §1-§5 + §7-banner is `#if defined(NRF52_PLATFORM)`. On ESP32 the WDT/fault/RESETREAS pieces compile to **no-ops/stubs**, `faults` reports "unsupported on this build", and `version` still prints the banner (build/git). The ESP32-S3 real port (esp_task_wdt / esp_reset_reason / panic handler / NVS) is a **follow-on spec**.

### 9. Bench fault-injection (debug-gated)
Add a `crashtest <hang|fault|reboot>` command, gated behind `debug on` (always compiled, active only after `debug on` — so the bench exercises the **real deployable image**, not a separate crashtest build), so the gate can exercise each path on real metal: `hang` = a `while(1){}` (→ WDT/DOG in ~8 s), `fault` = a deliberate null-deref/`udf` (→ HardFault capture), `reboot` = `NVIC_SystemReset` (→ SREQ). Without it the only way to test is to wait for a real wedge.

## Tests / gate

- **Native unit** (the off-HW-testable parts): `FaultRecord`/`FaultLog` (de)serialize round-trip; the ring push + drop-oldest at `kFaultRingN`; `RESETREAS`-bits → reason-string decode; the `version`/`faults` text formatters (feed synthetic records, assert the lines). The WDT/HardFault/scratch are HW-only (no native coverage — exercised on the bench).
- **Build ALL envs:** `pio run -e gateway -e xiao_sx1262` (nRF52 = full feature) **and** `-e heltec_v3 -e xiao_esp32s3` (ESP32 = the `#if` stubs must compile clean). Full native suite stays green.
- **No sim impact:** HW-only; `sim_main`/native never compile the nRF paths. BASELINE suite untouched (don't even need to re-run s18 — `fw_main`/device-only).
- **Metal (user bench):** `version` prints build+git+last-reset **without resetting**; `crashtest hang` → the node self-resets in ~8 s, reconnect (no reset) → `faults` shows a `DOG` record with `ran ≈` the run time; `crashtest fault` → `faults` shows `HARDFAULT pc=… cfsr=…`; `crashtest reboot` and a real power-cycle log `SREQ` and `POR`; `status` shows `reset=`. Confirm the WDT does NOT trip during normal operation (incl. an OTA + a long flash op) over a soak.

## Notes

- The WDT timeout (8 s) and the ring depth (16) are tunable; flag the final values at the gate.
- Independent of the channel-repair and e2e-ack work — order-free.
- This is **instrumentation to find the wedge**, plus auto-recovery — it does NOT itself fix the root-cause hang. Once a `faults` history accumulates (DOG vs HARDFAULT + the PC), that points the real fix (my prior: a radio `poll_rx`/`service_tx` wedge — the PC/CFSR will confirm or refute).
