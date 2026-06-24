// MeshRoute — frame_codec.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Wire-format codecs for the dv_dual_sf protocol's frame types.
// Each function mirrors a `pack_*` / `parse_*` pair in dv_dual_sf.lua for FIELD
// MEANING; the C++ wire DIVERGES from the Lua tag-byte wire by design (cmd-nibble,
// shorter frames) — NOT byte-for-byte identical.
//
// Status: §10 cmd-nibble codec track C0–C6 COMPLETE — BCN(0x0), RTS(0x1), CTS(0x2),
// DATA(0x3), ACK(0x4), NACK(0x5), Q(0x6), H(0x7), F(0x8), J(0x9). The layout docs
// below are the §10 cmd-nibble form (not the legacy tag-byte stubs).
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
#ifndef MESHROUTE_NS
#define MESHROUTE_NS meshroute   // Slice 5 faithful two-lib: gateway variant compiles with -DMESHROUTE_NS=meshroute_gw
#endif

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include "protocol_constants.h"   // gw_env_max_hops (the cross-layer layer-path bound, Slice 4b)

namespace MESHROUTE_NS {

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
// CALLER (runtime) CONTRACT: duration_100ms and period_units are floored to [1,255]
// during the runtime's ms→units conversion (mirroring Lua pack_schedule_record:1628/
// 1646) — the codec packs them VERBATIM, exactly as it does offset_100ms. A 0 reaching
// pack_beacon is a runtime conversion bug; the codec does not mask it (so it surfaces).
struct schedule_record {
    uint8_t layer_id;       // 4-bit
    uint8_t routing_sf;     // 5..12 (wire stores sf-5, 3 bits)
    bool    period_unit_5s;
    uint8_t duration_100ms;
    uint8_t offset_100ms;
    uint8_t period_units;
};

// R6.1 leaf-config membership header: a FIXED +6-B block written right after key_hash32, before the schedule (so it
// survives beacon_max_bytes truncation — never the cut field). FLAG-DAY: present on every beacon. Right-sized to u16×3
// (2026-06-20b): ~256 leaves birthday-safe, 65k config writes, 1/65k missed-misconfig (honest-node-benign, §3.4).
inline constexpr size_t BCN_LEAF_HEADER_LEN = 6;    // lineage_id(2) + config_epoch(2) + config_hash(2)

struct beacon_in {
    uint8_t  leaf_id;
    bool     self_gateway;
    bool     is_mobile;
    uint8_t  src;
    uint32_t key_hash32;
    uint16_t lineage_id   = 0;     // R6.1: 0 = UNMANAGED leaf (peer-by-config_hash, backward-compat); else the operator-minted lineage
    uint16_t config_epoch = 0;     // R6.1: monotonic config version (LWW; ties -> higher key_hash32 canonical)
    uint16_t config_hash  = 0;     // R6.1: BLAKE2b(sf_bitmap ‖ duty_ppm ‖ leaf_name)[:2] — the misconfig fingerprint
    uint8_t  gateway_spread_nibble;                  // schedule herd-spread (0..15)
    std::span<const schedule_record> schedule;       // empty -> has_schedule=0 (<=15)
    std::span<const beacon_entry>    entries;        // <=63
    std::span<const uint8_t> seen_bitmap;            // empty -> has_seen_bitmap=0; else exactly 32 B (else pack->0)
    std::span<const uint8_t> ext;                    // empty -> has_ext=0; else opaque payload (<=255)
};
// Bytes written, or 0 on bad input (schedule>15 / entries>63 / ext>255) or short out.
size_t pack_beacon(const beacon_in& in, std::span<uint8_t> out);

// Max route entries that fit a `frame_cap`-byte beacon AFTER its fixed 8-B header and the variable
// schedule / seen-bitmap / ext blocks (caller passes each block's on-wire byte size: schedule = 1+4*n
// or 0; bitmap = 32 or 0; ext_block = 1+ext_payload or 0). Clamped to the 6-bit n_entries field (63).
// This is the TRUE byte-budget cap — replaces a fixed constant that ignored the variable blocks and
// let a full page + a populated ext TLV silently overflow `frame_cap` (→ dropped beacon).
uint8_t beacon_max_entries(size_t frame_cap, size_t sched_bytes, size_t bitmap_bytes, size_t ext_block_bytes);

struct beacon_out {
    uint8_t  leaf_id; bool self_gateway; bool is_mobile; uint8_t src; uint32_t key_hash32;
    uint8_t  wire_version;   // §7c: byte-3 low nibble — cross-version handshake (checked before the format-dependent parse)
    uint16_t lineage_id; uint16_t config_epoch; uint16_t config_hash;   // R6.1 leaf-config header (u16×3 = 6 B)
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

// BCN channel-digest ext-TLV (type 3, dv:1426/1965). Wire: [type<<4 | body_len][count(1B)][count × channel_msg_id
// (4B BE)], body_len = 1+4*count. count is capped at channel_dirty_max_per_bcn (3) so body_len<=13 fits the 4-bit
// length nibble. TLV-framed so the digest coexists with future ext TLVs (parse skips other types).
size_t  pack_channel_digest_tlv(const uint32_t* ids, uint8_t count, std::span<uint8_t> out);    // bytes written, or 0
uint8_t parse_channel_digest_tlv(std::span<const uint8_t> ext, uint32_t* ids_out, uint8_t max); // ids found (0 if no type-3 TLV)

// BCN gateway-layer ext-TLV (type 4, dv:1249/1513) — multi-hop gateway discovery. Wire = the Lua split-list:
// [type<<4 | body_len] then body = N × gw_id(1B), then ceil(N/2) packed layer-nibble bytes (entry i's dest_leaf in
// the LOW nibble if i even, HIGH nibble if i odd). gw_id = the gateway's node_id on the advertising leaf; dest_leaf =
// the 4-bit leaf it bridges TO. N <= bridged_layers_max_per_tlv (9) -> body <= 9+5 = 14 <= the 4-bit len cap (15).
struct GwLayerEntry { uint8_t gw_id; uint8_t dest_leaf; };
size_t  pack_gateway_layer_tlv(const GwLayerEntry* e, uint8_t n, std::span<uint8_t> out);       // bytes written, or 0 (n==0 -> 0)
uint8_t parse_gateway_layer_tlv(std::span<const uint8_t> ext, GwLayerEntry* out, uint8_t max);  // entries found (0 if no type-4 TLV)

// §P4 BCN suspect-node gossip ext-TLVs (dv:1373 build / 1949 parse). A beacon carries EITHER a type-1 OR a type-2 TLV:
//   type 1 SUSPECT_NODES : [type<<4 | N][N × node_id(1B)]              — applied by the receiver as SUSPECT (level 1).
//   type 2 LIVENESS_STATE: [type<<4 | 2N][N × (node_id(1B), state(1B & 0x03))] — state 2=SILENT / 3=DEAD on the wire.
// The encoder emits type 2 iff any advertised peer is DEAD, else type 1 (a SILENT-only set downgrades to a SUSPECT id
// list — Lua-faithful). state in {1 SUSPECT, 2 SILENT, 3 DEAD}; SuspectEntry::state==0 means "healthy/absent".
struct SuspectEntry { uint8_t node_id; uint8_t state; };
size_t  pack_suspect_nodes_tlv(const uint8_t* ids, uint8_t n, std::span<uint8_t> out);          // type 1; bytes written, or 0 (n==0 -> 0)
size_t  pack_liveness_state_tlv(const SuspectEntry* e, uint8_t n, std::span<uint8_t> out);      // type 2; bytes written, or 0 (n==0 -> 0)
uint8_t parse_suspect_tlv(std::span<const uint8_t> ext, SuspectEntry* out, uint8_t max);        // scans BOTH type 1 (state=1) + type 2 (state from wire); entries found (0 if neither)

// -----------------------------------------------------------------------------
// CTS — clear-to-send (cmd-nibble 0x2, 3 B; +1 B if payload_len != 0) — ROADMAP §10.3
// -----------------------------------------------------------------------------
//   byte 0 : cmd=0x2(4 hi) | (sf-5)(3) | already_received(1)   [flags in the low nibble]
//   byte 1 : tx_id(8) — CTS sender (the forwarder clearing the requester)
//   byte 2 : rx_id(8) — intended requester id (the RTS sender being cleared)
//   byte 3 : payload_len(8) — OPTIONAL: the cleared DATA's inner+MAC length, for NAV. Present iff non-zero
//            (the sender adds it only when nav_enabled). A CTS-overhearer reads it to size an exact NAV
//            reservation for the upcoming DATA; absent (3-B CTS) => fall back to a max-size estimate.
// ctr_lo DROPPED vs the legacy CTS: tx_id+rx_id pin the flight under single-slot
// stop-and-wait, and tx_id (not ctr_lo) disambiguates cascade alts; tx_id also makes
// the CTS addressable/attributable on metal (no PHY-sender god-view). The Lua mirror is
// 4 B (literal 'C' tag); the cmd-nibble packs cmd+flags into byte 0. sf in 5..12;
// already_received short-circuits a resend whose ACK was lost.
struct cts_in  { uint8_t chosen_data_sf; bool already_received; uint8_t tx_id; uint8_t rx_id; uint8_t payload_len = 0; };
struct cts_out { uint8_t chosen_data_sf; bool already_received; uint8_t tx_id; uint8_t rx_id; uint8_t payload_len = 0; };
// Returns 3 (or 4 if in.payload_len != 0) on success; 0 on bad input (sf outside 5..12) or out span too small.
size_t pack_cts(const cts_in& in, std::span<uint8_t> out);
// nullopt on wrong cmd nibble or len not in {3,4}. payload_len = byte 3 (0 if a 3-B CTS).
std::optional<cts_out> parse_cts(std::span<const uint8_t> frame);

// -----------------------------------------------------------------------------
// ACK — (cmd-nibble 0x4, 3 B) — ROADMAP §10.3
// -----------------------------------------------------------------------------
//   byte 0 : cmd=0x4(4 hi) | ctr_lo(4 lo)
//   byte 1 : budget_hint(2 hi) | snr_bucket(2) | rsv(3) | AIRTIME_WARN(bit 0)
//   byte 2 : to(8)
// budget_hint / snr_bucket are the already-computed 2-bit fields; the
// snr_q4 -> bucket mapping is protocol-layer (wired when ACK is used at R3).
// warn (DM Inc 3): "you're near my airtime cap" — rides the byte1 rsv nibble, NO size change. The C++
// cmd-nibble has the room the Lua ACK lacked, so the Lua GREW to 4 B while this stays 3 B.
struct ack_in  { uint8_t ctr_lo; uint8_t budget_hint; uint8_t snr_bucket; uint8_t to; bool warn = false; };
struct ack_out { uint8_t ctr_lo; uint8_t budget_hint; uint8_t snr_bucket; uint8_t to; bool warn = false; };
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
//   bytes 7-8 : id_lo16 (BE)  — present iff (rts_flags & RTS_FLAG_M_BROADCAST) without FLOOD
// sf_index: 0..2 = singleton into allowed_data_sfs; 3 = ANY (receiver picks by SNR).
// Contract: all fields MASK/wrap (no clamp); payload_len wraps; parse rejects
// addr_len != 0 (hierarchy deferred).
//
// FLOOD RTS-M (channel-flood, 2026-06-08 redesign) — sets BOTH M_BROADCAST|FLOOD; 43 B total.
// The FLOOD tail REPLACES the 2-B id_lo16 tail (flood is checked first):
//   byte 2  : next = 0xFF  (broadcast convention; the `next` slot, set by the flood layer)
//   byte 4  : hop_left     (TTL safety cap; reuses the `dst` slot, decremented each forward)
//   bytes 7-10  : channel_msg_id (4 B, BIG-ENDIAN)            — IMMUTABLE
//   bytes 11-42 : coverage bitmap (32 B = 256 bits, bit i = node id i in this leaf) — MUTABLE
constexpr uint8_t RTS_FLAG_M_BROADCAST = 0x01;
constexpr uint8_t RTS_FLAG_RELAY       = 0x02;
constexpr uint8_t RTS_FLAG_FLOOD       = 0x04;   // channel flood: extended 43-B RTS-M tail (id + bitmap)
struct rts_in {
    uint8_t  leaf_id; uint8_t src; uint8_t next; uint8_t ctr_lo;
    uint8_t  dst; uint8_t sf_index; uint8_t rts_flags; uint8_t payload_len;
    uint16_t m_payload_id_lo16 = 0;        // appended (BE) iff M_BROADCAST and NOT FLOOD
    uint32_t flood_channel_msg_id = 0;     // FLOOD tail: channel_msg_id (BE, bytes 7-10)
    std::span<const uint8_t> flood_bitmap = {};   // FLOOD tail: exactly 32 B (bytes 11-42); pack->0 if FLOOD and size != 32
};
struct rts_out {
    uint8_t  leaf_id; uint8_t src; uint8_t next; uint8_t ctr_lo; uint8_t addr_len;
    uint8_t  dst; uint8_t sf_index; uint8_t rts_flags; uint8_t payload_len;
    bool     m_broadcast; uint16_t m_payload_id_lo16;
    bool     flood = false;                // rts_flags & RTS_FLAG_FLOOD (43-B tail present)
    uint32_t flood_channel_msg_id = 0;     // bytes 7-10 (BE) when flood
    size_t   flood_bitmap_off = 0;         // offset of the 32-B bitmap when flood (use rts_flood_bitmap)
};
size_t pack_rts(const rts_in& in, std::span<uint8_t> out);          // 7 / 9 / 43; 0 on short buf or FLOOD bitmap != 32 B
std::optional<rts_out> parse_rts(std::span<const uint8_t> frame);   // nullopt: len<7 (or <43 if FLOOD) / cmd / addr_len!=0
std::span<const uint8_t> rts_flood_bitmap(std::span<const uint8_t> frame, const rts_out& o);  // 32 B; empty unless flood

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
enum class q_opcode : uint8_t { req_sync = 1, config_pull = 2, channel_pull = 3 };   // R6.2: config_pull (2-bit opcode; 2 was free)
struct q_in {
    uint8_t leaf_id; uint8_t src; uint8_t dest; q_opcode opcode; bool mobile;
    std::span<const uint32_t> channel_ids;   // only for channel_pull; else empty
    uint16_t pull_lineage = 0;               // R6.2 config_pull: the lineage the puller wants
    uint16_t pull_epoch   = 0;               // R6.2 config_pull: the epoch the puller has (0 = fresh joiner)
};
struct q_out {
    uint8_t leaf_id; uint8_t src; uint8_t dest; uint8_t opcode; bool mobile;
    uint8_t channel_id_count;                // 0 unless channel_pull
    uint16_t pull_lineage;                    // R6.2: valid iff opcode==config_pull
    uint16_t pull_epoch;
};
size_t pack_q(const q_in& in, std::span<uint8_t> out);   // 4, or 5+4N for pull; 0 on short buf / N>255
std::optional<q_out> parse_q(std::span<const uint8_t> frame);
// i-th channel_msg_id (BE) of a CHANNEL_PULL frame; nullopt if index >= count.
std::optional<uint32_t> parse_q_channel_id(std::span<const uint8_t> frame,
                                           const q_out& q, uint8_t index);

// -----------------------------------------------------------------------------
// H — hash-locate flood (cmd-nibble 0x7, 8 B; 7 B legacy → soft) — ROADMAP §10.3 / §10.6
// -----------------------------------------------------------------------------
// §10.6 flag byte DROPPED (lossless: the flags nibble was hard-zero on pack and
// never read on parse; leaf_id relocates into the cmd byte). Forwardable TTL flood.
//   byte 0   : cmd=0x7(7..4) | leaf_id(3..0)
//   byte 1   : origin            (querier node_id; PRESERVED across forwards)
//   bytes 2-5: key_hash32 (LITTLE-ENDIAN)
//   byte 6   : ttl               (decremented per forward; 0 = drop)
//   byte 7   : H flags — bit 0 = HARD (skip the id_bind cache; resolve own-hash only -> reach the OWNER for
//              an authoritative correction; the verify-on-use escalation). soft (default) consults the cache.
enum HFlag : uint8_t { H_FLAG_HARD = 0x01,
                       H_FLAG_WANT_PUBKEY = 0x02 };   // E2E §6: request the owner's ed_pub (set WITH HARD; owner answers DATA TYPE 5)
// §2 mutual reqpubkey: when want_pubkey is set, the H frame APPENDS the requester's ed_pub[32] (so the owner caches
// the requester + can decrypt its future sealed DMs). requester_ed_pub is meaningful ONLY when want_pubkey.
struct h_in  { uint8_t leaf_id; uint8_t origin; uint32_t key_hash32; uint8_t ttl; bool hard = false; bool want_pubkey = false; uint8_t requester_ed_pub[32] = {}; };
struct h_out { uint8_t leaf_id; uint8_t origin; uint32_t key_hash32; uint8_t ttl; bool hard = false; bool want_pubkey = false; uint8_t requester_ed_pub[32] = {}; };
size_t pack_h(const h_in& in, std::span<uint8_t> out);            // 8, or 8+32=40 when want_pubkey; 0 on short buf
std::optional<h_out> parse_h(std::span<const uint8_t> frame);     // nullopt: len<7 / cmd / (want_pubkey && len<40); hard+flags from byte 7

// -----------------------------------------------------------------------------
// F — route-find RREQ/RREP flood (cmd-nibble 0x8, 7 B) — ROADMAP §10.3
// -----------------------------------------------------------------------------
//   byte 0 : cmd=0x8(7..4) | leaf_id(3..0)
//   byte 1 : origin              (querier node_id; PRESERVED across forwards)
//   byte 2 : is_reply(bit 7) | rsv(6..0)        [0 = RREQ, 1 = RREP]
//   byte 3 : dst_id
//   byte 4 : ttl_or_next_hop  — RAW dual byte: ttl (RREQ) | next_hop (RREP).
//            The codec surfaces it verbatim; the handler interprets by is_reply.
//            NEVER clamp/validate it (it's a node address when is_reply=1).
//   byte 5 : hops
//   byte 6 : relay  — immediate forwarder's node_id. Reverse/forward path learning
//            takes next_hop FROM THIS, not the PHY sender (metal has no src_hint);
//            every (re)transmitter stamps its own id. Deliberate divergence from the
//            Lua F wire (which used god-view meta.src) — metal-correct, decision (b).
// NB: is_reply is at bit 7 here (the Lua had it at byte2 bit 0) — re-placed, not bit-copied.
struct f_in  { uint8_t leaf_id; uint8_t origin; bool is_reply; uint8_t dst_id;
               uint8_t ttl_or_next_hop; uint8_t hops; uint8_t relay;
               uint16_t config_hash = 0; };   // R6.1 §6.4: the leaf fingerprint — handle_f gates a divergent F (flood-bypass closure)
struct f_out { uint8_t leaf_id; uint8_t origin; bool is_reply; uint8_t dst_id;
               uint8_t ttl_or_next_hop; uint8_t hops; uint8_t relay;
               uint16_t config_hash; };
size_t pack_f(const f_in& in, std::span<uint8_t> out);            // 9 (7 + config_hash u16); 0 on short buf
std::optional<f_out> parse_f(std::span<const uint8_t> frame);     // nullopt: len<9 / cmd

// -----------------------------------------------------------------------------
// J — join family (cmd-nibble 0x9) — ROADMAP §10.3. OTAA-style join + short-id
// lease. 4 opcodes, exact length per opcode. All multi-byte fields LITTLE-ENDIAN.
// -----------------------------------------------------------------------------
//   byte 0 : cmd=0x9(7..4) | leaf_id(3..0)
//   byte 1 : gateway_capable(bit 7) | is_mobile(bit 6) | opcode(bits 5..4) | rsv(3..0)
//            [reading A — RTS-byte5-consistent; §10.3 wording was ambiguous]
//   [opcode-specific body]
// Opcode values are NON-sequential: DISCOVER=0, CLAIM=1, DENY=2, OFFER=3.
// R6: JOIN will carry a 1-byte wire_version (wire-compat gate, NOT node version) — PORT_PLAN §9 / identity spec §5.
enum class j_opcode : uint8_t { discover = 0, claim = 1, deny = 2, offer = 3 };
constexpr uint8_t J_DENY_CONFLICT = 1, J_DENY_PENDING_CLAIM = 2, J_DENY_OWN_ID_DEFENSE = 3, J_DENY_MEDIATED = 4;  // 4 = third-party shared-neighbour heal (L2a)

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
    uint8_t  wire_version;                                                     // R6.2 §5.2 (byte-1 rsv nibble)
    uint32_t key_hash32;                                                       // DISCOVER, CLAIM
    uint8_t  responder_node_id; uint32_t responder_key_hash32; uint8_t data_sf_bitmap;  // OFFER
    uint8_t  proposed_node_id; uint16_t lease_age_seconds; uint8_t claim_epoch; uint8_t nonce;  // CLAIM
    uint8_t  denied_node_id; uint32_t owner_key_hash32; uint32_t claimant_key_hash32;
    uint16_t owner_lease_age_seconds; uint8_t owner_claim_epoch; uint8_t reason;            // DENY
};
std::optional<j_out> parse_j(std::span<const uint8_t> frame);

// -----------------------------------------------------------------------------
// DATA — data plane frame (cmd-nibble 0x3, 12+n B) — ROADMAP §10.3
// -----------------------------------------------------------------------------
//   byte 0     : cmd=0x3(7..4) | addr_len(3..1) | rsv(0)    [addr_len 0 this phase]
//   byte 1     : FLAGS (full byte — see DataFlag)
//   byte 2     : next (next-hop short-id)
//   byte 3     : dst  (final dest short-id; present because addr_len==0)
//   byte 4     : hops_remaining(7..3, 5-bit 0..31) | committed_hops(2..0, 3-bit 0..7)
//   byte 5     : prev_fwd_rt_hops (soft hop-gradient)
//   bytes 6-7  : ctr (16-bit, LITTLE-endian)
//   byte 8     : TYPE (present IFF flags & APP; enum DataType 1..255 — see DataType)
//   bytes 8/9..: inner  (OPAQUE ciphertext slot, n bytes — crypto is a behaviour layer)
//   last 4     : MAC    (OPAQUE 4-byte trailer)
// bytes 2-7 are the FIXED routing header relays read regardless of APP; the TYPE byte sits where the old
// inner[0] payload-flags byte was (promoted from the inner into the cleartext header, gated by APP).
// The NORMAL / hash-bind inner sub-layouts are exposed by SEPARATE optional helpers the
// behaviour layer calls — the mandatory parse keeps inner+MAC opaque (mirrors the BCN
// ext-block seam). The inner no longer carries a payload-flags byte: its flag-bits moved to byte 1
// (DataFlag) and its type-bits became the TYPE byte (DataType). ENDIANNESS: ctr is LITTLE-endian.
// (Channel messages have their OWN frame — the lean M frame, cmd 0xA — not a DATA inner.)

// NOTE: the C++ DATA frame DROPS the Lua's visited[6] (loop/dedup is done by _seen_origins +
// hops_remaining TTL, never a visited list) — a deliberate wire divergence from the frozen Lua
// (which keeps visited -> DATA_HDR_LEN 14). So the C++ header is 8 B; see docs/frames.md.
inline constexpr size_t DATA_HDR_LEN     = 8;
inline constexpr size_t DATA_MAC_LEN     = 4;
// byte-1 FLAGS (full byte): combinable modifiers. APP gates a TYPE byte at offset 8. CROSS_LAYER/CRYPTED/
// SOURCE_HASH are reserved (wire position locked, behaviour lands with the feature); DST_HASH/E2E_ACK_REQ
// are live; PRIORITY is decoded-only. The inner layout is read from these flags (no payload-flags byte).
enum DataFlag : uint8_t {
    DATA_FLAG_APP         = 0x80,    // a TYPE byte (DataType) follows the 8-B header
    DATA_FLAG_CROSS_LAYER = 0x40,    // reserved (R7: the inner carries a layer-path)
    DATA_FLAG_CRYPTED     = 0x20,    // reserved (E2E: the inner body is sealed)
    DATA_FLAG_E2E_ACK_REQ = 0x10,    // request an end-to-end ack
    DATA_FLAG_LOCATION    = 0x08,    // opt-in 6-B sender location in the sealed inner (after source_hash); set ONLY on origination
    DATA_FLAG_SOURCE_HASH = 0x04,    // LIVE: the inner carries the origin's key_hash32 (after origin) — the STABLE
                                     // sender identity (default-on for app DMs); the E2E-ack also reads it. Moves
                                     // into the CRYPTED-sealed region when E2E encryption lands.
    DATA_FLAG_DST_HASH    = 0x02,    // the inner carries the recipient's key_hash32 (L2c verify-on-delivery)
    DATA_FLAG_PRIORITY    = 0x01,    // decoded-only (no behaviour wired yet)
};

// Phase 1: the DATA trailer is CONDITIONAL on CRYPTED — a CRYPTED frame repurposes the 4-B MAC into an
// 8-B cleartext nonce-seed (rand8); non-CRYPTED keeps the 4-B(-zero) trailer (-> s18 byte-identical).
// Read from the cleartext byte-1 flags BEFORE the trailer, so a relay sizes the frame without decrypting.
inline constexpr size_t data_mac_len(uint8_t flags) { return (flags & DATA_FLAG_CRYPTED) ? 8 : 4; }

// byte-8 TYPE (enum, present IFF APP=1): mutually-exclusive message kinds. 0 is reserved/invalid (never on
// the wire — APP=0 means no TYPE byte). AUTHORITATIVE is folded into the H_ANSWER code (1 vs 2); E2E_IS_ACK
// became the E2E_ACK type. The H_ANSWER inner is cleartext so relays cache-on-pass.
enum DataType : uint8_t {
    DATA_TYPE_H_ANSWER               = 1,   // the inner is a hash-bind answer (cache key_hash32->node_id)
    DATA_TYPE_AUTHORITATIVE_H_ANSWER = 2,   // same inner; the answer is the owner's (authoritative)
    DATA_TYPE_E2E_ACK                = 3,   // normal-unicast inner; body = the acked ctr (2 B LE)
    DATA_TYPE_H_ANSWER_PUBKEY               = 4,   // E2E §6: RESERVED (overheard/soft pubkey answer) — NOT emitted in v1
    DATA_TYPE_AUTHORITATIVE_H_ANSWER_PUBKEY = 5,   // E2E §6: the owner's pubkey answer — inner [target_layer][node_id][ed_pub 32]
    DATA_TYPE_REMOTE_CMD             = 6,   // OTA remote diagnostics (2026-06-24): inner body = a console-style query keyword
    DATA_TYPE_REMOTE_RESP            = 7,   //   its response text. Plain inner, cleartext (honest-net diagnostic; E2E-seal is a later option).
    // (TYPE 6 was CONFIG_ANSWER, removed 2026-06-22 — leaf config now rides the C control frame cmd 0xB; reused here.)
};

struct data_in {
    uint8_t  addr_len;          // 0 this phase (pack returns 0 if != 0)
    uint8_t  flags;             // OR of DataFlag (full byte; APP is DERIVED from type by pack_data)
    uint8_t  type = 0;          // DataType (0 = normal DM, no APP byte); pack_data sets APP + emits it iff != 0
    uint8_t  next;
    uint8_t  dst;
    uint8_t  hops_remaining = 31;  // saturated 0..31; DEFAULT 31 = no TTL enforcement
                                   // (faithful to Lua pack_data 'hb.remaining or 31');
                                   // a 0 here is the wire code for TTL-exhausted -> drop.
    uint8_t  committed_hops;    // saturated 0..7
    uint8_t  prev_fwd_rt_hops;
    uint16_t ctr;               // packed LITTLE-endian
    std::span<const uint8_t> inner;    // opaque ciphertext slot (0..max)
    std::span<const uint8_t> mac;      // empty -> 4 zero bytes; else exactly 4 (else pack->0)
};
// Bytes written, or 0 on bad input (addr_len!=0 / mac size) or short out.
size_t pack_data(const data_in& in, std::span<uint8_t> out);

struct data_out {
    uint8_t  addr_len, flags;   // flags = the full byte-1 byte (OR of DataFlag)
    bool     app, cross_layer, crypted, e2e_ack_req, source_hash, dst_hash, priority;
    uint8_t  type;              // DataType (0 = no APP byte); read from byte 8 iff app
    bool     e2e_is_ack;        // DERIVED convenience: (type == DATA_TYPE_E2E_ACK)
    uint8_t  next, dst, hops_remaining, committed_hops, prev_fwd_rt_hops;
    uint16_t ctr;               // full 16-bit LE
    uint8_t  ctr_lo4;           // derived ctr & 0x0F (CTS/ACK/NACK hop-match convenience)
    size_t   inner_off, inner_len, mac_off, frame_len;
};
std::optional<data_out> parse_data(std::span<const uint8_t> frame);   // nullopt: len<12 / cmd / addr_len!=0 / APP w/o room for TYPE
std::span<const uint8_t> data_inner  (std::span<const uint8_t> frame, const data_out& d);  // opaque, inner_len B
std::span<const uint8_t> data_mac    (std::span<const uint8_t> frame, const data_out& d);  // 4 B (8 if CRYPTED)
// Phase 1: the 8-B cleartext nonce-seed (rand8) carried in the trailer of a CRYPTED DATA. Empty otherwise.
std::span<const uint8_t> data_nonce_seed(std::span<const uint8_t> frame, const data_out& d);

// Phase-1 E2E observability: the byte regions of a CRYPTED DATA inner, so the device console's decoded trace
// (frame_trace.h) can let an operator EYE-CONFIRM exactly which bytes are encrypted. All offsets are into the
// WHOLE frame. valid=false when `d` is not CRYPTED, or the inner is too short to hold [aad 4 + tag 16].
struct crypted_region {
    size_t aad_off = 0,  aad_len = 0;     // [dst_hash 4] — CLEARTEXT (the AEAD's authenticated data; §1c: origin SEALED)
    size_t ct_off = 0,   ct_len = 0;      // the sealed {origin+source_hash?+location?+body} = ciphertext (ENCRYPTED)
    size_t tag_off = 0,  tag_len = 0;     // 16-B Poly1305 tag (== dm_crypto DM_TAG_LEN)
    size_t seed_off = 0, seed_len = 0;    // 8-B cleartext nonce-seed (the conditional MAC trailer)
    bool   valid = false;
};
crypted_region data_crypted_region(const data_out& d);

// 6-byte location codec — 21-bit lat + 22-bit lon quantized from int32 deg×1e7 (~11 m, global).
// LOCATION-propagation spec 2026-06-14. step = 1024 e7-units; +512 cell-centring on decode. The
// DECODE MUST use int64 intermediates — u_lon<<10 reaches 3.6e9 > INT32_MAX.
size_t pack_loc6(int32_t lat_e7, int32_t lon_e7, std::span<uint8_t> out6);          // 6, or 0 on short buf
bool   unpack_loc6(std::span<const uint8_t> in6, int32_t& lat_e7, int32_t& lon_e7); // false if < 6 B

// OPTIONAL inner helpers (behaviour layer; the inner layout is read from the byte-1 FLAGS, not a payload byte).
// Unicast inner (the LOCKED order, spec §5): [dst_key_hash32 (4 B LE, iff DST_HASH)][layer-path (iff CROSS_LAYER:
// n_layers:1 | cur:1 | layer_ids: n_layers×1B FULL 8-bit ids)][origin][source_hash (4 B LE, iff SOURCE_HASH)][body].
// The caller passes the header flags so the helper knows which optional fields are present. `source_hash` = the
// origin's key_hash32 (the stable sender identity). The CROSS_LAYER layer-path is a PRESERVED full path with a
// cursor (`cur` indexes the NEXT layer to enter; gateways ADVANCE cur, never pop — §0.10).
struct data_unicast_inner { uint8_t origin; std::span<const uint8_t> body;
                            bool has_dst_hash = false;    uint32_t dst_key_hash32 = 0;
                            bool has_source_hash = false; uint32_t source_hash = 0;
                            bool has_location = false;    int32_t lat_e7 = 0, lon_e7 = 0;
                            bool has_cross_layer = false; uint8_t n_layers = 0, cur = 0;
                            uint8_t layer_ids[protocol::gw_env_max_hops] = {}; };
std::optional<data_unicast_inner> parse_unicast_inner(std::span<const uint8_t> inner, uint8_t flags);
// Pack the unicast inner in the LOCKED order above — the SINGLE source of byte ORDER (parse_unicast_inner reads the
// SAME layout). Presence is driven by `flags` (DST_HASH / CROSS_LAYER / SOURCE_HASH); layer_ids/n_layers/cur are used
// iff CROSS_LAYER. Returns bytes written, or 0 on overflow / an invalid path (caller FAILS LOUD — never truncates).
size_t pack_unicast_inner(std::span<uint8_t> out, uint8_t flags, uint32_t dst_key_hash32,
                          const uint8_t* layer_ids, uint8_t n_layers, uint8_t cur,
                          uint8_t origin, uint32_t source_hash, const uint8_t* body, uint8_t body_len,
                          int32_t lat_e7, int32_t lon_e7);   // lat/lon written iff flags & DATA_FLAG_LOCATION
// -----------------------------------------------------------------------------
// M — lean channel-message frame (cmd-nibble 0xA, 7+n B) — 2026-06-09 design.
// -----------------------------------------------------------------------------
// A purpose-built channel-message frame: drops the ~17 B of DM-only plumbing the old
// DATA+PAYLOAD_TYPE_M carried (next/dst/hops/ctr/visited/MAC), and rides leaf_id in
// byte-0's low nibble so the leak gate is the standard byte-0 leaf check (the cross-leaf
// leak fix). DELIBERATE divergence from the frozen Lua (which keeps channel-M on DATA).
//   byte 0   : cmd=0xA(7..4) | leaf_id(3..0)   — leaf_id = the leak gate
//   byte 1   : channel_id
//   byte 2   : flavor    (0=public plaintext, 1=group/encrypted, … — encryption deferred)
//   bytes 3-6: channel_msg_id (4 B, BIG-ENDIAN; origin = byte 3)
//   bytes 7..: payload   (by flavor; public = plaintext body)
inline constexpr size_t M_FRAME_HDR_LEN = 7;   // cmd|leaf + channel_id + flavor + channel_msg_id(4)
struct m_in  { uint8_t leaf_id; uint8_t channel_id; uint8_t flavor; uint32_t channel_msg_id;  // id BIG-endian on wire
               std::span<const uint8_t> body; };
struct m_out { uint8_t leaf_id; uint8_t channel_id; uint8_t flavor; uint32_t channel_msg_id;  // id BIG-endian
               std::span<const uint8_t> body; };
size_t pack_m(const m_in& in, std::span<uint8_t> out);          // 7 + body; 0 on short buf
std::optional<m_out> parse_m(std::span<const uint8_t> frame);   // nullopt: len < 7 / cmd != M

// Hash-bind answer inner (H §3.7a): [target_layer][node_id][key_hash32 LE] = 6 B. NO payload-flags byte —
// the frame TYPE (DATA_TYPE_H_ANSWER vs DATA_TYPE_AUTHORITATIVE_H_ANSWER) carries H_ANSWER + AUTHORITATIVE,
// so the caller types the frame from `authoritative` (and reads it back from the TYPE). CLEARTEXT (CRYPTED=0)
// so relays cache-on-pass. key_hash32 is LITTLE-endian (matches pack_h / the beacon). `authoritative` here is
// a caller convenience (set the frame type from it on pack; the parse leaves it default — the caller knows it
// from the frame TYPE).
struct hash_bind_inner { uint8_t target_layer; uint8_t node_id; uint32_t key_hash32; bool authoritative = false; };
size_t pack_hash_bind_inner(const hash_bind_inner& in, std::span<uint8_t> out);          // 6; 0 on short buf
std::optional<hash_bind_inner> parse_hash_bind_inner(std::span<const uint8_t> inner);    // nullopt: < 6 B

// Hash-bind PUBKEY answer inner (E2E §6, DATA TYPE 5): [target_layer 1][node_id 1][ed_pub 32] = 34 B. The key_hash32
// is DROPPED (== ed_pub[:4]; the cacher derives + verifies it). CLEARTEXT (CRYPTED=0) so relays cache-on-pass.
struct hash_bind_pubkey_inner { uint8_t target_layer; uint8_t node_id; uint8_t ed_pub[32]; };
size_t pack_hash_bind_pubkey_inner(const hash_bind_pubkey_inner& in, std::span<uint8_t> out);          // 34; 0 on short buf
std::optional<hash_bind_pubkey_inner> parse_hash_bind_pubkey_inner(std::span<const uint8_t> inner);    // nullopt: < 34 B

}  // namespace meshroute
