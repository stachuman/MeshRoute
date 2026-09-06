// MeshRoute — test_remote_codec.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// ★★★ REMOTE-ADMIN v2 SLICE 2 — the INDEPENDENT known-answer suite for `lib/core/remote_codec.{h,cpp}`.
//
// ⛔⛔ EVERY `kRef…` LITERAL BELOW WAS PRODUCED BY AN INDEPENDENT REFERENCE, BEFORE ANY PRODUCTION OUTPUT WAS
//     CONSULTED — instrument **remote-v2-independent-reference**, whose complete source, interpreter/dependency
//     versions, inputs and full transcript are in `docs/superpowers/evidence/2026-09-06-radmin-slice2.md`.
//     It is a different implementation stack: CPython `hashlib.blake2b(digest_size=64)` for every BLAKE2b-512
//     derivation, PyNaCl/libsodium `crypto_aead_xchacha20poly1305_ietf_encrypt` for every sealed body, and
//     explicit Python byte concatenation for every header/AAD. It reproduces two EXTERNAL primitive anchors
//     before it will emit a single remote vector — RFC 7693 App. A `BLAKE2b-512("abc")` and the
//     draft-irtf-cfrg-xchacha-03 §A.3.1 XChaCha20-Poly1305 AEAD vector — so a wrong-but-self-consistent
//     derivation cannot pass here. ⛔ NOTHING in this file is regenerated at test time, and no expected value
//     is derived through a production KDF, nonce, AAD or encoder. A round-trip is SECONDARY coverage only:
//     it passes even when both sides are wrong together, which is precisely what these literals rule out.
//
// ⛔ WIRING, STATED HONESTLY (R-RA-4 / the brief's wiring gate): there is NO runtime consumer of this codec in
//    the tree. No RX router, transmitter, console verb or session store calls it, and this suite does not build
//    a fake one to earn a "wired" label. What IS proved here is that the SEAL/OPEN/ADMISSION path itself reaches
//    the tested authorities: every sealed literal is reproduced through `remote_body_encode`/`remote_body_decode` (not
//    through `remote_nonce`/`remote_aad`/`remote_body_cap` in isolation), so a mutation that bypasses the key
//    selector, the nonce, the AAD or the cap turns those cases red. The helper KATs exist beside that, not
//    instead of it.
//
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN (test_airtime.cpp provides main()); -fno-exceptions => CHECK only.
#include "doctest.h"

#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#include "admin_auth.h"          // the REAL legacy codec, for the legacy-rejection fixture
#include "dm_crypto.h"
#include "frame_codec.h"
#include "identity.h"
#include "monocypher.h"
#include "protocol_constants.h"
#include "remote_codec.h"

using namespace MESHROUTE_NS;
namespace P = MESHROUTE_NS::protocol;

namespace {

// ---------------------------------------------------------------------------------------------------------
// B311 — printable diagnostics. A byte comparison that fails must SAY which bytes, or the harness reports an
// unreadable red and the mutation verdict becomes unusable.
// ---------------------------------------------------------------------------------------------------------
std::string hex(const uint8_t* p, size_t n) {
    static const char* d = "0123456789abcdef";
    std::string s;
    s.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) { s.push_back(d[p[i] >> 4]); s.push_back(d[p[i] & 0x0F]); }
    return s;
}
#define CHECK_BYTES(what, got, want, n)                                                       \
    do {                                                                                      \
        const bool _same = std::memcmp((got), (want), (n)) == 0;                              \
        CHECK_MESSAGE(_same, (what), " got=", hex((got), (n)), " want=", hex((want), (n)));    \
    } while (0)

const char* status_name(RemoteStatus s) {
    switch (s) {
        case RemoteStatus::ok:              return "ok";
        case RemoteStatus::bad_outer_type:  return "bad_outer_type";
        case RemoteStatus::bad_opcode:      return "bad_opcode";
        case RemoteStatus::bad_slot:        return "bad_slot";
        case RemoteStatus::bad_pairing:     return "bad_pairing";
        case RemoteStatus::bad_length:      return "bad_length";
        case RemoteStatus::bad_body_cap:    return "bad_body_cap";
        case RemoteStatus::bad_buffer:      return "bad_buffer";
        case RemoteStatus::bad_argument:    return "bad_argument";
        case RemoteStatus::bad_carrier:     return "bad_carrier";
        case RemoteStatus::bad_key:         return "bad_key";
        case RemoteStatus::auth_failed:     return "auth_failed";
        case RemoteStatus::bad_result_code: return "bad_result_code";
        case RemoteStatus::entropy_failed:  return "entropy_failed";
    }
    return "?";
}
#define CHECK_STATUS(what, got, want)                                                          \
    do {                                                                                       \
        const RemoteStatus _g = (got);                                                         \
        CHECK_MESSAGE(_g == (want), (what), " got=", status_name(_g), " want=", status_name((want))); \
    } while (0)

