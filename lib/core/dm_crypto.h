// MeshRoute — lib/core/dm_crypto.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Phase 1 — E2E direct-message crypto core (platform-neutral; monocypher only).
// The KDF + nonce derivation + the AEAD seal/open wrapper. ECDH itself lives in
// identity.cpp (ecdh_shared / ed_pub_to_x25519); this module turns a raw shared
// point into a per-DM key + nonce and seals/opens the DM payload.
// See docs/superpowers/specs/2026-06-15-phase1-e2e-dm-crypto.md (E1/E4).
//
// SECURITY MODEL (design-spec §0): X25519 static-static (NO forward secrecy), TOFU
// pubkey resolution (NOT MITM-secure), no replay protection — confidentiality of the
// DM *payload* against PASSIVE eavesdroppers only. Honest-node threat model.
#pragma once
#ifndef MESHROUTE_NS
#define MESHROUTE_NS meshroute   // Slice 5 faithful two-lib: gateway variant compiles with -DMESHROUTE_NS=meshroute_gw
#endif
#include <cstdint>
#include <cstddef>

namespace MESHROUTE_NS {

// Tag + nonce-seed sizes (the seed rides the repurposed DATA MAC trailer, 4->8 B under CRYPTED).
inline constexpr size_t DM_TAG_LEN        = 16;   // Poly1305 tag
inline constexpr size_t DM_NONCE_LEN      = 24;   // XChaCha20 nonce
inline constexpr size_t DM_NONCE_SEED_LEN = 8;    // cleartext random bytes carried on the wire

// E4 — the per-pair DM key. Endpoint-bound + domain-separated so BOTH directions derive ONE key:
//   key[32] = BLAKE2b-512("MR-E2E-v1" | shared32 | min(a,b) LE4 | max(a,b) LE4)[:32]
// a/b = the two endpoints' key_hash32 (sorted numerically, each serialized little-endian).
// NB: BLAKE2b-512-then-truncate (the spec's "[:32]" notation; matches identity.cpp's crypto_blake2b(.,64,.)),
// NOT the parameterized blake2b-256 — the two differ.
void dm_kdf(uint8_t key[32], const uint8_t shared[32], uint32_t my_hash, uint32_t peer_hash);

// E1 — the per-DM nonce, derived from CLEARTEXT inputs only (source_hash is sealed, so it can't be a
// nonce input; the per-pair ECDH key already binds the sender):
//   nonce[24] = BLAKE2b-512(rand8 | ctr LE2 | dst_key_hash32 LE4)[:24]
// rand8 = 8 fresh random bytes carried in the trailer (64-bit collision margin defeats the 16-bit ctr wrap).
void dm_nonce(uint8_t nonce[DM_NONCE_LEN], const uint8_t rand8[DM_NONCE_SEED_LEN],
              uint16_t ctr, uint32_t dst_key_hash32);

// Seal `pt_len` plaintext bytes -> `ct` (pt_len bytes) + `tag` (16 B), under key/nonce, authenticating
// `aad` (the cleartext routing header). Thin wrapper over crypto_aead_lock. ct may alias pt (in-place).
void dm_seal(uint8_t* ct, uint8_t tag[DM_TAG_LEN], const uint8_t key[32], const uint8_t nonce[DM_NONCE_LEN],
             const uint8_t* aad, size_t aad_len, const uint8_t* pt, size_t pt_len);

// Open: returns true + writes `pt` (ct_len bytes) ONLY on a valid tag; returns false (caller MUST DROP)
// on any tag/AAD/key/nonce mismatch — and on failure `pt` is NOT left holding forged plaintext.
[[nodiscard]] bool dm_open(uint8_t* pt, const uint8_t key[32], const uint8_t nonce[DM_NONCE_LEN],
                           const uint8_t* aad, size_t aad_len,
                           const uint8_t* ct, size_t ct_len, const uint8_t tag[DM_TAG_LEN]);

// =============================================================================
// ★★★ §hybrid-rts S1 (2026-08-08) — THE ONE UNICAST-RTS FLIGHT IDENTITY PRODUCER.
// Design: docs/superpowers/specs/2026-08-08-hybrid-rts-flight-identity-design.md §2/§3.
// =============================================================================
// WHY IT LIVES HERE AND NOT IN frame_codec.cpp: the encrypted half is a BLAKE2b digest, and
// **the codec must not own crypto** (design §3). This TU already owns the project's ONE
// hash-then-truncate convention (`dm_kdf`, `dm_nonce` above), so the encrypted identity is
// colocated with the canonical nonce derivation whose seed it consumes — one convention, one
// TU, no second `crypto_blake2b` call site to drift. The plaintext half rides along because
// "one producer per identity, one comparator" (§3) is worth more than a purity split that
// would put two halves of one wire tail in two files. `frame_codec.h` includes this header
// for the POD only; `frame_codec.cpp` calls NO function from it — it copies `bytes[0..width)`.
//
// ★ THE PROBLEM IT SOLVES, measured (design §1): the old 7-B unicast RTS identified a flight
// only by `(immediate src, dst, ctr_lo[4], payload_len)`. Across the 36-stream corpus that
// aliased 4 of 5 gateway/different-origin pairs (80 %) and 6.1 % overall, because peer send
// counters are CORRELATED, not independent. Two different messages produced BYTE-IDENTICAL
// RTS frames, so any TERMINAL decision built on one could credit the wrong flight.
//
// ⛔ DO NOT "simplify" the encrypted half to an XOR/fold of the seed: that restores exactly
//    the correlated-alias failure under a new name (design §2.2).
// ⛔ DO NOT switch to a parameterized 4-byte BLAKE2b output. `crypto_blake2b(out, 4, ...)` is a
//    DIFFERENT digest from `crypto_blake2b(full, 64, ...)` then prefix — BLAKE2b's output length
//    is a parameter of its IV. The project's convention is 64-then-truncate (see dm_kdf/dm_nonce
//    above and identity.cpp's x_secret derivation); the KAT vector in test_frame_codec.cpp pins it.
// ⛔ DO NOT serialize the identity through a host-endian integer. The bytes go on the wire in
//    DIGEST ORDER; that is why this is a byte array and not the `uint32_t value` the spec sketched.
inline constexpr size_t  RTS_ID_PLAIN_LEN      = 3;      // origin | ctr_hi | ctr_lo (exact, collision-free)
inline constexpr size_t  RTS_ID_CRYPTED_LEN    = 4;      // BLAKE2b-512(...)[:4]    (~1/2^32 conditional)
inline constexpr size_t  RTS_ID_MAX_LEN        = 4;
inline constexpr uint8_t RTS_ID_DOMAIN_CRYPTED = 0xE1;   // domain-separation prefix (design §2.2)

// The domain is a SEPARATE field from the width even though today they are 1:1, so that a
// plaintext and an encrypted identity can never compare equal by numeric accident, and so a
// future third domain cannot be introduced by silently reusing a width.
enum class RtsIdDomain : uint8_t { none = 0, plaintext = 1, crypted = 2 };

struct RtsFlightIdentity {
    uint8_t     bytes[RTS_ID_MAX_LEN] = {};   // WIRE ORDER (== digest order for `crypted`)
    uint8_t     width  = 0;                   // 0 (absent: M/flood) · 3 (plaintext) · 4 (crypted)
    RtsIdDomain domain = RtsIdDomain::none;
};
// A unicast RTS/terminal CTS REQUIRES a valid identity; an M/flood RTS and an ordinary CTS
// REQUIRE an absent one. Both codecs enforce that pairing and fail loud (return 0 / nullopt).
constexpr bool rts_flight_identity_valid(const RtsFlightIdentity& id) {
    return (id.domain == RtsIdDomain::plaintext && id.width == RTS_ID_PLAIN_LEN)
        || (id.domain == RtsIdDomain::crypted   && id.width == RTS_ID_CRYPTED_LEN);
}
constexpr bool rts_flight_identity_absent(const RtsFlightIdentity& id) {
    return id.domain == RtsIdDomain::none && id.width == 0;
}
// THE ONE COMPARATOR (design §3). FULL width + domain — never a prefix, never a truncated tag:
// a shorter comparison would reintroduce the probabilistic silent-loss class this design paid
// three RTS bytes to remove (design §2.3).
constexpr bool rts_flight_identity_equal(const RtsFlightIdentity& a, const RtsFlightIdentity& b) {
    if (a.domain != b.domain || a.width != b.width) return false;
    if (a.domain == RtsIdDomain::none) return false;          // "absent == absent" is NOT a match
    for (uint8_t i = 0; i < a.width; ++i) if (a.bytes[i] != b.bytes[i]) return false;
    return true;
}

// PLAINTEXT: the exact canonical DATA identity a relay already has. Plaintext DATA carries
// `origin` in the clear, so the RTS reveals nothing the following DATA does not.
RtsFlightIdentity rts_flight_identity_plain(uint8_t origin, uint16_t ctr);

// ENCRYPTED: first 4 bytes of BLAKE2b-512(0xE1 | nonce_seed[8] | ctr_hi | ctr_lo | dst).
// ★ It must NOT expose `origin` — a CRYPTED DATA seals the origin, so leaking it on the RTS
//   would undo sealed-sender at RTS time. The nonce seed is ALREADY carried clear by the DATA
//   trailer (`data_nonce_seed`), so both endpoints can recompute this from the frame itself.
// ★ `ctr` + `dst` are bound so the tail cannot be replayed outside its exact flight context.
// ⓘ `ctr` is serialized BIG-endian here (hi then lo) — deliberately matching the plaintext
//   tail's wire order, NOT dm_nonce's little-endian `ctr`. Two different derivations, two
//   different byte orders, both pinned by known-answer tests.
RtsFlightIdentity rts_flight_identity_crypted(const uint8_t nonce_seed[DM_NONCE_SEED_LEN],
                                              uint16_t ctr, uint8_t dst);

// THE SINGLE ENTRY POINT the producer calls, so no call site chooses the domain by hand.
// `crypted` is `(flags & DATA_FLAG_CRYPTED) != 0` at the one unicast RTS producer.
RtsFlightIdentity rts_flight_identity(bool crypted, uint8_t origin, uint16_t ctr, uint8_t dst,
                                      const uint8_t nonce_seed[DM_NONCE_SEED_LEN]);

}  // namespace MESHROUTE_NS
