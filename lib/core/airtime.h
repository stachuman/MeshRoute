// MeshRoute — airtime.h
//
// LoRa-PHY airtime calculation. Direct port of `airtime_ms()` in
// spec/dv_dual_sf.lua (line ~2203). The math follows the Semtech
// AN1200.13 formula, including the SF≥10 low-data-rate-optimize
// fix (de=1).
//
// All inputs / outputs match the Lua signature exactly so the
// cross-implementation differential test can plug values from
// either side and compare.

#pragma once

#include <cstdint>

namespace meshroute {

// Airtime in whole milliseconds for one LoRa frame.
//
//   sf            : 5..12 spreading factor
//   bw_hz         : 125000 / 250000 / 500000 (other valid LoRa BWs OK)
//   cr            : 5..8 (CR4/5..CR4/8)
//   preamble_sym  : preamble symbol count (typically 16; PROTOCOL constant)
//   len_bytes     : on-air payload length in bytes (whole frame, not user body)
//
// Returns floor(t_pre + pay_sym * t_sym) — matches Lua math.floor.
uint32_t airtime_ms(uint8_t sf,
                    uint32_t bw_hz,
                    uint8_t cr,
                    uint16_t preamble_sym,
                    uint16_t len_bytes);

}  // namespace meshroute
