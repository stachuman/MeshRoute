// MeshRoute — frame_codec.h
//
// Wire-format codecs for the dv_dual_sf protocol's frame types.
// Each function mirrors a `pack_*` / `parse_*` pair in spec/dv_dual_sf.lua
// and produces byte-for-byte identical output for a given input.
//
// Status: CTS + ACK implemented in the §10 cmd-nibble layout (codec track C1).
// BCN below is still the iteration-1 stub documenting the STALE pre-§10 tag-byte
// layout — it migrates to §10 when its codec lands (C5). RTS/NACK/Q (C2),
// H/F (C3), J (C4), DATA (C6) follow.
//
// Wire authority: byte positions = ROADMAP §10.3 (cmd-nibble v2); field meaning
// = the Lua pack_*/parse_* code. Shared primitives live in wire.h.
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
// BCN — periodic beacon frame  (cmd-nibble 0x0)   [C5 — codec NOT yet ported]
// -----------------------------------------------------------------------------
// §10 cmd-nibble layout (ROADMAP §10.3): byte 0 is the SHORT cmd code, never the
// legacy ASCII 'B' (0x42) — C++ frames carry no tag byte.
//   byte 0     : cmd=0x0(4 hi) | leaf_id(4 lo)        [1-byte layer-filter dispatch]
//   byte 1     : src (8-bit node_id)
//   byte 2     : has_schedule(1)|self_gateway(1)|is_mobile(1)|has_seen_bitmap(1)|has_ext(1)|n_entries(3)
//   byte 3     : n_entries(hi bits) | rsv             (or dropped — see SHORTEN below)
//   bytes 4-7  : key_hash32 (LE u32)
//   [schedule block, if has_schedule]
//   route entries × n_entries  (4 B each in the current Lua: dest|next|score_bucket+gw|hops(full byte))
//   [seen_bitmap (32 B), if has_seen_bitmap]
//   [ext: 1-byte length + payload, if has_ext]
//
// SHORTEN-vs-Lua (a standing goal — shrink frames wherever functionality is not
// lost): at C5, pursue ROADMAP §10.6 — order the optional sections BEFORE the
// route entries so n_entries is derivable from frame_len, dropping it (−1 B/BCN).
//
// NOTE: the structs below are iteration-1 placeholders and are STALE (they assume
// 3-B entries / 3-bit hops; the current Lua uses 4-B entries with a full hops
// byte). They get redesigned to the §10 layout when the BCN codec lands (C5).

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

// -----------------------------------------------------------------------------
// CTS — clear-to-send (cmd-nibble 0x2, 3 B) — ROADMAP §10.3
// -----------------------------------------------------------------------------
//   byte 0 : cmd=0x2(4 hi) | ctr_lo(4 lo)
//   byte 1 : (sf-5)(3 hi) | already_received(1) | rsv(4 lo)
//   byte 2 : to(8)
// ctr_lo = low 4 bits of the DATA ctr (hop match); chosen_data_sf in 5..12;
// already_received short-circuits a resend whose ACK was lost; to = requester id.
struct cts_in  { uint8_t ctr_lo; uint8_t chosen_data_sf; bool already_received; uint8_t to; };
struct cts_out { uint8_t ctr_lo; uint8_t chosen_data_sf; bool already_received; uint8_t to; };
// Returns 3 on success; 0 on bad input (sf outside 5..12) or out span < 3.
size_t pack_cts(const cts_in& in, std::span<uint8_t> out);
// nullopt on wrong cmd nibble or len != 3.
std::optional<cts_out> parse_cts(std::span<const uint8_t> frame);

// -----------------------------------------------------------------------------
// ACK — (cmd-nibble 0x4, 3 B) — ROADMAP §10.3
// -----------------------------------------------------------------------------
//   byte 0 : cmd=0x4(4 hi) | ctr_lo(4 lo)
//   byte 1 : budget_hint(2 hi) | snr_bucket(2) | rsv(4 lo)
//   byte 2 : to(8)
// budget_hint / snr_bucket are the already-computed 2-bit fields; the
// snr_q4 -> bucket mapping is protocol-layer (wired when ACK is used at R3).
struct ack_in  { uint8_t ctr_lo; uint8_t budget_hint; uint8_t snr_bucket; uint8_t to; };
struct ack_out { uint8_t ctr_lo; uint8_t budget_hint; uint8_t snr_bucket; uint8_t to; };
size_t pack_ack(const ack_in& in, std::span<uint8_t> out);
std::optional<ack_out> parse_ack(std::span<const uint8_t> frame);

