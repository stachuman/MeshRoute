// MeshRoute — tools/probe_inbox_verbs/identity_platform.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// ★★ W0 (brief §2.7; pre-check §7) — THE IDENTITY ARM's LOCAL PLATFORM SHIM. It is FORCE-INCLUDED (`-include`) into
//    ONE translation unit only: the REAL, UNEDITED `src/firmware_config.cpp` of the identity arm. The board build gets
//    these four facts from `platformio.ini` and `fw_main.cpp`; a host compile of that TU needs them supplied, and this
//    file supplies exactly those and nothing else — ⛔ it changes no production behaviour and is never a product header.
//      1. the LoRa build defaults — `[env]`'s own values (platformio.ini: LORA_FREQ … LORA_DUTY_CYCLE_PCT), which the
//         config TU reads as compile-time constants;
//      2. `meshroute::default_output_dbm` — `lib/hal/rf_capabilities.h` derives it from `LORA_TX_POWER` exactly as the
//         board build does, so it is included here AFTER the defaults;
//      3. `__FlashStringHelper` — the fake `F()` (tools/probe_board_ui/fakes/Arduino.h) already yields `const char*`,
//         so the type the config TU names in `role_refused`'s signature is that character type;
//      4. `g_persist_team_local_id` — declared `extern` in `fw_context.h` and DEFINED in `fw_main.cpp`, which no host
//         build compiles. Defined HERE because this header lands in exactly one TU, so the definition exists once.
#pragma once
#include <stdint.h>

#ifndef LORA_FREQ
#define LORA_FREQ 869.4625
#endif
#ifndef LORA_BW
#define LORA_BW 125.0
#endif
#ifndef LORA_SF
#define LORA_SF 8
#endif
#ifndef LORA_CR
#define LORA_CR 5
#endif
#ifndef LORA_TX_POWER
#define LORA_TX_POWER 22
#endif
#ifndef LORA_PREAMBLE_SYM
#define LORA_PREAMBLE_SYM 16
#endif
#ifndef LORA_DUTY_CYCLE_PCT
#define LORA_DUTY_CYCLE_PCT 10
#endif

using __FlashStringHelper = char;
#include "rf_capabilities.h"

uint8_t g_persist_team_local_id = 0;
