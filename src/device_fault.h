// MeshRoute — src/device_fault.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// nRF52840 HW glue for the persistent fault log (spec 2026-06-24-persistent-fault-log.md): a retained `.noinit`
// scratch (survives WDT/fault/pin/soft reset, lost only on true power-off), the 8 s hardware watchdog (runs
// through light-sleep, auto-resets a hang), and the HardFault-family capture handlers. The platform-neutral
// ring/decode/formatters live in lib/core/fault_log.h; the flash store (`/mrfault`) is mrnv::load_faults/save_faults.
//
// ALL content is `#if defined(NRF52_PLATFORM)` — on ESP32/native this header is EMPTY (fw_main guards every call),
// so those envs compile clean; their real port (esp_task_wdt / esp_reset_reason / panic handler) is a follow-on spec.
// Header-inline + the strong fault vectors => include from THE ONE device TU only (fw_main.cpp), like device_nv.h.
#pragma once
#include <stdint.h>
#include "fault_log.h"   // mrfault::FaultRecord + the ResetReason bits

// NRF52_PLATFORM is -D'd by the nRF52 envs (platformio.ini); self-derive too, for safety, from the core's board macros.
#if !defined(NRF52_PLATFORM) && (defined(NRF52_SERIES) || defined(ARDUINO_ARCH_NRF52) || defined(NRF52840_XXAA) || defined(BOARD_XIAO_WIO_SX1262))
  #define NRF52_PLATFORM 1
#endif
#if !defined(MRFAULT_ESP32) && (defined(ARDUINO_ARCH_ESP32) || defined(ESP32) || defined(BOARD_HELTEC_V3) || defined(BOARD_XIAO_ESP32S3))
  #define MRFAULT_ESP32 1
#endif
// MRFAULT_HW = the fault log is HW-backed (nRF52 OR ESP32). fw_main guards the boot-capture / WDT / scratch / faults
// on this, so both real platforms run it (only native/unknown is a true no-op — and fw_main isn't built there).
#if defined(NRF52_PLATFORM) || defined(MRFAULT_ESP32)
  #define MRFAULT_HW 1
#endif

#if defined(NRF52_PLATFORM)

