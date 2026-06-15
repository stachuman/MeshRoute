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

}  // namespace MESHROUTE_NS
