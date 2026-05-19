// MeshRoute — frame_codec.cpp
//
// Iteration 1 stub: function signatures compile but return empty/nullopt.
// First real implementation lands in iteration 2 (pack_beacon round-trip
// matching the Lua reference byte-for-byte).
//
// Spec to mirror: spec/dv_dual_sf.lua lines ~1284 (pack_beacon) and
// ~1402 (parse_beacon). The Lua already returns identical bytes for
// identical inputs — our port just needs to match.

#include "frame_codec.h"

namespace meshroute {

size_t pack_beacon(const beacon_in& /*in*/, std::span<uint8_t> /*out*/) {
    // TODO(iteration-2): port pack_beacon from spec/dv_dual_sf.lua:1284.
    //   - byte 0 = 'B'
    //   - byte 1 = packed flags + leaf_id
    //   - byte 2 = src
    //   - byte 3 = n_byte (n_entries + has_seen_bitmap + has_ext bits)
    //   - bytes 4..7 = key_hash32 LE
    //   - schedule block (when has_schedule)
    //   - 3-byte entries × n
    //   - optional seen_bitmap (32 B), optional ext block
    return 0;
}

std::optional<beacon_out> parse_beacon(std::span<const uint8_t> /*frame*/) {
    // TODO(iteration-2): port parse_beacon from spec/dv_dual_sf.lua:1402.
    //   - Validate tag 'B' and minimum length 8 bytes
    //   - Unpack byte 1 flags + leaf_id
    //   - Read src, n_byte, key_hash32 LE
    //   - Handle schedule block presence
    //   - Bounds-check entries + optional blocks
    return std::nullopt;
}

std::optional<beacon_entry> parse_beacon_entry(std::span<const uint8_t> /*frame*/,
                                                const beacon_out& /*bcn*/,
                                                uint8_t /*index*/) {
    // TODO(iteration-2): walk the entries span at the right offset.
    return std::nullopt;
}

}  // namespace meshroute
