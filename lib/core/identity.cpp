// MeshRoute — lib/core/identity.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Identity derivation over monocypher 4.0.2. The one subtle step is the X25519
// secret: it must be the SAME curve25519 scalar that EdDSA signing derives from
// the seed, so that crypto_x25519(our x_secret, peer_x) agrees with the peer's
// crypto_eddsa_to_x25519(our ed_pub). monocypher's secret_key[0..31] is the raw
// seed, and crypto_eddsa_sign forms the scalar as trim(BLAKE2b-512(seed)) — we
// reproduce exactly that (proven by the x_pub == eddsa_to_x25519(ed_pub) doctest).
#include "identity.h"
#include "monocypher.h"

#include <cstring>

namespace MESHROUTE_NS {

uint32_t key_hash32_of(const uint8_t ed_pub[32]) {
    return  static_cast<uint32_t>(ed_pub[0])
         | (static_cast<uint32_t>(ed_pub[1]) << 8)
         | (static_cast<uint32_t>(ed_pub[2]) << 16)
         | (static_cast<uint32_t>(ed_pub[3]) << 24);
}

void identity_from_seed(Identity& out, const uint8_t seed[32]) {
    std::memcpy(out.seed, seed, 32);
    // crypto_eddsa_key_pair WIPES its seed argument — pass a scratch copy so out.seed (and the
    // caller's seed) survive. After this: ed_secret = seed || ed_pub; ed_pub = scalarbase(scalar).
    uint8_t scratch[32];
    std::memcpy(scratch, seed, 32);
    crypto_eddsa_key_pair(out.ed_secret, out.ed_pub, scratch);   // wipes scratch

    // X25519 secret = the curve25519 scalar EdDSA itself uses: trim(BLAKE2b-512(seed)).
    // (mirrors crypto_eddsa_sign: blake2b(a,64,secret_key[:32]=seed,32); trim_scalar(a,a).)
    uint8_t h[64];
    crypto_blake2b(h, 64, out.seed, 32);
    crypto_eddsa_trim_scalar(out.x_secret, h);                   // clamp -> the scalar (reads h[0..31])
    crypto_x25519_public_key(out.x_pub, out.x_secret);
    crypto_wipe(h, sizeof h);

    out.key_hash32 = key_hash32_of(out.ed_pub);
}

void ed_pub_to_x25519(uint8_t x_pub_out[32], const uint8_t peer_ed_pub[32]) {
    crypto_eddsa_to_x25519(x_pub_out, peer_ed_pub);
}

void ecdh_shared(uint8_t shared_out[32], const Identity& self, const uint8_t peer_x_pub[32]) {
    crypto_x25519(shared_out, self.x_secret, peer_x_pub);        // monocypher clamps internally
}

// Constant-time-ish all-zero test (OR-accumulate, no early return — the e2e_seal_inner L10/R7 idiom).
static bool all_zero32(const uint8_t p[32]) {
    uint8_t acc = 0;
    for (int i = 0; i < 32; ++i) acc |= p[i];
    return acc == 0;
}

// §team-ch-key (T-K1). See identity.h for the clamping contract this function exists to pin down.
// Writes pub/priv ONLY on success, so a refused mint cannot leave half a keypair behind (C2).
bool team_channel_key_derive(uint8_t pub[32], uint8_t priv[32], const uint8_t scalar_in[32]) {
    if (all_zero32(scalar_in)) return false;              // dead RNG / all-zero supplied key -> refuse loud
    uint8_t sk[32];
    crypto_eddsa_trim_scalar(sk, scalar_in);              // CANONICALISE AT STORE (identity.h): out[0]&=248, out[31]&=127|=64
    uint8_t pk[32];
    crypto_x25519_public_key(pk, sk);                     // == crypto_x25519(pk, sk, base_point{9}); clamps sk again (idempotent)
    if (all_zero32(pk)) { crypto_wipe(sk, sizeof sk); return false; }   // degenerate point -> refuse (cannot happen for a trimmed scalar; defence in depth)
    for (int i = 0; i < 32; ++i) { priv[i] = sk[i]; pub[i] = pk[i]; }
    crypto_wipe(sk, sizeof sk);
    return true;
}

}  // namespace meshroute
