// MeshRoute — fw_main.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Firmware entry point for board builds (xiao_sx1262, heltec_v3). Constructs the SX1262 PHY
// (vendored CustomSX1262), wraps it in the device Sx1262Radio (IRadio), builds the device
// meshroute::Hal (DeviceHal) + a meshroute::Node on top, and PUMPS the protocol loop:
//   RX     : Sx1262Radio.poll_rx -> Node::on_recv ; preamble -> Node::on_preamble_detected
//   timers : DeviceHal.pop_due_timer -> Node::on_timer  (beacons / RTS-timeouts / ACK-waits / retries)
//   app    : Node::next_push -> USB console ; USB line -> parse_command -> Node::on_command
//
// REALITY SPLIT: I compile-verify this under both board envs; the on-metal flash + the 2-device
// beacon/DM exchange is the user's. The MeshRoute-owned HAL logic (timer wheel, device_hal facade)
// is already native-proven against a FakeClock + MockRadio (test_timer_wheel / test_device_hal).

#include <Arduino.h>
#include <RadioLib.h>

#include "helpers/radiolib/CustomSX1262.h"   // vendored from MeshCore — DO NOT EDIT
#include "protocol_constants.h"
#include "iclock.h"
#include "device_radio.h"
#include "device_hal.h"
#include "frame_trace.h"      // mr_trace_frame() — decoded one-line RX/TX console trace
#include "node.h"
#include "node_role.h"        // ★ §role-model/B28 R2: role_enforce — the boot normalisation of `team_id != 0` ⇒ `is_mobile`
#include "leaf_config.h"      // §5: duty_to_bp/bp_to_duty — quantize duty to the C-frame wire step (hash parity)
#include "identity.h"
#include "command.h"
#include "console_parse.h"
#include "device_nv.h"
#include "device_inbox_store.h"
#include "fixed_inbox_store.h"   // the interim VOLATILE RAM inbox (until the durable QSPI records backend lands)
#include "device_rng.h"
#include "console_json.h"    // write_ack/write_push/write_ready/write_err — the BLE companion's JSON twin
#include "device_ble.h"      // BLE companion transport (XIAO nRF52840; an inert no-op on ESP32/native)
#include "device_ota.h"      // WiFi OTA (Heltec ESP32-S3); inert no-op on XIAO/native
#include "mr_ui.h"           // §featuresplit slice 4: board-UI hooks (real on MR_FEAT_OLED boards, inline no-ops elsewhere)
#include "dispatch_sink.h"   // §command-sink-consolidation: BufferSink (remote/rcmd capture) + LineSink (BLE streaming)
#include "firmware_config_parse.h"   // §cleanup 2026-07-14: pure config/provisioning parse primitives (native-tested)
#include "fw_context.h"              // §cleanup 2026-07-14: extern decls of the shared device-stack/runtime globals defined below (static→extern seam)
using mrfw::parse_sf_list;   // keep call sites unchanged (extracted verbatim from this file)
using mrfw::kv_next;
using mrfw::team_fnv1a32;
#include "firmware_remote.h"         // §cleanup 2026-07-14: remote-mgmt cluster moved out; REMOTE_FLAG_SEALED (used by mesh_service) lives here
#include "firmware_config.h"         // §cleanup 2026-07-14: config/provisioning cluster
#include "firmware_inbox.h"          // §cleanup 2026-07-14: inbox/companion-sync cluster (pull_inbox / mark_read)
using mrfw::handle_pull_inbox;       // dispatch + ble_dispatch_line verbs; call sites unchanged
using mrfw::handle_mark_read;
using mrfw::handle_del_msg;           // §3.5 durable single-record delete
#include "firmware_commands.h"       // §cleanup 2026-07-15: console command cluster (dispatch + diagnostics) — moved in batches
using mrfw::handle_peerkey;          // §3 export; call sites (service_console + ble_dispatch_line) unchanged
using mrfw::handle_peername;         // §AB2 export; same two call sites, same shape as handle_peerkey
using mrfw::dispatch;                 // §3 export: the console verb-router (service_console + ble_dispatch_line)
using mrfw::print_banner;             // §3 export: setup() banner + `version`
using mrfw::print_identity;           // §3 export: setup()
using mrfw::print_sf_list;            // §3 export: setup() + mesh_service_once()
using mrfw::board_name;               // §3 export: ble_dispatch_line `version`
using mrfw::handle_routes;            // §3 export: ble_dispatch_line `routes`
using mrfw::handle_peers;             // §3 export: ble_dispatch_line `peers` (§AB3, the bounded JSON address book)
using mrfw::print_reqpubkey_hint;     // §3 export: §id-hash S1 — the refused-`reqpubkey` remedy line (service_console)
using mrfw::make_status_fields;       // §3 export: ble_dispatch_line `status`
using mrfw::node_state_str;           // §3 export: ble_dispatch_line `status`
using mrfw::make_cfg_extras;          // §3 export: ble_dispatch_line `cfg`
using mrfw::remote_exec;             // keep call sites unchanged (mesh_service_once + dispatch)
using mrfw::handle_rcmd;
using mrfw::handle_cfg_set;          // dispatch verbs (moved to firmware_config); call sites unchanged
using mrfw::handle_gateway;
using mrfw::nv_load_stamped;         // §nv-ritual: the shared /mrcfg load-or-seed/stamp prologue (persist_cfg_if_needed)
#if MR_N_LAYERS < 2
using mrfw::handle_join;
using mrfw::handle_create;
using mrfw::handle_team;
#if MR_FEAT_MOBILE
using mrfw::handle_mobile;
#endif
#endif
using mrfw::handle_leave;
#if MR_FEAT_REMOTE_MGMT
using mrfw::handle_password;
#endif
#if MR_FEAT_REMOTE_MGMT
using mrfw::handle_unlock;
using mrfw::handle_lock;
#endif
#if MR_FEAT_REMOTE_MGMT
#include "admin_auth.h"      // §remote-mgmt: password KDF + sealed-command seal/open/verify
#include "console_binary.h"  // §remote-mgmt: the binary TLV response encoders (enc_status/enc_routes/…)
#endif
#include "fault_log.h"       // persistent fault log — platform-neutral ring/decode/formatters (lib/core)
#include "device_fault.h"    // nRF52 HW glue: retained scratch + 8 s watchdog + HardFault capture (empty on ESP32)
#include "sched_send.h"      // firmware scheduled-send CORE (on-node test workload; pure logic, host-unit-tested)
#include "console_sink.h"    // `mrcon` — the ONE guarded console-output sink (drop-never-block; MR_CONSOLE compile-out)
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifndef GIT_REV
#define GIT_REV "nogit"      // tools/git_rev.py injects -DGIT_REV at build; this fallback keeps every env compiling
#endif

// Persistent fault log (spec 2026-06-24). The boot-capture loads/records/persists into g_fault_log on BOTH HW platforms
// (MRFAULT_HW = nRF52 [.noinit + WDT + HardFault] OR ESP32 [RTC scratch + esp_task_wdt + esp_reset_reason]). On a
// native/unknown build the calls are #if MRFAULT_HW-guarded out (and fw_main isn't compiled there anyway).
mrfault::FaultLog    g_fault_log;                // extern in fw_context.h
mrfault::FaultRecord g_last_reset{};
bool                 g_last_reset_valid = false;

// `prep-restart`: when true the loop SKIPS the operating block (RX/timers/tx/beacon/sleep) — the node is intentionally
// DORMANT — but still feeds the WDT (not a hang) + services the console. RAM only, so a power-cycle clears it.
bool                 g_halted = false;           // extern in fw_context.h

// `rcmd` deferred recovery action: respond FIRST, then act ~3 s later so the response DM airs. 0=none, 1=reboot, 2=prep-restart.
uint8_t              g_remote_action = 0;        // extern in fw_context.h
uint64_t             g_remote_action_at = 0;

// firmware scheduled-send (testsend/testch): the on-node test workload. RAM-only (transient); the loop tick fires
// due entries through the real send path (queue-gated). Lost on reboot — acceptable (the durable inbox tells the story).
mrsched::Schedule    g_sched;                    // extern in fw_context.h

// ---- Radio-Module corruption canary (debug instrument, spec 2026-06-25; MR_RADIO_CANARY, default OFF) ------------
// Where (which loop subsystem) the Module-corruption was first SEEN. The id is stored in the durable canary record;
// the live message prints the name. Keep in sync with the canary() calls scattered through loop().
enum CanaryWhere : uint8_t { CW_loop_top = 0, CW_poll_rx = 1, CW_tx_done = 2, CW_node_tick = 3,
                             CW_console = 4, CW_ble = 5, CW_nv = 6, CW_sched = 7, CW_noise = 8 };
// canary_where_name() + canary() are defined just before loop() — they use g_iradio/mrcon/mrfault (declared later).

// Step 4 light-sleep — platform sleep primitives (radio stays in continuous RX; DIO1 RxDone wakes the MCU).
#if !defined(MR_NO_POWERSAVE)
  #ifndef MR_MAX_SLEEP_MS
    #define MR_MAX_SLEEP_MS 1000u         // cap an idle sleep so the console + periodic work stay responsive (tunable)
  #endif
  // Sleep policy (see loop()): a HEADLESS node light-sleeps when idle; the moment a host is detected (any
  // console byte) the board latches AWAKE so the serial console stays usable; an explicit `sleep` command
  // forces it back to sleep. WHY: ESP32 light-sleep gates the UART clock, so the console is unreachable while
  // asleep and a typed byte can't even wake it (UART-wake proved unreliable on the Heltec) — so we must NOT
  // sleep while a host is present. Mirrors MeshCore, whose CLI firmware never sleeps (only headless repeaters
  // do). MR_BOOT_GRACE_MS keeps us awake right after boot so the host's first byte is caught (a sleeping board
  // would miss it); connecting a monitor resets the board over DTR, so "a host connects" == "a fresh boot".
  #ifndef MR_BOOT_GRACE_MS
    #define MR_BOOT_GRACE_MS 30000u       // 30 s (tunable) — stay awake this long after boot to catch the host's first byte
  #endif
  #if defined(NRF52_SERIES) || defined(ARDUINO_ARCH_NRF52) || defined(BOARD_XIAO_WIO_SX1262)
    #include <nrf_soc.h>                  // sd_softdevice_is_enabled / sd_app_evt_wait
  #elif defined(ARDUINO_ARCH_ESP32) || defined(ESP32) || defined(BOARD_HELTEC_V3)
    #include <esp_sleep.h>
    #include <esp_ota_ops.h>             // esp_ota_mark_app_valid_cancel_rollback
    #include <driver/rtc_io.h>           // rtc_gpio_is_valid_gpio
  #endif
#endif

namespace P = meshroute::protocol;

// ---- node identity. key_hash32 = ed_pub[:4], DERIVED from a 32-byte master seed persisted in `/mrid`
//      (HW-RNG on first boot; `regen` to rotate). node_id is the disposable short address (NV `cfg set
//      node_id` / join); 0 = unprovisioned (do_send refused). The seed/keys live in g_identity, set in setup().

// LoRa sync word — the PHY-level filter that keeps alien same-freq/SF/BW traffic out: the SX1262 only
// raises RxDone for frames carrying THIS word. Distinct from MeshCore (0x12 PRIVATE, what std_init sets),
// Meshtastic (0x2B) and LoRaWAN (0x34), so their frames are dropped in the radio before any MAC parse.
static constexpr uint8_t MESHROUTE_SYNC_WORD = 0x4D;   // 'M'

// ---- the device stack (global ctor order = declaration order; refs bind to already-built objects) ----
Module                  g_mod(LORA_PIN_NSS, LORA_PIN_DIO1, LORA_PIN_RST, LORA_PIN_BUSY);   // all extern in fw_context.h
CustomSX1262            g_radio(&g_mod);
meshroute::ArduinoClock g_clock;
meshroute::Sx1262Radio  g_iradio(g_radio);
meshroute::DeviceHal    g_hal(g_clock, g_iradio);
meshroute::Node         g_node(g_hal, /*node_id=*/0, /*key_hash32=*/0, "node");   // identity set in setup() from /mrid
// Inbox stores. On nRF52 (QSPIFLASH=1 -> MRINBOX_QSPI_READY) the durable QSPI/LittleFS DeviceInboxStore records
// backend IS the live inbox (its on-metal begin()/flash behaviour is USER-BENCH-VERIFY-PENDING; if begin() fails the
// inbox goes disabled, not to RAM). On ESP32 the records backend is a later slice, so we install the interim volatile
// FixedInboxStore RAM ring (bounded; record-on-delivery + pull_inbox WORK, session-scoped, lost on reboot — the
// per-boot epoch set in setup makes the app re-pull). The durable-vs-RAM choice below tracks MRINBOX_QSPI_READY.
// MR_RAM_INBOX_SLOTS + the guard now live in fw_context.h (so the extern decls match); definitions below (extern in fw_context.h).
#if defined(MRINBOX_QSPI_READY)
mrinbox::DeviceInboxStore g_inbox_dm("/dm", "/mri_dm", meshroute::protocol::inbox_dm_store_bytes,   mrinbox::kSegScratchBytes);
mrinbox::DeviceInboxStore g_inbox_ch("/ch", "/mri_ch", meshroute::protocol::inbox_chan_store_bytes, mrinbox::kSegScratchBytes);
#else
meshroute::FixedInboxStore<MR_RAM_INBOX_SLOTS> g_inbox_dm;
meshroute::FixedInboxStore<MR_RAM_INBOX_SLOTS> g_inbox_ch;
#endif
meshroute::Identity     g_identity{};                                            // seed -> Ed25519/X25519 + key_hash32; extern in fw_context.h

uint8_t  g_rxbuf[P::max_payload_bytes_hard_cap + 32];   // block below all extern in fw_context.h
bool     g_radio_ok = false;   // SX1262 std_init result — surfaced in the heartbeat below
uint32_t g_rx_count = 0;       // frames received (status diagnostic)
uint32_t g_sleep_count = 0;    // idle light-sleep entries (status `slept=`); climbs = the gate fires, stuck = never sleeps
bool     g_host_present = false; // a console byte was seen this boot -> a human is here -> stay awake (MeshCore inhibit_sleep)
bool     g_force_sleep  = false; // the `sleep` console command -> light-sleep when idle even with a host present
double   g_freq_mhz = LORA_FREQ;   // live operating freq (compile default; Slice-2 NV will override at boot)
int8_t   g_tx_power = LORA_TX_POWER;   // live TX power (dBm); NV `cfg set tx_power` overrides at boot
// BLE companion policy (NV v7; read at boot, reboot-to-apply). Compile defaults = the documented bare-metal
// node: off / 15-min periodic window / PIN 123456 (spec §4 + §A.3). A v7 blob overrides these at boot.
uint8_t  g_ble_mode = 0;            // 0=off (bare-metal), 1=on, 2=periodic — all extern in fw_context.h
uint8_t  g_ble_period_min = 15;     // periodic-mode advertising period (minutes)
uint32_t g_ble_pin = 123456;        // 6-digit pairing passkey
// Node location (deployment metadata, persisted in the /mrid record alongside name). Degrees × 1e7;
// (0,0) = unset. A FIXED node is set once (`cfg set lat`/`lon` or the app); a mobile node is fed by its phone.
int32_t  g_lat_e7 = 0;             // all extern in fw_context.h
int32_t  g_lon_e7 = 0;
uint8_t  g_persist_id = 0, g_persist_epoch = 0, g_persist_join = 0;   // last DAD lease state written to NV (change-detect)
uint8_t  g_persist_team_local_id = 0;   // §mobile 6.4: last team-DAD id written to NV (change-detect -> persist across a power-cycle)
// InternalFS self-heal Part 3 (2026-06-24): the channel-ctr is no longer written every send — instead /mrcfg holds a
// LEASE (the live ctr + margin). g_ctr_lease = that persisted leased value; a write fires only when the live ctr
// catches it (every ~margin sends), and a reboot resumes FROM the lease (≥ any id used pre-crash -> no v15 id-reuse).
uint16_t g_ctr_lease = 0;          // extern in fw_context.h
// ids reserved per /mrcfg write. MUST exceed the max channel originations one loop() can drain before the post-drain
// re-lease (persist_cfg_if_needed runs at the loop tail, AFTER service_console + mrble::service_rx). That count is
// RX-buffer-capped: USB-CDC + BLE-NUS buffers (≤ ~1 KB total) / a `send_channel …` line (≈18 B) -> a few dozen, so
// 256 is a >5x margin (and a reboot mid-burst resumes from the lease ≥ any id minted -> no v15 id-reuse). Also bounds
// the /mrcfg write rate to 1 per `margin` channel sends.
static constexpr uint16_t kChannelCtrLeaseMargin = 256;
bool     g_fs_reformatted = false;   // Part 2: mount_or_repair() reformatted a corrupt InternalFS this boot (surfaced in status; RAM); extern in fw_context.h

