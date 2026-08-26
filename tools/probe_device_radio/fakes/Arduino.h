// V4-3 production-radio probe shim. It supplies the Arduino surface pulled by device_radio.h and by the real
// Heltec V4 board_rf.cpp. GPIO state/logging lives here as inline data so both probe translation units share it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <string>
#include <vector>

#define F(x) (x)
#define HEX 16
#define IRAM_ATTR
#define LOW 0
#define HIGH 1
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define INPUT_PULLDOWN 3

inline uint32_t g_probe_millis = 0;
inline uint32_t millis() { return g_probe_millis; }

namespace probe_gpio {
inline std::vector<std::string> events;
inline std::array<int, 64> modes{};
inline std::array<int, 64> levels{};
inline std::array<int, 2> pullup_reads{HIGH, HIGH};
inline std::array<int, 2> pulldown_reads{LOW, LOW};
inline size_t pullup_index = 0;
inline size_t pulldown_index = 0;

inline void reset() {
    events.clear();
    modes.fill(INPUT);
    levels.fill(LOW);
    pullup_reads = {HIGH, HIGH};
    pulldown_reads = {LOW, LOW};
    pullup_index = 0;
    pulldown_index = 0;
}
}  // namespace probe_gpio

inline void pinMode(uint8_t pin, uint8_t mode) {
    probe_gpio::modes[pin] = mode;
    probe_gpio::events.emplace_back("mode:" + std::to_string(pin) + ":" + std::to_string(mode));
}
inline void digitalWrite(uint8_t pin, uint8_t level) {
    probe_gpio::levels[pin] = level;
    probe_gpio::events.emplace_back("write:" + std::to_string(pin) + ":" + std::to_string(level));
}
inline int digitalRead(uint8_t pin) {
    probe_gpio::events.emplace_back("read:" + std::to_string(pin));
    if (probe_gpio::modes[pin] == INPUT_PULLUP) {
        const size_t i = probe_gpio::pullup_index < 2 ? probe_gpio::pullup_index++ : 1;
        return probe_gpio::pullup_reads[i];
    }
    if (probe_gpio::modes[pin] == INPUT_PULLDOWN) {
        const size_t i = probe_gpio::pulldown_index < 2 ? probe_gpio::pulldown_index++ : 1;
        return probe_gpio::pulldown_reads[i];
    }
    return probe_gpio::levels[pin];
}
inline void delay(uint32_t ms) {
    g_probe_millis += ms;
    probe_gpio::events.emplace_back("delay:" + std::to_string(ms));
}

struct ProbeSerial {
    template <typename T> size_t print(const T&) { return 0; }
    template <typename T> size_t print(const T&, int) { return 0; }
    template <typename T> size_t println(const T&) { return 0; }
    size_t println() { return 0; }
    size_t write(const uint8_t*, size_t n) { return n; }
    void flush() {}
};

inline ProbeSerial Serial;