// -----------------------------------------------------------------------------
// RTS — request-to-send (cmd-nibble 0x1, 7 B; +2 B if M_BROADCAST) — ROADMAP §10.3
// -----------------------------------------------------------------------------
//   byte 0 : cmd=0x1(7..4) | leaf_id(3..0)
//   byte 1 : src
//   byte 2 : next
//   byte 3 : ctr_lo(7..4) | addr_len(3..1) | rsv(0)        [addr_len=0 only today]
//   byte 4 : dst
//   byte 5 : sf_index(7..6) | rts_flags(5..2) | rsv(1..0)
//            READING A (§10.3 wording is ambiguous; we pin flags to bits 5..2):
//            within byte 5, M_BROADCAST -> bit 2 (0x04), RELAY -> bit 3 (0x08).
//   byte 6 : payload_len                                   [wraps mod-256 via uint8_t]
//   bytes 7-8 : id_lo16 (BE)  — present iff (rts_flags & RTS_FLAG_M_BROADCAST)
// sf_index: 0..2 = singleton into allowed_data_sfs; 3 = ANY (receiver picks by SNR).
// Contract: all fields MASK/wrap (no clamp); payload_len wraps; parse rejects
// addr_len != 0 (hierarchy deferred).
constexpr uint8_t RTS_FLAG_M_BROADCAST = 0x01;
constexpr uint8_t RTS_FLAG_RELAY       = 0x02;
struct rts_in {
    uint8_t  leaf_id; uint8_t src; uint8_t next; uint8_t ctr_lo;
    uint8_t  dst; uint8_t sf_index; uint8_t rts_flags; uint8_t payload_len;
    uint16_t m_payload_id_lo16;   // appended (BE) iff rts_flags & RTS_FLAG_M_BROADCAST
};
struct rts_out {
    uint8_t  leaf_id; uint8_t src; uint8_t next; uint8_t ctr_lo; uint8_t addr_len;
    uint8_t  dst; uint8_t sf_index; uint8_t rts_flags; uint8_t payload_len;
    bool     m_broadcast; uint16_t m_payload_id_lo16;
};
size_t pack_rts(const rts_in& in, std::span<uint8_t> out);          // 7 or 9; 0 on short buf
std::optional<rts_out> parse_rts(std::span<const uint8_t> frame);   // nullopt: len<7 / cmd / addr_len!=0

// -----------------------------------------------------------------------------
// NACK — (cmd-nibble 0x5, 4 B) — ROADMAP §10.3
// -----------------------------------------------------------------------------
//   byte 0 : cmd=0x5(7..4) | reason(3..0)   0 BUSY_RX, 1 BUDGET, 2 HOP_BUDGET, 3 LOOP_DUP
//   byte 1 : ctr_lo(7..4) | rsv(3..0)
//   byte 2 : payload   (reason-specific; encoded by the protocol layer, packed verbatim)
//   byte 3 : to
// payload is a u8 (0..255) so the Lua's clamp-to-[0,255] is satisfied by the type.
struct nack_in  { uint8_t reason; uint8_t ctr_lo; uint8_t payload; uint8_t to; };
struct nack_out { uint8_t reason; uint8_t ctr_lo; uint8_t payload; uint8_t to; };
size_t pack_nack(const nack_in& in, std::span<uint8_t> out);
std::optional<nack_out> parse_nack(std::span<const uint8_t> frame);