// device-console diagnostics (handle_route_cmd/dump_routes/print_sf_list/dump_cfg/board_name/print_banner) moved to firmware_commands.{h,cpp} (cleanup 2026-07-15); §3 exports via `using mrfw::…` above.

void fw_wdt_feed() { mrfault::fault_wdt_feed(); }   // extern in fw_context.h — the WDT kick exposed for firmware_remote (device_fault.h's ISR vectors can't be pulled into a 2nd TU)

// §cleanup 2026-07-15: firmware_commands seam wrappers (fw_context.h). The moved firmware_commands reaches these
// STAY-set board-glue fns through here — do_reboot/do_ota/dump_faults/handle_crashtest carry the device_fault.h
// ISR-vector + MRFAULT_HW/MRFAULT_ESP32 MACRO trap; handle_prep_restart writes the loop's g_halted latch. Forward-
// declared here (defined below); the wrappers themselves are the ONLY cross-TU entry points.
static void do_reboot();
static void do_ota();
static void dump_faults(Print& out);
static void handle_crashtest(const char* args, Print& out);
static void handle_prep_restart(Print& out);
void fw_reboot()                                { do_reboot(); }
void fw_ota()                                   { do_ota(); }
void fw_faults_dump(Print& out)                 { dump_faults(out); }
void fw_crashtest(const char* args, Print& out) { handle_crashtest(args, out); }
void fw_prep_restart(Print& out)                { handle_prep_restart(out); }

// ADDENDUM 4 (2026-06-25) instrument — the FreeRTOS loop-task stack high-water mark: the SMALLEST number of free bytes
// the loop task has ever had. The fleet-wide jump-to-0x0 was THIS 4 KB stack silently overflowing in do_post_ack into
// the adjacent heap radio HAL. A healthy margin (hundreds of bytes) confirms the frame-shrink held; a near-zero value
// means escalate to a dedicated bigger-stack task. uxTaskGetStackHighWaterMark returns words. nRF52 only (the cramped
// platform; ESP32's loopTask is large, native has no task) -> 0 elsewhere. Read it over-the-air via `rcmd <id> status`.
uint32_t loop_stack_free_bytes() {   // extern in fw_context.h (shared with firmware_remote's status TLV)
#if defined(NRF52_PLATFORM) || defined(ARDUINO_ARCH_NRF52)
    return (uint32_t)uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t);
#else
    return 0;
#endif
}

// dump_status/dump_duty/print_identity/do_regen moved to firmware_commands.{h,cpp} (cleanup 2026-07-15).

// `cfg set <key> <value>` — ACCUMULATES onto the pending NV blob (so several sets + ONE reboot works), then
// applies LIVE to the running node where possible. RADIO knobs (freq/routing_sf|control_sf/bw/cr/tx_power) +
// MAC knobs (sf_list/lbt/beacon_ms) take effect NOW; node_id + duty need a reboot (identity / on_init budget).
// Extra protocol knobs (nav/nav_ignore/hop_cap/leaf_id/gateway) apply live but are NOT persisted yet (reboot reverts).
// handle_cfg_set moved to firmware_config.{h,cpp} (cleanup 2026-07-14, Increment A); `using mrfw::handle_cfg_set` (top).

static void do_reboot() {
    mrcon.println(F("> rebooting")); mrcon.flush(); delay(100);
#if defined(MRFAULT_HW)
    mrfault::mark_expected_reset();   // v2 fault log: classify the upcoming reset as REBOOT, not UNEXPECTED
#endif
#if defined(NRF52_SERIES) || defined(ARDUINO_ARCH_NRF52) || defined(BOARD_XIAO_WIO_SX1262)
    NVIC_SystemReset();
#elif defined(ARDUINO_ARCH_ESP32) || defined(ESP32) || defined(BOARD_HELTEC_V3)
    ESP.restart();
#endif
}

// handle_factory_reset moved to firmware_commands.{h,cpp} (cleanup 2026-07-15; terminal reset via fw_reboot()).

// `ota` — platform-native firmware update. XIAO: BLE DFU; Heltec: WiFi SoftAP + web upload.
static void do_ota() {
#if defined(NRF52_SERIES) || defined(ARDUINO_ARCH_NRF52) || defined(BOARD_XIAO_WIO_SX1262)
    mrcon.println(F("> OTA: rebooting into BLE DFU now — this USB console will drop here."));
    mrcon.println(F(">      Push firmware.zip via the Nordic DFU app (enable its auto-reboot). Double-tap RESET to abort."));
    mrcon.flush(); delay(500);
    mrfault::mark_expected_reset();   // v2 fault log: the OTA reset is a REBOOT, not UNEXPECTED
    NRF_POWER->GPREGRET = 0xA8;   // DFU_MAGIC_OTA_RESET
    NVIC_SystemReset();
#elif defined(ARDUINO_ARCH_ESP32) || defined(ESP32) || defined(BOARD_HELTEC_V3)
    if (mrota::ota_active()) {
        mrota::ota_stop();
        mrcon.println(F("> OTA: stopped"));
    } else {
        if (mrota::ota_start()) {
            mrota::set_pre_reboot_hook([] { mrfault::mark_expected_reset(); });   // v2 fault log: a WiFi-OTA reboot is a REBOOT, not UNEXPECTED
            mrcon.println(F("> OTA: browse to the IP above, upload firmware.bin — node reboots on success"));
        } else
            mrcon.println(F("> OTA: start FAILED"));
    }
#endif
}

// handle_sleep/handle_debug/handle_lookup/handle_nameof/handle_hashof/handle_whoami/hl/dump_help moved to firmware_commands.{h,cpp} (cleanup 2026-07-15).

// ---- Phase-3 inbox sync (schema: ios-companion/INBOX_SYNC_CONTRACT.md) -----------------------------------
// `pull_inbox <dm_since> <chan_since>` streams the inbox (DM block then channel block, oldest-first) + an
// inbox_end terminator; `mark_read <dm|chan> <seq>` advances the per-store read cursor. Both stream NDJSON to a
// transport SINK (USB Serial OR the BLE NUS), so one handler serves both consoles. The companion link is JSON;
// on USB it's structured output for the host harness.
// §command-sink-consolidation: the JSON handlers take `Print& out` (mrcon on USB, a LineSink over BLE). ble_sink is the
// LineSink flush callback — ship one NDJSON line over BLE NUS. (usb_sink is gone: USB callers pass the global `mrcon`.)
static void ble_sink(const char* s, size_t n) { mrble::tx_line(s, n); }   // inert off-XIAO / when no client

char s_inbox_jb[1700];   // shared NDJSON line scratch: pulled inbox records AND live-push lines (loop()) — sequential, single-threaded, never concurrent (241-B body 6x-escaped + envelope); extern in fw_context.h

// dump_limits moved to firmware_commands.{h,cpp} (cleanup 2026-07-15).

// inbox_pull_cb / handle_pull_inbox / handle_mark_read (+ PullCtx) moved to firmware_inbox.{h,cpp} (cleanup 2026-07-14); `using mrfw::handle_pull_inbox/handle_mark_read` above. (ble_sink + s_inbox_jb stay — shared by routes/status/cfg/live-push.)

// Handle a debug/diagnostic console line (help/routes/cfg/status/cfg set/reboot/sleep/debug). Returns true if consumed.
// `faults` — dump the /mrfault ring newest-first + a one-line summary. nRF52 only; ESP32 = unsupported.
static void dump_faults(Print& out) {
#if defined(MRFAULT_HW)
    char buf[160];
    for (uint16_t i = 0; i < g_fault_log.count; ++i) {
        const mrfault::FaultRecord* r = mrfault::fault_log_at(g_fault_log, i);
        if (!r) break;
        mrfault::format_fault_record(*r, buf, sizeof buf);
        out.print(F("[fault] ")); out.println(buf);
    }
    mrfault::format_fault_summary(g_fault_log, buf, sizeof buf);
    out.println(buf);
#else
    out.println(F("unsupported on this build (no HW fault backend)"));
#endif
}

// `crashtest <hang|fault|reboot>` — deliberate fault injection to exercise the WDT / HardFault / reset paths on
// metal. Gated behind `debug on` (ALWAYS compiled, active only after `debug on` — so the bench exercises the real
// deployable image, not a separate crashtest build). spec 2026-06-24 §9.
static void handle_crashtest(const char* args, Print& out) {
    if (!meshroute::g_mr_trace_on) { out.println(F("> crashtest err (enable `debug on` first — gated to avoid an accidental crash)")); return; }
    while (*args == ' ') ++args;
    if (!strncmp(args, "hang", 4)) {
        out.println(F("> crashtest hang — spinning; the watchdog should reset in ~8 s")); out.flush();
        for (;;) { /* no WDT feed -> DOG reset (nRF52); on a no-WDT build this hangs until power-cycle) */ }
    } else if (!strncmp(args, "fault", 5)) {
        out.println(F("> crashtest fault — forcing a crash")); out.flush();
#if defined(NRF52_PLATFORM)
        volatile uint32_t* p = reinterpret_cast<volatile uint32_t*>(0xFFFFFFF0u); (void)*p;   // bad-address read -> BusFault -> HardFault capture
        __asm volatile("udf #0");                                                              // belt+braces: undefined instruction
#elif defined(MRFAULT_ESP32)
        abort();                                                                               // -> the IDF panic handler -> reboot, ESP_RST_PANIC (recorded as PANIC)
#else
        out.println(F("> (no HW fault path on this build)"));
#endif
    } else if (!strncmp(args, "reboot", 6)) {
        out.println(F("> crashtest reboot — NVIC_SystemReset (SREQ)")); out.flush();
        do_reboot();
    } else {
        out.println(F("> crashtest err usage: crashtest <hang|fault|reboot>"));
    }
}

// `prep-restart` (middle-tier reset): drop the learned state (routes/channel/liveness/pending/dedup) + the inbox
// records, KEEP the provisioning (node_id/layer/sf_list/lineage + identity), then go DORMANT (no reboot).
// Run on every node -> the net falls silent (no stale beacons to cross-poison) -> power-cycle the whole fleet ->
// everyone converges from true zero. spec 2026-06-24.
static void handle_prep_restart(Print& out) {
    g_node.clear_learned_state();                 // routes + channel buffer + liveness + pending + dedup -> empty (KEEPS _cfg + identity + join)
    g_inbox_dm.wipe(); g_inbox_ch.wipe();         // QSPI inbox RECORDS (no-op on the RAM/ESP32 store); the boot epoch bumps -> companion re-syncs
    g_halted = true;                              // the loop now skips the operating block (dormant) but stays console-responsive
    out.println(F("> prep-restart — routes + inbox cleared, network membership KEPT, node HALTED. Power-cycle the fleet to restart clean."));
}

// OTA remote diagnostics — execute a whitelisted query for `from` and DM the response back. Reads build a compact
// one-DM body (≤ inbox_max_body, truncated with "…"); the two recovery WRITES respond FIRST then DEFER the action
// ~3 s (so the response actually airs). Anything else -> `err: <q> not allowed`. spec 2026-06-24.
// §remote-mgmt (cleanup 2026-07-14): REMOTE_FLAG_SEALED + remote_encode / remote_verb_open / remote_seal_resp /
// remote_exec (+ the inert stub) moved to firmware_remote.{h,cpp}. REMOTE_FLAG_SEALED now lives in firmware_remote.h
// (shared with mesh_service_once below); `using mrfw::remote_exec` (top) keeps the mesh_service call site unchanged.

#if MR_FEAT_REMOTE_MGMT
// §remote-mgmt admin-ISSUE side (operator device): `unlock <pw>` derives the admin key into RAM; a gated `rcmd` then
// seals the command to the target. Transient — wiped on `lock`/reboot (the credential lives in the operator's head).
meshroute::Identity g_admin_id{};          // all extern in fw_context.h
bool     g_admin_unlocked = false;
uint32_t g_admin_tx_ctr   = 0;      // monotonic command counter (bumped past a target's reject-hint floor)
// handle_unlock / handle_lock / admin_verb_gated moved to firmware_remote.{h,cpp} (cleanup 2026-07-14). g_admin_*
// STAY defined here — mesh_service_once opens sealed replies with them (shared, not cluster-private); the
// `using mrfw::handle_unlock/handle_lock` decls (top) keep the dispatch call sites unchanged.
#endif

// handle_rcmd (`rcmd <dst> <verb>` origin) moved to firmware_remote.{h,cpp} (cleanup 2026-07-14); `using mrfw::handle_rcmd` (top).

// handle_testsched/handle_teststatus/dispatch/read_batt_mv/make_status_fields/node_state_str/handle_routes/make_cfg_extras moved to firmware_commands.{h,cpp} (cleanup 2026-07-15; dispatch reaches board-glue via fw_* wrappers). §3 exports via `using mrfw::…` above.

// E2E §2 + §AB1: the /mrpeers PEER ADDRESS BOOK — mirror a live peer's key + NAME + CONFIDENCE into NV (whole-blob
// rewrite; update-in-place, append, or evict the oldest non-pinned), and re-install the book at boot. Best-effort —
// a store full of pinned keys / a no-NV target just means the key won't survive a reboot (the RAM key still works).
// Both halves (peer_store_sync — was persist_pinned_peer — and peer_store_restore) plus handle_peerkey live in
// firmware_commands.{h,cpp} (cleanup 2026-07-15; §AB1 for the restore); `using mrfw::handle_peerkey` above.

