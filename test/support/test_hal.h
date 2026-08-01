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
    void log(const char*) override {}
};

}  // namespace mrtest
