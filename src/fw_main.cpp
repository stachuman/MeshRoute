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
#include "firmware_config.h"         // §cleanup 2026-07-14: config/provisioning cluster (Increment A: apply_radio_live so far)
using mrfw::remote_exec;             // keep call sites unchanged (mesh_service_once + dispatch)
using mrfw::handle_rcmd;
using mrfw::apply_radio_live;        // called by handle_cfg_set (moved) + fw_main's provision_apply_live (resident)
using mrfw::handle_cfg_set;          // dispatch verb (moved to firmware_config)
using mrfw::handle_gateway;
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

// ---- device-console diagnostics (host tool: tools/meshroute_client.py) ---------------------------
// Print the live routing table in the meshroute_client `routes` wire format.
// `route add <dest> <next_hop> <hops> [score_q4]` / `route del <dest>` — manually force / drop a route. A TESTING lever
// to stress the routing algorithms with arbitrary or deliberately-inconsistent routes. `score_q4` is the same Q4-dB
// value the `routes` dump shows; rt_merge competes the injected candidate like any learned one (high score -> primary).
static void handle_route_cmd(const char* args, Print& out) {
    while (*args == ' ') ++args;
    char* e;
    if (!strncmp(args, "add", 3) && (args[3] == ' ' || args[3] == '\0')) {
        const char* p = args + 3;
        const long dest = strtol(p, &e, 10); const bool d1 = (e != p); p = e;
        const long next = strtol(p, &e, 10); const bool d2 = (e != p); p = e;
        const long hops = strtol(p, &e, 10); const bool d3 = (e != p); p = e;
        long score = strtol(p, &e, 10); if (e == p) score = 160;          // optional; default ~10 dB (Q4) = a sticky primary
        if (d1 && d2 && d3 && dest >= 1 && dest <= 254 && next >= 1 && next <= 254 && hops >= 1 && hops <= 255) {
            const bool ok = g_node.route_inject((uint8_t)dest, (uint8_t)next, (uint8_t)hops, (int16_t)score);
            out.print(F("> route add dest=")); out.print(dest); out.print(F(" via=")); out.print(next);
            out.print(F(" hops="));            out.print(hops); out.print(F(" score=")); out.print(score);
            out.println(ok ? F(" — installed (see `routes`)") : F(" — REJECTED (better candidates hold the slots)"));
            return;
        }
    } else if (!strncmp(args, "del", 3) && (args[3] == ' ' || args[3] == '\0')) {
        const long dest = strtol(args + 3, &e, 10);
        if (e != args + 3 && dest >= 1 && dest <= 254) {
            const bool ok = g_node.route_remove((uint8_t)dest);
            out.print(F("> route del dest=")); out.print(dest); out.println(ok ? F(" — removed") : F(" — not found"));
            return;
        }
    }
    out.println(F("> route err usage: route add <dest> <next_hop> <hops> [score_q4] | route del <dest>"));
}

static void dump_routes(Print& out) {
    const uint64_t now = g_hal.now();
    //out.print(F("[routes] n=")); out.println(g_node.rt_count());
    if (!g_node.rt_count()) out.println(F("empty"));
    for (uint8_t i = 0; i < g_node.rt_count(); ++i) {
        const meshroute::RtEntry& e = g_node.rt_at(i);
        const meshroute::RtCandidate& c = e.candidates[0];           // candidates[0] = the primary next-hop
        out.print(F("[route] dest="));   out.print(e.dest);
        out.print(F(" next="));          out.print(c.next_hop);
        out.print(F(" hops="));          out.print(c.hops);
        out.print(F(" score="));         out.print(c.score);
        out.print(F(" pen="));           out.print(g_node.peer_penalty_q4(c.next_hop));   // liveness penalty on this next-hop (effective = score - pen)
        out.print(F(" gw="));            out.print(c.is_gateway ? 1 : 0);
        out.print(F(" leaf="));          out.print(c.learned_leaf);
        out.print(F(" age_ms="));        out.print((uint32_t)(now - c.last_seen_ms));
        out.print(F(" cand="));          out.println(e.n);
        // A gateway route carries unique state: its advertised window schedule (period + per-leaf windows) — known
        // when we've heard the gateway 1-hop. Print it on a continuation line so a node can see when the gw is reachable.
        if (c.is_gateway) {
            const meshroute::GatewaySchedule* gs = g_node.rt_gateway_schedule(e.dest);
            if (gs && gs->valid) {
                out.print(F("[route]   gw_sched period="));  out.print(gs->period_ms);
                out.print(F("ms heard_ms="));                out.print((uint32_t)(now - gs->heard_ms));
                out.print(F(" defer_ms="));                  out.print(g_node.rt_gateway_defer_ms(e.dest));
                out.print(F(" n_rec="));                     out.print(gs->n_rec);
                for (uint8_t r = 0; r < gs->n_rec; ++r) {
                    out.print(F(" [leaf"));   out.print(gs->rec[r].leaf_id);
                    out.print(F(" win"));     out.print(gs->rec[r].window_ms);
                    out.print(F("@"));        out.print(gs->rec[r].offset_ms);
                    out.print(F("]"));
                }
                out.println();
            } else {
                out.println(F("[route]   gw_sched unknown (not heard 1-hop)"));
            }
        }
    }
    //out.println(F("[routes] end"));
    // §mobile 6.2: the TEAM-plane routing table (_rt_team) — same RtEntry fields as the static plane; team routes are never
    // gateways (no gw schedule). Printed for ANY team member (team_id!=0) EVEN WHEN EMPTY (n=0) so the operator can tell a
    // team member with no peers-yet (a PHY mismatch / just-joined) from a non-team node — a static/non-team node prints nothing.
#if MR_FEAT_TEAM   // §featuresplit: the diagnostic is compiled out on a static-only build (no _rt_team)
    if (g_node.config().team_id != 0) {
        char tx[9]; snprintf(tx, sizeof tx, "%08lX", (unsigned long)g_node.config().team_id);
        out.print(F("team_id=0x")); out.print(tx);
        out.print(F(" team_local_id=")); out.print(g_node.team_local_id());
        out.print(F(" n=")); out.println(g_node.rt_team_count());
        for (uint8_t i = 0; i < g_node.rt_team_count(); ++i) {
            const meshroute::RtEntry& e = g_node.rt_team_at(i);
            const meshroute::RtCandidate& c = e.candidates[0];       // candidates[0] = the primary next-hop
            out.print(F("[team-route] dest="));   out.print(e.dest);
            out.print(F(" next="));                out.print(c.next_hop);
            out.print(F(" hops="));                out.print(c.hops);
            out.print(F(" score="));               out.print(c.score);
            out.print(F(" leaf="));                out.print(c.learned_leaf);
            out.print(F(" age_ms="));              out.print((uint32_t)(now - c.last_seen_ms));
            out.print(F(" cand="));                out.println(e.n);
        }

    }
#endif   // MR_FEAT_TEAM
    // §mobile: the hosted-mobile registry (this node is a HOME) — mobiles reachable by a DIRECT last-mile from here.
    // mobile_reg_count()==0 on a non-host / static build -> nothing printed.
    if (g_node.mobile_reg_count()) {
        out.print(F("hosted-mobiles n=")); out.println(g_node.mobile_reg_count());
        for (uint8_t i = 0; i < g_node.mobile_reg_count(); ++i) {
            uint32_t kh = 0; uint8_t lid = 0; bool hpk = false;
            if (!g_node.mobile_reg_at(i, kh, lid, hpk)) continue;
            char hx[9]; snprintf(hx, sizeof hx, "%08lX", (unsigned long)kh);
            out.print(F("[hosted-mobile] hash=0x")); out.print(hx);
            out.print(F(" local_id="));              out.print(lid);
            out.print(F(" pubkey="));                out.println(hpk ? F("yes") : F("no"));
        }
    }
}

// allowed_sf_bitmap -> "7,12" CSV (SF index = bit position). 0 = unconfigured.
static void print_sf_list(uint16_t bitmap) {
    bool first = true;
    for (uint8_t sf = 5; sf <= 12; ++sf)
        if (bitmap & (1u << sf)) { if (!first) mrcon.print(','); mrcon.print(sf); first = false; }
    if (first) mrcon.print('-');
}

static void dump_cfg(Print& out) {
    const meshroute::NodeConfig& c = g_node.config();
    // Grouped, one section per line — readable on a raw serial monitor. Keys match the `cfg set <key>` names.
    out.print(F("node_id="));     out.println(g_node.node_id());
    const double show_freq = (c.is_mobile && c.layers[0].freq_mhz > 0.0) ? c.layers[0].freq_mhz : g_freq_mhz;   // §mobile: a retune stores the live freq in layers[0]; g_freq_mhz stays the boot/global
    out.print(F("  radio : freq="));    out.print(show_freq, 4);
    out.print(F(" routing_sf="));       out.print(c.routing_sf);
    out.print(F(" sf_list="));          print_sf_list(c.allowed_sf_bitmap);
    out.print(F(" bw="));               out.print(g_node.active_bw_hz());   // the ACTIVE leaf's BW (a gateway alternates per window; single-layer == the global)
    out.print(F(" cr="));               out.print((int)g_node.active_cr());
    out.print(F(" tx_power="));         out.println((int)g_tx_power);
    out.print(F("  proto : duty="));    out.print(c.duty_cycle, 3);
    out.print(F(" beacon_ms="));        out.print(c.beacon_period_ms);
    out.print(F(" hop_cap="));          out.print(c.dv_hop_cap);
    out.print(F(" lbt="));              out.print(c.lbt_enabled ? 1 : 0);
    out.print(F(" nav="));              out.print(c.nav_enabled ? 1 : 0);
    out.print(F(" intra_relay="));      out.print(c.intra_layer_relay ? 1 : 0);   // §gateway: relay same-leaf DMs? (default OFF)
    out.print(F(" host_mobiles="));     out.print(c.host_mobiles ? 1 : 0);        // §mobile 2a: accept/host mobiles? (default ON)
    out.print(F(" nav_ignore="));       out.println(c.nav_ignore_rts ? 1 : 0);
    out.print(F("  aspam : active_fraction=")); out.print(c.channel_active_fraction, 3);   // anti-spam v2 promoted knobs (in the config_hash)
    out.print(F(" ch_min_ms="));        out.print(c.channel_min_interval_ms);
    out.print(F(" dm_min_ms="));        out.println(c.dm_min_interval_ms);
    out.print(F("  layer : "));                                            // R6.3 §3: the full 1..255 layer id (NV-side) + its wire leaf nibble (clash check)
    { mrnv::Blob lb{}; if (mrnv::load(lb) && lb.layer0_id) { out.print(F("layer=")); out.print(lb.layer0_id); out.print(F(" ")); } }
    out.print(F("leaf="));              out.print(c.leaf_id);            // leaf = layer & 0x0F (the byte-0 wire filter)
    out.print(F(" gateway="));          out.print(c.is_gateway ? 1 : 0);
    out.print(F(" gateway_only="));     out.print(c.gateway_only ? 1 : 0);
    out.print(F(" mobile="));           out.println(c.is_mobile ? 1 : 0);
    if (c.team_id) {   // §mobile 6.1/6.4: team plane — the team scope + OUR id on it. team_local_id 0 = not team-DAD'd yet
        char tx[9]; snprintf(tx, sizeof tx, "%08lX", (unsigned long)c.team_id);
        const uint8_t tid = g_node.team_local_id();
        out.print(F(" team=0x")); out.print(tx);
        out.print(F(" team_local_id=")); out.print(tid);
        // §6.4 Option X: off-grid the team-DAD'd id IS node_id (the mobile link-layer carries team DMs). Flag the plane state
        // so a bench operator can tell an off-grid member (node_id==team id) from a dual one (static node_id + separate team id).
        if (tid == 0)                       out.print(F(" (team-DAD pending)"));
        else if (g_node.node_id() == tid)   out.print(F(" (off-grid: node_id==team id)"));
        else                                out.print(F(" (dual: static node_id + team id)"));
        out.println();
    }
    if (c.is_mobile) {                                                        // §mobile: registration state (bench diagnostic) — did we register, and with whom?
        const uint8_t h = g_node.mobile_home_id();
        out.print(F("  mobile-reg: ")); if (h) { out.print(F("REGISTERED home=")); out.println(h); } else out.println(F("UNREGISTERED (scanning)"));
    }
    if (g_node.mobile_reg_count()) { out.print(F("  hosting=")); out.print(g_node.mobile_reg_count()); out.println(F(" mobile(s)")); }   // §mobile: this node is a host with registered mobiles
    out.print(F("  member: lineage_id=")); out.print(c.lineage_id);          // R6.1 leaf-config membership: 0 = UNMANAGED. A managed leaf (lineage!=0) only routes same-lineage peers -> a lineage-0 gateway is silently dropped (node_beacon.cpp:462). This is the field to compare across nodes.
    out.print(F(" config_epoch="));     out.print(c.config_epoch);
    if (c.leaf_name_len) { out.print(F(" leaf_name=\"")); for (uint8_t i = 0; i < c.leaf_name_len; ++i) out.print(c.leaf_name[i]); out.print(F("\"")); }
    out.println();
    out.print(F("  ble   : ble_mode=")); out.print(g_ble_mode == 0 ? F("off") : g_ble_mode == 1 ? F("on") : F("periodic"));
    out.print(F(" ble_period="));       out.print(g_ble_period_min);
    out.print(F(" ble_pin="));          out.println(g_ble_pin);
    // Arduino Print formats floats via its own dtostrf (NOT newlib printf), so 7-decimal degrees print fine.
    out.print(F("  loc   : loc_dm="));  out.print(c.loc_in_dm ? 1 : 0);
    out.print(F(" e2e_dm="));           out.print(c.e2e_dm ? 1 : 0);
    out.print(F(" lat="));              out.print(g_lat_e7 / 1e7, 7);
    out.print(F(" lon="));              out.println(g_lon_e7 / 1e7, 7);
    // Dual-layer gateway: an ADDITIVE second line per leaf (single-layer dump above is unchanged). Prints each
    // leaf's node_id/layer_id/routing_sf + the (possibly on_init-derived) window_ms/offset of the active config.
    if (c.n_layers == 2) {
        for (uint8_t li = 0; li < 2; ++li) {
            const meshroute::LayerConfig& L = c.layers[li];
            out.print(F("[cfg.layer")); out.print(li);
            out.print(F("] node_id="));    out.print(L.node_id);
            out.print(F(" layer_id="));    out.print(L.layer_id);
            out.print(F(" routing_sf="));  out.print(L.routing_sf);
            out.print(F(" sf_list="));     print_sf_list(L.allowed_sf_bitmap);
            out.print(F(" bw="));          out.print(L.bw_hz > 0 ? L.bw_hz : c.radio_bw_hz);   // per-layer BW (0 = inherit -> the effective/global)
            out.print(F(" cr="));          out.print((int)(L.cr > 0 ? L.cr : c.radio_cr));
            out.print(F(" beacon_ms="));   out.print(L.beacon_period_ms);
            out.print(F(" window_period_ms=")); out.print(L.window_period_ms);
            out.print(F(" window_ms="));   out.print(L.window_ms);
            out.print(F(" window_offset_ms=")); out.println(L.window_offset_ms);
        }
    }
}

