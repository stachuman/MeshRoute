// MeshRoute — test_frame_codec.cpp
//
// Pins the §10 cmd-nibble wire codecs (C1: CTS, ACK) + the C0 wire primitives.
// Byte positions are from ROADMAP §10.3; the golden-hex vectors are hand-derived
// here, NOT captured from the Lua — the C++ cmd-nibble wire diverges from the
// Lua tag-byte wire by design. Field *meaning* matches the Lua pack_*/parse_*.
//
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN — test_airtime.cpp provides main().
// The native build is -fno-exceptions, so doctest's REQUIRE is unavailable;
// we use CHECK only (codebase convention) and guard optional derefs with `if`.

#include "doctest.h"

#include "frame_codec.h"
#include "wire.h"

#include <array>
#include <cstdint>

using namespace meshroute;

TEST_CASE("wire — cmd-nibble byte helpers") {
    CHECK(wire::cmd_byte(wire::Cmd::C, 0x5) == 0x25);
    CHECK(wire::cmd_byte(wire::Cmd::K, 0x3) == 0x43);
    CHECK(wire::cmd_of(0x25) == wire::Cmd::C);
    CHECK(wire::cmd_of(0x4F) == wire::Cmd::K);
    CHECK(wire::flags_of(0x25) == 0x5);
    CHECK(wire::flags_of(0x4F) == 0xF);
}

TEST_CASE("wire — Writer/Reader LE/BE round-trip + bounds") {
    std::array<uint8_t, 8> buf{};
    wire::Writer w(buf);
    w.u16_le(0x1234);
    w.u32_be(0xDEADBEEF);
    CHECK(w.ok());
    CHECK(w.size() == 6);
    CHECK(buf[0] == 0x34);  CHECK(buf[1] == 0x12);                 // LE
    CHECK(buf[2] == 0xDE);  CHECK(buf[5] == 0xEF);                 // BE

    wire::Reader r(std::span<const uint8_t>(buf.data(), 6));
    CHECK(r.u16_le() == 0x1234);
    CHECK(r.u32_be() == 0xDEADBEEF);
    CHECK(r.ok());

    std::array<uint8_t, 1> tiny{};
    wire::Writer w2(tiny);
    w2.u16_le(0xFFFF);
    CHECK_FALSE(w2.ok());                                          // overflow flagged

    wire::Reader r2(std::span<const uint8_t>(tiny.data(), 1));
    r2.u8(); r2.u8();
    CHECK_FALSE(r2.ok());                                          // read-past-end flagged
}

TEST_CASE("CTS — round-trip across the field ranges") {
    for (uint8_t ctr : {0, 5, 15})
        for (uint8_t sf : {5, 7, 8, 12})
            for (bool ar : {false, true})
                for (uint8_t to : {0, 1, 255}) {
                    std::array<uint8_t, 3> buf{};
                    cts_in in{ctr, sf, ar, to};
                    CHECK(pack_cts(in, buf) == 3);
                    auto out = parse_cts(buf);
                    CHECK(out.has_value());
                    if (out) {
                        CHECK(out->ctr_lo == ctr);
                        CHECK(out->chosen_data_sf == sf);
                        CHECK(out->already_received == ar);
                        CHECK(out->to == to);
                    }
                }
}

TEST_CASE("CTS — golden hex (§10.3)") {
    std::array<uint8_t, 3> buf{};
    CHECK(pack_cts({0x5, 8, true, 0x2A}, buf) == 3);
    CHECK(buf[0] == 0x25);  CHECK(buf[1] == 0x70);  CHECK(buf[2] == 0x2A);
    CHECK(pack_cts({0x0, 5, false, 0xFF}, buf) == 3);
    CHECK(buf[0] == 0x20);  CHECK(buf[1] == 0x00);  CHECK(buf[2] == 0xFF);
}

TEST_CASE("ACK — round-trip across the field ranges") {
    for (uint8_t ctr : {0, 5, 15})
        for (uint8_t bh : {0, 1, 2, 3})
            for (uint8_t sb : {0, 1, 2, 3})
                for (uint8_t to : {0, 7, 255}) {
                    std::array<uint8_t, 3> buf{};
                    ack_in in{ctr, bh, sb, to};
                    CHECK(pack_ack(in, buf) == 3);
                    auto out = parse_ack(buf);
                    CHECK(out.has_value());
                    if (out) {
                        CHECK(out->ctr_lo == ctr);
                        CHECK(out->budget_hint == bh);
                        CHECK(out->snr_bucket == sb);
                        CHECK(out->to == to);
                    }
                }
}

TEST_CASE("ACK — golden hex (§10.3)") {
    std::array<uint8_t, 3> buf{};
    CHECK(pack_ack({0x3, 2, 1, 0x07}, buf) == 3);
    CHECK(buf[0] == 0x43);  CHECK(buf[1] == 0x90);  CHECK(buf[2] == 0x07);
    CHECK(pack_ack({0xF, 3, 3, 0x00}, buf) == 3);
    CHECK(buf[0] == 0x4F);  CHECK(buf[1] == 0xF0);  CHECK(buf[2] == 0x00);
}

