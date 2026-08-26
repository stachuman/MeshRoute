#pragma once

#include <driver/gpio.h>

inline esp_err_t rtc_gpio_hold_dis(gpio_num_t pin) {
    probe_gpio::events.emplace_back("rtc_hold:" + std::to_string(pin));
    return probe_gpio::hold_release_ok[static_cast<size_t>(pin)] ? ESP_OK : ESP_FAIL;
}