static const char* board_name() {
#if defined(BOARD_XIAO_WIO_SX1262)
    return "xiao_nrf52";
#elif defined(BOARD_XIAO_ESP32S3)
    return "xiao_esp32s3";
#elif defined(BOARD_HELTEC_V3)
    return "heltec_v3";
#else
    return "native";
#endif
}
// The `version` banner — build stamp + git rev + board + the last reset reason (ON DEMAND, no reset). Refactored
// from the old boot prints so setup() and the `version` command share one source. (spec 2026-06-24 §6)
static void print_banner(Print& out) {
    char buf[160];
    mrfault::format_version_banner(buf, sizeof buf, __DATE__ " " __TIME__, GIT_REV, board_name());
    out.println(buf);
    mrfault::format_last_reset(g_last_reset_valid ? &g_last_reset : nullptr, buf, sizeof buf);
    out.println(buf);
}

void fw_wdt_feed() { mrfault::fault_wdt_feed(); }   // extern in fw_context.h — the WDT kick exposed for firmware_remote (device_fault.h's ISR vectors can't be pulled into a 2nd TU)

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

static void dump_status(Print& out) {
    out.print(F("uptime_ms="));  out.print((uint32_t)g_hal.now());
    out.print(F(" rx="));                 out.print(g_rx_count);
    out.print(F(" tx="));                 out.print(g_iradio.tx_count());
    out.print(F(" isr="));                out.print(g_iradio.isr_count());   // DIO1 edges — isr=0 ⇒ pin/mask; isr>0 & rx=0 ⇒ drain/re-arm
    out.print(F(" rxbad="));              out.print(g_iradio.rxbad_count());  // failed-decode RX (CRC storm) — a clean counter delta (per-event print is `debug on`-gated)
    out.print(F(" rxarm="));              out.print(g_iradio.rx_arm_failures());  // L5: startReceive() re-arm failures — non-zero = an SPI glitch left RX transiently un-armed (was silent before)
    out.print(F(" txq="));                out.print(g_hal.txq_depth());      // async-TX queue depth (should idle at 0)
    out.print(F(" txdrop="));             out.print(g_hal.txq_drops());      // outbound-queue overflow drops (should stay 0)
    out.print(F(" txto="));               out.print(g_hal.tx_timeouts());    // TX-watchdog recoveries — a missed TxDone (should stay 0)
    out.print(F(" slept="));              out.print(g_sleep_count);          // idle light-sleep entries — climbs = the gate fires (0 = never sleeps)
    out.print(F(" sleep="));              out.print(g_force_sleep ? F("forced") : (g_host_present ? F("off-host") : F("auto"))); // policy: auto=headless→sleeps, off-host=awake (host seen), forced=`sleep` cmd
    out.print(F(" lbt="));                out.print(g_node.config().lbt_enabled ? 1 : 0);
    out.print(F(" nf="));                 out.print(g_iradio.noise_floor(), 0); // LBT noise floor (dBm)
    out.print(F(" duty_ms="));            out.print((uint32_t)g_hal.airtime_used_ms(3600000));
    out.print(F(" routes="));             out.print(g_node.rt_count());
    out.print(F(" pending="));            out.print(g_node.has_pending_tx() ? 1 : 0);
    out.print(F(" reset="));                                                     // v2: the fault-log's newest CAUSE ("-" = none)
    if (g_last_reset_valid) out.print(mrfault::fault_cause_str(g_last_reset.cause));
    else                    out.print('-');
    out.print(F(" halted="));             out.print(g_halted ? 1 : 0);        // prep-restart: 1 = intentionally dormant, not wedged
#if defined(NRF52_PLATFORM) || defined(ARDUINO_ARCH_NRF52)
    out.print(F(" stackhw="));            out.print(loop_stack_free_bytes()); // ADDENDUM 4: loop-task min free stack bytes — the jump-to-0x0 was this overflowing; must stay well >0
#endif
    if (g_fs_reformatted) out.print(F(" fs=REFORMATTED"));                        // Part 2: InternalFS was corrupt this boot -> reformatted (re-provision)
#if defined(NRF52_PLATFORM) && defined(PIN_VBAT) && !defined(MR_NO_BATT)
    // Battery diagnostic. VBAT (P0.31) reads the CELL through a ÷3 divider — NEVER USB's 5 V (max ~4.2 V).
    // Verify vs a multimeter on the battery: mv = raw × ADC_MULTIPLIER(3.0) × AREF_VOLTAGE(3.0) / 4.096.
    pinMode(VBAT_ENABLE, OUTPUT); digitalWrite(VBAT_ENABLE, LOW);
    analogReadResolution(12); analogReference(AR_INTERNAL_3_0);
    const int braw = analogRead(PIN_VBAT);
    out.print(F(" batt_raw="));           out.print(braw);
    out.print(F(" batt_mv="));            out.print((int)((braw * ADC_MULTIPLIER * AREF_VOLTAGE) / 4.096f));
#endif
    out.println();
}

// `duty` — duty-cycle consumption readout: 0..100% of the rolling-window budget (100 = the node must stay silent),
// + when at 100% how long until airtime ages back in. `disabled` when there is no duty limit.
static void dump_duty(Print& out) {
    const auto d = g_node.duty_status();
    if (!d.enabled) { out.println(F("disabled (no duty limit)")); return; }
    out.print(d.pct); out.print('%');
    if (d.pct >= 100) { out.print(F(" — SILENT, ~")); out.print((d.avail_ms + 500) / 1000); out.print(F(" s to availability")); }
    out.println();
}

// "7,12" -> allowed_sf_bitmap (bit per SF index 5..12); 0 if none valid.
// parse_sf_list moved to firmware_config_parse.h (pure, native-tested); `using mrfw::parse_sf_list` above.

// Apply the RADIO operating point from the (just-saved) NV blob LIVE — no reboot. `reconfig` re-tunes the
// radio (freq/SF/BW/CR changed); a tx_power-only change skips the re-tune (it's set per-TX via the Hal).
// apply_radio_live moved to firmware_config.{h,cpp} (cleanup 2026-07-14, Increment A); `using mrfw::apply_radio_live` (top).

// Print the node's key_hash32 (hex, from g_identity) + name (from /mrid). Shared by boot, `status`, `regen`.
static void print_identity(const mrnv::IdBlob& idb) {
    char hx[9];
    snprintf(hx, sizeof hx, "%08lX", (unsigned long)g_identity.key_hash32);
    mrcon.print(F("  key_hash32= 0x")); mrcon.print(hx);
    if (idb.name_len > 0 && idb.name_len <= sizeof idb.name) {
        mrcon.print(F("  name=\""));
        for (uint16_t i = 0; i < idb.name_len; ++i) mrcon.print(idb.name[i]);
        mrcon.print(F("\""));
    }
    mrcon.println();
}

// `regen` — mint a NEW identity (fresh HW-RNG seed) -> persist /mrid -> re-derive -> re-seed the node's
// self binding. Keeps `name` + `node_id` (the short address is independent of the keypair). The new
// key_hash32 propagates on the next beacon; peers re-bind by it (the old one ages out of their id_bind).
static void do_regen() {
    mrnv::IdBlob idb{};
    mrnv::load_id(idb);                                          // preserve the existing name (if any)
    mrrng::fill(idb.seed, sizeof idb.seed);
    idb.magic = mrnv::kIdMagic; idb.version = mrnv::kIdVersion;
    if (!mrnv::save_id(idb)) { mrcon.println(F("> regen err nv_save_failed")); return; }
    meshroute::identity_from_seed(g_identity, idb.seed);
    g_node.set_identity(g_node.node_id(), g_identity.key_hash32);
    g_node.set_crypto_identity(g_identity.x_secret, g_identity.ed_pub);   // DP1: re-install the E2E crypto identity
    mrcon.print(F("> regen ok"));
    print_identity(idb);
}

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

// `factory_reset` — confirm-gated full NV wipe -> reboot factory-fresh (default config + a NEW identity + no peers
// + empty inbox). The literal `confirm` token guards against an accidental paste (irreversible).
static void handle_factory_reset(const char* arg, size_t n, Print& out) {
    while (n && *arg == ' ') { ++arg; --n; }
    if (n == 7 && !strncmp(arg, "confirm", 7)) {
        out.println(F("> factory reset — erasing all NV, rebooting…"));
        g_inbox_dm.wipe(); g_inbox_ch.wipe();   // §5: drop the QSPI inbox RECORDS (their store's domain); factory_erase does the InternalFS slots + meta
        if (!mrnv::factory_erase()) out.println(F("> factory_reset WARN: an NV slot did not erase (boot re-defaults it)"));
        do_reboot();
    } else {
        out.println(F("> factory_reset WIPES ALL flash (config + identity + peers + inbox) and reboots to factory. Type 'factory_reset confirm' to proceed."));
    }
}

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

// `sleep` / `sleep on` -> light-sleep when idle even though a host is present (the explicit override the user
// asked for); `sleep off` -> cancel it, stay awake. A headless node (no console byte this boot) light-sleeps
// on its own — this command is only for a node you're connected to. After `sleep on` the console goes quiet
// (light-sleep gates the UART) — reconnect to get it back (DTR resets the board). The node still wakes on RX
// (a peer DM prints RECV) and on its scheduled timers. No-op on -DMR_NO_POWERSAVE builds (the gate is gone).
static void handle_sleep(const char* arg, size_t n, Print& out) {
    while (n && *arg == ' ') { ++arg; --n; }
    if (n >= 3 && !strncmp(arg, "off", 3)) {
        g_force_sleep = false;
        out.println(F("> sleep off — staying awake while a host is connected"));
    } else {
        g_force_sleep = true;
        out.println(F("> sleep on — light-sleeping when idle; reconnect to wake the console (still wakes on RX)"));
    }
}

// `debug on` / `debug off` (also `debug 1`/`debug 0`) — gate the decoded per-frame «rx/»tx console trace
// (frame_trace.h g_mr_trace_on). §3: default OFF at boot; `debug on` enables it for the session.
static void handle_debug(const char* arg, size_t n, Print& out) {
    while (n && *arg == ' ') { ++arg; --n; }
    const bool off = (n >= 3 && !strncmp(arg, "off", 3)) || (n >= 1 && arg[0] == '0');
    meshroute::g_mr_trace_on = !off;
    out.println(off ? F("> debug off — RX/TX frame trace silenced") : F("> debug on — tracing RX/TX frames"));
}

// `lookup <hash>` — local id_bind cache peek (NO airtime): resolve a key_hash32 -> node short-id from what
// this node already knows (beacons / prior H answers). Hash is hex (e.g. `lookup 8a3f1c02`). For a network
// resolve of an unknown hash, use `resolve` (floods H).
static void handle_lookup(const char* arg, size_t n, Print& out) {
    while (n && *arg == ' ') { ++arg; --n; }
    if (n < 3 || arg[0] != '0' || (arg[1] != 'x' && arg[1] != 'X')) { out.println(F("> lookup err: hash must be 0x-prefixed (e.g. lookup 0x8a3f1c02)")); return; }
    const uint32_t hash = (uint32_t)strtoul(arg + 2, nullptr, 16);   // 0x-only (kills id-vs-hash ambiguity)
    meshroute::Node::IdBindConf conf = meshroute::Node::IdBindConf::claimed;
    const int id = g_node.id_bind_find_by_hash(hash, &conf);
    out.print(F("[lookup] 0x")); out.print(hash, HEX);
    if (id < 0) { out.println(F(" -> miss")); return; }
    out.print(F(" -> id=")); out.print(id);
    out.println(conf == meshroute::Node::IdBindConf::authoritative ? F(" (authoritative)") : F(" (claimed)"));
}

