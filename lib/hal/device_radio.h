// MeshRoute — lib/hal/device_radio.h  (H2 / H3 device radio)
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// The DEVICE-ONLY IRadio: drives the vendored CustomSX1262 (RadioLib SX1262 subclass). Guarded by
// ARDUINO so the native build (which has no RadioLib) skips it — the native tests use a MockRadio.
//
// REALITY SPLIT: this compiles under the board envs (xiao_sx1262 / heltec_v3) here; the on-metal
// behaviour (real CAD timing, the preamble-detect IRQ flags, TX/RX state) is BENCH-VERIFIED BY THE
// USER. The MeshRoute-owned logic above it (DeviceHal) is already native-proven against MockRadio.
#pragma once
#include "iradio.h"

#if defined(ARDUINO)
#include <RadioLib.h>
#include "helpers/radiolib/CustomSX1262.h"   // vendored — getIrqFlags() + SX126X_IRQ_PREAMBLE_DETECTED

namespace meshroute {

class Sx1262Radio : public IRadio {
public:
    explicit Sx1262Radio(CustomSX1262& radio) : _radio(radio) {}

    // Arm continuous RX after std_init(). Call once in setup() (post-std_init).
    bool begin() { return _radio.startReceive() == RADIOLIB_ERR_NONE; }

    TxResult transmit(const uint8_t* b, size_t n,
                      int16_t sf, int32_t bw_hz, int8_t cr, int8_t pw, int16_t pre) override {
        Serial.print(F("X(")); Serial.print((unsigned)n); Serial.print(F("/")); Serial.print(sf);  // TEMP TRACE
        if (sf  > 0)    _radio.setSpreadingFactor(static_cast<uint8_t>(sf));
        if (bw_hz > 0)  _radio.setBandwidth(static_cast<float>(bw_hz) / 1000.0f);   // RadioLib wants kHz
        if (cr  > 0)    _radio.setCodingRate(static_cast<uint8_t>(cr));
        if (pw  > -100) _radio.setOutputPower(static_cast<int8_t>(pw));
        if (pre > 0)    _radio.setPreambleLength(static_cast<uint16_t>(pre));
        Serial.print(F("t"));                                                      // TEMP TRACE: entering blocking TX
        const int16_t st = _radio.transmit(const_cast<uint8_t*>(b), n);            // blocking TX
        Serial.print(F(")")); Serial.print(st);                                    // TEMP TRACE: TX returned (st code)
        _radio.startReceive();                                                     // back to listening
        Serial.print(F("R"));                                                      // TEMP TRACE: RX re-armed
        _pre_seen = false;
        return st == RADIOLIB_ERR_NONE ? TxResult::ok : TxResult::radio_error;
    }

    void set_rx_sf(int sf) override {
        _radio.setSpreadingFactor(static_cast<uint8_t>(sf));
        _radio.startReceive();
        _pre_seen = false;
    }

    // LBT: SX1262 hardware CAD. DRIFT — real channel-activity-detection, not the sim's airtime estimate.
    bool channel_busy() override { return _radio.scanChannel() == RADIOLIB_LORA_DETECTED; }

    // Polled each loop: latches a preamble (rising-edge -> on_preamble_detected witness) and, on RxDone,
    // reads the frame + its SNR/RSSI then re-arms RX. (No DIO1 ISR needed: getIrqFlags() reads the raw
    // IRQ register, and a few-ms poll cadence catches a tens-of-ms preamble; the witness is idempotent.)
    bool poll_rx(uint8_t* buf, size_t cap, size_t& out_len, float& snr_db, float& rssi_dbm) override {
        const uint16_t irq = _radio.getIrqFlags();
        const bool pre = (irq & SX126X_IRQ_PREAMBLE_DETECTED) != 0;
        if (pre && !_pre_seen) { _preamble = true; _pre_seen = true; }   // rising edge
        if (!pre) _pre_seen = false;                                     // window ended
        if ((irq & RADIOLIB_SX126X_IRQ_RX_DONE) == 0) return false;
        size_t l = _radio.getPacketLength();
        if (l > cap) l = cap;
        const int16_t st = _radio.readData(buf, l);                     // clears the RX IRQs
        _radio.startReceive();
        _pre_seen = false;
        if (st != RADIOLIB_ERR_NONE) return false;
        out_len  = l;
        snr_db   = _radio.getSNR();
        rssi_dbm = _radio.getRSSI();
        return true;
    }

    // The loop drains this each iteration -> Node::on_preamble_detected(now).
    bool take_preamble() { const bool f = _preamble; _preamble = false; return f; }

private:
    CustomSX1262& _radio;
    bool _preamble = false;   // latched preamble event (drained by take_preamble)
    bool _pre_seen = false;   // edge-detect: preamble already latched this RX cycle
};

}  // namespace meshroute
#endif  // ARDUINO
