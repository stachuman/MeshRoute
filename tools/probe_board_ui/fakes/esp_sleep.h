// MeshRoute — tools/probe_board_ui/fakes/esp_sleep.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// phA5 PROBE SHIM for the TWO ESP-IDF `esp_sleep.h` entry points `variants/heltec_v3/board_ui.cpp` uses
// (§B197 admitted the source; §B200 withdraws it again after every sleep).
// ⛔ Deliberately does NOT declare `esp_sleep_enable_ext1_wakeup`, `esp_light_sleep_start`, the timer source or
//    `esp_sleep_get_wakeup_cause`: those belong to `src/fw_main.cpp`'s `board_sleep_until()`, which this probe does
//    not compile and which owns the radio's wake path. Their absence is what makes "the board TU did not start
//    reordering the radio's wake path" a BUILD FACT rather than a review promise.
#pragma once
#include <esp_wake_probe.h>

// esp_err_t esp_sleep_enable_gpio_wakeup(void) — the real signature (IDF `esp_sleep.h`). This is what admits the
// per-pin sources configured by gpio_wakeup_enable() to the NEXT esp_light_sleep_start().
esp_err_t esp_sleep_enable_gpio_wakeup(void);
// esp_err_t esp_sleep_disable_wakeup_source(esp_sleep_source_t) — the real signature. §B200's other withdrawal: it
// takes the GPIO source back OUT, so no later sleep can inherit an arm nobody asked for.
esp_err_t esp_sleep_disable_wakeup_source(esp_sleep_source_t source);
