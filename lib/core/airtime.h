// MeshRoute — airtime.h
//
// LoRa-PHY airtime calculation. Direct port of `airtime_ms()` in
// spec/dv_dual_sf.lua (line ~2931). The math follows the Semtech
// AN1200.13 formula, including the low-data-rate-optimize fix
// (de=1 when t_sym >= 16 ms, i.e. SF11/SF12 @ BW125) and the
// SX126x datasheet §6.1.4 SF5/SF6 framing (6.25-symbol sync + a
// +36 payload numerator). Stays numerically identical to the Lua.
//
// All inputs / outputs match the Lua signature exactly so the
// cross-implementation differential test can plug values from
// either side and compare.
//
// Cross-checked 2026-05-29 vs MeshCore's RadioLib SX126x::calculateTimeOnAir:
// identical for SF7-12; the SF5/SF6 §6.1.4 case was added to BOTH this port
// and the Lua so airtime matches the real chip (e.g. SF6/len50: 60 -> 61 ms).

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