// §1.3 `nameof 0x<hash>` — the cached human name for a peer's key_hash32 (learned via the pubkey exchange, refreshed on each).
static void handle_nameof(const char* arg, size_t n, Print& out) {
    while (n && *arg == ' ') { ++arg; --n; }
    if (n < 3 || arg[0] != '0' || (arg[1] != 'x' && arg[1] != 'X')) { out.println(F("> nameof err: hash must be 0x-prefixed (e.g. nameof 0x8a3f1c02)")); return; }
    const uint32_t hash = (uint32_t)strtoul(arg + 2, nullptr, 16);
    char nm[32]; const uint8_t nl = g_node.peer_name_find(hash, nm, sizeof nm);
    out.print(F("[nameof] 0x")); out.print(hash, HEX); out.print(F(" = "));
    if (nl) { out.print('"'); out.write(nm, nl); out.println('"'); }
    else     out.println(F("<unknown — reqpubkey it first>"));
}

// `hashof <id>` — reverse lookup: a node short-id -> its key_hash32 (AUTHORITATIVE bindings only — a node we
// can vouch for). Decimal id 0..254.
static void handle_hashof(const char* arg, size_t n, Print& out) {
    while (n && *arg == ' ') { ++arg; --n; }
    if (!n) { out.println(F("> hashof err bad_args (id 0..254)")); return; }
    const int id = atoi(arg);
    uint32_t hash = 0;
    out.print(F("[hashof] id=")); out.print(id);
    if (id >= 0 && id <= 254 && g_node.key_hash_of_id((uint8_t)id, hash)) { out.print(F(" -> 0x")); out.println(hash, HEX); }
    else                                                                  out.println(F(" -> unknown"));
}

// `whoami` — this node's own identity + role. The hash printed here is what a peer types into `sendhash` to
// reach you (the device can't surface its own key_hash32 any other way). Name is read from /mrid.
static void handle_whoami(Print& out) {
    out.print(F("[whoami] id=")); out.print(g_node.node_id());
    out.print(F(" hash=0x"));     out.print(g_node.key_hash32(), HEX);
    { char nm[32]; uint8_t nn = g_node.effective_name(nm, sizeof nm); out.print(F(" name=\"")); out.write(nm, nn); out.print('"'); }   // §1.3: always show the effective name (the MeshRoute node: 0x<hash> default when unset)
    const meshroute::NodeConfig& c = g_node.config();
    out.print(F(" leaf="));   out.print(c.leaf_id);
    out.print(F(" gw="));     out.print(c.is_gateway ? 1 : 0);
    out.print(F(" gwonly=")); out.print(c.gateway_only ? 1 : 0);
    out.print(F(" mobile=")); out.println(c.is_mobile ? 1 : 0);
    // Dual-layer gateway: an ADDITIVE per-leaf line. Single-layer whoami above is BYTE-IDENTICAL to before.
    if (c.n_layers == 2) {
        for (uint8_t li = 0; li < 2; ++li) {
            const meshroute::LayerConfig& L = c.layers[li];
            out.print(F("[whoami.layer")); out.print(li);
            out.print(F("] node_id="));   out.print(L.node_id);
            out.print(F(" layer_id="));   out.print(L.layer_id);
            out.print(F(" routing_sf=")); out.print(L.routing_sf);
            out.print(F(" window_ms="));  out.print(L.window_ms);
            out.print(F(" window_offset_ms=")); out.println(L.window_offset_ms);
        }
    }
}

// gw_parse_err_str / gw_val_err_str / handle_gateway moved to firmware_config.{h,cpp} (cleanup 2026-07-14, Increment A); `using mrfw::handle_gateway` (top).

// ---- `leaf` command REMOVED (2026-07-03) --------------------------------------------------------------------
// The low-level `leaf create` (which minted a leaf from the node's CURRENT settings) is folded into `create`
// (explicit key=value params; anti-spam knobs default rather than inherit). `leaf name <text>` -> `cfg set
// leaf_name "<text>"` (the config-hash rename that bumps the epoch). One leaf-mint verb now: `create`.

// ---- R6.3 normal-node provisioning verbs: join / create / leave — LIVE-APPLY (no reboot). Spec
//      2026-06-21-leaf-provisioning-console-verbs.md. `create` is the ONE leaf-mint verb (2026-07-03: the old
//      low-level `leaf create` folded in; `key=value` args); `cfg set <key>` stays the granular per-field path
//      (§4). Normal nodes ONLY (gateways are multi-layer -> a future join_as_gateway, §5). ------------------

// Seed a fresh blob from the live config (so a save on a never-persisted node doesn't zero the non-provisioning fields).
static void seed_blob_from_live(mrnv::Blob& b) {
    const meshroute::NodeConfig& nc = g_node.config();
    b.freq_mhz = g_freq_mhz;        b.bw_hz = nc.radio_bw_hz;       b.beacon_ms = nc.beacon_period_ms;
    b.duty = nc.duty_cycle;         b.allowed_sf_bitmap = nc.allowed_sf_bitmap;
    b.routing_sf = nc.routing_sf;   b.cr = nc.radio_cr;
    b.lbt = nc.lbt_enabled ? 1 : 0; b.node_id = g_node.canonical_node_id();   b.tx_power = g_tx_power;
    b.is_gateway = nc.is_gateway ? 1 : 0; b.gateway_only = nc.gateway_only ? 1 : 0;
    b.is_mobile  = nc.is_mobile ? 1 : 0;  b.leaf_id      = nc.leaf_id;  b.team_id = nc.team_id; b.mobile_autoregister = nc.mobile_autoregister ? 1 : 0; b.team_local_id = g_node.team_local_id();   // §mobile: preserve team + autoreg + team-DAD id across create/join
    b.ble_mode   = g_ble_mode;            b.ble_period_min = g_ble_period_min;  b.ble_pin = g_ble_pin;
    b.loc_in_dm  = nc.loc_in_dm ? 1 : 0;  b.e2e_dm     = nc.e2e_dm ? 1 : 0;
    b.gw_announce_duty_pct = nc.gw_announce_duty_pct; b.gw_announce_min_interval_ms = nc.gw_announce_min_interval_ms;
    b.l1_freq_mhz = nc.layers[1].freq_mhz; b.gw_herd_slack = nc.gw_herd_slack;
    b.l1_bw_hz = nc.layers[1].bw_hz; b.l1_cr = nc.layers[1].cr;   // v17 per-layer BW/CR (0 = inherit)
    b.lineage_id = nc.lineage_id; b.config_epoch = nc.config_epoch; b.leaf_name_len = nc.leaf_name_len;
    for (uint8_t i = 0; i < nc.leaf_name_len && i < sizeof(b.leaf_name); ++i) b.leaf_name[i] = (uint8_t)nc.leaf_name[i];
    b.channel_active_fraction = nc.channel_active_fraction; b.channel_min_interval_ms = nc.channel_min_interval_ms; b.dm_min_interval_ms = nc.dm_min_interval_ms;   // v16 anti-spam per-leaf tunables
    b.magic = mrnv::kMagic; b.version = mrnv::kVersion;   // ★ STAMP here so EVERY caller gets a VALID blob. Without it a save path that seeds (load failed / fresh chip) but forgets to re-stamp — e.g. handle_team — persists magic=0/version=0, which the next boot's load() REJECTS => the whole config resets to defaults (the `cfg set mobile 1` -> reboot -> mobile=0 bug). The other callers also re-stamp (harmless/redundant now).
}

// kv_next (the key=value provisioning-verb tokenizer) moved to firmware_config_parse.h (pure, native-tested); `using mrfw::kv_next` above.

// Apply a just-saved provisioning blob LIVE (no reboot): radio re-tune + membership + config + (re-)DAD. The four
// §2 sub-paths. do_dad=false only for `leave` (stays unprovisioned, idle awaiting a join).
static void provision_apply_live(const mrnv::Blob& b, bool do_dad) {
    apply_radio_live(b, /*reconfig=*/true);                                  // §2 radio: freq/bw/ctrl_sf live (sets routing_sf/bw/cr)
    meshroute::NodeConfig& lc = g_node.mutable_config();                     // §2 config: MAC re-reads these each use
    lc.leaf_id = b.leaf_id; lc.layers[0].layer_id = b.leaf_id; lc.layers[0].routing_sf = b.routing_sf;
    lc.allowed_sf_bitmap = b.allowed_sf_bitmap; lc.layers[0].allowed_sf_bitmap = b.allowed_sf_bitmap;
    lc.duty_cycle = b.duty;  lc.lineage_id = b.lineage_id;
    if (b.channel_active_fraction > 0.0f) lc.channel_active_fraction = b.channel_active_fraction;   // v16: 0 => keep the NodeConfig default
    if (b.channel_min_interval_ms)        lc.channel_min_interval_ms  = b.channel_min_interval_ms;
    if (b.dm_min_interval_ms)             lc.dm_min_interval_ms       = b.dm_min_interval_ms;
    lc.leaf_name_len = b.leaf_name_len;
    for (uint8_t i = 0; i < b.leaf_name_len && i < sizeof(lc.leaf_name); ++i) lc.leaf_name[i] = (char)b.leaf_name[i];
    g_node.reset_leaf_epoch_state(b.config_epoch);                          // config_epoch + _max_seen_epoch (fresh-lineage numbering)
    g_node.recompute_duty_budget();                                         // §2(b) duty enforcement live
    g_node.reset_join_for_reprovision(); lc.layers[0].node_id = 0;          // §2 membership: drop id + CLEAR _joined so the re-DAD actually runs (set_identity alone leaves _joined -> join no-ops)
    g_node.clear_routing_state();                                           // the old network's routes/bindings/schedules are stale -> wipe
    g_node.set_rediscover_pending(do_dad);                                  // join/create: restart discovery once the new id is adopted (NOT leave -> idle)
    if (do_dad) { meshroute::Command jc{}; jc.kind = meshroute::CmdKind::join; (void)g_node.on_command(jc); }   // re-DAD live (claim-after-listen -> J ~join_listen_ms later)
}

// `join layer=<1..255> freq=<MHz> bw=<kHz> sf=<5..12>` — set the radio floor + (re-)DAD; auto-pulls the leaf config (R6.2).
#if MR_N_LAYERS < 2   // §config-integrity: create/join are normal-node-only — compiled out on the gateway build (refused at dispatch)
static void handle_join(const char* args, Print& out) {
    char buf[128]; size_t bn = 0; for (; args[bn] && bn < sizeof(buf) - 1; ++bn) buf[bn] = args[bn]; buf[bn] = '\0';
    double freq = 0, bwk = 0; long sf = 0, layer = 0; bool hf = false, hb = false, hs = false, hlv = false;   // bwk is kHz (FRACTIONAL — 62.5 / 41.67 / 31.25 are valid LoRa BWs)
    char* p = buf; char* k; char* v;
    while (kv_next(p, k, v)) {
        if      (v && !strcmp(k, "freq"))  { freq  = atof(v); hf = true; }
        else if (v && !strcmp(k, "bw"))    { bwk   = atof(v); hb = true; }
        else if (v && !strcmp(k, "sf"))    { sf    = atol(v); hs = true; }
        else if (v && !strcmp(k, "layer")) { layer = atol(v); hlv = true; }   // the full 1..255 layer id (wire leaf nibble = layer & 0x0F)
        else { out.print(F("> join err bad/unknown key: ")); out.println(k); goto usage; }
    }
    if (!(hf && hb && hs && hlv) || freq < 100.0 || freq > 1000.0 || bwk < 7 || bwk > 500 || sf < 5 || sf > 12 || layer < 1 || layer > 255) goto usage;
    {
        mrnv::Blob b{}; if (!mrnv::load(b)) seed_blob_from_live(b);
        b.magic = mrnv::kMagic; b.version = mrnv::kVersion;
        b.freq_mhz = freq; b.bw_hz = (uint32_t)(bwk * 1000.0 + 0.5); b.routing_sf = (uint8_t)sf;   // kHz->Hz, ROUNDED (62.5->62500, not 62000)
        b.leaf_id = (uint8_t)(layer & 0x0F); b.layer0_id = (uint8_t)layer;       // full layer id stored; leaf = layer & 0x0F (byte-0 wire filter)
        b.node_id = 0; b.joined = 0; b.lineage_id = 0; b.config_epoch = 0;       // unprovisioned -> DAD + adopt the leaf's lineage via pull
        if (!mrnv::save(b)) { out.println(F("> join err nv_save_failed")); return; }
        provision_apply_live(b, /*do_dad=*/true);
        out.print(F("> join layer=")); out.print((int)layer); out.print(F(" freq=")); out.print(freq, 3);
        out.print(F(" bw=")); out.print(b.bw_hz); out.print(F("Hz sf=")); out.print((int)sf);
        out.print(F(" (leaf ")); out.print((int)(layer & 0x0F)); out.println(F(") — DADing id + pulling config (live)"));
        return;
    }
usage:
    out.println(F("> join err usage: join layer=<1..255> freq=<MHz> bw=<kHz 7..500, fractional ok e.g. 62.5> sf=<5..12>   (leaf = layer & 0x0F)"));
}