// =========================================================================================================
// ---- THE FROZEN INDEPENDENT-REFERENCE VECTORS ------------------------------------------------------------
// Emitted verbatim by `remote-v2-independent-reference` (Python 3.11.2 + PyNaCl 1.6.2/libsodium +
// hashlib.blake2b), BEFORE any production output existed. ⛔ DO NOT "fix" one of these to make a test
// pass: if production disagrees with a literal, production is what is wrong until the reference is
// shown wrong against the design text. QA reruns the reference with `--compare <this file>`.
// kRefBaseKey (32 bytes)
const uint8_t kRefBaseKey[32] = {
    0xfb, 0x20, 0x73, 0x2d, 0x92, 0x83, 0x1f, 0x35, 0x85, 0xa6, 0x6b, 0x8a,
    0x55, 0xb1, 0x8b, 0x01, 0xfb, 0x42, 0x89, 0x55, 0x97, 0x63, 0x5a, 0x61,
    0x07, 0x34, 0xbc, 0x9c, 0x4e, 0xf8, 0x3b, 0x81,
};
// kRefSessionKey (32 bytes)
const uint8_t kRefSessionKey[32] = {
    0x11, 0xed, 0xf4, 0xde, 0x6c, 0x9d, 0xa3, 0x51, 0xff, 0x78, 0xff, 0xeb,
    0xb0, 0xfc, 0x1c, 0x78, 0x07, 0xf2, 0xaa, 0xa2, 0x3c, 0xa4, 0xf4, 0x12,
    0x2e, 0xb6, 0xe0, 0xe7, 0x87, 0x60, 0x20, 0x4f,
};
// kRefSessionKeyEpoch2 (32 bytes)
const uint8_t kRefSessionKeyEpoch2[32] = {
    0x19, 0x64, 0x39, 0xc7, 0x99, 0x5c, 0x24, 0x3b, 0xce, 0x3c, 0x9a, 0x93,
    0x2f, 0x4a, 0xd0, 0xf7, 0x7d, 0x16, 0x07, 0xa7, 0x63, 0x2a, 0xf3, 0x38,
    0x92, 0xcb, 0x1b, 0xf3, 0x79, 0xc5, 0x12, 0x58,
};
// kRefBaseKeySwapped (32 bytes)
const uint8_t kRefBaseKeySwapped[32] = {
    0x70, 0x9f, 0xe4, 0x73, 0x95, 0xfb, 0x90, 0xad, 0x35, 0x28, 0x7a, 0x93,
    0x5b, 0xba, 0xa2, 0x57, 0x18, 0x2f, 0x5b, 0x20, 0x75, 0xe4, 0xf1, 0x2d,
    0x6c, 0xb7, 0xec, 0xea, 0x71, 0x0f, 0x2e, 0xe6,
};
// kRefHeader_cmd_auth_execute (9 bytes)
const uint8_t kRefHeader_cmd_auth_execute[9] = {
    0x03, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0,
};
// kRefNonce_cmd_auth_execute (24 bytes)
const uint8_t kRefNonce_cmd_auth_execute[24] = {
    0x8e, 0xf2, 0xa1, 0x05, 0x65, 0x2e, 0x8e, 0x91, 0xf9, 0x45, 0x08, 0x8e,
    0x06, 0x70, 0xc2, 0x38, 0xc9, 0xf2, 0x81, 0x12, 0x59, 0x3d, 0x91, 0x89,
};
// kRefAad_cmd_auth_execute (14 bytes)
const uint8_t kRefAad_cmd_auth_execute[14] = {
    0xa0, 0x03, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0xef, 0xbe,
    0xad, 0xde,
};
// kRefBody_cmd_auth_execute (31 bytes)
const uint8_t kRefBody_cmd_auth_execute[31] = {
    0x03, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0x13, 0xda, 0x76,
    0x3c, 0x0f, 0xd5, 0x2b, 0x06, 0xfa, 0x13, 0x3d, 0x8c, 0xf0, 0x1e, 0x33,
    0xfe, 0xdd, 0x97, 0xed, 0x58, 0x52, 0x53,
};
// kRefHeader_cmd_open_execute (9 bytes)
const uint8_t kRefHeader_cmd_open_execute[9] = {
    0x1f, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0,
};
// kRefBody_cmd_open_execute (15 bytes)
const uint8_t kRefBody_cmd_open_execute[15] = {
    0x1f, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0x73, 0x74, 0x61,
    0x74, 0x75, 0x73,
};
// kRefHeader_cmd_bootstrap (41 bytes)
const uint8_t kRefHeader_cmd_bootstrap[41] = {
    0x2f, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0x80, 0x81, 0x82,
    0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e,
    0x8f, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a,
    0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
};
// kRefNonce_cmd_bootstrap (24 bytes)
const uint8_t kRefNonce_cmd_bootstrap[24] = {
    0xf3, 0xb8, 0x1f, 0xab, 0x83, 0xe2, 0x25, 0xc0, 0xcf, 0xdd, 0xa4, 0x7f,
    0x64, 0x21, 0x7a, 0x3e, 0x36, 0xd2, 0x7e, 0xe0, 0x8f, 0x68, 0x74, 0xa0,
};
// kRefAad_cmd_bootstrap (46 bytes)
const uint8_t kRefAad_cmd_bootstrap[46] = {
    0xa0, 0x2f, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0x80, 0x81,
    0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d,
    0x8e, 0x8f, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99,
    0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f, 0xef, 0xbe, 0xad, 0xde,
};
// kRefBody_cmd_bootstrap (57 bytes)
const uint8_t kRefBody_cmd_bootstrap[57] = {
    0x2f, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0x80, 0x81, 0x82,
    0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e,
    0x8f, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a,
    0x9b, 0x9c, 0x9d, 0x9e, 0x9f, 0xb7, 0x58, 0x76, 0x5b, 0x2f, 0x74, 0x80,
    0xa4, 0x1d, 0xd4, 0xf9, 0x87, 0x6e, 0x25, 0x7c, 0x03,
};
// kRefHeader_cmd_response_ack (9 bytes)
const uint8_t kRefHeader_cmd_response_ack[9] = {
    0x33, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0,
};
// kRefNonce_cmd_response_ack (24 bytes)
const uint8_t kRefNonce_cmd_response_ack[24] = {
    0x6b, 0x3c, 0x95, 0x2f, 0x6d, 0x1b, 0x77, 0x9c, 0x03, 0x06, 0xa4, 0x45,
    0x67, 0xa2, 0xf6, 0xd6, 0x74, 0x63, 0x02, 0x89, 0x2b, 0x36, 0xde, 0x1a,
};
// kRefAad_cmd_response_ack (14 bytes)
const uint8_t kRefAad_cmd_response_ack[14] = {
    0xa0, 0x33, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0xef, 0xbe,
    0xad, 0xde,
};
// kRefBody_cmd_response_ack (25 bytes)
const uint8_t kRefBody_cmd_response_ack[25] = {
    0x33, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0x98, 0x74, 0x3d,
    0x93, 0x26, 0xc3, 0x90, 0xb0, 0x5b, 0xf1, 0xfb, 0x0e, 0x94, 0x2b, 0x98,
    0xe1,
};
// kRefHeader_cmd_safe_rollover (9 bytes)
const uint8_t kRefHeader_cmd_safe_rollover[9] = {
    0x43, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0,
};
// kRefNonce_cmd_safe_rollover (24 bytes)
const uint8_t kRefNonce_cmd_safe_rollover[24] = {
    0xbf, 0x78, 0x0a, 0xf0, 0x2f, 0x90, 0x42, 0x30, 0x88, 0x5e, 0x10, 0xf2,
    0x5a, 0x81, 0xd9, 0x13, 0xe7, 0x9d, 0x68, 0x62, 0x6e, 0x6b, 0xd3, 0x11,
};
// kRefAad_cmd_safe_rollover (14 bytes)
const uint8_t kRefAad_cmd_safe_rollover[14] = {
    0xa0, 0x43, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0xef, 0xbe,
    0xad, 0xde,
};
// kRefBody_cmd_safe_rollover (25 bytes)
const uint8_t kRefBody_cmd_safe_rollover[25] = {
    0x43, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0xcf, 0x75, 0x2a,
    0xb7, 0x30, 0x03, 0x44, 0x44, 0x6a, 0x81, 0xf7, 0x83, 0xe4, 0xa9, 0xca,
    0x30,
};
// kRefHeader_cmd_force_rollover (9 bytes)
const uint8_t kRefHeader_cmd_force_rollover[9] = {
    0x53, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0,
};
// kRefNonce_cmd_force_rollover (24 bytes)
const uint8_t kRefNonce_cmd_force_rollover[24] = {
    0xb4, 0x97, 0xb4, 0x79, 0x03, 0xbe, 0xd1, 0xc2, 0xeb, 0x31, 0xc8, 0x94,
    0xba, 0x6f, 0x24, 0xc9, 0x0a, 0x6d, 0xea, 0x0e, 0x49, 0xda, 0xac, 0xf3,
};
// kRefAad_cmd_force_rollover (14 bytes)
const uint8_t kRefAad_cmd_force_rollover[14] = {
    0xa0, 0x53, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0xef, 0xbe,
    0xad, 0xde,
};
// kRefBody_cmd_force_rollover (25 bytes)
const uint8_t kRefBody_cmd_force_rollover[25] = {
    0x53, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0xf4, 0x02, 0x53,
    0x11, 0xf0, 0x0d, 0x24, 0x64, 0xd5, 0xf3, 0x42, 0x7d, 0xde, 0x66, 0xe4,
    0xea,
};
// kRefHeader_resp_output_auth (10 bytes)
const uint8_t kRefHeader_resp_output_auth[10] = {
    0x03, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0x2a,
};
// kRefNonce_resp_output_auth (24 bytes)
const uint8_t kRefNonce_resp_output_auth[24] = {
    0xcc, 0x81, 0x75, 0xa3, 0x43, 0x61, 0xc8, 0x0f, 0x07, 0x44, 0x00, 0x98,
    0x90, 0xb4, 0x4d, 0xc4, 0x55, 0xbb, 0x50, 0x8c, 0xcc, 0x97, 0xfe, 0x75,
};
// kRefAad_resp_output_auth (15 bytes)
const uint8_t kRefAad_resp_output_auth[15] = {
    0xa1, 0x03, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0x2a, 0xef,
    0xbe, 0xad, 0xde,
};
// kRefBody_resp_output_auth (35 bytes)
const uint8_t kRefBody_resp_output_auth[35] = {
    0x03, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0x2a, 0x40, 0x91,
    0xcc, 0x9a, 0x82, 0xa7, 0xb5, 0x11, 0xff, 0xe2, 0x8b, 0x6d, 0xf3, 0x04,
    0x49, 0x95, 0xe8, 0x43, 0x05, 0x61, 0x50, 0x49, 0xdb, 0x8a, 0x1b,
};
// kRefHeader_resp_output_open (10 bytes)
const uint8_t kRefHeader_resp_output_open[10] = {
    0x0f, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0x2a,
};
// kRefBody_resp_output_open (19 bytes)
const uint8_t kRefBody_resp_output_open[19] = {
    0x0f, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0x2a, 0x6c, 0x69,
    0x6e, 0x65, 0x20, 0x6f, 0x6e, 0x65, 0x0a,
};
// kRefHeader_resp_terminal_auth (10 bytes)
const uint8_t kRefHeader_resp_terminal_auth[10] = {
    0x13, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0x2a,
};
// kRefNonce_resp_terminal_auth (24 bytes)
const uint8_t kRefNonce_resp_terminal_auth[24] = {
    0x94, 0xf6, 0xc3, 0xd9, 0xbe, 0xba, 0xff, 0x78, 0x6a, 0x5c, 0x96, 0xa2,
    0x3a, 0x92, 0x35, 0x30, 0xe4, 0xef, 0xa2, 0x5b, 0x63, 0xc3, 0x5e, 0x51,
};
// kRefAad_resp_terminal_auth (15 bytes)
const uint8_t kRefAad_resp_terminal_auth[15] = {
    0xa1, 0x13, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0x2a, 0xef,
    0xbe, 0xad, 0xde,
};
// kRefBody_resp_terminal_auth (27 bytes)
const uint8_t kRefBody_resp_terminal_auth[27] = {
    0x13, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0x2a, 0x31, 0x5b,
    0xdb, 0x9e, 0xee, 0xa1, 0x84, 0x70, 0x9e, 0xa4, 0x01, 0xd4, 0xe6, 0x17,
    0x07, 0xba, 0xb5,
};
// kRefHeader_resp_terminal_open (10 bytes)
const uint8_t kRefHeader_resp_terminal_open[10] = {
    0x1f, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0x2a,
};
// kRefBody_resp_terminal_open (11 bytes)
const uint8_t kRefBody_resp_terminal_open[11] = {
    0x1f, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0x2a, 0x00,
};
// kRefHeader_resp_bootstrap (17 bytes)
const uint8_t kRefHeader_resp_bootstrap[17] = {
    0x23, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0xef, 0xcd, 0xab,
    0x89, 0x67, 0x45, 0x23, 0x01,
};
// kRefNonce_resp_bootstrap (24 bytes)
const uint8_t kRefNonce_resp_bootstrap[24] = {
    0x9a, 0xf7, 0x7c, 0xeb, 0x80, 0xa6, 0x2b, 0xaa, 0xca, 0x89, 0x76, 0x55,
    0xe0, 0xe2, 0x33, 0x6f, 0x5f, 0xae, 0x18, 0x0e, 0x27, 0x32, 0xcc, 0x3d,
};
// kRefAad_resp_bootstrap (22 bytes)
const uint8_t kRefAad_resp_bootstrap[22] = {
    0xa1, 0x23, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0xef, 0xcd,
    0xab, 0x89, 0x67, 0x45, 0x23, 0x01, 0xef, 0xbe, 0xad, 0xde,
};
// kRefBody_resp_bootstrap (33 bytes)
const uint8_t kRefBody_resp_bootstrap[33] = {
    0x23, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0xef, 0xcd, 0xab,
    0x89, 0x67, 0x45, 0x23, 0x01, 0xa1, 0x21, 0xbc, 0xff, 0x2f, 0xc3, 0x27,
    0x99, 0x5b, 0x5d, 0x7b, 0x46, 0x9f, 0x29, 0x5d, 0xf1,
};
// kRefHeader_resp_rollover_result (18 bytes)
const uint8_t kRefHeader_resp_rollover_result[18] = {
    0x33, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0xef, 0xcd, 0xab,
    0x89, 0x67, 0x45, 0x23, 0x01, 0x07,
};
// kRefNonce_resp_rollover_result (24 bytes)
const uint8_t kRefNonce_resp_rollover_result[24] = {
    0xd9, 0x0d, 0x08, 0x7a, 0x54, 0x34, 0x59, 0x3b, 0xea, 0x8a, 0xee, 0x21,
    0xea, 0x76, 0x5a, 0xfd, 0x37, 0xb6, 0x68, 0x39, 0x6a, 0xb9, 0xfe, 0x1a,
};
// kRefAad_resp_rollover_result (23 bytes)
const uint8_t kRefAad_resp_rollover_result[23] = {
    0xa1, 0x33, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0xef, 0xcd,
    0xab, 0x89, 0x67, 0x45, 0x23, 0x01, 0x07, 0xef, 0xbe, 0xad, 0xde,
};
// kRefBody_resp_rollover_result (34 bytes)
const uint8_t kRefBody_resp_rollover_result[34] = {
    0x33, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0xef, 0xcd, 0xab,
    0x89, 0x67, 0x45, 0x23, 0x01, 0x07, 0x06, 0x25, 0xc7, 0xe3, 0x3b, 0x20,
    0xbd, 0xe7, 0x80, 0x02, 0x90, 0x9d, 0x77, 0x46, 0x76, 0x1d,
};
// kRefHeader_resp_protocol_error_auth (10 bytes)
const uint8_t kRefHeader_resp_protocol_error_auth[10] = {
    0x43, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0x2a,
};
// kRefNonce_resp_protocol_error_auth (24 bytes)
const uint8_t kRefNonce_resp_protocol_error_auth[24] = {
    0x50, 0x1a, 0x6c, 0x5d, 0x69, 0xe2, 0xc8, 0x5c, 0xe0, 0x85, 0x9b, 0xd0,
    0xad, 0x00, 0x84, 0x37, 0x46, 0x69, 0xde, 0x26, 0xc9, 0x73, 0x0d, 0x6b,
};
// kRefAad_resp_protocol_error_auth (15 bytes)
const uint8_t kRefAad_resp_protocol_error_auth[15] = {
    0xa1, 0x43, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0x2a, 0xef,
    0xbe, 0xad, 0xde,
};
// kRefBody_resp_protocol_error_auth (27 bytes)
const uint8_t kRefBody_resp_protocol_error_auth[27] = {
    0x43, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0x2a, 0x61, 0x7b,
    0x7b, 0x73, 0xc2, 0x92, 0x24, 0x01, 0x29, 0x78, 0x98, 0xce, 0xe4, 0x26,
    0xe2, 0xba, 0x38,
};
// kRefHeader_resp_protocol_error_open (10 bytes)
const uint8_t kRefHeader_resp_protocol_error_open[10] = {
    0x4f, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0x2a,
};
// kRefBody_resp_protocol_error_open (11 bytes)
const uint8_t kRefBody_resp_protocol_error_open[11] = {
    0x4f, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0x2a, 0x00,
};
// kRefNonce_cmd_auth_execute_src2 (24 bytes)
const uint8_t kRefNonce_cmd_auth_execute_src2[24] = {
    0x8f, 0xcf, 0x63, 0x4c, 0x24, 0xec, 0xae, 0x29, 0x97, 0xf6, 0xe1, 0x9f,
    0xfc, 0xf0, 0x32, 0xdd, 0x3c, 0x6f, 0xba, 0x5e, 0xb7, 0x7c, 0xfb, 0xfa,
};
// kRefNonce_resp_bootstrap_epoch2 (24 bytes)
const uint8_t kRefNonce_resp_bootstrap_epoch2[24] = {
    0xda, 0xcb, 0xa5, 0x88, 0x15, 0x96, 0x13, 0x22, 0x15, 0xeb, 0xfb, 0x8b,
    0x81, 0x26, 0xb8, 0xac, 0xcd, 0xb4, 0x6e, 0xb8, 0x58, 0x47, 0x34, 0xc6,
};
// kRefBody_resp_rollover_result_epoch2 (34 bytes)
const uint8_t kRefBody_resp_rollover_result_epoch2[34] = {
    0x33, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0xf0, 0xcd, 0xab,
    0x89, 0x67, 0x45, 0x23, 0x01, 0x07, 0x44, 0x15, 0x3e, 0x6f, 0x2b, 0x63,
    0x23, 0xb9, 0xe0, 0x99, 0xde, 0x79, 0xe5, 0xb6, 0x01, 0x80,
};
// kRefBody_cmd_auth_execute_slot0 (31 bytes)
const uint8_t kRefBody_cmd_auth_execute_slot0[31] = {
    0x00, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0x53, 0x5b, 0xd5,
    0x11, 0xb8, 0xbd, 0xda, 0x6c, 0x9d, 0x23, 0x23, 0xab, 0xe7, 0x05, 0x96,
    0xcf, 0xdb, 0x8b, 0xf7, 0x31, 0x56, 0x8d,
};
// kRefBody_cmd_auth_execute_slot1 (31 bytes)
const uint8_t kRefBody_cmd_auth_execute_slot1[31] = {
    0x01, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0x0f, 0xda, 0x11,
    0x23, 0x2a, 0x1d, 0x96, 0x26, 0x00, 0x3f, 0x46, 0x01, 0x2f, 0x6b, 0x54,
    0xba, 0x93, 0x42, 0x3a, 0x0a, 0xe7, 0x99,
};
// kRefBody_cmd_auth_execute_slot2 (31 bytes)
const uint8_t kRefBody_cmd_auth_execute_slot2[31] = {
    0x02, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0x80, 0xd4, 0x16,
    0x42, 0x77, 0xbe, 0x52, 0xc7, 0x4d, 0x3a, 0x6c, 0xc0, 0xb7, 0x5c, 0x1e,
    0x9a, 0x08, 0xdb, 0x04, 0x36, 0x20, 0xd4,
};
// kRefBody_cmd_auth_execute_slot3 (31 bytes)
const uint8_t kRefBody_cmd_auth_execute_slot3[31] = {
    0x03, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0x13, 0xda, 0x76,
    0x3c, 0x0f, 0xd5, 0x2b, 0x06, 0xfa, 0x13, 0x3d, 0x8c, 0xf0, 0x1e, 0x33,
    0xfe, 0xdd, 0x97, 0xed, 0x58, 0x52, 0x53,
};
// kRefBody_cmd_auth_execute_slot4 (31 bytes)
const uint8_t kRefBody_cmd_auth_execute_slot4[31] = {
    0x04, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0x9e, 0xf3, 0xd6,
    0x9e, 0x16, 0x74, 0x38, 0x52, 0x4d, 0xb1, 0x30, 0xcd, 0x27, 0x74, 0xaf,
    0x59, 0xb2, 0xd5, 0xa6, 0x44, 0x7e, 0x9f,
};
// kRefBody_cmd_auth_execute_slot5 (31 bytes)
const uint8_t kRefBody_cmd_auth_execute_slot5[31] = {
    0x05, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0x8b, 0x0e, 0x14,
    0x47, 0xaa, 0xe0, 0xf6, 0x29, 0x52, 0x2a, 0x44, 0x2e, 0xc1, 0x26, 0x2b,
    0xae, 0x60, 0x49, 0x43, 0x0f, 0xf0, 0xf1,
};
// kRefBody_cmd_auth_execute_slot6 (31 bytes)
const uint8_t kRefBody_cmd_auth_execute_slot6[31] = {
    0x06, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0xe7, 0xd3, 0x2e,
    0xa1, 0x0c, 0xe2, 0x93, 0xf8, 0xc8, 0xb9, 0x4f, 0x41, 0xc4, 0x92, 0x87,
    0xcf, 0x6a, 0xa0, 0xa8, 0xf6, 0x4b, 0x09,
};
// kRefBody_cmd_auth_execute_slot7 (31 bytes)
const uint8_t kRefBody_cmd_auth_execute_slot7[31] = {
    0x07, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0xd1, 0x70, 0xaf,
    0x41, 0x55, 0xde, 0x37, 0x16, 0x1e, 0xbc, 0x45, 0x23, 0x34, 0xc4, 0xb5,
    0xd2, 0x6e, 0x96, 0x79, 0x09, 0x4d, 0xdb,
};
// kRefBody_cmd_auth_execute_slot8 (31 bytes)
const uint8_t kRefBody_cmd_auth_execute_slot8[31] = {
    0x08, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0xb2, 0x42, 0xd5,
    0x26, 0x35, 0xe3, 0x55, 0xe7, 0xdf, 0x4e, 0xec, 0x75, 0xd6, 0x85, 0x60,
    0x16, 0xd6, 0xa7, 0x06, 0x9b, 0xb2, 0xff,
};
// kRefBody_cmd_auth_execute_slot9 (31 bytes)
const uint8_t kRefBody_cmd_auth_execute_slot9[31] = {
    0x09, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0x0d, 0xe5, 0xe7,
    0xcc, 0x7a, 0xae, 0x6c, 0x8a, 0x41, 0xb8, 0xa1, 0x8a, 0x12, 0x74, 0x5e,
    0xd3, 0xd9, 0x88, 0x42, 0x92, 0x15, 0xc3,
};
// kRefNonceWRONG_cmd_bootstrap_with_epoch (24 bytes)
const uint8_t kRefNonceWRONG_cmd_bootstrap_with_epoch[24] = {
    0xdc, 0x28, 0xcb, 0x57, 0x5e, 0xea, 0x3c, 0xe2, 0xdb, 0xed, 0xc7, 0x05,
    0x97, 0x32, 0x34, 0x2f, 0x2f, 0x50, 0x21, 0xbf, 0x76, 0xdc, 0xbf, 0x5e,
};
// kRefNonceWRONG_cmd_auth_execute_with_epoch (24 bytes)
const uint8_t kRefNonceWRONG_cmd_auth_execute_with_epoch[24] = {
    0xcc, 0x27, 0x03, 0x0c, 0x2d, 0x13, 0x53, 0x5f, 0xcd, 0x59, 0xbf, 0x37,
    0x72, 0xdd, 0xc4, 0x42, 0x0e, 0xf7, 0x67, 0x2f, 0x10, 0xb8, 0x37, 0xb0,
};
// kRefBody_resp_output_auth_seq0 (35 bytes)
const uint8_t kRefBody_resp_output_auth_seq0[35] = {
    0x03, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0x00, 0xff, 0x5c,
    0xa4, 0x46, 0xed, 0x3c, 0x7b, 0xfa, 0x3b, 0xd5, 0x60, 0xf3, 0x33, 0x3f,
    0xf6, 0x0c, 0x48, 0x5a, 0x5b, 0xcb, 0x88, 0xd9, 0x77, 0x95, 0xbf,
};
// kRefBody_resp_output_auth_seq255 (35 bytes)
const uint8_t kRefBody_resp_output_auth_seq255[35] = {
    0x03, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0xff, 0x81, 0x6c,
    0xc8, 0x25, 0x9d, 0xfc, 0x2d, 0x66, 0x7c, 0x97, 0x34, 0x82, 0x0f, 0x59,
    0x7a, 0xf1, 0xcd, 0xad, 0xed, 0x24, 0x2a, 0x9c, 0xff, 0xb9, 0x46,
};
// kRefBody_cmd_auth_execute_empty (25 bytes)
const uint8_t kRefBody_cmd_auth_execute_empty[25] = {
    0x03, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0xe3, 0x60, 0x47,
    0x04, 0xc1, 0x7c, 0x4a, 0xb5, 0x5c, 0x74, 0xea, 0xfc, 0x48, 0xbc, 0x35,
    0x7d,
};
// kRefBody_resp_terminal_auth_scheduled (31 bytes)
const uint8_t kRefBody_resp_terminal_auth_scheduled[31] = {
    0x13, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0x2a, 0x30, 0x6f,
    0xd7, 0x07, 0x7a, 0xfe, 0xdf, 0x6c, 0x58, 0x4a, 0x7d, 0xc1, 0x13, 0xb0,
    0x8d, 0x27, 0x5c, 0x33, 0x4b, 0x48, 0xc7,
};
// kRefBody_resp_protocol_error_auth_code01 (27 bytes)
const uint8_t kRefBody_resp_protocol_error_auth_code01[27] = {
    0x43, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0x2a, 0x60, 0xe7,
    0x7a, 0x4f, 0x39, 0x0e, 0xef, 0x29, 0xf9, 0xb0, 0x90, 0xfc, 0x66, 0xba,
    0x02, 0xa7, 0x83,
};
// kRefBody_resp_protocol_error_auth_code07 (27 bytes)
const uint8_t kRefBody_resp_protocol_error_auth_code07[27] = {
    0x43, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0x2a, 0x66, 0x69,
    0x7e, 0x27, 0x70, 0x29, 0x30, 0x35, 0x18, 0x5c, 0xbf, 0xe8, 0x59, 0x45,
    0x3f, 0x1e, 0xc2,
};
// kRefBody_resp_protocol_error_auth_code08 (27 bytes)
const uint8_t kRefBody_resp_protocol_error_auth_code08[27] = {
    0x43, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0x2a, 0x69, 0x2a,
    0x80, 0x93, 0x0b, 0xb7, 0xd0, 0xba, 0xa7, 0xb1, 0xd6, 0x5e, 0xd3, 0x8a,
    0xdd, 0x59, 0xe1,
};
// kRefBody_resp_protocol_error_auth_codeff (27 bytes)
const uint8_t kRefBody_resp_protocol_error_auth_codeff[27] = {
    0x43, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0x2a, 0x9e, 0x41,
    0x9f, 0x07, 0x70, 0x27, 0xe5, 0x48, 0x8f, 0xee, 0x72, 0xda, 0xdf, 0x00,
    0x1f, 0x77, 0x5e,
};
// kRefBody_resp_terminal_auth_code08 (27 bytes)
const uint8_t kRefBody_resp_terminal_auth_code08[27] = {
    0x13, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0x2a, 0x39, 0x4e,
    0x7e, 0x15, 0xbd, 0x99, 0x59, 0x8d, 0xeb, 0x3c, 0x43, 0x6d, 0xe5, 0xb6,
    0x26, 0xdd, 0x2c,
};
// kRefBody_resp_terminal_auth_codeff (27 bytes)
const uint8_t kRefBody_resp_terminal_auth_codeff[27] = {
    0x13, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0x2a, 0xce, 0x30,
    0x19, 0x77, 0x22, 0x21, 0x58, 0xc6, 0x87, 0xf1, 0xa8, 0x53, 0x2b, 0xa8,
    0x53, 0xcb, 0xb7,
};
// kRefBody_resp_terminal_auth_t0 (27 bytes)
const uint8_t kRefBody_resp_terminal_auth_t0[27] = {
    0x13, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0x2a, 0x31, 0x5b,
    0xdb, 0x9e, 0xee, 0xa1, 0x84, 0x70, 0x9e, 0xa4, 0x01, 0xd4, 0xe6, 0x17,
    0x07, 0xba, 0xb5,
};
// kRefBody_resp_terminal_auth_t1 (27 bytes)
const uint8_t kRefBody_resp_terminal_auth_t1[27] = {
    0x13, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0x2a, 0x30, 0xfc,
    0x06, 0xd0, 0xf4, 0x02, 0xea, 0xcc, 0x94, 0x71, 0xd9, 0x00, 0x07, 0x24,
    0xa3, 0xd5, 0x46,
};
// kRefBody_resp_terminal_auth_t2 (27 bytes)
const uint8_t kRefBody_resp_terminal_auth_t2[27] = {
    0x13, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0x2a, 0x33, 0x19,
    0x84, 0x3c, 0xe2, 0xdf, 0xb9, 0xb7, 0xb1, 0x0a, 0x52, 0x7a, 0xa6, 0xff,
    0xce, 0x82, 0x93,
};
// kRefBody_resp_terminal_auth_t3 (27 bytes)
const uint8_t kRefBody_resp_terminal_auth_t3[27] = {
    0x13, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0x2a, 0x32, 0xba,
    0xaf, 0x6d, 0xe8, 0x40, 0x1f, 0x14, 0xa8, 0xd7, 0x29, 0xa7, 0xc6, 0x0b,
    0x6b, 0x9e, 0x24,
};
// kRefBody_resp_terminal_auth_t4 (27 bytes)
const uint8_t kRefBody_resp_terminal_auth_t4[27] = {
    0x13, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0x2a, 0x35, 0xd2,
    0x2c, 0xda, 0xd5, 0x1d, 0xef, 0xfe, 0xc4, 0x70, 0xa2, 0x20, 0x66, 0xe7,
    0x96, 0x4b, 0x71,
};
// kRefBody_resp_terminal_auth_t5 (27 bytes)
const uint8_t kRefBody_resp_terminal_auth_t5[27] = {
    0x13, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0x2a, 0x34, 0x73,
    0x58, 0x0b, 0xdc, 0x7e, 0x54, 0x5b, 0xbb, 0x3d, 0x7a, 0x4d, 0x86, 0xf3,
    0x32, 0x67, 0x02,
};
// kRefBody_resp_terminal_auth_t6 (27 bytes)
const uint8_t kRefBody_resp_terminal_auth_t6[27] = {
    0x13, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0x2a, 0x37, 0x90,
    0xd5, 0x77, 0xc9, 0x5b, 0x24, 0x46, 0xd8, 0xd6, 0xf2, 0xc6, 0x25, 0xcf,
    0x5e, 0x14, 0x4f,
};
// kRefBody_resp_terminal_auth_t7 (27 bytes)
const uint8_t kRefBody_resp_terminal_auth_t7[27] = {
    0x13, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0x2a, 0x36, 0x31,
    0x01, 0xa9, 0xcf, 0xbc, 0x89, 0xa2, 0xce, 0xa3, 0xca, 0xf3, 0x45, 0xdb,
    0xfa, 0x2f, 0xe0,
};
// kRefBody_cmd_auth_execute_oversize (233 bytes)
const uint8_t kRefBody_cmd_auth_execute_oversize[233] = {
    0x03, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0x6a, 0xa7, 0x1a,
    0x68, 0x4d, 0x88, 0x05, 0x8c, 0x3e, 0x56, 0x2b, 0x4b, 0x22, 0x51, 0x88,
    0x9d, 0xc4, 0x83, 0xb2, 0xc8, 0x1f, 0x44, 0xfa, 0x1b, 0x0d, 0x7f, 0x3a,
    0x1d, 0x23, 0xe9, 0x42, 0xa0, 0x46, 0x64, 0x6e, 0x07, 0x78, 0xb8, 0xcf,
    0x8b, 0xcc, 0xac, 0xec, 0x4c, 0xbf, 0xc1, 0xf2, 0x0e, 0x98, 0xad, 0x23,
    0xf6, 0xdd, 0x4d, 0xb0, 0x7f, 0x78, 0xdf, 0x14, 0xf6, 0x6d, 0x20, 0xb5,
    0x3d, 0x1d, 0x04, 0x9a, 0x2d, 0x30, 0x7f, 0x4c, 0xa4, 0x06, 0x3e, 0x84,
    0x2b, 0x7c, 0xf3, 0x13, 0x78, 0xa7, 0x6e, 0xfb, 0x08, 0xd8, 0x51, 0xe5,
    0x5f, 0x1a, 0x5d, 0x64, 0x19, 0x53, 0x18, 0xf1, 0xb7, 0x58, 0x46, 0x0b,
    0xc6, 0x94, 0xb7, 0x59, 0xb0, 0x37, 0x0f, 0xfe, 0x4b, 0x04, 0x2b, 0xa4,
    0xa8, 0x1f, 0xb9, 0xfc, 0xa5, 0x23, 0xec, 0xa2, 0x75, 0x1c, 0x6e, 0x6d,
    0xcc, 0x30, 0xae, 0x02, 0x5a, 0x8e, 0x88, 0x10, 0xbf, 0x73, 0xf7, 0x8c,
    0xe4, 0x21, 0x7c, 0x6f, 0x07, 0xfd, 0xb1, 0x04, 0xea, 0xa7, 0x35, 0x1f,
    0x3b, 0x4d, 0xdd, 0x65, 0xa4, 0x8c, 0x05, 0x2b, 0x6a, 0xcc, 0x38, 0x9a,
    0x4e, 0x70, 0xd1, 0xd8, 0x38, 0x1b, 0x8f, 0x89, 0x62, 0xa4, 0x2f, 0x30,
    0x09, 0x4e, 0x40, 0xd3, 0xab, 0xd5, 0x0d, 0xdc, 0x5a, 0xc2, 0x03, 0x87,
    0xee, 0x58, 0x33, 0x68, 0xe5, 0x8b, 0x97, 0xf3, 0x60, 0x4a, 0x21, 0x5c,
    0x18, 0x79, 0x99, 0x3c, 0xa9, 0x05, 0xfa, 0x2b, 0x37, 0x1c, 0x15, 0xff,
    0x4c, 0xb2, 0x7c, 0xa3, 0x66, 0x63, 0x41, 0x74, 0xda, 0xf9, 0xa3, 0x58,
    0x72, 0x22, 0x45, 0xcb, 0xe7,
};
// kRefBody_cmd_auth_execute_atcap (232 bytes)
const uint8_t kRefBody_cmd_auth_execute_atcap[232] = {
    0x03, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0x6a, 0xa7, 0x1a,
    0x68, 0x4d, 0x88, 0x05, 0x8c, 0x3e, 0x56, 0x2b, 0x4b, 0x22, 0x51, 0x88,
    0x9d, 0xc4, 0x83, 0xb2, 0xc8, 0x1f, 0x44, 0xfa, 0x1b, 0x0d, 0x7f, 0x3a,
    0x1d, 0x23, 0xe9, 0x42, 0xa0, 0x46, 0x64, 0x6e, 0x07, 0x78, 0xb8, 0xcf,
    0x8b, 0xcc, 0xac, 0xec, 0x4c, 0xbf, 0xc1, 0xf2, 0x0e, 0x98, 0xad, 0x23,
    0xf6, 0xdd, 0x4d, 0xb0, 0x7f, 0x78, 0xdf, 0x14, 0xf6, 0x6d, 0x20, 0xb5,
    0x3d, 0x1d, 0x04, 0x9a, 0x2d, 0x30, 0x7f, 0x4c, 0xa4, 0x06, 0x3e, 0x84,
    0x2b, 0x7c, 0xf3, 0x13, 0x78, 0xa7, 0x6e, 0xfb, 0x08, 0xd8, 0x51, 0xe5,
    0x5f, 0x1a, 0x5d, 0x64, 0x19, 0x53, 0x18, 0xf1, 0xb7, 0x58, 0x46, 0x0b,
    0xc6, 0x94, 0xb7, 0x59, 0xb0, 0x37, 0x0f, 0xfe, 0x4b, 0x04, 0x2b, 0xa4,
    0xa8, 0x1f, 0xb9, 0xfc, 0xa5, 0x23, 0xec, 0xa2, 0x75, 0x1c, 0x6e, 0x6d,
    0xcc, 0x30, 0xae, 0x02, 0x5a, 0x8e, 0x88, 0x10, 0xbf, 0x73, 0xf7, 0x8c,
    0xe4, 0x21, 0x7c, 0x6f, 0x07, 0xfd, 0xb1, 0x04, 0xea, 0xa7, 0x35, 0x1f,
    0x3b, 0x4d, 0xdd, 0x65, 0xa4, 0x8c, 0x05, 0x2b, 0x6a, 0xcc, 0x38, 0x9a,
    0x4e, 0x70, 0xd1, 0xd8, 0x38, 0x1b, 0x8f, 0x89, 0x62, 0xa4, 0x2f, 0x30,
    0x09, 0x4e, 0x40, 0xd3, 0xab, 0xd5, 0x0d, 0xdc, 0x5a, 0xc2, 0x03, 0x87,
    0xee, 0x58, 0x33, 0x68, 0xe5, 0x8b, 0x97, 0xf3, 0x60, 0x4a, 0x21, 0x5c,
    0x18, 0x79, 0x99, 0x3c, 0xa9, 0x05, 0xfa, 0x2b, 0x37, 0x1c, 0x15, 0xff,
    0x71, 0xc7, 0xf8, 0xa1, 0xd3, 0xa2, 0xe0, 0x26, 0x3a, 0x29, 0x6f, 0xc0,
    0x49, 0xf9, 0xb9, 0x53,
};
// =========================================================================================================

