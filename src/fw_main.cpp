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
#include "identity.h"
#include "command.h"
#include "console_parse.h"
#include "device_nv.h"
#include "device_inbox_store.h"
#include "fixed_inbox_store.h"   // the interim VOLATILE RAM inbox (until the durable QSPI records backend lands)
#include "device_rng.h"
#include "console_json.h"    // write_ack/write_push/write_ready/write_err — the BLE companion's JSON twin
#include "device_ble.h"      // BLE companion transport (XIAO nRF52840; an inert no-op on ESP32/native)
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

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
    #include <driver/rtc_io.h>            // rtc_gpio_is_valid_gpio
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
static Module                  g_mod(LORA_PIN_NSS, LORA_PIN_DIO1, LORA_PIN_RST, LORA_PIN_BUSY);
static CustomSX1262            g_radio(&g_mod);
static meshroute::ArduinoClock g_clock;
static meshroute::Sx1262Radio  g_iradio(g_radio);
static meshroute::DeviceHal    g_hal(g_clock, g_iradio);
static meshroute::Node         g_node(g_hal, /*node_id=*/0, /*key_hash32=*/0, "node");   // identity set in setup() from /mrid
// Inbox stores. The durable QSPI/LittleFS DeviceInboxStore records backend is a bench-TODO (its begin() fails ->
// inbox disabled), so until MRINBOX_QSPI_READY lands we install the INTERIM volatile FixedInboxStore: a bounded
// RAM ring so record-on-delivery + pull_inbox actually WORK on metal (session-scoped history the iOS companion
// can sync today). Lost on reboot; the per-boot epoch (set in setup) makes the app re-pull after a node reboot.
#ifndef MR_RAM_INBOX_SLOTS
#define MR_RAM_INBOX_SLOTS 32           // interim RAM inbox depth per store (~8.5 KB/store at 272-B slots)
#endif
#if defined(MRINBOX_QSPI_READY)
static mrinbox::DeviceInboxStore g_inbox_dm("/dm", "/mri_dm", meshroute::protocol::inbox_dm_store_bytes,   mrinbox::kSegScratchBytes);
static mrinbox::DeviceInboxStore g_inbox_ch("/ch", "/mri_ch", meshroute::protocol::inbox_chan_store_bytes, mrinbox::kSegScratchBytes);
#else
static meshroute::FixedInboxStore<MR_RAM_INBOX_SLOTS> g_inbox_dm;
static meshroute::FixedInboxStore<MR_RAM_INBOX_SLOTS> g_inbox_ch;
#endif
static meshroute::Identity     g_identity{};                                            // seed -> Ed25519/X25519 + key_hash32

static uint8_t  g_rxbuf[P::max_payload_bytes_hard_cap + 32];
static bool     g_radio_ok = false;   // SX1262 std_init result — surfaced in the heartbeat below
static uint32_t g_rx_count = 0;       // frames received (status diagnostic)
static uint32_t g_sleep_count = 0;    // idle light-sleep entries (status `slept=`); climbs = the gate fires, stuck = never sleeps
static bool     g_host_present = false; // a console byte was seen this boot -> a human is here -> stay awake (MeshCore inhibit_sleep)
static bool     g_force_sleep  = false; // the `sleep` console command -> light-sleep when idle even with a host present
static double   g_freq_mhz = LORA_FREQ;   // live operating freq (compile default; Slice-2 NV will override at boot)
static int8_t   g_tx_power = LORA_TX_POWER;   // live TX power (dBm); NV `cfg set tx_power` overrides at boot
// BLE companion policy (NV v7; read at boot, reboot-to-apply). Compile defaults = the documented bare-metal
// node: off / 15-min periodic window / PIN 123456 (spec §4 + §A.3). A v7 blob overrides these at boot.
static uint8_t  g_ble_mode = 0;            // 0=off (bare-metal), 1=on, 2=periodic
static uint8_t  g_ble_period_min = 15;     // periodic-mode advertising period (minutes)
static uint32_t g_ble_pin = 123456;        // 6-digit pairing passkey
static uint8_t  g_persist_id = 0, g_persist_epoch = 0, g_persist_join = 0;   // last DAD lease state written to NV (change-detect)

// ---- device-console diagnostics (host tool: tools/meshroute_client.py) ---------------------------
// Print the live routing table in the meshroute_client `routes` wire format.
static void dump_routes() {
    const uint64_t now = g_hal.now();
    Serial.print(F("[routes] n=")); Serial.println(g_node.rt_count());
    for (uint8_t i = 0; i < g_node.rt_count(); ++i) {
        const meshroute::RtEntry& e = g_node.rt_at(i);
        const meshroute::RtCandidate& c = e.candidates[0];           // candidates[0] = the primary next-hop
        Serial.print(F("[route] dest="));   Serial.print(e.dest);
        Serial.print(F(" next="));          Serial.print(c.next_hop);
        Serial.print(F(" hops="));          Serial.print(c.hops);
        Serial.print(F(" score="));         Serial.print(c.score);
        Serial.print(F(" gw="));            Serial.print(c.is_gateway ? 1 : 0);
        Serial.print(F(" layer="));         Serial.print(c.learned_layer_id);
        Serial.print(F(" age_ms="));        Serial.print((uint32_t)(now - c.last_seen_ms));
        Serial.print(F(" cand="));          Serial.println(e.n);
    }
    Serial.println(F("[routes] end"));
}

// allowed_sf_bitmap -> "7,12" CSV (SF index = bit position). 0 = unconfigured.
static void print_sf_list(uint16_t bitmap) {
    bool first = true;
    for (uint8_t sf = 5; sf <= 12; ++sf)
        if (bitmap & (1u << sf)) { if (!first) Serial.print(','); Serial.print(sf); first = false; }
    if (first) Serial.print('-');
}

