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
#include "node.h"
#include "command.h"
#include "console_parse.h"
#include "device_nv.h"
#include <string.h>
#include <stdlib.h>

namespace P = meshroute::protocol;

// ---- node identity: from NV (`cfg set node_id`) or the join runtime — NOT the build. 0 = unprovisioned
//      (do_send refused; only join). key_hash32 is derived from the id until the join runtime assigns one.
static constexpr uint32_t key_for(uint8_t id) { return 0x4D455300u | id; }   // "MES\0" | id

// ---- the device stack (global ctor order = declaration order; refs bind to already-built objects) ----
static Module                  g_mod(LORA_PIN_NSS, LORA_PIN_DIO1, LORA_PIN_RST, LORA_PIN_BUSY);
static CustomSX1262            g_radio(&g_mod);
static meshroute::ArduinoClock g_clock;
static meshroute::Sx1262Radio  g_iradio(g_radio);
static meshroute::DeviceHal    g_hal(g_clock, g_iradio);
static meshroute::Node         g_node(g_hal, /*node_id=*/0, key_for(0), "node");   // real id set in setup() from NV

static uint8_t  g_rxbuf[P::max_payload_bytes_hard_cap + 32];
static bool     g_radio_ok = false;   // SX1262 std_init result — surfaced in the heartbeat below
static uint32_t g_rx_count = 0;       // frames received (status diagnostic)
static double   g_freq_mhz = LORA_FREQ;   // live operating freq (compile default; Slice-2 NV will override at boot)
static int8_t   g_tx_power = LORA_TX_POWER;   // live TX power (dBm); NV `cfg set tx_power` overrides at boot

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
    Serial.print(F(" data_sf="));        Serial.print(c.data_sf);
    Serial.print(F(" sf_list="));        print_sf_list(c.allowed_sf_bitmap);
    Serial.print(F(" bw="));             Serial.print(c.radio_bw_hz);
    Serial.print(F(" cr="));             Serial.print(c.radio_cr);
    Serial.print(F(" tx_power="));       Serial.print((int)g_tx_power);
    Serial.print(F(" duty="));           Serial.print(c.duty_cycle, 3);
    Serial.print(F(" lbt="));            Serial.print(c.lbt_enabled ? 1 : 0);
    Serial.print(F(" beacon_ms="));      Serial.println(c.beacon_period_ms);
}

