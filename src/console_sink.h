// MeshRoute — src/console_sink.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// `mrcon` — the ONE guarded console-output sink every firmware print goes through (USB-CDC reliability, 2026-06-25
// Parts 3 + 5; LINE INTEGRITY, §B95 2026-08-04). A Print-derived wrapper that NEVER blocks, waits, flushes or yields
// for the host — so loop() can never stall on a stalled host (the anti-wedge) — and that emits only WHOLE LINES, so a
// response can never be re-assembled from surviving fragments (the B95 defect).
//
// ★★ WHY A LINE STAGE AND NOT A PER-WRITE GUARD (§B95, bench-found on Heltec V3 H5-06):
//   The old sink gated each individual `Print::write()` call against `Serial.availableForWrite()`. A console row is
//   assembled from TENS of such calls (label, value, separator, …, and the terminator as its own call — and
//   `leaf_name` one CHARACTER per call), and the USB/UART task frees capacity BETWEEN them. So a row lost its labels,
//   spaces and newline while later values survived and fused into the next row:
//       proto : duty=1.00% beacon_ms=900000168010102layer=5 leaf=5000
//   That is MISLEADING SYNTAX, not RAM corruption. ⇒ the admission unit is now a LINE, never a fragment.
//
// ★★★ THE TRANSPORT CEILING THAT SHAPES THIS FILE — MEASURED IN THE INSTALLED FRAMEWORKS, NOT ASSUMED:
//   ESP32-S3 UART profiles (heltec_v3 / heltec_mobile / xiao_esp32s3): `Serial` is `Serial0` = UART0 (`ARDUINO_USB_CDC_ON_BOOT`
//     is unset ⇒ HardwareSerial.h:364 aliases it), and `HardwareSerial::_txBufferSize` defaults to 0 ⇒
//     `availableForWrite()` == free bytes of the **128-byte** hardware TX FIFO (SOC_UART_FIFO_LEN = 128).
//   ESP32-S3 native-USB profile (heltec_v4): the vendored board JSON sets `ARDUINO_USB_MODE=1` and
//     `ARDUINO_USB_CDC_ON_BOOT=1`, so `Serial` is HWCDC. In the pinned framework HWCDC::availableForWrite() reports
//     current TX-ring free bytes and HWCDC::write() first enqueues at most that space without waiting. This sink's
//     single-writer rule prevents an intervening writer consuming the measured space; actual unplug/backpressure
//     behaviour remains an explicit V4 metal check.
//   nRF52840 (xiao_sx1262 / gateway): TinyUSB CDC ⇒ `availableForWrite()` == free bytes of CFG_TUD_CDC_TX_BUFSIZE =
//     **256**. ⚠ And `Adafruit_USBD_CDC::write()` LOOPS WITH `yield()` when the FIFO is short — so a write must never
//     be handed more than `availableForWrite()` bytes, or the anti-wedge is lost inside the core.
//   ⇒ NO line longer than 128 B can EVER be handed to Serial in ONE call on the board B95 was found on: the `cfg`
//     `proto :` row is 118 B, `[cfg.layer0]` ~160 B, the `hashof` remedy ~392 B, and 8 `help` lines exceed 128 B.
//     A strict "one write call per line, else drop" rule would make all of those PERMANENTLY undeliverable (a
//     healthy, idle host included) and reduce `cfg` to its first row. MEASURED, see BASELINE §B95.
//   ⇒ so the guarantee here is the one that is both achievable and sufficient:
//       ★ A COMMITTED LINE REACHES THE WIRE AS A CONTIGUOUS, GAP-FREE, IN-ORDER BYTE RUN INCLUDING ITS TERMINATOR,
//         OR NOT AT ALL. A line is discarded only BEFORE ITS FIRST BYTE IS WRITTEN — never mid-line.
//     It may cross several `Serial.write()` calls (that is what makes >FIFO lines deliverable); it can never skip a
//     byte, lose a terminator, or interleave with another line. Fragments cannot fuse because nothing is ever
//     admitted out of order.
//
// ★ CONSEQUENCE — WHY THE STAGE LIVES *INSIDE* mrcon AND NOT IN A WRAPPER IN FRONT OF IT: with an in-order drain,
//   a SECOND writer to `Serial` would cut straight into a half-drained line and re-create the very fusion this
//   removes. Single-writer discipline is a REQUIREMENT of the drain, not a preference — so every writer (command
//   responses, async pushes, `!!` operator logs, debug traces, the boot banner) shares this one stage. `hl()`'s
//   direct-`Serial` help bypass (firmware_commands.cpp) was deleted for the same reason.
//
// Drops are COUNTED and reported later, never silently: `!! CONSOLE_DROP lines=<N>` once the queue has drained.
// Dropping live output under extreme load is fine: the durable inbox is the truth, not the live stream.
//
// MR_CONSOLE (default 1 = dev/bench, the harness drives the node over USB):
//   MR_CONSOLE=1 -> the STAGED, guarded console (the above).
//   MR_CONSOLE=0 (production) -> a NullPrint: every write() is an empty no-op, so the ~300 migrated output calls
//                 dead-code-eliminate and reference NO `Serial` symbol AND allocate NO staging storage. fw_main
//                 #if's out the INPUT path (service_console / Serial.begin / the DTR-wait / Serial.available).
//                 Production diagnostics stay over-the-air (BLE-NUS + `rcmd` + the persistent fault-log).
#pragma once
#ifndef MR_CONSOLE
#define MR_CONSOLE 1
#endif
// The line stage, in bytes (.bss, MR_CONSOLE=1 only). Holds committed-but-unsent COMPLETE lines plus the line being
// assembled. Sized from the measured responses: a gateway `cfg` ~850 B, `peers` (16 rows) ~1.4 KB, ~22 `routes` rows.
// A response that outgrows it drops WHOLE LINES and says so (`!! CONSOLE_DROP`) — it never truncates one. `help` is
// 6121 B / 75 lines and therefore does NOT fit: it delivers ~25 lines and reports the rest. ★ This is the one lever
// if that trade needs changing (a 6400-B stage delivers all of `help`, at +4.4 KB .bss per board).
#ifndef MR_CONSOLE_STAGE_BYTES
#define MR_CONSOLE_STAGE_BYTES 2048
#endif

