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

namespace P = meshroute::protocol;

// ---- node identity (static for bring-up; the OTA join/lease runtime is Slice 3) ----
#ifndef MESHROUTE_NODE_ID
#define MESHROUTE_NODE_ID 1
#endif
#ifndef MESHROUTE_KEY_HASH32
#define MESHROUTE_KEY_HASH32 (0x4D455300u | (MESHROUTE_NODE_ID & 0xFF))   // "MES\0" | id
#endif

// ---- the device stack (global ctor order = declaration order; refs bind to already-built objects) ----
static Module                  g_mod(LORA_PIN_NSS, LORA_PIN_DIO1, LORA_PIN_RST, LORA_PIN_BUSY);
static CustomSX1262            g_radio(&g_mod);
static meshroute::ArduinoClock g_clock;
static meshroute::Sx1262Radio  g_iradio(g_radio);
static meshroute::DeviceHal    g_hal(g_clock, g_iradio);
static meshroute::Node         g_node(g_hal, MESHROUTE_NODE_ID, MESHROUTE_KEY_HASH32, "node");

static uint8_t g_rxbuf[P::max_payload_bytes_hard_cap + 32];
static bool    g_radio_ok = false;   // SX1262 std_init result — surfaced in the heartbeat below

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000) { /* wait for USB CDC, but don't block forever */ }

    Serial.println(F("MeshRoute firmware v0.1 — boot"));
    Serial.print(F("  node id   = ")); Serial.println(MESHROUTE_NODE_ID);
    Serial.print(F("  freq      = ")); Serial.print((double)LORA_FREQ, 4); Serial.println(F(" MHz"));
    Serial.print(F("  sf/bw/cr  = ")); Serial.print(LORA_SF); Serial.print(F("/"));
    Serial.print((double)LORA_BW, 1); Serial.print(F("/4/")); Serial.println(LORA_CR);
    Serial.print(F("  tx power  = ")); Serial.print(LORA_TX_POWER); Serial.println(F(" dBm"));
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
    if (ok) g_iradio.begin();

    // Tell the Hal the operating point so its airtime ledger matches the Node's own airtime math.
    g_hal.configure(/*sf=*/(int16_t)LORA_SF, /*bw_hz=*/(int32_t)(LORA_BW * 1000.0),
                    /*cr=*/(int8_t)LORA_CR, /*preamble=*/(int16_t)P::preamble_sym,
                    /*power=*/(int8_t)LORA_TX_POWER, /*channel_busy_hold_ms=*/100);
    g_hal.seed_rng((uint32_t)millis() ^ (MESHROUTE_KEY_HASH32 * 2654435761u));

    meshroute::NodeConfig cfg;
    cfg.routing_sf            = LORA_SF;                         // RX + control plane on the radio's SF
    cfg.data_sf               = LORA_SF;                         // bring-up: single SF (adaptive select_data_sf is Slice 2)
    cfg.leaf_id               = 0;
    cfg.duty_cycle            = (double)LORA_DUTY_CYCLE_PCT / 100.0;
    cfg.duty_cycle_window_ms  = 3600000;                        // 1 h (ETSI)
    cfg.peer_count            = 0;                              // no sim:nodes() on device -> no rt_full telemetry
    g_node.on_init(cfg);
    Serial.println(F("  node      = up (beaconing). Type: send <id> <text>"));
}

// Accumulate a USB-CDC line; on '\n' parse it into a Command + hand it to the Node.
static void service_console() {
    static char   line[160];
    static size_t pos = 0;
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\r') continue;
        if (c == '\n') {
            meshroute::Command cmd{};
            const meshroute::console::ParseErr e = meshroute::console::parse_command(line, pos, cmd);
            pos = 0;
            if (e == meshroute::console::ParseErr::ok) {
                const meshroute::CmdResult r = g_node.on_command(cmd);
                Serial.print(F("> "));
                Serial.print(r.code == meshroute::CmdCode::queued ? F("queued ctr=") : F("err ctr="));
                Serial.print(r.ctr); Serial.print(F(" depth=")); Serial.println(r.queue_depth);
            } else if (e != meshroute::console::ParseErr::empty) {
                Serial.println(F("> parse error"));
            }
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
    if (now - s_last_hb >= 2000) {
        s_last_hb = now;
        Serial.print(F("[hb] t="));    Serial.print((uint32_t)(now / 1000));
        Serial.print(F("s radio="));   Serial.print(g_radio_ok ? F("OK") : F("FAIL"));
        Serial.print(F(" duty_ms="));  Serial.print((uint32_t)g_hal.airtime_used_ms(3600000));
        Serial.println();
    }
}