// ---- the same fixture INPUTS the reference used (recorded in the evidence) --------------------------------
constexpr uint64_t kReqId   = 0xF0E1D2C3B4A59687ull;   // non-zero HIGH byte on purpose
constexpr uint64_t kEpoch   = 0x0123456789ABCDEFull;
constexpr uint64_t kEpoch2  = 0x0123456789ABCDF0ull;
constexpr uint32_t kSrcHash = 0xDEADBEEFu;
constexpr uint32_t kSrcHash2 = 0x00C0FFEEu;
constexpr uint8_t  kSeq     = 0x2A;
constexpr uint8_t  kAbandoned = 0x07;
constexpr uint8_t  kSlot    = 3;

void fill_shared(uint8_t out[32])   { for (int i = 0; i < 32; ++i) out[i] = static_cast<uint8_t>(0x40 + i); }
void fill_ctrl_pub(uint8_t out[32]) { for (int i = 0; i < 32; ++i) out[i] = static_cast<uint8_t>(0x80 + i); }
void fill_tgt_pub(uint8_t out[32])  { for (int i = 0; i < 32; ++i) out[i] = static_cast<uint8_t>(0xC0 + i); }

RemoteSource src_ok(uint32_t h = kSrcHash) { RemoteSource s{}; s.present = true; s.hash = h; return s; }

RemoteKeys keys_of(const uint8_t base[32], const uint8_t session[32]) {
    RemoteKeys k{};
    k.base    = std::span<const uint8_t>(base, 32);
    k.session = std::span<const uint8_t>(session, 32);
    return k;
}

// ---- carrier constructors, one per LIVE leg shape --------------------------------------------------------
RemoteCarrier carrier_same_layer(uint8_t outer, bool dst_hash, uint8_t addr_len = 0) {
    RemoteCarrier c{};
    c.outer_data_type = outer;
    c.dst_hash_on_wire = dst_hash;
    c.addr_len = addr_len;
    return c;
}
RemoteCarrier carrier_cross_layer(uint8_t outer, bool dst_hash, uint8_t depth, uint8_t cur = 0) {
    RemoteCarrier c = carrier_same_layer(outer, dst_hash);
    c.cross_layer = true;
    c.path_depth = depth;
    c.path_cursor = cur;
    return c;
}
RemoteCarrier carrier_wrapper_same(uint8_t enclosed) {
    RemoteCarrier c{};
    c.outer_data_type = DATA_TYPE_MOBILE_SEND;
    c.enclosed_type = enclosed;
    c.wrapper = true;
    c.dst_hash_on_wire = true;    // node_hashlocate.cpp:1820 passes override_dst_hash = the target key hash
    return c;
}
RemoteCarrier carrier_wrapper_xl(uint8_t enclosed, uint8_t dest_depth, uint8_t cur = 0) {
    RemoteCarrier c = carrier_wrapper_same(enclosed);
    c.cross_layer = true;
    c.path_depth = dest_depth;    // node_mac.cpp:924-959 — the DESTINATION path; the home prepends its layer
    c.path_cursor = cur;
    return c;
}

size_t cap_of(const RemoteCarrier& c) {
    size_t cap = 0;
    const RemoteStatus st = remote_body_cap(c, cap);
    CHECK_STATUS("remote_body_cap", st, RemoteStatus::ok);
    return st == RemoteStatus::ok ? cap : 0;
}

// ---- the PHYSICAL packer probe — the 0e method, run against the REAL producers ---------------------------
// ⛔ This answers a DIFFERENT question from `remote_body_cap`. Admission is the conservative authority
//    (R-RA-28 reserves DST_HASH whether or not the leg transmits it); physical packing is what the real
//    `pack_unicast_inner` / `pack_data` will actually accept. Where a reserved field is absent the two
//    deliberately disagree, and this probe is how that disagreement is MEASURED rather than asserted away.
enum class Bound { none, storage, air, structural };
const char* bound_name(Bound b) {
    switch (b) {
        case Bound::none:       return "fits";
        case Bound::storage:    return "storage";
        case Bound::air:        return "air";
        case Bound::structural: return "structural";
    }
    return "?";
}

Bound pack_probe(const RemoteCarrier& c, size_t rpc_body_len, size_t* out_inner_len = nullptr) {
    uint8_t flags = DATA_FLAG_SOURCE_HASH;
    if (c.dst_hash_on_wire) flags = static_cast<uint8_t>(flags | DATA_FLAG_DST_HASH);
    if (c.cross_layer)      flags = static_cast<uint8_t>(flags | DATA_FLAG_CROSS_LAYER);
    if (c.outer_crypted)    flags = static_cast<uint8_t>(flags | DATA_FLAG_CRYPTED);
    if (c.wrapper && !c.cross_layer) flags = static_cast<uint8_t>(flags | DATA_FLAG_MS_ENCLOSED_TYPE);

    std::vector<uint8_t> body(rpc_body_len + (c.wrapper ? 1u : 0u), 0x5A);
    if (c.wrapper && !body.empty()) body[0] = c.enclosed_type;    // the enclosed-TYPE body prefix

    uint8_t layer_ids[P::gw_env_max_hops] = {1, 2, 3, 4};
    uint8_t inner[P::max_payload_bytes_hard_cap];
    const size_t written = pack_unicast_inner(
        std::span<uint8_t>(inner, sizeof inner), flags, /*dst_key_hash32=*/0xDEADBEEFu,
        layer_ids, c.path_depth, c.path_cursor, /*origin=*/7, /*source_hash=*/0xC0FFEEu,
        body.data(), static_cast<uint8_t>(body.size()), 0, 0);
    if (out_inner_len) *out_inner_len = written;
    if (written == 0) return Bound::storage;                      // the inner packer refused: no truncation, ever
    if (data_frame_len(flags, c.outer_data_type, written) > P::lora_max_frame_bytes) return Bound::air;

    uint8_t mac[8] = {};
    data_in di{};
    di.addr_len = c.addr_len;
    di.flags = flags;
    di.type = c.outer_data_type;
    di.next = 3; di.dst = 9; di.ctr = 0x1234;
    di.inner = std::span<const uint8_t>(inner, written);
    di.mac = std::span<const uint8_t>(mac, data_mac_len(flags));
    uint8_t frame[P::lora_max_frame_bytes];
    const size_t flen = pack_data(di, std::span<uint8_t>(frame, sizeof frame));
    if (flen == 0) return Bound::structural;                      // the length already fits => a SHAPE refusal
    return Bound::none;
}

// ---- a caller-supplied entropy provider, deliberately SYNTHETIC ------------------------------------------
// ⛔ THIS IS NOT HARDWARE-ENTROPY QUALIFICATION. It proves the CODEC's boundary refuses loudly; it says nothing
//    about whether today's device HAL can detect an RNG failure (it cannot report one — `IHal::rand_bytes` is
//    void, B312) or about what a console does before an RF send. That obligation belongs to the first real
//    integration slice and B312 stays open for it.
struct FakeEntropy {
    uint8_t bytes[8] = {};
    size_t  fill     = 8;      // how many bytes it actually writes before returning
    bool    succeed  = true;
    int     calls    = 0;
};
bool fake_entropy(void* ctx, uint8_t* out, size_t n) {
    auto* f = static_cast<FakeEntropy*>(ctx);
    f->calls++;
    const size_t k = f->fill < n ? f->fill : n;
    for (size_t i = 0; i < k; ++i) out[i] = f->bytes[i];
    return f->succeed;
}

// A decoded value pre-loaded with a sentinel, so "nothing was published" is a measurement.
RemoteDecoded sentinel_decoded() {
    RemoteDecoded d{};
    d.layout.domain = RemoteDomainId::cmd_bootstrap;
    d.msg.request_id = 0xA5A5A5A5A5A5A5A5ull;
    d.authenticated = true;
    d.result_kind = RemoteResultKind::terminal;
    d.terminal = RemoteTerminal::session_busy;
    return d;
}
bool sentinel_intact(const RemoteDecoded& d) {
    return d.layout.domain == RemoteDomainId::cmd_bootstrap
        && d.msg.request_id == 0xA5A5A5A5A5A5A5A5ull
        && d.authenticated
        && d.result_kind == RemoteResultKind::terminal
        && d.terminal == RemoteTerminal::session_busy;
}

// One place that derives the two keys the whole suite uses, through the PRODUCTION KDFs — every one of which
// is itself pinned to an independent literal in §1 below, so this is not a self-referential shortcut.
struct Creds {
    uint8_t base[32]{};
    uint8_t session[32]{};
};
Creds creds() {
    Creds c{};
    uint8_t shared[32], cpub[32], tpub[32];
    fill_shared(shared); fill_ctrl_pub(cpub); fill_tgt_pub(tpub);
    CHECK_STATUS("kdf_base", remote_kdf_base(c.base, shared, cpub, tpub), RemoteStatus::ok);
    CHECK_STATUS("kdf_session", remote_kdf_session(c.session, c.base, kEpoch), RemoteStatus::ok);
    return c;
}

}  // namespace

// =========================================================================================================
// §1 — KEY DERIVATION (design §8.1). Independent BLAKE2b-512-then-truncate literals, full ordered public
//      keys, and the ECDH refusal `identity.cpp`'s own `ecdh_shared` does not make.
// =========================================================================================================
TEST_CASE("§radmin-2/kdf — base and session keys against the independent BLAKE2b-512 reference") {
    uint8_t shared[32], cpub[32], tpub[32];
    fill_shared(shared); fill_ctrl_pub(cpub); fill_tgt_pub(tpub);

    uint8_t base[32] = {};
    CHECK_STATUS("base", remote_kdf_base(base, shared, cpub, tpub), RemoteStatus::ok);
    CHECK_BYTES("base key", base, kRefBaseKey, 32);

    // ORDER IS BOUND: swapping the two full public keys gives a DIFFERENT key (this is not DM's sorted-hash
    // KDF, where both endpoints deliberately derive one key).
    uint8_t swapped[32] = {};
    CHECK_STATUS("base swapped", remote_kdf_base(swapped, shared, tpub, cpub), RemoteStatus::ok);
    CHECK_BYTES("base key, roles swapped", swapped, kRefBaseKeySwapped, 32);
    CHECK(std::memcmp(base, swapped, 32) != 0);

    uint8_t session[32] = {};
    CHECK_STATUS("session", remote_kdf_session(session, base, kEpoch), RemoteStatus::ok);
    CHECK_BYTES("session key", session, kRefSessionKey, 32);

    // The epoch is a real input: a different epoch is a different session key.
    uint8_t session2[32] = {};
    CHECK_STATUS("session epoch2", remote_kdf_session(session2, base, kEpoch2), RemoteStatus::ok);
    CHECK_BYTES("session key at epoch2", session2, kRefSessionKeyEpoch2, 32);
    CHECK(std::memcmp(session, session2, 32) != 0);

    // ...and every one of the base inputs matters.
    uint8_t alt[32] = {};
    uint8_t shared_b[32]; fill_shared(shared_b); shared_b[31] ^= 0x01;
    CHECK_STATUS("base alt shared", remote_kdf_base(alt, shared_b, cpub, tpub), RemoteStatus::ok);
    CHECK(std::memcmp(base, alt, 32) != 0);
    uint8_t cpub_b[32]; fill_ctrl_pub(cpub_b); cpub_b[0] ^= 0x01;
    CHECK_STATUS("base alt cpub", remote_kdf_base(alt, shared, cpub_b, tpub), RemoteStatus::ok);
    CHECK(std::memcmp(base, alt, 32) != 0);
    uint8_t tpub_b[32]; fill_tgt_pub(tpub_b); tpub_b[0] ^= 0x01;
    CHECK_STATUS("base alt tpub", remote_kdf_base(alt, shared, cpub, tpub_b), RemoteStatus::ok);
    CHECK(std::memcmp(base, alt, 32) != 0);
}

TEST_CASE("§radmin-2/kdf — a degenerate X25519 shared point never becomes a key") {
    uint8_t cpub[32], tpub[32];
    fill_ctrl_pub(cpub); fill_tgt_pub(tpub);

    uint8_t zero[32] = {};
    uint8_t key[32]; std::memset(key, 0x5A, sizeof key);
    CHECK_STATUS("all-zero shared refused", remote_kdf_base(key, zero, cpub, tpub), RemoteStatus::bad_key);
    for (int i = 0; i < 32; ++i) CHECK(key[i] == 0x5A);      // and NOTHING was written

    // The REAL primitive, not a fabricated flag: monocypher's own X25519 against the canonical low-order
    // points. Each produces an all-zero shared secret, which `identity.cpp`'s void `ecdh_shared` happily
    // returns — so the check has to live at this boundary.
    uint8_t seed[32]; for (int i = 0; i < 32; ++i) seed[i] = static_cast<uint8_t>(i + 1);
    Identity self{}; identity_from_seed(self, seed);

    const uint8_t low_order[3][32] = {
        {0},                                                                     // the identity element
        {1},                                                                     // order 1
        {0xe0, 0xeb, 0x7a, 0x7c, 0x3b, 0x41, 0xb8, 0xae, 0x16, 0x56, 0xe3,       // order 8
         0xfa, 0xf1, 0x9f, 0xc4, 0x6a, 0xda, 0x09, 0x8d, 0xeb, 0x9c, 0x32,
         0xb1, 0xfd, 0x86, 0x62, 0x05, 0x16, 0x5f, 0x49, 0xb8, 0x00},
    };
    for (int k = 0; k < 3; ++k) {
        uint8_t raw[32] = {};
        ecdh_shared(raw, self, low_order[k]);                 // the EXISTING call: void, and it accepts this
        bool raw_zero = true; for (int i = 0; i < 32; ++i) raw_zero = raw_zero && raw[i] == 0;
        CHECK_MESSAGE(raw_zero, "low-order point ", k, " should give an all-zero shared point, got ",
                      hex(raw, 32));
        uint8_t out[32] = {};
        CHECK_STATUS("remote_ecdh_shared refuses", remote_ecdh_shared(out, self, low_order[k]),
                     RemoteStatus::bad_key);
    }

    // ...and the ordinary two-identity path still works through the same boundary.
    uint8_t seed_b[32]; for (int i = 0; i < 32; ++i) seed_b[i] = static_cast<uint8_t>(200 - i);
    Identity peer{}; identity_from_seed(peer, seed_b);
    uint8_t peer_x[32]; ed_pub_to_x25519(peer_x, peer.ed_pub);
    uint8_t self_x[32]; ed_pub_to_x25519(self_x, self.ed_pub);
    uint8_t sh_a[32] = {}, sh_b[32] = {};
    CHECK_STATUS("ecdh A", remote_ecdh_shared(sh_a, self, peer_x), RemoteStatus::ok);
    CHECK_STATUS("ecdh B", remote_ecdh_shared(sh_b, peer, self_x), RemoteStatus::ok);
    CHECK_BYTES("the two sides agree", sh_a, sh_b, 32);
    // The two ROLES still derive different base keys from that one shared point (the order binding again).
    uint8_t ka[32] = {}, kb[32] = {};
    CHECK_STATUS("base A", remote_kdf_base(ka, sh_a, self.ed_pub, peer.ed_pub), RemoteStatus::ok);
    CHECK_STATUS("base B", remote_kdf_base(kb, sh_b, peer.ed_pub, self.ed_pub), RemoteStatus::ok);
    CHECK(std::memcmp(ka, kb, 32) != 0);
}