// BLE companion inbound: handle ONE console line, emitting a single NDJSON response (the schema of
// docs/specs/2026-05-30-device-console-design.md §4). Reuses the USB command engine — parse_command +
// g_node.on_command — so the wire grammar physically cannot drift between the USB and BLE transports.
// `whoami` -> a `ready` identity object; an unparseable/unknown line -> a fail-loud `err` (never a silent
// drop). The phone-facing link is JSON-only by design (the human plain-text dumps stay on USB). Handed to
// mrble::begin() as the transport's DispatchFn — device_ble.h owns the bytes, fw_main owns the meaning.
static size_t ble_dispatch_line(const char* line, size_t len, char* out, size_t cap) {
    using namespace meshroute::console;
    if (len == 0) return 0;
    if (len == 6 && !strncmp(line, "whoami", 6)) {
        mrnv::IdBlob idb{}; mrnv::load_id(idb);              // the /mrid name (no RAM copy kept; whoami is rare)
        const size_t nl = (idb.name_len <= sizeof idb.name) ? idb.name_len : 0;
        const auto ds = g_node.duty_status();               // duty snapshot in the ready object (app shows it on connect)
        // ready carries the 64-hex pubkey + the duty snapshot -> ~280 B, over the 256-B `out`; stream via the big
        // scratch like status/cfg (return 0 = no buffered single-line ack).
        meshroute::console::MobileReadyFields mob{};   // §S1: mobile/team snapshot (all omit-when-inactive -> a static node's ready is byte-identical)
        const meshroute::NodeConfig& rc = g_node.config();
        mob.is_mobile  = rc.is_mobile;
        mob.registered = g_node.mobile_attached();   // ★ §MH-S4 §4.1/§7.1: CONFIRMED attachment only — `ready` is app-facing, so it must not report a transmitted-but-unconfirmed CLAIM as registered (the §S0-4 defect). `mobile_registered()` is the LINK-LAYER/provisional flag and is deliberately not used here.
        mob.home       = g_node.mobile_home_id();
        mob.local      = g_node.mobile_local_id();
        mob.home_layer = g_node.mobile_home_layer();
        mob.hosting    = g_node.mobile_reg_count();
        mob.team_id    = rc.team_id;
        mob.team_local = g_node.team_local_id();
        mob.team_ch_key = g_node.team_channel_key_present();   // §team-ch-key (T-K1b): the LOCK-STATE BOOLEAN so the app's indicator never calls `team exportkey` just to test for presence. ★ The KEY ITSELF must NEVER ride `ready` — it is unsolicited and fires on every connect.
        const size_t m = write_ready(s_inbox_jb, sizeof s_inbox_jb, g_node.node_id(), g_node.key_hash32(), g_node.config(),
                                     "existing", g_node.inbox().storage_epoch(), g_hal.now(), idb.name, nl,
                                     g_identity.ed_pub, ds.pct, ds.avail_ms, mob);   // §4: export pubkey for the QR `p`; §S1: mobile/team state
        if (m) ble_sink(s_inbox_jb, m);
        return 0;
    }
    if (len == 7 && !strncmp(line, "version", 7)) {         // build/git/board + last reset — on demand, no reset
        return (size_t)snprintf(out, cap,
            "{\"ev\":\"version\",\"fw\":\"v0.1\",\"built\":\"%s\",\"git\":\"%s\",\"board\":\"%s\",\"reset\":\"%s\"}\n",
            __DATE__ " " __TIME__, GIT_REV, board_name(), g_last_reset_valid ? mrfault::fault_cause_str(g_last_reset.cause) : "-");
    }
    if (len == 12 && !strncmp(line, "prep-restart", 12)) {  // clear routes+inbox, keep join, go dormant (companion/harness can issue it)
        handle_prep_restart(mrcon);
        return (size_t)snprintf(out, cap, "{\"ev\":\"prep_restart\",\"halted\":true}\n");
    }
    if ((len == 4 || (len > 4 && line[4] == ' ')) && !strncmp(line, "rcmd", 4)) {   // issue an OTA remote query; the `[rcmd <from>]` reply lands on USB
        handle_rcmd(line + 4, mrcon);
        return (size_t)snprintf(out, cap, "{\"ev\":\"rcmd_sent\"}\n");
    }
    if (len == 4 && !strncmp(line, "duty", 4)) {            // companion polls this for the silent-countdown banner
        const auto ds = g_node.duty_status();
        return write_duty(out, cap, ds.pct, ds.avail_ms, ds.enabled);
    }
    if (len == 6 && !strncmp(line, "limits", 6)) {          // companion anti-spam/headroom screen (BLE-only, no OTA change)
        const auto s = g_node.limits_snapshot();
        meshroute::console::LimitsFields L;
        L.win_ms = s.win_ms; L.win_left_ms = s.win_left_ms; L.n = s.n; L.ch_sf = s.ch_sf;
        L.ch_cap = s.ch_cap; L.ch_used = s.ch_used; L.ch_min_ms = s.ch_min_ms;
        L.ch_next_ms = s.ch_next_ms; L.ch_ceiling = s.ch_ceiling;
        L.dm_min_ms = s.dm_min_ms; L.dm_next_ms = s.dm_next_ms;
        L.duty_ms = s.duty_ms; L.duty_used_ms = s.duty_used_ms;
        return write_limits(out, cap, L);                  // fits the 256-B `out` (13 u32 fields ~185 B)
    }
    // status/cfg/routes stream via the big s_inbox_jb scratch (NOT the 256-B `out`): an enriched status
    // is ~260 B and a gateway's status/cfg (with the layers[] array) reaches ~680 B — both overflow a
    // 256-B buffer. Reusing the 1700-B line scratch costs no extra RAM. return 0 (no buffered single-line ack).
    if (len == 6 && !strncmp(line, "status", 6)) {
        const size_t m = write_status(s_inbox_jb, sizeof s_inbox_jb, g_node.node_id(), g_node.key_hash32(),
                                      g_node.config(), node_state_str(), make_status_fields());
        if (m) ble_sink(s_inbox_jb, m);
        return 0;
    }
    // `cfg set <key> <val>` from the app (e.g. `cfg set lat 52.2297`): apply + persist via the shared handler
    // (its `> cfg ...` lines go to USB; harmless), then reply with the FRESH cfg object so the app's view updates.
    if (len > 8 && !strncmp(line, "cfg set ", 8)) {
        handle_cfg_set(line + 8, mrcon);
        const size_t m = write_cfg(s_inbox_jb, sizeof s_inbox_jb, g_node.config(), make_cfg_extras());
        if (m) ble_sink(s_inbox_jb, m);
        return 0;
    }
    if (len == 3 && !strncmp(line, "cfg", 3)) {
        const size_t m = write_cfg(s_inbox_jb, sizeof s_inbox_jb, g_node.config(), make_cfg_extras());
        if (m) ble_sink(s_inbox_jb, m);
        return 0;
    }
    if (len == 6 && !strncmp(line, "routes", 6)) { LineSink ls(ble_sink); handle_routes(ls); ls.flush(); return 0; }
    // ★ §AB3: the address book over BLE. Plain `peers` streams the BOUNDED book (≤ cap_peer_keys rows). ⚠ `peers all`
    // is REFUSED here, not silently narrowed: §2.6(a) keeps the up-to-256 id-only list console-only because a few
    // hundred rows over this transport is the self-inflicted console-flood wedge already fixed once (mrcon). C2 — the
    // refusal names the remedy (plain `peers`, or the USB console). Intercepted BEFORE the text fallback at the bottom,
    // which would otherwise hand `peers all` straight to dispatch() and stream the whole thing.
    if (len == 5 && !strncmp(line, "peers", 5)) { LineSink ls(ble_sink); handle_peers(ls); ls.flush(); return 0; }
    if (len > 5 && !strncmp(line, "peers ", 6)) return write_peers_err(out, cap, "console_only");
    // Inbox sync (companion-only): stream the reply via mrble::tx_line and return 0 (no buffered single-line ack).
    if ((len == 10 || (len > 10 && line[10] == ' ')) && !strncmp(line, "pull_inbox", 10)) { LineSink ls(ble_sink); handle_pull_inbox(line + 10, ls); ls.flush(); return 0; }
    if ((len ==  9 || (len >  9 && line[9]  == ' ')) && !strncmp(line, "mark_read",   9)) { LineSink ls(ble_sink); handle_mark_read(line + 9,  ls); ls.flush(); return 0; }
    if ((len ==  7 || (len >  7 && line[7]  == ' ')) && !strncmp(line, "del_msg",     7)) { LineSink ls(ble_sink); handle_del_msg(line + 7,   ls); ls.flush(); return 0; }   // §3.5 delete
    meshroute::Command cmd{};
    const ParseErr e = parse_command(line, len, cmd);
    if (e == ParseErr::ok) {
        if (cmd.kind == meshroute::CmdKind::peerkey) return handle_peerkey(out, cap, cmd);   // §2/§3: install + persist + contract ack
        if (cmd.kind == meshroute::CmdKind::peername) return handle_peername(out, cap, cmd); // §AB2: rename + persist + the synchronous ack
        const meshroute::CmdResult r = g_node.on_command(cmd);
        // ★★ §id-hash S1b (QA finding P1c): `r.accepted`, NOT `r.code == queued`. `reqpubkey_sent` means "the TX path ACCEPTED it" (owner ruling 2026-08-02), NOT "the on-air
        // request was FLOODED". Two accepted outcomes hand the TX path nothing at all: the hosted-mobile local
        // cache hit (which reports through its own peer_key_cached push), and — before S1b — every one of
        // emit_hash_query's four silent early-outs, which now carry their own error codes instead. Anything that did
        // not reach the transmitter falls through to the generic write_ack, which is the honest answer for both.
        // ⚠ ACCEPTANCE IS NOT AIRTIME: a frame accepted into the LBT defer ring reaches the radio when a timer fires;
        // if it dies there, node.cpp's defer arm reports it late (`!!` operator log). No synchronous result can know.
        if (cmd.kind == meshroute::CmdKind::reqpubkey && r.code == meshroute::CmdCode::queued && r.accepted) {
            // ★★ §id-hash S1 (spec §1-A's SECOND SITE): this echo used to re-resolve the id itself with
            //     `if (rh == 0 && dst_id != 0) g_node.team_key_of_id(dst_id, rh);`
            // — a THIRD hand-rolled one-table lookup, so a static-plane by-id reqpubkey would still have echoed
            // hash=0 to the companion after node.cpp's arm was fixed. The result now CARRIES the answer
            // (CmdResult::dst_hash = the hash the query flew for, ::plane = which plane resolved it), so this
            // transport reads it instead of re-deriving it and the two can no longer disagree (U1).
            return write_reqpubkey_sent(out, cap, r.dst_hash, r.plane);   // §2: the contract's reqpubkey_sent event (the no-identity fail path keeps its existing error ack)
        }
        return write_ack(out, cap, r);
    }
    if (e == ParseErr::empty) return 0;
    // §3: a malformed peerkey -> the contract's peerkey_err.
    // ⚠ §AB2, KNOWN AND DELIBERATELY NOT WIDENED (C1): `bad_hex` is now a slight over-claim. Since `peerkey` accepts an
    // OPTIONAL quoted name, a `peerkey <valid 64-hex> "` (unterminated) or `peerkey <valid 64-hex> ""` (empty) also lands
    // here and is reported as `bad_hex` when the hex was fine. Distinguishing them needs a ParseErr the enum cannot
    // express (or a second parse), and widening a shipped contract reason is its own slice. The hex is by far the likelier
    // cause and the remedy (re-scan / re-type the card) is unchanged, so this stays until someone needs the split.
    if (len >= 8 && !strncmp(line, "peerkey ", 8))
        return (size_t)snprintf(out, cap, "{\"ev\":\"peerkey_err\",\"reason\":\"bad_hex\"}\n");
    if (len >= 9 && !strncmp(line, "peername ", 9))                                           // §AB2: a malformed peername -> peer_name_err, not a bare parse error
        return meshroute::console::write_peer_name_err(out, cap, "bad_args");
    // §command-sink-consolidation: not a companion JSON verb and not a Node command -> offer the FULL console surface as
    // canonical text over BLE via the unified dispatch. ADDITIVE: every companion verb is handled above, so this only
    // catches lines that previously returned "unknown_cmd" (team/mobile/gateway/faults/help/lookup/…). The BLE link is
    // the authenticated (MITM-passkey) admin transport. (reboot/regen/ota/factory_reset become reachable here too —
    // factory_reset still requires its `confirm` token; flag for review if the console should stay USB-only.)
    // ★★ §B95 invariant 9: `help` / `?` is REFUSED here, BEFORE the text fallback below, and that refusal is
    // load-bearing rather than cosmetic. The old `hl()` help wrote straight to `Serial`, so a BLE `help` returned
    // NOTHING over BLE (it printed to USB instead) — an accident that happened to bound it. Now that help honours its
    // sink, the fallback would stream 75 lines / 6121 B over BLE-NUS at ~20 B per notification: precisely the
    // self-inflicted flood that has wedged this node before, and the reason `peers all` is refused two lines up (U3 —
    // same shape, same reason). The remedy is named: the USB console. A bounded ONE-LINE answer, never a stream.
    if ((len == 4 && !strncmp(line, "help", 4)) || (len == 1 && line[0] == '?'))
        return write_err(out, cap, "help", "console_only");
    if (e == ParseErr::unknown_verb) {
        LineSink ls(ble_sink);
        if (dispatch(line, len, ls)) { ls.flush(); return 0; }
    }
    return write_err(out, cap, "parse", e == ParseErr::unknown_verb ? "unknown_cmd" : "bad_args");
}

