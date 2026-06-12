// MeshRoute — src/device_ble.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// BLE companion transport (Nordic UART Service over the S140 SoftDevice) — the iOS/phone-facing twin of the
// USB-CDC console. XIAO nRF52840 ONLY; an inert no-op stub on ESP32 (Heltec) + the native/host build, so
// fw_main's loop() calls compile unchanged on every target. PURE TRANSPORT: it owns Bluefruit + the BLEUart
// + the advertising-window policy + the inbound line buffer, and knows NOTHING of Node/commands — fw_main
// supplies a DispatchFn (one line -> one JSON reply) and hands it pre-formatted Push JSON to TX. Mirrors the
// device_nv.h / device_rng.h board guards. The companion link speaks the NDJSON schema of
// docs/specs/2026-05-30-device-console-design.md §4 (the same one the sim's FirmwareNode emits).
//
// REALITY SPLIT: compile-verified under the xiao env; the on-metal advertise / pair / RX-TX, and the
// LoRa-timing soak under a live S140 (spec §8.1), are the user's bench (Steps 6 + 9). The GATT chars are
// UNPAIRED until Step 6 secures them — begin() prints that loudly; this is a documented phase boundary, not
// a silent fallback.
//
// KEYSTONE: begin() is the ONLY caller of Bluefruit.begin(), and it sets mrrng::sd_enabled()=true IMMEDIATELY
// after. Once the SoftDevice owns the radio, the bare-metal NRF_RNG path in device_rng.h would HardFault, so
// every later regen()/seed draw must route through the SD entropy pool (device_rng.h / spec §8.1).
#pragma once
#include <stdint.h>
#include <stddef.h>

#if defined(ARDUINO) && (defined(NRF52_SERIES) || defined(ARDUINO_ARCH_NRF52) || defined(NRF52840_XXAA) || defined(BOARD_XIAO_WIO_SX1262))
  #define MRBLE_NRF52 1
#endif

namespace mrble {

// fw_main supplies this: handle ONE inbound console line, writing a single NDJSON response line into `out`
// (NUL-terminated, '\n'-ended); returns bytes written (0 = no reply). Keeps device_ble.h free of any
// Node / command / console_json dependency (fw_main owns g_node + the encoders).
using DispatchFn = size_t (*)(const char* line, size_t len, char* out, size_t cap);

#if defined(MRBLE_NRF52)
bool begin(uint8_t mode, uint8_t period_min, uint32_t pin, const char* name, DispatchFn dispatch);
void on_tick(uint64_t now_ms);              // advertising-window policy: start/stop advertising per ble_mode
void service_rx();                          // poll the NUS RX FIFO -> line buffer -> dispatch -> TX the reply
void tx_line(const char* s, size_t n);      // TX one pre-formatted JSON line to the client (no-op if none)
bool connected();                           // a companion is connected (used to inhibit idle light-sleep)
#else
// Inert on ESP32 + native: every entry is a no-op so fw_main compiles unchanged on all targets.
inline bool begin(uint8_t, uint8_t, uint32_t, const char*, DispatchFn) { return false; }
inline void on_tick(uint64_t) {}
inline void service_rx() {}
inline void tx_line(const char*, size_t) {}
inline bool connected() { return false; }
#endif

}  // namespace mrble


#if defined(MRBLE_NRF52)
// ===== device implementation (XIAO nRF52840) — header-inline, included by the one device TU (fw_main) =====
#include <bluefruit.h>
#include "companion_policy.h"   // meshroute::CompanionPolicy / BleMode (lib/core) — the off/on/periodic scheduler
#include "device_rng.h"         // mrrng::sd_enabled() — the SD-RNG keystone flag
#include <string.h>
#include <stdio.h>              // snprintf — format the 6-digit passkey

