// MeshRoute — lib/hal/device_radio.h  (H2 / H3 device radio)
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// The DEVICE-ONLY IRadio: drives the vendored CustomSX1262 (RadioLib SX1262 subclass). Guarded by
// ARDUINO so the native build (which has no RadioLib) skips it — the native tests use a MockRadio.
//
// RX is POLLED: poll_rx() reads getIrqFlags() each loop and acts on PREAMBLE_DETECTED + RX_DONE. (The
// event-driven setPacketReceivedAction/DIO1 version is in git history — REVERTED here: on metal it
// delivered no RX events [isr flag never surfaced a packet], so we fall back to polling to get a working
// 2-device RX baseline; re-add the IRQ once RX is proven + verifiable against this baseline.)
//
// TEMP RX DEBUG: [pre] on a detected preamble, [rxdone irq=..] on a completed packet — to tell whether
// the radio detects the other node at all (bring-up diagnostic; strip once 2 nodes talk).
//
// REALITY SPLIT: compiles under the board envs here; on-metal RX is BENCH-VERIFIED BY THE USER. The
// MeshRoute logic above it (DeviceHal) is native-proven against MockRadio.
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
        ++_tx_count;
        // TX DEBUG: mirror of the [rx] line — the OTHER half of the handshake. cmd nibble = byte0 high 4 bits.
        if (sf > 0) _cur_sf = sf;                                                  // TEMP DEBUG
        Serial.print(F("[tx] cmd=")); Serial.print(n ? (b[0] >> 4) : 0); Serial.print(F(" len=")); Serial.print((unsigned)n); Serial.print(F(" sf=")); Serial.print(_cur_sf); Serial.print(F(" t=")); Serial.println(millis());  // TEMP DEBUG: + tx SF + ms timestamp
        if (sf  > 0)    _radio.setSpreadingFactor(static_cast<uint8_t>(sf));
        if (bw_hz > 0)  _radio.setBandwidth(static_cast<float>(bw_hz) / 1000.0f);   // RadioLib wants kHz
        if (cr  > 0)    _radio.setCodingRate(static_cast<uint8_t>(cr));
        if (pw  > -100) _radio.setOutputPower(static_cast<int8_t>(pw));
        if (pre > 0)    _radio.setPreambleLength(static_cast<uint16_t>(pre));
        const int16_t st = _radio.transmit(const_cast<uint8_t*>(b), n);            // blocking TX
        // Return RX to the LISTENING SF (routing), not the SF we just TX'd on. After a DATA (sent on the
        // data SF) the ACK comes back on routing — without this the sender stays parked on the data SF
        // and misses the ACK, forcing RTS retries. The sim's radio never moves the rx SF on TX, so this
        // just makes metal match the sim's assumption.
        if (_rx_sf > 0 && _rx_sf != _cur_sf) { _cur_sf = _rx_sf; _radio.setSpreadingFactor(static_cast<uint8_t>(_rx_sf)); }
        _radio.startReceive();                                                     // back to listening (on the rx/listening SF)
        _pre_seen = false;
        return st == RADIOLIB_ERR_NONE ? TxResult::ok : TxResult::radio_error;
    }

    void set_rx_sf(int sf) override {
        _cur_sf = sf; _rx_sf = sf;                                                 // _rx_sf = the LISTENING SF; transmit() restores it post-TX (so we don't sit on the data SF after a DATA)
        Serial.print(F("[rxsf=")); Serial.print(sf); Serial.print(F(" t=")); Serial.print(millis()); Serial.println(F("]"));   // TEMP DEBUG: RX SF hop + ms timestamp (data-SF window = Δt between consecutive hops)
        _radio.standby();                                                          // SX1262: SetModulationParams (the SF) only latches in STANDBY — issued mid-RX it is dropped, so set_rx_sf was re-arming on the OLD SF (the data-leg bug)
        _radio.setSpreadingFactor(static_cast<uint8_t>(sf));
        _radio.startReceive();
        _pre_seen = false;
    }

    // LBT: SX1262 hardware CAD. NB: currently BYPASSED (cfg.lbt_enabled=false) because scanChannel()
    // can block unbounded on the CAD-done IRQ — must be made non-blocking before LBT is re-enabled.
    bool channel_busy() override { return _radio.scanChannel() == RADIOLIB_LORA_DETECTED; }

    // Polled each loop: latches a preamble (rising-edge -> on_preamble_detected witness) and, on RxDone,
    // reads the frame + its SNR/RSSI then re-arms RX. getIrqFlags() reads the raw IRQ register directly,
    // so this works regardless of DIO1 interrupt routing.
    bool poll_rx(uint8_t* buf, size_t cap, size_t& out_len, float& snr_db, float& rssi_dbm) override {
        const uint16_t irq = _radio.getIrqFlags();
        const bool pre = (irq & SX126X_IRQ_PREAMBLE_DETECTED) != 0;
        if (pre && !_pre_seen) { _preamble = true; _pre_seen = true; Serial.print(F("[pre sf=")); Serial.print(_cur_sf); Serial.print(F(" t=")); Serial.print(millis()); Serial.print(F("]")); }  // RX DEBUG: + SF + ms timestamp
        if (!pre) _pre_seen = false;                                     // window ended
        if ((irq & RADIOLIB_SX126X_IRQ_RX_DONE) == 0) return false;
        Serial.print(F("[rxdone irq=")); Serial.print(irq, HEX); Serial.print(F(" sf=")); Serial.print(_cur_sf); Serial.print(F(" t=")); Serial.print(millis()); Serial.print(F("]"));   // RX DEBUG: + SF + ms timestamp
        size_t l = _radio.getPacketLength();
        if (l > cap) l = cap;
        const int16_t st = _radio.readData(buf, l);                     // clears the RX IRQs
        // Read the packet's SNR/RSSI BEFORE re-arming — after startReceive() they reflect the live
        // channel (noise floor), not the just-received frame.
        snr_db   = _radio.getSNR();
        rssi_dbm = _radio.getRSSI();
        _radio.startReceive();
        _pre_seen = false;
        if (st != RADIOLIB_ERR_NONE) { Serial.print(F("[rxbad st=")); Serial.print(st); Serial.print(F(" t=")); Serial.print(millis()); Serial.println(F("]")); return false; }   // TEMP DEBUG: frame arrived but failed to decode + ms timestamp
        out_len  = l;
        return true;
    }

    // The loop drains this each iteration -> Node::on_preamble_detected(now).
    bool take_preamble() { const bool f = _preamble; _preamble = false; return f; }

    uint32_t tx_count() const { return _tx_count; }   // status diagnostic (frames transmitted)

private:
    CustomSX1262& _radio;
    uint32_t _tx_count = 0;   // frames transmitted (status diagnostic)
    bool _preamble = false;   // latched preamble event (drained by take_preamble)
    bool _pre_seen = false;   // edge-detect: preamble already latched this RX cycle
    int  _cur_sf   = 0;       // TEMP DEBUG: radio's current SF (set on tx + set_rx_sf), shown in rx/tx traces
    int  _rx_sf    = 0;       // the LISTENING SF (set by set_rx_sf); transmit() restores it post-TX so the sender doesn't sit on the data SF after a DATA and miss the ACK
};

}  // namespace meshroute
#endif  // ARDUINO
