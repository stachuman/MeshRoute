// MeshRoute — lib/hal/iradio.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// The internal radio seam for the DEVICE Hal (H2/H3). DeviceHal calls the radio ONLY through this
// interface, so device_hal.cpp's LOGIC (param resolution, airtime ledger, LBT busy-until hold, timer
// pumping) links + unit-tests on `native` against a MockRadio — without RadioLib/Arduino. The real
// CustomSX1262-backed `Sx1262Radio : IRadio` is a device-only TU (compiled only under the board envs).
//
// This is NOT the meshroute::Hal (that is the Node<->host contract). IRadio is one level lower: the
// host<->silicon seam that the device Hal sits on top of.
#pragma once
#include <cstddef>
#include <cstdint>
// Relative (not "hal.h"): this is a lib/hal file reaching into lib/core, and a bare "hal.h" can
// collide with a framework header of the same name on some toolchains' search order (Windows nRF52
// resolved it to the wrong hal.h -> 'TxResult does not name a type'). The ../ is unambiguous.
#include "../core/hal.h"   // TxResult, RxMeta

namespace meshroute {

struct IRadio {
    virtual ~IRadio() = default;

    // Async TX (Step 2). Arm a NON-BLOCKING transmit of the per-frame params; returns immediately:
    //   ok          -> armed; the frame is going on air. Poll poll_tx_done() for completion.
    //   radio_error -> the PHY refused to start — nothing armed.
    // The caller (DeviceHal) MUST NOT call start_transmit() while tx_busy() — half-duplex, one in-flight
    // TX. bytes borrowed for the call only (impl copies into the PHY FIFO). Airtime accounting (the
    // duty-cycle ledger) is the DeviceHal's job, not the radio's.
    virtual TxResult start_transmit(const uint8_t* bytes, size_t len,
                                    int16_t sf, int32_t bw_hz, int8_t cr, int8_t power_dbm, int16_t preamble_sym) = 0;

    // Drain the in-flight TX completion: true EXACTLY ONCE per start_transmit(), on the TxDone edge. On
    // that true edge the radio has restored the listening SF + re-armed continuous RX. false while no TX
    // is in flight, or while one is still on air. Call every loop.
    virtual bool poll_tx_done() = 0;

    // Is a start_transmit() still on air (armed, TxDone not yet drained by poll_tx_done())?
    virtual bool tx_busy() const = 0;

    // Force-recover a STUCK in-flight TX (a TxDone edge that never arrived — SPI/IRQ glitch). Stop the
    // transmit, restore the listening SF, re-arm RX, clear the in-flight state. The DeviceHal watchdog
    // calls this once the in-flight TX overruns its airtime deadline (else the node is left deaf + mute).
    virtual void abort_tx() = 0;

    // Hal::set_rx_sf -> set the spreading factor + (re)arm continuous receive on the routing/data SF.
    virtual void set_rx_sf(int sf) = 0;

    // Hal::set_rx_freq -> retune the RF carrier (per-layer gateway window switch). standby -> setFrequency -> re-arm RX
    // (freq latches in standby, same as SF). NON-pure no-op default so an IRadio that doesn't tune freq need not override.
    virtual void set_rx_freq(double /*mhz*/) {}

    // Hal::channel_busy_until -> Listen-Before-Talk primitive: is the channel busy RIGHT NOW? (SX1262
    // hardware CAD, or an RSSI-over-threshold sample). DeviceHal wraps this into a busy-until hold.
    virtual bool channel_busy() = 0;

    // fw_main RX service: did a frame arrive since the last poll? Copy up to `cap` bytes into `buf`,
    // set out_len + the PHY meta (snr/rssi; recv_ms is stamped by the caller, src_hint = -1 = no PHY
    // hint on LoRa — the Node derives src from the frame). false = nothing received.
    virtual bool poll_rx(uint8_t* buf, size_t cap, size_t& out_len, float& snr_db, float& rssi_dbm) = 0;
};

}  // namespace meshroute
