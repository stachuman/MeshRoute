// MeshRoute — lib/core/fault_log.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Platform-neutral persistent fault-log CORE (spec docs/superpowers/specs/2026-06-24-persistent-fault-log.md).
// The PODs + the ring (drop-oldest) + the RESETREAS decode + the `version`/`faults` text formatters — all
// host-testable (test/test_fault_log.cpp), NO Arduino/nRF deps. The HW glue (the retained .noinit scratch, the
// NRF_WDT, the HardFault handlers, the RESETREAS register read) is nRF52-only in src/device_fault.h; the flash
// store (`/mrfault`) is mrnv-style in src/device_nv.h. The FaultLog is a packed POD written whole, exactly like
// mrnv::Blob — so a `version` bump rejects an old record (load returns false -> init fresh).
//
// Namespace `mrfault` (NOT MESHROUTE_NS): this is firmware instrumentation, not the protocol Node, so it stays
// out of the gateway two-lib (meshroute / meshroute_gw) namespace split — one definition, the device compiles it once.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace mrfault {

inline constexpr uint16_t kFaultRingN   = 16;
inline constexpr uint32_t kFaultMagic   = 0x4D524654u;   // 'MRFT'
inline constexpr uint16_t kFaultVersion = 1;

// Compact reset-reason bits — MIRROR the nRF52 NRF_POWER->RESETREAS bits, compacted into 16 bits (the HW OFF bit
// is 16, which a uint16_t can't hold, so the device maps it to bit 4). reason_bits == 0 => POR (a true power-on /
// brownout: RESETREAS reads 0). Multiple bits can be set; the decoder joins them with '+'.
enum ResetReason : uint16_t {
    kResetPin      = 1u << 0,   // RESETPIN — external pin / reset button (nRF) · ESP_RST_EXT
    kResetDog      = 1u << 1,   // watchdog timeout — a HANG (no feed in 8 s) -> the auto-recovery signal (nRF DOG · ESP TASK/INT WDT)
    kResetSreq     = 1u << 2,   // soft reset — OTA, `crashtest reboot`, our HardFault-capture reset (nRF SREQ · ESP_RST_SW)
    kResetLockup   = 1u << 3,   // CPU lockup (escalated fault, nRF)
    kResetOff      = 1u << 4,   // woke from System OFF / deep-sleep (nRF RESETREAS bit 16 · ESP_RST_DEEPSLEEP)
    kResetBrownout = 1u << 5,   // supply brownout (ESP_RST_BROWNOUT)
    kResetPanic    = 1u << 6,   // a captured crash — ESP_RST_PANIC (ESP32; nRF crashes vector through HardFault -> SREQ + had_fault)
};

struct FaultRecord {              // ~24 B POD (one ring slot)
    uint32_t boot_seq;            // monotonic across power-cycles (== this record's boot number)
    uint16_t reason_bits;         // compact ResetReason; 0 = POR
    uint8_t  had_fault;           // 1 = a HardFault frame was captured (fault_pc/cfsr/fault_addr valid)
    uint8_t  _pad;
    uint32_t ran_ms;              // uptime at death (the retained scratch's last_uptime_ms; 0 + POR => unknown)
    uint32_t fault_pc;            // ARM fault frame (0 if no fault)
    uint32_t cfsr;
    uint32_t fault_addr;
};

struct FaultLog {                 // packed POD; whole-blob R/W to /mrfault (mrnv style)
    uint32_t    magic;            // kFaultMagic
    uint32_t    boot_seq;         // the persistent monotonic boot counter (the newest record's boot_seq)
    uint16_t    version;          // kFaultVersion
    uint16_t    count;            // live records (0..kFaultRingN)
    uint16_t    head;             // ring index of the NEXT write; oldest = (head - count) mod N
    uint16_t    _pad;
    FaultRecord ring[kFaultRingN];
};

// ---- ring ------------------------------------------------------------------------------------------
void fault_log_init(FaultLog& f);                          // fresh: magic/version set, count=head=boot_seq=0
bool fault_log_valid(const FaultLog& f);                   // magic + version match (else the loader inits fresh)
void fault_log_push(FaultLog& f, const FaultRecord& r);    // append; drop-oldest at kFaultRingN
const FaultRecord* fault_log_at(const FaultLog& f, uint16_t i_newest_first);   // i=0 -> newest; nullptr if i>=count

// ---- formatters (all return strlen written, NUL-terminate, never overflow `cap`; no %f/%lld — newlib-nano) ----
size_t reset_reason_str   (uint16_t reason_bits, char* buf, size_t cap);                   // "POR" / "DOG" / "PIN+DOG"
size_t format_fault_record(const FaultRecord& r, char* buf, size_t cap);                   // one `faults` line, newest-first
size_t format_fault_summary(const FaultLog& f, char* buf, size_t cap);                     // "N records · M faults · P watchdog resets"
size_t format_last_reset  (const FaultRecord* last, char* buf, size_t cap);                // the `version` banner's reset line
size_t format_version_banner(char* buf, size_t cap, const char* build, const char* git, const char* board);  // line 1

}  // namespace mrfault
