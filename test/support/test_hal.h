// MeshRoute — test/support/test_hal.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Shared in-memory `Hal` fixture for the native (doctest) suite. NATIVE-ONLY: nothing under test/ is
// compiled into any board env, so this header carries zero device risk.
//
// Eight test TUs used to hand-roll a full `Hal` implementation each (test_node_r2 / _r3 / _query / _join /
// _channel / _hashlocate / _e2e_ack / test_dual_layer). Roughly two thirds of every copy was the same inert
// boilerplate — and the copies had silently DIVERGED on the one seam that matters, the forced-rand seam
// (3-B item 8; the divergence was found by 3-B item 5). This base owns the inert seams plus ONE rand
// semantic; each TU derives and overrides only the seams it genuinely spies on (tx capture, timer capture,
// crypto RNG, ...). `emit` is deliberately left PURE — every TU captures a different projection of it.
//
// ★ THE FORCED-RAND SEAM — read before touching `rand_range`:
//   The real Hal contract is `rand_range(lo, hi) -> [lo, hi)` (hal.h:114). The eight stubs used four
//   different semantics, and SEVEN of them dropped `hi` on the floor (`int rand_range(int lo, int)`),
//   which is why no native test could observe a jitter WINDOW's upper bound: with a forced value the
//   returned delay was independent of the window, so poisoning the window changed nothing.
//   This fixture HONOURS `hi`: a forced value is clamped into [lo, hi), exactly as the real Hal must be.
//   Consequence, and the point: forcing a deliberately-huge value now yields `hi - 1`, so a test CAN pin
//   a window's top (see the `§3e herd-spread` case in test_dual_layer.cpp, the one pre-existing user of
//   this idiom: `_rand_ret = 999999` asserts the jitter cap at `jmax - 1`).
//   Owner ruling 2026-07-26: pick the CLAMPED semantic.
#pragma once

#include "hal.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mrtest {

// Inert-by-default Hal. Derive, override the seams you spy on, and implement `emit`.
class TestHalBase : public MESHROUTE_NS::Hal {
public:
    uint64_t _now = 0;              // settable clock; now() reads it

    // Forced-rand knobs. Both compose; `_rand_ret` wins when set.
    int _rand_ret     = -1;         // >= 0 => force this draw (CLAMPED into [lo, hi)); -1 => use lo + _rand_lo_bias
    int _rand_lo_bias = 0;          // offset from `lo` for the un-forced draw (0 => plain `lo`)
    int rand_calls    = 0;          // every rand_range() call, incl. those made via rand_bytes()

    // ---- radio — inert: TX succeeds and is discarded, LBT/duty always idle.
    MESHROUTE_NS::TxResult tx(const uint8_t*, size_t, const MESHROUTE_NS::TxParams&) override {
        return MESHROUTE_NS::TxResult::ok;
    }
    void     set_rx_sf(int) override {}
    // ★ §id-hash S1c: scriptable, DEFAULT 0 — so every pre-existing test sees the historical "always idle" channel and
    // is byte-identical. Set it (with cfg.lbt_enabled) to force tx_initiating down its LBT-defer path, which is the
    // only way to reach `schedule_lbt_defer`'s ring-full DROP from a native fixture.
    uint64_t _busy_until = 0;
    uint64_t channel_busy_until() override { return _busy_until; }
    uint64_t airtime_used_ms(uint64_t) override { return 0; }
    uint64_t oldest_tx_end_ms() override { return 0; }

    // ---- time / timers — the clock is scriptable; timers always accept and are not recorded.
    uint64_t now() override { return _now; }
    bool     after(uint32_t, uint32_t) override { return true; }
    void     cancel(uint32_t) override {}

    // ---- identity
    void     set_protocol_id(int) override {}

    // ---- rng — the ONE forced-rand semantic. Honours BOTH bounds: result is always in [lo, hi)
    // whenever the range is non-empty (and `lo` for a degenerate range, matching the real Hal).
    int rand_range(int lo, int hi) override {
        ++rand_calls;
        int v = (_rand_ret >= 0) ? _rand_ret : (lo + _rand_lo_bias);
        if (v < lo) v = lo;
        if (hi > lo && v >= hi) v = hi - 1;
        return v;
    }
    // Weak entropy derived from the same seam (so `_rand_ret` colours it too). Default => all-zero bytes,
    // which e2e_seal_inner REFUSES by design (R7 bad-RNG guard) — a TU that needs a non-degenerate stream
    // overrides this (test_node_r3 / test_node_hashlocate do).
    void rand_bytes(uint8_t* o, size_t n) override {
        for (size_t i = 0; i < n; ++i) o[i] = static_cast<uint8_t>(rand_range(0, 256));
    }