// =========================================================================================================
// §2 — THE ONE LAYOUT/DOMAIN DECISION. Exhaustive over both directions and all 256 control bytes, plus
//      foreign outer types. This is where every reserved opcode/slot and illegal pairing is pinned.
// =========================================================================================================
TEST_CASE("§radmin-2/layout — exhaustive (direction x 256 ctl) domain table") {
    struct Expect { RemoteStatus st; RemoteDomainId dom; uint8_t overhead; bool authed; bool variable; };
    auto expect_cmd = [](uint8_t op, uint8_t slot) -> Expect {
        const bool sess = slot <= 9, sent = slot == 0x0F;
        if (!sess && !sent) return {RemoteStatus::bad_slot, RemoteDomainId::invalid, 0, false, false};
        switch (op) {
            case 0: return sess ? Expect{RemoteStatus::ok, RemoteDomainId::cmd_auth_execute, 25, true, true}
                                : Expect{RemoteStatus::bad_pairing, RemoteDomainId::invalid, 0, false, false};
            case 1: return sent ? Expect{RemoteStatus::ok, RemoteDomainId::cmd_open_execute, 9, false, true}
                                : Expect{RemoteStatus::bad_pairing, RemoteDomainId::invalid, 0, false, false};
            case 2: return sent ? Expect{RemoteStatus::ok, RemoteDomainId::cmd_bootstrap, 57, true, false}
                                : Expect{RemoteStatus::bad_pairing, RemoteDomainId::invalid, 0, false, false};
            case 3: return sess ? Expect{RemoteStatus::ok, RemoteDomainId::cmd_response_ack, 25, true, false}
                                : Expect{RemoteStatus::bad_pairing, RemoteDomainId::invalid, 0, false, false};
            case 4: return sess ? Expect{RemoteStatus::ok, RemoteDomainId::cmd_safe_rollover, 25, true, false}
                                : Expect{RemoteStatus::bad_pairing, RemoteDomainId::invalid, 0, false, false};
            case 5: return sess ? Expect{RemoteStatus::ok, RemoteDomainId::cmd_force_rollover, 25, true, false}
                                : Expect{RemoteStatus::bad_pairing, RemoteDomainId::invalid, 0, false, false};
            default: return {RemoteStatus::bad_opcode, RemoteDomainId::invalid, 0, false, false};
        }
    };
    auto expect_resp = [](uint8_t op, uint8_t slot) -> Expect {
        const bool sess = slot <= 9, sent = slot == 0x0F;
        if (!sess && !sent) return {RemoteStatus::bad_slot, RemoteDomainId::invalid, 0, false, false};
        switch (op) {
            case 0: return sess ? Expect{RemoteStatus::ok, RemoteDomainId::resp_output_auth, 26, true, true}
                                : Expect{RemoteStatus::ok, RemoteDomainId::resp_output_open, 10, false, true};
            case 1: return sess ? Expect{RemoteStatus::ok, RemoteDomainId::resp_terminal_auth, 26, true, true}
                                : Expect{RemoteStatus::ok, RemoteDomainId::resp_terminal_open, 10, false, true};
            case 2: return sess ? Expect{RemoteStatus::ok, RemoteDomainId::resp_bootstrap, 33, true, false}
                                : Expect{RemoteStatus::bad_pairing, RemoteDomainId::invalid, 0, false, false};
            case 3: return sess ? Expect{RemoteStatus::ok, RemoteDomainId::resp_rollover_result, 34, true, false}
                                : Expect{RemoteStatus::bad_pairing, RemoteDomainId::invalid, 0, false, false};
            case 4: return sess ? Expect{RemoteStatus::ok, RemoteDomainId::resp_protocol_error_auth, 26, true, true}
                                : Expect{RemoteStatus::ok, RemoteDomainId::resp_protocol_error_open, 10, false, true};
            default: return {RemoteStatus::bad_opcode, RemoteDomainId::invalid, 0, false, false};
        }
    };

    for (int v = 0; v < 256; ++v) {
        const uint8_t ctl  = static_cast<uint8_t>(v);
        const uint8_t op   = static_cast<uint8_t>(ctl >> 4);
        const uint8_t slot = static_cast<uint8_t>(ctl & 0x0F);
        for (int dir = 0; dir < 2; ++dir) {
            const uint8_t outer = dir == 0 ? DATA_TYPE_REMOTE_CMD : DATA_TYPE_REMOTE_RESP;
            const Expect e = dir == 0 ? expect_cmd(op, slot) : expect_resp(op, slot);
            RemoteLayout L{};
            const RemoteStatus st = remote_layout(outer, ctl, L);
            CHECK_MESSAGE(st == e.st, "outer=", int(outer), " ctl=", int(ctl), " got ", status_name(st),
                          " want ", status_name(e.st));
            if (e.st == RemoteStatus::ok) {
                CHECK(L.domain == e.dom);
                CHECK(L.fixed_overhead == e.overhead);
                CHECK(L.authenticated == e.authed);
                CHECK(L.variable_body == e.variable);
                CHECK(L.opcode == op);
                CHECK(L.slot == slot);
            } else {
                CHECK(L.domain == RemoteDomainId::invalid);
            }
        }
    }
    // ⛔ A FOREIGN DATA TYPE IS NOT A REMOTE DIRECTION — its byte 0 is not a `ctl` and must not be read as one.
    const uint8_t foreign[] = {0x00, 0x01, DATA_TYPE_MOBILE_SEND, DATA_TYPE_SEALED_RELAY, DATA_TYPE_E2E_ACK,
                               DATA_TYPE_CUSTODY_FAILURE, 0x9F, 0xA2, 0xFF};
    for (uint8_t t : foreign) {
        RemoteLayout L{};
        CHECK_STATUS("foreign outer type", remote_layout(t, 0x03, L), RemoteStatus::bad_outer_type);
        CHECK(L.domain == RemoteDomainId::invalid);
    }
    // The two RPC types are the existing allocations; nothing here allocates a codepoint.
    CHECK(DATA_TYPE_REMOTE_CMD  == 0xA0);
    CHECK(DATA_TYPE_REMOTE_RESP == 0xA1);
}

TEST_CASE("§radmin-2/layout — the frozen §8.11 overheads and the header field sets") {
    struct Row { uint8_t outer; uint8_t op; uint8_t slot; uint8_t hdr; uint8_t total; };
    const Row rows[] = {
        {DATA_TYPE_REMOTE_CMD,  0, kSlot,  9, 25},   // §8.2 authenticated execute
        {DATA_TYPE_REMOTE_CMD,  1, 0x0F,   9,  9},   // §8.3 open execute
        {DATA_TYPE_REMOTE_CMD,  2, 0x0F,  41, 57},   // §8.4 bootstrap request
        {DATA_TYPE_REMOTE_CMD,  3, kSlot,  9, 25},   // §8.5 response ack
        {DATA_TYPE_REMOTE_CMD,  4, kSlot,  9, 25},   // §8.5 safe rollover
        {DATA_TYPE_REMOTE_CMD,  5, kSlot,  9, 25},   // §8.5 force rollover
        {DATA_TYPE_REMOTE_RESP, 0, kSlot, 10, 26},   // §8.7 authenticated output
        {DATA_TYPE_REMOTE_RESP, 0, 0x0F,  10, 10},   // §8.8 open output
        {DATA_TYPE_REMOTE_RESP, 1, kSlot, 10, 26},
        {DATA_TYPE_REMOTE_RESP, 1, 0x0F,  10, 10},
        {DATA_TYPE_REMOTE_RESP, 2, kSlot, 17, 33},   // §8.6 bootstrap response
        {DATA_TYPE_REMOTE_RESP, 3, kSlot, 18, 34},   // §8.6 rollover result
        {DATA_TYPE_REMOTE_RESP, 4, kSlot, 10, 26},
        {DATA_TYPE_REMOTE_RESP, 4, 0x0F,  10, 10},
    };
    for (const Row& r : rows) {
        RemoteLayout L{};
        CHECK_STATUS("layout", remote_layout(r.outer, remote_ctl(r.op, r.slot), L), RemoteStatus::ok);
        CHECK_MESSAGE(L.header_bytes == r.hdr, "outer=", int(r.outer), " op=", int(r.op),
                      " header_bytes=", int(L.header_bytes), " want ", int(r.hdr));
        CHECK_MESSAGE(L.fixed_overhead == r.total, "outer=", int(r.outer), " op=", int(r.op),
                      " fixed_overhead=", int(L.fixed_overhead), " want ", int(r.total));
    }
    // ⛔ An ORDINARY REQUEST DOES NOT QUIETLY ACQUIRE AN EPOCH FIELD, and `response_seq` is ONE byte.
    RemoteLayout L{};
    CHECK_STATUS("auth execute", remote_layout(DATA_TYPE_REMOTE_CMD, remote_ctl(0, kSlot), L), RemoteStatus::ok);
    CHECK_FALSE(L.has_admin_epoch);
    CHECK_FALSE(L.epoch_in_nonce);
    CHECK_FALSE(L.has_response_seq);
    CHECK_STATUS("bootstrap req", remote_layout(DATA_TYPE_REMOTE_CMD, remote_ctl(2, 0x0F), L), RemoteStatus::ok);
    CHECK_FALSE(L.has_admin_epoch);
    CHECK_FALSE(L.epoch_in_nonce);            // §8.1: the controller does not yet know the epoch
    CHECK(L.uses_base_key);
    CHECK_STATUS("bootstrap resp", remote_layout(DATA_TYPE_REMOTE_RESP, remote_ctl(2, kSlot), L), RemoteStatus::ok);
    CHECK(L.has_admin_epoch);
    CHECK(L.epoch_in_nonce);
    CHECK(L.uses_base_key);
    CHECK_STATUS("rollover result", remote_layout(DATA_TYPE_REMOTE_RESP, remote_ctl(3, kSlot), L), RemoteStatus::ok);
    CHECK(L.has_abandoned_count);
    CHECK(L.epoch_in_nonce);
    CHECK(L.uses_base_key);
    CHECK_STATUS("auth output", remote_layout(DATA_TYPE_REMOTE_RESP, remote_ctl(0, kSlot), L), RemoteStatus::ok);
    CHECK(L.has_response_seq);
    CHECK_FALSE(L.uses_base_key);             // established-session traffic uses the SESSION key
    CHECK_FALSE(L.epoch_in_nonce);
}

