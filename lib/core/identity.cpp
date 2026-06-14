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

}  // namespace meshroute
