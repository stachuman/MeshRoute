// MeshRoute — lib/hal/device_hal.cpp  (H3)
#include "device_hal.h"

#include "airtime.h"   // lib/core — the SAME airtime_ms formula the Node uses (device ledger == Node math)

namespace meshroute {

TxResult DeviceHal::tx(const uint8_t* bytes, size_t len, const TxParams& p) {
    // Resolve the per-frame params (sentinel -1/-127 = the radio operating-point default).
    const int16_t sf  = p.sf           >= 0    ? p.sf           : _def_sf;
    const int32_t bw  = p.bw_hz        >= 0    ? p.bw_hz        : _def_bw;
    const int8_t  cr  = p.cr           >= 0    ? p.cr           : _def_cr;
    const int16_t pre = p.preamble_sym >= 0    ? p.preamble_sym : _def_preamble;
    const int8_t  pw  = p.power_dbm    >= -126 ? p.power_dbm    : _def_power;

    const TxResult r = _radio.transmit(bytes, len, sf, bw, cr, pw, pre);

    // Record airtime into OUR duty-cycle ledger only on a real on-air TX — the duty DECISION was already
    // the protocol's (it called airtime_used_ms() first); the Hal just logs what actually flew.
    if (r == TxResult::ok) {
        const uint32_t air = airtime_ms(static_cast<uint8_t>(sf), static_cast<uint32_t>(bw),
                                        static_cast<uint8_t>(cr), static_cast<uint16_t>(pre),
                                        static_cast<uint16_t>(len));
        _ledger.record(_clock.now_ms() + air, air);
    }
    return r;
}

void DeviceHal::set_rx_sf(int sf) {
    if (sf < 5) sf = 5; else if (sf > 12) sf = 12;   // clamp to the LoRa SF range (matches the sim's set_rx_sf)
    _radio.set_rx_sf(sf);
}

uint64_t DeviceHal::channel_busy_until() {
    // LBT: a CAD/RSSI hit reads as busy for a conservative hold so the Node's LBT defers past it. DRIFT:
    // real SX1262 CAD instead of the sim's airtime-derived busy estimate (the sensing is more accurate).
    return _radio.channel_busy() ? _clock.now_ms() + _busy_hold_ms : 0;
}

uint64_t DeviceHal::airtime_used_ms(uint64_t window_ms) { return _ledger.used_in_window(_clock.now_ms(), window_ms); }
uint64_t DeviceHal::oldest_tx_end_ms()                  { return _ledger.oldest_tx_end_ms(); }

int DeviceHal::rand_range(int lo, int hi) {
    if (hi <= lo) return lo;                          // [lo,hi); empty/degenerate range -> lo
    _rng ^= _rng << 13; _rng ^= _rng >> 17; _rng ^= _rng << 5;   // xorshift32
    return lo + static_cast<int>(_rng % static_cast<uint32_t>(hi - lo));
}

}  // namespace meshroute
