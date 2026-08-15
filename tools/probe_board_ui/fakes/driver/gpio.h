// MeshRoute — tools/probe_board_ui/fakes/driver/gpio.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// phA5 PROBE SHIM for the ONE ESP-IDF `driver/gpio.h` entry point `variants/heltec_v3/board_ui.cpp` uses (§B197).
// ⛔ Not a port of the header: everything else in the real one is deliberately absent, so a future use of another IDF
//    GPIO call is a BUILD FAILURE here rather than a silently unmeasured call.
#pragma once
#include <esp_wake_probe.h>

// esp_err_t gpio_wakeup_enable(gpio_num_t, gpio_int_type_t) — the real signature (IDF `driver/gpio.h`).
esp_err_t gpio_wakeup_enable(gpio_num_t gpio_num, gpio_int_type_t intr_type);