void setup() {
#if MR_CONSOLE
    Serial.begin(115200);
#endif
    // §5.1: capture + CLEAR the reset reason FIRST — before BLE/SoftDevice (direct NRF_POWER access must be safe;
    // on ESP32 it's the IDF-latched esp_reset_reason(), read-only). MRFAULT_HW = nRF52 OR ESP32.
#if defined(MRFAULT_HW)
    const uint16_t resetreas = mrfault::fault_read_resetreas_and_clear();
#endif
    // Debug-trace hooks: route lib/core's _hal.trace_on()/_hal.log() to `debug on` + Serial. Keeps device_hal
    // Arduino-free (it can't read frame_trace.h's g_mr_trace_on). The log sink itself gates on g_mr_trace_on so
    // `debug off` stays fully silent (as before — DeviceHal::log was a no-op). Captureless lambdas -> fn-pointers.
    // ★★ §id-hash S1d-fix (2026-08-01, QA P2): the sink is NO LONGER unconditionally trace-gated. A `lib/core`
    // message prefixed `!!` is an OPERATOR-CRITICAL diagnostic and prints even under `debug off`.
    // ⚠ THE CLAIM THIS REPAIRS WAS MINE AND IT WAS FALSE: the deferred-TX loss report was documented as
    // "no silent loss", but with the old lambda it was invisible on hardware in normal operation — `_hal.log` is a
    // DEBUG channel, not an operator channel, and I described it as the latter. QA caught it.
    // ⓘ A prefix rather than a new Hal method: `log()` is the only text channel `lib/core` has, and widening the
    // HAL interface would force every implementation (device, sim, three test HALs) to grow a method for one
    // message. The marker is checked in ONE place, here.
    // ⓘ Other existing `_hal.log` sites are equally operator-critical candidates ("beacon pack failed",
    //   "team M-frame with no team_id — refusing tx"). They are deliberately NOT re-marked in this slice (C1):
    //   changing when they print is a behaviour change of their own. The mechanism is now there for them.
    g_hal.set_debug_hooks([]() -> bool { return meshroute::g_mr_trace_on; },
                          [](const char* m) {
                              if (meshroute::g_mr_trace_on) { mrcon.println(m); return; }
                              if (m && m[0] == '!' && m[1] == '!') mrcon.println(m);   // operator-critical: never silent
                          });
#if MR_CONSOLE
    while (!Serial && millis() < 3000) { /* wait for USB CDC, but don't block forever */ }
#endif
    delay(2000);   // Settle: the USB-CDC port re-enumerates on every reset, and the host serial
                   // monitor reattaches AFTER that — so without a pause the one-time boot banner
                   // prints into the void. 2 s lets the monitor catch up before we print it.

    // InternalFS self-heal (Part 2, 2026-06-24): mount the on-chip FS; if a reset-during-write corrupted it
    // (LFS_NO_ASSERT now makes that an ERROR, not a halt), REFORMAT to a clean FS so the node BOOTS instead of
    // bricking-to-serial. MUST precede every load*() below. nRF52 only (ESP32 NVS = no-op). ⚠ A reformat wipes
    // /mrid too -> the node re-mints identity + loses its join -> re-provision (cfg set + join, or the harness).
    if (mrnv::mount_or_repair()) {
        g_fs_reformatted = true;
        mrcon.println(F("\n\xe2\x9a\xa0 INTERNALFS CORRUPT \xe2\x80\x94 REFORMATTED (re-provision needed: `cfg set` + `join`, or the harness `provision`)"));
    }

#if defined(MRFAULT_HW)
    // §5.2-5: record THIS boot in the fault ring (reason + the scratch's ran_ms + any captured fault), then re-prime
    // the scratch and ARM the 8 s watchdog (just after the deliberate settle, so the settle isn't watched but radio
    // init + NV + the whole runtime are). The store (InternalFS on nRF52 / NVS on ESP32) is brought up inside load_faults.
    if (!mrnv::load_faults(g_fault_log)) mrfault::fault_log_init(g_fault_log);
    g_last_reset = mrfault::fault_compose_record(resetreas, g_fault_log.boot_seq + 1);
    mrfault::fault_log_push(g_fault_log, g_last_reset);
    // §nv-unchecked [1/5]: KNOWN UNCHECKED SAVE, preserved deliberately (dedup 3-B item 4 is a refactor — adding
    // error handling where there is none would be a behaviour change, and no test or scenario can see NV at all).
    // A failed /mrfault write here loses ONE boot record; the ring is best-effort diagnostics. Owner ruling owed.
    (void)mrnv::save_faults(g_fault_log);
    g_last_reset_valid = true;
    mrfault::fault_scratch_reset_after_capture();
    mrfault::fault_wdt_start();
#endif

    print_banner(mrcon);   // §6: version (build/git/board) + the last reset reason — replaces the old boot banner + board lines
    // These are the COMPILE-TIME build defaults, printed BEFORE the NV blob loads — NOT the live config.
    // A persisted `cfg set` overrides them; the real operating point prints below (control sf / data sf / `cfg`).
    mrcon.print(F("  build def = ")); mrcon.print((double)LORA_FREQ, 4); mrcon.print(F(" MHz  sf"));
    mrcon.print(LORA_SF); mrcon.print(F("/bw")); mrcon.print((double)LORA_BW, 1); mrcon.print(F("/cr"));
    mrcon.print(LORA_CR); mrcon.println(F("  (NV cfg overrides — live values below)"));

    // Bring up the SX1262 (begin/CRC/TCXO/DIO2-rf-switch/RXEN/RX-boost) then arm continuous RX.
#if defined(P_LORA_SCLK)
    bool ok = g_radio.std_init(&SPI);
#else
    bool ok = g_radio.std_init();
#endif
    mrcon.print(F("  radio     = ")); mrcon.println(ok ? F("OK") : F("INIT FAILED"));
    g_radio_ok = ok;
    // Device config from compile-time defaults; a persisted `cfg set` (NV) then overrides the radio/protocol
    // knobs AND the node identity. node_id 0 = unprovisioned (sends refused; provision via NV or join).
    meshroute::NodeConfig cfg;
    cfg.routing_sf            = LORA_SF;                         // RX + control plane on the radio's SF
    // §bw-round-invariant: ONE conversion path for kHz->Hz (protocol::khz_to_hz, U1). That helper ROUNDS where this
    // site TRUNCATED, so the switch is only a no-op while the two agree for the configured LORA_BW — which the
    // static_assert proves at compile time, per env, instead of a doc asserting it. A future board BW where they
    // disagree FAILS THE BUILD (deliberately loud: it would silently shift the value the fleet ships with).
    static_assert(static_cast<uint32_t>(LORA_BW * 1000.0) == meshroute::protocol::khz_to_hz(LORA_BW),
                  "§bw-round-invariant: truncating and rounding kHz->Hz disagree for this board's LORA_BW — "
                  "routing it through khz_to_hz CHANGES the shipped value; re-gate that deliberately");
    cfg.radio_bw_hz           = meshroute::protocol::khz_to_hz(LORA_BW);   // keep the Node's airtime math == the radio's BW
    cfg.radio_cr              = LORA_CR;
    // §layer-freq (2026-07-27): the Node's view of the GLOBAL carrier, same source as g_freq_mhz above
    // (LORA_FREQ, then nv.freq_mhz below). It is what Node::active_freq_mhz() falls back to when a layer
    // leaves freq_mhz at 0, so activate_layer can RESET the carrier instead of leaving a stale one. Without
    // it that fallback would be 0.0 = a Hal no-op and the reset could not happen at all.
    // ⚠ On THIS device it is belt-and-braces today, not the live fix: the NV restore below already
    // pre-resolves the inherit by writing nv.freq_mhz into both cfg.layers[] entries, so a provisioned board
    // never presents a 0 per-layer carrier. Keep BOTH — the pre-resolution is the metal-behaviour-preserving
    // path (removing it is a separate, bench-gated change), this is the single rule everything else uses.
    // LORA_FREQ is already MHz (units note in platformio.ini) — no conversion, so no §bw-round-invariant twin.
    cfg.radio_freq_mhz        = LORA_FREQ;
    cfg.leaf_id               = 0;
    cfg.duty_cycle            = (double)LORA_DUTY_CYCLE_PCT / 100.0;
    cfg.duty_cycle_window_ms  = 3600000;                        // 1 h (ETSI)
    cfg.peer_count            = 0;                              // no sim:nodes() on device -> no rt_full telemetry
    // Default OFF (pending Step-3 bench sign-off); `cfg set lbt 1` + reboot enables it via NV. The old
    // scanChannel()-spin reason is gone — channel_busy() is now the non-blocking software noise-floor LBT.
    cfg.lbt_enabled           = false;

    uint8_t node_id = 0;                                         // unprovisioned default; NV / join sets it
    mrnv::Blob nv{};
    if (mrnv::load(nv)) {                                        // a prior `cfg set` persisted -> apply it
        node_id               = nv.node_id;
        g_freq_mhz            = nv.freq_mhz;
        cfg.radio_freq_mhz    = nv.freq_mhz;     // §layer-freq: keep the Node's global carrier == g_freq_mhz (activate_layer's inherit fallback)
        cfg.routing_sf        = nv.routing_sf;
        cfg.allowed_sf_bitmap = nv.allowed_sf_bitmap;
        cfg.radio_bw_hz       = nv.bw_hz;        cfg.radio_cr     = nv.cr;
        cfg.duty_cycle        = nv.duty;         cfg.lbt_enabled  = nv.lbt != 0;
        cfg.beacon_period_ms  = nv.beacon_ms;
        g_tx_power            = (nv.version >= 3) ? nv.tx_power : (int8_t)LORA_TX_POWER;   // v2 blob had no tx_power -> keep the default
        cfg.is_gateway        = nv.is_gateway != 0;   cfg.gateway_only = nv.gateway_only != 0;   // v6 role/topology (only v6 blobs load -> always present)
        cfg.is_mobile         = nv.is_mobile != 0;    cfg.leaf_id      = nv.leaf_id;
        cfg.team_id           = nv.team_id;           // §mobile 6.1: team-id overlay (0 = no team)
        // ★★ §role-model / B28 R2 — THE BOOT ENFORCEMENT POINT, and the reason the live switch alone is not enough:
        // NV persists `team_id` and `is_mobile` INDEPENDENTLY (the two lines above), so a provisioned blob can
        // reproduce the outlawed `team_id != 0 && !is_mobile` config with NO console involved — without this, a power
        // cycle trivially bypasses Node::set_team_id's enforcement. role_enforce() is the SAME rule the live switch
        // uses (node_role.h — ONE definition, U1), and the correction is REPORTED, never silent (B28 constraint 3).
        // ⓘ LIVE-ONLY, deliberately: this does NOT rewrite NV. This function avoids boot-time NV writes on principle
        // (see the g_persist_* priming below, whose whole purpose is "no spurious boot write"); the fix is idempotent
        // so it simply re-applies each boot; and every surface the operator/app reads (`cfg`, `status`, the ready JSON)
        // reports the LIVE role, not the blob. The bytes are corrected the next time the role legitimately changes
        // (handle_team persists config().is_mobile right after its switch).
        // ⓘ The `dropped_team` arm is the owner's "unless it is impossible" case: a MR_FEAT_MOBILE 0 build has no plane
        // for a team member to be reachable on, and a stale non-zero team_id there is NOT inert — the pre-parse
        // foreign-nibble beacon drop (`wire::flags_of(bytes[0]) != _cfg.leaf_id && _cfg.team_id == 0`,
        // node_beacon.cpp) is NOT MR_FEAT_TEAM-gated, so it would make such a build ingest beacons from FOREIGN leaf
        // nibbles. Dropping the id is what keeps a feature-stripped build behaving as built.
        switch (meshroute::role_enforce(cfg)) {
            case meshroute::RoleFix::none: break;
            case meshroute::RoleFix::forced_mobile:
                mrcon.println(F("  role      = MOBILE (forced: NV held team_id != 0 with mobile=0 — a team member IS a mobile; `team 0` leaves the team)"));
                break;
            case meshroute::RoleFix::dropped_team:
                mrcon.println(F("  role      = STATIC, NV team_id DROPPED (this firmware has no mobile/team plane)"));
                break;
        }
        cfg.mobile_autoregister = nv.mobile_autoregister != 0;   // §mobile console: autonomy toggle (a valid v18 NV was seeded from the ON default)
        cfg.intro_attach      = nv.intro_attach != 0;            // §S2: first-contact INTRO auto-attach (a valid v21 NV was seeded from the ON default)
        g_ble_mode            = nv.ble_mode;          g_ble_period_min = nv.ble_period_min;      // v7 BLE policy (only v7 blobs load)
        g_ble_pin             = nv.ble_pin;
        // §loc-per-send (v23): the v9 `loc_in_dm` restore is GONE with the field — location is a per-send `-l` flag now.
        cfg.e2e_dm            = (nv.e2e_dm != 0);                                                   // v10 E2E encrypt toggle (§4b)
        if (nv.gw_announce_duty_pct != 0)        cfg.gw_announce_duty_pct        = nv.gw_announce_duty_pct;        // v11 gateway noise control;
        if (nv.gw_announce_min_interval_ms != 0) cfg.gw_announce_min_interval_ms = nv.gw_announce_min_interval_ms; //   0 => keep the default
        if (nv.gw_herd_slack != 0)               cfg.gw_herd_slack              = nv.gw_herd_slack;               // v13 §3e herd-spread slack (0 => default 2)
        if (nv.channel_active_fraction > 0.0f)   cfg.channel_active_fraction    = nv.channel_active_fraction;    // v16 anti-spam per-leaf tunables;
        if (nv.channel_min_interval_ms != 0)     cfg.channel_min_interval_ms    = nv.channel_min_interval_ms;    //   0 => keep the NodeConfig default
        if (nv.dm_min_interval_ms != 0)          cfg.dm_min_interval_ms         = nv.dm_min_interval_ms;
        cfg.lineage_id   = nv.lineage_id;        cfg.config_epoch = nv.config_epoch;                            // v14 R6.1 leaf-config membership
        cfg.leaf_name_len = (nv.leaf_name_len <= meshroute::protocol::leaf_name_max) ? nv.leaf_name_len : 0;
        for (uint8_t i = 0; i < cfg.leaf_name_len; ++i) cfg.leaf_name[i] = (char)nv.leaf_name[i];
        // v8 DUAL-LAYER GATEWAY: provision the raw per-layer fields ONLY (on_init validates the 2-layer config + derives
        // window_ms/window_offset_ms when 0). n_layers != 2 -> single-layer exactly as today (no behaviour change).
        if (nv.n_layers == 2) {
            cfg.n_layers = 2;
            // layer 0 = the legacy single-layer fields (node_id / routing_sf / sf_list / beacon) + the persisted window schedule.
            cfg.layers[0].layer_id          = nv.layer0_id;
            cfg.layers[0].node_id           = nv.node_id;
            cfg.layers[0].routing_sf        = nv.routing_sf;
            cfg.layers[0].allowed_sf_bitmap = nv.allowed_sf_bitmap;
            cfg.layers[0].beacon_period_ms  = nv.beacon_ms;
            cfg.layers[0].window_period_ms  = nv.window_period_ms;   // shared layer0<->layer1 cycle
            cfg.layers[0].window_ms         = nv.l0_window_ms;       // 0 = on_init derives
            cfg.layers[0].window_offset_ms  = nv.l0_window_offset_ms;
            cfg.layers[0].freq_mhz          = nv.freq_mhz;           // v12 per-layer freq: layer 0 = the node's freq
            // layer 1 = the l1_* block (window_period_ms shared with layer 0).
            cfg.layers[1].layer_id          = nv.l1_layer_id;
            cfg.layers[1].node_id           = nv.l1_node_id;
            cfg.layers[1].routing_sf        = nv.l1_routing_sf;
            cfg.layers[1].allowed_sf_bitmap = nv.l1_allowed_sf_bitmap;
            cfg.layers[1].beacon_period_ms  = nv.l1_beacon_period_ms;
            cfg.layers[1].window_period_ms  = nv.window_period_ms;   // shared cycle
            cfg.layers[1].window_ms         = nv.l1_window_ms;       // 0 = on_init derives
            cfg.layers[1].window_offset_ms  = nv.l1_window_offset_ms;
            cfg.layers[1].freq_mhz          = (nv.l1_freq_mhz > 0.0) ? nv.l1_freq_mhz : nv.freq_mhz;  // v12: 0 = inherit layer 0's freq
            cfg.layers[1].bw_hz             = nv.l1_bw_hz;   // v17: 0 = inherit (active_bw_hz() resolves the inherit at read)
            cfg.layers[1].cr                = nv.l1_cr;      // v17: 0 = inherit
        }
        mrcon.println(F("  config    = loaded from NV"));
    }
    // Identity (/mrid): load the 32-byte master seed, or mint one from the HW-RNG on first boot.
    mrnv::IdBlob idb{};
    if (mrnv::load_id(idb)) {
        mrcon.println(F("  identity  = loaded from NV (/mrid)"));
    } else {
        mrrng::fill(idb.seed, sizeof idb.seed);                 // first boot -> generate a fresh seed
        idb.magic = mrnv::kIdMagic; idb.version = mrnv::kIdVersion; idb.name_len = 0;
        mrcon.println(mrnv::save_id(idb) ? F("  identity  = generated (first boot -> /mrid)")
                                          : F("  identity  = generated (first boot, NV SAVE FAILED — volatile)"));
    }
    meshroute::identity_from_seed(g_identity, idb.seed);        // key_hash32 = ed_pub[:4]
    g_node.set_identity(node_id, g_identity.key_hash32);        // node_id 0 stays unprovisioned -> do_send refused
    g_node.set_crypto_identity(g_identity.x_secret, g_identity.ed_pub);   // DP1: install the E2E crypto identity (X25519 + ed_pub)
    g_node.set_name(idb.name, static_cast<uint8_t>(idb.name_len));   // §1.3: load the human name into the core (pubkey exchange + display); empty -> effective_name defaults to MeshRoute node: 0x<hash>
    g_lat_e7 = idb.lat_e7; g_lon_e7 = idb.lon_e7;              // node location (persisted in /mrid; 0,0 on first boot)
    cfg.lat_e7 = g_lat_e7; cfg.lon_e7 = g_lon_e7;             // the node's fix, from /mrid — what a per-send `send … -l` attaches (§loc-per-send; there is no `loc_in_dm` toggle any more)
    // §remote-mgmt (v20): restore the pinned admin pubkey + replay counter floor (no-op stub when MR_FEAT_REMOTE_MGMT=0).
    g_node.admin_load(nv.admin_pubkey, nv.admin_counter_floor, nv.admin_provisioned);
    // §team-ch-key (v22): restore the TEAM CHANNEL keypair (no-op stub when MR_FEAT_TEAM=0). `nv` is
    // ZERO-INITIALISED above, so a fresh chip or a version-rejected blob restores present=0 + zeroed buffers —
    // never a fabricated key. Verbatim, no re-derivation: these bytes ARE the secret (there is no seed).
    g_node.team_channel_key_load(nv.team_ch_pub, nv.team_ch_priv, nv.team_ch_key_present != 0);
    // node_id DAD: restore the persisted lease state so a reboot KEEPS its id + tiebreak seniority (NV blob v4).
    g_node.restore_join_state(nv.claim_epoch, (node_id != 0) && (nv.joined != 0));
    g_persist_id = node_id; g_persist_epoch = nv.claim_epoch;        // prime the persist tracker -> no spurious boot write
    g_persist_join = ((node_id != 0) && (nv.joined != 0)) ? 1 : 0;
    // §mobile 6.4: restore the PERSISTED team-DAD id so a power-cycle (hiker switches off) keeps a STABLE team-plane id
    // (no re-DAD churn). Non-zero -> CONFIRMED (the no-host trigger is guarded on _team_local_id==0, so no re-DAD; it
    // announces via periodic beacons + defends). 0 -> team-DAD picks fresh on the no-host path.
    g_node.set_team_local_id(nv.team_local_id); g_persist_team_local_id = nv.team_local_id;
    // NB g_ctr_lease is primed on the on_init-SUCCESS path below (after restore_channel_ctr), NOT here: if on_init is
    // REFUSED the live ctr stays 0 while a here-primed lease (nv.channel_ctr) would read as "due" and REGRESS the
    // persisted lease to 64. Priming only alongside the restore keeps live ctr == lease -> no spurious/regressing write.
    print_identity(idb);                                        // key_hash32 (hex) + name
    mrcon.print(F("  node id   = ")); mrcon.print(node_id);
    mrcon.println(node_id == 0 ? F("  (UNPROVISIONED: cfg set node_id <1..254> + reboot, or join)") : F(""));
    mrcon.print(F("  control sf= ")); mrcon.print(cfg.routing_sf); mrcon.println(F("  (RTS/CTS/ACK + beacons)"));
    mrcon.print(F("  data sf   = "));
    if (cfg.allowed_sf_bitmap) { print_sf_list(mrcon, cfg.allowed_sf_bitmap); mrcon.println(F("  (receiver picks the fastest by SNR)")); }
    else                       { mrcon.println(F("(none — set sf_list; data send is REFUSED until configured)")); }

    mrcon.print(F("  tx power  = ")); mrcon.print((int)g_tx_power); mrcon.println(F(" dBm"));

    // Apply the operating point to the radio (freq/SF/BW/CR), re-arm RX, and match the Hal airtime ledger.
    if (ok) {
        g_radio.setFrequency((float)g_freq_mhz);
        g_radio.setSpreadingFactor((uint8_t)cfg.routing_sf);
        g_radio.setBandwidth((float)cfg.radio_bw_hz / 1000.0f);
        g_radio.setCodingRate((uint8_t)cfg.radio_cr);
        g_radio.setSyncWord(MESHROUTE_SYNC_WORD);               // override std_init's PRIVATE (0x12): reject alien protocols at the PHY
        g_iradio.begin();                                       // (re)arm continuous RX on the applied SF

    }
    g_hal.configure(/*sf=*/(int16_t)cfg.routing_sf, /*bw_hz=*/(int32_t)cfg.radio_bw_hz,
                    /*cr=*/(int8_t)cfg.radio_cr, /*preamble=*/(int16_t)P::preamble_sym,
                    /*power=*/g_tx_power, /*channel_busy_hold_ms=*/100);
    g_hal.seed_rng((uint32_t)millis() ^ (g_node.key_hash32() * 2654435761u));

    // on_init REFUSES a bad dual-layer config (§3.2 fail-loud). Today the device builds cfg with n_layers==1
    // (always valid), so this never fires — but Slice 3 (per-layer cfg keys) can produce an invalid gateway, and
    // the device must NOT operate on a half-applied config. Print loud + leave the node unconfigured.
    if (!g_node.on_init(cfg)) mrcon.println(F("  config    = REFUSED (invalid layer config — node NOT operational)"));
    else { g_node.restore_channel_ctr(nv.channel_ctr);          // v15: continue the channel send-ctr across reboot (no id-reuse); after on_init so _active+_node_id are valid
           g_node.restore_peer_ctr_floor(nv.channel_ctr);       // D7: seed the per-peer FLOOR from the same leased high-water so DM ctrs also resume above the pre-reboot value (no re-mint -> no silent companion dedup)
           g_ctr_lease = nv.channel_ctr; }                      // prime the lease = the (leased) ctr ONLY now that the live ctr was restored -> live == lease, no spurious/regressing write
    // Install the inbox stores so record-on-delivery + pull_inbox work. With the interim RAM store: give it a
    // per-boot-unique storage_epoch (HW-RNG; drawn here BEFORE BLE init, so the bare-metal NRF_RNG path is still
    // valid) -> after a reboot the companion sees a NEW epoch and re-pulls (the volatile store lost its history).
#if !defined(MRINBOX_QSPI_READY)
    uint32_t boot_epoch = 0; mrrng::fill(reinterpret_cast<uint8_t*>(&boot_epoch), sizeof boot_epoch);
    g_inbox_dm.set_epoch(boot_epoch); g_inbox_ch.set_epoch(boot_epoch);
#endif
    g_node.inbox().on_init(&g_inbox_dm, &g_inbox_ch);
    // E2E §2 + §AB1: reload the peer ADDRESS BOOK (/mrpeers) — the QR-pinned keys AND the on-air `authoritative`
    // ones, each with its cached NAME, re-installed AT THE STORED CONFIDENCE (v1 re-installed everything as
    // `pinned`, silently promoting on-air keys). After on_init so the LayerRuntime _active is live. ⚠ The 1160-B
    // blob buffer is deliberately NOT a local here: setup() runs on the FIXED 4 KB nRF52 loop-task stack — see the
    // static in firmware_commands.cpp, which owns both /mrpeers users.
    mrfw::peer_store_restore();
#if defined(MRINBOX_QSPI_READY)
    mrcon.println(F("  inbox     = QSPI (durable)"));
#else
    mrcon.print(F("  inbox     = RAM volatile, ")); mrcon.print(MR_RAM_INBOX_SLOTS);
    mrcon.println(F(" msgs/store (interim — lost on reboot; durable QSPI store is a bench-TODO)"));
#endif
    // node_id DAD auto-join: an UNPROVISIONED node (no persisted id) self-assigns one via the claim state machine.
    // A node that rebooted WITH a persisted id skips this — it already owns it (restored above). BUT a freshly-flashed
    // node must FIRST be configured with a target network: with NO layer/leaf set it would DAD (J + BCN) on the default
    // freq/bw/leaf=0 — the wrong channel. So gate on the configured sentinel: layer0_id (the full 1..255 layer id)
    // != 0 OR leaf_id != 0 (the latter covers the advanced `cfg set leaf_id` path, which sets the nibble but not
    // layer0_id). layer 0 = unconfigured -> stay IDLE until `join`/`create` sets the floor + triggers the DAD.
    if (node_id == 0) {
        mrnv::Blob lb{};
        const bool configured = mrnv::load(lb) && (lb.layer0_id != 0 || lb.leaf_id != 0);
        if (configured) {
            meshroute::Command jc{}; jc.kind = meshroute::CmdKind::join;
            g_node.on_command(jc);
            mrcon.println(F("  join      = auto-DAD started (unprovisioned)"));
        } else {
            mrcon.println(F("  join      = IDLE — unconfigured (layer=0). Set freq/bw/ctrl_sf/layer via 'join' or 'create'."));
        }
    }
    // Step 5: BLE companion transport (XIAO nRF52840 only; an inert no-op on ESP32/native). Brings up the
    // S140 SoftDevice + Nordic UART Service + the advertising-window policy, and arms the SD-RNG keystone
    // (mrble::begin sets mrrng::sd_enabled()=true). Gated on the persisted ble_mode (NV v7); off = today's
    // exact bare-metal path. The advert name is the /mrid name if set, else "MeshRoute-<id>" (cosmetic —
    // iOS discovers the node by the NUS service UUID, not the name).
    if (g_ble_mode != 0) {
        char ble_name[20];
        if (idb.name_len) { const size_t k = idb.name_len < 19 ? (size_t)idb.name_len : 19; memcpy(ble_name, idb.name, k); ble_name[k] = '\0'; }
        else              { snprintf(ble_name, sizeof ble_name, "MeshRoute-%u", (unsigned)node_id); }
        const bool up = mrble::begin(g_ble_mode, g_ble_period_min, g_ble_pin, ble_name, &ble_dispatch_line);
        mrcon.print(F("  ble       = "));
        if (up) { mrcon.print(g_ble_mode == 1 ? F("on") : F("periodic"));
                  mrcon.println(F("  (secured: MITM passkey pairing — PIN in `cfg`)")); }
        else    { mrcon.println(F("INIT FAILED")); }      // fail loud: NO silent fall-back to bare-metal/insecure
    }
    // OTA rollback safety (ESP32 only — inert on nRF52): tell the bootloader this firmware is healthy.
    // If we crash before reaching here (bad config, radio fail), the bootloader boots the previous slot.
    #if defined(ARDUINO_ARCH_ESP32) || defined(ESP32)
    esp_ota_mark_app_valid_cancel_rollback();
    #endif
    mr_ui_init();   // §featuresplit slice 4: bring up the board display (no-op unless MR_FEAT_OLED)
    mrcon.println(F("  node      = up. Type 'help' for commands."));
}

