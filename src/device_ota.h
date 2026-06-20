// MeshRoute — src/device_ota.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// ESP32-S3 WiFi OTA interface (Heltec V3 + XIAO ESP32-S3). The WiFi/WebServer/Update implementation
// is in device_ota.cpp (compiled in via build_src_filter on the ESP32 envs) so the heavy framework
// includes don't leak into the shared fw_main.cpp translation unit. XIAO nRF52840 OTA stays the
// existing BLE DFU path (it never calls these — the call sites are ESP32-guarded).
#pragma once
#include <stdint.h>

namespace mrota {

bool ota_start();        // bring up SoftAP + web server; false on failure
void ota_stop();         // tear down
void ota_loop();         // call from main loop — handles one HTTP request (non-blocking)
bool ota_active();       // is the server currently running?

}  // namespace mrota