TEST_CASE("ACK — budget_hint SATURATES at 3 (matches Lua pack_ack, not a wrap)") {
    std::array<uint8_t, 3> buf{};
    CHECK(pack_ack({0x0, 4, 0, 0x00}, buf) == 3);   // 4 -> 3 (Lua), NOT (4 & 3)=0
    CHECK(buf[1] == 0xC0);                            // (3 << 6) | 0
    CHECK(pack_ack({0x0, 7, 0, 0x00}, buf) == 3);   // 7 -> 3
    CHECK(buf[1] == 0xC0);
}

TEST_CASE("CTS/ACK — robustness: reject wrong cmd / wrong length / bad input") {
    std::array<uint8_t, 3> cts{};
    CHECK(pack_cts({1, 7, false, 2}, cts) == 3);
    CHECK_FALSE(parse_ack(cts).has_value());            // CTS bytes, wrong cmd for ACK

    std::array<uint8_t, 2> shortbuf{0x20, 0x00};
    CHECK_FALSE(parse_cts(shortbuf).has_value());        // len != 3
    std::array<uint8_t, 4> longbuf{0x25, 0x70, 0x2A, 0x00};
    CHECK_FALSE(parse_cts(longbuf).has_value());         // len != 3

    std::array<uint8_t, 2> tiny{};
    CHECK(pack_cts({1, 7, false, 2}, tiny) == 0);        // out span too small
    std::array<uint8_t, 3> b{};
    CHECK(pack_cts({1, 13, false, 2}, b) == 0);          // sf > 12
    CHECK(pack_cts({1, 4,  false, 2}, b) == 0);          // sf < 5
}

TEST_CASE("CTS — field isolation: already_received toggles only byte1 bit 4") {
    std::array<uint8_t, 3> a{}, b{};
    CHECK(pack_cts({0x5, 8, false, 0x2A}, a) == 3);
    CHECK(pack_cts({0x5, 8, true,  0x2A}, b) == 3);
    CHECK(a[0] == b[0]);
    CHECK((a[1] ^ b[1]) == 0x10);   // only bit 4 differs
    CHECK(a[2] == b[2]);
}

// ===== C2: RTS / NACK / Q (§10 cmd-nibble; RTS byte-5 reading A) =============

TEST_CASE("RTS — round-trip (no M_BROADCAST = 7 B, M_BROADCAST = 9 B)") {
    for (uint8_t leaf : {0, 2, 15})
        for (uint8_t ctr : {0, 5, 15})
            for (uint8_t sf : {0, 1, 2, 3})
                for (uint8_t flags : {uint8_t(0), RTS_FLAG_M_BROADCAST, RTS_FLAG_RELAY,
                                      uint8_t(RTS_FLAG_M_BROADCAST | RTS_FLAG_RELAY)})
                    for (uint8_t dst : {0, 1, 255}) {
                        std::array<uint8_t, 9> buf{};
                        rts_in in{leaf, 0x0A, 0x0B, ctr, dst, sf, flags, 20, 0x5678};
                        const bool m = (flags & RTS_FLAG_M_BROADCAST) != 0;
                        CHECK(pack_rts(in, buf) == (m ? 9u : 7u));
                        auto o = parse_rts(buf);
                        CHECK(o.has_value());
                        if (o) {
                            CHECK(o->leaf_id == leaf);
                            CHECK(o->src == 0x0A);   CHECK(o->next == 0x0B);
                            CHECK(o->ctr_lo == ctr); CHECK(o->addr_len == 0);
                            CHECK(o->dst == dst);    CHECK(o->sf_index == sf);
                            CHECK(o->rts_flags == flags);
                            CHECK(o->payload_len == 20);
                            CHECK(o->m_broadcast == m);
                            CHECK(o->m_payload_id_lo16 == (m ? 0x5678 : 0x0000));
                        }
                    }
}

TEST_CASE("RTS — golden hex (§10.3, byte-5 reading A)") {
    std::array<uint8_t, 9> buf{};
    CHECK(pack_rts({2, 0x0A, 0x0B, 5, 0x0C, 3, 0, 20, 0}, buf) == 7);
    const uint8_t ex1[] = {0x12, 0x0A, 0x0B, 0x50, 0x0C, 0xC0, 0x14};
    for (int i = 0; i < 7; ++i) CHECK(buf[i] == ex1[i]);

    CHECK(pack_rts({1, 0x07, 0x09, 0xF, 0xFF, 2, RTS_FLAG_M_BROADCAST, 0xC8, 0x5678}, buf) == 9);
    const uint8_t ex2[] = {0x11, 0x07, 0x09, 0xF0, 0xFF, 0x84, 0xC8, 0x56, 0x78};
    for (int i = 0; i < 9; ++i) CHECK(buf[i] == ex2[i]);

    // both flags; payload_len 0 (= 256 wrapped by uint8_t, NOT clamped)
    CHECK(pack_rts({0, 0x14, 0x15, 0, 0x16, 0,
                    uint8_t(RTS_FLAG_M_BROADCAST | RTS_FLAG_RELAY), 0, 0x00FF}, buf) == 9);
    const uint8_t ex3[] = {0x10, 0x14, 0x15, 0x00, 0x16, 0x0C, 0x00, 0x00, 0xFF};
    for (int i = 0; i < 9; ++i) CHECK(buf[i] == ex3[i]);
}

