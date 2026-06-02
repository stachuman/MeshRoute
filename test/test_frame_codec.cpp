// MeshRoute — test_frame_codec.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
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
    for (uint8_t sf : {5, 7, 8, 12})
        for (bool ar : {false, true})
            for (uint8_t tx : {0, 3, 254})
                for (uint8_t rx : {0, 1, 255}) {
                    std::array<uint8_t, 3> buf{};
                    cts_in in{sf, ar, tx, rx};
                    CHECK(pack_cts(in, buf) == 3);
                    auto out = parse_cts(buf);
                    CHECK(out.has_value());
                    if (out) {
                        CHECK(out->chosen_data_sf == sf);
                        CHECK(out->already_received == ar);
                        CHECK(out->tx_id == tx);
                        CHECK(out->rx_id == rx);
                    }
                }
}

TEST_CASE("CTS — golden hex (§10.3)") {
    std::array<uint8_t, 3> buf{};
    CHECK(pack_cts({8, true, 0x11, 0x2A}, buf) == 3);   // sf=8 -> sf3=3; flags=(3<<1)|1=0x7
    CHECK(buf[0] == 0x27);  CHECK(buf[1] == 0x11);  CHECK(buf[2] == 0x2A);
    CHECK(pack_cts({5, false, 0x00, 0xFF}, buf) == 3);  // sf=5 -> sf3=0; flags=0
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
    CHECK(pack_cts({7, false, 1, 2}, cts) == 3);
    CHECK_FALSE(parse_ack(cts).has_value());            // CTS bytes, wrong cmd for ACK

    std::array<uint8_t, 2> shortbuf{0x20, 0x00};
    CHECK_FALSE(parse_cts(shortbuf).has_value());        // len != 3
    std::array<uint8_t, 4> longbuf{0x27, 0x11, 0x2A, 0x00};
    CHECK_FALSE(parse_cts(longbuf).has_value());         // len != 3

    std::array<uint8_t, 2> tiny{};
    CHECK(pack_cts({7, false, 1, 2}, tiny) == 0);        // out span too small
    std::array<uint8_t, 3> b{};
    CHECK(pack_cts({13, false, 1, 2}, b) == 0);          // sf > 12
    CHECK(pack_cts({4,  false, 1, 2}, b) == 0);          // sf < 5
}

