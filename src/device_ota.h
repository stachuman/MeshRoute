// MeshRoute — src/device_ota.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Heltec ESP32-S3 WiFi OTA interface. The WiFi/WebServer/Update implementation is in
// device_ota.cpp (compiled only for heltec_v3 via build_src_filter) so the heavy
// framework includes don't leak into the shared fw_main.cpp translation unit.
// XIAO nRF52840 OTA stays the existing BLE DFU path.
#pragma once
#include <stdint.h>

namespace mrota {

bool ota_start();        // bring up SoftAP + web server; false on failure
void ota_stop();         // tear down
void ota_loop();         // call from main loop — handles one HTTP request (non-blocking)
bool ota_active();       // is the server currently running?

}  // namespace mrota
