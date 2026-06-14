// MeshRoute — airtime.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
// Implementation note: keep this file numerically identical to
// the Lua reference. The test/test_airtime.cpp suite pins a dozen
// (sf, bw, cr, len) combinations against values captured from the
// Lua model; if you tweak the formula, regenerate those baselines.

#include "airtime.h"

#include <cmath>

namespace MESHROUTE_NS {

uint32_t airtime_ms(uint8_t sf,
                    uint32_t bw_hz,
                    uint8_t cr,
                    uint16_t preamble_sym,
                    uint16_t len_bytes) {
    // ms per symbol = 2^sf / (bw_hz / 1000)
    const double t_sym = static_cast<double>(1u << sf) /
                          (static_cast<double>(bw_hz) / 1000.0);

    // SX126x datasheet §6.1.4: SF5/SF6 use a 6.25-symbol sync offset (not 4.25)
    // and drop the +8 header-symbol constant from the payload numerator (+36,
    // not +44). SF7-12 unchanged. Mirrors the Lua airtime_ms and RadioLib
    // SX126x::calculateTimeOnAir so low-SF (SF5/SF6) airtime stays realistic.
    const bool   low_sf   = (sf == 5 || sf == 6);
    const double sync_sym = low_sf ? 6.25 : 4.25;

    // preamble + sync = (preamble_sym + sync_sym) symbols
    const double t_pre = (static_cast<double>(preamble_sym) + sync_sym) * t_sym;

    // Low-data-rate-optimize: de=1 when t_sym >= 16 ms (typically SF11/SF12 @ BW125)
    const int de = (t_sym >= 16.0) ? 1 : 0;

    // Payload-symbol formula (Semtech AN1200.13; SF5/SF6 per SX126x §6.1.4)
    const int num = 8 * static_cast<int>(len_bytes) - 4 * sf + (low_sf ? 36 : 44);
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