TEST_CASE("RTS — rejects addr_len != 0 and wrong cmd / short frame") {
    std::array<uint8_t, 7> f{0x12, 0x0A, 0x0B, uint8_t(0x50 | (1 << 1)), 0x0C, 0xC0, 0x14};
    CHECK_FALSE(parse_rts(f).has_value());                 // addr_len = 1
    std::array<uint8_t, 4> nack{};
    CHECK(pack_nack({0, 0, 0, 0}, nack) == 4);
    CHECK_FALSE(parse_rts(nack).has_value());              // len < 7 (and wrong cmd)
}

TEST_CASE("NACK — round-trip + golden + reject") {
    for (uint8_t reason : {0, 1, 2, 3})
        for (uint8_t ctr : {0, 5, 15})
            for (uint8_t pl : {0, 0x30, 0xFF})
                for (uint8_t to : {0, 7, 255}) {
                    std::array<uint8_t, 4> buf{};
                    CHECK(pack_nack({reason, ctr, pl, to}, buf) == 4);
                    auto o = parse_nack(buf);
                    CHECK(o.has_value());
                    if (o) {
                        CHECK(o->reason == reason); CHECK(o->ctr_lo == ctr);
                        CHECK(o->payload == pl);    CHECK(o->to == to);
                    }
                }
    std::array<uint8_t, 4> b{};
    CHECK(pack_nack({0, 5, 5, 0x11}, b) == 4);
    CHECK(b[0] == 0x50); CHECK(b[1] == 0x50); CHECK(b[2] == 0x05); CHECK(b[3] == 0x11);
    CHECK(pack_nack({2, 0xA, 0x30, 0x07}, b) == 4);
    CHECK(b[0] == 0x52); CHECK(b[1] == 0xA0); CHECK(b[2] == 0x30); CHECK(b[3] == 0x07);
    CHECK(pack_nack({3, 1, 0xFF, 0x09}, b) == 4);
    CHECK(b[0] == 0x53); CHECK(b[1] == 0x10); CHECK(b[2] == 0xFF); CHECK(b[3] == 0x09);
    std::array<uint8_t, 3> sh{0x50, 0x50, 0x05};
    CHECK_FALSE(parse_nack(sh).has_value());               // len != 4
}

TEST_CASE("Q — REQ_SYNC + CHANNEL_PULL round-trip + golden + reject") {
    for (uint8_t leaf : {0, 1, 15})
        for (bool mob : {false, true}) {
            std::array<uint8_t, 4> buf{};
            q_in in{leaf, 0x14, 0xFF, q_opcode::req_sync, mob, {}};
            CHECK(pack_q(in, buf) == 4);
            auto o = parse_q(buf);
            CHECK(o.has_value());
            if (o) {
                CHECK(o->leaf_id == leaf); CHECK(o->opcode == 1);
                CHECK(o->mobile == mob);   CHECK(o->channel_id_count == 0);
            }
        }

    const uint32_t ids[] = {0x14123407u, 0x01020304u, 0xFFFFFFFFu};
    std::array<uint8_t, 32> buf{};
    q_in in{2, 0x14, 0x21, q_opcode::channel_pull, false, std::span<const uint32_t>(ids, 3)};
    CHECK(pack_q(in, buf) == 4u + 1u + 12u);               // 17
    auto pull_frame = std::span<const uint8_t>(buf.data(), 17);
    auto o = parse_q(pull_frame);
    CHECK(o.has_value());
    if (o) {
        CHECK(o->opcode == 3); CHECK(o->channel_id_count == 3);
        for (uint8_t i = 0; i < 3; ++i) {
            auto id = parse_q_channel_id(pull_frame, *o, i);
            CHECK(id.has_value());
            if (id) CHECK(*id == ids[i]);                  // big-endian round-trip
        }
    }

    std::array<uint8_t, 4> g{};
    CHECK(pack_q({1, 0x14, 0xFF, q_opcode::req_sync, true, {}}, g) == 4);
    CHECK(g[0] == 0x61); CHECK(g[1] == 0x14); CHECK(g[2] == 0xFF); CHECK(g[3] == 0x60);

    std::array<uint8_t, 9> gp{};
    const uint32_t one[] = {0x14123407u};
    CHECK(pack_q({2, 0x14, 0x21, q_opcode::channel_pull, false,
                  std::span<const uint32_t>(one, 1)}, gp) == 9);
    const uint8_t exq[] = {0x62, 0x14, 0x21, 0xC0, 0x01, 0x14, 0x12, 0x34, 0x07};
    for (int i = 0; i < 9; ++i) CHECK(gp[i] == exq[i]);

    std::array<uint8_t, 7> trunc{0x62, 0x14, 0x21, 0xC0, 0x01, 0x14, 0x12};  // count=1, 3 id bytes
    CHECK_FALSE(parse_q(trunc).has_value());               // len < 5 + 4
}

// ===== C3: H (hash-locate) / F (RREQ-RREP) floods (§10 cmd-nibble) ===========

