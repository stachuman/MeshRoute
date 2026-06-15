// MeshRoute — test_dm_crypto.cpp
// Phase 1 — E2E DM crypto core. The KDF + nonce KATs are pinned against an
// INDEPENDENT reference (Python hashlib.blake2b), NOT a round-trip — a round-trip
// passes even if both sides derive the same WRONG key/nonce. ECDH itself is already
// RFC-7748-KAT'd in test_identity.cpp. AEAD is pinned to the draft-irtf-cfrg-xchacha §A.3.1 EXTERNAL
// vector (+ round-trip / tamper / key+nonce sensitivity); BLAKE2b to RFC 7693 — every primitive is
// externally anchored, so a wrong-but-self-consistent derivation cannot pass (§2 [xcheck]).
//
// No main() here (test_airtime.cpp owns it); -fno-exceptions -> CHECK only.
#include "doctest.h"

#include "dm_crypto.h"
#include "identity.h"
#include "monocypher.h"

#include <array>
#include <cstdint>
#include <cstring>

using namespace meshroute;

namespace {
bool eq(const uint8_t* a, const uint8_t* b, size_t n) { return std::memcmp(a, b, n) == 0; }
}

// ---- dm_kdf KAT — external reference: hashlib.blake2b(digest_size=64)[:32] ------------------------
// key = BLAKE2b-512("MR-E2E-v1" | shared(0x00..0x1f) | min LE4 | max LE4)[:32], min=0x11223344 max=0xAABBCCDD
TEST_CASE("dm_kdf — KAT against an independent BLAKE2b-512 reference") {
    uint8_t shared[32]; for (int i = 0; i < 32; ++i) shared[i] = static_cast<uint8_t>(i);
    const uint8_t expected[32] = {
        0x0c, 0x86, 0xed, 0x4e, 0xff, 0xc5, 0x65, 0xdd, 0x75, 0x4a, 0xfa, 0xfa, 0xbe, 0xe8, 0x1b, 0x9c,
        0x55, 0x1f, 0xfe, 0x23, 0x9e, 0x7f, 0x5f, 0xb4, 0x2f, 0xa1, 0xea, 0x10, 0x1c, 0xae, 0x1a, 0xc1 };
    uint8_t key[32] = {};
    dm_kdf(key, shared, 0x11223344u, 0xAABBCCDDu);
    CHECK(eq(key, expected, 32));
    // endpoint-symmetry: swapping (my,peer) yields the SAME key (the min/max sort) — both directions agree.
    uint8_t key_swapped[32] = {};
    dm_kdf(key_swapped, shared, 0xAABBCCDDu, 0x11223344u);
    CHECK(eq(key, key_swapped, 32));
}

// ---- dm_nonce KAT — external reference: hashlib.blake2b(digest_size=64)[:24] ----------------------
// nonce = BLAKE2b-512(rand8(DEADBEEF01020304) | ctr LE2(0x1234) | dst LE4(0xAABBCCDD))[:24]
TEST_CASE("dm_nonce — KAT against an independent BLAKE2b-512 reference (cleartext inputs only)") {
    const uint8_t rand8[8] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04 };
    const uint8_t expected[24] = {
        0xcf, 0x95, 0x0d, 0x27, 0xb6, 0x16, 0xd2, 0x9a, 0x37, 0x95, 0x39, 0x6b,
        0x9a, 0x53, 0x6e, 0xa5, 0x43, 0x6b, 0xf5, 0x50, 0xb5, 0xba, 0xe9, 0xc9 };
    uint8_t nonce[24] = {};
    dm_nonce(nonce, rand8, 0x1234u, 0xAABBCCDDu);
    CHECK(eq(nonce, expected, 24));
    // sensitivity: a different rand8 / ctr / dst each move the nonce (no input ignored).
    uint8_t n2[24] = {}; const uint8_t r2[8] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x05 };
    dm_nonce(n2, r2, 0x1234u, 0xAABBCCDDu);             CHECK_FALSE(eq(nonce, n2, 24));
    dm_nonce(n2, rand8, 0x1235u, 0xAABBCCDDu);          CHECK_FALSE(eq(nonce, n2, 24));
    dm_nonce(n2, rand8, 0x1234u, 0xAABBCCDEu);          CHECK_FALSE(eq(nonce, n2, 24));
}