// Accumulate a USB-CDC line; on '\n' parse it into a Command + hand it to the Node.
// USB console input present? false in a production (MR_CONSOLE=0) build — there IS no USB serial — so a console-free
// node latches/sleeps like a headless node. (The real input drain is service_console, #if'd out below.)
#if MR_CONSOLE
static inline bool serial_has_input() { return Serial.available(); }
#else
static inline bool serial_has_input() { return false; }
#endif

#if MR_CONSOLE
static void service_console() {
    static char   line[1024];  // 1024 (Part 4): matched to CFG_TUD_CDC_RX_BUFSIZE so a long `testsend … -t ms,…` arm line fits
    static size_t pos = 0;
    static bool   overflow = false;
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\r') continue;
        if (c == '\n') {
            if (overflow) {                              // a line longer than the buffer: REJECT loudly — never process a
                mrcon.println(F("> err: line too long (>1023) — rejected (chunk the testsend offsets)"));   // silently-truncated command (a cut offset would fire at the wrong time)
                pos = 0; overflow = false; continue;
            }
            line[pos] = '\0';                            // null-terminate (pos <= sizeof-1) for the debug cmds
            if (!dispatch(line, pos, mrcon)) {             // routes/cfg/status handled here; else a Node command
                meshroute::Command cmd{};
                const meshroute::console::ParseErr e = meshroute::console::parse_command(line, pos, cmd);
                if (e == meshroute::console::ParseErr::ok) {
                    if (cmd.kind == meshroute::CmdKind::peerkey) {       // §2/§3: install + persist + the contract ack
                        char jb[80]; const size_t m = handle_peerkey(jb, sizeof jb, cmd);
                        mrcon.write(reinterpret_cast<const uint8_t*>(jb), m);
                    } else if (cmd.kind == meshroute::CmdKind::peername) {   // §AB2: rename + persist + the synchronous ack
                        // 256 = the BLE g_out size, and it is the WORST CASE not a guess: 29 B envelope + 10 digits of
                        // hash + 8 B `,"name":` + 2 quotes + a 32-B name whose every byte escapes to `\u00xx` (6x) =
                        // 240 + `}` + '\n' + NUL = 244. A tighter buffer would make JsonBuf::finish() return 0 and the
                        // ack vanish SILENTLY (it is overflow-safe, not overflow-loud), which is the failure to avoid.
                        char jb[256]; const size_t m = handle_peername(jb, sizeof jb, cmd);
                        mrcon.write(reinterpret_cast<const uint8_t*>(jb), m);
                    } else {
                    const meshroute::CmdResult r = g_node.on_command(cmd);
                    mrcon.print(F("> "));
                    // ★ §err-reason/B32 (bench-found 2026-07-31): print the CmdCode ITSELF, never a bare `err`. The old
                    // ternary collapsed err_no_binding / err_unprovisioned / err_unknown_dst / err_too_large … into ONE
                    // indistinguishable `err ctr=`, so a refusal named no reason: `reqpubkey 245` answered `err ctr=0
                    // depth=0` and the operator could not tell which wall he had hit. C2 — printing `err` without the
                    // reason is not "loud". cmdcode_name is the ONE mapper (U1, no second switch here) and it is the
                    // SAME token the companion's {"ack":"…"} carries, so the text and JSON transports can no longer
                    // drift apart. ★ No `err ` word is prefixed and that is deliberate, not an omission: every
                    // non-`queued` enumerator's string already begins with `err_` (so does the out-of-range fallback
                    // "err_unknown"), so the token self-labels — an invariant this print site cannot test, and which is
                    // therefore ASSERTED NATIVELY in test/test_console_json.cpp beside the enum-walker. The success
                    // line `queued ctr=N depth=N` is byte-identical to before; only refusals gained a reason.
                    mrcon.print(meshroute::console::cmdcode_name(r.code)); mrcon.print(F(" ctr="));
                    mrcon.print(r.ctr); mrcon.print(F(" depth=")); mrcon.print(r.queue_depth);
                    // The send handle for hash/layer-addressed sends (dh != 0 = correlate by hash, not id).
                    if (r.dst_hash) { mrcon.print(F(" dh=0x")); mrcon.print(r.dst_hash, HEX); }
                    if (r.layer_path) { mrcon.print(F(" lp=0x")); mrcon.print(r.layer_path, HEX); }
                    // ★ §id-hash S1 (spec §3-D9): the plane the command executed on. Omitted when 0 (= not
                    // plane-scoped), so every other verb's line is byte-identical to before. On `reqpubkey <id>` this is
                    // the answer to "which namespace did you just spend airtime in", which a bare `queued` never said.
                    if (r.plane) { mrcon.print(F(" plane=")); mrcon.print(meshroute::console::cmdplane_name(r.plane)); }
                    mrcon.println();
                    // §id-hash S1: the remedy line for a refused reqpubkey, at handle_hashof parity. The text lives in
                    // firmware_commands beside hashof's (U3 — fw_main stays glue); a `queued` prints nothing.
                    print_reqpubkey_hint(mrcon, cmd, r);
                    }
                } else if (e != meshroute::console::ParseErr::empty) {
                    if (pos >= 8 && !strncmp(line, "peerkey ", 8))       // §3: a malformed peerkey -> the contract's peerkey_err
                        mrcon.println(F("{\"ev\":\"peerkey_err\",\"reason\":\"bad_hex\"}"));
                    else
                        mrcon.println(F("> parse error"));
                }
            }
            pos = 0;
        } else if (pos < sizeof(line) - 1) {
            line[pos++] = c;
        } else {
            overflow = true;   // [5] mark + reject on '\n' — don't silently truncate (a cut multi-digit offset = a wrong-time fire)
        }
    }
    // ★★ §B95: the console sink's ONCE-PER-LOOP-PASS service — OUTSIDE the input drain, so it runs on every pass even
    // with no host byte waiting. It (a) terminates any line a writer left unterminated, so it can never fuse with the
    // next response, (b) hands the TX FIFO as much of the pending queue as it will take right now (this is what
    // delivers a response longer than the 128-B ESP32 FIFO — in order, gap-free, across passes), and (c) emits the
    // deferred `!! CONSOLE_DROP lines=N` once the queue drains. It never waits, flushes, delays or yields.
    mrcon.service();
}
#else
static void service_console() {}   // production (MR_CONSOLE=0): NO USB console — diagnostics over the air (BLE-NUS + rcmd + the fault-log)
#endif