static void dump_cfg() {
    const meshroute::NodeConfig& c = g_node.config();
    Serial.print(F("[cfg] node_id="));   Serial.print(g_node.node_id());
    Serial.print(F(" freq="));           Serial.print(g_freq_mhz, 4);
    Serial.print(F(" routing_sf="));     Serial.print(c.routing_sf);
    Serial.print(F(" sf_list="));        print_sf_list(c.allowed_sf_bitmap);
    Serial.print(F(" bw="));             Serial.print(c.radio_bw_hz);
    Serial.print(F(" cr="));             Serial.print(c.radio_cr);
    Serial.print(F(" tx_power="));       Serial.print((int)g_tx_power);
    Serial.print(F(" duty="));           Serial.print(c.duty_cycle, 3);
    Serial.print(F(" lbt="));            Serial.print(c.lbt_enabled ? 1 : 0);
    Serial.print(F(" beacon_ms="));      Serial.print(c.beacon_period_ms);
    Serial.print(F(" nav="));            Serial.print(c.nav_enabled ? 1 : 0);
    Serial.print(F(" nav_ignore="));     Serial.print(c.nav_ignore_rts ? 1 : 0);
    Serial.print(F(" hop_cap="));        Serial.print(c.dv_hop_cap);
    Serial.print(F(" leaf_id="));        Serial.print(c.leaf_id);
    Serial.print(F(" gateway="));        Serial.print(c.is_gateway ? 1 : 0);
    Serial.print(F(" gateway_only="));   Serial.print(c.gateway_only ? 1 : 0);
    Serial.print(F(" mobile="));         Serial.print(c.is_mobile ? 1 : 0);
    Serial.print(F(" ble_mode="));       Serial.print(g_ble_mode == 0 ? F("off") : g_ble_mode == 1 ? F("on") : F("periodic"));
    Serial.print(F(" ble_period="));     Serial.print(g_ble_period_min);
    Serial.print(F(" ble_pin="));        Serial.println(g_ble_pin);
    // Dual-layer gateway: an ADDITIVE second line per leaf (single-layer dump above is unchanged). Prints each
    // leaf's node_id/layer_id/routing_sf + the (possibly on_init-derived) window_ms/offset of the active config.
    if (c.n_layers == 2) {
        for (uint8_t li = 0; li < 2; ++li) {
            const meshroute::LayerConfig& L = c.layers[li];
            Serial.print(F("[cfg.layer")); Serial.print(li);
            Serial.print(F("] node_id="));    Serial.print(L.node_id);
            Serial.print(F(" layer_id="));    Serial.print(L.layer_id);
            Serial.print(F(" routing_sf="));  Serial.print(L.routing_sf);
            Serial.print(F(" sf_list="));     print_sf_list(L.allowed_sf_bitmap);
            Serial.print(F(" beacon_ms="));   Serial.print(L.beacon_period_ms);
            Serial.print(F(" window_period_ms=")); Serial.print(L.window_period_ms);
            Serial.print(F(" window_ms="));   Serial.print(L.window_ms);
            Serial.print(F(" window_offset_ms=")); Serial.println(L.window_offset_ms);
        }
    }
}

static void dump_status() {
    Serial.print(F("[status] uptime_ms="));  Serial.print((uint32_t)g_hal.now());
    Serial.print(F(" rx="));                 Serial.print(g_rx_count);
    Serial.print(F(" tx="));                 Serial.print(g_iradio.tx_count());
    Serial.print(F(" isr="));                Serial.print(g_iradio.isr_count());   // DIO1 edges — isr=0 ⇒ pin/mask; isr>0 & rx=0 ⇒ drain/re-arm
    Serial.print(F(" txq="));                Serial.print(g_hal.txq_depth());      // async-TX queue depth (should idle at 0)
    Serial.print(F(" txdrop="));             Serial.print(g_hal.txq_drops());      // outbound-queue overflow drops (should stay 0)
    Serial.print(F(" txto="));               Serial.print(g_hal.tx_timeouts());    // TX-watchdog recoveries — a missed TxDone (should stay 0)
    Serial.print(F(" slept="));              Serial.print(g_sleep_count);          // idle light-sleep entries — climbs = the gate fires (0 = never sleeps)
    Serial.print(F(" sleep="));              Serial.print(g_force_sleep ? F("forced") : (g_host_present ? F("off-host") : F("auto"))); // policy: auto=headless→sleeps, off-host=awake (host seen), forced=`sleep` cmd
    Serial.print(F(" lbt="));                Serial.print(g_node.config().lbt_enabled ? 1 : 0);
    Serial.print(F(" nf="));                 Serial.print(g_iradio.noise_floor(), 0); // LBT noise floor (dBm)
    Serial.print(F(" duty_ms="));            Serial.print((uint32_t)g_hal.airtime_used_ms(3600000));
    Serial.print(F(" routes="));             Serial.print(g_node.rt_count());
    Serial.print(F(" pending="));            Serial.println(g_node.has_pending_tx() ? 1 : 0);
}

// "7,12" -> allowed_sf_bitmap (bit per SF index 5..12); 0 if none valid.
static uint16_t parse_sf_list(const char* s) {
    uint16_t bm = 0; int v = 0; bool have = false;
    for (;; ++s) {
        const char ch = *s;
        if (ch >= '0' && ch <= '9') { v = v * 10 + (ch - '0'); have = true; }
        else { if (have && v >= 5 && v <= 12) bm |= static_cast<uint16_t>(1u << v); v = 0; have = false; if (!ch) break; }
    }
    return bm;
}

// Apply the RADIO operating point from the (just-saved) NV blob LIVE — no reboot. `reconfig` re-tunes the
// radio (freq/SF/BW/CR changed); a tx_power-only change skips the re-tune (it's set per-TX via the Hal).
static void apply_radio_live(const mrnv::Blob& b, bool reconfig) {
    g_freq_mhz = b.freq_mhz;
    g_tx_power = b.tx_power;
    if (reconfig && g_radio_ok) {
        g_radio.standby();                                         // SX1262: RF/modulation params latch in STANDBY
        g_radio.setFrequency((float)b.freq_mhz);
        g_radio.setBandwidth((float)b.bw_hz / 1000.0f);
        g_radio.setCodingRate((uint8_t)b.cr);
        g_iradio.set_rx_sf((int)b.routing_sf);                     // setSpreadingFactor + re-arm RX (+ _rx_sf)
    }
    g_hal.configure(/*sf=*/(int16_t)b.routing_sf, /*bw_hz=*/(int32_t)b.bw_hz, /*cr=*/(int8_t)b.cr,
                    /*preamble=*/(int16_t)P::preamble_sym, /*power=*/(int8_t)b.tx_power, /*busy_hold=*/100);
    g_node.set_radio_cfg((uint8_t)b.routing_sf, (uint32_t)b.bw_hz, (uint8_t)b.cr);
}

// Print the node's key_hash32 (hex, from g_identity) + name (from /mrid). Shared by boot, `status`, `regen`.
static void print_identity(const mrnv::IdBlob& idb) {
    char hx[9];
    snprintf(hx, sizeof hx, "%08lX", (unsigned long)g_identity.key_hash32);
    Serial.print(F("  key_hash32= 0x")); Serial.print(hx);
    if (idb.name_len > 0 && idb.name_len <= sizeof idb.name) {
        Serial.print(F("  name=\""));
        for (uint16_t i = 0; i < idb.name_len; ++i) Serial.print(idb.name[i]);
        Serial.print(F("\""));
    }
    Serial.println();
}

// `regen` — mint a NEW identity (fresh HW-RNG seed) -> persist /mrid -> re-derive -> re-seed the node's
// self binding. Keeps `name` + `node_id` (the short address is independent of the keypair). The new
// key_hash32 propagates on the next beacon; peers re-bind by it (the old one ages out of their id_bind).
static void do_regen() {
    mrnv::IdBlob idb{};
    mrnv::load_id(idb);                                          // preserve the existing name (if any)
    mrrng::fill(idb.seed, sizeof idb.seed);
    idb.magic = mrnv::kIdMagic; idb.version = mrnv::kIdVersion;
    if (!mrnv::save_id(idb)) { Serial.println(F("> regen err nv_save_failed")); return; }
    meshroute::identity_from_seed(g_identity, idb.seed);
    g_node.set_identity(g_node.node_id(), g_identity.key_hash32);
    Serial.print(F("> regen ok"));
    print_identity(idb);
}

