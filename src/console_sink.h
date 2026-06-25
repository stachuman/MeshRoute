// MeshRoute — src/console_sink.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// `mrcon` — the ONE guarded console-output sink every firmware print goes through (USB-CDC reliability, 2026-06-25
// Parts 3 + 5). A Print-derived wrapper whose write() DROPS (never blocks) when the host is gone (`!Serial`) or the
// CDC TX FIFO can't fit the chunk (`availableForWrite < n`) — so loop() can NEVER stall on a stalled host (the
// anti-wedge). Dropping live output under extreme load is fine: the durable inbox is the truth, not the live stream.
//
// MR_CONSOLE (default 1 = dev/bench, the harness drives the node over USB):
//   MR_CONSOLE=1 -> a GUARDED console (the above).
//   MR_CONSOLE=0 (production) -> a NullPrint: every write() is an empty no-op, so the ~300 migrated output calls
//                 dead-code-eliminate and reference NO `Serial` symbol. fw_main #if's out the INPUT path
//                 (service_console / Serial.begin / the DTR-wait / Serial.available). Production diagnostics stay
//                 over-the-air (BLE-NUS + `rcmd` + the persistent fault-log) — exactly the BLE-managed production path.
#pragma once
#ifndef MR_CONSOLE
#define MR_CONSOLE 1
#endif

#if defined(ARDUINO)
#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>

namespace mrcon_detail {
#if MR_CONSOLE
class GuardedConsole : public Print {
public:
    using Print::write;   // un-hide Print::write(const char*) / write(const char*,size_t) — our overrides below would name-hide them
    // printf — Print has none; some sites (ESP32 OTA) used Serial.printf. Format into a bounded buffer, then the
    // GUARDED chunk write (so printf also drops-never-blocks). Bounded (OTA/diag strings are short).
    int printf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
        char b[160]; va_list ap; va_start(ap, fmt);
        const int n = vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
        if (n <= 0) return n < 0 ? 0 : n;
        const size_t w = static_cast<size_t>(n) < sizeof b ? static_cast<size_t>(n) : sizeof b - 1;
        write(reinterpret_cast<const uint8_t*>(b), w);
        return n;
    }
    // DROP, never block: a single byte goes only if the FIFO has room; else discard (no blocking write).
    size_t write(uint8_t b) override {
        if (!Serial || Serial.availableForWrite() < 1) return 0;
        return Serial.write(b);
    }
    // Whole-chunk gate: emit only if the entire chunk fits the TX FIFO; else drop it whole (never a partial blocking write).
    size_t write(const uint8_t* buf, size_t n) override {
        if (!Serial || static_cast<size_t>(Serial.availableForWrite()) < n) return n;   // claim success (drop) — callers must not retry/block
        return Serial.write(buf, n);
    }
    void flush() { if (Serial) Serial.flush(); }   // guarded (no host -> no-op); loop() no longer calls this (the Adafruit USB task drains the FIFO)
};
#else
class GuardedConsole : public Print {              // production NullPrint — every call is a no-op, no Serial reference
public:
    using Print::write;
    size_t write(uint8_t) override { return 1; }
    size_t write(const uint8_t*, size_t n) override { return n; }
    int printf(const char*, ...) __attribute__((format(printf, 2, 3))) { return 0; }
    void flush() {}
};
#endif
}  // namespace mrcon_detail

inline mrcon_detail::GuardedConsole mrcon;          // the single sink (inline => one ODR-merged instance across TUs)
#endif  // ARDUINO