// `create layer=<1..255> freq=<MHz> bw=<kHz> sf=<5..12> sf_list=<7,9> duty=<pct> name="<text>"
//         [active_fraction=<0..1>] [ch_min_ms=<ms>] [dm_min_ms=<ms>]` — join's floor + mint a MANAGED leaf (mother).
// The anti-spam keys are OPTIONAL: omitted => the protocol DEFAULTS (never inherited from the node's current settings).
static void handle_create(const char* args, Print& out) {
    char buf[192]; size_t bn = 0; for (; args[bn] && bn < sizeof(buf) - 1; ++bn) buf[bn] = args[bn]; buf[bn] = '\0';
    double freq = 0, dutypct = -1, bwk = 0; long sf = 0, layer = 0; uint16_t sfbm = 0;   // bwk is kHz (FRACTIONAL — 62.5 / 41.67 / 31.25 are valid LoRa BWs)
    char nm[meshroute::protocol::leaf_name_max]; uint8_t nlen = 0;
    float af = 0.125f; long chi = P::channel_min_interval_ms, dmi = P::dm_min_interval_ms;   // anti-spam DEFAULTS (overridden only if the key is given)
    bool hf = false, hb = false, hs = false, hlv = false, hlist = false, hduty = false, hname = false;
    char* p = buf; char* k; char* v;
    while (kv_next(p, k, v)) {
        if      (v && !strcmp(k, "freq"))            { freq = atof(v); hf = true; }
        else if (v && !strcmp(k, "bw"))              { bwk = atof(v); hb = true; }
        else if (v && !strcmp(k, "sf"))              { sf = atol(v); hs = true; }
        else if (v && !strcmp(k, "layer"))           { layer = atol(v); hlv = true; }   // the full 1..255 layer id (wire leaf nibble = layer & 0x0F)
        else if (v && !strcmp(k, "sf_list"))         { sfbm = parse_sf_list(v); hlist = true; }
        else if (v && !strcmp(k, "duty"))            { dutypct = atof(v); hduty = true; }
        else if (v && !strcmp(k, "name"))            { for (const char* c = v; *c && nlen < sizeof(nm); ++c) nm[nlen++] = *c; hname = true; }
        else if (v && !strcmp(k, "active_fraction")) { af = (float)atof(v); }
        else if (v && !strcmp(k, "ch_min_ms"))       { chi = atol(v); }
        else if (v && !strcmp(k, "dm_min_ms"))       { dmi = atol(v); }
        else { out.print(F("> create err bad/unknown key: ")); out.println(k); goto usage; }
    }
    if (!(hf && hb && hs && hlv && hlist && hduty && hname)) goto usage;
    if (freq < 100.0 || freq > 1000.0 || bwk < 7 || bwk > 500 || sf < 5 || sf > 12 || layer < 1 || layer > 255 || sfbm == 0 || dutypct < 0.0 || dutypct > 100.0) goto usage;
    if (af <= 0.0f) af = 0.125f; if (af > 1.0f) af = 1.0f;                    // clamp; 0/absent -> the default
    if (chi < 1) chi = P::channel_min_interval_ms; if (dmi < 1) dmi = P::dm_min_interval_ms;
    {
        mrnv::Blob b{}; if (!mrnv::load(b)) seed_blob_from_live(b);
        b.magic = mrnv::kMagic; b.version = mrnv::kVersion;
        b.freq_mhz = freq; b.bw_hz = (uint32_t)(bwk * 1000.0 + 0.5); b.routing_sf = (uint8_t)sf;   // kHz->Hz, ROUNDED (62.5->62500, not 62000)
        b.leaf_id = (uint8_t)(layer & 0x0F); b.layer0_id = (uint8_t)layer;    // full layer id; leaf = layer & 0x0F
        b.allowed_sf_bitmap = sfbm;
        b.duty = meshroute::bp_to_duty(meshroute::duty_to_bp(dutypct / 100.0));   // §5: percent -> 0..1, quantized to the 0.01% wire step
        for (uint8_t i = 0; i < nlen; ++i) b.leaf_name[i] = (uint8_t)nm[i]; b.leaf_name_len = nlen;
        b.channel_active_fraction = meshroute::bp_to_frac(meshroute::frac_to_bp(af));   // EXPLICIT (or default) — NEVER inherited; quantized for hash parity
        b.channel_min_interval_ms = (uint32_t)meshroute::ms_to_u16((uint32_t)chi);
        b.dm_min_interval_ms      = (uint32_t)meshroute::ms_to_u16((uint32_t)dmi);
        uint16_t lin = 0; do { mrrng::fill(reinterpret_cast<uint8_t*>(&lin), sizeof lin); } while (lin == 0);   // mint a managed lineage (never 0)
        b.lineage_id = lin; b.config_epoch = 1; b.node_id = 0; b.joined = 0;      // a fresh managed leaf starts at epoch 1
        if (!mrnv::save(b)) { out.println(F("> create err nv_save_failed")); return; }
        provision_apply_live(b, /*do_dad=*/true);
        out.print(F("> create layer=")); out.print((int)layer); out.print(F(" lineage=")); out.print(lin);
        out.print(F(" (leaf ")); out.print((int)(layer & 0x0F)); out.print(F(") name=\""));
        for (uint8_t i = 0; i < nlen; ++i) out.print((char)b.leaf_name[i]);
        out.print(F("\" af=")); out.print(b.channel_active_fraction, 3);
        out.print(F(" ch_min=")); out.print(b.channel_min_interval_ms); out.print(F(" dm_min=")); out.print(b.dm_min_interval_ms);
        out.println(F(" — mother live"));
        return;
    }
usage:
    out.println(F("> create err usage: create layer=<1..255> freq=<MHz> bw=<kHz 7..500, fractional ok e.g. 62.5> sf=<5..12> sf_list=<e.g.7,9> duty=<pct, fractional ok e.g. 0.1> name=\"<text>\" [active_fraction=<0..1>] [ch_min_ms=<ms>] [dm_min_ms=<ms>]   (leaf = layer & 0x0F)"));
}

// §mobile 6.1: FNV-1a over (key_hash32 ‖ nonce) = the 32-bit team_id.
// team_fnv1a32 moved to firmware_config_parse.h (pure, native-tested); `using mrfw::team_fnv1a32` above.
// `team new` = MINT a fresh team_id = hash(our key ‖ HW-RNG nonce). `team <id>` = JOIN an existing team. `team 0` = leave.
static void handle_team(const char* args, Print& out) {
    while (*args == ' ') ++args;
    const meshroute::NodeConfig& c = g_node.config();
    uint32_t t;
    const char* phy_args = nullptr;
    if (!strncmp(args, "new", 3)) {
        uint32_t nonce = 0; g_hal.rand_bytes(reinterpret_cast<uint8_t*>(&nonce), 4);
        t = team_fnv1a32(g_node.key_hash32(), nonce);
        phy_args = args + 3;   // §mobile 6.4: `team new [freq=<MHz> sf=<5-12> bw=<kHz>]` — optional team PHY
    } else if (args[0]) {
        char* endp = nullptr;
        t = (uint32_t)strtoul(args, &endp, 0);
        phy_args = endp;   // §6.4: `team <id> [freq= sf= bw=]` — a JOIN can set the shared team PHY too (mirrors `team new`)
    } else {
        out.println(F("> team err usage: `team new [freq= sf= bw=]` (mint) | `team <id> [freq= sf= bw=]` (join) | `team 0` (leave)"));
        return;
    }
    mrnv::Blob b{}; if (!mrnv::load(b)) seed_blob_from_live(b);
    b.magic = mrnv::kMagic; b.version = mrnv::kVersion;   // ★ was MISSING (the only save path without a stamp) -> a `team` command on a fresh/rejected blob persisted magic=0 => next boot's load() rejected it => the whole config reset (mobile=0). seed_blob_from_live now also stamps; this is belt-and-suspenders + matches the other 6 save paths.
    b.team_id = t;
    // §mobile 6.4 Fix 6: set the team PHY so teammates hear each other (AND a member can later register with a compatible
    // static network). Mirror `mobile register freq=`. Omitted -> keep the current PHY. Requires is_mobile (a team is mobile).
#if MR_FEAT_MOBILE
    if (phy_args && strstr(phy_args, "freq=") && c.is_mobile) {
        const char* fs = strstr(phy_args, "freq="); const char* ss = strstr(phy_args, "sf="); const char* bs = strstr(phy_args, "bw=");
        double freq = strtod(fs + 5, nullptr);
        int    sf   = ss ? atoi(ss + 3) : 0;
        double bw   = bs ? strtod(bs + 3, nullptr) : 125.0;   // FRACTIONAL kHz (62.5 valid)
        if (freq < 100.0 || freq > 1000.0 || sf < 5 || sf > 12 || bw < 7.0 || bw > 500.0) {
            out.println(F("> team new err: freq 100..1000 MHz, sf 5..12, bw 7..500 kHz")); return;
        }
        meshroute::LayerConfig phy{};
        phy.layer_id = c.leaf_id; phy.routing_sf = (uint8_t)sf; phy.freq_mhz = freq;
        phy.bw_hz = (uint32_t)(bw * 1000.0 + 0.5); phy.allowed_sf_bitmap = (uint16_t)(1u << sf);
        g_node.mobile_register_phy(phy);                       // retune the radio (+ kick the FSM -> team-DAD via the no-host path)
        b.freq_mhz = freq; b.routing_sf = (uint8_t)sf; b.bw_hz = phy.bw_hz; b.allowed_sf_bitmap = phy.allowed_sf_bitmap;   // PERSIST the team PHY
        out.print(F("> team PHY: freq=")); out.print(freq, 3); out.print(F(" sf=")); out.print(sf); out.print(F(" bw=")); out.print(bw, 2); out.println(F(" kHz"));
    }
#endif
    // §6.4: a team is a SHARED-PHY overlay — members can only hear each other on a COMMON freq/routing_sf/sf_list/bw, and an
    // empty sf_list blocks DATA entirely ([[data-sf-removed]]). Refuse to mint/join (t!=0) with an INCOMPLETE PHY so a member
    // never lands on an isolated island (the 250-vs-125 kHz / empty-sf_list state seen on the bench). Leave (t==0) is exempt.
    if (t != 0) {
        const double eff_freq = (c.is_mobile && c.layers[0].freq_mhz > 0.0) ? c.layers[0].freq_mhz : g_freq_mhz;
        if (eff_freq <= 0.0 || c.routing_sf < 5 || c.routing_sf > 12 || c.allowed_sf_bitmap == 0 || g_node.active_bw_hz() == 0) {
            out.println(F("> team err: incomplete PHY — need freq, routing_sf(5..12), sf_list(DATA SF), bw."));
            out.println(F(">   set them inline: `team new freq=869.0 sf=7 bw=125` — ALL members MUST use the SAME freq/sf/bw."));
            return;   // NOT joined/minted: team_id, _team_local_id, NV all unchanged
        }
    }
    const bool team_switched = (c.team_id != t);              // §6.4: capture BEFORE the mutable set (c is a live ref)
    if (team_switched) g_node.set_team_local_id(0);           // §6.4: leaving OR switching teams -> drop the stale team-DAD id (0 = left; a re-DAD picks a fresh one for the new team)
    g_node.mutable_config().team_id = t;                     // LIVE (team_dad_fire reads _cfg.team_id)
    if (c.is_mobile && t != 0 && team_switched) g_node.team_dad_fire();   // §6.4: bootstrap the team plane (self-assign a _team_local_id, no static host needed)
    b.node_id       = g_node.canonical_node_id();            // §6.4: team_dad_fire may have MOVED node_id (off-grid: node_id==team id) -> persist the live id, don't re-save the stale one loaded at entry
    b.team_local_id = g_node.team_local_id();                // §6.4: persist the fresh id (or 0 on leave) alongside team_id
    mrnv::save(b);                                            // PERSISTED
    char tx[9]; snprintf(tx, sizeof tx, "%08lX", (unsigned long)t);
    out.print(F("> team -> team_id=0x")); out.println(tx);
    if (c.is_mobile && t != 0) { out.print(F("  team-DAD: local_id=")); out.println(g_node.team_local_id()); }
}

