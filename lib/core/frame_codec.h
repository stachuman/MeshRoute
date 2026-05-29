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
// BCN — periodic beacon frame (cmd-nibble 0x0) — ROADMAP §10.3
// -----------------------------------------------------------------------------
//   byte 0     : cmd=0x0(7..4) | leaf_id(3..0)        [short cmd code, never 'B']
//   byte 1     : src (8-bit node_id)
//   byte 2     : has_schedule(b7)|self_gateway(b6)|is_mobile(b5)|has_seen_bitmap(b4)|has_ext(b3)|n_entries_lo(b2..0)
//   byte 3     : n_entries_hi(b7..5) | rsv(b4..0)     [n_entries = lo3 | hi3<<3, 0..63]
//   bytes 4-7  : key_hash32 (LITTLE-ENDIAN u32)
//   body: [schedule if has_schedule] -> n_entries x 4-B entry -> [32-B seen-bitmap] -> [ext_len + ext bytes]
//
// C5 scope: header + 4-B entries + schedule block + 32-B seen-bitmap implemented;
// the ext-TLV block is OPAQUE (ext_len + raw bytes) — the 4 TLV body codecs land
// with the channel/gateway/liveness iterations. n_entries kept (§10.6 drop deferred).

// Route entry (4 B): dest | next | score_bucket(4 hi)|rsv(3)|is_gateway(b0) | hops(full byte).
struct beacon_entry {
    uint8_t dest;
    uint8_t next;
    uint8_t score_bucket;   // 4-bit
    bool    is_gateway;
    uint8_t hops;           // full byte (1..255)
};

// Schedule record (4 B): b0 = layer_id(4 hi)|(routing_sf-5)(b3..1)|period_unit_5s(b0);
//   b1 = duration_100ms; b2 = offset_100ms (re-stamped at TX by the RUNTIME, not the
//   codec); b3 = period_units (×1000 ms if period_unit_5s==0, ×5000 ms if ==1).
struct schedule_record {
    uint8_t layer_id;       // 4-bit
    uint8_t routing_sf;     // 5..12 (wire stores sf-5, 3 bits)
    bool    period_unit_5s;
    uint8_t duration_100ms;
    uint8_t offset_100ms;
    uint8_t period_units;
};

struct beacon_in {
    uint8_t  leaf_id;
    bool     self_gateway;
    bool     is_mobile;
    uint8_t  src;
    uint32_t key_hash32;
    uint8_t  gateway_spread_nibble;                  // schedule herd-spread (0..15)
    std::span<const schedule_record> schedule;       // empty -> has_schedule=0 (<=15)
    std::span<const beacon_entry>    entries;        // <=63
    std::span<const uint8_t> seen_bitmap;            // empty -> has_seen_bitmap=0; else exactly 32 B (else pack->0)
    std::span<const uint8_t> ext;                    // empty -> has_ext=0; else opaque payload (<=255)
};
// Bytes written, or 0 on bad input (schedule>15 / entries>63 / ext>255) or short out.
size_t pack_beacon(const beacon_in& in, std::span<uint8_t> out);

struct beacon_out {
    uint8_t  leaf_id; bool self_gateway; bool is_mobile; uint8_t src; uint32_t key_hash32;
    bool     has_schedule; uint8_t gateway_spread_nibble; uint8_t schedule_count;
    uint8_t  n_entries; bool has_seen_bitmap; bool has_ext;
    // byte offsets into `frame` for the accessors:
    size_t   schedule_off; size_t entries_off; size_t seen_off; size_t ext_off; size_t ext_len; size_t frame_len;
};
std::optional<beacon_out> parse_beacon(std::span<const uint8_t> frame);
// i-th route entry (i < n_entries) / schedule record (i < schedule_count); nullopt if out of range.
std::optional<beacon_entry>    parse_beacon_entry   (std::span<const uint8_t> frame, const beacon_out& bcn, uint8_t i);
std::optional<schedule_record> parse_beacon_schedule(std::span<const uint8_t> frame, const beacon_out& bcn, uint8_t i);
// 32-byte seen-bitmap span (empty if !has_seen_bitmap). bit `id`: byte id/8, mask 1<<(id%8).
std::span<const uint8_t> beacon_seen_bitmap(std::span<const uint8_t> frame, const beacon_out& bcn);
// opaque ext payload span (empty if !has_ext).
std::span<const uint8_t> beacon_ext(std::span<const uint8_t> frame, const beacon_out& bcn);

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

