// MeshRoute — fw_main.cpp
//
// Firmware entry point for board builds (xiao_sx1262, heltec_v3).
// Iteration 1 is just a "compiles, blinks, prints protocol summary
// over USB serial" smoke test. RadioLib + state machines land in
// later iterations.

#include <Arduino.h>

#include "protocol_constants.h"

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000) {
        // Wait for USB CDC enumeration, but don't block forever.
    }

    namespace P = meshroute::protocol;
    Serial.println(F("MeshRoute firmware v0.1 — boot"));
    Serial.print(F("  freq      = ")); Serial.print(LORA_FREQ_HZ / 1e6, 4); Serial.println(F(" MHz"));
    Serial.print(F("  sf        = ")); Serial.println(LORA_SF);
    Serial.print(F("  bw_hz     = ")); Serial.println(LORA_BW_HZ);
    Serial.print(F("  cr        = 4/")); Serial.println(LORA_CR);
    Serial.print(F("  duty %    = ")); Serial.println(LORA_DUTY_CYCLE_PCT);
    Serial.print(F("  hard cap  = ")); Serial.println(P::max_payload_bytes_hard_cap);

#ifdef BOARD_XIAO_WIO_SX1262
    Serial.println(F("  board     = XIAO nRF52840 + Wio-SX1262"));
    Serial.print(F("  NSS=")); Serial.print(LORA_PIN_NSS);
    Serial.print(F(" BUSY=")); Serial.print(LORA_PIN_BUSY);
    Serial.print(F(" RST=")); Serial.print(LORA_PIN_RST);
    Serial.print(F(" DIO1=")); Serial.println(LORA_PIN_DIO1);
#elif defined(BOARD_HELTEC_V3)
    Serial.println(F("  board     = Heltec WiFi LoRa 32 V3"));
#endif
}

void loop() {
    // TODO(iteration-3): wire RadioLib, instantiate node state, run
    // event loop. For now just heartbeat so we can confirm flashing.
    Serial.println(F("heartbeat"));
    delay(5000);
}