// §mobile console: `mobile register [freq=<MHz> sf=<5-12> bw=<kHz> | scan]` · `gateways` · `query <gw>` · `status`.
#if MR_FEAT_MOBILE   // §featuresplit: the whole mobile console command compiles out on a static build (the FSM/accessors it drives are gone)
static void handle_mobile(const char* args, Print& out) {
    while (*args == ' ') ++args;
    const meshroute::NodeConfig& c = g_node.config();
    if (!c.is_mobile) { out.println(F("> mobile err: not a mobile (cfg set mobile 1 + reboot)")); return; }
    if (!strncmp(args, "register", 8)) {
        const char* p = args + 8; while (*p == ' ') ++p;
        if (!strncmp(p, "scan", 4)) {
            g_node.mobile_register_scan();
            out.print(F("> mobile register: scanning current + ")); out.print(g_node.learned_layers_count()); out.println(F(" known networks"));
        } else if (strstr(p, "freq=")) {
            const char* fs = strstr(p, "freq="); const char* ss = strstr(p, "sf="); const char* bs = strstr(p, "bw=");
            double freq = fs ? strtod(fs + 5, nullptr) : 0.0;
            int sf = ss ? atoi(ss + 3) : 0;
            double bw = bs ? strtod(bs + 3, nullptr) : 125.0;   // FRACTIONAL kHz — 62.5 / 41.67 / 31.25 are valid LoRa BWs (atof like join/create, NOT atoi which truncates 62.5->62)
            if (freq < 100.0 || freq > 1000.0 || sf < 5 || sf > 12 || bw < 7.0 || bw > 500.0) { out.println(F("> mobile register err: freq 100..1000 MHz, sf 5..12, bw 7..500 kHz")); return; }
            meshroute::LayerConfig phy{};
            phy.layer_id = c.leaf_id; phy.routing_sf = (uint8_t)sf; phy.freq_mhz = freq;
            phy.bw_hz = (uint32_t)(bw * 1000.0 + 0.5); phy.allowed_sf_bitmap = (uint16_t)(1u << sf);   // kHz->Hz ROUNDED (62.5->62500, not 62000)
            g_node.mobile_register_phy(phy);
            out.print(F("> mobile register: on freq=")); out.print(freq, 3); out.print(F(" sf=")); out.print(sf); out.print(F(" bw=")); out.print(bw, 2); out.println(F(" kHz"));
        } else {
            g_node.mobile_register_current();
            out.println(F("> mobile register: DISCOVER on the current PHY"));
        }
        return;
    }
    if (!strcmp(args, "gateways")) {
        uint8_t shown = 0;
        for (uint8_t i = 0; i < g_node.bridged_layer_cap(); ++i) {
            const auto& b = g_node.bridged_layer(i);
            if (!b.valid) continue;
            out.print(F("  gw ")); out.print(b.gw_id); out.print(F(" -> leaf ")); out.println(b.dest_leaf); ++shown;
        }
        if (!shown) out.println(F("  no gateways learned"));
        const uint8_t nl = g_node.learned_layers_count();
        for (uint8_t i = 0; i < nl; ++i) {
            const auto& r = g_node.learned_layer(i);
            out.print(F("  net layer=")); out.print(r.layer_id); out.print(F(" \""));
            for (uint8_t k = 0; k < r.name_len; ++k) out.print((char)r.name[k]);
            out.print(F("\" freq=")); out.print((double)r.freq_khz / 1000.0, 3); out.print(F(" sf=")); out.print(r.sf);
            out.print(F(" bw=")); out.println(r.bw_hz / 1000);
        }
        if (!nl) out.println(F("  no networks learned (use 'mobile query <gw>')"));
        return;
    }
    if (!strncmp(args, "query", 5)) {
        const uint8_t gw = (uint8_t)strtoul(args + 5, nullptr, 0);
        if (!gw) { out.println(F("> mobile query err: usage 'mobile query <gw_id>'")); return; }
        g_node.mobile_send_layer_query(gw);
        out.print(F("> mobile query gw=")); out.print(gw); out.println(F(" sent (answer async -> 'mobile gateways')"));
        return;
    }
    if (!strcmp(args, "status")) {
        if (g_node.mobile_registered()) {
            out.print(F("  REGISTERED home=")); out.print(g_node.mobile_home_id());
            out.print(F(" local=")); out.print(g_node.mobile_local_id());
            out.print(F(" epoch=")); out.print(g_node.mobile_reg_epoch());
            out.print(F(" home_layer=")); out.println(g_node.mobile_home_layer());
        } else out.println(F("  UNREGISTERED"));
        out.print(F("  current PHY: layer=")); out.print(c.layers[0].layer_id);
        const double pf = c.layers[0].freq_mhz > 0.0 ? c.layers[0].freq_mhz : g_freq_mhz;   // §mobile: live layer freq (fallback to boot/global if not yet adopted)
        out.print(F(" freq=")); out.print(pf, 3); out.print(F(" sf=")); out.print(c.routing_sf);
        out.print(F(" bw=")); out.print((double)g_node.active_bw_hz() / 1000.0, 2); out.println(F(" kHz"));
        out.print(F("  autoregister=")); out.println(c.mobile_autoregister ? 1 : 0);
        out.print(F("  known networks=")); out.println(g_node.learned_layers_count());
        return;
    }
    out.println(F("> mobile err usage: register [freq= sf= bw= | scan] | gateways | query <gw> | status"));
}
#endif   // MR_FEAT_MOBILE (handle_mobile)
#endif   // MR_N_LAYERS < 2 — handle_join / handle_create (normal-node provisioning)

// `leave` — wipe to default, keep ONLY freq; go unprovisioned + idle (the clean managed->managed re-join primitive).
static void handle_leave(Print& out) {
    mrnv::Blob b{}; if (!mrnv::load(b)) seed_blob_from_live(b);
    const double keep_freq = b.freq_mhz;
    b = mrnv::Blob{};                                                        // zero everything...
    b.magic = mrnv::kMagic; b.version = mrnv::kVersion;
    b.freq_mhz = keep_freq;                                                  // ...keep only freq
    b.bw_hz = (uint32_t)(LORA_BW * 1000.0); b.routing_sf = LORA_SF; b.cr = LORA_CR; b.tx_power = LORA_TX_POWER;
    b.beacon_ms = 900000; b.duty = (double)LORA_DUTY_CYCLE_PCT / 100.0;       // NodeConfig defaults (15 min, 10%)
    b.channel_active_fraction = 0.125f; b.channel_min_interval_ms = P::channel_min_interval_ms; b.dm_min_interval_ms = P::dm_min_interval_ms;   // v16 anti-spam per-leaf defaults
    if (!mrnv::save(b)) { out.println(F("> leave err nv_save_failed")); return; }
    provision_apply_live(b, /*do_dad=*/false);                              // unprovisioned + idle (no DAD)
    out.print(F("> left network (kept freq=")); out.print(keep_freq, 3); out.println(F(") — idle; `join` to re-provision (live)"));
}

// A DRAINED, CHUNKED console line for the multi-line dumps: emits the WHOLE line even when it doesn't fit the
// current CDC TX FIFO free space — writing only what fits, then yielding to the async USB drainer, and repeating.
// mrcon's whole-chunk write DROPS a line that doesn't fit (the `help` truncation: the two LONGEST lines were
// discarded whole while shorter ones slipped through), so the bulk dumps write here instead. Per-line budget
// (≤40 ms) => a stalled/absent host still never hangs loop(). NOT the radio hot path; bypasses mrcon deliberately.
static void hl(const __FlashStringHelper* fs) {
#if MR_CONSOLE
    if (!Serial) return;
    const char* s = reinterpret_cast<const char*>(fs);   // nRF52/ESP32: F() is memory-mapped flash — a direct read is fine (Print::println does the same)
    const size_t len = strlen(s); size_t off = 0; const uint32_t t0 = millis();
    while (off < len && Serial && (uint32_t)(millis() - t0) < 40) {
        const int a = Serial.availableForWrite();
        if (a > 0) { const size_t rem = len - off; const size_t chunk = (static_cast<size_t>(a) < rem) ? static_cast<size_t>(a) : rem;
                     off += Serial.write(reinterpret_cast<const uint8_t*>(s) + off, chunk); }
        yield();
    }
    if (Serial && Serial.availableForWrite() >= 2) Serial.write(reinterpret_cast<const uint8_t*>("\r\n"), 2);
#else
    (void)fs;
#endif
}

// `help` / `?` — the command + cfg-key reference for the live console session. Grouped with blank separators for
// readability; a category label starts each group, continuation lines indent under it. (hl() writes per-line.)
static void dump_help(Print& out) {
    hl(F("===== MeshRoute console ====="));
    hl(F(""));
    hl(F("MESSAGING"));
    hl(F("  send <id|0xhash> \"<text>\" [-a] [-e] [-t] -a=ack  -e=encrypt(hash only)  -t=team plane; plain send=global/home (fails if no home)"));
    hl(F("  send_channel <ch> \"<text>\""));
    hl(F("  send_layer <0xhash> <l1,l2,…> \"<text>\" [-a]   explicit cross-layer destination path"));
    hl(F(""));
    hl(F("IDENTITY / KEYS"));
    hl(F("  whoami | lookup 0x<hash> | hashof <id> | nameof 0x<hash> | resolve 0x<hash> [hard]   (hashes are 0x-prefixed; hashof prints 0x…)"));
    hl(F("  peerkey <ed_pub hex64>      pin a scanned/QR pubkey"));
    hl(F("  reqpubkey <0xhash|team-id>  request a peer's key on-air (0xhash, or a bare team-id via the team cache)"));
#if MR_N_LAYERS < 2
    hl(F(""));
    hl(F("MOBILE / TEAM  (normal-node only)"));
    hl(F("  mobile register [freq=<MHz> sf=<5-12> bw=<kHz> | scan]     arm registration: current PHY / a given PHY / scan known networks"));
    hl(F("  mobile gateways            list learned gateways + networks"));
    hl(F("  mobile query <gw>          pull a gateway's network directory"));
    hl(F("  mobile status              registration + current PHY + autoregister"));
    hl(F("  team new                   mint a team (become its creator)"));
    hl(F("  team <id> | team 0         join an existing team / leave"));
#endif
    hl(F(""));
    hl(F("INBOX"));
    hl(F("  pull_inbox <dm_since> <chan_since> | mark_read <dm|chan> <seq>       NDJSON out"));
    hl(F(""));
    hl(F("DIAGNOSTICS"));
    hl(F("  routes | status | duty | limits | cfg | cfg set <k> <v>"));
    hl(F("  sleep [on|off] | debug [on|off] | regen | reboot | ota"));
    hl(F("  version            build/git/board + last reset (no reset)"));
    hl(F("  faults             the flash fault ring"));
    hl(F("  crashtest <hang|fault|reboot>      (needs `debug on`)"));
    hl(F("  rcmd <dst> <verb>      remote command via DM. status/routes = OPEN (cleartext); everything else = SEALED (needs `unlock`)"));
    hl(F("  prep-restart       clear routes+inbox, KEEP join, go DORMANT (run fleet-wide, then power-cycle)"));
#if MR_FEAT_REMOTE_MGMT
    hl(F(""));
    hl(F("REMOTE MANAGEMENT  (authenticated; static/gateway builds only)"));
    hl(F("  password <pass>        LOCAL-only: pin the fleet admin pubkey (derive from the passphrase) — set on every node"));
    hl(F("  unlock <pass> | lock   operator device: derive the admin key into RAM to sign `rcmd`s / wipe it"));
    hl(F("    then: `rcmd <dst> reboot` (etc.) is sealed to <dst>; `reqpubkey 0x<hash>` first if the target's pubkey isn't cached"));
#endif
    hl(F(""));
    hl(F("TEST"));
    hl(F("  route add <dest> <next_hop> <hops> [score_q4] | route del <dest>"));
    hl(F("  testsend <dst> <run> [-a] [-e] -t ms1,ms2,… | testch <ch> <run> -t ms1,ms2,… | teststatus | testclear"));
    hl(F("  factory_reset confirm      WIPE all flash (config+identity+peers+inbox) -> factory reboot"));
    hl(F(""));
    hl(F("PROVISIONING     (key=value, order-free; LIVE, no reboot)"));
    hl(F("  create layer= freq= bw= sf= sf_list= duty= name=\"<n>\" [active_fraction=] [ch_min_ms=] [dm_min_ms=]"));
    hl(F("  join layer= freq= bw= sf=      |  leave              layer=1..255 network id (leaf = layer & 0x0F)"));
    hl(F("  gateway l0=<layer>:<node>:<ctrl_sf>:<data_sfs> l1=…  [period=] [win0=ms:off] [win1=] [beacon=] [freq0=] [freq1=] [bw0=] [bw1=] [cr0=] [cr1=] [gateway_only=]"));
    hl(F("    dual-layer -> NV, reboot to apply.  e.g. gateway l0=1:1:8:7,9 l1=2:1:9:9,10"));
    hl(F(""));
    hl(F("CFG KEYS  (`cfg set <key> <val>`; bool keys take on|off / 1|0)"));
    hl(F("  node_id name freq routing_sf bw cr tx_power sf_list lbt beacon_ms duty nav nav_ignore hop_cap leaf_id"));
    hl(F("  mobile team_id mobile_autoregister host_mobiles intra_layer_relay gateway_only"));
    hl(F("  lat lon loc_in_dm e2e_dm ble_mode ble_period ble_pin gw_announce_pct gw_announce_interval gw_herd_slack"));
    hl(F("  active_fraction ch_min_ms dm_min_ms leaf_name"));
    hl(F("    `name`=node identity · `leaf_name`=managed leaf (bumps epoch) · team_id=0x-hex (`team new` mints) · identity via `regen`"));
    hl(F("  gateway-only keys: n_layers layer0_id window_period_ms l0_window_ms l0_window_offset_ms l1_layer_id l1_node_id l1_routing_sf l1_sf_list l1_beacon_ms l1_window_ms l1_window_offset_ms l1_freq"));
}