// -----------------------------------------------------------------------------
// Q — query (cmd-nibble 0x6, 4 B header + CHANNEL_PULL body) — ROADMAP §10.3
// -----------------------------------------------------------------------------
//   byte 0 : cmd=0x6(7..4) | leaf_id(3..0)
//   byte 1 : src
//   byte 2 : dest        (0xFF = REQ_SYNC broadcast convention)
//   byte 3 : opcode(7..6) | mobile(bit 5) | rsv(4..0)
//   [CHANNEL_PULL only] byte 4: count ; then count × channel_msg_id (4 B BIG-ENDIAN)
// channel_msg_id is BE (distinct from the LE key_hash32 elsewhere) — keep it BE.
enum class q_opcode : uint8_t { req_sync = 1, channel_pull = 3 };
struct q_in {
    uint8_t leaf_id; uint8_t src; uint8_t dest; q_opcode opcode; bool mobile;
    std::span<const uint32_t> channel_ids;   // only for channel_pull; else empty
};
struct q_out {
    uint8_t leaf_id; uint8_t src; uint8_t dest; uint8_t opcode; bool mobile;
    uint8_t channel_id_count;                // 0 unless channel_pull
};
size_t pack_q(const q_in& in, std::span<uint8_t> out);   // 4, or 5+4N for pull; 0 on short buf / N>255
std::optional<q_out> parse_q(std::span<const uint8_t> frame);
// i-th channel_msg_id (BE) of a CHANNEL_PULL frame; nullopt if index >= count.
std::optional<uint32_t> parse_q_channel_id(std::span<const uint8_t> frame,
                                           const q_out& q, uint8_t index);

// -----------------------------------------------------------------------------
// H — hash-locate flood (cmd-nibble 0x7, 7 B) — ROADMAP §10.3 / §10.6
// -----------------------------------------------------------------------------
// §10.6 flag byte DROPPED (lossless: the flags nibble was hard-zero on pack and
// never read on parse; leaf_id relocates into the cmd byte). Forwardable TTL flood.
//   byte 0   : cmd=0x7(7..4) | leaf_id(3..0)
//   byte 1   : origin            (querier node_id; PRESERVED across forwards)
//   bytes 2-5: key_hash32 (LITTLE-ENDIAN)
//   byte 6   : ttl               (decremented per forward; 0 = drop)
struct h_in  { uint8_t leaf_id; uint8_t origin; uint32_t key_hash32; uint8_t ttl; };
struct h_out { uint8_t leaf_id; uint8_t origin; uint32_t key_hash32; uint8_t ttl; };
size_t pack_h(const h_in& in, std::span<uint8_t> out);            // 7; 0 on short buf
std::optional<h_out> parse_h(std::span<const uint8_t> frame);     // nullopt: len<7 / cmd

// -----------------------------------------------------------------------------
// F — route-find RREQ/RREP flood (cmd-nibble 0x8, 6 B) — ROADMAP §10.3
// -----------------------------------------------------------------------------
//   byte 0 : cmd=0x8(7..4) | leaf_id(3..0)
//   byte 1 : origin              (querier node_id; PRESERVED across forwards)
//   byte 2 : is_reply(bit 7) | rsv(6..0)        [0 = RREQ, 1 = RREP]
//   byte 3 : dst_id
//   byte 4 : ttl_or_next_hop  — RAW dual byte: ttl (RREQ) | next_hop (RREP).
//            The codec surfaces it verbatim; the handler interprets by is_reply.
//            NEVER clamp/validate it (it's a node address when is_reply=1).
//   byte 5 : hops
// NB: is_reply is at bit 7 here (the Lua had it at byte2 bit 0) — re-placed, not bit-copied.
struct f_in  { uint8_t leaf_id; uint8_t origin; bool is_reply; uint8_t dst_id;
               uint8_t ttl_or_next_hop; uint8_t hops; };
struct f_out { uint8_t leaf_id; uint8_t origin; bool is_reply; uint8_t dst_id;
               uint8_t ttl_or_next_hop; uint8_t hops; };
size_t pack_f(const f_in& in, std::span<uint8_t> out);            // 6; 0 on short buf
std::optional<f_out> parse_f(std::span<const uint8_t> frame);     // nullopt: len<6 / cmd

}  // namespace meshroute