// node_id DAD: persist the lease state (node_id + claim_epoch + joined) to /mrcfg WHEN it changes (adopt /
// epoch bump / forced rejoin), so a reboot keeps its id + seniority. Load-modify-save so the config fields
// (set via `cfg set`) are preserved. Cheap on the no-change path (3 compares); a flash write only on change.
static void persist_cfg_if_needed() {
    const uint8_t id = g_node.canonical_node_id(), ep = g_node.claim_epoch(), jn = g_node.joined() ? 1 : 0;   // §config-integrity: the canonical (layer0) id, NOT the active-leaf mirror — else a gateway's window switch flips `id` -> join_changed thrashes nv.node_id every ~8s and collapses both layers. Single-layer: canonical == _node_id (unchanged).
    const uint16_t cc = g_node.peer_ctr_high();                      // D7: the MAX ctr over ALL peers (self/channel counter is one of them) — lease covers DM ctrs too, not just channel
    const bool join_changed = (id != g_persist_id || ep != g_persist_epoch || jn != g_persist_join);   // DAD adopt/epoch/forced-rejoin — RARE, persist promptly
    const bool team_changed = g_node.team_local_id() != g_persist_team_local_id;   // §mobile 6.4: the team-DAD id (re-)assigned / re-picked / cleared -> persist promptly (a power-cycle keeps it)
    const bool lease_due    = (int16_t)(uint16_t)(cc - g_ctr_lease) > 0;   // Part 3: the live ctr PASSED the persisted lease -> re-lease (every ~margin sends). wraparound-safe signed diff
    if (!join_changed && !team_changed && !lease_due) return;
    // §nv-ritual: load-or-seed + stamp via THE shared prologue. This used to hand-roll the Blob seed field-by-field
    // — a second, silently-diverging copy of seed_blob_from_live that had ALREADY dropped `node_id`: exactly the
    // field-drop class the "one conversion path for the data carriers" rule exists to stop. Byte-identical swap:
    // node_id is the ONE extra field the canonical seed writes, and `b.node_id = id` below writes the same
    // g_node.canonical_node_id() over it; the magic/version stamp is the same one this site did inline.
    mrnv::Blob b{}; nv_load_stamped(b);
    const uint16_t leased = (uint16_t)(cc + kChannelCtrLeaseMargin);   // persist the ctr AHEAD: a reboot in the un-flushed window resumes here (> any id used) -> no reuse
    b.node_id = id; b.claim_epoch = ep; b.joined = jn; b.channel_ctr = leased;
    b.team_local_id = g_node.team_local_id();   // §mobile 6.4: persist the team-DAD id
    if (mrnv::save(b)) { g_persist_id = id; g_persist_epoch = ep; g_persist_join = jn; g_ctr_lease = leased; g_persist_team_local_id = g_node.team_local_id(); }
}

// Step 4 — idle light-sleep: halt the CPU until `deadline_ms` OR a radio/console IRQ. The radio stays in
// continuous RX, so a DIO1 RxDone (an incoming frame) wakes us; the next-timer deadline wakes us for a
// scheduled beacon/timeout. nRF52: WFE (errata-87 FPU clear first) — wakes on ANY event (RTC tick / DIO1 /
// USB), so the deadline is advisory + the console stays responsive. ESP32-S3: esp_light_sleep to the
// deadline or DIO1 (ext1). -DMR_NO_POWERSAVE compiles it out (busy-spin, for A/B power measurement).
#if !defined(MR_NO_POWERSAVE)
static void board_sleep_until([[maybe_unused]] uint64_t deadline_ms, [[maybe_unused]] uint64_t now_ms) {
#if defined(NRF52_SERIES) || defined(ARDUINO_ARCH_NRF52) || defined(BOARD_XIAO_WIO_SX1262)
  #if (__FPU_USED == 1)
    __set_FPSCR(__get_FPSCR() & ~(0x0000009Fu));   // nRF52 errata 87: stale FPU flags keep WFE awake ("insomnia")
    (void)__get_FPSCR(); NVIC_ClearPendingIRQ(FPU_IRQn);
  #endif
    uint8_t sd_on = 0; sd_softdevice_is_enabled(&sd_on);
    if (sd_on) sd_app_evt_wait();                  // SoftDevice: process pending events, then sleep
    else { __SEV(); __WFE(); __WFE(); }            // raw WFE (SEV+double-WFE clears any stale event)
#elif defined(ARDUINO_ARCH_ESP32) || defined(ESP32) || defined(BOARD_HELTEC_V3)
    if (rtc_gpio_is_valid_gpio((gpio_num_t)LORA_PIN_DIO1)) {   // only if DIO1 can wake from light-sleep
        esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
        esp_sleep_enable_ext1_wakeup((1ULL << LORA_PIN_DIO1), ESP_EXT1_WAKEUP_ANY_HIGH);   // DIO1 RxDone wakes us
        if (deadline_ms != UINT64_MAX) {
            const uint64_t dt = deadline_ms > now_ms ? (deadline_ms - now_ms) : 1;
            esp_sleep_enable_timer_wakeup(dt * 1000ULL);       // ...or the next-timer deadline
        }
        // NB: no UART wake source — light-sleep gates the UART clock, so a typed byte can't reliably wake us
        // (verified dead on the Heltec). We only reach here when NO host is present (loop()'s `may_sleep`), so
        // there's no console to strand; regain it by reconnecting (DTR resets -> a fresh, awake boot).
        esp_light_sleep_start();
    }
#endif
}
#endif  // !MR_NO_POWERSAVE

// ---- Radio-Module corruption canary (MR_RADIO_CANARY): re-check + on the FIRST trip record DURABLY (the fault-log —
// this node's USB is dying) + print + reset CLEANLY (don't ride the corruption into the opaque SPItransferStream
// crash). No-op when MR_RADIO_CANARY=0. Defined HERE (after g_iradio/mrcon/mrfault are declared). spec 2026-06-25.
#if MR_RADIO_CANARY
static const char* canary_where_name(uint8_t w) {
    switch (w) {
        case CW_loop_top: return "loop_top";         case CW_poll_rx: return "after_poll_rx"; case CW_tx_done: return "after_tx_done";
        case CW_node_tick: return "after_node_tick"; case CW_console:  return "after_console"; case CW_ble:     return "after_ble";
        case CW_nv:        return "after_nv";         case CW_sched:    return "after_sched";   case CW_noise:   return "after_noise";
        default:           return "?";
    }
}
#endif
static void canary([[maybe_unused]] uint8_t where) {
#if MR_RADIO_CANARY
    const int off = g_iradio.radio_canary_check();
    if (off < 0) return;
    const uint32_t before = g_iradio.radio_canary_before(off), after = g_iradio.radio_canary_after(off);
    mrcon.print(F("\nCANARY @"));
    if (where >= 100) { mrcon.print(F("timer")); mrcon.print(where - 100); }   // ADDENDUM: a per-timer-id trip (where = 100+id)
    else                mrcon.print(canary_where_name(where));
    mrcon.print(F(" off=")); mrcon.print(off);
    mrcon.print(F(" 0x")); mrcon.print(before, HEX); mrcon.print(F("->0x")); mrcon.println(after, HEX); mrcon.flush();
    mrfault::radio_canary_record(where, (uint16_t)off, before, after);   // durable: `faults`/`rcmd faults` -> CANARY @<where>/timer id=N off=N before->after
    #if defined(NRF52_SERIES) || defined(ARDUINO_ARCH_NRF52) || defined(BOARD_XIAO_WIO_SX1262)
    NVIC_SystemReset();
    #elif defined(ARDUINO_ARCH_ESP32) || defined(ESP32) || defined(BOARD_HELTEC_V3)
    ESP.restart();
    #endif
#endif
}
// ADDENDUM 2026-06-25: the coarse canary named CW_node_tick (the timer drain) as the corruptor. canary_timer() is the
// FINER check — call it after EACH g_node.on_timer(id) so the trip records the EXACT timer id (where = 100+id; the
// formatter prints `timer id=N`). No-op when MR_RADIO_CANARY=0.
static inline void canary_timer([[maybe_unused]] uint32_t id) {
#if MR_RADIO_CANARY
    canary(static_cast<uint8_t>(100 + id));
#endif
}