#if defined(ARDUINO)
#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace mrcon_detail {
#if MR_CONSOLE
class GuardedConsole : public Print {
public:
    using Print::write;   // un-hide Print::write(const char*) / write(const char*,size_t) — our overrides below would name-hide them
    // printf — Print has none; some sites (ESP32 OTA) used Serial.printf. Format into a bounded buffer, then the
    // staged write (so printf also drops-never-blocks). Bounded (OTA/diag strings are short).
    int printf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
        char b[160]; va_list ap; va_start(ap, fmt);
        const int n = vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
        if (n <= 0) return n < 0 ? 0 : n;
        const size_t w = static_cast<size_t>(n) < sizeof b ? static_cast<size_t>(n) : sizeof b - 1;
        write(reinterpret_cast<const uint8_t*>(b), w);
        return n;
    }
    // Both writes only STAGE. Nothing reaches Serial until the line's terminator arrives (`commit`), so a caller can
    // never publish half a row. They claim success (callers must not retry/block); loss is reported by CONSOLE_DROP.
    size_t write(uint8_t b) override { stage(b); return 1; }
    size_t write(const uint8_t* buf, size_t n) override { for (size_t i = 0; i < n; ++i) stage(buf[i]); return n; }

    // ★ ONCE PER LOOP PASS (fw_main service_console). Three jobs, none of which may wait:
    //   1. terminate a line some writer left unterminated, so it can NEVER fuse with the next response (§B95 inv. 4);
    //   2. hand the sink whatever the TX FIFO will take right now (the opportunistic in-order drain);
    //   3. once the queue is empty, emit the deferred drop report — retained and retried if it does not fit.
    void service() {
        if (_line) { stage('\r'); stage('\n'); }
        pump();
        if (_dropped && _out == _pend && _line == 0) {
            char b[40];
            const int n = snprintf(b, sizeof b, "!! CONSOLE_DROP lines=%lu\r\n", (unsigned long)_dropped);
            // Written DIRECTLY, never through the stage: the report must not be counted recursively, and it must not
            // queue behind the very congestion it is reporting. Atomic (40 B < any FIFO) or retried next pass.
            if (n > 0 && Serial && Serial.availableForWrite() >= n) {
                Serial.write(reinterpret_cast<const uint8_t*>(b), static_cast<size_t>(n));
                _dropped = 0;
            }
        }
    }
    // ⚠ THE ONE DELIBERATELY BLOCKING ENTRY POINT, and it is bounded. Only the RESET/OTA path calls it (do_reboot,
    // do_ota, the canary, crashtest) — each already followed by a delay() and a reset, where the whole point is that
    // the message reaches the operator BEFORE the MCU goes away. NEVER call it from the loop or a handler.
    void flush() override {
        if (!Serial) return;
        for (int i = 0; i < 64 && (_line || _out != _pend); ++i) {
            if (_line) { stage('\r'); stage('\n'); }
            pump();
            Serial.flush();
        }
        Serial.flush();
    }
    uint32_t dropped_lines() const { return _dropped; }   // whole lines lost since the last report (diagnostic)