static void dump_status() {
    Serial.print(F("[status] uptime_ms="));  Serial.print((uint32_t)g_hal.now());
    Serial.print(F(" rx="));                 Serial.print(g_rx_count);
    Serial.print(F(" tx="));                 Serial.print(g_iradio.tx_count());
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

// `cfg set <key> <value>` — validate, persist to NV, apply on the NEXT boot. `args` is past "cfg set ".
static void handle_cfg_set(const char* args) {
    char key[20]; size_t k = 0;
    while (args[k] && args[k] != ' ' && k < sizeof(key) - 1) { key[k] = args[k]; ++k; }
    key[k] = '\0';
    const char* val = (args[k] == ' ') ? (args + k + 1) : (args + k);
    if (!*val) { Serial.println(F("> cfg err bad_args")); return; }

    meshroute::NodeConfig nc = g_node.config();      // start from the live config, override one key
    double  freq    = g_freq_mhz;
    uint8_t node_id = g_node.node_id();              // preserve identity across other `cfg set`s
    int8_t  tx_power = g_tx_power;                    // preserve TX power across other `cfg set`s
    bool known = true;
    if      (!strcmp(key, "node_id")) {
        const int v = atoi(val);
        if (v < 0 || v > 254) { Serial.println(F("> cfg err bad_value (node_id 0..254; 0=unprovisioned)")); return; }
        node_id = (uint8_t)v;
    }
    else if (!strcmp(key, "freq"))       freq = atof(val);
    else if (!strcmp(key, "routing_sf") ||
             !strcmp(key, "control_sf")) nc.routing_sf = (uint8_t)atoi(val);   // "control_sf" = the banner's name for it
    else if (!strcmp(key, "data_sf"))    nc.data_sf = (uint8_t)atoi(val);
    else if (!strcmp(key, "sf_list"))    nc.allowed_sf_bitmap = parse_sf_list(val);
    else if (!strcmp(key, "bw"))         nc.radio_bw_hz = (uint32_t)atol(val);
    else if (!strcmp(key, "cr"))         nc.radio_cr = (uint8_t)atoi(val);
    else if (!strcmp(key, "duty"))       nc.duty_cycle = atof(val);
    else if (!strcmp(key, "lbt"))        nc.lbt_enabled = atoi(val) != 0;
    else if (!strcmp(key, "beacon_ms"))  nc.beacon_period_ms = (uint32_t)atol(val);
    else if (!strcmp(key, "tx_power")) {
        const int v = atoi(val);
        if (v < -9 || v > 22) { Serial.println(F("> cfg err bad_value (tx_power -9..22 dBm)")); return; }
        tx_power = (int8_t)v;
    }
    else known = false;
    if (!known) { Serial.print(F("> cfg err unknown_key ")); Serial.println(key); return; }

    mrnv::Blob b{};
    b.magic = mrnv::kMagic; b.version = mrnv::kVersion;
    b.freq_mhz = freq;            b.bw_hz = nc.radio_bw_hz;     b.beacon_ms = nc.beacon_period_ms;
    b.duty = nc.duty_cycle;       b.allowed_sf_bitmap = nc.allowed_sf_bitmap;
    b.routing_sf = nc.routing_sf; b.data_sf = nc.data_sf;       b.cr = nc.radio_cr;  b.lbt = nc.lbt_enabled ? 1 : 0;
    b.node_id = node_id;
    b.tx_power = tx_power;
    if (mrnv::save(b)) {
        Serial.print(F("> cfg ")); Serial.print(key); Serial.print('='); Serial.print(val);
        Serial.println(F(" ok (reboot to apply)"));
    } else {
        Serial.println(F("> cfg err nv_save_failed"));
    }
}

static void do_reboot() {
    Serial.println(F("> rebooting")); Serial.flush(); delay(100);
#if defined(NRF52_SERIES) || defined(ARDUINO_ARCH_NRF52) || defined(BOARD_XIAO_WIO_SX1262)
    NVIC_SystemReset();
#elif defined(ARDUINO_ARCH_ESP32) || defined(ESP32) || defined(BOARD_HELTEC_V3)
    ESP.restart();
#endif
}

// Handle a debug/diagnostic console line (routes/cfg/status/cfg set/reboot). Returns true if consumed.
static bool service_debug(const char* line, size_t len) {
    if (len == 6 && !strncmp(line, "routes", 6))   { dump_routes(); return true; }
    if (len == 6 && !strncmp(line, "status", 6))   { dump_status(); return true; }
    if (len == 6 && !strncmp(line, "reboot", 6))   { do_reboot();   return true; }
    if (len >  8 && !strncmp(line, "cfg set ", 8)) { handle_cfg_set(line + 8); return true; }
    if (len == 3 && !strncmp(line, "cfg", 3))      { dump_cfg();    return true; }
    return false;
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
    cfg.data_sf               = LORA_SF;                         // single SF default; adaptive select_data_sf is live
    cfg.radio_bw_hz           = (uint32_t)(LORA_BW * 1000.0);    // keep the Node's airtime math == the radio's BW
    cfg.radio_cr              = LORA_CR;
    cfg.leaf_id               = 0;
    cfg.duty_cycle            = (double)LORA_DUTY_CYCLE_PCT / 100.0;
    cfg.duty_cycle_window_ms  = 3600000;                        // 1 h (ETSI)
    cfg.peer_count            = 0;                              // no sim:nodes() on device -> no rt_full telemetry
    // KEEP false unless NV overrides: device LBT -> scanChannel() can spin unbounded (the beacon-timer freeze).
    cfg.lbt_enabled           = false;

    uint8_t node_id = 0;                                         // unprovisioned default; NV / join sets it
    mrnv::Blob nv{};
    if (mrnv::load(nv)) {                                        // a prior `cfg set` persisted -> apply it
        node_id               = nv.node_id;
        g_freq_mhz            = nv.freq_mhz;
        cfg.routing_sf        = nv.routing_sf;   cfg.data_sf      = nv.data_sf;
        cfg.allowed_sf_bitmap = nv.allowed_sf_bitmap;
        cfg.radio_bw_hz       = nv.bw_hz;        cfg.radio_cr     = nv.cr;
        cfg.duty_cycle        = nv.duty;         cfg.lbt_enabled  = nv.lbt != 0;
        cfg.beacon_period_ms  = nv.beacon_ms;
        g_tx_power            = (nv.version >= 3) ? nv.tx_power : (int8_t)LORA_TX_POWER;   // v2 blob had no tx_power -> keep the default
        Serial.println(F("  config    = loaded from NV"));
    }
    g_node.set_identity(node_id, key_for(node_id));             // 0 stays unprovisioned -> do_send refused
    Serial.print(F("  node id   = ")); Serial.print(node_id);
    Serial.println(node_id == 0 ? F("  (UNPROVISIONED: cfg set node_id <1..254> + reboot, or join)") : F(""));
    Serial.print(F("  control sf= ")); Serial.print(cfg.routing_sf); Serial.println(F("  (RTS/CTS/ACK + beacons)"));
    Serial.print(F("  data sf   = "));
    if (cfg.allowed_sf_bitmap) { print_sf_list(cfg.allowed_sf_bitmap); Serial.println(F("  (receiver picks the fastest by SNR)")); }
    else                       { Serial.print(cfg.data_sf);           Serial.println(F("  (fixed — no sf_list configured)")); }

    Serial.print(F("  tx power  = ")); Serial.print((int)g_tx_power); Serial.println(F(" dBm"));

    // Apply the operating point to the radio (freq/SF/BW/CR), re-arm RX, and match the Hal airtime ledger.
    if (ok) {
        g_radio.setFrequency((float)g_freq_mhz);
        g_radio.setSpreadingFactor((uint8_t)cfg.routing_sf);
        g_radio.setBandwidth((float)cfg.radio_bw_hz / 1000.0f);
        g_radio.setCodingRate((uint8_t)cfg.radio_cr);
        g_iradio.begin();                                       // (re)arm continuous RX on the applied SF
    }
    g_hal.configure(/*sf=*/(int16_t)cfg.routing_sf, /*bw_hz=*/(int32_t)cfg.radio_bw_hz,
                    /*cr=*/(int8_t)cfg.radio_cr, /*preamble=*/(int16_t)P::preamble_sym,
                    /*power=*/g_tx_power, /*channel_busy_hold_ms=*/100);
    g_hal.seed_rng((uint32_t)millis() ^ (g_node.key_hash32() * 2654435761u));

    g_node.on_init(cfg);
    Serial.println(F("  node      = up. Type: send <id> <text>  |  routes | cfg | cfg set <k> <v> | status | reboot"));
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

void loop() {
    const uint64_t now = g_hal.now();

    // 1) RX: drain received frames into the Node (+ the preamble-detect throttle/LBT witness).
    size_t len = 0; float snr = 0, rssi = 0;
    while (g_iradio.poll_rx(g_rxbuf, sizeof(g_rxbuf), len, snr, rssi)) {
        // Bring-up visibility: a frame physically arrived (proves the two radios hear each other).
        // Only fires on an actual RX, so it's low-noise. cmd nibble = high 4 bits of byte 0 (§10 wire).
        ++g_rx_count;
        Serial.print(F("[rx] len="));  Serial.print((unsigned)len);
        Serial.print(F(" cmd="));      Serial.print(len ? (g_rxbuf[0] >> 4) : 0);
        Serial.print(F(" snr="));      Serial.print(snr, 1);
        Serial.print(F(" rssi="));     Serial.println(rssi, 0);
        meshroute::RxMeta meta{ snr, rssi, now, /*src_hint=*/(int16_t)-1 };   // LoRa carries no PHY src; Node derives it
        g_node.on_recv(g_rxbuf, len, meta);
    }
    if (g_iradio.take_preamble()) g_node.on_preamble_detected(now);

    // 2) Timers: fire every elapsed Node timer (beacons, RTS/ACK timeouts, retries, the duty/LBT defers).
    for (int id; (id = g_hal.pop_due_timer()) >= 0; ) g_node.on_timer((uint32_t)id);

    // 3) App pushes: surface deliveries / ACKs over the console.
    meshroute::Push pu{};
    while (g_node.next_push(pu)) {
        switch (pu.kind) {
            case meshroute::PushKind::msg_recv:
                Serial.print(F("RECV from=")); Serial.print(pu.origin); Serial.print(F(": "));
                Serial.write(pu.body, pu.body_len); Serial.println();
                break;
            case meshroute::PushKind::send_acked:
                Serial.print(F("ACKED ctr="));  Serial.println(pu.ctr); break;
            case meshroute::PushKind::send_failed:
                Serial.print(F("FAILED ctr=")); Serial.println(pu.ctr); break;
        }
    }

    // 4) Console input -> commands.
    service_console();

    // 5) Heartbeat — bring-up liveness so the console is never silent (a lone node prints nothing
    //    otherwise, and the one-time boot banner is lost across the USB re-enumeration on reset).
    //    Console-only, no protocol effect. duty_ms climbing over time = the node is TX'ing beacons.
    static uint64_t s_last_hb = 0;
    if (now - s_last_hb >= 5000) {
        s_last_hb = now;
        Serial.print(F("[hb] t="));    Serial.print((uint32_t)(now / 1000));
        Serial.print(F("s radio="));   Serial.print(g_radio_ok ? F("OK") : F("FAIL"));
        Serial.print(F(" duty_ms="));  Serial.print((uint32_t)g_hal.airtime_used_ms(3600000));
        Serial.println();
    }
}
