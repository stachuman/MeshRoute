// MeshRoute — src/dispatch_sink.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §command-sink-consolidation: the two CAPTURE sinks a non-serial transport passes to dispatch(line,len,out).
// Both are Print (so a handler written against `mrcon` — also a Print — works verbatim). BufferSink accumulates the
// whole response into a bounded char[] (remote/rcmd + the parity-reference capture); LineSink flushes each complete
// '\n'-terminated line to a callback (BLE streaming — routes/pull_inbox emit many lines, unbounded). Fixed-size,
// no-heap. Serial keeps passing the global `mrcon` (unchanged). Debug output NEVER reaches these — it stays on mrcon.
#pragma once
#if defined(ARDUINO)
#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>

class BufferSink : public Print {
public:
    using Print::write;
    size_t write(uint8_t b) override {
        if (_len + 1 < sizeof _buf) _buf[_len++] = static_cast<char>(b); else _truncated = true;
        return 1;
    }
    size_t write(const uint8_t* p, size_t n) override { for (size_t i = 0; i < n; ++i) write(p[i]); return n; }
    int printf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
        char b[160]; va_list ap; va_start(ap, fmt);
        const int n = vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
        if (n > 0) write(reinterpret_cast<const uint8_t*>(b), static_cast<size_t>(n) < sizeof b ? static_cast<size_t>(n) : sizeof b - 1);
        return n;
    }
    void flush() {}
    const char* data() const { return _buf; }
    size_t      len()  const { return _len; }
    bool        truncated() const { return _truncated; }   // response exceeded the buffer (LOUD: mark it, never silently drop)
    void        reset() { _len = 0; _truncated = false; _buf[0] = '\0'; }
private:
    char   _buf[512] = {};   // bounded: remote/rcmd responses are single-line (<256); status/cfg fit. Streaming uses LineSink.
    size_t _len = 0;
    bool   _truncated = false;
};

class LineSink : public Print {
public:
    using Print::write;
    using Flush = void (*)(const char* line, size_t n);
    explicit LineSink(Flush f) : _flush(f) {}
    size_t write(uint8_t b) override {
        if (_len < sizeof _buf) _buf[_len++] = static_cast<char>(b);
        if (b == '\n' || _len == sizeof _buf) { _flush(_buf, _len); _len = 0; }   // ship on newline OR buffer-full (never overflow)
        return 1;
    }
    size_t write(const uint8_t* p, size_t n) override { for (size_t i = 0; i < n; ++i) write(p[i]); return n; }
    int printf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
        char b[160]; va_list ap; va_start(ap, fmt);
        const int n = vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
        if (n > 0) write(reinterpret_cast<const uint8_t*>(b), static_cast<size_t>(n) < sizeof b ? static_cast<size_t>(n) : sizeof b - 1);
        return n;
    }
    void flush() { if (_len) { _flush(_buf, _len); _len = 0; } }   // ship any trailing partial (no-newline) line
private:
    // Sized to the widest console line so each '\n'-terminated line is buffered WHOLE and shipped in ONE _flush call —
    // byte-identical to today's ble_sink(s_inbox_jb, n) (mrble::tx_line then MTU-chunks it; the companion reassembles on
    // '\n'). Matches s_inbox_jb (fw_main.cpp): a 241-B inbox body 6x-escaped + envelope. A longer line still ships (it
    // flushes at the cap, no '\n') and the companion reassembles on '\n' regardless.
    char   _buf[1700] = {};
    Flush  _flush;
    size_t _len = 0;
};
#endif  // ARDUINO