// `cfg set <key> <value>` — ACCUMULATES onto the pending NV blob (so several sets + ONE reboot works), then
// applies LIVE to the running node where possible. RADIO knobs (freq/routing_sf|control_sf/bw/cr/tx_power) +
// MAC knobs (sf_list/lbt/beacon_ms) take effect NOW; node_id + duty need a reboot (identity / on_init budget).
// Extra protocol knobs (nav/nav_ignore/hop_cap/leaf_id/gateway) apply live but are NOT persisted yet (reboot reverts).
static void handle_cfg_set(const char* args) {
    char key[20]; size_t k = 0;
    while (args[k] && args[k] != ' ' && k < sizeof(key) - 1) { key[k] = args[k]; ++k; }
    key[k] = '\0';
    const char* val = (args[k] == ' ') ? (args + k + 1) : (args + k);
    if (!*val) { Serial.println(F("> cfg err bad_args")); return; }

    // `name` lives in the IDENTITY record (/mrid), NOT the config blob — handle it separately + early.
    if (!strcmp(key, "name")) {
        mrnv::IdBlob idb{};
        if (!mrnv::load_id(idb)) memcpy(idb.seed, g_identity.seed, sizeof idb.seed);  // keep the RUNNING seed
        size_t l = strlen(val); if (l > sizeof idb.name) l = sizeof idb.name;
        memcpy(idb.name, val, l); idb.name_len = (uint16_t)l;
        idb.magic = mrnv::kIdMagic; idb.version = mrnv::kIdVersion;
        Serial.println(mrnv::save_id(idb) ? F("> cfg ok name (saved to /mrid)") : F("> cfg err nv_save_failed"));
        return;
    }

    // Base = the PENDING NV blob so consecutive sets ACCUMULATE (else each snapshot reverts the others).
    mrnv::Blob b{};
    if (!mrnv::load(b)) {                                  // nothing persisted yet -> seed from the live config
        const meshroute::NodeConfig& nc = g_node.config();
        b.freq_mhz = g_freq_mhz;        b.bw_hz = nc.radio_bw_hz;       b.beacon_ms = nc.beacon_period_ms;
        b.duty = nc.duty_cycle;         b.allowed_sf_bitmap = nc.allowed_sf_bitmap;
        b.routing_sf = nc.routing_sf;   b.cr = nc.radio_cr;
        b.lbt = nc.lbt_enabled ? 1 : 0; b.node_id = g_node.node_id();   b.tx_power = g_tx_power;
        b.is_gateway = nc.is_gateway ? 1 : 0; b.gateway_only = nc.gateway_only ? 1 : 0;   // v6 role/topology
        b.is_mobile  = nc.is_mobile ? 1 : 0;  b.leaf_id      = nc.leaf_id;
        b.ble_mode   = g_ble_mode;            b.ble_period_min = g_ble_period_min;        // v7 BLE policy (live globals)
        b.ble_pin    = g_ble_pin;
    }
    b.magic = mrnv::kMagic; b.version = mrnv::kVersion;    // (re)stamp -> also upgrades a loaded v2 blob to v3

    // live = takes effect on the RUNNING node now (else reboot); radio = needs apply_radio_live; persist = write NV.
    // node-config knobs apply via mutable_config() (the MAC re-reads those each use). duty stays reboot (its
    // budget_ms is computed once at on_init); the extra protocol knobs are live-only (not in the NV blob yet).
    meshroute::NodeConfig& lc = g_node.mutable_config();
    bool live = true, reconfig = false, radio = false, persist = true;
    if      (!strcmp(key, "node_id")) {
        const int v = atoi(val);
        if (v < 0 || v > 254) { Serial.println(F("> cfg err bad_value (node_id 0..254; 0=unprovisioned)")); return; }
        b.node_id = (uint8_t)v; b.joined = 0; live = false;        // operator-pinned id -> NOT DAD-adopted (won't auto-yield)
    }
    else if (!strcmp(key, "freq"))                                     { b.freq_mhz = atof(val);             reconfig = radio = true; }
    else if (!strcmp(key, "routing_sf") || !strcmp(key, "control_sf")) { b.routing_sf = (uint8_t)atoi(val); reconfig = radio = true; }
    else if (!strcmp(key, "bw"))                                       { b.bw_hz = (uint32_t)atol(val);     reconfig = radio = true; }
    else if (!strcmp(key, "cr"))                                       { b.cr = (uint8_t)atoi(val);         reconfig = radio = true; }
    else if (!strcmp(key, "tx_power")) {
        const int v = atoi(val);
        if (v < -9 || v > 22) { Serial.println(F("> cfg err bad_value (tx_power -9..22 dBm)")); return; }
        b.tx_power = (int8_t)v; radio = true;                         // live, but no radio re-tune
    }
    // --- node-config knobs: LIVE via mutable_config() (the MAC re-reads each field per use), + persisted ---
    else if (!strcmp(key, "sf_list"))    { b.allowed_sf_bitmap = parse_sf_list(val); lc.allowed_sf_bitmap = b.allowed_sf_bitmap; }
    else if (!strcmp(key, "lbt"))        { b.lbt = atoi(val) != 0;            lc.lbt_enabled = (b.lbt != 0); }
    else if (!strcmp(key, "beacon_ms"))  { b.beacon_ms = (uint32_t)atol(val); lc.beacon_period_ms = b.beacon_ms; }
    else if (!strcmp(key, "duty"))       { b.duty = atof(val); live = false; }     // reboot: budget_ms is set at on_init
    // --- nav/hop tuning: LIVE-only (good defaults; reboot reverts) ---
    else if (!strcmp(key, "nav"))        { lc.nav_enabled    = atoi(val) != 0; persist = false; }
    else if (!strcmp(key, "nav_ignore")) { lc.nav_ignore_rts = atoi(val) != 0; persist = false; }
    else if (!strcmp(key, "hop_cap"))    { lc.dv_hop_cap = (uint8_t)atoi(val); persist = false; }
    // --- role/topology: LIVE via mutable_config() + PERSISTED (NV v6 -> survives reboot) ---
    else if (!strcmp(key, "leaf_id"))      { lc.leaf_id = (uint8_t)atoi(val);                            b.leaf_id      = lc.leaf_id; }
    // `gateway` is NOT a cfg key — is_gateway is DERIVED = (n_layers==2) in on_init (a gateway is the dedicated
    // gateway BUILD, MR_GATEWAY_BUILD; non-configurable so the companion's reported `gateway` is reliable).
    else if (!strcmp(key, "gateway_only")) { lc.gateway_only = (atoi(val) != 0 || !strcmp(val, "true")); b.gateway_only = lc.gateway_only ? 1 : 0; }
    else if (!strcmp(key, "mobile"))       { lc.is_mobile    = (atoi(val) != 0 || !strcmp(val, "true")); b.is_mobile    = lc.is_mobile    ? 1 : 0; }
    // --- BLE companion policy: PERSISTED, reboot-to-apply (the stack inits at boot from these). Invalid input
    //     is REJECTED (fail loud), never silently defaulted. ---
    else if (!strcmp(key, "ble_mode")) {
        uint8_t m;
        if      (!strcmp(val, "off"))      m = 0;
        else if (!strcmp(val, "on"))       m = 1;
        else if (!strcmp(val, "periodic")) m = 2;
        else { Serial.println(F("> cfg err bad_value (ble_mode off|on|periodic)")); return; }
        b.ble_mode = m; live = false;
    }
    else if (!strcmp(key, "ble_period")) {
        const int v = atoi(val);
        if (v < 1 || v > 255) { Serial.println(F("> cfg err bad_value (ble_period 1..255 min)")); return; }
        b.ble_period_min = (uint8_t)v; live = false;
    }
    else if (!strcmp(key, "ble_pin")) {
        const long v = atol(val);
        if (v < 0 || v > 999999) { Serial.println(F("> cfg err bad_value (ble_pin 0..999999, 6-digit passkey)")); return; }
        b.ble_pin = (uint32_t)v; live = false;
    }
    // --- v8 DUAL-LAYER GATEWAY: PERSISTED raw per-layer fields, reboot-to-apply (on_init validates + derives the
    //     window split). Invalid input is REJECTED (fail loud), never silently clamped/defaulted. layer 0 = the
    //     legacy node_id/routing_sf/sf_list/beacon_ms keys; these are the layer-1 + shared-schedule extras. ---
    else if (!strcmp(key, "n_layers")) {
        const int v = atoi(val);
        if (v != 1 && v != 2) { Serial.println(F("> cfg err bad_value (n_layers 1|2)")); return; }
        b.n_layers = (uint8_t)v; live = false;
    }
    else if (!strcmp(key, "layer0_id")) {
        const int v = atoi(val);
        if (v < 0 || v > 255) { Serial.println(F("> cfg err bad_value (layer0_id 0..255)")); return; }
        b.layer0_id = (uint8_t)v; live = false;
    }
    else if (!strcmp(key, "window_period_ms")) {
        const long v = atol(val);
        if (v < 1) { Serial.println(F("> cfg err bad_value (window_period_ms >= 1)")); return; }
        b.window_period_ms = (uint32_t)v; live = false;
    }
    else if (!strcmp(key, "l0_window_ms")) {
        const long v = atol(val);
        if (v < 0) { Serial.println(F("> cfg err bad_value (l0_window_ms 0=derive)")); return; }
        b.l0_window_ms = (uint32_t)v; live = false;
    }
    else if (!strcmp(key, "l0_window_offset_ms")) {
        const long v = atol(val);
        if (v < 0) { Serial.println(F("> cfg err bad_value (l0_window_offset_ms 0=derive)")); return; }
        b.l0_window_offset_ms = (uint32_t)v; live = false;
    }
    else if (!strcmp(key, "l1_layer_id")) {
        const int v = atoi(val);
        if (v < 0 || v > 255) { Serial.println(F("> cfg err bad_value (l1_layer_id 0..255)")); return; }
        b.l1_layer_id = (uint8_t)v; live = false;
    }
    else if (!strcmp(key, "l1_node_id")) {
        const int v = atoi(val);
        if (v < 0 || v > 254) { Serial.println(F("> cfg err bad_value (l1_node_id 0..254; 0=unprovisioned)")); return; }
        b.l1_node_id = (uint8_t)v; live = false;
    }
    else if (!strcmp(key, "l1_routing_sf")) {
        const int v = atoi(val);
        if (v < 5 || v > 12) { Serial.println(F("> cfg err bad_value (l1_routing_sf 5..12)")); return; }
        b.l1_routing_sf = (uint8_t)v; live = false;
    }
    else if (!strcmp(key, "l1_sf_list")) {
        const uint16_t bm = parse_sf_list(val);
        if (!bm) { Serial.println(F("> cfg err bad_value (l1_sf_list: comma SFs 5..12, e.g. 7,9)")); return; }
        b.l1_allowed_sf_bitmap = bm; live = false;
    }
    else if (!strcmp(key, "l1_beacon_ms")) {
        const long v = atol(val);
        if (v < 1) { Serial.println(F("> cfg err bad_value (l1_beacon_ms >= 1)")); return; }
        b.l1_beacon_period_ms = (uint32_t)v; live = false;
    }
    else if (!strcmp(key, "l1_window_ms")) {
        const long v = atol(val);
        if (v < 0) { Serial.println(F("> cfg err bad_value (l1_window_ms 0=derive)")); return; }
        b.l1_window_ms = (uint32_t)v; live = false;
    }
    else if (!strcmp(key, "l1_window_offset_ms")) {
        const long v = atol(val);
        if (v < 0) { Serial.println(F("> cfg err bad_value (l1_window_offset_ms 0=derive)")); return; }
        b.l1_window_offset_ms = (uint32_t)v; live = false;
    }
    else { Serial.print(F("> cfg err unknown_key ")); Serial.println(key); return; }

    if (persist && !mrnv::save(b)) { Serial.println(F("> cfg err nv_save_failed")); return; }
    if (radio && live) apply_radio_live(b, reconfig);
    Serial.print(F("> cfg ")); Serial.print(key); Serial.print('='); Serial.print(val);
    if      (!live)   Serial.println(F(" ok (reboot to apply)"));
    else if (persist) Serial.println(F(" ok (live + saved)"));
    else              Serial.println(F(" ok (live, not persisted)"));
}

