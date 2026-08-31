// MeshRoute — tools/probe_inbox_verbs/fakes/nvs.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §CUSTODY-D host-probe stand-in for an ESP32/arduino-esp32 platform header, so the REAL
// `src/firmware_commands.cpp` + `src/firmware_inbox.cpp` compile on the host under the `[env:heltec_v3]`
// `-D` arm. ⛔ NOTHING THE PROBE ASSERTS GOES THROUGH THIS FILE — the inbox verbs never touch NV, the FS or
// the RNG. Each entry point therefore answers the LEAST capable honest value (absent / would-not-mount), so a
// probe check that accidentally started depending on one would FAIL rather than read a pretend success.
// ⓘ Kept in THIS probe's own dir rather than added to the shared `probe_board_ui`/`probe_device_radio` fakes:
//   they are this arm's platform, not a shared Arduino surface (U1 — reuse what is shared, do not widen it).
#pragma once
#include <cstdint>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NVS_NOT_FOUND 0x1102
#define ESP_ERR_NVS_NOT_INITIALIZED 0x1101
typedef uint32_t nvs_handle_t;
typedef enum { NVS_READONLY = 0, NVS_READWRITE = 1 } nvs_open_mode_t;
inline esp_err_t nvs_open(const char*, nvs_open_mode_t, nvs_handle_t*) { return ESP_ERR_NVS_NOT_FOUND; }
inline void nvs_close(nvs_handle_t) {}
inline esp_err_t nvs_get_blob(nvs_handle_t, const char*, void*, size_t*) { return ESP_ERR_NVS_NOT_FOUND; }
