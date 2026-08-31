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
#include "dm_crypto.h"            // §hybrid-rts S1: RtsFlightIdentity (the POD only — this codec calls NO crypto)

namespace MESHROUTE_NS {

// -----------------------------------------------------------------------------
// BCN — periodic beacon frame (cmd-nibble 0x0) — ROADMAP §10.3
// -----------------------------------------------------------------------------
//   byte 0     : cmd=0x0(7..4) | leaf_id(3..0)        [short cmd code, never 'B']
//   byte 1     : src (8-bit node_id)
//   byte 2     : has_schedule(b7)|self_gateway(b6)|is_mobile(b5)|has_seen_bitmap(b4)|has_ext(b3)|n_entries_lo(b2..0)
//   byte 3     : n_entries_hi(b7..5) | heard_set_complete(b4) | wire_version(b3..0)   [n_entries = lo3 | hi3<<3; b4 = bidi heard-set authoritative-complete; b3..0 carry wire_version — see pack/unpack_beacon, NOT spare]
//   bytes 4-7  : key_hash32 (LITTLE-ENDIAN u32)
//   body: [schedule if has_schedule] -> n_entries x 4-B entry -> [32-B seen-bitmap] -> [ext_len + ext bytes]
//
// C5 scope: header + 4-B entries + schedule block + 32-B seen-bitmap implemented;
// the ext-TLV block is OPAQUE (ext_len + raw bytes) — the 4 TLV body codecs land
// with the channel/gateway/liveness iterations. n_entries kept (§10.6 drop deferred).

// Route entry (4 B): dest | next | score_bucket(4 hi)|degraded(b3)|rsv(b2..1)|is_gateway(b0) | hops(full byte).
struct beacon_entry {
    uint8_t dest;
    uint8_t next;
    uint8_t score_bucket;   // 4-bit
    bool    is_gateway;
    uint8_t hops;           // full byte (1..255)
    bool    degraded = false;   // WIRE = byte-2 b3 (one of the free rsv bits b3..1); one-way / transitively-bad next-hop
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
    bool     heard_set_complete = false;   // WIRE = byte-3 b4 ONLY: all hops==1 entries present this beacon (bidi census authoritative)
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
    bool     heard_set_complete = false;   // WIRE = byte-3 b4 ONLY (independent of wire_version b3..0)
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
size_t  pack_team_id_tlv(uint32_t team_id, std::span<uint8_t> out);    // §mobile 6.2: type-5 [(<<4)|4][team_id 4B LE] = 5 B; 0 on short buf
uint32_t parse_team_id_tlv(std::span<const uint8_t> ext);             // §mobile 6.2: the team_id from a type-5 TLV, or 0 (absent)

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
// CTS — clear-to-send (cmd-nibble 0x2) — ROADMAP §10.3
//   ORDINARY  (already_received = 0): 3 B, or 4 B with the NAV hint — UNCHANGED, byte-identical
//   TERMINAL  (already_received = 1): 6 B plaintext / 7 B crypted — §hybrid-rts S1, see below
// -----------------------------------------------------------------------------
//   byte 0 : cmd=0x2(4 hi) | (sf-5)(3) | already_received(1)   [flags in the low nibble]
//            ⚠ on the TERMINAL shape the (sf-5) 3 bits are NOT an SF — see §hybrid-rts below
//   byte 1 : tx_id(8) — CTS sender (the forwarder clearing the requester)
//   byte 2 : rx_id(8) — intended requester id (the RTS sender being cleared)
//   byte 3 : len6(7..2) | cr2(1..0) — OPTIONAL NAV hint. Present iff non-zero (the CTS sender adds it only
//            when nav_enabled). len6 = ceil(inner+MAC / CTS_LEN_QUANTUM), CLAMPED to CTS_LEN6_MAX; cr2 =
//            rts_cr_encode(the cleared DATA sender's CR). A CTS-overhearer reads BOTH to size the NAV
//            reservation for the upcoming DATA; absent (3-B CTS) => fall back to a max-size estimate at
//            OUR CR. See §cts-len6-cr2 below and docs/superpowers/specs/2026-07-27-cts-len6-cr2-design.md.
// ctr_lo DROPPED vs the legacy CTS: tx_id+rx_id pin the flight under single-slot
// stop-and-wait, and tx_id (not ctr_lo) disambiguates cascade alts; tx_id also makes
// the CTS addressable/attributable on metal (no PHY-sender god-view). The Lua mirror is
// 4 B (literal 'C' tag); the cmd-nibble packs cmd+flags into byte 0. sf in 5..12.
//
// ★★★ §hybrid-rts S1 (2026-08-08) — THE TERMINAL CTS SHAPE. **CODEC ONLY IN S1: STILL NO PRODUCER.**
// Design §2.3; plan S1 item 5. Owner-ruled core = the 10/11-B RTS; this conditional echo is the QA safety
// amendment that makes a terminal answer bindable to the flight that earned it.
//   TERMINAL layout (already_received = 1):
//     byte 0 : cmd=0x2(4 hi) | plane(bit 3) | rsv(bits 2..1, MUST be 0) | already_received=1(bit 0)
//     byte 1 : tx_id · byte 2 : rx_id
//     bytes 3.. : the FULL flight identity — 3 B plaintext (=> 6 B frame) / 4 B crypted (=> 7 B frame)
//   ⛔ NO NAV byte: a terminal CTS authorizes no DATA, so there is nothing to reserve for. `pack_cts`
//      REFUSES a terminal `cts_in` that carries `payload_len != 0` rather than silently dropping the hint.
//   ⛔ NO chosen SF: nothing follows, so the 3 bits that carry (sf-5) on the ordinary shape are reused for
//      one team/static PLANE bit + two RESERVED-ZERO bits. `parse_cts` sets `chosen_data_sf = 0` on this
//      shape and REJECTS a non-canonical reserved pair — the sender must ignore SF here (design §2.3).
//   ★ FRAME LENGTH IS THE DOMAIN DISCRIMINATOR, exactly as on the RTS: 6 B means plaintext, 7 B crypted.
//     No new flag bit is consumed anywhere.
//   ★ WHY THE ECHO IS MANDATORY AND NOT AN OPTIMISATION: a CTS is retry/duty-stash eligible and that stash
//     has NO flight-generation guard, so a delayed terminal response to flight A can land while flight B to
//     the same next hop is awaiting CTS. Without the echo it silently clears B — the same silent-loss class
//     [[B153]] was opened for, one layer down. A shorter 8/16-bit tag is REFUSED: it would reintroduce a
//     probabilistic terminal decision after this design paid RTS bytes to remove precisely that.
//   ⛔ REJECT MATRIX (all four directions, `parse_cts`): a 3/4-B frame with the bit SET · a 6/7-B frame with
//      the bit CLEAR · non-zero reserved bits · any other length. And on `pack_cts`: a terminal `cts_in`
//      without a valid identity, or an ordinary one carrying one.
//   ⓘ S1 SCOPE: `already_received` still has **ZERO producers** in this firmware (see §B153 below); S3
//      restores the emitter and the sender-side echo comparison. An inbound terminal CTS is still honoured
//      at `handle_cts`, but S1 does NOT yet compare the echo — do not read this codec as the restored
//      optimisation.
//
// ⛔⛔ §B153 (2026-08-08) — `already_received` IS RESERVED AND IS **NEVER EMITTED** BY THIS FIRMWARE. The bit
// still exists in the codec (pack/parse round-trip it, and `handle_cts` still honours an inbound one) so the
// wire stays compatible and no `wire_version` bump is needed — but **no code path sets it any more**, and
// nothing may start doing so. It used to short-circuit a resend whose ACK was lost.
// ⚠ SUPERSEDED IN PART BY §hybrid-rts S1 ABOVE, and read the amendment before reusing the argument: the
//   information-theoretic proof below is about a **7-byte** RTS, and it is still exactly right about one. What
//   changed is the premise — the unicast RTS is now 10/11 B and CARRIES the canonical identity, so the
//   evidence the old frame lacked is present. The retirement of the *un-keyed* gate stands permanently; S3
//   restores an *identity-keyed* one. The bit is still un-emitted as of S1.
//
// ★★★ WHY IT WAS RETIRED — AN INFORMATION-THEORETIC ARGUMENT, NOT A TUNING CHOICE. **A 7-byte RTS cannot
// distinguish (a) a RETRY of message A from (b) the FIRST ATTEMPT of message B that happens to share the same
// `(hop src, dst, ctr_lo, payload_len)`. Those two RTS frames are BYTE-IDENTICAL.** ⇒ **no receiver algorithm
// can safely return a TERMINAL `already_received = true` from that RTS alone** — more receiver state or
// cleverer matching cannot recover information that is not in the frame. Answering it anyway made the sender
// drop a *different* message with no DATA, no emit and no `send_failed` ([[B153]], measured on `s27`: 5
// expectations off one lost frame; the pre-fix key aliased in 4 of 5 different-origin cases).
// ★ **THE PRINCIPLE, WHICH IS THE DURABLE PART: RTS AUTHORIZES RECEPTION; ONLY DATA PROVES MESSAGE IDENTITY.**
// A free receiver therefore ALWAYS creates a `PendingRx`, returns a normal CTS, and waits for the DATA — and
// the DATA-level dedup (`_seen_origins`, keyed on the FULL `(origin, dst, ctr)` or the whole 8-B nonce-seed,
// `node_mac_rx.cpp` `handle_data`) is the sole authority on "have I already had this message?". It has the
// evidence the RTS never had, and a 30 s TTL against the RTS gate's 10 s.
// ⓘ The trade, stated so nobody re-adds the bit as an "optimization": SUCCESSFUL traffic is UNCHANGED
// (RTS→CTS→DATA→ACK). The only extra cost is on lost-ACK recovery — one redundant DATA, and only after an ACK
// was really lost — instead of a wrong answer on every hop of every message.
//
// ★ §cts-len6-cr2 (2026-07-27) — byte 3 was a flat 8-bit payload_len; it now splits into a 6-bit QUANTIZED
// length + the 2-bit CR of the DATA being cleared. Motive: the overhearer's NAV must size a THIRD PARTY's
// DATA, and CR multiplies the payload-symbol count directly — a cr5 overhearer reserving for a cr8 peer
// UNDER-reserved by up to 1327 ms and released NAV mid-DATA, i.e. the exact hidden-terminal collision NAV
// exists to prevent. This is the ONLY wrong-CR site of its class that failed in the dangerous direction.
// ★ THE INVARIANT THE SCHEME RESTS ON: DECODE >= TRUE FRAME LENGTH, ALWAYS. Rounding UP is therefore
// load-bearing — it keeps every residual length error in the over-reserve (safe) direction. Swept
// exhaustively over cr 5..8 x payload_len 0..255 x APP on/off x CRYPTED on/off against a REAL pack_data
// frame in test_frame_codec.cpp ("§cts-len6-cr2 — THE INVARIANT").
// ★ AIRTIME-NEUTRAL, not a cost: `payload_len` counts inner+MAC only (node_mac.cpp, rts_in.payload_len), so
// the consumer must add just the cleartext header = DATA_HDR_LEN(8), +1 iff APP => DATA_HDR_MAX_LEN(9). The
// pre-slice consumer added a flat +13 and thus already over-reserved by 4-5 B; quantization spends exactly
// that existing fudge (new worst case 0-3 quantization + 0-1 header = 0-4 B over). Same margin, exact CR.
// ⚠ This spends the CTS's LAST byte of flexibility (byte 0's low nibble is full: 3 bits of sf + the
// load-bearing already_received). A further CTS field now means widening the frame. Accepted deliberately.
// ⚠ byte3 == 0 MUST keep meaning "no NAV hint". It cannot collide: pack emits byte 3 only when
// payload_len != 0, so len6 >= 1 after the clamp => byte3 >= 4. The CTS_LEN6_MAX clamp is LOAD-BEARING for
// that proof, NOT belt-and-braces — payload_len is an unauthenticated RTS wire byte, and an unclamped
// ceil(255/4) = 64 would shift out of the 6-bit field to byte3 == cr2, colliding with the sentinel at cr5.
inline constexpr uint8_t CTS_LEN_QUANTUM = 4;    // byte-3 len6 counts 4-B units of inner+MAC, ROUNDED UP
inline constexpr uint8_t CTS_LEN6_MAX    = 63;   // the 6-bit field ceiling (ceil(255/4)=64 would overflow it)
// The ONE conversion path for byte 3's length half (U2) — used by pack_cts and by the invariant sweep.
constexpr uint8_t cts_len6_encode(uint8_t payload_len) {
    const unsigned q = (static_cast<unsigned>(payload_len) + CTS_LEN_QUANTUM - 1u) / CTS_LEN_QUANTUM;   // ceil
    return static_cast<uint8_t>(q > CTS_LEN6_MAX ? CTS_LEN6_MAX : q);
}
// cr_adv on BOTH structs is the same 2-bit code the RTS carries — encode/decode with rts_cr_encode /
// rts_cr_decode (below; named for the RTS that introduced them, now shared by both wires). 0 == cr5, NOT
// "unknown": the ONE validity predicate for the whole byte-3 hint is `payload_len != 0`.
// §hybrid-rts S1 — byte-0 low-nibble bit meanings ON THE TERMINAL SHAPE ONLY (already_received = 1). On the
// ordinary shape those same three bits are (sf-5) and these masks must not be applied to it.
inline constexpr uint8_t CTS_TERM_PLANE_BIT = 0x08;   // 1 = TEAM plane, 0 = STATIC/GLOBAL (the WIRE-declared plane)
inline constexpr uint8_t CTS_TERM_RSV_MASK  = 0x06;   // MUST be zero — parse_cts rejects a non-canonical pair
struct cts_in  { uint8_t chosen_data_sf; bool already_received; uint8_t tx_id; uint8_t rx_id;
                 uint8_t payload_len = 0;   // EXACT inner+MAC (0 = emit a 3-B CTS, no hint); pack_cts quantizes it
                 uint8_t cr_adv = 0;        // §cts-len6-cr2: rts_cr_encode(the cleared DATA sender's CR)
                 // §hybrid-rts S1 fields APPENDED at struct END so every existing positional aggregate-init
                 // (cts_in{sf, ar, tx, rx, ...}) keeps its meaning and cannot silently shift.
                 RtsFlightIdentity id{};    // TERMINAL only: the echo. MUST be absent when already_received = 0
                 bool team_plane = false; };// TERMINAL only: byte-0 bit 3 (the wire-declared plane)
struct cts_out { uint8_t chosen_data_sf; bool already_received; uint8_t tx_id; uint8_t rx_id;
                 uint8_t payload_len = 0;   // ⚠ QUANTIZED-UP inner+MAC (multiple of CTS_LEN_QUANTUM), NOT the
                                            // exact value packed in — 0 = no hint. Never use it where an exact
                                            // byte count is required; it exists to size a NAV reservation.
                 uint8_t cr_adv = 0;        // §cts-len6-cr2: 2-bit CR code; MEANINGLESS unless payload_len != 0
                 RtsFlightIdentity id{};    // §hybrid-rts S1: the echo (width 0 on an ordinary CTS)
                 bool team_plane = false; };// §hybrid-rts S1: MEANINGLESS unless already_received (no such bit exists there)
// ORDINARY (already_received = 0): returns 3, or 4 if in.payload_len != 0 — byte-identical to pre-S1.
// TERMINAL (already_received = 1): returns terminal_cts_wire_len() = 6 plaintext / 7 crypted.
// 0 on: sf outside 5..12 (ordinary only) · out span too small · a terminal without a valid identity · a
// terminal carrying a NAV payload_len · an ordinary carrying an identity (§hybrid-rts cross-shape refusal).
size_t pack_cts(const cts_in& in, std::span<uint8_t> out);
// nullopt on wrong cmd nibble, a length not in {3,4,6,7}, or any §hybrid-rts cross-shape pairing (3/4 B with
// the terminal bit set, 6/7 B without it, non-canonical terminal reserved bits, truncated identity tail).
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
struct ack_in  { uint8_t ctr_lo; uint8_t budget_hint; uint8_t snr_bucket; uint8_t to; bool warn = false; bool mobile_to = false; };  // §mobile Slice 1: mobile_to (byte-1 b1) — the `to` is a mobile local-id
struct ack_out { uint8_t ctr_lo; uint8_t budget_hint; uint8_t snr_bucket; uint8_t to; bool warn = false; bool mobile_to = false; };
size_t pack_ack(const ack_in& in, std::span<uint8_t> out);
std::optional<ack_out> parse_ack(std::span<const uint8_t> frame);

// -----------------------------------------------------------------------------
// RTS — request-to-send (cmd-nibble 0x1) — ROADMAP §10.3
//   UNICAST DM : 10 B plaintext / 11 B crypted   (7-B base + the §hybrid-rts S1 identity tail)
//   M_BROADCAST:  9 B  (7-B base + id_lo16)      — UNCHANGED, byte-identical
//   FLOOD      : 43 B  (7-B base + id + bitmap)  — UNCHANGED, byte-identical
// ⓘ THE 9/43 LABELS ARE THE CODE'S AND THE DESIGN DOCS NOW AGREE WITH THEM. ⛔ An earlier revision of this
//   comment warned that "design §2 and plan S1.4 both state M 43 B / flood 9 B and they are SWAPPED" — that
//   WAS true and was FIXED on 2026-08-08: design §2's table and plan S1.4 now read M 9 B / flood 43 B.
//   The code is the authority either way: `pack_rts` computes `need = flood ? 43 : (m_bcast ? 9 : ...)`, and
//   the corpus measures 544 FLOOD frames at exactly 43 B against 304 M_BROADCAST at exactly 9 B.
// -----------------------------------------------------------------------------
//   byte 0 : cmd=0x1(7..4) | leaf_id(3..0)
//   byte 1 : src
//   byte 2 : next
//   byte 3 : ctr_lo(7..4) | addr_len(3..1) | cr_adv HIGH bit(0)  [addr_len 0=normal, 1=mobile-next; 2..7 hierarchy-deferred]
//   byte 4 : dst
//   byte 5 : sf_index(7..6) | rts_flags(5..2) | MOBILE(1) | cr_adv LOW bit(0)  [MOBILE b1: src is a mobile local-id §mobile Slice 1]
//            READING A (§10.3 wording is ambiguous; we pin flags to bits 5..2):
//            within byte 5, M_BROADCAST -> bit 2 (0x04), RELAY -> bit 3 (0x08).
//            rts_flags nibble values: 0x01=M_BROADCAST, 0x02=RELAY, 0x04=FLOOD, 0x08=E2E_ACK.
//   byte 6 : payload_len                                   [wraps mod-256 via uint8_t]
//   bytes 7-9  : ★ §hybrid-rts S1 UNICAST identity tail — `origin | ctr_hi | ctr_lo`  (=> 10-B frame)
//   bytes 7-10 : ★ §hybrid-rts S1 UNICAST identity tail — BLAKE2b-512(0xE1|seed8|ctr_hi|ctr_lo|dst)[:4]
//                (=> 11-B frame). MUTUALLY EXCLUSIVE with the plaintext tail; the FRAME LENGTH is the
//                canonical domain discriminator (design §2.4), so no new flag bit is spent.
//   bytes 7-8 : id_lo16 (BE)  — present iff (rts_flags & RTS_FLAG_M_BROADCAST) without FLOOD
// sf_index: 0..2 = singleton into allowed_data_sfs; 3 = ANY (receiver picks by SNR).
// cr_adv: the SENDER's coding rate, 2 bits split across the last two reserved bits (byte 3 b0 = HIGH,
// byte 5 b0 = LOW — MSB-first in wire order, matching the u16_be/u32_be idiom). See §rts-cr below.
// Contract: all fields MASK/wrap (no clamp); payload_len wraps; parse rejects
// addr_len > 1 (0=normal, 1=mobile-next §mobile Slice 1; 2..7 hierarchy-deferred).
//
// FLOOD RTS-M (channel-flood, 2026-06-08 redesign) — sets BOTH M_BROADCAST|FLOOD; 43 B total.
// The FLOOD tail REPLACES the 2-B id_lo16 tail (flood is checked first):
//   byte 2  : next = 0xFF  (broadcast convention; the `next` slot, set by the flood layer)
//   byte 4  : hop_left     (TTL safety cap; reuses the `dst` slot, decremented each forward)
//   bytes 7-10  : channel_msg_id (4 B, BIG-ENDIAN)            — IMMUTABLE
//   bytes 11-42 : coverage bitmap (32 B = 256 bits, bit i = node id i in this leaf) — MUTABLE
// ★★★ §hybrid-rts S1 (2026-08-08) — WHY THE UNICAST RTS GREW, AND WHAT MAY NOT BE INFERRED FROM IT.
// Design: docs/superpowers/specs/2026-08-08-hybrid-rts-flight-identity-design.md; the producer, the digest
// convention and the ONE comparator live in dm_crypto.h (`RtsFlightIdentity`) — this codec only PACKS bytes
// that are already computed and MUST NOT call crypto (design §3).
// ★ MEASURED MOTIVE: the 7-B frame identified a flight by `(immediate src, dst, ctr_lo[4], payload_len)`.
//   Corpus census over 4 137 completed-flight stores: 66 at-risk pairs, 4 real aliases (6.1 %), and **4 of 5
//   (80 %) in the gateway/different-origin design case** because peer counters are correlated. That is an
//   ordinary topology, not bad luck.
// ★ THE TAIL IS MANDATORY ON A UNICAST RTS AND FORBIDDEN ON M/FLOOD, and both codecs enforce the pairing
//   (`pack_rts` returns 0, `parse_rts` returns nullopt) — design §2.4's "reject every cross-shape pairing".
// ⛔ LEGACY 7-B UNICAST RTS IS REJECTED OUTRIGHT. There is deliberately NO ambiguous compatibility parser:
//   MeshRoute is not deployed (owner-confirmed), mixed old/new firmware is unsupported, and a length-guessing
//   parser is exactly how a domain discriminator stops discriminating. Reflash every node in a bench topology.
// ⛔ `payload_len` IS NOT PART OF IDENTITY and never was — it is a frame-consistency/NAV field. Do not add it
//   to any identity comparison "for extra safety": the comparison is the full width or nothing.
// ⓘ S1 SCOPE: the wire and the codec, plus the CTS-wait correction that the grown frame forces
//   (`start_rts_timeout`). The receiver does NOT yet store or validate the identity, and neither retired
//   optimisation is back — S2/S3/S4 do that. `parse_rts` merely EXPOSES `rts_out::id` to the receiver.
// ⓘ THE WIRE-DECLARED PLANE, for the S2 receiver that will consume it, stated here because this is where the
//   bits are: it is `(addr_len == 1 && mobile_src)` — receiver-INDEPENDENT and the only plane evidence an
//   overhearer has. It is NOT `team_addr_for_us(next, addr_len)`, which is receiver-relative ADDRESS admission
//   and is true/false about *this* node, not about the frame. S0 proved the four-row canonical mark matrix
//   across every reachable producer (there is exactly one: `Node::tx_rts_retry`).
constexpr uint8_t RTS_FLAG_M_BROADCAST = 0x01;
constexpr uint8_t RTS_FLAG_RELAY       = 0x02;
constexpr uint8_t RTS_FLAG_FLOOD       = 0x04;   // channel flood: extended 43-B RTS-M tail (id + bitmap)
constexpr uint8_t RTS_FLAG_E2E_ACK     = 0x08;   // originator hint: the pending DATA is a DATA_TYPE_E2E_ACK -> the 1st-hop backstop DROP is exempted (still OBSERVED). Anti-spoof: verified at DATA-time; a liar is flagged + the exemption revoked. 4th free bit of the rts_flags nibble; old nodes ignore it (no flag-day).

// §rts-cr (2026-07-27) — the SENDER advertises ITS OWN coding rate so the receiver can size the DATA-wait
// window for the frame that is actually coming. THE BUG THIS FIXES: start_pending_rx_expiry sized the wait
// with the RECEIVER's active_cr(); a gateway leaf running cr8 next to leaves at cr5 made those leaves arm
// ~746 ms for a ~975 ms frame (SF11/BW250k) and abandon it -> data_rx_timeout. "Different CR settings are
// compatible" is TRUE at the LoRa PHY (the explicit header carries CR and the radio auto-detects it) but was
// FALSE in this firmware's RX-window sizing.
// ENCODING: cr is 5..8 = exactly 4 values = exactly 2 bits, so the mapping is total and the round-trip exact.
// ★ THERE IS NO "NOT ADVERTISED" STATE, DELIBERATELY (owner ruling 2026-07-27). All-zero bits decode to cr5,
//   not to "unknown".
// ⚠ WHAT THAT ASSUMES, STATED SO THE NEXT READER DOES NOT HAVE TO REDERIVE IT: a UNIFORMLY-REFLASHED fleet.
//   An un-reflashed sender leaves both bits 0; on a fleet whose global cr is NOT 5 a new receiver would read
//   that as cr5 and UNDER-wait — the exact failure this slice removes, reintroduced by mixed firmware. The
//   enforcement lever exists and works: protocol::wire_version is checked at node_join.cpp:213 and a mismatch
//   refuses the join outright, so a bump WOULD make this airtight. It is DELIBERATELY NOT DONE HERE (C4): a
//   bump rewrites the BCN version nibble and every J frame, re-anchoring all 29 scenario streams and swamping
//   this slice's own s32-only delta. Deferred to a standalone slice; the project reflashes together meanwhile.
//   ✔ MEASURED MITIGATION, so the residual risk is smaller than the paragraph above alone implies: every board
//   env compiles `-DLORA_CR=5` (platformio.ini) and `radio_cr` defaults to 5, so an un-reflashed node is at cr5,
//   sends both bits 0, and a new receiver decodes cr5 — CORRECT. The hazard is reachable only after the fleet's
//   GLOBAL cr is moved off 5 (nv.cr / `cfg set`) while un-reflashed nodes remain. Do that only after the bump.
// Out-of-range cr (not 5..8) WRAPS per this codec's stated mask/wrap contract. Where that range IS enforced,
// measured 2026-07-27 by following every writer of the global `NodeConfig::radio_cr`:
//   ENFORCED, fail-loud — `cfg set cr` (firmware_config.cpp: refuses outside 5..8) · `gateway cr0=/cr1=`
//     (validate_gateway_layers -> GwValErr::bad_cr, checked BEFORE the NV write) · the build default -DLORA_CR=5.
//   ⚠ NOT enforced — the NV LOAD (fw_main.cpp: `cfg.radio_cr = nv.cr` verbatim, so a blob from older/other
//     firmware or a partial corruption installs any byte) · the simulator's map_cr · the public
//     Node::set_radio_cfg(). PRE-EXISTING and NOT worsened here: airtime_ms multiplies payload symbols by `cr`
//     directly, so an out-of-range global already corrupts every timeout on that node long before the wire —
//     this codec merely mask/wraps it into 2 bits. Out of scope for this slice; recorded, not fixed.
constexpr uint8_t rts_cr_encode(uint8_t cr)   { return static_cast<uint8_t>((cr - 5) & 0x03); }   // 5..8 -> 0..3
constexpr uint8_t rts_cr_decode(uint8_t code) { return static_cast<uint8_t>((code & 0x03) + 5); } // 0..3 -> 5..8

struct rts_in {
    uint8_t  leaf_id; uint8_t src; uint8_t next; uint8_t ctr_lo;
    uint8_t  dst; uint8_t sf_index; uint8_t rts_flags; uint8_t payload_len;
    uint16_t m_payload_id_lo16 = 0;        // appended (BE) iff M_BROADCAST and NOT FLOOD
    uint32_t flood_channel_msg_id = 0;     // FLOOD tail: channel_msg_id (BE, bytes 7-10)
    std::span<const uint8_t> flood_bitmap = {};   // FLOOD tail: exactly 32 B (bytes 11-42); pack->0 if FLOOD and size != 32
    // §mobile Slice 1 fields at struct END so existing positional aggregate-inits (rts_in{…,m_payload_id}) are unaffected:
    uint8_t  addr_len = 0;                 // 0=normal, 1=mobile-next (`next` is a local id); 2..7 reserved (hierarchy)
    bool     mobile_src = false;           // MOBILE mark — src is a mobile local-id / mobile-originated (byte-5 b1)
    uint8_t  cr_adv = 0;                   // §rts-cr: rts_cr_encode(sender's active_cr()); 0 == cr5 (NOT "unknown")
    // §hybrid-rts S1 — APPENDED at struct END for the same reason: a positional aggregate-init cannot shift
    // into it. MANDATORY for a unicast RTS (pack refuses without it), FORBIDDEN on M_BROADCAST/FLOOD.
    RtsFlightIdentity id{};
};
struct rts_out {
    uint8_t  leaf_id; uint8_t src; uint8_t next; uint8_t ctr_lo; uint8_t addr_len;
    bool     mobile_src = false;           // §mobile Slice 1: MOBILE mark (byte-5 b1) — src is a mobile local-id
    uint8_t  dst; uint8_t sf_index; uint8_t rts_flags; uint8_t payload_len;
    bool     m_broadcast; uint16_t m_payload_id_lo16;
    bool     flood = false;                // rts_flags & RTS_FLAG_FLOOD (43-B tail present)
    uint32_t flood_channel_msg_id = 0;     // bytes 7-10 (BE) when flood
    size_t   flood_bitmap_off = 0;         // offset of the 32-B bitmap when flood (use rts_flood_bitmap)
    uint8_t  cr_adv = 0;                   // §rts-cr: the sender's CR as a 2-bit code — decode with rts_cr_decode
    // §hybrid-rts S1: the parsed identity tail. width 0 == an M_BROADCAST/FLOOD frame (no tail exists);
    // a UNICAST frame always yields width 3 (plaintext, 10 B) or 4 (crypted, 11 B) or the parse failed.
    RtsFlightIdentity id{};
};
// ★★★ §hybrid-rts S2 (2026-08-08) — THE WIRE-DECLARED PLANE, AS A PURE FUNCTION OF THE FRAME'S OWN BITS.
// `(addr_len == 1 && mobile_src)` is what the SENDER declared this flight's addressing plane to be. It is
// RECEIVER-INDEPENDENT: an overhearer, the addressee and a third party all read the same answer, which is why
// it — and only it — may be STORED as the plane of a flight and ECHOED on a terminal CTS.
// ⛔ IT IS NOT `team_addr_for_us(next, addr_len)`. That predicate is receiver-relative ADDRESS ADMISSION ("this
//    frame names *me* through my team-local id"); it never consults `mobile_src` and says nothing about the
//    frame's plane. The two DISAGREE on a real frame: a host's `(1, 0)` last-mile to a hosted mobile satisfies
//    `team_addr_for_us` at any team member whose `_team_local_id` equals that mobile's local id (the §18 numeric
//    collision), while the wire says STATIC — and the wire is right. ⇒ S2 stores THIS, never whichever predicate
//    happened to match. ⛔ And never `is_team_peer(src)`: that is our own state, not the frame's declaration.
// ⓘ THE FOUR-ROW CANONICAL MATRIX (design §4.3, proven by §HYBRID-RTS-S0 across every reachable producer — there
//   is exactly one, `Node::tx_rts_retry`): (0,0) ordinary static/global · (0,1) registered-mobile-originated
//   static/global · (1,0) host-to-mobile last-mile static/global · (1,1) TEAM. The out-of-matrix `(1,1)`-without-
//   a-team-plane cell is UNREACHABLE BY CONFIG (hosted-mobile state and mobile-origin state are mutually
//   exclusive, including across the runtime role flip), which is what lets this be a FATAL validator.
constexpr bool rts_wire_team_plane(uint8_t addr_len, bool mobile_src) { return addr_len == 1 && mobile_src; }
constexpr bool rts_wire_team_plane(const rts_out& r) { return rts_wire_team_plane(r.addr_len, r.mobile_src); }
constexpr bool rts_wire_team_plane(const rts_in&  r) { return rts_wire_team_plane(r.addr_len, r.mobile_src); }

// ★ §hybrid-rts S1 — SEMANTIC wire lengths for the UNICAST DM exchange. Use these where the quantity meant is
// "the DM RTS I am actually about to send" / "the terminal CTS that might come back" — above all in
// `start_rts_timeout`. ⛔ DO NOT fold the global RTS_LEN/CTS_LEN constants into these: M_BROADCAST (9),
// FLOOD (43) and the Lua-parity timing constants mean DIFFERENT things and must stay separate (design §6).
constexpr size_t unicast_rts_wire_len(bool crypted) {
    return 7u + (crypted ? RTS_ID_CRYPTED_LEN : RTS_ID_PLAIN_LEN);      // 10 / 11
}
constexpr size_t terminal_cts_wire_len(bool crypted) {
    return 3u + (crypted ? RTS_ID_CRYPTED_LEN : RTS_ID_PLAIN_LEN);      // 6 / 7
}
size_t pack_rts(const rts_in& in, std::span<uint8_t> out);          // 10/11 unicast · 9 M · 43 FLOOD; 0 on short buf, FLOOD bitmap != 32 B, or an identity/kind mismatch
std::optional<rts_out> parse_rts(std::span<const uint8_t> frame);   // nullopt: cmd · addr_len>1 · len not EXACTLY 10/11 (unicast) / 9 (M) / 43 (FLOOD)
std::span<const uint8_t> rts_flood_bitmap(std::span<const uint8_t> frame, const rts_out& o);  // 32 B; empty unless flood

// -----------------------------------------------------------------------------
// NACK — (cmd-nibble 0x5, 4 B) — ROADMAP §10.3
// -----------------------------------------------------------------------------
//   byte 0 : cmd=0x5(7..4) | reason(3..0)   0 BUSY_RX, 1 BUDGET, 2 HOP_BUDGET, 3 LOOP_DUP
//   byte 1 : ctr_lo(7..4) | rsv(3..0)
//   byte 2 : payload   (reason-specific; encoded by the protocol layer, packed verbatim)
//   byte 3 : to
// payload is a u8 (0..255) so the Lua's clamp-to-[0,255] is satisfied by the type.
struct nack_in  { uint8_t reason; uint8_t ctr_lo; uint8_t payload; uint8_t to; bool mobile_to = false; };  // §mobile: mobile_to (byte-1 b0) — the `to` is a mobile/team LOCAL id (mirror the ACK's mobile_to). A colliding static id ignores it, the mobile accepts it.
struct nack_out { uint8_t reason; uint8_t ctr_lo; uint8_t payload; uint8_t to; bool mobile_to = false; };
size_t pack_nack(const nack_in& in, std::span<uint8_t> out);
std::optional<nack_out> parse_nack(std::span<const uint8_t> frame);

// -----------------------------------------------------------------------------
// Q — query (cmd-nibble 0x6, 4 B header + a per-opcode body) — ROADMAP §10.3
// -----------------------------------------------------------------------------
//   byte 0 : cmd=0x6(7..4) | leaf_id(3..0)
//   byte 1 : src         (§team-parity T4: on a TEAM_SYNC this is the sender's team_local_id, NOT its node_id)
//   byte 2 : dest        (0xFF = REQ_SYNC / TEAM_SYNC broadcast convention)
//   byte 3 : opcode(7..6) | mobile(bit 5) | rsv(4..0)
//   [CHANNEL_PULL only] byte 4: count ; then count × channel_msg_id (4 B BIG-ENDIAN)
//   [TEAM_SYNC only]    bytes 4..7: team_id (4 B LITTLE-ENDIAN — the same convention as the beacon type-5 TLV / H / F)
// channel_msg_id is BE (distinct from the LE key_hash32 elsewhere) — keep it BE.
// ★★ THE OPCODE FIELD IS **2 BITS** WIDE — packed `(op & 0x03) << 6`, parsed `(b3 >> 6) & 0x03` — so it holds EXACTLY
// four codepoints, and `team_sync = 0` takes the LAST free one (0 was the "unknown opcode -> silent" catch-all and was
// never emitted by any packer). ⚠ A FIFTH Q kind therefore needs a WIRE change (e.g. promoting a rsv bit 4..0 into an
// opcode extension), NOT another enumerator: `team_sync = 4` would pack as `4 & 0x03 == 0`, parse back as 0, and then
// never equal its own enumerator at any dispatch site — a silently DEAD feature that no build error would catch.
enum class q_opcode : uint8_t { team_sync = 0, req_sync = 1, config_pull = 2, channel_pull = 3 };   // R6.2: config_pull (2-bit opcode; 2 was free). §team-parity T4: team_sync = 0, the last free codepoint — the field is now FULL
struct q_in {
    uint8_t leaf_id; uint8_t src; uint8_t dest; q_opcode opcode; bool mobile;
    std::span<const uint32_t> channel_ids;   // only for channel_pull; else empty
    uint16_t pull_lineage = 0;               // R6.2 config_pull: the lineage the puller wants
    uint16_t pull_epoch   = 0;               // R6.2 config_pull: the epoch the puller has (0 = fresh joiner)
    uint32_t team_id      = 0;               // §team-parity T4 team_sync: the team scope. APPENDED LAST on purpose — the
                                             // existing aggregate initialisers (`q_in{leaf, src, dest, op, mob, {}}`)
                                             // must keep compiling unchanged. 0 on a team_sync => pack_q REFUSES (C2).
};
struct q_out {
    uint8_t leaf_id; uint8_t src; uint8_t dest; uint8_t opcode; bool mobile;
    uint8_t channel_id_count;                // 0 unless channel_pull
    uint16_t pull_lineage;                    // R6.2: valid iff opcode==config_pull
    uint16_t pull_epoch;
    uint32_t team_id;                         // §team-parity T4: valid (and guaranteed non-zero) iff opcode==team_sync
};
size_t pack_q(const q_in& in, std::span<uint8_t> out);   // 4, or 5+4N for pull, or 8 for team_sync; 0 on short buf / N>255 / a team_sync with team_id==0
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
//   bytes 2-5: query_key32 (LITTLE-ENDIAN) — a key_hash32, OR a zero-extended id when BY_ID (see below)
//   byte 6   : ttl               (decremented per forward; 0 = drop)
//   byte 7   : H flags — bit 0 = HARD (skip the id_bind cache; resolve own-hash only -> reach the OWNER for
//              an authoritative correction; the verify-on-use escalation). soft (default) consults the cache.
enum HFlag : uint8_t { H_FLAG_HARD = 0x01,
                       H_FLAG_WANT_PUBKEY = 0x02,     // E2E §6: request the owner's ed_pub (set WITH HARD; owner answers DATA_TYPE_AUTHORITATIVE_H_ANSWER_PUBKEY)
                       H_FLAG_TEAM = 0x04,            // §mobile-team: a teammate's locate -> appends team_id (4 B); a registered mobile answers ONLY a same-team query (else the home proxies)
                       H_FLAG_MOBILE_REQ = 0x08,      // §mobile: the requester (origin) is a MOBILE/team member -> its origin is a LOCAL id, not a global identity. The owner-answerer MUST NOT id_bind it (a WANT_PUBKEY seal-back caches by hash + routes via home/_rt_team, never the static id-plane). Backward-compat rsv bit (old senders 0, old receivers ignore).
                       H_FLAG_BY_ID = 0x10 };         // ★ §id-hash S4a (spec 2026-08-01 §4): bytes 2-5 are a ZERO-EXTENDED id, not a hash — "who owns id N?". 0x20/0x40/0x80 remain free.
// ★★ §id-hash S4a — THE BY_ID QUERY KEY IS CANONICAL, and both directions enforce it.
// `H_FLAG_BY_ID` reuses bytes 2-5 (0 extra wire bytes) to carry an id. Without a canonical rule the same id has
// 2^24 spellings, each occupying a DIFFERENT dedup slot in HashQuerySeen -> redundant floods for one question and
// telemetry that cannot be correlated. So: bytes 3..5 MUST be zero and ids 0 (unprovisioned) / 255 (reserved,
// `peer_book_by_id`) are refused. ONE predicate, used by pack AND parse AND the originator, so the three cannot
// drift apart (U1).
// ⓘ `by_id` ALSO joins the dedup key (Node::HashQuerySeen) — without it `H(id 114)` and `H(hash 0x00000072)`
//   alias, and one silently suppresses the other's forward.
inline constexpr bool h_by_id_key_canonical(uint32_t query_key32) { return query_key32 >= 1u && query_key32 <= 254u; }
// §2 mutual reqpubkey: when want_pubkey is set, the H frame APPENDS the requester's ed_pub[32] (so the owner caches
// the requester + can decrypt its future sealed DMs). requester_ed_pub is meaningful ONLY when want_pubkey.
// §name: WITH the pubkey, a WANT_PUBKEY H also appends the requester's [name_len u8][name…] (iff name_len>0), AFTER the
// team_id — so the owner caches hash->name too, symmetric to the AUTHORITATIVE_H_ANSWER_PUBKEY / MOBILE_PUBKEY_PUSH / MOBILE_H_ANSWER_PUBKEY ANSWER frames. Optional/trailing:
// an old WANT_PUBKEY H (or one with no name) carries none -> name_len parses 0.
// ★ §id-hash S4a — HONEST IN-MEMORY NAMING (spec §4). The field is `query_key32` because bytes 2-5 hold two
// different things; `query_hash()` / `query_id()` name which one, and each answers 0 in the wrong mode — 0 is the
// "no hash" / "unprovisioned id" sentinel everywhere in this codebase, so a mis-read can never name a real target.
// ⚠ The `f_in.ttl_or_next_hop` precedent (:399-401) licenses OVERLOADED WIRE STORAGE; it does not license a struct
// field called `key_hash32` that holds an id. `by_id` is APPENDED LAST so the positional aggregate initialisers
// (`pack_h({leaf, origin, key, ttl, hard}, …)`) do not shift.
struct h_in  { uint8_t leaf_id; uint8_t origin; uint32_t query_key32; uint8_t ttl; bool hard = false; bool want_pubkey = false; uint8_t requester_ed_pub[32] = {};
               bool team_scoped = false; uint32_t team_id = 0; bool mobile_req = false; uint8_t name[32] = {}; uint8_t name_len = 0;   // §mobile-team: appended team_id (4 B) iff team_scoped; mobile_req = origin is a LOCAL id; §name: [name_len][name] iff want_pubkey&&name_len>0
               bool by_id = false;                                                        // §id-hash S4a: bytes 2-5 are a zero-extended id ("who owns id N?")
               uint32_t query_hash() const { return by_id ? 0u : query_key32; }
               uint8_t  query_id()   const { return by_id ? static_cast<uint8_t>(query_key32) : uint8_t(0); } };
struct h_out { uint8_t leaf_id; uint8_t origin; uint32_t query_key32; uint8_t ttl; bool hard = false; bool want_pubkey = false; uint8_t requester_ed_pub[32] = {};
               bool team_scoped = false; uint32_t team_id = 0; bool mobile_req = false; uint8_t name[32] = {}; uint8_t name_len = 0;
               bool by_id = false;
               uint32_t query_hash() const { return by_id ? 0u : query_key32; }
               uint8_t  query_id()   const { return by_id ? static_cast<uint8_t>(query_key32) : uint8_t(0); } };
size_t pack_h(const h_in& in, std::span<uint8_t> out);            // 8; +32 want_pubkey; +4 team_scoped; +1+name_len when want_pubkey&&name_len>0; 0 on short buf / non-canonical by_id / by_id+want_pubkey
std::optional<h_out> parse_h(std::span<const uint8_t> frame);     // nullopt: len<7 / cmd / (want_pubkey && len<40) / bad-name-len / non-canonical by_id / by_id+want_pubkey; hard+flags from byte 7

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
               uint16_t config_hash = 0;      // R6.1 §6.4: the leaf fingerprint — handle_f gates a divergent F (flood-bypass closure)
               bool team_scoped = false; uint32_t team_id = 0; };   // §team-multihop (spec §5): TEAM-plane F — byte-2 b6 = TEAM, team_id appended (4 B) at offset 9 after config_hash. Static F (team_scoped=false) = byte-identical.
struct f_out { uint8_t leaf_id; uint8_t origin; bool is_reply; uint8_t dst_id;
               uint8_t ttl_or_next_hop; uint8_t hops; uint8_t relay;
               uint16_t config_hash;
               bool team_scoped = false; uint32_t team_id = 0; };   // §team-multihop: parse mirrors pack — team_id read only when byte-2 b6 set
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

// DISCOVER (6 B static/legacy · 9 B mobile · 13 B re-home): key_hash32(LE) [+ §S6 last_home_id/layer/reg_epoch iff is_mobile
// [+ §B4 last_home_key_hash32(LE) iff last_home_id != 0]]. The +3-B last-home block feeds the new-home→old-home notify (D10);
// the +4-B hash (only on a re-home) lets the NEW home address a CROSS-LAYER breadcrumb to the old home BY HASH (§B4).
// Conditional-append (mirrors j_offer's target_key_hash32): a 6-B DISCOVER = static, a 9-B = a FRESH mobile (byte-identical).
struct j_discover_in { uint8_t leaf_id; bool gateway_capable; bool is_mobile; uint32_t key_hash32;
                       uint8_t last_home_id = 0; uint8_t last_home_layer = 0; uint8_t last_reg_epoch = 0; uint32_t last_home_key_hash32 = 0; };  // §S6/B4: appended iff is_mobile (9-B) [+ hash iff last_home_id != 0 (13-B)]; at struct END for positional aggregate-inits
// OFFER (8 B static / 13 B mobile): responder_node_id, responder_key_hash32(LE), data_sf_bitmap
//              [+ proposed_mobile_id, target_key_hash32(LE) iff is_mobile].
// data_sf_bitmap is ADVISORY since 2026-07-19 (F-SF-1): the mobile KEEPS its own configured sf_list across
// registration (sf_list is node config; the per-exchange RTS carries only an INDEX into the agreed set), so the
// adopt path no longer consumes this byte — it rides purely as an operator-misconfig diagnostic (a mobile whose
// configured low byte != this offered byte emits `mobile_sf_list_mismatch`). Its low-byte SF>=8 truncation is
// therefore harmless. Field kept on the wire for compat (no wire change).
struct j_offer_in    { uint8_t leaf_id; bool gateway_capable; bool is_mobile;
                       uint8_t responder_node_id; uint32_t responder_key_hash32; uint8_t data_sf_bitmap;
                       uint8_t proposed_mobile_id = 0; uint32_t target_key_hash32 = 0; };   // §mobile 2a: proposed_mobile_id (host-assigned LOCAL id) + §S6 target_key_hash32 (the mobile this OFFER is FOR) — appended iff is_mobile (13-B frame); at struct END to preserve positional aggregate-inits
// CLAIM (11 B): key_hash32(LE), proposed_node_id, lease_age_seconds(u16 LE), claim_epoch, nonce.
struct j_claim_in    { uint8_t leaf_id; bool gateway_capable; bool is_mobile; uint32_t key_hash32;
                       uint8_t proposed_node_id; uint16_t lease_age_seconds; uint8_t claim_epoch; uint8_t nonce;
                       uint8_t chosen_host_id = 0; };   // §mobile: reuses the byte-10 NONCE slot iff is_mobile (nonce is dead on the mobile path); at struct END to preserve positional aggregate-inits
// DENY (15 B static · 19 B team-scoped): denied_node_id, owner_key_hash32(LE), claimant_key_hash32(LE),
//              owner_lease_age_seconds(u16 LE), owner_claim_epoch, reason [+ §W2c team_id(LE) iff team_scoped].
// §W2c team-DAD L2a mediation: a team-plane mediated DENY (reason J_DENY_MEDIATED sent by a shared-neighbour
// observer) appends the 4-B team_id at bytes 15..18 -> 19-B frame. team_scoped is signalled purely by length
// (19 vs 15), exactly the J-family conditional-append idiom (DISCOVER 6/9/13, OFFER 8/13). A STATIC DENY stays
// 15 B -> byte-identical; the team_id lets a receiver require its own team (§18: a static/other-team drops it).
struct j_deny_in     { uint8_t leaf_id; bool gateway_capable; bool is_mobile; uint8_t denied_node_id;
                       uint32_t owner_key_hash32; uint32_t claimant_key_hash32;
                       uint16_t owner_lease_age_seconds; uint8_t owner_claim_epoch; uint8_t reason;
                       bool team_scoped = false; uint32_t team_id = 0; };   // §W2c: team-mediated DENY carries team_id (19-B); static DENY leaves these default (15-B, unchanged)
size_t pack_j_discover(const j_discover_in& in, std::span<uint8_t> out);   // 6 static/legacy; 9 if is_mobile (§S6 last-home block); 13 on a re-home (§B4 + old-home hash)
size_t pack_j_offer   (const j_offer_in&    in, std::span<uint8_t> out);   // 8 / 13 if is_mobile (§mobile 2a + §S6 target hash)
size_t pack_j_claim   (const j_claim_in&    in, std::span<uint8_t> out);   // 11
size_t pack_j_deny    (const j_deny_in&     in, std::span<uint8_t> out);   // 15; +4 team_id when team_scoped (19, §W2c)

// One parse returns opcode + the superset of fields (only the opcode's are
// meaningful — mirrors the Lua parse_j single-table). nullopt on wrong cmd or
// wrong EXACT length for the opcode.
struct j_out {
    uint8_t  leaf_id; bool gateway_capable; bool is_mobile; uint8_t opcode;
    uint8_t  wire_version;                                                     // R6.2 §5.2 (byte-1 rsv nibble)
    uint32_t key_hash32;                                                       // DISCOVER, CLAIM
    uint8_t  last_home_id = 0; uint8_t last_home_layer = 0; uint8_t last_reg_epoch = 0;  // DISCOVER §S6: valid iff is_mobile && >=9-B frame (0 = fresh)
    uint32_t last_home_key_hash32 = 0;                                         // DISCOVER §B4: the old home's hash (13-B re-home frame only; 0 = fresh / not carried) -> the new home addresses a CROSS-LAYER breadcrumb by hash
    uint8_t  responder_node_id; uint32_t responder_key_hash32; uint8_t data_sf_bitmap;  // OFFER (data_sf_bitmap advisory since F-SF-1; see the pack-struct comment)
    uint8_t  proposed_mobile_id = 0; uint32_t target_key_hash32 = 0;           // OFFER §mobile 2a/S6: valid iff opcode==OFFER && is_mobile (13-B): host-assigned id + the target mobile's hash
    uint8_t  proposed_node_id; uint16_t lease_age_seconds; uint8_t claim_epoch; uint8_t nonce;  // CLAIM
    uint8_t  chosen_host_id = 0;                                               // CLAIM §mobile: byte-10 read here too (a mobile CLAIM addresses its chosen host; static reads nonce)
    uint8_t  denied_node_id; uint32_t owner_key_hash32; uint32_t claimant_key_hash32;
    uint16_t owner_lease_age_seconds; uint8_t owner_claim_epoch; uint8_t reason;            // DENY
    bool     team_scoped = false; uint32_t team_id = 0;                                     // DENY §W2c: set iff a 19-B team-mediated DENY (team_id at bytes 15..18)
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
//   byte 8     : TYPE (present IFF flags & APP; enum DataType — a RANGE contract, see DataType)
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
// ★ §cts-len6-cr2: the WIDEST cleartext DATA header ahead of the inner+MAC that `payload_len` counts —
// the 8-B fixed header plus the TYPE byte emitted iff DATA_FLAG_APP (pack_data, frame_codec.cpp). So an
// on-air DATA is EXACTLY payload_len + DATA_HDR_LEN, or + DATA_HDR_MAX_LEN when APP is set. Anything sizing
// a reservation for a frame whose flags it cannot see must use the MAX — over-reserve, never under.
inline constexpr size_t DATA_HDR_MAX_LEN = DATA_HDR_LEN + 1;   // 9
inline constexpr size_t DATA_MAC_LEN     = 4;
// byte-1 FLAGS (full byte): combinable modifiers. APP gates a TYPE byte at offset 8. CROSS_LAYER (inner
// layer-path), CRYPTED (sealed inner), E2E_ACK_REQ, LOCATION, SOURCE_HASH, DST_HASH are all LIVE; so is 0x01,
// but as DATA_FLAG_MS_ENCLOSED_TYPE, NOT as a priority (see the alias below). The inner layout is read from
// these flags (no payload-flags byte).
enum DataFlag : uint8_t {
    DATA_FLAG_APP         = 0x80,    // a TYPE byte (DataType) follows the 8-B header
    DATA_FLAG_CROSS_LAYER = 0x40,    // LIVE: the inner carries a cross-layer layer-path (full-byte ids, between dst_hash and origin)
    DATA_FLAG_CRYPTED     = 0x20,    // LIVE: origin + everything after it sealed (XChaCha20-Poly1305); trailer grows to the 8-B nonce-seed
    DATA_FLAG_E2E_ACK_REQ = 0x10,    // request an end-to-end ack
    DATA_FLAG_LOCATION    = 0x08,    // 6-B sender location (after source_hash); set ONLY on origination. ★ §loc-per-send
                                     // (2026-07-31): PER-SEND (console `-l`), never a config toggle, and GUARANTEED to sit
                                     // in the SEALED inner — enqueue_data REFUSES the send outright if the DM would not be
                                     // CRYPTED. The old wording said "in the sealed inner" as a description while the gate
                                     // had no crypt check at all, so a plaintext DM aired the position in clear (register
                                     // B0). It is now a guarantee the code enforces, not a claim about intent.
    DATA_FLAG_SOURCE_HASH = 0x04,    // LIVE: the inner carries the origin's key_hash32 (after origin) — the STABLE
                                     // sender identity (default-on for app DMs); the E2E-ack also reads it. Sealed
                                     // under CRYPTED.
    DATA_FLAG_DST_HASH    = 0x02,    // the inner carries the recipient's key_hash32 (L2c verify-on-delivery)
    DATA_FLAG_PRIORITY    = 0x01,    // ⛔ THE NAME IS HISTORICAL AND THE BIT IS LIVE — see the alias below.
                                     // CORRECTED 2026-08-29 (A0-F9): this said "decoded-only (no behaviour wired
                                     // yet)" while THREE origination paths set it (node_channel.cpp:811,
                                     // node_hashlocate.cpp:1742, :1754) and TWO receive paths branch on it
                                     // (node_mac_rx.cpp:1581, :1638). ⇒ no PRIORITY semantics are wired, and
                                     // this is NOT a spare codepoint.
    // §S2 same-layer delegated INTRO discriminator (spec §3b). The XL MOBILE_SEND wrapper always carries a 1-B
    // enclosed-type body prefix (keyed off has_cross_layer); the SAME-LAYER wrapper carries NONE (byte-identity,
    // S1). When a same-layer wrapper must carry an enclosed type (a delegated INTRO), it borrows the 0x01 bit as
    // "enclosed-type byte present": wrapper body = [enclosed_type:1][payload]. The home strips both on unwrap.
    // ⛔ CORRECTED 2026-08-29 (A0-F9): this used to justify the borrow as "decode-only, NEVER set by any
    // origination path" — already false when written, since THIS mechanism is one of the three origination paths
    // that set it. The byte-identity claim survives (a plain type-0 wrapper still leaves the bit clear); the
    // "spare bit" claim does not, and must not be offered again as free space.
    DATA_FLAG_MS_ENCLOSED_TYPE = DATA_FLAG_PRIORITY,   // alias (0x01): same-layer MOBILE_SEND wrapper enclosed-type marker
};

// Phase 1: the DATA trailer is CONDITIONAL on CRYPTED — a CRYPTED frame repurposes the 4-B MAC into an
// 8-B cleartext nonce-seed (rand8); non-CRYPTED keeps the 4-B(-zero) trailer (-> s18 byte-identical).
// Read from the cleartext byte-1 flags BEFORE the trailer, so a relay sizes the frame without decrypting.
inline constexpr size_t data_mac_len(uint8_t flags) { return (flags & DATA_FLAG_CRYPTED) ? 8 : 4; }

// ★★ §B20/B21 (2026-08-28) — **THE ONE DATA LENGTH AUTHORITY.** A DATA frame is EXACTLY four terms:
//   the 8-B header · the TYPE byte iff APP (i.e. `type != 0`, which is what pack_data derives APP from) ·
//   the inner · the trailer `data_mac_len()` picks (4-B MAC, or the 8-B nonce-seed under CRYPTED).
// `pack_data` refuses on this expression, so anything that sizes an inner from it is asking the packer's own
// question rather than a parallel copy of it. ⛔ NEVER hand-write a cap beside this; ask the formula.
// ⚠ SCOPE, STATED NARROWLY ON PURPOSE: today the ONE consumer is `enqueue_data`'s sealed-DM preflight
// (node_mac.cpp) — the only origination whose inner length is variable AND whose trailer is the 8-B one.
// This is NOT yet a claim that every originator consults it: the plaintext and cross-layer builders still bound
// themselves by `TxItem.inner[]` (241), which is the stricter answer for their 4-B-trailer frames, and the
// fixed-size builders do not size anything. ⇒ read this as the authority a new variable-length carrier MUST use,
// not as a description of what all existing ones already do.
// ⓘ WHY IT EXISTS (register [[B20]]): the sender used to size a SEALED inner against the constant
// `protocol::max_payload_bytes_hard_cap` (241 = 255 − 8 − `data_inner_overhead` 6), which bakes in the **4-B**
// MAC. A CRYPTED frame's trailer is **8**, so a sealed 215-216-B body passed the sender's check, was sealed,
// and was then dropped by pack_data at TX time with NOTHING reported to the app. 241 is the `TxItem.inner[]`
// BUFFER bound — a different question from "what fits on the air", and it is the laxer of the two for a
// CRYPTED frame. Both must hold; the stricter one governs.
inline constexpr size_t data_frame_len(uint8_t flags, uint8_t type, size_t inner_len) {
    return DATA_HDR_LEN + (type != 0 ? size_t{1} : size_t{0}) + inner_len + data_mac_len(flags);
}
// The inverse: the largest inner a DATA frame OF THIS SHAPE can carry within `frame_cap` bytes (0 when the
// overhead alone already exceeds it). Same terms, same order — read it beside data_frame_len.
inline constexpr size_t data_inner_cap(uint8_t flags, uint8_t type,
                                       size_t frame_cap = protocol::lora_max_frame_bytes) {
    const size_t overhead = DATA_HDR_LEN + (type != 0 ? size_t{1} : size_t{0}) + data_mac_len(flags);
    return frame_cap > overhead ? frame_cap - overhead : size_t{0};
}

// byte-8 TYPE (enum, present IFF APP=1): mutually-exclusive message kinds. 0 = the ordinary untyped DM and is
// never emitted as a TYPE byte (APP=0 means the byte is absent). ⚠ That is a property of `pack_data`, NOT a
// receive guard: `parse_data` reads byte 8 whenever APP is set, so APP=1 with a 0x00 TYPE parses as type 0 with
// the inner already offset past the byte (test §A0-3b). The H_ANSWER inner is cleartext so relays cache-on-pass.
//
// ★★ §CUSTODY-A (2026-08-29) — THE NAMESPACE. Numbers are no longer allocation order; they are a RANGE
//    contract (design `2026-08-23-internal-data-and-custody-outcome-design.md` §5.1/§5.2):
//        0x00        the ordinary untyped DM — no TYPE byte is emitted
//        0x01..0x7F  application-bearing DATA types and envelopes
//        0x80..0xBF  protocol-internal DATA
//        0xC0..0xFD  reserved; not valid for origination
//        0xFE        inbox-store tombstone ONLY (inbox.h) — never a wire DataType
//        0xFF        invalid/reserved
//    The gaps inside 0x80..0xBF are DELIBERATE: they leave blocks for core outcomes (0x80..), hash/key
//    discovery (0x88..), mobility (0x90..) and administration/security (0xA0..) so a new member APPENDS
//    inside its block instead of renumbering an existing one. ⛔ Subrange position is a convenience for
//    readers, NOT a second behaviour authority — `data_type_traits()` below is the ONE authority, and the
//    range predicate is the EXACT bounded form `data_type_is_internal()`, never `t & 0x80`.
//    ⓘ These numbers are WIRE CONTRACT: after this transition, old and new firmware disagree on every typed
//      frame, so the whole fleet reflashes together (M3 — MeshRoute is unshipped, so that is free).
//      `protocol::wire_version` is deliberately UNCHANGED by owner ruling (§5.3); see the control in
//      `test/test_data_type_namespace.cpp`.
enum DataType : uint8_t {
    // ---- 0x01..0x7F — application-bearing types and envelopes -------------------------------------------
    DATA_TYPE_INTRO                         = 0x01,   // §S2 first-contact pubkey attach: a NORMAL plaintext app DM whose inner BODY is prefixed [ed_pub 32][name_len u8][name <=32] before the message text. Requires SOURCE_HASH; the ADDRESSED recipient verifies ed_pub[:4]==source_hash (peerkey self-consistency), peer_key_set(authoritative)+name (fires peer_key_cached), STRIPS the prefix, and delivers the remainder as a plain DM (inbox + msg_recv, enc absent, dedup (sender_hash,ctr) unchanged). Ride rule (D1): a plaintext hash-addressed send attaches INTRO iff we hold no peer_confirmed(dst) (no SEALED frame opened from dst yet) AND we have a crypto identity. s18-inert: no identity -> never attached, never received.
    DATA_TYPE_MOBILE_SEND                   = 0x02,   // §mobile delegated hash-locate (2026-07-11): a registered mobile asks its HOME to send the enclosed PLAINTEXT payload to DST_HASH (the target). dst=home_id, SOURCE_HASH=mobile_hash. The home re-originates via send_by_hash (resolve/park) stamping source_hash=mobile_hash so the target's E2E-ack routes back to the mobile. A mobile NEVER hash-locates on the static plane (origin=local id -> RREQ storm). Home-only (_mobile_reg_n>0) -> static-inert.
    DATA_TYPE_SEALED_RELAY                  = 0x03,   // §S4 encrypted cross-layer / delegated-sealed: a PLAINTEXT-framed DM (cleartext DST_HASH + SOURCE_HASH) whose BODY = [seal_ctr 2 LE][seed8 8][ciphertext‖tag]. The sender SEALED its text to DST_HASH under ITS OWN identity (source_hash) BEFORE the frame's MAC ctr existed (a mobile delegating to its home; or a static crossing a layer where the bridge re-issues the ctr), so the nonce ctr is CARRIED (seal_ctr) rather than the frame ctr — the frame ctr stays the originator/home's for MAC dedup. Routes/bridges/last-miles EXACTLY like any typed plaintext DM (no CRYPTED-frame changes; the crypto core stays SAME-LAYER-only). Recipient: directed open (source_hash in clear -> the sender's key, no trial), verify the SEALED source_hash == the clear source_hash (anti-spoof), IGNORE the sealed origin byte, deliver as a normal DM (enc=1). s18-inert (no identities -> no seals -> never emitted).
    DATA_TYPE_CHANNEL_POST                  = 0x04,   // §S7 T-B: an ENCLOSED-type marker — a registered mobile delegates a GLOBAL/leaf channel post to its home. Rides a PLAINTEXT MOBILE_SEND wrapper (SOURCE_HASH=mobile, DST_HASH=mobile's own hash [placeholder, unused], DATA_FLAG_MS_ENCLOSED_TYPE) whose BODY = [DATA_TYPE_CHANNEL_POST][channel_id u8][text]. ⚠ "enclosed-type marker" describes the ORIGINATION set and is NOT an enforced invariant (A0-F4): nothing rejects this value as an OUTER TYPE byte — an outer one has no `pa.type` arm at all and falls through to ordinary DM delivery (node_mac_rx.cpp:1936), and a crafted wrapper whose body is exactly [0x04] re-originates with type=0x04 (node_mac_rx.cpp:1639/:1646, XL :1597/:1611). The enclosed-type allow-list that would enforce it is Slice B's. The home strips it + re-originates via do_send_channel under ITS OWN origin/ctr (anti-spam bills the home, deliberate). A mobile can't originate a leaf flood on the static plane (empty _rt), so the home posts. s18-inert (no mobiles -> never emitted).
    // ★ RESERVED, UNIMPLEMENTED: the separate channel/app-code design owns its behaviour (docs/superpowers/specs/2026-08-05-channel-app-code-draft.md). The NUMBER is claimed here so that design
    //   appends into the application block instead of renumbering it; nothing produces or consumes it, and `data_type_traits()` reports it `known = false`
    //   on purpose — reserving a number is not knowing the type, so it takes the application range's UNKNOWN behaviour until the app-code design lands.
    DATA_TYPE_APP_MESSAGE                   = 0x05,   // application envelope — RESERVED ONLY (see above); ⛔ no producer, no consumer, no trait knowledge