// §stability (2026-07-11): one pass of the mesh service (RX drain / timers / tx / console / BLE / persist / sleep). Was
// the body of loop(); extracted so it can run in a DEDICATED 8 KB task on nRF52 (see the loop() variants at the bottom) —
// the 4 KB Arduino loop stack overflows on the deepest RX path (hash-locate -> RREQ route-discovery + do_post_ack), bench
// stackhw fell to 72 B -> HARDFAULT. Behaviour is byte-identical to the old loop(); only the STACK it runs on changes.
static void mesh_service_once() {
    const uint64_t now = g_hal.now();
#if defined(MRFAULT_HW)
    mrfault::fault_wdt_feed();                       // kick the 8 s watchdog; a hang freezes the loop -> DOG reset + auto-recovery
    mrfault::fault_scratch_alive((uint32_t)now);     // refresh the retained moment-of-death stamp (free; survives the reset)
#endif

    // `prep-restart` halt: skip the WHOLE operating block (RX/timers/tx/beacon/push) while dormant. The WDT-feed above
    // + service_console/BLE below still run, so the deliberate halt is NOT a hang and the node stays console-responsive.
    if (!g_halted) {
    canary(CW_loop_top);                                         // radio-Module canary checkpoints (no-op unless MR_RADIO_CANARY)
    // 1) RX: drain received frames into the Node (+ the preamble-detect throttle/LBT witness).
    size_t len = 0; float snr = 0, rssi = 0;
    while (g_iradio.poll_rx(g_rxbuf, sizeof(g_rxbuf), len, snr, rssi)) {
        // Bring-up visibility: a frame physically arrived (proves the two radios hear each other).
        // Only fires on an actual RX, so it's low-noise. cmd nibble = high 4 bits of byte 0 (§10 wire).
        ++g_rx_count;
        meshroute::mr_trace_frame(/*is_rx=*/true, g_rxbuf, len, g_iradio.rx_sf(), snr, rssi, (uint32_t)g_hal.now());  // per-frame time
        meshroute::RxMeta meta{ snr, rssi, now, /*src_hint=*/(int16_t)-1 };   // LoRa carries no PHY src; Node derives it
        g_node.on_recv(g_rxbuf, len, meta);
    }
    if (g_iradio.take_preamble()) g_node.on_preamble_detected(now);
    canary(CW_poll_rx);

    // ★★★ 1b) TX COMPLETION — COLLECTED **BEFORE** THE TIMERS (§T3 §2.1), and the order is the feature, not a tidy-up.
    //     `kMBcastClearTimerId` deletes `_pending_tx` 5 ms after the calculated M-frame airtime, and the `send_aired`
    //     rule needs that flight ALIVE to attribute the airing to the origination that owns it. With the collection
    //     after the timer loop, a loop pass delayed past both deadlines fired the M-clear FIRST and the channel post's
    //     completion was lost outright — the ordinary failure mode on a loaded node.
    // ⛔ Do NOT move this below the timer drain, and ⛔ do NOT merge it back with `pump_tx()` at (2b): the pump must
    //    stay AFTER the timers so a frame a timer enqueued still departs on this pass. `tools/probe_board_ui`'s W21
    //    pins both halves and their order, because `fw_main.cpp` is outside every host build.
    g_hal.collect_tx_completion();
    for (meshroute::TxOutcome outcome; g_hal.pop_tx_outcome(outcome); ) g_node.on_tx_complete(outcome);

    // 2) Timers: fire every elapsed Node timer (beacons, RTS/ACK timeouts, retries, the duty/LBT defers).
    for (int id; (id = g_hal.pop_due_timer()) >= 0; ) { g_node.on_timer((uint32_t)id); canary_timer((uint32_t)id); }   // ADDENDUM: per-timer-id fine canary -> names the exact handler that corrupts the HAL

    // 2a) Gateway visibility (`debug on`): a dual-layer gateway alternates which leaf it LISTENS on per the window
    //     schedule (window_switch_fire, a timer above). Announce each switch — active layer/leaf/node_id/routing_sf
    //     + the data-SF set — so the operator sees the cadence inline with the RX/TX trace. Single-layer never switches.
    if (meshroute::g_mr_trace_on && g_node.config().n_layers == 2) {
        static uint8_t s_last_layer = 0xFF;
        const uint8_t cur = g_node.active_layer_id();
        if (cur != s_last_layer) {
            s_last_layer = cur;
            const meshroute::NodeConfig& c = g_node.config();
            mrcon.print(F("\n t=")); mrcon.print((uint32_t)now); mrcon.print(F(" ms [gw] now LISTENING layer="));
            mrcon.print(cur);                  mrcon.print(F(" leaf="));       mrcon.print(c.leaf_id);
            mrcon.print(F(" node_id="));       mrcon.print(g_node.node_id());
            mrcon.print(F(" routing_sf="));    mrcon.print(c.routing_sf);
            mrcon.print(F(" data_sf="));       print_sf_list(mrcon, c.allowed_sf_bitmap);
            mrcon.println();   // (was Serial.flush() — dropped Part 3: a loop-body flush only risks a stall; the USB task drains the FIFO)
        }
    }

    // 2b) Async TX: start the next queued frame. AFTER RX + timers, since both enqueue TX — so a frame a timer
    //     enqueued departs on this same pass. The loop stays live during a long TX (no freeze).
    //     ⓘ The COMPLETION half moved to (1b) above; see the §T3 §2.1 note there for why.
    g_hal.pump_tx();
    canary(CW_tx_done);

    // 2b2) Firmware scheduled-send (testsend/testch): fire the next DUE entry through the REAL send path so it rides
    //      normal routing/duty/ACK. One per loop + gated on tx-queue SPACE (enqueue_data silently drops a full queue,
    //      node_mac.cpp:159) so a burst respects backpressure (counted `deferred` when it fires late); an entry overdue
    //      past the slack window while the queue stays full is DROPPED (visible in teststatus, never silent). The body
    //      is the harness tag + `@<sendms>` stamped at THIS instant (latency truth). Halt-gated (in the operating block).
    if (g_sched.armed() > 0 && !g_sched.done()) {
        const uint32_t mnow = (uint32_t)now;
        const int si = g_sched.next_due(mnow);
        if (si >= 0) {
            // Queue-gate ONLY DMs: enqueue_data silently drops a DM on a full tx_queue, but do_send_channel always
            // BUFFERS a channel post (repair digest is the delivery backstop) so it never hard-fails on a full queue.
            const bool dm = !(g_sched.items[si].flags & mrsched::kChannel);
            if (dm && g_node.tx_queue_full()) {
                if (g_sched.overdue(si, mnow)) g_sched.mark_dropped(si);   // sustained-full -> give up (don't snowball)
            } else {
                char body[48];
                const uint8_t blen = mrsched::build_body(body, sizeof body, g_sched.run, g_node.node_id(),
                                                         g_sched.items[si].seq, mnow);
                meshroute::Command cmd{};
                const uint8_t fl = g_sched.items[si].flags;
                if (fl & mrsched::kChannel) {
                    cmd.kind = meshroute::CmdKind::send_channel;
                    cmd.u.channel.channel_id = (uint8_t)g_sched.items[si].target;
                } else {
                    cmd.kind = meshroute::CmdKind::send;
                    // Match console_parse EXACTLY: dst_id (id) XOR dst_hash (8-hex; on_command routes by dst_hash!=0,
                    // NO DST_HASH flag here — enqueue_data sets it). flags = ack only; -e -> crypt=on (hash dst only).
                    if (fl & mrsched::kHash) cmd.u.send.dst_hash = g_sched.items[si].target;   // dst_id stays 0 (Command{} zero-init)
                    else                     cmd.u.send.dst_id   = (uint8_t)g_sched.items[si].target;
                    cmd.u.send.flags = (fl & mrsched::kAck) ? meshroute::DATA_FLAG_E2E_ACK_REQ : 0;
                    cmd.crypt = (fl & mrsched::kEnc) ? meshroute::CryptIntent::on : meshroute::CryptIntent::def;
                }
                cmd.body = (const uint8_t*)body; cmd.body_len = blen;
                const meshroute::CmdResult r = g_node.on_command(cmd);
                // NB on_command returns `queued` even for the rare enqueue_data EARLY-OUTs (an unsynced managed joiner;
                // an -e seal-fail with no authoritative pubkey) — those count `fired` here though no DM aired. The
                // DURABLE INBOX is the delivery truth (spec); teststatus `fired` = "handed to the send path". For the
                // headline workload (plain DMs on a provisioned node) neither early-out can trigger.
                // ★★ §b39 — BUT THAT LAST SENTENCE IS SCOPED TO THE **DM** ARM, and the `kChannel` arm above is NOT
                // covered by it: a channel post blocked by this node's own min_interval / cap answers `queued` with
                // `ctr == 0` and nothing airs — and a `testch` workload posting on a short period is precisely how that
                // gate gets hit, so it is the LIKELY case here, not a rare one. `ctr != 0` is the "a ctr was minted"
                // test (next_ctr never mints 0); see the contract on Node::on_command's send_channel return. NOT
                // changed here (C1 — this slice documents the seam): a `fired`-vs-`blocked` split for the harness is
                // its own edit, and `send_blocked` telemetry already names the reason on the sim side.
                if (r.code == meshroute::CmdCode::queued) g_sched.mark_fired(si, mnow);
                else                                       g_sched.mark_dropped(si);   // a permanent error (unprovisioned/no_data_sf/…) -> retry won't help
            }
        }
    }
    canary(CW_sched);

    // 2c) LBT noise-floor sampler (only when LBT is on — it feeds channel_busy()). Self-paced (≤1 RSSI/10 ms).
    if (g_node.config().lbt_enabled) g_iradio.sample_noise();
    canary(CW_noise);

    // 2d) Inbox meta: COALESCED slow persist (InternalFS self-heal Part 3, 2026-06-24). Inbox::flush() is now a
    //     no-op unless a store actually appended, so this writes /mri_* only when the cursor advanced — at a relaxed
    //     120 s cadence (was 30 s, unconditional) to cut the InternalFS write rate (the corruption window). Records
    //     live on QSPI; a power-loss costs ≤ one cycle of cursor advance, which the harness re-pull tolerates.
    static uint32_t s_nv_flush_ms = 0;
    if ((uint32_t)now - s_nv_flush_ms >= 120000u) { s_nv_flush_ms = (uint32_t)now; g_node.inbox().flush(); }
    canary(CW_nv);

    // 3) App pushes: surface deliveries / ACKs over the console.
    meshroute::Push pu{};
    while (g_node.next_push(pu)) {
        mr_ui_on_push(pu);   // §featuresplit slice 4: surface the delivery/ACK on the board display (no-op unless MR_FEAT_OLED)
        // ★ ALL 17 PushKinds are rendered, and this switch is DELIBERATELY `default`-less so -Wswitch fails the build
        // when an 18th is added (owner ruling 2026-07-26; 6 kinds used to fall through and print NOTHING here, which is
        // BASELINE 25m's enum→string defect class — this file is invisible to the native gate, so the compiler is the
        // only tripwire). Case order tracks the enum declaration order in command.h.
        // ✔ IT WORKED, again: §team-ch-key T-K3 added the 15th kind (team_key_received) plus the 17th
        // SendFailReason (unsealable), and BOTH switches here failed the ESP32 build while `pio test -e native` was
        // green — fw_main.cpp is outside the native build, so these two `default`-less switches are the ONLY detector.
        switch (pu.kind) {
            case meshroute::PushKind::msg_recv:
                mrcon.print(F("RECV from="));   mrcon.print(pu.origin); mrcon.print(F(": "));
                mrcon.write(pu.body, pu.body_len); mrcon.println();
                break;
            case meshroute::PushKind::channel_recv:
                mrcon.print(F("CH ")); mrcon.print(pu.channel_id);
                if (pu.enc) mrcon.print(F(" [enc]"));   // §chan-crypt CL2a: the post arrived SEALED under the team content key and was opened here
                mrcon.print(F(" from=")); mrcon.print(pu.origin);
                // ★ §chan-crypt CL2c — the operator-facing half of the app contract, and the ONLY place a bench
                // operator can read either value back (fw_main is outside the native gate and the corpus is blind to
                // the whole sealed plane). `src=0x…` is the sealed inner's source_hash, spelled exactly as
                // firmware_commands.cpp's `hashof`/`nameof`/`peers` print a key_hash32 (U3 — `0x` prefix, uppercase,
                // no padding); `loc=` is its position, matching the `peers` row's `loc=lat,lon`. Both omitted when
                // absent, so a plaintext post's line is unchanged.
                if (pu.sender_hash) { mrcon.print(F(" src=0x")); mrcon.print(pu.sender_hash, HEX); }
                if (pu.has_location) { mrcon.print(F(" loc=")); mrcon.print(pu.lat_e7); mrcon.print(','); mrcon.print(pu.lon_e7); }
                mrcon.print(F(": "));
                mrcon.write(pu.body, pu.body_len); mrcon.println();
                break;
            case meshroute::PushKind::send_acked:
                mrcon.print(F("ACKED ctr="));    mrcon.println(pu.ctr); break;
            // ★ §T3: the frame PHYSICALLY LEFT THE RADIO. ⛔ Deliberately NOT worded as a delivery — the send-level
            // outcome (ACKED / E2EACK / FAILED / CHSENT) still follows and is the authoritative one. `dst=0` means a
            // channel post, whose `ctr` is the local 16-bit correlation handle rather than a DM counter.
            case meshroute::PushKind::send_aired:
                mrcon.print(F("AIRED ctr="));    mrcon.print(pu.ctr);
                mrcon.print(F(" dst="));         mrcon.println(pu.dst); break;
            case meshroute::PushKind::send_failed:
                mrcon.print(F("FAILED ctr="));   mrcon.print(pu.ctr);
                switch (pu.reason) {   // §mobile/§3-A.5: surface WHY so a fail-loud is actionable — render EVERY reason (was a bare "FAILED" for 6 of them)
                    case meshroute::SendFailReason::mobile_no_home:       mrcon.print(F(" (mobile not registered — no home to route the reply; register first)")); break;
                    case meshroute::SendFailReason::no_pubkey:           mrcon.print(F(" (no recipient pubkey)")); break;
                    case meshroute::SendFailReason::no_identity:         mrcon.print(F(" (no crypto identity)")); break;
                    case meshroute::SendFailReason::too_large:           mrcon.print(F(" (payload too large)")); break;
                    case meshroute::SendFailReason::joining:             mrcon.print(F(" (joining — config not synced)")); break;
                    case meshroute::SendFailReason::bad_rng:             mrcon.print(F(" (RNG failure)")); break;
                    case meshroute::SendFailReason::no_route:            mrcon.print(F(" (no route to destination)")); break;
                    case meshroute::SendFailReason::cap:                 mrcon.print(F(" (send cap reached — try later)")); break;
                    case meshroute::SendFailReason::min_interval:        mrcon.print(F(" (sending too fast — try later)")); break;
                    case meshroute::SendFailReason::no_cts:              mrcon.print(F(" (no CTS — next hop silent)")); break;
                    case meshroute::SendFailReason::no_ack:              mrcon.print(F(" (no ACK — delivery unconfirmed)")); break;
                    case meshroute::SendFailReason::gateway_unreachable: mrcon.print(F(" (gateway unreachable — timed out)")); break;
                    case meshroute::SendFailReason::e2e_ack_timeout:     mrcon.print(F(" (no end-to-end ack in time — delivery UNCONFIRMED, a late ack still resolves)")); break;
                    case meshroute::SendFailReason::queue_full:          mrcon.print(F(" (defer queue full — retry shortly)")); break;
                    case meshroute::SendFailReason::reprovisioned:       mrcon.print(F(" (node reprovisioned — send discarded; the old network's ids are void, re-address before resending)")); break;
                    // ★ §chan-crypt CL1 appends the send_channel clause. This string is the ONLY operator-facing "why"
                    // for a channel `-e` refusal (the sync line prints the bare `err_unsupported` token, and the push
                    // reason is deliberately the SHARED `unsealable` enumerator — U1, no new enumerator), so the
                    // `-t -g` case MUST state that the clear global copy is what defeats the seal. Without that an
                    // operator reads it as an arbitrary limitation and works around it by dropping `-e` instead of `-g`.
                    case meshroute::SendFailReason::unsealable:          mrcon.print(F(" (this may travel ONLY sealed, and this route cannot carry it sealed — for `-l` use -e / `cfg set e2e_dm 1` (and NOT send_layer/cross-layer); for a team key grant, grant over `-t` or from the target's own layer; for send_channel -e the ONLY sealable form is `-t -e` ON A NODE THAT HOLDS THE TEAM CONTENT KEY — a GLOBAL channel has no key, `-t -g -e` is refused because the global copy would air the SAME text in the CLEAR (drop `-g`, not `-e`), and a member with no key must be GRANTED one (`team grantkey` from any keyholder, or the team QR): membership is not readership)")); break;   // §team-ch-key T-K3 + §loc-per-send + §chan-crypt CL2a
                    case meshroute::SendFailReason::no_location:         mrcon.print(F(" (-l asked to attach a position and this node has NO fix — set `cfg set lat`/`lon`, or wait for GPS; NOT an encryption problem)")); break;   // §loc-per-send
                    case meshroute::SendFailReason::none:                break;   // not a send_failed reason
                }
                mrcon.println(); break;
            case meshroute::PushKind::send_e2e_acked:   // the END-TO-END ack arrived (dest confirmed) — distinct from the hop ACK
                mrcon.print(F("E2E-ACKED ctr=")); mrcon.print(pu.ctr); mrcon.print(F(" from=")); mrcon.println(pu.dst); break;
            case meshroute::PushKind::hash_resolved: {
                const uint32_t hash = (uint32_t)pu.body[0] | ((uint32_t)pu.body[1] << 8)
                                    | ((uint32_t)pu.body[2] << 16) | ((uint32_t)pu.body[3] << 24);
                if (pu.origin == 0) { mrcon.print(F("UNRESOLVED 0x")); mrcon.print(hash, HEX); mrcon.println(F(" (timeout)")); }
                else { mrcon.print(F("RESOLVED 0x")); mrcon.print(hash, HEX);
                       mrcon.print(F(" -> id=")); mrcon.print(pu.origin);
                       mrcon.println(pu.dst ? F(" (auth)") : F(" (cached)")); }
                break;
            }
            case meshroute::PushKind::peer_key_cached: {  // E2E §7: a recipient's pubkey was learned on-air -> an `-e` send to that hash can now be sealed
                // §AB1: mirror it into /mrpeers so the ability to seal to this peer SURVIVES A REBOOT (v1 kept
                // on-air keys RAM-only, so every reboot cost a manual `reqpubkey` per peer). An on-air learn is
                // stored `authoritative` and is NEVER PROMOTED to pinned — but peer_store_sync reads the confidence
                // back out of the LIVE table, so if this hash was ALREADY pinned (a QR import) the record keeps its
                // `pinned` level rather than being demoted. (⚠ V1: this note used to read "stored at authoritative"
                // flatly, which was true only of a never-pinned hash.) peer_store_sync returns `unchanged` — and
                // writes no flash — when the record is already byte-identical, which is what makes a cache-on-pass
                // re-learn free. Its outcome is REPORTED, not swallowed: `refused_full` means the book is full of
                // pinned keys and this key will NOT survive the reboot (C2).
                const mrnv::PeerPut kept = mrfw::peer_store_sync(pu.sender_hash);
                mrcon.print(F("KEY CACHED hash=0x")); mrcon.print(pu.sender_hash, HEX);
                if (pu.body_len) { mrcon.print(F(" name=")); mrcon.write(pu.body, pu.body_len); }   // §S6: the name cached alongside the key (omitted when unknown)
                // ★ §AB2: the level, not a hardcoded label. This line said "(on-air, unpinned)" UNCONDITIONALLY — the
                // SAME defect as the JSON push's hardcoded `"pinned":false`, on the sibling surface: a QR-PINNED peer
                // whose key also arrives on air reaches here (peer_key_set no-ops but still returns true), and the
                // console then told the operator its verified contact was unpinned. Now it prints the real confidence.
                mrcon.print(F(" conf=")); mrcon.print(meshroute::console::peerkeyconf_name(
                        static_cast<meshroute::Node::PeerKeyConf>(pu.peer_conf)));
                mrcon.print(F(" nv=")); mrcon.println(mrnv::peer_put_name(kept));
                break;
            }
            case meshroute::PushKind::config_adopted: {   // R6.2: a pulled leaf config was adopted -> persist to NV
                const meshroute::NodeConfig& nc = g_node.config();
                // NB deliberately NOT the §nv-ritual prologue: this is load-IF-PRESENT, not load-or-seed. With no
                // persisted blob it must write NOTHING (the adopted config is already live; minting a record here
                // would persist a leaf config onto a node that was never provisioned).
                mrnv::Blob b{};
                if (mrnv::load(b)) {
                    b.lineage_id = nc.lineage_id; b.config_epoch = nc.config_epoch;
                    b.allowed_sf_bitmap = nc.allowed_sf_bitmap; b.duty = nc.duty_cycle;
                    b.channel_active_fraction = nc.channel_active_fraction; b.channel_min_interval_ms = nc.channel_min_interval_ms; b.dm_min_interval_ms = nc.dm_min_interval_ms;   // v16: persist the adopted anti-spam knobs (they're in the C-frame)
                    b.leaf_name_len = nc.leaf_name_len;
                    for (uint8_t i = 0; i < nc.leaf_name_len && i < sizeof(b.leaf_name); ++i) b.leaf_name[i] = (uint8_t)nc.leaf_name[i];
                    b.magic = mrnv::kMagic; b.version = mrnv::kVersion;
                    // §nv-unchecked [2/5]: KNOWN UNCHECKED SAVE, preserved deliberately (see [1/5]). The adopted
                    // config is LIVE either way; a failed write means the next reboot silently re-pulls the old
                    // epoch, and the "LEAF-CONFIG adopted" line below still prints. Owner ruling owed.
                    (void)mrnv::save(b);
                }
                mrcon.print(F("LEAF-CONFIG adopted lineage=")); mrcon.print(nc.lineage_id);
                mrcon.print(F(" epoch=")); mrcon.println(nc.config_epoch);
                break;
            }
            case meshroute::PushKind::join_refused:        // R6.3 §7c: refusal feedback (telemetry is invisible on metal)
                if (pu.join_reason == meshroute::JoinRefuseReason::wire_version) {
                    mrcon.print(F("⚠ JOIN REFUSED: network wire v")); mrcon.print(pu.origin);
                    mrcon.print(F(", this node v")); mrcon.print(pu.dst); mrcon.println(F(" — update firmware"));
                } else if (pu.join_reason == meshroute::JoinRefuseReason::phy_mismatch) {   // §3-A.1/P2-1: team PHY-mismatched home skipped
                    mrcon.print(F("⚠ HOME REFUSED: PHY differs from the team's (layer ")); mrcon.print(pu.layer_id);
                    mrcon.print(F(", sf ")); mrcon.print(pu.dst); mrcon.println(F(") — staying off-grid-but-team-reachable"));
                } else if (pu.join_reason == meshroute::JoinRefuseReason::sf_list_mismatch) {   // §3-A.1 advisory (still adopted)
                    mrcon.print(F("⚠ SF-LIST MISMATCH vs home: ours=0x")); mrcon.print(pu.origin, HEX);
                    mrcon.print(F(" offered=0x")); mrcon.print(pu.dst, HEX); mrcon.println(F(" — check sf_list on both"));
                } else {
                    mrcon.println(F("⚠ JOIN REFUSED: leaf full — no id available"));
                }
                break;
            case meshroute::PushKind::send_blocked:   // Slice 6a: our OWN cap / min-interval gate refused an origination pre-TX
                mrcon.print(F("BLOCKED ")); mrcon.print(pu.blocked_channel ? F("channel") : F("dm"));
                // U1: the ONE reason mapper (-Wswitch-guarded + pinned by the all-enumerators test) — never a second hand-rolled table here
                mrcon.print(F(" reason=")); mrcon.print(meshroute::console::sendfailreason_name(pu.reason));
                mrcon.print(F(" — retry in ")); mrcon.print(pu.next_ms); mrcon.println(F(" ms")); break;
            case meshroute::PushKind::channel_sent:   // Slice 6c: outcome of an OWN channel post's origin re-offer
                mrcon.print(F("CH SENT ctr=")); mrcon.print(pu.ctr);
                mrcon.println(pu.relayed ? F(" (relayed)") : F(" (no relay)")); break;
            case meshroute::PushKind::mobile_reg:   // §S2: registration changed. relayed=true -> registered/roamed (home_layer+epoch valid);
                                                    //   false -> home lost or deregistered (node_mobile.cpp:247 / node.cpp:350 — home/local are 0 there)
                if (pu.relayed) {
                    mrcon.print(F("MOBILE REGISTERED home=")); mrcon.print(pu.origin);
                    mrcon.print(F(" local=")); mrcon.print(pu.dst);
                    mrcon.print(F(" layer=")); mrcon.print(pu.layer_id);
                    mrcon.print(F(" epoch=")); mrcon.println(pu.ctr);
                } else {
                    mrcon.println(F("⚠ MOBILE UNREGISTERED — home lost / deregistered"));
                }
                break;
            case meshroute::PushKind::team_reg:   // §S2: team-DAD local id adopted / re-picked after a conflict
                mrcon.print(F("TEAM id=0x")); mrcon.print(pu.team_id, HEX);
                mrcon.print(F(" local=")); mrcon.println(pu.dst); break;
            case meshroute::PushKind::join_adopted:   // a static/DAD adopt landed -> this node's OWN id may have changed
                mrcon.print(F("ADOPTED id=")); mrcon.print(pu.dst);
                mrcon.print(F(" layer=")); mrcon.print(pu.layer_id);
                mrcon.print(F(" epoch=")); mrcon.println(pu.ctr); break;
            case meshroute::PushKind::team_key_received:   // §team-ch-key T-K3: a teammate GRANTED us the team CONTENT key (already adopted)
                mrcon.print(F("TEAM KEY RECEIVED team=0x")); mrcon.print(pu.team_id, HEX);
                mrcon.print(F(" from=0x")); mrcon.print(pu.sender_hash, HEX);
                if (pu.body_len) { mrcon.print(F(" name=")); mrcon.write(pu.body, pu.body_len); }   // the granter's optional label (NOT persisted)
                mrcon.println(F(" — this node can now read the team channel")); break;   // ⚠ the KEY itself is never printed; `team exportkey` is its one disclosure
            case meshroute::PushKind::team_channel_no_key:   // §chan-crypt CL2a: an ENCRYPTED team post arrived that this node cannot read. Rate-limited node-side, so this line is a prompt, not a flood.
                mrcon.print(F("CH ")); mrcon.print(pu.channel_id);
                mrcon.print(F(" from=")); mrcon.print(pu.origin);
                mrcon.print(F(" team=0x")); mrcon.print(pu.team_id, HEX);
                mrcon.println(F(": ENCRYPTED — no team content key (ask a teammate to `team grantkey <your 0xhash>`, or scan the team QR). The post was still RELAYED."));
                break;
        }
        // BLE companion: the structured NDJSON twin of the plain-text line above (design doc §4). The ring is
        // drained ONCE here and fanned to both sinks — formatting + TX happen only when a phone is connected,
        // and the whole block is inert (write_push never called) off-XIAO or with no client.
        if (mrble::connected()) {
            // Sized for the TRUE worst case: a 241-B body (max_payload_bytes_hard_cap) of all-control chars
            // escapes 6x (\uXXXX, console_json.cpp), i.e. ~1446 B + the field envelope (now incl. channel_msg_id /
            // sender_hash, ~90 B). 1700 keeps a comfortable margin (1536 was an exact-fit after the Phase-3 fields)
            // so a valid Push NEVER overflows. static (not stack) to keep it off the hot-path stack; bleuart chunks.
            // Reuse the inbox-pull scratch (s_inbox_jb): push-drain and pull_inbox run at different loop
            // phases, never concurrently (single-threaded), so one shared 1700-B line buffer suffices (−1.7 KB).
            const size_t n = meshroute::console::write_push(s_inbox_jb, sizeof s_inbox_jb, pu, &g_node.config());   // cfg: config_adopted membership (R6.3)
            if (n) mrble::tx_line(s_inbox_jb, n);
            else { static const char kOvf[] = "{\"err\":\"push_encode_overflow\"}\n";   // unreachable for valid
                   mrble::tx_line(kOvf, sizeof(kOvf) - 1); }                            // input; LOUD, never silent
        }
    }
    mr_ui_tick((uint32_t)now);   // §featuresplit slice 4: periodic board-display refresh (no-op unless MR_FEAT_OLED; throttled inside)
    // (was Serial.flush() — dropped Part 3: the Adafruit USB task drains the FIFO; a loop-body flush only risks a stall)

    // OTA remote diagnostics: drain the inbound rcmd slot — a response PRINTS (parseable line for the harness), a
    // command EXECUTES here on the main loop (never the RX path). static = the ~244 B slot is off the hot-path stack.
    { static meshroute::Node::RemoteInbound ri;
      if (g_node.take_remote_inbound(ri)) {
          if (ri.is_response) {
#if MR_FEAT_REMOTE_MGMT
              if (ri.len >= 1 && ri.body[0] == REMOTE_FLAG_SEALED && g_admin_unlocked) {   // sealed ack/hint -> open with the admin key
                  const uint32_t sh = g_node.key_hash_for_id(ri.from); uint8_t spk[32];
                  meshroute::AdminCmd ac{}; static uint8_t pt[241];
                  if (sh && g_node.peer_key_find(sh, spk) && meshroute::admin_cmd_open(ri.body + 1, ri.len - 1, spk, g_admin_id, ac, pt, sizeof pt)) {
                      if (ac.cmd_len > 6 && !memcmp(ac.cmd, "floor=", 6)) {                // reject-hint: bump our tx counter past N; the command did NOT run
                          char nb[16]; uint8_t k = (uint8_t)(ac.cmd_len - 6 < 15 ? ac.cmd_len - 6 : 15); memcpy(nb, ac.cmd + 6, k); nb[k] = '\0';
                          uint32_t fl = (uint32_t)strtoul(nb, nullptr, 10); if (fl >= g_admin_tx_ctr) g_admin_tx_ctr = fl + 1;
                          mrcon.print(F("[rcmd ")); mrcon.print(ri.from); mrcon.print(F("] not run — counter was stale, resynced to "));
                          mrcon.print(g_admin_tx_ctr); mrcon.println(F("; re-issue the command to run it"));
                      } else {                                                             // a real sealed response
                          mrcon.print(F("[rcmd ")); mrcon.print(ri.from); mrcon.print(F("] ")); mrcon.write(ac.cmd, ac.cmd_len); mrcon.println();
                      }
                  } else { mrcon.print(F("[rcmd ")); mrcon.print(ri.from); mrcon.println(F("] <sealed; open failed / no pubkey>")); }
              } else if (ri.len >= 2 && ri.body[0] == 0x01 /*console_binary TLV ver*/) {   // an OPEN read's unsealed TLV -> not decodable on-device
                  mrcon.print(F("[rcmd ")); mrcon.print(ri.from); mrcon.print(F("] <binary TLV, ")); mrcon.print(ri.len); mrcon.println(F(" B — decode with the host tool>"));
              } else
#endif
              { mrcon.print(F("[rcmd ")); mrcon.print(ri.from); mrcon.print(F("] ")); mrcon.write(ri.body, ri.len); mrcon.println(); }
          }
          else remote_exec(ri.from, ri.body, ri.len);
      } }
    // deferred recovery action (respond-first-then-act): fire reboot / prep-restart once its ~3 s defer elapses, so
    // the `ok …` response DM has aired first.
    if (g_remote_action && g_hal.now() >= g_remote_action_at) {
        const uint8_t act = g_remote_action; g_remote_action = 0;
        if (act == 1) do_reboot(); else handle_prep_restart(mrcon);
    }
    }  // end if (!g_halted) — the operating block

    // 4) Console input -> commands. A byte means a host is here -> latch awake so the console stays usable
    //    (service_console() drains Serial, so we must note it BEFORE; the sleep gate below honors the latch).
    if (serial_has_input()) g_host_present = true;
    service_console();
    canary(CW_console);

    // 4c) BLE companion: advance the advertising-window policy + drain inbound NUS lines (both inert off-XIAO).
    mrble::on_tick(now);
    mrble::service_rx();
    canary(CW_ble);

    #if defined(ARDUINO_ARCH_ESP32) || defined(ESP32) || defined(BOARD_HELTEC_V3)
    // 4c2) WiFi OTA server (Heltec ESP32 only; inert on XIAO): handle one HTTP client request.
    mrota::ota_loop();
    #endif 

    // 4b) Persist the DAD lease (adopt / epoch bump / forced rejoin) + re-lease the channel ctr when it catches up.
    persist_cfg_if_needed();

    // 5) Idle light-sleep: nothing pending -> halt the CPU until the next timer OR a radio/console IRQ.
    //    Capped at MR_MAX_SLEEP_MS so the console + periodic work stay responsive (matters on ESP32;
    //    nRF52 WFE wakes every RTC tick regardless). Gate: not mid-TX, no queued TX, no console input.
#if !defined(MR_NO_POWERSAVE)
    const uint64_t s_now = g_hal.now();
    // Sleep policy: a HEADLESS node (no host byte this boot, past the boot grace) light-sleeps when idle; an
    // explicit `sleep` command forces it even with a host present. A host that has typed latches us awake so
    // the console stays usable (ESP32 light-sleep would otherwise gate the UART and strand it). See MR_BOOT_GRACE_MS.
    const bool may_sleep = !g_halted && (g_force_sleep || (!g_host_present && s_now >= MR_BOOT_GRACE_MS));   // halted -> stay awake (console-responsive)
    // §B197/§B198: ...and the UI must have no objection — panel blanked, no gesture being classified, no page-buffer
    // frame open. Inlines to `true` off MR_FEAT_OLED (lib/hal/mr_ui.h), so no `#if` reaches this gate and a non-OLED
    // profile's behaviour is unchanged. ⚠ It constrains `g_force_sleep` too, deliberately: an explicit `sleep` on an
    // OLED build waits for the bounded UI work to finish rather than stranding a half-painted emergency screen.
    if (may_sleep && mr_ui_allows_sleep() &&
        !g_iradio.tx_busy() && g_hal.txq_depth() == 0 && !serial_has_input() && !mrble::connected()) {
        uint64_t due = g_hal.next_due_ms();                    // UINT64_MAX if no timer armed
        const uint64_t cap = s_now + MR_MAX_SLEEP_MS;
        if (due > cap) due = cap;
        ++g_sleep_count;                                       // count sleep entries (status `slept=`)
        board_sleep_until(due, s_now);
    }
#endif
}