TEST_CASE("H — round-trip (key_hash32 LE) + golden + reject") {
    for (uint8_t leaf : {0, 3, 15})
        for (uint8_t origin : {0, 42, 255})
            for (uint32_t kh : {0u, 0xDEADBEEFu, 0xFFFFFFFFu})
                for (uint8_t ttl : {0, 1, 16, 255}) {
                    std::array<uint8_t, 7> buf{};
                    CHECK(pack_h({leaf, origin, kh, ttl}, buf) == 7);
                    auto o = parse_h(buf);
                    CHECK(o.has_value());
                    if (o) {
                        CHECK(o->leaf_id == leaf);   CHECK(o->origin == origin);
                        CHECK(o->key_hash32 == kh);  CHECK(o->ttl == ttl);
                    }
                }
    std::array<uint8_t, 7> buf{};
    CHECK(pack_h({3, 0x2A, 0xDEADBEEFu, 0x10}, buf) == 7);
    const uint8_t ex[] = {0x73, 0x2A, 0xEF, 0xBE, 0xAD, 0xDE, 0x10};   // key_hash32 LITTLE-ENDIAN
    for (int i = 0; i < 7; ++i) CHECK(buf[i] == ex[i]);
    std::array<uint8_t, 6> sh{0x73, 0x2A, 0xEF, 0xBE, 0xAD, 0xDE};
    CHECK_FALSE(parse_h(sh).has_value());                 // len < 7
    std::array<uint8_t, 4> nack{};
    CHECK(pack_nack({0, 0, 0, 0}, nack) == 4);
    CHECK_FALSE(parse_h(nack).has_value());               // wrong cmd (+ short)
}

TEST_CASE("F — RREQ/RREP round-trip + golden + is_reply isolation + reject") {
    for (uint8_t leaf : {0, 3, 15})
        for (uint8_t origin : {0, 0x11, 255})
            for (bool rep : {false, true})
                for (uint8_t b4 : {0, 9, 255})
                    for (uint8_t hops : {0, 4, 255}) {
                        std::array<uint8_t, 6> buf{};
                        CHECK(pack_f({leaf, origin, rep, 0x2A, b4, hops}, buf) == 6);
                        auto o = parse_f(buf);
                        CHECK(o.has_value());
                        if (o) {
                            CHECK(o->leaf_id == leaf); CHECK(o->origin == origin);
                            CHECK(o->is_reply == rep); CHECK(o->dst_id == 0x2A);
                            CHECK(o->ttl_or_next_hop == b4); CHECK(o->hops == hops);
                        }
                    }
    std::array<uint8_t, 6> rreq{}, rrep{};
    CHECK(pack_f({3, 0x11, false, 0x2A, 8, 0}, rreq) == 6);   // RREQ: byte4 = ttl 8
    const uint8_t exq[] = {0x83, 0x11, 0x00, 0x2A, 0x08, 0x00};
    for (int i = 0; i < 6; ++i) CHECK(rreq[i] == exq[i]);
    CHECK(pack_f({3, 0x11, true, 0x2A, 9, 4}, rrep) == 6);    // RREP: byte4 = next_hop 9
    const uint8_t exr[] = {0x83, 0x11, 0x80, 0x2A, 0x09, 0x04};
    for (int i = 0; i < 6; ++i) CHECK(rrep[i] == exr[i]);
    // is_reply isolation: same everything else → only byte2 bit 7 flips.
    std::array<uint8_t, 6> a{}, b{};
    CHECK(pack_f({3, 0x11, false, 0x2A, 8, 4}, a) == 6);
    CHECK(pack_f({3, 0x11, true,  0x2A, 8, 4}, b) == 6);
    CHECK((a[2] ^ b[2]) == 0x80);
    CHECK(a[0] == b[0]); CHECK(a[1] == b[1]); CHECK(a[3] == b[3]);
    CHECK(a[4] == b[4]); CHECK(a[5] == b[5]);
    std::array<uint8_t, 5> sh{0x83, 0x11, 0x00, 0x2A, 0x08};
    CHECK_FALSE(parse_f(sh).has_value());                 // len < 6
}

// ===== C4: J join family (§10 cmd-nibble 0x9; byte1 reading A) ===============

TEST_CASE("J DISCOVER — round-trip (key_hash32 LE) + golden") {
    for (uint8_t leaf : {0, 3, 15})
        for (bool gw : {false, true})
            for (bool mob : {false, true})
                for (uint32_t kh : {0u, 0x11223344u, 0xDEADBEEFu}) {
                    std::array<uint8_t, 6> buf{};
                    CHECK(pack_j_discover({leaf, gw, mob, kh}, buf) == 6);
                    auto o = parse_j(buf);
                    CHECK(o.has_value());
                    if (o) {
                        CHECK(o->opcode == 0); CHECK(o->leaf_id == leaf);
                        CHECK(o->gateway_capable == gw); CHECK(o->is_mobile == mob);
                        CHECK(o->key_hash32 == kh);
                    }
                }
    std::array<uint8_t, 6> g{};
    CHECK(pack_j_discover({3, true, true, 0x11223344u}, g) == 6);
    const uint8_t ex[] = {0x93, 0xC0, 0x44, 0x33, 0x22, 0x11};
    for (int i = 0; i < 6; ++i) CHECK(g[i] == ex[i]);
}

