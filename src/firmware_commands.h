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
// ★ §AB2: a `peername` command -> rename the RAM entry + mirror to /mrpeers + the SYNCHRONOUS ack (spec 2026-07-29
// §2.3/§2.6(b): an ack, NOT a push — nothing is asynchronous, and no PushKind is touched). Same call sites as
// handle_peerkey: service_console (USB) and ble_dispatch_line.
size_t handle_peername(char* out, size_t cap, const meshroute::Command& cmd);

// §AB1 the /mrpeers address book (spec 2026-07-29 §2.4). The RECORD POLICY lives in mrnv:: (device_nv.h, pure +
// host-tested); these two are the I/O half, and they share one static blob buffer inside firmware_commands.cpp.
mrnv::PeerPut peer_store_sync(uint32_t key_hash32);   // mirror ONE live peer (key + name + confidence) into /mrpeers.
                                                     // Callers: handle_peerkey (a QR pin) and fw_main's
                                                     // PushKind::peer_key_cached case (an on-air key-learn).
uint16_t      peer_store_restore();                  // setup(): re-install the stored book AT THE STORED CONFIDENCE
                                                     // (prints the one-line boot summary); -> records re-installed

// §3 exports reached by the STAYING fw_main callers (setup / service_console / ble_dispatch_line / mesh_service_once):
bool dispatch(const char* line, size_t len, Print& out);            // the console verb-router
void print_banner(Print& out);                                      // setup() + `version`
void print_identity(const mrnv::IdBlob& idb);                       // setup()
void print_sf_list(uint16_t bitmap);                                // setup() + mesh_service_once()
const char* board_name();                                           // ble_dispatch_line `version`
void handle_routes(Print& out);                                     // ble_dispatch_line `routes`
// ★ §AB3: `peers` over BLE/companion — the BOUNDED (≤ cap_peer_keys) JSON address book, `peer`* then `peers_end`.
// The full up-to-256 id-only list is deliberately NOT reachable here (§2.6(a)); ble_dispatch_line refuses `peers all`
// with peers_err{console_only} rather than streaming a few hundred rows over a link that has wedged this node before.
void handle_peers(Print& out);                                      // ble_dispatch_line `peers`
// ★★ §id-hash S1 (spec §1-A): the REMEDY text for a refused `reqpubkey`, at parity with `handle_hashof`'s (which is in
// this TU, so the two wordings sit side by side and cannot drift). The bare CmdCode names the wall but not the way
// round it, and for this verb the way round it differs per plane — that is the whole point of the slice.
// Called from service_console (USB text) only: the companion gets the same facts STRUCTURED, as
// {"ack":"err_…","plane":"…"} — prose over a link that has wedged this node before is the wrong shape.
// A `queued` result prints nothing.
void print_reqpubkey_hint(Print& out, const meshroute::Command& cmd, const meshroute::CmdResult& r);
meshroute::console::StatusFields make_status_fields();              // ble_dispatch_line `status`
const char* node_state_str();                                       // ble_dispatch_line `status`
meshroute::console::CfgExtras make_cfg_extras();                    // ble_dispatch_line `cfg`

}  // namespace mrfw