// ---- Phase-3 inbox sync (schema: ios-companion/INBOX_SYNC_CONTRACT.md) -----------------------------------
// `pull_inbox <dm_since> <chan_since>` streams the inbox (DM block then channel block, oldest-first) + an
// inbox_end terminator; `mark_read <dm|chan> <seq>` advances the per-store read cursor. Both stream NDJSON to a
// transport SINK (USB Serial OR the BLE NUS), so one handler serves both consoles. The companion link is JSON;
// on USB it's structured output for the host harness.
// §command-sink-consolidation: the JSON handlers take `Print& out` (mrcon on USB, a LineSink over BLE). ble_sink is the
// LineSink flush callback — ship one NDJSON line over BLE NUS. (usb_sink is gone: USB callers pass the global `mrcon`.)
static void ble_sink(const char* s, size_t n) { mrble::tx_line(s, n); }   // inert off-XIAO / when no client

namespace { struct PullCtx { Print& out; uint32_t count; }; }
char s_inbox_jb[1700];   // shared NDJSON line scratch: pulled inbox records AND live-push lines (loop()) — sequential, single-threaded, never concurrent (241-B body 6x-escaped + envelope); extern in fw_context.h

// `limits` verb (USB): the companion anti-spam/headroom snapshot as one NDJSON line. Composed from limits_snapshot()
// then serialized via write_limits() into s_inbox_jb (declared just above) — same pattern as the other JSON dumps. A
// local-only read (no OTA change): NOT in the rcmd remote allow-list. Mirrors the BLE `limits` handler.
static void dump_limits(Print& out) {
    const auto s = g_node.limits_snapshot();
    meshroute::console::LimitsFields L;
    L.win_ms = s.win_ms; L.win_left_ms = s.win_left_ms; L.n = s.n; L.ch_sf = s.ch_sf;
    L.ch_cap = s.ch_cap; L.ch_used = s.ch_used; L.ch_min_ms = s.ch_min_ms;
    L.ch_next_ms = s.ch_next_ms; L.ch_ceiling = s.ch_ceiling;
    L.dm_min_ms = s.dm_min_ms; L.dm_next_ms = s.dm_next_ms;
    L.duty_ms = s.duty_ms; L.duty_used_ms = s.duty_used_ms;
    const size_t m = meshroute::console::write_limits(s_inbox_jb, sizeof s_inbox_jb, L);
    if (m) out.write(s_inbox_jb, m);   // JSON line to USB (mirrors the other write_* dumps)
}

// pull() callback: format ONE record -> JSON -> sink. The body ptr is valid only for this call (the encoder copies it).
static bool inbox_pull_cb(void* vctx, const meshroute::InboxEntry& e) {
    PullCtx* c = static_cast<PullCtx*>(vctx);
    const size_t n = (e.kind == meshroute::InboxKind::dm)
        ? meshroute::console::write_inbox_dm(s_inbox_jb, sizeof s_inbox_jb, e.seq, e.origin, e.layer_id,
              static_cast<uint16_t>(e.msg_id), e.sender_hash, e.rx_time_ms,
              reinterpret_cast<const char*>(e.body), e.body_len, e.enc != 0, e.type)   // §8b enc + the DATA_TYPE (E2E-ack receipt = "e2e_ack")
        : meshroute::console::write_inbox_channel(s_inbox_jb, sizeof s_inbox_jb, e.seq, e.origin, e.layer_id,
              e.channel_id, e.msg_id, e.rx_time_ms, reinterpret_cast<const char*>(e.body), e.body_len);
    if (n) { c->out.write(s_inbox_jb, n); ++c->count; }
    else {                                                // UNREACHABLE for a valid body (<=241 B fits 1700), but
        char eb[48];                                      // NEVER drop a record silently: tell the app one didn't encode.
        const size_t en = meshroute::console::write_err(eb, sizeof eb, "inbox_encode", nullptr);
        if (en) c->out.write(eb, en);
    }
    return true;                                          // never stop early — the app pulls the whole delta
}
static void handle_pull_inbox(const char* args, Print& out) {
    char* end;
    const uint32_t dm_since   = strtoul(args, &end, 10);  // missing/garbled args -> 0 (a full pull is always safe; the app dedups)
    const uint32_t chan_since = strtoul(end, nullptr, 10);
    meshroute::Inbox& ib = g_node.inbox();
    PullCtx ctx{ out, 0 };
    ib.pull(dm_since, chan_since, inbox_pull_cb, &ctx);
    // inbox_end carries the store's NEWEST seq per store (contract §"newest seq per store"), NOT a cursor echo —
    // so an empty store / a stale-high cursor self-heals (the app advances to the real high-water, re-syncing).
    const size_t n = meshroute::console::write_inbox_end(s_inbox_jb, sizeof s_inbox_jb,
                       ib.dm_newest_seq(), ib.chan_newest_seq(), ib.storage_epoch(), ctx.count, g_hal.now());
    if (n) out.write(s_inbox_jb, n);
}
static void handle_mark_read(const char* args, Print& out) {
    while (*args == ' ') ++args;
    // The kind must be EXACTLY "dm" or "chan" (word boundary = next char is space or end). Without the boundary
    // check, "dm5"/"dme" match strncmp("dm",2) and "channel" matches strncmp("chan",4) -> wrong/zero seq parsed.
    meshroute::InboxKind kind; const char* kstr;
    if      (!strncmp(args, "dm", 2)   && (args[2] == ' ' || args[2] == '\0')) { kind = meshroute::InboxKind::dm;      kstr = "dm";   args += 2; }
    else if (!strncmp(args, "chan", 4) && (args[4] == ' ' || args[4] == '\0')) { kind = meshroute::InboxKind::channel; kstr = "chan"; args += 4; }
    else { char eb[64]; const size_t n = meshroute::console::write_err(eb, sizeof eb, "mark_read", "kind must be dm|chan");
           if (n) out.write(eb, n); return; }             // fail loud on a bad kind
    const uint32_t seq = strtoul(args, nullptr, 10);
    g_node.inbox().mark_read(kind, seq);
    char ab[64]; const size_t n = meshroute::console::write_inbox_marked(ab, sizeof ab, kstr, seq);
    if (n) out.write(ab, n);
}

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

