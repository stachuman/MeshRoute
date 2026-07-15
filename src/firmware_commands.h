// MeshRoute — src/firmware_commands.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// The console COMMAND cluster extracted from fw_main.cpp (cleanup 2026-07-15, codebase-review triage; seam spec
// docs/superpowers/specs/2026-07-15-firmware-commands-seam-design.md). The `dispatch` verb-router + the diagnostic/
// console handlers. Moved in dependency-safe batches; this header declares ONLY the §3 public entry points that the
// STAYING fw_main callers (service_console / ble_dispatch_line / setup / mesh_service_once) reach — every other
// moved handler is `static` in firmware_commands.cpp (dispatch calls them in-TU).
//
// STAY in fw_main (device_fault.h ISR-vector + MRFAULT_HW/MRFAULT_ESP32 MACRO trap): do_reboot / do_ota /
// dump_faults / handle_crashtest; handle_prep_restart (loop g_halted). dispatch reaches those via the fw_context.h
// wrappers fw_reboot / fw_ota / fw_faults_dump / fw_crashtest / fw_prep_restart.
//
// DEVICE-layer header.
#pragma once
#include <Arduino.h>   // Print
#include <cstddef>     // size_t
#include "command.h"   // meshroute::Command (handle_peerkey)
#include <cstdint>     // uint16_t (print_sf_list)
#include "device_nv.h" // mrnv::IdBlob (print_identity)
#include "console_json.h" // meshroute::console::StatusFields / CfgExtras

namespace mrfw {

// E2E §3: a `peerkey` command -> install the RAM PINNED key + persist to /mrpeers + the contract ack.
size_t handle_peerkey(char* out, size_t cap, const meshroute::Command& cmd);

// §3 exports reached by the STAYING fw_main callers (setup / service_console / ble_dispatch_line / mesh_service_once):
bool dispatch(const char* line, size_t len, Print& out);            // the console verb-router
void print_banner(Print& out);                                      // setup() + `version`
void print_identity(const mrnv::IdBlob& idb);                       // setup()
void print_sf_list(uint16_t bitmap);                                // setup() + mesh_service_once()
const char* board_name();                                           // ble_dispatch_line `version`
void handle_routes(Print& out);                                     // ble_dispatch_line `routes`
meshroute::console::StatusFields make_status_fields();              // ble_dispatch_line `status`
const char* node_state_str();                                       // ble_dispatch_line `status`
meshroute::console::CfgExtras make_cfg_extras();                    // ble_dispatch_line `cfg`

}  // namespace mrfw
