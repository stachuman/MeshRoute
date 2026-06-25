// MeshRoute — lib/hal/radio_canary.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Radio-Module CANARY core (debug instrument, spec 2026-06-25-radio-module-canary.md). The recurring jump-to-0x0 /
// bus-fault crash (fault-log v3 LR = Module::SPItransferStream) is a WILD WRITE corrupting the RadioLib Module's
// pointers at runtime. This catches the corruptor: snapshot the Module's (+ its HAL's) critical bytes at init, then
// re-check after each loop() subsystem — the FIRST checkpoint that sees a change names the corrupting subsystem.
//
// THIS header is the PURE comparison logic (host-unit-tested, test/test_radio_canary.cpp). The device glue (getMod,
// the snapshot at begin(), the live re-check) is in device_radio.h; the durable record is device_fault.h's scratch +
// fault_log's kCauseCanary. ALL flag-gated by MR_RADIO_CANARY (default OFF) — never in a production build.
#pragma once
#include <stdint.h>
#include <stddef.h>

#ifndef MR_RADIO_CANARY
#define MR_RADIO_CANARY 0      // default OFF — a -DMR_RADIO_CANARY=1 bench build arms it; never in production
#endif

namespace mrcanary {

// First differing byte offset between baseline[0..n) and current[0..n), or -1 if identical. PURE (no SPI / no I/O).
inline int diff(const uint8_t* baseline, const uint8_t* current, size_t n) {
    for (size_t i = 0; i < n; ++i)
        if (baseline[i] != current[i]) return static_cast<int>(i);
    return -1;
}

// the little-endian dword at byte `off` of `p` (clamped so a near-end offset still reads 4 in-bounds bytes of [0,n)).
inline uint32_t dword_at(const uint8_t* p, size_t off, size_t n) {
    if (n < 4) return 0;
    if (off > n - 4) off = n - 4;
    return (uint32_t)p[off] | ((uint32_t)p[off + 1] << 8) | ((uint32_t)p[off + 2] << 16) | ((uint32_t)p[off + 3] << 24);
}

}  // namespace mrcanary
