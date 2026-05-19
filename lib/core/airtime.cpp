// MeshRoute — airtime.cpp
// Implementation note: keep this file numerically identical to
// the Lua reference. The test/test_airtime.cpp suite pins a dozen
// (sf, bw, cr, len) combinations against values captured from the
// Lua model; if you tweak the formula, regenerate those baselines.

#include "airtime.h"

#include <cmath>

namespace meshroute {

uint32_t airtime_ms(uint8_t sf,
                    uint32_t bw_hz,
                    uint8_t cr,
                    uint16_t preamble_sym,
                    uint16_t len_bytes) {
    // ms per symbol = 2^sf / (bw_hz / 1000)
    const double t_sym = static_cast<double>(1u << sf) /
                          (static_cast<double>(bw_hz) / 1000.0);

    // preamble + sync = (preamble_sym + 4.25) symbols
    const double t_pre = (static_cast<double>(preamble_sym) + 4.25) * t_sym;

    // Low-data-rate-optimize: de=1 when t_sym >= 16 ms (typically SF11/SF12 @ BW125)
    const int de = (t_sym >= 16.0) ? 1 : 0;

    // Payload-symbol formula (Semtech AN1200.13)
    const int num = 8 * static_cast<int>(len_bytes) - 4 * sf + 44;
    const int den = 4 * (static_cast<int>(sf) - 2 * de);

    // pay_sym = 8 + max(ceil(num/den) * cr, 0)
    int pay_sym_extra = 0;
    if (den > 0) {
        const int ceil_div = (num + den - 1) / den;  // ceil(num/den) for positive den
        pay_sym_extra = ceil_div * cr;
        if (pay_sym_extra < 0) pay_sym_extra = 0;
    }
    const double pay_sym = 8.0 + static_cast<double>(pay_sym_extra);

    const double total_ms = t_pre + pay_sym * t_sym;
    return static_cast<uint32_t>(std::floor(total_ms));
}

}  // namespace meshroute
