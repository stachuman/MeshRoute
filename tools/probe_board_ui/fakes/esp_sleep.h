// MeshRoute — tools/probe_board_ui/fakes/esp_sleep.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// phA5 PROBE SHIM for the ONE ESP-IDF `esp_sleep.h` entry point `variants/heltec_v3/board_ui.cpp` uses (§B197).
// ⛔ Deliberately does NOT declare `esp_sleep_enable_ext1_wakeup`, `esp_light_sleep_start` or the timer source: those
//    belong to `src/fw_main.cpp`'s `board_sleep_until()`, which this probe does not compile and which §B197 does not
//    touch. Their absence is what makes "the board TU did not start reordering the radio's wake path" a build fact.
#pragma once
#include <esp_wake_probe.h>

// esp_err_t esp_sleep_enable_gpio_wakeup(void) — the real signature (IDF `esp_sleep.h`). This is what admits the
// per-pin sources configured by gpio_wakeup_enable() to the NEXT esp_light_sleep_start().
esp_err_t esp_sleep_enable_gpio_wakeup(void);
