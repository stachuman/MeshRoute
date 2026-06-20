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

}  // namespace MESHROUTE_NS