// -----------------------------------------------------------------------------
// J — join family (cmd-nibble 0x9) — ROADMAP §10.3. OTAA-style join + short-id
// lease. 4 opcodes, exact length per opcode. All multi-byte fields LITTLE-ENDIAN.
// -----------------------------------------------------------------------------
//   byte 0 : cmd=0x9(7..4) | leaf_id(3..0)
//   byte 1 : gateway_capable(bit 7) | is_mobile(bit 6) | opcode(bits 5..4) | rsv(3..0)
//            [reading A — RTS-byte5-consistent; §10.3 wording was ambiguous]
//   [opcode-specific body]
// Opcode values are NON-sequential: DISCOVER=0, CLAIM=1, DENY=2, OFFER=3.
enum class j_opcode : uint8_t { discover = 0, claim = 1, deny = 2, offer = 3 };
constexpr uint8_t J_DENY_CONFLICT = 1, J_DENY_PENDING_CLAIM = 2, J_DENY_OWN_ID_DEFENSE = 3;

// DISCOVER (6 B): key_hash32(LE).
struct j_discover_in { uint8_t leaf_id; bool gateway_capable; bool is_mobile; uint32_t key_hash32; };
// OFFER (8 B): responder_node_id, responder_key_hash32(LE), data_sf_bitmap.
struct j_offer_in    { uint8_t leaf_id; bool gateway_capable; bool is_mobile;
                       uint8_t responder_node_id; uint32_t responder_key_hash32; uint8_t data_sf_bitmap; };
// CLAIM (11 B): key_hash32(LE), proposed_node_id, lease_age_seconds(u16 LE), claim_epoch, nonce.
struct j_claim_in    { uint8_t leaf_id; bool gateway_capable; bool is_mobile; uint32_t key_hash32;
                       uint8_t proposed_node_id; uint16_t lease_age_seconds; uint8_t claim_epoch; uint8_t nonce; };
// DENY (15 B): denied_node_id, owner_key_hash32(LE), claimant_key_hash32(LE),
//              owner_lease_age_seconds(u16 LE), owner_claim_epoch, reason.
struct j_deny_in     { uint8_t leaf_id; bool gateway_capable; bool is_mobile; uint8_t denied_node_id;
                       uint32_t owner_key_hash32; uint32_t claimant_key_hash32;
                       uint16_t owner_lease_age_seconds; uint8_t owner_claim_epoch; uint8_t reason; };
size_t pack_j_discover(const j_discover_in& in, std::span<uint8_t> out);   // 6
size_t pack_j_offer   (const j_offer_in&    in, std::span<uint8_t> out);   // 8
size_t pack_j_claim   (const j_claim_in&    in, std::span<uint8_t> out);   // 11
size_t pack_j_deny    (const j_deny_in&     in, std::span<uint8_t> out);   // 15

// One parse returns opcode + the superset of fields (only the opcode's are
// meaningful — mirrors the Lua parse_j single-table). nullopt on wrong cmd or
// wrong EXACT length for the opcode.
struct j_out {
    uint8_t  leaf_id; bool gateway_capable; bool is_mobile; uint8_t opcode;
    uint32_t key_hash32;                                                       // DISCOVER, CLAIM
    uint8_t  responder_node_id; uint32_t responder_key_hash32; uint8_t data_sf_bitmap;  // OFFER
    uint8_t  proposed_node_id; uint16_t lease_age_seconds; uint8_t claim_epoch; uint8_t nonce;  // CLAIM
    uint8_t  denied_node_id; uint32_t owner_key_hash32; uint32_t claimant_key_hash32;
    uint16_t owner_lease_age_seconds; uint8_t owner_claim_epoch; uint8_t reason;            // DENY
};
std::optional<j_out> parse_j(std::span<const uint8_t> frame);

}  // namespace meshroute
