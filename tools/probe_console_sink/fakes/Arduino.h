// §B95 PROBE SHIM — a host stand-in for Arduino.h so the REAL src/console_sink.h can be compiled and its line
// admission MEASURED. Not part of the firmware build.
//
// Two things must be faithful or the probe measures a fiction:
//   1. `Print`. The defect lives in the CALL GRANULARITY of Arduino's Print: `println(F("x"))` is TWO writes (the
//      text, then "\r\n" as its own call), `print(int)` is its own call, and dump_cfg prints `leaf_name` ONE
//      CHARACTER PER CALL. That granularity is what let labels vanish while values survived, so it is reproduced
//      exactly (see the real cores: Print::println(fs) = print(fs) + println(); println() = write("\r\n", 2)).
//   2. `Serial`. Modelled on the two MEASURED transports:
//        ESP32-S3 UART0  — availableForWrite() = free bytes of the 128-B hardware TX FIFO (no TX ring buffer).
//        nRF52 TinyUSB   — availableForWrite() = free bytes of CFG_TUD_CDC_TX_BUFSIZE = 256, and write() LOOPS WITH
//                          yield() if handed more than that. ⇒ FakeSerial FLAGS any write longer than
//                          availableForWrite() as an anti-wedge VIOLATION instead of quietly accepting it.
//      Capacity is freed by an asynchronous drainer, exactly as the USB/UART task does: `drain(k)` (a host read) and
//      `auto_drain` (bytes freed on every availableForWrite() call — the between-fragment capacity change that the
//      bench evidence exhibits).
#pragma once
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

class __FlashStringHelper;
#define F(x) (reinterpret_cast<const __FlashStringHelper*>(x))
#define HEX 16

// ---- Print: the subset the console formatters use, with the real cores' call granularity --------------------------
class Print {
public:
    virtual ~Print() = default;
    virtual size_t write(uint8_t b) = 0;
    virtual size_t write(const uint8_t* buf, size_t n) = 0;
    size_t write(const char* s) { return s ? write(reinterpret_cast<const uint8_t*>(s), strlen(s)) : 0; }
    size_t write(const char* s, size_t n) { return write(reinterpret_cast<const uint8_t*>(s), n); }
    virtual void flush() {}

    size_t print(const char* s)                    { return write(s); }
    size_t print(const __FlashStringHelper* fs)    { return write(reinterpret_cast<const char*>(fs)); }
    size_t print(char c)                           { return write(static_cast<uint8_t>(c)); }
    size_t print(int v)                            { char b[16]; snprintf(b, sizeof b, "%d", v);   return write(b); }
    size_t print(unsigned v)                       { char b[16]; snprintf(b, sizeof b, "%u", v);   return write(b); }
    size_t print(long v)                           { char b[24]; snprintf(b, sizeof b, "%ld", v);  return write(b); }
    size_t print(unsigned long v)                  { char b[24]; snprintf(b, sizeof b, "%lu", v);  return write(b); }
    size_t print(double v, int digits = 2)         { char b[40]; snprintf(b, sizeof b, "%.*f", digits, v); return write(b); }
    size_t println()                               { return write("\r\n", 2); }                    // ONE call, exactly as the cores do
    size_t println(const char* s)                  { size_t n = print(s);  return n + println(); }  // TWO calls — the terminator is separate
    size_t println(const __FlashStringHelper* fs)  { size_t n = print(fs); return n + println(); }
    size_t println(int v)                          { size_t n = print(v);  return n + println(); }
    size_t println(unsigned v)                     { size_t n = print(v);  return n + println(); }
    size_t println(double v, int d = 2)            { size_t n = print(v, d); return n + println(); }
};

// ---- FakeSerial: the transport model -----------------------------------------------------------------------------
class FakeSerial {
public:
    void reset(size_t capacity, bool connected_ = true) {
        cap = capacity; occupied = 0; wire.clear(); connected = connected_;
        writes = 0; bool_calls = 0; flushes = 0; overrun_writes = 0; auto_drain = 0;
    }
    explicit operator bool() { ++bool_calls; return connected; }
    int availableForWrite() {
        if (!connected) return 0;                       // nRF52 returns 0 when the CDC is invalid
        drain(auto_drain);                              // the async drainer frees capacity BETWEEN caller fragments
        return static_cast<int>(cap - occupied);
    }
    size_t write(const uint8_t* p, size_t n) {
        if (!connected) return 0;
        const size_t room = cap - occupied;
        if (n > room) ++overrun_writes;                 // ★ the anti-wedge violation: the real cores block/yield here
        const size_t take = n < room ? n : room;
        wire.append(reinterpret_cast<const char*>(p), take);
        occupied += take; ++writes;
        return take;
    }
    size_t write(uint8_t b) { return write(&b, 1); }
    void flush() { ++flushes; occupied = 0; }           // the host reads everything (blocking, in the real core)
    void drain(size_t k) { occupied -= (k < occupied ? k : occupied); }

    size_t      cap = 128, occupied = 0;
    bool        connected = true;
    std::string wire;                                   // everything the host has received, in order
    size_t      auto_drain = 0;
    long        writes = 0, bool_calls = 0, flushes = 0, overrun_writes = 0;
};
extern FakeSerial Serial;
