// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#pragma once
#include <cstdint>
#include <cstddef>

#ifndef MESHROUTE_NS
#define MESHROUTE_NS meshroute   // Slice 5 faithful two-lib: gateway variant compiles with -DMESHROUTE_NS=meshroute_gw
#endif

namespace MESHROUTE_NS {

// R6.1 leaf-config fingerprint (spec §2): the misconfiguration gate's per-leaf hash.
//   config_hash = BLAKE2b( u16 allowed_sf_bitmap LE ‖ u32 duty_ppm LE ‖ u8 leaf_name_len ‖ leaf_name )[:2]
// taken as a LE u16 of the first 2 BLAKE2b-512 bytes (the project's "[:N]" = blake2b-512-then-truncate convention,
// matching dm_crypto / identity). 2026-06-20b: 2-B, matched to the 6-B header field; the SAME value gates both B and F.
// PURE — no node state — so the emit path, the membership filter, and the golden test all compute the same value.
// duty_ppm = round(duty_cycle * 1e6); leaf_name_len is clamped to leaf_name_max.
uint16_t leaf_config_hash(uint16_t allowed_sf_bitmap, uint32_t duty_ppm,
                          const char* leaf_name, uint8_t leaf_name_len);

}  // namespace MESHROUTE_NS
