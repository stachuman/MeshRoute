// MeshRoute — lib/core/remote_codec.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// ★★★ REMOTE-ADMIN v2 SLICE 2 — the remote RPC codec. Read remote_codec.h first: it carries the authority
//     list, the frozen tables and the explicit statement of everything this module deliberately does NOT do.
//
// EVERY executable decision of this slice lives in THIS translation unit — the layout/domain table, the KDF
// labels and input order, the nonce/AAD composition, the exact-length rule, the capacity derivation, the result
// domains and the entropy refusal. The header holds declarations, types and frozen constants only, so the
// per-file mutation battery `radmin2codec` can reach every one of them.
//
// ⛔ NO HEAP, NO STATIC MUTABLE STATE, NO GLOBAL CONSTRUCTOR, NO VIRTUAL DISPATCH, NO `src/` INCLUDE.
//    The only non-const file-scope objects are `constexpr` label arrays.
#include "remote_codec.h"

#include "frame_codec.h"          // DATA_TYPE_* / DATA_FLAG_* / data_inner_cap — the REAL air-fit authority
#include "meshroute_wire.h"       // the bounded Writer/Reader (no u64 op: composed locally below)
#include "monocypher.h"           // crypto_blake2b / crypto_wipe / crypto_x25519 — the same primitives dm_crypto uses
#include "protocol_constants.h"   // the named storage cap and the named 1 + 4 + 4 reservation terms