TEST_CASE("J OFFER — round-trip + golden") {
    for (uint8_t leaf : {0, 5, 15})
        for (bool gw : {false, true})
            for (uint8_t rn : {0, 0x2A, 255})
                for (uint32_t kh : {0u, 0xDEADBEEFu})
                    for (uint8_t sfb : {0, 0x06, 255}) {
                        std::array<uint8_t, 8> buf{};
                        CHECK(pack_j_offer({leaf, gw, false, rn, kh, sfb}, buf) == 8);
                        auto o = parse_j(buf);
                        CHECK(o.has_value());
                        if (o) {
                            CHECK(o->opcode == 3); CHECK(o->leaf_id == leaf);
                            CHECK(o->gateway_capable == gw);
                            CHECK(o->responder_node_id == rn);
                            CHECK(o->responder_key_hash32 == kh);
                            CHECK(o->data_sf_bitmap == sfb);
                        }
                    }
    std::array<uint8_t, 8> g{};
    CHECK(pack_j_offer({5, true, false, 0x2A, 0xDEADBEEFu, 0x06}, g) == 8);
    const uint8_t ex[] = {0x95, 0xB0, 0x2A, 0xEF, 0xBE, 0xAD, 0xDE, 0x06};
    for (int i = 0; i < 8; ++i) CHECK(g[i] == ex[i]);
}

TEST_CASE("J CLAIM — round-trip (LE u16 lease) + golden") {
    for (uint8_t leaf : {0, 5, 15})
        for (bool mob : {false, true})
            for (uint16_t lease : {0u, 300u, 65535u})
                for (uint8_t epoch : {0, 7, 255}) {
                    std::array<uint8_t, 11> buf{};
                    CHECK(pack_j_claim({leaf, false, mob, 0xDEADBEEFu, 0x2A, lease, epoch, 0x99}, buf) == 11);
                    auto o = parse_j(buf);
                    CHECK(o.has_value());
                    if (o) {
                        CHECK(o->opcode == 1); CHECK(o->leaf_id == leaf); CHECK(o->is_mobile == mob);
                        CHECK(o->key_hash32 == 0xDEADBEEFu); CHECK(o->proposed_node_id == 0x2A);
                        CHECK(o->lease_age_seconds == lease);
                        CHECK(o->claim_epoch == epoch); CHECK(o->nonce == 0x99);
                    }
                }
    std::array<uint8_t, 11> g{};
    CHECK(pack_j_claim({5, false, true, 0xDEADBEEFu, 0x2A, 300, 7, 0x99}, g) == 11);
    const uint8_t ex[] = {0x95, 0x50, 0xEF, 0xBE, 0xAD, 0xDE, 0x2A, 0x2C, 0x01, 0x07, 0x99};
    for (int i = 0; i < 11; ++i) CHECK(g[i] == ex[i]);
}

TEST_CASE("J DENY — round-trip (two LE key_hash32 + LE lease) + golden") {
    std::array<uint8_t, 15> buf{};
    CHECK(pack_j_deny({5, true, false, 0x2A, 0x11223344u, 0xDEADBEEFu, 1000, 3, J_DENY_OWN_ID_DEFENSE}, buf) == 15);
    auto o = parse_j(buf);
    CHECK(o.has_value());
    if (o) {
        CHECK(o->opcode == 2); CHECK(o->leaf_id == 5);
        CHECK(o->gateway_capable == true); CHECK(o->is_mobile == false);
        CHECK(o->denied_node_id == 0x2A);
        CHECK(o->owner_key_hash32 == 0x11223344u);
        CHECK(o->claimant_key_hash32 == 0xDEADBEEFu);
        CHECK(o->owner_lease_age_seconds == 1000);
        CHECK(o->owner_claim_epoch == 3);
        CHECK(o->reason == J_DENY_OWN_ID_DEFENSE);
    }
    const uint8_t ex[] = {0x95, 0xA0, 0x2A, 0x44, 0x33, 0x22, 0x11,
                          0xEF, 0xBE, 0xAD, 0xDE, 0xE8, 0x03, 0x03, 0x03};
    for (int i = 0; i < 15; ++i) CHECK(buf[i] == ex[i]);
}

TEST_CASE("J — header flag/opcode isolation + strict-length + wrong-cmd reject") {
    std::array<uint8_t, 6> a{}, b{}, c{};
    CHECK(pack_j_discover({3, false, false, 0}, a) == 6);
    CHECK(pack_j_discover({3, true,  false, 0}, b) == 6);
    CHECK((a[1] ^ b[1]) == 0x80);                          // gateway_capable = bit 7
    CHECK(pack_j_discover({3, false, true,  0}, c) == 6);
    CHECK((a[1] ^ c[1]) == 0x40);                          // is_mobile = bit 6
    std::array<uint8_t, 11> cl{};
    CHECK(pack_j_claim({3, false, false, 0, 0, 0, 0, 0}, cl) == 11);
    CHECK((a[1] ^ cl[1]) == 0x10);                         // opcode 0 vs 1 → bits 5..4
    std::array<uint8_t, 7> rts{};
    CHECK(pack_rts({2, 0x0A, 0x0B, 5, 0x0C, 3, 0, 20, 0}, rts) == 7);
    CHECK_FALSE(parse_j(rts).has_value());                 // wrong cmd
    std::array<uint8_t, 7> bad{0x93, 0xC0, 0x44, 0x33, 0x22, 0x11, 0x00};
    CHECK_FALSE(parse_j(bad).has_value());                 // op 0 (DISCOVER) but 7 B ≠ 6
    std::array<uint8_t, 1> trunc{0x93};
    CHECK_FALSE(parse_j(trunc).has_value());               // < 2 B header
}

