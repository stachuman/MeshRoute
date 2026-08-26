// V4-2 production-radio probe shim. It supplies only the Arduino surface pulled by device_radio.h/frame_trace.h;
// all radio behavior is counted in the CustomSX1262 fake beside it.
#pragma once

#include <cstddef>
#include <cstdint>

#define F(x) (x)
#define HEX 16
#define IRAM_ATTR

inline uint32_t g_probe_millis = 0;
inline uint32_t millis() { return g_probe_millis; }

struct ProbeSerial {
    template <typename T> size_t print(const T&) { return 0; }
    template <typename T> size_t print(const T&, int) { return 0; }
    template <typename T> size_t println(const T&) { return 0; }
    size_t println() { return 0; }
    size_t write(const uint8_t*, size_t n) { return n; }
    void flush() {}
};

inline ProbeSerial Serial;
