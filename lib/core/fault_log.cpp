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
    char tmp[64], reason[40];                                // 64: holds " · pc=0x… cfsr=0x… @0x…" (3×8 hex + literals)
    reset_reason_str(r.reason_bits, reason, sizeof reason);
    snprintf(tmp, sizeof tmp, "boot %lu \xc2\xb7 ", (unsigned long)r.boot_seq);   // "boot <seq> · "
    put(buf, cap, &pos, tmp);
    put(buf, cap, &pos, reason);
    put(buf, cap, &pos, " \xc2\xb7 ran ");
    if (r.ran_ms == 0 && r.reason_bits == 0) put(buf, cap, &pos, "\xe2\x80\x94");   // POR + unknown uptime -> em-dash
    else                                     put_hms(buf, cap, &pos, r.ran_ms);
    if (r.had_fault) {
        snprintf(tmp, sizeof tmp, " \xc2\xb7 pc=0x%lx cfsr=0x%lx @0x%lx",
                 (unsigned long)r.fault_pc, (unsigned long)r.cfsr, (unsigned long)r.fault_addr);
        put(buf, cap, &pos, tmp);
    }
    return pos;
}

size_t format_fault_summary(const FaultLog& f, char* buf, size_t cap) {
    uint16_t faults = 0, dog = 0;
    for (uint16_t i = 0; i < f.count; ++i) {
        const FaultRecord* r = fault_log_at(f, i);
        if (!r) continue;
        if (r->had_fault) ++faults;
        if (r->reason_bits & kResetDog) ++dog;
    }
    char tmp[96];
    snprintf(tmp, sizeof tmp, "%u record%s \xc2\xb7 %u hardfault%s \xc2\xb7 %u watchdog reset%s",
             f.count, f.count == 1 ? "" : "s", faults, faults == 1 ? "" : "s", dog, dog == 1 ? "" : "s");
    size_t pos = 0; if (cap) buf[0] = '\0'; put(buf, cap, &pos, tmp);
    return pos;
}

size_t format_last_reset(const FaultRecord* last, char* buf, size_t cap) {
    size_t pos = 0;
    if (cap) buf[0] = '\0';
    put(buf, cap, &pos, "last reset: ");
    if (!last) { put(buf, cap, &pos, "unknown (no fault log)"); return pos; }
    char reason[40];
    reset_reason_str(last->reason_bits, reason, sizeof reason);
    put(buf, cap, &pos, reason);
    if (!(last->ran_ms == 0 && last->reason_bits == 0)) { put(buf, cap, &pos, " \xc2\xb7 ran "); put_hms(buf, cap, &pos, last->ran_ms); }
    if (last->had_fault) {
        char tmp[48];
        snprintf(tmp, sizeof tmp, " \xc2\xb7 HARDFAULT pc=0x%lx cfsr=0x%lx", (unsigned long)last->fault_pc, (unsigned long)last->cfsr);
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