// -----------------------------------------------------------------------------
// BCN — cmd 0x0 (C5). Header + 4-B entries + schedule + 32-B seen-bitmap + opaque
// ext. Golden hex hand-derived from §10.3; field meaning matches the Lua beacon.
// -----------------------------------------------------------------------------
TEST_CASE("BCN — golden minimal (header + 2 entries)") {
    const beacon_entry ents[] = {
        {0x05, 0x07, 0xC, false, 2},
        {0x09, 0x07, 0xA, true,  3},
    };
    beacon_in in{};
    in.leaf_id = 3; in.src = 0x11; in.key_hash32 = 0xDEADBEEF;
    in.entries = ents; in.seen_bitmap = {};
    std::array<uint8_t, 32> buf{};
    size_t n = pack_beacon(in, buf);
    CHECK(n == 16);
    const uint8_t ex[] = {0x03, 0x11, 0x02, 0x00, 0xEF, 0xBE, 0xAD, 0xDE,
                          0x05, 0x07, 0xC0, 0x02, 0x09, 0x07, 0xA1, 0x03};
    for (int i = 0; i < 16; ++i) CHECK(buf[i] == ex[i]);

    std::span<const uint8_t> fr(buf.data(), n);
    auto o = parse_beacon(fr);
    CHECK(o.has_value());
    if (o) {
        CHECK(o->leaf_id == 3);
        CHECK(o->src == 0x11);
        CHECK(o->key_hash32 == 0xDEADBEEF);
        CHECK_FALSE(o->has_schedule);
        CHECK_FALSE(o->self_gateway);
        CHECK_FALSE(o->is_mobile);
        CHECK_FALSE(o->has_seen_bitmap);
        CHECK_FALSE(o->has_ext);
        CHECK(o->n_entries == 2);
        CHECK(o->frame_len == 16);
        auto e0 = parse_beacon_entry(fr, *o, 0);
        CHECK(e0.has_value());
        if (e0) { CHECK(e0->dest == 0x05); CHECK(e0->next == 0x07);
                  CHECK(e0->score_bucket == 0xC); CHECK_FALSE(e0->is_gateway); CHECK(e0->hops == 2); }
        auto e1 = parse_beacon_entry(fr, *o, 1);
        CHECK(e1.has_value());
        if (e1) { CHECK(e1->dest == 0x09); CHECK(e1->next == 0x07);
                  CHECK(e1->score_bucket == 0xA); CHECK(e1->is_gateway); CHECK(e1->hops == 3); }
        CHECK_FALSE(parse_beacon_entry(fr, *o, 2).has_value());
        CHECK(beacon_seen_bitmap(fr, *o).empty());
        CHECK(beacon_ext(fr, *o).empty());
    }
}

TEST_CASE("BCN — golden schedule + seen-bitmap") {
    const schedule_record recs[] = {
        {2, 8, false, 30, 15, 60},   // layer2, sf8, ×1000ms, dur30, off15, period60
    };
    std::array<uint8_t, 32> bm{};
    bm[0]  = 0x20;   // id 5
    bm[1]  = 0x02;   // id 9
    bm[16] = 0x04;   // id 130
    beacon_in in{};
    in.leaf_id = 1; in.src = 5; in.self_gateway = true; in.key_hash32 = 0xDEADBEEF;
    in.gateway_spread_nibble = 0;
    in.schedule = recs;
    in.seen_bitmap = bm;
    std::array<uint8_t, 64> buf{};
    size_t n = pack_beacon(in, buf);
    CHECK(n == 8 + 1 + 4 + 32);   // 45
    const uint8_t head[] = {0x01, 0x05, 0xD0, 0x00, 0xEF, 0xBE, 0xAD, 0xDE,
                            0x01, 0x26, 0x1E, 0x0F, 0x3C};
    for (int i = 0; i < 13; ++i) CHECK(buf[i] == head[i]);
    CHECK(buf[13 + 0]  == 0x20);
    CHECK(buf[13 + 1]  == 0x02);
    CHECK(buf[13 + 16] == 0x04);

    std::span<const uint8_t> fr(buf.data(), n);
    auto o = parse_beacon(fr);
    CHECK(o.has_value());
    if (o) {
        CHECK(o->leaf_id == 1);
        CHECK(o->src == 5);
        CHECK(o->self_gateway);
        CHECK(o->has_schedule);
        CHECK(o->gateway_spread_nibble == 0);
        CHECK(o->schedule_count == 1);
        CHECK(o->n_entries == 0);
        CHECK(o->has_seen_bitmap);
        CHECK_FALSE(o->has_ext);
        CHECK(o->frame_len == 45);
        auto s0 = parse_beacon_schedule(fr, *o, 0);
        CHECK(s0.has_value());
        if (s0) { CHECK(s0->layer_id == 2); CHECK(s0->routing_sf == 8); CHECK_FALSE(s0->period_unit_5s);
                  CHECK(s0->duration_100ms == 30); CHECK(s0->offset_100ms == 15); CHECK(s0->period_units == 60); }
        CHECK_FALSE(parse_beacon_schedule(fr, *o, 1).has_value());
        auto bmp = beacon_seen_bitmap(fr, *o);
        CHECK(bmp.size() == 32);
        if (bmp.size() == 32) { CHECK(bmp[0] == 0x20); CHECK(bmp[1] == 0x02);
                                CHECK(bmp[16] == 0x04); CHECK(bmp[2] == 0x00); }
        CHECK(beacon_ext(fr, *o).empty());
    }
}

