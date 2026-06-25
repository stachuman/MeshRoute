// MeshRoute — lib/core/fault_log.cpp  (platform-neutral fault-log core; host-testable)
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Ring + RESETREAS decode + the version/faults text formatters. No Arduino/nRF deps; snprintf only, and NO
// %f/%lld (newlib-nano on the nRF52 can't print them — uint32 via (unsigned long)%lu, smaller via %u).
#include "fault_log.h"
#include <cstdio>
#include <cstring>

namespace mrfault {

void fault_log_init(FaultLog& f) {
    f = FaultLog{};
    f.magic = kFaultMagic;
    f.version = kFaultVersion;
    f.boot_seq = 0;
    f.count = 0;
    f.head = 0;
}

bool fault_log_valid(const FaultLog& f) {
    return f.magic == kFaultMagic && f.version == kFaultVersion;
}

void fault_log_push(FaultLog& f, const FaultRecord& r) {
    f.ring[f.head] = r;
    f.head = static_cast<uint16_t>((f.head + 1) % kFaultRingN);
    if (f.count < kFaultRingN) ++f.count;     // else drop-oldest (head already advanced past it)
    f.boot_seq = r.boot_seq;                  // the newest record's boot_seq is the log's high-water
}

const FaultRecord* fault_log_at(const FaultLog& f, uint16_t i_newest_first) {
    if (i_newest_first >= f.count) return nullptr;
    // newest = head-1; walk backwards. +kFaultRingN keeps the modulo non-negative.
    const uint16_t slot = static_cast<uint16_t>((f.head + kFaultRingN - 1 - i_newest_first) % kFaultRingN);
    return &f.ring[slot];
}

// ---- formatters ------------------------------------------------------------------------------------
namespace {
// append `s` to buf at *pos (bounded); keeps buf NUL-terminated. No-op once full.
void put(char* buf, size_t cap, size_t* pos, const char* s) {
    while (*s && *pos + 1 < cap) buf[(*pos)++] = *s++;
    if (cap) buf[*pos < cap ? *pos : cap - 1] = '\0';
}
// h/m/s from ms into buf (e.g. "1h02m03s", "12m05s", "8s"); used for ran_ms.
void put_hms(char* buf, size_t cap, size_t* pos, uint32_t ms) {
    const uint32_t s = ms / 1000u, h = s / 3600u, m = (s % 3600u) / 60u, sec = s % 60u;
    char tmp[24];
    if (h)      snprintf(tmp, sizeof tmp, "%luh%02lum%02lus", (unsigned long)h, (unsigned long)m, (unsigned long)sec);
    else if (m) snprintf(tmp, sizeof tmp, "%lum%02lus", (unsigned long)m, (unsigned long)sec);
    else        snprintf(tmp, sizeof tmp, "%lus", (unsigned long)sec);
    put(buf, cap, pos, tmp);
}
}  // namespace

FaultCause classify_cause(bool magic_valid, bool had_fault, bool wdt_fired, bool expected) {
    if (!magic_valid) return kCausePowerCycle;   // RAM was lost -> a true power-off
    if (had_fault)    return kCauseHardfault;     // a captured fault frame
    if (wdt_fired)    return kCauseWatchdog;      // the WDT pre-reset IRQ set the flag (a hang)
    if (expected)     return kCauseReboot;        // mark_expected_reset before a deliberate NVIC_SystemReset
    return kCauseUnexpected;                      // scratch valid, none set: pin reset / brownout-with-RAM / spontaneous
}

const char* fault_cause_str(uint8_t cause) {
    switch (cause) {
        case kCausePowerCycle: return "POWER_CYCLE";
        case kCauseHardfault:  return "HARDFAULT";
        case kCauseWatchdog:   return "WATCHDOG";
        case kCauseReboot:     return "REBOOT";
        case kCauseUnexpected: return "UNEXPECTED";
        case kCauseCanary:     return "CANARY";
        case kCauseWatchpoint: return "WATCHPOINT";
        default:               return "?";
    }
}

size_t reset_reason_str(uint16_t reason_bits, char* buf, size_t cap) {
    size_t pos = 0;
    if (cap) buf[0] = '\0';
    if (reason_bits == 0) { put(buf, cap, &pos, "POR"); return pos; }   // no bits set => power-on / brownout
    struct { uint16_t bit; const char* name; } tbl[] = {
        { kResetPin, "PIN" }, { kResetDog, "DOG" }, { kResetSreq, "SREQ" },
        { kResetLockup, "LOCKUP" }, { kResetOff, "OFF" }, { kResetBrownout, "BROWNOUT" }, { kResetPanic, "PANIC" },
    };
    bool first = true;
    for (auto& e : tbl) {
        if (reason_bits & e.bit) { if (!first) put(buf, cap, &pos, "+"); put(buf, cap, &pos, e.name); first = false; }
    }
    if (first) put(buf, cap, &pos, "?");      // an unmapped bit set -> not silently empty
    return pos;
}

size_t format_fault_record(const FaultRecord& r, char* buf, size_t cap) {
    size_t pos = 0;
    if (cap) buf[0] = '\0';
    char tmp[80];                                               // holds " · pc=0x… lr=0x… cfsr=0x… @0x…" (4×8 hex + literals)
    snprintf(tmp, sizeof tmp, "boot %lu \xc2\xb7 ", (unsigned long)r.boot_seq);   // "boot <seq> · "
    put(buf, cap, &pos, tmp);
    put(buf, cap, &pos, fault_cause_str(r.cause));               // the scratch-derived CAUSE is the headline
    put(buf, cap, &pos, " \xc2\xb7 ran ");
    if (r.cause == kCausePowerCycle) put(buf, cap, &pos, "\xe2\x80\x94");   // RAM lost -> uptime unknown (em-dash)
    else                             put_hms(buf, cap, &pos, r.ran_ms);
    if (r.had_fault) {
        snprintf(tmp, sizeof tmp, " \xc2\xb7 pc=0x%lx lr=0x%lx cfsr=0x%lx @0x%lx",     // v3: lr = the caller (addr2line)
                 (unsigned long)r.fault_pc, (unsigned long)r.fault_lr, (unsigned long)r.cfsr, (unsigned long)r.fault_addr);
        put(buf, cap, &pos, tmp);
    }
    if (r.cause == kCauseCanary) {                              // the radio-Module canary: where/off + the corrupted dword (before->after)
        if (r.cfsr >= 100)                                      // ADDENDUM: where>=100 = a per-timer-id trip -> print the timer id
            snprintf(tmp, sizeof tmp, " \xc2\xb7 timer id=%lu off=%lu 0x%lx\xe2\x86\x92" "0x%lx",
                     (unsigned long)(r.cfsr - 100), (unsigned long)r.fault_addr, (unsigned long)r.fault_pc, (unsigned long)r.fault_lr);
        else
            snprintf(tmp, sizeof tmp, " \xc2\xb7 @w%lu off=%lu 0x%lx\xe2\x86\x92" "0x%lx",   // "→"; split so \x92 doesn't absorb the '0'. cfsr=where, fault_addr=off, fault_pc=before, fault_lr=after
                     (unsigned long)r.cfsr, (unsigned long)r.fault_addr, (unsigned long)r.fault_pc, (unsigned long)r.fault_lr);
        put(buf, cap, &pos, tmp);
    }
    if (r.cause == kCauseWatchpoint) {                          // ADDENDUM 2: the DWT watchpoint — pc/lr of the exact store (addr2line the pc)
        snprintf(tmp, sizeof tmp, " \xc2\xb7 pc=0x%lx lr=0x%lx", (unsigned long)r.fault_pc, (unsigned long)r.fault_lr);
        put(buf, cap, &pos, tmp);
    }
    if (r.reason_bits) {                                         // the RESETREAS HINT (0 on the UF2-bootloader nRF52; real elsewhere)
        char reason[40]; reset_reason_str(r.reason_bits, reason, sizeof reason);
        put(buf, cap, &pos, " \xc2\xb7 hint:"); put(buf, cap, &pos, reason);
    }
    return pos;
}

size_t format_fault_summary(const FaultLog& f, char* buf, size_t cap) {
    uint16_t hf = 0, wd = 0;                                     // counted from the scratch-derived CAUSE (so watchdog is finally meaningful)
    for (uint16_t i = 0; i < f.count; ++i) {
        const FaultRecord* r = fault_log_at(f, i);
        if (!r) continue;
        if (r->cause == kCauseHardfault) ++hf;
        if (r->cause == kCauseWatchdog)  ++wd;
    }
    char tmp[96];
    snprintf(tmp, sizeof tmp, "%u record%s \xc2\xb7 %u hardfault%s \xc2\xb7 %u watchdog%s",
             f.count, f.count == 1 ? "" : "s", hf, hf == 1 ? "" : "s", wd, wd == 1 ? "" : "s");
    size_t pos = 0; if (cap) buf[0] = '\0'; put(buf, cap, &pos, tmp);
    return pos;
}

size_t format_last_reset(const FaultRecord* last, char* buf, size_t cap) {
    size_t pos = 0;
    if (cap) buf[0] = '\0';
    put(buf, cap, &pos, "last reset: ");
    if (!last) { put(buf, cap, &pos, "unknown (no fault log)"); return pos; }
    put(buf, cap, &pos, fault_cause_str(last->cause));
    if (last->cause != kCausePowerCycle) { put(buf, cap, &pos, " \xc2\xb7 ran "); put_hms(buf, cap, &pos, last->ran_ms); }
    if (last->had_fault) {
        char tmp[72];
        snprintf(tmp, sizeof tmp, " \xc2\xb7 HARDFAULT pc=0x%lx lr=0x%lx cfsr=0x%lx",     // v3: lr = the caller (addr2line)
                 (unsigned long)last->fault_pc, (unsigned long)last->fault_lr, (unsigned long)last->cfsr);
        put(buf, cap, &pos, tmp);
    }
    return pos;
}

size_t format_version_banner(char* buf, size_t cap, const char* build, const char* git, const char* board) {
    size_t pos = 0; if (cap) buf[0] = '\0';
    put(buf, cap, &pos, "MeshRoute fw v0.1 \xc2\xb7 built ");
    put(buf, cap, &pos, build ? build : "?");
    put(buf, cap, &pos, " \xc2\xb7 ");
    put(buf, cap, &pos, git ? git : "nogit");
    put(buf, cap, &pos, " \xc2\xb7 board=");
    put(buf, cap, &pos, board ? board : "?");
    return pos;
}

}  // namespace mrfault
