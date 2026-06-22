// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#pragma once
#include <cstdint>
#include <cstddef>
#include "protocol_constants.h"   // protocol::leaf_name_max (CConfig name buffer)

#ifndef MESHROUTE_NS
#define MESHROUTE_NS meshroute   // Slice 5 faithful two-lib: gateway variant compiles with -DMESHROUTE_NS=meshroute_gw
#endif

namespace MESHROUTE_NS {

// ★ CANONICAL wire encodings shared by the C config frame AND the config_hash (§5). The hash MUST hash the EXACT
// bytes the C frame carries — else a mother + a joiner that compute config_hash over different representations
// disagree forever and the joiner re-pulls in a loop. So both go through these two pairs of converters.
//
// SF set: internal `allowed_sf_bitmap` has bit n = SFn (SF5..SF12 => bits 5..12). The C frame / hash use a u8 where
// bit i = SF(5+i) (the 8 valid SFs packed to bits 0..7) -> wire = (bitmap >> 5) & 0xFF; bitmap = wire << 5.
constexpr uint8_t  sf_bitmap_to_wire(uint16_t bitmap) { return static_cast<uint8_t>((bitmap >> 5) & 0xFF); }
constexpr uint16_t sf_wire_to_bitmap(uint8_t wire)    { return static_cast<uint16_t>(static_cast<uint16_t>(wire) << 5); }
// Duty: the C frame / hash use `duty_bp` (u16, 0.01% units / basis points; 0..10000 = 0%..100%). The internal
// `duty_cycle` is a 0..1 fraction. create / `cfg set duty` quantize to this 0.01% step so the wire value is exact.
constexpr uint16_t duty_to_bp(double duty_cycle) {
    return duty_cycle <= 0.0 ? 0u : (duty_cycle >= 1.0 ? 10000u
                                                       : static_cast<uint16_t>(duty_cycle * 10000.0 + 0.5));
}
constexpr double   bp_to_duty(uint16_t duty_bp) { return static_cast<double>(duty_bp) / 10000.0; }

// R6.1 leaf-config fingerprint (spec §2): the misconfiguration gate's per-leaf hash. Hashed over the EXACT C-frame
// wire forms (§5): u8 sf_list (sf_bitmap_to_wire) ‖ u16 duty_bp LE ‖ u8 leaf_name_len ‖ leaf_name. Taken as a LE u16
// of the first 2 BLAKE2b-512 bytes (the project's "[:N]" convention). PURE — the emit path, the membership filter,
// and the golden test all compute the same value. leaf_name_len is clamped to leaf_name_max (10).
uint16_t leaf_config_hash(uint16_t allowed_sf_bitmap, uint16_t duty_bp,
                          const char* leaf_name, uint8_t leaf_name_len);

// The `C` config frame body (cmd 0xB; the control-plane answer to a CONFIG_PULL — replaces the routed CONFIG_ANSWER).
// Wire body (LE): [sf_list u8][duty_bp u16][config_epoch u16][leaf_name_len u8][leaf_name ...]. NO lineage_id on the
// wire (it's the stable leaf identity, taken from the beacon the joiner heard). config_epoch IS on the wire (race-free
// LWW adopt). Fixed prefix = 6 B + name. Max = 6 + leaf_name_max. The struct holds the INTERNAL allowed_sf_bitmap +
// duty_bp; pack/parse convert the SF set to/from the u8 wire form.
struct CConfig {
    uint16_t allowed_sf_bitmap = 0;   // internal form (bit n = SFn); pack converts to the u8 wire SF list
    uint16_t duty_bp = 0;             // 0.01% units (the wire + hash duty form)
    uint16_t config_epoch = 0;
    uint8_t  leaf_name_len = 0;
    char     leaf_name[protocol::leaf_name_max] = {};
};
size_t pack_c_config(const CConfig& in, uint8_t* out, size_t cap);   // bytes written, 0 on short buf
bool   parse_c_config(const uint8_t* body, size_t len, CConfig& out); // false on malformed

}  // namespace MESHROUTE_NS