// ---- AEAD wrapper — round-trip + integrity + sensitivity -----------------------------------------
TEST_CASE("dm_seal/dm_open — round-trip, and the ciphertext is NOT the plaintext") {
    uint8_t key[32]; for (int i = 0; i < 32; ++i) key[i] = static_cast<uint8_t>(0x40 + i);
    uint8_t nonce[24]; for (int i = 0; i < 24; ++i) nonce[i] = static_cast<uint8_t>(0x10 + i);
    const uint8_t aad[6] = { 'h', 'e', 'a', 'd', 'e', 'r' };
    const uint8_t pt[14] = { 's','e','c','r','e','t','-','d','m','-','b','o','d','y' };
    uint8_t ct[14] = {}, tag[16] = {};
    dm_seal(ct, tag, key, nonce, aad, sizeof aad, pt, sizeof pt);
    CHECK_FALSE(eq(ct, pt, sizeof pt));                 // actually encrypted
    uint8_t out[14] = {};
    CHECK(dm_open(out, key, nonce, aad, sizeof aad, ct, sizeof pt, tag));
    CHECK(eq(out, pt, sizeof pt));
}

TEST_CASE("dm_open — fails (and yields NO plaintext) on a tampered ct / tag / aad, wrong key, wrong nonce") {
    uint8_t key[32]; for (int i = 0; i < 32; ++i) key[i] = static_cast<uint8_t>(0x40 + i);
    uint8_t nonce[24]; for (int i = 0; i < 24; ++i) nonce[i] = static_cast<uint8_t>(0x10 + i);
    const uint8_t aad[6] = { 'h', 'e', 'a', 'd', 'e', 'r' };
    const uint8_t pt[14] = { 's','e','c','r','e','t','-','d','m','-','b','o','d','y' };
    uint8_t ct[14] = {}, tag[16] = {};
    dm_seal(ct, tag, key, nonce, aad, sizeof aad, pt, sizeof pt);
    uint8_t out[14];

    { uint8_t c2[14]; std::memcpy(c2, ct, 14); c2[0] ^= 0x01;
      CHECK_FALSE(dm_open(out, key, nonce, aad, sizeof aad, c2, 14, tag)); }            // tampered ciphertext
    { uint8_t t2[16]; std::memcpy(t2, tag, 16); t2[0] ^= 0x01;
      CHECK_FALSE(dm_open(out, key, nonce, aad, sizeof aad, ct, 14, t2)); }             // tampered tag
    { uint8_t a2[6]; std::memcpy(a2, aad, 6); a2[0] ^= 0x01;
      CHECK_FALSE(dm_open(out, key, nonce, a2, 6, ct, 14, tag)); }                      // tampered AAD
    { uint8_t k2[32]; std::memcpy(k2, key, 32); k2[0] ^= 0x01;
      CHECK_FALSE(dm_open(out, k2, nonce, aad, sizeof aad, ct, 14, tag)); }             // wrong key
    { uint8_t n2[24]; std::memcpy(n2, nonce, 24); n2[0] ^= 0x01;
      CHECK_FALSE(dm_open(out, key, n2, aad, sizeof aad, ct, 14, tag)); }               // wrong nonce
}

TEST_CASE("dm_seal — the nonce and the key each actually feed the cipher (no ignored-arg plumbing bug)") {
    uint8_t key[32]; for (int i = 0; i < 32; ++i) key[i] = static_cast<uint8_t>(0x40 + i);
    uint8_t nonce[24]; for (int i = 0; i < 24; ++i) nonce[i] = static_cast<uint8_t>(0x10 + i);
    const uint8_t aad[1] = { 0 };
    const uint8_t pt[16] = {};
    uint8_t ct0[16], ct1[16], t[16];
    dm_seal(ct0, t, key, nonce, aad, 0, pt, sizeof pt);
    { uint8_t n2[24]; std::memcpy(n2, nonce, 24); n2[23] ^= 0x80; dm_seal(ct1, t, key, n2, aad, 0, pt, sizeof pt); }
    CHECK_FALSE(eq(ct0, ct1, sizeof pt));              // a different nonce -> different ciphertext (nonce is wired)
    { uint8_t k2[32]; std::memcpy(k2, key, 32); k2[31] ^= 0x80; dm_seal(ct1, t, k2, nonce, aad, 0, pt, sizeof pt); }
    CHECK_FALSE(eq(ct0, ct1, sizeof pt));              // a different key -> different ciphertext (key is wired)
}