static void do_reboot() {
    Serial.println(F("> rebooting")); Serial.flush(); delay(100);
#if defined(NRF52_SERIES) || defined(ARDUINO_ARCH_NRF52) || defined(BOARD_XIAO_WIO_SX1262)
    NVIC_SystemReset();
#elif defined(ARDUINO_ARCH_ESP32) || defined(ESP32) || defined(BOARD_HELTEC_V3)
    ESP.restart();
#endif
}

// `ota` — reboot into the bootloader's BLE OTA DFU. Writes the retained DFU magic (0xA8 =
// DFU_MAGIC_OTA_RESET) so the OTAFIX/Adafruit bootloader brings up its OWN SoftDevice + BLE DFU — the
// (bare-metal) app needs no BLE stack of its own. GPREGRET survives NVIC_SystemReset(). Then push the
// new firmware (firmware.zip from `pio run`) with the Nordic "nRF Device Firmware Update" app / nRF
// Connect over BLE. REQUIRES the OTAFIX bootloader (flash once via UF2 — see docs/ota.md). To abort:
// double-tap RESET (UF2 mode). On ESP32 the esp_ota path isn't wired yet (spec §B.2 transport TBD).
static void do_ota() {
#if defined(NRF52_SERIES) || defined(ARDUINO_ARCH_NRF52) || defined(BOARD_XIAO_WIO_SX1262)
    // The reset below drops into the bootloader (no USB-CDC), so this console port disappears — print
    // the notice and give the host terminal time to render it BEFORE the USB tears down. Too short a
    // delay loses the line (nothing showed at 100 ms); 500 ms renders reliably.
    Serial.println(F("> OTA: rebooting into BLE DFU now — this USB console will drop here."));
    Serial.println(F(">      Push firmware.zip via the Nordic DFU app (enable its auto-reboot). Double-tap RESET to abort."));
    Serial.flush(); delay(500);
    NRF_POWER->GPREGRET = 0xA8;   // DFU_MAGIC_OTA_RESET: bootloader inits its own SD + starts BLE OTA DFU
    NVIC_SystemReset();           // the retained GPREGRET survives the reset
#elif defined(ARDUINO_ARCH_ESP32) || defined(ESP32) || defined(BOARD_HELTEC_V3)
    Serial.println(F("> ota: unsupported on this build yet (ESP32 esp_ota path not wired — see spec §B.2)"));
#endif
}

