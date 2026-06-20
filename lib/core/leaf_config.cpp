// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#include "leaf_config.h"
#include "protocol_constants.h"
#include "monocypher.h"

namespace MESHROUTE_NS {

uint16_t leaf_config_hash(uint16_t allowed_sf_bitmap, uint32_t duty_ppm,
                          const char* leaf_name, uint8_t leaf_name_len) {
    if (leaf_name_len > protocol::leaf_name_max) leaf_name_len = protocol::leaf_name_max;   // defensive clamp
    uint8_t msg[2 + 4 + 1 + protocol::leaf_name_max];
    size_t  n = 0;
    msg[n++] = static_cast<uint8_t>(allowed_sf_bitmap);                 // u16 LE
    msg[n++] = static_cast<uint8_t>(allowed_sf_bitmap >> 8);
    msg[n++] = static_cast<uint8_t>(duty_ppm);                          // u32 LE
    msg[n++] = static_cast<uint8_t>(duty_ppm >> 8);
    msg[n++] = static_cast<uint8_t>(duty_ppm >> 16);
    msg[n++] = static_cast<uint8_t>(duty_ppm >> 24);
    msg[n++] = leaf_name_len;                                           // u8 length prefix
    for (uint8_t i = 0; i < leaf_name_len; ++i) msg[n++] = static_cast<uint8_t>(leaf_name[i]);

    uint8_t h[64];
    crypto_blake2b(h, 64, msg, n);                                      // BLAKE2b-512, then [:2] LE (project convention)
    return static_cast<uint16_t>(static_cast<uint16_t>(h[0]) | (static_cast<uint16_t>(h[1]) << 8));
}

// R6.2 CONFIG_ANSWER body codec (LE): lineage·epoch·bitmap (u16) · duty_ppm (u32) · name_len (u8) · name.
size_t pack_config_answer(const ConfigAnswer& in, uint8_t* out, size_t cap) {
    uint8_t nlen = in.leaf_name_len > protocol::leaf_name_max ? protocol::leaf_name_max : in.leaf_name_len;
    const size_t need = 11 + nlen;
    if (cap < need) return 0;
    size_t i = 0;
    out[i++] = uint8_t(in.lineage_id);        out[i++] = uint8_t(in.lineage_id >> 8);
    out[i++] = uint8_t(in.config_epoch);      out[i++] = uint8_t(in.config_epoch >> 8);
    out[i++] = uint8_t(in.allowed_sf_bitmap); out[i++] = uint8_t(in.allowed_sf_bitmap >> 8);
    out[i++] = uint8_t(in.duty_ppm);          out[i++] = uint8_t(in.duty_ppm >> 8);
    out[i++] = uint8_t(in.duty_ppm >> 16);    out[i++] = uint8_t(in.duty_ppm >> 24);
    out[i++] = nlen;
    for (uint8_t k = 0; k < nlen; ++k) out[i++] = uint8_t(in.leaf_name[k]);
    return i;
}

bool parse_config_answer(const uint8_t* body, size_t len, ConfigAnswer& out) {
    if (len < 11) return false;
    out.lineage_id        = uint16_t(body[0] | (body[1] << 8));
    out.config_epoch      = uint16_t(body[2] | (body[3] << 8));
    out.allowed_sf_bitmap = uint16_t(body[4] | (body[5] << 8));
    out.duty_ppm = uint32_t(body[6]) | (uint32_t(body[7]) << 8) | (uint32_t(body[8]) << 16) | (uint32_t(body[9]) << 24);
    const uint8_t nlen = body[10];
    if (nlen > protocol::leaf_name_max || len < size_t(11) + nlen) return false;
    out.leaf_name_len = nlen;
    for (uint8_t k = 0; k < nlen; ++k) out.leaf_name[k] = char(body[11 + k]);
    return true;
}

}  // namespace MESHROUTE_NS
