// MeshRoute — frame_codec.h
//
// Wire-format codecs for the dv_dual_sf protocol's frame types.
// Each function mirrors a `pack_*` / `parse_*` pair in spec/dv_dual_sf.lua
// and produces byte-for-byte identical output for a given input.
//
// Current scaffold: BCN only. RTS/CTS/DATA/ACK/NACK/Q/J added in
// subsequent iterations, in the order they appear in PROTOCOL.md §3.
//
// Conventions:
//   * Buffers passed in as `std::span<const uint8_t>` (decode) /
//     `std::span<uint8_t>` (encode); callers own the backing storage.
//   * Return values: bytes_written for encoders; std::optional<Parsed>
//     for decoders (nullopt on malformed input).
//   * No exceptions, no heap. Encoders are bounded by PROTOCOL.beacon_max_bytes
//     etc.; decoders return nullopt on length-cap violation.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace meshroute {

// -----------------------------------------------------------------------------
// BCN — periodic beacon frame
// -----------------------------------------------------------------------------
// Wire layout (matches PROTOCOL.md §3.1):
//   byte 0       : tag 'B'
//   byte 1       : leaf_id(4 hi) | has_schedule(1) | self_gateway(1) | is_mobile(1) | req_sync(1)
//   byte 2       : src (8-bit node_id)
//   byte 3       : n_byte = n_entries(6) | has_seen_bitmap(1) | has_ext(1)
//   bytes 4-7    : key_hash32 (LE u32)
//   [schedule block, if has_schedule]
//   bytes ...    : 3-byte route entries × n_entries
//   [seen_bitmap (32 B), if has_seen_bitmap]
//   [ext: 1-byte length + payload, if has_ext]

struct beacon_entry {
    uint8_t dest;
    uint8_t next;
    uint8_t score_bucket;  // 4-bit
    uint8_t hops_wire;     // 3-bit (hops - 1)
    bool    is_gateway;
};

struct beacon_in {
    uint8_t  leaf_id;
    bool     has_schedule;
    bool     self_gateway;
    bool     is_mobile;
    bool     req_sync;
    uint8_t  src;
    uint32_t key_hash32;
    std::span<const beacon_entry> entries;  // caller-provided storage
    // Optional blocks deferred to iteration 2: schedule_block, seen_bitmap, ext_payload.
};

// Encode a beacon into `out`. Returns bytes written, or 0 on failure
// (buffer too small or beacon_max_bytes exceeded).
size_t pack_beacon(const beacon_in& in, std::span<uint8_t> out);

struct beacon_out {
    uint8_t  leaf_id;
    bool     has_schedule;
    bool     self_gateway;
    bool     is_mobile;
    bool     req_sync;
    uint8_t  src;
    uint32_t key_hash32;
    bool     has_seen_bitmap;
    bool     has_ext;
    uint8_t  n_entries;
    // Caller iterates entries via parse_beacon_entry(frame, idx, &entry).
    // Total parsed frame length:
    size_t   frame_len;
};

std::optional<beacon_out> parse_beacon(std::span<const uint8_t> frame);
std::optional<beacon_entry> parse_beacon_entry(std::span<const uint8_t> frame,
                                                const beacon_out& bcn,
                                                uint8_t index);

}  // namespace meshroute
