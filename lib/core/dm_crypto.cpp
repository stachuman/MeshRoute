// MeshRoute — lib/core/dm_crypto.cpp  (Phase 1 E2E DM crypto core)
//
// Platform-neutral; monocypher only. KDF + nonce are BLAKE2b-512-then-truncate (the spec's "[:N]"
// notation; matches identity.cpp's crypto_blake2b(.,64,.)). Seal/open are thin wrappers over
// crypto_aead_lock/unlock (XChaCha20-Poly1305). KAT-pinned in test_dm_crypto.cpp against an
// independent BLAKE2b reference (a round-trip alone can't catch a mutually-wrong derivation).
#include "dm_crypto.h"
#include "monocypher.h"

#include <cstring>

namespace MESHROUTE_NS {

namespace {
inline void put_u16_le(uint8_t* p, uint16_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); }
inline void put_u32_le(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); p[2] = uint8_t(v >> 16); p[3] = uint8_t(v >> 24);
}
constexpr char     kDomain[]   = "MR-E2E-v1";   // domain separation (E4) — 9 bytes, NO trailing NUL on the wire
constexpr size_t   kDomainLen  = sizeof(kDomain) - 1;
}  // namespace

void dm_kdf(uint8_t key[32], const uint8_t shared[32], uint32_t my_hash, uint32_t peer_hash) {
    const uint32_t lo = my_hash < peer_hash ? my_hash : peer_hash;   // endpoint-bound: sort so both directions
    const uint32_t hi = my_hash < peer_hash ? peer_hash : my_hash;   // derive ONE key regardless of who sends
    uint8_t msg[kDomainLen + 32 + 4 + 4];
    std::memcpy(msg, kDomain, kDomainLen);
    std::memcpy(msg + kDomainLen, shared, 32);
    put_u32_le(msg + kDomainLen + 32,     lo);
    put_u32_le(msg + kDomainLen + 32 + 4, hi);
    uint8_t full[64];
    crypto_blake2b(full, 64, msg, sizeof msg);          // BLAKE2b-512 ...
    std::memcpy(key, full, 32);                         // ... then truncate to 32
    crypto_wipe(full, sizeof full);
    crypto_wipe(msg, sizeof msg);                       // msg held the raw shared point — wipe it
}

void dm_nonce(uint8_t nonce[DM_NONCE_LEN], const uint8_t rand8[DM_NONCE_SEED_LEN],
              uint16_t ctr, uint32_t dst_key_hash32) {
    uint8_t msg[DM_NONCE_SEED_LEN + 2 + 4];             // rand8 | ctr LE | dst_key_hash32 LE — CLEARTEXT inputs only
    std::memcpy(msg, rand8, DM_NONCE_SEED_LEN);
    put_u16_le(msg + DM_NONCE_SEED_LEN,     ctr);
    put_u32_le(msg + DM_NONCE_SEED_LEN + 2, dst_key_hash32);
    uint8_t full[64];
    crypto_blake2b(full, 64, msg, sizeof msg);
    std::memcpy(nonce, full, DM_NONCE_LEN);
    crypto_wipe(full, sizeof full);
}

void dm_seal(uint8_t* ct, uint8_t tag[DM_TAG_LEN], const uint8_t key[32], const uint8_t nonce[DM_NONCE_LEN],
             const uint8_t* aad, size_t aad_len, const uint8_t* pt, size_t pt_len) {
    crypto_aead_lock(ct, tag, key, nonce, aad, aad_len, pt, pt_len);
}

bool dm_open(uint8_t* pt, const uint8_t key[32], const uint8_t nonce[DM_NONCE_LEN],
             const uint8_t* aad, size_t aad_len, const uint8_t* ct, size_t ct_len, const uint8_t tag[DM_TAG_LEN]) {
    // crypto_aead_unlock returns 0 on a valid tag, -1 otherwise — and on failure it WIPES `pt` (no forged
    // plaintext is ever exposed). The caller MUST treat false as a hard drop.
    return crypto_aead_unlock(pt, tag, key, nonce, aad, aad_len, ct, ct_len) == 0;
}

}  // namespace MESHROUTE_NS
