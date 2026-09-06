// MeshRoute — lib/core/remote_codec.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// ★★★ REMOTE-ADMIN v2 SLICE 2 — THE REMOTE RPC CODEC. Pure, stateless, allocation-free.
//
// Authority: `docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md` §8 (wire bodies),
// §8.9 (result domains), §8.11 (byte budget), §9 (nonce/request-id) and the owner rulings R-RA-3 (packer-derived
// carrier caps), R-RA-4 (frozen control values / labels / independent KATs), R-RA-5 (64-bit random request id),
// R-RA-13 (`SOURCE_HASH` mandatory on every RPC carrier), R-RA-25 and R-RA-28 (always reserve the DST_HASH bytes).
//
// ⛔⛔ THIS SLICE HAS NO CONSUMER, AND THAT IS DELIBERATE. Nothing in `lib/core` or `src/` calls this module: no
//    RX router stages a body through it, no transmitter builds one, no ACL/session/storage/timer state exists yet.
//    The codec is proved by `test/test_remote_codec.cpp` against an INDEPENDENT Python reference
//    (`remote-v2-independent-reference`, transcript in the slice evidence), never by round-tripping itself.
//    ⇒ zero remote events, 36/36 corpus streams byte-identical, zero board RAM/flash. The first real caller is a
//    later slice and it is the one that owns airtime, console output, at-most-once execution and RNG integration.
//
// ⛔ WHAT THIS MODULE DELIBERATELY DOES **NOT** DO (so no later reader mistakes a byte codec for policy):
//      * it does not validate a command string, enforce a command allow-list, or know any dispatcher semantics;
//      * it does not implement at-most-once execution, transcript retention, response ACK debt or scheduling delay;
//      * it does not own ACL rows, epochs, sessions or replay state — `ctl` is on the wire, all of that is local;
//      * it does not touch Node, the HAL, NV, timers, routing or any `src/` firmware type;
//      * it does not read the RNG. `remote_make_request_id` takes a caller-supplied, STATUS-RETURNING entropy
//        function (see B312 at that declaration) — this file never converts a void draw into "success".
//
// ⛔ IT IS NOT THE LEGACY REMOTE PATH. `lib/core/admin_auth.{h,cpp}` (`admin_cmd_seal`/`admin_cmd_open`, the
//    `[rand8 8][nonce_ctr 2][ct][tag 16]` frame and its counter floor) is a DIFFERENT protocol that Slice 9
//    deletes. This codec never extends, imports or trial-opens it; a legacy body simply fails v2 admission.
//
// ⛔ NAMING/OWNERSHIP: this is a SHARED codec, not another capability consumer. It carries no `MR_FEAT_RADMIN_*`
//    reference, so Slice 1b's three-file capability-ownership census (`tools/probe_features/ownership.py`) is
//    unchanged by its arrival.
#pragma once
#ifndef MESHROUTE_NS
#define MESHROUTE_NS meshroute   // Slice 5 faithful two-lib: gateway variant compiles with -DMESHROUTE_NS=meshroute_gw
#endif
#include <cstddef>
#include <cstdint>
#include <span>

#include "dm_crypto.h"      // DM_TAG_LEN / DM_NONCE_LEN + the seal/open wrappers this module reuses (U1)
#include "identity.h"       // Identity + ecdh_shared, for the checked key-derivation boundary