// `sleep` / `sleep on` -> light-sleep when idle even though a host is present (the explicit override the user
// asked for); `sleep off` -> cancel it, stay awake. A headless node (no console byte this boot) light-sleeps
// on its own — this command is only for a node you're connected to. After `sleep on` the console goes quiet
// (light-sleep gates the UART) — reconnect to get it back (DTR resets the board). The node still wakes on RX
// (a peer DM prints RECV) and on its scheduled timers. No-op on -DMR_NO_POWERSAVE builds (the gate is gone).
static void handle_sleep(const char* arg, size_t n) {
    while (n && *arg == ' ') { ++arg; --n; }
    if (n >= 3 && !strncmp(arg, "off", 3)) {
        g_force_sleep = false;
        Serial.println(F("> sleep off — staying awake while a host is connected"));
    } else {
        g_force_sleep = true;
        Serial.println(F("> sleep on — light-sleeping when idle; reconnect to wake the console (still wakes on RX)"));
    }
}

// `debug on` / `debug off` (also `debug 1`/`debug 0`) — gate the decoded per-frame «rx/»tx console trace
// (frame_trace.h g_mr_trace_on). Default ON. Lets the REPL silence the verbose tracing for normal use.
static void handle_debug(const char* arg, size_t n) {
    while (n && *arg == ' ') { ++arg; --n; }
    const bool off = (n >= 3 && !strncmp(arg, "off", 3)) || (n >= 1 && arg[0] == '0');
    meshroute::g_mr_trace_on = !off;
    Serial.println(off ? F("> debug off — RX/TX frame trace silenced") : F("> debug on — tracing RX/TX frames"));
}

// `lookup <hash>` — local id_bind cache peek (NO airtime): resolve a key_hash32 -> node short-id from what
// this node already knows (beacons / prior H answers). Hash is hex (e.g. `lookup 8a3f1c02`). For a network
// resolve of an unknown hash, use `resolve` (floods H).
static void handle_lookup(const char* arg, size_t n) {
    while (n && *arg == ' ') { ++arg; --n; }
    if (!n) { Serial.println(F("> lookup err bad_args (hex hash)")); return; }
    const uint32_t hash = (uint32_t)strtoul(arg, nullptr, 16);
    meshroute::Node::IdBindConf conf = meshroute::Node::IdBindConf::claimed;
    const int id = g_node.id_bind_find_by_hash(hash, &conf);
    Serial.print(F("[lookup] 0x")); Serial.print(hash, HEX);
    if (id < 0) { Serial.println(F(" -> miss")); return; }
    Serial.print(F(" -> id=")); Serial.print(id);
    Serial.println(conf == meshroute::Node::IdBindConf::authoritative ? F(" (authoritative)") : F(" (claimed)"));
}

// `hashof <id>` — reverse lookup: a node short-id -> its key_hash32 (AUTHORITATIVE bindings only — a node we
// can vouch for). Decimal id 0..254.
static void handle_hashof(const char* arg, size_t n) {
    while (n && *arg == ' ') { ++arg; --n; }
    if (!n) { Serial.println(F("> hashof err bad_args (id 0..254)")); return; }
    const int id = atoi(arg);
    uint32_t hash = 0;
    Serial.print(F("[hashof] id=")); Serial.print(id);
    if (id >= 0 && id <= 254 && g_node.key_hash_of_id((uint8_t)id, hash)) { Serial.print(F(" -> 0x")); Serial.println(hash, HEX); }
    else                                                                  Serial.println(F(" -> unknown"));
}

// `whoami` — this node's own identity + role. The hash printed here is what a peer types into `sendhash` to
// reach you (the device can't surface its own key_hash32 any other way). Name is read from /mrid.
static void handle_whoami() {
    Serial.print(F("[whoami] id=")); Serial.print(g_node.node_id());
    Serial.print(F(" hash=0x"));     Serial.print(g_node.key_hash32(), HEX);
    mrnv::IdBlob idb{};
    if (mrnv::load_id(idb) && idb.name_len) { Serial.print(F(" name=\"")); Serial.write(idb.name, idb.name_len); Serial.print('"'); }
    const meshroute::NodeConfig& c = g_node.config();
    Serial.print(F(" leaf="));   Serial.print(c.leaf_id);
    Serial.print(F(" gw="));     Serial.print(c.is_gateway ? 1 : 0);
    Serial.print(F(" gwonly=")); Serial.print(c.gateway_only ? 1 : 0);
    Serial.print(F(" mobile=")); Serial.println(c.is_mobile ? 1 : 0);
    // Dual-layer gateway: an ADDITIVE per-leaf line. Single-layer whoami above is BYTE-IDENTICAL to before.
    if (c.n_layers == 2) {
        for (uint8_t li = 0; li < 2; ++li) {
            const meshroute::LayerConfig& L = c.layers[li];
            Serial.print(F("[whoami.layer")); Serial.print(li);
            Serial.print(F("] node_id="));   Serial.print(L.node_id);
            Serial.print(F(" layer_id="));   Serial.print(L.layer_id);
            Serial.print(F(" routing_sf=")); Serial.print(L.routing_sf);
            Serial.print(F(" window_ms="));  Serial.print(L.window_ms);
            Serial.print(F(" window_offset_ms=")); Serial.println(L.window_offset_ms);
        }
    }
}

// `help` / `?` — a small command + cfg-key reference for the live console session.
static void dump_help() {
    Serial.println(F("[help] messaging:  send <id> <text> | send_ack <id> <text> | sendhash <hash> <text> | sendhash_ack <hash> <text> | send_channel <ch> <text>"));
    Serial.println(F("[help] hash/id:    lookup <hash> | hashof <id> | whoami"));
    Serial.println(F("[help] inbox:      pull_inbox <dm_since> <chan_since> | mark_read <dm|chan> <seq>  (NDJSON out)"));
    Serial.println(F("[help] diag:       routes | status | cfg | cfg set <k> <v> | sleep [on|off] | debug [on|off] | regen | reboot | ota"));
    Serial.println(F("  cfg keys: node_id name freq routing_sf bw cr tx_power sf_list lbt beacon_ms duty nav nav_ignore hop_cap leaf_id gateway_only mobile key ble_mode on|off"));
    Serial.println(F("  cfg keys (dual-layer gw): n_layers layer0_id window_period_ms l0_window_ms l0_window_offset_ms l1_layer_id l1_node_id l1_routing_sf l1_sf_list l1_beacon_ms l1_window_ms l1_window_offset_ms"));
}

// ---- Phase-3 inbox sync (schema: ios-companion/INBOX_SYNC_CONTRACT.md) -----------------------------------
// `pull_inbox <dm_since> <chan_since>` streams the inbox (DM block then channel block, oldest-first) + an
// inbox_end terminator; `mark_read <dm|chan> <seq>` advances the per-store read cursor. Both stream NDJSON to a
// transport SINK (USB Serial OR the BLE NUS), so one handler serves both consoles. The companion link is JSON;
// on USB it's structured output for the host harness.
using JsonSink = void (*)(const char* s, size_t n);
static void usb_sink(const char* s, size_t n) { Serial.write(reinterpret_cast<const uint8_t*>(s), n); }
static void ble_sink(const char* s, size_t n) { mrble::tx_line(s, n); }   // inert off-XIAO / when no client

namespace { struct PullCtx { JsonSink sink; uint32_t count; }; }
static char s_inbox_jb[1700];   // shared NDJSON line scratch: pulled inbox records AND live-push lines (loop()) — sequential, single-threaded, never concurrent (241-B body 6x-escaped + envelope)

