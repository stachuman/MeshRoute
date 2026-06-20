// MeshRoute — lib/core/hal.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Host contract for meshroute::Node. Implemented by FirmwareNode (sim backend,
// in lora-universal-simulator) and the MeshCore-PHY device backend (XIAO
// nRF52840 + SX1262; see ~/MeshRoute/docs/specs/2026-05-29-h0-*.md).
//
// PUBLIC SURFACE MUST stay free of RadioLib / Arduino / sol / nlohmann::json /
// std::span / std::function so BOTH backends include it cleanly, and so the
// header is C++17-includable by the (C++17) simulator while meshroute_core
// itself builds as C++20 (S2 design decision D-S2a = B). -fno-exceptions /
// -fno-rtti / no heap in hot paths.
#pragma once
#ifndef MESHROUTE_NS
#define MESHROUTE_NS meshroute   // Slice 5 faithful two-lib: gateway variant compiles with -DMESHROUTE_NS=meshroute_gw
#endif
#include <cstddef>
#include <cstdint>

namespace MESHROUTE_NS {

enum class TxResult  : uint8_t { ok, busy, too_long, radio_error };
enum class BusyReason : uint8_t { channel_busy, self_tx_in_flight, oversized, duty_cycle_exceeded };

struct TxParams {                 // sentinel = use the radio default (RF plan SF8/BW125/CR4/5)
    int16_t sf           = -1;
    int32_t bw_hz        = -1;    // Hz, not kHz (matches PendingTx::bw_hz)
    int8_t  cr           = -1;
    int8_t  power_dbm    = -127;
    int16_t preamble_sym = -1;
    uint16_t    tag   = 0;        // opaque token echoed back in on_radio_busy (heap-free retry match)
    const char* label = nullptr;  // static-literal telemetry (e.g. "RTS"); device may ignore
    const char* info  = nullptr;  // static-literal telemetry; device may ignore
};

// src_hint: the PHY sender's node_id, or -1 = "no hint". int16_t (NOT int8_t) so node ids 128..254 don't
// alias into the negative-sentinel range (the R4.4 `src_hint >= 0` guard + the uint8_t casts depend on it).
struct RxMeta { float snr_db; float rssi_dbm; uint64_t recv_ms; int16_t src_hint = -1; };

struct BusyInfo { BusyReason reason; uint16_t tag; int16_t sf; uint64_t busy_until_ms; };

// One telemetry key/value; bounded, heap-free — the Node never formats JSON,
// the backend serializes (sim → NDJSON; device → USB-CDC or compiled out).
struct EventField {
    enum class T : uint8_t { i64, f64, str, boolean };
    const char* key;
    T           type;
    int64_t     i = 0;            // active member per `type`
    double      f = 0;
    const char* s = nullptr;
    bool        b = false;
};

// MR_TELEMETRY(...) — wrap a diagnostic emit block (EventField array + _hal.emit) so the DEVICE build
// strips it ENTIRELY. With MESHROUTE_NO_TELEMETRY (set in the xiao/heltec envs, NOT native/sim) the
// preprocessor removes the body — no event-name/key string literals in flash, no array construction,
// no virtual call. ONLY wrap pure telemetry: never put load-bearing logic (returns, state writes,
// tx_initiating) inside MR_TELEMETRY, or it vanishes on metal. The variadic form swallows the commas
// in the EventField designated-initializers.
#ifdef MESHROUTE_NO_TELEMETRY
  #define MR_TELEMETRY(...) do {} while (0)
#else
  #define MR_TELEMETRY(...) do { __VA_ARGS__ } while (0)
#endif

// MR_EMIT(event, EF_*(key, value)...) — terse form of the EventField[] + _hal.emit block. Behaviour-identical
// (expands through MR_TELEMETRY, so device-stripped the same way); the field count is derived via sizeof so it
// can never drift from the array. e.g.  MR_EMIT("join_adopted", EF_I("node", id), EF_B("healed", ok));
#define MR_EMIT_TO(HAL, EV, ...) MR_TELEMETRY( const EventField _ef[] = { __VA_ARGS__ };            \
                                               (HAL).emit((EV), _ef, sizeof(_ef) / sizeof(_ef[0])); )