    // ---- telemetry — `emit` stays PURE ON PURPOSE (each TU captures its own projection).
    // ★ §id-hash S1d: logs are CAPTURED now (they were discarded). `_hal.log` is the ONE operator-facing channel
    // lib/core has on metal — MR_EMIT is device-stripped — so the no-silent-loss report for a deferred frame that
    // dies at the radio queue lands here, and a test must be able to see it. Default behaviour is unchanged for
    // every existing fixture: they simply never look.
    std::vector<std::string> logs;
    void log(const char* m) override { logs.emplace_back(m ? m : ""); }
    bool logged(const char* needle) const {
        for (const auto& l : logs) if (l.find(needle) != std::string::npos) return true;
        return false;
    }
};

// ★★★ §B278 S1b — THE ONE FIELD-LEVEL EMIT PROJECTION, shared rather than hand-rolled per TU (U1/U2).
//     `TestHalBase::emit` stays PURE on purpose (see the header note): each TU still owns WHAT it projects.
//     This is the recorder a TU's own override can feed when a case must assert on the FIELDS themselves —
//     their NAMES, their ORDER and their TYPES — and not merely on the event kind. F6's whole obligation is
//     that existing fields keep their name and position while new ones are APPENDED, and a kind-only capture
//     is structurally blind to every one of those three properties.
// ⛔ A MISSING FIELD IS NEVER A ZERO: `index_of` answers -1 and `has` answers false, so an assertion has to
//    state which it means. (`compare_corpus_slice_g.py`'s C5 records the same lesson on the analyzer side.)
struct EmitRecord {
    std::string              type;
    std::vector<std::string> keys;    // in EMIT ORDER — the order IS the contract
    std::vector<int64_t>     ints;    // EventField::i, meaningful where kinds[i] == i64
    std::vector<int>         kinds;   // static_cast<int>(EventField::T)
    int index_of(const char* k) const {
        for (size_t i = 0; i < keys.size(); ++i) if (keys[i] == k) return static_cast<int>(i);
        return -1;
    }
    bool    has(const char* k) const { return index_of(k) >= 0; }
    int64_t at(const char* k) const { const int i = index_of(k); return i < 0 ? -1 : ints[static_cast<size_t>(i)]; }
    bool    is_i64(const char* k) const {
        const int i = index_of(k);
        return i >= 0 && kinds[static_cast<size_t>(i)] == static_cast<int>(MESHROUTE_NS::EventField::T::i64);
    }
    // The complete key list, in order, as one comparable string — so a case pins the SHAPE in one assertion
    // and a reorder or an insertion in the middle can never read as "the field is still there".
    std::string shape() const {
        std::string s;
        for (const auto& k : keys) { if (!s.empty()) s += ","; s += k; }
        return s;
    }
};

struct EmitFieldLog {
    std::vector<EmitRecord> records;
    void record(const char* type, const MESHROUTE_NS::EventField* f, size_t n) {
        EmitRecord r; r.type = type ? type : "";
        for (size_t i = 0; i < n; ++i) {
            r.keys.emplace_back(f[i].key ? f[i].key : "");
            r.ints.push_back(f[i].i);
            r.kinds.push_back(static_cast<int>(f[i].type));
        }
        records.push_back(std::move(r));
    }
    std::vector<const EmitRecord*> all(const char* type) const {
        std::vector<const EmitRecord*> out;
        for (const auto& r : records) if (r.type == type) out.push_back(&r);
        return out;
    }
    const EmitRecord* last(const char* type) const {
        const EmitRecord* r = nullptr;
        for (const auto& e : records) if (e.type == type) r = &e;
        return r;
    }
    size_t n(const char* type) const { return all(type).size(); }
    void   clear() { records.clear(); }
};

}  // namespace mrtest