namespace mrfault {

constexpr uint32_t kScratchMagic = 0x46545343u;   // 'FTSC' — valid context; garbage after a true power-off => "no context"

// Retained across WDT/fault/pin/soft reset (only a power-off clears RAM). `.noinit` = NOLOAD (the Adafruit nRF52
// linker provides it). static => internal linkage, one TU. volatile => the fault handler's writes aren't elided.
struct RetainedScratch {
    uint32_t magic;
    uint32_t last_uptime_ms;     // refreshed every loop = the moment-of-death for a hang
    uint8_t  had_fault;          // the HardFault handler set it
    uint8_t  expected;           // v2: mark_expected_reset() set it (a deliberate reset -> REBOOT, not UNEXPECTED)
    uint8_t  wdt_fired;          // v2: the WDT pre-reset IRQ set it (the watchdog caught a hang -> WATCHDOG, despite the masked RESETREAS)
    uint8_t  _pad;
    uint32_t fault_pc, fault_lr, cfsr, fault_addr;   // the ARM fault frame (0 if no fault)
};
__attribute__((section(".noinit"))) static volatile RetainedScratch g_scratch;

// ---- HardFault-family capture: grab the active stack frame into the scratch, then reset (flash can't be written
// from a fault handler — IRQs off / SoftDevice mid-state — so it's capture -> reboot -> persist-at-boot). ----
extern "C" void fault_capture(uint32_t* sp) {        // sp[5]=LR, sp[6]=PC, sp[7]=xPSR (the exception stack frame)
    g_scratch.fault_pc   = sp[6];
    g_scratch.fault_lr   = sp[5];
    g_scratch.cfsr       = SCB->CFSR;
    g_scratch.fault_addr = (SCB->CFSR & 0x8000) ? SCB->BFAR : ((SCB->CFSR & 0x80) ? SCB->MMFAR : 0);   // BFARVALID / MMARVALID
    g_scratch.had_fault  = 1;
    g_scratch.magic      = kScratchMagic;
    NVIC_SystemReset();                              // -> RESETREAS.SREQ, with a HardFault frame in the scratch
}
// naked: no prologue, so r0 = the faulting SP (MSP vs PSP per EXC_RETURN bit 2), then tail-call fault_capture.
extern "C" __attribute__((naked)) void HardFault_Handler(void) {
    __asm volatile("tst lr, #4 \n ite eq \n mrseq r0, msp \n mrsne r0, psp \n b fault_capture \n");
}
// MemManage/BusFault/UsageFault -> the same capture path (escalations land here on a Cortex-M4 too).
extern "C" void MemManage_Handler(void) __attribute__((alias("HardFault_Handler")));
extern "C" void BusFault_Handler(void)  __attribute__((alias("HardFault_Handler")));
extern "C" void UsageFault_Handler(void) __attribute__((alias("HardFault_Handler")));

// v2: the WDT raises a TIMEOUT IRQ ~2 LFCLK cycles BEFORE it resets — just enough to set a RAM flag, so a watchdog
// reset is detectable despite the bootloader-masked RESETREAS. JUST set the flag (a couple cycles; no flash/logging).
extern "C" void WDT_IRQHandler(void) {
    g_scratch.wdt_fired = 1;
    g_scratch.magic     = kScratchMagic;
}

// ---- 8 s hardware watchdog. Configured ONCE (the WDT can't be stopped/reconfigured until reset). ----
inline void fault_wdt_start() {
    NRF_WDT->CONFIG      = 0x01u;             // SLEEP=1 (run through light-sleep) | HALT=0 (pause under a debugger). bits: SLEEP@0, HALT@3
    NRF_WDT->CRV         = 8u * 32768u - 1u;  // 8 s on the 32.768 kHz LFCLK
    NRF_WDT->RREN        = 0x01u;             // RR0 reload register enabled
    NRF_WDT->INTENSET    = 0x01u;             // v2: TIMEOUT interrupt (bit 0) -> WDT_IRQHandler sets wdt_fired pre-reset
    NVIC_EnableIRQ(WDT_IRQn);
    NRF_WDT->TASKS_START = 1u;
}
inline void fault_wdt_feed() { NRF_WDT->RR[0] = 0x6E524635u; }   // WDT_RR_RR_Reload — the only valid kick value

// v2: mark the NEXT reset DELIBERATE (REBOOT, not UNEXPECTED). Call immediately before each intentional NVIC_SystemReset.
inline void mark_expected_reset() { g_scratch.expected = 1; g_scratch.magic = kScratchMagic; }

// loop() heartbeat: refresh the moment-of-death stamp (free) + re-assert the magic. A hang freezes this -> at the
// next (WDT-forced) boot, last_uptime_ms is when it died.
inline void fault_scratch_alive(uint32_t uptime_ms) {
    g_scratch.last_uptime_ms = uptime_ms;
    g_scratch.magic          = kScratchMagic;
}

// §5.1: read NRF_POWER->RESETREAS, map to the compact mrfault bits, and CLEAR it (write-1-to-clear) so the NEXT
// reset's reason isn't OR'd onto this one. Call EARLY in setup (before BLE/SoftDevice — direct register access safe).
inline uint16_t fault_read_resetreas_and_clear() {
    const uint32_t rr = NRF_POWER->RESETREAS;
    NRF_POWER->RESETREAS = 0xFFFFFFFFu;       // write-1-to-clear all
    uint16_t bits = 0;
    if (rr & (1u << 0))  bits |= kResetPin;     // RESETPIN
    if (rr & (1u << 1))  bits |= kResetDog;     // DOG (watchdog)
    if (rr & (1u << 2))  bits |= kResetSreq;    // SREQ (soft reset)
    if (rr & (1u << 3))  bits |= kResetLockup;  // LOCKUP
    if (rr & (1u << 16)) bits |= kResetOff;     // OFF (woke from System OFF) — HW bit 16 compacted to bit 4
    return bits;                                // 0 => POR (power-on / brownout: RESETREAS read 0)
}

// Compose a FaultRecord — v2: CLASSIFY from the retained scratch (reliable), keep RESETREAS as a hint. Read the
// scratch BEFORE resetting it.
inline FaultRecord fault_compose_record(uint16_t reason_bits, uint32_t boot_seq) {
    FaultRecord r{};
    r.boot_seq    = boot_seq;
    r.reason_bits = reason_bits;                           // the (bootloader-masked) RESETREAS hint
    const bool ctx       = (g_scratch.magic == kScratchMagic);   // valid context => a reset, not a cold power-on
    const bool had_fault = ctx && g_scratch.had_fault;
    const bool wdt_fired = ctx && g_scratch.wdt_fired;
    const bool expected  = ctx && g_scratch.expected;
    r.ran_ms = ctx ? g_scratch.last_uptime_ms : 0;         // unknown (0) after a true power-off
    r.cause  = static_cast<uint8_t>(classify_cause(ctx, had_fault, wdt_fired, expected));
    if (had_fault) {
        r.had_fault  = 1;
        r.fault_pc   = g_scratch.fault_pc;
        r.cfsr       = g_scratch.cfsr;
        r.fault_addr = g_scratch.fault_addr;
    }
    return r;
}

// §5.4: clear the per-life flags, re-prime the magic + uptime for this life.
inline void fault_scratch_reset_after_capture() {
    g_scratch.had_fault = g_scratch.expected = g_scratch.wdt_fired = 0;
    g_scratch.fault_pc = g_scratch.fault_lr = g_scratch.cfsr = g_scratch.fault_addr = 0;
    g_scratch.last_uptime_ms = 0;
    g_scratch.magic = kScratchMagic;
}

}  // namespace mrfault