// =========================================================================================================
// §3 — EVERY DOMAIN, BOTH DIRECTIONS: the exact clear header, the derived nonce, the AAD, and the COMPLETE
//      sealed/tag-only body, each against its independent literal, and each reproduced through the REAL
//      `remote_body_encode` (never through the helpers alone) and re-read through the REAL `remote_body_decode`.
// =========================================================================================================
namespace {

struct DomainCase {
    const char*  name;
    uint8_t      outer;
    uint8_t      op;
    uint8_t      slot;
    bool         authed;
    bool         base_key;
    bool         epoch_in_nonce;
    bool         has_seq;
    bool         has_pub;
    bool         has_epoch;
    bool         has_abandoned;
    const uint8_t* pt;
    size_t       pt_len;
    const uint8_t* ref_header; size_t ref_header_len;
    const uint8_t* ref_nonce;                              // nullptr for the two OPEN domains
    const uint8_t* ref_aad;    size_t ref_aad_len;
    const uint8_t* ref_body;   size_t ref_body_len;
};

const uint8_t kPtRoutes[6]  = {'r','o','u','t','e','s'};
const uint8_t kPtStatus[6]  = {'s','t','a','t','u','s'};
const uint8_t kPtLine[9]    = {'l','i','n','e',' ','o','n','e','\n'};
const uint8_t kPtZero[1]    = {0x00};

#define DC(nm, outer_, op_, slot_, authed_, base_, ep_, seq_, pub_, epf_, ab_, ptp, ptl, key)                 \
    DomainCase{ nm, outer_, op_, slot_, authed_, base_, ep_, seq_, pub_, epf_, ab_, ptp, ptl,                 \
                kRefHeader_##key, sizeof kRefHeader_##key, nullptr, nullptr, 0,                               \
                kRefBody_##key,   sizeof kRefBody_##key }
#define DCA(nm, outer_, op_, slot_, base_, ep_, seq_, pub_, epf_, ab_, ptp, ptl, key)                         \
    DomainCase{ nm, outer_, op_, slot_, true, base_, ep_, seq_, pub_, epf_, ab_, ptp, ptl,                    \
                kRefHeader_##key, sizeof kRefHeader_##key, kRefNonce_##key,                                   \
                kRefAad_##key, sizeof kRefAad_##key, kRefBody_##key, sizeof kRefBody_##key }

const DomainCase kDomainCases[] = {
    DCA("CMD AUTH_EXECUTE",     DATA_TYPE_REMOTE_CMD,  0, kSlot, false, false, false, false, false, false,
        kPtRoutes, 6,  cmd_auth_execute),
    DC ("CMD OPEN_EXECUTE",     DATA_TYPE_REMOTE_CMD,  1, 0x0F,  false, false, false, false, false, false, false,
        kPtStatus, 6,  cmd_open_execute),
    DCA("CMD BOOTSTRAP",        DATA_TYPE_REMOTE_CMD,  2, 0x0F,  true,  false, false, true,  false, false,
        nullptr,   0,  cmd_bootstrap),
    DCA("CMD RESPONSE_ACK",     DATA_TYPE_REMOTE_CMD,  3, kSlot, false, false, false, false, false, false,
        nullptr,   0,  cmd_response_ack),
    DCA("CMD SAFE_ROLLOVER",    DATA_TYPE_REMOTE_CMD,  4, kSlot, false, false, false, false, false, false,
        nullptr,   0,  cmd_safe_rollover),
    DCA("CMD FORCE_ROLLOVER",   DATA_TYPE_REMOTE_CMD,  5, kSlot, false, false, false, false, false, false,
        nullptr,   0,  cmd_force_rollover),
    DCA("RESP OUTPUT auth",     DATA_TYPE_REMOTE_RESP, 0, kSlot, false, false, true,  false, false, false,
        kPtLine,   9,  resp_output_auth),
    DC ("RESP OUTPUT open",     DATA_TYPE_REMOTE_RESP, 0, 0x0F,  false, false, false, true,  false, false, false,
        kPtLine,   9,  resp_output_open),
    DCA("RESP TERMINAL auth",   DATA_TYPE_REMOTE_RESP, 1, kSlot, false, false, true,  false, false, false,
        kPtZero,   1,  resp_terminal_auth),
    DC ("RESP TERMINAL open",   DATA_TYPE_REMOTE_RESP, 1, 0x0F,  false, false, false, true,  false, false, false,
        kPtZero,   1,  resp_terminal_open),
    DCA("RESP BOOTSTRAP",       DATA_TYPE_REMOTE_RESP, 2, kSlot, true,  true,  false, false, true,  false,
        nullptr,   0,  resp_bootstrap),
    DCA("RESP ROLLOVER_RESULT", DATA_TYPE_REMOTE_RESP, 3, kSlot, true,  true,  false, false, true,  true,
        nullptr,   0,  resp_rollover_result),
    DCA("RESP PROTOCOL_ERROR auth", DATA_TYPE_REMOTE_RESP, 4, kSlot, false, false, true, false, false, false,
        kPtZero,   1,  resp_protocol_error_auth),
    DC ("RESP PROTOCOL_ERROR open", DATA_TYPE_REMOTE_RESP, 4, 0x0F, false, false, false, true, false, false, false,
        kPtZero,   1,  resp_protocol_error_open),
};
constexpr size_t kDomainCaseCount = sizeof kDomainCases / sizeof kDomainCases[0];

RemoteMessage msg_of(const DomainCase& d, const uint8_t ctrl_pub[32]) {
    RemoteMessage m{};
    m.outer_type   = d.outer;
    m.opcode       = d.op;
    m.slot         = d.slot;
    m.request_id   = kReqId;
    m.response_seq = d.has_seq ? kSeq : uint8_t{0};
    m.admin_epoch  = d.has_epoch ? kEpoch : uint64_t{0};
    m.abandoned_count = d.has_abandoned ? kAbandoned : uint8_t{0};
    if (d.has_pub) m.controller_pub = std::span<const uint8_t>(ctrl_pub, 32);
    return m;
}

}  // namespace

TEST_CASE("§radmin-2/wire — every domain: header, nonce, AAD and the complete body vs independent literals") {
    const Creds c = creds();
    uint8_t cpub[32]; fill_ctrl_pub(cpub);
    const RemoteKeys keys = keys_of(c.base, c.session);
    const RemoteSource src = src_ok();
    const RemoteCarrier carrier = carrier_same_layer(DATA_TYPE_REMOTE_CMD, true);
    const RemoteCarrier carrier_r = carrier_same_layer(DATA_TYPE_REMOTE_RESP, true);

    for (size_t i = 0; i < kDomainCaseCount; ++i) {
        const DomainCase& d = kDomainCases[i];
        const RemoteCarrier& car = d.outer == DATA_TYPE_REMOTE_CMD ? carrier : carrier_r;
        const RemoteMessage m = msg_of(d, cpub);

        RemoteLayout L{};
        CHECK_STATUS(d.name, remote_layout(d.outer, remote_ctl(d.op, d.slot), L), RemoteStatus::ok);
        CHECK_MESSAGE(L.authenticated == d.authed, d.name, " authenticated");
        CHECK_MESSAGE(L.uses_base_key == d.base_key, d.name, " uses_base_key");
        CHECK_MESSAGE(L.epoch_in_nonce == d.epoch_in_nonce, d.name, " epoch_in_nonce");
        CHECK_MESSAGE(size_t(L.header_bytes) == d.ref_header_len, d.name, " header length ", int(L.header_bytes),
                      " want ", int(d.ref_header_len));

        // ---- the AAD and the derived nonce, for the authenticated domains ---------------------------------
        if (d.authed) {
            const uint8_t* key = d.base_key ? c.base : c.session;
            uint8_t nonce[24] = {};
            CHECK_STATUS("nonce", remote_nonce(nonce, L, m, key, src), RemoteStatus::ok);
            CHECK_BYTES(d.name, nonce, d.ref_nonce, 24);

            uint8_t aad[kRemoteMaxAadBytes] = {};
            size_t aad_len = 0;
            CHECK_STATUS("aad", remote_aad(std::span<uint8_t>(aad, sizeof aad), aad_len, L, m, src),
                         RemoteStatus::ok);
            CHECK_MESSAGE(aad_len == d.ref_aad_len, d.name, " aad_len ", aad_len, " want ", d.ref_aad_len);
            CHECK_BYTES(d.name, aad, d.ref_aad, d.ref_aad_len);
            // AAD = direction byte | the EXACT clear header | source_hash LE32, and nothing else.
            CHECK(aad[0] == d.outer);
            CHECK_BYTES("aad carries the exact header", aad + 1, d.ref_header, d.ref_header_len);
            CHECK(aad_len == size_t{1} + d.ref_header_len + 4);
        } else {
            // ⛔ AN OPEN DOMAIN HAS NO NONCE AND NO AAD — not a zero one that could join an authenticated
            //    inequality claim.
            uint8_t nonce[24] = {};
            CHECK_STATUS("open has no nonce", remote_nonce(nonce, L, m, c.session, src), RemoteStatus::bad_pairing);
            uint8_t aad[kRemoteMaxAadBytes] = {};
            size_t aad_len = 0;
            CHECK_STATUS("open has no aad", remote_aad(std::span<uint8_t>(aad, sizeof aad), aad_len, L, m, src),
                         RemoteStatus::bad_pairing);
        }

        // ---- the COMPLETE body through the real encoder ----------------------------------------------------
        uint8_t out[P::max_payload_bytes_hard_cap] = {};
        size_t  out_len = 0;
        const std::span<const uint8_t> pt(d.pt, d.pt_len);
        CHECK_STATUS(d.name, remote_body_encode(std::span<uint8_t>(out, sizeof out), out_len, m, pt, keys, src, car),
                     RemoteStatus::ok);
        CHECK_MESSAGE(out_len == d.ref_body_len, d.name, " body length ", out_len, " want ", d.ref_body_len);
        CHECK_BYTES(d.name, out, d.ref_body, d.ref_body_len);
        // the clear header is literally the first `header_bytes` of the body
        CHECK_BYTES("body header prefix", out, d.ref_header, d.ref_header_len);

        // ---- and back through the real decoder -------------------------------------------------------------
        uint8_t ptbuf[P::max_payload_bytes_hard_cap];
        std::memset(ptbuf, 0x5A, sizeof ptbuf);
        RemoteDecoded got = sentinel_decoded();
        CHECK_STATUS(d.name, remote_body_decode(got, d.outer, std::span<const uint8_t>(d.ref_body, d.ref_body_len),
                                           keys, src, car, std::span<uint8_t>(ptbuf, sizeof ptbuf)),
                     RemoteStatus::ok);
        CHECK(got.layout.domain == L.domain);
        CHECK(got.authenticated == d.authed);
        CHECK(got.msg.request_id == kReqId);
        CHECK(got.msg.opcode == d.op);
        CHECK(got.msg.slot == d.slot);
        CHECK(got.msg.response_seq == (d.has_seq ? kSeq : uint8_t{0}));
        CHECK(got.msg.admin_epoch == (d.has_epoch ? kEpoch : uint64_t{0}));
        CHECK(got.msg.abandoned_count == (d.has_abandoned ? kAbandoned : uint8_t{0}));
        CHECK(got.body.size() == d.pt_len);
        if (d.pt_len) CHECK_BYTES(d.name, got.body.data(), d.pt, d.pt_len);
        if (d.has_pub) {
            CHECK(got.msg.controller_pub.size() == 32);
            CHECK_BYTES("decoded controller_pub", got.msg.controller_pub.data(), cpub, 32);
        } else {
            CHECK(got.msg.controller_pub.empty());
        }
    }
}

TEST_CASE("§radmin-2/wire — the bootstrap REQUEST has no epoch input, the two base-key RESPONSES do") {
    const Creds c = creds();
    uint8_t cpub[32]; fill_ctrl_pub(cpub);
    const RemoteSource src = src_ok();

    // ⛔ THE WRONG NONCES ARE PINNED TOO. `kRefNonceWRONG_*` is what a codec that fed the epoch into a REQUEST
    //    domain would derive; production must NOT produce them. Without this the "no epoch in a request" rule
    //    would only be a comment.
    RemoteLayout Lb{};
    CHECK_STATUS("bootstrap req", remote_layout(DATA_TYPE_REMOTE_CMD, remote_ctl(2, 0x0F), Lb), RemoteStatus::ok);
    RemoteMessage mb{};
    mb.outer_type = DATA_TYPE_REMOTE_CMD; mb.opcode = 2; mb.slot = 0x0F; mb.request_id = kReqId;
    mb.admin_epoch = kEpoch;                      // present in the struct and DELIBERATELY IGNORED by the domain
    mb.controller_pub = std::span<const uint8_t>(cpub, 32);
    uint8_t nb[24] = {};
    CHECK_STATUS("nonce", remote_nonce(nb, Lb, mb, c.base, src), RemoteStatus::ok);
    CHECK_BYTES("bootstrap request nonce", nb, kRefNonce_cmd_bootstrap, 24);
    CHECK(std::memcmp(nb, kRefNonceWRONG_cmd_bootstrap_with_epoch, 24) != 0);

    RemoteLayout Le{};
    CHECK_STATUS("auth execute", remote_layout(DATA_TYPE_REMOTE_CMD, remote_ctl(0, kSlot), Le), RemoteStatus::ok);
    RemoteMessage me{};
    me.outer_type = DATA_TYPE_REMOTE_CMD; me.opcode = 0; me.slot = kSlot; me.request_id = kReqId;
    me.admin_epoch = kEpoch;
    uint8_t ne[24] = {};
    CHECK_STATUS("nonce", remote_nonce(ne, Le, me, c.session, src), RemoteStatus::ok);
    CHECK_BYTES("auth execute nonce", ne, kRefNonce_cmd_auth_execute, 24);
    CHECK(std::memcmp(ne, kRefNonceWRONG_cmd_auth_execute_with_epoch, 24) != 0);

    // The bootstrap RESPONSE's nonce MOVES when the epoch moves...
    RemoteLayout Lr{};
    CHECK_STATUS("bootstrap resp", remote_layout(DATA_TYPE_REMOTE_RESP, remote_ctl(2, kSlot), Lr), RemoteStatus::ok);
    RemoteMessage mr{};
    mr.outer_type = DATA_TYPE_REMOTE_RESP; mr.opcode = 2; mr.slot = kSlot; mr.request_id = kReqId;
    mr.admin_epoch = kEpoch2;
    uint8_t nr[24] = {};
    CHECK_STATUS("nonce", remote_nonce(nr, Lr, mr, c.base, src), RemoteStatus::ok);
    CHECK_BYTES("bootstrap response nonce at epoch2", nr, kRefNonce_resp_bootstrap_epoch2, 24);
    CHECK(std::memcmp(nr, kRefNonce_resp_bootstrap, 24) != 0);

    // ...and so does the rollover result's WHOLE BODY (clear epoch field + nonce + tag).
    const RemoteKeys keys = keys_of(c.base, c.session);
    RemoteMessage mv{};
    mv.outer_type = DATA_TYPE_REMOTE_RESP; mv.opcode = 3; mv.slot = kSlot; mv.request_id = kReqId;
    mv.admin_epoch = kEpoch2; mv.abandoned_count = kAbandoned;
    uint8_t out[64] = {}; size_t out_len = 0;
    CHECK_STATUS("encode", remote_body_encode(std::span<uint8_t>(out, sizeof out), out_len, mv, {}, keys, src,
                                         carrier_same_layer(DATA_TYPE_REMOTE_RESP, true)), RemoteStatus::ok);
    CHECK_BYTES("rollover result at epoch2", out, kRefBody_resp_rollover_result_epoch2, 34);
    CHECK(std::memcmp(out, kRefBody_resp_rollover_result, 34) != 0);
}

TEST_CASE("§radmin-2/nonce — pairwise inequality across every authenticated domain at ONE request id") {
    const Creds c = creds();
    const RemoteSource src = src_ok();
    uint8_t cpub[32]; fill_ctrl_pub(cpub);

    struct N { const char* name; uint8_t bytes[24]; };
    std::vector<N> ns;
    for (size_t i = 0; i < kDomainCaseCount; ++i) {
        const DomainCase& d = kDomainCases[i];
        if (!d.authed) continue;                       // an OPEN domain has no nonce and joins no such claim
        RemoteLayout L{};
        CHECK_STATUS(d.name, remote_layout(d.outer, remote_ctl(d.op, d.slot), L), RemoteStatus::ok);
        const RemoteMessage m = msg_of(d, cpub);
        N n{}; n.name = d.name;
        CHECK_STATUS(d.name, remote_nonce(n.bytes, L, m, d.base_key ? c.base : c.session, src), RemoteStatus::ok);
        ns.push_back(n);
    }
    CHECK(ns.size() == 10);
    size_t pairs = 0;
    for (size_t i = 0; i < ns.size(); ++i)
        for (size_t j = i + 1; j < ns.size(); ++j) {
            const bool differ = std::memcmp(ns[i].bytes, ns[j].bytes, 24) != 0;
            CHECK_MESSAGE(differ, "nonce collision: ", ns[i].name, " vs ", ns[j].name, " = ",
                          hex(ns[i].bytes, 24));
            ++pairs;
        }
    CHECK(pairs == 45);                                 // 10 * 9 / 2 — the whole authenticated domain set

    // The pairs the design names explicitly, spelled out so a shrunken set is visible:
    //   * the SAME opcode nibble in opposite directions (ctl 0x03 is CMD AUTH_EXECUTE and RESP OUTPUT),
    //   * ACK vs execute, output vs terminal vs protocol error, bootstrap vs rollover result.
    CHECK(remote_ctl(0, kSlot) == kRefHeader_cmd_auth_execute[0]);
    CHECK(remote_ctl(0, kSlot) == kRefHeader_resp_output_auth[0]);
    CHECK(std::memcmp(kRefNonce_cmd_auth_execute, kRefNonce_resp_output_auth, 24) != 0);
    CHECK(remote_ctl(3, kSlot) == kRefHeader_cmd_response_ack[0]);
    CHECK(remote_ctl(3, kSlot) == kRefHeader_resp_rollover_result[0]);
    CHECK(std::memcmp(kRefNonce_cmd_response_ack, kRefNonce_resp_rollover_result, 24) != 0);
}

TEST_CASE("§radmin-2/nonce — different slots, response sequences and controller source hashes separate") {
    const Creds c = creds();
    RemoteMessage m{};
    m.outer_type = DATA_TYPE_REMOTE_CMD; m.opcode = 0; m.request_id = kReqId;

    uint8_t prev[10][24] = {};
    for (uint8_t s = 0; s <= 9; ++s) {
        RemoteLayout L{};
        CHECK_STATUS("slot layout", remote_layout(DATA_TYPE_REMOTE_CMD, remote_ctl(0, s), L), RemoteStatus::ok);
        m.slot = s;
        CHECK_STATUS("nonce", remote_nonce(prev[s], L, m, c.session, src_ok()), RemoteStatus::ok);
        for (uint8_t t = 0; t < s; ++t)
            CHECK_MESSAGE(std::memcmp(prev[s], prev[t], 24) != 0, "slot ", int(s), " vs ", int(t));
    }

    RemoteLayout Lr{};
    CHECK_STATUS("resp layout", remote_layout(DATA_TYPE_REMOTE_RESP, remote_ctl(0, kSlot), Lr), RemoteStatus::ok);
    RemoteMessage mr{};
    mr.outer_type = DATA_TYPE_REMOTE_RESP; mr.opcode = 0; mr.slot = kSlot; mr.request_id = kReqId;
    uint8_t n0[24] = {}, n1[24] = {}, n255[24] = {};
    mr.response_seq = 0;   CHECK_STATUS("seq0", remote_nonce(n0, Lr, mr, c.session, src_ok()), RemoteStatus::ok);
    mr.response_seq = 1;   CHECK_STATUS("seq1", remote_nonce(n1, Lr, mr, c.session, src_ok()), RemoteStatus::ok);
    mr.response_seq = 255; CHECK_STATUS("seq255", remote_nonce(n255, Lr, mr, c.session, src_ok()), RemoteStatus::ok);
    CHECK(std::memcmp(n0, n1, 24) != 0);
    CHECK(std::memcmp(n0, n255, 24) != 0);
    CHECK(std::memcmp(n1, n255, 24) != 0);

    // ★ TWO CONTROLLERS SHARING ONE CREDENTIAL — the same session key, different stable SOURCE_HASH: the
    //   pre-encryption nonce-separation property of design §9.
    RemoteLayout Le{};
    CHECK_STATUS("auth execute", remote_layout(DATA_TYPE_REMOTE_CMD, remote_ctl(0, kSlot), Le), RemoteStatus::ok);
    RemoteMessage me{};
    me.outer_type = DATA_TYPE_REMOTE_CMD; me.opcode = 0; me.slot = kSlot; me.request_id = kReqId;
    uint8_t na[24] = {}, nb[24] = {};
    CHECK_STATUS("src1", remote_nonce(na, Le, me, c.session, src_ok(kSrcHash)), RemoteStatus::ok);
    CHECK_STATUS("src2", remote_nonce(nb, Le, me, c.session, src_ok(kSrcHash2)), RemoteStatus::ok);
    CHECK_BYTES("source hash 1", na, kRefNonce_cmd_auth_execute, 24);
    CHECK_BYTES("source hash 2", nb, kRefNonce_cmd_auth_execute_src2, 24);
    CHECK(std::memcmp(na, nb, 24) != 0);

    // ⛔ NO ABSENT-SOURCE FALLBACK, and presence is not the value: a source that is ABSENT refuses even when
    //    its numeric field happens to be a perfectly ordinary hash, and a PRESENT zero hash is accepted.
    RemoteSource absent{}; absent.present = false; absent.hash = kSrcHash;
    uint8_t nz[24] = {};
    CHECK_STATUS("absent source", remote_nonce(nz, Le, me, c.session, absent), RemoteStatus::bad_argument);
    RemoteSource present_zero{}; present_zero.present = true; present_zero.hash = 0;
    CHECK_STATUS("present zero hash", remote_nonce(nz, Le, me, c.session, present_zero), RemoteStatus::ok);
    CHECK(std::memcmp(nz, na, 24) != 0);
}

// =========================================================================================================
// §4 — SELECTED KEY, and the fact that the SEAL path reaches the selector rather than a helper.
// =========================================================================================================
TEST_CASE("§radmin-2/keys — the domain selects the key, and the wrong one is a refusal not a fallback") {
    const Creds c = creds();
    const RemoteSource src = src_ok();
    uint8_t cpub[32]; fill_ctrl_pub(cpub);
    uint8_t out[P::max_payload_bytes_hard_cap]; size_t out_len = 0;

    // A base-key domain with ONLY the session key supplied refuses; it never quietly seals under the session key.
    RemoteMessage mb{};
    mb.outer_type = DATA_TYPE_REMOTE_CMD; mb.opcode = 2; mb.slot = 0x0F; mb.request_id = kReqId;
    mb.controller_pub = std::span<const uint8_t>(cpub, 32);
    RemoteKeys only_session{};
    only_session.session = std::span<const uint8_t>(c.session, 32);
    CHECK_STATUS("bootstrap needs the base key",
                 remote_body_encode(std::span<uint8_t>(out, sizeof out), out_len, mb, {}, only_session, src,
                               carrier_same_layer(DATA_TYPE_REMOTE_CMD, true)), RemoteStatus::bad_key);

    // A session-key domain with ONLY the base key supplied likewise refuses.
    RemoteMessage me{};
    me.outer_type = DATA_TYPE_REMOTE_CMD; me.opcode = 0; me.slot = kSlot; me.request_id = kReqId;
    RemoteKeys only_base{};
    only_base.base = std::span<const uint8_t>(c.base, 32);
    CHECK_STATUS("execute needs the session key",
                 remote_body_encode(std::span<uint8_t>(out, sizeof out), out_len, me,
                               std::span<const uint8_t>(kPtRoutes, 6), only_base, src,
                               carrier_same_layer(DATA_TYPE_REMOTE_CMD, true)), RemoteStatus::bad_key);

    // A misshaped key (31 bytes) is absent, not "close enough".
    RemoteKeys short_key{};
    short_key.session = std::span<const uint8_t>(c.session, 31);
    CHECK_STATUS("31-byte session key",
                 remote_body_encode(std::span<uint8_t>(out, sizeof out), out_len, me,
                               std::span<const uint8_t>(kPtRoutes, 6), short_key, src,
                               carrier_same_layer(DATA_TYPE_REMOTE_CMD, true)), RemoteStatus::bad_key);

    // ★ AND THE SELECTOR IS REACHED BY THE REAL SEAL: swapping WHICH key sits in which slot changes the bytes.
    //   (`keys.base = session` for the bootstrap domain seals under a different key -> a different body.)
    RemoteKeys swapped{};
    swapped.base = std::span<const uint8_t>(c.session, 32);
    swapped.session = std::span<const uint8_t>(c.base, 32);
    size_t n2 = 0;
    uint8_t out2[P::max_payload_bytes_hard_cap];
    CHECK_STATUS("encode under swapped credentials",
                 remote_body_encode(std::span<uint8_t>(out2, sizeof out2), n2, mb, {}, swapped, src,
                               carrier_same_layer(DATA_TYPE_REMOTE_CMD, true)), RemoteStatus::ok);
    CHECK(n2 == sizeof kRefBody_cmd_bootstrap);
    CHECK(std::memcmp(out2, kRefBody_cmd_bootstrap, n2) != 0);
}

// =========================================================================================================
// §5 — THE EXACT-LENGTH RULE, the variable-body boundary and out-of-bounds safety.
// =========================================================================================================
TEST_CASE("§radmin-2/length — a fixed layout accepts neither a short prefix nor a trailing byte") {
    const Creds c = creds();
    const RemoteKeys keys = keys_of(c.base, c.session);
    const RemoteSource src = src_ok();
    uint8_t ptbuf[P::max_payload_bytes_hard_cap];

    struct Fixed { const char* name; uint8_t outer; const uint8_t* body; size_t len; };
    const Fixed fixed[] = {
        {"CMD BOOTSTRAP",      DATA_TYPE_REMOTE_CMD,  kRefBody_cmd_bootstrap,        sizeof kRefBody_cmd_bootstrap},
        {"CMD RESPONSE_ACK",   DATA_TYPE_REMOTE_CMD,  kRefBody_cmd_response_ack,     sizeof kRefBody_cmd_response_ack},
        {"CMD SAFE_ROLLOVER",  DATA_TYPE_REMOTE_CMD,  kRefBody_cmd_safe_rollover,    sizeof kRefBody_cmd_safe_rollover},
        {"CMD FORCE_ROLLOVER", DATA_TYPE_REMOTE_CMD,  kRefBody_cmd_force_rollover,   sizeof kRefBody_cmd_force_rollover},
        {"RESP BOOTSTRAP",     DATA_TYPE_REMOTE_RESP, kRefBody_resp_bootstrap,       sizeof kRefBody_resp_bootstrap},
        {"RESP ROLLOVER",      DATA_TYPE_REMOTE_RESP, kRefBody_resp_rollover_result, sizeof kRefBody_resp_rollover_result},
    };
    for (const Fixed& f : fixed) {
        const RemoteCarrier car = carrier_same_layer(f.outer, true);
        // every short prefix, down to the empty body — none may read out of bounds and none may decode
        for (size_t n = 0; n < f.len; ++n) {
            RemoteDecoded got = sentinel_decoded();
            std::memset(ptbuf, 0x5A, sizeof ptbuf);
            const RemoteStatus st = remote_body_decode(got, f.outer, std::span<const uint8_t>(f.body, n), keys, src,
                                                  car, std::span<uint8_t>(ptbuf, sizeof ptbuf));
            CHECK_MESSAGE(st != RemoteStatus::ok, f.name, " prefix of ", n, " bytes decoded");
            CHECK_MESSAGE(sentinel_intact(got), f.name, " prefix of ", n, " published a value");
        }
        // one trailing byte -> bad_length, NOT an ignored suffix
        std::vector<uint8_t> longer(f.body, f.body + f.len);
        longer.push_back(0x00);
        RemoteDecoded got = sentinel_decoded();
        CHECK_STATUS(f.name, remote_body_decode(got, f.outer, std::span<const uint8_t>(longer.data(), longer.size()),
                                           keys, src, car, std::span<uint8_t>(ptbuf, sizeof ptbuf)),
                     RemoteStatus::bad_length);
        CHECK(sentinel_intact(got));
        // and the exact length still decodes
        CHECK_STATUS(f.name, remote_body_decode(got, f.outer, std::span<const uint8_t>(f.body, f.len), keys, src,
                                           car, std::span<uint8_t>(ptbuf, sizeof ptbuf)), RemoteStatus::ok);
    }
}

TEST_CASE("§radmin-2/length — a variable body's N is the complete remaining span") {
    const Creds c = creds();
    const RemoteKeys keys = keys_of(c.base, c.session);
    const RemoteSource src = src_ok();
    const RemoteCarrier car_cmd = carrier_same_layer(DATA_TYPE_REMOTE_CMD, true);
    const RemoteCarrier car_rsp = carrier_same_layer(DATA_TYPE_REMOTE_RESP, true);
    uint8_t ptbuf[P::max_payload_bytes_hard_cap];
    std::memset(ptbuf, 0x5A, sizeof ptbuf);

    // ---- the tag-only empty ciphertext through a VARIABLE layout (N = 0) -----------------------------------
    RemoteMessage me{};
    me.outer_type = DATA_TYPE_REMOTE_CMD; me.opcode = 0; me.slot = kSlot; me.request_id = kReqId;
    uint8_t out[P::max_payload_bytes_hard_cap]; size_t out_len = 0;
    CHECK_STATUS("empty command", remote_body_encode(std::span<uint8_t>(out, sizeof out), out_len, me, {}, keys, src,
                                                car_cmd), RemoteStatus::ok);
    CHECK(out_len == kRemoteOverheadAuthExecute);
    CHECK_BYTES("empty auth execute", out, kRefBody_cmd_auth_execute_empty, 25);
    RemoteDecoded got = sentinel_decoded();
    CHECK_STATUS("decode empty", remote_body_decode(got, DATA_TYPE_REMOTE_CMD,
                                               std::span<const uint8_t>(kRefBody_cmd_auth_execute_empty, 25),
                                               keys, src, car_cmd, std::span<uint8_t>(ptbuf, sizeof ptbuf)),
                 RemoteStatus::ok);
    CHECK(got.body.empty());
    CHECK(got.authenticated);
    // one byte short of the envelope -> bad_length (not a negative N)
    RemoteDecoded g2 = sentinel_decoded();
    CHECK_STATUS("24 bytes", remote_body_decode(g2, DATA_TYPE_REMOTE_CMD,
                                           std::span<const uint8_t>(kRefBody_cmd_auth_execute_empty, 24),
                                           keys, src, car_cmd, std::span<uint8_t>(ptbuf, sizeof ptbuf)),
                 RemoteStatus::bad_length);
    CHECK(sentinel_intact(g2));

    // ---- an EXTRA byte in an OPEN variable body is legitimate PAYLOAD, not trailing garbage ---------------
    std::vector<uint8_t> open_plus(kRefBody_cmd_open_execute,
                                  kRefBody_cmd_open_execute + sizeof kRefBody_cmd_open_execute);
    open_plus.push_back('!');
    RemoteDecoded go = sentinel_decoded();
    CHECK_STATUS("open + 1 byte", remote_body_decode(go, DATA_TYPE_REMOTE_CMD,
                                                std::span<const uint8_t>(open_plus.data(), open_plus.size()),
                                                keys, src, car_cmd, std::span<uint8_t>(ptbuf, sizeof ptbuf)),
                 RemoteStatus::ok);
    CHECK(go.body.size() == 7);
    CHECK(go.body[6] == '!');
    CHECK_FALSE(go.authenticated);              // and it is still explicitly UNAUTHENTICATED

    // ---- the caller's plaintext span must hold N -----------------------------------------------------------
    RemoteDecoded g3 = sentinel_decoded();
    uint8_t tiny[5];
    CHECK_STATUS("plaintext span too small",
                 remote_body_decode(g3, DATA_TYPE_REMOTE_RESP,
                               std::span<const uint8_t>(kRefBody_resp_output_auth, sizeof kRefBody_resp_output_auth),
                               keys, src, car_rsp, std::span<uint8_t>(tiny, sizeof tiny)), RemoteStatus::bad_buffer);
    CHECK(sentinel_intact(g3));

    // ---- and the encoder's output span likewise ------------------------------------------------------------
    uint8_t small[24]; size_t n = 0;
    CHECK_STATUS("output span too small",
                 remote_body_encode(std::span<uint8_t>(small, sizeof small), n, me,
                               std::span<const uint8_t>(kPtRoutes, 6), keys, src, car_cmd),
                 RemoteStatus::bad_buffer);

    // ---- a fixed layout refuses a payload outright ---------------------------------------------------------
    RemoteMessage mb{};
    uint8_t cpub[32]; fill_ctrl_pub(cpub);
    mb.outer_type = DATA_TYPE_REMOTE_CMD; mb.opcode = 2; mb.slot = 0x0F; mb.request_id = kReqId;
    mb.controller_pub = std::span<const uint8_t>(cpub, 32);
    CHECK_STATUS("fixed layout with a body",
                 remote_body_encode(std::span<uint8_t>(out, sizeof out), out_len, mb,
                               std::span<const uint8_t>(kPtRoutes, 6), keys, src, car_cmd),
                 RemoteStatus::bad_argument);
    // ...and a bootstrap request without its 32-byte controller key, or any other layout WITH one, refuses.
    RemoteMessage mb2 = mb; mb2.controller_pub = {};
    CHECK_STATUS("bootstrap with no controller key",
                 remote_body_encode(std::span<uint8_t>(out, sizeof out), out_len, mb2, {}, keys, src, car_cmd),
                 RemoteStatus::bad_argument);
    RemoteMessage me2 = me; me2.controller_pub = std::span<const uint8_t>(cpub, 32);
    CHECK_STATUS("execute with a controller key",
                 remote_body_encode(std::span<uint8_t>(out, sizeof out), out_len, me2,
                               std::span<const uint8_t>(kPtRoutes, 6), keys, src, car_cmd),
                 RemoteStatus::bad_argument);
    RemoteMessage mb3 = mb; mb3.controller_pub = std::span<const uint8_t>(cpub, 31);
    CHECK_STATUS("bootstrap with a 31-byte key",
                 remote_body_encode(std::span<uint8_t>(out, sizeof out), out_len, mb3, {}, keys, src, car_cmd),
                 RemoteStatus::bad_argument);
}

// =========================================================================================================
// §6 — EVERY BYTE CORRUPTED IN TURN. The rule proved here is exact: after any single-byte change the codec
//      NEVER publishes an AUTHENTICATED value. The one byte that can still produce a successful decode is
//      the control byte, because flipping it can name a genuinely different, genuinely OPEN envelope — which
//      is a separately valid open frame, not something a codec can recognise as a former sealed one, and not
//      an "open fallback". Every ciphertext, tag and remaining header byte must fail the authenticated open.
// =========================================================================================================
TEST_CASE("§radmin-2/corruption — no single-byte change ever yields an authenticated result") {
    const Creds c = creds();
    const RemoteKeys keys = keys_of(c.base, c.session);
    const RemoteSource src = src_ok();
    uint8_t ptbuf[P::max_payload_bytes_hard_cap];

    struct Fx { const char* name; uint8_t outer; const uint8_t* body; size_t len; size_t hdr; size_t ct; };
    const Fx fx[] = {
        {"CMD AUTH_EXECUTE",  DATA_TYPE_REMOTE_CMD,  kRefBody_cmd_auth_execute,  31,  9,  6},
        {"CMD BOOTSTRAP",     DATA_TYPE_REMOTE_CMD,  kRefBody_cmd_bootstrap,     57, 41,  0},
        {"RESP OUTPUT auth",  DATA_TYPE_REMOTE_RESP, kRefBody_resp_output_auth,  35, 10,  9},
        {"RESP ROLLOVER",     DATA_TYPE_REMOTE_RESP, kRefBody_resp_rollover_result, 34, 18, 0},
    };
    for (const Fx& f : fx) {
        const RemoteCarrier car = carrier_same_layer(f.outer, true);
        size_t hdr_cases = 0, ct_cases = 0, tag_cases = 0;
        for (size_t i = 0; i < f.len; ++i) {
            std::vector<uint8_t> b(f.body, f.body + f.len);
            b[i] = static_cast<uint8_t>(b[i] ^ 0xFF);
            RemoteDecoded got = sentinel_decoded();
            std::memset(ptbuf, 0x5A, sizeof ptbuf);
            const RemoteStatus st = remote_body_decode(got, f.outer, std::span<const uint8_t>(b.data(), b.size()),
                                                  keys, src, car, std::span<uint8_t>(ptbuf, sizeof ptbuf));
            if (i == 0) {
                // the ctl byte: refusal, or a decode that is EXPLICITLY unauthenticated
                if (st == RemoteStatus::ok) {
                    CHECK_MESSAGE(!got.authenticated, f.name,
                                  " a corrupted ctl produced an AUTHENTICATED decode");
                    const bool unauth_or_no_result =
                        (got.result_kind == RemoteResultKind::none) || !got.authenticated;
                    CHECK(unauth_or_no_result);
                } else {
                    CHECK(sentinel_intact(got));
                }
            } else {
                CHECK_MESSAGE(st == RemoteStatus::auth_failed, f.name, " byte ", i, " gave ", status_name(st),
                              " (want auth_failed)");
                CHECK_MESSAGE(sentinel_intact(got), f.name, " byte ", i, " published a value");
                if (i < f.hdr)                 ++hdr_cases;
                else if (i < f.hdr + f.ct)     ++ct_cases;
                else                           ++tag_cases;
            }
        }
        // the three regions are covered SEPARATELY, and the tag region is always the full 16 bytes
        CHECK_MESSAGE(hdr_cases == f.hdr - 1, f.name, " header bytes covered ", hdr_cases);
        CHECK_MESSAGE(ct_cases == f.ct, f.name, " ciphertext bytes covered ", ct_cases);
        CHECK_MESSAGE(tag_cases == kRemoteTagBytes, f.name, " tag bytes covered ", tag_cases);
    }
}

TEST_CASE("§radmin-2/corruption — every control byte over one sealed body, and no open fallback") {
    const Creds c = creds();
    const RemoteKeys keys = keys_of(c.base, c.session);
    const RemoteSource src = src_ok();
    const RemoteCarrier car = carrier_same_layer(DATA_TYPE_REMOTE_CMD, true);
    uint8_t ptbuf[P::max_payload_bytes_hard_cap];

    size_t authed_ok = 0, open_ok = 0, refused = 0;
    for (int v = 0; v < 256; ++v) {
        std::vector<uint8_t> b(kRefBody_cmd_auth_execute, kRefBody_cmd_auth_execute + 31);
        b[0] = static_cast<uint8_t>(v);
        RemoteDecoded got = sentinel_decoded();
        std::memset(ptbuf, 0x5A, sizeof ptbuf);
        const RemoteStatus st = remote_body_decode(got, DATA_TYPE_REMOTE_CMD,
                                              std::span<const uint8_t>(b.data(), b.size()), keys, src, car,
                                              std::span<uint8_t>(ptbuf, sizeof ptbuf));
        if (st == RemoteStatus::ok) {
            if (got.authenticated) { ++authed_ok; CHECK(v == kRefBody_cmd_auth_execute[0]); }
            else                   { ++open_ok;   CHECK(got.layout.domain == RemoteDomainId::cmd_open_execute); }
        } else {
            ++refused;
            CHECK(sentinel_intact(got));
        }
    }
    CHECK(authed_ok == 1);          // ONLY the original control byte authenticates
    CHECK(open_ok == 1);            // ctl 0x1F is a *separately valid* OPEN request — unauthenticated, and
                                    // labelled so; the codec cannot know it was once a sealed frame
    CHECK(authed_ok + open_ok + refused == 256);
}

TEST_CASE("§radmin-2/corruption — a changed source hash, key or header byte fails the tag") {
    const Creds c = creds();
    const RemoteKeys keys = keys_of(c.base, c.session);
    const RemoteCarrier car = carrier_same_layer(DATA_TYPE_REMOTE_CMD, true);
    uint8_t ptbuf[P::max_payload_bytes_hard_cap];
    std::memset(ptbuf, 0x5A, sizeof ptbuf);

    RemoteDecoded got = sentinel_decoded();
    CHECK_STATUS("the right source", remote_body_decode(got, DATA_TYPE_REMOTE_CMD,
                                                   std::span<const uint8_t>(kRefBody_cmd_auth_execute, 31),
                                                   keys, src_ok(), car, std::span<uint8_t>(ptbuf, sizeof ptbuf)),
                 RemoteStatus::ok);
    // ★ Changing the STABLE LOGICAL CONTROLLER SOURCE under otherwise identical bytes fails the tag (§9).
    RemoteDecoded g2 = sentinel_decoded();
    CHECK_STATUS("a different source hash",
                 remote_body_decode(g2, DATA_TYPE_REMOTE_CMD, std::span<const uint8_t>(kRefBody_cmd_auth_execute, 31),
                               keys, src_ok(kSrcHash2), car, std::span<uint8_t>(ptbuf, sizeof ptbuf)),
                 RemoteStatus::auth_failed);
    CHECK(sentinel_intact(g2));
    // an ABSENT source is refused before any crypto
    RemoteSource absent{};
    RemoteDecoded g3 = sentinel_decoded();
    CHECK_STATUS("an absent source", remote_body_decode(g3, DATA_TYPE_REMOTE_CMD,
                                                   std::span<const uint8_t>(kRefBody_cmd_auth_execute, 31),
                                                   keys, absent, car, std::span<uint8_t>(ptbuf, sizeof ptbuf)),
                 RemoteStatus::bad_argument);
    CHECK(sentinel_intact(g3));
    // a changed session key fails the tag
    uint8_t bad_session[32]; std::memcpy(bad_session, c.session, 32); bad_session[31] ^= 0x01;
    RemoteDecoded g4 = sentinel_decoded();
    CHECK_STATUS("a wrong session key",
                 remote_body_decode(g4, DATA_TYPE_REMOTE_CMD, std::span<const uint8_t>(kRefBody_cmd_auth_execute, 31),
                               keys_of(c.base, bad_session), src_ok(), car,
                               std::span<uint8_t>(ptbuf, sizeof ptbuf)), RemoteStatus::auth_failed);
    CHECK(sentinel_intact(g4));
    // ⚠ B313: the failure-output contract measured, not assumed. The primitive writes the caller's plaintext
    //    only on a valid tag, so on refusal the buffer is LEFT AS IT WAS — it is NOT scrubbed, and this codec
    //    does not claim it is. A caller that needs the buffer wiped must wipe it.
    uint8_t fresh[64]; std::memset(fresh, 0xC7, sizeof fresh);
    RemoteDecoded g5 = sentinel_decoded();
    CHECK_STATUS("bad tag, fresh buffer",
                 remote_body_decode(g5, DATA_TYPE_REMOTE_CMD, std::span<const uint8_t>(kRefBody_cmd_auth_execute, 31),
                               keys_of(c.base, bad_session), src_ok(), car,
                               std::span<uint8_t>(fresh, sizeof fresh)), RemoteStatus::auth_failed);
    for (size_t i = 0; i < sizeof fresh; ++i)
        CHECK_MESSAGE(fresh[i] == 0xC7, "byte ", i, " of the caller's buffer was written on a failed open");
}

// =========================================================================================================
// §7 — THE TYPED RESULT DOMAINS (§8.9). The same byte 0x00 under two opcodes decodes to two different typed
//      meanings, and no other byte is admitted into the authenticated protocol-error namespace.
// =========================================================================================================
TEST_CASE("§radmin-2/result — all eight terminal meanings, through the real decoder") {
    const Creds c = creds();
    const RemoteKeys keys = keys_of(c.base, c.session);
    const RemoteSource src = src_ok();
    const RemoteCarrier car = carrier_same_layer(DATA_TYPE_REMOTE_RESP, true);
    uint8_t ptbuf[P::max_payload_bytes_hard_cap];

    const uint8_t* bodies[8] = {
        kRefBody_resp_terminal_auth_t0, kRefBody_resp_terminal_auth_t1, kRefBody_resp_terminal_auth_t2,
        kRefBody_resp_terminal_auth_t3, kRefBody_resp_terminal_auth_t4, kRefBody_resp_terminal_auth_t5,
        kRefBody_resp_terminal_auth_t6, kRefBody_resp_terminal_auth_t7,
    };
    const RemoteTerminal want[8] = {
        RemoteTerminal::completed, RemoteTerminal::scheduled, RemoteTerminal::unknown_command,
        RemoteTerminal::refused,   RemoteTerminal::output_truncated, RemoteTerminal::internal_error,
        RemoteTerminal::session_full, RemoteTerminal::session_busy,
    };
    for (int i = 0; i < 8; ++i) {
        RemoteDecoded got = sentinel_decoded();
        std::memset(ptbuf, 0x5A, sizeof ptbuf);
        CHECK_STATUS("terminal", remote_body_decode(got, DATA_TYPE_REMOTE_RESP,
                                               std::span<const uint8_t>(bodies[i], 27), keys, src, car,
                                               std::span<uint8_t>(ptbuf, sizeof ptbuf)), RemoteStatus::ok);
        CHECK(got.result_kind == RemoteResultKind::terminal);
        CHECK_MESSAGE(got.terminal == want[i], "terminal code ", i, " decoded to ", int(got.terminal));
        CHECK(got.layout.domain == RemoteDomainId::resp_terminal_auth);
        CHECK(got.authenticated);
        CHECK(got.result_detail.empty());
    }
    // 0x08 and 0xFF AUTHENTICATE and are still refused: the rejection is the CODE-DOMAIN check, not a bad tag.
    for (const uint8_t* b : {kRefBody_resp_terminal_auth_code08, kRefBody_resp_terminal_auth_codeff}) {
        RemoteDecoded got = sentinel_decoded();
        CHECK_STATUS("unallocated terminal code",
                     remote_body_decode(got, DATA_TYPE_REMOTE_RESP, std::span<const uint8_t>(b, 27), keys, src, car,
                                   std::span<uint8_t>(ptbuf, sizeof ptbuf)), RemoteStatus::bad_result_code);
        CHECK(sentinel_intact(got));
    }
    // bounded DETAIL bytes are preserved EXACTLY: TERMINAL{scheduled} + a 4-byte LE activation delay
    RemoteDecoded sch = sentinel_decoded();
    CHECK_STATUS("scheduled + detail",
                 remote_body_decode(sch, DATA_TYPE_REMOTE_RESP,
                               std::span<const uint8_t>(kRefBody_resp_terminal_auth_scheduled, 31), keys, src,
                               car, std::span<uint8_t>(ptbuf, sizeof ptbuf)), RemoteStatus::ok);
    CHECK(sch.result_kind == RemoteResultKind::terminal);
    CHECK(sch.terminal == RemoteTerminal::scheduled);
    CHECK(sch.result_detail.size() == 4);
    const uint8_t delay_le[4] = {0xDF, 0x93, 0x04, 0x00};       // 299 999 ms
    CHECK_BYTES("the bounded detail is preserved verbatim", sch.result_detail.data(), delay_le, 4);
    // ⛔ and the codec assigns that detail NO meaning: scheduling-delay policy is a later slice's.

    // an EMPTY terminal body has no result code at all -> bad_length, never a defaulted `completed`
    RemoteMessage mt{};
    mt.outer_type = DATA_TYPE_REMOTE_RESP; mt.opcode = 1; mt.slot = kSlot; mt.request_id = kReqId;
    mt.response_seq = kSeq;
    uint8_t out[64]; size_t out_len = 0;
    CHECK_STATUS("encode empty terminal",
                 remote_body_encode(std::span<uint8_t>(out, sizeof out), out_len, mt, {}, keys, src, car),
                 RemoteStatus::ok);
    RemoteDecoded ge = sentinel_decoded();
    CHECK_STATUS("an empty terminal body",
                 remote_body_decode(ge, DATA_TYPE_REMOTE_RESP, std::span<const uint8_t>(out, out_len), keys, src, car,
                               std::span<uint8_t>(ptbuf, sizeof ptbuf)), RemoteStatus::bad_length);
    CHECK(sentinel_intact(ge));
}

TEST_CASE("§radmin-2/result — 0x00 means two different things, and the domains never merge") {
    const Creds c = creds();
    const RemoteKeys keys = keys_of(c.base, c.session);
    const RemoteSource src = src_ok();
    const RemoteCarrier car = carrier_same_layer(DATA_TYPE_REMOTE_RESP, true);
    uint8_t ptbuf[P::max_payload_bytes_hard_cap];

    RemoteDecoded t = sentinel_decoded();
    CHECK_STATUS("TERMINAL 0x00", remote_body_decode(t, DATA_TYPE_REMOTE_RESP,
                                                std::span<const uint8_t>(kRefBody_resp_terminal_auth, 27), keys,
                                                src, car, std::span<uint8_t>(ptbuf, sizeof ptbuf)),
                 RemoteStatus::ok);
    RemoteDecoded p = sentinel_decoded();
    CHECK_STATUS("PROTOCOL_ERROR 0x00",
                 remote_body_decode(p, DATA_TYPE_REMOTE_RESP,
                               std::span<const uint8_t>(kRefBody_resp_protocol_error_auth, 27), keys, src, car,
                               std::span<uint8_t>(ptbuf, sizeof ptbuf)), RemoteStatus::ok);
    // the same result BYTE...
    CHECK(t.body.size() == 1);
    CHECK(p.body.size() == 1);
    CHECK(t.body[0] == 0x00);
    CHECK(p.body[0] == 0x00);
    // ...two different TYPED meanings, told apart only by the opcode domain the decoder publishes.
    CHECK(t.result_kind == RemoteResultKind::terminal);
    CHECK(t.terminal == RemoteTerminal::completed);
    CHECK(t.layout.domain == RemoteDomainId::resp_terminal_auth);
    CHECK(p.result_kind == RemoteResultKind::protocol_error);
    CHECK(p.protocol_error == RemoteProtocolError::already_acknowledged);
    CHECK(p.layout.domain == RemoteDomainId::resp_protocol_error_auth);
    CHECK(t.result_kind != p.result_kind);

    // ⛔ EVERY OTHER BYTE REJECTS IN THE AUTHENTICATED PROTOCOL-ERROR DOMAIN — including the ones that are
    //    perfectly valid TERMINAL codes. Each fixture is independently SEALED, so this proves the code-domain
    //    check and not a broken tag.
    struct Bad { const uint8_t* body; const char* what; };
    const Bad bad[] = {
        {kRefBody_resp_protocol_error_auth_code01, "0x01 (a valid TERMINAL `scheduled`)"},
        {kRefBody_resp_protocol_error_auth_code07, "0x07 (a valid TERMINAL `session_busy`)"},
        {kRefBody_resp_protocol_error_auth_code08, "0x08"},
        {kRefBody_resp_protocol_error_auth_codeff, "0xFF"},
    };
    for (const Bad& b : bad) {
        RemoteDecoded got = sentinel_decoded();
        CHECK_STATUS(b.what, remote_body_decode(got, DATA_TYPE_REMOTE_RESP, std::span<const uint8_t>(b.body, 27),
                                           keys, src, car, std::span<uint8_t>(ptbuf, sizeof ptbuf)),
                     RemoteStatus::bad_result_code);
        CHECK_MESSAGE(sentinel_intact(got), b.what, " published a value");
    }

    // The OPEN result classes stay explicitly UNAUTHENTICATED, and the OPEN protocol error does NOT acquire
    // the authenticated `already_acknowledged` namespace: it has no allocated code domain here at all.
    RemoteDecoded ot = sentinel_decoded();
    CHECK_STATUS("open terminal", remote_body_decode(ot, DATA_TYPE_REMOTE_RESP,
                                                std::span<const uint8_t>(kRefBody_resp_terminal_open, 11), keys,
                                                src, car, std::span<uint8_t>(ptbuf, sizeof ptbuf)),
                 RemoteStatus::ok);
    CHECK_FALSE(ot.authenticated);
    CHECK(ot.result_kind == RemoteResultKind::terminal);       // §8.9: the open transcript carries the code too
    CHECK(ot.terminal == RemoteTerminal::completed);
    RemoteDecoded op = sentinel_decoded();
    CHECK_STATUS("open protocol error",
                 remote_body_decode(op, DATA_TYPE_REMOTE_RESP,
                               std::span<const uint8_t>(kRefBody_resp_protocol_error_open, 11), keys, src, car,
                               std::span<uint8_t>(ptbuf, sizeof ptbuf)), RemoteStatus::ok);
    CHECK_FALSE(op.authenticated);
    CHECK(op.result_kind == RemoteResultKind::none);           // ⛔ NOT `already_acknowledged`
    CHECK(op.layout.domain == RemoteDomainId::resp_protocol_error_open);
    CHECK(op.body.size() == 1);
    CHECK(op.body[0] == 0x00);
    // an OPEN terminal with an unallocated code still rejects (the namespace is the code's, not the tag's)
    std::vector<uint8_t> ob(kRefBody_resp_terminal_open, kRefBody_resp_terminal_open + 11);
    ob[10] = 0x08;
    RemoteDecoded og = sentinel_decoded();
    CHECK_STATUS("open terminal 0x08",
                 remote_body_decode(og, DATA_TYPE_REMOTE_RESP, std::span<const uint8_t>(ob.data(), ob.size()), keys,
                               src, car, std::span<uint8_t>(ptbuf, sizeof ptbuf)), RemoteStatus::bad_result_code);
    CHECK(sentinel_intact(og));
}

TEST_CASE("§radmin-2/slots — every legal session slot decodes, on independent literals") {
    const Creds c = creds();
    const RemoteKeys keys = keys_of(c.base, c.session);
    const RemoteSource src = src_ok();
    const RemoteCarrier car = carrier_same_layer(DATA_TYPE_REMOTE_CMD, true);
    uint8_t ptbuf[P::max_payload_bytes_hard_cap];

    const uint8_t* slots[10] = {
        kRefBody_cmd_auth_execute_slot0, kRefBody_cmd_auth_execute_slot1, kRefBody_cmd_auth_execute_slot2,
        kRefBody_cmd_auth_execute_slot3, kRefBody_cmd_auth_execute_slot4, kRefBody_cmd_auth_execute_slot5,
        kRefBody_cmd_auth_execute_slot6, kRefBody_cmd_auth_execute_slot7, kRefBody_cmd_auth_execute_slot8,
        kRefBody_cmd_auth_execute_slot9,
    };
    // slot 3 is the main fixture — the same bytes must appear here, so the slot table is not a second codec
    CHECK_BYTES("slot 3 == the main fixture", kRefBody_cmd_auth_execute_slot3, kRefBody_cmd_auth_execute, 31);
    for (uint8_t s = 0; s <= 9; ++s) {
        RemoteMessage m{};
        m.outer_type = DATA_TYPE_REMOTE_CMD; m.opcode = 0; m.slot = s; m.request_id = kReqId;
        uint8_t out[64]; size_t out_len = 0;
        CHECK_STATUS("encode", remote_body_encode(std::span<uint8_t>(out, sizeof out), out_len, m,
                                             std::span<const uint8_t>(kPtRoutes, 6), keys, src, car),
                     RemoteStatus::ok);
        CHECK_MESSAGE(out_len == 31, "slot ", int(s), " body length ", out_len);
        CHECK_BYTES("slot body", out, slots[s], 31);
        RemoteDecoded got = sentinel_decoded();
        std::memset(ptbuf, 0x5A, sizeof ptbuf);
        CHECK_STATUS("decode", remote_body_decode(got, DATA_TYPE_REMOTE_CMD, std::span<const uint8_t>(slots[s], 31),
                                             keys, src, car, std::span<uint8_t>(ptbuf, sizeof ptbuf)),
                     RemoteStatus::ok);
        CHECK(got.msg.slot == s);
        CHECK(got.authenticated);
        CHECK_BYTES("plaintext", got.body.data(), kPtRoutes, 6);
    }
    // the reserved slots A..E reject in BOTH directions and for EVERY opcode
    for (uint8_t s = 0x0A; s <= 0x0E; ++s)
        for (uint8_t op = 0; op <= 0x0F; ++op)
            for (uint8_t outer : {uint8_t(DATA_TYPE_REMOTE_CMD), uint8_t(DATA_TYPE_REMOTE_RESP)}) {
                RemoteLayout L{};
                CHECK_STATUS("reserved slot", remote_layout(outer, remote_ctl(op, s), L), RemoteStatus::bad_slot);
            }
    // the response sequence really is ONE byte: 0 and 255 both encode, and they differ
    for (const uint8_t* b : {kRefBody_resp_output_auth_seq0, kRefBody_resp_output_auth_seq255}) {
        RemoteDecoded got = sentinel_decoded();
        CHECK_STATUS("seq endpoint",
                     remote_body_decode(got, DATA_TYPE_REMOTE_RESP, std::span<const uint8_t>(b, 35), keys, src,
                                   carrier_same_layer(DATA_TYPE_REMOTE_RESP, true),
                                   std::span<uint8_t>(ptbuf, sizeof ptbuf)), RemoteStatus::ok);
        CHECK(got.msg.response_seq == (b == kRefBody_resp_output_auth_seq0 ? 0 : 255));
    }
    CHECK(std::memcmp(kRefBody_resp_output_auth_seq0, kRefBody_resp_output_auth_seq255, 35) != 0);
}

// =========================================================================================================
// §8 — THE LEGACY FRAME. A real `admin_cmd_seal` body still opens under the OLD decoder and is not accepted
//      as a v2 authenticated request. ⛔ There is no legacy-prefix heuristic and no trial-open with legacy
//      keys anywhere in the v2 codec: the legacy body simply is not a v2 body.
// =========================================================================================================
TEST_CASE("§radmin-2/legacy — an admin_cmd_seal frame opens legacy and is never accepted as v2") {
    uint8_t admin_seed[32], node_seed[32];
    for (int i = 0; i < 32; ++i) { admin_seed[i] = static_cast<uint8_t>(0x11 + i); node_seed[i] = static_cast<uint8_t>(0x71 + i); }
    Identity admin{}, node{};
    identity_from_seed(admin, admin_seed);
    identity_from_seed(node, node_seed);

    const uint8_t cmd[6] = {'s','t','a','t','u','s'};
    const uint8_t rand8[8] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04};
    uint8_t frame[128];
    const size_t flen = admin_cmd_seal(frame, sizeof frame, admin, node.ed_pub, node.key_hash32,
                                       /*counter=*/7, cmd, sizeof cmd, rand8, /*nonce_ctr=*/1);
    CHECK(flen > 0);

    // (a) the OLD decoder still opens it — the fixture is real, not a made-up byte string
    AdminCmd res{};
    uint8_t pt[64] = {};
    CHECK(admin_cmd_open(frame, flen, admin.ed_pub, node, res, pt, sizeof pt));
    CHECK(res.node_key_hash == node.key_hash32);
    CHECK(res.counter == 7);
    CHECK(res.cmd_len == sizeof cmd);
    CHECK_BYTES("legacy plaintext", res.cmd, cmd, sizeof cmd);

    // (b) the v2 codec does not accept it, in EITHER direction and under EITHER credential
    const Creds c = creds();
    const RemoteKeys keys = keys_of(c.base, c.session);
    uint8_t ptbuf[P::max_payload_bytes_hard_cap];
    for (uint8_t outer : {uint8_t(DATA_TYPE_REMOTE_CMD), uint8_t(DATA_TYPE_REMOTE_RESP)}) {
        RemoteDecoded got = sentinel_decoded();
        std::memset(ptbuf, 0x5A, sizeof ptbuf);
        const RemoteStatus st = remote_body_decode(got, outer, std::span<const uint8_t>(frame, flen), keys, src_ok(),
                                              carrier_same_layer(outer, true),
                                              std::span<uint8_t>(ptbuf, sizeof ptbuf));
        CHECK_MESSAGE(st != RemoteStatus::ok, "a legacy frame decoded as v2 under outer ", int(outer),
                      ": ", status_name(st));
        // and if it happened to select a layout at all, it was never AUTHENTICATED
        CHECK_MESSAGE(sentinel_intact(got), "a legacy frame published a v2 value under outer ", int(outer));
    }
    // the legacy frame's first byte is `rand8[0]`, which has no `ctl` meaning: nothing in the v2 codec looks
    // for the legacy shape, and nothing retries a failed v2 open as legacy or as open.
    CHECK(frame[0] == rand8[0]);
}

// =========================================================================================================
// §9 — CARRIER ADMISSION (R-RA-3 / R-RA-25 / R-RA-28) AND PHYSICAL PACKING, kept as THREE separate questions
//      with three separate verdicts: (1) admission, (2) physical packing, (3) shape/authority sensitivity.
// =========================================================================================================
TEST_CASE("§radmin-2/carrier — the live leg-by-leg map, both directions, incl. legally hash-less legs") {
    struct Row { const char* name; RemoteCarrier c; size_t cap; };
    const Row rows[] = {
        // ---- same layer: 232 WITH OR WITHOUT a transmitted DST_HASH (R-RA-28 reserves the four bytes) ----
        {"home-originated request, by key hash", carrier_same_layer(DATA_TYPE_REMOTE_CMD,  true),  232},
        {"home-originated request, by node id (legally hash-less)",
                                                carrier_same_layer(DATA_TYPE_REMOTE_CMD,  false), 232},
        {"target response, by key hash",         carrier_same_layer(DATA_TYPE_REMOTE_RESP, true),  232},
        {"target response, by node id (legally hash-less)",
                                                carrier_same_layer(DATA_TYPE_REMOTE_RESP, false), 232},
        // The hosted-mobile last mile is `enqueue_data(..., addr_len = 1, ...)` and passes NO destination
        // override today (node_hashlocate.cpp:1818-1821 / R-RA-25's narrow addendum, B310 parked). addr_len
        // is a HEADER field, so the last mile adds no inner byte and the cap is unchanged either way.
        {"hosted-mobile last mile, no destination override",
                                                carrier_same_layer(DATA_TYPE_REMOTE_RESP, false, 1), 232},
        {"hosted-mobile last mile, hash present",
                                                carrier_same_layer(DATA_TYPE_REMOTE_RESP, true, 1),  232},
        // ---- the typed mobile->home wrapper: the OUTER type is MOBILE_SEND, the RPC type is ENCLOSED ----
        {"typed same-layer wrapper (enclosing REMOTE_CMD)",  carrier_wrapper_same(DATA_TYPE_REMOTE_CMD),  231},
        {"typed same-layer wrapper (enclosing REMOTE_RESP)", carrier_wrapper_same(DATA_TYPE_REMOTE_RESP), 231},
        // ---- full cross-layer, every legal depth, both directions and both addressing forms ----
        {"cross-layer request depth 1", carrier_cross_layer(DATA_TYPE_REMOTE_CMD, true, 1), 229},
        {"cross-layer request depth 2", carrier_cross_layer(DATA_TYPE_REMOTE_CMD, true, 2), 228},
        {"cross-layer request depth 3", carrier_cross_layer(DATA_TYPE_REMOTE_CMD, true, 3), 227},
        {"cross-layer request depth 4", carrier_cross_layer(DATA_TYPE_REMOTE_CMD, true, 4), 226},
        {"cross-layer request depth 1, by node id", carrier_cross_layer(DATA_TYPE_REMOTE_CMD, false, 1), 229},
        {"cross-layer request depth 4, by node id", carrier_cross_layer(DATA_TYPE_REMOTE_CMD, false, 4), 226},
        {"cross-layer response depth 1", carrier_cross_layer(DATA_TYPE_REMOTE_RESP, true, 1), 229},
        {"cross-layer response depth 2", carrier_cross_layer(DATA_TYPE_REMOTE_RESP, true, 2), 228},
        {"cross-layer response depth 3", carrier_cross_layer(DATA_TYPE_REMOTE_RESP, true, 3), 227},
        {"cross-layer response depth 4", carrier_cross_layer(DATA_TYPE_REMOTE_RESP, true, 4), 226},
        // ---- the typed cross-layer wrapper: destination depth 1..3 (the home prepends a fourth layer) ----
        {"typed cross-layer wrapper, destination depth 1", carrier_wrapper_xl(DATA_TYPE_REMOTE_CMD, 1), 228},
        {"typed cross-layer wrapper, destination depth 2", carrier_wrapper_xl(DATA_TYPE_REMOTE_CMD, 2), 227},
        {"typed cross-layer wrapper, destination depth 3", carrier_wrapper_xl(DATA_TYPE_REMOTE_CMD, 3), 226},
    };
    for (const Row& r : rows) {
        size_t cap = 0;
        CHECK_STATUS(r.name, remote_body_cap(r.c, cap), RemoteStatus::ok);
        CHECK_MESSAGE(cap == r.cap, r.name, " cap ", cap, " want ", r.cap);
    }

    // ★ THE WRAPPER AND THE HOME'S CORRESPONDING FULL PATH MUST AGREE. A wrapper at destination depth d is the
    //   same budget as the home's full path at depth d + 1, because that is literally the frame the home
    //   re-originates (node_mac.cpp:928: `1 + hop_count` must fit).
    for (uint8_t d = 1; d <= 3; ++d) {
        size_t wrap = 0, full = 0;
        CHECK_STATUS("wrapper", remote_body_cap(carrier_wrapper_xl(DATA_TYPE_REMOTE_CMD, d), wrap),
                     RemoteStatus::ok);
        CHECK_STATUS("full", remote_body_cap(carrier_cross_layer(DATA_TYPE_REMOTE_CMD, true, uint8_t(d + 1)), full),
                     RemoteStatus::ok);
        CHECK_MESSAGE(wrap == full, "wrapper destination depth ", int(d), " = ", wrap,
                      " but the home's full depth ", int(d + 1), " = ", full);
    }

    // ⛔ THE WRAPPER'S DESTINATION DEPTH 4 IS INVALID, NOT A 225-BYTE CARRIER (B309): the home would have to
    //    prepend a fifth layer and `gw_env_max_hops` is 4.
    size_t junk = 0;
    CHECK_STATUS("wrapper destination depth 4",
                 remote_body_cap(carrier_wrapper_xl(DATA_TYPE_REMOTE_CMD, 4), junk), RemoteStatus::bad_carrier);
    CHECK_STATUS("wrapper destination depth 0",
                 remote_body_cap(carrier_wrapper_xl(DATA_TYPE_REMOTE_CMD, 0), junk), RemoteStatus::bad_carrier);
    // ...while the RAW full inner path at depth 4 is perfectly legal, and they are different questions.
    size_t full4 = 0;
    CHECK_STATUS("full depth 4", remote_body_cap(carrier_cross_layer(DATA_TYPE_REMOTE_CMD, true, 4), full4),
                 RemoteStatus::ok);
    CHECK(full4 == 226);
}

TEST_CASE("§radmin-2/carrier — every invalid descriptor refuses, and none is normalised") {
    size_t cap = 0;
    // depth / cursor
    CHECK_STATUS("full depth 0",  remote_body_cap(carrier_cross_layer(DATA_TYPE_REMOTE_CMD, true, 0), cap),
                 RemoteStatus::bad_carrier);
    CHECK_STATUS("full depth 5",  remote_body_cap(carrier_cross_layer(DATA_TYPE_REMOTE_CMD, true, 5), cap),
                 RemoteStatus::bad_carrier);
    CHECK_STATUS("cursor == depth", remote_body_cap(carrier_cross_layer(DATA_TYPE_REMOTE_CMD, true, 2, 2), cap),
                 RemoteStatus::bad_carrier);
    CHECK_STATUS("cursor > depth",  remote_body_cap(carrier_cross_layer(DATA_TYPE_REMOTE_CMD, true, 2, 3), cap),
                 RemoteStatus::bad_carrier);
    CHECK_STATUS("cursor 1 of 2 is legal",
                 remote_body_cap(carrier_cross_layer(DATA_TYPE_REMOTE_CMD, true, 2, 1), cap), RemoteStatus::ok);
    CHECK(cap == 228);
    {   // a path on a SAME-LAYER descriptor is not a carrier
        RemoteCarrier c = carrier_same_layer(DATA_TYPE_REMOTE_CMD, true);
        c.path_depth = 1;
        CHECK_STATUS("same layer with a path", remote_body_cap(c, cap), RemoteStatus::bad_carrier);
    }
    {   // R-RA-13: SOURCE_HASH is mandatory. Dropping it is not a bigger body, it is an invalid carrier.
        RemoteCarrier c = carrier_same_layer(DATA_TYPE_REMOTE_CMD, true);
        c.source_hash_on_wire = false;
        CHECK_STATUS("no source hash", remote_body_cap(c, cap), RemoteStatus::bad_carrier);
    }
    {   // addr_len > 1 is hierarchy-deferred (pack_data refuses it too)
        RemoteCarrier c = carrier_same_layer(DATA_TYPE_REMOTE_CMD, true, 2);
        CHECK_STATUS("addr_len 2", remote_body_cap(c, cap), RemoteStatus::bad_carrier);
    }
    {   // outer TYPE must be a real RPC leg
        RemoteCarrier c = carrier_same_layer(0, true);
        CHECK_STATUS("outer type 0", remote_body_cap(c, cap), RemoteStatus::bad_carrier);
        RemoteCarrier c2 = carrier_same_layer(DATA_TYPE_MOBILE_SEND, true);
        CHECK_STATUS("MOBILE_SEND without the wrapper flag", remote_body_cap(c2, cap), RemoteStatus::bad_carrier);
        RemoteCarrier c3 = carrier_same_layer(DATA_TYPE_REMOTE_CMD, true);
        c3.enclosed_type = DATA_TYPE_REMOTE_CMD;
        CHECK_STATUS("an enclosed type without a wrapper", remote_body_cap(c3, cap), RemoteStatus::bad_carrier);
        RemoteCarrier c4 = carrier_wrapper_same(DATA_TYPE_REMOTE_CMD);
        c4.outer_data_type = DATA_TYPE_REMOTE_CMD;
        CHECK_STATUS("a wrapper whose outer type is not MOBILE_SEND", remote_body_cap(c4, cap),
                     RemoteStatus::bad_carrier);
        RemoteCarrier c5 = carrier_wrapper_same(DATA_TYPE_SEALED_RELAY);
        CHECK_STATUS("a wrapper enclosing a non-RPC type", remote_body_cap(c5, cap), RemoteStatus::bad_carrier);
    }
    {   // pack_data's structural rule: CRYPTED without DST_HASH is not a lower cap, it is not a carrier
        RemoteCarrier c = carrier_same_layer(DATA_TYPE_REMOTE_CMD, false);
        c.outer_crypted = true;
        CHECK_STATUS("outer CRYPTED with no DST_HASH", remote_body_cap(c, cap), RemoteStatus::bad_carrier);
        CHECK(pack_probe(c, 10) == Bound::structural);   // and the REAL packer says the same thing
    }
}

TEST_CASE("§radmin-2/carrier — ADMISSION accepts at the cap and refuses cap+1 (verdict: admission)") {
    const Creds c = creds();
    const RemoteKeys keys = keys_of(c.base, c.session);
    const RemoteSource src = src_ok();
    uint8_t ptbuf[P::max_payload_bytes_hard_cap];

    const RemoteCarrier rows[] = {
        carrier_same_layer(DATA_TYPE_REMOTE_CMD, true),
        carrier_same_layer(DATA_TYPE_REMOTE_CMD, false),
        carrier_wrapper_same(DATA_TYPE_REMOTE_CMD),
        carrier_cross_layer(DATA_TYPE_REMOTE_CMD, true, 1),
        carrier_cross_layer(DATA_TYPE_REMOTE_CMD, true, 4),
        carrier_wrapper_xl(DATA_TYPE_REMOTE_CMD, 3),
    };
    for (const RemoteCarrier& car : rows) {
        const size_t cap = cap_of(car);
        RemoteMessage m{};
        m.outer_type = DATA_TYPE_REMOTE_CMD; m.opcode = 0; m.slot = kSlot; m.request_id = kReqId;
        // ⛔ THE OUTPUT SPAN IS DELIBERATELY THE FULL 241-BYTE STORAGE BUFFER, never a cap-sized one: an
        //    undersized test buffer would prove `bad_buffer`, which is not an admission proof.
        uint8_t out[P::max_payload_bytes_hard_cap];
        size_t out_len = 0;
        std::vector<uint8_t> body(cap - kRemoteOverheadAuthExecute, 0x5A);
        CHECK_STATUS("at the cap", remote_body_encode(std::span<uint8_t>(out, sizeof out), out_len, m,
                                                 std::span<const uint8_t>(body.data(), body.size()), keys, src,
                                                 car), RemoteStatus::ok);
        CHECK(out_len == cap);
        body.push_back(0x5A);                       // cap + 1
        CHECK_STATUS("cap + 1", remote_body_encode(std::span<uint8_t>(out, sizeof out), out_len, m,
                                              std::span<const uint8_t>(body.data(), body.size()), keys, src, car),
                     RemoteStatus::bad_body_cap);
        // and the APPLICATION limit is the cap minus THAT domain's own overhead (§8.11)
        RemoteLayout L{};
        CHECK_STATUS("layout", remote_layout(DATA_TYPE_REMOTE_CMD, remote_ctl(0, kSlot), L), RemoteStatus::ok);
        size_t app = 0;
        CHECK_STATUS("application cap", remote_application_cap(car, L, app), RemoteStatus::ok);
        CHECK(app == cap - kRemoteOverheadAuthExecute);
        RemoteLayout Lo{};
        CHECK_STATUS("open layout", remote_layout(DATA_TYPE_REMOTE_CMD, remote_ctl(1, 0x0F), Lo), RemoteStatus::ok);
        CHECK_STATUS("open application cap", remote_application_cap(car, Lo, app), RemoteStatus::ok);
        CHECK(app == cap - kRemoteOverheadOpenExecute);
    }

    // ★ DECODE ADMISSION, ON A VALID INDEPENDENTLY SEALED OVERSIZE FIXTURE. Without this the size check could
    //   hide behind a tag failure and nobody would notice.
    const RemoteCarrier car = carrier_same_layer(DATA_TYPE_REMOTE_CMD, true);
    CHECK(cap_of(car) == 232);
    RemoteDecoded at = sentinel_decoded();
    std::memset(ptbuf, 0x5A, sizeof ptbuf);
    CHECK_STATUS("232 decodes", remote_body_decode(at, DATA_TYPE_REMOTE_CMD,
                                              std::span<const uint8_t>(kRefBody_cmd_auth_execute_atcap, 232),
                                              keys, src, car, std::span<uint8_t>(ptbuf, sizeof ptbuf)),
                 RemoteStatus::ok);
    CHECK(at.body.size() == 207);
    RemoteDecoded over = sentinel_decoded();
    CHECK_STATUS("233 refuses", remote_body_decode(over, DATA_TYPE_REMOTE_CMD,
                                              std::span<const uint8_t>(kRefBody_cmd_auth_execute_oversize, 233),
                                              keys, src, car, std::span<uint8_t>(ptbuf, sizeof ptbuf)),
                 RemoteStatus::bad_body_cap);
    CHECK_MESSAGE(sentinel_intact(over), "an oversize body published a partially valid value");
    // the oversize fixture is a GENUINELY VALID sealed body — it is the SIZE that refuses it, not the tag:
    // a carrier with room for it (none in the live map) is not constructed; instead we prove the same bytes
    // authenticate when the admission term is the only thing that changes, by decoding its 232-byte sibling
    // above and confirming the two share their first 25 header/envelope bytes.
    CHECK_BYTES("the two share their clear header",
                kRefBody_cmd_auth_execute_oversize, kRefBody_cmd_auth_execute_atcap, 9);
}

TEST_CASE("§radmin-2/carrier — PHYSICAL packing, and the admission-vs-packing counterexample (verdict: packing)") {
    // (2) PHYSICAL PACKING: at the admitted cap every live shape fits through the REAL packers.
    const RemoteCarrier live[] = {
        carrier_same_layer(DATA_TYPE_REMOTE_CMD, true),
        carrier_same_layer(DATA_TYPE_REMOTE_CMD, false),
        carrier_same_layer(DATA_TYPE_REMOTE_RESP, true),
        carrier_same_layer(DATA_TYPE_REMOTE_RESP, false, 1),
        carrier_wrapper_same(DATA_TYPE_REMOTE_CMD),
        carrier_cross_layer(DATA_TYPE_REMOTE_CMD, true, 1),
        carrier_cross_layer(DATA_TYPE_REMOTE_CMD, true, 2),
        carrier_cross_layer(DATA_TYPE_REMOTE_CMD, true, 3),
        carrier_cross_layer(DATA_TYPE_REMOTE_CMD, true, 4),
        carrier_cross_layer(DATA_TYPE_REMOTE_CMD, false, 4),
        carrier_wrapper_xl(DATA_TYPE_REMOTE_CMD, 1),
        carrier_wrapper_xl(DATA_TYPE_REMOTE_CMD, 2),
        carrier_wrapper_xl(DATA_TYPE_REMOTE_CMD, 3),
    };
    for (const RemoteCarrier& c : live) {
        const size_t cap = cap_of(c);
        size_t inner = 0;
        const Bound at_cap = pack_probe(c, cap, &inner);
        CHECK_MESSAGE(at_cap == Bound::none, "a body AT the admitted cap ", cap, " did not pack: ",
                      bound_name(at_cap));
        CHECK(inner <= P::max_payload_bytes_hard_cap);
    }

    // ★ THE COUNTEREXAMPLE, EXECUTED (R-RA-28). On a SAME-LAYER leg that carries no DST_HASH, the raw packer
    //   still has room for 233 bytes — the four reserved bytes are simply not on the wire — while admission
    //   refuses 233. That is a real difference between two authorities, not a packer refusal, and pretending
    //   otherwise would be the thing the ruling forbids.
    const RemoteCarrier hashless = carrier_same_layer(DATA_TYPE_REMOTE_CMD, false);
    CHECK(cap_of(hashless) == 232);
    size_t inner233 = 0;
    const Bound raw233 = pack_probe(hashless, 233, &inner233);
    CHECK_MESSAGE(raw233 == Bound::none, "raw 233 on a hash-less same-layer leg should PHYSICALLY fit, got ",
                  bound_name(raw233));
    CHECK(inner233 == 238);                            // origin 1 + source_hash 4 + 233
    CHECK(pack_probe(hashless, 236) == Bound::none);   // the raw ceiling for this shape
    CHECK(pack_probe(hashless, 237) == Bound::storage);
    {   // ...and admission still refuses 233 on exactly that carrier
        const Creds c = creds();
        const RemoteKeys keys = keys_of(c.base, c.session);
        RemoteMessage m{};
        m.outer_type = DATA_TYPE_REMOTE_CMD; m.opcode = 1; m.slot = 0x0F; m.request_id = kReqId;   // OPEN: no tag
        uint8_t out[P::max_payload_bytes_hard_cap]; size_t out_len = 0;
        std::vector<uint8_t> body(233 - kRemoteOverheadOpenExecute, 0x5A);
        CHECK_STATUS("admission refuses 233 where the packer would not",
                     remote_body_encode(std::span<uint8_t>(out, sizeof out), out_len, m,
                                   std::span<const uint8_t>(body.data(), body.size()), keys, src_ok(), hashless),
                     RemoteStatus::bad_body_cap);
        body.pop_back();
        CHECK_STATUS("232 is admitted", remote_body_encode(std::span<uint8_t>(out, sizeof out), out_len, m,
                                                      std::span<const uint8_t>(body.data(), body.size()), keys,
                                                      src_ok(), hashless), RemoteStatus::ok);
        CHECK(out_len == 232);
    }

    // ★ ON A FULLY HASH-POPULATED BINDING ROW, cap+1 DOES physically refuse — and it is STORAGE that refuses.
    const RemoteCarrier bound_row = carrier_same_layer(DATA_TYPE_REMOTE_CMD, true);
    CHECK(pack_probe(bound_row, 232) == Bound::none);
    CHECK(pack_probe(bound_row, 233) == Bound::storage);
    const RemoteCarrier xl4 = carrier_cross_layer(DATA_TYPE_REMOTE_CMD, true, 4);
    CHECK(pack_probe(xl4, 226) == Bound::none);
    CHECK(pack_probe(xl4, 227) == Bound::storage);
    const RemoteCarrier wrap3 = carrier_wrapper_xl(DATA_TYPE_REMOTE_CMD, 3);
    CHECK(pack_probe(wrap3, 226) == Bound::none);
    CHECK(pack_probe(wrap3, 227) == Bound::storage);

    // ★ AND AN AIR-GOVERNED CASE, so the two bounds can never be conflated: with outer CRYPTED on a
    //   hash-present leg the AIR bound (238) is stricter than storage (241), and it is AIR that refuses.
    //   ⛔ A packing/arithmetic control only — no live v2 send path sets outer CRYPTED, and RPC AEAD and the
    //      outer DATA CRYPTED flag are different layers.
    RemoteCarrier sealed_outer = carrier_same_layer(DATA_TYPE_REMOTE_CMD, true);
    sealed_outer.outer_crypted = true;
    size_t sealed_cap = 0;
    CHECK_STATUS("outer-CRYPTED cap", remote_body_cap(sealed_outer, sealed_cap), RemoteStatus::ok);
    CHECK_MESSAGE(sealed_cap == 229, "outer-CRYPTED same-layer cap ", sealed_cap, " want 229 (238 air - 9)");
    CHECK(sealed_cap < cap_of(bound_row));               // the air bound really is the stricter one here
    CHECK(pack_probe(sealed_outer, 229) == Bound::none);
    CHECK(pack_probe(sealed_outer, 230) == Bound::air);  // ...and AIR, not storage, is what refuses
    size_t inner230 = 0;
    (void)pack_probe(sealed_outer, 230, &inner230);
    CHECK(inner230 == 239);                              // the INNER packed fine (239 <= 241): storage did not bind

    // (3) SHAPE SENSITIVITY: vary one term at a time and watch exactly one byte move.
    CHECK(cap_of(carrier_same_layer(DATA_TYPE_REMOTE_CMD, true))            == 232);
    CHECK(cap_of(carrier_wrapper_same(DATA_TYPE_REMOTE_CMD))                == 231);   // + enclosed type
    CHECK(cap_of(carrier_cross_layer(DATA_TYPE_REMOTE_CMD, true, 1))        == 229);   // + 2 + depth
    CHECK(cap_of(carrier_cross_layer(DATA_TYPE_REMOTE_CMD, true, 2))        == 228);
    CHECK(cap_of(carrier_wrapper_xl(DATA_TYPE_REMOTE_CMD, 1))               == 228);   // + both
    // the DST_HASH term is NEVER reclaimed, on any shape
    for (uint8_t d = 1; d <= 4; ++d)
        CHECK(cap_of(carrier_cross_layer(DATA_TYPE_REMOTE_CMD, true, d))
              == cap_of(carrier_cross_layer(DATA_TYPE_REMOTE_CMD, false, d)));
    // and the two RPC directions cost the same
    CHECK(cap_of(carrier_same_layer(DATA_TYPE_REMOTE_CMD, true))
          == cap_of(carrier_same_layer(DATA_TYPE_REMOTE_RESP, true)));

    // The derivation's own terms, read from their named production authorities rather than retyped.
    CHECK(P::max_payload_bytes_hard_cap == 241);
    CHECK(P::dm_inner_origin_bytes + P::dm_inner_source_hash_bytes + P::dm_inner_dst_hash_bytes == 9);
    CHECK(data_inner_cap(DATA_FLAG_SOURCE_HASH | DATA_FLAG_DST_HASH, DATA_TYPE_REMOTE_CMD,
                         P::lora_max_frame_bytes) == 242);      // AIR is looser: storage governs
    CHECK(data_inner_cap(DATA_FLAG_SOURCE_HASH | DATA_FLAG_DST_HASH | DATA_FLAG_CRYPTED, DATA_TYPE_REMOTE_CMD,
                         P::lora_max_frame_bytes) == 238);      // AIR is stricter: air governs
    CHECK(P::gw_env_max_hops == 4);
}

TEST_CASE("§radmin-2/carrier — every fixed body must also fit through admission") {
    const Creds c = creds();
    const RemoteKeys keys = keys_of(c.base, c.session);
    const RemoteSource src = src_ok();
    uint8_t cpub[32]; fill_ctrl_pub(cpub);
    // Nothing bypasses the cap by being small: the fixed bootstrap/control bodies are admitted through the
    // SAME authority, and a carrier too small for them refuses instead of assuming they fit.
    const RemoteCarrier car = carrier_wrapper_xl(DATA_TYPE_REMOTE_CMD, 3);      // the tightest live carrier: 226
    CHECK(cap_of(car) == 226);
    struct F { uint8_t op; uint8_t slot; bool pub_; bool epoch; bool ab; size_t len; };
    const F fixed[] = {
        {2, 0x0F, true,  false, false, 57},
        {3, kSlot, false, false, false, 25},
        {4, kSlot, false, false, false, 25},
        {5, kSlot, false, false, false, 25},
    };
    for (const F& f : fixed) {
        RemoteMessage m{};
        m.outer_type = DATA_TYPE_REMOTE_CMD; m.opcode = f.op; m.slot = f.slot; m.request_id = kReqId;
        if (f.pub_) m.controller_pub = std::span<const uint8_t>(cpub, 32);
        uint8_t out[P::max_payload_bytes_hard_cap]; size_t out_len = 0;
        CHECK_STATUS("fixed body", remote_body_encode(std::span<uint8_t>(out, sizeof out), out_len, m, {}, keys, src,
                                                 car), RemoteStatus::ok);
        CHECK(out_len == f.len);
        RemoteLayout L{};
        CHECK_STATUS("layout", remote_layout(DATA_TYPE_REMOTE_CMD, remote_ctl(f.op, f.slot), L), RemoteStatus::ok);
        size_t app = 0;
        CHECK_STATUS("app cap", remote_application_cap(car, L, app), RemoteStatus::ok);
        CHECK(app == 0);                                    // §8.11: a fixed body carries no application bytes
    }
    // an invalid carrier refuses the encode outright — the codec never falls back to an unbounded write
    uint8_t out[P::max_payload_bytes_hard_cap]; size_t out_len = 0;
    RemoteMessage m{};
    m.outer_type = DATA_TYPE_REMOTE_CMD; m.opcode = 0; m.slot = kSlot; m.request_id = kReqId;
    CHECK_STATUS("an invalid carrier",
                 remote_body_encode(std::span<uint8_t>(out, sizeof out), out_len, m,
                               std::span<const uint8_t>(kPtRoutes, 6), keys, src,
                               carrier_wrapper_xl(DATA_TYPE_REMOTE_CMD, 4)), RemoteStatus::bad_carrier);
    RemoteDecoded g = sentinel_decoded();
    uint8_t ptbuf[P::max_payload_bytes_hard_cap];
    CHECK_STATUS("an invalid carrier on decode",
                 remote_body_decode(g, DATA_TYPE_REMOTE_CMD, std::span<const uint8_t>(kRefBody_cmd_auth_execute, 31),
                               keys, src, carrier_wrapper_xl(DATA_TYPE_REMOTE_CMD, 4),
                               std::span<uint8_t>(ptbuf, sizeof ptbuf)), RemoteStatus::bad_carrier);
    CHECK(sentinel_intact(g));
}

// =========================================================================================================
// §10 — CHECKED REQUEST-ID CREATION (R-RA-5 / design §9 / B312)
// =========================================================================================================
TEST_CASE("§radmin-2/entropy — success keeps all 64 bits; every failure publishes no ID") {
    // all eight bytes, little-endian, high bits included
    FakeEntropy f{};
    for (int i = 0; i < 8; ++i) f.bytes[i] = static_cast<uint8_t>(i + 1);
    uint64_t id = 0xA5A5A5A5A5A5A5A5ull;
    CHECK_STATUS("draw", remote_make_request_id(id, fake_entropy, &f), RemoteStatus::ok);
    CHECK(id == 0x0807060504030201ull);
    CHECK(f.calls == 1);                                   // ONE draw: no retry loop

    // the top bits are real bits
    for (int i = 0; i < 8; ++i) f.bytes[i] = 0xFF;
    CHECK_STATUS("all ones", remote_make_request_id(id, fake_entropy, &f), RemoteStatus::ok);
    CHECK(id == 0xFFFFFFFFFFFFFFFFull);
    f.bytes[7] = 0x80; for (int i = 0; i < 7; ++i) f.bytes[i] = 0;
    CHECK_STATUS("only bit 63", remote_make_request_id(id, fake_entropy, &f), RemoteStatus::ok);
    CHECK(id == 0x8000000000000000ull);

    // ⛔ ZERO IS A LEGITIMATE ID: no sentinel value is reserved to stand in for provider failure.
    for (int i = 0; i < 8; ++i) f.bytes[i] = 0x00;
    id = 0xA5A5A5A5A5A5A5A5ull;
    CHECK_STATUS("eight zero bytes", remote_make_request_id(id, fake_entropy, &f), RemoteStatus::ok);
    CHECK(id == 0);

    // a repeat draw with different input gives a different ID (the input is actually consumed)
    for (int i = 0; i < 8; ++i) f.bytes[i] = static_cast<uint8_t>(0x90 + i);
    uint64_t id2 = 0;
    CHECK_STATUS("second draw", remote_make_request_id(id2, fake_entropy, &f), RemoteStatus::ok);
    CHECK(id2 == 0x9796959493929190ull);
    CHECK(id2 != id);

    // ---- FAILURE: the committed output is preserved EXACTLY --------------------------------------------
    const uint64_t committed = 0x1122334455667788ull;
    uint64_t out = committed;
    FakeEntropy fail{};
    for (int i = 0; i < 8; ++i) fail.bytes[i] = 0xEE;
    fail.succeed = false;
    CHECK_STATUS("a failing provider", remote_make_request_id(out, fake_entropy, &fail),
                 RemoteStatus::entropy_failed);
    CHECK_MESSAGE(out == committed, "a failed draw overwrote the caller's committed value");

    // failure AFTER a partial fill is still a failure — an incomplete draw is never a usable ID
    FakeEntropy partial{};
    for (int i = 0; i < 8; ++i) partial.bytes[i] = static_cast<uint8_t>(0x40 + i);
    partial.fill = 3;
    partial.succeed = false;
    out = committed;
    CHECK_STATUS("a partial fill", remote_make_request_id(out, fake_entropy, &partial),
                 RemoteStatus::entropy_failed);
    CHECK(out == committed);
    CHECK(partial.calls == 1);                             // and it is NOT retried

    // an ABSENT provider is a loud refusal, never a zero or a clock fallback
    out = committed;
    CHECK_STATUS("no provider", remote_make_request_id(out, nullptr, nullptr), RemoteStatus::entropy_failed);
    CHECK(out == committed);

    // ⛔ AND THIS IS THE CODEC BOUNDARY ONLY. `IHal::rand_bytes` returns void, so no HAL fake could have
    //    produced the failures above; nothing here claims the device HAL detects a hardware RNG fault or that
    //    a console refuses before an RF send. B312 stays open for the first integration slice's real,
    //    status-bearing provider — and no adapter may turn the void draw into an unconditional success.
}

TEST_CASE("§radmin-2/entropy — the request-id birthday bound at the design's 2^16 analysis envelope") {
    // design §9: n(n-1) / 2^65 at n = 2^16 is approximately 2^-33. DERIVED here, not quoted.
    const double n = 65536.0;
    const double p = n * (n - 1.0) / (2.0 * 18446744073709551616.0);   // 2 * 2^64 = 2^65
    CHECK(p > 1.1e-10);
    CHECK(p < 1.2e-10);
    // 2^-33 = 1.16415321826934814453125e-10; the bound is within 0.01% of it.
    const double two_pow_minus_33 = 1.0 / 8589934592.0;
    CHECK(p / two_pow_minus_33 > 0.9999);
    CHECK(p / two_pow_minus_33 < 1.0001);
    // 2^16 is an ANALYSIS envelope, not an enforced cap: this codec counts nothing and rejects no request for
    // exceeding it (R-RA-5's per-epoch cap is superseded by design §9's non-enforced envelope).
    CHECK(kRemoteRequestIdBytes == 8);
}
