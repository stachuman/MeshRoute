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
#include "console_parse.h" // meshroute::console::ParseErr — NAMED in ExecResult below, so it is included directly
                           // rather than left to arrive transitively through an unrelated header (U3).
#include "mr_features.h"   // MR_FEAT_OLED — the emergency seam below is compiled per profile (U3: named, not
                           // left to arrive transitively)

namespace mrfw {

class PresetCatalog;       // §UI-10/11 P2 — src/firmware_ui_presets.h. Forward-declared, ⛔ not included: this
                           // header is pulled into every device TU and the catalog is needed by two of them.

// ★★★ §UI-10/11 P2 — THE `busy` FACT'S SEAM, and it is a SEAM rather than a direct read for one hard reason: the
// UI model (`s_model`, `src/firmware_ui.cpp`) is FILE-LOCAL and its TU is compiled only where `MR_FEAT_OLED=1`
// (platformio.ini:218 lists `firmware_ui.cpp` for the heltec_v3 family and nowhere else), while the `ui preset`
// verbs live in `firmware_commands.cpp`, which EVERY env compiles. ⇒ the fact crosses one declared boundary.
// ⛔ NOT A NINTH `lib/hal/mr_ui.h` HOOK: that header is `lib/`, this slice is `src/`-confined by the spec's §3
//    ruling, and a hook there would re-anchor nothing but would put a P2 decision outside the slice's own tree.
// ★ THE NON-OLED ANSWER IS `false`, AND IT IS A FACT RATHER THAN A DEFAULT (C2): a profile with no panel has no
//   emergency long-press, no alarm state machine and therefore no attempt series a preset edit could interrupt.
//   ⛔ It is NOT "we could not tell" — that would be `true` (fail closed), and answering `busy` for ever on a
//   gateway would make the verbs permanently dead there.
// ⓘ The CLASSIFICATION — which `mrui::Emergency` states are an ACTIVE attempt series — is the OLED TU's, beside
//   the model it reads, and is pinned by `tools/probe_firmware_ui/` (which compiles that TU for real).
#if MR_FEAT_OLED
bool ui_emergency_active();
#else
inline bool ui_emergency_active() { return false; }
#endif

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
void print_sf_list(Print& out, uint16_t bitmap);                    // §B95: takes its sink — setup() + mesh_service_once() pass mrcon, dump_cfg passes `out`
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

// ★★ UI-7 — THE ONE FIRMWARE SURFACE THIS PLAN ADDS (owner-approved 2026-08-01). Parse ONE command line and execute
// it on the node, returning the TYPED result. No text output at all: the caller wants the `CmdResult`, not a human
// string — and the board UI is the third consumer of this sequence and the FIRST that needs the result
// programmatically. Scraping a `BufferSink` was explicitly rejected (spec §2.1): a safety behaviour must not hang off
// a formatting detail, and a discarded sink leaves a refused send on `SENDING...` for ever.
// ⚠ IT IS NOT A WRAPPER AROUND `dispatch()`. `dispatch` is a console VERB ROUTER and returns `false` for `send` /
//   `send_channel`; the send path lives in its CALLERS, which open-code `parse_command` + `Node::on_command`
//   (`fw_main.cpp:879-892` service_console, `:485-489` ble_dispatch_line). Routing a UI send through `dispatch` would
//   have returned false and sent NOTHING.
// ⚠ The two existing call sites are DELIBERATELY NOT retrofitted onto this helper. They use OPPOSITE orderings
//   relative to `dispatch()`, so unifying them is a behaviour change on two working transports and needs its own
//   slice and gate (C1). This is purely ADDITIVE: nothing that works today changes.
// ⓘ `Command::body` BORROWS into `line` (console_parse.h:17-20), so `on_command` must run before `line` is reused —
//   which is why the parse and the execute are one function and not two.
// ★★ §UI-10/11 P2 — THE ONE `/mrui` CATALOG INSTANCE, and it is exported for the reason `join_profile_service()`
// is: P3's OLED compose lists read the SAME catalog the `ui preset` verbs write, or the panel and the console
// become two opinions about the wearer's phrases. Function-local static (constructed on first call), so there is
// no cross-TU initialisation-order question.
// ⓘ THE FACT P3 NEEDS FROM P2 IS ALREADY IN IT: `generation()`. A successful durable mutation stamps the NEXT
//   generation into the record BEFORE the save (P1's `commit`), so a frozen frame's sealed generation stops
//   comparing equal the moment a change lands — which is §3.2.3's modal-close trigger AND its stale-`SendReq`
//   refusal, from ONE fact and with ⛔ no new hook.
PresetCatalog& preset_catalog();
// setup(): load `/mrui` through the four-state read and print the ruled diagnostic line for the two fault states
// (`ok`/`absent` print NOTHING). ⛔ ZERO writes on every arm. Same shape and same reason as peer_store_restore().
void preset_boot_restore_console();
// `ui preset list|set|clear|reset` — dispatched from BOTH transports through the ONE `dispatch(line,len,Print&)`.
void handle_ui(const char* args, size_t len, Print& out);

struct ExecResult {
    bool                         ok        = false;                                  // false => the line did not parse
    meshroute::console::ParseErr parse_err = meshroute::console::ParseErr::ok;
    meshroute::CmdResult         result{};                                           // valid only when `ok`
};
ExecResult exec_command(const char* line, size_t len);

}  // namespace mrfw