namespace mrble {
namespace {

constexpr uint32_t kAdvWindowMs = 30000;    // periodic-mode advertising window (30 s); matches the CompanionPolicy test

BLEUart                    g_bleuart;        // Nordic UART Service (RXD write / TXD notify)
meshroute::CompanionPolicy g_policy;         // when to advertise (off/on/periodic)
DispatchFn                 g_dispatch = nullptr;
bool                       g_started     = false;
// Connection count, shared with the Bluefruit connect/disconnect callbacks. On the single-core nRF52840 those
// callbacks run in a higher-priority context that PREEMPTS loop() (not a parallel core), so a `volatile` byte
// is the correct, sufficient idiom: volatile forces a fresh load (no register caching) and a byte store is
// atomic — no memory barrier / critical section is needed (adding one would be cargo-cult). Do NOT "fix".
volatile uint8_t           g_conn_count  = 0;
char                       g_pin_str[7]  = {0}; // the 6-digit MITM passkey as a string. setPIN() stores it BY
                                                // POINTER (no copy), so it MUST outlive pairing -> a static.
char                       g_line[160];       // inbound line buffer (mirrors the USB console)
size_t                     g_pos        = 0;
bool                       g_overflow   = false;
char                       g_out[256];        // outbound JSON scratch (one NDJSON line)

// Bluefruit callbacks — run in the Bluefruit event-task context (NOT a hard ISR), so Serial.print is safe here
// (the stock pairing_pin.ino example prints from these too). Concise on-USB signal for the bench bring-up.
void on_connect(uint16_t)             { if (g_conn_count < 255) ++g_conn_count; Serial.println(F("[ble] connected (pairing required before GATT)")); }
void on_disconnect(uint16_t, uint8_t r){ if (g_conn_count > 0)  --g_conn_count; Serial.print(F("[ble] disconnected reason=0x")); Serial.println(r, HEX); }
void on_secured(uint16_t)             { Serial.println(F("[ble] link secured (paired/bonded)")); }
void on_pair_complete(uint16_t, uint8_t st) { Serial.print(F("[ble] pairing ")); Serial.println(st == BLE_GAP_SEC_STATUS_SUCCESS ? F("OK") : F("FAILED")); }

void dispatch_current_line() {
    g_line[g_pos] = '\0';
    if (g_overflow) {                                       // a line longer than the buffer -> fail loud, drop it
        static const char kTooLong[] = "{\"err\":\"line_too_long\"}\n";
        g_bleuart.write(reinterpret_cast<const uint8_t*>(kTooLong), sizeof(kTooLong) - 1);
        g_overflow = false; g_pos = 0; return;
    }
    if (g_dispatch && g_pos > 0) {
        const size_t n = g_dispatch(g_line, g_pos, g_out, sizeof g_out);
        if (n) g_bleuart.write(reinterpret_cast<const uint8_t*>(g_out), n);
    }
    g_pos = 0;
}

}  // namespace

inline bool begin(uint8_t mode, uint8_t period_min, uint32_t pin, const char* name, DispatchFn dispatch) {
    if (mode == 0) return false;                            // off -> never bring the SoftDevice up
    g_dispatch = dispatch;

    // FIX: the default ATT MTU is 23 (20-B notification payload), so a 125-B `ready` / a long msg_recv
    // splits across ~7 notifications and only the first survives the SoftDevice's tiny default HVN queue
    // — the client gets a truncated line that never decodes. BANDWIDTH_MAX raises the MTU (to 247, so the
    // whole reply fits in ONE notification) AND enlarges the notify queue. MUST precede Bluefruit.begin().
    Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
    Bluefruit.begin(/*prph=*/1, /*central=*/0);
    // <-- KEYSTONE: Bluefruit.begin() just enabled the SoftDevice, so the SD now owns the radio. Set the flag
    // HERE, before any failure-return below (e.g. setPIN failing): once the SD is up it stays up, so a later
    // regen MUST use the SD RNG path regardless. Do NOT move this after setPIN — that would leave a setPIN-fail
    // path with the SD up but the flag false -> regen HardFaults on the bare-metal NRF_RNG.
    mrrng::sd_enabled() = true;
    Bluefruit.setTxPower(4);                               // dBm (nRF52840 supports up to +8; 4 is a safe default)
    if (name && name[0]) Bluefruit.setName(name);
    Bluefruit.autoConnLed(false);                          // don't assume a status LED is wired on the XIAO

    // Security (spec §A.3, MANDATORY): a STATIC 6-digit MITM passkey. setPIN() auto-sets mitm=1 / legacy-SC /
    // IO=DisplayOnly, so iOS shows a passkey-ENTRY prompt (the phone types this PIN). The string is stored BY
    // POINTER by the SoftDevice (g_pin_str is a static, so it outlives pairing). pin is cfg-validated 0..999999;
    // %06lu zero-pads to the required 6 chars (% 1000000 is a defensive clamp). Bonding auto-persists to InternalFS.
    snprintf(g_pin_str, sizeof g_pin_str, "%06lu", (unsigned long)(pin % 1000000u));
    if (!Bluefruit.Security.setPIN(g_pin_str)) return false;   // fail loud: refuse to serve INSECURE BLE (none > open)
    Bluefruit.Security.setPairCompleteCallback(on_pair_complete);
    Bluefruit.Security.setSecuredCallback(on_secured);

    Bluefruit.Periph.setConnectCallback(on_connect);
    Bluefruit.Periph.setDisconnectCallback(on_disconnect);

    // The §A.3 GATT gate: RXD/TXD require an encrypted + MITM-bonded link before a client can write/subscribe.
    // setPermission is inherited from BLEService and is the floor for both NUS characteristics — it MUST be set
    // BEFORE begin(). iOS pairs on the first touch of an encrypted char (its TX-notify subscribe). With this set,
    // the SoftDevice rejects unpaired RXD writes, so service_rx() only ever sees commands from a bonded client.
    g_bleuart.setPermission(SECMODE_ENC_WITH_MITM, SECMODE_ENC_WITH_MITM);
    g_bleuart.begin();

    // Advertising packet: iOS scans by the NUS SERVICE UUID, so addService(bleuart) is MANDATORY — it embeds
    // 6E400001-B5A3-F393-E0A9-E50E24DCCA9E into the advert. The human-readable name rides the scan response.
    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addTxPower();
    Bluefruit.Advertising.addService(g_bleuart);
    Bluefruit.ScanResponse.addName();
    Bluefruit.Advertising.restartOnDisconnect(true);       // auto re-advertise after a disconnect; on_tick still owns
                                                           //   the window (it stops a stray re-advert next idle tick)
    Bluefruit.Advertising.setInterval(32, 244);            // 0.625 ms units: 20 ms fast / 152.5 ms slow (Apple-friendly)
    Bluefruit.Advertising.setFastTimeout(30);

    g_policy.configure(static_cast<meshroute::BleMode>(mode), period_min, kAdvWindowMs);
    g_started = true;
    return true;
}

inline void on_tick(uint64_t now_ms) {
    if (!g_started) return;
    const meshroute::CompanionPolicy::Tick t = g_policy.on_tick(now_ms);
    // Reconcile against the ACTUAL stack state (isRunning), NOT a shadow flag: restartOnDisconnect(true) can
    // auto-restart advertising from the SoftDevice behind our back, which a shadow bool would miss (leaving a
    // stray advert that never gets stopped). Never touch advertising while a client is connected — prph=1 means
    // a single link, and the stack already stops advertising on connect, so there is nothing to start/stop.
    const bool running = Bluefruit.Advertising.isRunning();
    if      (t.should_advertise  && !running && g_conn_count == 0) Bluefruit.Advertising.start(0);  // 0 = no timeout
    else if (!t.should_advertise &&  running && g_conn_count == 0) Bluefruit.Advertising.stop();
}

inline void service_rx() {
    if (!g_started) return;
    while (g_bleuart.available()) {
        const char c = static_cast<char>(g_bleuart.read());
        if (c == '\r') continue;
        if (c == '\n')                          { dispatch_current_line(); }
        else if (g_pos < sizeof(g_line) - 1)    { g_line[g_pos++] = c; }
        else                                    { g_overflow = true; }   // keep eating until '\n', then fail loud
    }
}

inline void tx_line(const char* s, size_t n) {
    if (g_conn_count == 0) return;
    g_bleuart.write(reinterpret_cast<const uint8_t*>(s), n);
}

inline bool connected() { return g_conn_count > 0; }

}  // namespace mrble
#endif  // MRBLE_NRF52