TEST_CASE("BCN — opaque ext round-trip") {
    const uint8_t ext_payload[] = {0x35, 0x01, 0x07, 0x56, 0x78, 0x05};
    beacon_in in{};
    in.leaf_id = 2; in.src = 0x20; in.key_hash32 = 0xDEADBEEF;
    in.ext = ext_payload; in.seen_bitmap = {};
    std::array<uint8_t, 32> buf{};
    size_t n = pack_beacon(in, buf);
    CHECK(n == 8 + 1 + 6);   // 15
    CHECK(buf[2] == 0x08);   // only has_ext, n_entries 0
    CHECK(buf[8] == 0x06);   // ext_len
    for (int i = 0; i < 6; ++i) CHECK(buf[9 + i] == ext_payload[i]);

    std::span<const uint8_t> fr(buf.data(), n);
    auto o = parse_beacon(fr);
    CHECK(o.has_value());
    if (o) {
        CHECK(o->has_ext);
        CHECK_FALSE(o->has_schedule);
        CHECK_FALSE(o->has_seen_bitmap);
        CHECK(o->n_entries == 0);
        CHECK(o->ext_len == 6);
        CHECK(o->frame_len == 15);
        auto x = beacon_ext(fr, *o);
        CHECK(x.size() == 6);
        if (x.size() == 6) for (int i = 0; i < 6; ++i) CHECK(x[i] == ext_payload[i]);
    }
}

TEST_CASE("BCN — n_entries 6-bit split (byte3) round-trip") {
    for (uint8_t count : {uint8_t(10), uint8_t(63)}) {
        std::array<beacon_entry, 63> ents{};
        for (uint8_t i = 0; i < count; ++i)
            ents[i] = beacon_entry{uint8_t(i + 1), uint8_t(0x40 + i),
                                   uint8_t(i & 0x0F), (i & 1) != 0, uint8_t(i + 2)};
        beacon_in in{};
        in.leaf_id = 4; in.src = 0x33; in.key_hash32 = 0x01020304;
        in.entries = std::span<const beacon_entry>(ents.data(), count);
        in.seen_bitmap = {};
        std::array<uint8_t, 8 + 63 * 4> buf{};
        size_t n = pack_beacon(in, buf);
        CHECK(n == size_t(8 + count * 4));
        CHECK(buf[2] == uint8_t(count & 0x07));                       // flags 0, n_lo
        CHECK(buf[3] == uint8_t(((count >> 3) & 0x07) << 5));         // n_hi in byte3

        std::span<const uint8_t> fr(buf.data(), n);
        auto o = parse_beacon(fr);
        CHECK(o.has_value());
        if (o) {
            CHECK(o->n_entries == count);
            bool all = true;
            for (uint8_t i = 0; i < count; ++i) {
                auto e = parse_beacon_entry(fr, *o, i);
                if (!e || e->dest != uint8_t(i + 1) || e->next != uint8_t(0x40 + i) ||
                    e->score_bucket != (i & 0x0F) || e->is_gateway != ((i & 1) != 0) ||
                    e->hops != uint8_t(i + 2)) { all = false; break; }
            }
            CHECK(all);
            CHECK_FALSE(parse_beacon_entry(fr, *o, count).has_value());
        }
    }
}

TEST_CASE("BCN — schedule multi-record + both period units round-trip") {
    const schedule_record recs[] = {
        {1, 7,  false, 10, 0, 30},    // ×1000 ms
        {3, 9,  true,  20, 5, 12},    // ×5000 ms
        {5, 12, false, 40, 9, 250},
    };
    beacon_in in{};
    in.leaf_id = 6; in.src = 0x44; in.key_hash32 = 0xCAFEBABE;
    in.gateway_spread_nibble = 0xB;
    in.schedule = recs; in.seen_bitmap = {};
    std::array<uint8_t, 64> buf{};
    size_t n = pack_beacon(in, buf);
    CHECK(n == size_t(8 + 1 + 3 * 4));
    std::span<const uint8_t> fr(buf.data(), n);
    auto o = parse_beacon(fr);
    CHECK(o.has_value());
    if (o) {
        CHECK(o->has_schedule);
        CHECK(o->gateway_spread_nibble == 0xB);
        CHECK(o->schedule_count == 3);
        bool all = true;
        for (uint8_t i = 0; i < 3; ++i) {
            auto s = parse_beacon_schedule(fr, *o, i);
            if (!s || s->layer_id != recs[i].layer_id || s->routing_sf != recs[i].routing_sf ||
                s->period_unit_5s != recs[i].period_unit_5s || s->duration_100ms != recs[i].duration_100ms ||
                s->offset_100ms != recs[i].offset_100ms || s->period_units != recs[i].period_units) { all = false; break; }
        }
        CHECK(all);
    }
}

