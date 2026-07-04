// MeshRoute — lib/hal/device_radio.h  (H2 / H3 device radio)
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// The DEVICE-ONLY IRadio: drives the vendored CustomSX1262 (RadioLib SX1262 subclass). Guarded by
// ARDUINO so the native build (which has no RadioLib) skips it — the native tests use a MockRadio.
//
// RX is INTERRUPT-DRIVEN (Step 1 of docs/specs/2026-06-07-metal-rx-async-tx-sleep): a DIO1 ISR
// (setPacketReceivedAction) sets a volatile flag on RxDone; poll_rx() consumes the flag, reads the
// frame, and re-arms RX — the proven MeshCore recipe (MeshCore RadioLibWrappers.cpp). This replaces the
// earlier polled getIrqFlags() RX path, which is kept as a compile fallback (-DMR_RX_POLL): it had been
// a bring-up workaround after the first IRQ attempt "never surfaced a packet". isr_count() (console
// `status isr=`) proves the IRQ LINE fires, independent of delivery — the Step-1 diagnostic:
//   isr=0          -> the ISR never fires      -> DIO1 pin/mask (check LORA_PIN_DIO1 vs the board variant)
//   isr>0 & rx=0   -> ISR fires, no delivery   -> drain/re-arm bug
//
// TX is ASYNC (Step 2): start_transmit() arms a non-blocking startTransmit() and returns; poll_tx_done()
// drains the TxDone edge (the SAME shared DIO1 flag — _tx_in_flight routes it: poll_rx ignores it mid-TX)
// and re-arms RX. DeviceHal owns the outbound queue + half-duplex serialization (one in-flight TX). The
// loop stays live during a long (SF12) TX — timers/RX no longer freeze for the airtime.
//
// The preamble witness stays POLLED (a cheap getIrqFlags() read, not a DIO1 event) -> take_preamble().
//
// REALITY SPLIT: compiles under the board envs here; on-metal RX is BENCH-VERIFIED BY THE USER. The
// MeshRoute logic above it (DeviceHal) is native-proven against MockRadio.
#pragma once
#include "iradio.h"

#if defined(ARDUINO)
#include <RadioLib.h>
#include "helpers/radiolib/CustomSX1262.h"   // vendored — getIrqFlags() + SX126X_IRQ_PREAMBLE_DETECTED
#include "../core/frame_trace.h"             // mr_trace_frame() — shared decoded RX/TX console trace
#include "radio_canary.h"                    // mrcanary::diff/dword_at — the Module-corruption canary (MR_RADIO_CANARY)
#include <string.h>                          // memcpy (the canary snapshot)

namespace meshroute {

// ---- DIO1 interrupt-service glue ------------------------------------------------------------------
// RadioLib's setPacketReceivedAction() wants a plain void(void) fn ptr, so the ISR is a free function
// toggling a file-scope volatile. There is ONE radio instance per device (g_iradio), so a single shared
// flag is unambiguous. On ESP32 the ISR must live in IRAM. The same DIO1 line carries RxDone (while in
// RX mode) AND TxDone (after a transmit); in Step 1 only RxDone delivery matters — the TxDone edge is
// discarded in transmit().
#if defined(ESP32)
  #define MR_ISR_ATTR IRAM_ATTR
#else
  #define MR_ISR_ATTR
#endif
static volatile bool     g_dio1_fired = false;   // set by the ISR on a DIO1 edge; consumed by poll_rx
static volatile uint32_t g_isr_count  = 0;       // diagnostic: # of DIO1 edges seen (console `status isr=`)
static volatile uint32_t g_rxbad_count = 0;      // diagnostic: # of RX frames that failed to decode (the CRC storm). COUNTED unconditionally even though the per-event print is now `debug on`-gated -> `status rxbad=` is a clean counter delta, not a flood.

#if defined(MR_RADIO_CANARY) && MR_RADIO_CANARY
// Radio-Module corruption canary (debug, spec 2026-06-25). Snapshot the Module's + its HAL's first kCanaryN bytes at
// begin(); re-check after each loop() subsystem -> the first trip names the corruptor. File-scope so the const check
// is cheap; the live Module/HAL pointers are captured at arm (they don't move after init).
static constexpr size_t kCanaryN = 64;
static uint8_t        g_canary_mod[kCanaryN];
static uint8_t        g_canary_hal[kCanaryN];
static const uint8_t* g_canary_mod_p = nullptr;   // the live Module bytes (== getMod())
static const uint8_t* g_canary_hal_p = nullptr;   // the live HAL bytes (== mod->hal — SPItransferStream derefs the vtable here)
static bool           g_canary_armed = false;
#endif
static void MR_ISR_ATTR mr_on_dio1() { g_dio1_fired = true; g_isr_count = g_isr_count + 1; }  // ++ on volatile is C++20-deprecated
#undef MR_ISR_ATTR

class Sx1262Radio : public IRadio {
public:
    explicit Sx1262Radio(CustomSX1262& radio) : _radio(radio) {}

