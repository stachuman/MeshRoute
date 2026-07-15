<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Fault-log v2 — classify the reset from the retained scratch, not `RESETREAS`

**Status:** coder instruction. The user does ALL commits — land GREEN + uncommitted, report ready, I gate. nRF52840; firmware change; no wire change.

## Why

Bench-proven (ACM3 ring, 2026-06-24): **every record reads `POR`** — including a manual **pin** reset that should read `PIN`. The **Adafruit UF2 bootloader reads + clears `NRF_POWER->RESETREAS` before our app runs**, so `fault_read_resetreas_and_clear()` always sees 0 → POR. The reason field is therefore useless on this board (DOG / PIN / brownout all masked to POR), which is exactly the distinction we need (did the watchdog fire? was it a wedge?). What DID work: the `.noinit` scratch — `ran 16m02s` vs `—` correctly split a soft/pin reset from a true power-cycle, and `had_fault` correctly read 0. So: **classify from the scratch (reliable), keep RESETREAS only as a secondary hint.**

## Verified current state

- `RetainedScratch` (device_fault.h:37) survives WDT/fault/pin/soft reset, lost on power-off. `had_fault` + `last_uptime_ms` work; the `RESETREAS`→`reason_bits` path is masked to 0 by the bootloader.
- `fault_compose_record` (device_fault.h:97) already prefers the scratch for `ran_ms`/`had_fault`. The `FaultRecord.reason_bits` it stores comes from the (masked) `RESETREAS`.
- Deliberate resets: `do_reboot` (fw_main.cpp:590), OTA (`GPREGRET=0xA8` :582), `crashtest reboot`, the upcoming `prep-restart` — all call `NVIC_SystemReset` (→ SREQ, also masked).
- The nRF52 `WDT` can raise a `TIMEOUT` interrupt ~2 LFCLK cycles before it resets (`NRF_WDT->INTENSET`) — enough to set a RAM flag.

## The change — a scratch `cause`, set at the moment of each reset

1. **Add to `RetainedScratch`:** `uint8_t expected;` (a clean/deliberate reset) and `uint8_t wdt_fired;` (the watchdog tripped). (Keep `magic`/`last_uptime_ms`/`had_fault`/the fault frame.)
2. **WDT pre-reset IRQ** — in `fault_wdt_start`, also `NRF_WDT->INTENSET = WDT_INTENSET_TIMEOUT_Msk` and implement `WDT_IRQHandler`: set `g_scratch.wdt_fired = 1; g_scratch.magic = kScratchMagic;` (nothing else — only a couple cycles before reset). This makes a watchdog reset detectable **despite** the masked RESETREAS.
3. **Mark deliberate resets** — add `mrfault::mark_expected_reset()` (`g_scratch.expected = 1; g_scratch.magic = …;`) and call it immediately before every intentional `NVIC_SystemReset`: `do_reboot`, the OTA reset, `crashtest reboot`, `prep-restart`.
4. **Classify at boot** (`fault_compose_record`), in priority order, from the scratch — NOT `reason_bits`:
   - `magic != kScratchMagic` → **POWER_CYCLE** (RAM lost; `ran = —`).
   - `had_fault` → **HARDFAULT** (+ pc/cfsr).
   - `wdt_fired` → **WATCHDOG** (a hang the WDT caught).
   - `expected` → **REBOOT** (a deliberate reset — reboot/OTA/prep-restart/crashtest).
   - else (scratch valid, none set) → **UNEXPECTED** — a reset we didn't cause and the WDT didn't force: a **pin reset** (the operator), a brownout that kept RAM, or a spontaneous reset. The new headline class for "something reset it and we don't know why" (the wedge-recovery case). Keep the masked `RESETREAS` value in the record as a **hint** field (0/POR on this board, but real on a no-bootloader build).
5. **Reset the flags** in `fault_scratch_reset_after_capture` (clear `expected`/`wdt_fired`/`had_fault`, re-prime magic).
6. **`faults`/`version` render** the new cause names (POWER_CYCLE / HARDFAULT / WATCHDOG / REBOOT / UNEXPECTED) + `ran`. The `[faults] N records · M hardfaults · P watchdog` summary now counts from the scratch-derived cause (so `P` is finally meaningful).

## Tests / gate

- **Native unit:** the classifier is pure — feed synthetic scratch states → assert the cause (magic-invalid→POWER_CYCLE; had_fault→HARDFAULT; wdt_fired→WATCHDOG; expected→REBOOT; none→UNEXPECTED; priority order). The ring/format tests update to the new names.
- **Build:** `pio run -e gateway -e xiao_sx1262` (+ the ESP32 envs compile — the ESP32 branch maps esp_reset_reason, which is NOT bootloader-masked, so it can keep its reason path; mirror the `expected`/`wdt_fired` scratch fields there for parity).
- **Metal (user — the real proof):** `crashtest hang` → after the auto-reset, `faults` newest = **WATCHDOG** (not POR) with `ran≈8s`; `crashtest reboot` / `reboot` → **REBOOT**; a manual pin-press → **UNEXPECTED**; power-cycle → **POWER_CYCLE** `ran —`; `crashtest fault` → **HARDFAULT pc=…**. That fully validates the watchdog (the open question from ACM3) and the classifier.

## Note

This finally answers "did the watchdog fire?" — which on ACM3 we couldn't tell. Pairs with the USB-CDC finding: if `crashtest hang` now logs WATCHDOG (WDT works) yet real wedges log UNEXPECTED-after-a-long-`ran` with the radio still alive, that's the USB-CDC signature, not a hang.
