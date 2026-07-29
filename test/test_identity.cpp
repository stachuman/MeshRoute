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

// ---- §team-ch-key (T-K1): the TEAM CHANNEL keypair -------------------------------------------------------
// The corpus CANNOT reach this code (the simulator's `team` verb refuses `team new` outright — see
// NodeRuntimeWrapper.cpp's §sim-team-verb note — and src/ is not compiled into the sim at all), so these
// tests plus the Node-level ones in test_node_channel.cpp are the ENTIRE detector for this mechanism.

TEST_CASE("team channel key — KAT: the stored scalar is CANONICAL and pub matches RFC 7748 §6.1") {
    // ★ The one thing that had to be got right (see identity.h): monocypher clamps at USE, not at STORE, so an
    // unclamped scalar round-trips fine INSIDE monocypher while being non-interoperable outside it. This KAT
    // pins BOTH halves against an EXTERNAL reference, which a round-trip test could not: it feeds RFC 7748's
    // Alice private key (which is deliberately NOT in clamped form: 0x77 has low bits set, 0x2a lacks bit 254)
    // and requires (a) the stored priv to be the RFC's scalar CLAMPED, byte for byte, and (b) the derived pub
    // to equal the RFC's published X25519(a, 9) — i.e. clamping at store did not change the key it names.
    uint8_t rfc_a[32], want_pub[32];
    hex32("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", rfc_a);
    hex32("8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a", want_pub);

    uint8_t pub[32], priv[32];
    CHECK(team_channel_key_derive(pub, priv, rfc_a));

    uint8_t want_priv[32];
    std::memcpy(want_priv, rfc_a, 32);
    want_priv[0]  = static_cast<uint8_t>(want_priv[0] & 248);      // the RFC-7748 decodeScalar25519 clamp,
    want_priv[31] = static_cast<uint8_t>(want_priv[31] & 127);     // spelled out here rather than borrowed from
    want_priv[31] = static_cast<uint8_t>(want_priv[31] | 64);      // monocypher (else the test proves nothing)
    CHECK(std::memcmp(priv, want_priv, 32) == 0);                  // STORED CANONICAL — not the raw input
    CHECK(std::memcmp(priv, rfc_a, 32) != 0);                      // ...and the input genuinely needed clamping
    CHECK(std::memcmp(pub,  want_pub,  32) == 0);                  // pub == the RFC's own X25519(a, 9)
    CHECK((priv[0] & 7) == 0);
    CHECK((priv[31] & 0x80) == 0);
    CHECK((priv[31] & 0x40) != 0);
}

TEST_CASE("team channel key — KAT: Bob's half + the RFC 7748 shared secret through the STORED scalars") {
    // Derive BOTH RFC parties through our own function and reproduce the RFC's shared secret from the two
    // canonicalised scalars. This is what proves the stored form is interoperable rather than merely
    // self-consistent: if clamping at store had altered either key, K would not match.
    uint8_t rfc_a[32], rfc_b[32], want_k[32];
    hex32("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", rfc_a);
    hex32("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb", rfc_b);
    hex32("4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742", want_k);

    uint8_t a_pub[32], a_priv[32], b_pub[32], b_priv[32], want_b_pub[32];
    CHECK(team_channel_key_derive(a_pub, a_priv, rfc_a));
    CHECK(team_channel_key_derive(b_pub, b_priv, rfc_b));
    hex32("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f", want_b_pub);
    CHECK(std::memcmp(b_pub, want_b_pub, 32) == 0);

    uint8_t k_ab[32], k_ba[32];
    crypto_x25519(k_ab, a_priv, b_pub);
    crypto_x25519(k_ba, b_priv, a_pub);
    CHECK(std::memcmp(k_ab, want_k, 32) == 0);
    CHECK(std::memcmp(k_ba, want_k, 32) == 0);
}

TEST_CASE("team channel key — clamping is IDEMPOTENT: adopting a canonical scalar reproduces the pair exactly") {
    // The adopt path (`team new tkpriv=…`, the T-K3 grant, the T-K4 QR) re-runs the SAME derivation on a scalar
    // that is already canonical. If clamping were not idempotent, a key would mutate every time it was passed
    // on and only the first holder could read the team's posts.
    uint8_t seed[32];
    for (int i = 0; i < 32; ++i) seed[i] = static_cast<uint8_t>(0x5A ^ (i * 7));
    uint8_t p1[32], s1[32], p2[32], s2[32];
    CHECK(team_channel_key_derive(p1, s1, seed));
    CHECK(team_channel_key_derive(p2, s2, s1));     // feed the STORED scalar back in
    CHECK(std::memcmp(s1, s2, 32) == 0);
    CHECK(std::memcmp(p1, p2, 32) == 0);
}

TEST_CASE("team channel key — distinct from the node identity and from any team_id input") {
    // Spec §2.1: "Distinct from every existing key — NOT the node identity, NOT a team_id derivation input."
    // Feeding the identity MASTER SEED in must NOT reproduce the identity's own X25519 pair (which is
    // trim(BLAKE2b-512(seed)), a different scalar) — i.e. nobody can recompute the team key from /mrid.
    uint8_t seed[32];
    for (int i = 0; i < 32; ++i) seed[i] = static_cast<uint8_t>(i + 1);
    Identity id{}; identity_from_seed(id, seed);

    uint8_t pub[32], priv[32];
    CHECK(team_channel_key_derive(pub, priv, seed));
    CHECK(std::memcmp(priv, id.x_secret, 32) != 0);
    CHECK(std::memcmp(pub,  id.x_pub,    32) != 0);
    CHECK(std::memcmp(pub,  id.ed_pub,   32) != 0);
}

TEST_CASE("team channel key — C2: all-zero entropy is REFUSED and writes NOTHING") {
    // e2e_seal_inner's R7 guard, restated for keygen: a dead crypto RNG must not yield a "key". And the refusal
    // must leave the caller's buffers untouched, so a refused mint cannot leave half a keypair behind.
    uint8_t zero[32] = {};
    uint8_t pub[32], priv[32];
    for (int i = 0; i < 32; ++i) { pub[i] = 0xAA; priv[i] = 0xBB; }
    CHECK(team_channel_key_derive(pub, priv, zero) == false);
    for (int i = 0; i < 32; ++i) { CHECK(pub[i] == 0xAA); CHECK(priv[i] == 0xBB); }
}

TEST_CASE("team channel key — a single set bit anywhere is enough entropy to derive (not a length check)") {
    for (int bit = 0; bit < 256; bit += 37) {          // sample, not exhaustive — 8 spread-out positions
        uint8_t s[32] = {};
        s[bit / 8] = static_cast<uint8_t>(1u << (bit % 8));
        uint8_t pub[32], priv[32];
        CHECK(team_channel_key_derive(pub, priv, s));  // clamping forces bit 254 on, so the scalar is never 0
        CHECK(!all_zero(pub, 32));
    }
}

TEST_CASE("identity — distinct seeds give distinct identities") {
    uint8_t s1[32] = {0}, s2[32] = {0};
    s1[0] = 1; s2[0] = 2;
    Identity a{}, b{}; identity_from_seed(a, s1); identity_from_seed(b, s2);
    CHECK(std::memcmp(a.ed_pub, b.ed_pub, 32) != 0);
    CHECK(a.key_hash32 != b.key_hash32);
}