TEST_CASE("dm_seal/dm_open — full chain: two identities ECDH -> dm_kdf -> seal -> the peer opens") {
    uint8_t seedA[32], seedB[32];
    for (int i = 0; i < 32; ++i) { seedA[i] = static_cast<uint8_t>(i + 1); seedB[i] = static_cast<uint8_t>(200 - i); }
    Identity A{}, B{}; identity_from_seed(A, seedA); identity_from_seed(B, seedB);
    uint8_t b_xpub[32], a_xpub[32];
    ed_pub_to_x25519(b_xpub, B.ed_pub); ed_pub_to_x25519(a_xpub, A.ed_pub);
    uint8_t shA[32], shB[32]; ecdh_shared(shA, A, b_xpub); ecdh_shared(shB, B, a_xpub);
    uint8_t kA[32], kB[32];
    dm_kdf(kA, shA, A.key_hash32, B.key_hash32);        // sender A derives the pair key
    dm_kdf(kB, shB, B.key_hash32, A.key_hash32);        // recipient B derives the SAME key
    CHECK(eq(kA, kB, 32));
    const uint8_t rand8[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    uint8_t nA[24]; dm_nonce(nA, rand8, /*ctr*/ 7, /*dst=B*/ B.key_hash32);
    const uint8_t aad[3] = { 0xAB, 0xCD, 0xEF };
    const uint8_t pt[11] = { 'h','e','l','l','o','-','a','l','i','c','e' };
    uint8_t ct[11], tag[16], out[11];
    dm_seal(ct, tag, kA, nA, aad, sizeof aad, pt, sizeof pt);
    uint8_t nB[24]; dm_nonce(nB, rand8, 7, B.key_hash32);   // B reconstructs the nonce from the cleartext seed+ctr+its own hash
    CHECK(dm_open(out, kB, nB, aad, sizeof aad, ct, sizeof pt, tag));
    CHECK(eq(out, pt, sizeof pt));
}

// ---- EXTERNAL AEAD KAT — draft-irtf-cfrg-xchacha-03 §A.3.1 (the canonical XChaCha20-Poly1305 vector;
// libsodium + monocypher both target it). Proves monocypher's crypto_aead_lock IS standard
// XChaCha20-Poly1305 (not a self-consistent variant) AND our wrapper's arg order is correct. -------------
TEST_CASE("dm_seal/dm_open — external KAT: draft-irtf-cfrg-xchacha-03 §A.3.1 XChaCha20-Poly1305 vector") {
    const uint8_t key[32] = {
        0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
        0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f };
    const uint8_t nonce[24] = {
        0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f,
        0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57 };
    const uint8_t aad[12] = { 0x50, 0x51, 0x52, 0x53, 0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7 };
    const uint8_t pt[114] = {
        0x4c, 0x61, 0x64, 0x69, 0x65, 0x73, 0x20, 0x61, 0x6e, 0x64, 0x20, 0x47, 0x65, 0x6e, 0x74, 0x6c,
        0x65, 0x6d, 0x65, 0x6e, 0x20, 0x6f, 0x66, 0x20, 0x74, 0x68, 0x65, 0x20, 0x63, 0x6c, 0x61, 0x73,
        0x73, 0x20, 0x6f, 0x66, 0x20, 0x27, 0x39, 0x39, 0x3a, 0x20, 0x49, 0x66, 0x20, 0x49, 0x20, 0x63,
        0x6f, 0x75, 0x6c, 0x64, 0x20, 0x6f, 0x66, 0x66, 0x65, 0x72, 0x20, 0x79, 0x6f, 0x75, 0x20, 0x6f,
        0x6e, 0x6c, 0x79, 0x20, 0x6f, 0x6e, 0x65, 0x20, 0x74, 0x69, 0x70, 0x20, 0x66, 0x6f, 0x72, 0x20,
        0x74, 0x68, 0x65, 0x20, 0x66, 0x75, 0x74, 0x75, 0x72, 0x65, 0x2c, 0x20, 0x73, 0x75, 0x6e, 0x73,
        0x63, 0x72, 0x65, 0x65, 0x6e, 0x20, 0x77, 0x6f, 0x75, 0x6c, 0x64, 0x20, 0x62, 0x65, 0x20, 0x69,
        0x74, 0x2e };
    const uint8_t expected_ct[114] = {
        0xbd, 0x6d, 0x17, 0x9d, 0x3e, 0x83, 0xd4, 0x3b, 0x95, 0x76, 0x57, 0x94, 0x93, 0xc0, 0xe9, 0x39,
        0x57, 0x2a, 0x17, 0x00, 0x25, 0x2b, 0xfa, 0xcc, 0xbe, 0xd2, 0x90, 0x2c, 0x21, 0x39, 0x6c, 0xbb,
        0x73, 0x1c, 0x7f, 0x1b, 0x0b, 0x4a, 0xa6, 0x44, 0x0b, 0xf3, 0xa8, 0x2f, 0x4e, 0xda, 0x7e, 0x39,
        0xae, 0x64, 0xc6, 0x70, 0x8c, 0x54, 0xc2, 0x16, 0xcb, 0x96, 0xb7, 0x2e, 0x12, 0x13, 0xb4, 0x52,
        0x2f, 0x8c, 0x9b, 0xa4, 0x0d, 0xb5, 0xd9, 0x45, 0xb1, 0x1b, 0x69, 0xb9, 0x82, 0xc1, 0xbb, 0x9e,
        0x3f, 0x3f, 0xac, 0x2b, 0xc3, 0x69, 0x48, 0x8f, 0x76, 0xb2, 0x38, 0x35, 0x65, 0xd3, 0xff, 0xf9,
        0x21, 0xf9, 0x66, 0x4c, 0x97, 0x63, 0x7d, 0xa9, 0x76, 0x88, 0x12, 0xf6, 0x15, 0xc6, 0x8b, 0x13,
        0xb5, 0x2e };
    const uint8_t expected_tag[16] = {
        0xc0, 0x87, 0x59, 0x24, 0xc1, 0xc7, 0x98, 0x79, 0x47, 0xde, 0xaf, 0xd8, 0x78, 0x0a, 0xcf, 0x49 };
    uint8_t ct[114] = {}, tag[16] = {};
    dm_seal(ct, tag, key, nonce, aad, sizeof aad, pt, sizeof pt);
    CHECK(eq(ct, expected_ct, 114));                    // monocypher crypto_aead_lock == the IETF XChaCha vector
    CHECK(eq(tag, expected_tag, 16));
    uint8_t out[114] = {};
    CHECK(dm_open(out, key, nonce, aad, sizeof aad, expected_ct, 114, expected_tag));
    CHECK(eq(out, pt, 114));
}

// ---- BLAKE2b primitive anchor — RFC 7693 Appendix A: BLAKE2b-512("abc"). Proves the BLAKE2b underneath
// dm_kdf/dm_nonce is the standard (not a monocypher variant), so those KATs rest on a verified primitive. ----
TEST_CASE("blake2b — monocypher crypto_blake2b == RFC 7693 App. A BLAKE2b-512(\"abc\")") {
    const uint8_t expected[64] = {
        0xba, 0x80, 0xa5, 0x3f, 0x98, 0x1c, 0x4d, 0x0d, 0x6a, 0x27, 0x97, 0xb6, 0x9f, 0x12, 0xf6, 0xe9,
        0x4c, 0x21, 0x2f, 0x14, 0x68, 0x5a, 0xc4, 0xb7, 0x4b, 0x12, 0xbb, 0x6f, 0xdb, 0xff, 0xa2, 0xd1,
        0x7d, 0x87, 0xc5, 0x39, 0x2a, 0xab, 0x79, 0x2d, 0xc2, 0x52, 0xd5, 0xde, 0x45, 0x33, 0xcc, 0x95,
        0x18, 0xd3, 0x8a, 0xa8, 0xdb, 0xf1, 0x92, 0x5a, 0xb9, 0x23, 0x86, 0xed, 0xd4, 0x00, 0x99, 0x23 };
    uint8_t out[64] = {};
    crypto_blake2b(out, 64, reinterpret_cast<const uint8_t*>("abc"), 3);
    CHECK(eq(out, expected, 64));
}
