// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#pragma once
#include <cstdint>
#include <cstddef>
#include "protocol_constants.h"   // protocol::leaf_name_max (ConfigAnswer buffer)

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

// R6.2 CONFIG_ANSWER body (the DATA TYPE 6 payload a member sends to a puller). Layout (LE):
//   [lineage u16][config_epoch u16][allowed_sf_bitmap u16][duty_ppm u32][leaf_name_len u8][leaf_name ...]
// The puller adopts the whole tuple. Fixed prefix = 11 B + name. Max = 11 + leaf_name_max.
struct ConfigAnswer {
    uint16_t lineage_id = 0;
    uint16_t config_epoch = 0;
    uint16_t allowed_sf_bitmap = 0;
    uint32_t duty_ppm = 0;
    uint8_t  leaf_name_len = 0;
    char     leaf_name[protocol::leaf_name_max] = {};
};
size_t pack_config_answer(const ConfigAnswer& in, uint8_t* out, size_t cap);   // bytes written, 0 on short buf
bool   parse_config_answer(const uint8_t* body, size_t len, ConfigAnswer& out); // false on malformed

}  // namespace MESHROUTE_NS
