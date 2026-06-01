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
#include "hal.h"   // TxResult, RxMeta

namespace meshroute {

struct IRadio {
    virtual ~IRadio() = default;

    // Hal::tx -> apply the per-frame params then transmit. Returns the PHY result; airtime accounting
    // (the duty-cycle ledger) is the DeviceHal's job, not the radio's. bytes borrowed for the call only.
    virtual TxResult transmit(const uint8_t* bytes, size_t len,
                              int16_t sf, int32_t bw_hz, int8_t cr, int8_t power_dbm, int16_t preamble_sym) = 0;

    // Hal::set_rx_sf -> set the spreading factor + (re)arm continuous receive on the routing/data SF.
    virtual void set_rx_sf(int sf) = 0;

    // Hal::channel_busy_until -> Listen-Before-Talk primitive: is the channel busy RIGHT NOW? (SX1262
    // hardware CAD, or an RSSI-over-threshold sample). DeviceHal wraps this into a busy-until hold.
    virtual bool channel_busy() = 0;

    // fw_main RX service: did a frame arrive since the last poll? Copy up to `cap` bytes into `buf`,
    // set out_len + the PHY meta (snr/rssi; recv_ms is stamped by the caller, src_hint = -1 = no PHY
    // hint on LoRa — the Node derives src from the frame). false = nothing received.
    virtual bool poll_rx(uint8_t* buf, size_t cap, size_t& out_len, float& snr_db, float& rssi_dbm) = 0;
};

}  // namespace meshroute
