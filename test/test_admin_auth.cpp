// MeshRoute — test_admin_auth.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN — test_airtime.cpp provides main().
#include "doctest.h"
#include "admin_auth.h"
#include <cstring>
using namespace meshroute;

TEST_CASE("admin KDF — deterministic, password-sensitive") {
    Identity a{}, a2{}, b{};
    admin_key_from_password("correct horse battery staple", 28, a);
    admin_key_from_password("correct horse battery staple", 28, a2);
    admin_key_from_password("Correct horse battery staple", 28, b);   // 1 char differs
    CHECK(std::memcmp(a.ed_pub, a2.ed_pub, 32) == 0);   // deterministic
    CHECK(std::memcmp(a.ed_pub, b.ed_pub, 32) != 0);    // different password -> different key
    // key_hash32 = LE(ed_pub[0..3])
    CHECK(a.key_hash32 == (uint32_t(a.ed_pub[0]) | (uint32_t(a.ed_pub[1])<<8) | (uint32_t(a.ed_pub[2])<<16) | (uint32_t(a.ed_pub[3])<<24)));
}

TEST_CASE("admin counter — strictly-above-floor") {
    CHECK(admin_counter_ok(5, 6));
    CHECK_FALSE(admin_counter_ok(5, 5));
    CHECK_FALSE(admin_counter_ok(5, 4));
}

TEST_CASE("admin cmd — seal/open round-trip; wrong admin key fails") {
    Identity admin{}, node{}, attacker{};
    admin_key_from_password("adminpw", 7, admin);
    uint8_t nseed[32]; for (int i=0;i<32;++i) nseed[i]=uint8_t(i+1);   identity_from_seed(node, nseed);
    admin_key_from_password("attacker", 8, attacker);
    const uint8_t cmd[] = "reboot";
    uint8_t frame[128]; uint8_t rand8[8] = {1,2,3,4,5,6,7,8};
    size_t fl = admin_cmd_seal(frame, sizeof frame, admin, node.ed_pub, node.key_hash32, 5, cmd, 6, rand8, 1);
    CHECK(fl > 0);
    AdminCmd out{}; uint8_t pt[64];
    CHECK(admin_cmd_open(frame, fl, admin.ed_pub, node, out, pt, sizeof pt));   // genuine admin -> opens
    CHECK(out.node_key_hash == node.key_hash32);
    CHECK(out.counter == 5u);
    CHECK(out.cmd_len == 6); CHECK(std::memcmp(out.cmd, "reboot", 6) == 0);
    AdminCmd bad{};
    CHECK_FALSE(admin_cmd_open(frame, fl, attacker.ed_pub, node, bad, pt, sizeof pt));  // wrong pinned key -> tag fails
    frame[fl-1] ^= 0x01;   // flip a tag bit
    AdminCmd tampered{};
    CHECK_FALSE(admin_cmd_open(frame, fl, admin.ed_pub, node, tampered, pt, sizeof pt));
}

TEST_CASE("admin verify — verdicts") {
    Identity admin{}, node{}; admin_key_from_password("pw", 2, admin);
    uint8_t ns[32]; for (int i=0;i<32;++i) ns[i]=uint8_t(0x40+i); identity_from_seed(node, ns);
    uint8_t frame[128], rand8[8]={9,8,7,6,5,4,3,2}, pt[64];
    const uint8_t cmd[]="reboot";
    size_t fl = admin_cmd_seal(frame, sizeof frame, admin, node.ed_pub, node.key_hash32, 10, cmd, 6, rand8, 1);
    AdminCmd o{};
    CHECK(admin_cmd_verify(frame, fl, admin.ed_pub, node, 9, o, pt, sizeof pt) == AdminVerdict::ok);
    CHECK(admin_cmd_verify(frame, fl, admin.ed_pub, node, 10, o, pt, sizeof pt) == AdminVerdict::replay);   // counter==floor
    // sealed under a bogus node hash -> the node's dm_kdf/dm_nonce (on its real hash) differ -> open FAILS -> bad_tag
    uint8_t frame2[128]; size_t fl2 = admin_cmd_seal(frame2, sizeof frame2, admin, node.ed_pub, node.key_hash32 ^ 0xFF, 11, cmd, 6, rand8, 2);
    CHECK(admin_cmd_verify(frame2, fl2, admin.ed_pub, node, 9, o, pt, sizeof pt) == AdminVerdict::bad_tag);
}
