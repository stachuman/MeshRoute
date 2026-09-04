// MeshRoute — tools/probe_ble_line/fakes/bluefruit.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §0f PROBE SHIM — a host stand-in for Adafruit's <bluefruit.h>, so the REAL `src/device_ble.h` nRF52 arm can be
// COMPILED AND EXECUTED on the host. Nothing else in this directory models firmware behaviour: the one thing that
// must be faithful is the NUS RX seam, because that is what `service_rx()` drains.
//
//   • `BLEUart::available()/read()` deliver the inbound stream ONE BYTE AT A TIME out of a FIFO the probe fills in
//     whatever ATT-sized chunks it chooses. That is exactly what the SoftDevice's BLEUart FIFO does, and it is the
//     property the chunking cases are about — the probe controls the chunk boundary by choosing WHEN to call
//     `service_rx()`, never by reshaping this class.
//   • `BLEUart::write()` records every outbound byte in order, so the refusal line and a dispatch reply can be
//     compared byte-for-byte.
//
// ⛔ THIS FAKE MAKES NO INTAKE DECISION. It holds no line buffer, no newline logic, no length limit — all of that
//    is the real header's, which is the only reason this probe is a gate and not a model of itself.
// ⛔ NOT a second Arduino fake: `Print`, `F()` and `HEX` come from tools/probe_console_sink/fakes/Arduino.h (U1).
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string>
#include <vector>

// ---- SoftDevice / BLE stack constants the real header names ------------------------------------------------------
#define BLE_CONN_HANDLE_INVALID                        0xFFFFu
#define BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE    0x06u
#define BLE_GAP_SEC_STATUS_SUCCESS                     0x00u
#define SECMODE_ENC_WITH_MITM                          0x33u
#define BANDWIDTH_MAX                                  3

// ---- the NUS characteristic pair ---------------------------------------------------------------------------------
class BLEUart {
public:
    void   setPermission(uint8_t r, uint8_t w) { perm_read = r; perm_write = w; }
    bool   begin()                             { started = true; return true; }

    int    available()                         { return static_cast<int>(rx.size() - rx_pos); }
    int    read()                              { return (rx_pos < rx.size()) ? static_cast<unsigned char>(rx[rx_pos++]) : -1; }
    size_t write(const uint8_t* p, size_t n)   { tx.append(reinterpret_cast<const char*>(p), n);
                                                 tx_calls.push_back(std::string(reinterpret_cast<const char*>(p), n));
                                                 return n; }

    // ---- probe-side controls (never touched by the production header) --------------------------------------------
    void push(const char* p, size_t n)         { rx.append(p, n); }
    void reset()                               { rx.clear(); rx_pos = 0; tx.clear(); tx_calls.clear(); }

    std::string              rx;               // everything the client has written, in order
    size_t                   rx_pos = 0;       // how much service_rx() has drained
    std::string              tx;               // everything the node has notified, concatenated
    std::vector<std::string> tx_calls;         // ...and split by write() call, so "exactly one refusal" is checkable
    uint8_t                  perm_read = 0, perm_write = 0;
    bool                     started = false;
};

// ---- the connection handle (only getMtu() is reached, from tx_line) ------------------------------------------------
class BLEConnection {
public:
    uint16_t getMtu() const { return mtu; }
    uint16_t mtu = 247;
};

// ---- the singleton stack object ------------------------------------------------------------------------------------
class AdafruitBluefruit {
public:
    struct SecurityT {
        bool setPIN(const char* p)                          { pin = p ? p : ""; return pin_ok; }
        void setPairCompleteCallback(void (*)(uint16_t, uint8_t)) {}
        void setSecuredCallback(void (*)(uint16_t))              {}
        std::string pin;
        bool        pin_ok = true;
    } Security;

    struct PeriphT {
        void setConnectCallback(void (*)(uint16_t))          {}
        void setDisconnectCallback(void (*)(uint16_t, uint8_t)) {}
    } Periph;

    struct AdvertisingT {
        void addFlags(uint8_t)               {}
        void addTxPower()                    {}
        void addService(BLEUart&)            {}
        void restartOnDisconnect(bool)       {}
        void setInterval(uint16_t, uint16_t) {}
        void setFastTimeout(uint16_t)        {}
        bool isRunning() const               { return running; }
        void start(uint16_t)                 { running = true; }
        void stop()                          { running = false; }
        bool running = false;
    } Advertising;

    struct ScanResponseT { void addName() {} } ScanResponse;

    void           configPrphBandwidth(int b) { bandwidth = b; }
    bool           begin(int prph, int central) { (void)prph; (void)central; begun = true; return true; }
    void           setTxPower(int dbm)        { tx_power = dbm; }
    bool           setName(const char* n)     { name = n ? n : ""; return true; }
    void           autoConnLed(bool on)       { conn_led = on; }
    BLEConnection* Connection(uint16_t h)     { return (h == BLE_CONN_HANDLE_INVALID) ? nullptr : &conn; }

    BLEConnection conn;
    std::string   name;
    int           bandwidth = 0, tx_power = 0;
    bool          begun = false, conn_led = true;
};

extern AdafruitBluefruit Bluefruit;
