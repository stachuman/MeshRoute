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
#include "console_line.h"
#include "firmware_command_context.h"
#include "mr_features.h"   // MR_FEAT_OLED — the emergency seam below is compiled per profile (U3: named, not
                           // left to arrive transitively)

namespace mrfw {

// ★★ [[B255]] — OWNER-RULED (b), 2026-08-28: **THE WHOLE `/mrui` CONSOLE SURFACE IS `MR_FEAT_OLED`-GATED.** A
// headless build cannot select a preset, and the one live catalog cost it 1132 B of `.bss` for a panel it does not
// have (QG's linked `gateway` ELF: `preset_catalog()::cat` at exactly 0x46c). ⇒ off `MR_FEAT_OLED` `preset_catalog()`
// and `handle_ui()` are ABSENT (the `firmware_config.h` idiom for a compiled-out verb family — ⛔ NO inline stub for
// either: a stub needs an inert ANSWER, a `PresetCatalog&` has none, and one returning a reference would
// re-instantiate the very 1132 B the ruling reclaims), while `preset_boot_restore_console()` DOES get an inert
// inline stub, for the structural reason written at that `#else` below.
// ⛔ THE PURE UNITS STAY UNGATED (`src/firmware_ui_presets.h`, `src/firmware_ui_preset_verbs.h`, the `'MRU1'` record
//    in `device_nv.h`): the native suite compiles all three unchanged. Only the INSTANTIATION and the console
//    surfacing are compiled out, so every `MR_FEAT_OLED=1` env keeps the feature exactly as it was.
#if MR_FEAT_OLED
class PresetCatalog;       // §UI-10/11 P2 — src/firmware_ui_presets.h. Forward-declared, ⛔ not included: this
                           // header is pulled into every device TU and the catalog is needed by two of them.
#endif   // MR_FEAT_OLED

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
// §2.3/§2.6(b): an ack, NOT a push — nothing is asynchronous, and no PushKind is touched). ⛔ V1 CORRECTION
// (§RADMIN-0c): this used to say *"Same call sites as handle_peerkey: service_console (USB) and
// ble_dispatch_line."* — WITHDRAWN. Since 0c both verbs have exactly ONE caller, `exec_console_line()` below, which
// serves both transports; the two callers reach them only through it.
size_t handle_peername(char* out, size_t cap, const meshroute::Command& cmd);

// §AB1 the /mrpeers address book (spec 2026-07-29 §2.4). The RECORD POLICY lives in mrnv:: (device_nv.h, pure +
// host-tested); these two are the I/O half, and they share one static blob buffer inside firmware_commands.cpp.
mrnv::PeerPut peer_store_sync(uint32_t key_hash32);   // mirror ONE live peer (key + name + confidence) into /mrpeers.
                                                     // Callers: handle_peerkey (a QR pin) and fw_main's
                                                     // PushKind::peer_key_cached case (an on-air key-learn).
uint16_t      peer_store_restore();                  // setup(): re-install the stored book AT THE STORED CONFIDENCE
                                                     // (prints the one-line boot summary); -> records re-installed

// §3 exports reached by the STAYING fw_main callers (setup / service_console / ble_dispatch_line / mesh_service_once).
// ⛔ V1 CORRECTION (§RADMIN-0c): `dispatch` itself is NO LONGER one of them. Since 0c neither transport calls the
// router directly — both call `exec_console_line()` below, which owns the router-versus-parser fork once. The
// export stays because the host probes (`tools/probe_inbox_verbs`) drive the router directly, which is how the
// verb map is gated at all.
bool dispatch(const char* line, size_t len, Print& out);            // the console verb-router
void print_banner(Print& out);                                      // setup() + `version`
extern const char kBuildStamp[];                                    // one device-image build timestamp authority
extern const char kGitRevision[];                                   // one device-image Git-revision authority
void print_rf_diagnostics(Print& out);                              // setup() + USB `status`; no structured/BLE contract
void print_identity(const mrnv::IdBlob& idb, Print& out);           // §0b/[[B279]]: takes its sink — setup() passes
                                                                    //   mrcon, do_regen passes the dispatch `out`
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
// Reached on the USB text arm only: the companion gets the same facts STRUCTURED, as {"ack":"err_…","plane":"…"} —
// prose over a link that has wedged this node before is the wrong shape. ⛔ V1 CORRECTION (§RADMIN-0c): this used
// to say *"Called from service_console (USB text) only"* — WITHDRAWN. Since 0c its one caller is
// `exec_console_line()`'s `LineFormat::text` arm; `service_console` reaches it only through that.
// A `queued` result prints nothing.
void print_reqpubkey_hint(Print& out, const meshroute::Command& cmd, const meshroute::CmdResult& r);
meshroute::console::StatusFields make_status_fields();              // ble_dispatch_line `status`
const char* node_state_str();                                       // ble_dispatch_line `status`
meshroute::console::CfgExtras make_cfg_extras();                    // ble_dispatch_line `cfg`

// ★★★ §RADMIN-0c — THE ONE TRANSPORT-NEUTRAL LOCAL EXECUTION SEAM (design §12 / §19 slice 0c).
//
// ⛔ THE DEFECT IT CLOSES, STATED AS A DEFECT. Until 0c, `service_console` (USB) and `ble_dispatch_line` (BLE) each
//    open-coded the SAME fork — offer the line to `dispatch()`, else `parse_command` + `handle_peerkey` /
//    `handle_peername` / `Node::on_command` — and they did it in OPPOSITE ORDERS (USB asked the router first, BLE
//    asked the parser first). Two working transports, one decision, two implementations: every change to local
//    execution had to be made twice, correctly, or the grammar drifted between USB and the companion. This function
//    is now the ONLY place that decision is made.
//
// ★ THE ORDER IS ROUTER-FIRST, AND IT IS SAFE BECAUSE IT WAS MEASURED, NOT BECAUSE IT READS BETTER. The two orders
//   can differ only on a line BOTH surfaces accept, so `tools/probe_console_sink/ownership.py` derives both sets
//   from the GENERATED command inventory on all six real product profiles and requires the intersection EMPTY
//   (41 router forms vs 7 parser forms on `full_headless`; the other five differ only by named gated arms). That
//   gate is PERMANENT: an arm that ever collides turns it RED, and the winner is then an OWNER RULING — never a
//   consequence of which `if` a refactor happened to write first.
//
// ★ EACH TRANSPORT KEEPS ITS OWN ENVELOPE, so `fmt` selects the RENDERING and nothing else:
//     · `LineFormat::text` — the USB console. Everything, the peer-book acks included, goes to `stream`;
//       `reply`/`reply_cap` are unused and may be null/0.
//     · `LineFormat::json` — the companion. A single-line ack is written into `reply` and its length returned in
//       `n` (`State::buffered`), exactly as `device_ble.h`'s `g_out` contract requires; only a ROUTER response
//       streams, and it streams through `stream` (the caller's real `LineSink`).
//   ⛔ The seam NEVER selects a sink. It writes to the `stream` it is handed and to the `reply` it is handed; it
//      does not know `mrcon`, `Serial` or BLE-NUS exist. That is [[B279]]'s rule, applied to the whole path.
//
// ⛔ IT OWNS NO COMMAND-NAME SPECIAL CASE. The transport-specific NAMED failure envelopes (`peerkey_err`,
//    `peer_name_err`, the BLE help refusal) stay in their transports, where they have always been: they are
//    ENVELOPE choices, not the router/parser decision, and putting a `strncmp(line, "peerkey ", 8)` in here would
//    put verb knowledge inside the one place that must have none. ⇒ a line neither surface owns comes back
//    `State::unmatched` WITH its `ParseErr`, and each caller renders its own established contract.
//
// Slice 6: `State` preserves local rendering completion; `outcome`/`refuse` are the independent typed
// dispatch result. The required context supplies the validator's bound and remote authority. Local
// authority never consults the policy table; no production remote caller exists yet.
//
// ⓘ `Command::body` BORROWS into `line` (console_parse.h:17-20), so the parse, the peer-book handlers and
//    `Node::on_command` all run INSIDE this call, while the caller's buffer is still alive. Nothing returned from
//    here retains that pointer.
enum class LineFormat : uint8_t { text, json };

struct LineExec {
    enum class State : uint8_t {
        unmatched,   // neither the router nor the parser owned the line — the caller renders its own refusal
        empty,       // the line carried no token (ParseErr::empty); both transports answer with silence
        streamed,    // the console router answered, through `stream`
        buffered     // a parser-owned result was rendered into `reply`; `n` bytes
    };
    State                        state     = State::unmatched;
    DispatchOutcome              outcome   = DispatchOutcome::unmatched;
    RefuseReason                 refuse    = RefuseReason::none;
    bool                         action_busy = false; // typed conflict, never inferred from output text
    meshroute::console::LineErr   line_err  = meshroute::console::LineErr::ok;
    size_t                       n         = 0;                                  // valid only on `buffered`
    meshroute::console::ParseErr parse_err = meshroute::console::ParseErr::ok;   // the parser's verdict, for the
                                                                                 // caller's `unmatched` envelope
};
LineExec exec_console_line(const char* line, size_t len, LineFormat fmt, Print& stream,
                           char* reply, size_t reply_cap, const CommandContext& ctx);

// ★★ UI-7 — THE ONE FIRMWARE SURFACE THIS PLAN ADDS (owner-approved 2026-08-01). Parse ONE command line and execute
// it on the node, returning the TYPED result. No text output at all: the caller wants the `CmdResult`, not a human
// string — and the board UI is the third consumer of this sequence and the FIRST that needs the result
// programmatically. Scraping a `BufferSink` was explicitly rejected (spec §2.1): a safety behaviour must not hang off
// a formatting detail, and a discarded sink leaves a refused send on `SENDING...` for ever.
// ⚠ IT IS NOT A WRAPPER AROUND `dispatch()`. `dispatch` is a console VERB ROUTER and returns `false` for `send` /
//   `send_channel`; the send path lives ABOVE it — until 0c in the two CALLERS, which open-coded `parse_command` +
//   `Node::on_command`, and since 0c in `exec_console_line()` above. Routing a UI send through `dispatch` would
//   still return false and send NOTHING.
// ⛔ V1 CORRECTION (§RADMIN-0c / [[B298]], 2026-09-05) — THE OLD CLAIM IS KEPT VISIBLE AND IS NOW FALSE. This
//   paragraph read: *"The two existing call sites are DELIBERATELY NOT retrofitted onto this helper. They use
//   OPPOSITE orderings relative to `dispatch()`, so unifying them is a behaviour change on two working transports
//   and needs its own slice and gate (C1). This is purely ADDITIVE: nothing that works today changes."*
//   ⇒ **Slice 0c WAS that slice and that gate.** The opposite orderings are gone: `service_console` and
//   `ble_dispatch_line` now each make exactly ONE call to `exec_console_line()` above, which owns the
//   router-versus-parser fork once, in the measured order (the router/parser intersection is EMPTY on all six real
//   profiles — `tools/probe_console_sink/ownership.py`, pinned).
//   ⓘ WHAT DID **NOT** CHANGE, and why `exec_command` still exists separately: it is the TYPED, output-free
//     executor the board UI needs, and it runs `Node::on_command` for EVERY kind — including `peerkey`/`peername`,
//     which the console path deliberately routes to `handle_peerkey`/`handle_peername` (RAM install + /mrpeers
//     mirror + the contract ack). Those are two different contracts, so folding one into the other would be a
//     behaviour change, not a de-duplication. Its two `firmware_ui.cpp` call sites are untouched by 0c.
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
// ⓘ [[B255]]: gated with the forward declaration above — the ruling and its reasons are recorded there.
#if MR_FEAT_OLED
PresetCatalog& preset_catalog();
// setup(): load `/mrui` through the four-state read and print the ruled diagnostic line for the two fault states
// (`ok`/`absent` print NOTHING). ⛔ ZERO writes on every arm. Same shape and same reason as peer_store_restore().
void preset_boot_restore_console();
// `ui preset list|set|clear|reset` — dispatched from BOTH transports through the ONE `dispatch(line,len,Print&)`.
void handle_ui(const char* args, size_t len, Print& out);
#else
// ★★ [[B255]] — THE ONE STUBBED ARM, AND IT IS STUBBED FOR A STRUCTURAL REASON RATHER THAN FOR SYMMETRY.
// `preset_boot_restore_console()` is the only one of the three called from `fw_main.cpp`, which is board/runtime
// glue: `lib/hal/mr_ui.h`'s standing rule is that `fw_main` must never learn a panel exists, and
// `tools/probe_board_ui/run.sh` W24 enforces it file-wide as an executable check. ⇒ the gate is HERE, in the
// feature's own header, exactly as `ui_emergency_active()`'s is a few lines up — ⛔ never an `#if` at the call site.
// ⓘ An empty inline body costs NOTHING: the call inlines away, no catalog is constructed, `/mrui` is never read and
//   no line is printed. `preset_catalog()` and `handle_ui()` need no stub — their only callers (the OLED TU and the
//   `dispatch()` arm in `firmware_commands.cpp`) are compiled out with them.
inline void preset_boot_restore_console() {}
#endif   // MR_FEAT_OLED

// ★★ §RADMIN slice 3 — the two TARGET STORES' console surface is `MR_FEAT_RADMIN_ACCEPT`-gated (R-RA-8: accept
// = the static + gateway products). ⛔ THE PURE UNITS STAY UNGATED (`src/firmware_admin_identity.h`,
// `firmware_admin_acl.h`, `firmware_admin_verbs.h`, and the two records in `device_nv.h`): the native suite
// compiles all of them unchanged, which is the [[B255]] idiom. Only the INSTANTIATION and the console surfacing
// are compiled out.
// ⛔ AND THERE IS **NO** `#else` STUB HERE, unlike `preset_boot_restore_console()` above — deliberately: that one
//    is stubbed because `fw_main.cpp` must never learn a panel exists (`lib/hal/mr_ui.h`'s rule, enforced by
//    `tools/probe_board_ui/run.sh` W24). No such rule applies to an NV store, so the CLIENT arm carries ⛔ no
//    symbol, ⛔ no call and ⛔ no inert stub at all: `fw_main.cpp`'s boot call is itself ACCEPT-gated, which is
//    what makes the whole family verifiably ABSENT from a mobile object rather than merely inert in it.
#if MR_FEAT_RADMIN_ACCEPT
// setup(): validate `/mradmid` and `/mracl` through their four-state reads and print the two ruled boot lines.
// ⛔ ZERO writes, ⛔ zero entropy draws, ⛔ no auto-generation, ⛔ no key or fingerprint byte, ⛔ nothing installed.
void admin_stores_boot_report_console();
#endif   // MR_FEAT_RADMIN_ACCEPT

// ★★ §RADMIN slice 4 — the two CONTROLLER STORES' console surface is `MR_FEAT_RADMIN_CLIENT`-gated (R-RA-8's other
// half: client = the four mobile products). ⛔ THE PURE UNITS STAY UNGATED (`src/firmware_admin_keyring.h`,
// `firmware_admin_targets.h`, `firmware_admin_client_verbs.h`, and the two records in `device_nv.h`) — the
// [[B255]] idiom, exactly as for the ACCEPT half above. Only the INSTANTIATION and the console surfacing are
// compiled out, and there is ⛔ NO `#else` STUB for the same reason the ACCEPT arm has none.
#if MR_FEAT_RADMIN_CLIENT
// setup(): validate `/mrmkeys` and `/mrtargets` through their four-state reads and print the two ruled boot lines.
// ⛔ ZERO writes, ⛔ zero entropy draws, ⛔ no auto-generation, ⛔ no key, seed or fingerprint byte.
void admin_client_stores_boot_report_console();
#endif   // MR_FEAT_RADMIN_CLIENT

struct ExecResult {
    bool                         ok        = false;                                  // false => validation or parsing refused
    meshroute::console::LineErr  line_err  = meshroute::console::LineErr::ok;
    meshroute::console::ParseErr parse_err = meshroute::console::ParseErr::ok;
    meshroute::CmdResult         result{};                                           // valid only when `ok`
};
ExecResult exec_command(const char* line, size_t len);

}  // namespace mrfw
