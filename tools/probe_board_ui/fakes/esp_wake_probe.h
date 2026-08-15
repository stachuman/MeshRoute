// MeshRoute — tools/probe_board_ui/fakes/esp_wake_probe.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// phA5 PROBE SHIM — the host stand-in for the FOUR ESP-IDF calls §B197/§B200 use to arm and DISARM the user button as
// a light-sleep wake source. `driver/gpio.h` and `esp_sleep.h` (the two other files in this directory) both include
// THIS one, so the counters and the scripted return codes live in exactly one place rather than being duplicated per
// header (U1).
//
// ★ WHY IT HAS TO EXIST AT ALL: `variants/heltec_v3/board_ui.cpp` is compiled by neither the native suite nor the
//   simulator, so `arm_button_wake()` / `disarm_button_wake()` — functions whose ONLY observable behaviour is which
//   platform calls they make and what they do with the return codes — would otherwise have no automated cover at all.
// ★★ §B200 ADDED THE TWO TEARDOWN CALLS, and they are not decoration: the DEFECT was an arm that was never undone.
//   A shim that could only see arming would go green against the exact bug it is here to prevent.
//
// ⛔ WHAT IT CANNOT SAY, stated so a green probe is not over-read: this shim proves the CALLS, their ARGUMENTS, their
//    ORDER and the handling of BOTH failure returns. It cannot prove that the ESP32-S3 actually wakes, and it cannot
//    prove that this digital-domain GPIO source COEXISTS with the radio's RTC-domain `ext1` DIO1 source in light
//    sleep. That is the design's one unproven hardware assumption and it is metal-only (bench script Part 23).
#pragma once
#include <cstdint>

// ---- the fragment of the IDF error contract this shim needs -------------------------------------------------------
// ⓘ `esp_err_t` is a signed int and `ESP_OK` is 0 in the real headers; only those two facts are load-bearing here.
//   The one non-OK value below stands in for the whole family (`ESP_ERR_INVALID_ARG` is 0x102), because the code under
//   test must treat EVERY non-OK code the same way — it checks `!= ESP_OK`, never a specific code.
typedef int esp_err_t;
static constexpr esp_err_t ESP_OK               = 0;
static constexpr esp_err_t ESP_ERR_INVALID_ARG  = 0x102;

typedef int gpio_num_t;
// The light-sleep-capable GPIO trigger types. ★ The two EDGE values are declared DELIBERATELY even though the
// firmware must not use them: ESP32 light-sleep GPIO wake accepts only the two LEVEL triggers, so a check asserting
// "the level trigger was requested" measures nothing unless the wrong answers are expressible.
typedef int gpio_int_type_t;
static constexpr gpio_int_type_t GPIO_INTR_DISABLE    = 0;
static constexpr gpio_int_type_t GPIO_INTR_POSEDGE    = 1;
static constexpr gpio_int_type_t GPIO_INTR_NEGEDGE    = 2;
static constexpr gpio_int_type_t GPIO_INTR_ANYEDGE    = 3;
static constexpr gpio_int_type_t GPIO_INTR_LOW_LEVEL  = 4;
static constexpr gpio_int_type_t GPIO_INTR_HIGH_LEVEL = 5;

// The wake-source selector `esp_sleep_disable_wakeup_source()` takes. ★ `ESP_SLEEP_WAKEUP_EXT1` is declared
// DELIBERATELY even though this TU must NEVER pass it: EXT1 is the RADIO's DIO1 source, owned by fw_main, and a
// teardown that disabled it would silently take the radio's wake path down. The wrong answer has to be expressible
// for the check that it is not used to mean anything.
typedef int esp_sleep_source_t;
static constexpr esp_sleep_source_t ESP_SLEEP_WAKEUP_TIMER = 1;
static constexpr esp_sleep_source_t ESP_SLEEP_WAKEUP_GPIO  = 2;
static constexpr esp_sleep_source_t ESP_SLEEP_WAKEUP_EXT1  = 3;

// ---- what the probe MEASURES --------------------------------------------------------------------------------------
// ★ `seq_*` are ORDER stamps, not booleans: `gpio_wakeup_enable` must configure the pin BEFORE
//   `esp_sleep_enable_gpio_wakeup` admits the source to the next sleep. A pair of "was it called" flags cannot see a
//   swap, and a swap is a plausible wrong answer rather than an exotic one.
struct ProbeWake {
    int gpio_wakeup_calls = 0;
    int sleep_enable_calls = 0;
    int last_gpio = -1;                       // which pin gpio_wakeup_enable was asked for
    int last_intr = -1;                       // ...and with which trigger type
    int seq_gpio_wakeup = 0;                  // 0 = never called; otherwise the 1-based call ordinal
    int seq_sleep_enable = 0;
    int next_seq = 1;
    esp_err_t gpio_wakeup_result  = ESP_OK;   // scriptable: the probe drives BOTH failure arms
    esp_err_t sleep_enable_result = ESP_OK;
    // ---- §B200: the TEARDOWN half. Same shape, same reason -------------------------------------------------------
    // ★ `seq_*` again, because the ROLLBACK is an order fact: on a partial arm the disable must happen BEFORE the
    //   function returns, and on a normal disarm both withdrawals must happen AFTER the pair that armed them.
    int gpio_disable_calls = 0;
    int sleep_disable_calls = 0;
    int last_disable_gpio = -1;               // which pin gpio_wakeup_disable was asked for
    int last_disable_src  = -1;               // which source esp_sleep_disable_wakeup_source was asked for
    int seq_gpio_disable = 0;
    int seq_sleep_disable = 0;
    esp_err_t gpio_disable_result  = ESP_OK;  // scriptable: both teardown failure arms are reachable
    esp_err_t sleep_disable_result = ESP_OK;
};
extern ProbeWake g_wake;