// pull() callback: format ONE record -> JSON -> sink. The body ptr is valid only for this call (the encoder copies it).
static bool inbox_pull_cb(void* vctx, const meshroute::InboxEntry& e) {
    PullCtx* c = static_cast<PullCtx*>(vctx);
    const size_t n = (e.kind == meshroute::InboxKind::dm)
        ? meshroute::console::write_inbox_dm(s_inbox_jb, sizeof s_inbox_jb, e.seq, e.origin,
              static_cast<uint16_t>(e.msg_id), e.sender_hash, e.rx_time_ms,
              reinterpret_cast<const char*>(e.body), e.body_len)
        : meshroute::console::write_inbox_channel(s_inbox_jb, sizeof s_inbox_jb, e.seq, e.origin,
              e.channel_id, e.msg_id, e.rx_time_ms, reinterpret_cast<const char*>(e.body), e.body_len);
    if (n) { c->sink(s_inbox_jb, n); ++c->count; }
    else {                                                // UNREACHABLE for a valid body (<=241 B fits 1700), but
        char eb[48];                                      // NEVER drop a record silently: tell the app one didn't encode.
        const size_t en = meshroute::console::write_err(eb, sizeof eb, "inbox_encode", nullptr);
        if (en) c->sink(eb, en);
    }
    return true;                                          // never stop early — the app pulls the whole delta
}
static void handle_pull_inbox(const char* args, JsonSink sink) {
    char* end;
    const uint32_t dm_since   = strtoul(args, &end, 10);  // missing/garbled args -> 0 (a full pull is always safe; the app dedups)
    const uint32_t chan_since = strtoul(end, nullptr, 10);
    meshroute::Inbox& ib = g_node.inbox();
    PullCtx ctx{ sink, 0 };
    ib.pull(dm_since, chan_since, inbox_pull_cb, &ctx);
    // inbox_end carries the store's NEWEST seq per store (contract §"newest seq per store"), NOT a cursor echo —
    // so an empty store / a stale-high cursor self-heals (the app advances to the real high-water, re-syncing).
    const size_t n = meshroute::console::write_inbox_end(s_inbox_jb, sizeof s_inbox_jb,
                       ib.dm_newest_seq(), ib.chan_newest_seq(), ib.storage_epoch(), ctx.count, g_hal.now());
    if (n) sink(s_inbox_jb, n);
}
static void handle_mark_read(const char* args, JsonSink sink) {
    while (*args == ' ') ++args;
    // The kind must be EXACTLY "dm" or "chan" (word boundary = next char is space or end). Without the boundary
    // check, "dm5"/"dme" match strncmp("dm",2) and "channel" matches strncmp("chan",4) -> wrong/zero seq parsed.
    meshroute::InboxKind kind; const char* kstr;
    if      (!strncmp(args, "dm", 2)   && (args[2] == ' ' || args[2] == '\0')) { kind = meshroute::InboxKind::dm;      kstr = "dm";   args += 2; }
    else if (!strncmp(args, "chan", 4) && (args[4] == ' ' || args[4] == '\0')) { kind = meshroute::InboxKind::channel; kstr = "chan"; args += 4; }
    else { char eb[64]; const size_t n = meshroute::console::write_err(eb, sizeof eb, "mark_read", "kind must be dm|chan");
           if (n) sink(eb, n); return; }                 // fail loud on a bad kind
    const uint32_t seq = strtoul(args, nullptr, 10);
    g_node.inbox().mark_read(kind, seq);
    char ab[64]; const size_t n = meshroute::console::write_inbox_marked(ab, sizeof ab, kstr, seq);
    if (n) sink(ab, n);
}

// Handle a debug/diagnostic console line (help/routes/cfg/status/cfg set/reboot/sleep/debug). Returns true if consumed.
static bool service_debug(const char* line, size_t len) {
    if ((len == 4 && !strncmp(line, "help", 4)) || (len == 1 && line[0] == '?')) { dump_help(); return true; }
    if (len == 6 && !strncmp(line, "routes", 6))   { dump_routes(); return true; }
    if (len == 6 && !strncmp(line, "status", 6))   { dump_status(); return true; }
    if (len == 6 && !strncmp(line, "reboot", 6))   { do_reboot();   return true; }
    if (len == 5 && !strncmp(line, "regen", 5))    { do_regen();    return true; }
    if (len == 3 && !strncmp(line, "ota", 3))      { do_ota();      return true; }
    if (len >  8 && !strncmp(line, "cfg set ", 8)) { handle_cfg_set(line + 8); return true; }
    if (len == 3 && !strncmp(line, "cfg", 3))      { dump_cfg();    return true; }
    if ((len == 5 || (len > 5 && line[5] == ' ')) && !strncmp(line, "sleep", 5)) { handle_sleep(line + 5, len - 5); return true; }
    if ((len == 5 || (len > 5 && line[5] == ' ')) && !strncmp(line, "debug", 5)) { handle_debug(line + 5, len - 5); return true; }
    if (len == 6 && !strncmp(line, "whoami", 6)) { handle_whoami(); return true; }
    if ((len == 6 || (len > 6 && line[6] == ' ')) && !strncmp(line, "lookup", 6)) { handle_lookup(line + 6, len - 6); return true; }
    if ((len == 6 || (len > 6 && line[6] == ' ')) && !strncmp(line, "hashof", 6)) { handle_hashof(line + 6, len - 6); return true; }
    if ((len == 10 || (len > 10 && line[10] == ' ')) && !strncmp(line, "pull_inbox", 10)) { handle_pull_inbox(line + 10, usb_sink); return true; }
    if ((len ==  9 || (len >  9 && line[9]  == ' ')) && !strncmp(line, "mark_read",   9)) { handle_mark_read(line + 9,  usb_sink); return true; }
    return false;
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
        return write_ready(out, cap, g_node.node_id(), g_node.key_hash32(), g_node.config(), "existing",
                           g_node.inbox().storage_epoch(), g_hal.now(), idb.name, nl);
    }
    // Inbox sync (companion-only): stream the reply via mrble::tx_line and return 0 (no buffered single-line ack).
    if ((len == 10 || (len > 10 && line[10] == ' ')) && !strncmp(line, "pull_inbox", 10)) { handle_pull_inbox(line + 10, ble_sink); return 0; }
    if ((len ==  9 || (len >  9 && line[9]  == ' ')) && !strncmp(line, "mark_read",   9)) { handle_mark_read(line + 9,  ble_sink); return 0; }
    meshroute::Command cmd{};
    const ParseErr e = parse_command(line, len, cmd);
    if (e == ParseErr::ok)    return write_ack(out, cap, g_node.on_command(cmd));
    if (e == ParseErr::empty) return 0;
    return write_err(out, cap, "parse", e == ParseErr::unknown_verb ? "unknown_cmd" : "bad_args");
}

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000) { /* wait for USB CDC, but don't block forever */ }
    delay(2000);   // Settle: the USB-CDC port re-enumerates on every reset, and the host serial
                   // monitor reattaches AFTER that — so without a pause the one-time boot banner
                   // prints into the void. 2 s lets the monitor catch up before we print it.

    Serial.println(F("MeshRoute firmware v0.1 — boot"));
    Serial.print(F("  freq      = ")); Serial.print((double)LORA_FREQ, 4); Serial.println(F(" MHz"));
    Serial.print(F("  sf/bw/cr  = ")); Serial.print(LORA_SF); Serial.print(F("/"));
    Serial.print((double)LORA_BW, 1); Serial.print(F("/4/")); Serial.println(LORA_CR);
