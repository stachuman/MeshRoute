// MeshRoute — test_identity.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Slice A gate for lib/core/identity: deterministic keygen, key_hash32 = LE(ed_pub[:4]),
// the seed-not-wiped contract, and the crypto that MUST be right before any device wiring:
//  * the X25519 secret is the canonical scalar — proven by x_pub == eddsa_to_x25519(ed_pub),
//    a cross-check between two independent monocypher paths (NOT a self-consistent A·B==B·A);
//  * crypto_x25519 pinned to the RFC 7748 §6.1 known-answer vector (an external reference).
//
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN (test_airtime.cpp provides main()); -fno-exceptions => CHECK.
#include "doctest.h"

#include "identity.h"
#include "monocypher.h"

#include <cstdint>
#include <cstring>

using namespace meshroute;

namespace {

// Parse a 64-char hex string into 32 bytes (test helper; no validation beyond length use).
void hex32(const char* h, uint8_t out[32]) {
    auto nib = [](char c) -> uint8_t {
        if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
        return static_cast<uint8_t>(c - 'A' + 10);
    };
    for (int i = 0; i < 32; ++i)
        out[i] = static_cast<uint8_t>((nib(h[2 * i]) << 4) | nib(h[2 * i + 1]));
}

bool all_zero(const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; ++i) if (p[i]) return false;
    return true;
}

}  // namespace

TEST_CASE("identity — deterministic keygen, key_hash32 = LE(ed_pub[:4]), seed preserved") {
    uint8_t seed[32];
    for (int i = 0; i < 32; ++i) seed[i] = static_cast<uint8_t>(i + 1);

    Identity a{}; identity_from_seed(a, seed);
    Identity b{}; identity_from_seed(b, seed);

    CHECK(std::memcmp(a.ed_pub, b.ed_pub, 32) == 0);   // same seed -> same identity
    CHECK(a.key_hash32 == b.key_hash32);
    CHECK(!all_zero(a.ed_pub, 32));

    const uint32_t expect = static_cast<uint32_t>(a.ed_pub[0])
                          | (static_cast<uint32_t>(a.ed_pub[1]) << 8)
                          | (static_cast<uint32_t>(a.ed_pub[2]) << 16)
                          | (static_cast<uint32_t>(a.ed_pub[3]) << 24);
    CHECK(a.key_hash32 == expect);                     // handle = LE first 4 bytes of ed_pub

    // the caller's seed is NOT wiped (we pass monocypher a scratch copy), and out.seed is preserved.
    bool seed_intact = true;
    for (int i = 0; i < 32; ++i) if (seed[i] != static_cast<uint8_t>(i + 1)) seed_intact = false;
    CHECK(seed_intact);
    CHECK(std::memcmp(a.seed, seed, 32) == 0);
}

TEST_CASE("identity — x_pub == eddsa_to_x25519(ed_pub): the X25519 secret IS the canonical scalar") {
    uint8_t seed[32];
    for (int i = 0; i < 32; ++i) seed[i] = static_cast<uint8_t>(0xA0 ^ i);
    Identity id{}; identity_from_seed(id, seed);

    // Independent monocypher path: convert the ED public key to its Montgomery u-coordinate.
    uint8_t x_from_ed[32];
    crypto_eddsa_to_x25519(x_from_ed, id.ed_pub);

    // If our x_secret were the wrong scalar (e.g. the raw seed, un-hashed/un-clamped), these diverge.
    CHECK(std::memcmp(id.x_pub, x_from_ed, 32) == 0);
    CHECK(!all_zero(id.x_pub, 32));
}

TEST_CASE("identity — ECDH agrees both ways via ed_pub conversion (A·B == B·A)") {
    uint8_t sa[32], sb[32];
    for (int i = 0; i < 32; ++i) { sa[i] = static_cast<uint8_t>(i + 1); sb[i] = static_cast<uint8_t>(0xF0 ^ i); }
    Identity A{}, B{}; identity_from_seed(A, sa); identity_from_seed(B, sb);

    // Each side holds only the OTHER's advertised ed_pub (the realistic case).
    uint8_t b_xpub[32]; ed_pub_to_x25519(b_xpub, B.ed_pub);
    uint8_t a_xpub[32]; ed_pub_to_x25519(a_xpub, A.ed_pub);

    uint8_t shared_a[32]; ecdh_shared(shared_a, A, b_xpub);
    uint8_t shared_b[32]; ecdh_shared(shared_b, B, a_xpub);

    CHECK(std::memcmp(shared_a, shared_b, 32) == 0);
    CHECK(!all_zero(shared_a, 32));
}

TEST_CASE("identity — crypto_x25519 matches the RFC 7748 §6.1 known-answer vector") {
    uint8_t a_sk[32], b_pub[32], expect[32], shared[32];
    hex32("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", a_sk);
    hex32("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f", b_pub);
    hex32("4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742", expect);

    crypto_x25519(shared, a_sk, b_pub);                // monocypher clamps a_sk internally
    CHECK(std::memcmp(shared, expect, 32) == 0);
}

TEST_CASE("identity — GOLDEN vector: seed={1..32} pins ed_pub/key_hash32/x_pub (NV/wire drift guard)") {
    // Recorded from the KAT-verified derivation (monocypher 4.0.2). `key_hash32` is persisted to
    // /mrid and broadcast as the node's identity, and `ed_pub` is the trust anchor — so any
    // derivation change (a monocypher bump, a refactor) MUST break this test rather than silently
    // re-identify every node. Regenerate ONLY with a deliberate, reviewed identity-format change.
    uint8_t seed[32];
    for (int i = 0; i < 32; ++i) seed[i] = static_cast<uint8_t>(i + 1);
    Identity id{}; identity_from_seed(id, seed);

    uint8_t want_ed[32], want_x[32];
    hex32("d4f8e6f267271177c11d17d39810d747166572a1b6db8e352363d9786eb07983", want_ed);
    hex32("4a11aa2bdf401398cc805b7608b0e6c83c5d45c2a2e133df08ad06a45b59c928", want_x);
    CHECK(std::memcmp(id.ed_pub, want_ed, 32) == 0);
    CHECK(std::memcmp(id.x_pub,  want_x,  32) == 0);
    CHECK(id.key_hash32 == 0xf2e6f8d4u);              // == LE(ed_pub[0..3] = d4 f8 e6 f2)
}

TEST_CASE("identity — distinct seeds give distinct identities") {
    uint8_t s1[32] = {0}, s2[32] = {0};
    s1[0] = 1; s2[0] = 2;
    Identity a{}, b{}; identity_from_seed(a, s1); identity_from_seed(b, s2);
    CHECK(std::memcmp(a.ed_pub, b.ed_pub, 32) != 0);
    CHECK(a.key_hash32 != b.key_hash32);
}