    // ---- 0x80..0xBF — protocol-internal DATA ------------------------------------------------------------
    //   0x80.. core outcomes · 0x88.. hash/key discovery · 0x90.. mobility · 0xA0.. administration/security.
    //   ★★ 0x81 IS NOW ALLOCATED — §CUSTODY-F (2026-08-31) LIFTED SLICE A's FENCE. The line that stood here
    //      read *"0x81 is DELIBERATELY ABSENT: it is the forward reservation for DATA_TYPE_CUSTODY_FAILURE,
    //      added by the custody-codec slice (design §5.2/§17-F)"* — this IS that slice, so the reservation is
    //      redeemed rather than removed. The value never moved: 0x81 was pinned by A's namespace test as an
    //      UNALLOCATED internal value, and it is the same number now that it has a producer.
    DATA_TYPE_E2E_ACK                       = 0x80,   // normal-unicast inner; body = the acked ctr (2 B LE)
    DATA_TYPE_CUSTODY_FAILURE               = 0x81,   // §CUSTODY-F: a relay reports that IT could not complete onward custody transfer for a transit DATA it had already ACKed (design §8-§12). Standard plaintext unicast inner; BODY = the 24-B v1 custody record (`CustodyFailureRecord`, §9.2 — pack/parse below). Addressed to the FAILED DATA's original sender, `Plane::GLOBAL` explicit, fresh reporter counter, NO E2E_ACK_REQ, NO CRYPTED, no app deadline. ⛔ It is NOT a hop NACK and NOT proof the destination missed the DATA (§8) — the destination may have it, another path may have delivered a copy, and an E2E ack may still arrive. ⛔ NEVER generated about a `0x81` or a `DATA_TYPE_E2E_ACK` carrier, and a terminal failure of the notice ITSELF is telemetry-only (§12's recursion gate).
    DATA_TYPE_H_ANSWER                      = 0x88,   // canonical plaintext-unicast inner; body = hash-bind answer [target_layer][node_id][key_hash32 LE] (6 B)
    DATA_TYPE_AUTHORITATIVE_H_ANSWER        = 0x89,   // same canonical envelope/body; the answer is the owner's (authoritative)
    DATA_TYPE_H_ANSWER_PUBKEY               = 0x8A,   // E2E §6: RESERVED (overheard/soft pubkey answer) — NOT emitted in v1
    DATA_TYPE_AUTHORITATIVE_H_ANSWER_PUBKEY = 0x8B,   // E2E §6: canonical plaintext-unicast inner; body = [target_layer][node_id][ed_pub 32][name_len][name]. Already canonical before B161.
    DATA_TYPE_MOBILE_H_ANSWER               = 0x90,   // §mobile 4a: canonical plaintext-unicast inner; body = [target_layer][home][mobile_hash LE][epoch] (7 B). Distinct TYPE -> cache M->home, NEVER id_bind, freshest epoch wins.
    DATA_TYPE_MOBILE_BREADCRUMB             = 0x91,   // §mobile 4b: the NEW home tells the mobile's OLD home "it re-homed here" (node_join.cpp:1235, the D10 replacement — ⛔ there is NO mobile-side producer; the older wording said the moved mobile sends it, A0-F12); body [new_home_id:u8][new_epoch:u8][new_home_layer:u8], rides a DM carrying SOURCE_HASH=M. Old home records the redirect + answers future H-queries with the new home (4a MOBILE_H_ANSWER).
    DATA_TYPE_MOBILE_LAYER_QUERY            = 0x92,   // §mobile 5a: a mobile asks a gateway "list the layers you bridge" (SOURCE_HASH=M). Empty body.
    DATA_TYPE_MOBILE_LAYER_ANSWER           = 0x93,   // §mobile 5a: a gateway's reply = [count u8][ count × LayerRecord ]; the mobile unions it into its learned directory.
    DATA_TYPE_MOBILE_PUBKEY_PUSH            = 0x94,   // §mobile hash-locate Part 2 (Fix 6) — ⛔ RETIRED (A0-F10), ALLOCATED BUT NEVER EMITTED: ZERO producers and ZERO consumers in the tree; its addressed handler was deleted (node_mac_rx.cpp:1766-1768) and key custody now rides the presence probe's HAS_PUBKEY block (node_join.cpp:798). The number is HELD, not reused, so an old build's stray frame can never be read as something else. Historically: a mobile pushed its ed_pub[32] to its home (1-hop DM, SOURCE_HASH=M); body = ed_pub[32].
    DATA_TYPE_MOBILE_H_ANSWER_PUBKEY        = 0x95,   // §mobile hash-locate Part 2 (Fix 7): canonical plaintext-unicast inner; body = mobile hash-bind[7] + ed_pub[32] + name_len[1] + name[0..32]. Cache peer_key(M)+mobile_home(M->home), NEVER id_bind the local id.
    DATA_TYPE_MOBILE_KEY_FORWARD            = 0x96,   // §S3 part2: a HOME forwards a WANT_PUBKEY requester's key to its hosted mobile (1-hop last-mile, addr_len=1, plaintext). Body = [requester_ed_pub 32][name_len u8][name <=32]. The mobile caches it (self-consistency-checked) -> closes the recipient-side decrypt gap for the reqpubkey path (the mobile can now open the requester's sealed DM). Mobile-only consume — ⚠ an ADDRESSING CONVENTION, NOT ENFORCED (A0-F11): the consuming fork is gated on `is_mobile` AND `#if MR_FEAT_MOBILE` (node_mac_rx.cpp:1790-1799), so on a static / gateway / MR_FEAT_MOBILE=0 build an addressed one falls through to the deliver tail and the 32-B requester key lands in the inbox as message text. Slice B's fail-closed internal-range arm is what will actually enforce it.
    DATA_TYPE_REMOTE_CMD                    = 0xA0,   // OTA remote diagnostics (2026-06-24): inner body = a console-style query keyword
    DATA_TYPE_REMOTE_RESP                   = 0xA1,   //   its response text. Plain inner, cleartext (honest-net diagnostic; E2E-seal is a later option).
    // (ⓘ HISTORY: these two were ordinals 6/7 before the §CUSTODY-A namespace transition, and ordinal 6 was CONFIG_ANSWER
    //  before that — removed 2026-06-22, leaf config rides the C control frame cmd 0xB. Ordinals 6/7 are now UNALLOCATED
    //  application-range values; nothing translates them, by ruling §5.3.)
    DATA_TYPE_TEAM_KEY_GRANT                = 0xA2,   // §team-ch-key T-K3 (spec 2026-07-26 §2.3): a keyholder GRANTS the team CONTENT keypair to a vetted teammate. BODY = [team_id u32 LE][name_len u8][team_name <= 32][tkpriv 32] = 37..69 B. ★ tkpub is NOT on the wire (a deliberate divergence from the spec's 5-field body): the receiver re-derives it from tkpriv via the SAME team_channel_key_derive T-K1 stores through, so a pub/priv mismatch is structurally impossible instead of something a cross-check must catch — and it saves 32 B of airtime on the SF6/BW125 links this is used over. ★★ MUST TRAVEL SEALED, enforced STRUCTURALLY at the three places the invariant could break, not at the caller: enqueue_data refuses a non-CRYPTED TEAM_KEY_GRANT origination, enqueue_cross_layer refuses one outright (e2e_seal_inner is same-layer-only, so an XL TEAM_KEY_GRANT could only be cleartext), and send_by_hash refuses to DELEGATE it (the MOBILE_SEND wrapper's single enclosed-type slot is already spent on DATA_TYPE_SEALED_RELAY, so the grant would be silently LOST and 37 raw key bytes would land in the peer's inbox as text). ⇒ v1 transports = same-layer CRYPTED, team-plane CRYPTED (leaf-exempt, so an off-grid member on another leaf nibble IS reachable), and a home's CRYPTED last-mile to a hosted mobile. Receipt: a TEAM_KEY_GRANT that arrived UNSEALED is dropped loud (a plaintext grant is a bug or an attack); consumed, NEVER inbox'd or delivered as a DM. s18-inert (no identities -> no seals -> never emitted).
};

// ★★★ §CUSTODY-A — THE RANGE PREDICATE, IN ITS EXACT BOUNDED FORM (design §5.1).
// ⛔⛔ NEVER `t & 0x80`. The high-bit test is the tempting one-liner and it is WRONG: it admits the whole
//     reserved `0xC0..0xFD` block, the inbox-only `0xFE` tombstone and the invalid `0xFF` as "protocol-internal
//     DATA". Those are not internal types; they are values no origination may use, and Slice B's fail-closed
//     internal arm must not adopt them. The bound is closed at BOTH ends, deliberately.
inline constexpr uint8_t data_type_app_lo      = 0x01;   // application range, inclusive
inline constexpr uint8_t data_type_app_hi      = 0x7F;
inline constexpr uint8_t data_type_internal_lo = 0x80;   // protocol-internal range, inclusive
inline constexpr uint8_t data_type_internal_hi = 0xBF;
constexpr bool data_type_is_internal(uint8_t t) {
    return t >= data_type_internal_lo && t <= data_type_internal_hi;
}
// The application range is likewise CLOSED — 0x00 (the untyped DM) is not in it, and neither is anything
// above 0x7F. `data_type_is_internal` and this are complements only INSIDE 0x01..0xBF; 0x00 and 0xC0..0xFF
// are outside both, which is the fact the reserved rows depend on.
constexpr bool data_type_is_application(uint8_t t) {
    return t >= data_type_app_lo && t <= data_type_app_hi;
}

// ★★★ §CUSTODY-A — THE ONE DATA-TYPE TRAIT AUTHORITY (design §6.1).
// One constexpr, no-RAM answer per type. It exists so the MAC, the inbox, the JSON encoder and the UI stop
// keeping parallel exact-type lists (§6.1: "there must not be parallel lists"); §3.2 of the A0 matrix counts
// six such duplications today, two of which are the hand-copied DM-floor exemption written out twice.
//
// ⛔⛔ SLICE-A SCOPE, STATED SO IT IS NOT OVER-READ: this authority LANDS here and is proved by a table-driven
//     native test (`test/test_data_type_namespace.cpp`). ⛔ NOTHING CONSUMES IT YET. Replacing the duplicated
//     lists and wiring the fail-closed unknown-internal arm is Slice B's behaviour change, kept out of this
//     slice on purpose so the namespace transition's corpus movement stays attributable (C1).
//
// THE FIELDS, and what each does NOT mean:
//   known                   — this exact value is an ALLOCATED, UNDERSTOOD type. ★ Reserving a number is NOT
//                             knowing the type: `APP_MESSAGE` (0x05, reserved for the app-code design),
//                             `H_ANSWER_PUBKEY` (0x8A, never emitted) and the RETIRED `MOBILE_PUBKEY_PUSH`
//                             (0x94) are all `known = false` and therefore take their RANGE's unknown
//                             behaviour, which is the safe direction in both ranges.
//   internal                — exactly `data_type_is_internal(t)`. ⛔ §6.3: this is NOT a custody-report
//                             exclusion, NOT sealing, NOT trust, NOT priority and NOT an airtime exemption.
//   application_bearing     — carries logical user/application intent (§6.4), so the envelope types keep the
//                             user pacing and outcome of the logical send even though they are unwrapped.
//   generic_send_lifecycle  — may emit the generic send_blocked/send_acked/send_failed/send_aired family
//                             (§6.2(5)). Internal types may not; their protocol-specific results are
//                             untouched (§6.2(6)).
//   persistent_outcome      — is written to inbox storage as a durable internal OUTCOME record (§7.1).
//                             ★ EXACT MEMBERSHIP AT SLICE F = { E2E_ACK }, UNCHANGED FROM SLICE A.
//                             ⛔⛔ CORRECTED IN PLACE 2026-08-31 BY §CUSTODY-F, because the sentence that stood
//                             here — *"CUSTODY_FAILURE joins it when the custody-codec slice adds 0x81"* — is
//                             now FALSE AT THE SLICE THAT WAS SUPPOSED TO MAKE IT TRUE. F adds 0x81 and its
//                             PRODUCER; it adds no storing consumer at all (the receiver, the record-before-push
//                             ordering and the durable write are Slice G's, §17-G). A trait that claimed a
//                             durable write nothing performs would be a claim, not a description — the same
//                             "a success that isn't" shape the arc has corrected twice. ⇒ **Slice G flips
//                             `persistent_outcome` to true for 0x81 and closes the §18.2 endpoint**, and it is
//                             the slice that makes the statement true.
//                             ⛔ This trait is about INTERNAL outcome records
//                             only — ordinary application-message inbox persistence is `record_dm`'s path and
//                             is not this flag's subject.
struct DataTypeTraits {
    bool known;
    bool internal;
    bool application_bearing;
    bool generic_send_lifecycle;
    bool persistent_outcome;
};

constexpr DataTypeTraits data_type_traits(uint8_t t) {
    switch (t) {
        // 0x00 — the ordinary untyped DM. Known, application-bearing, full generic lifecycle.
        case 0x00:
            return DataTypeTraits{ true,  false, true,  true,  false };
        // 0x01..0x04 — the KNOWN application envelopes/markers (§6.4). 0x05 APP_MESSAGE is deliberately NOT
        // here: it is a reservation, so it falls to the unknown-application arm below.
        case DATA_TYPE_INTRO:
        case DATA_TYPE_MOBILE_SEND:
        case DATA_TYPE_SEALED_RELAY:
        case DATA_TYPE_CHANNEL_POST:
            return DataTypeTraits{ true,  false, true,  true,  false };
        // 0x80 — the one durable internal OUTCOME today.
        case DATA_TYPE_E2E_ACK:
            return DataTypeTraits{ true,  true,  false, false, true  };
        // ★★★★ 0x81 — §CUSTODY-F. `known = true` (it now has a PRODUCER and §9's defined meaning; a reservation
        //     is not knowledge, an allocated type with a generator is), `internal = true`, NOT application-
        //     bearing, NO generic send lifecycle — and ⛔ `persistent_outcome = FALSE IN F`, deliberately.
        //     See the trait's own note above: F writes nothing durable, so a `true` here would describe a store
        //     that does not exist. ⇒ **SLICE G FLIPS THIS BIT** when the storing consumption lands (§7.1/§17-G).
        //     ⓘ The consequence is exact and is what the §CUSTODY-C classification already handles: an 0x81
        //     record is `internal`, so `inbox_record_is_internal` hides it from ordinary views the moment G
        //     starts writing one — the visibility rule does not wait for this flag.
        case DATA_TYPE_CUSTODY_FAILURE:
            return DataTypeTraits{ true,  true,  false, false, false };
        // the KNOWN internal types with a live consumer. ⛔ 0x8A and 0x94 are NOT here (see below).
        case DATA_TYPE_H_ANSWER:
        case DATA_TYPE_AUTHORITATIVE_H_ANSWER:
        case DATA_TYPE_AUTHORITATIVE_H_ANSWER_PUBKEY:
        case DATA_TYPE_MOBILE_H_ANSWER:
        case DATA_TYPE_MOBILE_BREADCRUMB:
        case DATA_TYPE_MOBILE_LAYER_QUERY:
        case DATA_TYPE_MOBILE_LAYER_ANSWER:
        case DATA_TYPE_MOBILE_H_ANSWER_PUBKEY:
        case DATA_TYPE_MOBILE_KEY_FORWARD:
        case DATA_TYPE_REMOTE_CMD:
        case DATA_TYPE_REMOTE_RESP:
        case DATA_TYPE_TEAM_KEY_GRANT:
            return DataTypeTraits{ true,  true,  false, false, false };
        default:
            break;
    }
    // ⛔ EVERYTHING ELSE FALLS TO ITS RANGE, and that is the whole design: a value nobody has taught this
    //    authority about must behave like an unknown of its range, never like a neighbour that happens to
    //    share a subrange. `APP_MESSAGE` (0x05), the reserved `0x8A` and the retired `0x94` arrive here — each
    //    `known = false`, each taking its range's rule.
    //    ⓘ CORRECTED 2026-08-31 (§CUSTODY-F): this list used to end *"and the forward-reserved `0x81`"*. It no
    //      longer arrives here — 0x81 has an explicit case above now that it has a producer.
    if (data_type_is_application(t)) return DataTypeTraits{ false, false, true,  true,  false };
    if (data_type_is_internal(t))    return DataTypeTraits{ false, true,  false, false, false };
    // 0xC0..0xFD reserved · 0xFE the inbox-store tombstone (never a wire DataType) · 0xFF invalid.
    // Outside both ranges: not internal, and NOT application-bearing either — no origination may use them.
    return DataTypeTraits{ false, false, false, false, false };
}

// ★★★★ §CUSTODY-E (2026-08-31) — THE TWO CANONICAL CUSTODY-TERMINAL TYPES, WITH THEIR **APPROVED EXPLICIT
//      VALUES** (design §9.4 / §9.3, and the §17-F wording correction of the same date).
//
// ⛔⛔ ENUM OWNERSHIP IS RULED AND IT IS THIS SLICE'S: **Slice E lands these types AND their numeric values;
//     Slice F adds only the CODEC that serializes them and MAY NOT introduce a second mapping.** The earlier
//     §17-F bullet ("add the v1 codec and numeric enums") was contradictory and is superseded — a wire value
//     invented twice is a wire value that disagrees with itself exactly once.
//
// ⛔ THEY LIVE HERE, BESIDE `DataType`, AND NOT IN `node_carriers.h`, for one reason: these are WIRE values, and
//    this header is the wire authority the codec will be written against. `node_carriers.h` documents itself as
//    having no frame_codec dependency, so importing a wire enum into it would break that property for nothing.
//
// ⚠⚠ WITHDRAWN 2026-08-31 BY §CUSTODY-F, NOT DELETED. IT READ: *"NOTHING SERIALIZES THESE IN SLICE E … and
//   `DATA_TYPE_CUSTODY_FAILURE` (0x81) is still DELIBERATELY UNALLOCATED above."* **BOTH HALVES ARE NOW FALSE:**
//   `pack_custody_failure` (below) serializes these two enums onto the wire, and 0x81 is an ALLOCATED member of
//   `DataType` with a live producer. The sentence is kept in withdrawn form because the E→F handover is what it
//   records; ⛔ do not re-add either claim.
enum class CustodyFailureReason : uint8_t {
    invalid           = 0,   // never transmitted; also the "this terminal pass is NOT terminal" answer (see below)
    one_way_throttled = 1,   // the MF4 reprobe window refused another burst (node_cascade.cpp `cascade_to_alt`)
    cascade_count     = 2,   // `cascade_requeue_max` reached
    cascade_age       = 3,   // `cascade_requeue_total_max_ms` reached
    load_shed         = 5,   // the load-adaptive effective requeue budget rejected it
    queue_full        = 4,   // the TX queue had no requeue slot
};
// ⓘ THE DECLARATION ORDER ABOVE IS DELIBERATELY **NOT** THE PRECEDENCE ORDER, and `load_shed` is written out of
//   numeric order on purpose: the values are §9.4's wire contract and may never be re-ordered to look tidy, while
//   the PRECEDENCE (`cascade_count` -> `cascade_age` -> `queue_full`, with `load_shed` and `one_way_throttled` as
//   SEPARATE branches) is stated once, in code, by `Node::cascade_terminal_cause` — the one authority. If the two
//   were written as one ordered list a reader would inevitably take position for precedence, which is exactly the
//   "subrange position is not a second behaviour authority" mistake `DataType` above warns about.

// §9.3 bits 1/2 — WHICH MAC EXCHANGE THE TERMINAL FLIGHT DIED WAITING FOR. Exactly one of the two is true for a
// live carrier, and the stage is SEPARATE from the reason: `cascade_age` after repeated CTS failures is different
// evidence from a bare "no CTS" label, which would hide why the carrier was finally deleted.
// ★ THE VALUES ARE §9.3's BIT NUMBERS, not an invented ordinal — Slice F's `notice_flags` half is therefore the
//   DERIVATION `1u << static_cast<uint8_t>(stage)` (bit 1 = failed_at_cts, bit 2 = failed_at_ack) rather than a
//   second table. `invalid` is 0 so a default-constructed context can never claim a stage it does not have.
// ⛔ THE MAPPING TO THE ROOT TIMEOUT IS PINNED: an RTS root (`rts_timeout_fire`) is ALWAYS `cts`; a DATA-ACK root
//    (`ack_timeout_fire`) is ALWAYS `hop_ack`. It is derived from WHICH TIMER FIRED, never from an event name.
enum class CustodyRootStage : uint8_t {
    invalid = 0,
    cts     = 1,   // §9.3 bit 1 `failed_at_cts`  — the flight was waiting for a CTS
    hop_ack = 2,   // §9.3 bit 2 `failed_at_ack`  — the flight was waiting for a hop ACK
};

// =====================================================================================================
// ★★★★ §CUSTODY-F (2026-08-31) — THE v1 CUSTODY-FAILURE RECORD CODEC (design §9.2/§9.3/§9.5).
//
// ⛔⛔ THIS IS **THE ONE** PACK/PARSE PATH, AND ITS EXISTENCE IS THE CONTRACT §9.2 STATES:
//     *"The record codec is one shared pack/parse path used by core receive handling, Push JSON, pulled-record
//     JSON and native tests. Do not re-read byte offsets separately in those consumers."* Every future consumer
//     asks these two functions; nobody indexes the body. A second offset table is how a 24-byte record ends up
//     meaning two different things.
//
// ⛔ THE SLICE SPLIT IS RULED AND IT IS VISIBLE HERE (§17-F as corrected): **F DEFINES this API, USES `pack_…`
//    for generation, and EXERCISES `parse_…` in native tests. Slice G wires `parse_…` into receive handling,
//    Push JSON, pulled JSON and persistence.** ⇒ if you are looking for the caller of `parse_custody_failure`
//    outside `test/`, there is deliberately none yet. That is not a dangling API; it is the half of the shared
//    path that must exist BEFORE a consumer, so the consumer cannot invent its own.
//
// ⛔ AND THE ENUMS ARE **NOT REDEFINED HERE**: `CustodyFailureReason` / `CustodyRootStage` are Slice E's,
//    immediately above, and this codec only SERIALIZES them (§17-F: "⛔ no second mapping"). A wire value
//    invented twice is a wire value that disagrees with itself exactly once.
//
// ⓘ NO `wire_version` BUMP, AND THAT IS A DECISION RATHER THAN AN OMISSION (recorded because C4 requires it):
//   a post-Slice-B receiver DROPS an unknown addressed internal type at the fail-closed tail guard with bounded
//   `unsupported_internal` telemetry (node_mac_rx.cpp), so an 0x81 arriving at a node that has not yet learned
//   it is SAFE, not misread; and pre-namespace fleets are already incompatible under the standing
//   reflash-together ruling (protocol.md §2.4 / M3). The existing `wire_version == 1` control in
//   `test/test_data_type_namespace.cpp` still fails on any bump.
// =====================================================================================================

inline constexpr uint8_t custody_record_version_v1 = 1;    // §9.2 offset 0
inline constexpr uint8_t custody_record_v1_len     = 24;   // §9.2: the v1 FIXED PREFIX. `record_len` may exceed it.

// §9.3 — `notice_flags`. Bit NUMBERS 1/2 are `CustodyRootStage`'s own values, which is why the stage half of the
// byte is the DERIVATION `1u << stage` and not a second table (see `custody_notice_flags` below).
enum CustodyNoticeFlag : uint8_t {
    CUSTODY_FLAG_FORWARDED        = 0x01,   // bit 0 — MUST be 1 in v1
    CUSTODY_FLAG_FAILED_AT_CTS    = 0x02,   // bit 1 — terminal root was waiting for CTS
    CUSTODY_FLAG_FAILED_AT_ACK    = 0x04,   // bit 2 — terminal root was waiting for a hop ACK
    CUSTODY_FLAG_REPAIR_ATTEMPTED = 0x08,   // bit 3 — this terminal pass INVOKED repair-request logic. ⛔ It does
                                            //         NOT claim an RREQ was admitted or aired (§9.3, verbatim).
    CUSTODY_FLAG_NEXT_WAS_ONE_WAY = 0x10,   // bit 4 — `failed_next_hop` was classified one-way
    CUSTODY_FLAG_HAS_DST_HASH     = 0x20,   // bit 5 — `dst_hash32` is present and valid
};
inline constexpr uint8_t custody_flags_stage_mask    = CUSTODY_FLAG_FAILED_AT_CTS | CUSTODY_FLAG_FAILED_AT_ACK;
inline constexpr uint8_t custody_flags_reserved_mask = 0xC0;   // §9.3: bits 6-7 are zero in v1

// §9.5 — a NOTICE-SPECIFIC wire enum, ⛔ NOT a serialization of C++ `Plane`. `Plane::AUTO` is a ROUTING SELECTOR
// and is not a diagnostic plane: a v1 carrier which RESOLVED to static/global records `static_same_layer`
// whether its stored selector was `AUTO` or `GLOBAL` (§9.5, verbatim). v1 transmits value 0 and nothing else.
enum class CustodyFailurePlane : uint8_t {
    static_same_layer = 0,     // ★ THE ONLY VALUE TRANSMITTED BY v1
    team              = 1,     // reserved; unsupported in v1
    hosted_mobile     = 2,     // reserved; unsupported in v1
    cross_layer       = 3,     // reserved; unsupported in v1
    unknown           = 255,   // reserved; NEVER transmitted by v1
};
constexpr bool custody_plane_is_defined(uint8_t v) {
    return v <= static_cast<uint8_t>(CustodyFailurePlane::cross_layer)
        || v == static_cast<uint8_t>(CustodyFailurePlane::unknown);
}
// §9.4 — a wire reason value a v1 record may legitimately carry. ⛔ `invalid` (0) is NEVER transmitted.
constexpr bool custody_reason_is_transmittable(uint8_t v) {
    return v >= static_cast<uint8_t>(CustodyFailureReason::one_way_throttled)
        && v <= static_cast<uint8_t>(CustodyFailureReason::load_shed);
}
// §10.1(9) — a valid STATIC node id. The four identity fields of a v1 record must all satisfy it (0 = the
// unprovisioned id, 0xFF = reserved/broadcast — neither can be a custody party).
constexpr bool custody_node_id_valid(uint8_t id) { return id >= 1 && id <= 254; }

// §9.2's v1 body, as a VALUE. ⛔ It is NOT a memcpy image of the wire: `dst_hash32` forces 4-byte alignment, so
// `sizeof` exceeds 24 on every target. The 24 is the WIRE length (`custody_record_v1_len`) and only the codec
// below knows the offsets — which is exactly the property §9.2 asks for.
struct CustodyFailureRecord {
    uint8_t  version           = custody_record_version_v1;   // 0
    uint8_t  record_len        = custody_record_v1_len;       // 1  — 24..available body length
    uint8_t  notice_flags      = 0;                           // 2
    CustodyFailureReason terminal_reason = CustodyFailureReason::invalid;   // 3
    uint8_t  failed_origin     = 0;                           // 4  — the original DATA's origin (= the notice's dst)
    uint8_t  failed_dst        = 0;                           // 5
    uint16_t failed_ctr        = 0;                           // 6  — LITTLE-endian on the wire
    uint8_t  failed_type       = 0;                           // 8  — the original DATA's type; 0 for an ordinary DM
    uint8_t  failed_data_flags = 0;                           // 9  — header flags visible to this transit relay
    CustodyFailurePlane failed_plane = CustodyFailurePlane::static_same_layer;   // 10
    uint8_t  reporter_layer    = 0;                           // 11 — the relay's ACTIVE full layer id
    uint8_t  previous_hop      = 0;                           // 12 — the upstream custody source
    uint8_t  failed_next_hop   = 0;                           // 13 — the last attempted downstream hop
    uint8_t  requeue_count     = 0;                           // 14
    uint8_t  alternatives_tried = 0;                          // 15
    uint8_t  committed_hops    = 0;                           // 16
    uint8_t  remaining_hops    = 0;                           // 17
    uint32_t dst_hash32        = 0;                           // 18 — LITTLE-endian; zero when absent/unavailable
    uint16_t reserved          = 0;                           // 22 — transmit zero; must be zero for version 1
};

// §9.3's flags byte, DERIVED. ⛔⛔ THE SENTINEL IS REFUSED RATHER THAN SHIFTED, and that is the whole reason this
//    is a function: `1u << static_cast<uint8_t>(CustodyRootStage::invalid)` is `1u << 0` = `CUSTODY_FLAG_FORWARDED`,
//    so a naive derivation would turn "this context claims no stage" into "forwarded, and no stage bit" — a
//    record that satisfies bit 0 twice and §9.3's exactly-one-stage rule never. It returns 0, which is an
//    IMPOSSIBLE v1 flags byte (`forwarded` is mandatory), so `pack_custody_failure` refuses it. Two layers, both
//    tested: E's seam guarantee that `invalid` never reaches here, and this fail-closed answer if it ever did.
inline uint8_t custody_notice_flags(CustodyRootStage stage, bool repair_attempted,
                                    bool next_was_one_way, bool has_dst_hash) {
    if (stage == CustodyRootStage::invalid) return 0;                 // fail-closed; `pack_…` refuses a 0 byte
    uint8_t f = static_cast<uint8_t>(CUSTODY_FLAG_FORWARDED | (1u << static_cast<uint8_t>(stage)));
    if (repair_attempted) f |= CUSTODY_FLAG_REPAIR_ATTEMPTED;
    if (next_was_one_way) f |= CUSTODY_FLAG_NEXT_WAS_ONE_WAY;
    if (has_dst_hash)     f |= CUSTODY_FLAG_HAS_DST_HASH;
    return f;
}
// §9.3's own invariant, named once so the packer, the parser and the tests cannot each spell it differently.
constexpr bool custody_flags_exactly_one_stage(uint8_t flags) {
    const uint8_t s = static_cast<uint8_t>(flags & custody_flags_stage_mask);
    return s == CUSTODY_FLAG_FAILED_AT_CTS || s == CUSTODY_FLAG_FAILED_AT_ACK;
}

// Pack a v1 record. Returns `custody_record_v1_len` (24) on success, 0 on REFUSAL — and it refuses loudly (C2)
// rather than emitting a record that violates §9.2/§9.3, because a malformed notice is worse than none: the
// receiver would drop it anyway and the airtime would be spent. The refusals are exactly the invariants a v1
// TRANSMITTER owns: version, the 24-byte floor, `forwarded`, exactly one stage bit, no reserved bits, a
// transmittable reason, the four ids in 1..254, a nonzero ctr, the hash flag agreeing with the hash, and zeroed
// reserved bytes. ⛔ `record_len` is written as 24 — a v1 transmitter appends no tail.
size_t pack_custody_failure(const CustodyFailureRecord& in, std::span<uint8_t> out);

// Parse a v1 record out of a DATA body. `nullopt` = malformed at the CODEC level (§18.3.6's list): short body,
// unknown version, `record_len` below 24 or beyond the body, reserved flag bits, a missing `forwarded`,
// contradictory stage bits, an unknown/never-transmitted reason, an undefined plane value, an id outside
// 1..254, a zero ctr, an inconsistent hash flag, or nonzero reserved bytes.
// ★ TAIL ACCEPTANCE (§9.2): `record_len > 24` is VALID as long as it fits the body — a v1 reader INTERPRETS the
//   first 24 bytes and the returned `record_len` tells a storing consumer how many bytes it must retain. Use
//   `custody_record_tail()` to obtain those bytes; ⛔ never re-derive the offset.
// ⛔ WHAT THIS DELIBERATELY DOES **NOT** CHECK, because it needs NODE CONTEXT this pure codec does not have —
//   they are §13's receiver items and belong to Slice G, stated here so a reader does not mistake absence for
//   an oversight (the mark-done-vs-missing rule):
//     · §13.10 `failed_plane == static_same_layer` — v1 SUPPORT, not well-formedness; a reserved plane parses;
//     · §13.11 `failed_origin` equals THIS node's static id;
//     · §13.15 `reporter_layer` equals the active receiving full layer;
//     · §13.14 `failed_type` is neither E2E ACK nor custody failure — a RECEIVER-side sanity rule on a field
//       this codec only carries (the GENERATOR's side of it is §10.1(11), enforced at the eligibility gate);
//     · §13.18 count/hop fields fitting their protocol domains.
std::optional<CustodyFailureRecord> parse_custody_failure(std::span<const uint8_t> body);

// §9.2's unknown-version TAIL, as a span into the caller's body. Empty for a plain v1 record. ⛔ The ONE place
// the tail's offset is written down; a storing consumer (Slice G) asks for it rather than slicing at 24.
inline std::span<const uint8_t> custody_record_tail(std::span<const uint8_t> body,
                                                    const CustodyFailureRecord& rec) {
    if (rec.record_len <= custody_record_v1_len || body.size() < rec.record_len) return {};
    return body.subspan(custody_record_v1_len,
                        static_cast<size_t>(rec.record_len) - custody_record_v1_len);
}

// §mobile 5a: a neighbouring-layer record (the composite network identity — layer_id alone isn't unique across areas).
// Wire: [layer_id u8][freq_khz u32 LE][sf u8][bw_hz u32 LE][name_len u8][name … name_len]. freq_khz = MHz×1000.
struct LayerRecord {
    uint8_t  layer_id = 0;
    uint32_t freq_khz = 0;
    uint8_t  sf       = 0;
    uint32_t bw_hz    = 0;
    uint8_t  name_len = 0;
    char     name[protocol::leaf_name_max] = {};
};
size_t pack_layer_record(const LayerRecord& in, std::span<uint8_t> out);          // 11 + name_len; 0 on short buf
std::optional<LayerRecord> parse_layer_record(std::span<const uint8_t> in, size_t& consumed);  // reads one record; consumed = bytes used

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
    std::span<const uint8_t> mac;      // the DATA trailer. PLAIN DM: a 4-B MAC placeholder (empty span -> zero-filled; else exactly 4). CRYPTED: the 8-B XChaCha nonce seed (exactly 8 — the caller ALWAYS supplies it; the empty-span zero-fill is a plain-DM-only convenience, never reached under CRYPTED, per the M5 review). A wrong-size mac -> pack returns 0.
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
inline constexpr size_t M_FRAME_HDR_LEN      = 7;    // cmd|leaf + channel_id + flavor + channel_msg_id(4)
inline constexpr size_t M_FRAME_TEAM_HDR_LEN = 11;   // §mobile 6.3: + team_id(4 B BE) iff (flavor & channel_flavor_team)
struct m_in  { uint8_t leaf_id; uint8_t channel_id; uint8_t flavor; uint32_t channel_msg_id;  // id BIG-endian on wire
               std::span<const uint8_t> body; uint32_t team_id = 0; };   // §6.3: packed (BE) iff (flavor & channel_flavor_team); at struct END for aggregate-inits
struct m_out { uint8_t leaf_id; uint8_t channel_id; uint8_t flavor; uint32_t channel_msg_id;  // id BIG-endian
               std::span<const uint8_t> body; uint32_t team_id = 0; };   // §6.3: 0 unless the team flavor bit was set
size_t pack_m(const m_in& in, std::span<uint8_t> out);          // 7 (+4 team) + body; 0 on short buf
std::optional<m_out> parse_m(std::span<const uint8_t> frame);   // nullopt: len < 7 (or < 11 for a team frame) / cmd != M

// -----------------------------------------------------------------------------
// P — presence plane (cmd-nibble 0xC) — §S6. TWO frames on ONE nibble, split by the byte-0 dir bit (b3).
// LOCAL 1-hop broadcasts: no CTS/ACK/relay/flood, TTL-free. ⚠ LEAF-FREE — byte-0's low nibble is FLAGS, not a
// leaf gate; the rx dispatch routes the P nibble BEFORE the standard leaf filter (non-hosting statics drop on
// frame type). LE where multi-byte, per house rule.
// -----------------------------------------------------------------------------
// P-probe (mobile -> broadcast, dir=0) — REV 2 (multi-home, 2026-07-18):
//   byte 0     : cmd=0xC(7..4) | dir(3)=0 | HAS_LAST_HOME(2) | HAS_PUBKEY(1) | rsv(0)   [former LOST bit GONE]
//   byte 1     : selected_home_id                 — always (0 = searching; else "THIS is my home")
//   byte 2     : selected_home_layer (FULL 8-bit)  — always (the PAIR — leaf-free ⇒ a bare id aliases)
//   bytes 3..6 : mobile key_hash32 (LE)           — always (the identity)
//   byte 7     : reg_epoch (low byte of the u16)   — always
//   bytes 8..9 : last_home_id + last_home_layer    — iff HAS_LAST_HOME
//   8/10..     : ed_pub[32]                         — iff HAS_PUBKEY (flag-bit order; parse fail-loud on short)
// Sizes: check = 8 B · searching+last_home = 10 B · registering (+key) = 42 B. `searching` is DERIVED: selected_home_id==0.
// P-roster (home -> broadcast, dir=1):
//   byte 0     : cmd=0xC(7..4) | dir(3)=1 | TRUNC(2) | HAS_ECHO(1) | HAS_DELEG(0)
//   byte 1     : home_id
//   byte 2     : home_layer (FULL 8-bit)
//   byte 3     : dir_epoch
//   byte 4     : wire_version (§D16: == protocol::wire_version; a mismatched roster is DROPPED before field interpretation)
//   byte 5     : count (<= cap_host_mobiles)
//   6..        : count × [ key_hash32(4 LE) | local_id(1) | reg_epoch(1) ]   (6-B entries)
//   tail       : quality bitmap 2b/mobile ceil(count/4) B, then has_key bitmap 1b/mobile ceil(count/8) B,
//                then (§B2, iff HAS_DELEG) deleg_fail bitmap 1b/mobile ceil(count/8) B — the home dropped a delegated send; one-shot (entry order)
//   +ECHO      : iff HAS_ECHO — echo_hash32(4 LE) + [echo_quality(2b) | rsv(6b)] (1 B) = 5 B ("the probe I answer, RX'd at quality X")
// quality: 0=critical 1=weak 2=ok 3=strong.  Sizes (no deleg): 3 mobiles = 24 B (+5 echo) · 16 = 107 B (§D16 +1 header); +ceil(count/8) when HAS_DELEG.
constexpr uint8_t P_DIR_ROSTER          = 0x08;   // byte-0 b3: 0=probe, 1=roster
constexpr uint8_t P_PROBE_HAS_LAST_HOME = 0x04;   // probe b2
constexpr uint8_t P_PROBE_HAS_PUBKEY    = 0x02;   // probe b1
constexpr uint8_t P_ROSTER_TRUNC        = 0x04;   // roster b2: reserved for a future cap > frame capacity (unused at cap 16)
constexpr uint8_t P_ROSTER_HAS_ECHO     = 0x02;   // roster b1: the ECHO block (echo_hash32 + echo_quality) is appended
constexpr uint8_t P_ROSTER_HAS_DELEG    = 0x01;   // roster b0 (§B2): the deleg_fail bitmap is appended — flag-gated so a steady-state roster (no failures) stays airtime-minimal (the spec §0 airtime principle) AND byte-neutral vs pre-B2

struct p_probe_in  { uint8_t selected_home_id = 0; uint8_t selected_home_layer = 0;   // 0 = searching
                     bool has_last_home = false; bool has_pubkey = false;
                     uint32_t key_hash32 = 0; uint8_t reg_epoch = 0;
                     uint8_t last_home_id = 0, last_home_layer = 0; uint8_t ed_pub[32] = {}; };
struct p_probe_out { uint8_t selected_home_id; uint8_t selected_home_layer;
                     bool has_last_home; bool has_pubkey;
                     uint32_t key_hash32; uint8_t reg_epoch;
                     uint8_t last_home_id, last_home_layer; uint8_t ed_pub[32];
                     bool searching() const { return selected_home_id == 0; } };
size_t pack_p_probe(const p_probe_in& in, std::span<uint8_t> out);        // 8 / 10 / 42; 0 on short buf
std::optional<p_probe_out> parse_p_probe(std::span<const uint8_t> frame); // nullopt: cmd!=P / dir!=probe / short-for-flags

struct PRosterEntry { uint32_t key_hash32; uint8_t local_id; uint8_t reg_epoch; uint8_t quality; bool has_key; bool deleg_fail = false; };  // §B2: deleg_fail = the home dropped a delegated send for this mobile (one-shot roster bit)
struct p_roster_in  { uint8_t home_id; uint8_t home_layer; uint8_t dir_epoch; uint8_t wire_version = 0; bool trunc = false;  // §D16: wire_version rides beside dir_epoch (== protocol::wire_version at the home)
                      const PRosterEntry* entries = nullptr; uint8_t count = 0;
                      bool has_echo = false; uint32_t echo_hash32 = 0; uint8_t echo_quality = 0; };
struct p_roster_out { uint8_t home_id; uint8_t home_layer; uint8_t dir_epoch; uint8_t wire_version; bool trunc; uint8_t count;  // §D16: parse gate reads wire_version BEFORE field interpretation
                      bool has_echo; bool has_deleg; uint32_t echo_hash32; uint8_t echo_quality;   // §B2: has_deleg = the flag-gated deleg_fail bitmap is present
                      size_t entries_off; size_t quality_off; size_t haskey_off; size_t deleg_off; size_t echo_off; size_t frame_len; };  // §B2: deleg_off valid iff has_deleg (the third per-mobile bitmap, after has_key)
size_t pack_p_roster(const p_roster_in& in, std::span<uint8_t> out);          // §D16/B2: 6 + 6*count + ceil(count/4) + ceil(count/8) [+ceil(count/8) iff any deleg_fail] [+5 echo]; 0 on short buf
std::optional<p_roster_out> parse_p_roster(std::span<const uint8_t> frame);   // nullopt: cmd!=P / dir!=roster / len short for count+bitmaps[+echo]
// i-th roster entry (i < count) incl. its quality tier + has_key bit; nullopt if out of range.
std::optional<PRosterEntry> parse_p_roster_entry(std::span<const uint8_t> frame, const p_roster_out& r, uint8_t i);

// Hash-bind answer BODY (H §3.7a): [target_layer][node_id][key_hash32 LE] = 6 B. The DATA producer wraps this
// codec in the canonical plaintext-unicast envelope; this helper deliberately owns no origin or flags byte.
// NO payload-flags byte exists in the body —
// the frame TYPE (DATA_TYPE_H_ANSWER vs DATA_TYPE_AUTHORITATIVE_H_ANSWER) carries H_ANSWER + AUTHORITATIVE,
// so the caller types the frame from `authoritative` (and reads it back from the TYPE). CLEARTEXT (CRYPTED=0)
// so relays cache-on-pass. key_hash32 is LITTLE-endian (matches pack_h / the beacon). `authoritative` here is
// a caller convenience (set the frame type from it on pack; the parse leaves it default — the caller knows it
// from the frame TYPE).
struct hash_bind_inner { uint8_t target_layer; uint8_t node_id; uint32_t key_hash32; bool authoritative = false; uint8_t epoch = 0; };  // §mobile 4a: epoch packed ONLY for the mobile TYPE (7 B); normal = 6 B unchanged
size_t pack_hash_bind_inner(const hash_bind_inner& in, std::span<uint8_t> out, bool mobile = false);  // 6; 7 if mobile (+epoch); 0 on short buf
std::optional<hash_bind_inner> parse_hash_bind_inner(std::span<const uint8_t> inner);    // nullopt: < 6 B; reads a 7th epoch byte when present (mobile)

// Hash-bind PUBKEY answer BODY prefix (E2E §6, DATA_TYPE_AUTHORITATIVE_H_ANSWER_PUBKEY = 0x8B): [target_layer 1][node_id 1][ed_pub 32] = 34 B.
// Its producer appends [name_len][name] and the standard enqueue path supplies the canonical unicast envelope.
// The key_hash32 is DROPPED (== ed_pub[:4]; the cacher derives + verifies it). CLEARTEXT so relays cache-on-pass.
struct hash_bind_pubkey_inner { uint8_t target_layer; uint8_t node_id; uint8_t ed_pub[32]; };
size_t pack_hash_bind_pubkey_inner(const hash_bind_pubkey_inner& in, std::span<uint8_t> out);          // 34; 0 on short buf
std::optional<hash_bind_pubkey_inner> parse_hash_bind_pubkey_inner(std::span<const uint8_t> inner);    // nullopt: < 34 B

}  // namespace meshroute