// Firmware scheduled-send (spec 2026-06-24): arm the node to fire DMs/channel posts on an ms-offset schedule OVER THE
// RADIO, so the oracle touches USB only to arm + read (killing the continuous-stream USB-CDC death). `testsend <dst>
// <run> [-a] [-e] -t ms1,ms2,…` / `testch <ch> <run> -t ms1,ms2,…` — APPENDS (seq keeps counting). Offsets are ms
// from NOW (arm). The fired body = the harness tag `T<run>S<self>#<seq>` + `@<sendms>` (built in the loop tick).
static void handle_testsched(char* args, bool is_channel, Print& out) {
    char* toks[12]; int nt = 0;
    for (char* p = strtok(args, " "); p && nt < 12; p = strtok(nullptr, " ")) toks[nt++] = p;
    if (nt < 2) { out.println(F("> err usage: testsend <dst> <run> [-a] [-e] -t ms1,ms2,…  |  testch <ch> <run> -t ms1,ms2,…")); return; }
    const char* dst_s = toks[0];
    const char* run_s = toks[1];
    const char* list_s = nullptr; bool ack = false, enc = false;
    for (int i = 2; i < nt; ++i) {
        if      (!strcmp(toks[i], "-a")) ack = true;
        else if (!strcmp(toks[i], "-e")) enc = true;
        else if (!strcmp(toks[i], "-t") && i + 1 < nt) list_s = toks[i + 1];
    }
    if (!list_s) { out.println(F("> testsched err: missing -t <ms,ms,…>")); return; }
    // <run> must be ALNUM — the host reconcile regex `T([0-9A-Za-z]+)S…` only matches alnum, so a hyphen/dot/_ run
    // would send a body the harness CAN'T parse -> every message silently unreconciled. Fail loud instead.
    for (const char* q = run_s; *q; ++q)
        if (!((*q >= '0' && *q <= '9') || (*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z'))) {
            out.println(F("> testsched err: <run> must be alphanumeric (the host tag regex)")); return; }
    uint32_t target = 0, v = 0; uint8_t flags = 0;
    if (is_channel) {
        if (ack || enc) { out.println(F("> testch err: -a/-e not valid on a channel")); return; }   // matches send_channel (rejects them)
        if (!mrsched::parse_dec(dst_s, 255, v)) { out.println(F("> testch err: channel 0..255")); return; }
        target = v; flags |= mrsched::kChannel;
    } else {
        uint32_t h;
        if (mrsched::parse_hash8(dst_s, h)) { target = h; flags |= mrsched::kHash; }
        else if (mrsched::parse_dec(dst_s, 254, v) && v >= 1) { target = v; }
        else { out.println(F("> testsend err: dst 1..254 or 8-hex hash")); return; }
        if (enc && !(flags & mrsched::kHash)) { out.println(F("> testsend err: -e (encrypt) needs an 8-hex hash dst")); return; }   // matches `send` (allow_e=by_hash)
        if (ack) flags |= mrsched::kAck;
        if (enc) flags |= mrsched::kEnc;
    }
    g_sched.set_run(run_s);
    uint32_t offs[128];
    const uint16_t no = mrsched::parse_offsets(list_s, offs, 128);
    if (no == 0) { out.println(F("> testsched err: no offsets parsed")); return; }
    if (no == 128) out.println(F("> testsched warn: offset list capped at 128 — split into more lines"));   // never silent
    const uint32_t base = (uint32_t)g_hal.now();
    uint16_t added = 0;
    for (uint16_t i = 0; i < no; ++i) if (g_sched.add(base + offs[i], target, flags) >= 0) ++added;
    out.print(F("> ")); out.print(is_channel ? F("testch") : F("testsend"));
    out.print(F(" run="));   out.print(g_sched.run);
    out.print(is_channel ? F(" ch=") : F(" dst=")); out.print(dst_s);
    out.print(F(" +"));      out.print(added);
    out.print(F(" armed=")); out.print(g_sched.armed());
    if (added < no) out.print(F(" (SCHED FULL — rest dropped)"));
    out.println();
}

static void handle_teststatus(Print& out) {
    const uint32_t mnow = (uint32_t)g_hal.now();
    const int32_t nx = g_sched.next_offset_ms(mnow);
    const char* state = (g_sched.armed() == 0) ? "idle" : (g_sched.done() ? "done" : "running");
    out.print(F("[teststatus] run=")); out.print(g_sched.run[0] ? g_sched.run : "-");
    out.print(F(" armed="));    out.print(g_sched.armed());
    out.print(F(" fired="));    out.print(g_sched.fired);
    out.print(F(" deferred=")); out.print(g_sched.deferred);
    out.print(F(" dropped="));  out.print(g_sched.dropped);
    out.print(F(" next="));     if (nx < 0) out.print('-'); else { out.print('+'); out.print(nx); }
    out.print(F(" state="));    out.println(state);
}

#if MR_FEAT_REMOTE_MGMT
// `password <passphrase>` — LOCAL-ONLY (a dispatch verb; NEVER accepted over the mesh — remote_exec has no such verb).
// Derive the admin keypair (iterated-BLAKE2b -> identity_from_seed), pin admin_pubkey to NV, reset the replay floor,
// then discard the derived keypair (the credential lives in the operator's head, not the node — spec §2/§8).
static void handle_password(const char* args, Print& out) {
    while (*args == ' ') ++args;
    size_t n = strlen(args);
    while (n && (args[n-1]=='\r' || args[n-1]=='\n' || args[n-1]==' ')) --n;
    if (n == 0) { out.println(F("> password err: usage `password <passphrase>` (local only)")); return; }
    meshroute::Identity admin{};
    out.println(F("> deriving admin key (a few seconds)..."));   // the KDF blocks; tell the operator it's not hung
    meshroute::admin_key_from_password(args, n, admin, []{ mrfault::fault_wdt_feed(); });   // feed the WDT during the multi-second stretch
    g_node.admin_set_pubkey(admin.ed_pub);
    mrnv::Blob b{}; if (!mrnv::load(b)) seed_blob_from_live(b);
    b.magic = mrnv::kMagic; b.version = mrnv::kVersion;
    for (int i = 0; i < 32; ++i) b.admin_pubkey[i] = admin.ed_pub[i];
    b.admin_provisioned = 1; b.admin_counter_floor = 0;      // fresh credential -> reset the replay floor
    const bool saved = mrnv::save(b);
    memset(&admin, 0, sizeof admin);                          // discard the derived keypair (best-effort local wipe)
    if (!saved) { out.println(F("> password err: nv_save_failed")); return; }
    out.print(F("> admin pubkey pinned (fp "));               // print only a 4-byte fingerprint, NEVER the pubkey/pw
    const uint8_t* pk = g_node.admin_pubkey();
    for (int i = 0; i < 4 && pk; ++i) { char hx[3]; snprintf(hx, sizeof hx, "%02X", pk[i]); out.print(hx); }
    out.println(F(")"));
}
#endif

static bool dispatch(const char* line, size_t len, Print& out) {   // §command-sink-consolidation: the single line->handler verb map (was service_debug); every response goes to `out`
    if ((len == 4 && !strncmp(line, "help", 4)) || (len == 1 && line[0] == '?')) { dump_help(out); return true; }
    if (len == 7 && !strncmp(line, "version", 7))  { print_banner(out); return true; }
    if (len == 6 && !strncmp(line, "faults", 6))   { dump_faults(out);  return true; }
    if (len == 12 && !strncmp(line, "prep-restart", 12)) { handle_prep_restart(out); return true; }
    if ((len == 4 || (len > 4 && line[4] == ' ')) && !strncmp(line, "rcmd", 4)) { handle_rcmd(line + 4, out); return true; }
    if (len == 9 && !strncmp(line, "testclear", 9))    { g_sched.clear(); out.println(F("> testsched cleared")); return true; }
    if (len == 10 && !strncmp(line, "teststatus", 10)) { handle_teststatus(out); return true; }
    if ((len == 8 || (len > 8 && line[8] == ' ')) && !strncmp(line, "testsend", 8)) {   // strtok needs a mutable copy
        static char tb[512]; strncpy(tb, line + 8, sizeof tb - 1); tb[sizeof tb - 1] = '\0'; handle_testsched(tb, /*channel=*/false, out); return true; }
    if ((len == 6 || (len > 6 && line[6] == ' ')) && !strncmp(line, "testch", 6)) {
        static char tb[512]; strncpy(tb, line + 6, sizeof tb - 1); tb[sizeof tb - 1] = '\0'; handle_testsched(tb, /*channel=*/true, out);  return true; }
    if ((len == 9 || (len > 9 && line[9] == ' ')) && !strncmp(line, "crashtest", 9)) { handle_crashtest(line + 9, out); return true; }
    if (len == 6 && !strncmp(line, "routes", 6))   { dump_routes(out); return true; }
    if (len > 6 && !strncmp(line, "route ", 6))     { handle_route_cmd(line + 6, out); return true; }   // manual route inject/del (testing)
    if (len == 6 && !strncmp(line, "status", 6))   { dump_status(out); return true; }
    if (len == 4 && !strncmp(line, "duty", 4))     { dump_duty(out);   return true; }
    if (len == 6 && !strncmp(line, "limits", 6))   { dump_limits(out); return true; }   // companion anti-spam/headroom snapshot (local-only)
    if (len == 6 && !strncmp(line, "reboot", 6))   { do_reboot();   return true; }
    if ((len == 13 || (len > 13 && line[13] == ' ')) && !strncmp(line, "factory_reset", 13)) { handle_factory_reset(line + 13, len - 13, out); return true; }
    if (len == 5 && !strncmp(line, "regen", 5))    { do_regen();    return true; }
    if (len == 3 && !strncmp(line, "ota", 3))      { do_ota();      return true; }
    if (len >  8 && !strncmp(line, "gateway ", 8)) { handle_gateway(line + 8, out); return true; }
#if MR_N_LAYERS < 2
    if (len >  5 && !strncmp(line, "join ", 5))    { handle_join(line + 5, out);    return true; }   // R6.3 provisioning verbs (normal-node, live)
    if (len >  7 && !strncmp(line, "create ", 7))  { handle_create(line + 7, out);  return true; }
    if (len >  5 && !strncmp(line, "team ", 5))     { handle_team(line + 5, out);    return true; }   // §mobile 6.1: `team new` (mint) / `team <id>` (join)
#if MR_FEAT_MOBILE
    if (len >  7 && !strncmp(line, "mobile ", 7))   { handle_mobile(line + 7, out);  return true; }   // §mobile console: register/gateways/query/status
#endif
#else   // §config-integrity: create/join are normal-node provisioning -> refuse on the gateway build (mirrors how `gateway` errors on a normal build) — else `create` silently re-provisions the gateway into a managed leaf.
    if ((len > 5 && !strncmp(line, "join ", 5)) || (len > 7 && !strncmp(line, "create ", 7))) {
        out.println(F("> err gateway_build (create/join are normal-node only; use `gateway l0=<layer>:<node>:<sf>:<sfs> l1=…`)"));
        return true;
    }
#endif
    if (len == 5 && !strncmp(line, "leave", 5))    { handle_leave(out);           return true; }
    if (len >  8 && !strncmp(line, "cfg set ", 8)) { handle_cfg_set(line + 8, out); return true; }
    if (len == 3 && !strncmp(line, "cfg", 3))      { dump_cfg(out);    return true; }
    if ((len == 5 || (len > 5 && line[5] == ' ')) && !strncmp(line, "sleep", 5)) { handle_sleep(line + 5, len - 5, out); return true; }
    if ((len == 5 || (len > 5 && line[5] == ' ')) && !strncmp(line, "debug", 5)) { handle_debug(line + 5, len - 5, out); return true; }
    if (len == 6 && !strncmp(line, "whoami", 6)) { handle_whoami(out); return true; }
    if ((len == 6 || (len > 6 && line[6] == ' ')) && !strncmp(line, "lookup", 6)) { handle_lookup(line + 6, len - 6, out); return true; }
    if ((len == 6 || (len > 6 && line[6] == ' ')) && !strncmp(line, "nameof", 6)) { handle_nameof(line + 6, len - 6, out); return true; }   // §1.3 peer name by hash
    if ((len == 6 || (len > 6 && line[6] == ' ')) && !strncmp(line, "hashof", 6)) { handle_hashof(line + 6, len - 6, out); return true; }
    if ((len == 10 || (len > 10 && line[10] == ' ')) && !strncmp(line, "pull_inbox", 10)) { handle_pull_inbox(line + 10, out); return true; }
    if ((len ==  9 || (len >  9 && line[9]  == ' ')) && !strncmp(line, "mark_read",   9)) { handle_mark_read(line + 9,  out); return true; }
#if MR_FEAT_REMOTE_MGMT
    if (len > 9 && !strncmp(line, "password ", 9)) { handle_password(line + 9, out); return true; }   // §remote-mgmt: LOCAL-only admin credential set
    if (len > 7 && !strncmp(line, "unlock ", 7))   { handle_unlock(line + 7, out);   return true; }   // admin-issue: derive the admin key into RAM
    if (len == 4 && !strncmp(line, "lock", 4))     { handle_lock(out);               return true; }   // wipe the unlocked admin key
#endif
    return false;
}

// ---- Node / Network screens over BLE (companion Phase 3 — roadmap Theme D) -------------------------------
// Battery (VBAT) read — pins/divider/formula are the authoritative MeshCore XiaoNrf52Board method, using
// OUR variant.h macros (VBAT_ENABLE=D14/P0.14, PIN_VBAT=D32/P0.31/AIN7, ADC_MULTIPLIER=3.0, AREF=3.0 V):
//   mV = adc × ADC_MULTIPLIER × AREF_VOLTAGE / 4.096.
// initVariant() already holds VBAT_ENABLE LOW (divider always enabled — reading costs no extra power).
// An implausible read (USB-only / no cell / floating pin) returns -1 ⇒ the field is OMITTED, never garbage.
// Compile out with -DMR_NO_BATT (or on a board without PIN_VBAT, e.g. Heltec).
static int32_t read_batt_mv() {
#if !defined(NRF52_PLATFORM) || defined(MR_NO_BATT) || !defined(PIN_VBAT)
    return -1;   // non-nRF52 (Heltec/ESP32 uses a different ADC API), compiled out, or no VBAT divider
#else
    static bool adc_ready = false;
    if (!adc_ready) {                              // configure the ADC once (a reference switch needs settling)
        pinMode(VBAT_ENABLE, OUTPUT); digitalWrite(VBAT_ENABLE, LOW);
        pinMode(PIN_VBAT, INPUT);
        analogReadResolution(12);
        analogReference(AR_INTERNAL_3_0);
        delay(2);
        adc_ready = true;
    }
    const int adc = analogRead(PIN_VBAT);
    const int32_t mv = static_cast<int32_t>((adc * ADC_MULTIPLIER * AREF_VOLTAGE) / 4.096f);
    return (mv > 2000 && mv < 4500) ? mv : -1;     // 1S-LiPo plausible range; else omit
#endif
}

// Build the rich status snapshot from the device globals (the same data dump_status prints on USB).
static meshroute::console::StatusFields make_status_fields() {
    meshroute::console::StatusFields s;
    s.uptime_ms = g_hal.now();
    s.duty_ms   = static_cast<uint32_t>(g_hal.airtime_used_ms(3600000));
    s.txq       = static_cast<uint16_t>(g_hal.txq_depth());
    s.txdrop    = static_cast<uint16_t>(g_hal.txq_drops());
    s.rx        = g_rx_count;
    s.tx        = g_iradio.tx_count();
    s.routes    = g_node.rt_count();
    s.pending   = g_node.has_pending_tx();
    s.lbt       = g_node.config().lbt_enabled;
    s.batt_mv   = read_batt_mv();
    return s;
}
static const char* node_state_str() { return g_node.node_id() == 0 ? "unprovisioned" : "operating"; }

// `routes` over BLE: stream one {"ev":"route",...} per table entry then {"ev":"routes_end","count":N}.
static void handle_routes(Print& out) {
    const uint64_t now = g_hal.now();
    const uint8_t n = g_node.rt_count();
    for (uint8_t i = 0; i < n; ++i) {
        const meshroute::RtEntry& e = g_node.rt_at(i);
        const meshroute::RtCandidate& c = e.candidates[0];
        meshroute::console::RouteRow r;
        r.dest = e.dest; r.next = c.next_hop; r.hops = c.hops; r.score = c.score;
        r.gw = c.is_gateway; r.leaf = c.learned_leaf;
        r.age_ms = static_cast<uint32_t>(now - c.last_seen_ms); r.cand = e.n;
        const size_t m = meshroute::console::write_route(s_inbox_jb, sizeof s_inbox_jb, r);
        if (m) out.write(s_inbox_jb, m);
    }
    const size_t m = meshroute::console::write_routes_end(s_inbox_jb, sizeof s_inbox_jb, n);
    if (m) out.write(s_inbox_jb, m);
}

// Build the cfg extras (device globals not in NodeConfig) for write_cfg.
static meshroute::console::CfgExtras make_cfg_extras() {
    meshroute::console::CfgExtras x;
    x.node_id    = g_node.node_id();
    x.freq_hz    = static_cast<uint32_t>(g_freq_mhz * 1000000.0 + 0.5);
    x.tx_power   = g_tx_power;
    x.duty_x1000 = static_cast<uint32_t>(g_node.config().duty_cycle * 1000.0 + 0.5);
    x.ble_mode   = g_ble_mode == 0 ? "off" : g_ble_mode == 1 ? "on" : "periodic";
    x.ble_period = g_ble_period_min;
    x.ble_pin    = g_ble_pin;
    x.lat_e7     = g_lat_e7;
    x.lon_e7     = g_lon_e7;
    return x;
}

// E2E §2: persist a freshly-installed PINNED peer key to /mrpeers (whole-blob rewrite; update-in-place or append).
// Best-effort — a full store / no-NV target just means it won't survive a reboot (the RAM PINNED key still works).
static bool persist_pinned_peer(uint32_t kh, const uint8_t ed_pub[32]) {
    mrnv::PeerBlob pb{};
    if (!mrnv::load_peers(pb)) { pb = mrnv::PeerBlob{}; pb.magic = mrnv::kPeersMagic; pb.version = mrnv::kPeersVersion; pb.count = 0; }
    for (uint16_t i = 0; i < pb.count && i < mrnv::kMaxPinnedPeers; ++i)
        if (pb.rec[i].key_hash32 == kh) { memcpy(pb.rec[i].ed_pub, ed_pub, 32); return mrnv::save_peers(pb); }   // update in place
    if (pb.count >= mrnv::kMaxPinnedPeers) return false;                                                          // store full
    pb.rec[pb.count].key_hash32 = kh; memcpy(pb.rec[pb.count].ed_pub, ed_pub, 32); pb.count++;
    pb.magic = mrnv::kPeersMagic; pb.version = mrnv::kPeersVersion;
    return mrnv::save_peers(pb);
}
// E2E §3: a `peerkey` command -> install the RAM PINNED key (Node::on_command) + persist to /mrpeers + the contract ack.
static size_t handle_peerkey(char* out, size_t cap, const meshroute::Command& cmd) {
    const uint8_t* ep = cmd.u.peerkey.ed_pub;
    const uint32_t kh = (uint32_t)ep[0] | ((uint32_t)ep[1] << 8) | ((uint32_t)ep[2] << 16) | ((uint32_t)ep[3] << 24);
    if (g_node.on_command(cmd).code != meshroute::CmdCode::queued)        // false only when the cache is full of pinned keys
        return (size_t)snprintf(out, cap, "{\"ev\":\"peerkey_err\",\"reason\":\"full\"}\n");
    persist_pinned_peer(kh, ep);                                          // best-effort NV (bench); the RAM key works regardless
    return (size_t)snprintf(out, cap, "{\"ev\":\"peerkey_set\",\"hash\":%lu,\"pinned\":true}\n", (unsigned long)kh);
}

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
        const size_t m = write_ready(s_inbox_jb, sizeof s_inbox_jb, g_node.node_id(), g_node.key_hash32(), g_node.config(),
                                     "existing", g_node.inbox().storage_epoch(), g_hal.now(), idb.name, nl,
                                     g_identity.ed_pub, ds.pct, ds.avail_ms);   // §4: export pubkey for the QR `p`
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
    // Inbox sync (companion-only): stream the reply via mrble::tx_line and return 0 (no buffered single-line ack).
    if ((len == 10 || (len > 10 && line[10] == ' ')) && !strncmp(line, "pull_inbox", 10)) { LineSink ls(ble_sink); handle_pull_inbox(line + 10, ls); ls.flush(); return 0; }
    if ((len ==  9 || (len >  9 && line[9]  == ' ')) && !strncmp(line, "mark_read",   9)) { LineSink ls(ble_sink); handle_mark_read(line + 9,  ls); ls.flush(); return 0; }
    meshroute::Command cmd{};
    const ParseErr e = parse_command(line, len, cmd);
    if (e == ParseErr::ok) {
        if (cmd.kind == meshroute::CmdKind::peerkey) return handle_peerkey(out, cap, cmd);   // §2/§3: install + persist + contract ack
        const meshroute::CmdResult r = g_node.on_command(cmd);
        if (cmd.kind == meshroute::CmdKind::reqpubkey && r.code == meshroute::CmdCode::queued) {
            uint32_t rh = cmd.u.resolve.dst_hash;                                       // §enc: a by-team-id reqpubkey resolved the hash at execution -> echo the RESOLVED hash
            if (rh == 0 && cmd.u.resolve.dst_id != 0) (void)g_node.team_key_of_id(cmd.u.resolve.dst_id, rh);
            return write_reqpubkey_sent(out, cap, rh);   // §2: the contract's reqpubkey_sent event (the no-identity fail path keeps its existing error ack)
        }
        return write_ack(out, cap, r);
    }
    if (e == ParseErr::empty) return 0;
    if (len >= 8 && !strncmp(line, "peerkey ", 8))                                            // §3: a malformed peerkey -> the contract's peerkey_err
        return (size_t)snprintf(out, cap, "{\"ev\":\"peerkey_err\",\"reason\":\"bad_hex\"}\n");
    // §command-sink-consolidation: not a companion JSON verb and not a Node command -> offer the FULL console surface as
    // canonical text over BLE via the unified dispatch. ADDITIVE: every companion verb is handled above, so this only
    // catches lines that previously returned "unknown_cmd" (team/mobile/gateway/faults/help/lookup/…). The BLE link is
    // the authenticated (MITM-passkey) admin transport. (reboot/regen/ota/factory_reset become reachable here too —
    // factory_reset still requires its `confirm` token; flag for review if the console should stay USB-only.)
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
    g_hal.set_debug_hooks([]() -> bool { return meshroute::g_mr_trace_on; },
                          [](const char* m) { if (meshroute::g_mr_trace_on) mrcon.println(m); });
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
    mrnv::save_faults(g_fault_log);
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
    cfg.radio_bw_hz           = (uint32_t)(LORA_BW * 1000.0);    // keep the Node's airtime math == the radio's BW
    cfg.radio_cr              = LORA_CR;
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
        cfg.routing_sf        = nv.routing_sf;
        cfg.allowed_sf_bitmap = nv.allowed_sf_bitmap;
        cfg.radio_bw_hz       = nv.bw_hz;        cfg.radio_cr     = nv.cr;
        cfg.duty_cycle        = nv.duty;         cfg.lbt_enabled  = nv.lbt != 0;
        cfg.beacon_period_ms  = nv.beacon_ms;
        g_tx_power            = (nv.version >= 3) ? nv.tx_power : (int8_t)LORA_TX_POWER;   // v2 blob had no tx_power -> keep the default
        cfg.is_gateway        = nv.is_gateway != 0;   cfg.gateway_only = nv.gateway_only != 0;   // v6 role/topology (only v6 blobs load -> always present)
        cfg.is_mobile         = nv.is_mobile != 0;    cfg.leaf_id      = nv.leaf_id;
        cfg.team_id           = nv.team_id;           // §mobile 6.1: team-id overlay (0 = no team)
        cfg.mobile_autoregister = nv.mobile_autoregister != 0;   // §mobile console: autonomy toggle (a valid v18 NV was seeded from the ON default)
        g_ble_mode            = nv.ble_mode;          g_ble_period_min = nv.ble_period_min;      // v7 BLE policy (only v7 blobs load)
        g_ble_pin             = nv.ble_pin;
        cfg.loc_in_dm         = (nv.loc_in_dm != 0);                                                // v9 location piggyback toggle
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
    cfg.lat_e7 = g_lat_e7; cfg.lon_e7 = g_lon_e7;             // feed the node's location to the DM piggyback (loc_in_dm)
    // §remote-mgmt (v20): restore the pinned admin pubkey + replay counter floor (no-op stub when MR_FEAT_REMOTE_MGMT=0).
    g_node.admin_load(nv.admin_pubkey, nv.admin_counter_floor, nv.admin_provisioned);
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
    if (cfg.allowed_sf_bitmap) { print_sf_list(cfg.allowed_sf_bitmap); mrcon.println(F("  (receiver picks the fastest by SNR)")); }
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
    // E2E §2: reload the PINNED peer keys (/mrpeers) so a QR-scanned contact survives a reboot — re-install each as
    // PeerKeyConf::pinned (never on-air-overwritten/evicted/aged). After on_init so the LayerRuntime _active is live.
    { mrnv::PeerBlob pb{}; if (mrnv::load_peers(pb) && pb.count <= mrnv::kMaxPinnedPeers)
        for (uint16_t i = 0; i < pb.count; ++i)
            g_node.peer_key_set(pb.rec[i].key_hash32, pb.rec[i].ed_pub, meshroute::Node::PeerKeyConf::pinned); }
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
                    } else {
                    const meshroute::CmdResult r = g_node.on_command(cmd);
                    mrcon.print(F("> "));
                    mrcon.print(r.code == meshroute::CmdCode::queued ? F("queued ctr=") : F("err ctr="));
                    mrcon.print(r.ctr); mrcon.print(F(" depth=")); mrcon.print(r.queue_depth);
                    // The send handle for hash/layer-addressed sends (dh != 0 = correlate by hash, not id).
                    if (r.dst_hash) { mrcon.print(F(" dh=0x")); mrcon.print(r.dst_hash, HEX); }
                    if (r.layer_path) { mrcon.print(F(" lp=0x")); mrcon.print(r.layer_path, HEX); }
                    mrcon.println();
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
    mrnv::Blob b{};
    if (!mrnv::load(b)) {                                            // no blob yet -> seed from live config
        const meshroute::NodeConfig& nc = g_node.config();
        b.freq_mhz = g_freq_mhz;        b.bw_hz = nc.radio_bw_hz;   b.beacon_ms = nc.beacon_period_ms;
        b.duty = nc.duty_cycle;         b.allowed_sf_bitmap = nc.allowed_sf_bitmap;
        b.routing_sf = nc.routing_sf;   b.cr = nc.radio_cr;
        b.lbt = nc.lbt_enabled ? 1 : 0; b.tx_power = g_tx_power;
        b.is_gateway = nc.is_gateway ? 1 : 0; b.gateway_only = nc.gateway_only ? 1 : 0;   // v6 role/topology
        b.is_mobile  = nc.is_mobile ? 1 : 0;  b.leaf_id      = nc.leaf_id;  b.team_id = nc.team_id; b.mobile_autoregister = nc.mobile_autoregister ? 1 : 0; b.team_local_id = g_node.team_local_id();   // §mobile: preserve team + autoreg + team-DAD id across create/join
        b.ble_mode   = g_ble_mode;            b.ble_period_min = g_ble_period_min;        // v7 BLE policy (live globals)
        b.ble_pin    = g_ble_pin;
        b.loc_in_dm  = nc.loc_in_dm ? 1 : 0;                  // v9 location toggle (seed from the live config)
        b.e2e_dm     = nc.e2e_dm ? 1 : 0;                     // v10 e2e encrypt toggle (seed from the live config)
        b.gw_announce_duty_pct        = nc.gw_announce_duty_pct;        // v11 gateway noise control (seed from the live config)
        b.gw_announce_min_interval_ms = nc.gw_announce_min_interval_ms;
        b.l1_freq_mhz                 = nc.layers[1].freq_mhz;          // v12 per-layer freq (0 = inherit layer 0)
        b.l1_bw_hz                    = nc.layers[1].bw_hz;             // v17 per-layer BW (0 = inherit the global)
        b.l1_cr                       = nc.layers[1].cr;               // v17 per-layer CR (0 = inherit)
        b.gw_herd_slack               = nc.gw_herd_slack;              // v13 §3e herd-spread slack
        b.lineage_id = nc.lineage_id; b.config_epoch = nc.config_epoch; b.leaf_name_len = nc.leaf_name_len;     // v14 R6.1 leaf-config
        for (uint8_t i = 0; i < nc.leaf_name_len && i < sizeof(b.leaf_name); ++i) b.leaf_name[i] = (uint8_t)nc.leaf_name[i];
        b.channel_active_fraction = nc.channel_active_fraction; b.channel_min_interval_ms = nc.channel_min_interval_ms; b.dm_min_interval_ms = nc.dm_min_interval_ms;   // v16 anti-spam per-leaf tunables
    }
    b.magic = mrnv::kMagic; b.version = mrnv::kVersion;
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
            mrcon.print(F(" data_sf="));       print_sf_list(c.allowed_sf_bitmap);
            mrcon.println();   // (was Serial.flush() — dropped Part 3: a loop-body flush only risks a stall; the USB task drains the FIFO)
        }
    }

    // 2b) Async TX: drain the in-flight TX completion (radio re-arms RX) + start the next queued frame.
    //     After RX + timers, since both enqueue TX. The loop stays live during a long TX (no freeze).
    g_hal.service_tx();
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
        switch (pu.kind) {
            case meshroute::PushKind::msg_recv:
                mrcon.print(F("RECV from="));   mrcon.print(pu.origin); mrcon.print(F(": "));
                mrcon.write(pu.body, pu.body_len); mrcon.println();
                break;
            case meshroute::PushKind::channel_recv:
                mrcon.print(F("CH ")); mrcon.print(pu.channel_id);
                mrcon.print(F(" from=")); mrcon.print(pu.origin); mrcon.print(F(": "));
                mrcon.write(pu.body, pu.body_len); mrcon.println();
                break;
            case meshroute::PushKind::send_acked:
                mrcon.print(F("ACKED ctr="));    mrcon.println(pu.ctr); break;
            case meshroute::PushKind::send_failed:
                mrcon.print(F("FAILED ctr="));   mrcon.print(pu.ctr);
                switch (pu.reason) {   // §mobile: surface WHY so a fail-loud is actionable (was ctr-only)
                    case meshroute::SendFailReason::mobile_no_home: mrcon.print(F(" (mobile not registered — no home to route the reply; register first)")); break;
                    case meshroute::SendFailReason::no_pubkey:      mrcon.print(F(" (no recipient pubkey)")); break;
                    case meshroute::SendFailReason::no_identity:    mrcon.print(F(" (no crypto identity)")); break;
                    case meshroute::SendFailReason::too_large:      mrcon.print(F(" (payload too large)")); break;
                    case meshroute::SendFailReason::joining:        mrcon.print(F(" (joining — config not synced)")); break;
                    default: break;
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
            case meshroute::PushKind::config_adopted: {   // R6.2: a pulled leaf config was adopted -> persist to NV
                const meshroute::NodeConfig& nc = g_node.config();
                mrnv::Blob b{};
                if (mrnv::load(b)) {
                    b.lineage_id = nc.lineage_id; b.config_epoch = nc.config_epoch;
                    b.allowed_sf_bitmap = nc.allowed_sf_bitmap; b.duty = nc.duty_cycle;
                    b.channel_active_fraction = nc.channel_active_fraction; b.channel_min_interval_ms = nc.channel_min_interval_ms; b.dm_min_interval_ms = nc.dm_min_interval_ms;   // v16: persist the adopted anti-spam knobs (they're in the C-frame)
                    b.leaf_name_len = nc.leaf_name_len;
                    for (uint8_t i = 0; i < nc.leaf_name_len && i < sizeof(b.leaf_name); ++i) b.leaf_name[i] = (uint8_t)nc.leaf_name[i];
                    b.magic = mrnv::kMagic; b.version = mrnv::kVersion;
                    mrnv::save(b);
                }
                mrcon.print(F("LEAF-CONFIG adopted lineage=")); mrcon.print(nc.lineage_id);
                mrcon.print(F(" epoch=")); mrcon.println(nc.config_epoch);
                break;
            }
            case meshroute::PushKind::join_refused:        // R6.3 §7c: refusal feedback (telemetry is invisible on metal)
                if (pu.join_reason == meshroute::JoinRefuseReason::wire_version) {
                    mrcon.print(F("⚠ JOIN REFUSED: network wire v")); mrcon.print(pu.origin);
                    mrcon.print(F(", this node v")); mrcon.print(pu.dst); mrcon.println(F(" — update firmware"));
                } else {
                    mrcon.println(F("⚠ JOIN REFUSED: leaf full — no id available"));
                }
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
    if (may_sleep && !g_iradio.tx_busy() && g_hal.txq_depth() == 0 && !serial_has_input() && !mrble::connected()) {
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
