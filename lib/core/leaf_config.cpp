// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#include "leaf_config.h"
#include "protocol_constants.h"
#include "monocypher.h"

namespace MESHROUTE_NS {

uint16_t leaf_config_hash(uint16_t allowed_sf_bitmap, uint16_t duty_bp,
                          const char* leaf_name, uint8_t leaf_name_len) {
    if (leaf_name_len > protocol::leaf_name_max) leaf_name_len = protocol::leaf_name_max;   // defensive clamp
    uint8_t msg[1 + 2 + 1 + protocol::leaf_name_max];
    size_t  n = 0;
    msg[n++] = sf_bitmap_to_wire(allowed_sf_bitmap);                    // u8 SF list (SF5..12) — the C-frame wire form (§5)
    msg[n++] = static_cast<uint8_t>(duty_bp);                           // u16 duty_bp LE
    msg[n++] = static_cast<uint8_t>(duty_bp >> 8);
    msg[n++] = leaf_name_len;                                           // u8 length prefix
    for (uint8_t i = 0; i < leaf_name_len; ++i) msg[n++] = static_cast<uint8_t>(leaf_name[i]);

    uint8_t h[64];
    crypto_blake2b(h, 64, msg, n);                                      // BLAKE2b-512, then [:2] LE (project convention)
    return static_cast<uint16_t>(static_cast<uint16_t>(h[0]) | (static_cast<uint16_t>(h[1]) << 8));
}

// C config frame body codec (LE): sf_list (u8) · duty_bp (u16) · config_epoch (u16) · name_len (u8) · name (§2).
size_t pack_c_config(const CConfig& in, uint8_t* out, size_t cap) {
    uint8_t nlen = in.leaf_name_len > protocol::leaf_name_max ? protocol::leaf_name_max : in.leaf_name_len;
    const size_t need = 6 + nlen;
    if (cap < need) return 0;
    size_t i = 0;
    out[i++] = sf_bitmap_to_wire(in.allowed_sf_bitmap);                 // u8 SF list
    out[i++] = uint8_t(in.duty_bp);           out[i++] = uint8_t(in.duty_bp >> 8);
    out[i++] = uint8_t(in.config_epoch);      out[i++] = uint8_t(in.config_epoch >> 8);
    out[i++] = nlen;
    for (uint8_t k = 0; k < nlen; ++k) out[i++] = uint8_t(in.leaf_name[k]);
    return i;
}

bool parse_c_config(const uint8_t* body, size_t len, CConfig& out) {
    if (len < 6) return false;
    out.allowed_sf_bitmap = sf_wire_to_bitmap(body[0]);
    out.duty_bp      = uint16_t(body[1] | (body[2] << 8));
    out.config_epoch = uint16_t(body[3] | (body[4] << 8));
    const uint8_t nlen = body[5];
    if (nlen > protocol::leaf_name_max || len < size_t(6) + nlen) return false;
    out.leaf_name_len = nlen;
    for (uint8_t k = 0; k < nlen; ++k) out.leaf_name[k] = char(body[6 + k]);
    return true;
}

}  // namespace MESHROUTE_NS