TEST_CASE("BCN — all sections combined round-trip") {
    const schedule_record recs[] = {{2, 8, false, 30, 15, 60}};
    const beacon_entry ents[] = {{0x05, 0x07, 0xC, false, 2}, {0x09, 0x07, 0xA, true, 3}};
    std::array<uint8_t, 32> bm{};
    bm[3] = 0x80;   // id 31
    const uint8_t ext_payload[] = {0xAA, 0xBB};
    beacon_in in{};
    in.leaf_id = 7; in.src = 0x55; in.is_mobile = true; in.self_gateway = true;
    in.key_hash32 = 0x11223344; in.gateway_spread_nibble = 4;
    in.schedule = recs; in.entries = ents; in.seen_bitmap = bm; in.ext = ext_payload;
    std::array<uint8_t, 96> buf{};
    size_t n = pack_beacon(in, buf);
    CHECK(n == size_t(8 + (1 + 4) + (2 * 4) + 32 + (1 + 2)));   // 56
    std::span<const uint8_t> fr(buf.data(), n);
    auto o = parse_beacon(fr);
    CHECK(o.has_value());
    if (o) {
        CHECK(o->has_schedule); CHECK(o->self_gateway); CHECK(o->is_mobile);
        CHECK(o->has_seen_bitmap); CHECK(o->has_ext);
        CHECK(o->schedule_count == 1);
        CHECK(o->n_entries == 2);
        CHECK(o->gateway_spread_nibble == 4);
        CHECK(o->ext_len == 2);
        auto e1 = parse_beacon_entry(fr, *o, 1);
        if (e1) { CHECK(e1->dest == 0x09); CHECK(e1->is_gateway); CHECK(e1->hops == 3); }
        auto s0 = parse_beacon_schedule(fr, *o, 0);
        if (s0) { CHECK(s0->routing_sf == 8); CHECK(s0->duration_100ms == 30); }
        auto bmp = beacon_seen_bitmap(fr, *o);
        CHECK(bmp.size() == 32);
        if (bmp.size() == 32) { CHECK(bmp[3] == 0x80); CHECK(bmp[0] == 0x00); }
        auto x = beacon_ext(fr, *o);
        CHECK(x.size() == 2);
        if (x.size() == 2) { CHECK(x[0] == 0xAA); CHECK(x[1] == 0xBB); }
    }
}

TEST_CASE("BCN — reject: wrong cmd / truncation / trailing / over-cap") {
    const schedule_record recs[] = {{2, 8, false, 30, 15, 60}};
    const beacon_entry ents[] = {{0x05, 0x07, 0xC, false, 2}};
    std::array<uint8_t, 32> bm{};
    const uint8_t ext_payload[] = {0xAA, 0xBB};
    beacon_in in{};
    in.leaf_id = 1; in.src = 5; in.key_hash32 = 0xDEADBEEF;
    in.schedule = recs; in.entries = ents; in.seen_bitmap = bm; in.ext = ext_payload;
    std::array<uint8_t, 96> buf{};
    size_t n = pack_beacon(in, buf);
    CHECK(n == size_t(8 + 5 + 4 + 32 + 3));   // 52

    std::array<uint8_t, 8> wrong{};
    for (int i = 0; i < 8; ++i) wrong[i] = buf[i];
    wrong[0] = wire::cmd_byte(wire::Cmd::D, 1);
    CHECK_FALSE(parse_beacon(wrong).has_value());                              // wrong cmd
    CHECK_FALSE(parse_beacon(std::span<const uint8_t>(buf.data(), 7)).has_value());        // < 8-B header
    CHECK_FALSE(parse_beacon(std::span<const uint8_t>(buf.data(), 8 + 1 + 2)).has_value()); // mid-schedule
    CHECK_FALSE(parse_beacon(std::span<const uint8_t>(buf.data(), 8 + 5 + 2)).has_value()); // mid-entries
    CHECK_FALSE(parse_beacon(std::span<const uint8_t>(buf.data(), 8 + 5 + 4 + 16)).has_value()); // mid-bitmap
    CHECK_FALSE(parse_beacon(std::span<const uint8_t>(buf.data(), n - 1)).has_value());     // ext_len past end

    std::array<uint8_t, 18> tb{};
    const beacon_entry me[] = {{0x05, 0x07, 0xC, false, 2}};
    beacon_in min_in{};
    min_in.leaf_id = 3; min_in.src = 0x11; min_in.key_hash32 = 0xDEADBEEF;
    min_in.entries = me; min_in.seen_bitmap = {};
    size_t mn = pack_beacon(min_in, tb);
    CHECK(mn == 12);
    CHECK_FALSE(parse_beacon(std::span<const uint8_t>(tb.data(), mn + 1)).has_value());     // trailing byte

    std::array<beacon_entry, 64> too_many{};
    std::array<schedule_record, 16> too_many_sched{};
    std::array<uint8_t, 300> big{};
    beacon_in oc{}; oc.entries = std::span<const beacon_entry>(too_many.data(), 64);
    CHECK(pack_beacon(oc, big) == 0);                                          // entries > 63
    beacon_in os{}; os.schedule = std::span<const schedule_record>(too_many_sched.data(), 16);
    CHECK(pack_beacon(os, big) == 0);                                          // schedule > 15
}
