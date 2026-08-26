#pragma once

#include <Arduino.h>

using gpio_num_t = int;
using esp_err_t = int;
static constexpr esp_err_t ESP_OK = 0;
static constexpr esp_err_t ESP_FAIL = -1;

namespace probe_gpio {
inline std::array<bool, 64> hold_release_ok = [] {
    std::array<bool, 64> v{};
    v.fill(true);
    return v;
}();

inline void reset_holds() { hold_release_ok.fill(true); }
}  // namespace probe_gpio

inline esp_err_t gpio_hold_dis(gpio_num_t pin) {
    probe_gpio::events.emplace_back("gpio_hold:" + std::to_string(pin));
    return probe_gpio::hold_release_ok[static_cast<size_t>(pin)] ? ESP_OK : ESP_FAIL;
}
