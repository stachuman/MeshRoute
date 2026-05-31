// MeshRoute — fw_main.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Firmware entry point for board builds (xiao_sx1262, heltec_v3). H0 scope:
// the smallest device main that PROVES the vendored MeshCore PHY header
// compiles + links on metal — it constructs the RadioLib Module + the
// CustomSX1262 subclass and runs std_init() (begin/CRC/TCXO/DIO2-rf-switch/
// RXEN/RX-boost), then heartbeats over USB serial. The duty-cycle MAC, timer
// wheel and Node wiring (the device meshroute::Hal facade) land in H1–H3.
//
// Reality split: I compile-check this here; the on-metal bring-up (flash +
// confirm the radio inits) is the user's, on the XIAO.

#include <Arduino.h>
#include <RadioLib.h>

#include "helpers/radiolib/CustomSX1262.h"   // vendored from MeshCore — DO NOT EDIT
#include "protocol_constants.h"

// Control pins -> Module(NSS, DIO1, RST, BUSY). The board/analog wiring
// (RXEN/TCXO/DIO2-as-rf-switch/...) is read from -D macros inside std_init.
static Module        g_mod(LORA_PIN_NSS, LORA_PIN_DIO1, LORA_PIN_RST, LORA_PIN_BUSY);
static CustomSX1262  g_radio(&g_mod);

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000) {
        // Wait for USB CDC enumeration, but don't block forever.
    }

    namespace P = meshroute::protocol;
    Serial.println(F("MeshRoute firmware v0.1 — boot"));
    Serial.print(F("  freq      = ")); Serial.print((double)LORA_FREQ, 4); Serial.println(F(" MHz"));
    Serial.print(F("  sf        = ")); Serial.println(LORA_SF);
    Serial.print(F("  bw        = ")); Serial.print((double)LORA_BW, 1); Serial.println(F(" kHz"));
    Serial.print(F("  cr        = 4/")); Serial.println(LORA_CR);
    Serial.print(F("  tx power  = ")); Serial.print(LORA_TX_POWER); Serial.println(F(" dBm"));
    Serial.print(F("  duty %    = ")); Serial.println(LORA_DUTY_CYCLE_PCT);
    Serial.print(F("  hard cap  = ")); Serial.println(P::max_payload_bytes_hard_cap);
    Serial.print(F("  pins      = NSS:")); Serial.print(LORA_PIN_NSS);
    Serial.print(F(" DIO1:")); Serial.print(LORA_PIN_DIO1);
    Serial.print(F(" RST:")); Serial.print(LORA_PIN_RST);
    Serial.print(F(" BUSY:")); Serial.println(LORA_PIN_BUSY);

#ifdef BOARD_XIAO_WIO_SX1262
    Serial.println(F("  board     = XIAO nRF52840 + Wio-SX1262"));
#elif defined(BOARD_HELTEC_V3)
    Serial.println(F("  board     = Heltec WiFi LoRa 32 V3"));
#endif

    // Bring up the SX1262. On boards with a dedicated SPI bus (P_LORA_SCLK
    // defined, e.g. Heltec V3) std_init begins that bus; on the XIAO the
    // radio rides the default SPI, so std_init() takes no argument.
#if defined(P_LORA_SCLK)
    bool ok = g_radio.std_init(&SPI);
#else
    bool ok = g_radio.std_init();
#endif
    Serial.print(F("  radio     = ")); Serial.println(ok ? F("OK") : F("INIT FAILED"));
}

void loop() {
    // H1–H3 replace this with: pump the timer wheel + service radio RX/TX and
    // drive a Node over the device meshroute::Hal facade. For H0 just heartbeat
    // so flashing + serial are confirmable on metal.
    Serial.println(F("heartbeat"));
    delay(5000);
}