#define MR_EMIT(EV, ...) MR_EMIT_TO(_hal, EV, __VA_ARGS__)   // Node member methods (the Node's _hal); free
                                                             // functions that take a `Hal&` use MR_EMIT_TO(hal, ...)
#define EF_I(K, V) EventField{ .key = (K), .type = EventField::T::i64,     .i = static_cast<int64_t>(V) }
#define EF_F(K, V) EventField{ .key = (K), .type = EventField::T::f64,     .f = static_cast<double>(V) }
#define EF_S(K, V) EventField{ .key = (K), .type = EventField::T::str,     .s = (V) }
#define EF_B(K, V) EventField{ .key = (K), .type = EventField::T::boolean, .b = (V) }

class Hal {
public:
    virtual ~Hal() = default;

    // ---- radio — `bytes` borrowed for the call ONLY (impl copies); NOT re-entrant.
    [[nodiscard]] virtual TxResult tx(const uint8_t* bytes, size_t len, const TxParams& p) = 0;
    virtual void     set_rx_sf(int sf) = 0;            // clamp+ignore out-of-range; arms blind window
    // Per-layer frequency (dual-layer gateway): retune the RX frequency on a window switch. NON-pure with a no-op
    // default ON PURPOSE — a layer is a (freq, SF, leaf) channel, but only Hals that model RF carriers act on it.
    // The device Hal overrides it (standby+setFrequency+re-arm); the SIM Hal does NOT model RF frequency (it keys
    // reachability on leaf/SF), so it correctly inherits the no-op. mhz <= 0 => caller skips the call (inherit boot freq).
    virtual void     set_rx_freq(double /*mhz*/) {}
    virtual uint64_t channel_busy_until() = 0;         // LBT busy_until ms, or 0
    virtual uint64_t airtime_used_ms(uint64_t window_ms) = 0;
    virtual uint64_t oldest_tx_end_ms() = 0;           // duty-cycle headroom calc
    // Extra data-SF RX-window slack the airtime model can't predict: real-radio reconfig/mode-switch +
    // RX_DONE demod lag (bench-measured on the SX1262). 0 on the idealized sim (no s18 regression); the
    // device HAL returns the measured slop so the window covers the slow DATA's real RX_DONE. Non-pure:
    // sim Hals (FirmwareNode) inherit 0 with no change needed.
    virtual uint32_t rx_window_slop_ms(int sf) const { (void)sf; return 0; }

    // ---- time / timers — one-shot, caller-allocated ids, (re)arm-by-id, cap 80 (Slice 3b: 64->80 for the gw scheduler)
    virtual uint64_t now() = 0;
    [[nodiscard]] virtual bool after(uint32_t delay_ms, uint32_t timer_id) = 0;  // false if full
    virtual void     cancel(uint32_t timer_id) = 0;    // idempotent

    // ---- identity (runtime short-id; key_hash32 is the stable long id, ctor-fixed)
    virtual void     set_protocol_id(int id) = 0;      // clamp [0,255]; join/lease

    // ---- rng
    virtual int      rand_range(int lo, int hi) = 0;   // [lo,hi); shared mt19937 + draw order in sim
    // Crypto-strength entropy — DISTINCT from rand_range (a software mt19937 on device: fine for jitter, but
    // predictable + reboot-repeatable -> UNSAFE for an XChaCha nonce under a static ECDH key). Device draws the
    // HW RNG (mrrng::fill / SD-RNG); sim draws its DETERMINISTIC per-node RNG so E2E scenarios reproduce. ONLY
    // the CRYPTED seal path calls this -> never drawn with E2E off (the s18 byte-identity keystone is untouched).
    virtual void     rand_bytes(uint8_t* out, size_t n) = 0;

    // ---- telemetry — the BACKEND serializes; Node stays format-agnostic.
    virtual void     emit(const char* type, const EventField* fields, size_t n_fields) = 0;
    virtual void     log(const char* msg) = 0;

    // ---- exception-free fatal hook (sim assert / device reboot)
    virtual void     panic(const char* why) { (void)why; }
};

}  // namespace meshroute