    // Register the DIO1 ISR, then arm continuous RX. Call once in setup() (post-std_init).
    bool begin() {
        _radio.setPacketReceivedAction(mr_on_dio1);              // DIO1 -> mr_on_dio1 (RxDone while in RX)
        g_dio1_fired = false;
        const bool ok = _radio.startReceive() == RADIOLIB_ERR_NONE;   // arms the RxDone IRQ on DIO1
        radio_canary_arm();                                     // snapshot the Module/HAL critical bytes (no-op unless MR_RADIO_CANARY)
        return ok;
    }

    // ---- Radio-Module corruption canary (MR_RADIO_CANARY; no-ops otherwise) ----------------------------------
    void radio_canary_arm() {
#if defined(MR_RADIO_CANARY) && MR_RADIO_CANARY
        Module* mod = _radio.getMod();                          // RADIOLIB_GODMODE exposes getMod()
        if (!mod) return;
        g_canary_mod_p = reinterpret_cast<const uint8_t*>(mod);
        memcpy(g_canary_mod, g_canary_mod_p, kCanaryN);
        g_canary_hal_p = reinterpret_cast<const uint8_t*>(mod->hal);   // the SPI HAL (its vtable ptr is what dies)
        if (g_canary_hal_p) memcpy(g_canary_hal, g_canary_hal_p, kCanaryN);
        g_canary_armed = true;
        // ADDENDUM 2: arm a DWT data-write watchpoint on the HAL's vtable-ptr WORD (g_canary_hal_p[0..3]) — the byte
        // that dies. On the corrupting store, DebugMon_Handler (device_fault.h) captures the pc -> `faults` shows
        // WATCHPOINT pc=… = addr2line -> the exact offending line (the inbox LittleFS heap path, per ADDENDUM 3).
        #if defined(NRF52_SERIES) || defined(ARDUINO_ARCH_NRF52) || defined(BOARD_XIAO_WIO_SX1262)
        if (g_canary_hal_p) {
            CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk | CoreDebug_DEMCR_MON_EN_Msk;   // enable DWT + the self-hosted DebugMonitor
            DWT->COMP0     = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_canary_hal_p));   // the watched address
            DWT->MASK0     = 2;                                            // 4-byte (2^2) range
            DWT->FUNCTION0 = 0x6u;                                         // 0b0110 = data-write watchpoint -> a debug event
        }
        #endif
#endif
    }
    // -1 = intact. Else the differing byte offset: 0..kCanaryN-1 = the Module region, kCanaryN+.. = the HAL region.
    // PURE read — no SPI, safe to call every loop. (No-op -> always -1 unless the canary is compiled in + armed.)
    int radio_canary_check() const {
#if defined(MR_RADIO_CANARY) && MR_RADIO_CANARY
        if (!g_canary_armed) return -1;
        int d = mrcanary::diff(g_canary_mod, g_canary_mod_p, kCanaryN);
        if (d >= 0) return d;
        if (g_canary_hal_p) { d = mrcanary::diff(g_canary_hal, g_canary_hal_p, kCanaryN);
                              if (d >= 0) return static_cast<int>(kCanaryN) + d; }
#endif
        return -1;
    }
    // The before (snapshot) / after (live) dword at a tripped offset — for the durable canary record.
    uint32_t radio_canary_before(int off) const {
#if defined(MR_RADIO_CANARY) && MR_RADIO_CANARY
        if (off >= static_cast<int>(kCanaryN)) return mrcanary::dword_at(g_canary_hal, off - kCanaryN, kCanaryN);
        if (off >= 0)                          return mrcanary::dword_at(g_canary_mod, off,             kCanaryN);
#endif
        (void)off; return 0;
    }
    uint32_t radio_canary_after(int off) const {
#if defined(MR_RADIO_CANARY) && MR_RADIO_CANARY
        if (off >= static_cast<int>(kCanaryN)) return mrcanary::dword_at(g_canary_hal_p, off - kCanaryN, kCanaryN);
        if (off >= 0)                          return mrcanary::dword_at(g_canary_mod_p, off,             kCanaryN);
#endif
        (void)off; return 0;
    }

    // Async TX (Step 2): apply the per-frame params + arm a NON-BLOCKING startTransmit(); return at once.
    // The DIO1 TxDone edge is drained by poll_tx_done() (which re-arms RX). DeviceHal must not call this
    // while tx_busy(). The same DIO1 flag carries RxDone/TxDone — _tx_in_flight routes the edge (poll_rx
    // ignores it while a TX is on air; poll_tx_done consumes it).
    TxResult start_transmit(const uint8_t* b, size_t n,
                            int16_t sf, int32_t bw_hz, int8_t cr, int8_t pw, int16_t pre) override {
        ++_tx_count;
        if (sf > 0) _cur_sf = sf;                                                  // TEMP DEBUG
        mr_trace_frame(/*is_rx=*/false, b, n, _cur_sf, 0.0f, 0.0f, millis());   // decoded arm-time trace; [txdone] below marks completion -> Δ = airtime
        _radio.standby();   // M11: SX1262 latches SetModulationParams (SF/BW/CR) ONLY in STANDBY — issued mid-RX they're DROPPED and the frame flies on the stale (routing) SF (mirror set_rx_sf's fix). H6: it also stops continuous-RX BEFORE the clear+arm below, so no RxDone edge can land in the RX->TX gap and be mis-consumed as TxDone by poll_tx_done.
        if (sf  > 0)    _radio.setSpreadingFactor(static_cast<uint8_t>(sf));
        if (bw_hz > 0)  _radio.setBandwidth(static_cast<float>(bw_hz) / 1000.0f);   // RadioLib wants kHz
        if (cr  > 0)    _radio.setCodingRate(static_cast<uint8_t>(cr));
        if (pw  > -100) _radio.setOutputPower(static_cast<int8_t>(pw));
        if (pre > 0)    _radio.setPreambleLength(static_cast<uint16_t>(pre));
        g_dio1_fired = false;                                                      // H6: clear any edge latched up to here (now in STANDBY, RX stopped) — the real TxDone re-sets it a min-airtime later; no live RX means poll_tx_done can't see a premature RxDone-as-TxDone
        const int16_t st = _radio.startTransmit(const_cast<uint8_t*>(b), n);       // NON-BLOCKING; DIO1 -> TxDone
        if (st != RADIOLIB_ERR_NONE) {
            if (g_mr_trace_on) { Serial.print(F("[txerr st=")); Serial.print(st); Serial.print(F(" t=")); Serial.print(millis()); Serial.println(F("]")); }   // arm failed — recover to RX. GATED (the radio hot path stays silent by default — the MeshCore lesson; `debug on` restores it)
            arm_rx();
            return TxResult::radio_error;
        }
        _tx_in_flight = true;
        return TxResult::ok;
    }

    // Drain the in-flight TX: on the TxDone edge, finish + restore the LISTENING SF (routing) and re-arm RX.
    // Restoring _rx_sf matters for the dual-SF DATA: the DATA flies on the data SF but the ACK returns on
    // routing — without this the sender stays parked on the data SF and misses the ACK.
    bool poll_tx_done() override {
        if (!_tx_in_flight) return false;
        if (!g_dio1_fired) return false;                                          // still on air
        g_dio1_fired = false;                                                     // consume the TxDone edge
        _tx_in_flight = false;
        _radio.finishTransmit();                                                  // RadioLib async-TX cleanup
        //Serial.print(F("[txdone t=")); Serial.print(millis()); Serial.println(F("]"));   // arm -> here = the airtime (loop stayed live in between)
        if (_rx_sf > 0 && _rx_sf != _cur_sf) { _cur_sf = _rx_sf; _radio.setSpreadingFactor(static_cast<uint8_t>(_rx_sf)); }
        arm_rx();                                                    // re-arm continuous RX on the listening SF
        _pre_seen = false;
        return true;
    }

    bool tx_busy() const override { return _tx_in_flight; }
    int  rx_sf()   const { return _cur_sf; }   // the radio SF currently armed (for the decoded RX trace)

    // Watchdog recovery: the in-flight TX overran its deadline (TxDone never came). Stop it, restore the
    // listening SF, re-arm RX — mirrors poll_tx_done's tail without waiting for the (lost) edge.
    void abort_tx() override {
        if (g_mr_trace_on) { Serial.print(F("[txabort t=")); Serial.print(millis()); Serial.println(F("]")); }   // TX stuck -> forced recover. GATED
        _radio.standby();                                                         // stop the (stuck) transmit
        _tx_in_flight = false;
        g_dio1_fired = false;
        if (_rx_sf > 0 && _rx_sf != _cur_sf) { _cur_sf = _rx_sf; _radio.setSpreadingFactor(static_cast<uint8_t>(_rx_sf)); }
        arm_rx();                                                    // re-arm continuous RX
        _pre_seen = false;
    }

    void set_rx_sf(int sf) override {
        _cur_sf = sf; _rx_sf = sf;                                                 // _rx_sf = the LISTENING SF; transmit() restores it post-TX (so we don't sit on the data SF after a DATA)
        // Serial.print(F("↻ rx-sf → ")); Serial.print(sf); Serial.print(F("  t=")); Serial.print(millis()); Serial.println(F("ms"));   // RX listening-SF hop (data-SF window = Δt between hops)
        _radio.standby();                                                          // SX1262: SetModulationParams (the SF) only latches in STANDBY — issued mid-RX it is dropped, so set_rx_sf was re-arming on the OLD SF (the data-leg bug)
        _radio.setSpreadingFactor(static_cast<uint8_t>(sf));
        g_dio1_fired = false;                                                      // drop any stale edge before re-arming on the new SF
        arm_rx();
        _pre_seen = false;
    }

    // Per-layer gateway: retune the RF carrier on a window switch. Like set_rx_sf, SetRfFrequency latches in STANDBY —
    // issued mid-RX it is dropped — so standby -> setFrequency -> re-arm RX. The latched freq carries into the next TX
    // (start_transmit never sets frequency), so DATA on this layer flies on this freq even as the SF varies in-flight.
    void set_rx_freq(double mhz) override {
        if (g_mr_trace_on) { Serial.print(F("↻ rx-freq → ")); Serial.print(mhz, 4); Serial.print(F("  t=")); Serial.print(millis()); Serial.println(F("ms")); }   // GATED
        _radio.standby();
        _radio.setFrequency(static_cast<float>(mhz));
        g_dio1_fired = false;                                                      // drop any stale edge before re-arming on the new freq
        arm_rx();
        _pre_seen = false;
    }

    // Software LBT (Step 3): NON-BLOCKING carrier sense — busy if we're transmitting, OR a frame is in
    // progress (preamble/header), OR the channel RSSI is above the rolling noise floor + threshold.
    // Replaces the blocking HW-CAD scanChannel() (which could spin on the CAD-done IRQ). Fed to the Node's
    // LBT pre-check (channel_busy_until) AND the DeviceHal pump CSMA guard.
    bool channel_busy() override {
        if (_tx_in_flight) return true;                                // we're on air -> busy (by us)
        if (_radio.isReceiving()) return true;                         // carrier sense: preamble/header in progress
        return _radio.getRSSI(false) > (_noise_floor + kLbtThresholdDb);   // energy above the noise floor
    }

    // Refine the noise floor from quiet RSSI samples (paced, idle-only) — the baseline channel_busy()
    // compares against. Called each loop by fw_main when LBT is on. Excludes in-progress receptions +
    // above-floor (packet) energy so the floor tracks ambient noise, not traffic. Mirrors MeshCore's sampler.
    void sample_noise() {
        const uint32_t now = millis();
        if (now - _last_floor_ms < kFloorSampleMs) return;             // pace the SPI reads
        _last_floor_ms = now;
        if (_tx_in_flight || _radio.isReceiving()) return;             // don't fold traffic into the floor
        const float rssi = _radio.getRSSI(false);
        if (rssi >= _noise_floor + kFloorWindowDb) {                   // above floor+window = activity, not noise
            // Stuck-floor escape (ports MeshCore RadioLibWrapper::resetAGC's re-seed): a floor sitting far BELOW the
            // true ambient rejects EVERY in-window idle sample, self-reinforcing the stuck value forever (MeshCore's
            // documented failure: stuck -120 -> threshold -106 too low to accept ~-105 samples). After a long
            // all-rejected streak, re-seed to 0 so the floor reconverges from scratch to the real ambient.
            if (++_floor_rejects >= kFloorStuckRejects) reset_noise_floor();
            return;
        }
        _floor_rejects = 0;                                            // a sample landed in-window -> the floor is tracking, not stuck
        _floor_sum += rssi;
        if (++_floor_n >= kFloorSamples) {
            float avg = _floor_sum / static_cast<float>(_floor_n);
            _noise_floor = avg < -120.0f ? -120.0f : avg;             // clamp to the SX1262 floor
            _floor_sum = 0.0f; _floor_n = 0;
        }
    }
    // Stuck-floor escape: drop the floor back to the 0 seed so it reconverges from scratch — the only way out of a
    // self-reinforcing stuck floor (MeshCore re-seeds the same way in resetAGC). Triggered by a long all-rejected
    // streak in sample_noise(); also safe to call from any future RX-recovery path.
    void reset_noise_floor() { _noise_floor = 0.0f; _floor_sum = 0.0f; _floor_n = 0; _floor_rejects = 0; }
    float noise_floor() const { return _noise_floor; }   // status diagnostic (dBm)

    // Polled each loop: (1) latches a preamble (rising-edge -> on_preamble_detected witness) via a cheap
    // getIrqFlags() read; (2) on the DIO1 RxDone IRQ flag, reads the frame + its SNR/RSSI then re-arms RX.
    // (-DMR_RX_POLL reverts the RxDone gate to the legacy polled IRQ-bit, for an A/B bring-up fallback.)
    bool poll_rx(uint8_t* buf, size_t cap, size_t& out_len, float& snr_db, float& rssi_dbm) override {
        if (_tx_in_flight) return false;                                // mid-TX: the DIO1 edge is TxDone (poll_tx_done's), not RxDone
        const uint16_t irq = _radio.getIrqFlags();
        const bool pre = (irq & SX126X_IRQ_PREAMBLE_DETECTED) != 0;
        if (pre && !_pre_seen) { _preamble = true; _pre_seen = true; }   // latch the preamble witness; print removed (noise)
        if (!pre) _pre_seen = false;                                     // window ended
#if defined(MR_RX_POLL)
        if ((irq & RADIOLIB_SX126X_IRQ_RX_DONE) == 0) return false;      // legacy polled fallback (A/B)
#else
        if (!g_dio1_fired) return false;                                 // ISR-driven (default): no RxDone yet
        g_dio1_fired = false;                                            // consume this edge
#endif
        size_t l = _radio.getPacketLength();   // RxDone: decoded line is the fw_main «rx (was [rxdone] raw dump)
        if (l > cap) l = cap;
        const int16_t st = _radio.readData(buf, l);                     // clears the RX IRQs
        // Read the packet's SNR/RSSI BEFORE re-arming — after startReceive() they reflect the live
        // channel (noise floor), not the just-received frame.
        snr_db   = _radio.getSNR();
        rssi_dbm = _radio.getRSSI();
        arm_rx();                                          // re-arm RX (MeshCore discipline: startReceive after every read)
        _pre_seen = false;
        if (st != RADIOLIB_ERR_NONE) { ++g_rxbad_count; if (g_mr_trace_on) { Serial.print(F("[rxbad st=")); Serial.print(st); Serial.print(F(" t=")); Serial.print(millis()); Serial.println(F("]")); } return false; }   // count the CRC-storm event ALWAYS; the per-event print is GATED behind `debug on` (don't flood Serial when the radio is busiest)
        out_len  = l;
        return true;
    }

    // The loop drains this each iteration -> Node::on_preamble_detected(now).
    bool take_preamble() { const bool f = _preamble; _preamble = false; return f; }

    uint32_t tx_count() const { return _tx_count; }    // status diagnostic (frames transmitted)
    uint32_t isr_count() const { return g_isr_count; } // status diagnostic (DIO1 edges) — proves the IRQ line fires
    uint32_t rxbad_count() const { return g_rxbad_count; } // status diagnostic: failed-decode RX (CRC storm) — a clean counter delta, read by `status rxbad=` / `rcmd status`
    uint32_t rx_arm_failures() const { return _rx_arm_failures; } // L5 status diagnostic: startReceive() re-arm failures (SPI glitch) — a non-zero delta means the node went transiently deaf; visible instead of silent