#elif defined(MRFAULT_ESP32)
// ===== ESP32-S3 port =====================================================================================
// Reset reason via esp_reset_reason() (IDF-latched, read-only). Scratch in RTC slow memory (survives SW reset /
// TWDT / panic / deep-sleep; lost on power-off). Watchdog via esp_task_wdt (8 s, panic -> reboot). A crash is
// recorded as PANIC (had_fault=1) from the reset reason — the IDF panic handler owns the backtrace (UART/coredump),
// so the PC is uncaptured in v1 (a coredump-PC follow-on is flagged). The flash store is NVS (device_nv.h, ESP32 branch).
#include <esp_system.h>       // esp_reset_reason
#include <esp_task_wdt.h>     // the Task Watchdog
#include <esp_attr.h>         // RTC_NOINIT_ATTR

namespace mrfault {

constexpr uint32_t kScratchMagic = 0x46545343u;   // 'FTSC'
struct RetainedScratch {
    uint32_t magic;
    uint32_t last_uptime_ms;
    uint8_t  expected;        // v2: mark_expected_reset() (parity with nRF52; had_fault/wdt_fired derive from esp_reset_reason)
    uint8_t  _pad[3];
};
RTC_NOINIT_ATTR static RetainedScratch g_scratch;   // RTC slow RAM — survives reset, lost on power-off

inline void fault_wdt_start() {
    // The Arduino-ESP32 core inits a loopTask TWDT itself; set it to 8 s + subscribe THIS task. API forked at the v3 core.
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    const esp_task_wdt_config_t cfg = { .timeout_ms = 8000, .idle_core_mask = 0, .trigger_panic = true };
    if (esp_task_wdt_reconfigure(&cfg) != ESP_OK) esp_task_wdt_init(&cfg);   // reconfigure if the core already inited it
#else
    esp_task_wdt_init(8, true);   // v2: (timeout_s, panic)
#endif
    esp_task_wdt_add(NULL);       // watch the loop task (idempotent)
}
inline void fault_wdt_feed() { esp_task_wdt_reset(); }
inline void fault_scratch_alive(uint32_t uptime_ms) { g_scratch.last_uptime_ms = uptime_ms; g_scratch.magic = kScratchMagic; }
inline void mark_expected_reset() { g_scratch.expected = 1; g_scratch.magic = kScratchMagic; }   // v2: a deliberate reset -> REBOOT

// esp_reset_reason() is IDF-latched per boot (auto-cleared next boot) — read-only, nothing to clear (the name is the
// cross-platform shape). Maps the esp_reset_reason_t onto the compact mrfault bits.
inline uint16_t fault_read_resetreas_and_clear() {
    switch (esp_reset_reason()) {
        case ESP_RST_EXT:       return kResetPin;
        case ESP_RST_SW:        return kResetSreq;
        case ESP_RST_PANIC:     return kResetPanic;
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:       return kResetDog;
        case ESP_RST_BROWNOUT:  return kResetBrownout;
        case ESP_RST_DEEPSLEEP: return kResetOff;
        case ESP_RST_POWERON:
        case ESP_RST_UNKNOWN:
        default:                return 0;   // POR
    }
}
inline FaultRecord fault_compose_record(uint16_t reason_bits, uint32_t boot_seq) {
    FaultRecord r{};
    r.boot_seq = boot_seq; r.reason_bits = reason_bits;     // esp_reset_reason -> compact bits, kept as a hint
    const esp_reset_reason_t rr = esp_reset_reason();        // ESP32 reset reason is RELIABLE (not bootloader-masked)
    const bool ctx       = (g_scratch.magic == kScratchMagic);
    const bool power_on  = (rr == ESP_RST_POWERON || rr == ESP_RST_UNKNOWN);   // the authoritative power-cycle signal
    const bool had_fault = (rr == ESP_RST_PANIC);
    const bool wdt_fired = (rr == ESP_RST_TASK_WDT || rr == ESP_RST_INT_WDT || rr == ESP_RST_WDT);
    const bool expected  = ctx && g_scratch.expected;
    r.ran_ms = (ctx && !power_on) ? g_scratch.last_uptime_ms : 0;
    r.cause  = static_cast<uint8_t>(classify_cause(ctx && !power_on, had_fault, wdt_fired, expected));
    if (had_fault) r.had_fault = 1;                          // PC uncaptured on ESP32 v1 (the IDF backtrace is on UART)
    return r;
}
inline void fault_scratch_reset_after_capture() { g_scratch.expected = 0; g_scratch.last_uptime_ms = 0; g_scratch.magic = kScratchMagic; }

}  // namespace mrfault

#endif  // NRF52_PLATFORM / MRFAULT_ESP32
