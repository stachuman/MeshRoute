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
// esp_err_t gpio_set_intr_type(gpio_num_t, gpio_int_type_t) — the real signature. ★★ §B200 ROUND 3: this is the ONLY
// call that clears `GPIO_PINn_REG`'s INT_TYPE field (bits 9:7). `gpio_wakeup_disable()` clears bit 10 alone, so
// without this the pin stays configured to interrupt on a level a held button asserts — across a CPU reset.
esp_err_t gpio_set_intr_type(gpio_num_t gpio_num, gpio_int_type_t intr_type);