namespace MESHROUTE_NS {

// =====================================================================================================
// §8.1 — the frozen control byte, opcodes and slot values. APPEND-ONLY after this slice.
// =====================================================================================================
//   bits 7..4  opcode within REMOTE_CMD or REMOTE_RESP
//   bits 3..0  established ACL slot 0..9, or sentinel F
// The outer DATA type (REMOTE_CMD 0xA0 / REMOTE_RESP 0xA1) — NOT a bit of `ctl` — distinguishes direction.
inline constexpr uint8_t kRemoteSlotSessionMax = 0x09;   // 0..9 select established authenticated ACL sessions
inline constexpr uint8_t kRemoteSlotSentinel   = 0x0F;   // F: OPEN_EXECUTE, an open response, a bootstrap request
// A..E are RESERVED and reject; there is no "reserved max" constant because the reject rule is the interval
// (kRemoteSlotSessionMax, kRemoteSlotSentinel) and naming an endpoint invites a second, drifting authority.

enum class RemoteCmdOpcode : uint8_t {
    auth_execute   = 0x0,
    open_execute   = 0x1,
    bootstrap      = 0x2,
    response_ack   = 0x3,
    safe_rollover  = 0x4,
    force_rollover = 0x5,
    // 0x6..0xF reserved — reject.
};

enum class RemoteRespOpcode : uint8_t {
    output          = 0x0,
    terminal        = 0x1,
    bootstrap       = 0x2,
    rollover_result = 0x3,
    protocol_error  = 0x4,
    // 0x5..0xF reserved — reject.
};

// §8.9 TERMINAL result namespace (Author allocation, QA-accepted 2026-09-06). 0x08..0xFF are UNALLOCATED and
// must never decode as a known meaning or as success.
enum class RemoteTerminal : uint8_t {
    completed        = 0x00,
    scheduled        = 0x01,
    unknown_command  = 0x02,
    refused          = 0x03,
    output_truncated = 0x04,
    internal_error   = 0x05,
    session_full     = 0x06,
    session_busy     = 0x07,
};
inline constexpr uint8_t kRemoteTerminalMax = 0x07;

// §8.9 AUTHENTICATED PROTOCOL_ERROR result namespace — a SEPARATE domain that happens to reuse the byte 0x00.
// ⛔ `already_acknowledged` is NOT a ninth terminal result: 0x00 means `completed` under TERMINAL and
//    `already_acknowledged` under authenticated PROTOCOL_ERROR, and only the opcode domain tells them apart.
//    That is why a decoded result is a TYPED value here and never a bare byte (design §8.9 Slice-2 contract).
//    0x01..0xFF reject in this domain even though several are valid TERMINAL codes.
enum class RemoteProtocolError : uint8_t {
    already_acknowledged = 0x00,
};

// The TYPED opcode/security domain. One value per (direction, opcode, security class); it is what the decoder
// publishes beside a result so `0x00` can never be read out of its domain, and it is the nonce/AAD separation
// the KATs pin pairwise.
enum class RemoteDomainId : uint8_t {
    invalid = 0,
    cmd_auth_execute,
    cmd_open_execute,           // UNAUTHENTICATED
    cmd_bootstrap,
    cmd_response_ack,
    cmd_safe_rollover,
    cmd_force_rollover,
    resp_output_auth,
    resp_output_open,           // UNAUTHENTICATED
    resp_terminal_auth,
    resp_terminal_open,         // UNAUTHENTICATED
    resp_bootstrap,
    resp_rollover_result,
    resp_protocol_error_auth,
    resp_protocol_error_open,   // UNAUTHENTICATED — and NOT the already_acknowledged domain (§8.9)
};

// Every refusal this codec can produce. C2: a refusal is always explicit and typed; nothing clamps, truncates,
// falls back to the open decoder, or publishes a partially decoded value.
enum class RemoteStatus : uint8_t {
    ok = 0,
    bad_outer_type,      // the DATA type is neither REMOTE_CMD nor REMOTE_RESP (a foreign type is not a direction)
    bad_opcode,          // reserved opcode nibble for that direction
    bad_slot,            // reserved slot nibble A..E
    bad_pairing,         // a legal opcode with an illegal slot class (bootstrap on a session slot, and so on)
    bad_length,          // exact-length rule violated, or a variable body shorter than its fixed fields
    bad_body_cap,        // ADMISSION: the RPC body exceeds `remote_body_cap` for this carrier — refuse, never clamp
    bad_buffer,          // the caller's output span cannot hold the result
    bad_argument,        // a required input is missing/misshaped (no source hash, wrong controller_pub length, ...)
    bad_carrier,         // the RemoteCarrier descriptor is not a real carrier shape (depth/cursor/field/underflow)
    bad_key,             // the key this domain selects is absent/misshaped, or an ECDH result is degenerate
    auth_failed,         // the AEAD tag did not verify — no plaintext and no decoded value are published
    bad_result_code,     // the result byte is outside its typed domain
    entropy_failed,      // the caller's entropy function reported failure (or was absent) — no ID is published
};

// =====================================================================================================
// Fixed sizes and the §8.11 envelope overheads. Every one is `static_assert`ed against the field terms in
// remote_codec.cpp, so a layout edit that forgets a field cannot silently keep the old number.
// =====================================================================================================
inline constexpr size_t kRemoteKeyBytes        = 32;
inline constexpr size_t kRemoteNonceBytes      = DM_NONCE_LEN;   // 24 — XChaCha
inline constexpr size_t kRemoteTagBytes        = DM_TAG_LEN;     // 16 — Poly1305
inline constexpr size_t kRemoteRequestIdBytes  = 8;              // R-RA-5: random u64, LE on the wire
inline constexpr size_t kRemoteEpochBytes      = 8;
inline constexpr size_t kRemoteCtlBytes        = 1;
inline constexpr size_t kRemoteSeqBytes        = 1;
inline constexpr size_t kRemoteAbandonedBytes  = 1;
inline constexpr size_t kRemoteControllerPubBytes = 32;
inline constexpr size_t kRemoteSourceHashBytes = 4;

// The largest clear RPC header is the bootstrap request's `[ctl][request_id 8][controller_pub 32]`.
inline constexpr size_t kRemoteMaxHeaderBytes = kRemoteCtlBytes + kRemoteRequestIdBytes + kRemoteControllerPubBytes;
// AAD = outer_type ‖ the exact clear header ‖ source_hash LE32.
inline constexpr size_t kRemoteMaxAadBytes    = 1 + kRemoteMaxHeaderBytes + kRemoteSourceHashBytes;

inline constexpr size_t kRemoteOverheadAuthExecute       = 25;   // ctl + id8 + tag16
inline constexpr size_t kRemoteOverheadOpenExecute       =  9;   // ctl + id8
inline constexpr size_t kRemoteOverheadBootstrapRequest  = 57;   // ctl + id8 + ctrl_pub32 + tag16
inline constexpr size_t kRemoteOverheadSessionControl    = 25;   // ctl + id8 + tag16   (ACK / SAFE / FORCE)
inline constexpr size_t kRemoteOverheadAuthResponse      = 26;   // ctl + id8 + seq1 + tag16
inline constexpr size_t kRemoteOverheadOpenResponse      = 10;   // ctl + id8 + seq1
inline constexpr size_t kRemoteOverheadBootstrapResponse = 33;   // ctl + id8 + epoch8 + tag16
inline constexpr size_t kRemoteOverheadRolloverResult    = 34;   // ctl + id8 + epoch8 + abandoned1 + tag16

// =====================================================================================================
// The ONE byte-layout/domain decision. `remote_layout` derives it from (outer type, ctl); encode, decode,
// the nonce key selector, the AAD builder and the admission check all read THIS struct — there is no second
// place where a layout is decided.
// =====================================================================================================
struct RemoteLayout {
    RemoteDomainId domain             = RemoteDomainId::invalid;
    uint8_t        opcode             = 0;       // the ctl high nibble, revalidated
    uint8_t        slot               = 0;       // the ctl low nibble, revalidated
    bool           authenticated      = false;   // carries a Poly1305 tag over the AEAD domain
    bool           variable_body      = false;   // a trailing ciphertext/plaintext of length N (N may be 0)
    bool           uses_base_key      = false;   // §8.1: bootstrap req/resp + rollover result; else session key
    bool           epoch_in_nonce     = false;   // §8.1: bootstrap RESPONSE + rollover result ONLY
    bool           has_controller_pub = false;
    bool           has_response_seq   = false;
    bool           has_admin_epoch    = false;   // a CLEAR epoch field in the header
    bool           has_abandoned_count= false;
    bool           carries_result_code= false;   // TERMINAL (both classes) + AUTHENTICATED protocol error
    uint8_t        header_bytes       = 0;       // ctl + the clear fields, in wire order
    uint8_t        fixed_overhead     = 0;       // header_bytes + (authenticated ? 16 : 0)
};

// The logical message: the clear header fields, direction and opcode/slot nibbles. Spans are CALLER-OWNED.
struct RemoteMessage {
    uint8_t  outer_type      = 0;   // DATA_TYPE_REMOTE_CMD (0xA0) or DATA_TYPE_REMOTE_RESP (0xA1)
    uint8_t  opcode          = 0;   // RemoteCmdOpcode / RemoteRespOpcode, as the raw nibble
    uint8_t  slot            = 0;   // 0..9, or kRemoteSlotSentinel
    uint64_t request_id      = 0;   // LE on the wire
    uint8_t  response_seq    = 0;   // responses that carry the field; ZERO for every request (nonce input too)
    uint64_t admin_epoch     = 0;   // bootstrap response / rollover result
    uint8_t  abandoned_count = 0;   // rollover result
    std::span<const uint8_t> controller_pub{};   // bootstrap request: EXACTLY 32 bytes; empty otherwise
};

// The two credentials, supplied as caller-owned views. An empty span means ABSENT; a present one must be
// exactly 32 bytes. The layout's `uses_base_key` selects which is required — supplying the wrong one alone
// is a `bad_key` refusal, never a silent fallback to the other.
struct RemoteKeys {
    std::span<const uint8_t> base{};
    std::span<const uint8_t> session{};
};

// The stable logical CONTROLLER source hash captured from the request carrier (§9), required by every
// authenticated domain in BOTH directions. ⛔ PRESENCE AND VALUE ARE SEPARATE FIELDS ON PURPOSE: a hash that
// happens to be 0 is a legitimate value, and "absent" must never be spelled as 0. There is no absent-source
// fallback: an authenticated encode/decode without `present` is refused.
struct RemoteSource {
    bool     present = false;
    uint32_t hash    = 0;
};

// =====================================================================================================
// The carrier — the REAL outer DATA shape a body would ride on, never a controller's administration key.
// `remote_body_cap` is the ONE capacity authority and derives every term from the existing named constants
// and the real air-fit formula; see remote_codec.cpp for the derivation and R-RA-28's reservation rule.
// =====================================================================================================
struct RemoteCarrier {
    uint8_t outer_data_type      = 0;      // the actual byte-8 TYPE: REMOTE_CMD/REMOTE_RESP, or MOBILE_SEND for a wrapper
    uint8_t enclosed_type        = 0;      // a typed wrapper's enclosed REMOTE_CMD/REMOTE_RESP; 0 when not a wrapper
    bool    wrapper              = false;  // the typed mobile->home wrapper: spends ONE enclosed-type body byte
    bool    cross_layer          = false;  // DATA_FLAG_CROSS_LAYER: the inner carries [n_layers][cur][ids...]
    uint8_t path_depth           = 0;      // n_layers on the wire. For a WRAPPER this is the DESTINATION depth and
                                           // the home prepends its own layer, so 1 + path_depth must still fit.
    uint8_t path_cursor          = 0;      // `cur`; the real packer requires cur < n_layers
    uint8_t addr_len             = 0;      // 0 normal, 1 hosted-mobile last mile. A HEADER field: costs no inner byte.
    bool    dst_hash_on_wire     = false;  // whether this leg transmits DST_HASH. R-RA-28: it never changes the cap.
    bool    source_hash_on_wire  = true;   // R-RA-13: MANDATORY. `false` is an invalid descriptor, not a bigger body.
    bool    outer_crypted        = false;  // ⛔ ARITHMETIC/PACKING CONTROL ONLY — no live v2 send path sets this.
                                           //    It exists so the AIR-fit bound (which storage otherwise masks) and
                                           //    pack_data's CRYPTED⇒DST_HASH structural rule stay reachable.
};

// =====================================================================================================
// Decoding result. `body` and `result_detail` are VIEWS: into the caller's plaintext buffer for an
// authenticated body, into the caller's input body for an open one. Nothing is copied and nothing is owned.
// =====================================================================================================
enum class RemoteResultKind : uint8_t {
    none = 0,           // OUTPUT, the requests, and the OPEN protocol error (which has no allocated code domain)
    terminal,           // the §8.9 terminal namespace
    protocol_error,     // the SEPARATE authenticated protocol-error namespace
};

struct RemoteDecoded {
    RemoteLayout        layout{};
    RemoteMessage       msg{};
    bool                authenticated = false;   // true iff a tag was verified for this body
    std::span<const uint8_t> body{};
    RemoteResultKind    result_kind    = RemoteResultKind::none;
    RemoteTerminal      terminal       = RemoteTerminal::completed;
    RemoteProtocolError protocol_error = RemoteProtocolError::already_acknowledged;
    std::span<const uint8_t> result_detail{};    // the bounded bytes after the result code, preserved exactly
};

// =====================================================================================================
// API
// =====================================================================================================

// ---- the one layout/domain decision -----------------------------------------------------------------
[[nodiscard]] RemoteStatus remote_layout(uint8_t outer_type, uint8_t ctl, RemoteLayout& out);
// Compose the control byte. There is exactly one composition site so encode and the KATs cannot drift.
[[nodiscard]] uint8_t remote_ctl(uint8_t opcode, uint8_t slot);

// ---- key derivation (§8.1) --------------------------------------------------------------------------
// X25519 through the EXISTING primitive, plus the check `ecdh_shared` does not make: a degenerate (all-zero)
// shared point — which is what every low-order peer point produces — is REFUSED before any key exists.
[[nodiscard]] RemoteStatus remote_ecdh_shared(uint8_t out_shared32[32], const Identity& self,
                                              const uint8_t peer_x_pub32[32]);
// base = BLAKE2b-512(label ‖ shared32 ‖ controller_ed_pub32 ‖ target_admin_ed_pub32)[:32]
// ⛔ ORDERED full public keys — never DM's sorted 32-bit short hashes and never DM's label.
[[nodiscard]] RemoteStatus remote_kdf_base(uint8_t out_key32[32], const uint8_t shared32[32],
                                           const uint8_t controller_ed_pub32[32],
                                           const uint8_t target_admin_ed_pub32[32]);
// session = BLAKE2b-512(label ‖ base_key32 ‖ admin_epoch LE64)[:32]
[[nodiscard]] RemoteStatus remote_kdf_session(uint8_t out_key32[32], const uint8_t base_key32[32],
                                              uint64_t admin_epoch);

// ---- nonce and AAD (§8.1/§9) ------------------------------------------------------------------------
// nonce = BLAKE2b-512(label ‖ selected_key32 ‖ outer_type ‖ ctl ‖ request_id LE64 ‖ response_seq ‖
//                     source_hash LE32 [‖ admin_epoch LE64 iff layout.epoch_in_nonce])[:24]
[[nodiscard]] RemoteStatus remote_nonce(uint8_t out_nonce24[24], const RemoteLayout& layout,
                                        const RemoteMessage& msg, const uint8_t selected_key32[32],
                                        const RemoteSource& src);
// AAD = outer_type ‖ the EXACT clear header bytes in wire order ‖ source_hash LE32. Ciphertext and tag are
// never repeated in AAD, and no mutable forwarding header is bound.
[[nodiscard]] RemoteStatus remote_aad(std::span<uint8_t> out, size_t& out_len, const RemoteLayout& layout,
                                      const RemoteMessage& msg, const RemoteSource& src);

// ---- carrier admission (R-RA-3 / R-RA-25 / R-RA-28) -------------------------------------------------
// The ONE capacity authority. Invalid shapes, illegal depths/cursors, a missing mandatory field and
// underflow all REFUSE; nothing is normalised and no oversize value is returned wrapped.
[[nodiscard]] RemoteStatus remote_body_cap(const RemoteCarrier& carrier, size_t& out_cap);
// The application-byte limit for one domain on one carrier: the RPC cap minus THAT domain's fixed overhead
// (§8.11's 25 / 9 / 26 / 10 and the fixed control/bootstrap bodies). Refuses when the domain cannot fit.
[[nodiscard]] RemoteStatus remote_application_cap(const RemoteCarrier& carrier, const RemoteLayout& layout,
                                                  size_t& out_cap);

// ---- encode / decode --------------------------------------------------------------------------------
// Build one complete RPC body. For an AUTHENTICATED domain this IS the seal: header, derived nonce, AAD and
// `dm_seal` in one path, so nothing can encode without reaching the key selector, the nonce and the AAD.
// For an OPEN domain it writes the clear header and body and produces no nonce, key or tag.
// `body` is the application payload; it must be EMPTY for a fixed layout.
[[nodiscard]] RemoteStatus remote_body_encode(std::span<uint8_t> out, size_t& out_len,
                                         const RemoteMessage& msg, std::span<const uint8_t> body,
                                         const RemoteKeys& keys, const RemoteSource& src,
                                         const RemoteCarrier& carrier);

// Decode one complete RPC body. For an AUTHENTICATED domain this IS the open: the tag is verified BEFORE any
// value is published, and a failure yields `auth_failed` with `out` untouched — no forged plaintext, no
// decoded header, and NEVER a retry through the open decoder or the legacy codec.
//
// ⚠ B313 — the failure-output contract, stated truthfully. `dm_open`'s comment claims the primitive wipes the
//   caller's plaintext buffer on a bad tag; it does not. `crypto_aead_read` verifies first and only writes
//   `plain_text` when the tag matched (`lib/monocypher/src/monocypher.c:2919-2924`), so on failure the caller's
//   buffer is simply LEFT AS IT WAS. This codec's guarantee is therefore: it publishes no decoded result and
//   writes nothing into `plaintext_out` on failure. It does NOT promise to scrub caller storage; a caller that
//   needs that must wipe its own buffer.
[[nodiscard]] RemoteStatus remote_body_decode(RemoteDecoded& out, uint8_t outer_type,
                                         std::span<const uint8_t> body,
                                         const RemoteKeys& keys, const RemoteSource& src,
                                         const RemoteCarrier& carrier, std::span<uint8_t> plaintext_out);

// ---- checked request-ID creation (R-RA-5 / design §9) -----------------------------------------------
// ★★ B312, AND IT IS A SOURCE FACT, NOT A STYLE CHOICE: `IHal::rand_bytes` (`lib/core/hal.h:183`) and
//    `DeviceHal::rand_bytes` (`lib/hal/device_hal.cpp:158`) both return VOID, so no HAL call can report an
//    entropy failure. This codec therefore takes the entropy source as an explicit, STATUS-RETURNING function
//    with caller-owned context, and refuses loudly when it reports anything but complete success.
//    ⛔ NO adapter that turns the void HAL draw into an unconditional `true` may be written against this: the
//    first integration slice owes a real, gated provider. B312 stays open for exactly that obligation.
//    ⛔ No retry loop, no clock/counter fallback, no cached entropy, no per-epoch request cap, and no reserved
//    sentinel ID standing in for failure — a successful draw of eight zero bytes is a legitimate request ID.
//
// Contract for the callee: write EXACTLY `n` bytes to `out` and return true, or return false. Returning false
// after writing some bytes is an incomplete fill and is refused like any other failure.
using RemoteEntropyFn = bool (*)(void* ctx, uint8_t* out, size_t n);

// On success `out_id` receives the little-endian u64 of the eight drawn bytes. On ANY failure — a null
// function, a false return, or a partial fill — `out_id` is left EXACTLY as the caller committed it.
[[nodiscard]] RemoteStatus remote_make_request_id(uint64_t& out_id, RemoteEntropyFn fn, void* ctx);

}  // namespace MESHROUTE_NS