private:
    // Append one byte to the line under assembly; a '\n' commits it. `_over` = the line does not fit the stage at
    // all ⇒ it is dropped WHOLE at its terminator and no prefix ever reaches the wire (§B95 inv. 6).
    void stage(uint8_t b) {
        if (_pend + _line >= sizeof _buf) compact();          // reclaim what has already gone out, then re-test
        if (_pend + _line <  sizeof _buf) _buf[_pend + _line++] = static_cast<char>(b);
        else                              _over = true;
        if (b == '\n') commit();
    }
    void commit() {
        if (_over)     { _line = 0; _over = false; ++_dropped; return; }   // never started ⇒ safe to drop whole
        if (_line == 0) return;
        // No host AND nothing of ours in flight -> drop now rather than accumulate stale output for a host that may
        // never return (this is the old sink's `!Serial` behaviour, preserved). If a line IS mid-flight we must keep
        // the queue: abandoning it would leave an unterminated fragment on the wire = the fusion we are removing.
        if (!Serial && _out == _pend) { _line = 0; ++_dropped; return; }
        _pend += _line; _line = 0;
        pump();
    }
    // Hand Serial at most `availableForWrite()` bytes, in order, and never more — nRF52's CDC write() yields when the
    // FIFO is short, so exceeding it would block. Returns immediately when the FIFO is full: that is the anti-wedge.
    void pump() {
        if (_out == _pend) { compact(); return; }
        if (!Serial) return;
        const int avail = Serial.availableForWrite();
        if (avail <= 0) return;
        size_t n = _pend - _out;
        if (static_cast<size_t>(avail) < n) n = static_cast<size_t>(avail);
        _out += Serial.write(reinterpret_cast<const uint8_t*>(_buf) + _out, n);
        if (_out == _pend) compact();
    }
    // Slide the unsent run — [_out, _pend) plus the line under assembly, which is contiguous with it — to the front.
    void compact() {
        if (_out == 0) return;
        const size_t keep = (_pend - _out) + _line;
        if (keep) memmove(_buf, _buf + _out, keep);
        _pend -= _out; _out = 0;
    }
    char     _buf[MR_CONSOLE_STAGE_BYTES];   // [_out,_pend) = committed, awaiting the wire · [_pend,_pend+_line) = assembling
    size_t   _out     = 0;                   // next committed byte to hand to Serial
    size_t   _pend    = 0;                   // end of the committed run
    size_t   _line    = 0;                   // bytes of the line currently being assembled
    bool     _over    = false;               // that line overflowed the stage -> drop it whole at its terminator
    uint32_t _dropped = 0;                   // whole lines lost, awaiting one deferred `!! CONSOLE_DROP lines=N`
};
#else
class GuardedConsole : public Print {              // production NullPrint — every call is a no-op, no Serial reference
public:
    using Print::write;
    size_t write(uint8_t) override { return 1; }
    size_t write(const uint8_t*, size_t n) override { return n; }
    int printf(const char*, ...) __attribute__((format(printf, 2, 3))) { return 0; }
    void flush() override {}
    void service() {}                              // the loop calls this unconditionally; no state, no storage
    uint32_t dropped_lines() const { return 0; }
};
#endif
}  // namespace mrcon_detail

inline mrcon_detail::GuardedConsole mrcon;          // the single sink (inline => one ODR-merged instance across TUs)
#endif  // ARDUINO