#ifdef BOARD_XIAO_WIO_SX1262
    Serial.println(F("  board     = XIAO nRF52840 + Wio-SX1262"));
#elif defined(BOARD_HELTEC_V3)
    Serial.println(F("  board     = Heltec WiFi LoRa 32 V3"));
#endif

    // Bring up the SX1262 (begin/CRC/TCXO/DIO2-rf-switch/RXEN/RX-boost) then arm continuous RX.
#if defined(P_LORA_SCLK)
    bool ok = g_radio.std_init(&SPI);
#else
    bool ok = g_radio.std_init();
#endif
    Serial.print(F("  radio     = ")); Serial.println(ok ? F("OK") : F("INIT FAILED"));
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
        g_ble_mode            = nv.ble_mode;          g_ble_period_min = nv.ble_period_min;      // v7 BLE policy (only v7 blobs load)
        g_ble_pin             = nv.ble_pin;
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
            // layer 1 = the l1_* block (window_period_ms shared with layer 0).
            cfg.layers[1].layer_id          = nv.l1_layer_id;
            cfg.layers[1].node_id           = nv.l1_node_id;
            cfg.layers[1].routing_sf        = nv.l1_routing_sf;
            cfg.layers[1].allowed_sf_bitmap = nv.l1_allowed_sf_bitmap;
            cfg.layers[1].beacon_period_ms  = nv.l1_beacon_period_ms;
            cfg.layers[1].window_period_ms  = nv.window_period_ms;   // shared cycle
            cfg.layers[1].window_ms         = nv.l1_window_ms;       // 0 = on_init derives
            cfg.layers[1].window_offset_ms  = nv.l1_window_offset_ms;
        }
        Serial.println(F("  config    = loaded from NV"));
    }
    // Identity (/mrid): load the 32-byte master seed, or mint one from the HW-RNG on first boot.
    mrnv::IdBlob idb{};
    if (mrnv::load_id(idb)) {
        Serial.println(F("  identity  = loaded from NV (/mrid)"));
    } else {
        mrrng::fill(idb.seed, sizeof idb.seed);                 // first boot -> generate a fresh seed
        idb.magic = mrnv::kIdMagic; idb.version = mrnv::kIdVersion; idb.name_len = 0;
        Serial.println(mrnv::save_id(idb) ? F("  identity  = generated (first boot -> /mrid)")
                                          : F("  identity  = generated (first boot, NV SAVE FAILED — volatile)"));
    }
    meshroute::identity_from_seed(g_identity, idb.seed);        // key_hash32 = ed_pub[:4]
    g_node.set_identity(node_id, g_identity.key_hash32);        // node_id 0 stays unprovisioned -> do_send refused
    // node_id DAD: restore the persisted lease state so a reboot KEEPS its id + tiebreak seniority (NV blob v4).
    g_node.restore_join_state(nv.claim_epoch, (node_id != 0) && (nv.joined != 0));
    g_persist_id = node_id; g_persist_epoch = nv.claim_epoch;        // prime the persist tracker -> no spurious boot write
    g_persist_join = ((node_id != 0) && (nv.joined != 0)) ? 1 : 0;
    print_identity(idb);                                        // key_hash32 (hex) + name
    Serial.print(F("  node id   = ")); Serial.print(node_id);
    Serial.println(node_id == 0 ? F("  (UNPROVISIONED: cfg set node_id <1..254> + reboot, or join)") : F(""));
    Serial.print(F("  control sf= ")); Serial.print(cfg.routing_sf); Serial.println(F("  (RTS/CTS/ACK + beacons)"));
    Serial.print(F("  data sf   = "));
    if (cfg.allowed_sf_bitmap) { print_sf_list(cfg.allowed_sf_bitmap); Serial.println(F("  (receiver picks the fastest by SNR)")); }
    else                       { Serial.println(F("(none — set sf_list; data send is REFUSED until configured)")); }

    Serial.print(F("  tx power  = ")); Serial.print((int)g_tx_power); Serial.println(F(" dBm"));

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
    if (!g_node.on_init(cfg)) Serial.println(F("  config    = REFUSED (invalid layer config — node NOT operational)"));
    // Install the inbox stores so record-on-delivery + pull_inbox work. With the interim RAM store: give it a
    // per-boot-unique storage_epoch (HW-RNG; drawn here BEFORE BLE init, so the bare-metal NRF_RNG path is still
    // valid) -> after a reboot the companion sees a NEW epoch and re-pulls (the volatile store lost its history).
#if !defined(MRINBOX_QSPI_READY)
    uint32_t boot_epoch = 0; mrrng::fill(reinterpret_cast<uint8_t*>(&boot_epoch), sizeof boot_epoch);
    g_inbox_dm.set_epoch(boot_epoch); g_inbox_ch.set_epoch(boot_epoch);
#endif
    g_node.inbox().on_init(&g_inbox_dm, &g_inbox_ch);
#if defined(MRINBOX_QSPI_READY)
    Serial.println(F("  inbox     = QSPI (durable)"));
#else
    Serial.print(F("  inbox     = RAM volatile, ")); Serial.print(MR_RAM_INBOX_SLOTS);
    Serial.println(F(" msgs/store (interim — lost on reboot; durable QSPI store is a bench-TODO)"));
#endif
    // node_id DAD auto-join: an UNPROVISIONED node (no persisted id) self-assigns one via the claim state
    // machine. A node that rebooted WITH a persisted id skips this — it already owns it (restored above).
    if (node_id == 0) {
        meshroute::Command jc{}; jc.kind = meshroute::CmdKind::join;
        g_node.on_command(jc);
        Serial.println(F("  join      = auto-DAD started (unprovisioned)"));
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
        Serial.print(F("  ble       = "));
        if (up) { Serial.print(g_ble_mode == 1 ? F("on") : F("periodic"));
                  Serial.println(F("  (secured: MITM passkey pairing — PIN in `cfg`)")); }
        else    { Serial.println(F("INIT FAILED")); }      // fail loud: NO silent fall-back to bare-metal/insecure
    }
    Serial.println(F("  node      = up. Type 'help' for commands."));
}

// Accumulate a USB-CDC line; on '\n' parse it into a Command + hand it to the Node.
static void service_console() {
    static char   line[160];
    static size_t pos = 0;
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\r') continue;
        if (c == '\n') {
            line[pos] = '\0';                            // null-terminate (pos <= sizeof-1) for the debug cmds
            if (!service_debug(line, pos)) {             // routes/cfg/status handled here; else a Node command
                meshroute::Command cmd{};
                const meshroute::console::ParseErr e = meshroute::console::parse_command(line, pos, cmd);
                if (e == meshroute::console::ParseErr::ok) {
                    const meshroute::CmdResult r = g_node.on_command(cmd);
                    Serial.print(F("> "));
                    Serial.print(r.code == meshroute::CmdCode::queued ? F("queued ctr=") : F("err ctr="));
                    Serial.print(r.ctr); Serial.print(F(" depth=")); Serial.println(r.queue_depth);
                } else if (e != meshroute::console::ParseErr::empty) {
                    Serial.println(F("> parse error"));
                }
            }
            pos = 0;
        } else if (pos < sizeof(line) - 1) {
            line[pos++] = c;
        }
    }
}

