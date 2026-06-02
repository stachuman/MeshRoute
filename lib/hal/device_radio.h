// MeshRoute — lib/hal/device_radio.h  (H2 / H3 device radio)
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// The DEVICE-ONLY IRadio: drives the vendored CustomSX1262 (RadioLib SX1262 subclass). Guarded by
// ARDUINO so the native build (which has no RadioLib) skips it — the native tests use a MockRadio.
//
// RX is EVENT-DRIVEN (matches the MeshCore reference RadioLibWrappers.cpp): a DIO1 ISR
// (setPacketReceivedAction) sets a flag; the loop reads getIrqFlags() ONLY when the flag fired, so
// there is no per-loop SPI poll. DIO1 is armed on PREAMBLE_DETECTED *and* RX_DONE so the throttle's
// preamble witness stays live. Multi-stage handling follows RadioLib issue #703 (jgromes/RadioLib#703,
// raised by antirez): on a preamble-only IRQ we clear just that flag with the now-public clearIrqFlags()
// and KEEP receiving — we do NOT re-enter RX mode mid-packet (that resets the in-progress receive and
// opens a detection gap). On a completed packet we read it, then re-arm continuous RX.
//
// API contract (RadioLib 7.x, verified in SX126x.h): getIrqFlags() returns RAW SX126X_IRQ_* bits;
// startReceive()/clearIrqFlags() take PORTABLE RADIOLIB_IRQ_* flags (clearIrqStatus(raw) is protected).
//
// REALITY SPLIT: this compiles under the board envs here; the on-metal ISR + IRQ-mask behaviour is
// BENCH-VERIFIED BY THE USER (no native IRQ — native tests use a MockRadio). One thing to watch on
// metal: if continuous RX truly persists after readData() (per #703 intent) the re-arm in poll_rx is
// just belt-and-suspenders; if readData() drops to standby it is load-bearing. Either way RX stays up.
#pragma once
#include "iradio.h"

#if defined(ARDUINO)
#include <RadioLib.h>
#include "helpers/radiolib/CustomSX1262.h"   // vendored — getIrqFlags() + SX126X_IRQ_PREAMBLE_DETECTED (0x04 raw)

namespace meshroute {

// DIO1 ISR target. Set in the interrupt, drained in poll_rx. Internal linkage — device_radio.h is
// included only by fw_main.cpp (the single device TU), so there is exactly one flag + one ISR.
static volatile bool s_dio1_fired = false;
static volatile uint32_t s_isr_count = 0;   // RX DEBUG: total DIO1 ISR fires (TX-done + RX). 0 => ISR never attached.
static void sx1262_dio1_isr() { s_dio1_fired = true; ++s_isr_count; }

// DIO1 wakes on the default RX set (RX_DONE + header/crc/timeout) AND PREAMBLE_DETECTED (the witness).
// Portable RADIOLIB_IRQ_* flags: RADIOLIB_IRQ_RX_DEFAULT_FLAGS is a (1<<idx) mask; PREAMBLE_DETECTED is idx.
static constexpr uint32_t kRxIrqFlags =
    RADIOLIB_IRQ_RX_DEFAULT_FLAGS | (1UL << RADIOLIB_IRQ_PREAMBLE_DETECTED);

class Sx1262Radio : public IRadio {
public:
    explicit Sx1262Radio(CustomSX1262& radio) : _radio(radio) {}

    // Register the DIO1 ISR + arm continuous RX. Call once in setup() (post-std_init).
    bool begin() {
        _radio.setPacketReceivedAction(sx1262_dio1_isr);   // DIO1 ISR (RX-done + TX-done)
        return arm_rx();
    }

