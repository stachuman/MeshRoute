// MeshRoute — tools/probe_board_ui/fakes/driver/gpio.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// phA5 PROBE SHIM for the TWO ESP-IDF `driver/gpio.h` entry points `variants/heltec_v3/board_ui.cpp` uses
// (§B197 armed; §B200 added the disarm that makes the arm safe).
// ⛔ Not a port of the header: everything else in the real one is deliberately absent, so a future use of another IDF
//    GPIO call is a BUILD FAILURE here rather than a silently unmeasured call.
#pragma once
#include <esp_wake_probe.h>

// esp_err_t gpio_wakeup_enable(gpio_num_t, gpio_int_type_t) — the real signature (IDF `driver/gpio.h`).
esp_err_t gpio_wakeup_enable(gpio_num_t gpio_num, gpio_int_type_t intr_type);
// esp_err_t gpio_wakeup_disable(gpio_num_t) — the real signature. ★ THE COUNTERPART, and the whole of [[B200]] is
// that it was never called: a LEVEL trigger left armed on a running core re-asserts for as long as the button is
// held, storming the shared GPIO ISR until the Interrupt watchdog fires.
esp_err_t gpio_wakeup_disable(gpio_num_t gpio_num);
