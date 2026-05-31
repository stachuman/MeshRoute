// MeshRoute — lib/core/hal.h
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
#include <cstddef>
#include <cstdint>

namespace meshroute {

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

class Hal {
public:
    virtual ~Hal() = default;

    // ---- radio — `bytes` borrowed for the call ONLY (impl copies); NOT re-entrant.
    [[nodiscard]] virtual TxResult tx(const uint8_t* bytes, size_t len, const TxParams& p) = 0;
    virtual void     set_rx_sf(int sf) = 0;            // clamp+ignore out-of-range; arms blind window
    virtual uint64_t channel_busy_until() = 0;         // LBT busy_until ms, or 0
    virtual uint64_t airtime_used_ms(uint64_t window_ms) = 0;
    virtual uint64_t oldest_tx_end_ms() = 0;           // duty-cycle headroom calc

    // ---- time / timers — one-shot, caller-allocated ids, (re)arm-by-id, cap 64
    virtual uint64_t now() = 0;
    [[nodiscard]] virtual bool after(uint32_t delay_ms, uint32_t timer_id) = 0;  // false if full
    virtual void     cancel(uint32_t timer_id) = 0;    // idempotent

    // ---- identity (runtime short-id; key_hash32 is the stable long id, ctor-fixed)
    virtual void     set_protocol_id(int id) = 0;      // clamp [0,255]; join/lease

    // ---- rng
    virtual int      rand_range(int lo, int hi) = 0;   // [lo,hi); shared mt19937 + draw order in sim

    // ---- telemetry — the BACKEND serializes; Node stays format-agnostic.
    virtual void     emit(const char* type, const EventField* fields, size_t n_fields) = 0;
    virtual void     log(const char* msg) = 0;

    // ---- exception-free fatal hook (sim assert / device reboot)
    virtual void     panic(const char* why) { (void)why; }
};

}  // namespace meshroute