namespace MESHROUTE_NS {

namespace {

// ---------------------------------------------------------------------------------------------------------
// §8.1 — the EXACT ASCII KDF labels, without a trailing NUL. ⛔ These are not DM's `MR-E2E-v1` domain: reusing
// that label would put remote-admin keys in the same derivation domain as per-peer DM keys.
// ---------------------------------------------------------------------------------------------------------
constexpr char   kLabelBase[]      = "MeshRoute remote-admin v2 base";
constexpr char   kLabelSession[]   = "MeshRoute remote-admin v2 session";
constexpr char   kLabelNonce[]     = "MeshRoute remote-admin v2 nonce";
constexpr size_t kLabelBaseLen     = sizeof(kLabelBase)    - 1;   // 30
constexpr size_t kLabelSessionLen  = sizeof(kLabelSession) - 1;   // 33
constexpr size_t kLabelNonceLen    = sizeof(kLabelNonce)   - 1;   // 31
static_assert(kLabelBaseLen == 30 && kLabelSessionLen == 33 && kLabelNonceLen == 31,
              "the three remote-admin v2 KDF labels are frozen ASCII strings with no trailing NUL");

// BLAKE2b-512 then TRUNCATE — the project's one hash-then-truncate convention (dm_crypto.cpp:23-52), never the
// parameterized BLAKE2b-256/192, which is a different function.
constexpr size_t kDigestBytes = 64;

// ---------------------------------------------------------------------------------------------------------
// The little-endian u64, composed LOCALLY from the existing bounded Writer/Reader. `meshroute_wire.h` has
// u8/u16/u32 only; adding a u64 there would be a shared-helper refactor this slice is not (C1).
// ---------------------------------------------------------------------------------------------------------
inline void put_u64_le(wire::Writer& w, uint64_t v) {
    w.u32_le(static_cast<uint32_t>(v));
    w.u32_le(static_cast<uint32_t>(v >> 32));
}
inline uint64_t get_u64_le(wire::Reader& r) {
    const uint64_t lo = r.u32_le();
    const uint64_t hi = r.u32_le();
    return lo | (hi << 32);
}

// Constant-time-ish all-zero test over 32 bytes — the identity.cpp:52-57 OR-accumulate idiom, no early return.
bool all_zero32(const uint8_t p[32]) {
    uint8_t acc = 0;
    for (int i = 0; i < 32; ++i) acc = static_cast<uint8_t>(acc | p[i]);
    return acc == 0;
}

bool key_present(std::span<const uint8_t> k) { return k.size() == kRemoteKeyBytes; }

// ---------------------------------------------------------------------------------------------------------
// The header/overhead arithmetic, as constexpr functions so the SAME expressions the runtime layout uses are
// what the §8.11 table is static_asserted against. A field added to a layout without touching the table
// therefore fails to compile instead of silently keeping an old number.
// ---------------------------------------------------------------------------------------------------------
constexpr uint8_t header_bytes_of(bool controller_pub, bool admin_epoch, bool abandoned, bool response_seq) {
    return static_cast<uint8_t>(kRemoteCtlBytes + kRemoteRequestIdBytes
                                + (controller_pub ? kRemoteControllerPubBytes : size_t{0})
                                + (admin_epoch    ? kRemoteEpochBytes         : size_t{0})
                                + (abandoned      ? kRemoteAbandonedBytes     : size_t{0})
                                + (response_seq   ? kRemoteSeqBytes           : size_t{0}));
}
constexpr uint8_t fixed_overhead_of(bool authenticated, uint8_t header_bytes) {
    return static_cast<uint8_t>(header_bytes + (authenticated ? kRemoteTagBytes : size_t{0}));
}

// §8.11's eight rows, bound to the arithmetic above.
static_assert(fixed_overhead_of(true,  header_bytes_of(false, false, false, false)) == kRemoteOverheadAuthExecute,
              "§8.2 authenticated execute request = ctl 1 + request_id 8 + tag 16");
static_assert(fixed_overhead_of(false, header_bytes_of(false, false, false, false)) == kRemoteOverheadOpenExecute,
              "§8.3 open execute request = ctl 1 + request_id 8");
static_assert(fixed_overhead_of(true,  header_bytes_of(true,  false, false, false)) == kRemoteOverheadBootstrapRequest,
              "§8.4 bootstrap request = ctl 1 + request_id 8 + controller_pub 32 + tag 16");
static_assert(fixed_overhead_of(true,  header_bytes_of(false, false, false, false)) == kRemoteOverheadSessionControl,
              "§8.5 session control (ACK / SAFE / FORCE) = ctl 1 + request_id 8 + tag 16");
static_assert(fixed_overhead_of(true,  header_bytes_of(false, false, false, true))  == kRemoteOverheadAuthResponse,
              "§8.7 authenticated session response = ctl 1 + request_id 8 + response_seq 1 + tag 16");
static_assert(fixed_overhead_of(false, header_bytes_of(false, false, false, true))  == kRemoteOverheadOpenResponse,
              "§8.8 open response = ctl 1 + request_id 8 + response_seq 1");
static_assert(fixed_overhead_of(true,  header_bytes_of(false, true,  false, false)) == kRemoteOverheadBootstrapResponse,
              "§8.6 bootstrap response = ctl 1 + request_id 8 + admin_epoch 8 + tag 16");
static_assert(fixed_overhead_of(true,  header_bytes_of(false, true,  true,  false)) == kRemoteOverheadRolloverResult,
              "§8.6 rollover result = ctl 1 + request_id 8 + admin_epoch 8 + abandoned_count 1 + tag 16");

// The nibble split itself.
static_assert(kRemoteSlotSessionMax == 0x09, "§8.1: slots 0..9 select established ACL sessions");
static_assert(kRemoteSlotSentinel   == 0x0F, "§8.1: F is the sentinel, not a tenth session slot");
static_assert(static_cast<uint8_t>(RemoteCmdOpcode::force_rollover)  <= 0x0F, "opcodes are a nibble");
static_assert(static_cast<uint8_t>(RemoteRespOpcode::protocol_error) <= 0x0F, "opcodes are a nibble");
static_assert(kRemoteTerminalMax == static_cast<uint8_t>(RemoteTerminal::session_busy),
              "§8.9: 0x00..0x07 is the whole allocated terminal namespace; 0x08..0xFF reject");
static_assert(kRemoteMaxAadBytes == 1 + kRemoteMaxHeaderBytes + kRemoteSourceHashBytes,
              "AAD = outer_type | the exact clear header | source_hash LE32");

// Scratch sizes for the three hash inputs.
constexpr size_t kBaseMsgBytes    = kLabelBaseLen + 32 + kRemoteControllerPubBytes + kRemoteControllerPubBytes;
constexpr size_t kSessionMsgBytes = kLabelSessionLen + kRemoteKeyBytes + kRemoteEpochBytes;
constexpr size_t kNonceMsgBytes   = kLabelNonceLen + kRemoteKeyBytes + 1 /*outer*/ + 1 /*ctl*/
                                    + kRemoteRequestIdBytes + kRemoteSeqBytes + kRemoteSourceHashBytes
                                    + kRemoteEpochBytes;

}  // namespace

// =========================================================================================================
// The ONE layout/domain decision (§8.1's decoder rule: outer direction, opcode, slot class, exact length).
// =========================================================================================================
uint8_t remote_ctl(uint8_t opcode, uint8_t slot) {
    return static_cast<uint8_t>(((opcode & 0x0F) << 4) | (slot & 0x0F));
}

RemoteStatus remote_layout(uint8_t outer_type, uint8_t ctl, RemoteLayout& out) {
    out = RemoteLayout{};
    // ⛔ DIRECTION FIRST. A foreign DATA type is not a remote direction, and its byte 0 is not a `ctl`.
    if (outer_type != DATA_TYPE_REMOTE_CMD && outer_type != DATA_TYPE_REMOTE_RESP)
        return RemoteStatus::bad_outer_type;

    const uint8_t op   = static_cast<uint8_t>(ctl >> 4);
    const uint8_t slot = static_cast<uint8_t>(ctl & 0x0F);
    const bool slot_session  = slot <= kRemoteSlotSessionMax;
    const bool slot_sentinel = slot == kRemoteSlotSentinel;
    if (!slot_session && !slot_sentinel) return RemoteStatus::bad_slot;   // A..E are reserved in EVERY pairing

    RemoteLayout L{};
    L.opcode = op;
    L.slot   = slot;

    if (outer_type == DATA_TYPE_REMOTE_CMD) {
        switch (op) {
            case static_cast<uint8_t>(RemoteCmdOpcode::auth_execute):
                if (!slot_session) return RemoteStatus::bad_pairing;      // an execute never rides the sentinel
                L.domain = RemoteDomainId::cmd_auth_execute;
                L.authenticated = true; L.variable_body = true;
                break;
            case static_cast<uint8_t>(RemoteCmdOpcode::open_execute):
                if (!slot_sentinel) return RemoteStatus::bad_pairing;     // open is an OPCODE, not a fake ACL slot
                L.domain = RemoteDomainId::cmd_open_execute;
                L.variable_body = true;
                break;
            case static_cast<uint8_t>(RemoteCmdOpcode::bootstrap):
                if (!slot_sentinel) return RemoteStatus::bad_pairing;     // the row is not known yet: F, never 0..9
                L.domain = RemoteDomainId::cmd_bootstrap;
                L.authenticated = true; L.uses_base_key = true; L.has_controller_pub = true;
                break;
            case static_cast<uint8_t>(RemoteCmdOpcode::response_ack):
            case static_cast<uint8_t>(RemoteCmdOpcode::safe_rollover):
            case static_cast<uint8_t>(RemoteCmdOpcode::force_rollover):
                if (!slot_session) return RemoteStatus::bad_pairing;
                L.domain = (op == static_cast<uint8_t>(RemoteCmdOpcode::response_ack))
                               ? RemoteDomainId::cmd_response_ack
                               : (op == static_cast<uint8_t>(RemoteCmdOpcode::safe_rollover)
                                      ? RemoteDomainId::cmd_safe_rollover
                                      : RemoteDomainId::cmd_force_rollover);
                L.authenticated = true;
                break;
            default:
                return RemoteStatus::bad_opcode;                          // CMD 0x6..0xF reserved
        }
    } else {
        switch (op) {
            case static_cast<uint8_t>(RemoteRespOpcode::output):
            case static_cast<uint8_t>(RemoteRespOpcode::terminal):
            case static_cast<uint8_t>(RemoteRespOpcode::protocol_error):
                // BOTH classes are legal here: 0..9 is the authenticated session response, F the open one.
                L.variable_body    = true;
                L.has_response_seq = true;
                L.authenticated    = slot_session;
                if (op == static_cast<uint8_t>(RemoteRespOpcode::output)) {
                    L.domain = slot_session ? RemoteDomainId::resp_output_auth : RemoteDomainId::resp_output_open;
                } else if (op == static_cast<uint8_t>(RemoteRespOpcode::terminal)) {
                    L.domain = slot_session ? RemoteDomainId::resp_terminal_auth : RemoteDomainId::resp_terminal_open;
                    L.carries_result_code = true;                         // §8.9, in BOTH classes
                } else {
                    L.domain = slot_session ? RemoteDomainId::resp_protocol_error_auth
                                            : RemoteDomainId::resp_protocol_error_open;
                    // ⛔ ONLY the AUTHENTICATED protocol error owns the `already_acknowledged` code namespace.
                    //    The open envelope keeps §8.9's separate open-validation policy and is NOT given a
                    //    result-code domain here — inventing one would be a second allocation.
                    L.carries_result_code = slot_session;
                }
                break;
            case static_cast<uint8_t>(RemoteRespOpcode::bootstrap):
                // A bootstrap RESPONSE carries the ACTUAL matched slot 0..9 and never the sentinel (§8.1).
                if (!slot_session) return RemoteStatus::bad_pairing;
                L.domain = RemoteDomainId::resp_bootstrap;
                L.authenticated = true; L.uses_base_key = true;
                L.epoch_in_nonce = true; L.has_admin_epoch = true;
                break;
            case static_cast<uint8_t>(RemoteRespOpcode::rollover_result):
                if (!slot_session) return RemoteStatus::bad_pairing;
                L.domain = RemoteDomainId::resp_rollover_result;
                L.authenticated = true; L.uses_base_key = true;
                L.epoch_in_nonce = true; L.has_admin_epoch = true; L.has_abandoned_count = true;
                break;
            default:
                return RemoteStatus::bad_opcode;                          // RESP 0x5..0xF reserved
        }
    }

    L.header_bytes   = header_bytes_of(L.has_controller_pub, L.has_admin_epoch,
                                       L.has_abandoned_count, L.has_response_seq);
    L.fixed_overhead = fixed_overhead_of(L.authenticated, L.header_bytes);
    out = L;
    return RemoteStatus::ok;
}

// =========================================================================================================
// Key derivation (§8.1)
// =========================================================================================================
RemoteStatus remote_ecdh_shared(uint8_t out_shared32[32], const Identity& self, const uint8_t peer_x_pub32[32]) {
    uint8_t sh[32];
    ecdh_shared(sh, self, peer_x_pub32);           // the EXISTING primitive; it returns void and checks nothing
    if (all_zero32(sh)) {                          // every low-order peer point lands here — refuse before a key exists
        crypto_wipe(sh, sizeof sh);
        return RemoteStatus::bad_key;
    }
    for (int i = 0; i < 32; ++i) out_shared32[i] = sh[i];
    crypto_wipe(sh, sizeof sh);
    return RemoteStatus::ok;
}

RemoteStatus remote_kdf_base(uint8_t out_key32[32], const uint8_t shared32[32],
                             const uint8_t controller_ed_pub32[32], const uint8_t target_admin_ed_pub32[32]) {
    if (all_zero32(shared32)) return RemoteStatus::bad_key;   // a degenerate shared point never becomes a key

    uint8_t msg[kBaseMsgBytes];
    wire::Writer w(std::span<uint8_t>(msg, sizeof msg));
    for (size_t i = 0; i < kLabelBaseLen; ++i) w.u8(static_cast<uint8_t>(kLabelBase[i]));
    for (int i = 0; i < 32; ++i) w.u8(shared32[i]);
    // ⛔ ORDERED, and the order is the whole point: controller key first, then the TARGET ADMINISTRATION key.
    //    DM's dm_kdf sorts two 32-bit short hashes so both directions agree; this binds two FULL public keys in
    //    a fixed role order, so the two endpoints are not interchangeable and a short hash is never an input.
    for (size_t i = 0; i < kRemoteControllerPubBytes; ++i) w.u8(controller_ed_pub32[i]);
    for (size_t i = 0; i < kRemoteControllerPubBytes; ++i) w.u8(target_admin_ed_pub32[i]);
    if (!w.ok() || w.size() != sizeof msg) { crypto_wipe(msg, sizeof msg); return RemoteStatus::bad_argument; }

    uint8_t full[kDigestBytes];
    crypto_blake2b(full, kDigestBytes, msg, sizeof msg);      // BLAKE2b-512 ...
    for (size_t i = 0; i < kRemoteKeyBytes; ++i) out_key32[i] = full[i];   // ... then truncate to 32
    crypto_wipe(full, sizeof full);
    crypto_wipe(msg, sizeof msg);                             // msg held the raw shared point
    return RemoteStatus::ok;
}

RemoteStatus remote_kdf_session(uint8_t out_key32[32], const uint8_t base_key32[32], uint64_t admin_epoch) {
    uint8_t msg[kSessionMsgBytes];
    wire::Writer w(std::span<uint8_t>(msg, sizeof msg));
    for (size_t i = 0; i < kLabelSessionLen; ++i) w.u8(static_cast<uint8_t>(kLabelSession[i]));
    for (size_t i = 0; i < kRemoteKeyBytes; ++i) w.u8(base_key32[i]);
    put_u64_le(w, admin_epoch);
    if (!w.ok() || w.size() != sizeof msg) { crypto_wipe(msg, sizeof msg); return RemoteStatus::bad_argument; }

    uint8_t full[kDigestBytes];
    crypto_blake2b(full, kDigestBytes, msg, sizeof msg);
    for (size_t i = 0; i < kRemoteKeyBytes; ++i) out_key32[i] = full[i];
    crypto_wipe(full, sizeof full);
    crypto_wipe(msg, sizeof msg);                             // msg held the base key
    return RemoteStatus::ok;
}

// =========================================================================================================
// Nonce and AAD (§8.1 / §9)
// =========================================================================================================
RemoteStatus remote_nonce(uint8_t out_nonce24[24], const RemoteLayout& layout, const RemoteMessage& msg,
                          const uint8_t selected_key32[32], const RemoteSource& src) {
    if (layout.domain == RemoteDomainId::invalid) return RemoteStatus::bad_pairing;
    // ⛔ An OPEN body has NO nonce. Producing a zero/placeholder nonce for it would let an unauthenticated
    //    domain join an authenticated inequality claim, which is exactly the false-safety the design forbids.
    if (!layout.authenticated) return RemoteStatus::bad_pairing;
    if (!src.present) return RemoteStatus::bad_argument;      // no absent-source fallback (§9)

    uint8_t m[kNonceMsgBytes];
    wire::Writer w(std::span<uint8_t>(m, sizeof m));
    for (size_t i = 0; i < kLabelNonceLen; ++i) w.u8(static_cast<uint8_t>(kLabelNonce[i]));
    for (size_t i = 0; i < kRemoteKeyBytes; ++i) w.u8(selected_key32[i]);
    w.u8(msg.outer_type);
    w.u8(remote_ctl(layout.opcode, layout.slot));
    put_u64_le(w, msg.request_id);
    w.u8(layout.has_response_seq ? msg.response_seq : uint8_t{0});   // requests use response_seq ZERO
    w.u32_le(src.hash);
    // ⛔ THE EPOCH IS AN INPUT FOR EXACTLY TWO DOMAINS — the bootstrap RESPONSE and the rollover result. A
    //    bootstrap REQUEST cannot include an epoch the controller does not yet know (§8.1), so the field is
    //    absent from its preimage rather than present-and-zero.
    if (layout.epoch_in_nonce) put_u64_le(w, msg.admin_epoch);
    const size_t used = w.size();
    if (!w.ok()) { crypto_wipe(m, sizeof m); return RemoteStatus::bad_argument; }

    uint8_t full[kDigestBytes];
    crypto_blake2b(full, kDigestBytes, m, used);              // BLAKE2b-512 ...
    for (size_t i = 0; i < kRemoteNonceBytes; ++i) out_nonce24[i] = full[i];   // ... truncated to 24
    crypto_wipe(full, sizeof full);
    crypto_wipe(m, sizeof m);                                 // m held the selected key
    return RemoteStatus::ok;
}

namespace {
// The exact clear RPC header in wire order: ctl, request_id, [controller_pub], [admin_epoch],
// [abandoned_count], [response_seq]. No layout mixes the optional fields, so this ONE order reproduces
// every §8.2-§8.8 body header exactly, and encode/decode/AAD all read it from here.
RemoteStatus write_header(std::span<uint8_t> out, size_t& out_len,
                          const RemoteLayout& layout, const RemoteMessage& msg) {
    if (layout.has_controller_pub && msg.controller_pub.size() != kRemoteControllerPubBytes)
        return RemoteStatus::bad_argument;
    if (!layout.has_controller_pub && !msg.controller_pub.empty())
        return RemoteStatus::bad_argument;
    wire::Writer w(out);
    w.u8(remote_ctl(layout.opcode, layout.slot));
    put_u64_le(w, msg.request_id);
    if (layout.has_controller_pub)  for (uint8_t b : msg.controller_pub) w.u8(b);
    if (layout.has_admin_epoch)     put_u64_le(w, msg.admin_epoch);
    if (layout.has_abandoned_count) w.u8(msg.abandoned_count);
    if (layout.has_response_seq)    w.u8(msg.response_seq);
    if (!w.ok() || w.size() != layout.header_bytes) return RemoteStatus::bad_buffer;
    out_len = w.size();
    return RemoteStatus::ok;
}
}  // namespace

RemoteStatus remote_aad(std::span<uint8_t> out, size_t& out_len, const RemoteLayout& layout,
                        const RemoteMessage& msg, const RemoteSource& src) {
    if (layout.domain == RemoteDomainId::invalid) return RemoteStatus::bad_pairing;
    if (!layout.authenticated) return RemoteStatus::bad_pairing;   // an open body has no associated data
    if (!src.present) return RemoteStatus::bad_argument;
    const size_t need = size_t{1} + layout.header_bytes + kRemoteSourceHashBytes;
    if (out.size() < need) return RemoteStatus::bad_buffer;

    out[0] = msg.outer_type;                                       // the DIRECTION byte
    size_t hlen = 0;
    const RemoteStatus st = write_header(out.subspan(1, layout.header_bytes), hlen, layout, msg);
    if (st != RemoteStatus::ok) return st;
    wire::Writer tail(out.subspan(1 + hlen, kRemoteSourceHashBytes));
    tail.u32_le(src.hash);                                         // the stable logical CONTROLLER source hash
    if (!tail.ok()) return RemoteStatus::bad_buffer;
    // ⛔ Ciphertext and tag are NOT repeated here, no implicit request sequence zero is appended, and no
    //    mutable next-hop/relay/retry header is bound (§9).
    out_len = need;
    return RemoteStatus::ok;
}

// =========================================================================================================
// Carrier admission — the ONE capacity authority (R-RA-3 / R-RA-25 / R-RA-28)
// =========================================================================================================
// The derivation, term by term and each from its named production authority:
//
//   governing = min( protocol::max_payload_bytes_hard_cap        the TxItem.inner[] STORAGE bound   (241)
//                  , data_inner_cap(flags, outer_type, 255) )    the real AIR-FIT bound             (242 / 238)
//   reserved  = dm_inner_origin_bytes + dm_inner_source_hash_bytes + dm_inner_dst_hash_bytes   = 1 + 4 + 4
//   extras    = cross_layer ? 2 + path_depth : 0   +   wrapper ? 1 : 0
//   cap       = governing - reserved - extras                    (underflow REFUSES)
//
// ⛔ R-RA-28: the four DST_HASH bytes are reserved WHETHER OR NOT this leg transmits the field. Omitting it
//    never enlarges the admitted RPC body, so a hash-less leg has spare RAW packing room the admission cap
//    deliberately does not hand out — that difference is a MEASUREMENT, not a packer refusal.
// ⛔ It is NOT `dm_max_body_bytes - something`: that constant is the application-DM answer to a related but
//    different question, and copying it would create the second authority R-RA-3 exists to prevent.
RemoteStatus remote_body_cap(const RemoteCarrier& c, size_t& out_cap) {
    // ---- is this a real carrier shape at all? ------------------------------------------------------------
    if (c.outer_data_type == 0) return RemoteStatus::bad_carrier;            // type 0 is an untyped DM, not an RPC leg
    if (!c.source_hash_on_wire) return RemoteStatus::bad_carrier;            // R-RA-13: mandatory on every RPC carrier
    if (c.addr_len > 1) return RemoteStatus::bad_carrier;                    // pack_data's own rule (frame_codec.cpp:897)
    if (c.wrapper) {
        if (c.outer_data_type != DATA_TYPE_MOBILE_SEND) return RemoteStatus::bad_carrier;
        if (c.enclosed_type != DATA_TYPE_REMOTE_CMD && c.enclosed_type != DATA_TYPE_REMOTE_RESP)
            return RemoteStatus::bad_carrier;
    } else {
        if (c.outer_data_type != DATA_TYPE_REMOTE_CMD && c.outer_data_type != DATA_TYPE_REMOTE_RESP)
            return RemoteStatus::bad_carrier;
        if (c.enclosed_type != 0) return RemoteStatus::bad_carrier;
    }
    if (c.cross_layer) {
        // A WRAPPER carries the DESTINATION path and the home prepends its own layer (node_mac.cpp:928), so its
        // legal depth stops one short of the full path's. ⇒ wrapper destination depth 4 is INVALID, not a
        // 225-byte carrier (B309).
        const uint8_t max_depth = c.wrapper ? static_cast<uint8_t>(protocol::gw_env_max_hops - 1)
                                            : protocol::gw_env_max_hops;
        if (c.path_depth == 0 || c.path_depth > max_depth) return RemoteStatus::bad_carrier;
        if (c.path_cursor >= c.path_depth) return RemoteStatus::bad_carrier;   // pack_unicast_inner: cur < n_layers
    } else {
        if (c.path_depth != 0 || c.path_cursor != 0) return RemoteStatus::bad_carrier;
    }
    // pack_data refuses a CRYPTED frame with no DST_HASH (the per-DM nonce derives from the cleartext hash),
    // so that combination is not a lower cap — it is not a carrier.
    if (c.outer_crypted && !c.dst_hash_on_wire) return RemoteStatus::bad_carrier;

    // ---- the two bounds, asked of the real authorities ---------------------------------------------------
    uint8_t flags = DATA_FLAG_SOURCE_HASH;
    if (c.dst_hash_on_wire) flags = static_cast<uint8_t>(flags | DATA_FLAG_DST_HASH);
    if (c.cross_layer)      flags = static_cast<uint8_t>(flags | DATA_FLAG_CROSS_LAYER);
    if (c.outer_crypted)    flags = static_cast<uint8_t>(flags | DATA_FLAG_CRYPTED);
    // The SAME-LAYER wrapper marks its enclosed type with the byte-1 flag (node_hashlocate.cpp:1820); the
    // cross-layer wrapper prefixes the byte without the flag (node_mac.cpp:944). Neither costs an inner byte;
    // the flag is modelled so this descriptor is the real shape rather than a convenient one.
    if (c.wrapper && !c.cross_layer) flags = static_cast<uint8_t>(flags | DATA_FLAG_MS_ENCLOSED_TYPE);

    const size_t air       = data_inner_cap(flags, c.outer_data_type, protocol::lora_max_frame_bytes);
    const size_t storage   = protocol::max_payload_bytes_hard_cap;
    const size_t governing = air < storage ? air : storage;

    const size_t reserved = static_cast<size_t>(protocol::dm_inner_origin_bytes)
                          + protocol::dm_inner_source_hash_bytes
                          + protocol::dm_inner_dst_hash_bytes;
    size_t extras = 0;
    if (c.cross_layer) extras += static_cast<size_t>(2) + c.path_depth;   // [n_layers][cur][ids...]
    if (c.wrapper)     extras += 1;                                       // the enclosed-TYPE body prefix

    if (governing < reserved + extras) return RemoteStatus::bad_carrier;  // underflow: refuse, never wrap
    out_cap = governing - reserved - extras;
    return RemoteStatus::ok;
}

RemoteStatus remote_application_cap(const RemoteCarrier& carrier, const RemoteLayout& layout, size_t& out_cap) {
    if (layout.domain == RemoteDomainId::invalid) return RemoteStatus::bad_pairing;
    size_t cap = 0;
    const RemoteStatus st = remote_body_cap(carrier, cap);
    if (st != RemoteStatus::ok) return st;
    if (cap < layout.fixed_overhead) return RemoteStatus::bad_body_cap;   // even the fixed envelope will not fit
    out_cap = layout.variable_body ? cap - layout.fixed_overhead : size_t{0};
    return RemoteStatus::ok;
}

// =========================================================================================================
// Encode (== seal for an authenticated domain) and decode (== open)
// =========================================================================================================
RemoteStatus remote_body_encode(std::span<uint8_t> out, size_t& out_len, const RemoteMessage& msg,
                           std::span<const uint8_t> body, const RemoteKeys& keys, const RemoteSource& src,
                           const RemoteCarrier& carrier) {
    RemoteLayout L{};
    RemoteStatus st = remote_layout(msg.outer_type, remote_ctl(msg.opcode, msg.slot), L);
    if (st != RemoteStatus::ok) return st;
    if (!L.variable_body && !body.empty()) return RemoteStatus::bad_argument;   // a fixed layout has no payload

    // ---- ADMISSION, before anything is written. It is the SAME authority for a fixed body: nothing is small
    //      enough to bypass the carrier's cap by assumption.
    size_t cap = 0;
    st = remote_body_cap(carrier, cap);
    if (st != RemoteStatus::ok) return st;
    const size_t n_body = L.variable_body ? body.size() : size_t{0};
    const size_t total  = static_cast<size_t>(L.fixed_overhead) + n_body;
    if (total > cap) return RemoteStatus::bad_body_cap;     // refuse — never clamp, never truncate
    if (total > out.size()) return RemoteStatus::bad_buffer;

    size_t hlen = 0;
    st = write_header(out.subspan(0, L.header_bytes), hlen, L, msg);
    if (st != RemoteStatus::ok) return st;

    if (!L.authenticated) {                                 // OPEN: clear header + clear body, no key/nonce/tag
        for (size_t i = 0; i < n_body; ++i) out[hlen + i] = body[i];
        out_len = total;
        return RemoteStatus::ok;
    }

    const std::span<const uint8_t> key = L.uses_base_key ? keys.base : keys.session;
    if (!key_present(key)) return RemoteStatus::bad_key;    // the domain's key, never the other one as a fallback
    if (!src.present) return RemoteStatus::bad_argument;

    uint8_t nonce[kRemoteNonceBytes];
    st = remote_nonce(nonce, L, msg, key.data(), src);
    if (st != RemoteStatus::ok) { crypto_wipe(nonce, sizeof nonce); return st; }

    uint8_t aad[kRemoteMaxAadBytes];
    size_t  aad_len = 0;
    st = remote_aad(std::span<uint8_t>(aad, sizeof aad), aad_len, L, msg, src);
    if (st != RemoteStatus::ok) { crypto_wipe(nonce, sizeof nonce); return st; }

    // A fixed layout tags an EMPTY plaintext (§8.4/§8.5/§8.6 "no ciphertext"); never hand the primitive a
    // null pointer for that zero-length span.
    const uint8_t  empty_pt[1] = {0};
    const uint8_t* pt_src      = n_body > 0 ? body.data() : empty_pt;
    dm_seal(out.data() + hlen, out.data() + hlen + n_body, key.data(), nonce,
            aad, aad_len, pt_src, n_body);
    crypto_wipe(nonce, sizeof nonce);
    out_len = total;
    return RemoteStatus::ok;
}

RemoteStatus remote_body_decode(RemoteDecoded& out, uint8_t outer_type, std::span<const uint8_t> body,
                           const RemoteKeys& keys, const RemoteSource& src, const RemoteCarrier& carrier,
                           std::span<uint8_t> plaintext_out) {
    if (body.empty()) return RemoteStatus::bad_length;      // there is not even a ctl byte

    RemoteLayout L{};
    RemoteStatus st = remote_layout(outer_type, body[0], L);
    if (st != RemoteStatus::ok) return st;

    // ---- ADMISSION on receive too: an oversize body is refused, never truncated and never partly returned.
    size_t cap = 0;
    st = remote_body_cap(carrier, cap);
    if (st != RemoteStatus::ok) return st;
    if (body.size() > cap) return RemoteStatus::bad_body_cap;

    // ---- THE EXACT-LENGTH RULE. A fixed layout accepts neither a short prefix nor a trailing byte. A
    //      variable layout's N is the COMPLETE remaining span: no length field, no terminator, no ignored
    //      suffix. ⇒ an extra byte in an open variable body is legitimate payload, not trailing garbage.
    if (!L.variable_body) {
        if (body.size() != L.fixed_overhead) return RemoteStatus::bad_length;
    } else {
        if (body.size() < L.fixed_overhead) return RemoteStatus::bad_length;
    }
    const size_t n_body = body.size() - L.fixed_overhead;

    // ---- parse the clear header, bounded ----------------------------------------------------------------
    RemoteDecoded d{};
    d.layout             = L;
    d.msg.outer_type     = outer_type;
    d.msg.opcode         = L.opcode;
    d.msg.slot           = L.slot;
    {
        wire::Reader r(body.subspan(0, L.header_bytes));
        (void)r.u8();                                       // ctl — already consumed by remote_layout
        d.msg.request_id = get_u64_le(r);
        if (L.has_controller_pub) {
            d.msg.controller_pub = body.subspan(kRemoteCtlBytes + kRemoteRequestIdBytes,
                                                kRemoteControllerPubBytes);
            for (size_t i = 0; i < kRemoteControllerPubBytes; ++i) (void)r.u8();
        }
        if (L.has_admin_epoch)      d.msg.admin_epoch     = get_u64_le(r);
        if (L.has_abandoned_count)  d.msg.abandoned_count = r.u8();
        if (L.has_response_seq)     d.msg.response_seq    = r.u8();
        if (!r.ok()) return RemoteStatus::bad_length;
    }

    std::span<const uint8_t> payload{};
    if (L.authenticated) {
        const std::span<const uint8_t> key = L.uses_base_key ? keys.base : keys.session;
        if (!key_present(key)) return RemoteStatus::bad_key;
        if (!src.present) return RemoteStatus::bad_argument;
        if (plaintext_out.size() < n_body) return RemoteStatus::bad_buffer;

        uint8_t nonce[kRemoteNonceBytes];
        st = remote_nonce(nonce, L, d.msg, key.data(), src);
        if (st != RemoteStatus::ok) { crypto_wipe(nonce, sizeof nonce); return st; }
        uint8_t aad[kRemoteMaxAadBytes];
        size_t  aad_len = 0;
        st = remote_aad(std::span<uint8_t>(aad, sizeof aad), aad_len, L, d.msg, src);
        if (st != RemoteStatus::ok) { crypto_wipe(nonce, sizeof nonce); return st; }

        const uint8_t* ct  = body.data() + L.header_bytes;
        const uint8_t* tag = body.data() + L.header_bytes + n_body;
        uint8_t  empty_pt[1] = {0};
        uint8_t* pt_dst = n_body > 0 ? plaintext_out.data() : empty_pt;
        const bool okk = dm_open(pt_dst, key.data(), nonce, aad, aad_len, ct, n_body, tag);
        crypto_wipe(nonce, sizeof nonce);
        // ⛔ THE TAG GATES EVERYTHING. On failure nothing is published — not the plaintext, not the parsed
        //    header, not a "structural view" of it — and there is NO retry through the open decoder and no
        //    trial-open against the legacy codec. The caller's `plaintext_out` is left exactly as it was
        //    (B313: the primitive writes it only on a valid tag; it does not scrub it on failure).
        if (!okk) return RemoteStatus::auth_failed;
        payload = plaintext_out.subspan(0, n_body);
        d.authenticated = true;
    } else {
        payload = body.subspan(L.header_bytes, n_body);     // an OPEN body: explicitly UNAUTHENTICATED
        d.authenticated = false;
    }
    d.body = payload;

    // ---- the TYPED result domains (§8.9). The byte alone is never the answer: 0x00 is `completed` under
    //      TERMINAL and `already_acknowledged` under the AUTHENTICATED protocol error, and the opcode domain
    //      is what separates them.
    if (L.carries_result_code) {
        if (payload.empty()) return RemoteStatus::bad_length;         // the result code is REQUIRED
        const uint8_t rc = payload[0];
        if (L.domain == RemoteDomainId::resp_terminal_auth || L.domain == RemoteDomainId::resp_terminal_open) {
            if (rc > kRemoteTerminalMax) return RemoteStatus::bad_result_code;   // 0x08..0xFF unallocated
            d.result_kind = RemoteResultKind::terminal;
            d.terminal    = static_cast<RemoteTerminal>(rc);
        } else {
            // AUTHENTICATED PROTOCOL_ERROR: `already_acknowledged` is the whole namespace. 0x01..0xFF reject
            // even where the same byte is a valid TERMINAL code — never reinterpreted, never re-dispatched.
            if (rc != static_cast<uint8_t>(RemoteProtocolError::already_acknowledged))
                return RemoteStatus::bad_result_code;
            d.result_kind    = RemoteResultKind::protocol_error;
            d.protocol_error = RemoteProtocolError::already_acknowledged;
        }
        d.result_detail = payload.subspan(1);               // bounded detail bytes, preserved EXACTLY
    }

    out = d;                                                // published only now, and only on full success
    return RemoteStatus::ok;
}

// =========================================================================================================
// Checked request-ID creation (R-RA-5 / design §9). See remote_codec.h for the B312 boundary statement.
// =========================================================================================================
RemoteStatus remote_make_request_id(uint64_t& out_id, RemoteEntropyFn fn, void* ctx) {
    if (fn == nullptr) return RemoteStatus::entropy_failed;     // an absent provider is a failure, not a zero ID
    uint8_t b[kRemoteRequestIdBytes];
    for (size_t i = 0; i < sizeof b; ++i) b[i] = 0;
    if (!fn(ctx, b, sizeof b)) {                                // false = failure, INCLUDING a partial fill
        crypto_wipe(b, sizeof b);
        return RemoteStatus::entropy_failed;                    // out_id is left exactly as the caller had it
    }
    uint64_t v = 0;
    for (size_t i = 0; i < sizeof b; ++i) v |= static_cast<uint64_t>(b[i]) << (8 * i);   // little-endian
    crypto_wipe(b, sizeof b);
    out_id = v;                                                 // every drawn bit kept; zero is a legitimate ID
    return RemoteStatus::ok;
}

}  // namespace MESHROUTE_NS