    TxResult transmit(const uint8_t* b, size_t n,
                      int16_t sf, int32_t bw_hz, int8_t cr, int8_t pw, int16_t pre) override {
        if (sf  > 0)    _radio.setSpreadingFactor(static_cast<uint8_t>(sf));
        if (bw_hz > 0)  _radio.setBandwidth(static_cast<float>(bw_hz) / 1000.0f);   // RadioLib wants kHz
        if (cr  > 0)    _radio.setCodingRate(static_cast<uint8_t>(cr));
        if (pw  > -100) _radio.setOutputPower(static_cast<int8_t>(pw));
        if (pre > 0)    _radio.setPreambleLength(static_cast<uint16_t>(pre));
        const int16_t st = _radio.transmit(const_cast<uint8_t*>(b), n);            // blocking TX
        arm_rx();                                                                  // re-arm continuous RX (clears the TX-done flag)
        return st == RADIOLIB_ERR_NONE ? TxResult::ok : TxResult::radio_error;
    }

    void set_rx_sf(int sf) override {
        _radio.setSpreadingFactor(static_cast<uint8_t>(sf));
        arm_rx();
    }

    // LBT: SX1262 hardware CAD. NB: currently BYPASSED (cfg.lbt_enabled=false) because scanChannel()
    // can block unbounded on the CAD-done IRQ — must be made non-blocking before LBT is re-enabled.
    bool channel_busy() override { return _radio.scanChannel() == RADIOLIB_LORA_DETECTED; }

    // Event-driven: returns true (a frame) ONLY after the DIO1 ISR fired AND it was an RX_DONE. A
    // preamble-only IRQ latches the throttle witness, clears just that flag (issue #703: keep RXing,
    // do NOT re-arm mid-packet), and returns false. No SPI at all when nothing has fired.
    bool poll_rx(uint8_t* buf, size_t cap, size_t& out_len, float& snr_db, float& rssi_dbm) override {
        if (!s_dio1_fired) return false;             // nothing since last drain — skip the SPI read
        s_dio1_fired = false;
        const uint16_t irq = _radio.getIrqFlags();   // RAW SX126X_IRQ_* bits
        Serial.print(F("[irq=")); Serial.print(irq, HEX); Serial.print(F("]"));   // RX DEBUG: flags on each RX-side fire
        const bool pre = (irq & SX126X_IRQ_PREAMBLE_DETECTED) != 0;
        if (pre && !_pre_seen) { _preamble = true; _pre_seen = true; }   // rising edge -> witness
        if ((irq & RADIOLIB_SX126X_IRQ_RX_DONE) == 0) {
            // Preamble/header without a completed packet: clear it (portable flag), keep the in-progress
            // receive running — re-arming here would abort the packet mid-flight (#703).
            if (pre) _radio.clearIrqFlags(1UL << RADIOLIB_IRQ_PREAMBLE_DETECTED);
            return false;
        }
        size_t l = _radio.getPacketLength();
        if (l > cap) l = cap;
        const int16_t st = _radio.readData(buf, l);  // clears the RX IRQs
        arm_rx();                                     // packet complete -> re-arm continuous RX
        if (st != RADIOLIB_ERR_NONE) return false;
        out_len  = l;
        snr_db   = _radio.getSNR();
        rssi_dbm = _radio.getRSSI();
        return true;
    }

    // The loop drains this each iteration -> Node::on_preamble_detected(now).
    bool take_preamble() { const bool f = _preamble; _preamble = false; return f; }

    uint32_t isr_count() const { return s_isr_count; }   // RX DEBUG: surfaced in the heartbeat

private:
    // Arm DIO1 + continuous RX (RX_TIMEOUT_INF) on the masked IRQ set; clear the stale ISR flag + edge.
    bool arm_rx() {
        const int16_t st = _radio.startReceive(RADIOLIB_SX126X_RX_TIMEOUT_INF, kRxIrqFlags, kRxIrqFlags, 0);
        s_dio1_fired = false;
        _pre_seen = false;
        return st == RADIOLIB_ERR_NONE;
    }

    CustomSX1262& _radio;
    bool _preamble = false;   // latched preamble event (drained by take_preamble)
    bool _pre_seen = false;   // edge-detect: preamble already latched this RX cycle
};

}  // namespace meshroute
#endif  // ARDUINO