// §stability (2026-07-11): WHERE mesh_service_once() runs.
// nRF52 (Adafruit): the Arduino loop task stack is a fixed 4 KB (LOOP_STACK_SZ = 256*4, cores/nRF5/main.cpp — not
//   overridable by a -D flag). The deepest mesh RX path (hash-locate answer -> RREQ route-discovery flood + do_post_ack
//   forwarding) nests ~1.4 KB, so `status stackhw=` fell to 72 B on the bench and the next nested frame HARDFAULTed
//   (cfsr=0x8200, wild BFAR). Run the mesh in a DEDICATED 8 KB FreeRTOS task; the 4 KB Arduino loop task then just idles.
//   Created lazily on the first loop() so it starts AFTER setup() finished radio/node/BLE init. `stackhw=` (uxTaskGet-
//   StackHighWaterMark of the CURRENT task) is read from the console handler, which now runs in the mesh task -> it
//   reports the 8 KB task's headroom (should read thousands free, confirming the fix).
#if defined(NRF52_SERIES) || defined(ARDUINO_ARCH_NRF52) || defined(BOARD_XIAO_WIO_SX1262)
TaskHandle_t g_mesh_task = nullptr;   // extern in fw_context.h (nRF52 FreeRTOS only)
static void mesh_task_fn(void*) {
    for (;;) { mesh_service_once(); yield(); }        // yield -> let the idle task run (FreeRTOS housekeeping / tickless idle)
}
void loop() {
    if (!g_mesh_task)                                 // lazy create: setup() has finished; inherit a fully-initialized world
        xTaskCreate(mesh_task_fn, "mesh", 8192 / sizeof(StackType_t), nullptr, tskIDLE_PRIORITY + 1, &g_mesh_task);
    delay(1000);                                      // the 4 KB Arduino loop task idles; the mesh runs in g_mesh_task
}
#else
void loop() { mesh_service_once(); }                  // ESP32 / other: the loopTask already has a large (~8 KB) stack
#endif
