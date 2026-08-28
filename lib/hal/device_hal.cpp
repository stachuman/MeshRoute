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
    e.tag = p.tag;
    e.seq = p.seq;
    e.deadline_ms = p.deadline_ms;   // [[B159]]: 0 = no deadline; carried to the physical-start check in pump_tx

    e.len = static_cast<uint16_t>(len);
    for (size_t i = 0; i < len; ++i) e.buf[i] = bytes[i];
    _txq_count++;
    return TxResult::ok;
}

void DeviceHal::push_tx_outcome(const TxOutcome& outcome) {
    if (_tx_outcome_count >= kTxOutcomeCap) { ++_tx_outcome_drops; return; }
    const uint8_t slot = static_cast<uint8_t>((_tx_outcome_head + _tx_outcome_count) % kTxOutcomeCap);
    _tx_outcomes[slot] = outcome;
    ++_tx_outcome_count;
}

bool DeviceHal::pop_tx_outcome(TxOutcome& out) {
    if (_tx_outcome_count == 0) return false;
    out = _tx_outcomes[_tx_outcome_head];
    _tx_outcome_head = static_cast<uint8_t>((_tx_outcome_head + 1) % kTxOutcomeCap);
    --_tx_outcome_count;
    return true;
}

// Start the head queued frame iff the radio is idle (half-duplex: one in-flight TX). Debit the duty-cycle
// ledger on the REAL on-air send — the Hal logs what actually flew (the duty DECISION was the protocol's,
// it called airtime_used_ms() first). A failed arm is reported + counted, then the existing pop still drops it;
// there is deliberately no HAL retry here (the MAC's existing recovery remains authoritative).
void DeviceHal::pump_tx() {
    if (_radio.tx_busy()) return;                                         // a TX is still on air
    if (_txq_count == 0) return;
    TxQEntry& e = _txq[_txq_head];
    // ★★★ [[B159]] — **THE PHYSICAL-START DEADLINE, AND THIS IS THE TERMINAL AUTHORITY.** `tx()` above only
    // ENQUEUES; the frame can then wait behind up to kTxQCap-1 others, and ONE queued max-length frame at the
    // slowest legal PHY (SF12/BW7800/CR8/255 B) is ~229 s of airtime. So a Node-side guard before `Hal::tx()`
    // bounds ADMISSION only — an RTS admitted at age 149 s could still physically start well past the bound.
    // ⛔ 0 = the "no deadline" sentinel: every frame but a gateway-bound RTS carries it and is untouched here (C2).
    // ⛔ The refusal does NOT call `start_transmit` — nothing airs — and reports through the EXISTING outcome ring
    //    with the `expired` kind, echoing the seq/tag the SENDING SITE stamped so the Node can correlate it to
    //    exactly one flight. HAL-side code must not reach into Node state (C3), which is why this is a status on
    //    the established completion path rather than a new notification channel.
    if (e.deadline_ms != 0 && _clock.now_ms() >= e.deadline_ms) {
        const TxOutcome expired{ TxOutcomeKind::expired, BusyReason::none, TxResult::ok,
                                 e.tag, e.seq, e.sf, 0 };
        _txq_head = static_cast<uint8_t>((_txq_head + 1) % kTxQCap);   // drop it: the frame never flies
        _txq_count--;
        push_tx_outcome(expired);
        return;
    }
    const TxResult r = _radio.start_transmit(e.buf, e.len, e.sf, e.bw, e.cr, e.pw, e.pre);
    if (r == TxResult::ok) {
        _inflight = InflightTx{ e.seq, e.tag, e.sf };
        _inflight_valid = true;
        const uint32_t air = airtime_ms(static_cast<uint8_t>(e.sf), static_cast<uint32_t>(e.bw),
                                        static_cast<uint8_t>(e.cr), static_cast<uint16_t>(e.pre), e.len);
        _ledger.record(_clock.now_ms() + air, air);
        _tx_deadline_ms = _clock.now_ms() + air + air / 2 + 100;          // watchdog: 1.5x airtime + 100 ms slop
    } else {
        ++_tx_failed_arms;
        push_tx_outcome(TxOutcome{ TxOutcomeKind::failed, BusyReason::none, r,
                                   e.tag, e.seq, e.sf, /*busy_until_ms=*/0 });
    }
    _txq_head = static_cast<uint8_t>((_txq_head + 1) % kTxQCap);          // pop (ok, or a dropped failed-arm)
    _txq_count--;
}

// ★★★ §T3 §2.1 — COLLECTION IS ITS OWN CALL, AND THE SPLIT IS THE WHOLE POINT.
// This used to be the first half of a `service_tx()` that also called `pump_tx()`, and fw_main called that ONE
// function AFTER its timer drain. That order LOSES the completion fact for a channel post: `kMBcastClearTimerId`
// is armed at `data_air + 5` (node_mac.cpp, do_data_tx's m_broadcast arm) and its handler does
// `_pending_tx.reset(); become_free();` — so on a loop pass delayed past both deadlines the timer deleted the
// flight BEFORE the TxDone edge was ever collected, and `Node::on_tx_complete` had no `_pending_tx` left to
// attribute the airing to. The device loop now COLLECTS here, BEFORE the timers, and PUMPS after them.
// ⛔ Do NOT re-merge the two halves into one call, and ⛔ do NOT "fix" the race by widening the 5 ms margin —
//    that trades one timing race for a wider one and leaves the ordering wrong.
void DeviceHal::collect_tx_completion() {
    // Drain a normal completion (radio re-arms RX). If none AND a TX is still on air past its deadline,
    // the TxDone was lost -> force-recover, else the node is stuck deaf+mute (MeshCore outbound_expiry).
    if (_radio.poll_tx_done()) {
        if (_inflight_valid)
            push_tx_outcome(TxOutcome{ TxOutcomeKind::aired, BusyReason::none, TxResult::ok,
                                       _inflight.tag, _inflight.seq, _inflight.sf, /*busy_until_ms=*/0 });
        _inflight_valid = false;
        _tx_deadline_ms = 0;
    } else if (_radio.tx_busy() && _clock.now_ms() > _tx_deadline_ms) {
        _radio.abort_tx();
        _tx_timeouts++;
        if (_inflight_valid)
            push_tx_outcome(TxOutcome{ TxOutcomeKind::unknown, BusyReason::none, TxResult::ok,
                                       _inflight.tag, _inflight.seq, _inflight.sf, /*busy_until_ms=*/0 });
        _inflight_valid = false;
        _tx_deadline_ms = 0;
    }
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