TEST_CASE("CTS — field isolation: already_received toggles only byte0 bit 0") {
    std::array<uint8_t, 3> a{}, b{};
    CHECK(pack_cts({8, false, 0x11, 0x2A}, a) == 3);
    CHECK(pack_cts({8, true,  0x11, 0x2A}, b) == 3);
    CHECK((a[0] ^ b[0]) == 0x01);   // already_received is bit 0 of the flags nibble
    CHECK(a[1] == b[1]);            // tx_id unchanged
    CHECK(a[2] == b[2]);            // rx_id unchanged
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
        {5, 12, false, 40, 9, 250},   // routing_sf top edge: wire (sf-5)=7
        {2, 5,  false, 11, 1, 7},     // routing_sf bottom edge: wire (sf-5)=0 floor
        {4, 6,  true,  12, 2, 9},     // sf=6: wire (sf-5)=1 — catches a single-step shift
    };
    beacon_in in{};
    in.leaf_id = 6; in.src = 0x44; in.key_hash32 = 0xCAFEBABE;
    in.gateway_spread_nibble = 0xB;
    in.schedule = recs; in.seen_bitmap = {};
    std::array<uint8_t, 64> buf{};
    size_t n = pack_beacon(in, buf);
    CHECK(n == size_t(8 + 1 + 5 * 4));
    std::span<const uint8_t> fr(buf.data(), n);
    auto o = parse_beacon(fr);
    CHECK(o.has_value());
    if (o) {
        CHECK(o->has_schedule);
        CHECK(o->gateway_spread_nibble == 0xB);
        CHECK(o->schedule_count == 5);
        bool all = true;
        for (uint8_t i = 0; i < 5; ++i) {
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
    CHECK(buf[2] == 0xFA);   // has_schedule|self_gw|is_mobile|seen_bm|ext | n_lo(2) = 0xF8|0x02
    CHECK(buf[3] == 0x00);   // n_entries_hi = 0
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

TEST_CASE("BCN — byte2 flag bit isolation (each flag toggles exactly its bit)") {
    const beacon_entry ents[] = {{0x05, 0x07, 0xC, false, 2}};   // 1 entry, no flags -> byte2 = 0x01
    auto pack_with = [&](bool sched, bool gw, bool mob, bool bm_on, bool ext_on,
                         std::array<uint8_t, 64>& out) -> size_t {
        const schedule_record recs[] = {{2, 8, false, 30, 15, 60}};
        std::array<uint8_t, 32> bm{};
        const uint8_t ex[] = {0xAA};
        beacon_in in{};
        in.self_gateway = gw; in.is_mobile = mob;
        if (sched)  in.schedule = recs;
        in.entries = ents;
        if (bm_on)  in.seen_bitmap = bm;
        if (ext_on) in.ext = ex;
        return pack_beacon(in, out);   // recs/bm/ex outlive this call
    };
    std::array<uint8_t, 64> base{}, b_sched{}, b_gw{}, b_mob{}, b_bm{}, b_ext{};
    CHECK(pack_with(false, false, false, false, false, base)    > 0);
    CHECK(pack_with(true,  false, false, false, false, b_sched) > 0);
    CHECK(pack_with(false, true,  false, false, false, b_gw)    > 0);
    CHECK(pack_with(false, false, true,  false, false, b_mob)   > 0);
    CHECK(pack_with(false, false, false, true,  false, b_bm)    > 0);
    CHECK(pack_with(false, false, false, false, true,  b_ext)   > 0);
    CHECK(base[2] == 0x01);                         // 1 entry, every flag clear
    CHECK((base[2] ^ b_sched[2]) == 0x80);          // has_schedule    = bit 7
    CHECK((base[2] ^ b_gw[2])    == 0x40);          // self_gateway    = bit 6
    CHECK((base[2] ^ b_mob[2])   == 0x20);          // is_mobile       = bit 5
    CHECK((base[2] ^ b_bm[2])    == 0x10);          // has_seen_bitmap = bit 4
    CHECK((base[2] ^ b_ext[2])   == 0x08);          // has_ext         = bit 3
}

TEST_CASE("BCN — entry extremes (hops=255, bucket=0xF) + entry rsv bits ignored on parse") {
    const beacon_entry ents[] = {{0xFE, 0xFD, 0xF, true, 255}};
    beacon_in in{};
    in.leaf_id = 1; in.src = 2; in.key_hash32 = 0xDEADBEEF;
    in.entries = ents; in.seen_bitmap = {};
    std::array<uint8_t, 16> buf{};
    size_t n = pack_beacon(in, buf);
    CHECK(n == 12);
    std::span<const uint8_t> fr(buf.data(), n);
    auto o = parse_beacon(fr);
    CHECK(o.has_value());
    if (o) {
        auto e = parse_beacon_entry(fr, *o, 0);
        CHECK(e.has_value());
        if (e) { CHECK(e->dest == 0xFE); CHECK(e->next == 0xFD);
                 CHECK(e->score_bucket == 0xF); CHECK(e->is_gateway); CHECK(e->hops == 255); }
        // forge the entry's rsv bits (byte2 bits 1..3) -> parse must mask them off
        std::array<uint8_t, 12> forged{};
        for (size_t i = 0; i < n; ++i) forged[i] = buf[i];
        forged[8 + 2] |= 0x0E;
        std::span<const uint8_t> ff(forged.data(), n);
        auto o2 = parse_beacon(ff);
        CHECK(o2.has_value());
        if (o2) {
            auto e2 = parse_beacon_entry(ff, *o2, 0);
            CHECK(e2.has_value());
            if (e2) { CHECK(e2->score_bucket == 0xF); CHECK(e2->is_gateway); }
        }
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

    std::array<uint8_t, 16> bm16{};
    std::array<uint8_t, 33> bm33{};
    beacon_in ob16{}; ob16.seen_bitmap = bm16;
    CHECK(pack_beacon(ob16, big) == 0);                                        // seen_bitmap size 16 != 32
    beacon_in ob33{}; ob33.seen_bitmap = bm33;
    CHECK(pack_beacon(ob33, big) == 0);                                        // seen_bitmap size 33 != 32

    std::array<uint8_t, 256> ext256{};
    beacon_in oe{}; oe.ext = ext256;
    CHECK(pack_beacon(oe, big) == 0);                                          // ext > 255
    std::array<uint8_t, 255> ext255{};
    beacon_in oe255{}; oe255.leaf_id = 1; oe255.src = 2; oe255.ext = ext255;
    CHECK(pack_beacon(oe255, big) == size_t(8 + 1 + 255));                     // 255-B ext boundary packs
}

// -----------------------------------------------------------------------------
// DATA — cmd 0x3 (C6). 14-B §10 header + opaque inner + opaque 4-B MAC.
// Golden hex hand-derived from §10.3; field meaning matches the Lua data plane.
// -----------------------------------------------------------------------------
TEST_CASE("DATA — golden NORMAL (header + visited + inner + MAC)") {
    const uint8_t vis[]   = {0x05, 0x09, 0, 0, 0, 0};
    const uint8_t inner[] = {0x00, 0x07, 0xAA, 0xBB};   // src_addr_len=0, origin=7, body=AA BB
    const uint8_t mac[]   = {0, 0, 0, 0};
    data_in in{};
    in.addr_len = 0; in.flags = 0; in.next = 0x0B; in.dst = 0x0C;
    in.hops_remaining = 10; in.committed_hops = 2; in.prev_fwd_rt_hops = 3;
    in.ctr = 0x1234; in.visited = vis; in.inner = inner; in.mac = mac;
    std::array<uint8_t, 32> buf{};
    size_t n = pack_data(in, buf);
    CHECK(n == 22);
    const uint8_t ex[] = {0x30, 0x00, 0x0B, 0x0C, 0x52, 0x03, 0x34, 0x12,
                          0x05, 0x09, 0x00, 0x00, 0x00, 0x00,
                          0x00, 0x07, 0xAA, 0xBB, 0x00, 0x00, 0x00, 0x00};
    for (int i = 0; i < 22; ++i) CHECK(buf[i] == ex[i]);

    std::span<const uint8_t> fr(buf.data(), n);
    auto o = parse_data(fr);
    CHECK(o.has_value());
    if (o) {
        CHECK(o->addr_len == 0);
        CHECK(o->flags == 0);
        CHECK_FALSE(o->payload_type_m);
        CHECK(o->next == 0x0B); CHECK(o->dst == 0x0C);
        CHECK(o->hops_remaining == 10); CHECK(o->committed_hops == 2);
        CHECK(o->prev_fwd_rt_hops == 3);
        CHECK(o->ctr == 0x1234); CHECK(o->ctr_lo4 == 0x4);
        CHECK(o->inner_len == 4);
        CHECK(o->frame_len == 22);
        auto v = data_visited(fr, *o);
        CHECK(v.size() == 6);
        if (v.size() == 6) { CHECK(v[0] == 0x05); CHECK(v[1] == 0x09); CHECK(v[2] == 0x00); }
        auto inr = data_inner(fr, *o);
        CHECK(inr.size() == 4);
        auto mc = data_mac(fr, *o);
        CHECK(mc.size() == 4);
        if (mc.size() == 4) CHECK(mc[0] == 0x00);
        auto u = parse_unicast_inner(inr);
        CHECK(u.has_value());
        if (u) { CHECK(u->origin == 7); CHECK(u->body.size() == 2);
                 if (u->body.size() == 2) { CHECK(u->body[0] == 0xAA); CHECK(u->body[1] == 0xBB); } }
    }
}

TEST_CASE("DATA — golden M / channel (payload_type_m, channel_msg_id BE)") {
    const uint8_t inner[] = {0x07, 0xAB, 0xCD, 0xEF, 0x02, 0x01, 0x99};   // msgid BE | chan | flavor | body
    data_in in{};
    in.flags = DATA_FLAG_PAYLOAD_TYPE_M; in.next = 0x0B; in.dst = 0xFF;
    in.hops_remaining = 31; in.committed_hops = 0; in.prev_fwd_rt_hops = 0;
    in.ctr = 0x0001; in.visited = {}; in.inner = inner; in.mac = {};
    std::array<uint8_t, 32> buf{};
    size_t n = pack_data(in, buf);
    CHECK(n == 25);
    const uint8_t ex[] = {0x30, 0x10, 0x0B, 0xFF, 0xF8, 0x00, 0x01, 0x00,
                          0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                          0x07, 0xAB, 0xCD, 0xEF, 0x02, 0x01, 0x99, 0x00, 0x00, 0x00, 0x00};
    for (int i = 0; i < 25; ++i) CHECK(buf[i] == ex[i]);

    std::span<const uint8_t> fr(buf.data(), n);
    auto o = parse_data(fr);
    CHECK(o.has_value());
    if (o) {
        CHECK(o->payload_type_m);
        CHECK(o->flags == DATA_FLAG_PAYLOAD_TYPE_M);
        CHECK(o->dst == 0xFF);
        CHECK(o->hops_remaining == 31); CHECK(o->committed_hops == 0);
        CHECK(o->ctr == 0x0001);
        CHECK(o->inner_len == 7);
        auto m = parse_m_inner(data_inner(fr, *o));
        CHECK(m.has_value());
        if (m) { CHECK(m->channel_msg_id == 0x07ABCDEF); CHECK(m->channel_id == 0x02);
                 CHECK(m->flavor == 0x01); CHECK(m->body.size() == 1);
                 if (m->body.size() == 1) CHECK(m->body[0] == 0x99); }
    }
}

TEST_CASE("DATA — round-trip across fields (flags, visited, ctr, inner, hops)") {
    for (uint8_t flags : {uint8_t(0), uint8_t(DATA_FLAG_E2E_ACK_REQ),
                          uint8_t(DATA_FLAG_E2E_IS_ACK | DATA_FLAG_PRIORITY), uint8_t(0x0F)})
        for (uint16_t ctr : {uint16_t(0), uint16_t(0x1234), uint16_t(0xFFFF)})
            for (uint8_t hr : {uint8_t(0), uint8_t(31)})
                for (size_t inlen : {size_t(0), size_t(1), size_t(40)}) {
                    std::array<uint8_t, 6> vis{1, 2, 3, 4, 5, 6};
                    std::array<uint8_t, 40> inner{};
                    for (size_t i = 0; i < inlen; ++i) inner[i] = uint8_t(0x10 + i);
                    std::array<uint8_t, 4> mac{0xDE, 0xAD, 0xBE, 0xEF};
                    data_in in{};
                    in.flags = flags; in.next = 0x21; in.dst = 0x22;
                    in.hops_remaining = hr; in.committed_hops = 5; in.prev_fwd_rt_hops = 9;
                    in.ctr = ctr; in.visited = vis;
                    in.inner = std::span<const uint8_t>(inner.data(), inlen); in.mac = mac;
                    std::array<uint8_t, 64> buf{};
                    size_t n = pack_data(in, buf);
                    CHECK(n == 18 + inlen);
                    std::span<const uint8_t> fr(buf.data(), n);
                    auto o = parse_data(fr);
                    CHECK(o.has_value());
                    if (o) {
                        CHECK(o->flags == (flags & 0x0F));
                        CHECK(o->e2e_ack_req    == ((flags & DATA_FLAG_E2E_ACK_REQ)    != 0));
                        CHECK(o->e2e_is_ack     == ((flags & DATA_FLAG_E2E_IS_ACK)     != 0));
                        CHECK(o->priority       == ((flags & DATA_FLAG_PRIORITY)       != 0));
                        CHECK(o->payload_type_m == ((flags & DATA_FLAG_PAYLOAD_TYPE_M) != 0));
                        CHECK(o->ctr == ctr);
                        CHECK(o->ctr_lo4 == (ctr & 0x0F));
                        CHECK(o->hops_remaining == hr);
                        CHECK(o->committed_hops == 5);
                        CHECK(o->prev_fwd_rt_hops == 9);
                        CHECK(o->inner_len == inlen);
                        auto v = data_visited(fr, *o);
                        bool vok = v.size() == 6;
                        for (uint8_t i = 0; i < 6 && vok; ++i) vok = v[i] == uint8_t(i + 1);
                        CHECK(vok);
                        auto inr = data_inner(fr, *o);
                        bool iok = inr.size() == inlen;
                        for (size_t i = 0; i < inlen && iok; ++i) iok = inr[i] == uint8_t(0x10 + i);
                        CHECK(iok);
                        auto mc = data_mac(fr, *o);
                        CHECK(mc.size() == 4);
                        if (mc.size() == 4) { CHECK(mc[0] == 0xDE); CHECK(mc[3] == 0xEF); }
                    }
                }
}

TEST_CASE("DATA — hop_budget saturates (matches Lua math.min, not a wrap)") {
    data_in in{};
    in.next = 1; in.dst = 2; in.hops_remaining = 40; in.committed_hops = 9;
    in.visited = {}; in.inner = {}; in.mac = {};
    std::array<uint8_t, 24> buf{};
    size_t n = pack_data(in, buf);
    CHECK(n == 18);
    CHECK(buf[4] == 0xFF);   // (31<<3)|7 = 0xF8|0x07 — saturated, not 40&0x1f / 9&0x07
    std::span<const uint8_t> fr(buf.data(), n);
    auto o = parse_data(fr);
    CHECK(o.has_value());
    if (o) { CHECK(o->hops_remaining == 31); CHECK(o->committed_hops == 7); }
}

TEST_CASE("DATA — byte1 flag bit isolation (each flag toggles exactly its bit)") {
    auto pack_flags = [](uint8_t flags, std::array<uint8_t, 24>& out) -> size_t {
        data_in in{};
        in.next = 1; in.dst = 2; in.flags = flags;
        in.visited = {}; in.inner = {}; in.mac = {};
        return pack_data(in, out);
    };
    std::array<uint8_t, 24> base{}, ack_req{}, is_ack{}, prio{}, m{};
    CHECK(pack_flags(0, base) == 18);
    CHECK(pack_flags(DATA_FLAG_E2E_ACK_REQ,    ack_req) == 18);
    CHECK(pack_flags(DATA_FLAG_E2E_IS_ACK,     is_ack)  == 18);
    CHECK(pack_flags(DATA_FLAG_PRIORITY,       prio)    == 18);
    CHECK(pack_flags(DATA_FLAG_PAYLOAD_TYPE_M, m)       == 18);
    CHECK(base[1] == 0x00);
    CHECK((base[1] ^ ack_req[1]) == 0x80);   // E2E_ACK_REQ    -> bit 7
    CHECK((base[1] ^ is_ack[1])  == 0x40);   // E2E_IS_ACK     -> bit 6
    CHECK((base[1] ^ prio[1])    == 0x20);   // PRIORITY       -> bit 5
    CHECK((base[1] ^ m[1])       == 0x10);   // PAYLOAD_TYPE_M -> bit 4
}

TEST_CASE("DATA — reject: wrong cmd / <18 / addr_len!=0 / bad span sizes; 18-B min accepts") {
    const uint8_t vis[] = {1, 2, 3, 4, 5, 6};
    const uint8_t inner[] = {0x00, 0x07, 0xAA};
    const uint8_t mac[] = {0, 0, 0, 0};
    data_in in{};
    in.next = 0x0B; in.dst = 0x0C; in.hops_remaining = 5;
    in.ctr = 0x1234; in.visited = vis; in.inner = inner; in.mac = mac;
    std::array<uint8_t, 32> buf{};
    size_t n = pack_data(in, buf);
    CHECK(n == 21);   // 14 + 3 + 4

    std::array<uint8_t, 21> w2{};
    for (int i = 0; i < 21; ++i) w2[i] = buf[i];
    w2[0] = wire::cmd_byte(wire::Cmd::B, 0);
    CHECK_FALSE(parse_data(w2).has_value());                                   // wrong cmd
    CHECK_FALSE(parse_data(std::span<const uint8_t>(buf.data(), 17)).has_value()); // < 18

    std::array<uint8_t, 21> al{};
    for (int i = 0; i < 21; ++i) al[i] = buf[i];
    al[0] = static_cast<uint8_t>(al[0] | 0x02);   // addr_len=1 in byte0 bits 3..1
    CHECK_FALSE(parse_data(al).has_value());                                   // addr_len != 0
    std::array<uint8_t, 21> al7{};
    for (int i = 0; i < 21; ++i) al7[i] = buf[i];
    al7[0] = static_cast<uint8_t>(wire::cmd_byte(wire::Cmd::D, 0) | (0x07 << 1));  // addr_len=7
    CHECK_FALSE(parse_data(al7).has_value());                                  // 3-bit addr_len field
    std::array<uint8_t, 21> rsv{};
    for (int i = 0; i < 21; ++i) rsv[i] = buf[i];
    rsv[0] = static_cast<uint8_t>(rsv[0] | 0x01);   // byte0 bit 0 is rsv
    auto ro = parse_data(rsv);
    CHECK(ro.has_value());                                                     // rsv bit 0 ignored on parse
    if (ro) CHECK(ro->addr_len == 0);

    // 18-byte minimal (empty inner) parses — DATA has no inner length prefix
    data_in mn{}; mn.next = 1; mn.dst = 2; mn.visited = {}; mn.inner = {}; mn.mac = {};
    std::array<uint8_t, 18> mbuf{};
    CHECK(pack_data(mn, mbuf) == 18);
    auto mo = parse_data(mbuf);
    CHECK(mo.has_value());
    if (mo) { CHECK(mo->inner_len == 0); CHECK(data_inner(mbuf, *mo).empty()); }

    data_in bad_al = in; bad_al.addr_len = 1;
    CHECK(pack_data(bad_al, buf) == 0);                                        // addr_len != 0
    std::array<uint8_t, 5> vis5{};
    data_in bad_vis = in; bad_vis.visited = vis5;
    CHECK(pack_data(bad_vis, buf) == 0);                                       // visited size 5 != 6
    std::array<uint8_t, 3> mac3{};
    data_in bad_mac = in; bad_mac.mac = mac3;
    CHECK(pack_data(bad_mac, buf) == 0);                                       // mac size 3 != 4
}

TEST_CASE("DATA — endianness guard (ctr LE, channel_msg_id BE)") {
    data_in in{};
    in.next = 1; in.dst = 2; in.ctr = 0xBEEF;
    in.visited = {}; in.inner = {}; in.mac = {};
    std::array<uint8_t, 24> buf{};
    size_t n = pack_data(in, buf);
    CHECK(n == 18);
    CHECK(buf[6] == 0xEF);   // ctr LE: low byte first
    CHECK(buf[7] == 0xBE);
    const uint8_t m_inner[] = {0x12, 0x34, 0x56, 0x78, 0x00, 0x00};
    auto m = parse_m_inner(m_inner);
    CHECK(m.has_value());
    if (m) CHECK(m->channel_msg_id == 0x12345678);   // MSB-first (BE)
}

TEST_CASE("DATA — inner helpers: reject malformed + accept minimum (empty body)") {
    const uint8_t too_short_uni[] = {0x00};         // < 2
    CHECK_FALSE(parse_unicast_inner(too_short_uni).has_value());
    const uint8_t bad_srcaddr[] = {0x01, 0x07};     // src_addr_len != 0
    CHECK_FALSE(parse_unicast_inner(bad_srcaddr).has_value());
    const uint8_t too_short_m[] = {0x01, 0x02, 0x03, 0x04, 0x05};   // < 6
    CHECK_FALSE(parse_m_inner(too_short_m).has_value());

    // minimum-accept boundaries: empty body
    const uint8_t min_uni[] = {0x00, 0x07};         // src_addr_len=0, origin=7, body empty
    auto u = parse_unicast_inner(min_uni);
    CHECK(u.has_value());
    if (u) { CHECK(u->origin == 7); CHECK(u->body.size() == 0); }
    const uint8_t min_m[] = {0x00, 0x00, 0x00, 0x01, 0x02, 0x03};   // msgid BE | chan | flavor, body empty
    auto m = parse_m_inner(min_m);
    CHECK(m.has_value());
    if (m) { CHECK(m->channel_msg_id == 0x00000001); CHECK(m->channel_id == 0x02);
             CHECK(m->flavor == 0x03); CHECK(m->body.size() == 0); }
}

TEST_CASE("DATA — default hops_remaining is 31 (no TTL enforcement, Lua 'or 31')") {
    data_in in{};   // default-constructed: hops_remaining must default to 31, not 0
    in.next = 1; in.dst = 2; in.visited = {}; in.inner = {}; in.mac = {};
    std::array<uint8_t, 18> buf{};
    CHECK(pack_data(in, buf) == 18);
    CHECK(buf[4] == 0xF8);   // hops_remaining 31 (<<3) | committed 0 — NOT 0x00 (TTL exhausted)
    auto o = parse_data(buf);
    CHECK(o.has_value());
    if (o) { CHECK(o->hops_remaining == 31); CHECK(o->committed_hops == 0); }
}
