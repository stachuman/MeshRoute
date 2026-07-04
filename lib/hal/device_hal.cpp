// MeshRoute — lib/hal/device_hal.cpp  (H3)
#include "device_hal.h"

#include "../core/airtime.h"   // lib/core (relative — see device_hal.h); SAME airtime_ms formula the
                               // Node uses, so the device duty ledger == Node math == Lua.
#include "../../src/device_rng.h"   // mrrng::fill — HW RNG / SD-RNG for rand_bytes (host build: zeros, see header)

namespace meshroute {

// Async TX (Step 2): tx() ENQUEUES (resolving the per-frame params); the on-air send + ledger debit happen
// in pump_tx() once the radio is idle. Returns ok when queued (mirrors the sim's tx == enqueue + ok),
// too_long past the SX1262 length register, or busy if the bounded ring is full (dropped + counted).
TxResult DeviceHal::tx(const uint8_t* bytes, size_t len, const TxParams& p) {
    if (len > 255) return TxResult::too_long;                              // SX1262 length register (matches the sim)
    if (_txq_count >= kTxQCap) { _txq_drops++; return TxResult::busy; }    // ring full -> drop (MAC timeouts recover)

    const uint8_t slot = static_cast<uint8_t>((_txq_head + _txq_count) % kTxQCap);
    TxQEntry& e = _txq[slot];
    // Resolve the per-frame params (sentinel -1/-127 = the radio operating-point default) at enqueue.
    e.sf  = p.sf           >= 0    ? p.sf           : _def_sf;
    e.bw  = p.bw_hz        >= 0    ? p.bw_hz        : _def_bw;
    e.cr  = p.cr           >= 0    ? p.cr           : _def_cr;
    e.pre = p.preamble_sym >= 0    ? p.preamble_sym : _def_preamble;
    e.pw  = p.power_dbm    >= -126 ? p.power_dbm    : _def_power;
    e.len = static_cast<uint16_t>(len);
    for (size_t i = 0; i < len; ++i) e.buf[i] = bytes[i];
    _txq_count++;
    return TxResult::ok;
}

// Start the head queued frame iff the radio is idle (half-duplex: one in-flight TX). Debit the duty-cycle
// ledger on the REAL on-air send — the Hal logs what actually flew (the duty DECISION was the protocol's,
// it called airtime_used_ms() first). A failed arm drops the frame (rare radio_error; not retried here).
void DeviceHal::pump_tx() {
    if (_radio.tx_busy()) return;                                         // a TX is still on air
    if (_txq_count == 0) return;
    TxQEntry& e = _txq[_txq_head];
    const TxResult r = _radio.start_transmit(e.buf, e.len, e.sf, e.bw, e.cr, e.pw, e.pre);
    if (r == TxResult::ok) {
        const uint32_t air = airtime_ms(static_cast<uint8_t>(e.sf), static_cast<uint32_t>(e.bw),
                                        static_cast<uint8_t>(e.cr), static_cast<uint16_t>(e.pre), e.len);
        _ledger.record(_clock.now_ms() + air, air);
        _tx_deadline_ms = _clock.now_ms() + air + air / 2 + 100;          // watchdog: 1.5x airtime + 100 ms slop
    }
    _txq_head = static_cast<uint8_t>((_txq_head + 1) % kTxQCap);          // pop (ok, or a dropped failed-arm)
    _txq_count--;
}

void DeviceHal::service_tx() {
    // Drain a normal completion (radio re-arms RX). If none AND a TX is still on air past its deadline,
    // the TxDone was lost -> force-recover, else the node is stuck deaf+mute (MeshCore outbound_expiry).
    if (!_radio.poll_tx_done() && _radio.tx_busy() && _clock.now_ms() > _tx_deadline_ms) {
        _radio.abort_tx();
        _tx_timeouts++;
    }
    pump_tx();                                                           // start the next queued frame if the radio is now idle
}

void DeviceHal::set_rx_sf(int sf) {
    if (sf < 5) sf = 5; else if (sf > 12) sf = 12;   // clamp to the LoRa SF range (matches the sim's set_rx_sf)
    _radio.set_rx_sf(sf);
}

void DeviceHal::set_rx_freq(double mhz) {
    if (mhz > 0.0) _radio.set_rx_freq(mhz);          // 0/neg = inherit (core already skips; guard the HAL too)
}

void DeviceHal::set_rx_bw(uint32_t bw_hz) {
    if (bw_hz == 0) return;                          // 0 = inherit (core already skips; guard the HAL too)
    _def_bw = static_cast<int32_t>(bw_hz);           // ★ TX flies on _def_bw: tx() resolves the -1 TxParams bw sentinel
    _radio.set_rx_bw(bw_hz);                          //   to _def_bw, so the airtime debit (active_bw_hz) == the on-air BW
}

void DeviceHal::set_rx_cr(uint8_t cr) {
    if (cr == 0) return;                             // 0 = inherit
    _def_cr = static_cast<int8_t>(cr);               // TX flies on _def_cr (same -1-sentinel resolution as bw)
    _radio.set_rx_cr(cr);
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
// Crypto entropy (the XChaCha nonce-seed) — the HW RNG, NOT the xorshift32 above. mrrng::fill draws NRF_RNG /
// SD-RNG / esp_random on device; the host/native build fills zeros (degenerate-on-purpose, see device_rng.h).
void DeviceHal::rand_bytes(uint8_t* out, size_t n) { mrrng::fill(out, n); }

}  // namespace meshroute
