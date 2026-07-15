<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Fault-log v3 — surface the stacked LR (name the caller on a jump-to-0x0)

**Status:** coder instruction. The user commits + flashes + benches; I gate. nRF52840 (the ARM fault frame); the ESP32 branch is unaffected. No wire change.

## Why

Bench, 2026-06-24: a node crash-looped at boot — `HARDFAULT · ran 0s · pc=0x0 cfsr=0x20000` ×8 consecutively, then recovered. `cfsr=0x20000` = UsageFault **INVSTATE** = the CPU **branched to 0x0** (Thumb bit clear) = a **null function-pointer call or a corrupted return address**, very early in boot, transient. The fault-log v2 caught and classified it perfectly — but it tells us the crash jumped to null, **not who jumped there**.

The HardFault handler ALREADY captures the caller: `device_fault.h:52` does `g_scratch.fault_lr = sp[5]` (the stacked LR = the return address into the calling function). But `fault_compose_record` (device_fault.h:122–127) copies only `fault_pc`/`cfsr`/`fault_addr` to the persistent `FaultRecord` — **`fault_lr` is dropped**, so it never reaches the ring or the `faults` printout. One stacked register away from naming the bug.

## The change (small)

1. **`lib/core/fault_log.h`** — add `uint32_t fault_lr;` to `FaultRecord` (next to `fault_pc`). The record grows 24→28 B. **Bump `kFaultVersion` 2→3** (a format change; an old-version ring is rejected + re-inited on the next boot — we lose the *current* 16 records, which is fine: they predate the LR and the crash will recur).
2. **`src/device_fault.h`** — in the `had_fault` block of `fault_compose_record` (122–127), add `r.fault_lr = g_scratch.fault_lr;`.
3. **`lib/core/fault_log.cpp`** — add `lr=0x%lx` to BOTH hardfault formatters:
   - the per-record line (≈103–104): `… pc=0x%lx lr=0x%lx cfsr=0x%lx @0x%lx`
   - the `version`/summary "last fault" line (≈138): `… HARDFAULT pc=0x%lx lr=0x%lx cfsr=0x%lx`

   (Keep the existing fields + order; just insert `lr=` after `pc=`.)

Optional (only if the LR alone doesn't pin it — do NOT do preemptively): also persist the stacked `r0–r3` (`sp[0..3]`) for argument context. That's +16 B/record; skip unless asked.

## How it's used (the payoff)

Next hardfault prints `pc=0x0 lr=0x<addr> cfsr=0x20000`. Resolve the caller against the build:
```
arm-none-eabi-addr2line -f -e .pio/build/<env>/firmware.elf 0x<lr>
```
(or look `0x<lr>` up in `firmware.map`). That names the function + line that branched to 0x0 — the root cause. `firmware.map` is already emitted; if the `.elf` isn't kept per-build, note that so a symbolized build is retained for this.

## Tests / gate

- **Native:** `FaultRecord` round-trips `fault_lr` (set it, persist, read back); the formatter emits `lr=0x…` on a hardfault record and omits it on a clean one. Extend the existing fault_log unit cases.
- **Build:** all 4 boards; confirm the version bump compiles and an old ring re-inits cleanly (no read of a v2 record as v3).
- **Metal (the proof):** force a hardfault (`crashtest fault`) → `faults` shows a non-zero `lr=` that `addr2line` resolves into `crashtest`'s fault path. THEN watch for the real boot-loop to recur and capture its `lr=` → the actual culprit.

## Sequencing

Tiny, high-leverage, independent. Do it first in the hardware thread — it's the evidence that turns the next jump-to-0x0 from a mystery into a one-line addr2line. The leading suspect to check once we have the LR: an early-boot **null callback** (e.g. a radio RX IRQ dispatching to a callback registered later in setup). A defensive null-guard on ISR callback dispatch is a reasonable parallel hardening, but the LR drives the real fix.