// node_id DAD: persist the lease state (node_id + claim_epoch + joined) to /mrcfg WHEN it changes (adopt /
// epoch bump / forced rejoin), so a reboot keeps its id + seniority. Load-modify-save so the config fields
// (set via `cfg set`) are preserved. Cheap on the no-change path (3 compares); a flash write only on change.
static void persist_join_if_changed() {
    const uint8_t id = g_node.node_id(), ep = g_node.claim_epoch(), jn = g_node.joined() ? 1 : 0;
    if (id == g_persist_id && ep == g_persist_epoch && jn == g_persist_join) return;
    mrnv::Blob b{};
    if (!mrnv::load(b)) {                                            // no blob yet -> seed from live config
        const meshroute::NodeConfig& nc = g_node.config();
        b.freq_mhz = g_freq_mhz;        b.bw_hz = nc.radio_bw_hz;   b.beacon_ms = nc.beacon_period_ms;
        b.duty = nc.duty_cycle;         b.allowed_sf_bitmap = nc.allowed_sf_bitmap;
        b.routing_sf = nc.routing_sf;   b.cr = nc.radio_cr;
        b.lbt = nc.lbt_enabled ? 1 : 0; b.tx_power = g_tx_power;
        b.is_gateway = nc.is_gateway ? 1 : 0; b.gateway_only = nc.gateway_only ? 1 : 0;   // v6 role/topology
        b.is_mobile  = nc.is_mobile ? 1 : 0;  b.leaf_id      = nc.leaf_id;
        b.ble_mode   = g_ble_mode;            b.ble_period_min = g_ble_period_min;        // v7 BLE policy (live globals)
        b.ble_pin    = g_ble_pin;
    }
    b.magic = mrnv::kMagic; b.version = mrnv::kVersion;
    b.node_id = id; b.claim_epoch = ep; b.joined = jn;
    if (mrnv::save(b)) { g_persist_id = id; g_persist_epoch = ep; g_persist_join = jn; }
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

void loop() {
    const uint64_t now = g_hal.now();

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

    // 2) Timers: fire every elapsed Node timer (beacons, RTS/ACK timeouts, retries, the duty/LBT defers).
    for (int id; (id = g_hal.pop_due_timer()) >= 0; ) g_node.on_timer((uint32_t)id);

    // 2b) Async TX: drain the in-flight TX completion (radio re-arms RX) + start the next queued frame.
    //     After RX + timers, since both enqueue TX. The loop stays live during a long TX (no freeze).
    g_hal.service_tx();

    // 2c) LBT noise-floor sampler (only when LBT is on — it feeds channel_busy()). Self-paced (≤1 RSSI/10 ms).
    if (g_node.config().lbt_enabled) g_iradio.sample_noise();

    // 2d) Inbox: periodically persist the next-seq high-water (§6 "/ on a timer"; bounds the seq-reuse window
    //     if the records store is later lost). No-op while the inbox is disabled (records backend not wired).
    static uint32_t s_inbox_flush_ms = 0;
    if ((uint32_t)now - s_inbox_flush_ms >= 30000u) { s_inbox_flush_ms = (uint32_t)now; g_node.inbox().flush(); }

    // 3) App pushes: surface deliveries / ACKs over the console.
    meshroute::Push pu{};
    while (g_node.next_push(pu)) {
        switch (pu.kind) {
            case meshroute::PushKind::msg_recv:
                Serial.print(F("RECV from="));   Serial.print(pu.origin); Serial.print(F(": "));
                Serial.write(pu.body, pu.body_len); Serial.println();
                break;
            case meshroute::PushKind::channel_recv:
                Serial.print(F("CH ")); Serial.print(pu.channel_id);
                Serial.print(F(" from=")); Serial.print(pu.origin); Serial.print(F(": "));
                Serial.write(pu.body, pu.body_len); Serial.println();
                break;
            case meshroute::PushKind::send_acked:
                Serial.print(F("ACKED ctr="));    Serial.println(pu.ctr); break;
            case meshroute::PushKind::send_failed:
                Serial.print(F("FAILED ctr="));   Serial.println(pu.ctr); break;
            case meshroute::PushKind::hash_resolved: {
                const uint32_t hash = (uint32_t)pu.body[0] | ((uint32_t)pu.body[1] << 8)
                                    | ((uint32_t)pu.body[2] << 16) | ((uint32_t)pu.body[3] << 24);
                if (pu.origin == 0) { Serial.print(F("UNRESOLVED 0x")); Serial.print(hash, HEX); Serial.println(F(" (timeout)")); }
                else { Serial.print(F("RESOLVED 0x")); Serial.print(hash, HEX);
                       Serial.print(F(" -> id=")); Serial.print(pu.origin);
                       Serial.println(pu.dst ? F(" (auth)") : F(" (cached)")); }
                break;
            }
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
            const size_t n = meshroute::console::write_push(s_inbox_jb, sizeof s_inbox_jb, pu);
            if (n) mrble::tx_line(s_inbox_jb, n);
            else { static const char kOvf[] = "{\"err\":\"push_encode_overflow\"}\n";   // unreachable for valid
                   mrble::tx_line(kOvf, sizeof(kOvf) - 1); }                            // input; LOUD, never silent
        }
    }
    Serial.flush();

    // 4) Console input -> commands. A byte means a host is here -> latch awake so the console stays usable
    //    (service_console() drains Serial, so we must note it BEFORE; the sleep gate below honors the latch).
    if (Serial.available()) g_host_present = true;
    service_console();

    // 4c) BLE companion: advance the advertising-window policy + drain inbound NUS lines (both inert off-XIAO).
    mrble::on_tick(now);
    mrble::service_rx();

    // 4b) Persist the DAD lease state if it changed this iteration (adopt / epoch bump / forced rejoin).
    persist_join_if_changed();

    // 5) Idle light-sleep: nothing pending -> halt the CPU until the next timer OR a radio/console IRQ.
    //    Capped at MR_MAX_SLEEP_MS so the console + periodic work stay responsive (matters on ESP32;
    //    nRF52 WFE wakes every RTC tick regardless). Gate: not mid-TX, no queued TX, no console input.
#if !defined(MR_NO_POWERSAVE)
    const uint64_t s_now = g_hal.now();
    // Sleep policy: a HEADLESS node (no host byte this boot, past the boot grace) light-sleeps when idle; an
    // explicit `sleep` command forces it even with a host present. A host that has typed latches us awake so
    // the console stays usable (ESP32 light-sleep would otherwise gate the UART and strand it). See MR_BOOT_GRACE_MS.
    const bool may_sleep = g_force_sleep || (!g_host_present && s_now >= MR_BOOT_GRACE_MS);
    if (may_sleep && !g_iradio.tx_busy() && g_hal.txq_depth() == 0 && !Serial.available() && !mrble::connected()) {
        uint64_t due = g_hal.next_due_ms();                    // UINT64_MAX if no timer armed
        const uint64_t cap = s_now + MR_MAX_SLEEP_MS;
        if (due > cap) due = cap;
        ++g_sleep_count;                                       // count sleep entries (status `slept=`)
        board_sleep_until(due, s_now);
    }
#endif
}