private:
    // Software-LBT tunables (bench-tunable). Threshold too low -> false-busy/starvation; too high -> missed
    // activity. is_receiving() is the primary sense; the RSSI floor is the energy backstop.
    static constexpr float    kLbtThresholdDb = 10.0f;   // busy if RSSI > noise_floor + this
    static constexpr float    kFloorWindowDb  = 14.0f;   // a sample within floor+this counts toward the floor (exclude packets)
    static constexpr uint16_t kFloorSamples   = 32;      // samples per floor recompute
    static constexpr uint32_t kFloorSampleMs  = 10;      // min ms between RSSI samples (pace SPI)
    static constexpr uint16_t kFloorStuckRejects = 64;   // consecutive above-window idle samples -> re-seed the floor (stuck-floor escape; MeshCore resetAGC). ~0.64s @ kFloorSampleMs; bench-tunable

    CustomSX1262& _radio;
    // L5: every RX re-arm routes through here so a failed startReceive() (an SPI glitch) is COUNTED + surfaced in
    // status, instead of silently leaving the node deaf forever. (An auto-retry loop is a noted follow-up; making
    // the failure visible is the load-bearing half — currently a failed re-arm has no signal at all.)
    void arm_rx() { if (_radio.startReceive() != RADIOLIB_ERR_NONE) ++_rx_arm_failures; }
    uint32_t _tx_count = 0;   // frames transmitted (status diagnostic)
    uint32_t _rx_arm_failures = 0;   // L5: startReceive() re-arm failures (SPI glitch) — surfaced via rx_arm_failures()
    bool _preamble = false;   // latched preamble event (drained by take_preamble)
    bool _pre_seen = false;   // edge-detect: preamble already latched this RX cycle
    bool _tx_in_flight = false; // async TX armed (start_transmit) until poll_tx_done drains the TxDone edge; routes the shared DIO1 flag
    float    _noise_floor   = 0.0f;     // rolling ambient RSSI floor (dBm); 0 seed (MeshCore) converges DOWN to ambient — never sticks high. channel_busy() is permissive until the first batch lands (RSSI > 0+threshold is never true).
    float    _floor_sum     = 0.0f;     // accumulator for the current floor recompute
    uint16_t _floor_n       = 0;        // samples accumulated
    uint16_t _floor_rejects = 0;        // consecutive above-window idle samples (stuck-floor detector -> reset_noise_floor)
    uint32_t _last_floor_ms = 0;        // last RSSI sample time (pacing)
    int  _cur_sf   = 0;       // TEMP DEBUG: radio's current SF (set on tx + set_rx_sf), shown in rx/tx traces
    int  _rx_sf    = 0;       // the LISTENING SF (set by set_rx_sf); transmit() restores it post-TX so the sender doesn't sit on the data SF after a DATA and miss the ACK
};

}  // namespace meshroute
#endif  // ARDUINO
